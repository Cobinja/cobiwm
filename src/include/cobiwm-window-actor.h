/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2008 Matthew Allum
 * Copyright (C) 2007 Iain Holmes
 * Based on xcompmgr - (c) 2003 Keith Packard
 *          xfwm4    - (c) 2005-2007 Olivier Fourdan
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef COBIWM_WINDOW_ACTOR_H_
#define COBIWM_WINDOW_ACTOR_H_

#include <clutter/clutter.h>
#include <X11/Xlib.h>

#include <compositor.h>

/*
 * CobiwmWindowActor object (ClutterGroup sub-class)
 */
#define COBIWM_TYPE_WINDOW_ACTOR            (cobiwm_window_actor_get_type ())
#define COBIWM_WINDOW_ACTOR(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), COBIWM_TYPE_WINDOW_ACTOR, CobiwmWindowActor))
#define COBIWM_WINDOW_ACTOR_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), COBIWM_TYPE_WINDOW_ACTOR, CobiwmWindowActorClass))
#define COBIWM_IS_WINDOW_ACTOR(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), COBIWM_TYPE_WINDOW_ACTOR))
#define COBIWM_IS_WINDOW_ACTOR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), COBIWM_TYPE_WINDOW_ACTOR))
#define COBIWM_WINDOW_ACTOR_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), COBIWM_TYPE_WINDOW_ACTOR, CobiwmWindowActorClass))

typedef struct _CobiwmWindowActor        CobiwmWindowActor;
typedef struct _CobiwmWindowActorClass   CobiwmWindowActorClass;
typedef struct _CobiwmWindowActorPrivate CobiwmWindowActorPrivate;

struct _CobiwmWindowActorClass
{
  /*< private >*/
  ClutterActorClass parent_class;
};

struct _CobiwmWindowActor
{
  ClutterActor           parent;

  CobiwmWindowActorPrivate *priv;
};

GType cobiwm_window_actor_get_type (void);

Window             cobiwm_window_actor_get_x_window         (CobiwmWindowActor *self);
CobiwmWindow *       cobiwm_window_actor_get_cobiwm_window      (CobiwmWindowActor *self);
ClutterActor *     cobiwm_window_actor_get_texture          (CobiwmWindowActor *self);
gboolean       cobiwm_window_actor_is_destroyed (CobiwmWindowActor *self);

typedef enum {
  COBIWM_SHADOW_MODE_AUTO,
  COBIWM_SHADOW_MODE_FORCED_OFF,
  COBIWM_SHADOW_MODE_FORCED_ON,
} CobiwmShadowMode;

#endif /* COBIWM_WINDOW_ACTOR_H */
