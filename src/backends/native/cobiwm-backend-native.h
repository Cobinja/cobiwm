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

#ifndef COBIWM_BACKEND_NATIVE_H
#define COBIWM_BACKEND_NATIVE_H

#include "backends/cobiwm-backend-private.h"

#define COBIWM_TYPE_BACKEND_NATIVE             (cobiwm_backend_native_get_type ())
#define COBIWM_BACKEND_NATIVE(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), COBIWM_TYPE_BACKEND_NATIVE, CobiwmBackendNative))
#define COBIWM_BACKEND_NATIVE_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass),  COBIWM_TYPE_BACKEND_NATIVE, CobiwmBackendNativeClass))
#define COBIWM_IS_BACKEND_NATIVE(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), COBIWM_TYPE_BACKEND_NATIVE))
#define COBIWM_IS_BACKEND_NATIVE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass),  COBIWM_TYPE_BACKEND_NATIVE))
#define COBIWM_BACKEND_NATIVE_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj),  COBIWM_TYPE_BACKEND_NATIVE, CobiwmBackendNativeClass))

typedef struct _CobiwmBackendNative        CobiwmBackendNative;
typedef struct _CobiwmBackendNativeClass   CobiwmBackendNativeClass;

struct _CobiwmBackendNative
{
  CobiwmBackend parent;
};

struct _CobiwmBackendNativeClass
{
  CobiwmBackendClass parent_class;
};

GType cobiwm_backend_native_get_type (void) G_GNUC_CONST;

gboolean cobiwm_activate_vt (int vt, GError **error);

#endif /* COBIWM_BACKEND_NATIVE_H */
