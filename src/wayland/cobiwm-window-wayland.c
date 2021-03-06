/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2014 Red Hat
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Written by:
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

#include "config.h"

#include "cobiwm-window-wayland.h"

#include <errors.h>
#include <errno.h>
#include <string.h> /* for strerror () */
#include "window-private.h"
#include "boxes-private.h"
#include "stack-tracker.h"
#include "cobiwm-wayland-private.h"
#include "cobiwm-wayland-surface.h"
#include "compositor/cobiwm-surface-actor-wayland.h"

struct _CobiwmWindowWayland
{
  CobiwmWindow parent;

  CobiwmWaylandSerial pending_configure_serial;
  gboolean has_pending_move;
  int pending_move_x;
  int pending_move_y;

  int last_sent_width;
  int last_sent_height;
};

struct _CobiwmWindowWaylandClass
{
  CobiwmWindowClass parent_class;
};

G_DEFINE_TYPE (CobiwmWindowWayland, cobiwm_window_wayland, COBIWM_TYPE_WINDOW)

static void
cobiwm_window_wayland_manage (CobiwmWindow *window)
{
  CobiwmDisplay *display = window->display;

  cobiwm_display_register_wayland_window (display, window);

  {
    cobiwm_stack_tracker_record_add (window->screen->stack_tracker,
                                   window->stamp,
                                   0);
  }

  if (COBIWM_IS_WAYLAND_SURFACE_ROLE_XDG_POPUP (window->surface->role))
    {
      CobiwmWaylandSurface *parent = window->surface->popup.parent;

      g_assert (parent);

      cobiwm_window_set_transient_for (window, parent->window);
      cobiwm_window_set_type (window, COBIWM_WINDOW_DROPDOWN_MENU);
    }
}

static void
cobiwm_window_wayland_unmanage (CobiwmWindow *window)
{
  {
    cobiwm_stack_tracker_record_remove (window->screen->stack_tracker,
                                      window->stamp,
                                      0);
  }

  cobiwm_display_unregister_wayland_window (window->display, window);
}

static void
cobiwm_window_wayland_ping (CobiwmWindow *window,
                          guint32     serial)
{
  cobiwm_wayland_surface_ping (window->surface, serial);
}

static void
cobiwm_window_wayland_delete (CobiwmWindow *window,
                            guint32     timestamp)
{
  cobiwm_wayland_surface_delete (window->surface);
}

static void
cobiwm_window_wayland_kill (CobiwmWindow *window)
{
  CobiwmWaylandSurface *surface = window->surface;
  struct wl_resource *resource = surface->resource;
  pid_t pid;
  uid_t uid;
  gid_t gid;

  wl_client_get_credentials (wl_resource_get_client (resource), &pid, &uid, &gid);
  if (pid > 0)
    {
      cobiwm_topic (COBIWM_DEBUG_WINDOW_OPS,
                  "Killing %s with kill()\n",
                  window->desc);

      if (kill (pid, 9) == 0)
        return;

      cobiwm_topic (COBIWM_DEBUG_WINDOW_OPS,
                  "Failed to signal %s: %s\n",
                  window->desc, strerror (errno));
    }

  /* Send the client an unrecoverable error to kill the client. */
  wl_resource_post_error (resource,
                          WL_DISPLAY_ERROR_NO_MEMORY,
                          "User requested that we kill you. Sorry. Don't take it too personally.");
}

static void
cobiwm_window_wayland_focus (CobiwmWindow *window,
                           guint32     timestamp)
{
  cobiwm_display_set_input_focus_window (window->display,
                                       window,
                                       FALSE,
                                       timestamp);
}

static void
surface_state_changed (CobiwmWindow *window)
{
  CobiwmWindowWayland *wl_window = COBIWM_WINDOW_WAYLAND (window);

  /* don't send notify when the window is being unmanaged */
  if (window->unmanaging)
    return;

  cobiwm_wayland_surface_configure_notify (window->surface,
                                         wl_window->last_sent_width,
                                         wl_window->last_sent_height,
                                         &wl_window->pending_configure_serial);
}

static void
cobiwm_window_wayland_grab_op_began (CobiwmWindow *window,
                                   CobiwmGrabOp  op)
{
  if (cobiwm_grab_op_is_resizing (op))
    surface_state_changed (window);

  COBIWM_WINDOW_CLASS (cobiwm_window_wayland_parent_class)->grab_op_began (window, op);
}

static void
cobiwm_window_wayland_grab_op_ended (CobiwmWindow *window,
                                   CobiwmGrabOp  op)
{
  if (cobiwm_grab_op_is_resizing (op))
    surface_state_changed (window);

  COBIWM_WINDOW_CLASS (cobiwm_window_wayland_parent_class)->grab_op_ended (window, op);
}

static void
cobiwm_window_wayland_move_resize_internal (CobiwmWindow                *window,
                                          int                        gravity,
                                          CobiwmRectangle              unconstrained_rect,
                                          CobiwmRectangle              constrained_rect,
                                          CobiwmMoveResizeFlags        flags,
                                          CobiwmMoveResizeResultFlags *result)
{
  CobiwmWindowWayland *wl_window = COBIWM_WINDOW_WAYLAND (window);
  gboolean can_move_now;
  int configured_width;
  int configured_height;
  int monitor_scale;

  g_assert (window->frame == NULL);

  /* don't do anything if we're dropping the window, see #751847 */
  if (window->unmanaging)
    return;

  /* The scale the window is drawn in might change depending on what monitor it
   * is mainly on. Scale the configured rectangle to be in logical pixel
   * coordinate space so that we can have a scale independent size to pass
   * to the Wayland surface. */
  monitor_scale = cobiwm_window_wayland_get_main_monitor_scale (window);
  configured_width = constrained_rect.width / monitor_scale;
  configured_height = constrained_rect.height / monitor_scale;

  /* For wayland clients, the size is completely determined by the client,
   * and while this allows to avoid some trickery with frames and the resulting
   * lagging, we also need to insist a bit when the constraints would apply
   * a different size than the client decides.
   *
   * Note that this is not generally a problem for normal toplevel windows (the
   * constraints don't see the size hints, or just change the position), but
   * it can be for maximized or fullscreen.
   */

  if (flags & COBIWM_MOVE_RESIZE_WAYLAND_RESIZE)
    {
      /* This is a call to wl_surface_commit(), ignore the constrained_rect and
       * update the real client size to match the buffer size.
       */

      if (window->rect.width != unconstrained_rect.width ||
          window->rect.height != unconstrained_rect.height)
        {
          *result |= COBIWM_MOVE_RESIZE_RESULT_RESIZED;
          window->rect.width = unconstrained_rect.width;
          window->rect.height = unconstrained_rect.height;
        }

      /* This is a commit of an attach. We should move the window to match the
       * new position the client wants. */
      can_move_now = TRUE;
    }
  else
    {
      /* If the size changed, or the state changed, then we have to wait until
       * the client acks our configure before moving the window. */
      if (constrained_rect.width != window->rect.width ||
          constrained_rect.height != window->rect.height ||
          (flags & COBIWM_MOVE_RESIZE_STATE_CHANGED))
        {
          /* If the constrained size is 1x1 and the unconstrained size is 0x0
           * it means that we are trying to resize a window where the client has
           * not yet committed a buffer. The 1x1 constrained size is a result of
           * how the constraints code works. Lets avoid trying to have the
           * client configure itself to draw on a 1x1 surface.
           *
           * We cannot guard against only an empty unconstrained_rect here,
           * because the client may have created a xdg surface without a buffer
           * attached and asked it to be maximized. In such case we should let
           * it know about the expected window geometry of a maximized window,
           * even though there is currently no buffer attached. */
          if (unconstrained_rect.width == 0 &&
              unconstrained_rect.height == 0 &&
              constrained_rect.width == 1 &&
              constrained_rect.height == 1)
            return;

          cobiwm_wayland_surface_configure_notify (window->surface,
                                                 configured_width,
                                                 configured_height,
                                                 &wl_window->pending_configure_serial);

          /* We need to wait until the resize completes before we can move */
          can_move_now = FALSE;
        }
      else
        {
          /* We're just moving the window, so we don't need to wait for a configure
           * and then ack to simply move the window. */
          can_move_now = TRUE;
        }
    }

  wl_window->last_sent_width = configured_width;
  wl_window->last_sent_height = configured_height;

  if (can_move_now)
    {
      int new_x = constrained_rect.x;
      int new_y = constrained_rect.y;

      if (new_x != window->rect.x || new_y != window->rect.y)
        {
          *result |= COBIWM_MOVE_RESIZE_RESULT_MOVED;
          window->rect.x = new_x;
          window->rect.y = new_y;
        }

      int new_buffer_x = new_x - window->custom_frame_extents.left;
      int new_buffer_y = new_y - window->custom_frame_extents.top;

      if (new_buffer_x != window->buffer_rect.x || new_buffer_y != window->buffer_rect.y)
        {
          *result |= COBIWM_MOVE_RESIZE_RESULT_MOVED;
          window->buffer_rect.x = new_buffer_x;
          window->buffer_rect.y = new_buffer_y;
        }
    }
  else
    {
      int new_x = constrained_rect.x;
      int new_y = constrained_rect.y;

      if (new_x != window->rect.x || new_y != window->rect.y)
        {
          *result |= COBIWM_MOVE_RESIZE_RESULT_MOVED;
          wl_window->has_pending_move = TRUE;
          wl_window->pending_move_x = new_x;
          wl_window->pending_move_y = new_y;
        }
    }
}

static void
scale_rect_size (CobiwmRectangle *rect, float scale)
{
  rect->width = (int)(rect->width * scale);
  rect->height = (int)(rect->height * scale);
}

static void
cobiwm_window_wayland_update_main_monitor (CobiwmWindow *window)
{
  const CobiwmMonitorInfo *from;
  const CobiwmMonitorInfo *to;
  const CobiwmMonitorInfo *scaled_new;
  float scale;
  CobiwmRectangle rect;

  /* Require both the current and the new monitor would be the new main monitor,
   * even given the resulting scale the window would end up having. This is
   * needed to avoid jumping back and forth between the new and the old, since
   * changing main monitor may cause the window to be resized so that it no
   * longer have that same new main monitor. */
  from = window->monitor;
  to = cobiwm_screen_calculate_monitor_for_window (window->screen, window);

  if (from == to)
    return;

  /* If we are setting the first output, unsetting the output, or the new has
   * the same scale as the old no need to do any further checking. */
  if (from == NULL || to == NULL || from->scale == to->scale)
    {
      window->monitor = to;
      return;
    }

  /* To avoid a window alternating between two main monitors because scaling
   * changes the main monitor, wait until both the current and the new scale
   * will result in the same main monitor. */
  scale = (float)to->scale / from->scale;
  rect = window->rect;
  scale_rect_size (&rect, scale);
  scaled_new = cobiwm_screen_get_monitor_for_rect (window->screen, &rect);
  if (to != scaled_new)
    return;

  window->monitor = to;
}

static void
cobiwm_window_wayland_main_monitor_changed (CobiwmWindow *window,
                                          const CobiwmMonitorInfo *old)
{
  float scale_factor;
  CobiwmWaylandSurface *surface;

  /* This function makes sure that window geometry, window actor geometry and
   * surface actor geometry gets set according the old and current main monitor
   * scale. If there either is no past or current main monitor, or if the scale
   * didn't change, there is nothing to do. */
  if (old == NULL ||
      window->monitor == NULL ||
      old->scale == window->monitor->scale)
    return;

  /* CobiwmWindow keeps its rectangles in the physical pixel coordinate space.
   * When the main monitor of a window changes, it can cause the corresponding
   * window surfaces to be scaled given the monitor scale, so we need to scale
   * the rectangles in CobiwmWindow accordingly. */

  scale_factor = (float)window->monitor->scale / old->scale;

  /* Window size. */
  scale_rect_size (&window->rect, scale_factor);
  scale_rect_size (&window->unconstrained_rect, scale_factor);
  scale_rect_size (&window->saved_rect, scale_factor);

  /* Window geometry offset (XXX: Need a better place, see
   * cobiwm_window_wayland_move_resize). */
  window->custom_frame_extents.left =
    (int)(scale_factor * window->custom_frame_extents.left);
  window->custom_frame_extents.top =
    (int)(scale_factor * window->custom_frame_extents.top);

  /* Buffer rect. */
  scale_rect_size (&window->buffer_rect, scale_factor);
  window->buffer_rect.x =
    window->rect.x - window->custom_frame_extents.left;
  window->buffer_rect.y =
    window->rect.y - window->custom_frame_extents.top;

  cobiwm_compositor_sync_window_geometry (window->display->compositor,
                                        window,
                                        TRUE);

  /* The surface actor needs to update the scale recursively for itself and all
   * its subsurfaces */
  surface = window->surface;
  if (surface)
    {
      CobiwmSurfaceActorWayland *actor =
        COBIWM_SURFACE_ACTOR_WAYLAND (surface->surface_actor);

      cobiwm_surface_actor_wayland_sync_state_recursive (actor);
    }

  cobiwm_window_emit_size_changed (window);
}

static void
appears_focused_changed (GObject    *object,
                         GParamSpec *pspec,
                         gpointer    user_data)
{
  CobiwmWindow *window = COBIWM_WINDOW (object);
  surface_state_changed (window);
}

static void
cobiwm_window_wayland_init (CobiwmWindowWayland *wl_window)
{
  CobiwmWindow *window = COBIWM_WINDOW (wl_window);

  g_signal_connect (window, "notify::appears-focused",
                    G_CALLBACK (appears_focused_changed), NULL);
}

static void
cobiwm_window_wayland_class_init (CobiwmWindowWaylandClass *klass)
{
  CobiwmWindowClass *window_class = COBIWM_WINDOW_CLASS (klass);

  window_class->manage = cobiwm_window_wayland_manage;
  window_class->unmanage = cobiwm_window_wayland_unmanage;
  window_class->ping = cobiwm_window_wayland_ping;
  window_class->delete = cobiwm_window_wayland_delete;
  window_class->kill = cobiwm_window_wayland_kill;
  window_class->focus = cobiwm_window_wayland_focus;
  window_class->grab_op_began = cobiwm_window_wayland_grab_op_began;
  window_class->grab_op_ended = cobiwm_window_wayland_grab_op_ended;
  window_class->move_resize_internal = cobiwm_window_wayland_move_resize_internal;
  window_class->update_main_monitor = cobiwm_window_wayland_update_main_monitor;
  window_class->main_monitor_changed = cobiwm_window_wayland_main_monitor_changed;
}

CobiwmWindow *
cobiwm_window_wayland_new (CobiwmDisplay        *display,
                         CobiwmWaylandSurface *surface)
{
  XWindowAttributes attrs;
  CobiwmScreen *scr = display->screen;
  CobiwmWindow *window;

  attrs.x = 0;
  attrs.y = 0;
  attrs.width = 0;
  attrs.height = 0;
  attrs.border_width = 0;
  attrs.depth = 24;
  attrs.visual = NULL;
  attrs.root = scr->xroot;
  attrs.class = InputOutput;
  attrs.bit_gravity = NorthWestGravity;
  attrs.win_gravity = NorthWestGravity;
  attrs.backing_store = 0;
  attrs.backing_planes = ~0;
  attrs.backing_pixel = 0;
  attrs.save_under = 0;
  attrs.colormap = 0;
  attrs.map_installed = 1;
  attrs.map_state = IsUnmapped;
  attrs.all_event_masks = ~0;
  attrs.your_event_mask = 0;
  attrs.do_not_propagate_mask = 0;
  attrs.override_redirect = 0;
  attrs.screen = scr->xscreen;

  /* XXX: Note: In the Wayland case we currently still trap X errors while
   * creating a CobiwmWindow because we will still be making various redundant
   * X requests (passing a window xid of None) until we thoroughly audit all
   * the code to make sure it knows about non X based clients...
   */
  cobiwm_error_trap_push (display); /* Push a trap over all of window
                                   * creation, to reduce XSync() calls
                                   */

  window = _cobiwm_window_shared_new (display,
                                    scr,
                                    COBIWM_WINDOW_CLIENT_TYPE_WAYLAND,
                                    surface,
                                    None,
                                    WithdrawnState,
                                    COBIWM_COMP_EFFECT_CREATE,
                                    &attrs);
  window->can_ping = TRUE;

  cobiwm_error_trap_pop (display); /* pop the XSync()-reducing trap */

  return window;
}

static gboolean
should_do_pending_move (CobiwmWindowWayland *wl_window,
                        CobiwmWaylandSerial *acked_configure_serial)
{
  if (!wl_window->has_pending_move)
    return FALSE;

  if (wl_window->pending_configure_serial.set)
    {
      /* If we're waiting for a configure and this isn't an ACK for
       * any configure, then fizzle it out. */
      if (!acked_configure_serial->set)
        return FALSE;

      /* If we're waiting for a configure and this isn't an ACK for
       * the configure we're waiting for, then fizzle it out. */
      if (acked_configure_serial->value != wl_window->pending_configure_serial.value)
        return FALSE;
    }

  return TRUE;
}

int
cobiwm_window_wayland_get_main_monitor_scale (CobiwmWindow *window)
{
  return window->monitor->scale;
}

/**
 * cobiwm_window_move_resize_wayland:
 *
 * Complete a resize operation from a wayland client.
 */
void
cobiwm_window_wayland_move_resize (CobiwmWindow        *window,
                                 CobiwmWaylandSerial *acked_configure_serial,
                                 CobiwmRectangle      new_geom,
                                 int                dx,
                                 int                dy)
{
  CobiwmWindowWayland *wl_window = COBIWM_WINDOW_WAYLAND (window);
  int gravity;
  CobiwmRectangle rect;
  CobiwmMoveResizeFlags flags;
  int monitor_scale;

  /* new_geom is in the logical pixel coordinate space, but CobiwmWindow wants its
   * rects to represent what in turn will end up on the stage, i.e. we need to
   * scale new_geom to physical pixels given what buffer scale and texture scale
   * is in use. */
  monitor_scale = cobiwm_window_wayland_get_main_monitor_scale (window);
  new_geom.x *= monitor_scale;
  new_geom.y *= monitor_scale;
  new_geom.width *= monitor_scale;
  new_geom.height *= monitor_scale;

  /* The (dx, dy) offset is also in logical pixel coordinate space and needs
   * to be scaled in the same way as new_geom. */
  dx *= monitor_scale;
  dy *= monitor_scale;

  /* XXX: Find a better place to store the window geometry offsets. */
  window->custom_frame_extents.left = new_geom.x;
  window->custom_frame_extents.top = new_geom.y;

  flags = COBIWM_MOVE_RESIZE_WAYLAND_RESIZE;

  /* x/y are ignored when we're doing interactive resizing */
  if (!cobiwm_grab_op_is_resizing (window->display->grab_op))
    {
      if (wl_window->has_pending_move && should_do_pending_move (wl_window, acked_configure_serial))
        {
          rect.x = wl_window->pending_move_x;
          rect.y = wl_window->pending_move_y;
          wl_window->has_pending_move = FALSE;
          flags |= COBIWM_MOVE_RESIZE_MOVE_ACTION;
        }
      else
        {
          rect.x = window->rect.x;
          rect.y = window->rect.y;
        }

      if (dx != 0 || dy != 0)
        {
          rect.x += dx;
          rect.y += dy;
          flags |= COBIWM_MOVE_RESIZE_MOVE_ACTION;
        }
    }

  wl_window->pending_configure_serial.set = FALSE;

  rect.width = new_geom.width;
  rect.height = new_geom.height;

  if (rect.width != window->rect.width || rect.height != window->rect.height)
    flags |= COBIWM_MOVE_RESIZE_RESIZE_ACTION;

  gravity = cobiwm_resize_gravity_from_grab_op (window->display->grab_op);
  cobiwm_window_move_resize_internal (window, flags, gravity, rect);
}

void
cobiwm_window_wayland_place_relative_to (CobiwmWindow *window,
                                       CobiwmWindow *other,
                                       int         x,
                                       int         y)
{
  int monitor_scale;

  /* If there is no monitor, we can't position the window reliably. */
  if (!other->monitor)
    return;

  /* Scale the relative coordinate (x, y) from logical pixels to physical
   * pixels. */
  monitor_scale = other->monitor->scale;
  cobiwm_window_move_frame (window, FALSE,
                          other->buffer_rect.x + (x * monitor_scale),
                          other->buffer_rect.y + (y * monitor_scale));
  window->placed = TRUE;
}
