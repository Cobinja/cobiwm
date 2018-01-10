/*
 * Copyright (C) 2012 Intel Corporation
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

#ifndef COBIWM_WAYLAND_PRIVATE_H
#define COBIWM_WAYLAND_PRIVATE_H

#include <wayland-server.h>
#include <clutter/clutter.h>

#include <glib.h>

#include "window-private.h"
#include <cobiwm-cursor-tracker.h>

#include "cobiwm-wayland.h"
#include "cobiwm-wayland-versions.h"
#include "cobiwm-wayland-surface.h"
#include "cobiwm-wayland-seat.h"
#include "cobiwm-wayland-pointer-gestures.h"

typedef struct _CobiwmXWaylandSelection CobiwmXWaylandSelection;

typedef struct
{
  struct wl_list link;
  struct wl_resource *resource;
  CobiwmWaylandSurface *surface;
} CobiwmWaylandFrameCallback;

typedef struct
{
  int display_index;
  char *lock_file;
  int abstract_fd;
  int unix_fd;
  pid_t pid;
  struct wl_client *client;
  struct wl_resource *xserver_resource;
  char *display_name;

  GMainLoop *init_loop;

  CobiwmXWaylandSelection *selection_data;
} CobiwmXWaylandManager;

struct _CobiwmWaylandCompositor
{
  struct wl_display *wayland_display;
  const char *display_name;
  GHashTable *outputs;
  struct wl_list frame_callbacks;

  CobiwmXWaylandManager xwayland_manager;

  CobiwmWaylandSeat *seat;
};

#endif /* COBIWM_WAYLAND_PRIVATE_H */
