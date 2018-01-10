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

#ifndef COBIWM_SCREEN_H
#define COBIWM_SCREEN_H

#include <X11/Xlib.h>
#include <glib-object.h>
#include <types.h>
#include <workspace.h>

#define COBIWM_TYPE_SCREEN            (cobiwm_screen_get_type ())
#define COBIWM_SCREEN(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), COBIWM_TYPE_SCREEN, CobiwmScreen))
#define COBIWM_SCREEN_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  COBIWM_TYPE_SCREEN, CobiwmScreenClass))
#define COBIWM_IS_SCREEN(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), COBIWM_TYPE_SCREEN))
#define COBIWM_IS_SCREEN_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  COBIWM_TYPE_SCREEN))
#define COBIWM_SCREEN_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  COBIWM_TYPE_SCREEN, CobiwmScreenClass))

typedef struct _CobiwmScreenClass   CobiwmScreenClass;

GType cobiwm_screen_get_type (void);

int cobiwm_screen_get_screen_number (CobiwmScreen *screen);
CobiwmDisplay *cobiwm_screen_get_display (CobiwmScreen *screen);

Window cobiwm_screen_get_xroot (CobiwmScreen *screen);
void cobiwm_screen_get_size (CobiwmScreen *screen,
                           int        *width,
                           int        *height);

void cobiwm_screen_set_cm_selection (CobiwmScreen *screen);

GSList *cobiwm_screen_get_startup_sequences (CobiwmScreen *screen);

GList *cobiwm_screen_get_workspaces (CobiwmScreen *screen);

int cobiwm_screen_get_n_workspaces (CobiwmScreen *screen);

CobiwmWorkspace* cobiwm_screen_get_workspace_by_index (CobiwmScreen    *screen,
                                                   int            index);
void cobiwm_screen_remove_workspace (CobiwmScreen    *screen,
                                   CobiwmWorkspace *workspace,
                                   guint32        timestamp);

CobiwmWorkspace *cobiwm_screen_append_new_workspace (CobiwmScreen    *screen,
                                                 gboolean       activate,
                                                 guint32        timestamp);

int cobiwm_screen_get_active_workspace_index (CobiwmScreen *screen);

CobiwmWorkspace * cobiwm_screen_get_active_workspace (CobiwmScreen *screen);

/**
 * CobiwmScreenDirection:
 * @COBIWM_SCREEN_UP: up
 * @COBIWM_SCREEN_DOWN: down
 * @COBIWM_SCREEN_LEFT: left
 * @COBIWM_SCREEN_RIGHT: right
 */
typedef enum
{
  COBIWM_SCREEN_UP,
  COBIWM_SCREEN_DOWN,
  COBIWM_SCREEN_LEFT,
  COBIWM_SCREEN_RIGHT
} CobiwmScreenDirection;

int  cobiwm_screen_get_n_monitors       (CobiwmScreen    *screen);
int  cobiwm_screen_get_primary_monitor  (CobiwmScreen    *screen);
int  cobiwm_screen_get_current_monitor  (CobiwmScreen    *screen);
int  cobiwm_screen_get_current_monitor_for_pos  (CobiwmScreen    *screen,
                                               int x,
                                               int y);
void cobiwm_screen_get_monitor_geometry (CobiwmScreen    *screen,
                                       int            monitor,
                                       CobiwmRectangle *geometry);

gboolean cobiwm_screen_get_monitor_in_fullscreen (CobiwmScreen  *screen,
                                                int          monitor);

int cobiwm_screen_get_monitor_index_for_rect (CobiwmScreen    *screen,
                                            CobiwmRectangle *rect);

int cobiwm_screen_get_monitor_neighbor_index (CobiwmScreen *screen,
                                            int         which_monitor,
                                            CobiwmScreenDirection dir);

void cobiwm_screen_focus_default_window (CobiwmScreen *screen,
                                       guint32     timestamp);

/**
 * CobiwmScreenCorner:
 * @COBIWM_SCREEN_TOPLEFT: top-left corner
 * @COBIWM_SCREEN_TOPRIGHT: top-right corner
 * @COBIWM_SCREEN_BOTTOMLEFT: bottom-left corner
 * @COBIWM_SCREEN_BOTTOMRIGHT: bottom-right corner
 */
typedef enum
{
  COBIWM_SCREEN_TOPLEFT,
  COBIWM_SCREEN_TOPRIGHT,
  COBIWM_SCREEN_BOTTOMLEFT,
  COBIWM_SCREEN_BOTTOMRIGHT
} CobiwmScreenCorner;

void cobiwm_screen_override_workspace_layout (CobiwmScreen      *screen,
                                            CobiwmScreenCorner starting_corner,
                                            gboolean         vertical_layout,
                                            int              n_rows,
                                            int              n_columns);

void          cobiwm_screen_set_cursor          (CobiwmScreen                 *screen,
                                               CobiwmCursor                  cursor);

#endif
