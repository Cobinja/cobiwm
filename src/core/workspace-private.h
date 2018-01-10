/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/**
 * \file workspace.h    Workspaces
 *
 * A workspace is a set of windows which all live on the same
 * screen.  (You may also see the name "desktop" around the place,
 * which is the EWMH's name for the same thing.)  Only one workspace
 * of a screen may be active at once; all windows on all other workspaces
 * are unmapped.
 */

/*
 * Copyright (C) 2001 Havoc Pennington
 * Copyright (C) 2004, 2005 Elijah Newren
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

#ifndef COBIWM_WORKSPACE_PRIVATE_H
#define COBIWM_WORKSPACE_PRIVATE_H

#include <workspace.h>
#include "window-private.h"

struct _CobiwmWorkspace
{
  GObject parent_instance;
  CobiwmScreen *screen;

  GList *windows;

  /* The "MRU list", or "most recently used" list, is a list of
   * CobiwmWindows ordered based on the time the the user interacted
   * with the window most recently.
   *
   * For historical reasons, we keep an MRU list per workspace.
   * It used to be used to calculate the default focused window,
   * but isn't anymore, as the window next in the stacking order
   * can sometimes be not the window the user interacted with last,
   */
  GList *mru_list;

  GList  *list_containing_self;

  CobiwmRectangle work_area_screen;
  CobiwmRectangle *work_area_monitor;
  GList  *screen_region;
  GList  **monitor_region;
  gint n_monitor_regions;
  GList  *screen_edges;
  GList  *monitor_edges;
  GSList *builtin_struts;
  GSList *all_struts;
  guint work_areas_invalid : 1;

  guint showing_desktop : 1;
};

struct _CobiwmWorkspaceClass
{
  GObjectClass parent_class;
};

CobiwmWorkspace* cobiwm_workspace_new           (CobiwmScreen    *screen);
void           cobiwm_workspace_remove        (CobiwmWorkspace *workspace);
void           cobiwm_workspace_add_window    (CobiwmWorkspace *workspace,
                                             CobiwmWindow    *window);
void           cobiwm_workspace_remove_window (CobiwmWorkspace *workspace,
                                             CobiwmWindow    *window);
void           cobiwm_workspace_relocate_windows (CobiwmWorkspace *workspace,
                                                CobiwmWorkspace *new_home);

void cobiwm_workspace_invalidate_work_area (CobiwmWorkspace *workspace);

GList* cobiwm_workspace_get_onscreen_region       (CobiwmWorkspace *workspace);
GList* cobiwm_workspace_get_onmonitor_region      (CobiwmWorkspace *workspace,
                                                 int            which_monitor);

void cobiwm_workspace_focus_default_window (CobiwmWorkspace *workspace,
                                          CobiwmWindow    *not_this_one,
                                          guint32        timestamp);

const char* cobiwm_workspace_get_name (CobiwmWorkspace *workspace);

void cobiwm_workspace_index_changed (CobiwmWorkspace *workspace);

#endif
