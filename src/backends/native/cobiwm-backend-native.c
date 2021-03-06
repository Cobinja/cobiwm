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

#include "config.h"

#include "cobiwm-backend-native.h"
#include "cobiwm-backend-native-private.h"

#include <main.h>
#include <clutter/evdev/clutter-evdev.h>
#include <libupower-glib/upower.h>

#include "cobiwm-barrier-native.h"
#include "cobiwm-idle-monitor-native.h"
#include "cobiwm-monitor-manager-kms.h"
#include "cobiwm-cursor-renderer-native.h"
#include "cobiwm-launcher.h"
#include "backends/cobiwm-cursor-tracker-private.h"
#include "backends/cobiwm-pointer-constraint.h"

#include <stdlib.h>

struct _CobiwmBackendNativePrivate
{
  CobiwmLauncher *launcher;
  CobiwmBarrierManagerNative *barrier_manager;
  UpClient *up_client;
  guint sleep_signal_id;
  GCancellable *cancellable;
  GDBusConnection *system_bus;
};
typedef struct _CobiwmBackendNativePrivate CobiwmBackendNativePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (CobiwmBackendNative, cobiwm_backend_native, COBIWM_TYPE_BACKEND);

static void
cobiwm_backend_native_finalize (GObject *object)
{
  CobiwmBackendNative *native = COBIWM_BACKEND_NATIVE (object);
  CobiwmBackendNativePrivate *priv = cobiwm_backend_native_get_instance_private (native);

  cobiwm_launcher_free (priv->launcher);

  g_object_unref (priv->up_client);
  if (priv->sleep_signal_id)
    g_dbus_connection_signal_unsubscribe (priv->system_bus, priv->sleep_signal_id);
  g_cancellable_cancel (priv->cancellable);
  g_clear_object (&priv->cancellable);
  g_clear_object (&priv->system_bus);

  G_OBJECT_CLASS (cobiwm_backend_native_parent_class)->finalize (object);
}

static void
prepare_for_sleep_cb (GDBusConnection *connection,
                      const gchar     *sender_name,
                      const gchar     *object_path,
                      const gchar     *interface_name,
                      const gchar     *signal_name,
                      GVariant        *parameters,
                      gpointer         user_data)
{
  gboolean suspending;
  g_variant_get (parameters, "(b)", &suspending);
  if (suspending)
    return;
  cobiwm_idle_monitor_native_reset_idletime (cobiwm_idle_monitor_get_core ());
}

static void
system_bus_gotten_cb (GObject      *object,
                      GAsyncResult *res,
                      gpointer      user_data)
{
  CobiwmBackendNativePrivate *priv;
  GDBusConnection *bus;

  bus = g_bus_get_finish (res, NULL);
  if (!bus)
    return;

  priv = cobiwm_backend_native_get_instance_private (COBIWM_BACKEND_NATIVE (user_data));
  priv->system_bus = bus;
  priv->sleep_signal_id = g_dbus_connection_signal_subscribe (priv->system_bus,
                                                              "org.freedesktop.login1",
                                                              "org.freedesktop.login1.Manager",
                                                              "PrepareForSleep",
                                                              "/org/freedesktop/login1",
                                                              NULL,
                                                              G_DBUS_SIGNAL_FLAGS_NONE,
                                                              prepare_for_sleep_cb,
                                                              NULL,
                                                              NULL);
}

static void
lid_is_closed_changed_cb (UpClient   *client,
                          GParamSpec *pspec,
                          gpointer    user_data)
{
  if (up_client_get_lid_is_closed (client))
    return;

  cobiwm_idle_monitor_native_reset_idletime (cobiwm_idle_monitor_get_core ());
}

static void
constrain_to_barriers (ClutterInputDevice *device,
                       guint32             time,
                       float              *new_x,
                       float              *new_y)
{
  CobiwmBackendNative *native = COBIWM_BACKEND_NATIVE (cobiwm_get_backend ());
  CobiwmBackendNativePrivate *priv =
    cobiwm_backend_native_get_instance_private (native);

  cobiwm_barrier_manager_native_process (priv->barrier_manager,
                                       device,
                                       time,
                                       new_x, new_y);
}

static void
constrain_to_client_constraint (ClutterInputDevice *device,
                                guint32             time,
                                float               prev_x,
                                float               prev_y,
                                float              *x,
                                float              *y)
{
  CobiwmBackend *backend = cobiwm_get_backend ();
  CobiwmPointerConstraint *constraint = backend->client_pointer_constraint;

  if (!constraint)
    return;

  cobiwm_pointer_constraint_constrain (constraint, device,
                                     time, prev_x, prev_y, x, y);
}

/*
 * The pointer constrain code is mostly a rip-off of the XRandR code from Xorg.
 * (from xserver/randr/rrcrtc.c, RRConstrainCursorHarder)
 *
 * Copyright © 2006 Keith Packard
 * Copyright 2010 Red Hat, Inc
 *
 */

static void
constrain_all_screen_monitors (ClutterInputDevice *device,
			       CobiwmMonitorInfo    *monitors,
			       unsigned            n_monitors,
			       float              *x,
			       float              *y)
{
  ClutterPoint current;
  unsigned int i;
  float cx, cy;

  clutter_input_device_get_coords (device, NULL, &current);

  cx = current.x;
  cy = current.y;

  /* if we're trying to escape, clamp to the CRTC we're coming from */
  for (i = 0; i < n_monitors; i++)
    {
      CobiwmMonitorInfo *monitor = &monitors[i];
      int left, right, top, bottom;

      left = monitor->rect.x;
      right = left + monitor->rect.width;
      top = monitor->rect.y;
      bottom = top + monitor->rect.height;

      if ((cx >= left) && (cx < right) && (cy >= top) && (cy < bottom))
	{
	  if (*x < left)
	    *x = left;
	  if (*x >= right)
	    *x = right - 1;
	  if (*y < top)
	    *y = top;
	  if (*y >= bottom)
	    *y = bottom - 1;

	  return;
        }
    }
}

static void
pointer_constrain_callback (ClutterInputDevice *device,
                            guint32             time,
                            float               prev_x,
                            float               prev_y,
                            float              *new_x,
                            float              *new_y,
                            gpointer            user_data)
{
  CobiwmMonitorManager *monitor_manager;
  CobiwmMonitorInfo *monitors;
  unsigned int n_monitors;

  /* Constrain to barriers */
  constrain_to_barriers (device, time, new_x, new_y);

  /* Constrain to pointer lock */
  constrain_to_client_constraint (device, time, prev_x, prev_y, new_x, new_y);

  monitor_manager = cobiwm_monitor_manager_get ();
  monitors = cobiwm_monitor_manager_get_monitor_infos (monitor_manager, &n_monitors);

  /* if we're moving inside a monitor, we're fine */
  if (cobiwm_monitor_manager_get_monitor_at_point (monitor_manager, *new_x, *new_y) >= 0)
    return;

  /* if we're trying to escape, clamp to the CRTC we're coming from */
  constrain_all_screen_monitors(device, monitors, n_monitors, new_x, new_y);
}

static void
cobiwm_backend_native_post_init (CobiwmBackend *backend)
{
  ClutterDeviceManager *manager = clutter_device_manager_get_default ();

  COBIWM_BACKEND_CLASS (cobiwm_backend_native_parent_class)->post_init (backend);

  clutter_evdev_set_pointer_constrain_callback (manager, pointer_constrain_callback,
                                                NULL, NULL);
}

static CobiwmIdleMonitor *
cobiwm_backend_native_create_idle_monitor (CobiwmBackend *backend,
                                         int          device_id)
{
  return g_object_new (COBIWM_TYPE_IDLE_MONITOR_NATIVE,
                       "device-id", device_id,
                       NULL);
}

static CobiwmMonitorManager *
cobiwm_backend_native_create_monitor_manager (CobiwmBackend *backend)
{
  return g_object_new (COBIWM_TYPE_MONITOR_MANAGER_KMS, NULL);
}

static CobiwmCursorRenderer *
cobiwm_backend_native_create_cursor_renderer (CobiwmBackend *backend)
{
  return g_object_new (COBIWM_TYPE_CURSOR_RENDERER_NATIVE, NULL);
}

static void
cobiwm_backend_native_warp_pointer (CobiwmBackend *backend,
                                  int          x,
                                  int          y)
{
  ClutterDeviceManager *manager = clutter_device_manager_get_default ();
  ClutterInputDevice *device = clutter_device_manager_get_core_device (manager, CLUTTER_POINTER_DEVICE);
  CobiwmCursorTracker *tracker = cobiwm_cursor_tracker_get_for_screen (NULL);

  /* XXX */
  guint32 time_ = 0;

  /* Warp the input device pointer state. */
  clutter_evdev_warp_pointer (device, time_, x, y);

  /* Warp displayed pointer cursor. */
  cobiwm_cursor_tracker_update_position (tracker, x, y);
}

static void
cobiwm_backend_native_set_keymap (CobiwmBackend *backend,
                                const char  *layouts,
                                const char  *variants,
                                const char  *options)
{
  ClutterDeviceManager *manager = clutter_device_manager_get_default ();
  struct xkb_rule_names names;
  struct xkb_keymap *keymap;
  struct xkb_context *context;

  names.rules = DEFAULT_XKB_RULES_FILE;
  names.model = DEFAULT_XKB_MODEL;
  names.layout = layouts;
  names.variant = variants;
  names.options = options;

  context = xkb_context_new (XKB_CONTEXT_NO_FLAGS);
  keymap = xkb_keymap_new_from_names (context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
  xkb_context_unref (context);

  clutter_evdev_set_keyboard_map (manager, keymap);

  g_signal_emit_by_name (backend, "keymap-changed", 0);

  xkb_keymap_unref (keymap);
}

static struct xkb_keymap *
cobiwm_backend_native_get_keymap (CobiwmBackend *backend)
{
  ClutterDeviceManager *manager = clutter_device_manager_get_default ();
  return clutter_evdev_get_keyboard_map (manager);
}

static void
cobiwm_backend_native_lock_layout_group (CobiwmBackend *backend,
                                       guint        idx)
{
  ClutterDeviceManager *manager = clutter_device_manager_get_default ();
  clutter_evdev_set_keyboard_layout_index (manager, idx);
  g_signal_emit_by_name (backend, "keymap-layout-group-changed", idx, 0);
}

static gboolean
cobiwm_backend_native_get_relative_motion_deltas (CobiwmBackend *backend,
                                                const        ClutterEvent *event,
                                                double       *dx,
                                                double       *dy,
                                                double       *dx_unaccel,
                                                double       *dy_unaccel)
{
  return clutter_evdev_event_get_relative_motion (event,
                                                  dx, dy,
                                                  dx_unaccel, dy_unaccel);
}

static void
cobiwm_backend_native_class_init (CobiwmBackendNativeClass *klass)
{
  CobiwmBackendClass *backend_class = COBIWM_BACKEND_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = cobiwm_backend_native_finalize;

  backend_class->post_init = cobiwm_backend_native_post_init;
  backend_class->create_idle_monitor = cobiwm_backend_native_create_idle_monitor;
  backend_class->create_monitor_manager = cobiwm_backend_native_create_monitor_manager;
  backend_class->create_cursor_renderer = cobiwm_backend_native_create_cursor_renderer;

  backend_class->warp_pointer = cobiwm_backend_native_warp_pointer;
  backend_class->set_keymap = cobiwm_backend_native_set_keymap;
  backend_class->get_keymap = cobiwm_backend_native_get_keymap;
  backend_class->lock_layout_group = cobiwm_backend_native_lock_layout_group;
  backend_class->get_relative_motion_deltas = cobiwm_backend_native_get_relative_motion_deltas;
}

static void
cobiwm_backend_native_init (CobiwmBackendNative *native)
{
  CobiwmBackendNativePrivate *priv = cobiwm_backend_native_get_instance_private (native);
  GError *error = NULL;

  priv->launcher = cobiwm_launcher_new (&error);
  if (priv->launcher == NULL)
    {
      g_warning ("Can't initialize KMS backend: %s\n", error->message);
      exit (1);
    }

  priv->barrier_manager = cobiwm_barrier_manager_native_new ();

  priv->up_client = up_client_new ();
  g_signal_connect (priv->up_client, "notify::lid-is-closed",
                    G_CALLBACK (lid_is_closed_changed_cb), NULL);

  priv->cancellable = g_cancellable_new ();
  g_bus_get (G_BUS_TYPE_SYSTEM,
             priv->cancellable,
             system_bus_gotten_cb,
             native);
}

gboolean
cobiwm_activate_vt (int vt, GError **error)
{
  CobiwmBackend *backend = cobiwm_get_backend ();
  CobiwmBackendNative *native = COBIWM_BACKEND_NATIVE (backend);
  CobiwmBackendNativePrivate *priv = cobiwm_backend_native_get_instance_private (native);

  return cobiwm_launcher_activate_vt (priv->launcher, vt, error);
}

CobiwmBarrierManagerNative *
cobiwm_backend_native_get_barrier_manager (CobiwmBackendNative *native)
{
  CobiwmBackendNativePrivate *priv =
    cobiwm_backend_native_get_instance_private (native);

  return priv->barrier_manager;
}

/**
 * cobiwm_activate_session:
 *
 * Tells cobiwm to activate the session. When cobiwm is a
 * display server, this tells logind to switch over to
 * the new session.
 */
gboolean
cobiwm_activate_session (void)
{
  GError *error = NULL;
  CobiwmBackend *backend = cobiwm_get_backend ();

  /* Do nothing. */
  if (!COBIWM_IS_BACKEND_NATIVE (backend))
    return TRUE;

  CobiwmBackendNative *native = COBIWM_BACKEND_NATIVE (backend);
  CobiwmBackendNativePrivate *priv = cobiwm_backend_native_get_instance_private (native);

  if (!cobiwm_launcher_activate_session (priv->launcher, &error))
    {
      g_warning ("Could not activate session: %s\n", error->message);
      g_error_free (error);
      return FALSE;
    }

  return TRUE;
}
