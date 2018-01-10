/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2013 Red Hat Inc.
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

/* This file is shared between cobiwm (src/core/cobiwm-display-config-shared.h)
   and gnome-desktop (libgnome-desktop/cobiwm-xrandr-shared.h).

   The canonical place for all changes is cobiwm.

   There should be no includes in this file.
*/

#ifndef COBIWM_DISPLAY_CONFIG_SHARED_H
#define COBIWM_DISPLAY_CONFIG_SHARED_H

typedef enum {
  COBIWM_POWER_SAVE_UNSUPPORTED = -1,
  COBIWM_POWER_SAVE_ON = 0,
  COBIWM_POWER_SAVE_STANDBY,
  COBIWM_POWER_SAVE_SUSPEND,
  COBIWM_POWER_SAVE_OFF,
} CobiwmPowerSave;

#endif /* COBIWM_DISPLAY_CONFIG_SHARED_H */
