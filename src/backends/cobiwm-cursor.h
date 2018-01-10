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

#ifndef COBIWM_CURSOR_H
#define COBIWM_CURSOR_H

#include <common.h>
#include <boxes.h>

typedef struct _CobiwmCursorSprite CobiwmCursorSprite;

#define COBIWM_TYPE_CURSOR_SPRITE (cobiwm_cursor_sprite_get_type ())
G_DECLARE_FINAL_TYPE (CobiwmCursorSprite,
                      cobiwm_cursor_sprite,
                      COBIWM, CURSOR_SPRITE,
                      GObject);

CobiwmCursorSprite * cobiwm_cursor_sprite_new (void);

CobiwmCursorSprite * cobiwm_cursor_sprite_from_theme  (CobiwmCursor cursor);


void cobiwm_cursor_sprite_set_theme_scale (CobiwmCursorSprite *self,
                                         int               scale);

CobiwmCursor cobiwm_cursor_sprite_get_cobiwm_cursor (CobiwmCursorSprite *self);

Cursor cobiwm_cursor_create_x_cursor (Display    *xdisplay,
                                    CobiwmCursor  cursor);

void cobiwm_cursor_sprite_prepare_at (CobiwmCursorSprite *self,
                                    int               x,
                                    int               y);

void cobiwm_cursor_sprite_realize_texture (CobiwmCursorSprite *self);

void cobiwm_cursor_sprite_set_texture (CobiwmCursorSprite *self,
                                     CoglTexture      *texture,
                                     int               hot_x,
                                     int               hot_y);

void cobiwm_cursor_sprite_set_texture_scale (CobiwmCursorSprite *self,
                                           float             scale);

CoglTexture *cobiwm_cursor_sprite_get_cogl_texture (CobiwmCursorSprite *self);

void cobiwm_cursor_sprite_get_hotspot (CobiwmCursorSprite *self,
                                     int              *hot_x,
                                     int              *hot_y);

float cobiwm_cursor_sprite_get_texture_scale (CobiwmCursorSprite *self);

gboolean cobiwm_cursor_sprite_is_animated            (CobiwmCursorSprite *self);
void     cobiwm_cursor_sprite_tick_frame             (CobiwmCursorSprite *self);
guint    cobiwm_cursor_sprite_get_current_frame_time (CobiwmCursorSprite *self);

#endif /* COBIWM_CURSOR_H */
