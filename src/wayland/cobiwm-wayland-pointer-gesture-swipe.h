/*
 * Wayland Support
 *
 * Copyright (C) 2015 Red Hat
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#ifndef COBIWM_WAYLAND_POINTER_GESTURE_SWIPE_H
#define COBIWM_WAYLAND_POINTER_GESTURE_SWIPE_H

#include <wayland-server.h>
#include <clutter/clutter.h>
#include <glib.h>

#include "cobiwm-wayland-types.h"

gboolean cobiwm_wayland_pointer_gesture_swipe_handle_event (CobiwmWaylandPointer *pointer,
                                                          const ClutterEvent *event);

void cobiwm_wayland_pointer_gesture_swipe_create_new_resource (CobiwmWaylandPointer *pointer,
                                                             struct wl_client   *client,
                                                             struct wl_resource *pointer_resource,
                                                             uint32_t            id);

#endif /* COBIWM_WAYLAND_POINTER_GESTURE_SWIPE_H */
