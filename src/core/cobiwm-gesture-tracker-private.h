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
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#ifndef COBIWM_GESTURE_TRACKER_PRIVATE_H
#define COBIWM_GESTURE_TRACKER_PRIVATE_H

#include <glib-object.h>
#include <clutter/clutter.h>
#include <window.h>

#define COBIWM_TYPE_GESTURE_TRACKER            (cobiwm_gesture_tracker_get_type ())
#define COBIWM_GESTURE_TRACKER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), COBIWM_TYPE_GESTURE_TRACKER, CobiwmGestureTracker))
#define COBIWM_GESTURE_TRACKER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  COBIWM_TYPE_GESTURE_TRACKER, CobiwmGestureTrackerClass))
#define COBIWM_IS_GESTURE_TRACKER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), COBIWM_TYPE_GESTURE_TRACKER))
#define COBIWM_IS_GESTURE_TRACKER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  COBIWM_TYPE_GESTURE_TRACKER))
#define COBIWM_GESTURE_TRACKER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  COBIWM_TYPE_GESTURE_TRACKER, CobiwmGestureTrackerClass))

typedef struct _CobiwmGestureTracker CobiwmGestureTracker;
typedef struct _CobiwmGestureTrackerClass CobiwmGestureTrackerClass;

typedef enum {
  COBIWM_SEQUENCE_NONE,
  COBIWM_SEQUENCE_ACCEPTED,
  COBIWM_SEQUENCE_REJECTED,
  COBIWM_SEQUENCE_PENDING_END
} CobiwmSequenceState;

struct _CobiwmGestureTracker
{
  GObject parent_instance;
};

struct _CobiwmGestureTrackerClass
{
  GObjectClass parent_class;

  void (* state_changed) (CobiwmGestureTracker   *tracker,
                          ClutterEventSequence *sequence,
                          CobiwmSequenceState     state);
};

GType                cobiwm_gesture_tracker_get_type           (void) G_GNUC_CONST;

CobiwmGestureTracker * cobiwm_gesture_tracker_new                (void);

gboolean             cobiwm_gesture_tracker_handle_event       (CobiwmGestureTracker   *tracker,
                                                              const ClutterEvent   *event);
gboolean             cobiwm_gesture_tracker_set_sequence_state (CobiwmGestureTracker   *tracker,
                                                              ClutterEventSequence *sequence,
                                                              CobiwmSequenceState     state);
CobiwmSequenceState    cobiwm_gesture_tracker_get_sequence_state (CobiwmGestureTracker   *tracker,
                                                              ClutterEventSequence *sequence);
gint                 cobiwm_gesture_tracker_get_n_current_touches (CobiwmGestureTracker *tracker);

#endif /* COBIWM_GESTURE_TRACKER_PRIVATE_H */
