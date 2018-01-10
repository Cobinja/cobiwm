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

#include "config.h"

#include "cobiwm-idle-monitor-native.h"
#include "cobiwm-idle-monitor-private.h"

#include <util.h>
#include "display-private.h"

#include <string.h>

struct _CobiwmIdleMonitorNative
{
  CobiwmIdleMonitor parent;

  guint64 last_event_time;
};

struct _CobiwmIdleMonitorNativeClass
{
  CobiwmIdleMonitorClass parent_class;
};

typedef struct {
  CobiwmIdleMonitorWatch base;

  GSource *timeout_source;
} CobiwmIdleMonitorWatchNative;

G_DEFINE_TYPE (CobiwmIdleMonitorNative, cobiwm_idle_monitor_native, COBIWM_TYPE_IDLE_MONITOR)

static gint64
cobiwm_idle_monitor_native_get_idletime (CobiwmIdleMonitor *monitor)
{
  CobiwmIdleMonitorNative *monitor_native = COBIWM_IDLE_MONITOR_NATIVE (monitor);

  return (g_get_monotonic_time () - monitor_native->last_event_time) / 1000;
}

static guint32
get_next_watch_serial (void)
{
  static guint32 serial = 0;
  g_atomic_int_inc (&serial);
  return serial;
}

static gboolean
native_dispatch_timeout (GSource     *source,
                         GSourceFunc  callback,
                         gpointer     user_data)
{
  CobiwmIdleMonitorWatchNative *watch_native = user_data;
  CobiwmIdleMonitorWatch *watch = (CobiwmIdleMonitorWatch *) watch_native;

  _cobiwm_idle_monitor_watch_fire (watch);
  g_source_set_ready_time (watch_native->timeout_source, -1);
  return TRUE;
}

static GSourceFuncs native_source_funcs = {
  NULL, /* prepare */
  NULL, /* check */
  native_dispatch_timeout,
  NULL, /* finalize */
};

static void
free_watch (gpointer data)
{
  CobiwmIdleMonitorWatchNative *watch_native = data;
  CobiwmIdleMonitorWatch *watch = (CobiwmIdleMonitorWatch *) watch_native;
  CobiwmIdleMonitor *monitor = watch->monitor;

  g_object_ref (monitor);

  if (watch->idle_source_id)
    {
      g_source_remove (watch->idle_source_id);
      watch->idle_source_id = 0;
    }

  if (watch->notify != NULL)
    watch->notify (watch->user_data);

  if (watch_native->timeout_source != NULL)
    g_source_destroy (watch_native->timeout_source);

  g_object_unref (monitor);
  g_slice_free (CobiwmIdleMonitorWatchNative, watch_native);
}

static CobiwmIdleMonitorWatch *
cobiwm_idle_monitor_native_make_watch (CobiwmIdleMonitor           *monitor,
                                     guint64                    timeout_msec,
                                     CobiwmIdleMonitorWatchFunc   callback,
                                     gpointer                   user_data,
                                     GDestroyNotify             notify)
{
  CobiwmIdleMonitorWatchNative *watch_native;
  CobiwmIdleMonitorWatch *watch;
  CobiwmIdleMonitorNative *monitor_native = COBIWM_IDLE_MONITOR_NATIVE (monitor);

  watch_native = g_slice_new0 (CobiwmIdleMonitorWatchNative);
  watch = (CobiwmIdleMonitorWatch *) watch_native;

  watch->monitor = monitor;
  watch->id = get_next_watch_serial ();
  watch->callback = callback;
  watch->user_data = user_data;
  watch->notify = notify;
  watch->timeout_msec = timeout_msec;

  if (timeout_msec != 0)
    {
      GSource *source = g_source_new (&native_source_funcs, sizeof (GSource));

      g_source_set_callback (source, NULL, watch, NULL);
      g_source_set_ready_time (source, monitor_native->last_event_time + timeout_msec * 1000);
      g_source_attach (source, NULL);
      g_source_unref (source);

      watch_native->timeout_source = source;
    }

  return watch;
}

static void
cobiwm_idle_monitor_native_class_init (CobiwmIdleMonitorNativeClass *klass)
{
  CobiwmIdleMonitorClass *idle_monitor_class = COBIWM_IDLE_MONITOR_CLASS (klass);

  idle_monitor_class->get_idletime = cobiwm_idle_monitor_native_get_idletime;
  idle_monitor_class->make_watch = cobiwm_idle_monitor_native_make_watch;
}

static void
cobiwm_idle_monitor_native_init (CobiwmIdleMonitorNative *monitor_native)
{
  CobiwmIdleMonitor *monitor = COBIWM_IDLE_MONITOR (monitor_native);

  monitor->watches = g_hash_table_new_full (NULL, NULL, NULL, free_watch);
}

void
cobiwm_idle_monitor_native_reset_idletime (CobiwmIdleMonitor *monitor)
{
  CobiwmIdleMonitorNative *monitor_native = COBIWM_IDLE_MONITOR_NATIVE (monitor);
  GList *node, *watch_ids;

  monitor_native->last_event_time = g_get_monotonic_time ();

  watch_ids = g_hash_table_get_keys (monitor->watches);

  for (node = watch_ids; node != NULL; node = node->next)
    {
      guint watch_id = GPOINTER_TO_UINT (node->data);
      CobiwmIdleMonitorWatchNative *watch;

      watch = g_hash_table_lookup (monitor->watches, GUINT_TO_POINTER (watch_id));
      if (!watch)
        continue;

      if (watch->base.timeout_msec == 0)
        {
          _cobiwm_idle_monitor_watch_fire ((CobiwmIdleMonitorWatch *) watch);
        }
      else
        {
          g_source_set_ready_time (watch->timeout_source,
                                   monitor_native->last_event_time +
                                   watch->base.timeout_msec * 1000);
        }
    }

  g_list_free (watch_ids);
}
