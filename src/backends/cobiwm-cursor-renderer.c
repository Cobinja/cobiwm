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
 * Written by:
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

#include "config.h"

#include "cobiwm-cursor-renderer.h"

#include <cobiwm-backend.h>
#include <util.h>
#include <math.h>

#include <cogl/cogl.h>
#include <clutter/clutter.h>

#include "cobiwm-stage.h"

struct _CobiwmCursorRendererPrivate
{
  int current_x, current_y;

  CobiwmCursorSprite *displayed_cursor;
  gboolean handled_by_backend;
};
typedef struct _CobiwmCursorRendererPrivate CobiwmCursorRendererPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (CobiwmCursorRenderer, cobiwm_cursor_renderer, G_TYPE_OBJECT);

static void
queue_redraw (CobiwmCursorRenderer *renderer,
              CobiwmCursorSprite   *cursor_sprite)
{
  CobiwmCursorRendererPrivate *priv = cobiwm_cursor_renderer_get_instance_private (renderer);
  CobiwmBackend *backend = cobiwm_get_backend ();
  ClutterActor *stage = cobiwm_backend_get_stage (backend);
  CoglTexture *texture;
  CobiwmRectangle rect = { 0 };

  if (cursor_sprite)
    rect = cobiwm_cursor_renderer_calculate_rect (renderer, cursor_sprite);

  /* During early initialization, we can have no stage */
  if (!stage)
    return;

  if (cursor_sprite && !priv->handled_by_backend)
    texture = cobiwm_cursor_sprite_get_cogl_texture (cursor_sprite);
  else
    texture = NULL;

  cobiwm_stage_set_cursor (COBIWM_STAGE (stage), texture, &rect);
}

static gboolean
cobiwm_cursor_renderer_real_update_cursor (CobiwmCursorRenderer *renderer,
                                         CobiwmCursorSprite   *cursor_sprite)
{
  return FALSE;
}

static void
cobiwm_cursor_renderer_class_init (CobiwmCursorRendererClass *klass)
{
  klass->update_cursor = cobiwm_cursor_renderer_real_update_cursor;
}

static void
cobiwm_cursor_renderer_init (CobiwmCursorRenderer *renderer)
{
}

CobiwmRectangle
cobiwm_cursor_renderer_calculate_rect (CobiwmCursorRenderer *renderer,
                                     CobiwmCursorSprite   *cursor_sprite)
{
  CobiwmCursorRendererPrivate *priv =
    cobiwm_cursor_renderer_get_instance_private (renderer);
  CoglTexture *texture;
  int hot_x, hot_y;
  int width, height;
  float texture_scale;

  texture = cobiwm_cursor_sprite_get_cogl_texture (cursor_sprite);
  if (!texture)
    return (CobiwmRectangle) { 0 };

  cobiwm_cursor_sprite_get_hotspot (cursor_sprite, &hot_x, &hot_y);
  texture_scale = cobiwm_cursor_sprite_get_texture_scale (cursor_sprite);
  width = cogl_texture_get_width (texture);
  height = cogl_texture_get_height (texture);

  return (CobiwmRectangle) {
    .x = (int)roundf (priv->current_x - (hot_x * texture_scale)),
    .y = (int)roundf (priv->current_y - (hot_y * texture_scale)),
    .width = (int)roundf (width * texture_scale),
    .height = (int)roundf (height * texture_scale),
  };
}

static void
update_cursor (CobiwmCursorRenderer *renderer,
               CobiwmCursorSprite   *cursor_sprite)
{
  CobiwmCursorRendererPrivate *priv = cobiwm_cursor_renderer_get_instance_private (renderer);
  gboolean handled_by_backend;
  gboolean should_redraw = FALSE;

  if (cursor_sprite)
    cobiwm_cursor_sprite_prepare_at (cursor_sprite,
                                   priv->current_x,
                                   priv->current_y);

  handled_by_backend =
    COBIWM_CURSOR_RENDERER_GET_CLASS (renderer)->update_cursor (renderer,
                                                              cursor_sprite);
  if (handled_by_backend != priv->handled_by_backend)
    {
      priv->handled_by_backend = handled_by_backend;
      should_redraw = TRUE;
    }

  if (!handled_by_backend)
    should_redraw = TRUE;

  if (should_redraw)
    queue_redraw (renderer, cursor_sprite);
}

CobiwmCursorRenderer *
cobiwm_cursor_renderer_new (void)
{
  return g_object_new (COBIWM_TYPE_CURSOR_RENDERER, NULL);
}

void
cobiwm_cursor_renderer_set_cursor (CobiwmCursorRenderer *renderer,
                                 CobiwmCursorSprite   *cursor_sprite)
{
  CobiwmCursorRendererPrivate *priv = cobiwm_cursor_renderer_get_instance_private (renderer);

  if (priv->displayed_cursor == cursor_sprite)
    return;
  priv->displayed_cursor = cursor_sprite;

  update_cursor (renderer, cursor_sprite);
}

void
cobiwm_cursor_renderer_force_update (CobiwmCursorRenderer *renderer)
{
  CobiwmCursorRendererPrivate *priv =
    cobiwm_cursor_renderer_get_instance_private (renderer);

  update_cursor (renderer, priv->displayed_cursor);
}

void
cobiwm_cursor_renderer_set_position (CobiwmCursorRenderer *renderer,
                                   int x, int y)
{
  CobiwmCursorRendererPrivate *priv = cobiwm_cursor_renderer_get_instance_private (renderer);

  g_assert (cobiwm_is_wayland_compositor ());

  priv->current_x = x;
  priv->current_y = y;

  update_cursor (renderer, priv->displayed_cursor);
}

CobiwmCursorSprite *
cobiwm_cursor_renderer_get_cursor (CobiwmCursorRenderer *renderer)
{
  CobiwmCursorRendererPrivate *priv = cobiwm_cursor_renderer_get_instance_private (renderer);

  return priv->displayed_cursor;
}

#ifdef HAVE_WAYLAND
void
cobiwm_cursor_renderer_realize_cursor_from_wl_buffer (CobiwmCursorRenderer *renderer,
                                                    CobiwmCursorSprite   *cursor_sprite,
                                                    struct wl_resource *buffer)
{

  CobiwmCursorRendererClass *renderer_class = COBIWM_CURSOR_RENDERER_GET_CLASS (renderer);

  if (renderer_class->realize_cursor_from_wl_buffer)
    renderer_class->realize_cursor_from_wl_buffer (renderer, cursor_sprite, buffer);
}
#endif

void
cobiwm_cursor_renderer_realize_cursor_from_xcursor (CobiwmCursorRenderer *renderer,
                                                  CobiwmCursorSprite   *cursor_sprite,
                                                  XcursorImage       *xc_image)
{
  CobiwmCursorRendererClass *renderer_class = COBIWM_CURSOR_RENDERER_GET_CLASS (renderer);

  if (renderer_class->realize_cursor_from_xcursor)
    renderer_class->realize_cursor_from_xcursor (renderer, cursor_sprite, xc_image);
}
