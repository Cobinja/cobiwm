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
 *     Owen Taylor <otaylor@redhat.com>
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

#include "config.h"

#include "cobiwm-surface-actor-x11.h"

#include <X11/extensions/Xcomposite.h>
#include <cogl/cogl-texture-pixmap-x11.h>

#include <errors.h>
#include "window-private.h"
#include "cobiwm-shaped-texture-private.h"
#include "cobiwm-cullable.h"
#include "x11/window-x11.h"

struct _CobiwmSurfaceActorX11Private
{
  CobiwmWindow *window;

  CobiwmDisplay *display;

  CoglTexture *texture;
  Pixmap pixmap;
  Damage damage;

  int last_width;
  int last_height;

  /* This is used to detect fullscreen windows that need to be unredirected */
  guint full_damage_frames_count;
  guint does_full_damage  : 1;

  /* Other state... */
  guint received_damage : 1;
  guint size_changed : 1;

  guint unredirected   : 1;
};
typedef struct _CobiwmSurfaceActorX11Private CobiwmSurfaceActorX11Private;

G_DEFINE_TYPE_WITH_PRIVATE (CobiwmSurfaceActorX11, cobiwm_surface_actor_x11, COBIWM_TYPE_SURFACE_ACTOR)

static void
free_damage (CobiwmSurfaceActorX11 *self)
{
  CobiwmSurfaceActorX11Private *priv = cobiwm_surface_actor_x11_get_instance_private (self);
  CobiwmDisplay *display = priv->display;
  Display *xdisplay = cobiwm_display_get_xdisplay (display);

  if (priv->damage == None)
    return;

  cobiwm_error_trap_push (display);
  XDamageDestroy (xdisplay, priv->damage);
  priv->damage = None;
  cobiwm_error_trap_pop (display);
}

static void
detach_pixmap (CobiwmSurfaceActorX11 *self)
{
  CobiwmSurfaceActorX11Private *priv = cobiwm_surface_actor_x11_get_instance_private (self);
  CobiwmDisplay *display = priv->display;
  Display *xdisplay = cobiwm_display_get_xdisplay (display);
  CobiwmShapedTexture *stex = cobiwm_surface_actor_get_texture (COBIWM_SURFACE_ACTOR (self));

  if (priv->pixmap == None)
    return;

  /* Get rid of all references to the pixmap before freeing it; it's unclear whether
   * you are supposed to be able to free a GLXPixmap after freeing the underlying
   * pixmap, but it certainly doesn't work with current DRI/Mesa
   */
  cobiwm_shaped_texture_set_texture (stex, NULL);
  cogl_flush ();

  cobiwm_error_trap_push (display);
  XFreePixmap (xdisplay, priv->pixmap);
  priv->pixmap = None;
  cobiwm_error_trap_pop (display);

  g_clear_pointer (&priv->texture, cogl_object_unref);
}

static void
set_pixmap (CobiwmSurfaceActorX11 *self,
            Pixmap               pixmap)
{
  CobiwmSurfaceActorX11Private *priv = cobiwm_surface_actor_x11_get_instance_private (self);

  CoglContext *ctx = clutter_backend_get_cogl_context (clutter_get_default_backend ());
  CobiwmShapedTexture *stex = cobiwm_surface_actor_get_texture (COBIWM_SURFACE_ACTOR (self));
  CoglError *error = NULL;
  CoglTexture *texture;

  g_assert (priv->pixmap == None);
  priv->pixmap = pixmap;

  texture = COGL_TEXTURE (cogl_texture_pixmap_x11_new (ctx, priv->pixmap, FALSE, &error));

  if (error != NULL)
    {
      g_warning ("Failed to allocate stex texture: %s", error->message);
      cogl_error_free (error);
    }
  else if (G_UNLIKELY (!cogl_texture_pixmap_x11_is_using_tfp_extension (COGL_TEXTURE_PIXMAP_X11 (texture))))
    g_warning ("NOTE: Not using GLX TFP!\n");

  priv->texture = texture;
  cobiwm_shaped_texture_set_texture (stex, texture);
}

static void
update_pixmap (CobiwmSurfaceActorX11 *self)
{
  CobiwmSurfaceActorX11Private *priv = cobiwm_surface_actor_x11_get_instance_private (self);
  CobiwmDisplay *display = priv->display;
  Display *xdisplay = cobiwm_display_get_xdisplay (display);

  if (priv->size_changed)
    {
      detach_pixmap (self);
      priv->size_changed = FALSE;
    }

  if (priv->pixmap == None)
    {
      Pixmap new_pixmap;
      Window xwindow = cobiwm_window_x11_get_toplevel_xwindow (priv->window);

      cobiwm_error_trap_push (display);
      new_pixmap = XCompositeNameWindowPixmap (xdisplay, xwindow);

      if (cobiwm_error_trap_pop_with_return (display) != Success)
        {
          /* Probably a BadMatch if the window isn't viewable; we could
           * GrabServer/GetWindowAttributes/NameWindowPixmap/UngrabServer/Sync
           * to avoid this, but there's no reason to take two round trips
           * when one will do. (We need that Sync if we want to handle failures
           * for any reason other than !viewable. That's unlikely, but maybe
           * we'll BadAlloc or something.)
           */
          new_pixmap = None;
        }

      if (new_pixmap == None)
        {
          cobiwm_verbose ("Unable to get named pixmap for %s\n",
                        cobiwm_window_get_description (priv->window));
          return;
        }

      set_pixmap (self, new_pixmap);
    }
}

static gboolean
is_visible (CobiwmSurfaceActorX11 *self)
{
  CobiwmSurfaceActorX11Private *priv = cobiwm_surface_actor_x11_get_instance_private (self);
  return (priv->pixmap != None) && !priv->unredirected;
}

static void
cobiwm_surface_actor_x11_process_damage (CobiwmSurfaceActor *actor,
                                       int x, int y, int width, int height)
{
  CobiwmSurfaceActorX11 *self = COBIWM_SURFACE_ACTOR_X11 (actor);
  CobiwmSurfaceActorX11Private *priv = cobiwm_surface_actor_x11_get_instance_private (self);

  priv->received_damage = TRUE;

  if (cobiwm_window_is_fullscreen (priv->window) && !priv->unredirected && !priv->does_full_damage)
    {
      CobiwmRectangle window_rect;
      cobiwm_window_get_frame_rect (priv->window, &window_rect);

      if (x == 0 &&
          y == 0 &&
          window_rect.width == width &&
          window_rect.height == height)
        priv->full_damage_frames_count++;
      else
        priv->full_damage_frames_count = 0;

      if (priv->full_damage_frames_count >= 100)
        priv->does_full_damage = TRUE;
    }

  if (!is_visible (self))
    return;

  cogl_texture_pixmap_x11_update_area (priv->texture, x, y, width, height);
}

static void
cobiwm_surface_actor_x11_pre_paint (CobiwmSurfaceActor *actor)
{
  CobiwmSurfaceActorX11 *self = COBIWM_SURFACE_ACTOR_X11 (actor);
  CobiwmSurfaceActorX11Private *priv = cobiwm_surface_actor_x11_get_instance_private (self);
  CobiwmDisplay *display = priv->display;
  Display *xdisplay = cobiwm_display_get_xdisplay (display);

  if (priv->received_damage)
    {
      cobiwm_error_trap_push (display);
      XDamageSubtract (xdisplay, priv->damage, None, None);
      cobiwm_error_trap_pop (display);

      priv->received_damage = FALSE;
    }

  update_pixmap (self);
}

static gboolean
cobiwm_surface_actor_x11_is_visible (CobiwmSurfaceActor *actor)
{
  CobiwmSurfaceActorX11 *self = COBIWM_SURFACE_ACTOR_X11 (actor);
  return is_visible (self);
}

static gboolean
cobiwm_surface_actor_x11_is_opaque (CobiwmSurfaceActor *actor)
{
  CobiwmSurfaceActorX11 *self = COBIWM_SURFACE_ACTOR_X11 (actor);
  CobiwmSurfaceActorX11Private *priv = cobiwm_surface_actor_x11_get_instance_private (self);

  /* If we're not ARGB32, then we're opaque. */
  if (!cobiwm_surface_actor_is_argb32 (actor))
    return TRUE;

  cairo_region_t *opaque_region = cobiwm_surface_actor_get_opaque_region (actor);

  /* If we have no opaque region, then no pixels are opaque. */
  if (!opaque_region)
    return FALSE;

  CobiwmWindow *window = priv->window;
  cairo_rectangle_int_t client_area;
  cobiwm_window_get_client_area_rect (window, &client_area);

  /* Otherwise, check if our opaque region covers our entire surface. */
  if (cairo_region_contains_rectangle (opaque_region, &client_area) == CAIRO_REGION_OVERLAP_IN)
    return TRUE;

  return FALSE;
}

static gboolean
cobiwm_surface_actor_x11_should_unredirect (CobiwmSurfaceActor *actor)
{
  CobiwmSurfaceActorX11 *self = COBIWM_SURFACE_ACTOR_X11 (actor);
  CobiwmSurfaceActorX11Private *priv = cobiwm_surface_actor_x11_get_instance_private (self);

  CobiwmWindow *window = priv->window;

  if (cobiwm_window_requested_dont_bypass_compositor (window))
    return FALSE;

  if (window->opacity != 0xFF)
    return FALSE;

  if (window->shape_region != NULL)
    return FALSE;

  if (!cobiwm_window_is_monitor_sized (window))
    return FALSE;

  if (cobiwm_window_requested_bypass_compositor (window))
    return TRUE;

  if (!cobiwm_surface_actor_x11_is_opaque (actor))
    return FALSE;

  if (cobiwm_window_is_override_redirect (window))
    return TRUE;

  if (priv->does_full_damage)
    return TRUE;

  return FALSE;
}

static void
sync_unredirected (CobiwmSurfaceActorX11 *self)
{
  CobiwmSurfaceActorX11Private *priv = cobiwm_surface_actor_x11_get_instance_private (self);
  CobiwmDisplay *display = priv->display;
  Display *xdisplay = cobiwm_display_get_xdisplay (display);
  Window xwindow = cobiwm_window_x11_get_toplevel_xwindow (priv->window);

  cobiwm_error_trap_push (display);

  if (priv->unredirected)
    {
      detach_pixmap (self);
      XCompositeUnredirectWindow (xdisplay, xwindow, CompositeRedirectManual);
    }
  else
    {
      XCompositeRedirectWindow (xdisplay, xwindow, CompositeRedirectManual);
    }

  cobiwm_error_trap_pop (display);
}

static void
cobiwm_surface_actor_x11_set_unredirected (CobiwmSurfaceActor *actor,
                                         gboolean          unredirected)
{
  CobiwmSurfaceActorX11 *self = COBIWM_SURFACE_ACTOR_X11 (actor);
  CobiwmSurfaceActorX11Private *priv = cobiwm_surface_actor_x11_get_instance_private (self);

  if (priv->unredirected == unredirected)
    return;

  priv->unredirected = unredirected;
  sync_unredirected (self);
}

static gboolean
cobiwm_surface_actor_x11_is_unredirected (CobiwmSurfaceActor *actor)
{
  CobiwmSurfaceActorX11 *self = COBIWM_SURFACE_ACTOR_X11 (actor);
  CobiwmSurfaceActorX11Private *priv = cobiwm_surface_actor_x11_get_instance_private (self);

  return priv->unredirected;
}

static void
cobiwm_surface_actor_x11_dispose (GObject *object)
{
  CobiwmSurfaceActorX11 *self = COBIWM_SURFACE_ACTOR_X11 (object);

  detach_pixmap (self);
  free_damage (self);

  G_OBJECT_CLASS (cobiwm_surface_actor_x11_parent_class)->dispose (object);
}

static CobiwmWindow *
cobiwm_surface_actor_x11_get_window (CobiwmSurfaceActor *actor)
{
  CobiwmSurfaceActorX11Private *priv = cobiwm_surface_actor_x11_get_instance_private (COBIWM_SURFACE_ACTOR_X11 (actor));

  return priv->window;
}

static void
cobiwm_surface_actor_x11_class_init (CobiwmSurfaceActorX11Class *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  CobiwmSurfaceActorClass *surface_actor_class = COBIWM_SURFACE_ACTOR_CLASS (klass);

  object_class->dispose = cobiwm_surface_actor_x11_dispose;

  surface_actor_class->process_damage = cobiwm_surface_actor_x11_process_damage;
  surface_actor_class->pre_paint = cobiwm_surface_actor_x11_pre_paint;
  surface_actor_class->is_visible = cobiwm_surface_actor_x11_is_visible;

  surface_actor_class->should_unredirect = cobiwm_surface_actor_x11_should_unredirect;
  surface_actor_class->set_unredirected = cobiwm_surface_actor_x11_set_unredirected;
  surface_actor_class->is_unredirected = cobiwm_surface_actor_x11_is_unredirected;

  surface_actor_class->get_window = cobiwm_surface_actor_x11_get_window;
}

static void
cobiwm_surface_actor_x11_init (CobiwmSurfaceActorX11 *self)
{
  CobiwmSurfaceActorX11Private *priv = cobiwm_surface_actor_x11_get_instance_private (self);

  priv->last_width = -1;
  priv->last_height = -1;
}

static void
create_damage (CobiwmSurfaceActorX11 *self)
{
  CobiwmSurfaceActorX11Private *priv = cobiwm_surface_actor_x11_get_instance_private (self);
  Display *xdisplay = cobiwm_display_get_xdisplay (priv->display);
  Window xwindow = cobiwm_window_x11_get_toplevel_xwindow (priv->window);

  priv->damage = XDamageCreate (xdisplay, xwindow, XDamageReportBoundingBox);
}

static void
window_decorated_notify (CobiwmWindow *window,
                         GParamSpec *pspec,
                         gpointer    user_data)
{
  CobiwmSurfaceActorX11 *self = COBIWM_SURFACE_ACTOR_X11 (user_data);

  detach_pixmap (self);
  free_damage (self);
  create_damage (self);
}

CobiwmSurfaceActor *
cobiwm_surface_actor_x11_new (CobiwmWindow *window)
{
  CobiwmSurfaceActorX11 *self = g_object_new (COBIWM_TYPE_SURFACE_ACTOR_X11, NULL);
  CobiwmSurfaceActorX11Private *priv = cobiwm_surface_actor_x11_get_instance_private (self);
  CobiwmDisplay *display = cobiwm_window_get_display (window);

  g_assert (!cobiwm_is_wayland_compositor ());

  priv->window = window;
  priv->display = display;

  create_damage (self);
  g_signal_connect_object (priv->window, "notify::decorated",
                           G_CALLBACK (window_decorated_notify), self, 0);

  priv->unredirected = FALSE;
  sync_unredirected (self);

  clutter_actor_set_reactive (CLUTTER_ACTOR (self), TRUE);
  return COBIWM_SURFACE_ACTOR (self);
}

void
cobiwm_surface_actor_x11_set_size (CobiwmSurfaceActorX11 *self,
                                 int width, int height)
{
  CobiwmSurfaceActorX11Private *priv = cobiwm_surface_actor_x11_get_instance_private (self);
  CobiwmShapedTexture *stex = cobiwm_surface_actor_get_texture (COBIWM_SURFACE_ACTOR (self));

  if (priv->last_width == width &&
      priv->last_height == height)
    return;

  priv->size_changed = TRUE;
  priv->last_width = width;
  priv->last_height = height;
  cobiwm_shaped_texture_set_fallback_size (stex, width, height);
}
