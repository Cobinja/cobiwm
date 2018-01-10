/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * cobiwm-feedback-actor-private.h: Actor for painting user interaction feedback
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

#ifndef COBIWM_FEEDBACK_ACTOR_PRIVATE_H
#define COBIWM_FEEDBACK_ACTOR_PRIVATE_H

#include <clutter/clutter.h>

/**
 * CobiwmFeedbackActor:
 *
 * This class handles the rendering of user interaction feedback
 */

#define COBIWM_TYPE_FEEDBACK_ACTOR            (cobiwm_feedback_actor_get_type ())
#define COBIWM_FEEDBACK_ACTOR(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), COBIWM_TYPE_FEEDBACK_ACTOR, CobiwmFeedbackActor))
#define COBIWM_FEEDBACK_ACTOR_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), COBIWM_TYPE_FEEDBACK_ACTOR, CobiwmFeedbackActorClass))
#define COBIWM_IS_FEEDBACK_ACTOR(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), COBIWM_TYPE_FEEDBACK_ACTOR))
#define COBIWM_IS_FEEDBACK_ACTOR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), COBIWM_TYPE_FEEDBACK_ACTOR))
#define COBIWM_FEEDBACK_ACTOR_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), COBIWM_TYPE_FEEDBACK_ACTOR, CobiwmFeedbackActorClass))

typedef struct _CobiwmFeedbackActor        CobiwmFeedbackActor;
typedef struct _CobiwmFeedbackActorClass   CobiwmFeedbackActorClass;

struct _CobiwmFeedbackActorClass
{
  /*< private >*/
  ClutterActorClass parent_class;
};

struct _CobiwmFeedbackActor
{
  ClutterActor parent;
};

GType cobiwm_feedback_actor_get_type (void);

ClutterActor *cobiwm_feedback_actor_new (int anchor_x,
                                       int anchor_y);

void cobiwm_feedback_actor_set_anchor (CobiwmFeedbackActor *actor,
                                     int                anchor_x,
                                     int                anchor_y);
void cobiwm_feedback_actor_get_anchor (CobiwmFeedbackActor *actor,
                                     int               *anchor_x,
                                     int               *anchor_y);

void cobiwm_feedback_actor_set_position (CobiwmFeedbackActor  *self,
                                       int                 x,
                                       int                 y);

void cobiwm_feedback_actor_update (CobiwmFeedbackActor  *self,
                                 const ClutterEvent *event);

#endif /* COBIWM_FEEDBACK_ACTOR_PRIVATE_H */
