/*
 * Wayland Support
 *
 * Copyright (C) 2012,2013 Intel Corporation
 *               2013 Red Hat, Inc.
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
 */

#include "config.h"

#include "cobiwm-wayland-surface.h"

#include <clutter/clutter.h>
#include <clutter/wayland/clutter-wayland-compositor.h>
#include <clutter/wayland/clutter-wayland-surface.h>
#include <cogl/cogl-wayland-server.h>

#include <wayland-server.h>
#include "gtk-shell-server-protocol.h"
#include "xdg-shell-unstable-v5-server-protocol.h"

#include "cobiwm-wayland-private.h"
#include "cobiwm-xwayland-private.h"
#include "cobiwm-wayland-buffer.h"
#include "cobiwm-wayland-region.h"
#include "cobiwm-wayland-seat.h"
#include "cobiwm-wayland-keyboard.h"
#include "cobiwm-wayland-pointer.h"
#include "cobiwm-wayland-popup.h"
#include "cobiwm-wayland-data-device.h"
#include "cobiwm-wayland-outputs.h"

#include "cobiwm-cursor-tracker-private.h"
#include "display-private.h"
#include "window-private.h"
#include "cobiwm-window-wayland.h"
#include "bell.h"

#include "compositor/region-utils.h"

#include "cobiwm-surface-actor.h"
#include "cobiwm-surface-actor-wayland.h"
#include "cobiwm-xwayland-private.h"

enum {
  PENDING_STATE_SIGNAL_APPLIED,

  PENDING_STATE_SIGNAL_LAST_SIGNAL
};

static guint pending_state_signals[PENDING_STATE_SIGNAL_LAST_SIGNAL];

typedef struct _CobiwmWaylandSurfaceRolePrivate
{
  CobiwmWaylandSurface *surface;
} CobiwmWaylandSurfaceRolePrivate;

typedef enum
{
  COBIWM_WAYLAND_SUBSURFACE_PLACEMENT_ABOVE,
  COBIWM_WAYLAND_SUBSURFACE_PLACEMENT_BELOW
} CobiwmWaylandSubsurfacePlacement;

typedef struct
{
  CobiwmWaylandSubsurfacePlacement placement;
  CobiwmWaylandSurface *sibling;
  struct wl_listener sibling_destroy_listener;
} CobiwmWaylandSubsurfacePlacementOp;

G_DEFINE_TYPE (CobiwmWaylandSurface, cobiwm_wayland_surface, G_TYPE_OBJECT);

G_DEFINE_TYPE_WITH_PRIVATE (CobiwmWaylandSurfaceRole,
                            cobiwm_wayland_surface_role,
                            G_TYPE_OBJECT);

G_DEFINE_TYPE (CobiwmWaylandPendingState,
               cobiwm_wayland_pending_state,
               G_TYPE_OBJECT);

struct _CobiwmWaylandSurfaceRoleSubsurface
{
  CobiwmWaylandSurfaceRole parent;
};

G_DEFINE_TYPE (CobiwmWaylandSurfaceRoleSubsurface,
               cobiwm_wayland_surface_role_subsurface,
               COBIWM_TYPE_WAYLAND_SURFACE_ROLE);

struct _CobiwmWaylandSurfaceRoleXdgSurface
{
  CobiwmWaylandSurfaceRole parent;
};

G_DEFINE_TYPE (CobiwmWaylandSurfaceRoleXdgSurface,
               cobiwm_wayland_surface_role_xdg_surface,
               COBIWM_TYPE_WAYLAND_SURFACE_ROLE);

struct _CobiwmWaylandSurfaceRoleXdgPopup
{
  CobiwmWaylandSurfaceRole parent;
};

G_DEFINE_TYPE (CobiwmWaylandSurfaceRoleXdgPopup,
               cobiwm_wayland_surface_role_xdg_popup,
               COBIWM_TYPE_WAYLAND_SURFACE_ROLE);

struct _CobiwmWaylandSurfaceRoleWlShellSurface
{
  CobiwmWaylandSurfaceRole parent;
};

G_DEFINE_TYPE (CobiwmWaylandSurfaceRoleWlShellSurface,
               cobiwm_wayland_surface_role_wl_shell_surface,
               COBIWM_TYPE_WAYLAND_SURFACE_ROLE);

struct _CobiwmWaylandSurfaceRoleDND
{
  CobiwmWaylandSurfaceRole parent;
};

G_DEFINE_TYPE (CobiwmWaylandSurfaceRoleDND,
               cobiwm_wayland_surface_role_dnd,
               COBIWM_TYPE_WAYLAND_SURFACE_ROLE);

static void
cobiwm_wayland_surface_role_assigned (CobiwmWaylandSurfaceRole *surface_role);

static void
cobiwm_wayland_surface_role_pre_commit (CobiwmWaylandSurfaceRole  *surface_role,
                                      CobiwmWaylandPendingState *pending);

static void
cobiwm_wayland_surface_role_commit (CobiwmWaylandSurfaceRole  *surface_role,
                                  CobiwmWaylandPendingState *pending);

static gboolean
cobiwm_wayland_surface_role_is_on_output (CobiwmWaylandSurfaceRole *surface_role,
                                        CobiwmMonitorInfo *info);

gboolean
cobiwm_wayland_surface_assign_role (CobiwmWaylandSurface *surface,
                                  GType               role_type)
{
  if (!surface->role)
    {
      CobiwmWaylandSurfaceRolePrivate *role_priv;

      surface->role = g_object_new (role_type, NULL);
      role_priv =
        cobiwm_wayland_surface_role_get_instance_private (surface->role);
      role_priv->surface = surface;

      cobiwm_wayland_surface_role_assigned (surface->role);

      /* Release the use count held on behalf of the just assigned role. */
      if (surface->unassigned.buffer)
        {
          cobiwm_wayland_surface_unref_buffer_use_count (surface);
          g_clear_object (&surface->unassigned.buffer);
        }

      return TRUE;
    }
  else if (G_OBJECT_TYPE (surface->role) != role_type)
    {
      return FALSE;
    }
  else
    {
      return TRUE;
    }
}

static void
surface_process_damage (CobiwmWaylandSurface *surface,
                        cairo_region_t *region)
{
  CobiwmWaylandBuffer *buffer = surface->buffer_ref.buffer;
  unsigned int buffer_width;
  unsigned int buffer_height;
  cairo_rectangle_int_t surface_rect;
  cairo_region_t *scaled_region;
  int i, n_rectangles;

  /* If the client destroyed the buffer it attached before committing, but
   * still posted damage, or posted damage without any buffer, don't try to
   * process it on the non-existing buffer.
   */
  if (!buffer)
    return;

  /* Intersect the damage region with the surface region before scaling in
   * order to avoid integer overflow when scaling a damage region is too large
   * (for example INT32_MAX which mesa passes). */
  buffer_width = cogl_texture_get_width (buffer->texture);
  buffer_height = cogl_texture_get_height (buffer->texture);
  surface_rect = (cairo_rectangle_int_t) {
    .width = buffer_width / surface->scale,
    .height = buffer_height / surface->scale,
  };
  cairo_region_intersect_rectangle (region, &surface_rect);

  /* The damage region must be in the same coordinate space as the buffer,
   * i.e. scaled with surface->scale. */
  scaled_region = cobiwm_region_scale (region, surface->scale);

  /* First update the buffer. */
  cobiwm_wayland_buffer_process_damage (buffer, scaled_region);

  /* Now damage the actor. The actor expects damage in the unscaled texture
   * coordinate space, i.e. same as the buffer. */
  /* XXX: Should this be a signal / callback on CobiwmWaylandBuffer instead? */
  n_rectangles = cairo_region_num_rectangles (scaled_region);
  for (i = 0; i < n_rectangles; i++)
    {
      cairo_rectangle_int_t rect;
      cairo_region_get_rectangle (scaled_region, i, &rect);

      cobiwm_surface_actor_process_damage (surface->surface_actor,
                                         rect.x, rect.y,
                                         rect.width, rect.height);
    }

  cairo_region_destroy (scaled_region);
}

void
cobiwm_wayland_surface_queue_pending_state_frame_callbacks (CobiwmWaylandSurface      *surface,
                                                          CobiwmWaylandPendingState *pending)
{
  wl_list_insert_list (&surface->compositor->frame_callbacks,
                       &pending->frame_callback_list);
  wl_list_init (&pending->frame_callback_list);
}

static void
dnd_surface_commit (CobiwmWaylandSurfaceRole  *surface_role,
                    CobiwmWaylandPendingState *pending)
{
  CobiwmWaylandSurface *surface =
    cobiwm_wayland_surface_role_get_surface (surface_role);

  cobiwm_wayland_surface_queue_pending_state_frame_callbacks (surface, pending);
}

static void
calculate_surface_window_geometry (CobiwmWaylandSurface *surface,
                                   CobiwmRectangle      *total_geometry,
                                   float               parent_x,
                                   float               parent_y)
{
  CobiwmSurfaceActorWayland *surface_actor =
    COBIWM_SURFACE_ACTOR_WAYLAND (surface->surface_actor);
  CobiwmRectangle subsurface_rect;
  CobiwmRectangle geom;
  GList *l;

  /* Unmapped surfaces don't count. */
  if (!CLUTTER_ACTOR_IS_VISIBLE (CLUTTER_ACTOR (surface_actor)))
    return;

  if (!surface->buffer_ref.buffer)
    return;

  cobiwm_surface_actor_wayland_get_subsurface_rect (surface_actor,
                                                  &subsurface_rect);

  geom.x = parent_x + subsurface_rect.x;
  geom.y = parent_x + subsurface_rect.y;
  geom.width = subsurface_rect.width;
  geom.height = subsurface_rect.height;

  cobiwm_rectangle_union (total_geometry, &geom, total_geometry);

  for (l = surface->subsurfaces; l != NULL; l = l->next)
    {
      CobiwmWaylandSurface *subsurface = l->data;
      calculate_surface_window_geometry (subsurface, total_geometry,
                                         subsurface_rect.x,
                                         subsurface_rect.y);
    }
}

static void
destroy_window (CobiwmWaylandSurface *surface)
{
  if (surface->window)
    {
      CobiwmDisplay *display = cobiwm_get_display ();
      guint32 timestamp = cobiwm_display_get_current_time_roundtrip (display);

      cobiwm_window_unmanage (surface->window, timestamp);
    }

  g_assert (surface->window == NULL);
}

CobiwmWaylandBuffer *
cobiwm_wayland_surface_get_buffer (CobiwmWaylandSurface *surface)
{
  return surface->buffer_ref.buffer;
}

void
cobiwm_wayland_surface_ref_buffer_use_count (CobiwmWaylandSurface *surface)
{
  g_return_if_fail (surface->buffer_ref.buffer);
  g_warn_if_fail (surface->buffer_ref.buffer->resource);

  surface->buffer_ref.use_count++;
}

void
cobiwm_wayland_surface_unref_buffer_use_count (CobiwmWaylandSurface *surface)
{
  CobiwmWaylandBuffer *buffer = surface->buffer_ref.buffer;

  g_return_if_fail (surface->buffer_ref.use_count != 0);

  surface->buffer_ref.use_count--;

  g_return_if_fail (buffer);

  if (surface->buffer_ref.use_count == 0 && buffer->resource)
    wl_resource_queue_event (buffer->resource, WL_BUFFER_RELEASE);
}

static void
queue_surface_actor_frame_callbacks (CobiwmWaylandSurface      *surface,
                                     CobiwmWaylandPendingState *pending)
{
  CobiwmSurfaceActorWayland *surface_actor =
    COBIWM_SURFACE_ACTOR_WAYLAND (surface->surface_actor);

  cobiwm_surface_actor_wayland_add_frame_callbacks (surface_actor,
                                                  &pending->frame_callback_list);
  wl_list_init (&pending->frame_callback_list);
}

static void
toplevel_surface_commit (CobiwmWaylandSurfaceRole  *surface_role,
                         CobiwmWaylandPendingState *pending)
{
  CobiwmWaylandSurface *surface =
    cobiwm_wayland_surface_role_get_surface (surface_role);
  CobiwmWaylandBuffer *buffer = surface->buffer_ref.buffer;
  CobiwmWindow *window = surface->window;

  queue_surface_actor_frame_callbacks (surface, pending);

  /* If there's no new buffer pending, then there's nothing else to
   * do
   */
  if (!pending->newly_attached)
    return;

  if (COBIWM_IS_WAYLAND_SURFACE_ROLE_WL_SHELL_SURFACE (surface->role))
    {
      /* For wl_shell, it's equivalent to an unmap. Semantics
       * are poorly defined, so we can choose some that are
       * convenient for us. */
      if (buffer && !window)
        {
          window = cobiwm_window_wayland_new (cobiwm_get_display (), surface);
          cobiwm_wayland_surface_set_window (surface, window);
        }
      else if (buffer == NULL && window)
        {
          destroy_window (surface);
          return;
        }
    }
  else
    {
      if (buffer == NULL)
        {
          /* XDG surfaces can't commit NULL buffers */
          wl_resource_post_error (surface->resource,
                                  WL_DISPLAY_ERROR_INVALID_OBJECT,
                                  "Cannot commit a NULL buffer to an xdg_surface");
          return;
        }
    }

  /* Update the state of the CobiwmWindow if we still have one. We might not if
   * the window was unmanaged (for example popup destroyed, NULL buffer attached to
   * wl_shell_surface wl_surface, xdg_surface object was destroyed, etc).
   */
  if (window && window->client_type == COBIWM_WINDOW_CLIENT_TYPE_WAYLAND)
    {
      CobiwmRectangle geom = { 0 };

      CoglTexture *texture = buffer->texture;
      /* Update the buffer rect immediately. */
      window->buffer_rect.width = cogl_texture_get_width (texture);
      window->buffer_rect.height = cogl_texture_get_height (texture);

      if (pending->has_new_geometry)
        {
          /* If we have new geometry, use it. */
          geom = pending->new_geometry;
          surface->has_set_geometry = TRUE;
        }
      else if (!surface->has_set_geometry)
        {
          /* If the surface has never set any geometry, calculate
           * a default one unioning the surface and all subsurfaces together. */
          calculate_surface_window_geometry (surface, &geom, 0, 0);
        }
      else
        {
          /* Otherwise, keep the geometry the same. */

          /* XXX: We don't store the geometry in any consistent place
           * right now, so we can't re-fetch it. We should change
           * cobiwm_window_wayland_move_resize. */

          /* XXX: This is the common case. Recognize it to prevent
           * a warning. */
          if (pending->dx == 0 && pending->dy == 0)
            return;

          g_warning ("XXX: Attach-initiated move without a new geometry. This is unimplemented right now.");
          return;
        }

      cobiwm_window_wayland_move_resize (window,
                                       &surface->acked_configure_serial,
                                       geom, pending->dx, pending->dy);
      surface->acked_configure_serial.set = FALSE;
    }
}

static void
pending_buffer_resource_destroyed (CobiwmWaylandBuffer       *buffer,
                                   CobiwmWaylandPendingState *pending)
{
  g_signal_handler_disconnect (buffer, pending->buffer_destroy_handler_id);
  pending->buffer = NULL;
}

static void
pending_state_init (CobiwmWaylandPendingState *state)
{
  state->newly_attached = FALSE;
  state->buffer = NULL;
  state->dx = 0;
  state->dy = 0;
  state->scale = 0;

  state->input_region = NULL;
  state->input_region_set = FALSE;
  state->opaque_region = NULL;
  state->opaque_region_set = FALSE;

  state->damage = cairo_region_create ();
  wl_list_init (&state->frame_callback_list);

  state->has_new_geometry = FALSE;
}

static void
pending_state_destroy (CobiwmWaylandPendingState *state)
{
  CobiwmWaylandFrameCallback *cb, *next;

  g_clear_pointer (&state->damage, cairo_region_destroy);
  g_clear_pointer (&state->input_region, cairo_region_destroy);
  g_clear_pointer (&state->opaque_region, cairo_region_destroy);

  if (state->buffer)
    g_signal_handler_disconnect (state->buffer,
                                 state->buffer_destroy_handler_id);
  wl_list_for_each_safe (cb, next, &state->frame_callback_list, link)
    wl_resource_destroy (cb->resource);
}

static void
pending_state_reset (CobiwmWaylandPendingState *state)
{
  pending_state_destroy (state);
  pending_state_init (state);
}

static void
move_pending_state (CobiwmWaylandPendingState *from,
                    CobiwmWaylandPendingState *to)
{
  if (from->buffer)
    g_signal_handler_disconnect (from->buffer, from->buffer_destroy_handler_id);

  to->newly_attached = from->newly_attached;
  to->buffer = from->buffer;
  to->dx = from->dx;
  to->dy = from->dy;
  to->scale = from->scale;
  to->damage = from->damage;
  to->input_region = from->input_region;
  to->input_region_set = from->input_region_set;
  to->opaque_region = from->opaque_region;
  to->opaque_region_set = from->opaque_region_set;
  to->new_geometry = from->new_geometry;
  to->has_new_geometry = from->has_new_geometry;

  wl_list_init (&to->frame_callback_list);
  wl_list_insert_list (&to->frame_callback_list, &from->frame_callback_list);

  if (to->buffer)
    {
      to->buffer_destroy_handler_id =
        g_signal_connect (to->buffer, "resource-destroyed",
                          G_CALLBACK (pending_buffer_resource_destroyed),
                          to);
    }

  pending_state_init (from);
}

static void
cobiwm_wayland_pending_state_finalize (GObject *object)
{
  CobiwmWaylandPendingState *state = COBIWM_WAYLAND_PENDING_STATE (object);

  pending_state_destroy (state);

  G_OBJECT_CLASS (cobiwm_wayland_pending_state_parent_class)->finalize (object);
}

static void
cobiwm_wayland_pending_state_init (CobiwmWaylandPendingState *state)
{
  pending_state_init (state);
}

static void
cobiwm_wayland_pending_state_class_init (CobiwmWaylandPendingStateClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = cobiwm_wayland_pending_state_finalize;

  pending_state_signals[PENDING_STATE_SIGNAL_APPLIED] =
    g_signal_new ("applied",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
}

static void
subsurface_surface_commit (CobiwmWaylandSurfaceRole  *surface_role,
                           CobiwmWaylandPendingState *pending)
{
  CobiwmWaylandSurface *surface =
    cobiwm_wayland_surface_role_get_surface (surface_role);
  CobiwmSurfaceActorWayland *surface_actor =
    COBIWM_SURFACE_ACTOR_WAYLAND (surface->surface_actor);

  queue_surface_actor_frame_callbacks (surface, pending);

  if (surface->buffer_ref.buffer != NULL)
    clutter_actor_show (CLUTTER_ACTOR (surface_actor));
  else
    clutter_actor_hide (CLUTTER_ACTOR (surface_actor));
}

/* A non-subsurface is always desynchronized.
 *
 * A subsurface is effectively synchronized if either its parent is
 * synchronized or itself is in synchronized mode. */
static gboolean
is_surface_effectively_synchronized (CobiwmWaylandSurface *surface)
{
  if (surface->wl_subsurface == NULL)
    {
      return FALSE;
    }
  else
    {
      if (surface->sub.synchronous)
        return TRUE;
      else
        return is_surface_effectively_synchronized (surface->sub.parent);
    }
}

static void
apply_pending_state (CobiwmWaylandSurface      *surface,
                     CobiwmWaylandPendingState *pending);

static void
parent_surface_state_applied (gpointer data, gpointer user_data)
{
  CobiwmWaylandSurface *surface = data;

  if (surface->sub.pending_pos)
    {
      surface->sub.x = surface->sub.pending_x;
      surface->sub.y = surface->sub.pending_y;
      surface->sub.pending_pos = FALSE;
    }

  if (surface->sub.pending_placement_ops)
    {
      GSList *it;
      CobiwmWaylandSurface *parent = surface->sub.parent;
      ClutterActor *parent_actor =
        clutter_actor_get_parent (CLUTTER_ACTOR (parent->surface_actor));
      ClutterActor *surface_actor =
        surface_actor = CLUTTER_ACTOR (surface->surface_actor);

      for (it = surface->sub.pending_placement_ops; it; it = it->next)
        {
          CobiwmWaylandSubsurfacePlacementOp *op = it->data;
          ClutterActor *sibling_actor;

          if (!op->sibling)
            {
              g_slice_free (CobiwmWaylandSubsurfacePlacementOp, op);
              continue;
            }

          sibling_actor = CLUTTER_ACTOR (op->sibling->surface_actor);

          switch (op->placement)
            {
            case COBIWM_WAYLAND_SUBSURFACE_PLACEMENT_ABOVE:
              clutter_actor_set_child_above_sibling (parent_actor,
                                                     surface_actor,
                                                     sibling_actor);
              break;
            case COBIWM_WAYLAND_SUBSURFACE_PLACEMENT_BELOW:
              clutter_actor_set_child_below_sibling (parent_actor,
                                                     surface_actor,
                                                     sibling_actor);
              break;
            }

          wl_list_remove (&op->sibling_destroy_listener.link);
          g_slice_free (CobiwmWaylandSubsurfacePlacementOp, op);
        }

      g_slist_free (surface->sub.pending_placement_ops);
      surface->sub.pending_placement_ops = NULL;
    }

  if (is_surface_effectively_synchronized (surface))
    apply_pending_state (surface, surface->sub.pending);

  cobiwm_surface_actor_wayland_sync_subsurface_state (
    COBIWM_SURFACE_ACTOR_WAYLAND (surface->surface_actor));
}

static void
apply_pending_state (CobiwmWaylandSurface      *surface,
                     CobiwmWaylandPendingState *pending)
{
  CobiwmSurfaceActorWayland *surface_actor_wayland =
    COBIWM_SURFACE_ACTOR_WAYLAND (surface->surface_actor);

  if (surface->role)
    {
      cobiwm_wayland_surface_role_pre_commit (surface->role, pending);
    }
  else
    {
      if (pending->newly_attached && surface->unassigned.buffer)
        {
          cobiwm_wayland_surface_unref_buffer_use_count (surface);
          g_clear_object (&surface->unassigned.buffer);
        }
    }

  if (pending->newly_attached)
    {
      gboolean switched_buffer;

      if (!surface->buffer_ref.buffer && surface->window)
        cobiwm_window_queue (surface->window, COBIWM_QUEUE_CALC_SHOWING);

      /* Always release any previously held buffer. If the buffer held is same
       * as the newly attached buffer, we still need to release it here, because
       * wl_surface.attach+commit and wl_buffer.release on the attached buffer
       * is symmetric.
       */
      if (surface->buffer_held)
        cobiwm_wayland_surface_unref_buffer_use_count (surface);

      switched_buffer = g_set_object (&surface->buffer_ref.buffer,
                                      pending->buffer);

      if (pending->buffer)
        cobiwm_wayland_surface_ref_buffer_use_count (surface);

      if (switched_buffer && pending->buffer)
        {
          CoglTexture *texture;

          texture = cobiwm_wayland_buffer_ensure_texture (pending->buffer);
          cobiwm_surface_actor_wayland_set_texture (surface_actor_wayland,
                                                  texture);
        }

      /* If the newly attached buffer is going to be accessed directly without
       * making a copy, such as an EGL buffer, mark it as in-use don't release
       * it until is replaced by a subsequent wl_surface.commit or when the
       * wl_surface is destroyed.
       */
      surface->buffer_held = (pending->buffer &&
                              !wl_shm_buffer_get (pending->buffer->resource));
    }

  if (pending->scale > 0)
    surface->scale = pending->scale;

  if (!cairo_region_is_empty (pending->damage))
    surface_process_damage (surface, pending->damage);

  surface->offset_x += pending->dx;
  surface->offset_y += pending->dy;

  if (pending->opaque_region_set)
    {
      if (surface->opaque_region)
        cairo_region_destroy (surface->opaque_region);
      if (pending->opaque_region)
        surface->opaque_region = cairo_region_reference (pending->opaque_region);
      else
        surface->opaque_region = NULL;
    }

  if (pending->input_region_set)
    {
      if (surface->input_region)
        cairo_region_destroy (surface->input_region);
      if (pending->input_region)
        surface->input_region = cairo_region_reference (pending->input_region);
      else
        surface->input_region = NULL;
    }

  if (surface->role)
    {
      cobiwm_wayland_surface_role_commit (surface->role, pending);
      g_assert (wl_list_empty (&pending->frame_callback_list));
    }
  else
    {
      /* Since there is no role assigned to the surface yet, keep frame
       * callbacks queued until a role is assigned and we know how
       * the surface will be drawn.
       */
      wl_list_insert_list (&surface->pending_frame_callback_list,
                           &pending->frame_callback_list);
      wl_list_init (&pending->frame_callback_list);

      if (pending->newly_attached)
        {
          /* The need to keep the wl_buffer from being released depends on what
           * role the surface is given. That means we need to also keep a use
           * count for wl_buffer's that are used by unassigned wl_surface's.
           */
          g_set_object (&surface->unassigned.buffer, surface->buffer_ref.buffer);
          if (surface->unassigned.buffer)
            cobiwm_wayland_surface_ref_buffer_use_count (surface);
        }
    }

  /* If we have a buffer that we are not using, decrease the use count so it may
   * be released if no-one else has a use-reference to it.
   */
  if (pending->newly_attached &&
      !surface->buffer_held && surface->buffer_ref.buffer)
    cobiwm_wayland_surface_unref_buffer_use_count (surface);

  g_signal_emit (pending,
                 pending_state_signals[PENDING_STATE_SIGNAL_APPLIED],
                 0);

  cobiwm_surface_actor_wayland_sync_state (surface_actor_wayland);

  pending_state_reset (pending);

  g_list_foreach (surface->subsurfaces, parent_surface_state_applied, NULL);
}

static void
cobiwm_wayland_surface_commit (CobiwmWaylandSurface *surface)
{
  /*
   * If this is a sub-surface and it is in effective synchronous mode, only
   * cache the pending surface state until either one of the following two
   * scenarios happens:
   *  1) Its parent surface gets its state applied.
   *  2) Its mode changes from synchronized to desynchronized and its parent
   *     surface is in effective desynchronized mode.
   */
  if (is_surface_effectively_synchronized (surface))
    move_pending_state (surface->pending, surface->sub.pending);
  else
    apply_pending_state (surface, surface->pending);
}

static void
wl_surface_destroy (struct wl_client *client,
                    struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
wl_surface_attach (struct wl_client *client,
                   struct wl_resource *surface_resource,
                   struct wl_resource *buffer_resource,
                   gint32 dx, gint32 dy)
{
  CobiwmWaylandSurface *surface =
    wl_resource_get_user_data (surface_resource);
  CobiwmWaylandBuffer *buffer;

  /* X11 unmanaged window */
  if (!surface)
    return;

  if (buffer_resource)
    buffer = cobiwm_wayland_buffer_from_resource (buffer_resource);
  else
    buffer = NULL;

  if (surface->pending->buffer)
    {
      g_signal_handler_disconnect (surface->pending->buffer,
                                   surface->pending->buffer_destroy_handler_id);
    }

  surface->pending->newly_attached = TRUE;
  surface->pending->buffer = buffer;
  surface->pending->dx = dx;
  surface->pending->dy = dy;

  if (buffer)
    {
      surface->pending->buffer_destroy_handler_id =
        g_signal_connect (buffer, "resource-destroyed",
                          G_CALLBACK (pending_buffer_resource_destroyed),
                          surface->pending);
    }
}

static void
wl_surface_damage (struct wl_client *client,
                   struct wl_resource *surface_resource,
                   gint32 x,
                   gint32 y,
                   gint32 width,
                   gint32 height)
{
  CobiwmWaylandSurface *surface = wl_resource_get_user_data (surface_resource);
  cairo_rectangle_int_t rectangle = { x, y, width, height };

  /* X11 unmanaged window */
  if (!surface)
    return;

  cairo_region_union_rectangle (surface->pending->damage, &rectangle);
}

static void
destroy_frame_callback (struct wl_resource *callback_resource)
{
  CobiwmWaylandFrameCallback *callback =
    wl_resource_get_user_data (callback_resource);

  wl_list_remove (&callback->link);
  g_slice_free (CobiwmWaylandFrameCallback, callback);
}

static void
wl_surface_frame (struct wl_client *client,
                  struct wl_resource *surface_resource,
                  guint32 callback_id)
{
  CobiwmWaylandFrameCallback *callback;
  CobiwmWaylandSurface *surface = wl_resource_get_user_data (surface_resource);

  /* X11 unmanaged window */
  if (!surface)
    return;

  callback = g_slice_new0 (CobiwmWaylandFrameCallback);
  callback->surface = surface;
  callback->resource = wl_resource_create (client, &wl_callback_interface, COBIWM_WL_CALLBACK_VERSION, callback_id);
  wl_resource_set_implementation (callback->resource, NULL, callback, destroy_frame_callback);

  wl_list_insert (surface->pending->frame_callback_list.prev, &callback->link);
}

static void
wl_surface_set_opaque_region (struct wl_client *client,
                              struct wl_resource *surface_resource,
                              struct wl_resource *region_resource)
{
  CobiwmWaylandSurface *surface = wl_resource_get_user_data (surface_resource);

  /* X11 unmanaged window */
  if (!surface)
    return;

  g_clear_pointer (&surface->pending->opaque_region, cairo_region_destroy);
  if (region_resource)
    {
      CobiwmWaylandRegion *region = wl_resource_get_user_data (region_resource);
      cairo_region_t *cr_region = cobiwm_wayland_region_peek_cairo_region (region);
      surface->pending->opaque_region = cairo_region_copy (cr_region);
    }
  surface->pending->opaque_region_set = TRUE;
}

static void
wl_surface_set_input_region (struct wl_client *client,
                             struct wl_resource *surface_resource,
                             struct wl_resource *region_resource)
{
  CobiwmWaylandSurface *surface = wl_resource_get_user_data (surface_resource);

  /* X11 unmanaged window */
  if (!surface)
    return;

  g_clear_pointer (&surface->pending->input_region, cairo_region_destroy);
  if (region_resource)
    {
      CobiwmWaylandRegion *region = wl_resource_get_user_data (region_resource);
      cairo_region_t *cr_region = cobiwm_wayland_region_peek_cairo_region (region);
      surface->pending->input_region = cairo_region_copy (cr_region);
    }
  surface->pending->input_region_set = TRUE;
}

static void
wl_surface_commit (struct wl_client *client,
                   struct wl_resource *resource)
{
  CobiwmWaylandSurface *surface = wl_resource_get_user_data (resource);

  /* X11 unmanaged window */
  if (!surface)
    return;

  cobiwm_wayland_surface_commit (surface);
}

static void
wl_surface_set_buffer_transform (struct wl_client *client,
                                 struct wl_resource *resource,
                                 int32_t transform)
{
  g_warning ("TODO: support set_buffer_transform request");
}

static void
wl_surface_set_buffer_scale (struct wl_client *client,
                             struct wl_resource *resource,
                             int scale)
{
  CobiwmWaylandSurface *surface = wl_resource_get_user_data (resource);
  if (scale > 0)
    surface->pending->scale = scale;
  else
    g_warning ("Trying to set invalid buffer_scale of %d\n", scale);
}

static const struct wl_surface_interface cobiwm_wayland_wl_surface_interface = {
  wl_surface_destroy,
  wl_surface_attach,
  wl_surface_damage,
  wl_surface_frame,
  wl_surface_set_opaque_region,
  wl_surface_set_input_region,
  wl_surface_commit,
  wl_surface_set_buffer_transform,
  wl_surface_set_buffer_scale
};

static gboolean
surface_should_be_reactive (CobiwmWaylandSurface *surface)
{
  /* If we have a toplevel window, we should be reactive */
  if (surface->window)
    return TRUE;

  /* If we're a subsurface, we should be reactive */
  if (surface->wl_subsurface)
    return TRUE;

  return FALSE;
}

static void
sync_reactive (CobiwmWaylandSurface *surface)
{
  clutter_actor_set_reactive (CLUTTER_ACTOR (surface->surface_actor),
                              surface_should_be_reactive (surface));
}

static void
sync_drag_dest_funcs (CobiwmWaylandSurface *surface)
{
  if (surface->window &&
      surface->window->client_type == COBIWM_WINDOW_CLIENT_TYPE_X11)
    surface->dnd.funcs = cobiwm_xwayland_selection_get_drag_dest_funcs ();
  else
    surface->dnd.funcs = cobiwm_wayland_data_device_get_drag_dest_funcs ();
}

static void
surface_entered_output (CobiwmWaylandSurface *surface,
                        CobiwmWaylandOutput *wayland_output)
{
  GList *iter;
  struct wl_resource *resource;

  for (iter = wayland_output->resources; iter != NULL; iter = iter->next)
    {
      resource = iter->data;

      if (wl_resource_get_client (resource) !=
          wl_resource_get_client (surface->resource))
        continue;

      wl_surface_send_enter (surface->resource, resource);
    }
}

static void
surface_left_output (CobiwmWaylandSurface *surface,
                     CobiwmWaylandOutput *wayland_output)
{
  GList *iter;
  struct wl_resource *resource;

  for (iter = wayland_output->resources; iter != NULL; iter = iter->next)
    {
      resource = iter->data;

      if (wl_resource_get_client (resource) !=
          wl_resource_get_client (surface->resource))
        continue;

      wl_surface_send_leave (surface->resource, resource);
    }
}

static void
set_surface_is_on_output (CobiwmWaylandSurface *surface,
                          CobiwmWaylandOutput *wayland_output,
                          gboolean is_on_output);

static void
surface_handle_output_destroy (CobiwmWaylandOutput *wayland_output,
                               CobiwmWaylandSurface *surface)
{
  set_surface_is_on_output (surface, wayland_output, FALSE);
}

static void
set_surface_is_on_output (CobiwmWaylandSurface *surface,
                          CobiwmWaylandOutput *wayland_output,
                          gboolean is_on_output)
{
  gpointer orig_id;
  gboolean was_on_output = g_hash_table_lookup_extended (surface->outputs_to_destroy_notify_id,
                                                         wayland_output,
                                                         NULL, &orig_id);

  if (!was_on_output && is_on_output)
    {
      gulong id;

      id = g_signal_connect (wayland_output, "output-destroyed",
                             G_CALLBACK (surface_handle_output_destroy),
                             surface);
      g_hash_table_insert (surface->outputs_to_destroy_notify_id, wayland_output,
                           GSIZE_TO_POINTER ((gsize)id));
      surface_entered_output (surface, wayland_output);
    }
  else if (was_on_output && !is_on_output)
    {
      g_hash_table_remove (surface->outputs_to_destroy_notify_id, wayland_output);
      g_signal_handler_disconnect (wayland_output, (gulong) GPOINTER_TO_SIZE (orig_id));
      surface_left_output (surface, wayland_output);
    }
}

static gboolean
actor_surface_is_on_output (CobiwmWaylandSurfaceRole *surface_role,
                            CobiwmMonitorInfo        *monitor)
{
  CobiwmWaylandSurface *surface =
    cobiwm_wayland_surface_role_get_surface (surface_role);
  CobiwmSurfaceActorWayland *actor =
    COBIWM_SURFACE_ACTOR_WAYLAND (surface->surface_actor);

  return cobiwm_surface_actor_wayland_is_on_monitor (actor, monitor);
}

static void
update_surface_output_state (gpointer key, gpointer value, gpointer user_data)
{
  CobiwmWaylandOutput *wayland_output = value;
  CobiwmWaylandSurface *surface = user_data;
  CobiwmMonitorInfo *monitor;
  gboolean is_on_output;

  g_assert (surface->role);

  monitor = wayland_output->monitor_info;
  if (!monitor)
    {
      set_surface_is_on_output (surface, wayland_output, FALSE);
      return;
    }

  is_on_output = cobiwm_wayland_surface_role_is_on_output (surface->role, monitor);
  set_surface_is_on_output (surface, wayland_output, is_on_output);
}

static void
surface_output_disconnect_signal (gpointer key, gpointer value, gpointer user_data)
{
  g_signal_handler_disconnect (key, (gulong) GPOINTER_TO_SIZE (value));
}

void
cobiwm_wayland_surface_update_outputs (CobiwmWaylandSurface *surface)
{
  if (!surface->compositor)
    return;

  g_hash_table_foreach (surface->compositor->outputs,
                        update_surface_output_state,
                        surface);
}

void
cobiwm_wayland_surface_set_window (CobiwmWaylandSurface *surface,
                                 CobiwmWindow         *window)
{
  surface->window = window;
  sync_reactive (surface);
  sync_drag_dest_funcs (surface);
}

static void
wl_surface_destructor (struct wl_resource *resource)
{
  CobiwmWaylandSurface *surface = wl_resource_get_user_data (resource);
  CobiwmWaylandCompositor *compositor = surface->compositor;
  CobiwmWaylandFrameCallback *cb, *next;

  g_clear_object (&surface->role);

  /* If we still have a window at the time of destruction, that means that
   * the client is disconnecting, as the resources are destroyed in a random
   * order. Simply destroy the window in this case. */
  if (surface->window)
    destroy_window (surface);

  if (surface->unassigned.buffer)
    {
      cobiwm_wayland_surface_unref_buffer_use_count (surface);
      g_clear_object (&surface->unassigned.buffer);
    }

  if (surface->buffer_held)
    cobiwm_wayland_surface_unref_buffer_use_count (surface);
  g_clear_object (&surface->buffer_ref.buffer);

  g_clear_object (&surface->pending);

  if (surface->opaque_region)
    cairo_region_destroy (surface->opaque_region);
  if (surface->input_region)
    cairo_region_destroy (surface->input_region);

  cobiwm_surface_actor_wayland_surface_destroyed (
    COBIWM_SURFACE_ACTOR_WAYLAND (surface->surface_actor));

  g_object_unref (surface->surface_actor);

  cobiwm_wayland_compositor_destroy_frame_callbacks (compositor, surface);

  g_hash_table_foreach (surface->outputs_to_destroy_notify_id, surface_output_disconnect_signal, surface);
  g_hash_table_unref (surface->outputs_to_destroy_notify_id);

  wl_list_for_each_safe (cb, next, &surface->pending_frame_callback_list, link)
    wl_resource_destroy (cb->resource);

  if (surface->resource)
    wl_resource_set_user_data (surface->resource, NULL);

  if (surface->xdg_surface)
    wl_resource_destroy (surface->xdg_surface);
  if (surface->xdg_popup)
    wl_resource_destroy (surface->xdg_popup);
  if (surface->wl_subsurface)
    wl_resource_destroy (surface->wl_subsurface);
  if (surface->wl_shell_surface)
    wl_resource_destroy (surface->wl_shell_surface);
  if (surface->gtk_surface)
    wl_resource_destroy (surface->gtk_surface);

  g_object_unref (surface);

  cobiwm_wayland_compositor_repick (compositor);
}

static void
surface_actor_painting (CobiwmSurfaceActorWayland *surface_actor,
                        CobiwmWaylandSurface      *surface)
{
  cobiwm_wayland_surface_update_outputs (surface);
}

CobiwmWaylandSurface *
cobiwm_wayland_surface_create (CobiwmWaylandCompositor *compositor,
                             struct wl_client      *client,
                             struct wl_resource    *compositor_resource,
                             guint32                id)
{
  CobiwmWaylandSurface *surface = g_object_new (COBIWM_TYPE_WAYLAND_SURFACE, NULL);

  surface->compositor = compositor;
  surface->scale = 1;

  surface->resource = wl_resource_create (client, &wl_surface_interface, wl_resource_get_version (compositor_resource), id);
  wl_resource_set_implementation (surface->resource, &cobiwm_wayland_wl_surface_interface, surface, wl_surface_destructor);

  surface->surface_actor = g_object_ref_sink (cobiwm_surface_actor_wayland_new (surface));

  wl_list_init (&surface->pending_frame_callback_list);

  g_signal_connect_object (surface->surface_actor,
                           "painting",
                           G_CALLBACK (surface_actor_painting),
                           surface,
                           0);

  sync_drag_dest_funcs (surface);

  surface->outputs_to_destroy_notify_id = g_hash_table_new (NULL, NULL);

  return surface;
}

static void
xdg_shell_destroy (struct wl_client *client,
                   struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
xdg_shell_use_unstable_version (struct wl_client *client,
                                struct wl_resource *resource,
                                int32_t version)
{
  if (version != XDG_SHELL_VERSION_CURRENT)
    wl_resource_post_error (resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
                            "bad xdg-shell version: %d\n", version);
}

static void
xdg_shell_pong (struct wl_client *client,
                struct wl_resource *resource,
                uint32_t serial)
{
  CobiwmDisplay *display = cobiwm_get_display ();

  cobiwm_display_pong_for_serial (display, serial);
}

static void
xdg_surface_destructor (struct wl_resource *resource)
{
  CobiwmWaylandSurface *surface = wl_resource_get_user_data (resource);

  cobiwm_wayland_compositor_destroy_frame_callbacks (surface->compositor,
                                                   surface);
  destroy_window (surface);
  surface->xdg_surface = NULL;
}

static void
xdg_surface_destroy (struct wl_client *client,
                     struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
xdg_surface_set_parent (struct wl_client *client,
                        struct wl_resource *resource,
                        struct wl_resource *parent_resource)
{
  CobiwmWaylandSurface *surface = wl_resource_get_user_data (resource);
  CobiwmWindow *transient_for = NULL;

  if (parent_resource)
    {
      CobiwmWaylandSurface *parent_surface = wl_resource_get_user_data (parent_resource);
      transient_for = parent_surface->window;
    }

  cobiwm_window_set_transient_for (surface->window, transient_for);
}

static void
xdg_surface_set_title (struct wl_client *client,
                       struct wl_resource *resource,
                       const char *title)
{
  CobiwmWaylandSurface *surface = wl_resource_get_user_data (resource);

  if (!g_utf8_validate (title, -1, NULL))
    title = "";

  cobiwm_window_set_title (surface->window, title);
}

static void
xdg_surface_set_app_id (struct wl_client *client,
                        struct wl_resource *resource,
                        const char *app_id)
{
  CobiwmWaylandSurface *surface = wl_resource_get_user_data (resource);

  if (!g_utf8_validate (app_id, -1, NULL))
    app_id = "";

  cobiwm_window_set_wm_class (surface->window, app_id, app_id);
}

static void
xdg_surface_show_window_menu (struct wl_client *client,
                              struct wl_resource *resource,
                              struct wl_resource *seat_resource,
                              uint32_t serial,
                              int32_t x,
                              int32_t y)
{
  CobiwmWaylandSeat *seat = wl_resource_get_user_data (seat_resource);
  CobiwmWaylandSurface *surface = wl_resource_get_user_data (resource);

  if (!cobiwm_wayland_seat_get_grab_info (seat, surface, serial, FALSE, NULL, NULL))
    return;

  cobiwm_window_show_menu (surface->window, COBIWM_WINDOW_MENU_WM,
                         surface->window->buffer_rect.x + x,
                         surface->window->buffer_rect.y + y);
}

static gboolean
begin_grab_op_on_surface (CobiwmWaylandSurface *surface,
                          CobiwmWaylandSeat    *seat,
                          CobiwmGrabOp          grab_op,
                          gfloat              x,
                          gfloat              y)
{
  CobiwmWindow *window = surface->window;

  if (grab_op == COBIWM_GRAB_OP_NONE)
    return FALSE;

  /* This is an input driven operation so we set frame_action to
     constrain it in the same way as it would be if the window was
     being moved/resized via a SSD event. */
  return cobiwm_display_begin_grab_op (window->display,
                                     window->screen,
                                     window,
                                     grab_op,
                                     TRUE, /* pointer_already_grabbed */
                                     TRUE, /* frame_action */
                                     1, /* button. XXX? */
                                     0, /* modmask */
                                     cobiwm_display_get_current_time_roundtrip (window->display),
                                     x, y);
}

static void
xdg_surface_move (struct wl_client *client,
                  struct wl_resource *resource,
                  struct wl_resource *seat_resource,
                  guint32 serial)
{
  CobiwmWaylandSeat *seat = wl_resource_get_user_data (seat_resource);
  CobiwmWaylandSurface *surface = wl_resource_get_user_data (resource);
  gfloat x, y;

  if (!cobiwm_wayland_seat_get_grab_info (seat, surface, serial, TRUE, &x, &y))
    return;

  begin_grab_op_on_surface (surface, seat, COBIWM_GRAB_OP_MOVING, x, y);
}

static CobiwmGrabOp
grab_op_for_xdg_surface_resize_edge (int edge)
{
  CobiwmGrabOp op = COBIWM_GRAB_OP_WINDOW_BASE;

  if (edge & XDG_SURFACE_RESIZE_EDGE_TOP)
    op |= COBIWM_GRAB_OP_WINDOW_DIR_NORTH;
  if (edge & XDG_SURFACE_RESIZE_EDGE_BOTTOM)
    op |= COBIWM_GRAB_OP_WINDOW_DIR_SOUTH;
  if (edge & XDG_SURFACE_RESIZE_EDGE_LEFT)
    op |= COBIWM_GRAB_OP_WINDOW_DIR_WEST;
  if (edge & XDG_SURFACE_RESIZE_EDGE_RIGHT)
    op |= COBIWM_GRAB_OP_WINDOW_DIR_EAST;

  if (op == COBIWM_GRAB_OP_WINDOW_BASE)
    {
      g_warning ("invalid edge: %d", edge);
      return COBIWM_GRAB_OP_NONE;
    }

  return op;
}

static void
xdg_surface_resize (struct wl_client *client,
                    struct wl_resource *resource,
                    struct wl_resource *seat_resource,
                    guint32 serial,
                    guint32 edges)
{
  CobiwmWaylandSeat *seat = wl_resource_get_user_data (seat_resource);
  CobiwmWaylandSurface *surface = wl_resource_get_user_data (resource);
  gfloat x, y;

  if (!cobiwm_wayland_seat_get_grab_info (seat, surface, serial, TRUE, &x, &y))
    return;

  begin_grab_op_on_surface (surface, seat, grab_op_for_xdg_surface_resize_edge (edges), x, y);
}

static void
xdg_surface_ack_configure (struct wl_client *client,
                           struct wl_resource *resource,
                           uint32_t serial)
{
  CobiwmWaylandSurface *surface = wl_resource_get_user_data (resource);

  surface->acked_configure_serial.set = TRUE;
  surface->acked_configure_serial.value = serial;
}

static void
xdg_surface_set_window_geometry (struct wl_client *client,
                                 struct wl_resource *resource,
                                 int32_t x, int32_t y, int32_t width, int32_t height)
{
  CobiwmWaylandSurface *surface = wl_resource_get_user_data (resource);

  surface->pending->has_new_geometry = TRUE;
  surface->pending->new_geometry.x = x;
  surface->pending->new_geometry.y = y;
  surface->pending->new_geometry.width = width;
  surface->pending->new_geometry.height = height;
}

static void
xdg_surface_set_maximized (struct wl_client *client,
                           struct wl_resource *resource)
{
  CobiwmWaylandSurface *surface = wl_resource_get_user_data (resource);
  cobiwm_window_maximize (surface->window, COBIWM_MAXIMIZE_BOTH);
}

static void
xdg_surface_unset_maximized (struct wl_client *client,
                             struct wl_resource *resource)
{
  CobiwmWaylandSurface *surface = wl_resource_get_user_data (resource);
  cobiwm_window_unmaximize (surface->window, COBIWM_MAXIMIZE_BOTH);
}

static void
xdg_surface_set_fullscreen (struct wl_client *client,
                            struct wl_resource *resource,
                            struct wl_resource *output_resource)
{
  CobiwmWaylandSurface *surface = wl_resource_get_user_data (resource);
  cobiwm_window_make_fullscreen (surface->window);
}

static void
xdg_surface_unset_fullscreen (struct wl_client *client,
                              struct wl_resource *resource)
{
  CobiwmWaylandSurface *surface = wl_resource_get_user_data (resource);
  cobiwm_window_unmake_fullscreen (surface->window);
}

static void
xdg_surface_set_minimized (struct wl_client *client,
                           struct wl_resource *resource)
{
  CobiwmWaylandSurface *surface = wl_resource_get_user_data (resource);
  cobiwm_window_minimize (surface->window);
}

static const struct xdg_surface_interface cobiwm_wayland_xdg_surface_interface = {
  xdg_surface_destroy,
  xdg_surface_set_parent,
  xdg_surface_set_title,
  xdg_surface_set_app_id,
  xdg_surface_show_window_menu,
  xdg_surface_move,
  xdg_surface_resize,
  xdg_surface_ack_configure,
  xdg_surface_set_window_geometry,
  xdg_surface_set_maximized,
  xdg_surface_unset_maximized,
  xdg_surface_set_fullscreen,
  xdg_surface_unset_fullscreen,
  xdg_surface_set_minimized,
};

static void
xdg_shell_get_xdg_surface (struct wl_client *client,
                           struct wl_resource *resource,
                           guint32 id,
                           struct wl_resource *surface_resource)
{
  CobiwmWaylandSurface *surface = wl_resource_get_user_data (surface_resource);
  CobiwmWindow *window;

  if (surface->xdg_surface != NULL)
    {
      wl_resource_post_error (surface_resource,
                              WL_DISPLAY_ERROR_INVALID_OBJECT,
                              "xdg_shell::get_xdg_surface already requested");
      return;
    }

  if (!cobiwm_wayland_surface_assign_role (surface,
                                         COBIWM_TYPE_WAYLAND_SURFACE_ROLE_XDG_SURFACE))
    {
      wl_resource_post_error (resource, XDG_SHELL_ERROR_ROLE,
                              "wl_surface@%d already has a different role",
                              wl_resource_get_id (surface->resource));
      return;
    }

  surface->xdg_surface = wl_resource_create (client, &xdg_surface_interface, wl_resource_get_version (resource), id);
  wl_resource_set_implementation (surface->xdg_surface, &cobiwm_wayland_xdg_surface_interface, surface, xdg_surface_destructor);

  surface->xdg_shell_resource = resource;

  window = cobiwm_window_wayland_new (cobiwm_get_display (), surface);
  cobiwm_wayland_surface_set_window (surface, window);
}

static void
xdg_popup_destructor (struct wl_resource *resource)
{
  CobiwmWaylandSurface *surface = wl_resource_get_user_data (resource);

  cobiwm_wayland_compositor_destroy_frame_callbacks (surface->compositor,
                                                   surface);
  if (surface->popup.parent)
    {
      wl_list_remove (&surface->popup.parent_destroy_listener.link);
      surface->popup.parent = NULL;
    }

  if (surface->popup.popup)
    cobiwm_wayland_popup_dismiss (surface->popup.popup);

  surface->xdg_popup = NULL;
}

static void
xdg_popup_destroy (struct wl_client *client,
                   struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static const struct xdg_popup_interface cobiwm_wayland_xdg_popup_interface = {
  xdg_popup_destroy,
};

static void
handle_popup_parent_destroyed (struct wl_listener *listener, void *data)
{
  CobiwmWaylandSurface *surface =
    wl_container_of (listener, surface, popup.parent_destroy_listener);

  wl_resource_post_error (surface->xdg_shell_resource,
                          XDG_SHELL_ERROR_NOT_THE_TOPMOST_POPUP,
                          "destroyed popup not top most popup");
  surface->popup.parent = NULL;

  destroy_window (surface);
}

static void
handle_popup_destroyed (struct wl_listener *listener, void *data)
{
  CobiwmWaylandPopup *popup = data;
  CobiwmWaylandSurface *top_popup;
  CobiwmWaylandSurface *surface =
    wl_container_of (listener, surface, popup.destroy_listener);

  top_popup = cobiwm_wayland_popup_get_top_popup (popup);
  if (surface != top_popup)
    {
      wl_resource_post_error (surface->xdg_shell_resource,
                              XDG_SHELL_ERROR_NOT_THE_TOPMOST_POPUP,
                              "destroyed popup not top most popup");
    }

  surface->popup.popup = NULL;

  destroy_window (surface);
}

static void
xdg_shell_get_xdg_popup (struct wl_client *client,
                         struct wl_resource *resource,
                         uint32_t id,
                         struct wl_resource *surface_resource,
                         struct wl_resource *parent_resource,
                         struct wl_resource *seat_resource,
                         uint32_t serial,
                         int32_t x,
                         int32_t y)
{
  struct wl_resource *popup_resource;
  CobiwmWaylandSurface *surface = wl_resource_get_user_data (surface_resource);
  CobiwmWaylandSurface *parent_surf = wl_resource_get_user_data (parent_resource);
  CobiwmWaylandSurface *top_popup;
  CobiwmWaylandSeat *seat = wl_resource_get_user_data (seat_resource);
  CobiwmWindow *window;
  CobiwmDisplay *display = cobiwm_get_display ();
  CobiwmWaylandPopup *popup;

  if (surface->xdg_popup != NULL)
    {
      wl_resource_post_error (surface_resource,
                              WL_DISPLAY_ERROR_INVALID_OBJECT,
                              "xdg_shell::get_xdg_popup already requested");
      return;
    }

  if (!cobiwm_wayland_surface_assign_role (surface,
                                         COBIWM_TYPE_WAYLAND_SURFACE_ROLE_XDG_POPUP))
    {
      wl_resource_post_error (resource, XDG_SHELL_ERROR_ROLE,
                              "wl_surface@%d already has a different role",
                              wl_resource_get_id (surface->resource));
      return;
    }

  if (parent_surf == NULL ||
      parent_surf->window == NULL ||
      (parent_surf->xdg_popup == NULL && parent_surf->xdg_surface == NULL))
    {
      wl_resource_post_error (resource,
                              XDG_SHELL_ERROR_INVALID_POPUP_PARENT,
                              "invalid parent surface");
      return;
    }

  top_popup = cobiwm_wayland_pointer_get_top_popup (&seat->pointer);
  if ((top_popup == NULL && parent_surf->xdg_surface == NULL) ||
      (top_popup != NULL && parent_surf != top_popup))
    {
      wl_resource_post_error (resource,
                              XDG_SHELL_ERROR_NOT_THE_TOPMOST_POPUP,
                              "parent not top most surface");
      return;
    }

  popup_resource = wl_resource_create (client, &xdg_popup_interface,
                                       wl_resource_get_version (resource), id);
  wl_resource_set_implementation (popup_resource,
                                  &cobiwm_wayland_xdg_popup_interface,
                                  surface,
                                  xdg_popup_destructor);

  surface->xdg_popup = popup_resource;
  surface->xdg_shell_resource = resource;

  if (!cobiwm_wayland_seat_can_popup (seat, serial))
    {
      xdg_popup_send_popup_done (popup_resource);
      return;
    }

  surface->popup.parent = parent_surf;
  surface->popup.parent_destroy_listener.notify = handle_popup_parent_destroyed;
  wl_resource_add_destroy_listener (parent_surf->resource,
                                    &surface->popup.parent_destroy_listener);

  window = cobiwm_window_wayland_new (display, surface);
  cobiwm_window_wayland_place_relative_to (window, parent_surf->window, x, y);
  window->showing_for_first_time = FALSE;

  cobiwm_wayland_surface_set_window (surface, window);

  cobiwm_window_focus (window, cobiwm_display_get_current_time (display));
  popup = cobiwm_wayland_pointer_start_popup_grab (&seat->pointer, surface);
  if (popup == NULL)
    {
      destroy_window (surface);
      return;
    }

  surface->popup.destroy_listener.notify = handle_popup_destroyed;
  surface->popup.popup = popup;
  wl_signal_add (cobiwm_wayland_popup_get_destroy_signal (popup),
                 &surface->popup.destroy_listener);
}

static const struct xdg_shell_interface cobiwm_wayland_xdg_shell_interface = {
  xdg_shell_destroy,
  xdg_shell_use_unstable_version,
  xdg_shell_get_xdg_surface,
  xdg_shell_get_xdg_popup,
  xdg_shell_pong,
};

static void
bind_xdg_shell (struct wl_client *client,
                void *data,
                guint32 version,
                guint32 id)
{
  struct wl_resource *resource;

  if (version != COBIWM_XDG_SHELL_VERSION)
    {
      g_warning ("using xdg-shell without stable version %d\n", COBIWM_XDG_SHELL_VERSION);
      return;
    }

  resource = wl_resource_create (client, &xdg_shell_interface, version, id);
  wl_resource_set_implementation (resource, &cobiwm_wayland_xdg_shell_interface, data, NULL);
}

static void
wl_shell_surface_destructor (struct wl_resource *resource)
{
  CobiwmWaylandSurface *surface = wl_resource_get_user_data (resource);

  cobiwm_wayland_compositor_destroy_frame_callbacks (surface->compositor,
                                                   surface);
  surface->wl_shell_surface = NULL;
}

static void
wl_shell_surface_pong (struct wl_client *client,
                       struct wl_resource *resource,
                       uint32_t serial)
{
  CobiwmDisplay *display = cobiwm_get_display ();

  cobiwm_display_pong_for_serial (display, serial);
}

static void
wl_shell_surface_move (struct wl_client *client,
                       struct wl_resource *resource,
                       struct wl_resource *seat_resource,
                       uint32_t serial)
{
  CobiwmWaylandSeat *seat = wl_resource_get_user_data (seat_resource);
  CobiwmWaylandSurface *surface = wl_resource_get_user_data (resource);
  gfloat x, y;

  if (!cobiwm_wayland_seat_get_grab_info (seat, surface, serial, TRUE, &x, &y))
    return;

  begin_grab_op_on_surface (surface, seat, COBIWM_GRAB_OP_MOVING, x, y);
}

static CobiwmGrabOp
grab_op_for_wl_shell_surface_resize_edge (int edge)
{
  CobiwmGrabOp op = COBIWM_GRAB_OP_WINDOW_BASE;

  if (edge & WL_SHELL_SURFACE_RESIZE_TOP)
    op |= COBIWM_GRAB_OP_WINDOW_DIR_NORTH;
  if (edge & WL_SHELL_SURFACE_RESIZE_BOTTOM)
    op |= COBIWM_GRAB_OP_WINDOW_DIR_SOUTH;
  if (edge & WL_SHELL_SURFACE_RESIZE_LEFT)
    op |= COBIWM_GRAB_OP_WINDOW_DIR_WEST;
  if (edge & WL_SHELL_SURFACE_RESIZE_RIGHT)
    op |= COBIWM_GRAB_OP_WINDOW_DIR_EAST;

  if (op == COBIWM_GRAB_OP_WINDOW_BASE)
    {
      g_warning ("invalid edge: %d", edge);
      return COBIWM_GRAB_OP_NONE;
    }

  return op;
}

static void
wl_shell_surface_resize (struct wl_client *client,
                         struct wl_resource *resource,
                         struct wl_resource *seat_resource,
                         uint32_t serial,
                         uint32_t edges)
{
  CobiwmWaylandSeat *seat = wl_resource_get_user_data (seat_resource);
  CobiwmWaylandSurface *surface = wl_resource_get_user_data (resource);
  gfloat x, y;

  if (!cobiwm_wayland_seat_get_grab_info (seat, surface, serial, TRUE, &x, &y))
    return;

  begin_grab_op_on_surface (surface, seat, grab_op_for_wl_shell_surface_resize_edge (edges), x, y);
}

typedef enum {
  SURFACE_STATE_TOPLEVEL,
  SURFACE_STATE_FULLSCREEN,
  SURFACE_STATE_MAXIMIZED,
} SurfaceState;

static void
wl_shell_surface_set_state (CobiwmWaylandSurface *surface,
                            SurfaceState        state)
{
  if (state == SURFACE_STATE_FULLSCREEN)
    cobiwm_window_make_fullscreen (surface->window);
  else
    cobiwm_window_unmake_fullscreen (surface->window);

  if (state == SURFACE_STATE_MAXIMIZED)
    cobiwm_window_maximize (surface->window, COBIWM_MAXIMIZE_BOTH);
  else
    cobiwm_window_unmaximize (surface->window, COBIWM_MAXIMIZE_BOTH);
}

static void
wl_shell_surface_set_toplevel (struct wl_client *client,
                               struct wl_resource *resource)
{
  CobiwmWaylandSurface *surface = wl_resource_get_user_data (resource);

  wl_shell_surface_set_state (surface, SURFACE_STATE_TOPLEVEL);
}

static void
wl_shell_surface_set_transient (struct wl_client *client,
                                struct wl_resource *resource,
                                struct wl_resource *parent_resource,
                                int32_t x,
                                int32_t y,
                                uint32_t flags)
{
  CobiwmWaylandSurface *parent_surf = wl_resource_get_user_data (parent_resource);
  CobiwmWaylandSurface *surface = wl_resource_get_user_data (resource);

  wl_shell_surface_set_state (surface, SURFACE_STATE_TOPLEVEL);

  cobiwm_window_set_transient_for (surface->window, parent_surf->window);
  cobiwm_window_wayland_place_relative_to (surface->window,
                                         parent_surf->window,
                                         x, y);
}

static void
wl_shell_surface_set_fullscreen (struct wl_client *client,
                                 struct wl_resource *resource,
                                 uint32_t method,
                                 uint32_t framerate,
                                 struct wl_resource *output)
{
  CobiwmWaylandSurface *surface = wl_resource_get_user_data (resource);

  wl_shell_surface_set_state (surface, SURFACE_STATE_FULLSCREEN);
}

static void
handle_wl_shell_popup_parent_destroyed (struct wl_listener *listener,
                                        void *data)
{
  CobiwmWaylandSurface *surface =
    wl_container_of (listener, surface, popup.parent_destroy_listener);

  wl_list_remove (&surface->popup.parent_destroy_listener.link);
  surface->popup.parent = NULL;
}

static void
wl_shell_surface_set_popup (struct wl_client *client,
                            struct wl_resource *resource,
                            struct wl_resource *seat_resource,
                            uint32_t serial,
                            struct wl_resource *parent_resource,
                            int32_t x,
                            int32_t y,
                            uint32_t flags)
{
  CobiwmWaylandSurface *surface = wl_resource_get_user_data (resource);
  CobiwmWaylandSurface *parent_surf = wl_resource_get_user_data (parent_resource);
  CobiwmWaylandSeat *seat = wl_resource_get_user_data (seat_resource);

  wl_shell_surface_set_state (surface, SURFACE_STATE_TOPLEVEL);

  if (!cobiwm_wayland_seat_can_popup (seat, serial))
    {
      wl_shell_surface_send_popup_done (resource);
      return;
    }

  cobiwm_window_set_transient_for (surface->window, parent_surf->window);
  cobiwm_window_wayland_place_relative_to (surface->window,
                                         parent_surf->window,
                                         x, y);

  if (!surface->popup.parent)
    {
      surface->popup.parent = parent_surf;
      surface->popup.parent_destroy_listener.notify =
        handle_wl_shell_popup_parent_destroyed;
      wl_resource_add_destroy_listener (parent_surf->resource,
                                        &surface->popup.parent_destroy_listener);
    }

  cobiwm_wayland_pointer_start_popup_grab (&seat->pointer, surface);
}

static void
wl_shell_surface_set_maximized (struct wl_client *client,
                                struct wl_resource *resource,
                                struct wl_resource *output)
{
  CobiwmWaylandSurface *surface = wl_resource_get_user_data (resource);

  wl_shell_surface_set_state (surface, SURFACE_STATE_MAXIMIZED);
}

static void
wl_shell_surface_set_title (struct wl_client *client,
                            struct wl_resource *resource,
                            const char *title)
{
  CobiwmWaylandSurface *surface = wl_resource_get_user_data (resource);

  if (!g_utf8_validate (title, -1, NULL))
    title = "";

  cobiwm_window_set_title (surface->window, title);
}

static void
wl_shell_surface_set_class (struct wl_client *client,
                            struct wl_resource *resource,
                            const char *class_)
{
  CobiwmWaylandSurface *surface = wl_resource_get_user_data (resource);

  if (!g_utf8_validate (class_, -1, NULL))
    class_ = "";

  cobiwm_window_set_wm_class (surface->window, class_, class_);
}

static const struct wl_shell_surface_interface cobiwm_wayland_wl_shell_surface_interface = {
  wl_shell_surface_pong,
  wl_shell_surface_move,
  wl_shell_surface_resize,
  wl_shell_surface_set_toplevel,
  wl_shell_surface_set_transient,
  wl_shell_surface_set_fullscreen,
  wl_shell_surface_set_popup,
  wl_shell_surface_set_maximized,
  wl_shell_surface_set_title,
  wl_shell_surface_set_class,
};

static void
wl_shell_get_shell_surface (struct wl_client *client,
                            struct wl_resource *resource,
                            uint32_t id,
                            struct wl_resource *surface_resource)
{
  CobiwmWaylandSurface *surface = wl_resource_get_user_data (surface_resource);
  CobiwmWindow *window;

  if (surface->wl_shell_surface != NULL)
    {
      wl_resource_post_error (surface_resource,
                              WL_DISPLAY_ERROR_INVALID_OBJECT,
                              "wl_shell::get_shell_surface already requested");
      return;
    }

  if (!cobiwm_wayland_surface_assign_role (surface,
                                         COBIWM_TYPE_WAYLAND_SURFACE_ROLE_WL_SHELL_SURFACE))
    {
      wl_resource_post_error (resource, WL_SHELL_ERROR_ROLE,
                              "wl_surface@%d already has a different role",
                              wl_resource_get_id (surface->resource));
      return;
    }

  surface->wl_shell_surface = wl_resource_create (client, &wl_shell_surface_interface, wl_resource_get_version (resource), id);
  wl_resource_set_implementation (surface->wl_shell_surface, &cobiwm_wayland_wl_shell_surface_interface, surface, wl_shell_surface_destructor);

  window = cobiwm_window_wayland_new (cobiwm_get_display (), surface);
  cobiwm_wayland_surface_set_window (surface, window);
}

static const struct wl_shell_interface cobiwm_wayland_wl_shell_interface = {
  wl_shell_get_shell_surface,
};

static void
bind_wl_shell (struct wl_client *client,
               void             *data,
               uint32_t          version,
               uint32_t          id)
{
  struct wl_resource *resource;

  resource = wl_resource_create (client, &wl_shell_interface, version, id);
  wl_resource_set_implementation (resource, &cobiwm_wayland_wl_shell_interface, data, NULL);
}

static void
gtk_surface_destructor (struct wl_resource *resource)
{
  CobiwmWaylandSurface *surface = wl_resource_get_user_data (resource);

  surface->gtk_surface = NULL;
}

static void
gtk_surface_set_dbus_properties (struct wl_client   *client,
                                 struct wl_resource *resource,
                                 const char         *application_id,
                                 const char         *app_menu_path,
                                 const char         *menubar_path,
                                 const char         *window_object_path,
                                 const char         *application_object_path,
                                 const char         *unique_bus_name)
{
  CobiwmWaylandSurface *surface = wl_resource_get_user_data (resource);

  /* Broken client, let it die instead of us */
  if (!surface->window)
    {
      cobiwm_warning ("cobiwm-wayland-surface: set_dbus_properties called with invalid window!\n");
      return;
    }

  cobiwm_window_set_gtk_dbus_properties (surface->window,
                                       application_id,
                                       unique_bus_name,
                                       app_menu_path,
                                       menubar_path,
                                       application_object_path,
                                       window_object_path);
}

static void
gtk_surface_set_modal (struct wl_client   *client,
                       struct wl_resource *resource)
{
  CobiwmWaylandSurface *surface = wl_resource_get_user_data (resource);

  if (surface->is_modal)
    return;

  surface->is_modal = TRUE;
  cobiwm_window_set_type (surface->window, COBIWM_WINDOW_MODAL_DIALOG);
}

static void
gtk_surface_unset_modal (struct wl_client   *client,
                         struct wl_resource *resource)
{
  CobiwmWaylandSurface *surface = wl_resource_get_user_data (resource);

  if (!surface->is_modal)
    return;

  surface->is_modal = FALSE;
  cobiwm_window_set_type (surface->window, COBIWM_WINDOW_NORMAL);
}

static void
gtk_surface_present (struct wl_client   *client,
                     struct wl_resource *resource,
                     uint32_t            timestamp)
{
  CobiwmWaylandSurface *surface = wl_resource_get_user_data (resource);
  CobiwmWindow *window = surface->window;

  if (!window)
    return;

  cobiwm_window_activate_full (window, timestamp,
                             COBIWM_CLIENT_TYPE_APPLICATION, NULL);
}

static const struct gtk_surface1_interface cobiwm_wayland_gtk_surface_interface = {
  gtk_surface_set_dbus_properties,
  gtk_surface_set_modal,
  gtk_surface_unset_modal,
  gtk_surface_present,
};

static void
gtk_shell_get_gtk_surface (struct wl_client   *client,
                           struct wl_resource *resource,
                           guint32             id,
                           struct wl_resource *surface_resource)
{
  CobiwmWaylandSurface *surface = wl_resource_get_user_data (surface_resource);

  if (surface->gtk_surface != NULL)
    {
      wl_resource_post_error (surface_resource,
                              WL_DISPLAY_ERROR_INVALID_OBJECT,
                              "gtk_shell::get_gtk_surface already requested");
      return;
    }

  surface->gtk_surface = wl_resource_create (client,
                                             &gtk_surface1_interface,
                                             wl_resource_get_version (resource),
                                             id);
  wl_resource_set_implementation (surface->gtk_surface, &cobiwm_wayland_gtk_surface_interface, surface, gtk_surface_destructor);
}

static void
gtk_shell_set_startup_id (struct wl_client   *client,
                          struct wl_resource *resource,
                          const char         *startup_id)
{
  CobiwmDisplay *display;

  display = cobiwm_get_display ();
  cobiwm_startup_notification_remove_sequence (display->startup_notification,
                                             startup_id);
}

static void
gtk_shell_system_bell (struct wl_client   *client,
                       struct wl_resource *resource,
                       struct wl_resource *gtk_surface_resource)
{
  CobiwmDisplay *display = cobiwm_get_display ();

  if (gtk_surface_resource)
    {
      CobiwmWaylandSurface *surface =
        wl_resource_get_user_data (gtk_surface_resource);

      if (!surface->window)
        return;

      cobiwm_bell_notify (display, surface->window);
    }
  else
    {
      cobiwm_bell_notify (display, NULL);
    }
}

static const struct gtk_shell1_interface cobiwm_wayland_gtk_shell_interface = {
  gtk_shell_get_gtk_surface,
  gtk_shell_set_startup_id,
  gtk_shell_system_bell,
};

static void
bind_gtk_shell (struct wl_client *client,
                void             *data,
                guint32           version,
                guint32           id)
{
  struct wl_resource *resource;
  uint32_t capabilities = 0;

  resource = wl_resource_create (client, &gtk_shell1_interface, version, id);
  wl_resource_set_implementation (resource, &cobiwm_wayland_gtk_shell_interface, data, NULL);

  if (!cobiwm_prefs_get_show_fallback_app_menu ())
    capabilities = GTK_SHELL1_CAPABILITY_GLOBAL_APP_MENU;

  gtk_shell1_send_capabilities (resource, capabilities);
}

static void
unparent_actor (CobiwmWaylandSurface *surface)
{
  ClutterActor *parent_actor;
  parent_actor = clutter_actor_get_parent (CLUTTER_ACTOR (surface->surface_actor));
  clutter_actor_remove_child (parent_actor, CLUTTER_ACTOR (surface->surface_actor));
}

static void
wl_subsurface_destructor (struct wl_resource *resource)
{
  CobiwmWaylandSurface *surface = wl_resource_get_user_data (resource);

  cobiwm_wayland_compositor_destroy_frame_callbacks (surface->compositor,
                                                   surface);
  if (surface->sub.parent)
    {
      wl_list_remove (&surface->sub.parent_destroy_listener.link);
      surface->sub.parent->subsurfaces =
        g_list_remove (surface->sub.parent->subsurfaces, surface);
      unparent_actor (surface);
      surface->sub.parent = NULL;
    }

  g_clear_object (&surface->sub.pending);
  surface->wl_subsurface = NULL;
}

static void
wl_subsurface_destroy (struct wl_client *client,
                       struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
wl_subsurface_set_position (struct wl_client *client,
                            struct wl_resource *resource,
                            int32_t x,
                            int32_t y)
{
  CobiwmWaylandSurface *surface = wl_resource_get_user_data (resource);

  surface->sub.pending_x = x;
  surface->sub.pending_y = y;
  surface->sub.pending_pos = TRUE;
}

static gboolean
is_valid_sibling (CobiwmWaylandSurface *surface, CobiwmWaylandSurface *sibling)
{
  if (surface->sub.parent == sibling)
    return TRUE;
  if (surface->sub.parent == sibling->sub.parent)
    return TRUE;
  return FALSE;
}

static void
subsurface_handle_pending_sibling_destroyed (struct wl_listener *listener, void *data)
{
  CobiwmWaylandSubsurfacePlacementOp *op =
    wl_container_of (listener, op, sibling_destroy_listener);

  op->sibling = NULL;
}

static void
queue_subsurface_placement (CobiwmWaylandSurface *surface,
                            CobiwmWaylandSurface *sibling,
                            CobiwmWaylandSubsurfacePlacement placement)
{
  CobiwmWaylandSubsurfacePlacementOp *op =
    g_slice_new (CobiwmWaylandSubsurfacePlacementOp);

  op->placement = placement;
  op->sibling = sibling;
  op->sibling_destroy_listener.notify =
    subsurface_handle_pending_sibling_destroyed;
  wl_resource_add_destroy_listener (sibling->resource,
                                    &op->sibling_destroy_listener);

  surface->sub.pending_placement_ops =
    g_slist_append (surface->sub.pending_placement_ops, op);
}

static void
wl_subsurface_place_above (struct wl_client *client,
                           struct wl_resource *resource,
                           struct wl_resource *sibling_resource)
{
  CobiwmWaylandSurface *surface = wl_resource_get_user_data (resource);
  CobiwmWaylandSurface *sibling = wl_resource_get_user_data (sibling_resource);

  if (!is_valid_sibling (surface, sibling))
    {
      wl_resource_post_error (resource, WL_SUBSURFACE_ERROR_BAD_SURFACE,
                              "wl_subsurface::place_above: wl_surface@%d is "
                              "not a valid parent or sibling",
                              wl_resource_get_id (sibling->resource));
      return;
    }

  queue_subsurface_placement (surface,
                              sibling,
                              COBIWM_WAYLAND_SUBSURFACE_PLACEMENT_ABOVE);
}

static void
wl_subsurface_place_below (struct wl_client *client,
                           struct wl_resource *resource,
                           struct wl_resource *sibling_resource)
{
  CobiwmWaylandSurface *surface = wl_resource_get_user_data (resource);
  CobiwmWaylandSurface *sibling = wl_resource_get_user_data (sibling_resource);

  if (!is_valid_sibling (surface, sibling))
    {
      wl_resource_post_error (resource, WL_SUBSURFACE_ERROR_BAD_SURFACE,
                              "wl_subsurface::place_below: wl_surface@%d is "
                              "not a valid parent or sibling",
                              wl_resource_get_id (sibling->resource));
      return;
    }

  queue_subsurface_placement (surface,
                              sibling,
                              COBIWM_WAYLAND_SUBSURFACE_PLACEMENT_BELOW);
}

static void
wl_subsurface_set_sync (struct wl_client *client,
                        struct wl_resource *resource)
{
  CobiwmWaylandSurface *surface = wl_resource_get_user_data (resource);

  surface->sub.synchronous = TRUE;
}

static void
wl_subsurface_set_desync (struct wl_client *client,
                          struct wl_resource *resource)
{
  CobiwmWaylandSurface *surface = wl_resource_get_user_data (resource);
  gboolean was_effectively_synchronized;

  was_effectively_synchronized = is_surface_effectively_synchronized (surface);
  surface->sub.synchronous = FALSE;
  if (was_effectively_synchronized &&
      !is_surface_effectively_synchronized (surface))
    apply_pending_state (surface, surface->sub.pending);
}

static const struct wl_subsurface_interface cobiwm_wayland_wl_subsurface_interface = {
  wl_subsurface_destroy,
  wl_subsurface_set_position,
  wl_subsurface_place_above,
  wl_subsurface_place_below,
  wl_subsurface_set_sync,
  wl_subsurface_set_desync,
};

static void
wl_subcompositor_destroy (struct wl_client *client,
                          struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
surface_handle_parent_surface_destroyed (struct wl_listener *listener,
                                         void *data)
{
  CobiwmWaylandSurface *surface = wl_container_of (listener,
                                                 surface,
                                                 sub.parent_destroy_listener);

  surface->sub.parent = NULL;
  unparent_actor (surface);
}

static void
wl_subcompositor_get_subsurface (struct wl_client *client,
                                 struct wl_resource *resource,
                                 guint32 id,
                                 struct wl_resource *surface_resource,
                                 struct wl_resource *parent_resource)
{
  CobiwmWaylandSurface *surface = wl_resource_get_user_data (surface_resource);
  CobiwmWaylandSurface *parent = wl_resource_get_user_data (parent_resource);

  if (surface->wl_subsurface != NULL)
    {
      wl_resource_post_error (surface_resource,
                              WL_DISPLAY_ERROR_INVALID_OBJECT,
                              "wl_subcompositor::get_subsurface already requested");
      return;
    }

  if (!cobiwm_wayland_surface_assign_role (surface,
                                         COBIWM_TYPE_WAYLAND_SURFACE_ROLE_SUBSURFACE))
    {
      /* FIXME: There is no subcompositor "role" error yet, so lets just use something
       * similar until there is.
       */
      wl_resource_post_error (resource, WL_SHELL_ERROR_ROLE,
                              "wl_surface@%d already has a different role",
                              wl_resource_get_id (surface->resource));
      return;
    }

  surface->wl_subsurface = wl_resource_create (client, &wl_subsurface_interface, wl_resource_get_version (resource), id);
  wl_resource_set_implementation (surface->wl_subsurface, &cobiwm_wayland_wl_subsurface_interface, surface, wl_subsurface_destructor);

  surface->sub.pending = g_object_new (COBIWM_TYPE_WAYLAND_PENDING_STATE, NULL);
  surface->sub.synchronous = TRUE;
  surface->sub.parent = parent;
  surface->sub.parent_destroy_listener.notify = surface_handle_parent_surface_destroyed;
  wl_resource_add_destroy_listener (parent->resource, &surface->sub.parent_destroy_listener);
  parent->subsurfaces = g_list_append (parent->subsurfaces, surface);

  clutter_actor_add_child (CLUTTER_ACTOR (parent->surface_actor),
                           CLUTTER_ACTOR (surface->surface_actor));

  sync_reactive (surface);
}

static const struct wl_subcompositor_interface cobiwm_wayland_subcompositor_interface = {
  wl_subcompositor_destroy,
  wl_subcompositor_get_subsurface,
};

static void
bind_subcompositor (struct wl_client *client,
                    void             *data,
                    guint32           version,
                    guint32           id)
{
  struct wl_resource *resource;

  resource = wl_resource_create (client, &wl_subcompositor_interface, version, id);
  wl_resource_set_implementation (resource, &cobiwm_wayland_subcompositor_interface, data, NULL);
}

void
cobiwm_wayland_shell_init (CobiwmWaylandCompositor *compositor)
{
  if (wl_global_create (compositor->wayland_display,
                        &xdg_shell_interface,
                        COBIWM_XDG_SHELL_VERSION,
                        compositor, bind_xdg_shell) == NULL)
    g_error ("Failed to register a global xdg-shell object");

  if (wl_global_create (compositor->wayland_display,
                        &wl_shell_interface,
                        COBIWM_WL_SHELL_VERSION,
                        compositor, bind_wl_shell) == NULL)
    g_error ("Failed to register a global wl-shell object");

  if (wl_global_create (compositor->wayland_display,
                        &gtk_shell1_interface,
                        COBIWM_GTK_SHELL1_VERSION,
                        compositor, bind_gtk_shell) == NULL)
    g_error ("Failed to register a global gtk-shell object");

  if (wl_global_create (compositor->wayland_display,
                        &wl_subcompositor_interface,
                        COBIWM_WL_SUBCOMPOSITOR_VERSION,
                        compositor, bind_subcompositor) == NULL)
    g_error ("Failed to register a global wl-subcompositor object");
}

static void
fill_states (struct wl_array *states, CobiwmWindow *window)
{
  uint32_t *s;

  if (COBIWM_WINDOW_MAXIMIZED (window))
    {
      s = wl_array_add (states, sizeof *s);
      *s = XDG_SURFACE_STATE_MAXIMIZED;
    }
  if (cobiwm_window_is_fullscreen (window))
    {
      s = wl_array_add (states, sizeof *s);
      *s = XDG_SURFACE_STATE_FULLSCREEN;
    }
  if (cobiwm_grab_op_is_resizing (window->display->grab_op))
    {
      s = wl_array_add (states, sizeof *s);
      *s = XDG_SURFACE_STATE_RESIZING;
    }
  if (cobiwm_window_appears_focused (window))
    {
      s = wl_array_add (states, sizeof *s);
      *s = XDG_SURFACE_STATE_ACTIVATED;
    }
}

void
cobiwm_wayland_surface_configure_notify (CobiwmWaylandSurface *surface,
                                       int                 new_width,
                                       int                 new_height,
                                       CobiwmWaylandSerial  *sent_serial)
{
  if (surface->xdg_surface)
    {
      struct wl_client *client = wl_resource_get_client (surface->xdg_surface);
      struct wl_display *display = wl_client_get_display (client);
      uint32_t serial = wl_display_next_serial (display);
      struct wl_array states;

      wl_array_init (&states);
      fill_states (&states, surface->window);

      xdg_surface_send_configure (surface->xdg_surface, new_width, new_height, &states, serial);

      wl_array_release (&states);

      if (sent_serial)
        {
          sent_serial->set = TRUE;
          sent_serial->value = serial;
        }
    }
  else if (surface->xdg_popup)
    {
      /* This can happen if the popup window loses or receives focus.
       * Just ignore it. */
    }
  else if (surface->wl_shell_surface)
    wl_shell_surface_send_configure (surface->wl_shell_surface,
                                     0, new_width, new_height);
  else
    g_assert_not_reached ();
}

void
cobiwm_wayland_surface_ping (CobiwmWaylandSurface *surface,
                           guint32             serial)
{
  if (surface->xdg_shell_resource)
    xdg_shell_send_ping (surface->xdg_shell_resource, serial);
  else if (surface->wl_shell_surface)
    wl_shell_surface_send_ping (surface->wl_shell_surface, serial);
}

void
cobiwm_wayland_surface_delete (CobiwmWaylandSurface *surface)
{
  if (surface->xdg_surface)
    xdg_surface_send_close (surface->xdg_surface);
}

void
cobiwm_wayland_surface_popup_done (CobiwmWaylandSurface *surface)
{
  if (surface->xdg_popup)
    xdg_popup_send_popup_done (surface->xdg_popup);
  else if (surface->wl_shell_surface)
    wl_shell_surface_send_popup_done (surface->wl_shell_surface);
}

void
cobiwm_wayland_surface_drag_dest_focus_in (CobiwmWaylandSurface   *surface,
                                         CobiwmWaylandDataOffer *offer)
{
  CobiwmWaylandCompositor *compositor = cobiwm_wayland_compositor_get_default ();
  CobiwmWaylandDataDevice *data_device = &compositor->seat->data_device;

  surface->dnd.funcs->focus_in (data_device, surface, offer);
}

void
cobiwm_wayland_surface_drag_dest_motion (CobiwmWaylandSurface *surface,
                                       const ClutterEvent *event)
{
  CobiwmWaylandCompositor *compositor = cobiwm_wayland_compositor_get_default ();
  CobiwmWaylandDataDevice *data_device = &compositor->seat->data_device;

  surface->dnd.funcs->motion (data_device, surface, event);
}

void
cobiwm_wayland_surface_drag_dest_focus_out (CobiwmWaylandSurface *surface)
{
  CobiwmWaylandCompositor *compositor = cobiwm_wayland_compositor_get_default ();
  CobiwmWaylandDataDevice *data_device = &compositor->seat->data_device;

  surface->dnd.funcs->focus_out (data_device, surface);
}

void
cobiwm_wayland_surface_drag_dest_drop (CobiwmWaylandSurface *surface)
{
  CobiwmWaylandCompositor *compositor = cobiwm_wayland_compositor_get_default ();
  CobiwmWaylandDataDevice *data_device = &compositor->seat->data_device;

  surface->dnd.funcs->drop (data_device, surface);
}

void
cobiwm_wayland_surface_drag_dest_update (CobiwmWaylandSurface *surface)
{
  CobiwmWaylandCompositor *compositor = cobiwm_wayland_compositor_get_default ();
  CobiwmWaylandDataDevice *data_device = &compositor->seat->data_device;

  surface->dnd.funcs->update (data_device, surface);
}

CobiwmWindow *
cobiwm_wayland_surface_get_toplevel_window (CobiwmWaylandSurface *surface)
{
  while (surface)
    {
      if (surface->window)
        {
          if (surface->popup.parent)
            surface = surface->popup.parent;
          else
            return surface->window;
        }
      else
        surface = surface->sub.parent;
    }

  return NULL;
}

void
cobiwm_wayland_surface_get_relative_coordinates (CobiwmWaylandSurface *surface,
                                               float               abs_x,
                                               float               abs_y,
                                               float               *sx,
                                               float               *sy)
{
  ClutterActor *actor =
    CLUTTER_ACTOR (cobiwm_surface_actor_get_texture (surface->surface_actor));

  clutter_actor_transform_stage_point (actor, abs_x, abs_y, sx, sy);
  *sx /= surface->scale;
  *sy /= surface->scale;
}

void
cobiwm_wayland_surface_get_absolute_coordinates (CobiwmWaylandSurface *surface,
                                               float               sx,
                                               float               sy,
                                               float               *x,
                                               float               *y)
{
  ClutterActor *actor =
    CLUTTER_ACTOR (cobiwm_surface_actor_get_texture (surface->surface_actor));
  ClutterVertex sv = {
    .x = sx * surface->scale,
    .y = sy * surface->scale,
  };
  ClutterVertex v = { 0 };

  clutter_actor_apply_relative_transform_to_point (actor, NULL, &sv, &v);

  *x = v.x;
  *y = v.y;
}

static void
cobiwm_wayland_surface_init (CobiwmWaylandSurface *surface)
{
  surface->pending = g_object_new (COBIWM_TYPE_WAYLAND_PENDING_STATE, NULL);
}

static void
cobiwm_wayland_surface_class_init (CobiwmWaylandSurfaceClass *klass)
{
}

static void
cobiwm_wayland_surface_role_init (CobiwmWaylandSurfaceRole *role)
{
}

static void
cobiwm_wayland_surface_role_class_init (CobiwmWaylandSurfaceRoleClass *klass)
{
}

static void
cobiwm_wayland_surface_role_assigned (CobiwmWaylandSurfaceRole *surface_role)
{
  COBIWM_WAYLAND_SURFACE_ROLE_GET_CLASS (surface_role)->assigned (surface_role);
}

static void
cobiwm_wayland_surface_role_pre_commit (CobiwmWaylandSurfaceRole  *surface_role,
                                      CobiwmWaylandPendingState *pending)
{
  CobiwmWaylandSurfaceRoleClass *klass;

  klass = COBIWM_WAYLAND_SURFACE_ROLE_GET_CLASS (surface_role);
  if (klass->pre_commit)
    klass->pre_commit (surface_role, pending);
}

static void
cobiwm_wayland_surface_role_commit (CobiwmWaylandSurfaceRole  *surface_role,
                                  CobiwmWaylandPendingState *pending)
{
  COBIWM_WAYLAND_SURFACE_ROLE_GET_CLASS (surface_role)->commit (surface_role,
                                                              pending);
}

static gboolean
cobiwm_wayland_surface_role_is_on_output (CobiwmWaylandSurfaceRole *surface_role,
                                        CobiwmMonitorInfo        *monitor)
{
  CobiwmWaylandSurfaceRoleClass *klass;

  klass = COBIWM_WAYLAND_SURFACE_ROLE_GET_CLASS (surface_role);
  if (klass->is_on_output)
    return klass->is_on_output (surface_role, monitor);
  else
    return FALSE;
}

CobiwmWaylandSurface *
cobiwm_wayland_surface_role_get_surface (CobiwmWaylandSurfaceRole *role)
{
  CobiwmWaylandSurfaceRolePrivate *priv =
    cobiwm_wayland_surface_role_get_instance_private (role);

  return priv->surface;
}

void
cobiwm_wayland_surface_queue_pending_frame_callbacks (CobiwmWaylandSurface *surface)
{
  wl_list_insert_list (&surface->compositor->frame_callbacks,
                       &surface->pending_frame_callback_list);
  wl_list_init (&surface->pending_frame_callback_list);
}

static void
default_role_assigned (CobiwmWaylandSurfaceRole *surface_role)
{
  CobiwmWaylandSurface *surface =
    cobiwm_wayland_surface_role_get_surface (surface_role);

  cobiwm_wayland_surface_queue_pending_frame_callbacks (surface);
}

static void
actor_surface_assigned (CobiwmWaylandSurfaceRole *surface_role)
{
  CobiwmWaylandSurface *surface =
    cobiwm_wayland_surface_role_get_surface (surface_role);
  CobiwmSurfaceActorWayland *surface_actor =
    COBIWM_SURFACE_ACTOR_WAYLAND (surface->surface_actor);

  cobiwm_surface_actor_wayland_add_frame_callbacks (surface_actor,
                                                  &surface->pending_frame_callback_list);
  wl_list_init (&surface->pending_frame_callback_list);
}

static void
cobiwm_wayland_surface_role_dnd_init (CobiwmWaylandSurfaceRoleDND *role)
{
}

static void
cobiwm_wayland_surface_role_dnd_class_init (CobiwmWaylandSurfaceRoleDNDClass *klass)
{
  CobiwmWaylandSurfaceRoleClass *surface_role_class =
    COBIWM_WAYLAND_SURFACE_ROLE_CLASS (klass);

  surface_role_class->assigned = default_role_assigned;
  surface_role_class->commit = dnd_surface_commit;
}

static void
cobiwm_wayland_surface_role_xdg_surface_init (CobiwmWaylandSurfaceRoleXdgSurface *role)
{
}

static void
cobiwm_wayland_surface_role_xdg_surface_class_init (CobiwmWaylandSurfaceRoleXdgSurfaceClass *klass)
{
  CobiwmWaylandSurfaceRoleClass *surface_role_class =
    COBIWM_WAYLAND_SURFACE_ROLE_CLASS (klass);

  surface_role_class->assigned = actor_surface_assigned;
  surface_role_class->commit = toplevel_surface_commit;
  surface_role_class->is_on_output = actor_surface_is_on_output;
}

static void
cobiwm_wayland_surface_role_xdg_popup_init (CobiwmWaylandSurfaceRoleXdgPopup *role)
{
}

static void
cobiwm_wayland_surface_role_xdg_popup_class_init (CobiwmWaylandSurfaceRoleXdgPopupClass *klass)
{
  CobiwmWaylandSurfaceRoleClass *surface_role_class =
    COBIWM_WAYLAND_SURFACE_ROLE_CLASS (klass);

  surface_role_class->assigned = actor_surface_assigned;
  surface_role_class->commit = toplevel_surface_commit;
  surface_role_class->is_on_output = actor_surface_is_on_output;
}

static void
cobiwm_wayland_surface_role_wl_shell_surface_init (CobiwmWaylandSurfaceRoleWlShellSurface *role)
{
}

static void
cobiwm_wayland_surface_role_wl_shell_surface_class_init (CobiwmWaylandSurfaceRoleWlShellSurfaceClass *klass)
{
  CobiwmWaylandSurfaceRoleClass *surface_role_class =
    COBIWM_WAYLAND_SURFACE_ROLE_CLASS (klass);

  surface_role_class->assigned = actor_surface_assigned;
  surface_role_class->commit = toplevel_surface_commit;
  surface_role_class->is_on_output = actor_surface_is_on_output;
}

static void
cobiwm_wayland_surface_role_subsurface_init (CobiwmWaylandSurfaceRoleSubsurface *role)
{
}

static void
cobiwm_wayland_surface_role_subsurface_class_init (CobiwmWaylandSurfaceRoleSubsurfaceClass *klass)
{
  CobiwmWaylandSurfaceRoleClass *surface_role_class =
    COBIWM_WAYLAND_SURFACE_ROLE_CLASS (klass);

  surface_role_class->assigned = actor_surface_assigned;
  surface_role_class->commit = subsurface_surface_commit;
  surface_role_class->is_on_output = actor_surface_is_on_output;
}

cairo_region_t *
cobiwm_wayland_surface_calculate_input_region (CobiwmWaylandSurface *surface)
{
  cairo_region_t *region;
  cairo_rectangle_int_t buffer_rect;
  CoglTexture *texture;

  if (!surface->buffer_ref.buffer)
    return NULL;

  texture = surface->buffer_ref.buffer->texture;
  buffer_rect = (cairo_rectangle_int_t) {
    .width = cogl_texture_get_width (texture) / surface->scale,
    .height = cogl_texture_get_height (texture) / surface->scale,
  };
  region = cairo_region_create_rectangle (&buffer_rect);

  if (surface->input_region)
    cairo_region_intersect (region, surface->input_region);

  return region;
}
