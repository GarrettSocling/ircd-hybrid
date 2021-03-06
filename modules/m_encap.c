/*
 *  ircd-hybrid: an advanced, lightweight Internet Relay Chat Daemon (ircd)
 *
 *  Copyright (c) 2003-2017 ircd-hybrid development team
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

/*! \file m_encap.c
 * \brief Includes required functions for processing the ENCAP command.
 * \version $Id$
 */

#include "stdinc.h"
#include "client.h"
#include "parse.h"
#include "server.h"
#include "send.h"
#include "modules.h"
#include "irc_string.h"


/*
 * ms_encap()
 *
 * inputs	- destination server, subcommand, parameters
 * output	- none
 * side effects	- propagates subcommand to locally connected servers
 */
static int
ms_encap(struct Client *source_p, int parc, char *parv[])
{
  char buffer[IRCD_BUFSIZE] = "", *ptr = buffer;
  unsigned int cur_len = 0, len, i;
#ifdef NOT_USED_YET
  int paramcount, mpara = 0;
#endif
  struct Message *mptr = NULL;

  for (i = 1; i < (unsigned int)parc - 1; ++i)
  {
    len = strlen(parv[i]) + 1;

    if ((cur_len + len) >= sizeof(buffer))
      return 0;

    snprintf(ptr, sizeof(buffer) - cur_len, "%s ", parv[i]);
    cur_len += len;
    ptr += len;
  }

  /* If it's a command without parameters, don't prepend a ':' */
  if (parc == 3)
    snprintf(ptr, sizeof(buffer) - cur_len, "%s", parv[2]);
  else
    snprintf(ptr, sizeof(buffer) - cur_len, ":%s", parv[parc - 1]);

  sendto_match_servs(source_p, parv[1], CAPAB_ENCAP,
                     "ENCAP %s", buffer);

  if (match(parv[1], me.name))
    return 0;

  if ((mptr = find_command(parv[2])) == NULL)
    return 0;

#ifdef NOT_USED_YET
  paramcount = mptr->parameters;
  mpara      = mptr->maxpara;
#endif
  mptr->bytes += strlen(buffer);

  parv += 2;
  parc -= 2;

  if ((unsigned int)parc >= mptr->args_min)
    mptr->handlers[ENCAP_HANDLER](source_p, parc, parv);
  return 0;
}

static struct Message encap_msgtab =
{
  .cmd = "ENCAP",
  .args_min = 3,
  .args_max = MAXPARA,
  .handlers[UNREGISTERED_HANDLER] = m_ignore,
  .handlers[CLIENT_HANDLER] = m_ignore,
  .handlers[SERVER_HANDLER] = ms_encap,
  .handlers[ENCAP_HANDLER] = m_ignore,
  .handlers[OPER_HANDLER] = m_ignore
};

static void
module_init(void)
{
  mod_add_cmd(&encap_msgtab);
  add_capability("ENCAP", CAPAB_ENCAP);
}

static void
module_exit(void)
{
  mod_del_cmd(&encap_msgtab);
  delete_capability("ENCAP");
}

struct module module_entry =
{
  .version = "$Revision$",
  .modinit = module_init,
  .modexit = module_exit,
};
