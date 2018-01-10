/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright 2013 Red Hat, Inc.
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Adapted from gnome-session/gnome-session/gs-idle-monitor.c and
 *         from gnome-desktop/libgnome-desktop/gnome-idle-monitor.c
 */

/**
 * SECTION:idle-monitor
 * @title: CobiwmIdleMonitor
 * @short_description: Cobiwm idle counter (similar to X's IDLETIME)
 */

#include "config.h"

#include <string.h>
#include <clutter/clutter.h>
#include <X11/Xlib.h>
#include <X11/extensions/sync.h>

#include <util.h>
#include <main.h>
#include <cobiwm-idle-monitor.h>
#include "cobiwm-idle-monitor-private.h"
#include "cobiwm-idle-monitor-dbus.h"
#include "cobiwm-backend-private.h"

G_STATIC_ASSERT(sizeof(unsigned long) == sizeof(gpointer));

enum
{
  PROP_0,
  PROP_DEVICE_ID,
  PROP_LAST,
};

static GParamSpec *obj_props[PROP_LAST];

G_DEFINE_TYPE (CobiwmIdleMonitor, cobiwm_idle_monitor, G_TYPE_OBJECT)

void
_cobiwm_idle_monitor_watch_fire (CobiwmIdleMonitorWatch *watch)
{
  CobiwmIdleMonitor *monitor;
  guint id;
  gboolean is_user_active_watch;

  monitor = watch->monitor;
  g_object_ref (monitor);

  if (watch->idle_source_id)
    {
      g_source_remove (watch->idle_source_id);
      watch->idle_source_id = 0;
    }

  id = watch->id;
  is_user_active_watch = (watch->timeout_msec == 0);

  if (watch->callback)
    watch->callback (monitor, id, watch->user_data);

  if (is_user_active_watch)
    cobiwm_idle_monitor_remove_watch (monitor, id);

  g_object_unref (monitor);
}

static void
cobiwm_idle_monitor_dispose (GObject *object)
{
  CobiwmIdleMonitor *monitor = COBIWM_IDLE_MONITOR (object);

  g_clear_pointer (&monitor->watches, g_hash_table_destroy);

  G_OBJECT_CLASS (cobiwm_idle_monitor_parent_class)->dispose (object);
}

static void
cobiwm_idle_monitor_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  CobiwmIdleMonitor *monitor = COBIWM_IDLE_MONITOR (object);

  switch (prop_id)
    {
    case PROP_DEVICE_ID:
      g_value_set_int (value, monitor->device_id);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
cobiwm_idle_monitor_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  CobiwmIdleMonitor *monitor = COBIWM_IDLE_MONITOR (object);
  switch (prop_id)
    {
    case PROP_DEVICE_ID:
      monitor->device_id = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
cobiwm_idle_monitor_class_init (CobiwmIdleMonitorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = cobiwm_idle_monitor_dispose;
  object_class->get_property = cobiwm_idle_monitor_get_property;
  object_class->set_property = cobiwm_idle_monitor_set_property;

  /**
   * CobiwmIdleMonitor:device_id:
   *
   * The device to listen to idletime on.
   */
  obj_props[PROP_DEVICE_ID] =
    g_param_spec_int ("device-id",
                      "Device ID",
                      "The device to listen to idletime on",
                      0, 255, 0,
                      G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_property (object_class, PROP_DEVICE_ID, obj_props[PROP_DEVICE_ID]);
}

static void
cobiwm_idle_monitor_init (CobiwmIdleMonitor *monitor)
{
}

/**
 * cobiwm_idle_monitor_get_core:
 *
 * Returns: (transfer none): the #CobiwmIdleMonitor that tracks the server-global
 * idletime for all devices. To track device-specific idletime,
 * use cobiwm_idle_monitor_get_for_device().
 */
CobiwmIdleMonitor *
cobiwm_idle_monitor_get_core (void)
{
  CobiwmBackend *backend = cobiwm_get_backend ();
  return cobiwm_backend_get_idle_monitor (backend, 0);
}

/**
 * cobiwm_idle_monitor_get_for_device:
 * @device_id: the device to get the idle time for.
 *
 * Returns: (transfer none): a new #CobiwmIdleMonitor that tracks the
 * device-specific idletime for @device. To track server-global idletime
 * for all devices, use cobiwm_idle_monitor_get_core().
 */
CobiwmIdleMonitor *
cobiwm_idle_monitor_get_for_device (int device_id)
{
  CobiwmBackend *backend = cobiwm_get_backend ();
  return cobiwm_backend_get_idle_monitor (backend, device_id);
}

static CobiwmIdleMonitorWatch *
make_watch (CobiwmIdleMonitor           *monitor,
            guint64                    timeout_msec,
            CobiwmIdleMonitorWatchFunc   callback,
            gpointer                   user_data,
            GDestroyNotify             notify)
{
  CobiwmIdleMonitorWatch *watch;

  watch = COBIWM_IDLE_MONITOR_GET_CLASS (monitor)->make_watch (monitor,
                                                             timeout_msec,
                                                             callback,
                                                             user_data,
                                                             notify);

  g_hash_table_insert (monitor->watches,
                       GUINT_TO_POINTER (watch->id),
                       watch);
  return watch;
}

/**
 * cobiwm_idle_monitor_add_idle_watch:
 * @monitor: A #CobiwmIdleMonitor
 * @interval_msec: The idletime interval, in milliseconds
 * @callback: (nullable): The callback to call when the user has
 *     accumulated @interval_msec milliseconds of idle time.
 * @user_data: (nullable): The user data to pass to the callback
 * @notify: A #GDestroyNotify
 *
 * Returns: a watch id
 *
 * Adds a watch for a specific idle time. The callback will be called
 * when the user has accumulated @interval_msec milliseconds of idle time.
 * This function will return an ID that can either be passed to
 * cobiwm_idle_monitor_remove_watch(), or can be used to tell idle time
 * watches apart if you have more than one.
 *
 * Also note that this function will only care about positive transitions
 * (user's idle time exceeding a certain time). If you want to know about
 * when the user has become active, use
 * cobiwm_idle_monitor_add_user_active_watch().
 */
guint
cobiwm_idle_monitor_add_idle_watch (CobiwmIdleMonitor	       *monitor,
                                  guint64	                interval_msec,
                                  CobiwmIdleMonitorWatchFunc      callback,
                                  gpointer			user_data,
                                  GDestroyNotify		notify)
{
  CobiwmIdleMonitorWatch *watch;

  g_return_val_if_fail (COBIWM_IS_IDLE_MONITOR (monitor), 0);
  g_return_val_if_fail (interval_msec > 0, 0);

  watch = make_watch (monitor,
                      interval_msec,
                      callback,
                      user_data,
                      notify);

  return watch->id;
}

/**
 * cobiwm_idle_monitor_add_user_active_watch:
 * @monitor: A #CobiwmIdleMonitor
 * @callback: (nullable): The callback to call when the user is
 *     active again.
 * @user_data: (nullable): The user data to pass to the callback
 * @notify: A #GDestroyNotify
 *
 * Returns: a watch id
 *
 * Add a one-time watch to know when the user is active again.
 * Note that this watch is one-time and will de-activate after the
 * function is called, for efficiency purposes. It's most convenient
 * to call this when an idle watch, as added by
 * cobiwm_idle_monitor_add_idle_watch(), has triggered.
 */
guint
cobiwm_idle_monitor_add_user_active_watch (CobiwmIdleMonitor          *monitor,
                                         CobiwmIdleMonitorWatchFunc  callback,
                                         gpointer		   user_data,
                                         GDestroyNotify	           notify)
{
  CobiwmIdleMonitorWatch *watch;

  g_return_val_if_fail (COBIWM_IS_IDLE_MONITOR (monitor), 0);

  watch = make_watch (monitor,
                      0,
                      callback,
                      user_data,
                      notify);

  return watch->id;
}

/**
 * cobiwm_idle_monitor_remove_watch:
 * @monitor: A #CobiwmIdleMonitor
 * @id: A watch ID
 *
 * Removes an idle time watcher, previously added by
 * cobiwm_idle_monitor_add_idle_watch() or
 * cobiwm_idle_monitor_add_user_active_watch().
 */
void
cobiwm_idle_monitor_remove_watch (CobiwmIdleMonitor *monitor,
                                guint	         id)
{
  g_return_if_fail (COBIWM_IS_IDLE_MONITOR (monitor));

  g_object_ref (monitor);
  g_hash_table_remove (monitor->watches,
                       GUINT_TO_POINTER (id));
  g_object_unref (monitor);
}

/**
 * cobiwm_idle_monitor_get_idletime:
 * @monitor: A #CobiwmIdleMonitor
 *
 * Returns: The current idle time, in milliseconds, or -1 for not supported
 */
gint64
cobiwm_idle_monitor_get_idletime (CobiwmIdleMonitor *monitor)
{
  return COBIWM_IDLE_MONITOR_GET_CLASS (monitor)->get_idletime (monitor);
}
