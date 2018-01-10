/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2015 Red Hat
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
 *     Jonas Ådahl <jadahl@gmail.com>
 */

#ifndef COBIWM_WAYLAND_DATA_DEVICE_PRIVATE_H
#define COBIWM_WAYLAND_DATA_DEVICE_PRIVATE_H

#define COBIWM_TYPE_WAYLAND_DATA_SOURCE_WAYLAND (cobiwm_wayland_data_source_wayland_get_type ())
G_DECLARE_FINAL_TYPE (CobiwmWaylandDataSourceWayland,
                      cobiwm_wayland_data_source_wayland,
                      COBIWM, WAYLAND_DATA_SOURCE_WAYLAND,
                      CobiwmWaylandDataSource);

#define COBIWM_TYPE_WAYLAND_DATA_SOURCE_PRIMARY (cobiwm_wayland_data_source_primary_get_type ())
G_DECLARE_FINAL_TYPE (CobiwmWaylandDataSourcePrimary,
                      cobiwm_wayland_data_source_primary,
                      COBIWM, WAYLAND_DATA_SOURCE_PRIMARY,
                      CobiwmWaylandDataSourceWayland);

#endif /* COBIWM_WAYLAND_DATA_DEVICE_PRIVATE_H */
