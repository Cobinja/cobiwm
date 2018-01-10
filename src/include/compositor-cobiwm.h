/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2008 Matthew Allum
 * Copyright (C) 2007 Iain Holmes
 * Based on xcompmgr - (c) 2003 Keith Packard
 *          xfwm4    - (c) 2005-2007 Olivier Fourdan
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

#ifndef COBIWM_H_
#define COBIWM_H_

#include <clutter/clutter.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xfixes.h>

#include <types.h>
#include <compositor.h>
#include <cobiwm-window-actor.h>

/* Public compositor API */
ClutterActor *cobiwm_get_stage_for_screen         (CobiwmScreen *screen);
Window        cobiwm_get_overlay_window           (CobiwmScreen *screen);
GList        *cobiwm_get_window_actors            (CobiwmScreen *screen);
ClutterActor *cobiwm_get_window_group_for_screen  (CobiwmScreen *screen);
ClutterActor *cobiwm_get_top_window_group_for_screen (CobiwmScreen *screen);
ClutterActor *cobiwm_get_feedback_group_for_screen (CobiwmScreen *screen);

void        cobiwm_disable_unredirect_for_screen  (CobiwmScreen *screen);
void        cobiwm_enable_unredirect_for_screen   (CobiwmScreen *screen);

void cobiwm_set_stage_input_region     (CobiwmScreen    *screen,
                                      XserverRegion  region);
void cobiwm_empty_stage_input_region   (CobiwmScreen    *screen);
void cobiwm_focus_stage_window         (CobiwmScreen    *screen,
                                      guint32        timestamp);
gboolean cobiwm_stage_is_focused       (CobiwmScreen    *screen);

#endif
