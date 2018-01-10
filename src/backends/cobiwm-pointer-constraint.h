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

#ifndef COBIWM_POINTER_CONSTRAINT_H
#define COBIWM_POINTER_CONSTRAINT_H

#include <glib-object.h>
#include <clutter/clutter.h>

G_BEGIN_DECLS

#define COBIWM_TYPE_POINTER_CONSTRAINT (cobiwm_pointer_constraint_get_type ())
G_DECLARE_DERIVABLE_TYPE (CobiwmPointerConstraint, cobiwm_pointer_constraint,
                          COBIWM, POINTER_CONSTRAINT, GObject);

struct _CobiwmPointerConstraintClass
{
  GObjectClass parent_class;

  void (*constrain) (CobiwmPointerConstraint *constraint,
                     ClutterInputDevice *device,
                     guint32 time,
                     float prev_x,
                     float prev_y,
                     float *x,
                     float *y);
};

void cobiwm_pointer_constraint_constrain (CobiwmPointerConstraint *constraint,
                                        ClutterInputDevice    *device,
                                        guint32                time,
                                        float                  prev_x,
                                        float                  prev_y,
                                        float                 *x,
                                        float                 *y);

G_END_DECLS

#endif /* COBIWM_POINTER_CONSTRAINT_H */
