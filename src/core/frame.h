/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Cobiwm X window decorations */

/*
 * Copyright (C) 2001 Havoc Pennington
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
 */

#ifndef COBIWM_FRAME_PRIVATE_H
#define COBIWM_FRAME_PRIVATE_H

#include "window-private.h"

#include "ui/frames.h"

struct _CobiwmFrame
{
  /* window we frame */
  CobiwmWindow *window;

  /* reparent window */
  Window xwindow;

  CobiwmCursor current_cursor;

  /* This rect is trusted info from where we put the
   * frame, not the result of ConfigureNotify
   */
  CobiwmRectangle rect;

  CobiwmFrameBorders cached_borders; /* valid if borders_cached is set */

  /* position of client, size of frame */
  int child_x;
  int child_y;
  int right_width;
  int bottom_height;

  guint need_reapply_frame_shape : 1;
  guint is_flashing : 1; /* used by the visual bell flash */
  guint borders_cached : 1;

  CobiwmUIFrame *ui_frame;
};

void     cobiwm_window_ensure_frame           (CobiwmWindow *window);
void     cobiwm_window_destroy_frame          (CobiwmWindow *window);
void     cobiwm_frame_queue_draw              (CobiwmFrame  *frame);

CobiwmFrameFlags cobiwm_frame_get_flags   (CobiwmFrame *frame);
Window         cobiwm_frame_get_xwindow (CobiwmFrame *frame);

/* These should ONLY be called from cobiwm_window_move_resize_internal */
void cobiwm_frame_calc_borders      (CobiwmFrame        *frame,
                                   CobiwmFrameBorders *borders);

gboolean cobiwm_frame_sync_to_window (CobiwmFrame         *frame,
                                    gboolean           need_resize);

void cobiwm_frame_clear_cached_borders (CobiwmFrame *frame);

cairo_region_t *cobiwm_frame_get_frame_bounds (CobiwmFrame *frame);

void cobiwm_frame_get_mask (CobiwmFrame *frame,
                          cairo_t   *cr);

void cobiwm_frame_set_screen_cursor (CobiwmFrame	*frame,
				   CobiwmCursor	cursor);

void cobiwm_frame_update_style (CobiwmFrame *frame);
void cobiwm_frame_update_title (CobiwmFrame *frame);

#endif
