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

#include "config.h"

#include "wayland/cobiwm-pointer-lock-wayland.h"

#include <glib-object.h>

#include "backends/cobiwm-pointer-constraint.h"

struct _CobiwmPointerLockWayland
{
  CobiwmPointerConstraint parent;
};

G_DEFINE_TYPE (CobiwmPointerLockWayland, cobiwm_pointer_lock_wayland,
               COBIWM_TYPE_POINTER_CONSTRAINT);

static void
cobiwm_pointer_lock_wayland_constrain (CobiwmPointerConstraint *constraint,
                                     ClutterInputDevice    *device,
                                     guint32                time,
                                     float                  prev_x,
                                     float                  prev_y,
                                     float                 *x,
                                     float                 *y)
{
  *x = prev_x;
  *y = prev_y;
}

CobiwmPointerConstraint *
cobiwm_pointer_lock_wayland_new (void)
{
  return g_object_new (COBIWM_TYPE_POINTER_LOCK_WAYLAND, NULL);
}

static void
cobiwm_pointer_lock_wayland_init (CobiwmPointerLockWayland *lock_wayland)
{
}

static void
cobiwm_pointer_lock_wayland_class_init (CobiwmPointerLockWaylandClass *klass)
{
  CobiwmPointerConstraintClass *pointer_constraint_class =
    COBIWM_POINTER_CONSTRAINT_CLASS (klass);

  pointer_constraint_class->constrain = cobiwm_pointer_lock_wayland_constrain;
}
