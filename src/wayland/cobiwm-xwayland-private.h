/*
 * Copyright (C) 2013 Intel Corporation
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
 */

#ifndef COBIWM_XWAYLAND_PRIVATE_H
#define COBIWM_XWAYLAND_PRIVATE_H

#include "cobiwm-wayland-private.h"

#include <glib.h>

gboolean
cobiwm_xwayland_start (CobiwmXWaylandManager *manager,
                     struct wl_display   *display);

void
cobiwm_xwayland_complete_init (void);

void
cobiwm_xwayland_stop (CobiwmXWaylandManager *manager);

/* wl_data_device/X11 selection interoperation */
void     cobiwm_xwayland_init_selection         (void);
void     cobiwm_xwayland_shutdown_selection     (void);
gboolean cobiwm_xwayland_selection_handle_event (XEvent *xevent);

const CobiwmWaylandDragDestFuncs * cobiwm_xwayland_selection_get_drag_dest_funcs (void);

#endif /* COBIWM_XWAYLAND_PRIVATE_H */
