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

#ifndef COBIWM_IDLE_MONITOR_NATIVE_H
#define COBIWM_IDLE_MONITOR_NATIVE_H

#include <glib-object.h>
#include <cobiwm-idle-monitor.h>

#define COBIWM_TYPE_IDLE_MONITOR_NATIVE            (cobiwm_idle_monitor_native_get_type ())
#define COBIWM_IDLE_MONITOR_NATIVE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), COBIWM_TYPE_IDLE_MONITOR_NATIVE, CobiwmIdleMonitorNative))
#define COBIWM_IDLE_MONITOR_NATIVE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  COBIWM_TYPE_IDLE_MONITOR_NATIVE, CobiwmIdleMonitorNativeClass))
#define COBIWM_IS_IDLE_MONITOR_NATIVE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), COBIWM_TYPE_IDLE_MONITOR_NATIVE))
#define COBIWM_IS_IDLE_MONITOR_NATIVE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  COBIWM_TYPE_IDLE_MONITOR_NATIVE))
#define COBIWM_IDLE_MONITOR_NATIVE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  COBIWM_TYPE_IDLE_MONITOR_NATIVE, CobiwmIdleMonitorNativeClass))

typedef struct _CobiwmIdleMonitorNative        CobiwmIdleMonitorNative;
typedef struct _CobiwmIdleMonitorNativeClass   CobiwmIdleMonitorNativeClass;

GType cobiwm_idle_monitor_native_get_type (void);

void cobiwm_idle_monitor_native_reset_idletime (CobiwmIdleMonitor *monitor);

#endif /* COBIWM_IDLE_MONITOR_NATIVE_H */
