/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/**
 * \file screen-private.h  Screens which Cobiwm manages
 *
 * Managing X screens.
 * This file contains methods on this class which are available to
 * routines in core but not outside it.  (See screen.h for the routines
 * which the rest of the world is allowed to use.)
 */

/*
 * Copyright (C) 2001 Havoc Pennington
 * Copyright (C) 2003 Rob Adams
 * Copyright (C) 2004-2006 Elijah Newren
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

#ifndef COBIWM_SCREEN_PRIVATE_H
#define COBIWM_SCREEN_PRIVATE_H

#include "display-private.h"
#include <screen.h>
#include <X11/Xutil.h>
#include "stack-tracker.h"
#include "ui.h"
#include "cobiwm-monitor-manager-private.h"

typedef void (* CobiwmScreenWindowFunc) (CobiwmWindow *window,
                                       gpointer    user_data);

#define COBIWM_WIREFRAME_XOR_LINE_WIDTH 2

struct _CobiwmScreen
{
  GObject parent_instance;

  CobiwmDisplay *display;
  int number;
  char *screen_name;
  Screen *xscreen;
  Window xroot;
  int default_depth;
  Visual *default_xvisual;
  CobiwmRectangle rect;  /* Size of screen; rect.x & rect.y are always 0 */
  CobiwmUI *ui;

  guint tile_preview_timeout_id;

  CobiwmWorkspace *active_workspace;

  /* This window holds the focus when we don't want to focus
   * any actual clients
   */
  Window no_focus_window;

  GList *workspaces;

  CobiwmStack *stack;
  CobiwmStackTracker *stack_tracker;

  CobiwmCursor current_cursor;

  Window wm_sn_selection_window;
  Atom wm_sn_atom;
  guint32 wm_sn_timestamp;

  CobiwmMonitorInfo *monitor_infos;
  int n_monitor_infos;
  int primary_monitor_index;
  gboolean has_xinerama_indices;

  /* Cache the current monitor */
  int last_monitor_index;

  GSList *startup_sequences;

  Window wm_cm_selection_window;
  guint work_area_later;
  guint check_fullscreen_later;

  int rows_of_workspaces;
  int columns_of_workspaces;
  CobiwmScreenCorner starting_corner;
  guint vertical_workspaces : 1;
  guint workspace_layout_overridden : 1;

  guint keys_grabbed : 1;

  int closing;

  /* Instead of unmapping withdrawn windows we can leave them mapped
   * and restack them below a guard window. When using a compositor
   * this allows us to provide live previews of unmapped windows */
  Window guard_window;

  Window composite_overlay_window;
};

struct _CobiwmScreenClass
{
  GObjectClass parent_class;

  void (*restacked)         (CobiwmScreen *);
  void (*workareas_changed) (CobiwmScreen *);
  void (*monitors_changed)  (CobiwmScreen *);
};

CobiwmScreen*   cobiwm_screen_new                 (CobiwmDisplay                *display,
                                               int                         number,
                                               guint32                     timestamp);
void          cobiwm_screen_free                (CobiwmScreen                 *screen,
                                               guint32                     timestamp);
void          cobiwm_screen_init_workspaces     (CobiwmScreen                 *screen);
void          cobiwm_screen_manage_all_windows  (CobiwmScreen                 *screen);
void          cobiwm_screen_foreach_window      (CobiwmScreen                 *screen,
                                               CobiwmListWindowsFlags        flags,
                                               CobiwmScreenWindowFunc        func,
                                               gpointer                    data);

void          cobiwm_screen_update_cursor       (CobiwmScreen                 *screen);

void          cobiwm_screen_update_tile_preview          (CobiwmScreen    *screen,
                                                        gboolean       delay);
void          cobiwm_screen_hide_tile_preview            (CobiwmScreen    *screen);

CobiwmWindow*   cobiwm_screen_get_mouse_window     (CobiwmScreen                 *screen,
                                                CobiwmWindow                 *not_this_one);

const CobiwmMonitorInfo* cobiwm_screen_get_current_monitor_info   (CobiwmScreen    *screen);
const CobiwmMonitorInfo* cobiwm_screen_get_current_monitor_info_for_pos   (CobiwmScreen    *screen,
                                                                       int x,
                                                                       int y);
const CobiwmMonitorInfo* cobiwm_screen_get_monitor_for_rect   (CobiwmScreen    *screen,
                                                           CobiwmRectangle *rect);
const CobiwmMonitorInfo* cobiwm_screen_calculate_monitor_for_window (CobiwmScreen    *screen,
                                                                 CobiwmWindow    *window);

const CobiwmMonitorInfo* cobiwm_screen_get_monitor_for_point (CobiwmScreen    *screen,
                                                          int            x,
                                                          int            y);


const CobiwmMonitorInfo* cobiwm_screen_get_monitor_neighbor (CobiwmScreen *screen,
                                                         int         which_monitor,
                                                         CobiwmScreenDirection dir);
void          cobiwm_screen_get_natural_monitor_list (CobiwmScreen *screen,
                                                    int**       monitors_list,
                                                    int*        n_monitors);

void          cobiwm_screen_update_workspace_layout (CobiwmScreen             *screen);
void          cobiwm_screen_update_workspace_names  (CobiwmScreen             *screen);
void          cobiwm_screen_queue_workarea_recalc   (CobiwmScreen             *screen);
void          cobiwm_screen_queue_check_fullscreen  (CobiwmScreen             *screen);


Window cobiwm_create_offscreen_window (Display *xdisplay,
                                     Window   parent,
                                     long     valuemask);

typedef struct CobiwmWorkspaceLayout CobiwmWorkspaceLayout;

struct CobiwmWorkspaceLayout
{
  int rows;
  int cols;
  int *grid;
  int grid_area;
  int current_row;
  int current_col;
};

void cobiwm_screen_calc_workspace_layout (CobiwmScreen          *screen,
                                        int                  num_workspaces,
                                        int                  current_space,
                                        CobiwmWorkspaceLayout *layout);
void cobiwm_screen_free_workspace_layout (CobiwmWorkspaceLayout *layout);

void     cobiwm_screen_minimize_all_on_active_workspace_except (CobiwmScreen *screen,
                                                              CobiwmWindow *keep);

/* Show/hide the desktop (temporarily hide all windows) */
void     cobiwm_screen_show_desktop        (CobiwmScreen *screen,
                                          guint32     timestamp);
void     cobiwm_screen_unshow_desktop      (CobiwmScreen *screen);

/* Update whether the destkop is being shown for the current active_workspace */
void     cobiwm_screen_update_showing_desktop_hint          (CobiwmScreen *screen);

gboolean cobiwm_screen_apply_startup_properties (CobiwmScreen *screen,
                                               CobiwmWindow *window);
void     cobiwm_screen_restacked (CobiwmScreen *screen);

void     cobiwm_screen_workspace_switched (CobiwmScreen         *screen,
                                         int                 from,
                                         int                 to,
                                         CobiwmMotionDirection direction);

void cobiwm_screen_set_active_workspace_hint (CobiwmScreen *screen);

void cobiwm_screen_create_guard_window (CobiwmScreen *screen);

gboolean cobiwm_screen_handle_xevent (CobiwmScreen *screen,
                                    XEvent     *xevent);

int cobiwm_screen_xinerama_index_to_monitor_index (CobiwmScreen *screen,
                                                 int         index);
int cobiwm_screen_monitor_index_to_xinerama_index (CobiwmScreen *screen,
                                                 int         index);

#endif
