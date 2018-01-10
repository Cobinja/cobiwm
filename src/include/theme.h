/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Cobiwmcity Theme Rendering */

/*
 * Copyright (C) 2001 Havoc Pennington
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

#ifndef COBIWM_THEME_H
#define COBIWM_THEME_H

#include <glib.h>

/**
 * CobiwmTheme:
 *
 */
typedef struct _CobiwmTheme CobiwmTheme;

CobiwmTheme* cobiwm_theme_get_default (void);

CobiwmTheme* cobiwm_theme_new      (void);
void       cobiwm_theme_free     (CobiwmTheme *theme);
#endif
