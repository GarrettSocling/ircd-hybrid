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

/*! \file m_kline.c
 * \brief Includes required functions for processing the KLINE command.
 * \version $Id$
 */

#include "stdinc.h"
#include "list.h"
#include "client.h"
#include "irc_string.h"
#include "ircd.h"
#include "conf.h"
#include "conf_cluster.h"
#include "conf_shared.h"
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
kline_check(const struct AddressRec *arec)
{
  dlink_node *node = NULL, *node_next = NULL;

  DLINK_FOREACH_SAFE(node, node_next, local_client_list.head)
  {
    struct Client *client_p = node->data;

    if (IsDead(client_p))
      continue;

    if (match(arec->username, client_p->username))
      continue;

    switch (arec->masktype)
    {
      case HM_IPV4:
        if (client_p->connection->aftype == AF_INET)
          if (match_ipv4(&client_p->connection->ip, &arec->Mask.ipa.addr, arec->Mask.ipa.bits))
            conf_try_ban(client_p, CLIENT_BAN_KLINE, arec->conf->reason);
        break;
      case HM_IPV6:
        if (client_p->connection->aftype == AF_INET6)
          if (match_ipv6(&client_p->connection->ip, &arec->Mask.ipa.addr, arec->Mask.ipa.bits))
            conf_try_ban(client_p, CLIENT_BAN_KLINE, arec->conf->reason);
        break;
      default:  /* HM_HOST */
        if (!match(arec->Mask.hostname, client_p->host) || !match(arec->Mask.hostname, client_p->sockhost))
          conf_try_ban(client_p, CLIENT_BAN_KLINE, arec->conf->reason);
        break;
    }
  }
}

/* apply_tkline()
 *
 * inputs       -
 * output       - NONE
 * side effects - tkline as given is placed
 */
static void
kline_handle(struct Client *source_p, const char *user, const char *host,
             const char *reason, uintmax_t duration)
{
  char buf[IRCD_BUFSIZE];
  int bits = 0, aftype = 0;
  struct irc_ssaddr iphost, *piphost = NULL;
  struct MaskItem *conf = NULL;

  if (!HasFlag(source_p, FLAGS_SERVICE) && !valid_wild_card(2, user, host))
  {
    sendto_one_notice(source_p, &me,
                      ":Please include at least %u non-wildcard characters with the mask",
                      ConfigGeneral.min_nonwildcard);
    return;
  }

  switch (parse_netmask(host, &iphost, &bits))
  {
    case HM_IPV4:
      if (!HasFlag(source_p, FLAGS_SERVICE) && (unsigned int)bits < ConfigGeneral.kline_min_cidr)
      {
        if (IsClient(source_p))
          sendto_one_notice(source_p, &me, ":For safety, bitmasks less than %u require conf access.",
                            ConfigGeneral.kline_min_cidr);
        return;
      }

      aftype = AF_INET;
      piphost = &iphost;
      break;
    case HM_IPV6:
      if (!HasFlag(source_p, FLAGS_SERVICE) && (unsigned int)bits < ConfigGeneral.kline_min_cidr6)
      {
        if (IsClient(source_p))
          sendto_one_notice(source_p, &me, ":For safety, bitmasks less than %u require conf access.",
                            ConfigGeneral.kline_min_cidr6);
        return;
      }

      aftype = AF_INET6;
      piphost = &iphost;
      break;
    default:  /* HM_HOST */
      break;
  }

  if ((conf = find_conf_by_address(host, piphost, CONF_KLINE, aftype, user, NULL, 0)))
  {
    if (IsClient(source_p))
      sendto_one_notice(source_p, &me, ":[%s@%s] already K-Lined by [%s@%s] - %s",
                        user, host, conf->user, conf->host, conf->reason);
    return;
  }

  if (duration)
    snprintf(buf, sizeof(buf), "Temporary K-line %ju min. - %.*s (%s)",
             duration / 60, REASONLEN, reason, date_iso8601(0));
  else
    snprintf(buf, sizeof(buf), "%.*s (%s)", REASONLEN, reason, date_iso8601(0));

  conf = conf_make(CONF_KLINE);
  conf->host = xstrdup(host);
  conf->user = xstrdup(user);
  conf->setat = CurrentTime;
  conf->reason = xstrdup(buf);
  SetConfDatabase(conf);

  if (duration)
  {
    conf->until = CurrentTime + duration;

    if (IsClient(source_p))
      sendto_one_notice(source_p, &me, ":Added temporary %ju min. K-Line [%s@%s]",
                        duration / 60, conf->user, conf->host);

    sendto_realops_flags(UMODE_SERVNOTICE, L_ALL, SEND_NOTICE,
                         "%s added temporary %ju min. K-Line for [%s@%s] [%s]",
                         get_oper_name(source_p), duration / 60,
                         conf->user, conf->host,
                         conf->reason);
    ilog(LOG_TYPE_KLINE, "%s added temporary %ju min. K-Line for [%s@%s] [%s]",
         get_oper_name(source_p), duration / 60,
         conf->user, conf->host, conf->reason);
  }
  else
  {
    if (IsClient(source_p))
      sendto_one_notice(source_p, &me, ":Added K-Line [%s@%s]",
                        conf->user, conf->host);

    sendto_realops_flags(UMODE_SERVNOTICE, L_ALL, SEND_NOTICE,
                         "%s added K-Line for [%s@%s] [%s]",
                         get_oper_name(source_p),
                         conf->user, conf->host, conf->reason);
    ilog(LOG_TYPE_KLINE, "%s added K-Line for [%s@%s] [%s]",
         get_oper_name(source_p), conf->user, conf->host, conf->reason);
  }

  kline_check(add_conf_by_address(CONF_KLINE, conf));
}

/* mo_kline()
 *
 * inputs	- pointer to server
 *		- pointer to client
 *		- parameter count
 *		- parameter list
 * output	-
 * side effects - k line is added
 */
static int
mo_kline(struct Client *source_p, int parc, char *parv[])
{
  char *reason = NULL;
  char *user = NULL;
  char *host = NULL;
  char *target_server = NULL;
  uintmax_t duration = 0;

  if (!HasOFlag(source_p, OPER_FLAG_KLINE))
  {
    sendto_one_numeric(source_p, &me, ERR_NOPRIVS, "kline");
    return 0;
  }

  if (!parse_aline("KLINE", source_p, parc, parv, &user, &host,
                   &duration, &target_server, &reason))
    return 0;

  if (target_server)
  {
    sendto_match_servs(source_p, target_server, CAPAB_KLN, "KLINE %s %ju %s %s :%s",
                       target_server, duration,
                       user, host, reason);

    /* Allow ON to apply local kline as well if it matches */
    if (match(target_server, me.name))
      return 0;
  }
  else
    cluster_distribute(source_p, "KLINE", CAPAB_KLN, CLUSTER_KLINE,
                       "%ju %s %s :%s", duration, user, host, reason);

  kline_handle(source_p, user, host, reason, duration);
  return 0;
}

/*! \brief KLINE command handler
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
 *      - parv[3] = user mask
 *      - parv[4] = host mask
 *      - parv[5] = reason
 */
static int
ms_kline(struct Client *source_p, int parc, char *parv[])
{
  const char *user, *host, *reason;

  if (parc != 6 || EmptyString(parv[5]))
    return 0;

  sendto_match_servs(source_p, parv[1], CAPAB_KLN, "KLINE %s %s %s %s :%s",
                     parv[1], parv[2], parv[3], parv[4], parv[5]);

  if (match(parv[1], me.name))
    return 0;

  user = parv[3];
  host = parv[4];
  reason = parv[5];

  if (HasFlag(source_p, FLAGS_SERVICE) ||
      shared_find(SHARED_KLINE, source_p->servptr->name,
                  source_p->username, source_p->host))
    kline_handle(source_p, user, host, reason, strtoumax(parv[2], NULL, 10));

  return 0;
}

static struct Message kline_msgtab =
{
  .cmd = "KLINE",
  .args_min = 2,
  .args_max = MAXPARA,
  .handlers[UNREGISTERED_HANDLER] = m_unregistered,
  .handlers[CLIENT_HANDLER] = m_not_oper,
  .handlers[SERVER_HANDLER] = ms_kline,
  .handlers[ENCAP_HANDLER] = m_ignore,
  .handlers[OPER_HANDLER] = mo_kline
};

static void
module_init(void)
{
  mod_add_cmd(&kline_msgtab);
  add_capability("KLN", CAPAB_KLN);
}

static void
module_exit(void)
{
  mod_del_cmd(&kline_msgtab);
  delete_capability("KLN");
}

struct module module_entry =
{
  .version = "$Revision$",
  .modinit = module_init,
  .modexit = module_exit,
};
