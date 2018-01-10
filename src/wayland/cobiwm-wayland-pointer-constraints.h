/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2015 Red Hat
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
 *     Jonas Ådahl <jadahl@gmail.com>
 */

#ifndef COBIWM_WAYLAND_POINTER_CONSTRAINTS_H
#define COBIWM_WAYLAND_POINTER_CONSTRAINTS_H

#include "cobiwm-wayland-types.h"
#include "window.h"

#include <wayland-server.h>

#define COBIWM_TYPE_WAYLAND_POINTER_CONSTRAINT (cobiwm_wayland_pointer_constraint_get_type ())
G_DECLARE_FINAL_TYPE (CobiwmWaylandPointerConstraint,
                      cobiwm_wayland_pointer_constraint,
                      COBIWM, WAYLAND_POINTER_CONSTRAINT,
                      GObject);

void cobiwm_wayland_pointer_constraints_init (CobiwmWaylandCompositor *compositor);

CobiwmWaylandSeat * cobiwm_wayland_pointer_constraint_get_seat (CobiwmWaylandPointerConstraint *constraint);

cairo_region_t * cobiwm_wayland_pointer_constraint_calculate_effective_region (CobiwmWaylandPointerConstraint *constraint);

CobiwmWaylandSurface * cobiwm_wayland_pointer_constraint_get_surface (CobiwmWaylandPointerConstraint *constraint);

#endif /* COBIWM_WAYLAND_POINTER_CONSTRAINTS_H */
