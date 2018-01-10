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

#ifndef COBIWM_BARRIER_NATIVE_H
#define COBIWM_BARRIER_NATIVE_H

#include "backends/cobiwm-barrier-private.h"

G_BEGIN_DECLS

#define COBIWM_TYPE_BARRIER_IMPL_NATIVE            (cobiwm_barrier_impl_native_get_type ())
#define COBIWM_BARRIER_IMPL_NATIVE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), COBIWM_TYPE_BARRIER_IMPL_NATIVE, CobiwmBarrierImplNative))
#define COBIWM_BARRIER_IMPL_NATIVE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  COBIWM_TYPE_BARRIER_IMPL_NATIVE, CobiwmBarrierImplNativeClass))
#define COBIWM_IS_BARRIER_IMPL_NATIVE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), COBIWM_TYPE_BARRIER_IMPL_NATIVE))
#define COBIWM_IS_BARRIER_IMPL_NATIVE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  COBIWM_TYPE_BARRIER_IMPL_NATIVE))
#define COBIWM_BARRIER_IMPL_NATIVE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  COBIWM_TYPE_BARRIER_IMPL_NATIVE, CobiwmBarrierImplNativeClass))

typedef struct _CobiwmBarrierImplNative        CobiwmBarrierImplNative;
typedef struct _CobiwmBarrierImplNativeClass   CobiwmBarrierImplNativeClass;
typedef struct _CobiwmBarrierImplNativePrivate CobiwmBarrierImplNativePrivate;

typedef struct _CobiwmBarrierManagerNative     CobiwmBarrierManagerNative;

struct _CobiwmBarrierImplNative
{
  CobiwmBarrierImpl parent;
};

struct _CobiwmBarrierImplNativeClass
{
  CobiwmBarrierImplClass parent_class;
};

GType cobiwm_barrier_impl_native_get_type (void) G_GNUC_CONST;

CobiwmBarrierImpl *cobiwm_barrier_impl_native_new (CobiwmBarrier *barrier);

CobiwmBarrierManagerNative *cobiwm_barrier_manager_native_new (void);
void cobiwm_barrier_manager_native_process (CobiwmBarrierManagerNative *manager,
                                          ClutterInputDevice       *device,
                                          guint32                   time,
                                          float                    *x,
                                          float                    *y);

G_END_DECLS

#endif /* COBIWM_BARRIER_NATIVE_H */
