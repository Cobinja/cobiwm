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

#ifndef COBIWM_IDLE_MONITOR_PRIVATE_H
#define COBIWM_IDLE_MONITOR_PRIVATE_H

#include <cobiwm-idle-monitor.h>
#include "display-private.h"

#include <X11/Xlib.h>
#include <X11/extensions/sync.h>

typedef struct
{
  CobiwmIdleMonitor          *monitor;
  guint	                    id;
  CobiwmIdleMonitorWatchFunc  callback;
  gpointer		    user_data;
  GDestroyNotify            notify;
  guint64                   timeout_msec;
  int                       idle_source_id;
} CobiwmIdleMonitorWatch;

struct _CobiwmIdleMonitor
{
  GObject parent_instance;

  GHashTable *watches;
  int device_id;
};

struct _CobiwmIdleMonitorClass
{
  GObjectClass parent_class;

  gint64 (*get_idletime) (CobiwmIdleMonitor *monitor);
  CobiwmIdleMonitorWatch * (*make_watch) (CobiwmIdleMonitor           *monitor,
                                        guint64                    timeout_msec,
                                        CobiwmIdleMonitorWatchFunc   callback,
                                        gpointer                   user_data,
                                        GDestroyNotify             notify);
};

void _cobiwm_idle_monitor_watch_fire (CobiwmIdleMonitorWatch *watch);

#endif /* COBIWM_IDLE_MONITOR_PRIVATE_H */
