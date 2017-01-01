/*
 *  ircd-hybrid: an advanced, lightweight Internet Relay Chat Daemon (ircd)
 *
 *  Copyright (c) 2000-2017 ircd-hybrid development team
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

/*! \file event.c
 * \brief Timer based event execution
 * \version $Id$
 */

#include "stdinc.h"
#include "list.h"
#include "ircd.h"
#include "event.h"
#include "rng_mt.h"


static dlink_list event_list;

const dlink_list *
event_get_list(void)
{
  return &event_list;
}

void
event_add(struct event *ev, void *data)
{
  dlink_node *node;

  event_delete(ev);

  ev->data = data;
  ev->next = CurrentTime + ev->when;
  ev->active = 1;

  DLINK_FOREACH(node, event_list.head)
  {
    struct event *e = node->data;

    if (e->next > ev->next)
    {
      dlinkAddBefore(node, ev, &ev->node, &event_list);
      return;
    }
  }

  dlinkAddTail(ev, &ev->node, &event_list);
}

void
event_addish(struct event *ev, void *data)
{
  if (ev->when >= 3)
  {
    const uintmax_t two_third = (2 * ev->when) / 3;

    ev->when = two_third + ((genrand_int32() % 1000) * two_third) / 1000;
  }

  event_add(ev, data);
}

void
event_delete(struct event *ev)
{
  if (!ev->active)
    return;

  dlinkDelete(&ev->node, &event_list);
  ev->active = 0;
}

void
event_run(void)
{
  static uintmax_t last = 0;

  if (last == CurrentTime)
    return;
  last = CurrentTime;

  unsigned int len = dlink_list_length(&event_list);
  while (len-- && dlink_list_length(&event_list))
  {
    struct event *ev = event_list.head->data;

    if (ev->next > CurrentTime)
      break;

    event_delete(ev);

    ev->handler(ev->data);

    if (!ev->oneshot)
      event_add(ev, ev->data);
  }
}

/*
 * void event_set_back_events(uintmax_t by)
 * Input: Time to set back events by.
 * Output: None.
 * Side-effects: Sets back all events by "by" seconds.
 */
void
event_set_back_events(uintmax_t by)
{
  dlink_node *node;

  DLINK_FOREACH(node, event_list.head)
  {
    struct event *ev = node->data;
    ev->next -= by;
  }
}
