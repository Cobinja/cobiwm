/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2013 Red Hat
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
 *     Owen Taylor <otaylor@redhat.com>
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

#ifndef __COBIWM_SURFACE_ACTOR_X11_H__
#define __COBIWM_SURFACE_ACTOR_X11_H__

#include <glib-object.h>

#include "cobiwm-surface-actor.h"

#include <X11/extensions/Xdamage.h>

#include <display.h>
#include <window.h>

G_BEGIN_DECLS

#define COBIWM_TYPE_SURFACE_ACTOR_X11            (cobiwm_surface_actor_x11_get_type ())
#define COBIWM_SURFACE_ACTOR_X11(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), COBIWM_TYPE_SURFACE_ACTOR_X11, CobiwmSurfaceActorX11))
#define COBIWM_SURFACE_ACTOR_X11_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  COBIWM_TYPE_SURFACE_ACTOR_X11, CobiwmSurfaceActorX11Class))
#define COBIWM_IS_SURFACE_ACTOR_X11(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), COBIWM_TYPE_SURFACE_ACTOR_X11))
#define COBIWM_IS_SURFACE_ACTOR_X11_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  COBIWM_TYPE_SURFACE_ACTOR_X11))
#define COBIWM_SURFACE_ACTOR_X11_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  COBIWM_TYPE_SURFACE_ACTOR_X11, CobiwmSurfaceActorX11Class))

typedef struct _CobiwmSurfaceActorX11      CobiwmSurfaceActorX11;
typedef struct _CobiwmSurfaceActorX11Class CobiwmSurfaceActorX11Class;

struct _CobiwmSurfaceActorX11
{
  CobiwmSurfaceActor parent;
};

struct _CobiwmSurfaceActorX11Class
{
  CobiwmSurfaceActorClass parent_class;
};

GType cobiwm_surface_actor_x11_get_type (void);

CobiwmSurfaceActor * cobiwm_surface_actor_x11_new (CobiwmWindow *window);

void cobiwm_surface_actor_x11_set_size (CobiwmSurfaceActorX11 *self,
                                      int width, int height);

G_END_DECLS

#endif /* __COBIWM_SURFACE_ACTOR_X11_H__ */
