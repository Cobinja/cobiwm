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

#ifndef COBIWM_CURSOR_RENDERER_H
#define COBIWM_CURSOR_RENDERER_H

#include <glib-object.h>
#include <X11/Xcursor/Xcursor.h>
#ifdef HAVE_WAYLAND
#include <wayland-server.h>
#endif

#include <screen.h>
#include "cobiwm-cursor.h"

#define COBIWM_TYPE_CURSOR_RENDERER (cobiwm_cursor_renderer_get_type ())
G_DECLARE_DERIVABLE_TYPE (CobiwmCursorRenderer, cobiwm_cursor_renderer,
                          COBIWM, CURSOR_RENDERER, GObject);

struct _CobiwmCursorRendererClass
{
  GObjectClass parent_class;

  gboolean (* update_cursor) (CobiwmCursorRenderer *renderer,
                              CobiwmCursorSprite   *cursor_sprite);
#ifdef HAVE_WAYLAND
  void (* realize_cursor_from_wl_buffer) (CobiwmCursorRenderer *renderer,
                                          CobiwmCursorSprite *cursor_sprite,
                                          struct wl_resource *buffer);
#endif
  void (* realize_cursor_from_xcursor) (CobiwmCursorRenderer *renderer,
                                        CobiwmCursorSprite *cursor_sprite,
                                        XcursorImage *xc_image);
};

CobiwmCursorRenderer * cobiwm_cursor_renderer_new (void);

void cobiwm_cursor_renderer_set_cursor (CobiwmCursorRenderer *renderer,
                                      CobiwmCursorSprite   *cursor_sprite);

void cobiwm_cursor_renderer_set_position (CobiwmCursorRenderer *renderer,
                                        int x, int y);
void cobiwm_cursor_renderer_force_update (CobiwmCursorRenderer *renderer);

CobiwmCursorSprite * cobiwm_cursor_renderer_get_cursor (CobiwmCursorRenderer *renderer);

CobiwmRectangle cobiwm_cursor_renderer_calculate_rect (CobiwmCursorRenderer *renderer,
                                                   CobiwmCursorSprite   *cursor_sprite);

#ifdef HAVE_WAYLAND
void cobiwm_cursor_renderer_realize_cursor_from_wl_buffer (CobiwmCursorRenderer *renderer,
                                                         CobiwmCursorSprite   *cursor_sprite,
                                                         struct wl_resource *buffer);
#endif

void cobiwm_cursor_renderer_realize_cursor_from_xcursor (CobiwmCursorRenderer *renderer,
                                                       CobiwmCursorSprite   *cursor_sprite,
                                                       XcursorImage       *xc_image);

#endif /* COBIWM_CURSOR_RENDERER_H */
