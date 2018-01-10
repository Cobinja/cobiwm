/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * cobiwm-dnd-actor-private.h: Actor for painting the DnD surface
 *
 * Copyright 2014 Red Hat, Inc.
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
 *
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#ifndef COBIWM_DND_ACTOR_PRIVATE_H
#define COBIWM_DND_ACTOR_PRIVATE_H

#include "cobiwm-feedback-actor-private.h"

/**
 * CobiwmDnDActor:
 *
 * This class handles the rendering of the DnD surface
 */

#define COBIWM_TYPE_DND_ACTOR            (cobiwm_dnd_actor_get_type ())
#define COBIWM_DND_ACTOR(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), COBIWM_TYPE_DND_ACTOR, CobiwmDnDActor))
#define COBIWM_DND_ACTOR_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), COBIWM_TYPE_DND_ACTOR, CobiwmDnDActorClass))
#define COBIWM_IS_DND_ACTOR(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), COBIWM_TYPE_DND_ACTOR))
#define COBIWM_IS_DND_ACTOR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), COBIWM_TYPE_DND_ACTOR))
#define COBIWM_DND_ACTOR_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), COBIWM_TYPE_DND_ACTOR, CobiwmDnDActorClass))

typedef struct _CobiwmDnDActor        CobiwmDnDActor;
typedef struct _CobiwmDnDActorClass   CobiwmDnDActorClass;

struct _CobiwmDnDActorClass
{
  /*< private >*/
  CobiwmFeedbackActorClass parent_class;
};

struct _CobiwmDnDActor
{
  CobiwmFeedbackActor parent;
};

GType         cobiwm_dnd_actor_get_type (void);

ClutterActor *cobiwm_dnd_actor_new (ClutterActor *drag_origin,
                                  int           start_x,
                                  int           start_y);

void          cobiwm_dnd_actor_drag_finish (CobiwmDnDActor *self,
                                          gboolean      success);

#endif /* COBIWM_DND_ACTOR_PRIVATE_H */
