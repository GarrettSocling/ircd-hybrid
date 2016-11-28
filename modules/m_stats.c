/*
 *  ircd-hybrid: an advanced, lightweight Internet Relay Chat Daemon (ircd)
 *
 *  Copyright (c) 1997-2016 ircd-hybrid development team
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

/*! \file m_stats.c
 * \brief Includes required functions for processing the STATS command.
 * \version $Id$
 */

#include "stdinc.h"
#include "list.h"
#include "client.h"
#include "irc_string.h"
#include "ircd.h"
#include "listener.h"
#include "conf.h"
#include "conf_class.h"
#include "conf_cluster.h"
#include "conf_gecos.h"
#include "conf_resv.h"
#include "conf_service.h"
#include "conf_shared.h"
#include "hostmask.h"
#include "numeric.h"
#include "send.h"
#include "fdlist.h"
#include "misc.h"
#include "server.h"
#include "event.h"
#include "parse.h"
#include "modules.h"
#include "whowas.h"
#include "watch.h"
#include "reslib.h"
#include "motd.h"
#include "ipcache.h"


static void
report_shared(struct Client *source_p)
{
  static const struct shared_types
  {
    unsigned int type;
    unsigned char letter;
  } flag_table[] = {
    { SHARED_KLINE,   'K' },
    { SHARED_UNKLINE, 'U' },
    { SHARED_XLINE,   'X' },
    { SHARED_UNXLINE, 'Y' },
    { SHARED_RESV,    'Q' },
    { SHARED_UNRESV,  'R' },
    { SHARED_LOCOPS,  'L' },
    { SHARED_DLINE,   'D' },
    { SHARED_UNDLINE, 'E' },
    { 0, '\0' }
  };

  dlink_node *node;
  char buf[sizeof(flag_table) / sizeof(struct shared_types) + 1];  /* +1 for 'c' */

  DLINK_FOREACH(node, shared_get_list()->head)
  {
    const struct SharedItem *shared = node->data;
    char *p = buf;

    *p++ = 'c';

    for (const struct shared_types *tab = flag_table; tab->type; ++tab)
      if (tab->type & shared->type)
        *p++ = tab->letter;
      else
        *p++ = ToLower(tab->letter);

    *p = '\0';

    sendto_one_numeric(source_p, &me, RPL_STATSULINE, shared->server,
                       shared->user, shared->host, buf);
  }
}

static void
report_cluster(struct Client *source_p)
{
  static const struct cluster_types
  {
    unsigned int type;
    unsigned char letter;
  } flag_table[] = {
    { CLUSTER_KLINE,   'K' },
    { CLUSTER_UNKLINE, 'U' },
    { CLUSTER_XLINE,   'X' },
    { CLUSTER_UNXLINE, 'Y' },
    { CLUSTER_RESV,    'Q' },
    { CLUSTER_UNRESV,  'R' },
    { CLUSTER_LOCOPS,  'L' },
    { CLUSTER_DLINE,   'D' },
    { CLUSTER_UNDLINE, 'E' },
    { 0, '\0' }
  };

  dlink_node *node;
  char buf[sizeof(flag_table) / sizeof(struct cluster_types) + 1];  /* +1 for 'C' */

  DLINK_FOREACH(node, cluster_get_list()->head)
  {
    const struct ClusterItem *cluster = node->data;
    char *p = buf;

    *p++ = 'C';

    for (const struct cluster_types *tab = flag_table; tab->type; ++tab)
      if (tab->type & cluster->type)
        *p++ = tab->letter;
      else
        *p++ = ToLower(tab->letter);

    *p = '\0';

    sendto_one_numeric(source_p, &me, RPL_STATSULINE, cluster->server,
                       "*", "*", buf);
  }
}

static void
stats_service(struct Client *source_p, int parc, char *parv[])
{
  dlink_node *node;

  DLINK_FOREACH(node, service_get_list()->head)
  {
    const struct ServiceItem *service = node->data;
    sendto_one_numeric(source_p, &me, RPL_STATSSERVICE, 'S', "*", service->name, 0, 0);
  }
}

static void
stats_gecos(struct Client *source_p, int parc, char *parv[])
{
  dlink_node *node;

  DLINK_FOREACH(node, gecos_get_list()->head)
  {
    const struct GecosItem *gecos = node->data;
    sendto_one_numeric(source_p, &me, RPL_STATSXLINE,
                       gecos->expire ? 'x' : 'X',
                       gecos->mask, gecos->reason);
  }
}

/*
 * inputs	- pointer to client requesting confitem report
 *		- ConfType to report
 * output	- none
 * side effects	-
 */
static void
stats_operator(struct Client *source_p, int parc, char *parv[])
{
  dlink_node *node;

  if (!HasUMode(source_p, UMODE_OPER) && ConfigGeneral.stats_o_oper_only)
  {
    sendto_one_numeric(source_p, &me, ERR_NOPRIVILEGES);
    return;
  }

  DLINK_FOREACH(node, operator_items.head)
  {
    const struct MaskItem *conf = node->data;

    /* Don't allow non opers to see oper privs */
    if (HasUMode(source_p, UMODE_OPER))
      sendto_one_numeric(source_p, &me, RPL_STATSOLINE, 'O', conf->user, conf->host,
                         conf->name, oper_privs_as_string(conf->port),
                         conf->class->name);
    else
      sendto_one_numeric(source_p, &me, RPL_STATSOLINE, 'O', conf->user, conf->host,
                         conf->name, "0", conf->class->name);
  }
}

static void
stats_connect(struct Client *source_p, int parc, char *parv[])
{
  dlink_node *node;

  DLINK_FOREACH(node, connect_items.head)
  {
    char buf[8];
    char *p = buf;
    const struct MaskItem *conf = node->data;

    if (IsConfAllowAutoConn(conf))
      *p++ = 'A';
    if (IsConfSSL(conf))
      *p++ = 'S';
    if (p == buf)
      *p++ = '*';

    *p = '\0';

    /*
     * Allow admins to see actual ips unless hide_server_ips is enabled
     */
    if (!ConfigServerHide.hide_server_ips && HasUMode(source_p, UMODE_ADMIN))
      sendto_one_numeric(source_p, &me, RPL_STATSCLINE, 'C', conf->host,
                         buf, conf->name, conf->port,
                         conf->class->name);
    else
      sendto_one_numeric(source_p, &me, RPL_STATSCLINE, 'C',
                         "*@127.0.0.1", buf, conf->name, conf->port,
                         conf->class->name);
  }
}

/* report_resv()
 *
 * inputs       - pointer to client pointer to report to.
 * output       - NONE
 * side effects - report all resvs to client.
 */
static void
stats_resv(struct Client *source_p, int parc, char *parv[])
{
  dlink_node *node;

  DLINK_FOREACH(node, resv_chan_get_list()->head)
  {
    const struct ResvItem *resv = node->data;

    sendto_one_numeric(source_p, &me, RPL_STATSQLINE,
                       resv->expire ? 'q' : 'Q',
                       resv->mask, resv->reason);
  }

  DLINK_FOREACH(node, resv_nick_get_list()->head)
  {
    const struct ResvItem *resv = node->data;

    sendto_one_numeric(source_p, &me, RPL_STATSQLINE,
                       resv->expire ? 'q' : 'Q',
                       resv->mask, resv->reason);
  }
}

static void
stats_memory(struct Client *source_p, int parc, char *parv[])
{
  dlink_node *node, *node2;

  unsigned int local_client_conf_count = 0;      /* local client conf links */
  unsigned int users_counted = 0;                /* user structs */

  unsigned int channel_members = 0;
  unsigned int channel_invites = 0;
  unsigned int channel_bans = 0;
  unsigned int channel_except = 0;
  unsigned int channel_invex = 0;

  unsigned int wwu = 0;                  /* whowas users */
  unsigned int number_ips_stored = 0;        /* number of ip addresses hashed */

  size_t channel_ban_memory = 0;
  size_t channel_except_memory = 0;
  size_t channel_invex_memory = 0;

  unsigned int safelist_count = 0;
  size_t safelist_memory = 0;

  size_t wwm = 0;               /* whowas array memory used       */
  size_t mem_ips_stored = 0;        /* memory used by ip address hash */

  unsigned int local_client_count  = 0;
  unsigned int remote_client_count = 0;

  size_t local_client_memory_used  = 0;
  size_t remote_client_memory_used = 0;

  unsigned int watch_list_headers = 0;   /* watchlist headers     */
  unsigned int watch_list_entries = 0;   /* watchlist entries     */
  size_t watch_list_memory = 0; /* watchlist memory used */

  unsigned int listener_count = 0;
  size_t listener_memory = 0;


  DLINK_FOREACH(node, global_client_list.head)
  {
    const struct Client *target_p = node->data;

    if (MyConnect(target_p))
    {
      ++local_client_count;
      local_client_conf_count += dlink_list_length(&target_p->connection->confs);
      watch_list_entries += dlink_list_length(&target_p->connection->watches);
    }
    else
      ++remote_client_count;

    if (IsClient(target_p))
      ++users_counted;
  }

  /* Count up all members, invites, ban lists, except lists, Invex lists */
  DLINK_FOREACH(node, channel_list.head)
  {
    const struct Channel *chptr = node->data;

    channel_members += dlink_list_length(&chptr->members);
    channel_invites += dlink_list_length(&chptr->invites);

    channel_bans += dlink_list_length(&chptr->banlist);
    channel_ban_memory += dlink_list_length(&chptr->banlist) * sizeof(struct Ban);

    channel_except += dlink_list_length(&chptr->exceptlist);
    channel_except_memory += dlink_list_length(&chptr->exceptlist) * sizeof(struct Ban);

    channel_invex += dlink_list_length(&chptr->invexlist);
    channel_invex_memory += dlink_list_length(&chptr->invexlist) * sizeof(struct Ban);
  }

  safelist_count = dlink_list_length(&listing_client_list);
  if (safelist_count)
  {
    safelist_memory = safelist_count * sizeof(struct ListTask);

    DLINK_FOREACH(node, listing_client_list.head)
    {
      const struct Client *acptr = node->data;

      DLINK_FOREACH(node2, acptr->connection->list_task->show_mask.head)
        safelist_memory += strlen(node2->data);

      DLINK_FOREACH(node2, acptr->connection->list_task->hide_mask.head)
        safelist_memory += strlen(node2->data);
    }
  }

  watch_count_memory(&watch_list_headers, &watch_list_memory);
  sendto_one_numeric(source_p, &me, RPL_STATSDEBUG | SND_EXPLICIT,
                     "z :WATCH headers %u(%zu) entries %u(%zu)",
                     watch_list_headers,
                     watch_list_memory,
                     watch_list_entries,
                     watch_list_entries * sizeof(dlink_node) * 2);

  sendto_one_numeric(source_p, &me, RPL_STATSDEBUG | SND_EXPLICIT,
                     "z :Clients %u(%zu)",
                     users_counted,
                     users_counted * sizeof(struct Client));

  sendto_one_numeric(source_p, &me, RPL_STATSDEBUG | SND_EXPLICIT,
                     "z :Attached confs %u(%zu)",
                     local_client_conf_count,
                     local_client_conf_count * sizeof(dlink_node));

  sendto_one_numeric(source_p, &me, RPL_STATSDEBUG | SND_EXPLICIT,
                     "z :Resv channels %u(%zu) nicks %u(%zu)",
                     dlink_list_length(resv_chan_get_list()),
                     dlink_list_length(resv_chan_get_list()) * sizeof(struct ResvItem),
                     dlink_list_length(resv_nick_get_list()),
                     dlink_list_length(resv_nick_get_list()) * sizeof(struct ResvItem));

  listener_count_memory(&listener_count, &listener_memory);
  sendto_one_numeric(source_p, &me, RPL_STATSDEBUG | SND_EXPLICIT,
                     "z :Listeners %u(%zu)",
                     listener_count, listener_memory);

  sendto_one_numeric(source_p, &me, RPL_STATSDEBUG | SND_EXPLICIT,
                     "z :Classes %u(%zu)",
                     dlink_list_length(class_get_list()),
                     dlink_list_length(class_get_list()) * sizeof(struct ClassItem));

  sendto_one_numeric(source_p, &me, RPL_STATSDEBUG | SND_EXPLICIT,
                     "z :Channels %u(%zu)",
                     dlink_list_length(&channel_list),
                     dlink_list_length(&channel_list) * sizeof(struct Channel));

  sendto_one_numeric(source_p, &me, RPL_STATSDEBUG | SND_EXPLICIT,
                     "z :Bans %u(%zu)",
                     channel_bans, channel_ban_memory);

  sendto_one_numeric(source_p, &me, RPL_STATSDEBUG | SND_EXPLICIT,
                     "z :Exceptions %u(%zu)",
                     channel_except, channel_except_memory);

  sendto_one_numeric(source_p, &me, RPL_STATSDEBUG | SND_EXPLICIT,
                     "z :Invex %u(%zu)",
                     channel_invex, channel_invex_memory);

  sendto_one_numeric(source_p, &me, RPL_STATSDEBUG | SND_EXPLICIT,
                     "z :Channel members %u(%zu) invites %u(%zu)",
                     channel_members,
                     channel_members * sizeof(struct Membership),
                     channel_invites,
                     channel_invites * sizeof(struct Invite));

  sendto_one_numeric(source_p, &me, RPL_STATSDEBUG | SND_EXPLICIT,
                     "z :Safelist %u(%zu)",
                     safelist_count, safelist_memory);

  whowas_count_memory(&wwu, &wwm);
  sendto_one_numeric(source_p, &me, RPL_STATSDEBUG | SND_EXPLICIT,
                     "z :Whowas users %u(%zu)", wwu, wwm);

  motd_memory_count(source_p);

  ipcache_get_stats(&number_ips_stored, &mem_ips_stored);
  sendto_one_numeric(source_p, &me, RPL_STATSDEBUG | SND_EXPLICIT,
                     "z :iphash %u(%zu)",
                     number_ips_stored, mem_ips_stored);

  local_client_memory_used = local_client_count *(sizeof(struct Client) + sizeof(struct Connection));
  sendto_one_numeric(source_p, &me, RPL_STATSDEBUG | SND_EXPLICIT,
                     "z :Local client Memory in use: %u(%zu)",
                     local_client_count, local_client_memory_used);

  remote_client_memory_used = remote_client_count * sizeof(struct Client);
  sendto_one_numeric(source_p, &me, RPL_STATSDEBUG | SND_EXPLICIT,
                     "z :Remote client Memory in use: %u(%zu)",
                     remote_client_count, remote_client_memory_used);
}

static void
stats_dns_servers(struct Client *source_p, int parc, char *parv[])
{
  char ipaddr[HOSTIPLEN + 1] = "";

  for (unsigned int i = 0; i < irc_nscount; ++i)
  {
    getnameinfo((const struct sockaddr *)&(irc_nsaddr_list[i]),
                irc_nsaddr_list[i].ss_len, ipaddr,
                sizeof(ipaddr), NULL, 0, NI_NUMERICHOST);
    sendto_one_numeric(source_p, &me, RPL_STATSALINE, ipaddr);
  }
}

/* stats_deny()
 *
 * input	- client to report to
 * output	- none
 * side effects - client is given dline list.
 */
static void
stats_deny(struct Client *source_p, int parc, char *parv[])
{
  dlink_node *node;

  for (unsigned int i = 0; i < ATABLE_SIZE; ++i)
  {
    DLINK_FOREACH(node, atable[i].head)
    {
      const struct AddressRec *arec = node->data;

      if (arec->type != CONF_DLINE)
        continue;

      const struct MaskItem *conf = arec->conf;
      /* Don't report a temporary dline as permanent dline */
      if (conf->until)
        continue;

      sendto_one_numeric(source_p, &me, RPL_STATSDLINE, 'D', conf->host, conf->reason);
    }
  }
}

/* stats_tdeny()
 *
 * input        - client to report to
 * output       - none
 * side effects - client is given dline list.
 */
static void
stats_tdeny(struct Client *source_p, int parc, char *parv[])
{
  dlink_node *node;

  for (unsigned int i = 0; i < ATABLE_SIZE; ++i)
  {
    DLINK_FOREACH(node, atable[i].head)
    {
      const struct AddressRec *arec = node->data;

      if (arec->type != CONF_DLINE)
        continue;

      const struct MaskItem *conf = arec->conf;
      /* Don't report a permanent dline as temporary dline */
      if (!conf->until)
        continue;

      sendto_one_numeric(source_p, &me, RPL_STATSDLINE, 'd', conf->host, conf->reason);
    }
  }
}

/* stats_exempt()
 *
 * input        - client to report to
 * output       - none
 * side effects - client is given list of exempt blocks
 */
static void
stats_exempt(struct Client *source_p, int parc, char *parv[])
{
  dlink_node *node;

  if (ConfigGeneral.stats_e_disabled)
  {
    sendto_one_numeric(source_p, &me, ERR_NOPRIVILEGES);
    return;
  }

  for (unsigned int i = 0; i < ATABLE_SIZE; ++i)
  {
    DLINK_FOREACH(node, atable[i].head)
    {
      const struct AddressRec *arec = node->data;

      if (arec->type != CONF_EXEMPT)
        continue;

      const struct MaskItem *conf = arec->conf;
      sendto_one_numeric(source_p, &me, RPL_STATSDLINE, 'e', conf->host, "");
    }
  }
}

static void
stats_events(struct Client *source_p, int parc, char *parv[])
{
  dlink_node *node;

  sendto_one_numeric(source_p, &me, RPL_STATSDEBUG | SND_EXPLICIT,
                     "E :Operation                      Next Execution");
  sendto_one_numeric(source_p, &me, RPL_STATSDEBUG | SND_EXPLICIT,
                     "E :---------------------------------------------");

  DLINK_FOREACH(node, event_get_list()->head)
  {
    const struct event *ev = node->data;

    sendto_one_numeric(source_p, &me, RPL_STATSDEBUG | SND_EXPLICIT,
                       "E :%-30s %-4ji seconds",
                       ev->name, ev->next - CurrentTime);
  }
}

static void
stats_hubleaf(struct Client *source_p, int parc, char *parv[])
{
  dlink_node *node, *node2;

  DLINK_FOREACH(node, connect_items.head)
  {
    const struct MaskItem *conf = node->data;

    DLINK_FOREACH(node2, conf->hub_list.head)
      sendto_one_numeric(source_p, &me, RPL_STATSHLINE, 'H', node2->data, conf->name, 0, "*");
  }

  DLINK_FOREACH(node, connect_items.head)
  {
    const struct MaskItem *conf = node->data;

    DLINK_FOREACH(node2, conf->leaf_list.head)
      sendto_one_numeric(source_p, &me, RPL_STATSLLINE, 'L', node2->data, conf->name, 0, "*");
  }
}

/*
 * show_iline_prefix()
 *
 * inputs       - pointer to struct Client requesting output
 *              - pointer to struct MaskItem
 *              - name to which iline prefix will be prefixed to
 * output       - pointer to static string with prefixes listed in ascii form
 * side effects - NONE
 */
static const char *
show_iline_prefix(const struct Client *source_p, const struct MaskItem *conf)
{
  static char buf[USERLEN + 16];
  char *p = buf;

  if (IsConfWebIRC(conf))
    *p++ = '<';
  if (IsNoTilde(conf))
    *p++ = '-';
  if (IsNeedIdentd(conf))
    *p++ = '+';
  if (!IsNeedPassword(conf))
    *p++ = '&';
  if (IsConfExemptResv(conf))
    *p++ = '$';
  if (IsConfDoSpoofIp(conf))
    *p++ = '=';
  if (IsConfCanFlood(conf))
    *p++ = '|';
  if (HasUMode(source_p, UMODE_OPER))
  {
    if (IsConfExemptKline(conf))
      *p++ = '^';
    if (IsConfExemptXline(conf))
      *p++ = '!';
    if (IsConfExemptLimits(conf))
      *p++ = '>';
  }

  strlcpy(p, conf->user, USERLEN + 1);
  return buf;
}

static void
report_auth(struct Client *source_p, int parc, char *parv[])
{
  dlink_node *node;

  for (unsigned int i = 0; i < ATABLE_SIZE; ++i)
  {
    DLINK_FOREACH(node, atable[i].head)
    {
      const struct AddressRec *arec = node->data;

      if (arec->type != CONF_CLIENT)
        continue;

      const struct MaskItem *conf = arec->conf;
      if (!HasUMode(source_p, UMODE_OPER) && IsConfDoSpoofIp(conf))
        continue;

      sendto_one_numeric(source_p, &me, RPL_STATSILINE, 'I',
                         conf->name == NULL ? "*" : conf->name,
                         show_iline_prefix(source_p, conf),
                         conf->host, conf->port,
                         conf->class->name);
    }
  }
}

static void
stats_auth(struct Client *source_p, int parc, char *parv[])
{
  /* Oper only, if unopered, return ERR_NOPRIVILEGES */
  if (ConfigGeneral.stats_i_oper_only == 2 && !HasUMode(source_p, UMODE_OPER))
    sendto_one_numeric(source_p, &me, ERR_NOPRIVILEGES);

  /* If unopered, only return matching auth blocks */
  else if (ConfigGeneral.stats_i_oper_only == 1 && !HasUMode(source_p, UMODE_OPER))
  {
    const struct MaskItem *conf = NULL;

    if (MyConnect(source_p))
      conf = find_conf_by_address(source_p->host,
                                  &source_p->connection->ip, CONF_CLIENT,
                                  source_p->connection->aftype,
                                  source_p->username,
                                  source_p->connection->password, 1);
    else
      conf = find_conf_by_address(source_p->host, NULL, CONF_CLIENT, 0,
                                  source_p->username, NULL, 1);

    if (!conf)
      return;

    sendto_one_numeric(source_p, &me, RPL_STATSILINE,
                       'I', "*", show_iline_prefix(source_p, conf),
                       conf->host, conf->port,
                       conf->class->name);
  }
  else  /* They are opered, or allowed to see all auth blocks */
    report_auth(source_p, 0, NULL);
}

/* report_Klines()
 * Inputs: Client to report to,
 *         type(==0 for perm, !=0 for temporary)
 *         mask
 * Output: None
 * Side effects: Reports configured K(or k)-lines to source_p.
 */
static void
report_Klines(struct Client *source_p, int tkline)
{
  dlink_node *node;
  char c = '\0';

  if (tkline)
    c = 'k';
  else
    c = 'K';

  for (unsigned int i = 0; i < ATABLE_SIZE; ++i)
  {
    DLINK_FOREACH(node, atable[i].head)
    {
      const struct AddressRec *arec = node->data;

      if (arec->type != CONF_KLINE)
        continue;

      const struct MaskItem *conf = arec->conf;
      if ((!tkline && conf->until) ||
          (tkline && !conf->until))
        continue;

      sendto_one_numeric(source_p, &me, RPL_STATSKLINE, c, conf->host, conf->user,
                         conf->reason);
    }
  }
}

static void
stats_tklines(struct Client *source_p, int parc, char *parv[])
{
  /* Oper only, if unopered, return ERR_NOPRIVILEGES */
  if (ConfigGeneral.stats_k_oper_only == 2 && !HasUMode(source_p, UMODE_OPER))
    sendto_one_numeric(source_p, &me, ERR_NOPRIVILEGES);

  /* If unopered, only return matching klines */
  else if (ConfigGeneral.stats_k_oper_only == 1 && !HasUMode(source_p, UMODE_OPER))
  {
    const struct MaskItem *conf = NULL;

    if (MyConnect(source_p))
      conf = find_conf_by_address(source_p->host,
                                  &source_p->connection->ip, CONF_KLINE,
                                  source_p->connection->aftype,
                                  source_p->username, NULL, 1);
    else
      conf = find_conf_by_address(source_p->host, NULL, CONF_KLINE, 0,
                                  source_p->username, NULL, 1);

    if (!conf)
      return;

    /* Don't report a permanent kline as temporary kline */
    if (!conf->until)
      return;

    sendto_one_numeric(source_p, &me, RPL_STATSKLINE, 'k',
                       conf->host, conf->user, conf->reason);
  }
  else  /* They are opered, or allowed to see all klines */
    report_Klines(source_p, 1);
}

static void
stats_klines(struct Client *source_p, int parc, char *parv[])
{
  /* Oper only, if unopered, return ERR_NOPRIVILEGES */
  if (ConfigGeneral.stats_k_oper_only == 2 && !HasUMode(source_p, UMODE_OPER))
    sendto_one_numeric(source_p, &me, ERR_NOPRIVILEGES);

  /* If unopered, only return matching klines */
  else if (ConfigGeneral.stats_k_oper_only == 1 && !HasUMode(source_p, UMODE_OPER))
  {
    const struct MaskItem *conf = NULL;

    /* Search for a kline */
    if (MyConnect(source_p))
      conf = find_conf_by_address(source_p->host,
                                  &source_p->connection->ip, CONF_KLINE,
                                  source_p->connection->aftype,
                                  source_p->username, NULL, 0);
    else
      conf = find_conf_by_address(source_p->host, NULL, CONF_KLINE, 0,
                                  source_p->username, NULL, 0);

    if (!conf)
      return;

    /* Don't report a temporary kline as permanent kline */
    if (conf->until)
      return;

    sendto_one_numeric(source_p, &me, RPL_STATSKLINE, 'K',
                       conf->host, conf->user, conf->reason);
  }
  else  /* They are opered, or allowed to see all klines */
    report_Klines(source_p, 0);
}

static void
stats_messages(struct Client *source_p, int parc, char *parv[])
{
  if (!HasUMode(source_p, UMODE_OPER) && ConfigGeneral.stats_m_oper_only)
    sendto_one_numeric(source_p, &me, ERR_NOPRIVILEGES);
  else
    report_messages(source_p);
}

/* stats_operedup()
 *
 * input	- client pointer
 * output	- none
 * side effects - client is shown a list of active opers
 */
static void
stats_operedup(struct Client *source_p, int parc, char *parv[])
{
  dlink_node *node;
  unsigned int opercount = 0;
  char buf[IRCD_BUFSIZE] = "";

  DLINK_FOREACH(node, oper_list.head)
  {
    const struct Client *target_p = node->data;

    if (HasUMode(target_p, UMODE_HIDDEN) && !HasUMode(source_p, UMODE_OPER))
      continue;

    if (HasUMode(source_p, UMODE_OPER) || !HasUMode(target_p, UMODE_HIDEIDLE))
      snprintf(buf, sizeof(buf), "%s", time_dissect(client_get_idle_time(source_p, target_p)));
    else
      strlcpy(buf, "n/a", sizeof(buf));

    if (MyConnect(source_p) && HasUMode(source_p, UMODE_OPER))
      sendto_one_numeric(source_p, &me, RPL_STATSDEBUG | SND_EXPLICIT,
                         "p :[%c][%s] %s (%s@%s) Idle: %s",
                         HasUMode(target_p, UMODE_ADMIN) ? 'A' : 'O',
                         oper_privs_as_string(target_p->connection->operflags),
                         target_p->name, target_p->username, target_p->host, buf);
    else
      sendto_one_numeric(source_p, &me, RPL_STATSDEBUG | SND_EXPLICIT,
                         "p :[%c] %s (%s@%s) Idle: %s",
                         HasUMode(target_p, UMODE_ADMIN) ? 'A' : 'O',
                         target_p->name, target_p->username, target_p->host, buf);
    ++opercount;
  }

  sendto_one_numeric(source_p, &me, RPL_STATSDEBUG | SND_EXPLICIT,
                     "p :%u OPER(s)", opercount);
}

/* show_ports()
 *
 * inputs       - pointer to client to show ports to
 * output       - none
 * side effects - send port listing to a client
 */
static void
show_ports(struct Client *source_p)
{
  dlink_node *node;

  DLINK_FOREACH(node, listener_get_list()->head)
  {
    char buf[8];
    char *p = buf;
    const struct Listener *listener = node->data;

    if (listener->flags & LISTENER_HIDDEN)
    {
      if (!HasUMode(source_p, UMODE_ADMIN))
        continue;
      *p++ = 'H';
    }

    if (listener->flags & LISTENER_SERVER)
      *p++ = 'S';
    if (listener->flags & LISTENER_SSL)
      *p++ = 's';
    *p = '\0';

    if (HasUMode(source_p, UMODE_ADMIN) && !ConfigServerHide.hide_server_ips)
      sendto_one_numeric(source_p, &me, RPL_STATSPLINE, 'P', listener->port,
                         listener->name,
                         listener->ref_count, buf,
                         listener->active ? "active" : "disabled");
    else
      sendto_one_numeric(source_p, &me, RPL_STATSPLINE, 'P', listener->port,
                         me.name, listener->ref_count, buf,
                         listener->active ? "active" : "disabled");
  }
}

static void
stats_ports(struct Client *source_p, int parc, char *parv[])
{
  if (!HasUMode(source_p, UMODE_OPER) && ConfigGeneral.stats_P_oper_only)
    sendto_one_numeric(source_p, &me, ERR_NOPRIVILEGES);
  else
    show_ports(source_p);
}

static void
stats_tstats(struct Client *source_p, int parc, char *parv[])
{
  dlink_node *node;
  struct ServerStatistics tmp;
  struct ServerStatistics *sp = &tmp;

  memcpy(sp, &ServerStats, sizeof(struct ServerStatistics));

  /*
   * must use the += operator. is_sv is not the number of currently
   * active server connections. Note the incrementation in
   * s_bsd.c:close_connection.
   */
  sp->is_sv += dlink_list_length(&local_server_list);

  DLINK_FOREACH(node, local_server_list.head)
  {
    const struct Client *target_p = node->data;

    sp->is_sbs += target_p->connection->send.bytes;
    sp->is_sbr += target_p->connection->recv.bytes;
    sp->is_sti += CurrentTime - target_p->connection->firsttime;
  }

  sp->is_cl += dlink_list_length(&local_client_list);

  DLINK_FOREACH(node, local_client_list.head)
  {
    const struct Client *target_p = node->data;

    sp->is_cbs += target_p->connection->send.bytes;
    sp->is_cbr += target_p->connection->recv.bytes;
    sp->is_cti += CurrentTime - target_p->connection->firsttime;
  }

  sp->is_ni += dlink_list_length(&unknown_list);

  sendto_one_numeric(source_p, &me, RPL_STATSDEBUG | SND_EXPLICIT,
                     "t :accepts %u refused %u",
                     sp->is_ac, sp->is_ref);
  sendto_one_numeric(source_p, &me, RPL_STATSDEBUG | SND_EXPLICIT,
                     "t :unknown commands %u prefixes %u",
                     sp->is_unco, sp->is_unpf);
  sendto_one_numeric(source_p, &me, RPL_STATSDEBUG | SND_EXPLICIT,
                     "t :nick collisions %u unknown closes %u",
                     sp->is_kill, sp->is_ni);
  sendto_one_numeric(source_p, &me, RPL_STATSDEBUG | SND_EXPLICIT,
                     "t :wrong direction %u empty %u",
                     sp->is_wrdi, sp->is_empt);
  sendto_one_numeric(source_p, &me, RPL_STATSDEBUG | SND_EXPLICIT,
                     "t :numerics seen %u",
                     sp->is_num);
  sendto_one_numeric(source_p, &me, RPL_STATSDEBUG | SND_EXPLICIT,
                     "t :auth successes %u fails %u",
                     sp->is_asuc, sp->is_abad);
  sendto_one_numeric(source_p, &me, RPL_STATSDEBUG | SND_EXPLICIT,
                     "t :Client Server");
  sendto_one_numeric(source_p, &me, RPL_STATSDEBUG | SND_EXPLICIT,
                     "t :connected %u %u",
                     sp->is_cl, sp->is_sv);
  sendto_one_numeric(source_p, &me, RPL_STATSDEBUG | SND_EXPLICIT,
                     "t :bytes sent %ju %ju",
                     sp->is_cbs, sp->is_sbs);
  sendto_one_numeric(source_p, &me, RPL_STATSDEBUG | SND_EXPLICIT,
                     "t :bytes received %ju %ju",
                     sp->is_cbr, sp->is_sbr);
  sendto_one_numeric(source_p, &me, RPL_STATSDEBUG | SND_EXPLICIT,
                     "t :time connected %ju %ju",
                     sp->is_cti, sp->is_sti);
}

static void
stats_uptime(struct Client *source_p, int parc, char *parv[])
{
  if (!HasUMode(source_p, UMODE_OPER) && ConfigGeneral.stats_u_oper_only)
    sendto_one_numeric(source_p, &me, ERR_NOPRIVILEGES);
  else
  {
    sendto_one_numeric(source_p, &me, RPL_STATSUPTIME,
                       time_dissect(CurrentTime - me.connection->since));
    if (!ConfigServerHide.disable_remote_commands || HasUMode(source_p, UMODE_OPER))
       sendto_one_numeric(source_p, &me, RPL_STATSCONN, Count.max_loc_con,
                          Count.max_loc_cli, Count.totalrestartcount);
  }
}

static void
stats_shared(struct Client *source_p, int parc, char *parv[])
{
  report_shared(source_p);
  report_cluster(source_p);
}

/* stats_servers()
 *
 * input	- client pointer
 * output	- none
 * side effects - client is shown lists of who connected servers
 */
static void
stats_servers(struct Client *source_p, int parc, char *parv[])
{
  dlink_node *node;

  DLINK_FOREACH(node, local_server_list.head)
  {
    const struct Client *target_p = node->data;

    sendto_one_numeric(source_p, &me, RPL_STATSDEBUG | SND_EXPLICIT,
                       "v :%s (%s!%s@%s) Idle: %s",
                       target_p->name,
                       (target_p->serv->by[0] ? target_p->serv->by : "Remote."),
                       "*", "*", time_dissect(CurrentTime - target_p->connection->lasttime));
  }

  sendto_one_numeric(source_p, &me, RPL_STATSDEBUG | SND_EXPLICIT,
                     "v :%u Server(s)",
                     dlink_list_length(&local_server_list));
}

static void
stats_class(struct Client *source_p, int parc, char *parv[])
{
  dlink_node *node;

  DLINK_FOREACH(node, class_get_list()->head)
  {
    const struct ClassItem *class = node->data;

    sendto_one_numeric(source_p, &me, RPL_STATSYLINE, 'Y',
                       class->name, class->ping_freq,
                       class->con_freq,
                       class->max_total, class->max_sendq,
                       class->max_recvq,
                       class->ref_count,
                       class->number_per_cidr, class->cidr_bitlen_ipv4,
                       class->number_per_cidr, class->cidr_bitlen_ipv6,
                       class->active ? "active" : "disabled");
  }
}

static void
stats_servlinks(struct Client *source_p, int parc, char *parv[])
{
  dlink_node *node;
  uintmax_t sendB = 0, recvB = 0;
  uintmax_t uptime = 0;

  if (ConfigServerHide.flatten_links && !HasUMode(source_p, UMODE_OPER))
  {
    sendto_one_numeric(source_p, &me, ERR_NOPRIVILEGES);
    return;
  }

  DLINK_FOREACH(node, local_server_list.head)
  {
    const struct Client *target_p = node->data;

    if (HasFlag(target_p, FLAGS_SERVICE) && ConfigServerHide.hide_services)
      if (!HasUMode(source_p, UMODE_OPER))
        continue;

    sendB += target_p->connection->send.bytes;
    recvB += target_p->connection->recv.bytes;

    /* ":%s 211 %s %s %u %u %ju %u %ju :%u %u %s" */
    sendto_one_numeric(source_p, &me, RPL_STATSLINKINFO,
               get_client_name(target_p, HasUMode(source_p, UMODE_ADMIN) ? SHOW_IP : MASK_IP),
               dbuf_length(&target_p->connection->buf_sendq),
               target_p->connection->send.messages,
               target_p->connection->send.bytes >> 10,
               target_p->connection->recv.messages,
               target_p->connection->recv.bytes >> 10,
               (unsigned int)(CurrentTime - target_p->connection->firsttime),
               (CurrentTime > target_p->connection->since) ? (unsigned int)(CurrentTime - target_p->connection->since) : 0,
               HasUMode(source_p, UMODE_OPER) ? get_capabilities(target_p) : "TS");
  }

  sendB >>= 10;
  recvB >>= 10;

  sendto_one_numeric(source_p, &me, RPL_STATSDEBUG | SND_EXPLICIT, "? :%u total server(s)",
                     dlink_list_length(&local_server_list));
  sendto_one_numeric(source_p, &me, RPL_STATSDEBUG | SND_EXPLICIT, "? :Sent total: %7.2f %s",
                     _GMKv(sendB), _GMKs(sendB));
  sendto_one_numeric(source_p, &me, RPL_STATSDEBUG | SND_EXPLICIT, "? :Recv total: %7.2f %s",
                     _GMKv(recvB), _GMKs(recvB));

  uptime = (CurrentTime - me.connection->since);

  sendto_one_numeric(source_p, &me, RPL_STATSDEBUG | SND_EXPLICIT,
                     "? :Server send: %7.2f %s (%4.1f K/s)",
                     _GMKv((me.connection->send.bytes >> 10)),
                     _GMKs((me.connection->send.bytes >> 10)),
                     (float)((float)((me.connection->send.bytes) >> 10) /
                     (float)uptime));
  sendto_one_numeric(source_p, &me, RPL_STATSDEBUG | SND_EXPLICIT,
                     "? :Server recv: %7.2f %s (%4.1f K/s)",
                     _GMKv((me.connection->recv.bytes >> 10)),
                     _GMKs((me.connection->recv.bytes >> 10)),
                     (float)((float)((me.connection->recv.bytes) >> 10) /
                     (float)uptime));
}

/* parse_stats_args()
 *
 * inputs       - arg count
 *              - args
 *              - doall flag
 *              - wild card or not
 * output       - pointer to name to use
 * side effects -
 * common parse routine for m_stats args
 *
 */
static const char *
parse_stats_args(struct Client *source_p, int parc, char *parv[], int *doall, int *wilds)
{
  if (parc > 2)
  {
    const char *name = parv[2];

    if (!irccmp(name, ID_or_name(&me, source_p)))
      *doall = 2;
    else if (!match(name, ID_or_name(&me, source_p)))
      *doall = 1;

    *wilds = has_wildcards(name);

    return name;
  }

  return NULL;
}

static void
stats_L_list(struct Client *source_p, const char *name, int doall, int wilds,
             dlink_list *list, const char statchar)
{
  dlink_node *node;

  /*
   * Send info about connections which match, or all if the
   * mask matches from.
   */
  DLINK_FOREACH(node, list->head)
  {
    const struct Client *target_p = node->data;
    enum addr_mask_type type = 0;

    if (!doall && wilds && match(name, target_p->name))
      continue;

    if (!(doall || wilds) && irccmp(name, target_p->name))
      continue;

    if (IsUpper(statchar))
      type = SHOW_IP;
    else
      type = HIDE_IP;

    if (IsServer(target_p) || IsConnecting(target_p) || IsHandshake(target_p))
      if (!HasUMode(source_p, UMODE_ADMIN))
        type = MASK_IP;

    sendto_one_numeric(source_p, &me, RPL_STATSLINKINFO,
                       get_client_name(target_p, type),
                       dbuf_length(&target_p->connection->buf_sendq),
                       target_p->connection->send.messages,
                       target_p->connection->send.bytes >> 10,
                       target_p->connection->recv.messages,
                       target_p->connection->recv.bytes >> 10,
                       (unsigned int)(CurrentTime - target_p->connection->firsttime),
                       (CurrentTime > target_p->connection->since) ? (unsigned int)(CurrentTime - target_p->connection->since) : 0,
                       IsServer(target_p) ? get_capabilities(target_p) : "-");
  }
}

/*
 * stats_L
 *
 * inputs       - pointer to client to report to
 *              - doall flag
 *              - wild card or not
 * output       - NONE
 * side effects -
 */
static void
stats_L(struct Client *source_p, const char *name, int doall,
        int wilds, const char statchar)
{
  stats_L_list(source_p, name, doall, wilds, &unknown_list, statchar);
  stats_L_list(source_p, name, doall, wilds, &local_client_list, statchar);
  stats_L_list(source_p, name, doall, wilds, &local_server_list, statchar);
}

static void
stats_ltrace(struct Client *source_p, int parc, char *parv[])
{
  int doall = 0;
  int wilds = 0;
  const char *name = NULL;

  if ((name = parse_stats_args(source_p, parc, parv, &doall, &wilds)))
  {
    const char statchar = *parv[1];
    stats_L(source_p, name, doall, wilds, statchar);
  }
  else
    sendto_one_numeric(source_p, &me, ERR_NEEDMOREPARAMS, "STATS");
}

struct StatsStruct
{
  const unsigned char letter;
  void (*handler)(struct Client *, int, char *[]);
  const unsigned int required_modes;
};

static const struct StatsStruct *stats_map[256];
static const struct StatsStruct  stats_tab[] =
{
  { 'a',  stats_dns_servers, UMODE_ADMIN },
  { 'A',  stats_dns_servers, UMODE_ADMIN },
  { 'c',  stats_connect,     UMODE_OPER  },
  { 'C',  stats_connect,     UMODE_OPER  },
  { 'd',  stats_tdeny,       UMODE_OPER  },
  { 'D',  stats_deny,        UMODE_OPER  },
  { 'e',  stats_exempt,      UMODE_OPER  },
  { 'E',  stats_events,      UMODE_ADMIN },
  { 'f',  fd_dump,           UMODE_ADMIN },
  { 'F',  fd_dump,           UMODE_ADMIN },
  { 'h',  stats_hubleaf,     UMODE_OPER  },
  { 'H',  stats_hubleaf,     UMODE_OPER  },
  { 'i',  stats_auth,        0           },
  { 'I',  stats_auth,        0           },
  { 'k',  stats_tklines,     0           },
  { 'K',  stats_klines,      0           },
  { 'l',  stats_ltrace,      UMODE_OPER  },
  { 'L',  stats_ltrace,      UMODE_OPER  },
  { 'm',  stats_messages,    0           },
  { 'M',  stats_messages,    0           },
  { 'o',  stats_operator,    0           },
  { 'O',  stats_operator,    0           },
  { 'p',  stats_operedup,    0           },
  { 'P',  stats_ports,       0           },
  { 'q',  stats_resv,        UMODE_OPER  },
  { 'Q',  stats_resv,        UMODE_OPER  },
  { 's',  stats_service,     UMODE_OPER  },
  { 'S',  stats_service,     UMODE_OPER  },
  { 't',  stats_tstats,      UMODE_OPER  },
  { 'T',  motd_report,       UMODE_OPER  },
  { 'u',  stats_uptime,      0           },
  { 'U',  stats_shared,      UMODE_OPER  },
  { 'v',  stats_servers,     UMODE_OPER  },
  { 'x',  stats_gecos,       UMODE_OPER  },
  { 'X',  stats_gecos,       UMODE_OPER  },
  { 'y',  stats_class,       UMODE_OPER  },
  { 'Y',  stats_class,       UMODE_OPER  },
  { 'z',  stats_memory,      UMODE_OPER  },
  { '?',  stats_servlinks,   0           },
  { '\0', NULL,              0           }
};

static void
do_stats(struct Client *source_p, int parc, char *parv[])
{
  const unsigned char statchar = *parv[1];
  const struct StatsStruct *tab;

  if (statchar == '\0')
  {
    sendto_one_numeric(source_p, &me, RPL_ENDOFSTATS, '*');
    return;
  }

  if ((tab = stats_map[statchar]))
  {
    if (!tab->required_modes || HasUMode(source_p, tab->required_modes))
      tab->handler(source_p, parc, parv);
    else
      sendto_one_numeric(source_p, &me, ERR_NOPRIVILEGES);

    sendto_realops_flags(UMODE_SPY, L_ALL, SEND_NOTICE,
                         "STATS %c requested by %s (%s@%s) [%s]",
                         statchar, source_p->name, source_p->username,
                         source_p->host, source_p->servptr->name);
  }

  sendto_one_numeric(source_p, &me, RPL_ENDOFSTATS, statchar);
}

/*
 * m_stats()
 *      parv[0] = command
 *      parv[1] = stat letter/command
 *      parv[2] = (if present) server/mask in stats L
 */
static int
m_stats(struct Client *source_p, int parc, char *parv[])
{
  static uintmax_t last_used = 0;

  /* Check the user is actually allowed to do /stats, and isn't flooding */
  if ((last_used + ConfigGeneral.pace_wait) > CurrentTime)
  {
    sendto_one_numeric(source_p, &me, RPL_LOAD2HI, "STATS");
    return 0;
  }

  last_used = CurrentTime;

  /* Is the stats meant for us? */
  if (!ConfigServerHide.disable_remote_commands)
    if (hunt_server(source_p, ":%s STATS %s :%s", 2, parc, parv) != HUNTED_ISME)
      return 0;

  do_stats(source_p, parc, parv);
  return 0;
}

/*
 * ms_stats()
 *      parv[0] = command
 *      parv[1] = stat letter/command
 *      parv[2] = (if present) server/mask in stats L, or target
 */
static int
ms_stats(struct Client *source_p, int parc, char *parv[])
{
  if (hunt_server(source_p, ":%s STATS %s :%s", 2, parc, parv) != HUNTED_ISME)
     return 0;

  do_stats(source_p, parc, parv);
  return 0;
}

static void
stats_init(void)
{
  for (const struct StatsStruct *tab = stats_tab; tab->letter; ++tab)
    stats_map[tab->letter] = tab;
}

static struct Message stats_msgtab =
{
  .cmd = "STATS",
  .args_min = 2,
  .args_max = MAXPARA,
  .handlers[UNREGISTERED_HANDLER] = m_unregistered,
  .handlers[CLIENT_HANDLER] = m_stats,
  .handlers[SERVER_HANDLER] = ms_stats,
  .handlers[ENCAP_HANDLER] = m_ignore,
  .handlers[OPER_HANDLER] = ms_stats
};

static void
module_init(void)
{
  stats_init();
  mod_add_cmd(&stats_msgtab);
}

static void
module_exit(void)
{
  mod_del_cmd(&stats_msgtab);
}

struct module module_entry =
{
  .version = "$Revision$",
  .modinit = module_init,
  .modexit = module_exit,
};
