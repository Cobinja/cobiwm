/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/**
 * \file keybindings.h  Grab and ungrab keys, and process the key events
 *
 * Performs global X grabs on the keys we need to be told about, like
 * the one to close a window.  It also deals with incoming key events.
 */

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

#ifndef COBIWM_KEYBINDINGS_PRIVATE_H
#define COBIWM_KEYBINDINGS_PRIVATE_H

#include <gio/gio.h>
#include <keybindings.h>
#include <xkbcommon/xkbcommon.h>
#include "cobiwm-accel-parse.h"

typedef struct _CobiwmKeyHandler CobiwmKeyHandler;
struct _CobiwmKeyHandler
{
  char *name;
  CobiwmKeyHandlerFunc func;
  CobiwmKeyHandlerFunc default_func;
  gint data, flags;
  gpointer user_data;
  GDestroyNotify user_data_free_func;
};

typedef struct _CobiwmResolvedKeyCombo {
  xkb_keycode_t keycode;
  xkb_mod_mask_t mask;
} CobiwmResolvedKeyCombo;

/**
 * CobiwmKeyCombo:
 * @keysym: keysym
 * @keycode: keycode
 * @modifiers: modifiers
 */
struct _CobiwmKeyCombo
{
  unsigned int keysym;
  unsigned int keycode;
  CobiwmVirtualModifier modifiers;
};

struct _CobiwmKeyBinding
{
  const char *name;
  CobiwmKeyCombo combo;
  CobiwmResolvedKeyCombo resolved_combo;
  gint flags;
  CobiwmKeyHandler *handler;
};

typedef struct
{
  char *name;
  GSettings *settings;

  CobiwmKeyBindingAction action;

  /*
   * A list of CobiwmKeyCombos. Each of them is bound to
   * this keypref. If one has keysym==modifiers==0, it is
   * ignored.
   */
  GSList *combos;

  /* for keybindings not added with cobiwm_display_add_keybinding() */
  gboolean      builtin:1;
} CobiwmKeyPref;

typedef struct
{
  GHashTable     *key_bindings;
  GHashTable     *key_bindings_index;
  xkb_mod_mask_t ignored_modifier_mask;
  xkb_mod_mask_t hyper_mask;
  xkb_mod_mask_t virtual_hyper_mask;
  xkb_mod_mask_t super_mask;
  xkb_mod_mask_t virtual_super_mask;
  xkb_mod_mask_t cobiwm_mask;
  xkb_mod_mask_t virtual_cobiwm_mask;
  CobiwmKeyCombo overlay_key_combo;
  CobiwmResolvedKeyCombo overlay_resolved_key_combo;
  gboolean overlay_key_only_pressed;
  CobiwmResolvedKeyCombo *iso_next_group_combos;
  int n_iso_next_group_combos;

  xkb_level_index_t keymap_num_levels;

  /* Alt+click button grabs */
  ClutterModifierType window_grab_modifiers;
} CobiwmKeyBindingManager;

void     cobiwm_display_init_keys             (CobiwmDisplay *display);
void     cobiwm_display_shutdown_keys         (CobiwmDisplay *display);
void     cobiwm_screen_grab_keys              (CobiwmScreen  *screen);
void     cobiwm_screen_ungrab_keys            (CobiwmScreen  *screen);
void     cobiwm_window_grab_keys              (CobiwmWindow  *window);
void     cobiwm_window_ungrab_keys            (CobiwmWindow  *window);
gboolean cobiwm_window_grab_all_keys          (CobiwmWindow  *window,
                                             guint32      timestamp);
void     cobiwm_window_ungrab_all_keys        (CobiwmWindow  *window,
                                             guint32      timestamp);
gboolean cobiwm_keybindings_process_event     (CobiwmDisplay        *display,
                                             CobiwmWindow         *window,
                                             const ClutterEvent *event);

ClutterModifierType cobiwm_display_get_window_grab_modifiers (CobiwmDisplay *display);

gboolean cobiwm_prefs_add_keybinding          (const char           *name,
                                             GSettings            *settings,
                                             CobiwmKeyBindingAction  action,
                                             CobiwmKeyBindingFlags   flags);

gboolean cobiwm_prefs_remove_keybinding       (const char    *name);

GList *cobiwm_prefs_get_keybindings (void);
void cobiwm_prefs_get_overlay_binding (CobiwmKeyCombo *combo);
const char *cobiwm_prefs_get_iso_next_group_option (void);

#endif
