/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2014-2015 Red Hat
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
 *     Jonas Ådahl <jadahl@gmail.com>
 */

/**
 * SECTION:barrier-x11
 * @Title: CobiwmBarrierImplX11
 * @Short_Description: Pointer barriers implementation for X11
 */

#include "config.h"

#ifdef HAVE_XI23

#include <glib-object.h>

#include <X11/extensions/XInput2.h>
#include <X11/extensions/Xfixes.h>
#include <barrier.h>
#include "backends/x11/cobiwm-barrier-x11.h"
#include "display-private.h"

struct _CobiwmBarrierImplX11Private
{
  CobiwmBarrier *barrier;
  PointerBarrier xbarrier;
};

G_DEFINE_TYPE_WITH_PRIVATE (CobiwmBarrierImplX11, cobiwm_barrier_impl_x11,
                            COBIWM_TYPE_BARRIER_IMPL)

static gboolean
_cobiwm_barrier_impl_x11_is_active (CobiwmBarrierImpl *impl)
{
  CobiwmBarrierImplX11 *self = COBIWM_BARRIER_IMPL_X11 (impl);
  CobiwmBarrierImplX11Private *priv =
    cobiwm_barrier_impl_x11_get_instance_private (self);

  return priv->xbarrier != 0;
}

static void
_cobiwm_barrier_impl_x11_release (CobiwmBarrierImpl  *impl,
                                CobiwmBarrierEvent *event)
{
  CobiwmBarrierImplX11 *self = COBIWM_BARRIER_IMPL_X11 (impl);
  CobiwmBarrierImplX11Private *priv =
    cobiwm_barrier_impl_x11_get_instance_private (self);
  CobiwmDisplay *display = priv->barrier->priv->display;

  if (COBIWM_DISPLAY_HAS_XINPUT_23 (display))
    {
      XIBarrierReleasePointer (display->xdisplay,
                               COBIWM_VIRTUAL_CORE_POINTER_ID,
                               priv->xbarrier, event->event_id);
    }
}

static void
_cobiwm_barrier_impl_x11_destroy (CobiwmBarrierImpl *impl)
{
  CobiwmBarrierImplX11 *self = COBIWM_BARRIER_IMPL_X11 (impl);
  CobiwmBarrierImplX11Private *priv =
    cobiwm_barrier_impl_x11_get_instance_private (self);
  CobiwmDisplay *display = priv->barrier->priv->display;
  Display *dpy;

  if (display == NULL)
    return;

  dpy = display->xdisplay;

  if (!cobiwm_barrier_is_active (priv->barrier))
    return;

  XFixesDestroyPointerBarrier (dpy, priv->xbarrier);
  g_hash_table_remove (display->xids, &priv->xbarrier);
  priv->xbarrier = 0;
}

CobiwmBarrierImpl *
cobiwm_barrier_impl_x11_new (CobiwmBarrier *barrier)
{
  CobiwmBarrierImplX11 *self;
  CobiwmBarrierImplX11Private *priv;
  CobiwmDisplay *display = barrier->priv->display;
  Display *dpy;
  Window root;
  unsigned int allowed_motion_dirs;

  if (display == NULL)
    {
      g_warning ("A display must be provided when constructing a barrier.");
      return NULL;
    }

  self = g_object_new (COBIWM_TYPE_BARRIER_IMPL_X11, NULL);
  priv = cobiwm_barrier_impl_x11_get_instance_private (self);
  priv->barrier = barrier;

  dpy = display->xdisplay;
  root = DefaultRootWindow (dpy);

  allowed_motion_dirs =
    cobiwm_border_get_allows_directions (&barrier->priv->border);
  priv->xbarrier = XFixesCreatePointerBarrier (dpy, root,
                                               barrier->priv->border.line.a.x,
                                               barrier->priv->border.line.a.y,
                                               barrier->priv->border.line.b.x,
                                               barrier->priv->border.line.b.y,
                                               allowed_motion_dirs,
                                               0, NULL);

  g_hash_table_insert (display->xids, &priv->xbarrier, barrier);

  return COBIWM_BARRIER_IMPL (self);
}

static void
cobiwm_barrier_fire_xevent (CobiwmBarrier    *barrier,
                          XIBarrierEvent *xevent)
{
  CobiwmBarrierEvent *event = g_slice_new0 (CobiwmBarrierEvent);

  event->ref_count = 1;
  event->event_id = xevent->eventid;
  event->time = xevent->time;
  event->dt = xevent->dtime;

  event->x = xevent->root_x;
  event->y = xevent->root_y;
  event->dx = xevent->dx;
  event->dy = xevent->dy;

  event->released = (xevent->flags & XIBarrierPointerReleased) != 0;
  event->grabbed = (xevent->flags & XIBarrierDeviceIsGrabbed) != 0;

  switch (xevent->evtype)
    {
    case XI_BarrierHit:
      _cobiwm_barrier_emit_hit_signal (barrier, event);
      break;
    case XI_BarrierLeave:
      _cobiwm_barrier_emit_left_signal (barrier, event);
      break;
    default:
      g_assert_not_reached ();
    }

  cobiwm_barrier_event_unref (event);
}

gboolean
cobiwm_display_process_barrier_xevent (CobiwmDisplay *display,
                                     XIEvent     *event)
{
  CobiwmBarrier *barrier;
  XIBarrierEvent *xev;

  if (event == NULL)
    return FALSE;

  switch (event->evtype)
    {
    case XI_BarrierHit:
    case XI_BarrierLeave:
      break;
    default:
      return FALSE;
    }

  xev = (XIBarrierEvent *) event;
  barrier = g_hash_table_lookup (display->xids, &xev->barrier);
  if (barrier != NULL)
    {
      cobiwm_barrier_fire_xevent (barrier, xev);
      return TRUE;
    }

  return FALSE;
}

static void
cobiwm_barrier_impl_x11_class_init (CobiwmBarrierImplX11Class *klass)
{
  CobiwmBarrierImplClass *impl_class = COBIWM_BARRIER_IMPL_CLASS (klass);

  impl_class->is_active = _cobiwm_barrier_impl_x11_is_active;
  impl_class->release = _cobiwm_barrier_impl_x11_release;
  impl_class->destroy = _cobiwm_barrier_impl_x11_destroy;
}

static void
cobiwm_barrier_impl_x11_init (CobiwmBarrierImplX11 *self)
{
}

#endif /* HAVE_XI23 */
