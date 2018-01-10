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

#include "config.h"

#include "cobiwm-wayland-pointer-constraints.h"

#include <glib.h>

#include "cobiwm-backend.h"
#include "cobiwm-wayland-private.h"
#include "cobiwm-wayland-seat.h"
#include "cobiwm-wayland-pointer.h"
#include "cobiwm-wayland-surface.h"
#include "cobiwm-wayland-region.h"
#include "cobiwm-pointer-lock-wayland.h"
#include "cobiwm-pointer-confinement-wayland.h"
#include "window-private.h"
#include "backends/cobiwm-backend-private.h"
#include "backends/native/cobiwm-backend-native.h"
#include "backends/cobiwm-pointer-constraint.h"

#include "pointer-constraints-unstable-v1-server-protocol.h"

static GQuark quark_pending_constraint_state = 0;
static GQuark quark_surface_pointer_constraints_data = 0;

struct _CobiwmWaylandPointerConstraint
{
  GObject parent;

  CobiwmWaylandSurface *surface;
  gboolean is_enabled;
  cairo_region_t *region;
  struct wl_resource *resource;
  CobiwmWaylandPointerGrab grab;
  CobiwmWaylandSeat *seat;
  enum zwp_pointer_constraints_v1_lifetime lifetime;

  gboolean hint_set;
  wl_fixed_t x_hint;
  wl_fixed_t y_hint;

  CobiwmPointerConstraint *constraint;
};

typedef struct _CobiwmWaylandSurfacePointerConstraintsData
{
  GList *pointer_constraints;
  CobiwmWindow *window;
  gulong appears_changed_handler_id;
  gulong raised_handler_id;
} CobiwmWaylandSurfacePointerConstraintsData;

typedef struct
{
  CobiwmWaylandPointerConstraint *constraint;
  cairo_region_t *region;
  gulong applied_handler_id;
} CobiwmWaylandPendingConstraintState;

typedef struct
{
  GList *pending_constraint_states;
} CobiwmWaylandPendingConstraintStateContainer;

G_DEFINE_TYPE (CobiwmWaylandPointerConstraint, cobiwm_wayland_pointer_constraint,
               G_TYPE_OBJECT);

static const struct zwp_locked_pointer_v1_interface locked_pointer_interface;
static const struct zwp_confined_pointer_v1_interface confined_pointer_interface;
static const CobiwmWaylandPointerGrabInterface locked_pointer_grab_interface;
static const CobiwmWaylandPointerGrabInterface confined_pointer_grab_interface;

static void
cobiwm_wayland_pointer_constraint_destroy (CobiwmWaylandPointerConstraint *constraint);

static void
cobiwm_wayland_pointer_constraint_maybe_enable_for_window (CobiwmWindow *window);

static void
cobiwm_wayland_pointer_constraint_maybe_remove_for_seat (CobiwmWaylandSeat *seat,
                                                       CobiwmWindow      *window);

static CobiwmWaylandSurfacePointerConstraintsData *
get_surface_constraints_data (CobiwmWaylandSurface *surface)
{
  return g_object_get_qdata (G_OBJECT (surface),
                             quark_surface_pointer_constraints_data);
}

static void
appears_focused_changed (CobiwmWindow *window,
                         GParamSpec *pspec,
                         gpointer    user_data)
{
  CobiwmWaylandCompositor *wayland_compositor;

  wayland_compositor = cobiwm_wayland_compositor_get_default ();
  cobiwm_wayland_pointer_constraint_maybe_remove_for_seat (wayland_compositor->seat,
                                                         window);

  if (window->unmanaging)
    return;

  cobiwm_wayland_pointer_constraint_maybe_enable_for_window (window);
}

static void
window_raised (CobiwmWindow *window)
{
  cobiwm_wayland_pointer_constraint_maybe_enable_for_window (window);
}

static CobiwmWaylandSurfacePointerConstraintsData *
surface_constraint_data_new (CobiwmWaylandSurface *surface)
{
  CobiwmWaylandSurfacePointerConstraintsData *data;

  data = g_new0 (CobiwmWaylandSurfacePointerConstraintsData, 1);

  if (surface->window)
    {
      data->window = surface->window;
      g_object_add_weak_pointer (G_OBJECT (data->window),
                                 (gpointer *) &data->window);
      data->appears_changed_handler_id =
        g_signal_connect (data->window, "notify::appears-focused",
                          G_CALLBACK (appears_focused_changed), NULL);
      data->raised_handler_id =
        g_signal_connect (data->window, "raised",
                          G_CALLBACK (window_raised), NULL);
    }
  else
    {
      /* TODO: Support constraints on non-toplevel windows, such as subsurfaces.
       */
      g_warn_if_reached ();
    }

  return data;
}
static void
surface_constraint_data_free (CobiwmWaylandSurfacePointerConstraintsData *data)
{
  if (data->window)
    {
      g_signal_handler_disconnect (data->window,
                                   data->appears_changed_handler_id);
      g_signal_handler_disconnect (data->window,
                                   data->raised_handler_id);
      g_object_remove_weak_pointer (G_OBJECT (data->window),
                                    (gpointer *) &data->window);
    }

  g_list_free_full (data->pointer_constraints,
                    (GDestroyNotify) cobiwm_wayland_pointer_constraint_destroy);
  g_free (data);
}

static CobiwmWaylandSurfacePointerConstraintsData *
ensure_surface_constraints_data (CobiwmWaylandSurface *surface)
{
  CobiwmWaylandSurfacePointerConstraintsData *data;

  data = get_surface_constraints_data (surface);
  if (!data)
    {
      data = surface_constraint_data_new (surface);
      g_object_set_qdata_full (G_OBJECT (surface),
                               quark_surface_pointer_constraints_data,
                               data,
                               (GDestroyNotify) surface_constraint_data_free);
    }

  return data;
}

static void
surface_add_pointer_constraint (CobiwmWaylandSurface           *surface,
                                CobiwmWaylandPointerConstraint *constraint)
{
  CobiwmWaylandSurfacePointerConstraintsData *data;

  data = ensure_surface_constraints_data (surface);
  data->pointer_constraints = g_list_append (data->pointer_constraints,
                                             constraint);
}

static void
surface_remove_pointer_constraints (CobiwmWaylandSurface           *surface,
                                    CobiwmWaylandPointerConstraint *constraint)
{
  CobiwmWaylandSurfacePointerConstraintsData *data;

  data = get_surface_constraints_data (surface);
  data->pointer_constraints =
    g_list_remove (data->pointer_constraints, constraint);

  if (!data->pointer_constraints)
    {
      g_object_set_qdata (G_OBJECT (surface),
                          quark_surface_pointer_constraints_data,
                          NULL);
    }
}

static CobiwmWaylandPointerConstraint *
cobiwm_wayland_pointer_constraint_new (CobiwmWaylandSurface                      *surface,
                                     CobiwmWaylandSeat                         *seat,
                                     CobiwmWaylandRegion                       *region,
                                     enum zwp_pointer_constraints_v1_lifetime lifetime,
                                     struct wl_resource                      *resource,
                                     const CobiwmWaylandPointerGrabInterface   *grab_interface)
{
  CobiwmWaylandPointerConstraint *constraint;

  constraint = g_object_new (COBIWM_TYPE_WAYLAND_POINTER_CONSTRAINT, NULL);
  if (!constraint)
    return NULL;

  constraint->surface = surface;
  constraint->seat = seat;
  constraint->lifetime = lifetime;
  constraint->resource = resource;
  constraint->grab.interface = grab_interface;

  if (region)
    {
      constraint->region =
        cairo_region_copy (cobiwm_wayland_region_peek_cairo_region (region));
    }
  else
    {
      constraint->region = NULL;
    }

  return constraint;
}

static gboolean
cobiwm_wayland_pointer_constraint_is_enabled (CobiwmWaylandPointerConstraint *constraint)
{
  return constraint->is_enabled;
}

static void
cobiwm_wayland_pointer_constraint_notify_activated (CobiwmWaylandPointerConstraint *constraint)
{
  struct wl_resource *resource = constraint->resource;

  if (wl_resource_instance_of (resource,
                               &zwp_locked_pointer_v1_interface,
                               &locked_pointer_interface))
    {
      zwp_locked_pointer_v1_send_locked (resource);
    }
  else if (wl_resource_instance_of (resource,
                                    &zwp_confined_pointer_v1_interface,
                                    &confined_pointer_interface))
    {
      zwp_confined_pointer_v1_send_confined (resource);
    }
}

static void
cobiwm_wayland_pointer_constraint_notify_deactivated (CobiwmWaylandPointerConstraint *constraint)
{
  struct wl_resource *resource = constraint->resource;

  if (wl_resource_instance_of (resource,
                               &zwp_locked_pointer_v1_interface,
                               &locked_pointer_interface))
    zwp_locked_pointer_v1_send_unlocked (resource);
  else if (wl_resource_instance_of (resource,
                                    &zwp_confined_pointer_v1_interface,
                                    &confined_pointer_interface))
    zwp_confined_pointer_v1_send_unconfined (resource);
}

static CobiwmPointerConstraint *
cobiwm_wayland_pointer_constraint_create_pointer_constraint (CobiwmWaylandPointerConstraint *constraint)
{
  struct wl_resource *resource = constraint->resource;

  if (wl_resource_instance_of (resource,
                               &zwp_locked_pointer_v1_interface,
                               &locked_pointer_interface))
    {
      return cobiwm_pointer_lock_wayland_new ();
    }
  else if (wl_resource_instance_of (resource,
                                    &zwp_confined_pointer_v1_interface,
                                    &confined_pointer_interface))
    {
      return cobiwm_pointer_confinement_wayland_new (constraint);
    }
  g_assert_not_reached ();
  return NULL;
}

static void
cobiwm_wayland_pointer_constraint_enable (CobiwmWaylandPointerConstraint *constraint)
{
  CobiwmBackend *backend = cobiwm_get_backend ();

  g_assert (!constraint->is_enabled);

  constraint->is_enabled = TRUE;
  cobiwm_wayland_pointer_constraint_notify_activated (constraint);
  cobiwm_wayland_pointer_start_grab (&constraint->seat->pointer,
                                   &constraint->grab);

  constraint->constraint =
    cobiwm_wayland_pointer_constraint_create_pointer_constraint (constraint);
  cobiwm_backend_set_client_pointer_constraint (backend, constraint->constraint);
  g_object_add_weak_pointer (G_OBJECT (constraint->constraint),
                             (gpointer *) &constraint->constraint);
  g_object_unref (constraint->constraint);
}

static void
cobiwm_wayland_pointer_constraint_disable (CobiwmWaylandPointerConstraint *constraint)
{
  constraint->is_enabled = FALSE;
  cobiwm_wayland_pointer_constraint_notify_deactivated (constraint);
  cobiwm_wayland_pointer_end_grab (constraint->grab.pointer);
  cobiwm_backend_set_client_pointer_constraint (cobiwm_get_backend (), NULL);
}

void
cobiwm_wayland_pointer_constraint_destroy (CobiwmWaylandPointerConstraint *constraint)
{
  if (cobiwm_wayland_pointer_constraint_is_enabled (constraint))
    cobiwm_wayland_pointer_constraint_disable (constraint);

  wl_resource_set_user_data (constraint->resource, NULL);
  g_clear_pointer (&constraint->region, cairo_region_destroy);
  g_object_unref (constraint);
}

static gboolean
is_within_constraint_region (CobiwmWaylandPointerConstraint *constraint,
                             wl_fixed_t                    sx,
                             wl_fixed_t                    sy)
{
  cairo_region_t *region;
  gboolean is_within;

  region = cobiwm_wayland_pointer_constraint_calculate_effective_region (constraint);
  is_within = cairo_region_contains_point (region,
                                           wl_fixed_to_int (sx),
                                           wl_fixed_to_int (sy));
  cairo_region_destroy (region);

  return is_within;
}

static void
cobiwm_wayland_pointer_constraint_maybe_enable (CobiwmWaylandPointerConstraint *constraint)
{
  wl_fixed_t sx, sy;

  if (constraint->is_enabled)
    return;

  if (!constraint->surface->window)
    {
      g_warn_if_reached ();
      return;
    }

  if (!cobiwm_window_appears_focused (constraint->surface->window))
    return;

  cobiwm_wayland_pointer_get_relative_coordinates (&constraint->seat->pointer,
                                                 constraint->surface,
                                                 &sx, &sy);
  if (!is_within_constraint_region (constraint, sx, sy))
    return;

  cobiwm_wayland_pointer_constraint_enable (constraint);
}

static void
cobiwm_wayland_pointer_constraint_remove (CobiwmWaylandPointerConstraint *constraint)
{
  CobiwmWaylandSurface *surface = constraint->surface;

  surface_remove_pointer_constraints (surface, constraint);
  cobiwm_wayland_pointer_constraint_destroy (constraint);
}

void
cobiwm_wayland_pointer_constraint_maybe_remove_for_seat (CobiwmWaylandSeat *seat,
                                                       CobiwmWindow      *window)
{
  CobiwmWaylandPointer *pointer = &seat->pointer;
  CobiwmWaylandPointerConstraint *constraint;

  if ((pointer->grab->interface != &confined_pointer_grab_interface &&
       pointer->grab->interface != &locked_pointer_grab_interface))
    return;

  constraint = wl_container_of (pointer->grab, constraint, grab);

  if (constraint->surface != window->surface)
    return;

  if (cobiwm_window_appears_focused (window) &&
      pointer->focus_surface == window->surface)
    return;

  switch (constraint->lifetime)
    {
    case ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_ONESHOT:
      cobiwm_wayland_pointer_constraint_remove (constraint);
      break;

    case ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT:
      cobiwm_wayland_pointer_constraint_disable (constraint);
      break;

    default:
      g_assert_not_reached ();
    }
}

static void
cobiwm_wayland_pointer_constraint_maybe_enable_for_window (CobiwmWindow *window)
{
  CobiwmWaylandSurface *surface = window->surface;
  CobiwmWaylandSurfacePointerConstraintsData *surface_data;
  GList *l;

  surface_data = get_surface_constraints_data (surface);
  if (!surface_data)
    return;

  for (l = surface_data->pointer_constraints; l; l = l->next)
    {
      CobiwmWaylandPointerConstraint *constraint = l->data;

      cobiwm_wayland_pointer_constraint_maybe_enable (constraint);
    }
}

CobiwmWaylandSeat *
cobiwm_wayland_pointer_constraint_get_seat (CobiwmWaylandPointerConstraint *constraint)
{
  return constraint->seat;
}

cairo_region_t *
cobiwm_wayland_pointer_constraint_calculate_effective_region (CobiwmWaylandPointerConstraint *constraint)
{
  cairo_region_t *region;

  region = cobiwm_wayland_surface_calculate_input_region (constraint->surface);
  if (constraint->region)
    cairo_region_intersect (region, constraint->region);

  return region;
}

CobiwmWaylandSurface *
cobiwm_wayland_pointer_constraint_get_surface (CobiwmWaylandPointerConstraint *constraint)
{
  return constraint->surface;
}

static void
pointer_constraint_resource_destroyed (struct wl_resource *resource)
{
  CobiwmWaylandPointerConstraint *constraint =
    wl_resource_get_user_data (resource);

  if (!constraint)
    return;

  cobiwm_wayland_pointer_constraint_remove (constraint);
}

static void
pending_constraint_state_free (CobiwmWaylandPendingConstraintState *constraint_pending)
{
  g_clear_pointer (&constraint_pending->region, cairo_region_destroy);
  if (constraint_pending->constraint)
    g_object_remove_weak_pointer (G_OBJECT (constraint_pending->constraint),
                                  (gpointer *) &constraint_pending->constraint);
}

static CobiwmWaylandPendingConstraintStateContainer *
get_pending_constraint_state_container (CobiwmWaylandPendingState *pending)
{
  return g_object_get_qdata (G_OBJECT (pending),
                             quark_pending_constraint_state);
}

static CobiwmWaylandPendingConstraintState *
get_pending_constraint_state (CobiwmWaylandPointerConstraint *constraint)
{
  CobiwmWaylandPendingState *pending = constraint->surface->pending;
  CobiwmWaylandPendingConstraintStateContainer *container;
  GList *l;

  container = get_pending_constraint_state_container (pending);
  for (l = container->pending_constraint_states; l; l = l->next)
    {
      CobiwmWaylandPendingConstraintState *constraint_pending = l->data;

      if (constraint_pending->constraint == constraint)
        return constraint_pending;
    }

  return NULL;
}

static void
pending_constraint_state_container_free (CobiwmWaylandPendingConstraintStateContainer *container)
{
  g_list_free_full (container->pending_constraint_states,
                    (GDestroyNotify) pending_constraint_state_free);
  g_free (container);
}

static CobiwmWaylandPendingConstraintStateContainer *
ensure_pending_constraint_state_container (CobiwmWaylandPendingState *pending)
{
  CobiwmWaylandPendingConstraintStateContainer *container;

  container = get_pending_constraint_state_container (pending);
  if (!container)
    {
      container = g_new0 (CobiwmWaylandPendingConstraintStateContainer, 1);
      g_object_set_qdata_full (G_OBJECT (pending),
                               quark_pending_constraint_state,
                               container,
                               (GDestroyNotify) pending_constraint_state_container_free);

    }

  return container;
}

static void
remove_pending_constraint_state (CobiwmWaylandPointerConstraint *constraint,
                                 CobiwmWaylandPendingState      *pending)
{
  CobiwmWaylandPendingConstraintStateContainer *container;
  GList *l;

  container = get_pending_constraint_state_container (pending);
  for (l = container->pending_constraint_states; l; l = l->next)
    {
      CobiwmWaylandPendingConstraintState *constraint_pending = l->data;
      if (constraint_pending->constraint != constraint)
        continue;

      pending_constraint_state_free (l->data);
      container->pending_constraint_states =
        g_list_remove_link (container->pending_constraint_states, l);
      break;
    }
}

static void
pending_constraint_state_applied (CobiwmWaylandPendingState           *pending,
                                  CobiwmWaylandPendingConstraintState *constraint_pending)
{
  CobiwmWaylandPointerConstraint *constraint = constraint_pending->constraint;

  if (!constraint)
    return;

  g_clear_pointer (&constraint->region, cairo_region_destroy);
  if (constraint_pending->region)
    {
      constraint->region = constraint_pending->region;
      constraint_pending->region = NULL;
    }
  else
    {
      constraint->region = NULL;
    }

  g_signal_handler_disconnect (pending,
                               constraint_pending->applied_handler_id);
  remove_pending_constraint_state (constraint, pending);

  /* The pointer is potentially warped by the actor paint signal callback if
   * the new region proved it necessary.
   */
}

static CobiwmWaylandPendingConstraintState *
ensure_pending_constraint_state (CobiwmWaylandPointerConstraint *constraint)
{
  CobiwmWaylandPendingState *pending = constraint->surface->pending;
  CobiwmWaylandPendingConstraintStateContainer *container;
  CobiwmWaylandPendingConstraintState *constraint_pending;

  container = ensure_pending_constraint_state_container (pending);
  constraint_pending = get_pending_constraint_state (constraint);
  if (!constraint_pending)
    {
      constraint_pending = g_new0 (CobiwmWaylandPendingConstraintState, 1);
      constraint_pending->constraint = constraint;
      constraint_pending->applied_handler_id =
        g_signal_connect (pending, "applied",
                          G_CALLBACK (pending_constraint_state_applied),
                          constraint_pending);
      g_object_add_weak_pointer (G_OBJECT (constraint),
                                 (gpointer *) &constraint_pending->constraint);

      container->pending_constraint_states =
        g_list_append (container->pending_constraint_states,
                       constraint_pending);
    }

  return constraint_pending;
}

static void
cobiwm_wayland_pointer_constraint_set_pending_region (CobiwmWaylandPointerConstraint *constraint,
                                                    CobiwmWaylandRegion            *region)
{
  CobiwmWaylandPendingConstraintState *constraint_pending;

  constraint_pending = ensure_pending_constraint_state (constraint);

  g_clear_pointer (&constraint_pending->region, cairo_region_destroy);
  if (region)
    {
      constraint_pending->region =
        cairo_region_copy (cobiwm_wayland_region_peek_cairo_region (region));
    }
}

static CobiwmWaylandPointerConstraint *
get_pointer_constraint_for_seat (CobiwmWaylandSurface *surface,
                                 CobiwmWaylandSeat    *seat)
{
  CobiwmWaylandSurfacePointerConstraintsData *surface_data;
  GList *l;

  surface_data = get_surface_constraints_data (surface);
  if (!surface_data)
    return NULL;

  for (l = surface_data->pointer_constraints; l; l = l->next)
    {
      CobiwmWaylandPointerConstraint *constraint = l->data;

      if (seat == constraint->seat)
        return constraint;
    }

  return NULL;
}

static void
init_pointer_constraint (struct wl_resource                      *resource,
                         uint32_t                                 id,
                         CobiwmWaylandSurface                      *surface,
                         CobiwmWaylandSeat                         *seat,
                         CobiwmWaylandRegion                       *region,
                         enum zwp_pointer_constraints_v1_lifetime lifetime,
                         const struct wl_interface               *interface,
                         const void                              *implementation,
                         const CobiwmWaylandPointerGrabInterface   *grab_interface)
{
  struct wl_client *client = wl_resource_get_client (resource);
  struct wl_resource *cr;
  CobiwmWaylandPointerConstraint *constraint;

  if (get_pointer_constraint_for_seat (surface, seat))
    {
      wl_resource_post_error (resource,
                              WL_DISPLAY_ERROR_INVALID_OBJECT,
                              "the pointer as already requested to be "
                              "locked or confined on that surface");
      return;
    }

  cr = wl_resource_create (client, interface,
                           wl_resource_get_version (resource),
                           id);
  if (cr == NULL)
    {
      wl_client_post_no_memory (client);
      return;
    }

  constraint = cobiwm_wayland_pointer_constraint_new (surface, seat,
                                                    region,
                                                    lifetime,
                                                    cr, grab_interface);
  if (constraint == NULL)
    {
      wl_client_post_no_memory (client);
      return;
    }

  surface_add_pointer_constraint (surface, constraint);

  wl_resource_set_implementation (cr, implementation, constraint,
                                  pointer_constraint_resource_destroyed);

  cobiwm_wayland_pointer_constraint_maybe_enable (constraint);
}

static void
locked_pointer_destroy (struct wl_client   *client,
                        struct wl_resource *resource)
{
  CobiwmWaylandPointerConstraint *constraint =
    wl_resource_get_user_data (resource);
  gboolean warp_pointer = FALSE;
  int warp_x, warp_y;

  if (constraint && constraint->is_enabled && constraint->hint_set &&
      is_within_constraint_region (constraint,
                                   constraint->x_hint,
                                   constraint->y_hint))
    {
      float sx, sy;
      float x, y;

      sx = (float)wl_fixed_to_double (constraint->x_hint);
      sy = (float)wl_fixed_to_double (constraint->y_hint);
      cobiwm_wayland_surface_get_absolute_coordinates (constraint->surface,
                                                     sx, sy,
                                                     &x, &y);
      warp_pointer = TRUE;
      warp_x = (int) x;
      warp_y = (int) y;
    }
  wl_resource_destroy (resource);

  if (warp_pointer)
    cobiwm_backend_warp_pointer (cobiwm_get_backend (), warp_x, warp_y);
}

static void
locked_pointer_set_cursor_position_hint (struct wl_client   *client,
                                         struct wl_resource *resource,
                                         wl_fixed_t          surface_x,
                                         wl_fixed_t          surface_y)
{
  CobiwmWaylandPointerConstraint *constraint =
    wl_resource_get_user_data (resource);

  /* Ignore a set cursor hint that was already sent after the constraint
   * was cancelled. */
  if (!constraint || !constraint->resource || constraint->resource != resource)
    return;

  constraint->hint_set = TRUE;
  constraint->x_hint = surface_x;
  constraint->y_hint = surface_y;
}

static void
locked_pointer_set_region (struct wl_client   *client,
                           struct wl_resource *resource,
                           struct wl_resource *region_resource)
{
  CobiwmWaylandPointerConstraint *constraint =
    wl_resource_get_user_data (resource);
  CobiwmWaylandRegion *region =
    region_resource ? wl_resource_get_user_data (region_resource) : NULL;

  if (!constraint)
    return;

  cobiwm_wayland_pointer_constraint_set_pending_region (constraint, region);
}

static const struct zwp_locked_pointer_v1_interface locked_pointer_interface = {
  locked_pointer_destroy,
  locked_pointer_set_cursor_position_hint,
  locked_pointer_set_region,
};

static void
locked_pointer_grab_pointer_focus (CobiwmWaylandPointerGrab *grab,
                                   CobiwmWaylandSurface     *surface)
{
}

static void
locked_pointer_grab_pointer_motion (CobiwmWaylandPointerGrab *grab,
                                    const ClutterEvent     *event)
{
  cobiwm_wayland_pointer_send_relative_motion (grab->pointer, event);
}

static void
locked_pointer_grab_pointer_button (CobiwmWaylandPointerGrab *grab,
                                    const ClutterEvent     *event)
{
  cobiwm_wayland_pointer_send_button (grab->pointer, event);
}

static const CobiwmWaylandPointerGrabInterface locked_pointer_grab_interface = {
  locked_pointer_grab_pointer_focus,
  locked_pointer_grab_pointer_motion,
  locked_pointer_grab_pointer_button,
};

static void
pointer_constraints_destroy (struct wl_client   *client,
                             struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
pointer_constraints_lock_pointer (struct wl_client   *client,
                                  struct wl_resource *resource,
                                  uint32_t            id,
                                  struct wl_resource *surface_resource,
                                  struct wl_resource *pointer_resource,
                                  struct wl_resource *region_resource,
                                  uint32_t            lifetime)
{
  CobiwmWaylandSurface *surface = wl_resource_get_user_data (surface_resource);
  CobiwmWaylandPointer *pointer = wl_resource_get_user_data (pointer_resource);
  CobiwmWaylandSeat *seat = cobiwm_wayland_pointer_get_seat (pointer);
  CobiwmWaylandRegion *region =
    region_resource ? wl_resource_get_user_data (region_resource) : NULL;

  init_pointer_constraint (resource, id, surface, seat, region, lifetime,
                           &zwp_locked_pointer_v1_interface,
                           &locked_pointer_interface,
                           &locked_pointer_grab_interface);
}

static void
confined_pointer_grab_pointer_focus (CobiwmWaylandPointerGrab *grab,
                                     CobiwmWaylandSurface *surface)
{
}

static void
confined_pointer_grab_pointer_motion (CobiwmWaylandPointerGrab *grab,
                                      const ClutterEvent     *event)
{
  CobiwmWaylandPointerConstraint *constraint =
    wl_container_of (grab, constraint, grab);
  CobiwmWaylandPointer *pointer = grab->pointer;

  g_assert (pointer->focus_surface);
  g_assert (pointer->focus_surface == constraint->surface);

  cobiwm_wayland_pointer_send_motion (pointer, event);
}

static void
confined_pointer_grab_pointer_button (CobiwmWaylandPointerGrab *grab,
                                      const ClutterEvent     *event)
{
  cobiwm_wayland_pointer_send_button (grab->pointer, event);
}

static const CobiwmWaylandPointerGrabInterface confined_pointer_grab_interface = {
  confined_pointer_grab_pointer_focus,
  confined_pointer_grab_pointer_motion,
  confined_pointer_grab_pointer_button,
};

static void
confined_pointer_destroy (struct wl_client   *client,
                          struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
confined_pointer_set_region (struct wl_client   *client,
                             struct wl_resource *resource,
                             struct wl_resource *region_resource)
{
  CobiwmWaylandPointerConstraint *constraint =
    wl_resource_get_user_data (resource);
  CobiwmWaylandRegion *region =
    region_resource ? wl_resource_get_user_data (region_resource) : NULL;

  if (!constraint)
    return;

  cobiwm_wayland_pointer_constraint_set_pending_region (constraint, region);
}

static const struct zwp_confined_pointer_v1_interface confined_pointer_interface = {
  confined_pointer_destroy,
  confined_pointer_set_region,
};

static void
pointer_constraints_confine_pointer (struct wl_client   *client,
                                     struct wl_resource *resource,
                                     uint32_t            id,
                                     struct wl_resource *surface_resource,
                                     struct wl_resource *pointer_resource,
                                     struct wl_resource *region_resource,
                                     uint32_t            lifetime)
{
  CobiwmWaylandSurface *surface = wl_resource_get_user_data (surface_resource);
  CobiwmWaylandPointer *pointer = wl_resource_get_user_data (pointer_resource);
  CobiwmWaylandSeat *seat = cobiwm_wayland_pointer_get_seat (pointer);
  CobiwmWaylandRegion *region =
    region_resource ? wl_resource_get_user_data (region_resource) : NULL;

  init_pointer_constraint (resource, id, surface, seat, region, lifetime,
                           &zwp_confined_pointer_v1_interface,
                           &confined_pointer_interface,
                           &confined_pointer_grab_interface);

}

static const struct zwp_pointer_constraints_v1_interface pointer_constraints = {
  pointer_constraints_destroy,
  pointer_constraints_lock_pointer,
  pointer_constraints_confine_pointer,
};

static void
bind_pointer_constraints (struct wl_client *client,
                          void             *data,
                          uint32_t          version,
                          uint32_t          id)
{
  CobiwmWaylandCompositor *compositor = data;
  struct wl_resource *resource;

  resource = wl_resource_create (client,
                                 &zwp_pointer_constraints_v1_interface,
                                 1, id);

  wl_resource_set_implementation (resource,
                                  &pointer_constraints,
                                  compositor,
                                  NULL);
}

void
cobiwm_wayland_pointer_constraints_init (CobiwmWaylandCompositor *compositor)
{
  if (!wl_global_create (compositor->wayland_display,
                         &zwp_pointer_constraints_v1_interface, 1,
                         compositor, bind_pointer_constraints))
    g_error ("Could not create wp_pointer_constraints global");
}

static void
cobiwm_wayland_pointer_constraint_init (CobiwmWaylandPointerConstraint *constraint)
{
}

static void
cobiwm_wayland_pointer_constraint_class_init (CobiwmWaylandPointerConstraintClass *klass)
{
  quark_pending_constraint_state =
    g_quark_from_static_string ("-cobiwm-wayland-pointer-constraint-pending_state");
  quark_surface_pointer_constraints_data =
    g_quark_from_static_string ("-cobiwm-wayland-surface-constraints-data");
}
