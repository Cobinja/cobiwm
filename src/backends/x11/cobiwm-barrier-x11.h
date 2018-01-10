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

#ifndef COBIWM_BARRIER_X11_H
#define COBIWM_BARRIER_X11_H

#include "backends/cobiwm-barrier-private.h"

G_BEGIN_DECLS

#define COBIWM_TYPE_BARRIER_IMPL_X11            (cobiwm_barrier_impl_x11_get_type ())
#define COBIWM_BARRIER_IMPL_X11(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), COBIWM_TYPE_BARRIER_IMPL_X11, CobiwmBarrierImplX11))
#define COBIWM_BARRIER_IMPL_X11_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  COBIWM_TYPE_BARRIER_IMPL_X11, CobiwmBarrierImplX11Class))
#define COBIWM_IS_BARRIER_IMPL_X11(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), COBIWM_TYPE_BARRIER_IMPL_X11))
#define COBIWM_IS_BARRIER_IMPL_X11_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  COBIWM_TYPE_BARRIER_IMPL_X11))
#define COBIWM_BARRIER_IMPL_X11_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  COBIWM_TYPE_BARRIER_IMPL_X11, CobiwmBarrierImplX11Class))

typedef struct _CobiwmBarrierImplX11        CobiwmBarrierImplX11;
typedef struct _CobiwmBarrierImplX11Class   CobiwmBarrierImplX11Class;
typedef struct _CobiwmBarrierImplX11Private CobiwmBarrierImplX11Private;

struct _CobiwmBarrierImplX11
{
  CobiwmBarrierImpl parent;
};

struct _CobiwmBarrierImplX11Class
{
  CobiwmBarrierImplClass parent_class;
};

GType cobiwm_barrier_impl_x11_get_type (void) G_GNUC_CONST;

CobiwmBarrierImpl *cobiwm_barrier_impl_x11_new (CobiwmBarrier *barrier);

G_END_DECLS

#endif /* COBIWM_BARRIER_X11_H1 */
