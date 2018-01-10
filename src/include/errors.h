/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Cobiwm X error handling */

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

#ifndef COBIWM_ERRORS_H
#define COBIWM_ERRORS_H

#include <X11/Xlib.h>

#include <util.h>
#include <display.h>

void      cobiwm_error_trap_push (CobiwmDisplay *display);
void      cobiwm_error_trap_pop  (CobiwmDisplay *display);

/* returns X error code, or 0 for no error */
int       cobiwm_error_trap_pop_with_return  (CobiwmDisplay *display);


#endif
