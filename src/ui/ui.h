/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Cobiwm interface for talking to GTK+ UI module */

/*
 * Copyright (C) 2001 Havoc Pennington
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

#ifndef COBIWM_UI_H
#define COBIWM_UI_H

/* Don't include gtk.h or gdk.h here */
#include <common.h>
#include <types.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <cairo.h>
#include <glib.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

typedef struct _CobiwmUI CobiwmUI;
typedef struct _CobiwmUIFrame CobiwmUIFrame;

typedef gboolean (* CobiwmEventFunc) (XEvent *xevent, gpointer data);

void cobiwm_ui_init (void);

Display* cobiwm_ui_get_display (void);

CobiwmUI* cobiwm_ui_new (Display *xdisplay,
                     Screen  *screen);
void    cobiwm_ui_free (CobiwmUI *ui);

void cobiwm_ui_theme_get_frame_borders (CobiwmUI *ui,
                                      CobiwmFrameType      type,
                                      CobiwmFrameFlags     flags,
                                      CobiwmFrameBorders *borders);

CobiwmUIFrame * cobiwm_ui_create_frame (CobiwmUI *ui,
                                    Display *xdisplay,
                                    CobiwmWindow *cobiwm_window,
                                    Visual *xvisual,
                                    gint x,
                                    gint y,
                                    gint width,
                                    gint height,
                                    gulong *create_serial);
void cobiwm_ui_move_resize_frame (CobiwmUI *ui,
				Window frame,
				int x,
				int y,
				int width,
				int height);

/* GDK insists on tracking map/unmap */
void cobiwm_ui_map_frame   (CobiwmUI *ui,
                          Window  xwindow);
void cobiwm_ui_unmap_frame (CobiwmUI *ui,
                          Window  xwindow);

gboolean  cobiwm_ui_window_should_not_cause_focus (Display *xdisplay,
                                                 Window   xwindow);

gboolean cobiwm_ui_window_is_widget (CobiwmUI *ui,
                                   Window  xwindow);
gboolean cobiwm_ui_window_is_dummy  (CobiwmUI *ui,
                                   Window  xwindow);

#endif
