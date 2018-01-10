/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright 2013 Red Hat, Inc.
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
 * Author: Giovanni Campagna <gcampagn@redhat.com>
 */

#include "config.h"

#include "cobiwm-cursor.h"

#include <errors.h>

#include "display-private.h"
#include "screen-private.h"
#include "cobiwm-backend-private.h"

#include <string.h>

#include <X11/cursorfont.h>
#include <X11/extensions/Xfixes.h>
#include <X11/Xcursor/Xcursor.h>

enum {
  PREPARE_AT,

  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

struct _CobiwmCursorSprite
{
  GObject parent;

  CobiwmCursor cursor;

  CoglTexture2D *texture;
  float texture_scale;
  int hot_x, hot_y;

  int current_frame;
  XcursorImages *xcursor_images;

  int theme_scale;
  gboolean theme_dirty;
};

G_DEFINE_TYPE (CobiwmCursorSprite, cobiwm_cursor_sprite, G_TYPE_OBJECT)

static const char *
translate_cobiwm_cursor (CobiwmCursor cursor)
{
  switch (cursor)
    {
    case COBIWM_CURSOR_DEFAULT:
      return "left_ptr";
    case COBIWM_CURSOR_NORTH_RESIZE:
      return "top_side";
    case COBIWM_CURSOR_SOUTH_RESIZE:
      return "bottom_side";
    case COBIWM_CURSOR_WEST_RESIZE:
      return "left_side";
    case COBIWM_CURSOR_EAST_RESIZE:
      return "right_side";
    case COBIWM_CURSOR_SE_RESIZE:
      return "bottom_right_corner";
    case COBIWM_CURSOR_SW_RESIZE:
      return "bottom_left_corner";
    case COBIWM_CURSOR_NE_RESIZE:
      return "top_right_corner";
    case COBIWM_CURSOR_NW_RESIZE:
      return "top_left_corner";
    case COBIWM_CURSOR_MOVE_OR_RESIZE_WINDOW:
      return "fleur";
    case COBIWM_CURSOR_BUSY:
      return "watch";
    case COBIWM_CURSOR_DND_IN_DRAG:
      return "dnd-none";
    case COBIWM_CURSOR_DND_MOVE:
      return "dnd-move";
    case COBIWM_CURSOR_DND_COPY:
      return "dnd-copy";
    case COBIWM_CURSOR_DND_UNSUPPORTED_TARGET:
      return "dnd-none";
    case COBIWM_CURSOR_POINTING_HAND:
      return "hand2";
    case COBIWM_CURSOR_CROSSHAIR:
      return "crosshair";
    case COBIWM_CURSOR_IBEAM:
      return "xterm";
    default:
      break;
    }

  g_assert_not_reached ();
}

Cursor
cobiwm_cursor_create_x_cursor (Display    *xdisplay,
                             CobiwmCursor  cursor)
{
  return XcursorLibraryLoadCursor (xdisplay, translate_cobiwm_cursor (cursor));
}

static XcursorImages *
load_cursor_on_client (CobiwmCursor cursor, int scale)
{
  return XcursorLibraryLoadImages (translate_cobiwm_cursor (cursor),
                                   cobiwm_prefs_get_cursor_theme (),
                                   cobiwm_prefs_get_cursor_size () * scale);
}

static void
cobiwm_cursor_sprite_load_from_xcursor_image (CobiwmCursorSprite *self,
                                            XcursorImage     *xc_image)
{
  CobiwmBackend *cobiwm_backend = cobiwm_get_backend ();
  CobiwmCursorRenderer *renderer = cobiwm_backend_get_cursor_renderer (cobiwm_backend);
  uint width, height, rowstride;
  CoglPixelFormat cogl_format;
  ClutterBackend *clutter_backend;
  CoglContext *cogl_context;
  CoglTexture *texture;
  CoglError *error = NULL;

  g_assert (self->texture == NULL);

  width           = xc_image->width;
  height          = xc_image->height;
  rowstride       = width * 4;

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  cogl_format = COGL_PIXEL_FORMAT_BGRA_8888;
#else
  cogl_format = COGL_PIXEL_FORMAT_ARGB_8888;
#endif

  clutter_backend = clutter_get_default_backend ();
  cogl_context = clutter_backend_get_cogl_context (clutter_backend);
  texture = cogl_texture_2d_new_from_data (cogl_context,
                                           width, height,
                                           cogl_format,
                                           rowstride,
                                           (uint8_t *) xc_image->pixels,
                                           &error);

  if (error)
    {
      cobiwm_warning ("Failed to allocate cursor texture: %s\n", error->message);
      cogl_error_free (error);
    }

  cobiwm_cursor_sprite_set_texture (self, texture,
                                  xc_image->xhot, xc_image->yhot);

  if (texture)
    cogl_object_unref (texture);

  cobiwm_cursor_renderer_realize_cursor_from_xcursor (renderer, self, xc_image);
}

static XcursorImage *
cobiwm_cursor_sprite_get_current_frame_image (CobiwmCursorSprite *self)
{
  return self->xcursor_images->images[self->current_frame];
}

void
cobiwm_cursor_sprite_tick_frame (CobiwmCursorSprite *self)
{
  XcursorImage *image;

  if (!cobiwm_cursor_sprite_is_animated (self))
    return;

  self->current_frame++;

  if (self->current_frame >= self->xcursor_images->nimage)
    self->current_frame = 0;

  image = cobiwm_cursor_sprite_get_current_frame_image (self);

  g_clear_pointer (&self->texture, cogl_object_unref);
  cobiwm_cursor_sprite_load_from_xcursor_image (self, image);
}

guint
cobiwm_cursor_sprite_get_current_frame_time (CobiwmCursorSprite *self)
{
  if (!cobiwm_cursor_sprite_is_animated (self))
    return 0;

  return self->xcursor_images->images[self->current_frame]->delay;
}

gboolean
cobiwm_cursor_sprite_is_animated (CobiwmCursorSprite *self)
{
  return (self->xcursor_images &&
          self->xcursor_images->nimage > 1);
}

CobiwmCursorSprite *
cobiwm_cursor_sprite_new (void)
{
  return g_object_new (COBIWM_TYPE_CURSOR_SPRITE, NULL);
}

static void
cobiwm_cursor_sprite_load_from_theme (CobiwmCursorSprite *self)
{
  XcursorImage *image;

  g_assert (self->cursor != COBIWM_CURSOR_NONE);

  /* We might be reloading with a different scale. If so clear the old data. */
  if (self->xcursor_images)
    {
      g_clear_pointer (&self->texture, cogl_object_unref);
      XcursorImagesDestroy (self->xcursor_images);
    }

  self->current_frame = 0;
  self->xcursor_images = load_cursor_on_client (self->cursor,
                                                self->theme_scale);
  if (!self->xcursor_images)
    cobiwm_fatal ("Could not find cursor. Perhaps set XCURSOR_PATH?");

  image = cobiwm_cursor_sprite_get_current_frame_image (self);
  cobiwm_cursor_sprite_load_from_xcursor_image (self, image);

  self->theme_dirty = FALSE;
}

CobiwmCursorSprite *
cobiwm_cursor_sprite_from_theme (CobiwmCursor cursor)
{
  CobiwmCursorSprite *self;

  self = cobiwm_cursor_sprite_new ();

  self->cursor = cursor;
  self->theme_dirty = TRUE;

  return self;
}

void
cobiwm_cursor_sprite_set_texture (CobiwmCursorSprite *self,
                                CoglTexture      *texture,
                                int               hot_x,
                                int               hot_y)
{
  g_clear_pointer (&self->texture, cogl_object_unref);
  if (texture)
    self->texture = cogl_object_ref (texture);
  self->hot_x = hot_x;
  self->hot_y = hot_y;
}

void
cobiwm_cursor_sprite_set_texture_scale (CobiwmCursorSprite *self,
                                      float             scale)
{
  self->texture_scale = scale;
}

void
cobiwm_cursor_sprite_set_theme_scale (CobiwmCursorSprite *self,
                                    int               theme_scale)
{
  if (self->theme_scale != theme_scale)
    self->theme_dirty = TRUE;
  self->theme_scale = theme_scale;
}

CoglTexture *
cobiwm_cursor_sprite_get_cogl_texture (CobiwmCursorSprite *self)
{
  return COGL_TEXTURE (self->texture);
}

CobiwmCursor
cobiwm_cursor_sprite_get_cobiwm_cursor (CobiwmCursorSprite *self)
{
  return self->cursor;
}

void
cobiwm_cursor_sprite_get_hotspot (CobiwmCursorSprite *self,
                                int              *hot_x,
                                int              *hot_y)
{
  *hot_x = self->hot_x;
  *hot_y = self->hot_y;
}

float
cobiwm_cursor_sprite_get_texture_scale (CobiwmCursorSprite *self)
{
  return self->texture_scale;
}

void
cobiwm_cursor_sprite_prepare_at (CobiwmCursorSprite *self,
                               int               x,
                               int               y)
{
  g_signal_emit (self, signals[PREPARE_AT], 0, x, y);
}

void
cobiwm_cursor_sprite_realize_texture (CobiwmCursorSprite *self)
{
  if (self->theme_dirty)
    cobiwm_cursor_sprite_load_from_theme (self);
}

static void
cobiwm_cursor_sprite_init (CobiwmCursorSprite *self)
{
  self->texture_scale = 1.0f;
}

static void
cobiwm_cursor_sprite_finalize (GObject *object)
{
  CobiwmCursorSprite *self = COBIWM_CURSOR_SPRITE (object);

  if (self->xcursor_images)
    XcursorImagesDestroy (self->xcursor_images);

  g_clear_pointer (&self->texture, cogl_object_unref);

  G_OBJECT_CLASS (cobiwm_cursor_sprite_parent_class)->finalize (object);
}

static void
cobiwm_cursor_sprite_class_init (CobiwmCursorSpriteClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = cobiwm_cursor_sprite_finalize;

  signals[PREPARE_AT] = g_signal_new ("prepare-at",
                                      G_TYPE_FROM_CLASS (object_class),
                                      G_SIGNAL_RUN_LAST,
                                      0,
                                      NULL, NULL, NULL,
                                      G_TYPE_NONE, 2,
                                      G_TYPE_INT,
                                      G_TYPE_INT);
}
