/*
 * Wayland Support
 *
 * Copyright (C) 2013 Intel Corporation
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

#include "config.h"

#include "cobiwm-wayland-seat.h"

#include "cobiwm-wayland-private.h"
#include "cobiwm-wayland-versions.h"
#include "cobiwm-wayland-data-device.h"

#define CAPABILITY_ENABLED(prev, cur, capability) ((cur & (capability)) && !(prev & (capability)))
#define CAPABILITY_DISABLED(prev, cur, capability) ((prev & (capability)) && !(cur & (capability)))

static void
unbind_resource (struct wl_resource *resource)
{
  wl_list_remove (wl_resource_get_link (resource));
}

static void
seat_get_pointer (struct wl_client *client,
                  struct wl_resource *resource,
                  uint32_t id)
{
  CobiwmWaylandSeat *seat = wl_resource_get_user_data (resource);
  CobiwmWaylandPointer *pointer = &seat->pointer;

  if ((seat->capabilities & WL_SEAT_CAPABILITY_POINTER) != 0)
    cobiwm_wayland_pointer_create_new_resource (pointer, client, resource, id);
}

static void
seat_get_keyboard (struct wl_client *client,
                   struct wl_resource *resource,
                   uint32_t id)
{
  CobiwmWaylandSeat *seat = wl_resource_get_user_data (resource);
  CobiwmWaylandKeyboard *keyboard = &seat->keyboard;

  if ((seat->capabilities & WL_SEAT_CAPABILITY_KEYBOARD) != 0)
    cobiwm_wayland_keyboard_create_new_resource (keyboard, client, resource, id);
}

static void
seat_get_touch (struct wl_client *client,
                struct wl_resource *resource,
                uint32_t id)
{
  CobiwmWaylandSeat *seat = wl_resource_get_user_data (resource);
  CobiwmWaylandTouch *touch = &seat->touch;

  if ((seat->capabilities & WL_SEAT_CAPABILITY_TOUCH) != 0)
    cobiwm_wayland_touch_create_new_resource (touch, client, resource, id);
}

static const struct wl_seat_interface seat_interface = {
  seat_get_pointer,
  seat_get_keyboard,
  seat_get_touch
};

static void
bind_seat (struct wl_client *client,
           void *data,
           guint32 version,
           guint32 id)
{
  CobiwmWaylandSeat *seat = data;
  struct wl_resource *resource;

  resource = wl_resource_create (client, &wl_seat_interface, version, id);
  wl_resource_set_implementation (resource, &seat_interface, seat, unbind_resource);
  wl_list_insert (&seat->base_resource_list, wl_resource_get_link (resource));

  wl_seat_send_capabilities (resource, seat->capabilities);

  if (version >= WL_SEAT_NAME_SINCE_VERSION)
    wl_seat_send_name (resource, "seat0");
}

static uint32_t
lookup_device_capabilities (ClutterDeviceManager *device_manager)
{
  const GSList *devices, *l;
  uint32_t capabilities = 0;

  devices = clutter_device_manager_peek_devices (device_manager);

  for (l = devices; l; l = l->next)
    {
      ClutterInputDeviceType device_type;

      /* Only look for physical devices, master devices have rather generic
       * keyboard/pointer device types, which is not truly representative of
       * the slave devices connected to them.
       */
      if (clutter_input_device_get_device_mode (l->data) == CLUTTER_INPUT_MODE_MASTER)
        continue;

      device_type = clutter_input_device_get_device_type (l->data);

      switch (device_type)
        {
        case CLUTTER_TOUCHPAD_DEVICE:
        case CLUTTER_POINTER_DEVICE:
          capabilities |= WL_SEAT_CAPABILITY_POINTER;
          break;
        case CLUTTER_KEYBOARD_DEVICE:
          capabilities |= WL_SEAT_CAPABILITY_KEYBOARD;
          break;
        case CLUTTER_TOUCHSCREEN_DEVICE:
          capabilities |= WL_SEAT_CAPABILITY_TOUCH;
          break;
        default:
          g_debug ("Ignoring device '%s' with unhandled type %d",
                   clutter_input_device_get_device_name (l->data),
                   device_type);
          break;
        }
    }

  return capabilities;
}

static void
cobiwm_wayland_seat_set_capabilities (CobiwmWaylandSeat *seat,
                                    uint32_t         flags)
{
  struct wl_resource *resource;
  uint32_t prev_flags;

  prev_flags = seat->capabilities;

  if (prev_flags == flags)
    return;

  seat->capabilities = flags;

  if (CAPABILITY_ENABLED (prev_flags, flags, WL_SEAT_CAPABILITY_POINTER))
    cobiwm_wayland_pointer_init (&seat->pointer, seat->wl_display);
  else if (CAPABILITY_DISABLED (prev_flags, flags, WL_SEAT_CAPABILITY_POINTER))
    cobiwm_wayland_pointer_release (&seat->pointer);

  if (CAPABILITY_ENABLED (prev_flags, flags, WL_SEAT_CAPABILITY_KEYBOARD))
    {
      CobiwmDisplay *display;

      cobiwm_wayland_keyboard_init (&seat->keyboard, seat->wl_display);
      display = cobiwm_get_display ();

      /* Post-initialization, ensure the input focus is in sync */
      if (display)
        cobiwm_display_sync_wayland_input_focus (display);
    }
  else if (CAPABILITY_DISABLED (prev_flags, flags, WL_SEAT_CAPABILITY_KEYBOARD))
    cobiwm_wayland_keyboard_release (&seat->keyboard);

  if (CAPABILITY_ENABLED (prev_flags, flags, WL_SEAT_CAPABILITY_TOUCH))
    cobiwm_wayland_touch_init (&seat->touch, seat->wl_display);
  else if (CAPABILITY_DISABLED (prev_flags, flags, WL_SEAT_CAPABILITY_TOUCH))
    cobiwm_wayland_touch_release (&seat->touch);

  /* Broadcast capability changes */
  wl_resource_for_each (resource, &seat->base_resource_list)
    {
      wl_seat_send_capabilities (resource, flags);
    }
}

static void
cobiwm_wayland_seat_update_capabilities (CobiwmWaylandSeat      *seat,
                                       ClutterDeviceManager *device_manager)
{
  uint32_t capabilities;

  capabilities = lookup_device_capabilities (device_manager);
  cobiwm_wayland_seat_set_capabilities (seat, capabilities);
}

static void
cobiwm_wayland_seat_devices_updated (ClutterDeviceManager *device_manager,
                                   ClutterInputDevice   *input_device,
                                   CobiwmWaylandSeat      *seat)
{
  cobiwm_wayland_seat_update_capabilities (seat, device_manager);
}

static CobiwmWaylandSeat *
cobiwm_wayland_seat_new (struct wl_display *display)
{
  CobiwmWaylandSeat *seat = g_new0 (CobiwmWaylandSeat, 1);
  ClutterDeviceManager *device_manager;

  wl_list_init (&seat->base_resource_list);
  seat->wl_display = display;

  cobiwm_wayland_data_device_init (&seat->data_device);

  device_manager = clutter_device_manager_get_default ();
  cobiwm_wayland_seat_update_capabilities (seat, device_manager);
  g_signal_connect (device_manager, "device-added",
                    G_CALLBACK (cobiwm_wayland_seat_devices_updated), seat);
  g_signal_connect (device_manager, "device-removed",
                    G_CALLBACK (cobiwm_wayland_seat_devices_updated), seat);

  wl_global_create (display, &wl_seat_interface, COBIWM_WL_SEAT_VERSION, seat, bind_seat);

  return seat;
}

void
cobiwm_wayland_seat_init (CobiwmWaylandCompositor *compositor)
{
  compositor->seat = cobiwm_wayland_seat_new (compositor->wayland_display);
}

void
cobiwm_wayland_seat_free (CobiwmWaylandSeat *seat)
{
  ClutterDeviceManager *device_manager;

  device_manager = clutter_device_manager_get_default ();
  g_signal_handlers_disconnect_by_data (device_manager, seat);
  cobiwm_wayland_seat_set_capabilities (seat, 0);

  g_slice_free (CobiwmWaylandSeat, seat);
}

static gboolean
event_from_supported_hardware_device (CobiwmWaylandSeat    *seat,
                                      const ClutterEvent *event)
{
  ClutterInputDevice     *input_device;
  ClutterInputMode        input_mode;
  ClutterInputDeviceType  device_type;
  gboolean                hardware_device = FALSE;
  gboolean                supported_device = FALSE;

  input_device = clutter_event_get_source_device (event);

  if (input_device == NULL)
    goto out;

  input_mode = clutter_input_device_get_device_mode (input_device);

  if (input_mode != CLUTTER_INPUT_MODE_SLAVE)
    goto out;

  hardware_device = TRUE;

  device_type = clutter_input_device_get_device_type (input_device);

  switch (device_type)
    {
    case CLUTTER_TOUCHPAD_DEVICE:
    case CLUTTER_POINTER_DEVICE:
    case CLUTTER_KEYBOARD_DEVICE:
    case CLUTTER_TOUCHSCREEN_DEVICE:
      supported_device = TRUE;
      break;

    default:
      supported_device = FALSE;
      break;
    }

out:
  return hardware_device && supported_device;
}

void
cobiwm_wayland_seat_update (CobiwmWaylandSeat    *seat,
                          const ClutterEvent *event)
{
  if (!event_from_supported_hardware_device (seat, event))
    return;

  switch (event->type)
    {
    case CLUTTER_MOTION:
    case CLUTTER_BUTTON_PRESS:
    case CLUTTER_BUTTON_RELEASE:
    case CLUTTER_SCROLL:
      cobiwm_wayland_pointer_update (&seat->pointer, event);
      break;

    case CLUTTER_KEY_PRESS:
    case CLUTTER_KEY_RELEASE:
      cobiwm_wayland_keyboard_update (&seat->keyboard, (const ClutterKeyEvent *) event);
      break;

    case CLUTTER_TOUCH_BEGIN:
    case CLUTTER_TOUCH_UPDATE:
    case CLUTTER_TOUCH_END:
      cobiwm_wayland_touch_update (&seat->touch, event);
      break;

    default:
      break;
    }
}

gboolean
cobiwm_wayland_seat_handle_event (CobiwmWaylandSeat *seat,
                                const ClutterEvent *event)
{
  if (!event_from_supported_hardware_device (seat, event))
    return FALSE;

  switch (event->type)
    {
    case CLUTTER_MOTION:
    case CLUTTER_BUTTON_PRESS:
    case CLUTTER_BUTTON_RELEASE:
    case CLUTTER_SCROLL:
    case CLUTTER_TOUCHPAD_SWIPE:
    case CLUTTER_TOUCHPAD_PINCH:
      return cobiwm_wayland_pointer_handle_event (&seat->pointer, event);

    case CLUTTER_KEY_PRESS:
    case CLUTTER_KEY_RELEASE:
      return cobiwm_wayland_keyboard_handle_event (&seat->keyboard,
                                                 (const ClutterKeyEvent *) event);
    case CLUTTER_TOUCH_BEGIN:
    case CLUTTER_TOUCH_UPDATE:
    case CLUTTER_TOUCH_END:
      return cobiwm_wayland_touch_handle_event (&seat->touch, event);

    default:
      break;
    }

  return FALSE;
}

void
cobiwm_wayland_seat_repick (CobiwmWaylandSeat *seat)
{
  if ((seat->capabilities & WL_SEAT_CAPABILITY_POINTER) == 0)
    return;

  cobiwm_wayland_pointer_repick (&seat->pointer);
}

void
cobiwm_wayland_seat_set_input_focus (CobiwmWaylandSeat    *seat,
                                   CobiwmWaylandSurface *surface)
{
  if ((seat->capabilities & WL_SEAT_CAPABILITY_KEYBOARD) == 0)
    return;

  cobiwm_wayland_keyboard_set_focus (&seat->keyboard, surface);
  cobiwm_wayland_data_device_set_keyboard_focus (&seat->data_device);
}

gboolean
cobiwm_wayland_seat_get_grab_info (CobiwmWaylandSeat    *seat,
                                 CobiwmWaylandSurface *surface,
                                 uint32_t            serial,
                                 gboolean            require_pressed,
                                 gfloat             *x,
                                 gfloat             *y)
{
  ClutterEventSequence *sequence = NULL;
  gboolean can_grab_surface = FALSE;

  if ((seat->capabilities & WL_SEAT_CAPABILITY_TOUCH) != 0)
    sequence = cobiwm_wayland_touch_find_grab_sequence (&seat->touch, surface, serial);

  if (sequence)
    {
      cobiwm_wayland_touch_get_press_coords (&seat->touch, sequence, x, y);
    }
  else
    {
      if ((seat->capabilities & WL_SEAT_CAPABILITY_POINTER) != 0 &&
          (!require_pressed || seat->pointer.button_count > 0))
        can_grab_surface = cobiwm_wayland_pointer_can_grab_surface (&seat->pointer, surface, serial);

      if (can_grab_surface)
        {
          if (x)
            *x = seat->pointer.grab_x;
          if (y)
            *y = seat->pointer.grab_y;
        }
    }

  return sequence || can_grab_surface;
}

gboolean
cobiwm_wayland_seat_can_popup (CobiwmWaylandSeat *seat,
                             uint32_t         serial)
{
  return (cobiwm_wayland_pointer_can_popup (&seat->pointer, serial) ||
          cobiwm_wayland_keyboard_can_popup (&seat->keyboard, serial) ||
          cobiwm_wayland_touch_can_popup (&seat->touch, serial));
}
