/*
 * Wayland Support
 *
 * Copyright (C) 2013 Intel Corporation
 * Copyright (C) 2015 Red Hat, Inc.
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
 */

#ifndef COBIWM_WAYLAND_POPUP_H
#define COBIWM_WAYLAND_POPUP_H

#include <glib.h>
#include <wayland-server.h>

#include "cobiwm-wayland-types.h"
#include "cobiwm-wayland-pointer.h"

CobiwmWaylandPopupGrab *cobiwm_wayland_popup_grab_create (CobiwmWaylandPointer *pointer,
                                                      struct wl_client   *client);

void cobiwm_wayland_popup_grab_destroy (CobiwmWaylandPopupGrab *grab);

void cobiwm_wayland_popup_grab_begin (CobiwmWaylandPopupGrab *grab,
                                    CobiwmWaylandSurface   *surface);

void cobiwm_wayland_popup_grab_end (CobiwmWaylandPopupGrab *grab);

CobiwmWaylandSurface *cobiwm_wayland_popup_grab_get_top_popup (CobiwmWaylandPopupGrab *grab);

gboolean cobiwm_wayland_pointer_grab_is_popup_grab (CobiwmWaylandPointerGrab *grab);

CobiwmWaylandPopup *cobiwm_wayland_popup_create (CobiwmWaylandSurface   *surface,
                                             CobiwmWaylandPopupGrab *grab);

void cobiwm_wayland_popup_destroy (CobiwmWaylandPopup *popup);

void cobiwm_wayland_popup_dismiss (CobiwmWaylandPopup *popup);

CobiwmWaylandSurface *cobiwm_wayland_popup_get_top_popup (CobiwmWaylandPopup *popup);

struct wl_signal *cobiwm_wayland_popup_get_destroy_signal (CobiwmWaylandPopup *popup);

#endif /* COBIWM_WAYLAND_POPUP_H */
