/*
 *  ircd-hybrid: an advanced, lightweight Internet Relay Chat Daemon (ircd)
 *
 *  Copyright (c) 2001-2017 ircd-hybrid development team
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

/*! \file m_unresv.c
 * \brief Includes required functions for processing the UNRESV command.
 * \version $Id$
 */

#include "stdinc.h"
#include "list.h"
#include "client.h"
#include "irc_string.h"
#include "ircd.h"
#include "conf.h"
#include "conf_cluster.h"
#include "conf_resv.h"
#include "conf_shared.h"
#include "numeric.h"
#include "log.h"
#include "send.h"
#include "server.h"
#include "parse.h"
#include "modules.h"
#include "memory.h"


static void
resv_remove(struct Client *source_p, const char *mask)
{
  struct ResvItem *resv = NULL;

  if ((resv = resv_find(mask, irccmp)) == NULL)
  {
    if (IsClient(source_p))
      sendto_one_notice(source_p, &me, ":No RESV for %s", mask);

    return;
  }

  if (!resv->in_database)
  {
    if (IsClient(source_p))
      sendto_one_notice(source_p, &me, ":The RESV for %s is in ircd.conf and must be removed by hand",
                        resv->mask);
    return;
  }

  resv_delete(resv);

  if (IsClient(source_p))
    sendto_one_notice(source_p, &me, ":RESV for [%s] is removed", mask);

  sendto_realops_flags(UMODE_SERVNOTICE, L_ALL, SEND_NOTICE,
                       "%s has removed the RESV for: [%s]",
                       get_oper_name(source_p), mask);
  ilog(LOG_TYPE_RESV, "%s removed RESV for [%s]",
       get_oper_name(source_p), mask);
}

/*! \brief UNRESV command handler
 *
 * \param source_p Pointer to allocated Client struct from which the message
 *                 originally comes from.  This can be a local or remote client.
 * \param parc     Integer holding the number of supplied arguments.
 * \param parv     Argument vector where parv[0] .. parv[parc-1] are non-NULL
 *                 pointers.
 * \note Valid arguments for this command are:
 *      - parv[0] = command
 *      - parv[1] = channel/nick
 *      - parv[2] = "ON"
 *      - parv[3] = target server
 */
static int
mo_unresv(struct Client *source_p, int parc, char *parv[])
{
  char *mask = NULL;
  char *target_server = NULL;

  if (!HasOFlag(source_p, OPER_FLAG_UNRESV))
  {
    sendto_one_numeric(source_p, &me, ERR_NOPRIVS, "unresv");
    return 0;
  }

  if (!parse_aline("UNRESV", source_p, parc, parv, &mask, NULL,
                   NULL, &target_server, NULL))
    return 0;

  if (target_server)
  {
    sendto_match_servs(source_p, target_server, CAPAB_CLUSTER, "UNRESV %s %s",
                       target_server, mask);

    /* Allow ON to apply local unresv as well if it matches */
    if (match(target_server, me.name))
      return 0;
  }
  else
    cluster_distribute(source_p, "UNRESV", CAPAB_KLN, CLUSTER_UNRESV, mask);

  resv_remove(source_p, mask);
  return 0;
}

/*! \brief UNRESV command handler
 *
 * \param source_p Pointer to allocated Client struct from which the message
 *                 originally comes from.  This can be a local or remote client.
 * \param parc     Integer holding the number of supplied arguments.
 * \param parv     Argument vector where parv[0] .. parv[parc-1] are non-NULL
 *                 pointers.
 * \note Valid arguments for this command are:
 *      - parv[0] = command
 *      - parv[1] = target server mask
 *      - parv[2] = name mask
 */
static int
ms_unresv(struct Client *source_p, int parc, char *parv[])
{
  if (parc != 3 || EmptyString(parv[2]))
    return 0;

  sendto_match_servs(source_p, parv[1], CAPAB_CLUSTER, "UNRESV %s %s",
                     parv[1], parv[2]);

  if (match(parv[1], me.name))
    return 0;

  if (HasFlag(source_p, FLAGS_SERVICE) ||
      shared_find(SHARED_UNRESV, source_p->servptr->name,
                  source_p->username, source_p->host))
    resv_remove(source_p, parv[2]);

  return 0;
}

static struct Message unresv_msgtab =
{
  .cmd = "UNRESV",
  .args_min = 2,
  .args_max = MAXPARA,
  .handlers[UNREGISTERED_HANDLER] = m_unregistered,
  .handlers[CLIENT_HANDLER] = m_not_oper,
  .handlers[SERVER_HANDLER] = ms_unresv,
  .handlers[ENCAP_HANDLER] = m_ignore,
  .handlers[OPER_HANDLER] = mo_unresv
};

static void
module_init(void)
{
  mod_add_cmd(&unresv_msgtab);
}

static void
module_exit(void)
{
  mod_del_cmd(&unresv_msgtab);
}

struct module module_entry =
{
  .version = "$Revision$",
  .modinit = module_init,
  .modexit = module_exit,
};
