/*
 *  ircd-hybrid: an advanced, lightweight Internet Relay Chat Daemon (ircd)
 *
 *  Copyright (c) 2003-2016 ircd-hybrid development team
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

/*! \file m_xline.c
 * \brief Includes required functions for processing the XLINE command.
 * \version $Id$
 */

#include "stdinc.h"
#include "list.h"
#include "client.h"
#include "irc_string.h"
#include "ircd.h"
#include "conf.h"
#include "conf_cluster.h"
#include "conf_gecos.h"
#include "conf_shared.h"
#include "numeric.h"
#include "log.h"
#include "misc.h"
#include "send.h"
#include "server.h"
#include "parse.h"
#include "modules.h"
#include "memory.h"


static void
xline_check(const struct GecosItem *gecos)
{
  dlink_node *node = NULL, *node_next = NULL;

  DLINK_FOREACH_SAFE(node, node_next, local_client_list.head)
  {
    struct Client *client_p = node->data;

    if (IsDead(client_p))
      continue;

    if (!match(gecos->mask, client_p->info))
      conf_try_ban(client_p, CLIENT_BAN_XLINE, gecos->reason);
  }
}

/* xline_handle()
 *
 * inputs       - client taking credit for xline, gecos, reason, xline type
 * outputs      - none
 * side effects - when successful, adds an xline to the conf
 */
static void
xline_handle(struct Client *source_p, const char *mask, const char *reason, uintmax_t duration)
{
  char buf[IRCD_BUFSIZE];
  struct GecosItem *gecos = NULL;

  if (!HasFlag(source_p, FLAGS_SERVICE))
  {
    if (!valid_wild_card_simple(mask))
    {
      if (IsClient(source_p))
        sendto_one_notice(source_p, &me, ":Please include at least %u non-wildcard characters with the xline",
                          ConfigGeneral.min_nonwildcard_simple);
      return;
    }
  }

  if ((gecos = gecos_find(mask, match)))
  {
    if (IsClient(source_p))
      sendto_one_notice(source_p, &me, ":[%s] already X-Lined by [%s] - %s",
                        mask, gecos->mask, gecos->reason);
    return;
  }

  if (duration)
    snprintf(buf, sizeof(buf), "Temporary X-line %ju min. - %.*s (%s)",
             duration / 60, REASONLEN, reason, date_iso8601(0));
  else
    snprintf(buf, sizeof(buf), "%.*s (%s)", REASONLEN, reason, date_iso8601(0));

  gecos = gecos_make();
  gecos->mask = xstrdup(mask);
  gecos->reason = xstrdup(buf);
  gecos->setat = CurrentTime;
  gecos->in_database = 1;

  if (duration)
  {
    gecos->expire = CurrentTime + duration;

    if (IsClient(source_p))
      sendto_one_notice(source_p, &me, ":Added temporary %ju min. X-Line [%s]",
                        duration / 60, gecos->mask);

    sendto_realops_flags(UMODE_SERVNOTICE, L_ALL, SEND_NOTICE,
                         "%s added temporary %ju min. X-Line for [%s] [%s]",
                         get_oper_name(source_p), duration / 60,
                         gecos->mask, gecos->reason);
    ilog(LOG_TYPE_XLINE, "%s added temporary %ju min. X-Line for [%s] [%s]",
         get_oper_name(source_p), duration / 60, gecos->mask, gecos->reason);
  }
  else
  {
    if (IsClient(source_p))
      sendto_one_notice(source_p, &me, ":Added X-Line [%s] [%s]",
                        gecos->mask, gecos->reason);

    sendto_realops_flags(UMODE_SERVNOTICE, L_ALL, SEND_NOTICE,
                         "%s added X-Line for [%s] [%s]",
                         get_oper_name(source_p), gecos->mask,
                         gecos->reason);
    ilog(LOG_TYPE_XLINE, "%s added X-Line for [%s] [%s]",
         get_oper_name(source_p), gecos->mask, gecos->reason);
  }

  xline_check(gecos);
}

/* mo_xline()
 *
 * inputs	- pointer to server
 *		- pointer to client
 *		- parameter count
 *		- parameter list
 * output	-
 * side effects - x line is added
 *
 */
static int
mo_xline(struct Client *source_p, int parc, char *parv[])
{
  char *reason = NULL;
  char *mask = NULL;
  char *target_server = NULL;
  uintmax_t duration = 0;

  if (!HasOFlag(source_p, OPER_FLAG_XLINE))
  {
    sendto_one_numeric(source_p, &me, ERR_NOPRIVS, "xline");
    return 0;
  }

  if (!parse_aline("XLINE", source_p, parc, parv, &mask, NULL,
                   &duration, &target_server, &reason))
    return 0;

  if (target_server)
  {
    sendto_match_servs(source_p, target_server, CAPAB_CLUSTER, "XLINE %s %s %ju :%s",
                       target_server, mask, duration, reason);

    /* Allow ON to apply local xline as well if it matches */
    if (match(target_server, me.name))
      return 0;
  }
  else
    cluster_distribute(source_p, "XLINE", CAPAB_CLUSTER, CLUSTER_XLINE, "%s %ju :%s",
                       mask, duration, reason);

  xline_handle(source_p, mask, reason, duration);
  return 0;
}

/*! \brief XLINE command handler
 *
 * \param source_p Pointer to allocated Client struct from which the message
 *                 originally comes from.  This can be a local or remote client.
 * \param parc     Integer holding the number of supplied arguments.
 * \param parv     Argument vector where parv[0] .. parv[parc-1] are non-NULL
 *                 pointers.
 * \note Valid arguments for this command are:
 *      - parv[0] = command
 *      - parv[1] = target server mask
 *      - parv[2] = gecos
 *      - parv[3] = duration in seconds
 *      - parv[4] = reason
 */
static int
ms_xline(struct Client *source_p, int parc, char *parv[])
{
  if (parc != 5 || EmptyString(parv[4]))
    return 0;

  sendto_match_servs(source_p, parv[1], CAPAB_CLUSTER, "XLINE %s %s %s :%s",
                     parv[1], parv[2], parv[3], parv[4]);

  if (match(parv[1], me.name))
    return 0;

  if (HasFlag(source_p, FLAGS_SERVICE) ||
      shared_find(SHARED_XLINE, source_p->servptr->name,
                  source_p->username, source_p->host))
    xline_handle(source_p, parv[2], parv[4], strtoumax(parv[3], NULL, 10));

  return 0;
}

static struct Message xline_msgtab =
{
  .cmd = "XLINE",
  .args_min = 2,
  .args_max = MAXPARA,
  .handlers[UNREGISTERED_HANDLER] = m_unregistered,
  .handlers[CLIENT_HANDLER] = m_not_oper,
  .handlers[SERVER_HANDLER] = ms_xline,
  .handlers[ENCAP_HANDLER] = m_ignore,
  .handlers[OPER_HANDLER] = mo_xline
};

static void
module_init(void)
{
  mod_add_cmd(&xline_msgtab);
}

static void
module_exit(void)
{
  mod_del_cmd(&xline_msgtab);
}

struct module module_entry =
{
  .version = "$Revision$",
  .modinit = module_init,
  .modexit = module_exit,
};
