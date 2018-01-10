/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2015 Red Hat
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
 *     Jonas Ådahl <jadahl@gmail.com>
 */

/**
 * SECTION:barrier-native
 * @Title: CobiwmBarrierImplNative
 * @Short_Description: Pointer barriers implementation for the native backend
 */

#include "config.h"

#include <stdlib.h>

#include <barrier.h>
#include <util.h>
#include "backends/cobiwm-backend-private.h"
#include "backends/cobiwm-barrier-private.h"
#include "backends/native/cobiwm-backend-native.h"
#include "backends/native/cobiwm-backend-native-private.h"
#include "backends/native/cobiwm-barrier-native.h"

struct _CobiwmBarrierManagerNative
{
  GHashTable *barriers;
};

typedef enum {
  /* The barrier is active and responsive to pointer motion. */
  COBIWM_BARRIER_STATE_ACTIVE,

  /* An intermediate state after a pointer hit the pointer barrier. */
  COBIWM_BARRIER_STATE_HIT,

  /* The barrier was hit by a pointer and is still within the hit box and
   * has not been released.*/
  COBIWM_BARRIER_STATE_HELD,

  /* The pointer was released by the user. If the following motion hits
   * the barrier, it will pass through. */
  COBIWM_BARRIER_STATE_RELEASE,

  /* An intermediate state when the pointer has left the barrier. */
  COBIWM_BARRIER_STATE_LEFT,
} CobiwmBarrierState;

struct _CobiwmBarrierImplNativePrivate
{
  CobiwmBarrier              *barrier;
  CobiwmBarrierManagerNative *manager;

  gboolean                  is_active;
  CobiwmBarrierState          state;
  int                       trigger_serial;
  guint32                   last_event_time;
  CobiwmBarrierDirection      blocked_dir;
};

G_DEFINE_TYPE_WITH_PRIVATE (CobiwmBarrierImplNative, cobiwm_barrier_impl_native,
                            COBIWM_TYPE_BARRIER_IMPL)

static int
next_serial (void)
{
  static int barrier_serial = 1;

  barrier_serial++;

  /* If it wraps, avoid 0 as it's not a valid serial. */
  if (barrier_serial == 0)
    barrier_serial++;

  return barrier_serial;
}

static gboolean
is_barrier_horizontal (CobiwmBarrier *barrier)
{
  return cobiwm_border_is_horizontal (&barrier->priv->border);
}

static gboolean
is_barrier_blocking_directions (CobiwmBarrier         *barrier,
                                CobiwmBarrierDirection directions)
{
  return cobiwm_border_is_blocking_directions (&barrier->priv->border,
                                             directions);
}

static void
dismiss_pointer (CobiwmBarrierImplNative *self)
{
  CobiwmBarrierImplNativePrivate *priv =
    cobiwm_barrier_impl_native_get_instance_private (self);

  priv->state = COBIWM_BARRIER_STATE_LEFT;
}

/*
 * Calculate the hit box for a held motion. The hit box is a 2 px wide region
 * in the opposite direction of every direction the barrier blocks. The purpose
 * of this is to allow small movements without receiving a "left" signal. This
 * heuristic comes from the X.org pointer barrier implementation.
 */
static CobiwmLine2
calculate_barrier_hit_box (CobiwmBarrier *barrier)
{
  CobiwmLine2 hit_box = barrier->priv->border.line;

  if (is_barrier_horizontal (barrier))
    {
      if (is_barrier_blocking_directions (barrier,
                                          COBIWM_BARRIER_DIRECTION_POSITIVE_Y))
        hit_box.a.y -= 2.0f;
      if (is_barrier_blocking_directions (barrier,
                                          COBIWM_BARRIER_DIRECTION_NEGATIVE_Y))
        hit_box.b.y += 2.0f;
    }
  else
    {
      if (is_barrier_blocking_directions (barrier,
                                          COBIWM_BARRIER_DIRECTION_POSITIVE_X))
        hit_box.a.x -= 2.0f;
      if (is_barrier_blocking_directions (barrier,
                                          COBIWM_BARRIER_DIRECTION_NEGATIVE_X))
        hit_box.b.x += 2.0f;
    }

  return hit_box;
}

static gboolean
is_within_box (CobiwmLine2   box,
               CobiwmVector2 point)
{
  return (point.x >= box.a.x && point.x < box.b.x &&
          point.y >= box.a.y && point.y < box.b.y);
}

static void
maybe_release_barrier (gpointer key,
                       gpointer value,
                       gpointer user_data)
{
  CobiwmBarrierImplNative *self = key;
  CobiwmBarrierImplNativePrivate *priv =
    cobiwm_barrier_impl_native_get_instance_private (self);
  CobiwmBarrier *barrier = priv->barrier;
  CobiwmLine2 *motion = user_data;
  CobiwmLine2 hit_box;

  if (priv->state != COBIWM_BARRIER_STATE_HELD)
    return;

  /* Release if we end up outside barrier end points. */
  if (is_barrier_horizontal (barrier))
    {
      if (motion->b.x > MAX (barrier->priv->border.line.a.x,
                             barrier->priv->border.line.b.x) ||
          motion->b.x < MIN (barrier->priv->border.line.a.x,
                             barrier->priv->border.line.b.x))
        {
          dismiss_pointer (self);
          return;
        }
    }
  else
    {
      if (motion->b.y > MAX (barrier->priv->border.line.a.y,
                             barrier->priv->border.line.b.y) ||
          motion->b.y < MIN (barrier->priv->border.line.a.y,
                             barrier->priv->border.line.b.y))
        {
          dismiss_pointer (self);
          return;
        }
    }

  /* Release if we don't intersect and end up outside of hit box. */
  hit_box = calculate_barrier_hit_box (barrier);
  if (!is_within_box (hit_box, motion->b))
    {
      dismiss_pointer (self);
      return;
    }
}

static void
maybe_release_barriers (CobiwmBarrierManagerNative *manager,
                        float                     prev_x,
                        float                     prev_y,
                        float                     x,
                        float                     y)
{
  CobiwmLine2 motion = {
    .a = {
      .x = prev_x,
      .y = prev_y,
    },
    .b = {
      .x = x,
      .y = y,
    },
  };

  g_hash_table_foreach (manager->barriers,
                        maybe_release_barrier,
                        &motion);
}

typedef struct _CobiwmClosestBarrierData
{
  struct
  {
    CobiwmLine2                   motion;
    CobiwmBarrierDirection        directions;
  } in;

  struct
  {
    float                       closest_distance_2;
    CobiwmBarrierImplNative      *barrier_impl;
  } out;
} CobiwmClosestBarrierData;

static void
update_closest_barrier (gpointer key,
                        gpointer value,
                        gpointer user_data)
{
  CobiwmBarrierImplNative *self = key;
  CobiwmBarrierImplNativePrivate *priv =
    cobiwm_barrier_impl_native_get_instance_private (self);
  CobiwmBarrier *barrier = priv->barrier;
  CobiwmClosestBarrierData *data = user_data;
  CobiwmVector2 intersection;
  float dx, dy;
  float distance_2;

  /* Ignore if the barrier is not blocking in any of the motions directions. */
  if (!is_barrier_blocking_directions (barrier, data->in.directions))
    return;

  /* Ignore if the barrier released the pointer. */
  if (priv->state == COBIWM_BARRIER_STATE_RELEASE)
    return;

  /* Ignore if we are moving away from barrier. */
  if (priv->state == COBIWM_BARRIER_STATE_HELD &&
      (data->in.directions & priv->blocked_dir) == 0)
    return;

  /* Check if the motion intersects with the barrier, and retrieve the
   * intersection point if any. */
  if (!cobiwm_line2_intersects_with (&barrier->priv->border.line,
                                   &data->in.motion,
                                   &intersection))
    return;

  /* Calculate the distance to the barrier and keep track of the closest
   * barrier. */
  dx = intersection.x - data->in.motion.a.x;
  dy = intersection.y - data->in.motion.a.y;
  distance_2 = dx*dx + dy*dy;
  if (data->out.barrier_impl == NULL ||
      distance_2 < data->out.closest_distance_2)
    {
      data->out.barrier_impl = self;
      data->out.closest_distance_2 = distance_2;
    }
}

static gboolean
get_closest_barrier (CobiwmBarrierManagerNative *manager,
                     float                     prev_x,
                     float                     prev_y,
                     float                     x,
                     float                     y,
                     CobiwmBarrierDirection      motion_dir,
                     CobiwmBarrierImplNative   **barrier_impl)
{
  CobiwmClosestBarrierData closest_barrier_data;

  closest_barrier_data = (CobiwmClosestBarrierData) {
    .in = {
      .motion = {
        .a = {
          .x = prev_x,
          .y = prev_y,
        },
        .b = {
          .x = x,
          .y = y,
        },
      },
      .directions = motion_dir,
    },
  };

  g_hash_table_foreach (manager->barriers,
                        update_closest_barrier,
                        &closest_barrier_data);

  if (closest_barrier_data.out.barrier_impl != NULL)
    {
      *barrier_impl = closest_barrier_data.out.barrier_impl;
      return TRUE;
    }
  else
    {
      return FALSE;
    }
}

typedef struct _CobiwmBarrierEventData
{
  guint32             time;
  float               prev_x;
  float               prev_y;
  float               x;
  float               y;
  float               dx;
  float               dy;
} CobiwmBarrierEventData;

static void
emit_barrier_event (CobiwmBarrierImplNative *self,
                    guint32                time,
                    float                  prev_x,
                    float                  prev_y,
                    float                  x,
                    float                  y,
                    float                  dx,
                    float                  dy)
{
  CobiwmBarrierImplNativePrivate *priv =
    cobiwm_barrier_impl_native_get_instance_private (self);
  CobiwmBarrier *barrier = priv->barrier;
  CobiwmBarrierEvent *event = g_slice_new0 (CobiwmBarrierEvent);
  CobiwmBarrierState old_state = priv->state;

  switch (priv->state)
    {
    case COBIWM_BARRIER_STATE_HIT:
      priv->state = COBIWM_BARRIER_STATE_HELD;
      priv->trigger_serial = next_serial ();
      event->dt = 0;

      break;
    case COBIWM_BARRIER_STATE_RELEASE:
    case COBIWM_BARRIER_STATE_LEFT:
      priv->state = COBIWM_BARRIER_STATE_ACTIVE;

      /* Intentional fall-through. */
    case COBIWM_BARRIER_STATE_HELD:
      event->dt = time - priv->last_event_time;

      break;
    case COBIWM_BARRIER_STATE_ACTIVE:
      g_assert_not_reached (); /* Invalid state. */
    }

  event->ref_count = 1;
  event->event_id = priv->trigger_serial;
  event->time = time;

  event->x = x;
  event->y = y;
  event->dx = dx;
  event->dy = dy;

  event->grabbed = priv->state == COBIWM_BARRIER_STATE_HELD;
  event->released = old_state == COBIWM_BARRIER_STATE_RELEASE;

  priv->last_event_time = time;

  if (priv->state == COBIWM_BARRIER_STATE_HELD)
    _cobiwm_barrier_emit_hit_signal (barrier, event);
  else
    _cobiwm_barrier_emit_left_signal (barrier, event);

  cobiwm_barrier_event_unref (event);
}

static void
maybe_emit_barrier_event (gpointer key, gpointer value, gpointer user_data)
{
  CobiwmBarrierImplNative *self = key;
  CobiwmBarrierImplNativePrivate *priv =
    cobiwm_barrier_impl_native_get_instance_private (self);
  CobiwmBarrierEventData *data = user_data;

  switch (priv->state) {
    case COBIWM_BARRIER_STATE_ACTIVE:
      break;
    case COBIWM_BARRIER_STATE_HIT:
    case COBIWM_BARRIER_STATE_HELD:
    case COBIWM_BARRIER_STATE_RELEASE:
    case COBIWM_BARRIER_STATE_LEFT:
      emit_barrier_event (self,
                          data->time,
                          data->prev_x,
                          data->prev_y,
                          data->x,
                          data->y,
                          data->dx,
                          data->dy);
      break;
    }
}

/* Clamp (x, y) to the barrier and remove clamped direction from motion_dir. */
static void
clamp_to_barrier (CobiwmBarrierImplNative *self,
                  CobiwmBarrierDirection *motion_dir,
                  float *x,
                  float *y)
{
  CobiwmBarrierImplNativePrivate *priv =
    cobiwm_barrier_impl_native_get_instance_private (self);
  CobiwmBarrier *barrier = priv->barrier;

  if (is_barrier_horizontal (barrier))
    {
      if (*motion_dir & COBIWM_BARRIER_DIRECTION_POSITIVE_Y)
        *y = barrier->priv->border.line.a.y;
      else if (*motion_dir & COBIWM_BARRIER_DIRECTION_NEGATIVE_Y)
        *y = barrier->priv->border.line.a.y;

      priv->blocked_dir = *motion_dir & (COBIWM_BARRIER_DIRECTION_POSITIVE_Y |
                                         COBIWM_BARRIER_DIRECTION_NEGATIVE_Y);
      *motion_dir &= ~(COBIWM_BARRIER_DIRECTION_POSITIVE_Y |
                       COBIWM_BARRIER_DIRECTION_NEGATIVE_Y);
    }
  else
    {
      if (*motion_dir & COBIWM_BARRIER_DIRECTION_POSITIVE_X)
        *x = barrier->priv->border.line.a.x;
      else if (*motion_dir & COBIWM_BARRIER_DIRECTION_NEGATIVE_X)
        *x = barrier->priv->border.line.a.x;

      priv->blocked_dir = *motion_dir & (COBIWM_BARRIER_DIRECTION_POSITIVE_X |
                                         COBIWM_BARRIER_DIRECTION_NEGATIVE_X);
      *motion_dir &= ~(COBIWM_BARRIER_DIRECTION_POSITIVE_X |
                       COBIWM_BARRIER_DIRECTION_NEGATIVE_X);
    }

  priv->state = COBIWM_BARRIER_STATE_HIT;
}

void
cobiwm_barrier_manager_native_process (CobiwmBarrierManagerNative *manager,
                                     ClutterInputDevice       *device,
                                     guint32                   time,
                                     float                    *x,
                                     float                    *y)
{
  ClutterPoint prev_pos;
  float prev_x;
  float prev_y;
  float orig_x = *x;
  float orig_y = *y;
  CobiwmBarrierDirection motion_dir = 0;
  CobiwmBarrierEventData barrier_event_data;
  CobiwmBarrierImplNative *barrier_impl;

  if (!clutter_input_device_get_coords (device, NULL, &prev_pos))
    return;

  prev_x = prev_pos.x;
  prev_y = prev_pos.y;

  /* Get the direction of the motion vector. */
  if (prev_x < *x)
    motion_dir |= COBIWM_BARRIER_DIRECTION_POSITIVE_X;
  else if (prev_x > *x)
    motion_dir |= COBIWM_BARRIER_DIRECTION_NEGATIVE_X;
  if (prev_y < *y)
    motion_dir |= COBIWM_BARRIER_DIRECTION_POSITIVE_Y;
  else if (prev_y > *y)
    motion_dir |= COBIWM_BARRIER_DIRECTION_NEGATIVE_Y;

  /* Clamp to the closest barrier in any direction until either there are no
   * more barriers to clamp to or all directions have been clamped. */
  while (motion_dir != 0)
    {
      if (get_closest_barrier (manager,
                               prev_x, prev_y,
                               *x, *y,
                               motion_dir,
                               &barrier_impl))
        clamp_to_barrier (barrier_impl, &motion_dir, x, y);
      else
        break;
    }

  /* Potentially release active barrier movements. */
  maybe_release_barriers (manager, prev_x, prev_y, *x, *y);

  /* Initiate or continue barrier interaction. */
  barrier_event_data = (CobiwmBarrierEventData) {
    .time = time,
    .prev_x = prev_x,
    .prev_y = prev_y,
    .x = *x,
    .y = *y,
    .dx = orig_x - prev_x,
    .dy = orig_y - prev_y,
  };

  g_hash_table_foreach (manager->barriers,
                        maybe_emit_barrier_event,
                        &barrier_event_data);
}

static gboolean
_cobiwm_barrier_impl_native_is_active (CobiwmBarrierImpl *impl)
{
  CobiwmBarrierImplNative *self = COBIWM_BARRIER_IMPL_NATIVE (impl);
  CobiwmBarrierImplNativePrivate *priv =
    cobiwm_barrier_impl_native_get_instance_private (self);

  return priv->is_active;
}

static void
_cobiwm_barrier_impl_native_release (CobiwmBarrierImpl  *impl,
                                   CobiwmBarrierEvent *event)
{
  CobiwmBarrierImplNative *self = COBIWM_BARRIER_IMPL_NATIVE (impl);
  CobiwmBarrierImplNativePrivate *priv =
    cobiwm_barrier_impl_native_get_instance_private (self);

  if (priv->state == COBIWM_BARRIER_STATE_HELD &&
      event->event_id == priv->trigger_serial)
    priv->state = COBIWM_BARRIER_STATE_RELEASE;
}

static void
_cobiwm_barrier_impl_native_destroy (CobiwmBarrierImpl *impl)
{
  CobiwmBarrierImplNative *self = COBIWM_BARRIER_IMPL_NATIVE (impl);
  CobiwmBarrierImplNativePrivate *priv =
    cobiwm_barrier_impl_native_get_instance_private (self);

  g_hash_table_remove (priv->manager->barriers, self);
  priv->is_active = FALSE;
}

CobiwmBarrierImpl *
cobiwm_barrier_impl_native_new (CobiwmBarrier *barrier)
{
  CobiwmBarrierImplNative *self;
  CobiwmBarrierImplNativePrivate *priv;
  CobiwmBackendNative *native;
  CobiwmBarrierManagerNative *manager;

  self = g_object_new (COBIWM_TYPE_BARRIER_IMPL_NATIVE, NULL);
  priv = cobiwm_barrier_impl_native_get_instance_private (self);

  priv->barrier = barrier;
  priv->is_active = TRUE;

  native = COBIWM_BACKEND_NATIVE (cobiwm_get_backend ());
  manager = cobiwm_backend_native_get_barrier_manager (native);
  priv->manager = manager;
  g_hash_table_add (manager->barriers, self);

  return COBIWM_BARRIER_IMPL (self);
}

static void
cobiwm_barrier_impl_native_class_init (CobiwmBarrierImplNativeClass *klass)
{
  CobiwmBarrierImplClass *impl_class = COBIWM_BARRIER_IMPL_CLASS (klass);

  impl_class->is_active = _cobiwm_barrier_impl_native_is_active;
  impl_class->release = _cobiwm_barrier_impl_native_release;
  impl_class->destroy = _cobiwm_barrier_impl_native_destroy;
}

static void
cobiwm_barrier_impl_native_init (CobiwmBarrierImplNative *self)
{
}

CobiwmBarrierManagerNative *
cobiwm_barrier_manager_native_new (void)
{
  CobiwmBarrierManagerNative *manager;

  manager = g_new0 (CobiwmBarrierManagerNative, 1);

  manager->barriers = g_hash_table_new (NULL, NULL);

  return manager;
}
