/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2002 Red Hat Inc.
 * Copyright (C) 2003 Rob Adams
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/**
 * SECTION:group
 * @title: CobiwmGroup
 * @short_description: Cobiwm window groups
 *
 */

#include <config.h>
#include <util.h>
#include "group-private.h"
#include "group-props.h"
#include "window-private.h"
#include <window.h>
#include <X11/Xlib-xcb.h>

static CobiwmGroup*
cobiwm_group_new (CobiwmDisplay *display,
                Window       group_leader)
{
  CobiwmGroup *group;
#define N_INITIAL_PROPS 3
  Atom initial_props[N_INITIAL_PROPS];
  int i;

  g_assert (N_INITIAL_PROPS == (int) G_N_ELEMENTS (initial_props));

  group = g_new0 (CobiwmGroup, 1);

  group->display = display;
  group->windows = NULL;
  group->group_leader = group_leader;
  group->refcount = 1; /* owned by caller, hash table has only weak ref */

  xcb_connection_t *xcb_conn = XGetXCBConnection (display->xdisplay);
  xcb_generic_error_t *e;
  g_autofree xcb_get_window_attributes_reply_t *attrs =
    xcb_get_window_attributes_reply (xcb_conn,
                                     xcb_get_window_attributes (xcb_conn, group_leader),
                                     &e);
  if (e)
    return NULL;

  const uint32_t events[] = { attrs->your_event_mask | XCB_EVENT_MASK_PROPERTY_CHANGE };
  xcb_change_window_attributes (xcb_conn, group_leader,
                                XCB_CW_EVENT_MASK, events);

  if (display->groups_by_leader == NULL)
    display->groups_by_leader = g_hash_table_new (cobiwm_unsigned_long_hash,
                                                  cobiwm_unsigned_long_equal);

  g_assert (g_hash_table_lookup (display->groups_by_leader, &group_leader) == NULL);

  g_hash_table_insert (display->groups_by_leader,
                       &group->group_leader,
                       group);

  /* Fill these in the order we want them to be gotten */
  i = 0;
  initial_props[i++] = display->atom_WM_CLIENT_MACHINE;
  initial_props[i++] = display->atom__NET_WM_PID;
  initial_props[i++] = display->atom__NET_STARTUP_ID;
  g_assert (N_INITIAL_PROPS == i);

  cobiwm_group_reload_properties (group, initial_props, N_INITIAL_PROPS);

  cobiwm_topic (COBIWM_DEBUG_GROUPS,
              "Created new group with leader 0x%lx\n",
              group->group_leader);

  return group;
}

static void
cobiwm_group_unref (CobiwmGroup *group)
{
  g_return_if_fail (group->refcount > 0);

  group->refcount -= 1;
  if (group->refcount == 0)
    {
      cobiwm_topic (COBIWM_DEBUG_GROUPS,
                  "Destroying group with leader 0x%lx\n",
                  group->group_leader);

      g_assert (group->display->groups_by_leader != NULL);

      g_hash_table_remove (group->display->groups_by_leader,
                           &group->group_leader);

      /* mop up hash table, this is how it gets freed on display close */
      if (g_hash_table_size (group->display->groups_by_leader) == 0)
        {
          g_hash_table_destroy (group->display->groups_by_leader);
          group->display->groups_by_leader = NULL;
        }

      g_free (group->wm_client_machine);
      g_free (group->startup_id);

      g_free (group);
    }
}

/**
 * cobiwm_window_get_group: (skip)
 * @window: a #CobiwmWindow
 *
 */
CobiwmGroup*
cobiwm_window_get_group (CobiwmWindow *window)
{
  if (window->unmanaging)
    return NULL;

  return window->group;
}

void
cobiwm_window_compute_group (CobiwmWindow* window)
{
  CobiwmGroup *group;
  CobiwmWindow *ancestor;

  /* use window->xwindow if no window->xgroup_leader */

  group = NULL;

  /* Determine the ancestor of the window; its group setting will override the
   * normal grouping rules; see bug 328211.
   */
  ancestor = cobiwm_window_find_root_ancestor (window);

  if (window->display->groups_by_leader)
    {
      if (ancestor != window)
        group = ancestor->group;
      else if (window->xgroup_leader != None)
        group = g_hash_table_lookup (window->display->groups_by_leader,
                                     &window->xgroup_leader);
      else
        group = g_hash_table_lookup (window->display->groups_by_leader,
                                     &window->xwindow);
    }

  if (group != NULL)
    {
      window->group = group;
      group->refcount += 1;
    }
  else
    {
      if (ancestor != window && ancestor->xgroup_leader != None)
        group = cobiwm_group_new (window->display,
                                ancestor->xgroup_leader);
      else if (window->xgroup_leader != None)
        group = cobiwm_group_new (window->display,
                                window->xgroup_leader);
      else
        group = cobiwm_group_new (window->display,
                                window->xwindow);

      window->group = group;
    }

  if (!window->group)
    return;

  window->group->windows = g_slist_prepend (window->group->windows, window);

  cobiwm_topic (COBIWM_DEBUG_GROUPS,
              "Adding %s to group with leader 0x%lx\n",
              window->desc, group->group_leader);
}

static void
remove_window_from_group (CobiwmWindow *window)
{
  if (window->group != NULL)
    {
      cobiwm_topic (COBIWM_DEBUG_GROUPS,
                  "Removing %s from group with leader 0x%lx\n",
                  window->desc, window->group->group_leader);

      window->group->windows =
        g_slist_remove (window->group->windows,
                        window);
      cobiwm_group_unref (window->group);
      window->group = NULL;
    }
}

void
cobiwm_window_group_leader_changed (CobiwmWindow *window)
{
  remove_window_from_group (window);
  cobiwm_window_compute_group (window);
}

void
cobiwm_window_shutdown_group (CobiwmWindow *window)
{
  remove_window_from_group (window);
}

/**
 * cobiwm_display_lookup_group: (skip)
 * @display: a #CobiwmDisplay
 * @group_leader: a X window
 *
 */
CobiwmGroup*
cobiwm_display_lookup_group (CobiwmDisplay *display,
                           Window       group_leader)
{
  CobiwmGroup *group;

  group = NULL;

  if (display->groups_by_leader)
    group = g_hash_table_lookup (display->groups_by_leader,
                                 &group_leader);

  return group;
}

/**
 * cobiwm_group_list_windows:
 * @group: A #CobiwmGroup
 *
 * Returns: (transfer container) (element-type Cobiwm.Window): List of windows
 */
GSList*
cobiwm_group_list_windows (CobiwmGroup *group)
{
  return g_slist_copy (group->windows);
}

void
cobiwm_group_update_layers (CobiwmGroup *group)
{
  GSList *tmp;
  GSList *frozen_stacks;

  if (group->windows == NULL)
    return;

  frozen_stacks = NULL;
  tmp = group->windows;
  while (tmp != NULL)
    {
      CobiwmWindow *window = tmp->data;

      /* we end up freezing the same stack a lot of times,
       * but doesn't hurt anything. have to handle
       * groups that span 2 screens.
       */
      cobiwm_stack_freeze (window->screen->stack);
      frozen_stacks = g_slist_prepend (frozen_stacks, window->screen->stack);

      cobiwm_stack_update_layer (window->screen->stack,
                               window);

      tmp = tmp->next;
    }

  tmp = frozen_stacks;
  while (tmp != NULL)
    {
      cobiwm_stack_thaw (tmp->data);
      tmp = tmp->next;
    }

  g_slist_free (frozen_stacks);
}

const char*
cobiwm_group_get_startup_id (CobiwmGroup *group)
{
  return group->startup_id;
}

/**
 * cobiwm_group_property_notify: (skip)
 * @group: a #CobiwmGroup
 * @event: a X event
 *
 */
gboolean
cobiwm_group_property_notify (CobiwmGroup  *group,
                            XEvent     *event)
{
  cobiwm_group_reload_property (group,
                              event->xproperty.atom);

  return TRUE;

}

int
cobiwm_group_get_size (CobiwmGroup *group)
{
  if (!group)
    return 0;

  return group->refcount;
}

