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
 */

#ifndef COBIWM_IDLE_MONITOR_H
#define COBIWM_IDLE_MONITOR_H

#include <glib-object.h>
#include <types.h>

#define COBIWM_TYPE_IDLE_MONITOR            (cobiwm_idle_monitor_get_type ())
#define COBIWM_IDLE_MONITOR(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), COBIWM_TYPE_IDLE_MONITOR, CobiwmIdleMonitor))
#define COBIWM_IDLE_MONITOR_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  COBIWM_TYPE_IDLE_MONITOR, CobiwmIdleMonitorClass))
#define COBIWM_IS_IDLE_MONITOR(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), COBIWM_TYPE_IDLE_MONITOR))
#define COBIWM_IS_IDLE_MONITOR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  COBIWM_TYPE_IDLE_MONITOR))
#define COBIWM_IDLE_MONITOR_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  COBIWM_TYPE_IDLE_MONITOR, CobiwmIdleMonitorClass))

typedef struct _CobiwmIdleMonitor        CobiwmIdleMonitor;
typedef struct _CobiwmIdleMonitorClass   CobiwmIdleMonitorClass;

GType cobiwm_idle_monitor_get_type (void);

typedef void (*CobiwmIdleMonitorWatchFunc) (CobiwmIdleMonitor *monitor,
                                          guint            watch_id,
                                          gpointer         user_data);

CobiwmIdleMonitor *cobiwm_idle_monitor_get_core (void);
CobiwmIdleMonitor *cobiwm_idle_monitor_get_for_device (int device_id);

guint         cobiwm_idle_monitor_add_idle_watch        (CobiwmIdleMonitor          *monitor,
						       guint64                   interval_msec,
						       CobiwmIdleMonitorWatchFunc  callback,
						       gpointer                  user_data,
						       GDestroyNotify            notify);

guint         cobiwm_idle_monitor_add_user_active_watch (CobiwmIdleMonitor          *monitor,
						       CobiwmIdleMonitorWatchFunc  callback,
						       gpointer                  user_data,
						       GDestroyNotify            notify);

void          cobiwm_idle_monitor_remove_watch          (CobiwmIdleMonitor          *monitor,
						       guint                     id);
gint64        cobiwm_idle_monitor_get_idletime          (CobiwmIdleMonitor          *monitor);

#endif
