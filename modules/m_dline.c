/*
 *  ircd-hybrid: an advanced, lightweight Internet Relay Chat Daemon (ircd)
 *
 *  Copyright (c) 1997-2017 ircd-hybrid development team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 *  USA
 */

/*! \file m_dline.c
 * \brief Includes required functions for processing the DLINE command.
 * \version $Id$
 */

#include "stdinc.h"
#include "list.h"
#include "client.h"
#include "irc_string.h"
#include "conf.h"
#include "conf_cluster.h"
#include "conf_shared.h"
#include "ircd.h"
#include "hostmask.h"
#include "numeric.h"
#include "log.h"
#include "misc.h"
#include "send.h"
#include "server.h"
#include "parse.h"
#include "modules.h"
#include "memory.h"


static void
dline_check(const struct AddressRec *arec)
{
  dlink_node *node = NULL, *node_next = NULL;

  DLINK_FOREACH_SAFE(node, node_next, local_client_list.head)
  {
    struct Client *client_p = node->data;

    if (IsDead(client_p))
      continue;

    switch (arec->masktype)
    {
      case HM_IPV4:
        if (client_p->connection->aftype == AF_INET)
          if (match_ipv4(&client_p->connection->ip, &arec->Mask.ipa.addr, arec->Mask.ipa.bits))
            conf_try_ban(client_p, CLIENT_BAN_DLINE, arec->conf->reason);
        break;
      case HM_IPV6:
        if (client_p->connection->aftype == AF_INET6)
          if (match_ipv6(&client_p->connection->ip, &arec->Mask.ipa.addr, arec->Mask.ipa.bits))
            conf_try_ban(client_p, CLIENT_BAN_DLINE, arec->conf->reason);
        break;
      default: break;
    }
  }

  DLINK_FOREACH_SAFE(node, node_next, unknown_list.head)
  {
    struct Client *client_p = node->data;

    if (IsDead(client_p))
      continue;

    switch (arec->masktype)
    {
      case HM_IPV4:
        if (client_p->connection->aftype == AF_INET)
          if (match_ipv4(&client_p->connection->ip, &arec->Mask.ipa.addr, arec->Mask.ipa.bits))
            conf_try_ban(client_p, CLIENT_BAN_DLINE, arec->conf->reason);
        break;
      case HM_IPV6:
        if (client_p->connection->aftype == AF_INET6)
          if (match_ipv6(&client_p->connection->ip, &arec->Mask.ipa.addr, arec->Mask.ipa.bits))
            conf_try_ban(client_p, CLIENT_BAN_DLINE, arec->conf->reason);
        break;
      default: break;
    }
  }
}

/* dline_add()
 *
 * inputs	-
 * output	- NONE
 * side effects	- dline as given is placed
 */
static void
dline_handle(struct Client *source_p, const char *addr, const char *reason, uintmax_t duration)
{
  char buf[IRCD_BUFSIZE];
  struct irc_ssaddr daddr;
  int bits = 0, aftype = 0;
  struct MaskItem *conf = NULL;

  if (!HasFlag(source_p, FLAGS_SERVICE) && !valid_wild_card(1, addr))
  {
    sendto_one_notice(source_p, &me,
                      ":Please include at least %u non-wildcard characters with the mask",
                      ConfigGeneral.min_nonwildcard);
    return;
  }

  switch (parse_netmask(addr, &daddr, &bits))
  {
    case HM_IPV4:
      if (!HasFlag(source_p, FLAGS_SERVICE) && (unsigned int)bits < ConfigGeneral.dline_min_cidr)
      {
        if (IsClient(source_p))
          sendto_one_notice(source_p, &me, ":For safety, bitmasks less than %u require conf access.",
                            ConfigGeneral.dline_min_cidr);
        return;
      }

      aftype = AF_INET;
      break;
    case HM_IPV6:
      if (!HasFlag(source_p, FLAGS_SERVICE) && (unsigned int)bits < ConfigGeneral.dline_min_cidr6)
      {
        if (IsClient(source_p))
          sendto_one_notice(source_p, &me, ":For safety, bitmasks less than %u require conf access.",
                            ConfigGeneral.dline_min_cidr6);
        return;
      }

      aftype = AF_INET6;
      break;
    default:  /* HM_HOST */
     return;
  }

  if ((conf = find_conf_by_address(NULL, &daddr, CONF_DLINE, aftype, NULL, NULL, 1)))
  {
    if (IsClient(source_p))
      sendto_one_notice(source_p, &me, ":[%s] already D-lined by [%s] - %s",
                        addr, conf->host, conf->reason);
    return;
  }

  if (duration)
    snprintf(buf, sizeof(buf), "Temporary D-line %ju min. - %.*s (%s)",
             duration / 60, REASONLEN, reason, date_iso8601(0));
  else
    snprintf(buf, sizeof(buf), "%.*s (%s)", REASONLEN, reason, date_iso8601(0));

  conf = conf_make(CONF_DLINE);
  conf->host = xstrdup(addr);
  conf->reason = xstrdup(buf);
  conf->setat = CurrentTime;
  SetConfDatabase(conf);

  if (duration)
  {
    conf->until = CurrentTime + duration;

    if (IsClient(source_p))
      sendto_one_notice(source_p, &me, ":Added temporary %ju min. D-Line [%s]",
                        duration / 60, conf->host);

    sendto_realops_flags(UMODE_SERVNOTICE, L_ALL, SEND_NOTICE,
                         "%s added temporary %ju min. D-Line for [%s] [%s]",
                         get_oper_name(source_p), duration / 60,
                         conf->host, conf->reason);
    ilog(LOG_TYPE_DLINE, "%s added temporary %ju min. D-Line for [%s] [%s]",
         get_oper_name(source_p), duration / 60, conf->host, conf->reason);
  }
  else
  {
    if (IsClient(source_p))
      sendto_one_notice(source_p, &me, ":Added D-Line [%s]", conf->host);

    sendto_realops_flags(UMODE_SERVNOTICE, L_ALL, SEND_NOTICE,
                         "%s added D-Line for [%s] [%s]",
                         get_oper_name(source_p), conf->host, conf->reason);
    ilog(LOG_TYPE_DLINE, "%s added D-Line for [%s] [%s]",
         get_oper_name(source_p), conf->host, conf->reason);
  }

  dline_check(add_conf_by_address(CONF_DLINE, conf));
}

/* mo_dline()
 *
 * inputs	- pointer to server
 *		- pointer to client
 *		- parameter count
 *		- parameter list
 * output	-
 * side effects - D line is added
 *
 */
static int
mo_dline(struct Client *source_p, int parc, char *parv[])
{
  char *dlhost = NULL, *reason = NULL;
  char *target_server = NULL;
  const struct Client *target_p = NULL;
  uintmax_t duration = 0;
  int t = 0;
  char hostip[HOSTIPLEN + 1];

  if (!HasOFlag(source_p, OPER_FLAG_DLINE))
  {
    sendto_one_numeric(source_p, &me, ERR_NOPRIVS, "dline");
    return 0;
  }

  if (!parse_aline("DLINE", source_p, parc, parv, &dlhost,
                   NULL, &duration, &target_server, &reason))
    return 0;

  if (target_server)
  {
    sendto_match_servs(source_p, target_server, CAPAB_DLN, "DLINE %s %ju %s :%s",
                       target_server, duration,
                       dlhost, reason);

    /* Allow ON to apply local dline as well if it matches */
    if (match(target_server, me.name))
      return 0;
  }
  else
    cluster_distribute(source_p, "DLINE", CAPAB_DLN, CLUSTER_DLINE,
                       "%ju %s :%s", duration, dlhost, reason);

  if ((t = parse_netmask(dlhost, NULL, NULL)) == HM_HOST)
  {
    if ((target_p = find_chasing(source_p, dlhost)) == NULL)
      return 0;  /* find_chasing sends ERR_NOSUCHNICK */

    if (!MyConnect(target_p))
    {
      sendto_one_notice(source_p, &me, ":Cannot DLINE nick on another server");
      return 0;
    }

    if (HasFlag(target_p, FLAGS_EXEMPTKLINE))
    {
      sendto_one_notice(source_p, &me, ":%s is E-lined", target_p->name);
      return 0;
    }

    getnameinfo((const struct sockaddr *)&target_p->connection->ip,
                target_p->connection->ip.ss_len, hostip,
                sizeof(hostip), NULL, 0, NI_NUMERICHOST);
    dlhost = hostip;
  }

  dline_handle(source_p, dlhost, reason, duration);
  return 0;
}

/*! \brief DLINE command handler
 *
 * \param source_p Pointer to allocated Client struct from which the message
 *                 originally comes from.  This can be a local or remote client.
 * \param parc     Integer holding the number of supplied arguments.
 * \param parv     Argument vector where parv[0] .. parv[parc-1] are non-NULL
 *                 pointers.
 * \note Valid arguments for this command are:
 *      - parv[0] = command
 *      - parv[1] = target server mask
 *      - parv[2] = duration in seconds
 *      - parv[3] = IP address
 *      - parv[4] = reason
 */
static int
ms_dline(struct Client *source_p, int parc, char *parv[])
{
  const char *dlhost, *reason;

  if (parc != 5 || EmptyString(parv[4]))
    return 0;

  sendto_match_servs(source_p, parv[1], CAPAB_DLN, "DLINE %s %s %s :%s",
                     parv[1], parv[2], parv[3], parv[4]);

  if (match(parv[1], me.name))
    return 0;

  dlhost = parv[3];
  reason = parv[4];

  if (HasFlag(source_p, FLAGS_SERVICE) ||
      shared_find(SHARED_DLINE, source_p->servptr->name,
                  source_p->username, source_p->host))
    dline_handle(source_p, dlhost, reason, strtoumax(parv[2], NULL, 10));

  return 0;
}

static struct Message dline_msgtab =
{
  .cmd = "DLINE",
  .args_min = 2,
  .args_max = MAXPARA,
  .handlers[UNREGISTERED_HANDLER] = m_unregistered,
  .handlers[CLIENT_HANDLER] = m_not_oper,
  .handlers[SERVER_HANDLER] = ms_dline,
  .handlers[ENCAP_HANDLER] = m_ignore,
  .handlers[OPER_HANDLER] = mo_dline
};

static void
module_init(void)
{
  mod_add_cmd(&dline_msgtab);
  add_capability("DLN", CAPAB_DLN);
}

static void
module_exit(void)
{
  mod_del_cmd(&dline_msgtab);
  delete_capability("DLN");
}

struct module module_entry =
{
  .version = "$Revision$",
  .modinit = module_init,
  .modexit = module_exit,
};
