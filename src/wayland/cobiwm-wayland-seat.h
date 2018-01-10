/*
 * Wayland Support
 *
 * Copyright (C) 2012 Intel Corporation
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
 */

#ifndef COBIWM_WAYLAND_SEAT_H
#define COBIWM_WAYLAND_SEAT_H

#include <wayland-server.h>
#include <clutter/clutter.h>

#include "cobiwm-wayland-types.h"
#include "cobiwm-wayland-pointer.h"
#include "cobiwm-wayland-keyboard.h"
#include "cobiwm-wayland-touch.h"
#include "cobiwm-wayland-data-device.h"

struct _CobiwmWaylandSeat
{
  struct wl_list base_resource_list;
  struct wl_display *wl_display;

  CobiwmWaylandPointer pointer;
  CobiwmWaylandKeyboard keyboard;
  CobiwmWaylandTouch touch;
  CobiwmWaylandDataDevice data_device;

  guint capabilities;
};

void cobiwm_wayland_seat_init (CobiwmWaylandCompositor *compositor);

void cobiwm_wayland_seat_free (CobiwmWaylandSeat *seat);

void cobiwm_wayland_seat_update (CobiwmWaylandSeat    *seat,
                               const ClutterEvent *event);

gboolean cobiwm_wayland_seat_handle_event (CobiwmWaylandSeat *seat,
                                         const ClutterEvent *event);

void cobiwm_wayland_seat_set_input_focus (CobiwmWaylandSeat    *seat,
                                        CobiwmWaylandSurface *surface);

void cobiwm_wayland_seat_repick (CobiwmWaylandSeat *seat);

gboolean cobiwm_wayland_seat_get_grab_info (CobiwmWaylandSeat    *seat,
                                          CobiwmWaylandSurface *surface,
                                          uint32_t            serial,
                                          gboolean            require_pressed,
                                          gfloat             *x,
                                          gfloat             *y);
gboolean cobiwm_wayland_seat_can_popup     (CobiwmWaylandSeat *seat,
                                          uint32_t         serial);

#endif /* COBIWM_WAYLAND_SEAT_H */
