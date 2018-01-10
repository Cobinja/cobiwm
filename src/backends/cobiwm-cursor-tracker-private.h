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

#ifndef COBIWM_CURSOR_TRACKER_PRIVATE_H
#define COBIWM_CURSOR_TRACKER_PRIVATE_H

#include <cobiwm-cursor-tracker.h>

#include "cobiwm-cursor.h"
#include "cobiwm-cursor-renderer.h"

struct _CobiwmCursorTracker {
  GObject parent_instance;

  CobiwmCursorRenderer *renderer;

  gboolean is_showing;

  CobiwmCursorSprite *displayed_cursor;

  /* Wayland clients can set a NULL buffer as their cursor
   * explicitly, which means that we shouldn't display anything.
   * So, we can't simply store a NULL in window_cursor to
   * determine an unset window cursor; we need an extra boolean.
   */
  gboolean has_window_cursor;
  CobiwmCursorSprite *window_cursor;

  CobiwmCursorSprite *root_cursor;

  /* The cursor from the X11 server. */
  CobiwmCursorSprite *xfixes_cursor;
};

struct _CobiwmCursorTrackerClass {
  GObjectClass parent_class;
};

gboolean cobiwm_cursor_tracker_handle_xevent (CobiwmCursorTracker *tracker,
					    XEvent            *xevent);

void     cobiwm_cursor_tracker_set_window_cursor   (CobiwmCursorTracker *tracker,
                                                  CobiwmCursorSprite  *cursor_sprite);
void     cobiwm_cursor_tracker_unset_window_cursor (CobiwmCursorTracker *tracker);
void     cobiwm_cursor_tracker_set_root_cursor     (CobiwmCursorTracker *tracker,
                                                  CobiwmCursorSprite  *cursor_sprite);

void     cobiwm_cursor_tracker_update_position (CobiwmCursorTracker *tracker,
					      int                new_x,
					      int                new_y);

CobiwmCursorSprite * cobiwm_cursor_tracker_get_displayed_cursor (CobiwmCursorTracker *tracker);

#endif
