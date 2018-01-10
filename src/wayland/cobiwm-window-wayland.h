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

#ifndef COBIWM_WINDOW_WAYLAND_H
#define COBIWM_WINDOW_WAYLAND_H

#include <window.h>
#include "wayland/cobiwm-wayland-types.h"

G_BEGIN_DECLS

#define COBIWM_TYPE_WINDOW_WAYLAND            (cobiwm_window_wayland_get_type())
#define COBIWM_WINDOW_WAYLAND(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), COBIWM_TYPE_WINDOW_WAYLAND, CobiwmWindowWayland))
#define COBIWM_WINDOW_WAYLAND_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  COBIWM_TYPE_WINDOW_WAYLAND, CobiwmWindowWaylandClass))
#define COBIWM_IS_WINDOW_WAYLAND(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), COBIWM_TYPE_WINDOW_WAYLAND))
#define COBIWM_IS_WINDOW_WAYLAND_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  COBIWM_TYPE_WINDOW_WAYLAND))
#define COBIWM_WINDOW_WAYLAND_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  COBIWM_TYPE_WINDOW_WAYLAND, CobiwmWindowWaylandClass))

GType cobiwm_window_wayland_get_type (void);

typedef struct _CobiwmWindowWayland      CobiwmWindowWayland;
typedef struct _CobiwmWindowWaylandClass CobiwmWindowWaylandClass;

CobiwmWindow * cobiwm_window_wayland_new       (CobiwmDisplay        *display,
                                            CobiwmWaylandSurface *surface);

void cobiwm_window_wayland_move_resize (CobiwmWindow        *window,
                                      CobiwmWaylandSerial *acked_configure_serial,
                                      CobiwmRectangle      new_geom,
                                      int                dx,
                                      int                dy);
int cobiwm_window_wayland_get_main_monitor_scale (CobiwmWindow *window);

void cobiwm_window_wayland_place_relative_to (CobiwmWindow *window,
                                            CobiwmWindow *other,
                                            int         x,
                                            int         y);

#endif
