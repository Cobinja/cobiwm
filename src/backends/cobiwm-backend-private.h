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


#ifndef COBIWM_BACKEND_PRIVATE_H
#define COBIWM_BACKEND_PRIVATE_H

#include <glib-object.h>

#include <xkbcommon/xkbcommon.h>

#include <cobiwm-backend.h>
#include <cobiwm-idle-monitor.h>
#include "cobiwm-cursor-renderer.h"
#include "cobiwm-monitor-manager-private.h"
#include "backends/cobiwm-pointer-constraint.h"

#define DEFAULT_XKB_RULES_FILE "evdev"
#define DEFAULT_XKB_MODEL "pc105+inet"

#define COBIWM_TYPE_BACKEND             (cobiwm_backend_get_type ())
#define COBIWM_BACKEND(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), COBIWM_TYPE_BACKEND, CobiwmBackend))
#define COBIWM_BACKEND_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass),  COBIWM_TYPE_BACKEND, CobiwmBackendClass))
#define COBIWM_IS_BACKEND(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), COBIWM_TYPE_BACKEND))
#define COBIWM_IS_BACKEND_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass),  COBIWM_TYPE_BACKEND))
#define COBIWM_BACKEND_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj),  COBIWM_TYPE_BACKEND, CobiwmBackendClass))

struct _CobiwmBackend
{
  GObject parent;

  GHashTable *device_monitors;
  gint current_device_id;

  CobiwmPointerConstraint *client_pointer_constraint;
};

struct _CobiwmBackendClass
{
  GObjectClass parent_class;

  void (* post_init) (CobiwmBackend *backend);

  CobiwmIdleMonitor * (* create_idle_monitor) (CobiwmBackend *backend,
                                             int          device_id);
  CobiwmMonitorManager * (* create_monitor_manager) (CobiwmBackend *backend);
  CobiwmCursorRenderer * (* create_cursor_renderer) (CobiwmBackend *backend);

  gboolean (* grab_device) (CobiwmBackend *backend,
                            int          device_id,
                            uint32_t     timestamp);
  gboolean (* ungrab_device) (CobiwmBackend *backend,
                              int          device_id,
                              uint32_t     timestamp);

  void (* warp_pointer) (CobiwmBackend *backend,
                         int          x,
                         int          y);

  void (* set_keymap) (CobiwmBackend *backend,
                       const char  *layouts,
                       const char  *variants,
                       const char  *options);

  struct xkb_keymap * (* get_keymap) (CobiwmBackend *backend);

  void (* lock_layout_group) (CobiwmBackend *backend,
                              guint        idx);

  void (* update_screen_size) (CobiwmBackend *backend, int width, int height);
  void (* select_stage_events) (CobiwmBackend *backend);

  gboolean (* get_relative_motion_deltas) (CobiwmBackend *backend,
                                           const        ClutterEvent *event,
                                           double       *dx,
                                           double       *dy,
                                           double       *dx_unaccel,
                                           double       *dy_unaccel);
};

CobiwmIdleMonitor * cobiwm_backend_get_idle_monitor (CobiwmBackend *backend,
                                                 int          device_id);
CobiwmMonitorManager * cobiwm_backend_get_monitor_manager (CobiwmBackend *backend);
CobiwmCursorRenderer * cobiwm_backend_get_cursor_renderer (CobiwmBackend *backend);

gboolean cobiwm_backend_grab_device (CobiwmBackend *backend,
                                   int          device_id,
                                   uint32_t     timestamp);
gboolean cobiwm_backend_ungrab_device (CobiwmBackend *backend,
                                     int          device_id,
                                     uint32_t     timestamp);

void cobiwm_backend_warp_pointer (CobiwmBackend *backend,
                                int          x,
                                int          y);

struct xkb_keymap * cobiwm_backend_get_keymap (CobiwmBackend *backend);

void cobiwm_backend_update_last_device (CobiwmBackend *backend,
                                      int          device_id);

gboolean cobiwm_backend_get_relative_motion_deltas (CobiwmBackend *backend,
                                                  const        ClutterEvent *event,
                                                  double       *dx,
                                                  double       *dy,
                                                  double       *dx_unaccel,
                                                  double       *dy_unaccel);

void cobiwm_backend_set_client_pointer_constraint (CobiwmBackend *backend,
                                                 CobiwmPointerConstraint *constraint);

#endif /* COBIWM_BACKEND_PRIVATE_H */
