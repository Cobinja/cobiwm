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

#ifndef COBIWM_BACKEND_X11_H
#define COBIWM_BACKEND_X11_H

#include "backends/cobiwm-backend-private.h"

#include <X11/Xlib.h>

#define COBIWM_TYPE_BACKEND_X11             (cobiwm_backend_x11_get_type ())
#define COBIWM_BACKEND_X11(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), COBIWM_TYPE_BACKEND_X11, CobiwmBackendX11))
#define COBIWM_BACKEND_X11_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass),  COBIWM_TYPE_BACKEND_X11, CobiwmBackendX11Class))
#define COBIWM_IS_BACKEND_X11(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), COBIWM_TYPE_BACKEND_X11))
#define COBIWM_IS_BACKEND_X11_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass),  COBIWM_TYPE_BACKEND_X11))
#define COBIWM_BACKEND_X11_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj),  COBIWM_TYPE_BACKEND_X11, CobiwmBackendX11Class))

typedef struct _CobiwmBackendX11        CobiwmBackendX11;
typedef struct _CobiwmBackendX11Class   CobiwmBackendX11Class;

struct _CobiwmBackendX11
{
  CobiwmBackend parent;
};

struct _CobiwmBackendX11Class
{
  CobiwmBackendClass parent_class;
};

GType cobiwm_backend_x11_get_type (void) G_GNUC_CONST;

Display * cobiwm_backend_x11_get_xdisplay (CobiwmBackendX11 *backend);

Window cobiwm_backend_x11_get_xwindow (CobiwmBackendX11 *backend);

#endif /* COBIWM_BACKEND_X11_H */
