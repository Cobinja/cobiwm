/*
 * Wayland Support
 *
 * Copyright (C) 2014 Red Hat
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

#ifndef COBIWM_WAYLAND_TOUCH_H
#define COBIWM_WAYLAND_TOUCH_H

#include <wayland-server.h>

#include <glib.h>

#include "cobiwm-wayland-types.h"

typedef struct _CobiwmWaylandTouchSurface CobiwmWaylandTouchSurface;
typedef struct _CobiwmWaylandTouchInfo CobiwmWaylandTouchInfo;

struct _CobiwmWaylandTouch
{
  struct wl_display *display;
  struct wl_list resource_list;

  GHashTable *touch_surfaces; /* HT of CobiwmWaylandSurface->CobiwmWaylandTouchSurface */
  GHashTable *touches; /* HT of sequence->CobiwmWaylandTouchInfo */

  ClutterInputDevice *device;
  guint64 frame_slots;
};

void cobiwm_wayland_touch_init (CobiwmWaylandTouch  *touch,
                              struct wl_display *display);

void cobiwm_wayland_touch_release (CobiwmWaylandTouch *touch);

void cobiwm_wayland_touch_update (CobiwmWaylandTouch   *touch,
                                const ClutterEvent *event);

gboolean cobiwm_wayland_touch_handle_event (CobiwmWaylandTouch   *touch,
                                          const ClutterEvent *event);

void cobiwm_wayland_touch_create_new_resource (CobiwmWaylandTouch   *touch,
                                             struct wl_client   *client,
                                             struct wl_resource *seat_resource,
                                             uint32_t            id);
void cobiwm_wayland_touch_cancel (CobiwmWaylandTouch *touch);


ClutterEventSequence * cobiwm_wayland_touch_find_grab_sequence (CobiwmWaylandTouch   *touch,
                                                              CobiwmWaylandSurface *surface,
                                                              uint32_t            serial);

gboolean cobiwm_wayland_touch_get_press_coords (CobiwmWaylandTouch     *touch,
                                              ClutterEventSequence *sequence,
                                              gfloat               *x,
                                              gfloat               *y);

gboolean cobiwm_wayland_touch_can_popup        (CobiwmWaylandTouch *touch,
                                              uint32_t          serial);

#endif /* COBIWM_WAYLAND_TOUCH_H */
