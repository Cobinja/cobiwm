/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Cobiwm utilities */

/*
 * Copyright (C) 2001 Havoc Pennington
 * Copyright (C) 2005 Elijah Newren
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

#ifndef COBIWM_UTIL_PRIVATE_H
#define COBIWM_UTIL_PRIVATE_H

#include <util.h>
#include <glib/gi18n-lib.h>

void     cobiwm_set_verbose (gboolean setting);
void     cobiwm_set_debugging (gboolean setting);
void     cobiwm_set_syncing (gboolean setting);
void     cobiwm_set_replace_current_wm (gboolean setting);
void     cobiwm_set_is_wayland_compositor (gboolean setting);

#endif
