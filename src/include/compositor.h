/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2008 Iain Holmes
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

#ifndef COBIWM_COMPOSITOR_H
#define COBIWM_COMPOSITOR_H

#include <glib.h>
#include <X11/Xlib.h>

#include <types.h>
#include <boxes.h>
#include <window.h>
#include <workspace.h>

/**
 * CobiwmCompEffect:
 * @COBIWM_COMP_EFFECT_CREATE: The window is newly created
 *   (also used for a window that was previously on a different
 *   workspace and is changed to become visible on the active
 *   workspace.)
 * @COBIWM_COMP_EFFECT_UNMINIMIZE: The window should be shown
 *   as unminimizing from its icon geometry.
 * @COBIWM_COMP_EFFECT_DESTROY: The window is being destroyed
 * @COBIWM_COMP_EFFECT_MINIMIZE: The window should be shown
 *   as minimizing to its icon geometry.
 * @COBIWM_COMP_EFFECT_NONE: No effect, the window should be
 *   shown or hidden immediately.
 *
 * Indicates the appropriate effect to show the user for
 * cobiwm_compositor_show_window() and cobiwm_compositor_hide_window()
 */
typedef enum
{
  COBIWM_COMP_EFFECT_CREATE,
  COBIWM_COMP_EFFECT_UNMINIMIZE,
  COBIWM_COMP_EFFECT_DESTROY,
  COBIWM_COMP_EFFECT_MINIMIZE,
  COBIWM_COMP_EFFECT_NONE
} CobiwmCompEffect;

typedef enum {
  COBIWM_SIZE_CHANGE_MAXIMIZE,
  COBIWM_SIZE_CHANGE_UNMAXIMIZE,
  COBIWM_SIZE_CHANGE_FULLSCREEN,
  COBIWM_SIZE_CHANGE_UNFULLSCREEN,
} CobiwmSizeChange;

CobiwmCompositor *cobiwm_compositor_new     (CobiwmDisplay    *display);
void            cobiwm_compositor_destroy (CobiwmCompositor *compositor);

void cobiwm_compositor_manage   (CobiwmCompositor *compositor);
void cobiwm_compositor_unmanage (CobiwmCompositor *compositor);

void cobiwm_compositor_window_shape_changed (CobiwmCompositor *compositor,
                                           CobiwmWindow     *window);
void cobiwm_compositor_window_opacity_changed (CobiwmCompositor *compositor,
                                             CobiwmWindow     *window);
void cobiwm_compositor_window_surface_changed (CobiwmCompositor *compositor,
                                             CobiwmWindow     *window);

gboolean cobiwm_compositor_process_event (CobiwmCompositor *compositor,
                                        XEvent         *event,
                                        CobiwmWindow     *window);

void cobiwm_compositor_add_window        (CobiwmCompositor      *compositor,
                                        CobiwmWindow          *window);
void cobiwm_compositor_remove_window     (CobiwmCompositor      *compositor,
                                        CobiwmWindow          *window);
void cobiwm_compositor_show_window       (CobiwmCompositor      *compositor,
                                        CobiwmWindow          *window,
                                        CobiwmCompEffect       effect);
void cobiwm_compositor_hide_window       (CobiwmCompositor      *compositor,
                                        CobiwmWindow          *window,
                                        CobiwmCompEffect       effect);
void cobiwm_compositor_switch_workspace  (CobiwmCompositor      *compositor,
                                        CobiwmWorkspace       *from,
                                        CobiwmWorkspace       *to,
                                        CobiwmMotionDirection  direction);

void cobiwm_compositor_sync_window_geometry (CobiwmCompositor *compositor,
                                           CobiwmWindow     *window,
                                           gboolean        did_placement);
void cobiwm_compositor_sync_updates_frozen  (CobiwmCompositor *compositor,
                                           CobiwmWindow     *window);
void cobiwm_compositor_queue_frame_drawn    (CobiwmCompositor *compositor,
                                           CobiwmWindow     *window,
                                           gboolean        no_delay_frame);

void cobiwm_compositor_sync_stack                (CobiwmCompositor *compositor,
                                                GList          *stack);

void cobiwm_compositor_flash_screen              (CobiwmCompositor *compositor,
                                                CobiwmScreen     *screen);

void cobiwm_compositor_show_tile_preview (CobiwmCompositor *compositor,
                                        CobiwmWindow     *window,
                                        CobiwmRectangle  *tile_rect,
                                        int             tile_monitor_number);
void cobiwm_compositor_hide_tile_preview (CobiwmCompositor *compositor);
void cobiwm_compositor_show_window_menu (CobiwmCompositor     *compositor,
                                       CobiwmWindow         *window,
				       CobiwmWindowMenuType  menu,
                                       int                 x,
                                       int                 y);
void cobiwm_compositor_show_window_menu_for_rect (CobiwmCompositor     *compositor,
                                                CobiwmWindow         *window,
				                CobiwmWindowMenuType  menu,
                                                CobiwmRectangle      *rect);

#endif /* COBIWM_COMPOSITOR_H */
