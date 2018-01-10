/*
 * Copyright (C) 2009 Intel Corporation.
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

#ifndef COBIWM_KEYBINDINGS_H
#define COBIWM_KEYBINDINGS_H

#include <display.h>
#include <common.h>

#define COBIWM_TYPE_KEY_BINDING               (cobiwm_key_binding_get_type ())

const char          *cobiwm_key_binding_get_name      (CobiwmKeyBinding *binding);
CobiwmVirtualModifier  cobiwm_key_binding_get_modifiers (CobiwmKeyBinding *binding);
guint                cobiwm_key_binding_get_mask      (CobiwmKeyBinding *binding);
gboolean             cobiwm_key_binding_is_builtin    (CobiwmKeyBinding *binding);
gboolean             cobiwm_key_binding_is_reversed   (CobiwmKeyBinding *binding);

gboolean cobiwm_keybindings_set_custom_handler (const gchar        *name,
					      CobiwmKeyHandlerFunc  handler,
					      gpointer            user_data,
					      GDestroyNotify      free_data);
#endif
