/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2013 Red Hat
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
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

#include "config.h"

#include "cobiwm-surface-actor-wayland.h"

#include <math.h>
#include <cogl/cogl-wayland-server.h>
#include "cobiwm-shaped-texture-private.h"

#include "wayland/cobiwm-wayland-buffer.h"
#include "wayland/cobiwm-wayland-private.h"
#include "wayland/cobiwm-window-wayland.h"

#include "compositor/region-utils.h"

enum {
  PAINTING,

  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

struct _CobiwmSurfaceActorWaylandPrivate
{
  CobiwmWaylandSurface *surface;
  struct wl_list frame_callback_list;
};
typedef struct _CobiwmSurfaceActorWaylandPrivate CobiwmSurfaceActorWaylandPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (CobiwmSurfaceActorWayland, cobiwm_surface_actor_wayland, COBIWM_TYPE_SURFACE_ACTOR)

static void
cobiwm_surface_actor_wayland_process_damage (CobiwmSurfaceActor *actor,
                                           int x, int y, int width, int height)
{
}

static void
cobiwm_surface_actor_wayland_pre_paint (CobiwmSurfaceActor *actor)
{
}

static gboolean
cobiwm_surface_actor_wayland_is_visible (CobiwmSurfaceActor *actor)
{
  /* TODO: ensure that the buffer isn't NULL, implement
   * wayland mapping semantics */
  return TRUE;
}

static gboolean
cobiwm_surface_actor_wayland_should_unredirect (CobiwmSurfaceActor *actor)
{
  return FALSE;
}

static void
cobiwm_surface_actor_wayland_set_unredirected (CobiwmSurfaceActor *actor,
                                             gboolean          unredirected)
{
  /* Do nothing. In the future, we'll use KMS to set this
   * up as a hardware overlay or something. */
}

static gboolean
cobiwm_surface_actor_wayland_is_unredirected (CobiwmSurfaceActor *actor)
{
  return FALSE;
}

double
cobiwm_surface_actor_wayland_get_scale (CobiwmSurfaceActorWayland *actor)
{
   CobiwmSurfaceActorWaylandPrivate *priv = cobiwm_surface_actor_wayland_get_instance_private (actor);
   CobiwmWaylandSurface *surface = priv->surface;
   CobiwmWindow *window;
   int output_scale = 1;

   if (!surface)
     return 1;

   window = cobiwm_wayland_surface_get_toplevel_window (surface);

   /* XXX: We do not handle x11 clients yet */
   if (window && window->client_type != COBIWM_WINDOW_CLIENT_TYPE_X11)
     output_scale = cobiwm_window_wayland_get_main_monitor_scale (window);

   return (double)output_scale / (double)priv->surface->scale;
}

static void
logical_to_actor_position (CobiwmSurfaceActorWayland *self,
                           int                     *x,
                           int                     *y)
{
  CobiwmWaylandSurface *surface = cobiwm_surface_actor_wayland_get_surface (self);
  CobiwmWindow *toplevel_window;
  int monitor_scale = 1;

  toplevel_window = cobiwm_wayland_surface_get_toplevel_window (surface);
  if (toplevel_window)
    monitor_scale = cobiwm_window_wayland_get_main_monitor_scale (toplevel_window);

  *x = *x * monitor_scale;
  *y = *y * monitor_scale;
}

/* Convert the current actor state to the corresponding subsurface rectangle
 * in logical pixel coordinate space. */
void
cobiwm_surface_actor_wayland_get_subsurface_rect (CobiwmSurfaceActorWayland *self,
                                                CobiwmRectangle           *rect)
{
  CobiwmWaylandSurface *surface = cobiwm_surface_actor_wayland_get_surface (self);
  CobiwmWaylandBuffer *buffer = cobiwm_wayland_surface_get_buffer (surface);
  CoglTexture *texture = buffer->texture;
  CobiwmWindow *toplevel_window;
  int monitor_scale;
  float x, y;

  toplevel_window = cobiwm_wayland_surface_get_toplevel_window (surface);
  monitor_scale = cobiwm_window_wayland_get_main_monitor_scale (toplevel_window);

  clutter_actor_get_position (CLUTTER_ACTOR (self), &x, &y);
  *rect = (CobiwmRectangle) {
    .x = x / monitor_scale,
    .y = y / monitor_scale,
    .width = cogl_texture_get_width (texture) / surface->scale,
    .height = cogl_texture_get_height (texture) / surface->scale,
  };
}

void
cobiwm_surface_actor_wayland_sync_subsurface_state (CobiwmSurfaceActorWayland *self)
{
  CobiwmWaylandSurface *surface = cobiwm_surface_actor_wayland_get_surface (self);
  CobiwmWindow *window;
  int x = surface->offset_x + surface->sub.x;
  int y = surface->offset_y + surface->sub.y;

  window = cobiwm_wayland_surface_get_toplevel_window (surface);
  if (window && window->client_type == COBIWM_WINDOW_CLIENT_TYPE_X11)
    {
      /* Bail directly if this is part of a Xwayland window and warn
       * if there happen to be offsets anyway since that is not supposed
       * to happen. */
      g_warn_if_fail (x == 0 && y == 0);
      return;
    }

  logical_to_actor_position (self, &x, &y);
  clutter_actor_set_position (CLUTTER_ACTOR (self), x, y);
}

void
cobiwm_surface_actor_wayland_sync_state (CobiwmSurfaceActorWayland *self)
{
  CobiwmWaylandSurface *surface = cobiwm_surface_actor_wayland_get_surface (self);
  CobiwmShapedTexture *stex =
    cobiwm_surface_actor_get_texture (COBIWM_SURFACE_ACTOR (self));
  double texture_scale;

  /* Given the surface's window type and what output the surface actor has the
   * largest region, scale the actor with the determined scale. */
  texture_scale = cobiwm_surface_actor_wayland_get_scale (self);

  /* Actor scale. */
  clutter_actor_set_scale (CLUTTER_ACTOR (stex), texture_scale, texture_scale);

  /* Input region */
  if (surface->input_region)
    {
      cairo_region_t *scaled_input_region;
      int region_scale;

      /* The input region from the Wayland surface is in the Wayland surface
       * coordinate space, while the surface actor input region is in the
       * physical pixel coordinate space. */
      region_scale = (int)(surface->scale * texture_scale);
      scaled_input_region = cobiwm_region_scale (surface->input_region,
                                               region_scale);
      cobiwm_surface_actor_set_input_region (COBIWM_SURFACE_ACTOR (self),
                                           scaled_input_region);
      cairo_region_destroy (scaled_input_region);
    }
  else
    {
      cobiwm_surface_actor_set_input_region (COBIWM_SURFACE_ACTOR (self), NULL);
    }

  /* Opaque region */
  if (surface->opaque_region)
    {
      cairo_region_t *scaled_opaque_region;

      /* The opaque region from the Wayland surface is in Wayland surface
       * coordinate space, while the surface actor opaque region is in the
       * same coordinate space as the unscaled buffer texture. */
      scaled_opaque_region = cobiwm_region_scale (surface->opaque_region,
                                                surface->scale);
      cobiwm_surface_actor_set_opaque_region (COBIWM_SURFACE_ACTOR (self),
                                            scaled_opaque_region);
      cairo_region_destroy (scaled_opaque_region);
    }
  else
    {
      cobiwm_surface_actor_set_opaque_region (COBIWM_SURFACE_ACTOR (self), NULL);
    }

  cobiwm_surface_actor_wayland_sync_subsurface_state (self);
}

void
cobiwm_surface_actor_wayland_sync_state_recursive (CobiwmSurfaceActorWayland *self)
{
  CobiwmWaylandSurface *surface = cobiwm_surface_actor_wayland_get_surface (self);
  CobiwmWindow *window = cobiwm_wayland_surface_get_toplevel_window (surface);
  GList *iter;

  cobiwm_surface_actor_wayland_sync_state (self);

  if (window && window->client_type != COBIWM_WINDOW_CLIENT_TYPE_X11)
    {
      for (iter = surface->subsurfaces; iter != NULL; iter = iter->next)
        {
          CobiwmWaylandSurface *subsurf = iter->data;

          cobiwm_surface_actor_wayland_sync_state_recursive (
            COBIWM_SURFACE_ACTOR_WAYLAND (subsurf->surface_actor));
        }
    }
}

gboolean
cobiwm_surface_actor_wayland_is_on_monitor (CobiwmSurfaceActorWayland *self,
                                          CobiwmMonitorInfo         *monitor)
{
  float x, y, width, height;
  cairo_rectangle_int_t actor_rect;
  cairo_region_t *region;
  gboolean is_on_monitor;

  clutter_actor_get_transformed_position (CLUTTER_ACTOR (self), &x, &y);
  clutter_actor_get_transformed_size (CLUTTER_ACTOR (self), &width, &height);

  actor_rect.x = (int)roundf (x);
  actor_rect.y = (int)roundf (y);
  actor_rect.width = (int)roundf (x + width) - actor_rect.x;
  actor_rect.height = (int)roundf (y + height) - actor_rect.y;

  /* Calculate the scaled surface actor region. */
  region = cairo_region_create_rectangle (&actor_rect);

  cairo_region_intersect_rectangle (region,
				    &((cairo_rectangle_int_t) {
				      .x = monitor->rect.x,
				      .y = monitor->rect.y,
				      .width = monitor->rect.width,
				      .height = monitor->rect.height,
				    }));

  is_on_monitor = !cairo_region_is_empty (region);
  cairo_region_destroy (region);

  return is_on_monitor;
}

void
cobiwm_surface_actor_wayland_add_frame_callbacks (CobiwmSurfaceActorWayland *self,
                                                struct wl_list *frame_callbacks)
{
  CobiwmSurfaceActorWaylandPrivate *priv = cobiwm_surface_actor_wayland_get_instance_private (self);

  wl_list_insert_list (&priv->frame_callback_list, frame_callbacks);
}

static CobiwmWindow *
cobiwm_surface_actor_wayland_get_window (CobiwmSurfaceActor *actor)
{
  CobiwmSurfaceActorWaylandPrivate *priv = cobiwm_surface_actor_wayland_get_instance_private (COBIWM_SURFACE_ACTOR_WAYLAND (actor));
  CobiwmWaylandSurface *surface = priv->surface;

  if (!surface)
    return NULL;

  return surface->window;
}

static void
cobiwm_surface_actor_wayland_get_preferred_width  (ClutterActor *self,
                                                 gfloat        for_height,
                                                 gfloat       *min_width_p,
                                                 gfloat       *natural_width_p)
{
  CobiwmShapedTexture *stex = cobiwm_surface_actor_get_texture (COBIWM_SURFACE_ACTOR (self));
  double scale = cobiwm_surface_actor_wayland_get_scale (COBIWM_SURFACE_ACTOR_WAYLAND (self));

  clutter_actor_get_preferred_width (CLUTTER_ACTOR (stex), for_height, min_width_p, natural_width_p);

  if (min_width_p)
     *min_width_p *= scale;

  if (natural_width_p)
    *natural_width_p *= scale;
}

static void
cobiwm_surface_actor_wayland_get_preferred_height  (ClutterActor *self,
                                                  gfloat        for_width,
                                                  gfloat       *min_height_p,
                                                  gfloat       *natural_height_p)
{
  CobiwmShapedTexture *stex = cobiwm_surface_actor_get_texture (COBIWM_SURFACE_ACTOR (self));
  double scale = cobiwm_surface_actor_wayland_get_scale (COBIWM_SURFACE_ACTOR_WAYLAND (self));

  clutter_actor_get_preferred_height (CLUTTER_ACTOR (stex), for_width, min_height_p, natural_height_p);

  if (min_height_p)
     *min_height_p *= scale;

  if (natural_height_p)
    *natural_height_p *= scale;
}

static void
cobiwm_surface_actor_wayland_paint (ClutterActor *actor)
{
  CobiwmSurfaceActorWayland *self = COBIWM_SURFACE_ACTOR_WAYLAND (actor);
  CobiwmSurfaceActorWaylandPrivate *priv =
    cobiwm_surface_actor_wayland_get_instance_private (self);

  if (priv->surface)
    {
      CobiwmWaylandCompositor *compositor = priv->surface->compositor;

      wl_list_insert_list (&compositor->frame_callbacks, &priv->frame_callback_list);
      wl_list_init (&priv->frame_callback_list);
    }

  g_signal_emit (actor, signals[PAINTING], 0);

  CLUTTER_ACTOR_CLASS (cobiwm_surface_actor_wayland_parent_class)->paint (actor);
}

static void
cobiwm_surface_actor_wayland_dispose (GObject *object)
{
  CobiwmSurfaceActorWayland *self = COBIWM_SURFACE_ACTOR_WAYLAND (object);

  cobiwm_surface_actor_wayland_set_texture (self, NULL);

  G_OBJECT_CLASS (cobiwm_surface_actor_wayland_parent_class)->dispose (object);
}

static void
cobiwm_surface_actor_wayland_class_init (CobiwmSurfaceActorWaylandClass *klass)
{
  CobiwmSurfaceActorClass *surface_actor_class = COBIWM_SURFACE_ACTOR_CLASS (klass);
  ClutterActorClass *actor_class = CLUTTER_ACTOR_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  actor_class->get_preferred_width = cobiwm_surface_actor_wayland_get_preferred_width;
  actor_class->get_preferred_height = cobiwm_surface_actor_wayland_get_preferred_height;
  actor_class->paint = cobiwm_surface_actor_wayland_paint;

  surface_actor_class->process_damage = cobiwm_surface_actor_wayland_process_damage;
  surface_actor_class->pre_paint = cobiwm_surface_actor_wayland_pre_paint;
  surface_actor_class->is_visible = cobiwm_surface_actor_wayland_is_visible;

  surface_actor_class->should_unredirect = cobiwm_surface_actor_wayland_should_unredirect;
  surface_actor_class->set_unredirected = cobiwm_surface_actor_wayland_set_unredirected;
  surface_actor_class->is_unredirected = cobiwm_surface_actor_wayland_is_unredirected;

  surface_actor_class->get_window = cobiwm_surface_actor_wayland_get_window;

  object_class->dispose = cobiwm_surface_actor_wayland_dispose;

  signals[PAINTING] = g_signal_new ("painting",
                                    G_TYPE_FROM_CLASS (object_class),
                                    G_SIGNAL_RUN_LAST,
                                    0,
                                    NULL, NULL, NULL,
                                    G_TYPE_NONE, 0);
}

static void
cobiwm_surface_actor_wayland_init (CobiwmSurfaceActorWayland *self)
{
}

CobiwmSurfaceActor *
cobiwm_surface_actor_wayland_new (CobiwmWaylandSurface *surface)
{
  CobiwmSurfaceActorWayland *self = g_object_new (COBIWM_TYPE_SURFACE_ACTOR_WAYLAND, NULL);
  CobiwmSurfaceActorWaylandPrivate *priv = cobiwm_surface_actor_wayland_get_instance_private (self);

  g_assert (cobiwm_is_wayland_compositor ());

  wl_list_init (&priv->frame_callback_list);
  priv->surface = surface;

  return COBIWM_SURFACE_ACTOR (self);
}

void
cobiwm_surface_actor_wayland_set_texture (CobiwmSurfaceActorWayland *self,
                                        CoglTexture *texture)
{
  CobiwmShapedTexture *stex = cobiwm_surface_actor_get_texture (COBIWM_SURFACE_ACTOR (self));
  cobiwm_shaped_texture_set_texture (stex, texture);
}

CobiwmWaylandSurface *
cobiwm_surface_actor_wayland_get_surface (CobiwmSurfaceActorWayland *self)
{
  CobiwmSurfaceActorWaylandPrivate *priv = cobiwm_surface_actor_wayland_get_instance_private (self);
  return priv->surface;
}

void
cobiwm_surface_actor_wayland_surface_destroyed (CobiwmSurfaceActorWayland *self)
{
  CobiwmWaylandFrameCallback *callback, *next;
  CobiwmSurfaceActorWaylandPrivate *priv =
    cobiwm_surface_actor_wayland_get_instance_private (self);

  wl_list_for_each_safe (callback, next, &priv->frame_callback_list, link)
    {
      wl_resource_destroy (callback->resource);
    }

  priv->surface = NULL;
}
