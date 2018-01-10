/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2013 Red Hat, Inc.
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

#ifndef COBIWM_CURSOR_TRACKER_H
#define COBIWM_CURSOR_TRACKER_H

#include <glib-object.h>
#include <types.h>
#include <workspace.h>
#include <cogl/cogl.h>
#include <clutter/clutter.h>

#define COBIWM_TYPE_CURSOR_TRACKER            (cobiwm_cursor_tracker_get_type ())
#define COBIWM_CURSOR_TRACKER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), COBIWM_TYPE_CURSOR_TRACKER, CobiwmCursorTracker))
#define COBIWM_CURSOR_TRACKER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  COBIWM_TYPE_CURSOR_TRACKER, CobiwmCursorTrackerClass))
#define COBIWM_IS_CURSOR_TRACKER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), COBIWM_TYPE_CURSOR_TRACKER))
#define COBIWM_IS_CURSOR_TRACKER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  COBIWM_TYPE_CURSOR_TRACKER))
#define COBIWM_CURSOR_TRACKER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  COBIWM_TYPE_CURSOR_TRACKER, CobiwmCursorTrackerClass))

typedef struct _CobiwmCursorTrackerClass   CobiwmCursorTrackerClass;

GType cobiwm_cursor_tracker_get_type (void);

CobiwmCursorTracker *cobiwm_cursor_tracker_get_for_screen (CobiwmScreen *screen);

void           cobiwm_cursor_tracker_get_hot    (CobiwmCursorTracker *tracker,
                                               int               *x,
                                               int               *y);
CoglTexture   *cobiwm_cursor_tracker_get_sprite (CobiwmCursorTracker *tracker);

void           cobiwm_cursor_tracker_get_pointer (CobiwmCursorTracker   *tracker,
                                                int                 *x,
                                                int                 *y,
                                                ClutterModifierType *mods);
void           cobiwm_cursor_tracker_set_pointer_visible (CobiwmCursorTracker *tracker,
                                                        gboolean           visible);

#endif
