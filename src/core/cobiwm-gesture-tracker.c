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
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

/**
 * SECTION:gesture-tracker
 * @Title: CobiwmGestureTracker
 * @Short_Description: Manages gestures on windows/desktop
 *
 * Forwards touch events to clutter actors, and accepts/rejects touch sequences
 * based on the outcome of those.
 */

#include "config.h"
#include "cobiwm-gesture-tracker-private.h"
#include "cobiwm-surface-actor.h"

#define DISTANCE_THRESHOLD 30

typedef struct _CobiwmGestureTrackerPrivate CobiwmGestureTrackerPrivate;
typedef struct _GestureActionData GestureActionData;
typedef struct _CobiwmSequenceInfo CobiwmSequenceInfo;

struct _CobiwmSequenceInfo
{
  CobiwmGestureTracker *tracker;
  ClutterEventSequence *sequence;
  CobiwmSequenceState state;
  guint autodeny_timeout_id;
  gfloat start_x;
  gfloat start_y;
};

struct _GestureActionData
{
  ClutterGestureAction *gesture;
  CobiwmSequenceState state;
  guint gesture_begin_id;
  guint gesture_end_id;
  guint gesture_cancel_id;
};

struct _CobiwmGestureTrackerPrivate
{
  GHashTable *sequences; /* Hashtable of ClutterEventSequence->CobiwmSequenceInfo */

  CobiwmSequenceState stage_state;
  GArray *stage_gestures; /* Array of GestureActionData */
  GList *listeners; /* List of ClutterGestureAction */
  guint autodeny_timeout;
};

enum {
  PROP_0,
  PROP_AUTODENY_TIMEOUT,
  LAST_PROP,
};

static GParamSpec *obj_props[LAST_PROP];

enum {
  STATE_CHANGED,
  N_SIGNALS
};

static guint signals[N_SIGNALS] = { 0 };

#define DEFAULT_AUTODENY_TIMEOUT 150

static void cobiwm_gesture_tracker_untrack_stage (CobiwmGestureTracker *tracker);

G_DEFINE_TYPE_WITH_PRIVATE (CobiwmGestureTracker, cobiwm_gesture_tracker, G_TYPE_OBJECT)

static void
cobiwm_gesture_tracker_finalize (GObject *object)
{
  CobiwmGestureTrackerPrivate *priv;

  priv = cobiwm_gesture_tracker_get_instance_private (COBIWM_GESTURE_TRACKER (object));

  g_hash_table_destroy (priv->sequences);
  g_array_free (priv->stage_gestures, TRUE);
  g_list_free (priv->listeners);

  G_OBJECT_CLASS (cobiwm_gesture_tracker_parent_class)->finalize (object);
}

static void
cobiwm_gesture_tracker_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  CobiwmGestureTrackerPrivate *priv;

  priv = cobiwm_gesture_tracker_get_instance_private (COBIWM_GESTURE_TRACKER (object));

  switch (prop_id)
    {
    case PROP_AUTODENY_TIMEOUT:
      priv->autodeny_timeout = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
cobiwm_gesture_tracker_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  CobiwmGestureTrackerPrivate *priv;

  priv = cobiwm_gesture_tracker_get_instance_private (COBIWM_GESTURE_TRACKER (object));

  switch (prop_id)
    {
    case PROP_AUTODENY_TIMEOUT:
      g_value_set_uint (value, priv->autodeny_timeout);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
cobiwm_gesture_tracker_class_init (CobiwmGestureTrackerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = cobiwm_gesture_tracker_finalize;
  object_class->set_property = cobiwm_gesture_tracker_set_property;
  object_class->get_property = cobiwm_gesture_tracker_get_property;

  obj_props[PROP_AUTODENY_TIMEOUT] = g_param_spec_uint ("autodeny-timeout",
                                                        "Auto-deny timeout",
                                                        "Auto-deny timeout",
                                                        0, G_MAXUINT, DEFAULT_AUTODENY_TIMEOUT,
                                                        G_PARAM_STATIC_STRINGS |
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_properties (object_class, LAST_PROP, obj_props);

  signals[STATE_CHANGED] =
    g_signal_new ("state-changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (CobiwmGestureTrackerClass, state_changed),
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 2, G_TYPE_POINTER, G_TYPE_UINT);
}

static gboolean
autodeny_sequence (gpointer user_data)
{
  CobiwmSequenceInfo *info = user_data;

  /* Deny the sequence automatically after the given timeout */
  if (info->state == COBIWM_SEQUENCE_NONE)
    cobiwm_gesture_tracker_set_sequence_state (info->tracker, info->sequence,
                                             COBIWM_SEQUENCE_REJECTED);

  info->autodeny_timeout_id = 0;
  return G_SOURCE_REMOVE;
}

static CobiwmSequenceInfo *
cobiwm_sequence_info_new (CobiwmGestureTracker *tracker,
                        const ClutterEvent *event)
{
  CobiwmGestureTrackerPrivate *priv;
  CobiwmSequenceInfo *info;
  guint ms;

  priv = cobiwm_gesture_tracker_get_instance_private (tracker);
  ms = priv->autodeny_timeout;

  info = g_slice_new0 (CobiwmSequenceInfo);
  info->tracker = tracker;
  info->sequence = event->touch.sequence;
  info->state = COBIWM_SEQUENCE_NONE;
  info->autodeny_timeout_id = g_timeout_add (ms, autodeny_sequence, info);

  clutter_event_get_coords (event, &info->start_x, &info->start_y);

  return info;
}

static void
cobiwm_sequence_info_free (CobiwmSequenceInfo *info)
{
  if (info->autodeny_timeout_id)
    g_source_remove (info->autodeny_timeout_id);

  if (info->state == COBIWM_SEQUENCE_NONE)
    cobiwm_gesture_tracker_set_sequence_state (info->tracker, info->sequence,
                                             COBIWM_SEQUENCE_REJECTED);
  g_slice_free (CobiwmSequenceInfo, info);
}

static gboolean
state_is_applicable (CobiwmSequenceState prev_state,
                     CobiwmSequenceState state)
{
  if (prev_state == COBIWM_SEQUENCE_PENDING_END)
    return FALSE;

  /* Don't allow reverting to none */
  if (state == COBIWM_SEQUENCE_NONE)
    return FALSE;

  /* PENDING_END state is final */
  if (prev_state == COBIWM_SEQUENCE_PENDING_END)
    return FALSE;

  /* Sequences must be accepted/denied before PENDING_END */
  if (prev_state == COBIWM_SEQUENCE_NONE &&
      state == COBIWM_SEQUENCE_PENDING_END)
    return FALSE;

  /* Make sequences stick to their accepted/denied state */
  if (state != COBIWM_SEQUENCE_PENDING_END &&
      prev_state != COBIWM_SEQUENCE_NONE)
    return FALSE;

  return TRUE;
}

static gboolean
cobiwm_gesture_tracker_set_state (CobiwmGestureTracker *tracker,
                                CobiwmSequenceState   state)
{
  CobiwmGestureTrackerPrivate *priv;
  ClutterEventSequence *sequence;
  GHashTableIter iter;

  priv = cobiwm_gesture_tracker_get_instance_private (tracker);

  if (priv->stage_state != state &&
      !state_is_applicable (priv->stage_state, state))
    return FALSE;

  g_hash_table_iter_init (&iter, priv->sequences);
  priv->stage_state = state;

  while (g_hash_table_iter_next (&iter, (gpointer*) &sequence, NULL))
    cobiwm_gesture_tracker_set_sequence_state (tracker, sequence, state);

  return TRUE;
}

static gboolean
gesture_begin_cb (ClutterGestureAction *gesture,
                  ClutterActor         *actor,
                  CobiwmGestureTracker   *tracker)
{
  CobiwmGestureTrackerPrivate *priv;

  priv = cobiwm_gesture_tracker_get_instance_private (tracker);

  if (!g_list_find (priv->listeners, gesture) &&
      cobiwm_gesture_tracker_set_state (tracker, COBIWM_SEQUENCE_ACCEPTED))
    priv->listeners = g_list_prepend (priv->listeners, gesture);

  return TRUE;
}

static void
gesture_end_cb (ClutterGestureAction *gesture,
                ClutterActor         *actor,
                CobiwmGestureTracker   *tracker)
{
  CobiwmGestureTrackerPrivate *priv;

  priv = cobiwm_gesture_tracker_get_instance_private (tracker);
  priv->listeners = g_list_remove (priv->listeners, gesture);

  if (!priv->listeners)
    cobiwm_gesture_tracker_untrack_stage (tracker);
}

static void
gesture_cancel_cb (ClutterGestureAction *gesture,
                   ClutterActor         *actor,
                   CobiwmGestureTracker   *tracker)
{
  CobiwmGestureTrackerPrivate *priv;

  priv = cobiwm_gesture_tracker_get_instance_private (tracker);

  if (g_list_find (priv->listeners, gesture))
    {
      priv->listeners = g_list_remove (priv->listeners, gesture);

      if (!priv->listeners)
        cobiwm_gesture_tracker_set_state (tracker, COBIWM_SEQUENCE_PENDING_END);
    }
}

static gboolean
cancel_and_unref_gesture_cb (ClutterGestureAction *action)
{
  clutter_gesture_action_cancel (action);
  g_object_unref (action);
  return G_SOURCE_REMOVE;
}

static void
clear_gesture_data (GestureActionData *data)
{
  g_signal_handler_disconnect (data->gesture, data->gesture_begin_id);
  g_signal_handler_disconnect (data->gesture, data->gesture_end_id);
  g_signal_handler_disconnect (data->gesture, data->gesture_cancel_id);

  /* Defer cancellation to an idle, as it may happen within event handling */
  g_idle_add ((GSourceFunc) cancel_and_unref_gesture_cb, data->gesture);
}

static void
cobiwm_gesture_tracker_init (CobiwmGestureTracker *tracker)
{
  CobiwmGestureTrackerPrivate *priv;

  priv = cobiwm_gesture_tracker_get_instance_private (tracker);
  priv->sequences = g_hash_table_new_full (NULL, NULL, NULL,
                                           (GDestroyNotify) cobiwm_sequence_info_free);
  priv->stage_gestures = g_array_new (FALSE, FALSE, sizeof (GestureActionData));
  g_array_set_clear_func (priv->stage_gestures, (GDestroyNotify) clear_gesture_data);
}

CobiwmGestureTracker *
cobiwm_gesture_tracker_new (void)
{
  return g_object_new (COBIWM_TYPE_GESTURE_TRACKER, NULL);
}

static void
cobiwm_gesture_tracker_track_stage (CobiwmGestureTracker *tracker,
                                  ClutterActor       *stage)
{
  CobiwmGestureTrackerPrivate *priv;
  GList *actions, *l;

  priv = cobiwm_gesture_tracker_get_instance_private (tracker);
  actions = clutter_actor_get_actions (stage);

  for (l = actions; l; l = l->next)
    {
      GestureActionData data;

      if (!CLUTTER_IS_GESTURE_ACTION (l->data))
        continue;

      data.gesture = g_object_ref (l->data);
      data.state = COBIWM_SEQUENCE_NONE;
      data.gesture_begin_id =
        g_signal_connect (data.gesture, "gesture-begin",
                          G_CALLBACK (gesture_begin_cb), tracker);
      data.gesture_end_id =
        g_signal_connect (data.gesture, "gesture-end",
                          G_CALLBACK (gesture_end_cb), tracker);
      data.gesture_cancel_id =
        g_signal_connect (data.gesture, "gesture-cancel",
                          G_CALLBACK (gesture_cancel_cb), tracker);
      g_array_append_val (priv->stage_gestures, data);
    }

  g_list_free (actions);
}

static void
cobiwm_gesture_tracker_untrack_stage (CobiwmGestureTracker *tracker)
{
  CobiwmGestureTrackerPrivate *priv;

  priv = cobiwm_gesture_tracker_get_instance_private (tracker);
  priv->stage_state = COBIWM_SEQUENCE_NONE;

  g_hash_table_remove_all (priv->sequences);

  if (priv->stage_gestures->len > 0)
    g_array_remove_range (priv->stage_gestures, 0, priv->stage_gestures->len);

  g_list_free (priv->listeners);
  priv->listeners = NULL;
}

gboolean
cobiwm_gesture_tracker_handle_event (CobiwmGestureTracker *tracker,
				   const ClutterEvent *event)
{
  CobiwmGestureTrackerPrivate *priv;
  ClutterEventSequence *sequence;
  CobiwmSequenceState state;
  CobiwmSequenceInfo *info;
  ClutterActor *stage;
  gfloat x, y;

  sequence = clutter_event_get_event_sequence (event);

  if (!sequence)
    return FALSE;

  priv = cobiwm_gesture_tracker_get_instance_private (tracker);
  stage = CLUTTER_ACTOR (clutter_event_get_stage (event));

  switch (event->type)
    {
    case CLUTTER_TOUCH_BEGIN:
      if (g_hash_table_size (priv->sequences) == 0)
        cobiwm_gesture_tracker_track_stage (tracker, stage);

      info = cobiwm_sequence_info_new (tracker, event);
      g_hash_table_insert (priv->sequences, sequence, info);

      if (priv->stage_gestures->len == 0)
        {
          /* If no gestures are attached, reject the sequence right away */
          cobiwm_gesture_tracker_set_sequence_state (tracker, sequence,
                                                   COBIWM_SEQUENCE_REJECTED);
        }
      else if (priv->stage_state != COBIWM_SEQUENCE_NONE)
        {
          /* Make the sequence state match the general state */
          cobiwm_gesture_tracker_set_sequence_state (tracker, sequence,
                                                   priv->stage_state);
        }
      state = info->state;
      break;
    case CLUTTER_TOUCH_END:
      info = g_hash_table_lookup (priv->sequences, sequence);

      if (!info)
        return FALSE;

      /* If nothing was done yet about the sequence, reject it so X11
       * clients may see it
       */
      if (info->state == COBIWM_SEQUENCE_NONE)
        cobiwm_gesture_tracker_set_sequence_state (tracker, sequence,
                                                 COBIWM_SEQUENCE_REJECTED);

      state = info->state;
      g_hash_table_remove (priv->sequences, sequence);

      if (g_hash_table_size (priv->sequences) == 0)
        cobiwm_gesture_tracker_untrack_stage (tracker);
      break;
    case CLUTTER_TOUCH_UPDATE:
      info = g_hash_table_lookup (priv->sequences, sequence);

      if (!info)
        return FALSE;

      clutter_event_get_coords (event, &x, &y);

      if (info->state == COBIWM_SEQUENCE_NONE &&
          (ABS (info->start_x - x) > DISTANCE_THRESHOLD ||
           ABS (info->start_y - y) > DISTANCE_THRESHOLD))
        cobiwm_gesture_tracker_set_sequence_state (tracker, sequence,
                                                 COBIWM_SEQUENCE_REJECTED);
      state = info->state;
      break;
    default:
      return FALSE;
      break;
    }

  /* As soon as a sequence is accepted, we replay it to
   * the stage as a captured event, and make sure it's never
   * propagated anywhere else. Since ClutterGestureAction does
   * all its event handling from a captured-event handler on
   * the stage, this effectively acts as a "sequence grab" on
   * gesture actions.
   *
   * Sequences that aren't (yet or never) in an accepted state
   * will go through, these events will get processed through
   * the compositor, and eventually through clutter, still
   * triggering the gestures capturing events on the stage, and
   * possibly resulting in CobiwmSequenceState changes.
   */
  if (state == COBIWM_SEQUENCE_ACCEPTED)
    {
      clutter_actor_event (CLUTTER_ACTOR (clutter_event_get_stage (event)),
                           event, TRUE);
      return TRUE;
    }

  return FALSE;
}

gboolean
cobiwm_gesture_tracker_set_sequence_state (CobiwmGestureTracker   *tracker,
                                         ClutterEventSequence *sequence,
                                         CobiwmSequenceState     state)
{
  CobiwmGestureTrackerPrivate *priv;
  CobiwmSequenceInfo *info;

  g_return_val_if_fail (COBIWM_IS_GESTURE_TRACKER (tracker), FALSE);

  priv = cobiwm_gesture_tracker_get_instance_private (tracker);
  info = g_hash_table_lookup (priv->sequences, sequence);

  if (!info)
    return FALSE;
  else if (state == info->state)
    return TRUE;

  if (!state_is_applicable (info->state, state))
    return FALSE;

  /* Unset autodeny timeout */
  if (info->autodeny_timeout_id)
    {
      g_source_remove (info->autodeny_timeout_id);
      info->autodeny_timeout_id = 0;
    }

  info->state = state;
  g_signal_emit (tracker, signals[STATE_CHANGED], 0, sequence, info->state);

  /* If the sequence was denied, set immediately to PENDING_END after emission */
  if (state == COBIWM_SEQUENCE_REJECTED)
    {
      info->state = COBIWM_SEQUENCE_PENDING_END;
      g_signal_emit (tracker, signals[STATE_CHANGED], 0, sequence, info->state);
    }

  return TRUE;
}

CobiwmSequenceState
cobiwm_gesture_tracker_get_sequence_state (CobiwmGestureTracker   *tracker,
                                         ClutterEventSequence *sequence)
{
  CobiwmGestureTrackerPrivate *priv;
  CobiwmSequenceInfo *info;

  g_return_val_if_fail (COBIWM_IS_GESTURE_TRACKER (tracker), COBIWM_SEQUENCE_PENDING_END);

  priv = cobiwm_gesture_tracker_get_instance_private (tracker);
  info = g_hash_table_lookup (priv->sequences, sequence);

  if (!info)
    return COBIWM_SEQUENCE_PENDING_END;

  return info->state;
}

gint
cobiwm_gesture_tracker_get_n_current_touches (CobiwmGestureTracker *tracker)
{
  CobiwmGestureTrackerPrivate *priv;

  g_return_val_if_fail (COBIWM_IS_GESTURE_TRACKER (tracker), 0);

  priv = cobiwm_gesture_tracker_get_instance_private (tracker);
  return g_hash_table_size (priv->sequences);
}
