/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Cobiwm X window decorations */

/*
 * Copyright (C) 2001 Havoc Pennington
 * Copyright (C) 2003, 2004 Red Hat, Inc.
 * Copyright (C) 2005 Elijah Newren
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

#include <config.h>
#include "frame.h"
#include "bell.h"
#include <errors.h>
#include "keybindings-private.h"
#include "backends/x11/cobiwm-backend-x11.h"

#define EVENT_MASK (SubstructureRedirectMask |                     \
                    StructureNotifyMask | SubstructureNotifyMask | \
                    ExposureMask | FocusChangeMask)

void
cobiwm_window_ensure_frame (CobiwmWindow *window)
{
  CobiwmFrame *frame;
  XSetWindowAttributes attrs;
  gulong create_serial;

  if (window->frame)
    return;

  frame = g_new (CobiwmFrame, 1);

  frame->window = window;
  frame->xwindow = None;

  frame->rect = window->rect;
  frame->child_x = 0;
  frame->child_y = 0;
  frame->bottom_height = 0;
  frame->right_width = 0;
  frame->current_cursor = 0;

  frame->is_flashing = FALSE;
  frame->borders_cached = FALSE;

  cobiwm_verbose ("Frame geometry %d,%d  %dx%d\n",
                frame->rect.x, frame->rect.y,
                frame->rect.width, frame->rect.height);

  frame->ui_frame = cobiwm_ui_create_frame (window->screen->ui,
                                          window->display->xdisplay,
                                          frame->window,
                                          window->xvisual,
                                          frame->rect.x,
                                          frame->rect.y,
                                          frame->rect.width,
                                          frame->rect.height,
                                          &create_serial);
  frame->xwindow = frame->ui_frame->xwindow;

  cobiwm_stack_tracker_record_add (window->screen->stack_tracker,
                                 frame->xwindow,
                                 create_serial);

  cobiwm_verbose ("Frame for %s is 0x%lx\n", frame->window->desc, frame->xwindow);
  attrs.event_mask = EVENT_MASK;
  XChangeWindowAttributes (window->display->xdisplay,
			   frame->xwindow, CWEventMask, &attrs);

  cobiwm_display_register_x_window (window->display, &frame->xwindow, window);

  cobiwm_error_trap_push (window->display);
  if (window->mapped)
    {
      window->mapped = FALSE; /* the reparent will unmap the window,
                               * we don't want to take that as a withdraw
                               */
      cobiwm_topic (COBIWM_DEBUG_WINDOW_STATE,
                  "Incrementing unmaps_pending on %s for reparent\n", window->desc);
      window->unmaps_pending += 1;
    }

  cobiwm_stack_tracker_record_remove (window->screen->stack_tracker,
                                    window->xwindow,
                                    XNextRequest (window->display->xdisplay));
  XReparentWindow (window->display->xdisplay,
                   window->xwindow,
                   frame->xwindow,
                   frame->child_x,
                   frame->child_y);
  /* FIXME handle this error */
  cobiwm_error_trap_pop (window->display);

  /* stick frame to the window */
  window->frame = frame;

  /* Now that frame->xwindow is registered with window, we can set its
   * style and background.
   */
  cobiwm_frame_update_style (frame);
  cobiwm_frame_update_title (frame);

  cobiwm_ui_map_frame (frame->window->screen->ui, frame->xwindow);

  {
    CobiwmBackend *backend = cobiwm_get_backend ();
    if (COBIWM_IS_BACKEND_X11 (backend))
      {
        Display *xdisplay = cobiwm_backend_x11_get_xdisplay (COBIWM_BACKEND_X11 (backend));

        /* Since the backend selects for events on another connection,
         * make sure to sync the GTK+ connection to ensure that the
         * frame window has been created on the server at this point. */
        XSync (window->display->xdisplay, False);

        unsigned char mask_bits[XIMaskLen (XI_LASTEVENT)] = { 0 };
        XIEventMask mask = { XIAllMasterDevices, sizeof (mask_bits), mask_bits };

        XISelectEvents (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()),
                        frame->xwindow, &mask, 1);

        XISetMask (mask.mask, XI_ButtonPress);
        XISetMask (mask.mask, XI_ButtonRelease);
        XISetMask (mask.mask, XI_Motion);
        XISetMask (mask.mask, XI_Enter);
        XISetMask (mask.mask, XI_Leave);

        XISelectEvents (xdisplay, frame->xwindow, &mask, 1);
      }
  }

  /* Move keybindings to frame instead of window */
  cobiwm_window_grab_keys (window);
}

void
cobiwm_window_destroy_frame (CobiwmWindow *window)
{
  CobiwmFrame *frame;
  CobiwmFrameBorders borders;

  if (window->frame == NULL)
    return;

  cobiwm_verbose ("Unframing window %s\n", window->desc);

  frame = window->frame;

  cobiwm_frame_calc_borders (frame, &borders);

  cobiwm_bell_notify_frame_destroy (frame);

  /* Unparent the client window; it may be destroyed,
   * thus the error trap.
   */
  cobiwm_error_trap_push (window->display);
  if (window->mapped)
    {
      window->mapped = FALSE; /* Keep track of unmapping it, so we
                               * can identify a withdraw initiated
                               * by the client.
                               */
      cobiwm_topic (COBIWM_DEBUG_WINDOW_STATE,
                  "Incrementing unmaps_pending on %s for reparent back to root\n", window->desc);
      window->unmaps_pending += 1;
    }
  cobiwm_stack_tracker_record_add (window->screen->stack_tracker,
                                 window->xwindow,
                                 XNextRequest (window->display->xdisplay));
  XReparentWindow (window->display->xdisplay,
                   window->xwindow,
                   window->screen->xroot,
                   /* Using anything other than client root window coordinates
                    * coordinates here means we'll need to ensure a configure
                    * notify event is sent; see bug 399552.
                    */
                   window->frame->rect.x + borders.invisible.left,
                   window->frame->rect.y + borders.invisible.top);
  cobiwm_error_trap_pop (window->display);

  cobiwm_ui_frame_unmanage (frame->ui_frame);

  cobiwm_display_unregister_x_window (window->display,
                                    frame->xwindow);

  window->frame = NULL;
  if (window->frame_bounds)
    {
      cairo_region_destroy (window->frame_bounds);
      window->frame_bounds = NULL;
    }

  /* Move keybindings to window instead of frame */
  cobiwm_window_grab_keys (window);

  g_free (frame);

  /* Put our state back where it should be */
  cobiwm_window_queue (window, COBIWM_QUEUE_CALC_SHOWING);
  cobiwm_window_queue (window, COBIWM_QUEUE_MOVE_RESIZE);
}


CobiwmFrameFlags
cobiwm_frame_get_flags (CobiwmFrame *frame)
{
  CobiwmFrameFlags flags;

  flags = 0;

  if (frame->window->border_only)
    {
      ; /* FIXME this may disable the _function_ as well as decor
         * in some cases, which is sort of wrong.
         */
    }
  else
    {
      flags |= COBIWM_FRAME_ALLOWS_MENU;

      if (cobiwm_prefs_get_show_fallback_app_menu () &&
          frame->window->gtk_app_menu_object_path)
        flags |= COBIWM_FRAME_ALLOWS_APPMENU;

      if (frame->window->has_close_func)
        flags |= COBIWM_FRAME_ALLOWS_DELETE;

      if (frame->window->has_maximize_func)
        flags |= COBIWM_FRAME_ALLOWS_MAXIMIZE;

      if (frame->window->has_minimize_func)
        flags |= COBIWM_FRAME_ALLOWS_MINIMIZE;

      if (frame->window->has_shade_func)
        flags |= COBIWM_FRAME_ALLOWS_SHADE;
    }

  if (COBIWM_WINDOW_ALLOWS_MOVE (frame->window))
    flags |= COBIWM_FRAME_ALLOWS_MOVE;

  if (COBIWM_WINDOW_ALLOWS_HORIZONTAL_RESIZE (frame->window))
    flags |= COBIWM_FRAME_ALLOWS_HORIZONTAL_RESIZE;

  if (COBIWM_WINDOW_ALLOWS_VERTICAL_RESIZE (frame->window))
    flags |= COBIWM_FRAME_ALLOWS_VERTICAL_RESIZE;

  if (cobiwm_window_appears_focused (frame->window))
    flags |= COBIWM_FRAME_HAS_FOCUS;

  if (frame->window->shaded)
    flags |= COBIWM_FRAME_SHADED;

  if (frame->window->on_all_workspaces_requested)
    flags |= COBIWM_FRAME_STUCK;

  /* FIXME: Should we have some kind of UI for windows that are just vertically
   * maximized or just horizontally maximized?
   */
  if (COBIWM_WINDOW_MAXIMIZED (frame->window))
    flags |= COBIWM_FRAME_MAXIMIZED;

  if (COBIWM_WINDOW_TILED_LEFT (frame->window))
    flags |= COBIWM_FRAME_TILED_LEFT;

  if (COBIWM_WINDOW_TILED_RIGHT (frame->window))
    flags |= COBIWM_FRAME_TILED_RIGHT;

  if (frame->window->fullscreen)
    flags |= COBIWM_FRAME_FULLSCREEN;

  if (frame->is_flashing)
    flags |= COBIWM_FRAME_IS_FLASHING;

  if (frame->window->wm_state_above)
    flags |= COBIWM_FRAME_ABOVE;

  return flags;
}

void
cobiwm_frame_borders_clear (CobiwmFrameBorders *self)
{
  self->visible.top    = self->invisible.top    = self->total.top    = 0;
  self->visible.bottom = self->invisible.bottom = self->total.bottom = 0;
  self->visible.left   = self->invisible.left   = self->total.left   = 0;
  self->visible.right  = self->invisible.right  = self->total.right  = 0;
}

void
cobiwm_frame_calc_borders (CobiwmFrame        *frame,
                         CobiwmFrameBorders *borders)
{
  /* Save on if statements and potential uninitialized values
   * in callers -- if there's no frame, then zero the borders. */
  if (frame == NULL)
    cobiwm_frame_borders_clear (borders);
  else
    {
      if (!frame->borders_cached)
        {
          cobiwm_ui_frame_get_borders (frame->ui_frame, &frame->cached_borders);
          frame->borders_cached = TRUE;
        }

      *borders = frame->cached_borders;
    }
}

void
cobiwm_frame_clear_cached_borders (CobiwmFrame *frame)
{
  frame->borders_cached = FALSE;
}

gboolean
cobiwm_frame_sync_to_window (CobiwmFrame *frame,
                           gboolean   need_resize)
{
  cobiwm_topic (COBIWM_DEBUG_GEOMETRY,
              "Syncing frame geometry %d,%d %dx%d (SE: %d,%d)\n",
              frame->rect.x, frame->rect.y,
              frame->rect.width, frame->rect.height,
              frame->rect.x + frame->rect.width,
              frame->rect.y + frame->rect.height);

  cobiwm_ui_frame_move_resize (frame->ui_frame,
			     frame->rect.x,
			     frame->rect.y,
			     frame->rect.width,
			     frame->rect.height);

  return need_resize;
}

cairo_region_t *
cobiwm_frame_get_frame_bounds (CobiwmFrame *frame)
{
  return cobiwm_ui_frame_get_bounds (frame->ui_frame);
}

void
cobiwm_frame_get_mask (CobiwmFrame                    *frame,
                     cairo_t                      *cr)
{
  cobiwm_ui_frame_get_mask (frame->ui_frame, cr);
}

void
cobiwm_frame_queue_draw (CobiwmFrame *frame)
{
  cobiwm_ui_frame_queue_draw (frame->ui_frame);
}

void
cobiwm_frame_set_screen_cursor (CobiwmFrame	*frame,
			      CobiwmCursor cursor)
{
  Cursor xcursor;
  if (cursor == frame->current_cursor)
    return;
  frame->current_cursor = cursor;
  if (cursor == COBIWM_CURSOR_DEFAULT)
    XUndefineCursor (frame->window->display->xdisplay, frame->xwindow);
  else
    {
      xcursor = cobiwm_display_create_x_cursor (frame->window->display, cursor);
      XDefineCursor (frame->window->display->xdisplay, frame->xwindow, xcursor);
      XFlush (frame->window->display->xdisplay);
      XFreeCursor (frame->window->display->xdisplay, xcursor);
    }
}

Window
cobiwm_frame_get_xwindow (CobiwmFrame *frame)
{
  return frame->xwindow;
}

void
cobiwm_frame_update_style (CobiwmFrame *frame)
{
  cobiwm_ui_frame_update_style (frame->ui_frame);
}

void
cobiwm_frame_update_title (CobiwmFrame *frame)
{
  if (frame->window->title)
    cobiwm_ui_frame_set_title (frame->ui_frame, frame->window->title);
}
