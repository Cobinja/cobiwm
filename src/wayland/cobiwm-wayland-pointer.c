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

/*
 * Copyright © 2008 Kristian Høgsberg
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

/* The file is based on src/input.c from Weston */

#include "config.h"

#include <clutter/clutter.h>
#include <clutter/evdev/clutter-evdev.h>
#include <cogl/cogl.h>
#include <cogl/cogl-wayland-server.h>
#include <linux/input.h>

#include "cobiwm-wayland-pointer.h"
#include "cobiwm-wayland-popup.h"
#include "cobiwm-wayland-private.h"
#include "cobiwm-wayland-surface.h"
#include "cobiwm-wayland-buffer.h"
#include "cobiwm-xwayland.h"
#include "cobiwm-cursor.h"
#include "cobiwm-cursor-tracker-private.h"
#include "cobiwm-surface-actor-wayland.h"
#include "cobiwm-cursor-tracker.h"
#include "backends/cobiwm-backend-private.h"
#include "backends/cobiwm-cursor-tracker-private.h"
#include "backends/cobiwm-cursor-renderer.h"

#include "relative-pointer-unstable-v1-server-protocol.h"

#ifdef HAVE_NATIVE_BACKEND
#include "backends/native/cobiwm-backend-native.h"
#endif

#include <string.h>

#define DEFAULT_AXIS_STEP_DISTANCE wl_fixed_from_int (10)

struct _CobiwmWaylandSurfaceRoleCursor
{
  CobiwmWaylandSurfaceRole parent;

  int hot_x;
  int hot_y;
  CobiwmCursorSprite *cursor_sprite;

  CobiwmWaylandBuffer *buffer;
};

G_DEFINE_TYPE (CobiwmWaylandSurfaceRoleCursor,
               cobiwm_wayland_surface_role_cursor,
               COBIWM_TYPE_WAYLAND_SURFACE_ROLE);

static void
cobiwm_wayland_pointer_update_cursor_surface (CobiwmWaylandPointer *pointer);

static CobiwmWaylandPointerClient *
cobiwm_wayland_pointer_client_new (void)
{
  CobiwmWaylandPointerClient *pointer_client;

  pointer_client = g_slice_new0 (CobiwmWaylandPointerClient);
  wl_list_init (&pointer_client->pointer_resources);
  wl_list_init (&pointer_client->swipe_gesture_resources);
  wl_list_init (&pointer_client->pinch_gesture_resources);
  wl_list_init (&pointer_client->relative_pointer_resources);

  return pointer_client;
}

static void
cobiwm_wayland_pointer_client_free (CobiwmWaylandPointerClient *pointer_client)
{
  struct wl_resource *resource, *next;

  /* Since we make every wl_pointer resource defunct when we stop advertising
   * the pointer capability on the wl_seat, we need to make sure all the
   * resources in the pointer client instance gets removed.
   */
  wl_resource_for_each_safe (resource, next, &pointer_client->pointer_resources)
    {
      wl_list_remove (wl_resource_get_link (resource));
      wl_list_init (wl_resource_get_link (resource));
    }
  wl_resource_for_each_safe (resource, next, &pointer_client->swipe_gesture_resources)
    {
      wl_list_remove (wl_resource_get_link (resource));
      wl_list_init (wl_resource_get_link (resource));
    }
  wl_resource_for_each_safe (resource, next, &pointer_client->pinch_gesture_resources)
    {
      wl_list_remove (wl_resource_get_link (resource));
      wl_list_init (wl_resource_get_link (resource));
    }
  wl_resource_for_each_safe (resource, next, &pointer_client->relative_pointer_resources)
    {
      wl_list_remove (wl_resource_get_link (resource));
      wl_list_init (wl_resource_get_link (resource));
    }

  g_slice_free (CobiwmWaylandPointerClient, pointer_client);
}

static gboolean
cobiwm_wayland_pointer_client_is_empty (CobiwmWaylandPointerClient *pointer_client)
{
  return (wl_list_empty (&pointer_client->pointer_resources) &&
          wl_list_empty (&pointer_client->swipe_gesture_resources) &&
          wl_list_empty (&pointer_client->pinch_gesture_resources) &&
          wl_list_empty (&pointer_client->relative_pointer_resources));
}

CobiwmWaylandPointerClient *
cobiwm_wayland_pointer_get_pointer_client (CobiwmWaylandPointer *pointer,
                                         struct wl_client   *client)
{
  if (!pointer->pointer_clients)
    return NULL;
  return g_hash_table_lookup (pointer->pointer_clients, client);
}

static CobiwmWaylandPointerClient *
cobiwm_wayland_pointer_ensure_pointer_client (CobiwmWaylandPointer *pointer,
                                            struct wl_client   *client)
{
  CobiwmWaylandPointerClient *pointer_client;

  pointer_client = cobiwm_wayland_pointer_get_pointer_client (pointer, client);
  if (pointer_client)
    return pointer_client;

  pointer_client = cobiwm_wayland_pointer_client_new ();
  g_hash_table_insert (pointer->pointer_clients, client, pointer_client);

  if (!pointer->focus_client &&
      pointer->focus_surface &&
      wl_resource_get_client (pointer->focus_surface->resource) == client)
    pointer->focus_client = pointer_client;

  return pointer_client;
}

static void
cobiwm_wayland_pointer_cleanup_pointer_client (CobiwmWaylandPointer       *pointer,
                                             CobiwmWaylandPointerClient *pointer_client,
                                             struct wl_client         *client)
{
  if (cobiwm_wayland_pointer_client_is_empty (pointer_client))
    {
      if (pointer->focus_client == pointer_client)
        pointer->focus_client = NULL;
      g_hash_table_remove (pointer->pointer_clients, client);
    }
}

void
cobiwm_wayland_pointer_unbind_pointer_client_resource (struct wl_resource *resource)
{
  CobiwmWaylandPointer *pointer = wl_resource_get_user_data (resource);
  CobiwmWaylandPointerClient *pointer_client;
  struct wl_client *client = wl_resource_get_client (resource);

  wl_list_remove (wl_resource_get_link (resource));

  pointer_client = cobiwm_wayland_pointer_get_pointer_client (pointer, client);
  if (!pointer_client)
    {
      /* This happens if all pointer devices were unplugged and no new resources
       * were created by the client.
       *
       * If this is a resource that was previously made defunct, pointer_client
       * be non-NULL but it is harmless since the below cleanup call will be
       * prevented from removing the pointer client because of valid resources.
       */
      return;
    }

  cobiwm_wayland_pointer_cleanup_pointer_client (pointer,
                                               pointer_client,
                                               client);
}

static void
sync_focus_surface (CobiwmWaylandPointer *pointer)
{
  CobiwmDisplay *display = cobiwm_get_display ();

  switch (display->event_route)
    {
    case COBIWM_EVENT_ROUTE_WINDOW_OP:
    case COBIWM_EVENT_ROUTE_COMPOSITOR_GRAB:
    case COBIWM_EVENT_ROUTE_FRAME_BUTTON:
      /* The compositor has a grab, so remove our focus... */
      cobiwm_wayland_pointer_set_focus (pointer, NULL);
      break;

    case COBIWM_EVENT_ROUTE_NORMAL:
    case COBIWM_EVENT_ROUTE_WAYLAND_POPUP:
      {
        const CobiwmWaylandPointerGrabInterface *interface = pointer->grab->interface;
        interface->focus (pointer->grab, pointer->current);
      }
      break;

    default:
      g_assert_not_reached ();
    }

}

static void
pointer_handle_focus_surface_destroy (struct wl_listener *listener, void *data)
{
  CobiwmWaylandPointer *pointer = wl_container_of (listener, pointer, focus_surface_listener);

  cobiwm_wayland_pointer_set_focus (pointer, NULL);
}

static void
cobiwm_wayland_pointer_send_frame (CobiwmWaylandPointer *pointer,
				 struct wl_resource *resource)
{
  if (wl_resource_get_version (resource) >= WL_POINTER_AXIS_SOURCE_SINCE_VERSION)
    wl_pointer_send_frame (resource);
}

static void
cobiwm_wayland_pointer_broadcast_frame (CobiwmWaylandPointer *pointer)
{
  struct wl_resource *resource;

  if (!pointer->focus_client)
    return;

  wl_resource_for_each (resource, &pointer->focus_client->pointer_resources)
    {
      cobiwm_wayland_pointer_send_frame (pointer, resource);
    }
}

void
cobiwm_wayland_pointer_send_relative_motion (CobiwmWaylandPointer *pointer,
                                           const ClutterEvent *event)
{
  struct wl_resource *resource;
  double dx, dy;
  double dx_unaccel, dy_unaccel;
  uint64_t time_us;
  uint32_t time_us_hi;
  uint32_t time_us_lo;
  wl_fixed_t dxf, dyf;
  wl_fixed_t dx_unaccelf, dy_unaccelf;

  if (!pointer->focus_client)
    return;

  if (!cobiwm_backend_get_relative_motion_deltas (cobiwm_get_backend (),
                                                event,
                                                &dx, &dy,
                                                &dx_unaccel, &dy_unaccel))
    return;

#ifdef HAVE_NATIVE_BACKEND
  time_us = clutter_evdev_event_get_time_usec (event);
  if (time_us == 0)
#endif
    time_us = clutter_event_get_time (event) * 1000ULL;
  time_us_hi = (uint32_t) (time_us >> 32);
  time_us_lo = (uint32_t) time_us;
  dxf = wl_fixed_from_double (dx);
  dyf = wl_fixed_from_double (dy);
  dx_unaccelf = wl_fixed_from_double (dx_unaccel);
  dy_unaccelf = wl_fixed_from_double (dy_unaccel);

  wl_resource_for_each (resource,
                        &pointer->focus_client->relative_pointer_resources)
    {
      zwp_relative_pointer_v1_send_relative_motion (resource,
                                                    time_us_hi,
                                                    time_us_lo,
                                                    dxf,
                                                    dyf,
                                                    dx_unaccelf,
                                                    dy_unaccelf);
    }
}

void
cobiwm_wayland_pointer_send_motion (CobiwmWaylandPointer *pointer,
                                  const ClutterEvent *event)
{
  struct wl_resource *resource;
  uint32_t time;
  float sx, sy;

  if (!pointer->focus_client)
    return;

  time = clutter_event_get_time (event);
  cobiwm_wayland_surface_get_relative_coordinates (pointer->focus_surface,
                                                 event->motion.x,
                                                 event->motion.y,
                                                 &sx, &sy);

  wl_resource_for_each (resource, &pointer->focus_client->pointer_resources)
    {
      wl_pointer_send_motion (resource, time,
                              wl_fixed_from_double (sx),
                              wl_fixed_from_double (sy));
    }

  cobiwm_wayland_pointer_send_relative_motion (pointer, event);

  cobiwm_wayland_pointer_broadcast_frame (pointer);
}

void
cobiwm_wayland_pointer_send_button (CobiwmWaylandPointer *pointer,
                                  const ClutterEvent *event)
{
  struct wl_resource *resource;
  ClutterEventType event_type;

  event_type = clutter_event_type (event);

  if (pointer->focus_client &&
      !wl_list_empty (&pointer->focus_client->pointer_resources))
    {
      struct wl_client *client = wl_resource_get_client (pointer->focus_surface->resource);
      struct wl_display *display = wl_client_get_display (client);
      uint32_t time;
      uint32_t button;
      uint32_t serial;

#ifdef HAVE_NATIVE_BACKEND
      CobiwmBackend *backend = cobiwm_get_backend ();
      if (COBIWM_IS_BACKEND_NATIVE (backend))
        button = clutter_evdev_event_get_event_code (event);
      else
#endif
        {
          button = clutter_event_get_button (event);
          switch (button)
            {
            case 1:
              button = BTN_LEFT;
              break;

              /* The evdev input right and middle button numbers are swapped
                 relative to how Clutter numbers them */
            case 2:
              button = BTN_MIDDLE;
              break;

            case 3:
              button = BTN_RIGHT;
              break;

            default:
              button = button + (BTN_LEFT - 1) + 4;
              break;
            }
        }

      time = clutter_event_get_time (event);
      serial = wl_display_next_serial (display);

      wl_resource_for_each (resource, &pointer->focus_client->pointer_resources)
        {
          wl_pointer_send_button (resource, serial,
                                  time, button,
                                  event_type == CLUTTER_BUTTON_PRESS ? 1 : 0);
        }

      cobiwm_wayland_pointer_broadcast_frame (pointer);
    }

  if (pointer->button_count == 0 && event_type == CLUTTER_BUTTON_RELEASE)
    sync_focus_surface (pointer);
}

static void
default_grab_focus (CobiwmWaylandPointerGrab *grab,
                    CobiwmWaylandSurface     *surface)
{
  CobiwmWaylandPointer *pointer = grab->pointer;

  if (pointer->button_count > 0)
    return;

  cobiwm_wayland_pointer_set_focus (pointer, surface);
}

static void
default_grab_motion (CobiwmWaylandPointerGrab *grab,
		     const ClutterEvent     *event)
{
  CobiwmWaylandPointer *pointer = grab->pointer;

  cobiwm_wayland_pointer_send_motion (pointer, event);
}

static void
default_grab_button (CobiwmWaylandPointerGrab *grab,
		     const ClutterEvent     *event)
{
  CobiwmWaylandPointer *pointer = grab->pointer;

  cobiwm_wayland_pointer_send_button (pointer, event);
}

static const CobiwmWaylandPointerGrabInterface default_pointer_grab_interface = {
  default_grab_focus,
  default_grab_motion,
  default_grab_button
};

static void
cobiwm_wayland_pointer_on_cursor_changed (CobiwmCursorTracker *cursor_tracker,
                                        CobiwmWaylandPointer *pointer)
{
  if (pointer->cursor_surface)
    cobiwm_wayland_surface_update_outputs (pointer->cursor_surface);
}

void
cobiwm_wayland_pointer_init (CobiwmWaylandPointer *pointer,
                           struct wl_display  *display)
{
  CobiwmCursorTracker *cursor_tracker = cobiwm_cursor_tracker_get_for_screen (NULL);
  ClutterDeviceManager *manager;

  memset (pointer, 0, sizeof *pointer);

  pointer->display = display;

  pointer->pointer_clients =
    g_hash_table_new_full (NULL, NULL, NULL,
                           (GDestroyNotify) cobiwm_wayland_pointer_client_free);

  pointer->focus_surface_listener.notify = pointer_handle_focus_surface_destroy;

  pointer->cursor_surface = NULL;

  pointer->default_grab.interface = &default_pointer_grab_interface;
  pointer->default_grab.pointer = pointer;
  pointer->grab = &pointer->default_grab;

  manager = clutter_device_manager_get_default ();
  pointer->device = clutter_device_manager_get_core_device (manager, CLUTTER_POINTER_DEVICE);

  g_signal_connect (cursor_tracker,
                    "cursor-changed",
                    G_CALLBACK (cobiwm_wayland_pointer_on_cursor_changed),
                    pointer);
}

void
cobiwm_wayland_pointer_release (CobiwmWaylandPointer *pointer)
{
  CobiwmCursorTracker *cursor_tracker = cobiwm_cursor_tracker_get_for_screen (NULL);

  g_signal_handlers_disconnect_by_func (cursor_tracker,
                                        (gpointer) cobiwm_wayland_pointer_on_cursor_changed,
                                        pointer);

  cobiwm_wayland_pointer_set_focus (pointer, NULL);

  g_clear_pointer (&pointer->pointer_clients, g_hash_table_unref);
  pointer->display = NULL;
  pointer->cursor_surface = NULL;
}

static int
count_buttons (const ClutterEvent *event)
{
  static gint maskmap[5] =
    {
      CLUTTER_BUTTON1_MASK, CLUTTER_BUTTON2_MASK, CLUTTER_BUTTON3_MASK,
      CLUTTER_BUTTON4_MASK, CLUTTER_BUTTON5_MASK
    };
  ClutterModifierType mod_mask;
  int i, count;

  mod_mask = clutter_event_get_state (event);
  count = 0;
  for (i = 0; i < 5; i++)
    {
      if (mod_mask & maskmap[i])
	count++;
    }

  return count;
}

static void
repick_for_event (CobiwmWaylandPointer *pointer,
                  const ClutterEvent *for_event)
{
  ClutterActor *actor;

  if (for_event)
    actor = clutter_event_get_source (for_event);
  else
    actor = clutter_input_device_get_pointer_actor (pointer->device);

  if (COBIWM_IS_SURFACE_ACTOR_WAYLAND (actor))
    pointer->current = cobiwm_surface_actor_wayland_get_surface (COBIWM_SURFACE_ACTOR_WAYLAND (actor));
  else
    pointer->current = NULL;

  sync_focus_surface (pointer);
  cobiwm_wayland_pointer_update_cursor_surface (pointer);
}

void
cobiwm_wayland_pointer_update (CobiwmWaylandPointer *pointer,
                             const ClutterEvent *event)
{
  repick_for_event (pointer, event);

  pointer->button_count = count_buttons (event);
}

static void
notify_motion (CobiwmWaylandPointer *pointer,
               const ClutterEvent *event)
{
  pointer->grab->interface->motion (pointer->grab, event);
}

static void
handle_motion_event (CobiwmWaylandPointer *pointer,
                     const ClutterEvent *event)
{
  notify_motion (pointer, event);
}

static void
handle_button_event (CobiwmWaylandPointer *pointer,
                     const ClutterEvent *event)
{
  gboolean implicit_grab;

  implicit_grab = (event->type == CLUTTER_BUTTON_PRESS) && (pointer->button_count == 1);
  if (implicit_grab)
    {
      pointer->grab_button = clutter_event_get_button (event);
      pointer->grab_time = clutter_event_get_time (event);
      clutter_event_get_coords (event, &pointer->grab_x, &pointer->grab_y);
    }

  pointer->grab->interface->button (pointer->grab, event);

  if (implicit_grab)
    pointer->grab_serial = wl_display_get_serial (pointer->display);
}

static void
handle_scroll_event (CobiwmWaylandPointer *pointer,
                     const ClutterEvent *event)
{
  struct wl_resource *resource;
  wl_fixed_t x_value = 0, y_value = 0;
  int x_discrete = 0, y_discrete = 0;
  enum wl_pointer_axis_source source = -1;

  if (clutter_event_is_pointer_emulated (event))
    return;

  switch (event->scroll.scroll_source)
    {
    case CLUTTER_SCROLL_SOURCE_WHEEL:
      source = WL_POINTER_AXIS_SOURCE_WHEEL;
      break;
    case CLUTTER_SCROLL_SOURCE_FINGER:
      source = WL_POINTER_AXIS_SOURCE_FINGER;
      break;
    case CLUTTER_SCROLL_SOURCE_CONTINUOUS:
      source = WL_POINTER_AXIS_SOURCE_CONTINUOUS;
      break;
    default:
      source = WL_POINTER_AXIS_SOURCE_WHEEL;
      break;
    }

  switch (clutter_event_get_scroll_direction (event))
    {
    case CLUTTER_SCROLL_UP:
      y_value = -DEFAULT_AXIS_STEP_DISTANCE;
      y_discrete = -1;
      break;

    case CLUTTER_SCROLL_DOWN:
      y_value = DEFAULT_AXIS_STEP_DISTANCE;
      y_discrete = 1;
      break;

    case CLUTTER_SCROLL_LEFT:
      x_value = -DEFAULT_AXIS_STEP_DISTANCE;
      x_discrete = -1;
      break;

    case CLUTTER_SCROLL_RIGHT:
      x_value = DEFAULT_AXIS_STEP_DISTANCE;
      x_discrete = 1;
      break;

    case CLUTTER_SCROLL_SMOOTH:
      {
        double dx, dy;
        /* Clutter smooth scroll events are in discrete steps (1 step = 1.0 long
         * vector along one axis). To convert to smooth scroll events that are
         * in pointer motion event space, multiply the vector with the 10. */
        const double factor = 10.0;
        clutter_event_get_scroll_delta (event, &dx, &dy);
        x_value = wl_fixed_from_double (dx) * factor;
        y_value = wl_fixed_from_double (dy) * factor;
      }
      break;

    default:
      return;
    }

  if (pointer->focus_client)
    {
      wl_resource_for_each (resource, &pointer->focus_client->pointer_resources)
        {
          if (wl_resource_get_version (resource) >= WL_POINTER_AXIS_SOURCE_SINCE_VERSION)
            wl_pointer_send_axis_source (resource, source);

          /* X axis */
          if (x_discrete != 0 &&
              wl_resource_get_version (resource) >= WL_POINTER_AXIS_DISCRETE_SINCE_VERSION)
            wl_pointer_send_axis_discrete (resource,
                                           WL_POINTER_AXIS_HORIZONTAL_SCROLL,
                                           x_discrete);

          if (x_value)
            wl_pointer_send_axis (resource, clutter_event_get_time (event),
                                  WL_POINTER_AXIS_HORIZONTAL_SCROLL, x_value);

          if ((event->scroll.finish_flags & CLUTTER_SCROLL_FINISHED_HORIZONTAL) &&
              wl_resource_get_version (resource) >= WL_POINTER_AXIS_STOP_SINCE_VERSION)
            wl_pointer_send_axis_stop (resource,
                                       clutter_event_get_time (event),
                                       WL_POINTER_AXIS_HORIZONTAL_SCROLL);
          /* Y axis */
          if (y_discrete != 0 &&
              wl_resource_get_version (resource) >= WL_POINTER_AXIS_DISCRETE_SINCE_VERSION)
            wl_pointer_send_axis_discrete (resource,
                                           WL_POINTER_AXIS_VERTICAL_SCROLL,
                                           y_discrete);

          if (y_value)
            wl_pointer_send_axis (resource, clutter_event_get_time (event),
                                  WL_POINTER_AXIS_VERTICAL_SCROLL, y_value);

          if ((event->scroll.finish_flags & CLUTTER_SCROLL_FINISHED_VERTICAL) &&
              wl_resource_get_version (resource) >= WL_POINTER_AXIS_STOP_SINCE_VERSION)
            wl_pointer_send_axis_stop (resource,
                                       clutter_event_get_time (event),
                                       WL_POINTER_AXIS_VERTICAL_SCROLL);
        }

      cobiwm_wayland_pointer_broadcast_frame (pointer);
    }
}

gboolean
cobiwm_wayland_pointer_handle_event (CobiwmWaylandPointer *pointer,
                                   const ClutterEvent *event)
{
  switch (event->type)
    {
    case CLUTTER_MOTION:
      handle_motion_event (pointer, event);
      break;

    case CLUTTER_BUTTON_PRESS:
    case CLUTTER_BUTTON_RELEASE:
      handle_button_event (pointer, event);
      break;

    case CLUTTER_SCROLL:
      handle_scroll_event (pointer, event);
      break;

    case CLUTTER_TOUCHPAD_SWIPE:
      cobiwm_wayland_pointer_gesture_swipe_handle_event (pointer, event);
      break;

    case CLUTTER_TOUCHPAD_PINCH:
      cobiwm_wayland_pointer_gesture_pinch_handle_event (pointer, event);
      break;

    default:
      break;
    }

  return FALSE;
}

static void
cobiwm_wayland_pointer_send_enter (CobiwmWaylandPointer *pointer,
                                 struct wl_resource *pointer_resource,
                                 uint32_t            serial,
                                 CobiwmWaylandSurface *surface)
{
  wl_fixed_t sx, sy;

  cobiwm_wayland_pointer_get_relative_coordinates (pointer, surface, &sx, &sy);
  wl_pointer_send_enter (pointer_resource,
                         serial,
                         surface->resource,
                         sx, sy);
}

static void
cobiwm_wayland_pointer_send_leave (CobiwmWaylandPointer *pointer,
                                 struct wl_resource *pointer_resource,
                                 uint32_t            serial,
                                 CobiwmWaylandSurface *surface)
{
  wl_pointer_send_leave (pointer_resource, serial, surface->resource);
}

static void
cobiwm_wayland_pointer_broadcast_enter (CobiwmWaylandPointer *pointer,
                                      uint32_t            serial,
                                      CobiwmWaylandSurface *surface)
{
  struct wl_resource *pointer_resource;

  wl_resource_for_each (pointer_resource,
                        &pointer->focus_client->pointer_resources)
    cobiwm_wayland_pointer_send_enter (pointer, pointer_resource,
                                     serial, surface);

  cobiwm_wayland_pointer_broadcast_frame (pointer);
}

static void
cobiwm_wayland_pointer_broadcast_leave (CobiwmWaylandPointer *pointer,
                                      uint32_t            serial,
                                      CobiwmWaylandSurface *surface)
{
  struct wl_resource *pointer_resource;

  wl_resource_for_each (pointer_resource,
                        &pointer->focus_client->pointer_resources)
    cobiwm_wayland_pointer_send_leave (pointer, pointer_resource,
                                     serial, surface);

  cobiwm_wayland_pointer_broadcast_frame (pointer);
}

void
cobiwm_wayland_pointer_set_focus (CobiwmWaylandPointer *pointer,
                                CobiwmWaylandSurface *surface)
{
  if (pointer->display == NULL)
    return;

  if (pointer->focus_surface == surface)
    return;

  if (pointer->focus_surface != NULL)
    {
      struct wl_client *client =
        wl_resource_get_client (pointer->focus_surface->resource);
      struct wl_display *display = wl_client_get_display (client);
      uint32_t serial;

      serial = wl_display_next_serial (display);

      if (pointer->focus_client)
        {
          cobiwm_wayland_pointer_broadcast_leave (pointer,
                                                serial,
                                                pointer->focus_surface);
          pointer->focus_client = NULL;
        }

      wl_list_remove (&pointer->focus_surface_listener.link);
      pointer->focus_surface = NULL;
    }

  if (surface != NULL)
    {
      struct wl_client *client = wl_resource_get_client (surface->resource);
      struct wl_display *display = wl_client_get_display (client);
      ClutterPoint pos;

      pointer->focus_surface = surface;
      wl_resource_add_destroy_listener (pointer->focus_surface->resource, &pointer->focus_surface_listener);

      clutter_input_device_get_coords (pointer->device, NULL, &pos);

      if (pointer->focus_surface->window)
        cobiwm_window_handle_enter (pointer->focus_surface->window,
                                  /* XXX -- can we reliably get a timestamp for setting focus? */
                                  clutter_get_current_event_time (),
                                  pos.x, pos.y);

      pointer->focus_client =
        cobiwm_wayland_pointer_get_pointer_client (pointer, client);
      if (pointer->focus_client)
        {
          pointer->focus_serial = wl_display_next_serial (display);
          cobiwm_wayland_pointer_broadcast_enter (pointer,
                                                pointer->focus_serial,
                                                pointer->focus_surface);
        }
    }

  cobiwm_wayland_pointer_update_cursor_surface (pointer);
}

void
cobiwm_wayland_pointer_start_grab (CobiwmWaylandPointer *pointer,
                                 CobiwmWaylandPointerGrab *grab)
{
  const CobiwmWaylandPointerGrabInterface *interface;

  pointer->grab = grab;
  interface = pointer->grab->interface;
  grab->pointer = pointer;

  interface->focus (pointer->grab, pointer->current);
}

void
cobiwm_wayland_pointer_end_grab (CobiwmWaylandPointer *pointer)
{
  const CobiwmWaylandPointerGrabInterface *interface;

  pointer->grab = &pointer->default_grab;
  interface = pointer->grab->interface;
  interface->focus (pointer->grab, pointer->current);

  cobiwm_wayland_pointer_update_cursor_surface (pointer);
}

void
cobiwm_wayland_pointer_end_popup_grab (CobiwmWaylandPointer *pointer)
{
  CobiwmWaylandPopupGrab *popup_grab = (CobiwmWaylandPopupGrab*)pointer->grab;

  cobiwm_wayland_popup_grab_end (popup_grab);
  cobiwm_wayland_popup_grab_destroy (popup_grab);
}

CobiwmWaylandPopup *
cobiwm_wayland_pointer_start_popup_grab (CobiwmWaylandPointer *pointer,
				       CobiwmWaylandSurface *surface)
{
  CobiwmWaylandPopupGrab *grab;

  if (pointer->grab != &pointer->default_grab &&
      !cobiwm_wayland_pointer_grab_is_popup_grab (pointer->grab))
    return NULL;

  if (pointer->grab == &pointer->default_grab)
    {
      struct wl_client *client = wl_resource_get_client (surface->resource);

      grab = cobiwm_wayland_popup_grab_create (pointer, client);
      cobiwm_wayland_popup_grab_begin (grab, surface);
    }
  else
    grab = (CobiwmWaylandPopupGrab*)pointer->grab;

  return cobiwm_wayland_popup_create (surface, grab);
}

void
cobiwm_wayland_pointer_repick (CobiwmWaylandPointer *pointer)
{
  repick_for_event (pointer, NULL);
}

void
cobiwm_wayland_pointer_get_relative_coordinates (CobiwmWaylandPointer *pointer,
					       CobiwmWaylandSurface *surface,
					       wl_fixed_t         *sx,
					       wl_fixed_t         *sy)
{
  float xf = 0.0f, yf = 0.0f;
  ClutterPoint pos;

  clutter_input_device_get_coords (pointer->device, NULL, &pos);
  cobiwm_wayland_surface_get_relative_coordinates (surface, pos.x, pos.y, &xf, &yf);

  *sx = wl_fixed_from_double (xf);
  *sy = wl_fixed_from_double (yf);
}

static void
cobiwm_wayland_pointer_update_cursor_surface (CobiwmWaylandPointer *pointer)
{
  CobiwmCursorTracker *cursor_tracker = cobiwm_cursor_tracker_get_for_screen (NULL);

  if (pointer->current)
    {
      CobiwmCursorSprite *cursor_sprite = NULL;

      if (pointer->cursor_surface)
        {
          CobiwmWaylandSurfaceRoleCursor *cursor_role =
            COBIWM_WAYLAND_SURFACE_ROLE_CURSOR (pointer->cursor_surface->role);

          cursor_sprite = cursor_role->cursor_sprite;
        }

      cobiwm_cursor_tracker_set_window_cursor (cursor_tracker, cursor_sprite);
    }
  else
    {
      cobiwm_cursor_tracker_unset_window_cursor (cursor_tracker);
    }
}

static void
update_cursor_sprite_texture (CobiwmWaylandSurface *surface)
{
  CobiwmCursorRenderer *cursor_renderer =
    cobiwm_backend_get_cursor_renderer (cobiwm_get_backend ());
  CobiwmCursorTracker *cursor_tracker = cobiwm_cursor_tracker_get_for_screen (NULL);
  CobiwmWaylandSurfaceRoleCursor *cursor_role =
    COBIWM_WAYLAND_SURFACE_ROLE_CURSOR (surface->role);
  CobiwmCursorSprite *cursor_sprite = cursor_role->cursor_sprite;
  CobiwmWaylandBuffer *buffer = cobiwm_wayland_surface_get_buffer (surface);

  g_return_if_fail (!buffer || buffer->texture);

  if (buffer)
    {
      cobiwm_cursor_sprite_set_texture (cursor_sprite,
                                      buffer->texture,
                                      cursor_role->hot_x * surface->scale,
                                      cursor_role->hot_y * surface->scale);

      if (cursor_role->buffer)
        {
          struct wl_resource *buffer_resource;

          g_assert (cursor_role->buffer == buffer);
          buffer_resource = buffer->resource;
          cobiwm_cursor_renderer_realize_cursor_from_wl_buffer (cursor_renderer,
                                                              cursor_sprite,
                                                              buffer_resource);

          cobiwm_wayland_surface_unref_buffer_use_count (surface);
          g_clear_object (&cursor_role->buffer);
        }
    }
  else
    {
      cobiwm_cursor_sprite_set_texture (cursor_sprite, NULL, 0, 0);
    }

  if (cursor_sprite == cobiwm_cursor_tracker_get_displayed_cursor (cursor_tracker))
    cobiwm_cursor_renderer_force_update (cursor_renderer);
}

static void
cursor_sprite_prepare_at (CobiwmCursorSprite *cursor_sprite,
                          int x,
                          int y,
                          CobiwmWaylandSurfaceRoleCursor *cursor_role)
{
  CobiwmWaylandSurfaceRole *role = COBIWM_WAYLAND_SURFACE_ROLE (cursor_role);
  CobiwmWaylandSurface *surface = cobiwm_wayland_surface_role_get_surface (role);
  CobiwmDisplay *display = cobiwm_get_display ();
  CobiwmScreen *screen = display->screen;
  const CobiwmMonitorInfo *monitor;

  if (!cobiwm_xwayland_is_xwayland_surface (surface))
    {
      monitor = cobiwm_screen_get_monitor_for_point (screen, x, y);
      if (monitor)
        cobiwm_cursor_sprite_set_texture_scale (cursor_sprite,
                                              (float)monitor->scale / surface->scale);
    }
  cobiwm_wayland_surface_update_outputs (surface);
}

static void
cobiwm_wayland_pointer_set_cursor_surface (CobiwmWaylandPointer *pointer,
                                         CobiwmWaylandSurface *cursor_surface)
{
  CobiwmWaylandSurface *prev_cursor_surface;

  prev_cursor_surface = pointer->cursor_surface;
  pointer->cursor_surface = cursor_surface;

  if (prev_cursor_surface != cursor_surface)
    {
      if (prev_cursor_surface)
        cobiwm_wayland_surface_update_outputs (prev_cursor_surface);
      cobiwm_wayland_pointer_update_cursor_surface (pointer);
    }
}

static void
pointer_set_cursor (struct wl_client *client,
                    struct wl_resource *resource,
                    uint32_t serial,
                    struct wl_resource *surface_resource,
                    int32_t hot_x, int32_t hot_y)
{
  CobiwmWaylandPointer *pointer = wl_resource_get_user_data (resource);
  CobiwmWaylandSurface *surface;

  surface = (surface_resource ? wl_resource_get_user_data (surface_resource) : NULL);

  if (pointer->focus_surface == NULL)
    return;
  if (wl_resource_get_client (pointer->focus_surface->resource) != client)
    return;
  if (pointer->focus_serial - serial > G_MAXUINT32 / 2)
    return;

  if (surface &&
      !cobiwm_wayland_surface_assign_role (surface,
                                         COBIWM_TYPE_WAYLAND_SURFACE_ROLE_CURSOR))
    {
      wl_resource_post_error (resource, WL_POINTER_ERROR_ROLE,
                              "wl_surface@%d already has a different role",
                              wl_resource_get_id (surface_resource));
      return;
    }

  if (surface)
    {
      CobiwmWaylandSurfaceRoleCursor *cursor_role;

      cursor_role = COBIWM_WAYLAND_SURFACE_ROLE_CURSOR (surface->role);
      if (!cursor_role->cursor_sprite)
        {
          cursor_role->cursor_sprite = cobiwm_cursor_sprite_new ();
          g_signal_connect_object (cursor_role->cursor_sprite,
                                   "prepare-at",
                                   G_CALLBACK (cursor_sprite_prepare_at),
                                   cursor_role,
                                   0);
        }

      cursor_role->hot_x = hot_x;
      cursor_role->hot_y = hot_y;

      update_cursor_sprite_texture (surface);
    }

  cobiwm_wayland_pointer_set_cursor_surface (pointer, surface);
}

static void
pointer_release (struct wl_client *client,
                 struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static const struct wl_pointer_interface pointer_interface = {
  pointer_set_cursor,
  pointer_release,
};

void
cobiwm_wayland_pointer_create_new_resource (CobiwmWaylandPointer *pointer,
                                          struct wl_client   *client,
                                          struct wl_resource *seat_resource,
                                          uint32_t id)
{
  struct wl_resource *cr;
  CobiwmWaylandPointerClient *pointer_client;

  cr = wl_resource_create (client, &wl_pointer_interface, wl_resource_get_version (seat_resource), id);
  wl_resource_set_implementation (cr, &pointer_interface, pointer,
                                  cobiwm_wayland_pointer_unbind_pointer_client_resource);

  pointer_client = cobiwm_wayland_pointer_ensure_pointer_client (pointer, client);

  wl_list_insert (&pointer_client->pointer_resources,
                  wl_resource_get_link (cr));

  if (pointer->focus_client == pointer_client)
    {
      cobiwm_wayland_pointer_send_enter (pointer, cr,
                                       pointer->focus_serial,
                                       pointer->focus_surface);
      cobiwm_wayland_pointer_send_frame (pointer, cr);
    }
}

gboolean
cobiwm_wayland_pointer_can_grab_surface (CobiwmWaylandPointer *pointer,
                                       CobiwmWaylandSurface *surface,
                                       uint32_t            serial)
{
  return (pointer->grab_serial == serial &&
          pointer->focus_surface == surface);
}

gboolean
cobiwm_wayland_pointer_can_popup (CobiwmWaylandPointer *pointer, uint32_t serial)
{
  return pointer->grab_serial == serial;
}

CobiwmWaylandSurface *
cobiwm_wayland_pointer_get_top_popup (CobiwmWaylandPointer *pointer)
{
  CobiwmWaylandPopupGrab *grab;

  if (!cobiwm_wayland_pointer_grab_is_popup_grab (pointer->grab))
    return NULL;

  grab = (CobiwmWaylandPopupGrab*)pointer->grab;
  return cobiwm_wayland_popup_grab_get_top_popup(grab);
}

static void
relative_pointer_destroy (struct wl_client *client,
                          struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static const struct zwp_relative_pointer_v1_interface relative_pointer_interface = {
  relative_pointer_destroy
};

static void
relative_pointer_manager_destroy (struct wl_client *client,
                                  struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
relative_pointer_manager_get_relative_pointer (struct wl_client   *client,
                                               struct wl_resource *resource,
                                               uint32_t            id,
                                               struct wl_resource *pointer_resource)
{
  CobiwmWaylandPointer *pointer = wl_resource_get_user_data (pointer_resource);
  struct wl_resource *cr;
  CobiwmWaylandPointerClient *pointer_client;

  cr = wl_resource_create (client, &zwp_relative_pointer_v1_interface,
                           wl_resource_get_version (resource), id);
  if (cr == NULL)
    {
      wl_client_post_no_memory (client);
      return;
    }

  wl_resource_set_implementation (cr, &relative_pointer_interface,
                                  pointer,
                                  cobiwm_wayland_pointer_unbind_pointer_client_resource);

  pointer_client = cobiwm_wayland_pointer_ensure_pointer_client (pointer, client);

  wl_list_insert (&pointer_client->relative_pointer_resources,
                  wl_resource_get_link (cr));
}

static const struct zwp_relative_pointer_manager_v1_interface relative_pointer_manager = {
  relative_pointer_manager_destroy,
  relative_pointer_manager_get_relative_pointer,
};

static void
bind_relative_pointer_manager (struct wl_client *client,
                               void             *data,
                               uint32_t          version,
                               uint32_t          id)
{
  CobiwmWaylandCompositor *compositor = data;
  struct wl_resource *resource;

  resource = wl_resource_create (client,
                                 &zwp_relative_pointer_manager_v1_interface,
                                 1, id);

  if (version != 1)
    wl_resource_post_error (resource,
                            WL_DISPLAY_ERROR_INVALID_OBJECT,
                            "bound invalid version %u of "
                            "wp_relative_pointer_manager",
                            version);

  wl_resource_set_implementation (resource, &relative_pointer_manager,
                                  compositor,
                                  NULL);
}

void
cobiwm_wayland_relative_pointer_init (CobiwmWaylandCompositor *compositor)
{
  /* Relative pointer events are currently only supported by the native backend
   * so lets just advertise the extension when the native backend is used.
   */
#ifdef HAVE_NATIVE_BACKEND
  if (!COBIWM_IS_BACKEND_NATIVE (cobiwm_get_backend ()))
    return;
#else
  return;
#endif

  if (!wl_global_create (compositor->wayland_display,
                         &zwp_relative_pointer_manager_v1_interface, 1,
                         compositor, bind_relative_pointer_manager))
    g_error ("Could not create relative pointer manager global");
}

CobiwmWaylandSeat *
cobiwm_wayland_pointer_get_seat (CobiwmWaylandPointer *pointer)
{
  CobiwmWaylandSeat *seat = wl_container_of (pointer, seat, pointer);
  return seat;
}

static void
cursor_surface_role_assigned (CobiwmWaylandSurfaceRole *surface_role)
{
  CobiwmWaylandSurfaceRoleCursor *cursor_role =
    COBIWM_WAYLAND_SURFACE_ROLE_CURSOR (surface_role);
  CobiwmWaylandSurface *surface =
    cobiwm_wayland_surface_role_get_surface (surface_role);
  CobiwmWaylandBuffer *buffer = cobiwm_wayland_surface_get_buffer (surface);

  if (buffer)
    {
      g_set_object (&cursor_role->buffer, buffer);
      cobiwm_wayland_surface_ref_buffer_use_count (surface);
    }

  cobiwm_wayland_surface_queue_pending_frame_callbacks (surface);
}

static void
cursor_surface_role_pre_commit (CobiwmWaylandSurfaceRole  *surface_role,
                                CobiwmWaylandPendingState *pending)
{
  CobiwmWaylandSurfaceRoleCursor *cursor_role =
    COBIWM_WAYLAND_SURFACE_ROLE_CURSOR (surface_role);
  CobiwmWaylandSurface *surface =
    cobiwm_wayland_surface_role_get_surface (surface_role);

  if (pending->newly_attached && cursor_role->buffer)
    {
      cobiwm_wayland_surface_unref_buffer_use_count (surface);
      g_clear_object (&cursor_role->buffer);
    }
}

static void
cursor_surface_role_commit (CobiwmWaylandSurfaceRole  *surface_role,
                            CobiwmWaylandPendingState *pending)
{
  CobiwmWaylandSurfaceRoleCursor *cursor_role =
    COBIWM_WAYLAND_SURFACE_ROLE_CURSOR (surface_role);
  CobiwmWaylandSurface *surface =
    cobiwm_wayland_surface_role_get_surface (surface_role);
  CobiwmWaylandBuffer *buffer = cobiwm_wayland_surface_get_buffer (surface);

  if (pending->newly_attached)
    {
      g_set_object (&cursor_role->buffer, buffer);
      if (cursor_role->buffer)
        cobiwm_wayland_surface_ref_buffer_use_count (surface);
    }

  cobiwm_wayland_surface_queue_pending_state_frame_callbacks (surface, pending);

  if (pending->newly_attached)
    update_cursor_sprite_texture (surface);
}

static gboolean
cursor_surface_role_is_on_output (CobiwmWaylandSurfaceRole *role,
                                  CobiwmMonitorInfo        *monitor)
{
  CobiwmWaylandSurface *surface =
    cobiwm_wayland_surface_role_get_surface (role);
  CobiwmWaylandPointer *pointer = &surface->compositor->seat->pointer;
  CobiwmCursorTracker *cursor_tracker = cobiwm_cursor_tracker_get_for_screen (NULL);
  CobiwmCursorRenderer *cursor_renderer =
    cobiwm_backend_get_cursor_renderer (cobiwm_get_backend ());
  CobiwmWaylandSurfaceRoleCursor *cursor_role =
    COBIWM_WAYLAND_SURFACE_ROLE_CURSOR (surface->role);
  CobiwmCursorSprite *displayed_cursor_sprite;
  CobiwmRectangle rect;

  if (surface != pointer->cursor_surface)
    return FALSE;

  displayed_cursor_sprite =
    cobiwm_cursor_tracker_get_displayed_cursor (cursor_tracker);
  if (!displayed_cursor_sprite)
    return FALSE;

  if (cursor_role->cursor_sprite != displayed_cursor_sprite)
    return FALSE;

  rect = cobiwm_cursor_renderer_calculate_rect (cursor_renderer,
                                              cursor_role->cursor_sprite);
  return cobiwm_rectangle_overlap (&rect, &monitor->rect);
}

static void
cursor_surface_role_dispose (GObject *object)
{
  CobiwmWaylandSurfaceRoleCursor *cursor_role =
    COBIWM_WAYLAND_SURFACE_ROLE_CURSOR (object);
  CobiwmWaylandSurface *surface =
    cobiwm_wayland_surface_role_get_surface (COBIWM_WAYLAND_SURFACE_ROLE (object));
  CobiwmWaylandCompositor *compositor = cobiwm_wayland_compositor_get_default ();
  CobiwmWaylandPointer *pointer = &compositor->seat->pointer;

  if (pointer->cursor_surface == surface)
    pointer->cursor_surface = NULL;
  cobiwm_wayland_pointer_update_cursor_surface (pointer);

  g_clear_object (&cursor_role->cursor_sprite);

  if (cursor_role->buffer)
    {
      cobiwm_wayland_surface_unref_buffer_use_count (surface);
      g_clear_object (&cursor_role->buffer);
    }

  G_OBJECT_CLASS (cobiwm_wayland_surface_role_cursor_parent_class)->dispose (object);
}

static void
cobiwm_wayland_surface_role_cursor_init (CobiwmWaylandSurfaceRoleCursor *role)
{
}

static void
cobiwm_wayland_surface_role_cursor_class_init (CobiwmWaylandSurfaceRoleCursorClass *klass)
{
  CobiwmWaylandSurfaceRoleClass *surface_role_class =
    COBIWM_WAYLAND_SURFACE_ROLE_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  surface_role_class->assigned = cursor_surface_role_assigned;
  surface_role_class->pre_commit = cursor_surface_role_pre_commit;
  surface_role_class->commit = cursor_surface_role_commit;
  surface_role_class->is_on_output = cursor_surface_role_is_on_output;

  object_class->dispose = cursor_surface_role_dispose;
}
