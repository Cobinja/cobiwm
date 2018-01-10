/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * cobiwm-background-actor.h: Actor for painting the root window background
 *
 * Copyright 2010 Red Hat, Inc.
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

#ifndef COBIWM_BACKGROUND_ACTOR_H
#define COBIWM_BACKGROUND_ACTOR_H

#include <clutter/clutter.h>
#include <screen.h>
#include <cobiwm-background.h>

#include <gsettings-desktop-schemas/gdesktop-enums.h>

/**
 * CobiwmBackgroundActor:
 *
 * This class handles tracking and painting the root window background.
 * By integrating with #CobiwmWindowGroup we can avoid painting parts of
 * the background that are obscured by other windows.
 */

#define COBIWM_TYPE_BACKGROUND_ACTOR            (cobiwm_background_actor_get_type ())
#define COBIWM_BACKGROUND_ACTOR(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), COBIWM_TYPE_BACKGROUND_ACTOR, CobiwmBackgroundActor))
#define COBIWM_BACKGROUND_ACTOR_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), COBIWM_TYPE_BACKGROUND_ACTOR, CobiwmBackgroundActorClass))
#define COBIWM_IS_BACKGROUND_ACTOR(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), COBIWM_TYPE_BACKGROUND_ACTOR))
#define COBIWM_IS_BACKGROUND_ACTOR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), COBIWM_TYPE_BACKGROUND_ACTOR))
#define COBIWM_BACKGROUND_ACTOR_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), COBIWM_TYPE_BACKGROUND_ACTOR, CobiwmBackgroundActorClass))

typedef struct _CobiwmBackgroundActor        CobiwmBackgroundActor;
typedef struct _CobiwmBackgroundActorClass   CobiwmBackgroundActorClass;
typedef struct _CobiwmBackgroundActorPrivate CobiwmBackgroundActorPrivate;

struct _CobiwmBackgroundActorClass
{
  /*< private >*/
  ClutterActorClass parent_class;
};

struct _CobiwmBackgroundActor
{
  ClutterActor parent;

  CobiwmBackgroundActorPrivate *priv;
};

GType cobiwm_background_actor_get_type (void);

ClutterActor *cobiwm_background_actor_new    (CobiwmScreen *screen,
                                            int         monitor);

void cobiwm_background_actor_set_background  (CobiwmBackgroundActor *self,
                                            CobiwmBackground      *background);

void cobiwm_background_actor_set_vignette (CobiwmBackgroundActor *self,
                                         gboolean             enabled,
                                         double               brightness,
                                         double               sharpness);

#endif /* COBIWM_BACKGROUND_ACTOR_H */
