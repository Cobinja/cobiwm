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
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

#ifndef __COBIWM_SURFACE_ACTOR_WAYLAND_H__
#define __COBIWM_SURFACE_ACTOR_WAYLAND_H__

#include <glib-object.h>

#include "cobiwm-surface-actor.h"

#include "wayland/cobiwm-wayland.h"
#include "wayland/cobiwm-wayland-private.h"

#include "backends/cobiwm-monitor-manager-private.h"

G_BEGIN_DECLS

#define COBIWM_TYPE_SURFACE_ACTOR_WAYLAND            (cobiwm_surface_actor_wayland_get_type ())
#define COBIWM_SURFACE_ACTOR_WAYLAND(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), COBIWM_TYPE_SURFACE_ACTOR_WAYLAND, CobiwmSurfaceActorWayland))
#define COBIWM_SURFACE_ACTOR_WAYLAND_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  COBIWM_TYPE_SURFACE_ACTOR_WAYLAND, CobiwmSurfaceActorWaylandClass))
#define COBIWM_IS_SURFACE_ACTOR_WAYLAND(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), COBIWM_TYPE_SURFACE_ACTOR_WAYLAND))
#define COBIWM_IS_SURFACE_ACTOR_WAYLAND_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  COBIWM_TYPE_SURFACE_ACTOR_WAYLAND))
#define COBIWM_SURFACE_ACTOR_WAYLAND_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  COBIWM_TYPE_SURFACE_ACTOR_WAYLAND, CobiwmSurfaceActorWaylandClass))

typedef struct _CobiwmSurfaceActorWayland      CobiwmSurfaceActorWayland;
typedef struct _CobiwmSurfaceActorWaylandClass CobiwmSurfaceActorWaylandClass;

struct _CobiwmSurfaceActorWayland
{
  CobiwmSurfaceActor parent;
};

struct _CobiwmSurfaceActorWaylandClass
{
  CobiwmSurfaceActorClass parent_class;
};

GType cobiwm_surface_actor_wayland_get_type (void);

CobiwmSurfaceActor * cobiwm_surface_actor_wayland_new (CobiwmWaylandSurface *surface);
CobiwmWaylandSurface * cobiwm_surface_actor_wayland_get_surface (CobiwmSurfaceActorWayland *self);
void cobiwm_surface_actor_wayland_surface_destroyed (CobiwmSurfaceActorWayland *self);

void cobiwm_surface_actor_wayland_set_texture (CobiwmSurfaceActorWayland *self,
                                             CoglTexture *texture);

double cobiwm_surface_actor_wayland_get_scale (CobiwmSurfaceActorWayland *actor);

void cobiwm_surface_actor_wayland_get_subsurface_rect (CobiwmSurfaceActorWayland *self,
                                                     CobiwmRectangle           *rect);

void cobiwm_surface_actor_wayland_sync_state (CobiwmSurfaceActorWayland *self);

void cobiwm_surface_actor_wayland_sync_state_recursive (CobiwmSurfaceActorWayland *self);

void cobiwm_surface_actor_wayland_sync_subsurface_state (CobiwmSurfaceActorWayland *self);

gboolean cobiwm_surface_actor_wayland_is_on_monitor (CobiwmSurfaceActorWayland *self,
                                                   CobiwmMonitorInfo         *monitor);

void cobiwm_surface_actor_wayland_add_frame_callbacks (CobiwmSurfaceActorWayland *self,
                                                     struct wl_list *frame_callbacks);

G_END_DECLS

#endif /* __COBIWM_SURFACE_ACTOR_WAYLAND_H__ */
