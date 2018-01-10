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

#ifndef COBIWM_WAYLAND_OUTPUTS_H
#define COBIWM_WAYLAND_OUTPUTS_H

#include "backends/cobiwm-monitor-manager-private.h"
#include "cobiwm-wayland-private.h"

#define COBIWM_TYPE_WAYLAND_OUTPUT            (cobiwm_wayland_output_get_type ())
#define COBIWM_WAYLAND_OUTPUT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), COBIWM_TYPE_WAYLAND_OUTPUT, CobiwmWaylandOutput))
#define COBIWM_WAYLAND_OUTPUT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  COBIWM_TYPE_WAYLAND_OUTPUT, CobiwmWaylandOutputClass))
#define COBIWM_IS_WAYLAND_OUTPUT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), COBIWM_TYPE_WAYLAND_OUTPUT))
#define COBIWM_IS_WAYLAND_OUTPUT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  COBIWM_TYPE_WAYLAND_OUTPUT))
#define COBIWM_WAYLAND_OUTPUT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  COBIWM_TYPE_WAYLAND_OUTPUT, CobiwmWaylandOutputClass))

typedef struct _CobiwmWaylandOutputClass  CobiwmWaylandOutputClass;

struct _CobiwmWaylandOutput
{
  GObject                   parent;

  struct wl_global         *global;
  CobiwmMonitorInfo          *monitor_info;
  enum wl_output_transform  transform;
  guint                     mode_flags;
  gint                      scale;

  GList                    *resources;
};

struct _CobiwmWaylandOutputClass
{
  GObjectClass parent_class;
};

GType cobiwm_wayland_output_get_type (void) G_GNUC_CONST;

void cobiwm_wayland_outputs_init (CobiwmWaylandCompositor *compositor);

#endif /* COBIWM_WAYLAND_OUTPUTS_H */
