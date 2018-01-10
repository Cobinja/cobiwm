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

#include "backends/x11/nested/cobiwm-cursor-renderer-x11-nested.h"

#include "backends/x11/cobiwm-backend-x11.h"

struct _CobiwmCursorRendererX11Nested
{
  CobiwmCursorRenderer parent;
};

G_DEFINE_TYPE (CobiwmCursorRendererX11Nested, cobiwm_cursor_renderer_x11_nested,
               COBIWM_TYPE_CURSOR_RENDERER);

static gboolean
cobiwm_cursor_renderer_x11_nested_update_cursor (CobiwmCursorRenderer *renderer,
                                               CobiwmCursorSprite   *cursor_sprite)
{
  if (cursor_sprite)
    cobiwm_cursor_sprite_realize_texture (cursor_sprite);
  return FALSE;
}

static Cursor
create_empty_cursor (Display *xdisplay)
{
  XcursorImage *image;
  XcursorPixel *pixels;
  Cursor xcursor;

  image = XcursorImageCreate (1, 1);
  if (image == NULL)
    return None;

  image->xhot = 0;
  image->yhot = 0;

  pixels = image->pixels;
  pixels[0] = 0;

  xcursor = XcursorImageLoadCursor (xdisplay, image);
  XcursorImageDestroy (image);

  return xcursor;
}

static void
cobiwm_cursor_renderer_x11_nested_init (CobiwmCursorRendererX11Nested *x11_nested)
{
  CobiwmBackendX11 *backend = COBIWM_BACKEND_X11 (cobiwm_get_backend ());
  Window xwindow = cobiwm_backend_x11_get_xwindow (backend);
  Display *xdisplay = cobiwm_backend_x11_get_xdisplay (backend);

  Cursor empty_xcursor = create_empty_cursor (xdisplay);
  XDefineCursor (xdisplay, xwindow, empty_xcursor);
  XFreeCursor (xdisplay, empty_xcursor);
}

static void
cobiwm_cursor_renderer_x11_nested_class_init (CobiwmCursorRendererX11NestedClass *klass)
{
  CobiwmCursorRendererClass *renderer_class = COBIWM_CURSOR_RENDERER_CLASS (klass);

  renderer_class->update_cursor = cobiwm_cursor_renderer_x11_nested_update_cursor;
}
