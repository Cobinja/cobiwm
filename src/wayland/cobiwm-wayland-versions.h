/*
 * Wayland Support
 *
 * Copyright (C) 2012,2013 Intel Corporation
 *               2013 Red Hat, Inc.
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

#ifndef COBIWM_WAYLAND_VERSIONS_H
#define COBIWM_WAYLAND_VERSIONS_H

/* Protocol objects, will never change version */
/* #define COBIWM_WL_DISPLAY_VERSION  1 */
/* #define COBIWM_WL_REGISTRY_VERSION 1 */
#define COBIWM_WL_CALLBACK_VERSION 1

/* Not handled by cobiwm-wayland directly */
/* #define COBIWM_WL_SHM_VERSION        1 */
/* #define COBIWM_WL_SHM_POOL_VERSION   1 */
/* #define COBIWM_WL_DRM_VERSION        1 */
/* #define COBIWM_WL_BUFFER_VERSION     1 */

/* Global/master objects (version exported by wl_registry and negotiated through bind) */
#define COBIWM_WL_COMPOSITOR_VERSION          3
#define COBIWM_WL_DATA_DEVICE_MANAGER_VERSION 3
#define COBIWM_XDG_SHELL_VERSION              1
#define COBIWM_WL_SHELL_VERSION               1
#define COBIWM_WL_SEAT_VERSION                5
#define COBIWM_WL_OUTPUT_VERSION              2
#define COBIWM_XSERVER_VERSION                1
#define COBIWM_GTK_SHELL1_VERSION             1
#define COBIWM_WL_SUBCOMPOSITOR_VERSION       1
#define COBIWM_ZWP_POINTER_GESTURES_V1_VERSION    1

#endif
