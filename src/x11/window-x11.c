/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2001 Havoc Pennington, Anders Carlsson
 * Copyright (C) 2002, 2003 Red Hat, Inc.
 * Copyright (C) 2003 Rob Adams
 * Copyright (C) 2004-2006 Elijah Newren
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

#include "config.h"

#include "window-x11.h"
#include "window-x11-private.h"

#include <string.h>
#include <X11/Xatom.h>
#include <X11/Xlibint.h> /* For display->resource_mask */

#include <X11/extensions/shape.h>

#include <X11/extensions/Xcomposite.h>
#include "core.h"

#include <common.h>
#include <errors.h>
#include <prefs.h>
#include <cobiwm-cursor-tracker.h>

#include "frame.h"
#include "boxes-private.h"
#include "window-private.h"
#include "window-props.h"
#include "xprops.h"
#include "session.h"
#include "workspace-private.h"

#include "backends/x11/cobiwm-backend-x11.h"

struct _CobiwmWindowX11Class
{
  CobiwmWindowClass parent_class;
};

G_DEFINE_TYPE_WITH_PRIVATE (CobiwmWindowX11, cobiwm_window_x11, COBIWM_TYPE_WINDOW)

static void
cobiwm_window_x11_init (CobiwmWindowX11 *window_x11)
{
  window_x11->priv = cobiwm_window_x11_get_instance_private (window_x11);
}

static void
send_icccm_message (CobiwmWindow *window,
                    Atom        atom,
                    guint32     timestamp)
{
  /* This comment and code are from twm, copyright
   * Open Group, Evans & Sutherland, etc.
   */

  /*
   * ICCCM Client Messages - Section 4.2.8 of the ICCCM dictates that all
   * client messages will have the following form:
   *
   *     event type	ClientMessage
   *     message type	_XA_WM_PROTOCOLS
   *     window		tmp->w
   *     format		32
   *     data[0]		message atom
   *     data[1]		time stamp
   */

  XClientMessageEvent ev;

  ev.type = ClientMessage;
  ev.window = window->xwindow;
  ev.message_type = window->display->atom_WM_PROTOCOLS;
  ev.format = 32;
  ev.data.l[0] = atom;
  ev.data.l[1] = timestamp;

  cobiwm_error_trap_push (window->display);
  XSendEvent (window->display->xdisplay,
              window->xwindow, False, 0, (XEvent*) &ev);
  cobiwm_error_trap_pop (window->display);
}

static Window
read_client_leader (CobiwmDisplay *display,
                    Window       xwindow)
{
  Window retval = None;

  cobiwm_prop_get_window (display, xwindow,
                        display->atom_WM_CLIENT_LEADER,
                        &retval);

  return retval;
}

typedef struct
{
  Window leader;
} ClientLeaderData;

static gboolean
find_client_leader_func (CobiwmWindow *ancestor,
                         void       *data)
{
  ClientLeaderData *d;

  d = data;

  d->leader = read_client_leader (ancestor->display,
                                  ancestor->xwindow);

  /* keep going if no client leader found */
  return d->leader == None;
}

static void
update_sm_hints (CobiwmWindow *window)
{
  Window leader;

  window->xclient_leader = None;
  window->sm_client_id = NULL;

  /* If not on the current window, we can get the client
   * leader from transient parents. If we find a client
   * leader, we read the SM_CLIENT_ID from it.
   */
  leader = read_client_leader (window->display, window->xwindow);
  if (leader == None)
    {
      ClientLeaderData d;
      d.leader = None;
      cobiwm_window_foreach_ancestor (window, find_client_leader_func,
                                    &d);
      leader = d.leader;
    }

  if (leader != None)
    {
      char *str;

      window->xclient_leader = leader;

      if (cobiwm_prop_get_latin1_string (window->display, leader,
                                       window->display->atom_SM_CLIENT_ID,
                                       &str))
        {
          window->sm_client_id = g_strdup (str);
          cobiwm_XFree (str);
        }
    }
  else
    {
      cobiwm_verbose ("Didn't find a client leader for %s\n", window->desc);

      if (!cobiwm_prefs_get_disable_workarounds ())
        {
          /* Some broken apps (kdelibs fault?) set SM_CLIENT_ID on the app
           * instead of the client leader
           */
          char *str;

          str = NULL;
          if (cobiwm_prop_get_latin1_string (window->display, window->xwindow,
                                           window->display->atom_SM_CLIENT_ID,
                                           &str))
            {
              if (window->sm_client_id == NULL) /* first time through */
                cobiwm_warning ("Window %s sets SM_CLIENT_ID on itself, instead of on the WM_CLIENT_LEADER window as specified in the ICCCM.\n",
                              window->desc);

              window->sm_client_id = g_strdup (str);
              cobiwm_XFree (str);
            }
        }
    }

  cobiwm_verbose ("Window %s client leader: 0x%lx SM_CLIENT_ID: '%s'\n",
                window->desc, window->xclient_leader,
                window->sm_client_id ? window->sm_client_id : "none");
}

static void
send_configure_notify (CobiwmWindow *window)
{
  CobiwmWindowX11 *window_x11 = COBIWM_WINDOW_X11 (window);
  CobiwmWindowX11Private *priv = cobiwm_window_x11_get_instance_private (window_x11);
  XEvent event;

  g_assert (!window->override_redirect);

  /* from twm */

  event.type = ConfigureNotify;
  event.xconfigure.display = window->display->xdisplay;
  event.xconfigure.event = window->xwindow;
  event.xconfigure.window = window->xwindow;
  event.xconfigure.x = priv->client_rect.x - priv->border_width;
  event.xconfigure.y = priv->client_rect.y - priv->border_width;
  if (window->frame)
    {
      if (window->withdrawn)
        {
          CobiwmFrameBorders borders;
          /* We reparent the client window and put it to the position
           * where the visible top-left of the frame window currently is.
           */

          cobiwm_frame_calc_borders (window->frame, &borders);

          event.xconfigure.x = window->frame->rect.x + borders.invisible.left;
          event.xconfigure.y = window->frame->rect.y + borders.invisible.top;
        }
      else
        {
          /* Need to be in root window coordinates */
          event.xconfigure.x += window->frame->rect.x;
          event.xconfigure.y += window->frame->rect.y;
        }
    }
  event.xconfigure.width = priv->client_rect.width;
  event.xconfigure.height = priv->client_rect.height;
  event.xconfigure.border_width = priv->border_width; /* requested not actual */
  event.xconfigure.above = None; /* FIXME */
  event.xconfigure.override_redirect = False;

  cobiwm_topic (COBIWM_DEBUG_GEOMETRY,
              "Sending synthetic configure notify to %s with x: %d y: %d w: %d h: %d\n",
              window->desc,
              event.xconfigure.x, event.xconfigure.y,
              event.xconfigure.width, event.xconfigure.height);

  cobiwm_error_trap_push (window->display);
  XSendEvent (window->display->xdisplay,
              window->xwindow,
              False, StructureNotifyMask, &event);
  cobiwm_error_trap_pop (window->display);
}

static void
adjust_for_gravity (CobiwmWindow        *window,
                    gboolean           coords_assume_border,
                    int                gravity,
                    CobiwmRectangle     *rect)
{
  CobiwmWindowX11 *window_x11 = COBIWM_WINDOW_X11 (window);
  CobiwmWindowX11Private *priv = cobiwm_window_x11_get_instance_private (window_x11);
  int ref_x, ref_y;
  int bw;
  int child_x, child_y;
  int frame_width, frame_height;
  CobiwmFrameBorders borders;

  /* We're computing position to pass to window_move, which is
   * the position of the client window (StaticGravity basically)
   *
   * (see WM spec description of gravity computation, but note that
   * their formulas assume we're honoring the border width, rather
   * than compensating for having turned it off)
   */

  if (gravity == StaticGravity)
    return;

  if (coords_assume_border)
    bw = priv->border_width;
  else
    bw = 0;

  cobiwm_frame_calc_borders (window->frame, &borders);

  child_x = borders.visible.left;
  child_y = borders.visible.top;
  frame_width = child_x + rect->width + borders.visible.right;
  frame_height = child_y + rect->height + borders.visible.bottom;

  /* Calculate the the reference point, which is the corner of the
   * outer window specified by the gravity. So, NorthEastGravity
   * would have the reference point as the top-right corner of the
   * outer window. */
  ref_x = rect->x;
  ref_y = rect->y;

  switch (gravity)
    {
    case NorthGravity:
    case CenterGravity:
    case SouthGravity:
      ref_x += rect->width / 2 + bw;
      break;
    case NorthEastGravity:
    case EastGravity:
    case SouthEastGravity:
      ref_x += rect->width + bw * 2;
      break;
    default:
      break;
    }

  switch (gravity)
    {
    case WestGravity:
    case CenterGravity:
    case EastGravity:
      ref_y += rect->height / 2 + bw;
      break;
    case SouthWestGravity:
    case SouthGravity:
    case SouthEastGravity:
      ref_y += rect->height + bw * 2;
      break;
    default:
      break;
    }

  /* Find the top-left corner of the outer window from
   * the reference point. */

  rect->x = ref_x;
  rect->y = ref_y;

  switch (gravity)
    {
    case NorthGravity:
    case CenterGravity:
    case SouthGravity:
      rect->x -= frame_width / 2;
      break;
    case NorthEastGravity:
    case EastGravity:
    case SouthEastGravity:
      rect->x -= frame_width;
      break;
    }

  switch (gravity)
    {
    case WestGravity:
    case CenterGravity:
    case EastGravity:
      rect->y -= frame_height / 2;
      break;
    case SouthWestGravity:
    case SouthGravity:
    case SouthEastGravity:
      rect->y -= frame_height;
      break;
    }

  /* Adjust to get the top-left corner of the inner window. */
  rect->x += child_x;
  rect->y += child_y;
}

static void
cobiwm_window_apply_session_info (CobiwmWindow *window,
                                const CobiwmWindowSessionInfo *info)
{
  if (info->stack_position_set)
    {
      cobiwm_topic (COBIWM_DEBUG_SM,
                  "Restoring stack position %d for window %s\n",
                  info->stack_position, window->desc);

      /* FIXME well, I'm not sure how to do this. */
    }

  if (info->minimized_set)
    {
      cobiwm_topic (COBIWM_DEBUG_SM,
                  "Restoring minimized state %d for window %s\n",
                  info->minimized, window->desc);

      if (info->minimized)
        cobiwm_window_minimize (window);
    }

  if (info->maximized_set)
    {
      cobiwm_topic (COBIWM_DEBUG_SM,
                  "Restoring maximized state %d for window %s\n",
                  info->maximized, window->desc);

      if (window->has_maximize_func && info->maximized)
        {
          cobiwm_window_maximize (window, COBIWM_MAXIMIZE_BOTH);

          if (info->saved_rect_set)
            {
              cobiwm_topic (COBIWM_DEBUG_SM,
                          "Restoring saved rect %d,%d %dx%d for window %s\n",
                          info->saved_rect.x,
                          info->saved_rect.y,
                          info->saved_rect.width,
                          info->saved_rect.height,
                          window->desc);

              window->saved_rect.x = info->saved_rect.x;
              window->saved_rect.y = info->saved_rect.y;
              window->saved_rect.width = info->saved_rect.width;
              window->saved_rect.height = info->saved_rect.height;
            }
	}
    }

  if (info->on_all_workspaces_set)
    {
      window->on_all_workspaces_requested = info->on_all_workspaces;
      cobiwm_window_on_all_workspaces_changed (window);
      cobiwm_topic (COBIWM_DEBUG_SM,
                  "Restoring sticky state %d for window %s\n",
                  window->on_all_workspaces_requested, window->desc);
    }

  if (info->workspace_indices)
    {
      GSList *tmp;
      GSList *spaces;

      spaces = NULL;

      tmp = info->workspace_indices;
      while (tmp != NULL)
        {
          CobiwmWorkspace *space;

          space =
            cobiwm_screen_get_workspace_by_index (window->screen,
                                                GPOINTER_TO_INT (tmp->data));

          if (space)
            spaces = g_slist_prepend (spaces, space);

          tmp = tmp->next;
        }

      if (spaces)
        {
          /* XXX: What should we do if there's more than one workspace
           * listed? We only support one workspace for each window.
           *
           * For now, just choose the first one.
           */
          CobiwmWorkspace *workspace = spaces->data;

          cobiwm_window_change_workspace (window, workspace);

          cobiwm_topic (COBIWM_DEBUG_SM,
                      "Restoring saved window %s to workspace %d\n",
                      window->desc,
                      cobiwm_workspace_index (workspace));

          g_slist_free (spaces);
        }
    }

  if (info->geometry_set)
    {
      CobiwmRectangle rect;
      CobiwmMoveResizeFlags flags;
      int gravity;

      window->placed = TRUE; /* don't do placement algorithms later */

      rect.x = info->rect.x;
      rect.y = info->rect.y;

      rect.width = window->size_hints.base_width + info->rect.width * window->size_hints.width_inc;
      rect.height = window->size_hints.base_height + info->rect.height * window->size_hints.height_inc;

      /* Force old gravity, ignoring anything now set */
      window->size_hints.win_gravity = info->gravity;
      gravity = window->size_hints.win_gravity;

      flags = COBIWM_MOVE_RESIZE_MOVE_ACTION | COBIWM_MOVE_RESIZE_RESIZE_ACTION;

      adjust_for_gravity (window, FALSE, gravity, &rect);
      cobiwm_window_client_rect_to_frame_rect (window, &rect, &rect);
      cobiwm_window_move_resize_internal (window, flags, gravity, rect);
    }
}

static void
cobiwm_window_x11_manage (CobiwmWindow *window)
{
  CobiwmDisplay *display = window->display;
  CobiwmWindowX11 *window_x11 = COBIWM_WINDOW_X11 (window);
  CobiwmWindowX11Private *priv = cobiwm_window_x11_get_instance_private (window_x11);

  cobiwm_icon_cache_init (&priv->icon_cache);

  cobiwm_display_register_x_window (display, &window->xwindow, window);

  /* assign the window to its group, or create a new group if needed */
  window->group = NULL;
  window->xgroup_leader = None;
  cobiwm_window_compute_group (window);

  cobiwm_window_load_initial_properties (window);

  if (!window->override_redirect)
    update_sm_hints (window); /* must come after transient_for */

  if (window->decorated)
    cobiwm_window_ensure_frame (window);

  /* Now try applying saved stuff from the session */
  {
    const CobiwmWindowSessionInfo *info;

    info = cobiwm_window_lookup_saved_state (window);

    if (info)
      {
        cobiwm_window_apply_session_info (window, info);
        cobiwm_window_release_saved_state (info);
      }
  }

  /* For override-redirect windows, save the client rect
   * directly. window->rect was assigned from the XWindowAttributes
   * in the main cobiwm_window_shared_new.
   *
   * For normal windows, do a full ConfigureRequest based on the
   * window hints, as that's what the ICCCM says to do.
   */
  priv->client_rect = window->rect;
  window->buffer_rect = window->rect;

  if (!window->override_redirect)
    {
      CobiwmRectangle rect;
      CobiwmMoveResizeFlags flags;
      int gravity = window->size_hints.win_gravity;

      rect.x = window->size_hints.x;
      rect.y = window->size_hints.y;
      rect.width = window->size_hints.width;
      rect.height = window->size_hints.height;

      flags = COBIWM_MOVE_RESIZE_CONFIGURE_REQUEST | COBIWM_MOVE_RESIZE_MOVE_ACTION | COBIWM_MOVE_RESIZE_RESIZE_ACTION;

      adjust_for_gravity (window, TRUE, gravity, &rect);
      cobiwm_window_client_rect_to_frame_rect (window, &rect, &rect);
      cobiwm_window_move_resize_internal (window, flags, gravity, rect);
    }

  cobiwm_window_x11_update_shape_region (window);
  cobiwm_window_x11_update_input_region (window);
}

static void
cobiwm_window_x11_unmanage (CobiwmWindow *window)
{
  CobiwmWindowX11 *window_x11 = COBIWM_WINDOW_X11 (window);
  CobiwmWindowX11Private *priv = cobiwm_window_x11_get_instance_private (window_x11);

  cobiwm_error_trap_push (window->display);

  cobiwm_window_x11_destroy_sync_request_alarm (window);

  if (window->withdrawn)
    {
      /* We need to clean off the window's state so it
       * won't be restored if the app maps it again.
       */
      cobiwm_verbose ("Cleaning state from window %s\n", window->desc);
      XDeleteProperty (window->display->xdisplay,
                       window->xwindow,
                       window->display->atom__NET_WM_DESKTOP);
      XDeleteProperty (window->display->xdisplay,
                       window->xwindow,
                       window->display->atom__NET_WM_STATE);
      XDeleteProperty (window->display->xdisplay,
                       window->xwindow,
                       window->display->atom__NET_WM_FULLSCREEN_MONITORS);
      cobiwm_window_x11_set_wm_state (window);
    }
  else
    {
      /* We need to put WM_STATE so that others will understand it on
       * restart.
       */
      if (!window->minimized)
        cobiwm_window_x11_set_wm_state (window);

      /* If we're unmanaging a window that is not withdrawn, then
       * either (a) cobiwm is exiting, in which case we need to map
       * the window so the next WM will know that it's not Withdrawn,
       * or (b) we want to create a new CobiwmWindow to replace the
       * current one, which will happen automatically if we re-map
       * the X Window.
       */
      XMapWindow (window->display->xdisplay,
                  window->xwindow);
    }

  cobiwm_display_unregister_x_window (window->display, window->xwindow);

  /* Put back anything we messed up */
  if (priv->border_width != 0)
    XSetWindowBorderWidth (window->display->xdisplay,
                           window->xwindow,
                           priv->border_width);

  /* No save set */
  XRemoveFromSaveSet (window->display->xdisplay,
                      window->xwindow);

  /* Even though the window is now unmanaged, we can't unselect events. This
   * window might be a window from this process, like a GdkMenu, in
   * which case it will have pointer events and so forth selected
   * for it by GDK. There's no way to disentangle those events from the events
   * we've selected. Even for a window from a different X client,
   * GDK could also have selected events for it for IPC purposes, so we
   * can't unselect in that case either.
   *
   * Similarly, we can't unselected for events on window->user_time_window.
   * It might be our own GDK focus window, or it might be a window that a
   * different client is using for multiple different things:
   * _NET_WM_USER_TIME_WINDOW and IPC, perhaps.
   */

  if (window->user_time_window != None)
    {
      cobiwm_display_unregister_x_window (window->display,
                                        window->user_time_window);
      window->user_time_window = None;
    }

  if (COBIWM_DISPLAY_HAS_SHAPE (window->display))
    XShapeSelectInput (window->display->xdisplay, window->xwindow, NoEventMask);

  cobiwm_window_ungrab_keys (window);
  cobiwm_display_ungrab_window_buttons (window->display, window->xwindow);
  cobiwm_display_ungrab_focus_window_button (window->display, window);

  cobiwm_error_trap_pop (window->display);

  if (window->frame)
    {
      /* The XReparentWindow call in cobiwm_window_destroy_frame() moves the
       * window so we need to send a configure notify; see bug 399552.  (We
       * also do this just in case a window got unmaximized.)
       */
      send_configure_notify (window);

      cobiwm_window_destroy_frame (window);
    }
}

static void
cobiwm_window_x11_ping (CobiwmWindow *window,
                      guint32     serial)
{
  CobiwmDisplay *display = window->display;

  send_icccm_message (window, display->atom__NET_WM_PING, serial);
}

static void
cobiwm_window_x11_delete (CobiwmWindow *window,
                        guint32     timestamp)
{
  cobiwm_error_trap_push (window->display);
  if (window->delete_window)
    {
      cobiwm_topic (COBIWM_DEBUG_WINDOW_OPS,
                  "Deleting %s with delete_window request\n",
                  window->desc);
      send_icccm_message (window, window->display->atom_WM_DELETE_WINDOW, timestamp);
    }
  else
    {
      cobiwm_topic (COBIWM_DEBUG_WINDOW_OPS,
                  "Deleting %s with explicit kill\n",
                  window->desc);
      XKillClient (window->display->xdisplay, window->xwindow);
    }
  cobiwm_error_trap_pop (window->display);
}

static void
cobiwm_window_x11_kill (CobiwmWindow *window)
{
  cobiwm_topic (COBIWM_DEBUG_WINDOW_OPS,
              "Killing %s brutally\n",
              window->desc);

  if (!cobiwm_window_is_remote (window) &&
      window->net_wm_pid > 0)
    {
      cobiwm_topic (COBIWM_DEBUG_WINDOW_OPS,
                  "Killing %s with kill()\n",
                  window->desc);

      if (kill (window->net_wm_pid, 9) < 0)
        cobiwm_topic (COBIWM_DEBUG_WINDOW_OPS,
                    "Failed to signal %s: %s\n",
                    window->desc, strerror (errno));
    }

  cobiwm_topic (COBIWM_DEBUG_WINDOW_OPS,
              "Disconnecting %s with XKillClient()\n",
              window->desc);

  cobiwm_error_trap_push (window->display);
  XKillClient (window->display->xdisplay, window->xwindow);
  cobiwm_error_trap_pop (window->display);
}

static void
request_take_focus (CobiwmWindow *window,
                    guint32     timestamp)
{
  CobiwmDisplay *display = window->display;

  cobiwm_topic (COBIWM_DEBUG_FOCUS, "WM_TAKE_FOCUS(%s, %u)\n",
              window->desc, timestamp);

  send_icccm_message (window, display->atom_WM_TAKE_FOCUS, timestamp);
}

static void
cobiwm_window_x11_focus (CobiwmWindow *window,
                       guint32     timestamp)
{
  /* For output-only or shaded windows, focus the frame.
   * This seems to result in the client window getting key events
   * though, so I don't know if it's icccm-compliant.
   *
   * Still, we have to do this or keynav breaks for these windows.
   */
  if (window->frame &&
      (window->shaded ||
       !(window->input || window->take_focus)))
    {
      cobiwm_topic (COBIWM_DEBUG_FOCUS,
                  "Focusing frame of %s\n", window->desc);
      cobiwm_display_set_input_focus_window (window->display,
                                           window,
                                           TRUE,
                                           timestamp);
    }
  else
    {
      if (window->input)
        {
          cobiwm_topic (COBIWM_DEBUG_FOCUS,
                      "Setting input focus on %s since input = true\n",
                      window->desc);
          cobiwm_display_set_input_focus_window (window->display,
                                               window,
                                               FALSE,
                                               timestamp);
        }

      if (window->take_focus)
        {
          cobiwm_topic (COBIWM_DEBUG_FOCUS,
                      "Sending WM_TAKE_FOCUS to %s since take_focus = true\n",
                      window->desc);

          if (!window->input)
            {
              /* The "Globally Active Input" window case, where the window
               * doesn't want us to call XSetInputFocus on it, but does
               * want us to send a WM_TAKE_FOCUS.
               *
               * Normally, we want to just leave the focus undisturbed until
               * the window responds to WM_TAKE_FOCUS, but if we're unmanaging
               * the current focus window we *need* to move the focus away, so
               * we focus the no_focus_window now (and set
               * display->focus_window to that) before sending WM_TAKE_FOCUS.
               */
              if (window->display->focus_window != NULL &&
                  window->display->focus_window->unmanaging)
                cobiwm_display_focus_the_no_focus_window (window->display,
                                                        window->screen,
                                                        timestamp);
            }

          request_take_focus (window, timestamp);
        }
    }
}

static void
cobiwm_window_get_client_root_coords (CobiwmWindow    *window,
                                    CobiwmRectangle *rect)
{
  CobiwmWindowX11 *window_x11 = COBIWM_WINDOW_X11 (window);
  CobiwmWindowX11Private *priv = cobiwm_window_x11_get_instance_private (window_x11);

  *rect = priv->client_rect;

  if (window->frame)
    {
      rect->x += window->frame->rect.x;
      rect->y += window->frame->rect.y;
    }
}

static void
cobiwm_window_refresh_resize_popup (CobiwmWindow *window)
{
  CobiwmWindowX11 *window_x11 = COBIWM_WINDOW_X11 (window);
  CobiwmWindowX11Private *priv = cobiwm_window_x11_get_instance_private (window_x11);

  if (priv->showing_resize_popup)
    {
      CobiwmRectangle rect;
      int display_w, display_h;

      cobiwm_window_get_client_root_coords (window, &rect);

      display_w = (rect.width - window->size_hints.base_width);
      if (window->size_hints.width_inc > 0)
        display_w /= window->size_hints.width_inc;

      display_h = (rect.height - window->size_hints.base_height);
      if (window->size_hints.height_inc > 0)
        display_h /= window->size_hints.height_inc;

      cobiwm_display_show_resize_popup (window->display, TRUE, &rect, display_w, display_h);
    }
  else
    {
      cobiwm_display_show_resize_popup (window->display, FALSE, NULL, 0, 0);
    }
}

static void
cobiwm_window_x11_grab_op_began (CobiwmWindow *window,
                               CobiwmGrabOp  op)
{
  CobiwmWindowX11 *window_x11 = COBIWM_WINDOW_X11 (window);
  CobiwmWindowX11Private *priv = cobiwm_window_x11_get_instance_private (window_x11);

  if (cobiwm_grab_op_is_resizing (op))
    {
      if (window->sync_request_counter != None)
        cobiwm_window_x11_create_sync_request_alarm (window);

      if (window->size_hints.width_inc > 2 || window->size_hints.height_inc > 2)
        {
          priv->showing_resize_popup = TRUE;
          cobiwm_window_refresh_resize_popup (window);
        }
    }

  COBIWM_WINDOW_CLASS (cobiwm_window_x11_parent_class)->grab_op_began (window, op);
}

static void
cobiwm_window_x11_grab_op_ended (CobiwmWindow *window,
                               CobiwmGrabOp  op)
{
  CobiwmWindowX11 *window_x11 = COBIWM_WINDOW_X11 (window);
  CobiwmWindowX11Private *priv = cobiwm_window_x11_get_instance_private (window_x11);

  if (priv->showing_resize_popup)
    {
      priv->showing_resize_popup = FALSE;
      cobiwm_window_refresh_resize_popup (window);
    }

  COBIWM_WINDOW_CLASS (cobiwm_window_x11_parent_class)->grab_op_ended (window, op);
}

static void
update_net_frame_extents (CobiwmWindow *window)
{
  unsigned long data[4];
  CobiwmFrameBorders borders;

  cobiwm_frame_calc_borders (window->frame, &borders);
  /* Left */
  data[0] = borders.visible.left;
  /* Right */
  data[1] = borders.visible.right;
  /* Top */
  data[2] = borders.visible.top;
  /* Bottom */
  data[3] = borders.visible.bottom;

  cobiwm_topic (COBIWM_DEBUG_GEOMETRY,
              "Setting _NET_FRAME_EXTENTS on managed window 0x%lx "
 "to left = %lu, right = %lu, top = %lu, bottom = %lu\n",
              window->xwindow, data[0], data[1], data[2], data[3]);

  cobiwm_error_trap_push (window->display);
  XChangeProperty (window->display->xdisplay, window->xwindow,
                   window->display->atom__NET_FRAME_EXTENTS,
                   XA_CARDINAL,
                   32, PropModeReplace, (guchar*) data, 4);
  cobiwm_error_trap_pop (window->display);
}

static gboolean
sync_request_timeout (gpointer data)
{
  CobiwmWindow *window = data;

  window->sync_request_timeout_id = 0;

  /* We have now waited for more than a second for the
   * application to respond to the sync request
   */
  window->disable_sync = TRUE;

  /* Reset the wait serial, so we don't continue freezing
   * window updates
   */
  window->sync_request_wait_serial = 0;
  cobiwm_compositor_sync_updates_frozen (window->display->compositor, window);

  if (window == window->display->grab_window &&
      cobiwm_grab_op_is_resizing (window->display->grab_op))
    {
      cobiwm_window_update_resize (window,
                                 window->display->grab_last_user_action_was_snap,
                                 window->display->grab_latest_motion_x,
                                 window->display->grab_latest_motion_y,
                                 TRUE);
    }

  return FALSE;
}

static void
send_sync_request (CobiwmWindow *window)
{
  XClientMessageEvent ev;
  gint64 wait_serial;

  /* For the old style of _NET_WM_SYNC_REQUEST_COUNTER, we just have to
   * increase the value, but for the new "extended" style we need to
   * pick an even (unfrozen) value sufficiently ahead of the last serial
   * that we received from the client; the same code still works
   * for the old style. The increment of 240 is specified by the EWMH
   * and is (1 second) * (60fps) * (an increment of 4 per frame).
   */
  wait_serial = window->sync_request_serial + 240;

  window->sync_request_wait_serial = wait_serial;

  ev.type = ClientMessage;
  ev.window = window->xwindow;
  ev.message_type = window->display->atom_WM_PROTOCOLS;
  ev.format = 32;
  ev.data.l[0] = window->display->atom__NET_WM_SYNC_REQUEST;
  /* FIXME: cobiwm_display_get_current_time() is bad, but since calls
   * come from cobiwm_window_move_resize_internal (which in turn come
   * from all over), I'm not sure what we can do to fix it.  Do we
   * want to use _roundtrip, though?
   */
  ev.data.l[1] = cobiwm_display_get_current_time (window->display);
  ev.data.l[2] = wait_serial & G_GUINT64_CONSTANT(0xffffffff);
  ev.data.l[3] = wait_serial >> 32;
  ev.data.l[4] = window->extended_sync_request_counter ? 1 : 0;

  /* We don't need to trap errors here as we are already
   * inside an error_trap_push()/pop() pair.
   */
  XSendEvent (window->display->xdisplay,
	      window->xwindow, False, 0, (XEvent*) &ev);

  /* We give the window 1 sec to respond to _NET_WM_SYNC_REQUEST;
   * if this time expires, we consider the window unresponsive
   * and resize it unsynchonized.
   */
  window->sync_request_timeout_id = g_timeout_add (1000,
                                                   sync_request_timeout,
                                                   window);
  g_source_set_name_by_id (window->sync_request_timeout_id,
                           "[cobiwm] sync_request_timeout");

  cobiwm_compositor_sync_updates_frozen (window->display->compositor, window);
}

static unsigned long
cobiwm_window_get_net_wm_desktop (CobiwmWindow *window)
{
  if (window->on_all_workspaces)
    return 0xFFFFFFFF;
  else
    return cobiwm_workspace_index (window->workspace);
}

static void
cobiwm_window_x11_current_workspace_changed (CobiwmWindow *window)
{
  /* FIXME if on more than one workspace, we claim to be "sticky",
   * the WM spec doesn't say what to do here.
   */
  unsigned long data[1];

  if (window->workspace == NULL)
    {
      /* this happens when unmanaging windows */
      return;
    }

  data[0] = cobiwm_window_get_net_wm_desktop (window);

  cobiwm_verbose ("Setting _NET_WM_DESKTOP of %s to %lu\n",
                window->desc, data[0]);

  cobiwm_error_trap_push (window->display);
  XChangeProperty (window->display->xdisplay, window->xwindow,
                   window->display->atom__NET_WM_DESKTOP,
                   XA_CARDINAL,
                   32, PropModeReplace, (guchar*) data, 1);
  cobiwm_error_trap_pop (window->display);
}

static void
cobiwm_window_x11_move_resize_internal (CobiwmWindow                *window,
                                      int                        gravity,
                                      CobiwmRectangle              unconstrained_rect,
                                      CobiwmRectangle              constrained_rect,
                                      CobiwmMoveResizeFlags        flags,
                                      CobiwmMoveResizeResultFlags *result)
{
  CobiwmWindowX11 *window_x11 = COBIWM_WINDOW_X11 (window);
  CobiwmWindowX11Private *priv = cobiwm_window_x11_get_instance_private (window_x11);
  CobiwmFrameBorders borders;
  CobiwmRectangle client_rect;
  int size_dx, size_dy;
  XWindowChanges values;
  unsigned int mask;
  gboolean need_configure_notify;
  gboolean need_move_client = FALSE;
  gboolean need_move_frame = FALSE;
  gboolean need_resize_client = FALSE;
  gboolean need_resize_frame = FALSE;
  gboolean frame_shape_changed = FALSE;
  gboolean configure_frame_first;

  gboolean is_configure_request;

  is_configure_request = (flags & COBIWM_MOVE_RESIZE_CONFIGURE_REQUEST) != 0;

  cobiwm_frame_calc_borders (window->frame, &borders);

  size_dx = constrained_rect.x - window->rect.width;
  size_dy = constrained_rect.y - window->rect.height;

  window->rect = constrained_rect;

  if (window->frame)
    {
      int new_w, new_h;
      int new_x, new_y;

      /* Compute new frame size */
      new_w = window->rect.width + borders.invisible.left + borders.invisible.right;

      if (window->shaded)
        new_h = borders.total.top + borders.total.bottom;
      else
        new_h = window->rect.height + borders.invisible.top + borders.invisible.bottom;

      if (new_w != window->frame->rect.width ||
          new_h != window->frame->rect.height)
        {
          need_resize_frame = TRUE;
          window->frame->rect.width = new_w;
          window->frame->rect.height = new_h;
        }

      /* Compute new frame coords */
      new_x = window->rect.x - borders.invisible.left;
      new_y = window->rect.y - borders.invisible.top;

      if (new_x != window->frame->rect.x ||
          new_y != window->frame->rect.y)
        {
          need_move_frame = TRUE;
          window->frame->rect.x = new_x;
          window->frame->rect.y = new_y;
        }
    }

  /* Calculate the new client rect */
  cobiwm_window_frame_rect_to_client_rect (window, &constrained_rect, &client_rect);

  /* The above client_rect is in root window coordinates. The
   * values we need to pass to XConfigureWindow are in parent
   * coordinates, so if the window is in a frame, we need to
   * correct the x/y positions here. */
  if (window->frame)
    {
      client_rect.x = borders.total.left;
      client_rect.y = borders.total.top;
    }

  if (client_rect.x != priv->client_rect.x ||
      client_rect.y != priv->client_rect.y)
    {
      need_move_client = TRUE;
      priv->client_rect.x = client_rect.x;
      priv->client_rect.y = client_rect.y;
    }

  if (client_rect.width != priv->client_rect.width ||
      client_rect.height != priv->client_rect.height)
    {
      need_resize_client = TRUE;
      priv->client_rect.width = client_rect.width;
      priv->client_rect.height = client_rect.height;
    }

  /* If frame extents have changed, fill in other frame fields and
     change frame's extents property. */
  if (window->frame &&
      (window->frame->child_x != borders.total.left ||
       window->frame->child_y != borders.total.top ||
       window->frame->right_width != borders.total.right ||
       window->frame->bottom_height != borders.total.bottom))
    {
      window->frame->child_x = borders.total.left;
      window->frame->child_y = borders.total.top;
      window->frame->right_width = borders.total.right;
      window->frame->bottom_height = borders.total.bottom;

      update_net_frame_extents (window);
    }

  /* See ICCCM 4.1.5 for when to send ConfigureNotify */

  need_configure_notify = FALSE;

  /* If this is a configure request and we change nothing, then we
   * must send configure notify.
   */
  if  (is_configure_request &&
       !(need_move_client || need_move_frame ||
         need_resize_client || need_resize_frame ||
         priv->border_width != 0))
    need_configure_notify = TRUE;

  /* We must send configure notify if we move but don't resize, since
   * the client window may not get a real event
   */
  if ((need_move_client || need_move_frame) &&
      !(need_resize_client || need_resize_frame))
    need_configure_notify = TRUE;

  /* MapRequest events with a PPosition or UPosition hint with a frame
   * are moved by cobiwm without resizing; send a configure notify
   * in such cases.  See #322840.  (Note that window->constructing is
   * only true iff this call is due to a MapRequest, and when
   * PPosition/UPosition hints aren't set, cobiwm seems to send a
   * ConfigureNotify anyway due to the above code.)
   */
  if (window->constructing && window->frame &&
      ((window->size_hints.flags & PPosition) ||
       (window->size_hints.flags & USPosition)))
    need_configure_notify = TRUE;

  /* The rest of this function syncs our new size/pos with X as
   * efficiently as possible
   */

  /* For nice effect, when growing the window we want to move/resize
   * the frame first, when shrinking the window we want to move/resize
   * the client first. If we grow one way and shrink the other,
   * see which way we're moving "more"
   *
   * Mail from Owen subject "Suggestion: Gravity and resizing from the left"
   * http://mail.gnome.org/archives/wm-spec-list/1999-November/msg00088.html
   *
   * An annoying fact you need to know in this code is that StaticGravity
   * does nothing if you _only_ resize or _only_ move the frame;
   * it must move _and_ resize, otherwise you get NorthWestGravity
   * behavior. The move and resize must actually occur, it is not
   * enough to set CWX | CWWidth but pass in the current size/pos.
   */

  /* Normally, we configure the frame first depending on whether
   * we grow the frame more than we shrink. The idea is to avoid
   * messing up the window contents by having a temporary situation
   * where the frame is smaller than the window. However, if we're
   * cooperating with the client to create an atomic frame upate,
   * and the window is redirected, then we should always update
   * the frame first, since updating the frame will force a new
   * backing pixmap to be allocated, and the old backing pixmap
   * will be left undisturbed for us to paint to the screen until
   * the client finishes redrawing.
   */
  if (window->extended_sync_request_counter)
    configure_frame_first = TRUE;
  else
    configure_frame_first = size_dx + size_dy >= 0;

  if (configure_frame_first && window->frame)
    frame_shape_changed = cobiwm_frame_sync_to_window (window->frame, need_resize_frame);

  values.border_width = 0;
  values.x = client_rect.x;
  values.y = client_rect.y;
  values.width = client_rect.width;
  values.height = client_rect.height;

  mask = 0;
  if (is_configure_request && priv->border_width != 0)
    mask |= CWBorderWidth; /* must force to 0 */
  if (need_move_client)
    mask |= (CWX | CWY);
  if (need_resize_client)
    mask |= (CWWidth | CWHeight);

  if (mask != 0)
    {
      cobiwm_error_trap_push (window->display);

      if (window == window->display->grab_window &&
          cobiwm_grab_op_is_resizing (window->display->grab_op) &&
          !window->disable_sync &&
          window->sync_request_counter != None &&
          window->sync_request_alarm != None &&
          window->sync_request_timeout_id == 0)
        {
          send_sync_request (window);
        }

      XConfigureWindow (window->display->xdisplay,
                        window->xwindow,
                        mask,
                        &values);

      cobiwm_error_trap_pop (window->display);
    }

  if (!configure_frame_first && window->frame)
    frame_shape_changed = cobiwm_frame_sync_to_window (window->frame, need_resize_frame);

  if (window->frame)
    window->buffer_rect = window->frame->rect;
  else
    window->buffer_rect = client_rect;

  if (need_configure_notify)
    send_configure_notify (window);

  if (priv->showing_resize_popup)
    cobiwm_window_refresh_resize_popup (window);

  if (frame_shape_changed)
    *result |= COBIWM_MOVE_RESIZE_RESULT_FRAME_SHAPE_CHANGED;
  if (need_move_client || need_move_frame)
    *result |= COBIWM_MOVE_RESIZE_RESULT_MOVED;
  if (need_resize_client || need_resize_frame)
    *result |= COBIWM_MOVE_RESIZE_RESULT_RESIZED;
}

static gboolean
cobiwm_window_x11_update_struts (CobiwmWindow *window)
{
  GSList *old_struts;
  GSList *new_struts;
  GSList *old_iter, *new_iter;
  uint32_t *struts = NULL;
  int nitems;
  gboolean changed;

  g_return_val_if_fail (!window->override_redirect, FALSE);

  cobiwm_verbose ("Updating struts for %s\n", window->desc);

  old_struts = window->struts;
  new_struts = NULL;

  if (cobiwm_prop_get_cardinal_list (window->display,
                                   window->xwindow,
                                   window->display->atom__NET_WM_STRUT_PARTIAL,
                                   &struts, &nitems))
    {
      if (nitems != 12)
        cobiwm_verbose ("_NET_WM_STRUT_PARTIAL on %s has %d values instead "
                      "of 12\n",
                      window->desc, nitems);
      else
        {
          /* Pull out the strut info for each side in the hint */
          int i;
          for (i=0; i<4; i++)
            {
              CobiwmStrut *temp;
              int thickness, strut_begin, strut_end;

              thickness = struts[i];
              if (thickness == 0)
                continue;
              strut_begin = struts[4+(i*2)];
              strut_end   = struts[4+(i*2)+1];

              temp = g_new (CobiwmStrut, 1);
              temp->side = 1 << i; /* See CobiwmSide def.  Matches nicely, eh? */
              temp->rect = window->screen->rect;
              switch (temp->side)
                {
                case COBIWM_SIDE_RIGHT:
                  temp->rect.x = BOX_RIGHT(temp->rect) - thickness;
                  /* Intentionally fall through without breaking */
                case COBIWM_SIDE_LEFT:
                  temp->rect.width  = thickness;
                  temp->rect.y      = strut_begin;
                  temp->rect.height = strut_end - strut_begin + 1;
                  break;
                case COBIWM_SIDE_BOTTOM:
                  temp->rect.y = BOX_BOTTOM(temp->rect) - thickness;
                  /* Intentionally fall through without breaking */
                case COBIWM_SIDE_TOP:
                  temp->rect.height = thickness;
                  temp->rect.x      = strut_begin;
                  temp->rect.width  = strut_end - strut_begin + 1;
                  break;
                default:
                  g_assert_not_reached ();
                }

              new_struts = g_slist_prepend (new_struts, temp);
            }

          cobiwm_verbose ("_NET_WM_STRUT_PARTIAL struts %u %u %u %u for "
                        "window %s\n",
                        struts[0], struts[1], struts[2], struts[3],
                        window->desc);
        }
      cobiwm_XFree (struts);
    }
  else
    {
      cobiwm_verbose ("No _NET_WM_STRUT property for %s\n",
                    window->desc);
    }

  if (!new_struts &&
      cobiwm_prop_get_cardinal_list (window->display,
                                   window->xwindow,
                                   window->display->atom__NET_WM_STRUT,
                                   &struts, &nitems))
    {
      if (nitems != 4)
        cobiwm_verbose ("_NET_WM_STRUT on %s has %d values instead of 4\n",
                      window->desc, nitems);
      else
        {
          /* Pull out the strut info for each side in the hint */
          int i;
          for (i=0; i<4; i++)
            {
              CobiwmStrut *temp;
              int thickness;

              thickness = struts[i];
              if (thickness == 0)
                continue;

              temp = g_new (CobiwmStrut, 1);
              temp->side = 1 << i;
              temp->rect = window->screen->rect;
              switch (temp->side)
                {
                case COBIWM_SIDE_RIGHT:
                  temp->rect.x = BOX_RIGHT(temp->rect) - thickness;
                  /* Intentionally fall through without breaking */
                case COBIWM_SIDE_LEFT:
                  temp->rect.width  = thickness;
                  break;
                case COBIWM_SIDE_BOTTOM:
                  temp->rect.y = BOX_BOTTOM(temp->rect) - thickness;
                  /* Intentionally fall through without breaking */
                case COBIWM_SIDE_TOP:
                  temp->rect.height = thickness;
                  break;
                default:
                  g_assert_not_reached ();
                }

              new_struts = g_slist_prepend (new_struts, temp);
            }

          cobiwm_verbose ("_NET_WM_STRUT struts %u %u %u %u for window %s\n",
                        struts[0], struts[1], struts[2], struts[3],
                        window->desc);
        }
      cobiwm_XFree (struts);
    }
  else if (!new_struts)
    {
      cobiwm_verbose ("No _NET_WM_STRUT property for %s\n",
                    window->desc);
    }

  /* Determine whether old_struts and new_struts are the same */
  old_iter = old_struts;
  new_iter = new_struts;
  while (old_iter && new_iter)
    {
      CobiwmStrut *old_strut = (CobiwmStrut*) old_iter->data;
      CobiwmStrut *new_strut = (CobiwmStrut*) new_iter->data;

      if (old_strut->side != new_strut->side ||
          !cobiwm_rectangle_equal (&old_strut->rect, &new_strut->rect))
        break;

      old_iter = old_iter->next;
      new_iter = new_iter->next;
    }
  changed = (old_iter != NULL || new_iter != NULL);

  /* Update appropriately */
  cobiwm_free_gslist_and_elements (old_struts);
  window->struts = new_struts;
  return changed;
}

static void
cobiwm_window_x11_get_default_skip_hints (CobiwmWindow *window,
                                        gboolean   *skip_taskbar_out,
                                        gboolean   *skip_pager_out)
{
  CobiwmWindowX11 *window_x11 = COBIWM_WINDOW_X11 (window);
  CobiwmWindowX11Private *priv = cobiwm_window_x11_get_instance_private (window_x11);

  *skip_taskbar_out = priv->wm_state_skip_taskbar;
  *skip_pager_out = priv->wm_state_skip_pager;
}

static gboolean
cobiwm_window_x11_update_icon (CobiwmWindow       *window,
                             cairo_surface_t **icon,
                             cairo_surface_t **mini_icon)
{
  CobiwmWindowX11 *window_x11 = COBIWM_WINDOW_X11 (window);
  CobiwmWindowX11Private *priv = cobiwm_window_x11_get_instance_private (window_x11);

  return cobiwm_read_icons (window->screen,
                          window->xwindow,
                          &priv->icon_cache,
                          priv->wm_hints_pixmap,
                          priv->wm_hints_mask,
                          icon,
                          COBIWM_ICON_WIDTH, COBIWM_ICON_HEIGHT,
                          mini_icon,
                          COBIWM_MINI_ICON_WIDTH, COBIWM_MINI_ICON_HEIGHT);
}

static void
cobiwm_window_x11_update_main_monitor (CobiwmWindow *window)
{
  window->monitor = cobiwm_screen_calculate_monitor_for_window (window->screen,
                                                              window);
}

static void
cobiwm_window_x11_main_monitor_changed (CobiwmWindow *window,
                                      const CobiwmMonitorInfo *old)
{
}

static void
cobiwm_window_x11_class_init (CobiwmWindowX11Class *klass)
{
  CobiwmWindowClass *window_class = COBIWM_WINDOW_CLASS (klass);

  window_class->manage = cobiwm_window_x11_manage;
  window_class->unmanage = cobiwm_window_x11_unmanage;
  window_class->ping = cobiwm_window_x11_ping;
  window_class->delete = cobiwm_window_x11_delete;
  window_class->kill = cobiwm_window_x11_kill;
  window_class->focus = cobiwm_window_x11_focus;
  window_class->grab_op_began = cobiwm_window_x11_grab_op_began;
  window_class->grab_op_ended = cobiwm_window_x11_grab_op_ended;
  window_class->current_workspace_changed = cobiwm_window_x11_current_workspace_changed;
  window_class->move_resize_internal = cobiwm_window_x11_move_resize_internal;
  window_class->update_struts = cobiwm_window_x11_update_struts;
  window_class->get_default_skip_hints = cobiwm_window_x11_get_default_skip_hints;
  window_class->update_icon = cobiwm_window_x11_update_icon;
  window_class->update_main_monitor = cobiwm_window_x11_update_main_monitor;
  window_class->main_monitor_changed = cobiwm_window_x11_main_monitor_changed;
}

void
cobiwm_window_x11_set_net_wm_state (CobiwmWindow *window)
{
  CobiwmWindowX11 *window_x11 = COBIWM_WINDOW_X11 (window);
  CobiwmWindowX11Private *priv = cobiwm_window_x11_get_instance_private (window_x11);
  int i;
  unsigned long data[13];

  i = 0;
  if (window->shaded)
    {
      data[i] = window->display->atom__NET_WM_STATE_SHADED;
      ++i;
    }
  if (priv->wm_state_modal)
    {
      data[i] = window->display->atom__NET_WM_STATE_MODAL;
      ++i;
    }
  if (window->skip_pager)
    {
      data[i] = window->display->atom__NET_WM_STATE_SKIP_PAGER;
      ++i;
    }
  if (window->skip_taskbar)
    {
      data[i] = window->display->atom__NET_WM_STATE_SKIP_TASKBAR;
      ++i;
    }
  if (window->maximized_horizontally)
    {
      data[i] = window->display->atom__NET_WM_STATE_MAXIMIZED_HORZ;
      ++i;
    }
  if (window->maximized_vertically)
    {
      data[i] = window->display->atom__NET_WM_STATE_MAXIMIZED_VERT;
      ++i;
    }
  if (window->fullscreen)
    {
      data[i] = window->display->atom__NET_WM_STATE_FULLSCREEN;
      ++i;
    }
  if (!cobiwm_window_showing_on_its_workspace (window) || window->shaded)
    {
      data[i] = window->display->atom__NET_WM_STATE_HIDDEN;
      ++i;
    }
  if (window->wm_state_above)
    {
      data[i] = window->display->atom__NET_WM_STATE_ABOVE;
      ++i;
    }
  if (window->wm_state_below)
    {
      data[i] = window->display->atom__NET_WM_STATE_BELOW;
      ++i;
    }
  if (window->wm_state_demands_attention)
    {
      data[i] = window->display->atom__NET_WM_STATE_DEMANDS_ATTENTION;
      ++i;
    }
  if (window->on_all_workspaces_requested)
    {
      data[i] = window->display->atom__NET_WM_STATE_STICKY;
      ++i;
    }
  if (cobiwm_window_appears_focused (window))
    {
      data[i] = window->display->atom__NET_WM_STATE_FOCUSED;
      ++i;
    }

  cobiwm_verbose ("Setting _NET_WM_STATE with %d atoms\n", i);

  cobiwm_error_trap_push (window->display);
  XChangeProperty (window->display->xdisplay, window->xwindow,
                   window->display->atom__NET_WM_STATE,
                   XA_ATOM,
                   32, PropModeReplace, (guchar*) data, i);
  cobiwm_error_trap_pop (window->display);

  if (window->fullscreen)
    {
      if (window->fullscreen_monitors[0] >= 0)
        {
          data[0] = cobiwm_screen_monitor_index_to_xinerama_index (window->screen,
                                                                 window->fullscreen_monitors[0]);
          data[1] = cobiwm_screen_monitor_index_to_xinerama_index (window->screen,
                                                                 window->fullscreen_monitors[1]);
          data[2] = cobiwm_screen_monitor_index_to_xinerama_index (window->screen,
                                                                 window->fullscreen_monitors[2]);
          data[3] = cobiwm_screen_monitor_index_to_xinerama_index (window->screen,
                                                                 window->fullscreen_monitors[3]);

          cobiwm_verbose ("Setting _NET_WM_FULLSCREEN_MONITORS\n");
          cobiwm_error_trap_push (window->display);
          XChangeProperty (window->display->xdisplay,
                           window->xwindow,
                           window->display->atom__NET_WM_FULLSCREEN_MONITORS,
                           XA_CARDINAL, 32, PropModeReplace,
                           (guchar*) data, 4);
          cobiwm_error_trap_pop (window->display);
        }
      else
        {
          cobiwm_verbose ("Clearing _NET_WM_FULLSCREEN_MONITORS\n");
          cobiwm_error_trap_push (window->display);
          XDeleteProperty (window->display->xdisplay,
                           window->xwindow,
                           window->display->atom__NET_WM_FULLSCREEN_MONITORS);
          cobiwm_error_trap_pop (window->display);
        }
    }
}

static cairo_region_t *
region_create_from_x_rectangles (const XRectangle *rects,
                                 int n_rects)
{
  int i;
  cairo_rectangle_int_t *cairo_rects = g_newa (cairo_rectangle_int_t, n_rects);

  for (i = 0; i < n_rects; i ++)
    {
      cairo_rects[i].x = rects[i].x;
      cairo_rects[i].y = rects[i].y;
      cairo_rects[i].width = rects[i].width;
      cairo_rects[i].height = rects[i].height;
    }

  return cairo_region_create_rectangles (cairo_rects, n_rects);
}

static void
cobiwm_window_set_input_region (CobiwmWindow     *window,
                              cairo_region_t *region)
{
  if (cairo_region_equal (window->input_region, region))
    return;

  g_clear_pointer (&window->input_region, cairo_region_destroy);

  if (region != NULL)
    window->input_region = cairo_region_reference (region);

  cobiwm_compositor_window_shape_changed (window->display->compositor, window);
}

#if 0
/* Print out a region; useful for debugging */
static void
print_region (cairo_region_t *region)
{
  int n_rects;
  int i;

  n_rects = cairo_region_num_rectangles (region);
  g_print ("[");
  for (i = 0; i < n_rects; i++)
    {
      cairo_rectangle_int_t rect;
      cairo_region_get_rectangle (region, i, &rect);
      g_print ("+%d+%dx%dx%d ",
               rect.x, rect.y, rect.width, rect.height);
    }
  g_print ("]\n");
}
#endif

void
cobiwm_window_x11_update_input_region (CobiwmWindow *window)
{
  cairo_region_t *region = NULL;
  CobiwmWindowX11 *window_x11 = COBIWM_WINDOW_X11 (window);
  CobiwmWindowX11Private *priv = cobiwm_window_x11_get_instance_private (window_x11);

  /* Decorated windows don't have an input region, because
     we don't shape the frame to match the client windows
     (so the events are blocked by the frame anyway)
  */
  if (window->decorated)
    {
      if (window->input_region)
        cobiwm_window_set_input_region (window, NULL);
      return;
    }

  if (COBIWM_DISPLAY_HAS_SHAPE (window->display))
    {
      /* Translate the set of XShape rectangles that we
       * get from the X server to a cairo_region. */
      XRectangle *rects = NULL;
      int n_rects = -1, ordering;

      cobiwm_error_trap_push (window->display);
      rects = XShapeGetRectangles (window->display->xdisplay,
                                   window->xwindow,
                                   ShapeInput,
                                   &n_rects,
                                   &ordering);
      cobiwm_error_trap_pop (window->display);

      /* XXX: The X Shape specification is quite unfortunately specified.
       *
       * By default, the window has a shape the same as its bounding region,
       * which we consider "NULL".
       *
       * If the window sets an empty region, then we'll get n_rects as 0
       * and rects as NULL, which we need to transform back into an empty
       * region.
       *
       * It would be great to have a less-broken extension for this, but
       * hey, it's X11!
       */

      if (n_rects == -1)
        {
          /* We had an error. */
          region = NULL;
        }
      else if (n_rects == 0)
        {
          /* Client set an empty region. */
          region = cairo_region_create ();
        }
      else if (n_rects == 1 &&
               (rects[0].x == 0 &&
                rects[0].y == 0 &&
                rects[0].width == priv->client_rect.width &&
                rects[0].height == priv->client_rect.height))
        {
          /* This is the bounding region case. Keep the
           * region as NULL. */
          region = NULL;
        }
      else
        {
          /* Window has a custom shape. */
          region = region_create_from_x_rectangles (rects, n_rects);
        }

      cobiwm_XFree (rects);
    }

  if (region != NULL)
    {
      cairo_rectangle_int_t client_area;

      client_area.x = 0;
      client_area.y = 0;
      client_area.width = priv->client_rect.width;
      client_area.height = priv->client_rect.height;

      /* The shape we get back from the client may have coordinates
       * outside of the frame. The X SHAPE Extension requires that
       * the overall shape the client provides never exceeds the
       * "bounding rectangle" of the window -- the shape that the
       * window would have gotten if it was unshaped. In our case,
       * this is simply the client area.
       */
      cairo_region_intersect_rectangle (region, &client_area);
    }

  cobiwm_window_set_input_region (window, region);
  cairo_region_destroy (region);
}

static void
cobiwm_window_set_shape_region (CobiwmWindow     *window,
                              cairo_region_t *region)
{
  if (cairo_region_equal (window->shape_region, region))
    return;

  g_clear_pointer (&window->shape_region, cairo_region_destroy);

  if (region != NULL)
    window->shape_region = cairo_region_reference (region);

  cobiwm_compositor_window_shape_changed (window->display->compositor, window);
}

void
cobiwm_window_x11_update_shape_region (CobiwmWindow *window)
{
  CobiwmWindowX11 *window_x11 = COBIWM_WINDOW_X11 (window);
  CobiwmWindowX11Private *priv = cobiwm_window_x11_get_instance_private (window_x11);
  cairo_region_t *region = NULL;

  if (COBIWM_DISPLAY_HAS_SHAPE (window->display))
    {
      /* Translate the set of XShape rectangles that we
       * get from the X server to a cairo_region. */
      XRectangle *rects = NULL;
      int n_rects, ordering;

      int x_bounding, y_bounding, x_clip, y_clip;
      unsigned w_bounding, h_bounding, w_clip, h_clip;
      int bounding_shaped, clip_shaped;

      cobiwm_error_trap_push (window->display);
      XShapeQueryExtents (window->display->xdisplay, window->xwindow,
                          &bounding_shaped, &x_bounding, &y_bounding,
                          &w_bounding, &h_bounding,
                          &clip_shaped, &x_clip, &y_clip,
                          &w_clip, &h_clip);

      if (bounding_shaped)
        {
          rects = XShapeGetRectangles (window->display->xdisplay,
                                       window->xwindow,
                                       ShapeBounding,
                                       &n_rects,
                                       &ordering);
        }
      cobiwm_error_trap_pop (window->display);

      if (rects)
        {
          region = region_create_from_x_rectangles (rects, n_rects);
          XFree (rects);
        }
    }

  if (region != NULL)
    {
      cairo_rectangle_int_t client_area;

      client_area.x = 0;
      client_area.y = 0;
      client_area.width = priv->client_rect.width;
      client_area.height = priv->client_rect.height;

      /* The shape we get back from the client may have coordinates
       * outside of the frame. The X SHAPE Extension requires that
       * the overall shape the client provides never exceeds the
       * "bounding rectangle" of the window -- the shape that the
       * window would have gotten if it was unshaped. In our case,
       * this is simply the client area.
       */
      cairo_region_intersect_rectangle (region, &client_area);

      /* Some applications might explicitly set their bounding region
       * to the client area. Detect these cases, and throw out the
       * bounding region in this case. */
      if (cairo_region_contains_rectangle (region, &client_area) == CAIRO_REGION_OVERLAP_IN)
        g_clear_pointer (&region, cairo_region_destroy);
    }

  cobiwm_window_set_shape_region (window, region);
  cairo_region_destroy (region);
}

/* Generally cobiwm_window_same_application() is a better idea
 * of "sameness", since it handles the case where multiple apps
 * want to look like the same app or the same app wants to look
 * like multiple apps, but in the case of workarounds for legacy
 * applications (which likely aren't setting the group properly
 * anyways), it may be desirable to check this as well.
 */
static gboolean
cobiwm_window_same_client (CobiwmWindow *window,
                         CobiwmWindow *other_window)
{
  int resource_mask = window->display->xdisplay->resource_mask;

  return ((window->xwindow & ~resource_mask) ==
          (other_window->xwindow & ~resource_mask));
}

static void
cobiwm_window_move_resize_request (CobiwmWindow *window,
                                 guint       value_mask,
                                 int         gravity,
                                 int         new_x,
                                 int         new_y,
                                 int         new_width,
                                 int         new_height)
{
  int x, y, width, height;
  gboolean allow_position_change;
  gboolean in_grab_op;
  CobiwmMoveResizeFlags flags;

  /* We ignore configure requests while the user is moving/resizing
   * the window, since these represent the app sucking and fighting
   * the user, most likely due to a bug in the app (e.g. pfaedit
   * seemed to do this)
   *
   * Still have to do the ConfigureNotify and all, but pretend the
   * app asked for the current size/position instead of the new one.
   */
  in_grab_op = (window->display->grab_window == window &&
                cobiwm_grab_op_is_mouse (window->display->grab_op));

  /* it's essential to use only the explicitly-set fields,
   * and otherwise use our current up-to-date position.
   *
   * Otherwise you get spurious position changes when the app changes
   * size, for example, if window->rect is not in sync with the
   * server-side position in effect when the configure request was
   * generated.
   */
  cobiwm_window_get_gravity_position (window,
                                    gravity,
                                    &x, &y);

  allow_position_change = FALSE;

  if (cobiwm_prefs_get_disable_workarounds ())
    {
      if (window->type == COBIWM_WINDOW_DIALOG ||
          window->type == COBIWM_WINDOW_MODAL_DIALOG ||
          window->type == COBIWM_WINDOW_SPLASHSCREEN)
        ; /* No position change for these */
      else if ((window->size_hints.flags & PPosition) ||
               /* USPosition is just stale if window is placed;
                * no --geometry involved here.
                */
               ((window->size_hints.flags & USPosition) &&
                !window->placed))
        allow_position_change = TRUE;
    }
  else
    {
      allow_position_change = TRUE;
    }

  if (in_grab_op)
    allow_position_change = FALSE;

  if (allow_position_change)
    {
      if (value_mask & CWX)
        x = new_x;
      if (value_mask & CWY)
        y = new_y;
      if (value_mask & (CWX | CWY))
        {
          /* Once manually positioned, windows shouldn't be placed
           * by the window manager.
           */
          window->placed = TRUE;
        }
    }
  else
    {
      cobiwm_topic (COBIWM_DEBUG_GEOMETRY,
		  "Not allowing position change for window %s PPosition 0x%lx USPosition 0x%lx type %u\n",
		  window->desc, window->size_hints.flags & PPosition,
		  window->size_hints.flags & USPosition,
		  window->type);
    }

  width = window->rect.width;
  height = window->rect.height;
  if (!in_grab_op)
    {
      if (value_mask & CWWidth)
        width = new_width;

      if (value_mask & CWHeight)
        height = new_height;
    }

  /* ICCCM 4.1.5 */

  /* We're ignoring the value_mask here, since sizes
   * not in the mask will be the current window geometry.
   */
  window->size_hints.x = x;
  window->size_hints.y = y;
  window->size_hints.width = width;
  window->size_hints.height = height;

  /* NOTE: We consider ConfigureRequests to be "user" actions in one
   * way, but not in another.  Explanation of the two cases are in the
   * next two big comments.
   */

  /* The constraints code allows user actions to move windows
   * offscreen, etc., and configure request actions would often send
   * windows offscreen when users don't want it if not constrained
   * (e.g. hitting a dropdown triangle in a fileselector to show more
   * options, which makes the window bigger).  Thus we do not set
   * COBIWM_MOVE_RESIZE_USER_ACTION in flags to the
   * cobiwm_window_move_resize_internal() call.
   */
  flags = COBIWM_MOVE_RESIZE_CONFIGURE_REQUEST;
  if (value_mask & (CWX | CWY))
    flags |= COBIWM_MOVE_RESIZE_MOVE_ACTION;
  if (value_mask & (CWWidth | CWHeight))
    flags |= COBIWM_MOVE_RESIZE_RESIZE_ACTION;

  if (flags & (COBIWM_MOVE_RESIZE_MOVE_ACTION | COBIWM_MOVE_RESIZE_RESIZE_ACTION))
    {
      CobiwmRectangle rect, monitor_rect;

      rect.x = x;
      rect.y = y;
      rect.width = width;
      rect.height = height;

      cobiwm_screen_get_monitor_geometry (window->screen, window->monitor->number, &monitor_rect);

      /* Workaround braindead legacy apps that don't know how to
       * fullscreen themselves properly - don't get fooled by
       * windows which hide their titlebar when maximized or which are
       * client decorated; that's not the same as fullscreen, even
       * if there are no struts making the workarea smaller than
       * the monitor.
       */
      if (cobiwm_prefs_get_force_fullscreen() &&
          !window->hide_titlebar_when_maximized &&
          (window->decorated || !cobiwm_window_is_client_decorated (window)) &&
          cobiwm_rectangle_equal (&rect, &monitor_rect) &&
          window->has_fullscreen_func &&
          !window->fullscreen)
        {
          /*
          cobiwm_topic (COBIWM_DEBUG_GEOMETRY,
          */
          cobiwm_warning (
                      "Treating resize request of legacy application %s as a "
                      "fullscreen request\n",
                      window->desc);
          cobiwm_window_make_fullscreen_internal (window);
        }

      adjust_for_gravity (window, TRUE, gravity, &rect);
      cobiwm_window_client_rect_to_frame_rect (window, &rect, &rect);
      cobiwm_window_move_resize_internal (window, flags, gravity, rect);
    }
}

gboolean
cobiwm_window_x11_configure_request (CobiwmWindow *window,
                                   XEvent     *event)
{
  CobiwmWindowX11 *window_x11 = COBIWM_WINDOW_X11 (window);
  CobiwmWindowX11Private *priv = cobiwm_window_x11_get_instance_private (window_x11);

  /* Note that x, y is the corner of the window border,
   * and width, height is the size of the window inside
   * its border, but that we always deny border requests
   * and give windows a border of 0. But we save the
   * requested border here.
   */
  if (event->xconfigurerequest.value_mask & CWBorderWidth)
    priv->border_width = event->xconfigurerequest.border_width;

  cobiwm_window_move_resize_request(window,
                                  event->xconfigurerequest.value_mask,
                                  window->size_hints.win_gravity,
                                  event->xconfigurerequest.x,
                                  event->xconfigurerequest.y,
                                  event->xconfigurerequest.width,
                                  event->xconfigurerequest.height);

  /* Handle stacking. We only handle raises/lowers, mostly because
   * stack.c really can't deal with anything else.  I guess we'll fix
   * that if a client turns up that really requires it. Only a very
   * few clients even require the raise/lower (and in fact all client
   * attempts to deal with stacking order are essentially broken,
   * since they have no idea what other clients are involved or how
   * the stack looks).
   *
   * I'm pretty sure no interesting client uses TopIf, BottomIf, or
   * Opposite anyway, so the only possible missing thing is
   * Above/Below with a sibling set. For now we just pretend there's
   * never a sibling set and always do the full raise/lower instead of
   * the raise-just-above/below-sibling.
   */
  if (event->xconfigurerequest.value_mask & CWStackMode)
    {
      CobiwmWindow *active_window;
      active_window = window->display->focus_window;
      if (cobiwm_prefs_get_disable_workarounds ())
        {
          cobiwm_topic (COBIWM_DEBUG_STACK,
                      "%s sent an xconfigure stacking request; this is "
                      "broken behavior and the request is being ignored.\n",
                      window->desc);
        }
      else if (active_window &&
               !cobiwm_window_same_application (window, active_window) &&
               !cobiwm_window_same_client (window, active_window) &&
               XSERVER_TIME_IS_BEFORE (window->net_wm_user_time,
                                       active_window->net_wm_user_time))
        {
          cobiwm_topic (COBIWM_DEBUG_STACK,
                      "Ignoring xconfigure stacking request from %s (with "
                      "user_time %u); currently active application is %s (with "
                      "user_time %u).\n",
                      window->desc,
                      window->net_wm_user_time,
                      active_window->desc,
                      active_window->net_wm_user_time);
          if (event->xconfigurerequest.detail == Above)
            cobiwm_window_set_demands_attention(window);
        }
      else
        {
          switch (event->xconfigurerequest.detail)
            {
            case Above:
              cobiwm_window_raise (window);
              break;
            case Below:
              cobiwm_window_lower (window);
              break;
            case TopIf:
            case BottomIf:
            case Opposite:
              break;
            }
        }
    }

  return TRUE;
}

static gboolean
process_property_notify (CobiwmWindow     *window,
                         XPropertyEvent *event)
{
  Window xid = window->xwindow;

  if (cobiwm_is_verbose ()) /* avoid looking up the name if we don't have to */
    {
      char *property_name = XGetAtomName (window->display->xdisplay,
                                          event->atom);

      cobiwm_verbose ("Property notify on %s for %s\n",
                    window->desc, property_name);
      XFree (property_name);
    }

  if (event->atom == window->display->atom__NET_WM_USER_TIME &&
      window->user_time_window)
    {
        xid = window->user_time_window;
    }

  cobiwm_window_reload_property_from_xwindow (window, xid, event->atom, FALSE);

  return TRUE;
}

gboolean
cobiwm_window_x11_property_notify (CobiwmWindow *window,
                                 XEvent     *event)
{
  return process_property_notify (window, &event->xproperty);
}

#define _NET_WM_MOVERESIZE_SIZE_TOPLEFT      0
#define _NET_WM_MOVERESIZE_SIZE_TOP          1
#define _NET_WM_MOVERESIZE_SIZE_TOPRIGHT     2
#define _NET_WM_MOVERESIZE_SIZE_RIGHT        3
#define _NET_WM_MOVERESIZE_SIZE_BOTTOMRIGHT  4
#define _NET_WM_MOVERESIZE_SIZE_BOTTOM       5
#define _NET_WM_MOVERESIZE_SIZE_BOTTOMLEFT   6
#define _NET_WM_MOVERESIZE_SIZE_LEFT         7
#define _NET_WM_MOVERESIZE_MOVE              8
#define _NET_WM_MOVERESIZE_SIZE_KEYBOARD     9
#define _NET_WM_MOVERESIZE_MOVE_KEYBOARD    10
#define _NET_WM_MOVERESIZE_CANCEL           11

static int
query_pressed_buttons (CobiwmWindow *window)
{
  CobiwmCursorTracker *tracker = cobiwm_cursor_tracker_get_for_screen (window->screen);
  ClutterModifierType mods;
  int button = 0;

  cobiwm_cursor_tracker_get_pointer (tracker, NULL, NULL, &mods);

  if (mods & CLUTTER_BUTTON1_MASK)
    button |= 1 << 1;
  if (mods & CLUTTER_BUTTON2_MASK)
    button |= 1 << 2;
  if (mods & CLUTTER_BUTTON3_MASK)
    button |= 1 << 3;

  return button;
}

gboolean
cobiwm_window_x11_client_message (CobiwmWindow *window,
                                XEvent     *event)
{
  CobiwmWindowX11 *window_x11 = COBIWM_WINDOW_X11 (window);
  CobiwmWindowX11Private *priv = cobiwm_window_x11_get_instance_private (window_x11);
  CobiwmDisplay *display;

  display = window->display;

  if (window->override_redirect)
    {
      /* Don't warn here: we could warn on any of the messages below,
       * but we might also receive other client messages that are
       * part of protocols we don't know anything about. So, silently
       * ignoring is simplest.
       */
      return FALSE;
    }

  if (event->xclient.message_type ==
      display->atom__NET_CLOSE_WINDOW)
    {
      guint32 timestamp;

      if (event->xclient.data.l[0] != 0)
	timestamp = event->xclient.data.l[0];
      else
        {
          cobiwm_warning ("Receiving a NET_CLOSE_WINDOW message for %s without "
                        "a timestamp!  This means some buggy (outdated) "
                        "application is on the loose!\n",
                        window->desc);
          timestamp = cobiwm_display_get_current_time (window->display);
        }

      cobiwm_window_delete (window, timestamp);

      return TRUE;
    }
  else if (event->xclient.message_type ==
           display->atom__NET_WM_DESKTOP)
    {
      int space;
      CobiwmWorkspace *workspace;

      space = event->xclient.data.l[0];

      cobiwm_verbose ("Request to move %s to workspace %d\n",
                    window->desc, space);

      workspace =
        cobiwm_screen_get_workspace_by_index (window->screen,
                                            space);

      if (workspace)
        cobiwm_window_change_workspace (window, workspace);
      else if (space == (int) 0xFFFFFFFF)
        cobiwm_window_stick (window);
      else
        cobiwm_verbose ("No such workspace %d for screen\n", space);

      cobiwm_verbose ("Window %s now on_all_workspaces = %d\n",
                    window->desc, window->on_all_workspaces);

      return TRUE;
    }
  else if (event->xclient.message_type ==
           display->atom__NET_WM_STATE)
    {
      gulong action;
      Atom first;
      Atom second;

      action = event->xclient.data.l[0];
      first = event->xclient.data.l[1];
      second = event->xclient.data.l[2];

      if (cobiwm_is_verbose ())
        {
          char *str1;
          char *str2;

          cobiwm_error_trap_push (display);
          str1 = XGetAtomName (display->xdisplay, first);
          if (cobiwm_error_trap_pop_with_return (display) != Success)
            str1 = NULL;

          cobiwm_error_trap_push (display);
          str2 = XGetAtomName (display->xdisplay, second);
          if (cobiwm_error_trap_pop_with_return (display) != Success)
            str2 = NULL;

          cobiwm_verbose ("Request to change _NET_WM_STATE action %lu atom1: %s atom2: %s\n",
                        action,
                        str1 ? str1 : "(unknown)",
                        str2 ? str2 : "(unknown)");

          cobiwm_XFree (str1);
          cobiwm_XFree (str2);
        }

      if (first == display->atom__NET_WM_STATE_SHADED ||
          second == display->atom__NET_WM_STATE_SHADED)
        {
          gboolean shade;
          guint32 timestamp;

          /* Stupid protocol has no timestamp; of course, shading
           * sucks anyway so who really cares that we're forced to do
           * a roundtrip here?
           */
          timestamp = cobiwm_display_get_current_time_roundtrip (window->display);

          shade = (action == _NET_WM_STATE_ADD ||
                   (action == _NET_WM_STATE_TOGGLE && !window->shaded));
          if (shade && window->has_shade_func)
            cobiwm_window_shade (window, timestamp);
          else
            cobiwm_window_unshade (window, timestamp);
        }

      if (first == display->atom__NET_WM_STATE_FULLSCREEN ||
          second == display->atom__NET_WM_STATE_FULLSCREEN)
        {
          gboolean make_fullscreen;

          make_fullscreen = (action == _NET_WM_STATE_ADD ||
                             (action == _NET_WM_STATE_TOGGLE && !window->fullscreen));
          if (make_fullscreen && window->has_fullscreen_func)
            cobiwm_window_make_fullscreen (window);
          else
            cobiwm_window_unmake_fullscreen (window);
        }

      if (first == display->atom__NET_WM_STATE_MAXIMIZED_HORZ ||
          second == display->atom__NET_WM_STATE_MAXIMIZED_HORZ ||
          first == display->atom__NET_WM_STATE_MAXIMIZED_VERT ||
          second == display->atom__NET_WM_STATE_MAXIMIZED_VERT)
        {
          gboolean max;
          CobiwmMaximizeFlags directions = 0;

          max = (action == _NET_WM_STATE_ADD ||
                 (action == _NET_WM_STATE_TOGGLE &&
                  !window->maximized_horizontally));

          if (first == display->atom__NET_WM_STATE_MAXIMIZED_HORZ ||
              second == display->atom__NET_WM_STATE_MAXIMIZED_HORZ)
            directions |= COBIWM_MAXIMIZE_HORIZONTAL;

          if (first == display->atom__NET_WM_STATE_MAXIMIZED_VERT ||
              second == display->atom__NET_WM_STATE_MAXIMIZED_VERT)
            directions |= COBIWM_MAXIMIZE_VERTICAL;

          if (max && window->has_maximize_func)
            {
              if (cobiwm_prefs_get_raise_on_click ())
                cobiwm_window_raise (window);
              cobiwm_window_maximize (window, directions);
            }
          else
            {
              if (cobiwm_prefs_get_raise_on_click ())
                cobiwm_window_raise (window);
              cobiwm_window_unmaximize (window, directions);
            }
        }

      if (first == display->atom__NET_WM_STATE_MODAL ||
          second == display->atom__NET_WM_STATE_MODAL)
        {
          priv->wm_state_modal =
            (action == _NET_WM_STATE_ADD) ||
            (action == _NET_WM_STATE_TOGGLE && !priv->wm_state_modal);

          cobiwm_window_x11_recalc_window_type (window);
          cobiwm_window_queue(window, COBIWM_QUEUE_MOVE_RESIZE);
        }

      if (first == display->atom__NET_WM_STATE_SKIP_PAGER ||
          second == display->atom__NET_WM_STATE_SKIP_PAGER)
        {
          priv->wm_state_skip_pager =
            (action == _NET_WM_STATE_ADD) ||
            (action == _NET_WM_STATE_TOGGLE && !window->skip_pager);

          cobiwm_window_recalc_features (window);
          cobiwm_window_x11_set_net_wm_state (window);
        }

      if (first == display->atom__NET_WM_STATE_SKIP_TASKBAR ||
          second == display->atom__NET_WM_STATE_SKIP_TASKBAR)
        {
          priv->wm_state_skip_taskbar =
            (action == _NET_WM_STATE_ADD) ||
            (action == _NET_WM_STATE_TOGGLE && !window->skip_taskbar);

          cobiwm_window_recalc_features (window);
          cobiwm_window_x11_set_net_wm_state (window);
        }

      if (first == display->atom__NET_WM_STATE_ABOVE ||
          second == display->atom__NET_WM_STATE_ABOVE)
        {
          if ((action == _NET_WM_STATE_ADD) ||
              (action == _NET_WM_STATE_TOGGLE && !window->wm_state_demands_attention))
            cobiwm_window_make_above (window);
          else
            cobiwm_window_unmake_above (window);
        }

      if (first == display->atom__NET_WM_STATE_BELOW ||
          second == display->atom__NET_WM_STATE_BELOW)
        {
          window->wm_state_below =
            (action == _NET_WM_STATE_ADD) ||
            (action == _NET_WM_STATE_TOGGLE && !window->wm_state_below);

          cobiwm_window_update_layer (window);
          cobiwm_window_x11_set_net_wm_state (window);
        }

      if (first == display->atom__NET_WM_STATE_DEMANDS_ATTENTION ||
          second == display->atom__NET_WM_STATE_DEMANDS_ATTENTION)
        {
          if ((action == _NET_WM_STATE_ADD) ||
              (action == _NET_WM_STATE_TOGGLE && !window->wm_state_demands_attention))
            cobiwm_window_set_demands_attention (window);
          else
            cobiwm_window_unset_demands_attention (window);
        }

       if (first == display->atom__NET_WM_STATE_STICKY ||
          second == display->atom__NET_WM_STATE_STICKY)
        {
          if ((action == _NET_WM_STATE_ADD) ||
              (action == _NET_WM_STATE_TOGGLE && !window->on_all_workspaces_requested))
            cobiwm_window_stick (window);
          else
            cobiwm_window_unstick (window);
        }

      return TRUE;
    }
  else if (event->xclient.message_type ==
           display->atom_WM_CHANGE_STATE)
    {
      cobiwm_verbose ("WM_CHANGE_STATE client message, state: %ld\n",
                    event->xclient.data.l[0]);
      if (event->xclient.data.l[0] == IconicState)
        cobiwm_window_minimize (window);

      return TRUE;
    }
  else if (event->xclient.message_type ==
           display->atom__NET_WM_MOVERESIZE)
    {
      int x_root;
      int y_root;
      int action;
      CobiwmGrabOp op;
      int button;
      guint32 timestamp;

      /* _NET_WM_MOVERESIZE messages are almost certainly going to come from
       * clients when users click on the fake "frame" that the client has,
       * thus we should also treat such messages as though it were a
       * "frame action".
       */
      gboolean const frame_action = TRUE;

      x_root = event->xclient.data.l[0];
      y_root = event->xclient.data.l[1];
      action = event->xclient.data.l[2];
      button = event->xclient.data.l[3];

      /* FIXME: What a braindead protocol; no timestamp?!? */
      timestamp = cobiwm_display_get_current_time_roundtrip (display);
      cobiwm_topic (COBIWM_DEBUG_WINDOW_OPS,
                  "Received _NET_WM_MOVERESIZE message on %s, %d,%d action = %d, button %d\n",
                  window->desc,
                  x_root, y_root, action, button);

      op = COBIWM_GRAB_OP_NONE;
      switch (action)
        {
        case _NET_WM_MOVERESIZE_SIZE_TOPLEFT:
          op = COBIWM_GRAB_OP_RESIZING_NW;
          break;
        case _NET_WM_MOVERESIZE_SIZE_TOP:
          op = COBIWM_GRAB_OP_RESIZING_N;
          break;
        case _NET_WM_MOVERESIZE_SIZE_TOPRIGHT:
          op = COBIWM_GRAB_OP_RESIZING_NE;
          break;
        case _NET_WM_MOVERESIZE_SIZE_RIGHT:
          op = COBIWM_GRAB_OP_RESIZING_E;
          break;
        case _NET_WM_MOVERESIZE_SIZE_BOTTOMRIGHT:
          op = COBIWM_GRAB_OP_RESIZING_SE;
          break;
        case _NET_WM_MOVERESIZE_SIZE_BOTTOM:
          op = COBIWM_GRAB_OP_RESIZING_S;
          break;
        case _NET_WM_MOVERESIZE_SIZE_BOTTOMLEFT:
          op = COBIWM_GRAB_OP_RESIZING_SW;
          break;
        case _NET_WM_MOVERESIZE_SIZE_LEFT:
          op = COBIWM_GRAB_OP_RESIZING_W;
          break;
        case _NET_WM_MOVERESIZE_MOVE:
          op = COBIWM_GRAB_OP_MOVING;
          break;
        case _NET_WM_MOVERESIZE_SIZE_KEYBOARD:
          op = COBIWM_GRAB_OP_KEYBOARD_RESIZING_UNKNOWN;
          break;
        case _NET_WM_MOVERESIZE_MOVE_KEYBOARD:
          op = COBIWM_GRAB_OP_KEYBOARD_MOVING;
          break;
        case _NET_WM_MOVERESIZE_CANCEL:
          /* handled below */
          break;
        default:
          break;
        }

      if (action == _NET_WM_MOVERESIZE_CANCEL)
        {
          cobiwm_display_end_grab_op (window->display, timestamp);
        }
      else if (op != COBIWM_GRAB_OP_NONE &&
          ((window->has_move_func && op == COBIWM_GRAB_OP_KEYBOARD_MOVING) ||
           (window->has_resize_func && op == COBIWM_GRAB_OP_KEYBOARD_RESIZING_UNKNOWN)))
        {
          cobiwm_window_begin_grab_op (window, op, frame_action, timestamp);
        }
      else if (op != COBIWM_GRAB_OP_NONE &&
               ((window->has_move_func && op == COBIWM_GRAB_OP_MOVING) ||
               (window->has_resize_func &&
                (op != COBIWM_GRAB_OP_MOVING &&
                 op != COBIWM_GRAB_OP_KEYBOARD_MOVING))))
        {
          int button_mask;

          cobiwm_topic (COBIWM_DEBUG_WINDOW_OPS,
                      "Beginning move/resize with button = %d\n", button);
          cobiwm_display_begin_grab_op (window->display,
                                      window->screen,
                                      window,
                                      op,
                                      FALSE,
                                      frame_action,
                                      button, 0,
                                      timestamp,
                                      x_root,
                                      y_root);

          button_mask = query_pressed_buttons (window);

          if (button == 0)
            {
              /*
               * the button SHOULD already be included in the message
               */
              if ((button_mask & (1 << 1)) != 0)
                button = 1;
              else if ((button_mask & (1 << 2)) != 0)
                button = 2;
              else if ((button_mask & (1 << 3)) != 0)
                button = 3;

              if (button != 0)
                window->display->grab_button = button;
              else
                cobiwm_display_end_grab_op (window->display,
                                          timestamp);
            }
          else
            {
              /* There is a potential race here. If the user presses and
               * releases their mouse button very fast, it's possible for
               * both the ButtonPress and ButtonRelease to be sent to the
               * client before it can get a chance to send _NET_WM_MOVERESIZE
               * to us. When that happens, we'll become stuck in a grab
               * state, as we haven't received a ButtonRelease to cancel the
               * grab.
               *
               * We can solve this by querying after we take the explicit
               * pointer grab -- if the button isn't pressed, we cancel the
               * drag immediately.
               */

              if ((button_mask & (1 << button)) == 0)
                cobiwm_display_end_grab_op (window->display, timestamp);
            }
        }

      return TRUE;
    }
  else if (event->xclient.message_type ==
           display->atom__NET_MOVERESIZE_WINDOW)
    {
      int gravity;
      guint value_mask;

      gravity = (event->xclient.data.l[0] & 0xff);
      value_mask = (event->xclient.data.l[0] & 0xf00) >> 8;
      /* source = (event->xclient.data.l[0] & 0xf000) >> 12; */

      if (gravity == 0)
        gravity = window->size_hints.win_gravity;

      cobiwm_window_move_resize_request(window,
                                      value_mask,
                                      gravity,
                                      event->xclient.data.l[1],  /* x */
                                      event->xclient.data.l[2],  /* y */
                                      event->xclient.data.l[3],  /* width */
                                      event->xclient.data.l[4]); /* height */
    }
  else if (event->xclient.message_type ==
           display->atom__NET_ACTIVE_WINDOW)
    {
      CobiwmClientType source_indication;
      guint32        timestamp;

      cobiwm_verbose ("_NET_ACTIVE_WINDOW request for window '%s', activating\n",
                    window->desc);

      source_indication = event->xclient.data.l[0];
      timestamp = event->xclient.data.l[1];

      if (source_indication > COBIWM_CLIENT_TYPE_MAX_RECOGNIZED)
        source_indication = COBIWM_CLIENT_TYPE_UNKNOWN;

      if (timestamp == 0)
        {
          /* Client using older EWMH _NET_ACTIVE_WINDOW without a timestamp */
          cobiwm_warning ("Buggy client sent a _NET_ACTIVE_WINDOW message with a "
                        "timestamp of 0 for %s\n",
                        window->desc);
          timestamp = cobiwm_display_get_current_time (display);
        }

      cobiwm_window_activate_full (window, timestamp, source_indication, NULL);
      return TRUE;
    }
  else if (event->xclient.message_type ==
           display->atom__NET_WM_FULLSCREEN_MONITORS)
    {
      gulong top, bottom, left, right;

      cobiwm_verbose ("_NET_WM_FULLSCREEN_MONITORS request for window '%s'\n",
                    window->desc);

      top = cobiwm_screen_xinerama_index_to_monitor_index (window->screen,
                                                         event->xclient.data.l[0]);
      bottom = cobiwm_screen_xinerama_index_to_monitor_index (window->screen,
                                                            event->xclient.data.l[1]);
      left = cobiwm_screen_xinerama_index_to_monitor_index (window->screen,
                                                          event->xclient.data.l[2]);
      right = cobiwm_screen_xinerama_index_to_monitor_index (window->screen,
                                                           event->xclient.data.l[3]);
      /* source_indication = event->xclient.data.l[4]; */

      cobiwm_window_update_fullscreen_monitors (window, top, bottom, left, right);
    }
  else if (event->xclient.message_type ==
           display->atom__GTK_SHOW_WINDOW_MENU)
    {
      gulong x, y;

      /* l[0] is device_id, which we don't use */
      x = event->xclient.data.l[1];
      y = event->xclient.data.l[2];

      cobiwm_window_show_menu (window, COBIWM_WINDOW_MENU_WM, x, y);
    }

  return FALSE;
}

static void
set_wm_state_on_xwindow (CobiwmDisplay *display,
                         Window       xwindow,
                         int          state)
{
  unsigned long data[2];

  /* Cobiwm doesn't use icon windows, so data[1] should be None
   * according to the ICCCM 2.0 Section 4.1.3.1.
   */
  data[0] = state;
  data[1] = None;

  cobiwm_error_trap_push (display);
  XChangeProperty (display->xdisplay, xwindow,
                   display->atom_WM_STATE,
                   display->atom_WM_STATE,
                   32, PropModeReplace, (guchar*) data, 2);
  cobiwm_error_trap_pop (display);
}

void
cobiwm_window_x11_set_wm_state (CobiwmWindow *window)
{
  int state;

  if (window->withdrawn)
    state = WithdrawnState;
  else if (window->iconic)
    state = IconicState;
  else
    state = NormalState;

  set_wm_state_on_xwindow (window->display, window->xwindow, state);
}

/* The COBIWM_WM_CLASS_FILTER environment variable is designed for
 * performance and regression testing environments where we want to do
 * tests with only a limited set of windows and ignore all other windows
 *
 * When it is set to a comma separated list of WM_CLASS class names, all
 * windows not matching the list will be ignored.
 *
 * Returns TRUE if window has been filtered out and should be ignored.
 */
static gboolean
maybe_filter_xwindow (CobiwmDisplay       *display,
                      Window             xwindow,
                      gboolean           must_be_viewable,
                      XWindowAttributes *attrs)
{
  static char **filter_wm_classes = NULL;
  static gboolean initialized = FALSE;
  XClassHint class_hint;
  gboolean filtered;
  Status success;
  int i;

  if (!initialized)
    {
      const char *filter_string = g_getenv ("COBIWM_WM_CLASS_FILTER");
      if (filter_string)
        filter_wm_classes = g_strsplit (filter_string, ",", -1);
      initialized = TRUE;
    }

  if (!filter_wm_classes || !filter_wm_classes[0])
    return FALSE;

  filtered = TRUE;

  cobiwm_error_trap_push (display);
  success = XGetClassHint (display->xdisplay, xwindow, &class_hint);

  if (success)
    {
      for (i = 0; filter_wm_classes[i]; i++)
        {
          if (strcmp (class_hint.res_class, filter_wm_classes[i]) == 0)
            {
              filtered = FALSE;
              break;
            }
        }

      XFree (class_hint.res_name);
      XFree (class_hint.res_class);
    }

  if (filtered)
    {
      /* We want to try and get the window managed by the next WM that come along,
       * so we need to make sure that windows that are requested to be mapped while
       * Cobiwm is running (!must_be_viewable), or windows already viewable at startup
       * get a non-withdrawn WM_STATE property. Previously unmapped windows are left
       * with whatever WM_STATE property they had.
       */
      if (!must_be_viewable || attrs->map_state == IsViewable)
        {
          uint32_t old_state;

          if (!cobiwm_prop_get_cardinal_with_atom_type (display, xwindow,
                                                      display->atom_WM_STATE,
                                                      display->atom_WM_STATE,
                                                      &old_state))
            old_state = WithdrawnState;

          if (old_state == WithdrawnState)
            set_wm_state_on_xwindow (display, xwindow, NormalState);
        }

      /* Make sure filtered windows are hidden from view */
      XUnmapWindow (display->xdisplay, xwindow);
    }

  cobiwm_error_trap_pop (display);

  return filtered;
}

static gboolean
is_our_xwindow (CobiwmDisplay       *display,
                CobiwmScreen        *screen,
                Window             xwindow,
                XWindowAttributes *attrs)
{
  if (xwindow == screen->no_focus_window)
    return TRUE;

  if (xwindow == screen->wm_sn_selection_window)
    return TRUE;

  if (xwindow == screen->wm_cm_selection_window)
    return TRUE;

  if (xwindow == screen->guard_window)
    return TRUE;

  if (xwindow == screen->composite_overlay_window)
    return TRUE;

  {
    CobiwmBackend *backend = cobiwm_get_backend ();

    if (COBIWM_IS_BACKEND_X11 (backend))
      {
        if (xwindow == cobiwm_backend_x11_get_xwindow (COBIWM_BACKEND_X11 (backend)))
          return TRUE;
      }
  }

  /* Any windows created via cobiwm_create_offscreen_window */
  if (attrs->override_redirect && attrs->x == -100 && attrs->y == -100 && attrs->width == 1 && attrs->height == 1)
    return TRUE;

  return FALSE;
}

#ifdef WITH_VERBOSE_MODE
static const char*
wm_state_to_string (int state)
{
  switch (state)
    {
    case NormalState:
      return "NormalState";
    case IconicState:
      return "IconicState";
    case WithdrawnState:
      return "WithdrawnState";
    }

  return "Unknown";
}
#endif

CobiwmWindow *
cobiwm_window_x11_new (CobiwmDisplay       *display,
                     Window             xwindow,
                     gboolean           must_be_viewable,
                     CobiwmCompEffect     effect)
{
  CobiwmScreen *screen = display->screen;
  XWindowAttributes attrs;
  gulong existing_wm_state;
  CobiwmWindow *window = NULL;
  gulong event_mask;

  cobiwm_verbose ("Attempting to manage 0x%lx\n", xwindow);

  if (cobiwm_display_xwindow_is_a_no_focus_window (display, xwindow))
    {
      cobiwm_verbose ("Not managing no_focus_window 0x%lx\n",
                    xwindow);
      return NULL;
    }

  cobiwm_error_trap_push (display); /* Push a trap over all of window
                                   * creation, to reduce XSync() calls
                                   */
  /*
   * This function executes without any server grabs held. This means that
   * the window could have already gone away, or could go away at any point,
   * so we must be careful with X error handling.
   */

  if (!XGetWindowAttributes (display->xdisplay, xwindow, &attrs))
    {
      cobiwm_verbose ("Failed to get attributes for window 0x%lx\n",
                    xwindow);
      goto error;
    }

  if (attrs.root != screen->xroot)
    {
      cobiwm_verbose ("Not on our screen\n");
      goto error;
    }

  if (is_our_xwindow (display, screen, xwindow, &attrs))
    {
      cobiwm_verbose ("Not managing our own windows\n");
      goto error;
    }

  if (maybe_filter_xwindow (display, xwindow, must_be_viewable, &attrs))
    {
      cobiwm_verbose ("Not managing filtered window\n");
      goto error;
    }

  existing_wm_state = WithdrawnState;
  if (must_be_viewable && attrs.map_state != IsViewable)
    {
      /* Only manage if WM_STATE is IconicState or NormalState */
      uint32_t state;

      /* WM_STATE isn't a cardinal, it's type WM_STATE, but is an int */
      if (!(cobiwm_prop_get_cardinal_with_atom_type (display, xwindow,
                                                   display->atom_WM_STATE,
                                                   display->atom_WM_STATE,
                                                   &state) &&
            (state == IconicState || state == NormalState)))
        {
          cobiwm_verbose ("Deciding not to manage unmapped or unviewable window 0x%lx\n", xwindow);
          goto error;
        }

      existing_wm_state = state;
      cobiwm_verbose ("WM_STATE of %lx = %s\n", xwindow,
                    wm_state_to_string (existing_wm_state));
    }

  /*
   * XAddToSaveSet can only be called on windows created by a different
   * client.  with Cobiwm we want to be able to create manageable windows
   * from within the process (such as a dummy desktop window). As we do not
   * want this call failing to prevent the window from being managed, we
   * call this before creating the return-checked error trap.
   */
  XAddToSaveSet (display->xdisplay, xwindow);

  cobiwm_error_trap_push (display);

  event_mask = PropertyChangeMask;
  if (attrs.override_redirect)
    event_mask |= StructureNotifyMask;

  /* If the window is from this client (a menu, say) we need to augment
   * the event mask, not replace it. For windows from other clients,
   * attrs.your_event_mask will be empty at this point.
   */
  XSelectInput (display->xdisplay, xwindow, attrs.your_event_mask | event_mask);

  {
    unsigned char mask_bits[XIMaskLen (XI_LASTEVENT)] = { 0 };
    XIEventMask mask = { XIAllMasterDevices, sizeof (mask_bits), mask_bits };

    XISetMask (mask.mask, XI_Enter);
    XISetMask (mask.mask, XI_Leave);
    XISetMask (mask.mask, XI_FocusIn);
    XISetMask (mask.mask, XI_FocusOut);

    XISelectEvents (display->xdisplay, xwindow, &mask, 1);
  }

  if (COBIWM_DISPLAY_HAS_SHAPE (display))
    XShapeSelectInput (display->xdisplay, xwindow, ShapeNotifyMask);

  /* Get rid of any borders */
  if (attrs.border_width != 0)
    XSetWindowBorderWidth (display->xdisplay, xwindow, 0);

  /* Get rid of weird gravities */
  if (attrs.win_gravity != NorthWestGravity)
    {
      XSetWindowAttributes set_attrs;

      set_attrs.win_gravity = NorthWestGravity;

      XChangeWindowAttributes (display->xdisplay,
                               xwindow,
                               CWWinGravity,
                               &set_attrs);
    }

  if (cobiwm_error_trap_pop_with_return (display) != Success)
    {
      cobiwm_verbose ("Window 0x%lx disappeared just as we tried to manage it\n",
                    xwindow);
      goto error;
    }

  window = _cobiwm_window_shared_new (display,
                                    screen,
                                    COBIWM_WINDOW_CLIENT_TYPE_X11,
                                    NULL,
                                    xwindow,
                                    existing_wm_state,
                                    effect,
                                    &attrs);

  CobiwmWindowX11 *window_x11 = COBIWM_WINDOW_X11 (window);
  CobiwmWindowX11Private *priv = cobiwm_window_x11_get_instance_private (window_x11);

  priv->border_width = attrs.border_width;

  cobiwm_window_grab_keys (window);
  if (window->type != COBIWM_WINDOW_DOCK && !window->override_redirect)
    {
      cobiwm_display_grab_window_buttons (window->display, window->xwindow);
      cobiwm_display_grab_focus_window_button (window->display, window);
    }

  cobiwm_error_trap_pop (display); /* pop the XSync()-reducing trap */
  return window;

error:
  cobiwm_error_trap_pop (display);
  return NULL;
}

void
cobiwm_window_x11_recalc_window_type (CobiwmWindow *window)
{
  CobiwmWindowX11 *window_x11 = COBIWM_WINDOW_X11 (window);
  CobiwmWindowX11Private *priv = cobiwm_window_x11_get_instance_private (window_x11);
  CobiwmWindowType type;

  if (priv->type_atom != None)
    {
      if (priv->type_atom  == window->display->atom__NET_WM_WINDOW_TYPE_DESKTOP)
        type = COBIWM_WINDOW_DESKTOP;
      else if (priv->type_atom  == window->display->atom__NET_WM_WINDOW_TYPE_DOCK)
        type = COBIWM_WINDOW_DOCK;
      else if (priv->type_atom  == window->display->atom__NET_WM_WINDOW_TYPE_TOOLBAR)
        type = COBIWM_WINDOW_TOOLBAR;
      else if (priv->type_atom  == window->display->atom__NET_WM_WINDOW_TYPE_MENU)
        type = COBIWM_WINDOW_MENU;
      else if (priv->type_atom  == window->display->atom__NET_WM_WINDOW_TYPE_UTILITY)
        type = COBIWM_WINDOW_UTILITY;
      else if (priv->type_atom  == window->display->atom__NET_WM_WINDOW_TYPE_SPLASH)
        type = COBIWM_WINDOW_SPLASHSCREEN;
      else if (priv->type_atom  == window->display->atom__NET_WM_WINDOW_TYPE_DIALOG)
        type = COBIWM_WINDOW_DIALOG;
      else if (priv->type_atom  == window->display->atom__NET_WM_WINDOW_TYPE_NORMAL)
        type = COBIWM_WINDOW_NORMAL;
      /* The below are *typically* override-redirect windows, but the spec does
       * not disallow using them for managed windows.
       */
      else if (priv->type_atom  == window->display->atom__NET_WM_WINDOW_TYPE_DROPDOWN_MENU)
        type = COBIWM_WINDOW_DROPDOWN_MENU;
      else if (priv->type_atom  == window->display->atom__NET_WM_WINDOW_TYPE_POPUP_MENU)
        type = COBIWM_WINDOW_POPUP_MENU;
      else if (priv->type_atom  == window->display->atom__NET_WM_WINDOW_TYPE_TOOLTIP)
        type = COBIWM_WINDOW_TOOLTIP;
      else if (priv->type_atom  == window->display->atom__NET_WM_WINDOW_TYPE_NOTIFICATION)
        type = COBIWM_WINDOW_NOTIFICATION;
      else if (priv->type_atom  == window->display->atom__NET_WM_WINDOW_TYPE_COMBO)
        type = COBIWM_WINDOW_COMBO;
      else if (priv->type_atom  == window->display->atom__NET_WM_WINDOW_TYPE_DND)
        type = COBIWM_WINDOW_DND;
      else
        {
          char *atom_name;

          /*
           * Fallback on a normal type, and print warning. Don't abort.
           */
          type = COBIWM_WINDOW_NORMAL;

          cobiwm_error_trap_push (window->display);
          atom_name = XGetAtomName (window->display->xdisplay,
                                    priv->type_atom);
          cobiwm_error_trap_pop (window->display);

          cobiwm_warning ("Unrecognized type atom [%s] set for %s \n",
                        atom_name ? atom_name : "unknown",
                        window->desc);

          if (atom_name)
            XFree (atom_name);
        }
    }
  else if (window->transient_for != NULL)
    {
      type = COBIWM_WINDOW_DIALOG;
    }
  else
    {
      type = COBIWM_WINDOW_NORMAL;
    }

  if (type == COBIWM_WINDOW_DIALOG && priv->wm_state_modal)
    type = COBIWM_WINDOW_MODAL_DIALOG;

  /* We don't want to allow override-redirect windows to have decorated-window
   * types since that's just confusing.
   */
  if (window->override_redirect)
    {
      switch (type)
        {
        /* Decorated types */
        case COBIWM_WINDOW_NORMAL:
        case COBIWM_WINDOW_DIALOG:
        case COBIWM_WINDOW_MODAL_DIALOG:
        case COBIWM_WINDOW_MENU:
        case COBIWM_WINDOW_UTILITY:
          type = COBIWM_WINDOW_OVERRIDE_OTHER;
          break;
        /* Undecorated types, normally not override-redirect */
        case COBIWM_WINDOW_DESKTOP:
        case COBIWM_WINDOW_DOCK:
        case COBIWM_WINDOW_TOOLBAR:
        case COBIWM_WINDOW_SPLASHSCREEN:
        /* Undecorated types, normally override-redirect types */
        case COBIWM_WINDOW_DROPDOWN_MENU:
        case COBIWM_WINDOW_POPUP_MENU:
        case COBIWM_WINDOW_TOOLTIP:
        case COBIWM_WINDOW_NOTIFICATION:
        case COBIWM_WINDOW_COMBO:
        case COBIWM_WINDOW_DND:
        /* To complete enum */
        case COBIWM_WINDOW_OVERRIDE_OTHER:
          break;
        }
    }

  cobiwm_verbose ("Calculated type %u for %s, old type %u\n",
                type, window->desc, type);
  cobiwm_window_set_type (window, type);
}

/**
 * cobiwm_window_x11_configure_notify: (skip)
 * @window: a #CobiwmWindow
 * @event: a #XConfigureEvent
 *
 * This is used to notify us of an unrequested configuration
 * (only applicable to override redirect windows)
 */
void
cobiwm_window_x11_configure_notify (CobiwmWindow      *window,
                                  XConfigureEvent *event)
{
  CobiwmWindowX11 *window_x11 = COBIWM_WINDOW_X11 (window);
  CobiwmWindowX11Private *priv = cobiwm_window_x11_get_instance_private (window_x11);

  g_assert (window->override_redirect);
  g_assert (window->frame == NULL);

  window->rect.x = event->x;
  window->rect.y = event->y;
  window->rect.width = event->width;
  window->rect.height = event->height;

  priv->client_rect = window->rect;
  window->buffer_rect = window->rect;

  cobiwm_window_update_monitor (window, FALSE);

  /* Whether an override-redirect window is considered fullscreen depends
   * on its geometry.
   */
  if (window->override_redirect)
    cobiwm_screen_queue_check_fullscreen (window->screen);

  if (!event->override_redirect && !event->send_event)
    cobiwm_warning ("Unhandled change of windows override redirect status\n");

  cobiwm_compositor_sync_window_geometry (window->display->compositor, window, FALSE);
}

void
cobiwm_window_x11_set_allowed_actions_hint (CobiwmWindow *window)
{
#define MAX_N_ACTIONS 12
  unsigned long data[MAX_N_ACTIONS];
  int i;

  i = 0;
  if (window->has_move_func)
    {
      data[i] = window->display->atom__NET_WM_ACTION_MOVE;
      ++i;
    }
  if (window->has_resize_func)
    {
      data[i] = window->display->atom__NET_WM_ACTION_RESIZE;
      ++i;
    }
  if (window->has_fullscreen_func)
    {
      data[i] = window->display->atom__NET_WM_ACTION_FULLSCREEN;
      ++i;
    }
  if (window->has_minimize_func)
    {
      data[i] = window->display->atom__NET_WM_ACTION_MINIMIZE;
      ++i;
    }
  if (window->has_shade_func)
    {
      data[i] = window->display->atom__NET_WM_ACTION_SHADE;
      ++i;
    }
  /* sticky according to EWMH is different from cobiwm's sticky;
   * cobiwm doesn't support EWMH sticky
   */
  if (window->has_maximize_func)
    {
      data[i] = window->display->atom__NET_WM_ACTION_MAXIMIZE_HORZ;
      ++i;
      data[i] = window->display->atom__NET_WM_ACTION_MAXIMIZE_VERT;
      ++i;
    }
  /* We always allow this */
  data[i] = window->display->atom__NET_WM_ACTION_CHANGE_DESKTOP;
  ++i;
  if (window->has_close_func)
    {
      data[i] = window->display->atom__NET_WM_ACTION_CLOSE;
      ++i;
    }

  /* I guess we always allow above/below operations */
  data[i] = window->display->atom__NET_WM_ACTION_ABOVE;
  ++i;
  data[i] = window->display->atom__NET_WM_ACTION_BELOW;
  ++i;

  g_assert (i <= MAX_N_ACTIONS);

  cobiwm_verbose ("Setting _NET_WM_ALLOWED_ACTIONS with %d atoms\n", i);

  cobiwm_error_trap_push (window->display);
  XChangeProperty (window->display->xdisplay, window->xwindow,
                   window->display->atom__NET_WM_ALLOWED_ACTIONS,
                   XA_ATOM,
                   32, PropModeReplace, (guchar*) data, i);
  cobiwm_error_trap_pop (window->display);
#undef MAX_N_ACTIONS
}

void
cobiwm_window_x11_create_sync_request_alarm (CobiwmWindow *window)
{
  XSyncAlarmAttributes values;
  XSyncValue init;

  if (window->sync_request_counter == None ||
      window->sync_request_alarm != None)
    return;

  cobiwm_error_trap_push (window->display);

  /* In the new (extended style), the counter value is initialized by
   * the client before mapping the window. In the old style, we're
   * responsible for setting the initial value of the counter.
   */
  if (window->extended_sync_request_counter)
    {
      if (!XSyncQueryCounter(window->display->xdisplay,
                             window->sync_request_counter,
                             &init))
        {
          cobiwm_error_trap_pop_with_return (window->display);
          window->sync_request_counter = None;
          return;
        }

      window->sync_request_serial =
        XSyncValueLow32 (init) + ((gint64)XSyncValueHigh32 (init) << 32);
    }
  else
    {
      XSyncIntToValue (&init, 0);
      XSyncSetCounter (window->display->xdisplay,
                       window->sync_request_counter, init);
      window->sync_request_serial = 0;
    }

  values.trigger.counter = window->sync_request_counter;
  values.trigger.test_type = XSyncPositiveComparison;

  /* Initialize to one greater than the current value */
  values.trigger.value_type = XSyncRelative;
  XSyncIntToValue (&values.trigger.wait_value, 1);

  /* After triggering, increment test_value by this until
   * until the test condition is false */
  XSyncIntToValue (&values.delta, 1);

  /* we want events (on by default anyway) */
  values.events = True;

  window->sync_request_alarm = XSyncCreateAlarm (window->display->xdisplay,
                                                 XSyncCACounter |
                                                 XSyncCAValueType |
                                                 XSyncCAValue |
                                                 XSyncCATestType |
                                                 XSyncCADelta |
                                                 XSyncCAEvents,
                                                 &values);

  if (cobiwm_error_trap_pop_with_return (window->display) == Success)
    cobiwm_display_register_sync_alarm (window->display, &window->sync_request_alarm, window);
  else
    {
      window->sync_request_alarm = None;
      window->sync_request_counter = None;
    }
}

void
cobiwm_window_x11_destroy_sync_request_alarm (CobiwmWindow *window)
{
  if (window->sync_request_alarm != None)
    {
      /* Has to be unregistered _before_ clearing the structure field */
      cobiwm_display_unregister_sync_alarm (window->display, window->sync_request_alarm);
      XSyncDestroyAlarm (window->display->xdisplay,
                         window->sync_request_alarm);
      window->sync_request_alarm = None;
    }
}

void
cobiwm_window_x11_update_sync_request_counter (CobiwmWindow *window,
                                             gint64      new_counter_value)
{
  gboolean needs_frame_drawn = FALSE;
  gboolean no_delay_frame = FALSE;

  if (window->extended_sync_request_counter && new_counter_value % 2 == 0)
    {
      needs_frame_drawn = TRUE;
      no_delay_frame = new_counter_value == window->sync_request_serial + 1;
    }

  window->sync_request_serial = new_counter_value;
  cobiwm_compositor_sync_updates_frozen (window->display->compositor, window);

  if (window == window->display->grab_window &&
      cobiwm_grab_op_is_resizing (window->display->grab_op) &&
      new_counter_value >= window->sync_request_wait_serial &&
      (!window->extended_sync_request_counter || new_counter_value % 2 == 0) &&
      window->sync_request_timeout_id)
    {
      cobiwm_topic (COBIWM_DEBUG_RESIZING,
                  "Alarm event received last motion x = %d y = %d\n",
                  window->display->grab_latest_motion_x,
                  window->display->grab_latest_motion_y);

      g_source_remove (window->sync_request_timeout_id);
      window->sync_request_timeout_id = 0;

      /* This means we are ready for another configure;
       * no pointer round trip here, to keep in sync */
      cobiwm_window_update_resize (window,
                                 window->display->grab_last_user_action_was_snap,
                                 window->display->grab_latest_motion_x,
                                 window->display->grab_latest_motion_y,
                                 TRUE);
    }

  /* If sync was previously disabled, turn it back on and hope
   * the application has come to its senses (maybe it was just
   * busy with a pagefault or a long computation).
   */
  window->disable_sync = FALSE;

  if (needs_frame_drawn)
    cobiwm_compositor_queue_frame_drawn (window->display->compositor, window,
                                       no_delay_frame);
}

Window
cobiwm_window_x11_get_toplevel_xwindow (CobiwmWindow *window)
{
  return window->frame ? window->frame->xwindow : window->xwindow;
}
