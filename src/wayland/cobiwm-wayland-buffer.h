/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2014 Endless Mobile
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

#ifndef COBIWM_WAYLAND_BUFFER_H
#define COBIWM_WAYLAND_BUFFER_H

#include <cogl/cogl.h>
#include <cairo.h>
#include <wayland-server.h>

#include "cobiwm-wayland-types.h"

struct _CobiwmWaylandBuffer
{
  GObject parent;

  struct wl_resource *resource;
  struct wl_listener destroy_listener;

  CoglTexture *texture;
};

#define COBIWM_TYPE_WAYLAND_BUFFER (cobiwm_wayland_buffer_get_type ())
G_DECLARE_FINAL_TYPE (CobiwmWaylandBuffer, cobiwm_wayland_buffer,
                      COBIWM, WAYLAND_BUFFER, GObject);

CobiwmWaylandBuffer *     cobiwm_wayland_buffer_from_resource       (struct wl_resource    *resource);
CoglTexture *           cobiwm_wayland_buffer_ensure_texture      (CobiwmWaylandBuffer     *buffer);
void                    cobiwm_wayland_buffer_process_damage      (CobiwmWaylandBuffer     *buffer,
                                                                 cairo_region_t        *region);

#endif /* COBIWM_WAYLAND_BUFFER_H */
