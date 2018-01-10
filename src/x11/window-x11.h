/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2001 Havoc Pennington
 * Copyright (C) 2002 Red Hat, Inc.
 * Copyright (C) 2003, 2004 Rob Adams
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

#ifndef COBIWM_WINDOW_X11_H
#define COBIWM_WINDOW_X11_H

#include <window.h>
#include <compositor.h>
#include <X11/Xlib.h>

G_BEGIN_DECLS

#define COBIWM_TYPE_WINDOW_X11            (cobiwm_window_x11_get_type())
#define COBIWM_WINDOW_X11(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), COBIWM_TYPE_WINDOW_X11, CobiwmWindowX11))
#define COBIWM_WINDOW_X11_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  COBIWM_TYPE_WINDOW_X11, CobiwmWindowX11Class))
#define COBIWM_IS_WINDOW_X11(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), COBIWM_TYPE_WINDOW_X11))
#define COBIWM_IS_WINDOW_X11_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  COBIWM_TYPE_WINDOW_X11))
#define COBIWM_WINDOW_X11_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  COBIWM_TYPE_WINDOW_X11, CobiwmWindowX11Class))

GType cobiwm_window_x11_get_type (void);

typedef struct _CobiwmWindowX11      CobiwmWindowX11;
typedef struct _CobiwmWindowX11Class CobiwmWindowX11Class;

CobiwmWindow * cobiwm_window_x11_new           (CobiwmDisplay        *display,
                                            Window              xwindow,
                                            gboolean            must_be_viewable,
                                            CobiwmCompEffect      effect);

void cobiwm_window_x11_set_net_wm_state            (CobiwmWindow *window);
void cobiwm_window_x11_set_wm_state                (CobiwmWindow *window);
void cobiwm_window_x11_set_allowed_actions_hint    (CobiwmWindow *window);

void cobiwm_window_x11_create_sync_request_alarm   (CobiwmWindow *window);
void cobiwm_window_x11_destroy_sync_request_alarm  (CobiwmWindow *window);
void cobiwm_window_x11_update_sync_request_counter (CobiwmWindow *window,
                                                  gint64      new_counter_value);

void cobiwm_window_x11_update_input_region         (CobiwmWindow *window);
void cobiwm_window_x11_update_shape_region         (CobiwmWindow *window);

void cobiwm_window_x11_recalc_window_type          (CobiwmWindow *window);

gboolean cobiwm_window_x11_configure_request       (CobiwmWindow *window,
                                                  XEvent     *event);
gboolean cobiwm_window_x11_property_notify         (CobiwmWindow *window,
                                                  XEvent     *event);
gboolean cobiwm_window_x11_client_message          (CobiwmWindow *window,
                                                  XEvent     *event);

void     cobiwm_window_x11_configure_notify        (CobiwmWindow      *window,
                                                  XConfigureEvent *event);

Window   cobiwm_window_x11_get_toplevel_xwindow    (CobiwmWindow *window);

#endif
