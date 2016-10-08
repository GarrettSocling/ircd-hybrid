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

/*! \file conf.h
 * \brief A header for the configuration functions.
 * \version $Id$
 */

#ifndef INCLUDED_conf_h
#define INCLUDED_conf_h
#include "config.h"
#include "client.h"
#include "conf_class.h"
#include "tls.h"


#define CONF_NOREASON "<No reason supplied>"

/* MaskItem->flags */
enum
{
  CONF_FLAGS_NO_TILDE        = 0x00000001U,
  CONF_FLAGS_NEED_IDENTD     = 0x00000002U,
  CONF_FLAGS_EXEMPTKLINE     = 0x00000004U,
  CONF_FLAGS_NOLIMIT         = 0x00000008U,
  CONF_FLAGS_SPOOF_IP        = 0x00000010U,
  CONF_FLAGS_SPOOF_NOTICE    = 0x00000020U,
  CONF_FLAGS_REDIR           = 0x00000040U,
  CONF_FLAGS_CAN_FLOOD       = 0x00000080U,
  CONF_FLAGS_NEED_PASSWORD   = 0x00000100U,
  CONF_FLAGS_ALLOW_AUTO_CONN = 0x00000200U,
  CONF_FLAGS_ENCRYPTED       = 0x00000400U,
  CONF_FLAGS_IN_DATABASE     = 0x00000800U,
  CONF_FLAGS_EXEMPTRESV      = 0x00001000U,
  CONF_FLAGS_SSL             = 0x00002000U,
  CONF_FLAGS_WEBIRC          = 0x00004000U,
  CONF_FLAGS_EXEMPTXLINE     = 0x00008000U
};

/* Macros for struct MaskItem */
#define IsConfWebIRC(x)           ((x)->flags & CONF_FLAGS_WEBIRC)
#define IsNoTilde(x)              ((x)->flags & CONF_FLAGS_NO_TILDE)
#define IsConfCanFlood(x)         ((x)->flags & CONF_FLAGS_CAN_FLOOD)
#define IsNeedPassword(x)         ((x)->flags & CONF_FLAGS_NEED_PASSWORD)
#define IsNeedIdentd(x)           ((x)->flags & CONF_FLAGS_NEED_IDENTD)
#define IsConfExemptKline(x)      ((x)->flags & CONF_FLAGS_EXEMPTKLINE)
#define IsConfExemptXline(x)      ((x)->flags & CONF_FLAGS_EXEMPTXLINE)
#define IsConfExemptLimits(x)     ((x)->flags & CONF_FLAGS_NOLIMIT)
#define IsConfExemptResv(x)       ((x)->flags & CONF_FLAGS_EXEMPTRESV)
#define IsConfDoSpoofIp(x)        ((x)->flags & CONF_FLAGS_SPOOF_IP)
#define IsConfSpoofNotice(x)      ((x)->flags & CONF_FLAGS_SPOOF_NOTICE)
#define IsConfAllowAutoConn(x)    ((x)->flags & CONF_FLAGS_ALLOW_AUTO_CONN)
#define SetConfAllowAutoConn(x)   ((x)->flags |= CONF_FLAGS_ALLOW_AUTO_CONN)
#define ClearConfAllowAutoConn(x) ((x)->flags &= ~CONF_FLAGS_ALLOW_AUTO_CONN)
#define IsConfRedir(x)            ((x)->flags & CONF_FLAGS_REDIR)
#define IsConfSSL(x)              ((x)->flags & CONF_FLAGS_SSL)
#define IsConfDatabase(x)         ((x)->flags & CONF_FLAGS_IN_DATABASE)
#define SetConfDatabase(x)        ((x)->flags |= CONF_FLAGS_IN_DATABASE)


enum maskitem_type
{
  CONF_CLIENT  = 1 << 0,
  CONF_SERVER  = 1 << 1,
  CONF_KLINE   = 1 << 2,
  CONF_DLINE   = 1 << 3,
  CONF_EXEMPT  = 1 << 4,
  CONF_OPER    = 1 << 5
};

#define IsConfKill(x)           ((x)->type == CONF_KLINE)
#define IsConfClient(x)         ((x)->type == CONF_CLIENT)

enum
{
  NOT_AUTHORIZED = -1,
  I_LINE_FULL    = -2,
  TOO_MANY       = -3,
  BANNED_CLIENT  = -4,
  TOO_FAST       = -5
};

struct split_nuh_item
{
  dlink_node node;

  char *nuhmask;
  char *nickptr;
  char *userptr;
  char *hostptr;

  size_t nicksize;
  size_t usersize;
  size_t hostsize;
};

struct MaskItem
{
  dlink_node         node;
  dlink_list         leaf_list;
  dlink_list         hub_list;
  enum maskitem_type type;
  unsigned int       dns_failed;
  unsigned int       dns_pending;
  unsigned int       flags;
  unsigned int       modes;
  unsigned int       port;
  unsigned int       aftype;
  unsigned int       active;
  unsigned int       htype;
  unsigned int       ref_count;  /* Number of *LOCAL* clients using this */
  int                bits;
  uintmax_t          until;     /* Hold action until this time (calendar time) */
  uintmax_t          setat;
  struct irc_ssaddr  bind;  /* ip to bind to for outgoing connect */
  struct irc_ssaddr  addr;  /* ip to connect to */
  struct ClassItem  *class;  /* Class of connection */
  char              *name;
  char              *user;     /* user part of user@host */
  char              *host;     /* host part of user@host */
  char              *passwd;
  char              *spasswd;  /* Password to send. */
  char              *reason;
  char              *certfp;
  char              *whois;
  char              *cipher_list;
};

struct CidrItem
{
  dlink_node node;
  struct irc_ssaddr mask;
  unsigned int number_on_this_cidr;
};

struct conf_parser_context
{
  unsigned int boot;
  unsigned int pass;
  FILE *conf_file;
};

struct config_general_entry
{
  const char *dpath;
  const char *mpath;
  const char *spath;
  const char *configfile;
  const char *klinefile;
  const char *xlinefile;
  const char *dlinefile;
  const char *resvfile;

  unsigned int dline_min_cidr;
  unsigned int dline_min_cidr6;
  unsigned int kline_min_cidr;
  unsigned int kline_min_cidr6;
  unsigned int dots_in_ident;
  unsigned int failed_oper_notice;
  unsigned int anti_spam_exit_message_time;
  unsigned int max_accept;
  unsigned int max_watch;
  unsigned int whowas_history_length;
  unsigned int away_time;
  unsigned int away_count;
  unsigned int max_nick_time;
  unsigned int max_nick_changes;
  unsigned int ts_max_delta;
  unsigned int ts_warn_delta;
  unsigned int anti_nick_flood;
  unsigned int warn_no_connect_block;
  unsigned int invisible_on_connect;
  unsigned int stats_e_disabled;
  unsigned int stats_i_oper_only;
  unsigned int stats_k_oper_only;
  unsigned int stats_m_oper_only;
  unsigned int stats_o_oper_only;
  unsigned int stats_P_oper_only;
  unsigned int stats_u_oper_only;
  unsigned int short_motd;
  unsigned int no_oper_flood;
  unsigned int tkline_expire_notices;
  unsigned int opers_bypass_callerid;
  unsigned int ignore_bogus_ts;
  unsigned int pace_wait;
  unsigned int pace_wait_simple;
  unsigned int oper_only_umodes;
  unsigned int oper_umodes;
  unsigned int max_targets;
  unsigned int caller_id_wait;
  unsigned int min_nonwildcard;
  unsigned int min_nonwildcard_simple;
  unsigned int kill_chase_time_limit;
  unsigned int default_floodcount;
  unsigned int throttle_count;
  unsigned int throttle_time;
  unsigned int ping_cookie;
  unsigned int disable_auth;
  unsigned int cycle_on_host_change;
};

struct config_channel_entry
{
  unsigned int disable_fake_channels;
  unsigned int invite_client_count;
  unsigned int invite_client_time;
  unsigned int invite_delay_channel;
  unsigned int knock_client_count;
  unsigned int knock_client_time;
  unsigned int knock_delay_channel;
  unsigned int max_invites;
  unsigned int max_bans;
  unsigned int max_channels;
  unsigned int default_join_flood_count;
  unsigned int default_join_flood_time;
};

struct config_serverhide_entry
{
  char *hidden_name;
  char *flatten_links_file;
  unsigned int flatten_links;
  unsigned int flatten_links_delay;
  unsigned int disable_remote_commands;
  unsigned int hide_servers;
  unsigned int hide_services;
  unsigned int hidden;
  unsigned int hide_server_ips;
};

struct config_serverinfo_entry
{
  char *sid;
  char *name;
  char *description;
  char *network_name;
  char *network_desc;
  char *libgeoip_ipv4_database_file;
  char *libgeoip_ipv6_database_file;
  char *rsa_private_key_file;
  char *ssl_certificate_file;
  char *ssl_dh_param_file;
  char *ssl_dh_elliptic_curve;
  char *ssl_cipher_list;
  char *ssl_message_digest_algorithm;
  tls_context_t tls_ctx;
  tls_md_t message_digest_algorithm;
  unsigned int hub;
  unsigned int default_max_clients;
  unsigned int max_nick_length;
  unsigned int max_topic_length;
  unsigned int libgeoip_database_options;
  unsigned int specific_ipv4_vhost;
  unsigned int specific_ipv6_vhost;
  struct irc_ssaddr ip;
  struct irc_ssaddr ip6;
};

struct config_admin_entry
{
  char *name;
  char *description;
  char *email;
};

struct config_log_entry
{
  unsigned int use_logging;
};

extern dlink_list flatten_links;
extern dlink_list connect_items;
extern dlink_list operator_items;
extern struct conf_parser_context conf_parser_ctx;
extern struct config_log_entry ConfigLog;
extern struct config_general_entry ConfigGeneral;
extern struct config_channel_entry ConfigChannel;
extern struct config_serverhide_entry ConfigServerHide;
extern struct config_serverinfo_entry ConfigServerInfo;
extern struct config_admin_entry ConfigAdminInfo;

extern int valid_wild_card_simple(const char *);
extern int valid_wild_card(int, ...);
/* End GLOBAL section */

extern struct MaskItem *conf_make(enum maskitem_type);
extern void read_conf_files(int);
extern int attach_conf(struct Client *, struct MaskItem *);
extern int attach_connect_block(struct Client *, const char *, const char *);
extern int check_client(struct Client *);


extern void detach_conf(struct Client *, enum maskitem_type);
extern struct MaskItem *find_conf_name(dlink_list *, const char *, enum maskitem_type);
extern int conf_connect_allowed(struct irc_ssaddr *, int);
extern const char *oper_privs_as_string(const unsigned int);
extern void split_nuh(struct split_nuh_item *);
extern struct MaskItem *operator_find(const struct Client *, const char *);
extern struct MaskItem *connect_find(const char *, const char *, int (*)(const char *, const char *));
extern void conf_free(struct MaskItem *);
extern void yyerror(const char *);
extern void conf_error_report(const char *);
extern void cleanup_tklines(void *);
extern void conf_rehash(int);
extern void lookup_confhost(struct MaskItem *);
extern void conf_add_class_to_conf(struct MaskItem *, const char *);

extern const char *get_oper_name(const struct Client *);

/* XXX should the parse_aline stuff go into another file ?? */
extern int parse_aline(const char *, struct Client *, int, char **,
                       char **, char **, uintmax_t *, char **, char **);

#define TK_SECONDS 0
#define TK_MINUTES 1
extern uintmax_t valid_tkline(const char *, const int);
extern int match_conf_password(const char *, const struct MaskItem *);

enum { CLEANUP_TKLINES_TIME = 60 };
#endif /* INCLUDED_s_conf_h */
