/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

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

#ifndef COBIWM_WORKSPACE_H
#define COBIWM_WORKSPACE_H

#include <types.h>
#include <boxes.h>
#include <screen.h>

#define COBIWM_TYPE_WORKSPACE            (cobiwm_workspace_get_type ())
#define COBIWM_WORKSPACE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), COBIWM_TYPE_WORKSPACE, CobiwmWorkspace))
#define COBIWM_WORKSPACE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  COBIWM_TYPE_WORKSPACE, CobiwmWorkspaceClass))
#define COBIWM_IS_WORKSPACE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), COBIWM_TYPE_WORKSPACE))
#define COBIWM_IS_WORKSPACE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  COBIWM_TYPE_WORKSPACE))
#define COBIWM_WORKSPACE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  COBIWM_TYPE_WORKSPACE, CobiwmWorkspaceClass))

typedef struct _CobiwmWorkspaceClass   CobiwmWorkspaceClass;

GType cobiwm_workspace_get_type (void);

int  cobiwm_workspace_index (CobiwmWorkspace *workspace);
CobiwmScreen *cobiwm_workspace_get_screen (CobiwmWorkspace *workspace);
GList* cobiwm_workspace_list_windows (CobiwmWorkspace *workspace);
void cobiwm_workspace_get_work_area_for_monitor (CobiwmWorkspace *workspace,
                                               int            which_monitor,
                                               CobiwmRectangle *area);
void cobiwm_workspace_get_work_area_all_monitors (CobiwmWorkspace *workspace,
                                                CobiwmRectangle *area);
void cobiwm_workspace_activate (CobiwmWorkspace *workspace, guint32 timestamp);
void cobiwm_workspace_activate_with_focus (CobiwmWorkspace *workspace,
                                         CobiwmWindow    *focus_this,
                                         guint32        timestamp);

void cobiwm_workspace_set_builtin_struts (CobiwmWorkspace *workspace,
                                        GSList        *struts);

CobiwmWorkspace* cobiwm_workspace_get_neighbor (CobiwmWorkspace      *workspace,
                                            CobiwmMotionDirection direction);

#endif
