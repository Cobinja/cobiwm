/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

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

#ifndef COBIWM_BACKEND_H
#define COBIWM_BACKEND_H

#include <glib-object.h>

#include <clutter/clutter.h>

typedef struct _CobiwmBackend        CobiwmBackend;
typedef struct _CobiwmBackendClass   CobiwmBackendClass;

GType cobiwm_backend_get_type (void);

CobiwmBackend * cobiwm_get_backend (void);

void cobiwm_backend_set_keymap (CobiwmBackend *backend,
                              const char  *layouts,
                              const char  *variants,
                              const char  *options);

void cobiwm_backend_lock_layout_group (CobiwmBackend *backend,
                                     guint        idx);

ClutterActor *cobiwm_backend_get_stage (CobiwmBackend *backend);

void cobiwm_clutter_init (void);

#endif /* COBIWM_BACKEND_H */
