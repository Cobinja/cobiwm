/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Cobiwm interface for talking to GTK+ UI module */

/*
 * Copyright (C) 2002 Havoc Pennington
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
#include <prefs.h>
#include "ui.h"
#include "frames.h"
#include <util.h>
#include "core.h"
#include "theme-private.h"

#include <string.h>
#include <stdlib.h>
#include <cairo-xlib.h>

struct _CobiwmUI
{
  Display *xdisplay;
  Screen *xscreen;
  CobiwmFrames *frames;

  /* For double-click tracking */
  gint button_click_number;
  Window button_click_window;
  int button_click_x;
  int button_click_y;
  guint32 button_click_time;
};

void
cobiwm_ui_init (void)
{
  const char *gdk_gl_env = NULL;
  gdk_set_allowed_backends ("x11");

  gdk_gl_env = g_getenv ("GDK_GL");
  g_setenv("GDK_GL", "disable", TRUE);

  if (!gtk_init_check (NULL, NULL))
    cobiwm_fatal ("Unable to open X display %s\n", XDisplayName (NULL));

  if (gdk_gl_env)
    g_setenv("GDK_GL", gdk_gl_env, TRUE);
  else
    unsetenv("GDK_GL");

  /* We need to be able to fully trust that the window and monitor sizes
     that Gdk reports corresponds to the X ones, so we disable the automatic
     scale handling */
  gdk_x11_display_set_window_scale (gdk_display_get_default (), 1);
}

Display*
cobiwm_ui_get_display (void)
{
  return GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());
}

CobiwmUI*
cobiwm_ui_new (Display *xdisplay,
             Screen  *screen)
{
  GdkDisplay *gdisplay;
  CobiwmUI *ui;

  ui = g_new0 (CobiwmUI, 1);
  ui->xdisplay = xdisplay;
  ui->xscreen = screen;

  gdisplay = gdk_x11_lookup_xdisplay (xdisplay);
  g_assert (gdisplay == gdk_display_get_default ());

  ui->frames = cobiwm_frames_new (XScreenNumberOfScreen (screen));
  /* GTK+ needs the frame-sync protocol to work in order to properly
   * handle style changes. This means that the dummy widget we create
   * to get the style for title bars actually needs to be mapped
   * and fully tracked as a CobiwmWindow. Horrible, but mostly harmless -
   * the window is a 1x1 overide redirect window positioned offscreen.
   */
  gtk_widget_show (GTK_WIDGET (ui->frames));

  g_object_set_data (G_OBJECT (gdisplay), "cobiwm-ui", ui);

  return ui;
}

void
cobiwm_ui_free (CobiwmUI *ui)
{
  GdkDisplay *gdisplay;

  gtk_widget_destroy (GTK_WIDGET (ui->frames));

  gdisplay = gdk_x11_lookup_xdisplay (ui->xdisplay);
  g_object_set_data (G_OBJECT (gdisplay), "cobiwm-ui", NULL);

  g_free (ui);
}

static void
set_background_none (Display *xdisplay,
                     Window   xwindow)
{
  XSetWindowAttributes attrs;

  attrs.background_pixmap = None;
  XChangeWindowAttributes (xdisplay, xwindow,
                           CWBackPixmap, &attrs);
}

CobiwmUIFrame *
cobiwm_ui_create_frame (CobiwmUI *ui,
                      Display *xdisplay,
                      CobiwmWindow *cobiwm_window,
                      Visual *xvisual,
                      gint x,
                      gint y,
                      gint width,
                      gint height,
                      gulong *create_serial)
{
  GdkDisplay *display = gdk_x11_lookup_xdisplay (xdisplay);
  GdkScreen *screen = gdk_display_get_default_screen (display);
  GdkWindowAttr attrs;
  gint attributes_mask;
  GdkWindow *window;
  GdkVisual *visual;

  /* Default depth/visual handles clients with weird visuals; they can
   * always be children of the root depth/visual obviously, but
   * e.g. DRI games can't be children of a parent that has the same
   * visual as the client.
   */
  if (!xvisual)
    visual = gdk_screen_get_system_visual (screen);
  else
    {
      visual = gdk_x11_screen_lookup_visual (screen,
                                             XVisualIDFromVisual (xvisual));
    }

  attrs.title = NULL;

  attrs.event_mask = GDK_EXPOSURE_MASK;
  attrs.x = x;
  attrs.y = y;
  attrs.wclass = GDK_INPUT_OUTPUT;
  attrs.visual = visual;
  attrs.window_type = GDK_WINDOW_CHILD;
  attrs.cursor = NULL;
  attrs.wmclass_name = NULL;
  attrs.wmclass_class = NULL;
  attrs.override_redirect = FALSE;

  attrs.width  = width;
  attrs.height = height;

  attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL;

  /* We make an assumption that gdk_window_new() is going to call
   * XCreateWindow as it's first operation; this seems to be true currently
   * as long as you pass in a colormap.
   */
  if (create_serial)
    *create_serial = XNextRequest (xdisplay);
  window =
    gdk_window_new (gdk_screen_get_root_window(screen),
		    &attrs, attributes_mask);

  gdk_window_resize (window, width, height);
  set_background_none (xdisplay, GDK_WINDOW_XID (window));

  return cobiwm_frames_manage_window (ui->frames, cobiwm_window, GDK_WINDOW_XID (window), window);
}

void
cobiwm_ui_map_frame   (CobiwmUI *ui,
                     Window  xwindow)
{
  GdkWindow *window;
  GdkDisplay *display;

  display = gdk_x11_lookup_xdisplay (ui->xdisplay);
  window = gdk_x11_window_lookup_for_display (display, xwindow);

  if (window)
    gdk_window_show_unraised (window);
}

void
cobiwm_ui_unmap_frame (CobiwmUI *ui,
                     Window  xwindow)
{
  GdkWindow *window;
  GdkDisplay *display;

  display = gdk_x11_lookup_xdisplay (ui->xdisplay);
  window = gdk_x11_window_lookup_for_display (display, xwindow);

  if (window)
    gdk_window_hide (window);
}

gboolean
cobiwm_ui_window_should_not_cause_focus (Display *xdisplay,
                                       Window   xwindow)
{
  GdkWindow *window;
  GdkDisplay *display;

  display = gdk_x11_lookup_xdisplay (xdisplay);
  window = gdk_x11_window_lookup_for_display (display, xwindow);

  /* we shouldn't cause focus if we're an override redirect
   * toplevel which is not foreign
   */
  if (window && gdk_window_get_window_type (window) == GDK_WINDOW_TEMP)
    return TRUE;
  else
    return FALSE;
}

void
cobiwm_ui_theme_get_frame_borders (CobiwmUI *ui,
                                 CobiwmFrameType      type,
                                 CobiwmFrameFlags     flags,
                                 CobiwmFrameBorders  *borders)
{
  int text_height;
  CobiwmStyleInfo *style_info = NULL;
  PangoContext *context;
  const PangoFontDescription *font_desc;
  PangoFontDescription *free_font_desc = NULL;

  GdkDisplay *display = gdk_x11_lookup_xdisplay (ui->xdisplay);
  GdkScreen *screen = gdk_display_get_default_screen (display);

  style_info = cobiwm_theme_create_style_info (screen, NULL);

  context = gtk_widget_get_pango_context (GTK_WIDGET (ui->frames));
  font_desc = cobiwm_prefs_get_titlebar_font ();

  if (!font_desc)
    {
      free_font_desc = cobiwm_style_info_create_font_desc (style_info);
      font_desc = (const PangoFontDescription *) free_font_desc;
    }

  text_height = cobiwm_pango_font_desc_get_text_height (font_desc, context);

  cobiwm_theme_get_frame_borders (cobiwm_theme_get_default (),
                                style_info, type, text_height, flags,
                                borders);

  if (free_font_desc)
    pango_font_description_free (free_font_desc);

  if (style_info != NULL)
    cobiwm_style_info_unref (style_info);
}

gboolean
cobiwm_ui_window_is_widget (CobiwmUI *ui,
                          Window  xwindow)
{
  GdkDisplay *display;
  GdkWindow *window;

  display = gdk_x11_lookup_xdisplay (ui->xdisplay);
  window = gdk_x11_window_lookup_for_display (display, xwindow);

  if (window)
    {
      void *user_data = NULL;
      gdk_window_get_user_data (window, &user_data);
      return user_data != NULL && user_data != ui->frames;
    }
  else
    return FALSE;
}

gboolean
cobiwm_ui_window_is_dummy (CobiwmUI *ui,
                         Window  xwindow)
{
  GdkWindow *frames_window = gtk_widget_get_window (GTK_WIDGET (ui->frames));
  return xwindow == gdk_x11_window_get_xid (frames_window);
}
