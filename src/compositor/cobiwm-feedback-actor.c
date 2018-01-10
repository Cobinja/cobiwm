/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
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

/**
 * SECTION:cobiwm-feedback-actor
 * @title: CobiwmFeedbackActor
 * @short_description: Actor for painting user interaction feedback
 */

#include <config.h>

#include "display-private.h"
#include "compositor-private.h"
#include "cobiwm-feedback-actor-private.h"

enum {
  PROP_ANCHOR_X = 1,
  PROP_ANCHOR_Y
};

typedef struct _CobiwmFeedbackActorPrivate CobiwmFeedbackActorPrivate;

struct _CobiwmFeedbackActorPrivate
{
  int anchor_x;
  int anchor_y;
  int pos_x;
  int pos_y;
};

G_DEFINE_TYPE_WITH_PRIVATE (CobiwmFeedbackActor, cobiwm_feedback_actor, CLUTTER_TYPE_ACTOR)

static void
cobiwm_feedback_actor_constructed (GObject *object)
{
  CobiwmDisplay *display;

  display = cobiwm_get_display ();
  clutter_actor_add_child (display->compositor->feedback_group,
                           CLUTTER_ACTOR (object));
}

static void
cobiwm_feedback_actor_update_position (CobiwmFeedbackActor *self)
{
  CobiwmFeedbackActorPrivate *priv = cobiwm_feedback_actor_get_instance_private (self);

  clutter_actor_set_position (CLUTTER_ACTOR (self),
                              priv->pos_x - priv->anchor_x,
                              priv->pos_y - priv->anchor_y);
}

static void
cobiwm_feedback_actor_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  CobiwmFeedbackActor *self = COBIWM_FEEDBACK_ACTOR (object);
  CobiwmFeedbackActorPrivate *priv = cobiwm_feedback_actor_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_ANCHOR_X:
      priv->anchor_x = g_value_get_int (value);
      cobiwm_feedback_actor_update_position (self);
      break;
    case PROP_ANCHOR_Y:
      priv->anchor_y = g_value_get_int (value);
      cobiwm_feedback_actor_update_position (self);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
cobiwm_feedback_actor_get_property (GObject      *object,
                                  guint         prop_id,
                                  GValue       *value,
                                  GParamSpec   *pspec)
{
  CobiwmFeedbackActor *self = COBIWM_FEEDBACK_ACTOR (object);
  CobiwmFeedbackActorPrivate *priv = cobiwm_feedback_actor_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_ANCHOR_X:
      g_value_set_int (value, priv->anchor_x);
      break;
    case PROP_ANCHOR_Y:
      g_value_set_int (value, priv->anchor_y);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
cobiwm_feedback_actor_class_init (CobiwmFeedbackActorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GParamSpec *pspec;

  object_class->constructed = cobiwm_feedback_actor_constructed;
  object_class->set_property = cobiwm_feedback_actor_set_property;
  object_class->get_property = cobiwm_feedback_actor_get_property;

  pspec = g_param_spec_int ("anchor-x",
                            "Anchor X",
                            "The X axis of the anchor point",
                            0, G_MAXINT, 0,
                            G_PARAM_READWRITE);

  g_object_class_install_property (object_class,
                                   PROP_ANCHOR_X,
                                   pspec);

  pspec = g_param_spec_int ("anchor-y",
                            "Anchor Y",
                            "The Y axis of the anchor point",
                            0, G_MAXINT, 0,
                            G_PARAM_READWRITE);

  g_object_class_install_property (object_class,
                                   PROP_ANCHOR_Y,
                                   pspec);
}

static void
cobiwm_feedback_actor_init (CobiwmFeedbackActor *self)
{
  clutter_actor_set_reactive (CLUTTER_ACTOR (self), FALSE);
}

/**
 * cobiwm_feedback_actor_new:
 *
 * Creates a new actor to draw the current drag and drop surface.
 *
 * Return value: the newly created background actor
 */
ClutterActor *
cobiwm_feedback_actor_new (int anchor_x,
                         int anchor_y)
{
  CobiwmFeedbackActor *self;

  self = g_object_new (COBIWM_TYPE_FEEDBACK_ACTOR,
                       "anchor-x", anchor_x,
                       "anchor-y", anchor_y,
                       NULL);

  return CLUTTER_ACTOR (self);
}

void
cobiwm_feedback_actor_set_anchor (CobiwmFeedbackActor *self,
                                int                anchor_x,
                                int                anchor_y)
{
  CobiwmFeedbackActorPrivate *priv;

  g_return_if_fail (COBIWM_IS_FEEDBACK_ACTOR (self));

  priv = cobiwm_feedback_actor_get_instance_private (self);

  if (priv->anchor_x == anchor_x && priv->anchor_y == anchor_y)
    return;

  if (priv->anchor_x != anchor_x)
    {
      priv->anchor_x = anchor_x;
      g_object_notify (G_OBJECT (self), "anchor-x");
    }

  if (priv->anchor_y != anchor_y)
    {
      priv->anchor_y = anchor_y;
      g_object_notify (G_OBJECT (self), "anchor-y");
    }

  cobiwm_feedback_actor_update_position (self);
}

void
cobiwm_feedback_actor_get_anchor (CobiwmFeedbackActor *self,
                                int               *anchor_x,
                                int               *anchor_y)
{
  CobiwmFeedbackActorPrivate *priv;

  g_return_if_fail (COBIWM_IS_FEEDBACK_ACTOR (self));

  priv = cobiwm_feedback_actor_get_instance_private (self);

  if (anchor_x)
    *anchor_x = priv->anchor_x;
  if (anchor_y)
    *anchor_y = priv->anchor_y;
}

void
cobiwm_feedback_actor_set_position (CobiwmFeedbackActor  *self,
                                  int                 x,
                                  int                 y)
{
  CobiwmFeedbackActorPrivate *priv;

  g_return_if_fail (COBIWM_IS_FEEDBACK_ACTOR (self));

  priv = cobiwm_feedback_actor_get_instance_private (self);
  priv->pos_x = x;
  priv->pos_y = y;

  cobiwm_feedback_actor_update_position (self);
}

void
cobiwm_feedback_actor_update (CobiwmFeedbackActor  *self,
                            const ClutterEvent *event)
{
  ClutterPoint point;

  g_return_if_fail (COBIWM_IS_FEEDBACK_ACTOR (self));
  g_return_if_fail (event != NULL);

  clutter_event_get_position (event, &point);
  cobiwm_feedback_actor_set_position (self, point.x, point.y);
}
