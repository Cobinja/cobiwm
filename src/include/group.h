/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Cobiwm window groups */

/*
 * Copyright (C) 2002 Red Hat Inc.
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

#ifndef COBIWM_GROUP_H
#define COBIWM_GROUP_H

#include <X11/Xlib.h>
#include <glib.h>
#include <types.h>

/* note, can return NULL */
CobiwmGroup* cobiwm_window_get_group       (CobiwmWindow *window);
void       cobiwm_window_compute_group   (CobiwmWindow* window);
void       cobiwm_window_shutdown_group  (CobiwmWindow *window);

void       cobiwm_window_group_leader_changed (CobiwmWindow *window);

/* note, can return NULL */
CobiwmGroup* cobiwm_display_lookup_group   (CobiwmDisplay *display,
                                        Window       group_leader);

GSList*    cobiwm_group_list_windows     (CobiwmGroup *group);

void       cobiwm_group_update_layers    (CobiwmGroup *group);

const char* cobiwm_group_get_startup_id  (CobiwmGroup *group);

int        cobiwm_group_get_size         (CobiwmGroup *group);

gboolean cobiwm_group_property_notify   (CobiwmGroup  *group,
                                       XEvent     *event);

#endif




