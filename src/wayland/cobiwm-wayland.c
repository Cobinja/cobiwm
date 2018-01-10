/*
 * Wayland Support
 *
 * Copyright (C) 2012,2013 Intel Corporation
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

#include "cobiwm-wayland.h"

#include <clutter/clutter.h>
#include <clutter/wayland/clutter-wayland-compositor.h>
#include <clutter/wayland/clutter-wayland-surface.h>

#include <sys/time.h>
#include <string.h>
#include <stdlib.h>

#include <wayland-server.h>

#include "cobiwm-wayland-private.h"
#include "cobiwm-xwayland-private.h"
#include "cobiwm-wayland-region.h"
#include "cobiwm-wayland-seat.h"
#include "cobiwm-wayland-outputs.h"
#include "cobiwm-wayland-data-device.h"

static CobiwmWaylandCompositor _cobiwm_wayland_compositor;

CobiwmWaylandCompositor *
cobiwm_wayland_compositor_get_default (void)
{
  return &_cobiwm_wayland_compositor;
}

typedef struct
{
  GSource source;
  struct wl_display *display;
} WaylandEventSource;

static gboolean
wayland_event_source_prepare (GSource *base, int *timeout)
{
  WaylandEventSource *source = (WaylandEventSource *)base;

  *timeout = -1;

  wl_display_flush_clients (source->display);

  return FALSE;
}

static gboolean
wayland_event_source_dispatch (GSource *base,
                               GSourceFunc callback,
                               void *data)
{
  WaylandEventSource *source = (WaylandEventSource *)base;
  struct wl_event_loop *loop = wl_display_get_event_loop (source->display);

  wl_event_loop_dispatch (loop, 0);

  return TRUE;
}

static GSourceFuncs wayland_event_source_funcs =
{
  wayland_event_source_prepare,
  NULL,
  wayland_event_source_dispatch,
  NULL
};

static GSource *
wayland_event_source_new (struct wl_display *display)
{
  WaylandEventSource *source;
  struct wl_event_loop *loop = wl_display_get_event_loop (display);

  source = (WaylandEventSource *) g_source_new (&wayland_event_source_funcs,
                                                sizeof (WaylandEventSource));
  source->display = display;
  g_source_add_unix_fd (&source->source,
                        wl_event_loop_get_fd (loop),
                        G_IO_IN | G_IO_ERR);

  return &source->source;
}

void
cobiwm_wayland_compositor_set_input_focus (CobiwmWaylandCompositor *compositor,
                                         CobiwmWindow            *window)
{
  CobiwmWaylandSurface *surface = window ? window->surface : NULL;

  cobiwm_wayland_seat_set_input_focus (compositor->seat, surface);
}

void
cobiwm_wayland_compositor_repick (CobiwmWaylandCompositor *compositor)
{
  cobiwm_wayland_seat_repick (compositor->seat);
}

static void
wl_compositor_create_surface (struct wl_client *client,
                              struct wl_resource *resource,
                              guint32 id)
{
  CobiwmWaylandCompositor *compositor = wl_resource_get_user_data (resource);
  cobiwm_wayland_surface_create (compositor, client, resource, id);
}

static void
wl_compositor_create_region (struct wl_client *client,
                             struct wl_resource *resource,
                             uint32_t id)
{
  CobiwmWaylandCompositor *compositor = wl_resource_get_user_data (resource);
  cobiwm_wayland_region_create (compositor, client, resource, id);
}

static const struct wl_compositor_interface cobiwm_wayland_wl_compositor_interface = {
  wl_compositor_create_surface,
  wl_compositor_create_region
};

static void
compositor_bind (struct wl_client *client,
		 void *data,
                 guint32 version,
                 guint32 id)
{
  CobiwmWaylandCompositor *compositor = data;
  struct wl_resource *resource;

  resource = wl_resource_create (client, &wl_compositor_interface, version, id);
  wl_resource_set_implementation (resource, &cobiwm_wayland_wl_compositor_interface, compositor, NULL);
}

/**
 * cobiwm_wayland_compositor_update:
 * @compositor: the #CobiwmWaylandCompositor instance
 * @event: the #ClutterEvent used to update @seat's state
 *
 * This is used to update display server state like updating cursor
 * position and keeping track of buttons and keys pressed. It must be
 * called for all input events coming from the underlying devices.
 */
void
cobiwm_wayland_compositor_update (CobiwmWaylandCompositor *compositor,
                                const ClutterEvent    *event)
{
  cobiwm_wayland_seat_update (compositor->seat, event);
}

void
cobiwm_wayland_compositor_paint_finished (CobiwmWaylandCompositor *compositor)
{
  while (!wl_list_empty (&compositor->frame_callbacks))
    {
      CobiwmWaylandFrameCallback *callback =
        wl_container_of (compositor->frame_callbacks.next, callback, link);

      wl_callback_send_done (callback->resource, g_get_monotonic_time () / 1000);
      wl_resource_destroy (callback->resource);
    }
}

/**
 * cobiwm_wayland_compositor_handle_event:
 * @compositor: the #CobiwmWaylandCompositor instance
 * @event: the #ClutterEvent to be sent
 *
 * This method sends events to the focused wayland client, if any.
 *
 * Return value: whether @event was sent to a wayland client.
 */
gboolean
cobiwm_wayland_compositor_handle_event (CobiwmWaylandCompositor *compositor,
                                      const ClutterEvent    *event)
{
  return cobiwm_wayland_seat_handle_event (compositor->seat, event);
}

/* cobiwm_wayland_compositor_update_key_state:
 * @compositor: the #CobiwmWaylandCompositor
 * @key_vector: bit vector of key states
 * @key_vector_len: length of @key_vector
 * @offset: the key for the first evdev keycode is found at this offset in @key_vector
 *
 * This function is used to resynchronize the key state that Cobiwm
 * is tracking with the actual keyboard state. This is useful, for example,
 * to handle changes in key state when a nested compositor doesn't
 * have focus. We need to fix up the XKB modifier tracking and deliver
 * any modifier changes to clients.
 */
void
cobiwm_wayland_compositor_update_key_state (CobiwmWaylandCompositor *compositor,
                                          char                  *key_vector,
                                          int                    key_vector_len,
                                          int                    offset)
{
  cobiwm_wayland_keyboard_update_key_state (&compositor->seat->keyboard,
                                          key_vector, key_vector_len, offset);
}

void
cobiwm_wayland_compositor_destroy_frame_callbacks (CobiwmWaylandCompositor *compositor,
                                                 CobiwmWaylandSurface    *surface)
{
  CobiwmWaylandFrameCallback *callback, *next;

  wl_list_for_each_safe (callback, next, &compositor->frame_callbacks, link)
    {
      if (callback->surface == surface)
        wl_resource_destroy (callback->resource);
    }
}

static void
set_gnome_env (const char *name,
	       const char *value)
{
  GDBusConnection *session_bus;
  GError *error = NULL;

  setenv (name, value, TRUE);

  session_bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, NULL);
  g_assert (session_bus);

  g_dbus_connection_call_sync (session_bus,
			       "org.cinnamon.SessionManager",
			       "/org/cinnamon/SessionManager",
			       "org.cinnamon.SessionManager",
			       "Setenv",
			       g_variant_new ("(ss)", name, value),
			       NULL,
			       G_DBUS_CALL_FLAGS_NO_AUTO_START,
			       -1, NULL, &error);
  if (error)
    {
      if (g_strcmp0 (g_dbus_error_get_remote_error (error), "org.cinnamon.SessionManager.NotInInitialization") != 0)
        cobiwm_warning ("Failed to set environment variable %s for gnome-session: %s\n", name, error->message);

      g_error_free (error);
    }
}

static void cobiwm_wayland_log_func (const char *, va_list) G_GNUC_PRINTF (1, 0);

static void
cobiwm_wayland_log_func (const char *fmt,
                       va_list     arg)
{
  char *str = g_strdup_vprintf (fmt, arg);
  g_warning ("WL: %s", str);
  g_free (str);
}

static void
cobiwm_wayland_compositor_init (CobiwmWaylandCompositor *compositor)
{
  memset (compositor, 0, sizeof (CobiwmWaylandCompositor));
  wl_list_init (&compositor->frame_callbacks);
}

void
cobiwm_wayland_pre_clutter_init (void)
{
  CobiwmWaylandCompositor *compositor = &_cobiwm_wayland_compositor;

  cobiwm_wayland_compositor_init (compositor);

  wl_log_set_handler_server (cobiwm_wayland_log_func);

  compositor->wayland_display = wl_display_create ();
  if (compositor->wayland_display == NULL)
    g_error ("Failed to create the global wl_display");

  clutter_wayland_set_compositor_display (compositor->wayland_display);
}

void
cobiwm_wayland_init (void)
{
  CobiwmWaylandCompositor *compositor = cobiwm_wayland_compositor_get_default ();
  GSource *wayland_event_source;

  wayland_event_source = wayland_event_source_new (compositor->wayland_display);

  /* XXX: Here we are setting the wayland event source to have a
   * slightly lower priority than the X event source, because we are
   * much more likely to get confused being told about surface changes
   * relating to X clients when we don't know what's happened to them
   * according to the X protocol.
   */
  g_source_set_priority (wayland_event_source, GDK_PRIORITY_EVENTS + 1);
  g_source_attach (wayland_event_source, NULL);

  if (!wl_global_create (compositor->wayland_display,
			 &wl_compositor_interface,
			 COBIWM_WL_COMPOSITOR_VERSION,
			 compositor, compositor_bind))
    g_error ("Failed to register the global wl_compositor");

  wl_display_init_shm (compositor->wayland_display);

  cobiwm_wayland_outputs_init (compositor);
  cobiwm_wayland_data_device_manager_init (compositor);
  cobiwm_wayland_shell_init (compositor);
  cobiwm_wayland_pointer_gestures_init (compositor);
  cobiwm_wayland_seat_init (compositor);
  cobiwm_wayland_relative_pointer_init (compositor);
  cobiwm_wayland_pointer_constraints_init (compositor);

  if (!cobiwm_xwayland_start (&compositor->xwayland_manager, compositor->wayland_display))
    g_error ("Failed to start X Wayland");

  compositor->display_name = wl_display_add_socket_auto (compositor->wayland_display);
  if (compositor->display_name == NULL)
    g_error ("Failed to create socket");

  set_gnome_env ("DISPLAY", cobiwm_wayland_get_xwayland_display_name (compositor));
  set_gnome_env ("WAYLAND_DISPLAY", cobiwm_wayland_get_wayland_display_name (compositor));
}

const char *
cobiwm_wayland_get_wayland_display_name (CobiwmWaylandCompositor *compositor)
{
  return compositor->display_name;
}

const char *
cobiwm_wayland_get_xwayland_display_name (CobiwmWaylandCompositor *compositor)
{
  return compositor->xwayland_manager.display_name;
}

void
cobiwm_wayland_finalize (void)
{
  CobiwmWaylandCompositor *compositor;

  compositor = cobiwm_wayland_compositor_get_default ();

  cobiwm_xwayland_stop (&compositor->xwayland_manager);
}
