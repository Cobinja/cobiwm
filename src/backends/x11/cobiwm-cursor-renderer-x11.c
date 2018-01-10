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

#include "cobiwm-cursor-renderer-x11.h"

#include <X11/extensions/Xfixes.h>

#include "cobiwm-backend-x11.h"
#include "cobiwm-stage.h"

struct _CobiwmCursorRendererX11Private
{
  gboolean server_cursor_visible;
};
typedef struct _CobiwmCursorRendererX11Private CobiwmCursorRendererX11Private;

G_DEFINE_TYPE_WITH_PRIVATE (CobiwmCursorRendererX11, cobiwm_cursor_renderer_x11, COBIWM_TYPE_CURSOR_RENDERER);

static gboolean
cobiwm_cursor_renderer_x11_update_cursor (CobiwmCursorRenderer *renderer,
                                        CobiwmCursorSprite   *cursor_sprite)
{
  CobiwmCursorRendererX11 *x11 = COBIWM_CURSOR_RENDERER_X11 (renderer);
  CobiwmCursorRendererX11Private *priv = cobiwm_cursor_renderer_x11_get_instance_private (x11);

  CobiwmBackendX11 *backend = COBIWM_BACKEND_X11 (cobiwm_get_backend ());
  Window xwindow = cobiwm_backend_x11_get_xwindow (backend);
  Display *xdisplay = cobiwm_backend_x11_get_xdisplay (backend);

  if (xwindow == None)
    {
      if (cursor_sprite)
        cobiwm_cursor_sprite_realize_texture (cursor_sprite);
      return FALSE;
    }

  gboolean has_server_cursor = FALSE;

  if (cursor_sprite)
    {
      CobiwmCursor cursor = cobiwm_cursor_sprite_get_cobiwm_cursor (cursor_sprite);

      if (cursor != COBIWM_CURSOR_NONE)
        {
          Cursor xcursor = cobiwm_cursor_create_x_cursor (xdisplay, cursor);
          XDefineCursor (xdisplay, xwindow, xcursor);
          XFlush (xdisplay);
          XFreeCursor (xdisplay, xcursor);

          has_server_cursor = TRUE;
        }
    }

  if (has_server_cursor != priv->server_cursor_visible)
    {
      if (has_server_cursor)
        XFixesShowCursor (xdisplay, xwindow);
      else
        XFixesHideCursor (xdisplay, xwindow);

      priv->server_cursor_visible = has_server_cursor;
    }

  if (!priv->server_cursor_visible && cursor_sprite)
    cobiwm_cursor_sprite_realize_texture (cursor_sprite);

  return priv->server_cursor_visible;
}

static void
cobiwm_cursor_renderer_x11_class_init (CobiwmCursorRendererX11Class *klass)
{
  CobiwmCursorRendererClass *renderer_class = COBIWM_CURSOR_RENDERER_CLASS (klass);

  renderer_class->update_cursor = cobiwm_cursor_renderer_x11_update_cursor;
}

static void
cobiwm_cursor_renderer_x11_init (CobiwmCursorRendererX11 *x11)
{
  CobiwmCursorRendererX11Private *priv = cobiwm_cursor_renderer_x11_get_instance_private (x11);

  /* XFixes has no way to retrieve the current cursor visibility. */
  priv->server_cursor_visible = TRUE;
}
