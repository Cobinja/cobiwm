/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2014-2015 Red Hat
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
 *     Jonas Ådahl <jadahl@gmail.com>
 */

#ifndef COBIWM_BARRIER_PRIVATE_H
#define COBIWM_BARRIER_PRIVATE_H

#include "core/cobiwm-border.h"

G_BEGIN_DECLS

#define COBIWM_TYPE_BARRIER_IMPL            (cobiwm_barrier_impl_get_type ())
#define COBIWM_BARRIER_IMPL(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), COBIWM_TYPE_BARRIER_IMPL, CobiwmBarrierImpl))
#define COBIWM_BARRIER_IMPL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  COBIWM_TYPE_BARRIER_IMPL, CobiwmBarrierImplClass))
#define COBIWM_IS_BARRIER_IMPL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), COBIWM_TYPE_BARRIER_IMPL))
#define COBIWM_IS_BARRIER_IMPL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  COBIWM_TYPE_BARRIER_IMPL))
#define COBIWM_BARRIER_IMPL_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  COBIWM_TYPE_BARRIER_IMPL, CobiwmBarrierImplClass))

typedef struct _CobiwmBarrierImpl        CobiwmBarrierImpl;
typedef struct _CobiwmBarrierImplClass   CobiwmBarrierImplClass;

struct _CobiwmBarrierImpl
{
  GObject parent;
};

struct _CobiwmBarrierImplClass
{
  GObjectClass parent_class;

  gboolean (*is_active) (CobiwmBarrierImpl *barrier);
  void (*release) (CobiwmBarrierImpl  *barrier,
                   CobiwmBarrierEvent *event);
  void (*destroy) (CobiwmBarrierImpl *barrier);
};

GType cobiwm_barrier_impl_get_type (void) G_GNUC_CONST;

void _cobiwm_barrier_emit_hit_signal (CobiwmBarrier      *barrier,
                                    CobiwmBarrierEvent *event);
void _cobiwm_barrier_emit_left_signal (CobiwmBarrier      *barrier,
                                     CobiwmBarrierEvent *event);

void cobiwm_barrier_event_unref (CobiwmBarrierEvent *event);

G_END_DECLS

struct _CobiwmBarrierPrivate
{
  CobiwmDisplay *display;
  CobiwmBorder border;
  CobiwmBarrierImpl *impl;
};

#endif /* COBIWM_BARRIER_PRIVATE_H */
