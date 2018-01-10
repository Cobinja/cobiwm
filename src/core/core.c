/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Cobiwm interface used by GTK+ UI to talk to core */

/*
 * Copyright (C) 2001 Havoc Pennington
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

#include <config.h>
#include "core.h"
#include "frame.h"
#include "workspace-private.h"
#include <prefs.h>
#include <errors.h>
#include "util-private.h"

#include "x11/window-x11.h"
#include "x11/window-x11-private.h"

/* Looks up the CobiwmWindow representing the frame of the given X window.
 * Used as a helper function by a bunch of the functions below.
 *
 * FIXME: The functions that use this function throw the result away
 * after use. Many of these functions tend to be called in small groups,
 * which results in get_window() getting called several times in succession
 * with the same parameters. We should profile to see whether this wastes
 * much time, and if it does we should look into a generalised
 * cobiwm_core_get_window_info() which takes a bunch of pointers to variables
 * to put its results in, and only fills in the non-null ones.
 */
static CobiwmWindow *
get_window (Display *xdisplay,
            Window   frame_xwindow)
{
  CobiwmDisplay *display;
  CobiwmWindow *window;

  display = cobiwm_display_for_x_display (xdisplay);
  window = cobiwm_display_lookup_x_window (display, frame_xwindow);

  if (window == NULL || window->frame == NULL)
    {
      cobiwm_bug ("No such frame window 0x%lx!\n", frame_xwindow);
      return NULL;
    }

  return window;
}

void
cobiwm_core_queue_frame_resize (Display *xdisplay,
                              Window   frame_xwindow)
{
  CobiwmWindow *window = get_window (xdisplay, frame_xwindow);

  cobiwm_window_queue (window, COBIWM_QUEUE_MOVE_RESIZE);
  cobiwm_window_frame_size_changed (window);
}

static gboolean
lower_window_and_transients (CobiwmWindow *window,
                             gpointer   data)
{
  cobiwm_window_lower (window);

  cobiwm_window_foreach_transient (window, lower_window_and_transients, NULL);

  if (cobiwm_prefs_get_raise_on_click ())
    {
      /* Move window to the back of the focusing workspace's MRU list.
       * Do extra sanity checks to avoid possible race conditions.
       * (Borrowed from window.c.)
       */
      if (window->screen->active_workspace &&
          cobiwm_window_located_on_workspace (window,
                                            window->screen->active_workspace))
        {
          GList* link;
          link = g_list_find (window->screen->active_workspace->mru_list,
                              window);
          g_assert (link);

          window->screen->active_workspace->mru_list =
            g_list_remove_link (window->screen->active_workspace->mru_list,
                                link);
          g_list_free (link);

          window->screen->active_workspace->mru_list =
            g_list_append (window->screen->active_workspace->mru_list,
                           window);
        }
    }

  return FALSE;
}

void
cobiwm_core_user_lower_and_unfocus (Display *xdisplay,
                                  Window   frame_xwindow,
                                  guint32  timestamp)
{
  CobiwmWindow *window = get_window (xdisplay, frame_xwindow);

  lower_window_and_transients (window, NULL);

 /* Rather than try to figure that out whether we just lowered
  * the focus window, assume that's always the case. (Typically,
  * this will be invoked via keyboard action or by a mouse action;
  * in either case the window or a modal child will have been focused.) */
  cobiwm_workspace_focus_default_window (window->screen->active_workspace,
                                       NULL,
                                       timestamp);
}

void
cobiwm_core_toggle_maximize_vertically (Display *xdisplay,
				      Window   frame_xwindow)
{
  CobiwmWindow *window = get_window (xdisplay, frame_xwindow);

  if (cobiwm_prefs_get_raise_on_click ())
    cobiwm_window_raise (window);

  if (COBIWM_WINDOW_MAXIMIZED_VERTICALLY (window))
    cobiwm_window_unmaximize (window, COBIWM_MAXIMIZE_VERTICAL);
  else
    cobiwm_window_maximize (window, COBIWM_MAXIMIZE_VERTICAL);
}

void
cobiwm_core_toggle_maximize_horizontally (Display *xdisplay,
				        Window   frame_xwindow)
{
  CobiwmWindow *window = get_window (xdisplay, frame_xwindow);

  if (cobiwm_prefs_get_raise_on_click ())
    cobiwm_window_raise (window);

  if (COBIWM_WINDOW_MAXIMIZED_HORIZONTALLY (window))
    cobiwm_window_unmaximize (window, COBIWM_MAXIMIZE_HORIZONTAL);
  else
    cobiwm_window_maximize (window, COBIWM_MAXIMIZE_HORIZONTAL);
}

void
cobiwm_core_toggle_maximize (Display *xdisplay,
                           Window   frame_xwindow)
{
  CobiwmWindow *window = get_window (xdisplay, frame_xwindow);

  if (cobiwm_prefs_get_raise_on_click ())
    cobiwm_window_raise (window);

  if (COBIWM_WINDOW_MAXIMIZED (window))
    cobiwm_window_unmaximize (window, COBIWM_MAXIMIZE_BOTH);
  else
    cobiwm_window_maximize (window, COBIWM_MAXIMIZE_BOTH);
}

void
cobiwm_core_show_window_menu (Display            *xdisplay,
                            Window              frame_xwindow,
                            CobiwmWindowMenuType  menu,
                            int                 root_x,
                            int                 root_y,
                            guint32             timestamp)
{
  CobiwmWindow *window = get_window (xdisplay, frame_xwindow);

  if (cobiwm_prefs_get_raise_on_click ())
    cobiwm_window_raise (window);
  cobiwm_window_focus (window, timestamp);

  cobiwm_window_show_menu (window, menu, root_x, root_y);
}

void
cobiwm_core_show_window_menu_for_rect (Display            *xdisplay,
                                     Window              frame_xwindow,
                                     CobiwmWindowMenuType  menu,
                                     CobiwmRectangle      *rect,
                                     guint32             timestamp)
{
  CobiwmWindow *window = get_window (xdisplay, frame_xwindow);

  if (cobiwm_prefs_get_raise_on_click ())
    cobiwm_window_raise (window);
  cobiwm_window_focus (window, timestamp);

  cobiwm_window_show_menu_for_rect (window, menu, rect);
}

gboolean
cobiwm_core_begin_grab_op (Display    *xdisplay,
                         Window      frame_xwindow,
                         CobiwmGrabOp  op,
                         gboolean    pointer_already_grabbed,
                         gboolean    frame_action,
                         int         button,
                         gulong      modmask,
                         guint32     timestamp,
                         int         root_x,
                         int         root_y)
{
  CobiwmWindow *window = get_window (xdisplay, frame_xwindow);
  CobiwmDisplay *display;
  CobiwmScreen *screen;

  display = cobiwm_display_for_x_display (xdisplay);
  screen = display->screen;

  g_assert (screen != NULL);

  return cobiwm_display_begin_grab_op (display, screen, window,
                                     op, pointer_already_grabbed,
                                     frame_action,
                                     button, modmask,
                                     timestamp, root_x, root_y);
}

void
cobiwm_core_end_grab_op (Display *xdisplay,
                       guint32  timestamp)
{
  CobiwmDisplay *display;

  display = cobiwm_display_for_x_display (xdisplay);

  cobiwm_display_end_grab_op (display, timestamp);
}

CobiwmGrabOp
cobiwm_core_get_grab_op (Display *xdisplay)
{
  CobiwmDisplay *display;

  display = cobiwm_display_for_x_display (xdisplay);

  return display->grab_op;
}

void
cobiwm_core_grab_buttons  (Display *xdisplay,
                         Window   frame_xwindow)
{
  CobiwmDisplay *display;

  display = cobiwm_display_for_x_display (xdisplay);

  cobiwm_verbose ("Grabbing buttons on frame 0x%lx\n", frame_xwindow);
  cobiwm_display_grab_window_buttons (display, frame_xwindow);
}

void
cobiwm_core_set_screen_cursor (Display *xdisplay,
                             Window   frame_on_screen,
                             CobiwmCursor cursor)
{
  CobiwmWindow *window = get_window (xdisplay, frame_on_screen);

  cobiwm_frame_set_screen_cursor (window->frame, cursor);
}

void
cobiwm_invalidate_default_icons (void)
{
  /* XXX: Actually invalidate the icons when they're used. */
}

void
cobiwm_retheme_all (void)
{
  if (cobiwm_get_display ())
    cobiwm_display_retheme_all ();
}
