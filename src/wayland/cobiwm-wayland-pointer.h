/*
 * Wayland Support
 *
 * Copyright (C) 2013 Intel Corporation
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

#ifndef COBIWM_WAYLAND_POINTER_H
#define COBIWM_WAYLAND_POINTER_H

#include <wayland-server.h>

#include <glib.h>

#include "cobiwm-wayland-types.h"
#include "cobiwm-wayland-pointer-gesture-swipe.h"
#include "cobiwm-wayland-pointer-gesture-pinch.h"
#include "cobiwm-wayland-surface.h"
#include "cobiwm-wayland-pointer-constraints.h"

#include <cobiwm-cursor-tracker.h>

#define COBIWM_TYPE_WAYLAND_SURFACE_ROLE_CURSOR (cobiwm_wayland_surface_role_cursor_get_type ())
G_DECLARE_FINAL_TYPE (CobiwmWaylandSurfaceRoleCursor,
                      cobiwm_wayland_surface_role_cursor,
                      COBIWM, WAYLAND_SURFACE_ROLE_CURSOR,
                      CobiwmWaylandSurfaceRole);

struct _CobiwmWaylandPointerGrabInterface
{
  void (*focus) (CobiwmWaylandPointerGrab *grab,
                 CobiwmWaylandSurface     *surface);
  void (*motion) (CobiwmWaylandPointerGrab *grab,
		  const ClutterEvent     *event);
  void (*button) (CobiwmWaylandPointerGrab *grab,
		  const ClutterEvent     *event);
};

struct _CobiwmWaylandPointerGrab
{
  const CobiwmWaylandPointerGrabInterface *interface;
  CobiwmWaylandPointer *pointer;
};

struct _CobiwmWaylandPointerClient
{
  struct wl_list pointer_resources;
  struct wl_list swipe_gesture_resources;
  struct wl_list pinch_gesture_resources;
  struct wl_list relative_pointer_resources;
};

struct _CobiwmWaylandPointer
{
  struct wl_display *display;

  CobiwmWaylandPointerClient *focus_client;
  GHashTable *pointer_clients;

  CobiwmWaylandSurface *focus_surface;
  struct wl_listener focus_surface_listener;
  guint32 focus_serial;
  guint32 click_serial;

  CobiwmWaylandSurface *cursor_surface;

  CobiwmWaylandPointerGrab *grab;
  CobiwmWaylandPointerGrab default_grab;
  guint32 grab_button;
  guint32 grab_serial;
  guint32 grab_time;
  float grab_x, grab_y;

  ClutterInputDevice *device;
  CobiwmWaylandSurface *current;

  guint32 button_count;
};

void cobiwm_wayland_pointer_init (CobiwmWaylandPointer *pointer,
                                struct wl_display  *display);

void cobiwm_wayland_pointer_release (CobiwmWaylandPointer *pointer);

void cobiwm_wayland_pointer_update (CobiwmWaylandPointer *pointer,
                                  const ClutterEvent *event);

gboolean cobiwm_wayland_pointer_handle_event (CobiwmWaylandPointer *pointer,
                                            const ClutterEvent *event);

void cobiwm_wayland_pointer_send_motion (CobiwmWaylandPointer *pointer,
                                       const ClutterEvent *event);

void cobiwm_wayland_pointer_send_relative_motion (CobiwmWaylandPointer *pointer,
                                                const ClutterEvent *event);

void cobiwm_wayland_pointer_send_button (CobiwmWaylandPointer *pointer,
                                       const ClutterEvent *event);

void cobiwm_wayland_pointer_set_focus (CobiwmWaylandPointer *pointer,
                                     CobiwmWaylandSurface *surface);

void cobiwm_wayland_pointer_start_grab (CobiwmWaylandPointer *pointer,
                                      CobiwmWaylandPointerGrab *grab);

void cobiwm_wayland_pointer_end_grab (CobiwmWaylandPointer *pointer);

CobiwmWaylandPopup *cobiwm_wayland_pointer_start_popup_grab (CobiwmWaylandPointer *pointer,
                                                         CobiwmWaylandSurface *popup);

void cobiwm_wayland_pointer_end_popup_grab (CobiwmWaylandPointer *pointer);

void cobiwm_wayland_pointer_repick (CobiwmWaylandPointer *pointer);

void cobiwm_wayland_pointer_get_relative_coordinates (CobiwmWaylandPointer *pointer,
                                                    CobiwmWaylandSurface *surface,
                                                    wl_fixed_t         *x,
                                                    wl_fixed_t         *y);

void cobiwm_wayland_pointer_create_new_resource (CobiwmWaylandPointer *pointer,
                                               struct wl_client   *client,
                                               struct wl_resource *seat_resource,
                                               uint32_t id);

gboolean cobiwm_wayland_pointer_can_grab_surface (CobiwmWaylandPointer *pointer,
                                                CobiwmWaylandSurface *surface,
                                                uint32_t            serial);

gboolean cobiwm_wayland_pointer_can_popup (CobiwmWaylandPointer *pointer,
                                         uint32_t            serial);

CobiwmWaylandSurface *cobiwm_wayland_pointer_get_top_popup (CobiwmWaylandPointer *pointer);

CobiwmWaylandPointerClient * cobiwm_wayland_pointer_get_pointer_client (CobiwmWaylandPointer *pointer,
                                                                    struct wl_client   *client);
void cobiwm_wayland_pointer_unbind_pointer_client_resource (struct wl_resource *resource);

void cobiwm_wayland_relative_pointer_init (CobiwmWaylandCompositor *compositor);

CobiwmWaylandSeat *cobiwm_wayland_pointer_get_seat (CobiwmWaylandPointer *pointer);

#endif /* COBIWM_WAYLAND_POINTER_H */
