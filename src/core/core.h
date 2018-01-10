/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Cobiwm interface used by GTK+ UI to talk to core */

/*
 * Copyright (C) 2001 Havoc Pennington
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

#ifndef COBIWM_CORE_H
#define COBIWM_CORE_H

/* Don't include core headers here */
#include <gdk/gdkx.h>
#include <common.h>
#include <boxes.h>

void cobiwm_core_queue_frame_resize (Display *xdisplay,
                                   Window frame_xwindow);

void cobiwm_core_user_lower_and_unfocus (Display *xdisplay,
                                       Window   frame_xwindow,
                                       guint32  timestamp);

void cobiwm_core_toggle_maximize  (Display *xdisplay,
                                 Window   frame_xwindow);
void cobiwm_core_toggle_maximize_horizontally  (Display *xdisplay,
                                              Window   frame_xwindow);
void cobiwm_core_toggle_maximize_vertically    (Display *xdisplay,
                                              Window   frame_xwindow);

void cobiwm_core_show_window_menu (Display            *xdisplay,
                                 Window              frame_xwindow,
                                 CobiwmWindowMenuType  menu,
                                 int                 root_x,
                                 int                 root_y,
                                 guint32             timestamp);

void cobiwm_core_show_window_menu_for_rect (Display            *xdisplay,
                                          Window              frame_xwindow,
                                          CobiwmWindowMenuType  menu,
                                          CobiwmRectangle      *rect,
                                          guint32             timestamp);

gboolean   cobiwm_core_begin_grab_op (Display    *xdisplay,
                                    Window      frame_xwindow,
                                    CobiwmGrabOp  op,
                                    gboolean    pointer_already_grabbed,
                                    gboolean    frame_action,
                                    int         button,
                                    gulong      modmask,
                                    guint32     timestamp,
                                    int         root_x,
                                    int         root_y);
void       cobiwm_core_end_grab_op   (Display    *xdisplay,
                                    guint32     timestamp);
CobiwmGrabOp cobiwm_core_get_grab_op     (Display    *xdisplay);


void       cobiwm_core_grab_buttons  (Display *xdisplay,
                                    Window   frame_xwindow);

void       cobiwm_core_set_screen_cursor (Display *xdisplay,
                                        Window   frame_on_screen,
                                        CobiwmCursor cursor);

void cobiwm_invalidate_default_icons (void);
void cobiwm_retheme_all (void);

#endif
