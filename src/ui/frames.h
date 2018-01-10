/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Cobiwmcity window frame manager widget */

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

#ifndef COBIWM_FRAMES_H
#define COBIWM_FRAMES_H

#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <common.h>
#include <types.h>
#include "theme-private.h"
#include "ui.h"

typedef enum
{
  COBIWM_FRAME_CONTROL_NONE,
  COBIWM_FRAME_CONTROL_TITLE,
  COBIWM_FRAME_CONTROL_DELETE,
  COBIWM_FRAME_CONTROL_MENU,
  COBIWM_FRAME_CONTROL_APPMENU,
  COBIWM_FRAME_CONTROL_MINIMIZE,
  COBIWM_FRAME_CONTROL_MAXIMIZE,
  COBIWM_FRAME_CONTROL_UNMAXIMIZE,
  COBIWM_FRAME_CONTROL_RESIZE_SE,
  COBIWM_FRAME_CONTROL_RESIZE_S,
  COBIWM_FRAME_CONTROL_RESIZE_SW,
  COBIWM_FRAME_CONTROL_RESIZE_N,
  COBIWM_FRAME_CONTROL_RESIZE_NE,
  COBIWM_FRAME_CONTROL_RESIZE_NW,
  COBIWM_FRAME_CONTROL_RESIZE_W,
  COBIWM_FRAME_CONTROL_RESIZE_E,
  COBIWM_FRAME_CONTROL_CLIENT_AREA
} CobiwmFrameControl;

/* This is one widget that manages all the window frames
 * as subwindows.
 */

#define COBIWM_TYPE_FRAMES            (cobiwm_frames_get_type ())
#define COBIWM_FRAMES(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), COBIWM_TYPE_FRAMES, CobiwmFrames))
#define COBIWM_FRAMES_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), COBIWM_TYPE_FRAMES, CobiwmFramesClass))
#define COBIWM_IS_FRAMES(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), COBIWM_TYPE_FRAMES))
#define COBIWM_IS_FRAMES_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), COBIWM_TYPE_FRAMES))
#define COBIWM_FRAMES_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), COBIWM_TYPE_FRAMES, CobiwmFramesClass))

typedef struct _CobiwmFrames        CobiwmFrames;
typedef struct _CobiwmFramesClass   CobiwmFramesClass;

struct _CobiwmUIFrame
{
  CobiwmFrames *frames;
  CobiwmWindow *cobiwm_window;
  Window xwindow;
  GdkWindow *window;
  CobiwmStyleInfo *style_info;
  CobiwmFrameLayout *cache_layout;
  PangoLayout *text_layout;
  int text_height;
  char *title; /* NULL once we have a layout */
  guint maybe_ignore_leave_notify : 1;

  /* FIXME get rid of this, it can just be in the CobiwmFrames struct */
  CobiwmFrameControl prelit_control;
  CobiwmButtonState button_state;
  int grab_button;
};

struct _CobiwmFrames
{
  GtkWindow parent_instance;

  GHashTable *text_heights;

  GHashTable *frames;

  CobiwmStyleInfo *normal_style;
  GHashTable *style_variants;

  CobiwmGrabOp current_grab_op;
  CobiwmUIFrame *grab_frame;
  guint grab_button;
  gdouble grab_x;
  gdouble grab_y;
};

struct _CobiwmFramesClass
{
  GtkWindowClass parent_class;

};

GType        cobiwm_frames_get_type               (void) G_GNUC_CONST;

CobiwmFrames *cobiwm_frames_new (int screen_number);

CobiwmUIFrame * cobiwm_frames_manage_window (CobiwmFrames *frames,
                                         CobiwmWindow *cobiwm_window,
                                         Window      xwindow,
                                         GdkWindow  *window);

void cobiwm_ui_frame_unmanage (CobiwmUIFrame *frame);

void cobiwm_ui_frame_set_title (CobiwmUIFrame *frame,
                              const char *title);

void cobiwm_ui_frame_update_style (CobiwmUIFrame *frame);

void cobiwm_ui_frame_get_borders (CobiwmUIFrame      *frame,
                                CobiwmFrameBorders *borders);

cairo_region_t * cobiwm_ui_frame_get_bounds (CobiwmUIFrame *frame);

void cobiwm_ui_frame_get_mask (CobiwmUIFrame *frame,
                             cairo_t     *cr);

void cobiwm_ui_frame_move_resize (CobiwmUIFrame *frame,
                                int x, int y, int width, int height);

void cobiwm_ui_frame_queue_draw (CobiwmUIFrame *frame);

gboolean cobiwm_ui_frame_handle_event (CobiwmUIFrame *frame, const ClutterEvent *event);

#endif
