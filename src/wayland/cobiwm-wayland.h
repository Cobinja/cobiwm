/*
 * Copyright (C) 2014 Red Hat
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Written by:
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

#ifndef COBIWM_WAYLAND_H
#define COBIWM_WAYLAND_H

#include <clutter/clutter.h>
#include <types.h>
#include "cobiwm-wayland-types.h"

void                    cobiwm_wayland_pre_clutter_init           (void);
void                    cobiwm_wayland_init                       (void);
void                    cobiwm_wayland_finalize                   (void);

/* We maintain a singleton CobiwmWaylandCompositor which can be got at via this
 * API after cobiwm_wayland_init() has been called. */
CobiwmWaylandCompositor  *cobiwm_wayland_compositor_get_default     (void);

void                    cobiwm_wayland_compositor_update          (CobiwmWaylandCompositor *compositor,
                                                                 const ClutterEvent    *event);
gboolean                cobiwm_wayland_compositor_handle_event    (CobiwmWaylandCompositor *compositor,
                                                                 const ClutterEvent    *event);
void                    cobiwm_wayland_compositor_update_key_state (CobiwmWaylandCompositor *compositor,
                                                                 char                  *key_vector,
                                                                  int                    key_vector_len,
                                                                  int                    offset);
void                    cobiwm_wayland_compositor_repick          (CobiwmWaylandCompositor *compositor);

void                    cobiwm_wayland_compositor_set_input_focus (CobiwmWaylandCompositor *compositor,
                                                                 CobiwmWindow            *window);

void                    cobiwm_wayland_compositor_paint_finished  (CobiwmWaylandCompositor *compositor);

void                    cobiwm_wayland_compositor_destroy_frame_callbacks (CobiwmWaylandCompositor *compositor,
                                                                         CobiwmWaylandSurface    *surface);

const char             *cobiwm_wayland_get_wayland_display_name   (CobiwmWaylandCompositor *compositor);
const char             *cobiwm_wayland_get_xwayland_display_name  (CobiwmWaylandCompositor *compositor);

#endif

