/*
 * Copyright (C) 2013 Red Hat, Inc.
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

#ifndef COBIWM_LAUNCHER_H
#define COBIWM_LAUNCHER_H

#include <glib-object.h>

typedef struct _CobiwmLauncher CobiwmLauncher;

CobiwmLauncher     *cobiwm_launcher_new                     (GError       **error);
void              cobiwm_launcher_free                    (CobiwmLauncher  *self);

gboolean          cobiwm_launcher_activate_session        (CobiwmLauncher  *self,
							 GError       **error);

gboolean          cobiwm_launcher_activate_vt             (CobiwmLauncher  *self,
							 signed char    vt,
							 GError       **error);

#endif /* COBIWM_LAUNCHER_H */
