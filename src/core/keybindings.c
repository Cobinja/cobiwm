/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Cobiwm Keybindings */
/*
 * Copyright (C) 2001 Havoc Pennington
 * Copyright (C) 2002 Red Hat Inc.
 * Copyright (C) 2003 Rob Adams
 * Copyright (C) 2004-2006 Elijah Newren
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

/**
 * SECTION:keybindings
 * @Title: CobiwmKeybinding
 * @Short_Description: Key bindings
 */

#define _GNU_SOURCE

#include <config.h>
#include "keybindings-private.h"
#include "workspace-private.h"
#include <compositor.h>
#include <errors.h>
#include "edge-resistance.h"
#include "frame.h"
#include "screen-private.h"
#include <prefs.h>
#include "cobiwm-accel-parse.h"

#ifdef __linux__
#include <linux/input.h>
#elif !defined KEY_GRAVE
#define KEY_GRAVE 0x29 /* assume the use of xf86-input-keyboard */
#endif

#include "backends/x11/cobiwm-backend-x11.h"
#include "x11/window-x11.h"

#ifdef HAVE_NATIVE_BACKEND
#include "backends/native/cobiwm-backend-native.h"
#endif

#define SCHEMA_COBIWM_KEYBINDINGS "org.cobiwm.keybindings"
#define SCHEMA_COBIWM_WAYLAND_KEYBINDINGS "org.cobiwm.wayland.keybindings"

static gboolean add_builtin_keybinding (CobiwmDisplay          *display,
                                        const char           *name,
                                        GSettings            *settings,
                                        CobiwmKeyBindingFlags   flags,
                                        CobiwmKeyBindingAction  action,
                                        CobiwmKeyHandlerFunc    handler,
                                        int                   handler_arg);

static void
cobiwm_key_binding_free (CobiwmKeyBinding *binding)
{
  g_slice_free (CobiwmKeyBinding, binding);
}

static CobiwmKeyBinding *
cobiwm_key_binding_copy (CobiwmKeyBinding *binding)
{
  return g_slice_dup (CobiwmKeyBinding, binding);
}

G_DEFINE_BOXED_TYPE(CobiwmKeyBinding,
                    cobiwm_key_binding,
                    cobiwm_key_binding_copy,
                    cobiwm_key_binding_free)

const char *
cobiwm_key_binding_get_name (CobiwmKeyBinding *binding)
{
  return binding->name;
}

CobiwmVirtualModifier
cobiwm_key_binding_get_modifiers (CobiwmKeyBinding *binding)
{
  return binding->combo.modifiers;
}

gboolean
cobiwm_key_binding_is_reversed (CobiwmKeyBinding *binding)
{
  return (binding->handler->flags & COBIWM_KEY_BINDING_IS_REVERSED) != 0;
}

guint
cobiwm_key_binding_get_mask (CobiwmKeyBinding *binding)
{
  return binding->resolved_combo.mask;
}

gboolean
cobiwm_key_binding_is_builtin (CobiwmKeyBinding *binding)
{
  return binding->handler->flags & COBIWM_KEY_BINDING_BUILTIN;
}

/* These can't be bound to anything, but they are used to handle
 * various other events.  TODO: Possibly we should include them as event
 * handler functions and have some kind of flag to say they're unbindable.
 */

static gboolean process_mouse_move_resize_grab (CobiwmDisplay     *display,
                                                CobiwmScreen      *screen,
                                                CobiwmWindow      *window,
                                                ClutterKeyEvent *event);

static gboolean process_keyboard_move_grab (CobiwmDisplay     *display,
                                            CobiwmScreen      *screen,
                                            CobiwmWindow      *window,
                                            ClutterKeyEvent *event);

static gboolean process_keyboard_resize_grab (CobiwmDisplay     *display,
                                              CobiwmScreen      *screen,
                                              CobiwmWindow      *window,
                                              ClutterKeyEvent *event);

static void grab_key_bindings           (CobiwmDisplay *display);
static void ungrab_key_bindings         (CobiwmDisplay *display);

static GHashTable *key_handlers;
static GHashTable *external_grabs;

#define HANDLER(name) g_hash_table_lookup (key_handlers, (name))

static void
key_handler_free (CobiwmKeyHandler *handler)
{
  g_free (handler->name);
  if (handler->user_data_free_func && handler->user_data)
    handler->user_data_free_func (handler->user_data);
  g_free (handler);
}

typedef struct _CobiwmKeyGrab CobiwmKeyGrab;
struct _CobiwmKeyGrab {
  char *name;
  guint action;
  CobiwmKeyCombo combo;
};

static void
cobiwm_key_grab_free (CobiwmKeyGrab *grab)
{
  g_free (grab->name);
  g_free (grab);
}

static guint32
key_combo_key (CobiwmResolvedKeyCombo *resolved_combo)
{
  /* On X, keycodes are only 8 bits while libxkbcommon supports 32 bit
     keycodes, but since we're using the same XKB keymaps that X uses,
     we won't find keycodes bigger than 8 bits in practice. The bits
     that cobiwm cares about in the modifier mask are also all in the
     lower 8 bits both on X and clutter key events. This means that we
     can use a 32 bit integer to safely concatenate both keycode and
     mask and thus making it easy to use them as an index in a
     GHashTable. */
  guint32 key = resolved_combo->keycode & 0xffff;
  return (key << 16) | (resolved_combo->mask & 0xffff);
}

static void
reload_modmap (CobiwmKeyBindingManager *keys)
{
  CobiwmBackend *backend = cobiwm_get_backend ();
  struct xkb_keymap *keymap = cobiwm_backend_get_keymap (backend);
  struct xkb_state *scratch_state;
  xkb_mod_mask_t scroll_lock_mask;
  xkb_mod_mask_t dummy_mask;

  /* Modifiers to find. */
  struct {
    const char *name;
    xkb_mod_mask_t *mask_p;
    xkb_mod_mask_t *virtual_mask_p;
  } mods[] = {
    { "ScrollLock", &scroll_lock_mask, &dummy_mask },
    { "Cobiwm",       &keys->cobiwm_mask,  &keys->virtual_cobiwm_mask },
    { "Hyper",      &keys->hyper_mask, &keys->virtual_hyper_mask },
    { "Super",      &keys->super_mask, &keys->virtual_super_mask },
  };

  scratch_state = xkb_state_new (keymap);

  gsize i;
  for (i = 0; i < G_N_ELEMENTS (mods); i++)
    {
      xkb_mod_mask_t *mask_p = mods[i].mask_p;
      xkb_mod_mask_t *virtual_mask_p = mods[i].virtual_mask_p;
      xkb_mod_index_t idx = xkb_keymap_mod_get_index (keymap, mods[i].name);

      if (idx != XKB_MOD_INVALID)
        {
          xkb_mod_mask_t vmodmask = (1 << idx);
          xkb_state_update_mask (scratch_state, vmodmask, 0, 0, 0, 0, 0);
          *mask_p = xkb_state_serialize_mods (scratch_state, XKB_STATE_MODS_DEPRESSED) & ~vmodmask;
          *virtual_mask_p = vmodmask;
        }
      else
        {
          *mask_p = 0;
          *virtual_mask_p = 0;
        }
    }

  xkb_state_unref (scratch_state);

  keys->ignored_modifier_mask = (scroll_lock_mask | Mod2Mask | LockMask);

  cobiwm_topic (COBIWM_DEBUG_KEYBINDINGS,
              "Ignoring modmask 0x%x scroll lock 0x%x hyper 0x%x super 0x%x cobiwm 0x%x\n",
              keys->ignored_modifier_mask,
              scroll_lock_mask,
              keys->hyper_mask,
              keys->super_mask,
              keys->cobiwm_mask);
}

static gboolean
is_keycode_for_keysym (struct xkb_keymap *keymap,
                       xkb_layout_index_t layout,
                       xkb_level_index_t  level,
                       xkb_keycode_t      keycode,
                       xkb_keysym_t       keysym)
{
  const xkb_keysym_t *syms;
  int num_syms, k;

  num_syms = xkb_keymap_key_get_syms_by_level (keymap, keycode, layout, level, &syms);
  for (k = 0; k < num_syms; k++)
    {
      if (syms[k] == keysym)
        return TRUE;
    }

  return FALSE;
}

typedef struct
{
  GArray *keycodes;
  xkb_keysym_t keysym;
  xkb_layout_index_t layout;
  xkb_level_index_t level;
} FindKeysymData;

static void
get_keycodes_for_keysym_iter (struct xkb_keymap *keymap,
                              xkb_keycode_t      keycode,
                              void              *data)
{
  FindKeysymData *search_data = data;
  GArray *keycodes = search_data->keycodes;
  xkb_keysym_t keysym = search_data->keysym;
  xkb_layout_index_t layout = search_data->layout;
  xkb_level_index_t level = search_data->level;

  if (is_keycode_for_keysym (keymap, layout, level, keycode, keysym))
    g_array_append_val (keycodes, keycode);
}

/* Original code from gdk_x11_keymap_get_entries_for_keyval() in
 * gdkkeys-x11.c */
static int
get_keycodes_for_keysym (CobiwmKeyBindingManager  *keys,
                         int                     keysym,
                         int                   **keycodes)
{
  GArray *retval;
  int n_keycodes;
  int keycode;

  retval = g_array_new (FALSE, FALSE, sizeof (int));

  /* Special-case: Fake cobiwm keysym */
  if (keysym == COBIWM_KEY_ABOVE_TAB)
    {
      keycode = KEY_GRAVE + 8;
      g_array_append_val (retval, keycode);
      goto out;
    }

  {
    CobiwmBackend *backend = cobiwm_get_backend ();
    struct xkb_keymap *keymap = cobiwm_backend_get_keymap (backend);
    xkb_layout_index_t i;
    xkb_level_index_t j;

    for (i = 0; i < xkb_keymap_num_layouts (keymap); i++)
      for (j = 0; j < keys->keymap_num_levels; j++)
        {
          FindKeysymData search_data = { retval, keysym, i, j };
          xkb_keymap_key_for_each (keymap, get_keycodes_for_keysym_iter, &search_data);
        }
  }

 out:
  n_keycodes = retval->len;
  *keycodes = (int*) g_array_free (retval, n_keycodes == 0 ? TRUE : FALSE);
  return n_keycodes;
}

static guint
get_first_keycode_for_keysym (CobiwmKeyBindingManager *keys,
                              guint                  keysym)
{
  int *keycodes;
  int n_keycodes;
  int keycode;

  n_keycodes = get_keycodes_for_keysym (keys, keysym, &keycodes);

  if (n_keycodes > 0)
    keycode = keycodes[0];
  else
    keycode = 0;

  g_free (keycodes);
  return keycode;
}

static void
determine_keymap_num_levels_iter (struct xkb_keymap *keymap,
                                  xkb_keycode_t      keycode,
                                  void              *data)
{
  xkb_level_index_t *num_levels = data;
  xkb_layout_index_t i;

  for (i = 0; i < xkb_keymap_num_layouts_for_key (keymap, keycode); i++)
    {
      xkb_level_index_t level = xkb_keymap_num_levels_for_key (keymap, keycode, i);
      if (level > *num_levels)
        *num_levels = level;
    }
}

static void
determine_keymap_num_levels (CobiwmKeyBindingManager *keys)
{
  CobiwmBackend *backend = cobiwm_get_backend ();
  struct xkb_keymap *keymap = cobiwm_backend_get_keymap (backend);

  keys->keymap_num_levels = 0;
  xkb_keymap_key_for_each (keymap, determine_keymap_num_levels_iter, &keys->keymap_num_levels);
}

static void
reload_iso_next_group_combos (CobiwmKeyBindingManager *keys)
{
  const char *iso_next_group_option;
  CobiwmResolvedKeyCombo *combos;
  int *keycodes;
  int n_keycodes;
  int n_combos;
  int i;

  g_clear_pointer (&keys->iso_next_group_combos, g_free);
  keys->n_iso_next_group_combos = 0;

  iso_next_group_option = cobiwm_prefs_get_iso_next_group_option ();
  if (iso_next_group_option == NULL)
    return;

  n_keycodes = get_keycodes_for_keysym (keys, XKB_KEY_ISO_Next_Group, &keycodes);

  if (g_str_equal (iso_next_group_option, "toggle") ||
      g_str_equal (iso_next_group_option, "lalt_toggle") ||
      g_str_equal (iso_next_group_option, "lwin_toggle") ||
      g_str_equal (iso_next_group_option, "rwin_toggle") ||
      g_str_equal (iso_next_group_option, "lshift_toggle") ||
      g_str_equal (iso_next_group_option, "rshift_toggle") ||
      g_str_equal (iso_next_group_option, "lctrl_toggle") ||
      g_str_equal (iso_next_group_option, "rctrl_toggle") ||
      g_str_equal (iso_next_group_option, "sclk_toggle") ||
      g_str_equal (iso_next_group_option, "menu_toggle") ||
      g_str_equal (iso_next_group_option, "caps_toggle"))
    {
      n_combos = n_keycodes;
      combos = g_new (CobiwmResolvedKeyCombo, n_combos);

      for (i = 0; i < n_keycodes; ++i)
        {
          combos[i].keycode = keycodes[i];
          combos[i].mask = 0;
        }
    }
  else if (g_str_equal (iso_next_group_option, "shift_caps_toggle") ||
           g_str_equal (iso_next_group_option, "shifts_toggle"))
    {
      n_combos = n_keycodes;
      combos = g_new (CobiwmResolvedKeyCombo, n_combos);

      for (i = 0; i < n_keycodes; ++i)
        {
          combos[i].keycode = keycodes[i];
          combos[i].mask = ShiftMask;
        }
    }
  else if (g_str_equal (iso_next_group_option, "alt_caps_toggle") ||
           g_str_equal (iso_next_group_option, "alt_space_toggle"))
    {
      n_combos = n_keycodes;
      combos = g_new (CobiwmResolvedKeyCombo, n_combos);

      for (i = 0; i < n_keycodes; ++i)
        {
          combos[i].keycode = keycodes[i];
          combos[i].mask = Mod1Mask;
        }
    }
  else if (g_str_equal (iso_next_group_option, "ctrl_shift_toggle") ||
           g_str_equal (iso_next_group_option, "lctrl_lshift_toggle") ||
           g_str_equal (iso_next_group_option, "rctrl_rshift_toggle"))
    {
      n_combos = n_keycodes * 2;
      combos = g_new (CobiwmResolvedKeyCombo, n_combos);

      for (i = 0; i < n_keycodes; ++i)
        {
          combos[i].keycode = keycodes[i];
          combos[i].mask = ShiftMask;

          combos[i + n_keycodes].keycode = keycodes[i];
          combos[i + n_keycodes].mask = ControlMask;
        }
    }
  else if (g_str_equal (iso_next_group_option, "ctrl_alt_toggle"))
    {
      n_combos = n_keycodes * 2;
      combos = g_new (CobiwmResolvedKeyCombo, n_combos);

      for (i = 0; i < n_keycodes; ++i)
        {
          combos[i].keycode = keycodes[i];
          combos[i].mask = Mod1Mask;

          combos[i + n_keycodes].keycode = keycodes[i];
          combos[i + n_keycodes].mask = ControlMask;
        }
    }
  else if (g_str_equal (iso_next_group_option, "alt_shift_toggle") ||
           g_str_equal (iso_next_group_option, "lalt_lshift_toggle"))
    {
      n_combos = n_keycodes * 2;
      combos = g_new (CobiwmResolvedKeyCombo, n_combos);

      for (i = 0; i < n_keycodes; ++i)
        {
          combos[i].keycode = keycodes[i];
          combos[i].mask = Mod1Mask;

          combos[i + n_keycodes].keycode = keycodes[i];
          combos[i + n_keycodes].mask = ShiftMask;
        }
    }
  else
    {
      n_combos = 0;
      combos = NULL;
    }

  g_free (keycodes);

  keys->n_iso_next_group_combos = n_combos;
  keys->iso_next_group_combos = combos;
}

static void
devirtualize_modifiers (CobiwmKeyBindingManager *keys,
                        CobiwmVirtualModifier     modifiers,
                        unsigned int           *mask)
{
  *mask = 0;

  if (modifiers & COBIWM_VIRTUAL_SHIFT_MASK)
    *mask |= ShiftMask;
  if (modifiers & COBIWM_VIRTUAL_CONTROL_MASK)
    *mask |= ControlMask;
  if (modifiers & COBIWM_VIRTUAL_ALT_MASK)
    *mask |= Mod1Mask;
  if (modifiers & COBIWM_VIRTUAL_COBIWM_MASK)
    *mask |= keys->cobiwm_mask;
  if (modifiers & COBIWM_VIRTUAL_HYPER_MASK)
    *mask |= keys->hyper_mask;
  if (modifiers & COBIWM_VIRTUAL_SUPER_MASK)
    *mask |= keys->super_mask;
  if (modifiers & COBIWM_VIRTUAL_MOD2_MASK)
    *mask |= Mod2Mask;
  if (modifiers & COBIWM_VIRTUAL_MOD3_MASK)
    *mask |= Mod3Mask;
  if (modifiers & COBIWM_VIRTUAL_MOD4_MASK)
    *mask |= Mod4Mask;
  if (modifiers & COBIWM_VIRTUAL_MOD5_MASK)
    *mask |= Mod5Mask;
}

static void
index_binding (CobiwmKeyBindingManager *keys,
               CobiwmKeyBinding         *binding)
{
  guint32 index_key;

  index_key = key_combo_key (&binding->resolved_combo);
  g_hash_table_replace (keys->key_bindings_index,
                        GINT_TO_POINTER (index_key), binding);
}

static void
resolve_key_combo (CobiwmKeyBindingManager *keys,
                   CobiwmKeyCombo          *combo,
                   CobiwmResolvedKeyCombo  *resolved_combo)
{
  if (combo->keysym != 0)
    resolved_combo->keycode = get_first_keycode_for_keysym (keys, combo->keysym);
  else
    resolved_combo->keycode = combo->keycode;

  devirtualize_modifiers (keys, combo->modifiers, &resolved_combo->mask);
}

static void
binding_reload_combos_foreach (gpointer key,
                               gpointer value,
                               gpointer data)
{
  CobiwmKeyBindingManager *keys = data;
  CobiwmKeyBinding *binding = value;

  resolve_key_combo (keys, &binding->combo, &binding->resolved_combo);
  index_binding (keys, binding);
}

static void
reload_combos (CobiwmKeyBindingManager *keys)
{
  g_hash_table_remove_all (keys->key_bindings_index);

  determine_keymap_num_levels (keys);

  resolve_key_combo (keys,
                     &keys->overlay_key_combo,
                     &keys->overlay_resolved_key_combo);

  reload_iso_next_group_combos (keys);

  g_hash_table_foreach (keys->key_bindings, binding_reload_combos_foreach, keys);
}

static void
rebuild_binding_table (CobiwmKeyBindingManager *keys,
                       GList                  *prefs,
                       GList                  *grabs)
{
  CobiwmKeyBinding *b;
  GList *p, *g;

  g_hash_table_remove_all (keys->key_bindings);

  p = prefs;
  while (p)
    {
      CobiwmKeyPref *pref = (CobiwmKeyPref*)p->data;
      GSList *tmp = pref->combos;

      while (tmp)
        {
          CobiwmKeyCombo *combo = tmp->data;

          if (combo && (combo->keysym != None || combo->keycode != 0))
            {
              CobiwmKeyHandler *handler = HANDLER (pref->name);

              b = g_slice_new0 (CobiwmKeyBinding);
              b->name = pref->name;
              b->handler = handler;
              b->flags = handler->flags;
              b->combo = *combo;

              g_hash_table_add (keys->key_bindings, b);
            }

          tmp = tmp->next;
        }

      p = p->next;
    }

  g = grabs;
  while (g)
    {
      CobiwmKeyGrab *grab = (CobiwmKeyGrab*)g->data;
      if (grab->combo.keysym != None || grab->combo.keycode != 0)
        {
          CobiwmKeyHandler *handler = HANDLER ("external-grab");

          b = g_slice_new0 (CobiwmKeyBinding);
          b->name = grab->name;
          b->handler = handler;
          b->flags = handler->flags;
          b->combo = grab->combo;

          g_hash_table_add (keys->key_bindings, b);
        }

      g = g->next;
    }

  cobiwm_topic (COBIWM_DEBUG_KEYBINDINGS,
              " %d bindings in table\n",
              g_hash_table_size (keys->key_bindings));
}

static void
rebuild_key_binding_table (CobiwmKeyBindingManager *keys)
{
  GList *prefs, *grabs;

  cobiwm_topic (COBIWM_DEBUG_KEYBINDINGS,
              "Rebuilding key binding table from preferences\n");

  prefs = cobiwm_prefs_get_keybindings ();
  grabs = g_hash_table_get_values (external_grabs);
  rebuild_binding_table (keys, prefs, grabs);
  g_list_free (prefs);
  g_list_free (grabs);
}

static void
rebuild_special_bindings (CobiwmKeyBindingManager *keys)
{
  CobiwmKeyCombo combo;

  cobiwm_prefs_get_overlay_binding (&combo);
  keys->overlay_key_combo = combo;
}

static void
ungrab_key_bindings (CobiwmDisplay *display)
{
  GSList *windows, *l;

  cobiwm_screen_ungrab_keys (display->screen);

  windows = cobiwm_display_list_windows (display, COBIWM_LIST_DEFAULT);
  for (l = windows; l; l = l->next)
    {
      CobiwmWindow *w = l->data;
      cobiwm_window_ungrab_keys (w);
    }

  g_slist_free (windows);
}

static void
grab_key_bindings (CobiwmDisplay *display)
{
  GSList *windows, *l;

  cobiwm_screen_grab_keys (display->screen);

  windows = cobiwm_display_list_windows (display, COBIWM_LIST_DEFAULT);
  for (l = windows; l; l = l->next)
    {
      CobiwmWindow *w = l->data;
      cobiwm_window_grab_keys (w);
    }

  g_slist_free (windows);
}

static CobiwmKeyBinding *
get_keybinding (CobiwmKeyBindingManager *keys,
                CobiwmResolvedKeyCombo  *resolved_combo)
{
  guint32 key;
  key = key_combo_key (resolved_combo);
  return g_hash_table_lookup (keys->key_bindings_index, GINT_TO_POINTER (key));
}

static guint
next_dynamic_keybinding_action (void)
{
  static guint num_dynamic_bindings = 0;
  return COBIWM_KEYBINDING_ACTION_LAST + (++num_dynamic_bindings);
}

static gboolean
add_keybinding_internal (CobiwmDisplay          *display,
                         const char           *name,
                         GSettings            *settings,
                         CobiwmKeyBindingFlags   flags,
                         CobiwmKeyBindingAction  action,
                         CobiwmKeyHandlerFunc    func,
                         int                   data,
                         gpointer              user_data,
                         GDestroyNotify        free_data)
{
  CobiwmKeyHandler *handler;

  if (!cobiwm_prefs_add_keybinding (name, settings, action, flags))
    return FALSE;

  handler = g_new0 (CobiwmKeyHandler, 1);
  handler->name = g_strdup (name);
  handler->func = func;
  handler->default_func = func;
  handler->data = data;
  handler->flags = flags;
  handler->user_data = user_data;
  handler->user_data_free_func = free_data;

  g_hash_table_insert (key_handlers, g_strdup (name), handler);

  return TRUE;
}

static gboolean
add_builtin_keybinding (CobiwmDisplay          *display,
                        const char           *name,
                        GSettings            *settings,
                        CobiwmKeyBindingFlags   flags,
                        CobiwmKeyBindingAction  action,
                        CobiwmKeyHandlerFunc    handler,
                        int                   handler_arg)
{
  return add_keybinding_internal (display, name, settings,
                                  flags | COBIWM_KEY_BINDING_BUILTIN,
                                  action, handler, handler_arg, NULL, NULL);
}

/**
 * cobiwm_display_add_keybinding:
 * @display: a #CobiwmDisplay
 * @name: the binding's name
 * @settings: the #GSettings object where @name is stored
 * @flags: flags to specify binding details
 * @handler: function to run when the keybinding is invoked
 * @user_data: the data to pass to @handler
 * @free_data: function to free @user_data
 *
 * Add a keybinding at runtime. The key @name in @schema needs to be of
 * type %G_VARIANT_TYPE_STRING_ARRAY, with each string describing a
 * keybinding in the form of "&lt;Control&gt;a" or "&lt;Shift&gt;&lt;Alt&gt;F1". The parser
 * is fairly liberal and allows lower or upper case, and also abbreviations
 * such as "&lt;Ctl&gt;" and "&lt;Ctrl&gt;". If the key is set to the empty list or a
 * list with a single element of either "" or "disabled", the keybinding is
 * disabled.
 *
 * Use cobiwm_display_remove_keybinding() to remove the binding.
 *
 * Returns: the corresponding keybinding action if the keybinding was
 *          added successfully, otherwise %COBIWM_KEYBINDING_ACTION_NONE
 */
guint
cobiwm_display_add_keybinding (CobiwmDisplay         *display,
                             const char          *name,
                             GSettings           *settings,
                             CobiwmKeyBindingFlags  flags,
                             CobiwmKeyHandlerFunc   handler,
                             gpointer             user_data,
                             GDestroyNotify       free_data)
{
  guint new_action = next_dynamic_keybinding_action ();

  if (!add_keybinding_internal (display, name, settings, flags, new_action,
                                handler, 0, user_data, free_data))
    return COBIWM_KEYBINDING_ACTION_NONE;

  return new_action;
}

/**
 * cobiwm_display_remove_keybinding:
 * @display: the #CobiwmDisplay
 * @name: name of the keybinding to remove
 *
 * Remove keybinding @name; the function will fail if @name is not a known
 * keybinding or has not been added with cobiwm_display_add_keybinding().
 *
 * Returns: %TRUE if the binding has been removed sucessfully,
 *          otherwise %FALSE
 */
gboolean
cobiwm_display_remove_keybinding (CobiwmDisplay *display,
                                const char  *name)
{
  if (!cobiwm_prefs_remove_keybinding (name))
    return FALSE;

  g_hash_table_remove (key_handlers, name);

  return TRUE;
}

static guint
get_keybinding_action (CobiwmKeyBindingManager *keys,
                       CobiwmResolvedKeyCombo  *resolved_combo)
{
  CobiwmKeyBinding *binding;

  /* This is much more vague than the CobiwmDisplay::overlay-key signal,
   * which is only emitted if the overlay-key is the only key pressed;
   * as this method is primarily intended for plugins to allow processing
   * of cobiwm keybindings while holding a grab, the overlay-key-only-pressed
   * tracking is left to the plugin here.
   */
  if (resolved_combo->keycode == (unsigned int)keys->overlay_resolved_key_combo.keycode)
    return COBIWM_KEYBINDING_ACTION_OVERLAY_KEY;

  binding = get_keybinding (keys, resolved_combo);
  if (binding)
    {
      CobiwmKeyGrab *grab = g_hash_table_lookup (external_grabs, binding->name);
      if (grab)
        return grab->action;
      else
        return (guint) cobiwm_prefs_get_keybinding_action (binding->name);
    }
  else
    {
      return COBIWM_KEYBINDING_ACTION_NONE;
    }
}

static void
resolved_combo_from_event_params (CobiwmResolvedKeyCombo *resolved_combo,
                                  CobiwmKeyBindingManager *keys,
                                  unsigned int keycode,
                                  unsigned long mask)
{
  resolved_combo->keycode = keycode;
  resolved_combo->mask = mask & 0xff & ~keys->ignored_modifier_mask;
}

/**
 * cobiwm_display_get_keybinding_action:
 * @display: A #CobiwmDisplay
 * @keycode: Raw keycode
 * @mask: Event mask
 *
 * Get the keybinding action bound to @keycode. Builtin keybindings
 * have a fixed associated #CobiwmKeyBindingAction, for bindings added
 * dynamically the function will return the keybinding action
 * cobiwm_display_add_keybinding() returns on registration.
 *
 * Returns: The action that should be taken for the given key, or
 * %COBIWM_KEYBINDING_ACTION_NONE.
 */
guint
cobiwm_display_get_keybinding_action (CobiwmDisplay  *display,
                                    unsigned int  keycode,
                                    unsigned long mask)
{
  CobiwmKeyBindingManager *keys = &display->key_binding_manager;
  CobiwmResolvedKeyCombo resolved_combo;
  resolved_combo_from_event_params (&resolved_combo, keys, keycode, mask);
  return get_keybinding_action (keys, &resolved_combo);
}

static void
on_keymap_changed (CobiwmBackend *backend,
                   gpointer     user_data)
{
  CobiwmDisplay *display = user_data;
  CobiwmKeyBindingManager *keys = &display->key_binding_manager;

  ungrab_key_bindings (display);

  /* Deciphering the modmap depends on the loaded keysyms to find out
   * what modifiers is Super and so forth, so we need to reload it
   * even when only the keymap changes */
  reload_modmap (keys);

  reload_combos (keys);

  grab_key_bindings (display);
}

static void
cobiwm_change_button_grab (CobiwmKeyBindingManager *keys,
                         Window                  xwindow,
                         gboolean                grab,
                         gboolean                sync,
                         int                     button,
                         int                     modmask)
{
  if (cobiwm_is_wayland_compositor ())
    return;

  CobiwmBackendX11 *backend = COBIWM_BACKEND_X11 (cobiwm_get_backend ());
  Display *xdisplay = cobiwm_backend_x11_get_xdisplay (backend);

  unsigned int ignored_mask;
  unsigned char mask_bits[XIMaskLen (XI_LASTEVENT)] = { 0 };
  XIEventMask mask = { XIAllMasterDevices, sizeof (mask_bits), mask_bits };

  XISetMask (mask.mask, XI_ButtonPress);
  XISetMask (mask.mask, XI_ButtonRelease);
  XISetMask (mask.mask, XI_Motion);

  ignored_mask = 0;
  while (ignored_mask <= keys->ignored_modifier_mask)
    {
      XIGrabModifiers mods;

      if (ignored_mask & ~(keys->ignored_modifier_mask))
        {
          /* Not a combination of ignored modifiers
           * (it contains some non-ignored modifiers)
           */
          ++ignored_mask;
          continue;
        }

      mods = (XIGrabModifiers) { modmask | ignored_mask, 0 };

      /* GrabModeSync means freeze until XAllowEvents */

      if (grab)
        XIGrabButton (xdisplay,
                      COBIWM_VIRTUAL_CORE_POINTER_ID,
                      button, xwindow, None,
                      sync ? XIGrabModeSync : XIGrabModeAsync,
                      XIGrabModeAsync, False,
                      &mask, 1, &mods);
      else
        XIUngrabButton (xdisplay,
                        COBIWM_VIRTUAL_CORE_POINTER_ID,
                        button, xwindow, 1, &mods);

      ++ignored_mask;
    }
}

ClutterModifierType
cobiwm_display_get_window_grab_modifiers (CobiwmDisplay *display)
{
  CobiwmKeyBindingManager *keys = &display->key_binding_manager;
  return keys->window_grab_modifiers;
}

static void
cobiwm_change_buttons_grab (CobiwmKeyBindingManager *keys,
                          Window                 xwindow,
                          gboolean               grab,
                          gboolean               sync,
                          int                    modmask)
{
#define MAX_BUTTON 3

  int i;
  for (i = 1; i <= MAX_BUTTON; i++)
    cobiwm_change_button_grab (keys, xwindow, grab, sync, i, modmask);
}

void
cobiwm_display_grab_window_buttons (CobiwmDisplay *display,
                                  Window       xwindow)
{
  CobiwmKeyBindingManager *keys = &display->key_binding_manager;

  /* Grab Alt + button1 for moving window.
   * Grab Alt + button2 for resizing window.
   * Grab Alt + button3 for popping up window menu.
   * Grab Alt + Shift + button1 for snap-moving window.
   */
  cobiwm_verbose ("Grabbing window buttons for 0x%lx\n", xwindow);

  /* FIXME If we ignored errors here instead of spewing, we could
   * put one big error trap around the loop and avoid a bunch of
   * XSync()
   */

  if (keys->window_grab_modifiers != 0)
    {
      cobiwm_change_buttons_grab (keys, xwindow, TRUE, FALSE,
                                keys->window_grab_modifiers);

      /* In addition to grabbing Alt+Button1 for moving the window,
       * grab Alt+Shift+Button1 for snap-moving the window.  See bug
       * 112478.  Unfortunately, this doesn't work with
       * Shift+Alt+Button1 for some reason; so at least part of the
       * order still matters, which sucks (please FIXME).
       */
      cobiwm_change_button_grab (keys, xwindow,
                               TRUE,
                               FALSE,
                               1, keys->window_grab_modifiers | ShiftMask);
    }
}

void
cobiwm_display_ungrab_window_buttons (CobiwmDisplay *display,
                                    Window       xwindow)
{
  CobiwmKeyBindingManager *keys = &display->key_binding_manager;

  if (keys->window_grab_modifiers == 0)
    return;

  cobiwm_change_buttons_grab (keys, xwindow, FALSE, FALSE,
                            keys->window_grab_modifiers);
}

static void
update_window_grab_modifiers (CobiwmKeyBindingManager *keys)
{
  CobiwmVirtualModifier virtual_mods;
  unsigned int mods;

  virtual_mods = cobiwm_prefs_get_mouse_button_mods ();
  devirtualize_modifiers (keys, virtual_mods, &mods);

  keys->window_grab_modifiers = mods;
}

/* Grab buttons we only grab while unfocused in click-to-focus mode */
void
cobiwm_display_grab_focus_window_button (CobiwmDisplay *display,
                                       CobiwmWindow  *window)
{
  CobiwmKeyBindingManager *keys = &display->key_binding_manager;

  /* Grab button 1 for activating unfocused windows */
  cobiwm_verbose ("Grabbing unfocused window buttons for %s\n", window->desc);

#if 0
  /* FIXME:115072 */
  /* Don't grab at all unless in click to focus mode. In click to
   * focus, we may sometimes be clever about intercepting and eating
   * the focus click. But in mouse focus, we never do that since the
   * focus window may not be raised, and who wants to think about
   * mouse focus anyway.
   */
  if (cobiwm_prefs_get_focus_mode () != G_DESKTOP_FOCUS_MODE_CLICK)
    {
      cobiwm_verbose (" (well, not grabbing since not in click to focus mode)\n");
      return;
    }
#endif

  if (window->have_focus_click_grab)
    {
      cobiwm_verbose (" (well, not grabbing since we already have the grab)\n");
      return;
    }

  /* FIXME If we ignored errors here instead of spewing, we could
   * put one big error trap around the loop and avoid a bunch of
   * XSync()
   */

  cobiwm_change_buttons_grab (keys, window->xwindow, TRUE, TRUE, 0);
  window->have_focus_click_grab = TRUE;
}

void
cobiwm_display_ungrab_focus_window_button (CobiwmDisplay *display,
                                         CobiwmWindow  *window)
{
  CobiwmKeyBindingManager *keys = &display->key_binding_manager;

  cobiwm_verbose ("Ungrabbing unfocused window buttons for %s\n", window->desc);

  if (!window->have_focus_click_grab)
    return;

  cobiwm_change_buttons_grab (keys, window->xwindow, FALSE, FALSE, 0);
  window->have_focus_click_grab = FALSE;
}

static void
prefs_changed_callback (CobiwmPreference pref,
                        void          *data)
{
  CobiwmDisplay *display = data;
  CobiwmKeyBindingManager *keys = &display->key_binding_manager;

  switch (pref)
    {
    case COBIWM_PREF_KEYBINDINGS:
      ungrab_key_bindings (display);
      rebuild_key_binding_table (keys);
      rebuild_special_bindings (keys);
      reload_combos (keys);
      grab_key_bindings (display);
      break;
    case COBIWM_PREF_MOUSE_BUTTON_MODS:
      {
        GSList *windows, *l;
        windows = cobiwm_display_list_windows (display, COBIWM_LIST_DEFAULT);

        for (l = windows; l; l = l->next)
          {
            CobiwmWindow *w = l->data;
            cobiwm_display_ungrab_window_buttons (display, w->xwindow);
          }

        update_window_grab_modifiers (keys);

        for (l = windows; l; l = l->next)
          {
            CobiwmWindow *w = l->data;
            if (w->type != COBIWM_WINDOW_DOCK)
              cobiwm_display_grab_window_buttons (display, w->xwindow);
          }

        g_slist_free (windows);
      }
    default:
      break;
    }
}


void
cobiwm_display_shutdown_keys (CobiwmDisplay *display)
{
  CobiwmKeyBindingManager *keys = &display->key_binding_manager;

  cobiwm_prefs_remove_listener (prefs_changed_callback, display);

  g_hash_table_destroy (keys->key_bindings_index);
  g_hash_table_destroy (keys->key_bindings);
}

/* Grab/ungrab, ignoring all annoying modifiers like NumLock etc. */
static void
cobiwm_change_keygrab (CobiwmKeyBindingManager *keys,
                     Window                 xwindow,
                     gboolean               grab,
                     CobiwmResolvedKeyCombo  *resolved_combo)
{
  unsigned int ignored_mask;

  unsigned char mask_bits[XIMaskLen (XI_LASTEVENT)] = { 0 };
  XIEventMask mask = { XIAllMasterDevices, sizeof (mask_bits), mask_bits };

  XISetMask (mask.mask, XI_KeyPress);
  XISetMask (mask.mask, XI_KeyRelease);

  if (cobiwm_is_wayland_compositor ())
    return;

  CobiwmBackendX11 *backend = COBIWM_BACKEND_X11 (cobiwm_get_backend ());
  Display *xdisplay = cobiwm_backend_x11_get_xdisplay (backend);

  /* Grab keycode/modmask, together with
   * all combinations of ignored modifiers.
   * X provides no better way to do this.
   */

  cobiwm_topic (COBIWM_DEBUG_KEYBINDINGS,
              "%s keybinding keycode %d mask 0x%x on 0x%lx\n",
              grab ? "Grabbing" : "Ungrabbing",
              resolved_combo->keycode, resolved_combo->mask, xwindow);

  ignored_mask = 0;
  while (ignored_mask <= keys->ignored_modifier_mask)
    {
      XIGrabModifiers mods;

      if (ignored_mask & ~(keys->ignored_modifier_mask))
        {
          /* Not a combination of ignored modifiers
           * (it contains some non-ignored modifiers)
           */
          ++ignored_mask;
          continue;
        }

      mods = (XIGrabModifiers) { resolved_combo->mask | ignored_mask, 0 };

      if (grab)
        XIGrabKeycode (xdisplay,
                       COBIWM_VIRTUAL_CORE_KEYBOARD_ID,
                       resolved_combo->keycode, xwindow,
                       XIGrabModeSync, XIGrabModeAsync,
                       False, &mask, 1, &mods);
      else
        XIUngrabKeycode (xdisplay,
                         COBIWM_VIRTUAL_CORE_KEYBOARD_ID,
                         resolved_combo->keycode, xwindow, 1, &mods);

      ++ignored_mask;
    }
}

typedef struct
{
  CobiwmKeyBindingManager *keys;
  Window xwindow;
  gboolean only_per_window;
  gboolean grab;
} ChangeKeygrabData;

static void
change_keygrab_foreach (gpointer key,
                        gpointer value,
                        gpointer user_data)
{
  ChangeKeygrabData *data = user_data;
  CobiwmKeyBinding *binding = value;
  gboolean binding_is_per_window = (binding->flags & COBIWM_KEY_BINDING_PER_WINDOW) != 0;

  if (data->only_per_window != binding_is_per_window)
    return;

  if (binding->resolved_combo.keycode == 0)
    return;

  cobiwm_change_keygrab (data->keys, data->xwindow, data->grab, &binding->resolved_combo);
}

static void
change_binding_keygrabs (CobiwmKeyBindingManager *keys,
                         Window                  xwindow,
                         gboolean                only_per_window,
                         gboolean                grab)
{
  ChangeKeygrabData data;

  data.keys = keys;
  data.xwindow = xwindow;
  data.only_per_window = only_per_window;
  data.grab = grab;

  g_hash_table_foreach (keys->key_bindings, change_keygrab_foreach, &data);
}

static void
cobiwm_screen_change_keygrabs (CobiwmScreen *screen,
                             gboolean    grab)
{
  CobiwmDisplay *display = screen->display;
  CobiwmKeyBindingManager *keys = &display->key_binding_manager;

  if (keys->overlay_resolved_key_combo.keycode != 0)
    cobiwm_change_keygrab (keys, screen->xroot, grab, &keys->overlay_resolved_key_combo);

  if (keys->iso_next_group_combos)
    {
      int i = 0;
      while (i < keys->n_iso_next_group_combos)
        {
          if (keys->iso_next_group_combos[i].keycode != 0)
            cobiwm_change_keygrab (keys, screen->xroot, grab, &keys->iso_next_group_combos[i]);

          ++i;
        }
    }

  change_binding_keygrabs (keys, screen->xroot, FALSE, grab);
}

void
cobiwm_screen_grab_keys (CobiwmScreen *screen)
{
  if (screen->keys_grabbed)
    return;

  cobiwm_screen_change_keygrabs (screen, TRUE);

  screen->keys_grabbed = TRUE;
}

void
cobiwm_screen_ungrab_keys (CobiwmScreen  *screen)
{
  if (!screen->keys_grabbed)
    return;

  cobiwm_screen_change_keygrabs (screen, FALSE);

  screen->keys_grabbed = FALSE;
}

static void
change_window_keygrabs (CobiwmKeyBindingManager *keys,
                        Window                 xwindow,
                        gboolean               grab)
{
  change_binding_keygrabs (keys, xwindow, TRUE, grab);
}

void
cobiwm_window_grab_keys (CobiwmWindow  *window)
{
  CobiwmDisplay *display = window->display;
  CobiwmKeyBindingManager *keys = &display->key_binding_manager;

  if (window->all_keys_grabbed)
    return;

  if (window->type == COBIWM_WINDOW_DOCK
      || window->override_redirect)
    {
      if (window->keys_grabbed)
        change_window_keygrabs (keys, window->xwindow, FALSE);
      window->keys_grabbed = FALSE;
      return;
    }

  if (window->keys_grabbed)
    {
      if (window->frame && !window->grab_on_frame)
        change_window_keygrabs (keys, window->xwindow, FALSE);
      else if (window->frame == NULL &&
               window->grab_on_frame)
        ; /* continue to regrab on client window */
      else
        return; /* already all good */
    }

  change_window_keygrabs (keys,
                          cobiwm_window_x11_get_toplevel_xwindow (window),
                          TRUE);

  window->keys_grabbed = TRUE;
  window->grab_on_frame = window->frame != NULL;
}

void
cobiwm_window_ungrab_keys (CobiwmWindow  *window)
{
  if (window->keys_grabbed)
    {
      CobiwmDisplay *display = window->display;
      CobiwmKeyBindingManager *keys = &display->key_binding_manager;

      if (window->grab_on_frame &&
          window->frame != NULL)
        change_window_keygrabs (keys, window->frame->xwindow, FALSE);
      else if (!window->grab_on_frame)
        change_window_keygrabs (keys, window->xwindow, FALSE);

      window->keys_grabbed = FALSE;
    }
}

static void
handle_external_grab (CobiwmDisplay     *display,
                      CobiwmScreen      *screen,
                      CobiwmWindow      *window,
                      ClutterKeyEvent *event,
                      CobiwmKeyBinding  *binding,
                      gpointer         user_data)
{
  CobiwmKeyBindingManager *keys = &display->key_binding_manager;
  guint action = get_keybinding_action (keys, &binding->resolved_combo);
  cobiwm_display_accelerator_activate (display, action, event);
}


guint
cobiwm_display_grab_accelerator (CobiwmDisplay *display,
                               const char  *accelerator)
{
  CobiwmKeyBindingManager *keys = &display->key_binding_manager;
  CobiwmKeyBinding *binding;
  CobiwmKeyGrab *grab;
  CobiwmKeyCombo combo = { 0 };
  CobiwmResolvedKeyCombo resolved_combo = { 0 };

  if (!cobiwm_parse_accelerator (accelerator, &combo))
    {
      cobiwm_topic (COBIWM_DEBUG_KEYBINDINGS,
                  "Failed to parse accelerator\n");
      cobiwm_warning ("\"%s\" is not a valid accelerator\n", accelerator);

      return COBIWM_KEYBINDING_ACTION_NONE;
    }

  resolve_key_combo (keys, &combo, &resolved_combo);

  if (resolved_combo.keycode == 0)
    return COBIWM_KEYBINDING_ACTION_NONE;

  if (get_keybinding (keys, &resolved_combo))
    return COBIWM_KEYBINDING_ACTION_NONE;

  cobiwm_change_keygrab (keys, display->screen->xroot, TRUE, &resolved_combo);

  grab = g_new0 (CobiwmKeyGrab, 1);
  grab->action = next_dynamic_keybinding_action ();
  grab->name = cobiwm_external_binding_name_for_action (grab->action);
  grab->combo = combo;

  g_hash_table_insert (external_grabs, grab->name, grab);

  binding = g_slice_new0 (CobiwmKeyBinding);
  binding->name = grab->name;
  binding->handler = HANDLER ("external-grab");
  binding->combo = combo;
  binding->resolved_combo = resolved_combo;

  g_hash_table_add (keys->key_bindings, binding);
  index_binding (keys, binding);

  return grab->action;
}

gboolean
cobiwm_display_ungrab_accelerator (CobiwmDisplay *display,
                                 guint        action)
{
  CobiwmKeyBindingManager *keys = &display->key_binding_manager;
  CobiwmKeyBinding *binding;
  CobiwmKeyGrab *grab;
  char *key;
  CobiwmResolvedKeyCombo resolved_combo;

  g_return_val_if_fail (action != COBIWM_KEYBINDING_ACTION_NONE, FALSE);

  key = cobiwm_external_binding_name_for_action (action);
  grab = g_hash_table_lookup (external_grabs, key);
  if (!grab)
    return FALSE;

  resolve_key_combo (keys, &grab->combo, &resolved_combo);
  binding = get_keybinding (keys, &resolved_combo);
  if (binding)
    {
      guint32 index_key;

      cobiwm_change_keygrab (keys, display->screen->xroot, FALSE, &binding->resolved_combo);

      index_key = key_combo_key (&binding->resolved_combo);
      g_hash_table_remove (keys->key_bindings_index, GINT_TO_POINTER (index_key));

      g_hash_table_remove (keys->key_bindings, binding);
    }

  g_hash_table_remove (external_grabs, key);
  g_free (key);

  return TRUE;
}

static gboolean
grab_keyboard (Window  xwindow,
               guint32 timestamp,
               int     grab_mode)
{
  int grab_status;

  unsigned char mask_bits[XIMaskLen (XI_LASTEVENT)] = { 0 };
  XIEventMask mask = { XIAllMasterDevices, sizeof (mask_bits), mask_bits };

  XISetMask (mask.mask, XI_KeyPress);
  XISetMask (mask.mask, XI_KeyRelease);

  if (cobiwm_is_wayland_compositor ())
    return TRUE;

  /* Grab the keyboard, so we get key releases and all key
   * presses
   */

  CobiwmBackendX11 *backend = COBIWM_BACKEND_X11 (cobiwm_get_backend ());
  Display *xdisplay = cobiwm_backend_x11_get_xdisplay (backend);

  /* Strictly, we only need to set grab_mode on the keyboard device
   * while the pointer should always be XIGrabModeAsync. Unfortunately
   * there is a bug in the X server, only fixed (link below) in 1.15,
   * which swaps these arguments for keyboard devices. As such, we set
   * both the device and the paired device mode which works around
   * that bug and also works on fixed X servers.
   *
   * http://cgit.freedesktop.org/xorg/xserver/commit/?id=9003399708936481083424b4ff8f18a16b88b7b3
   */
  grab_status = XIGrabDevice (xdisplay,
                              COBIWM_VIRTUAL_CORE_KEYBOARD_ID,
                              xwindow,
                              timestamp,
                              None,
                              grab_mode, grab_mode,
                              False, /* owner_events */
                              &mask);

  return (grab_status == Success);
}

static void
ungrab_keyboard (guint32 timestamp)
{
  if (cobiwm_is_wayland_compositor ())
    return;

  CobiwmBackendX11 *backend = COBIWM_BACKEND_X11 (cobiwm_get_backend ());
  Display *xdisplay = cobiwm_backend_x11_get_xdisplay (backend);

  XIUngrabDevice (xdisplay, COBIWM_VIRTUAL_CORE_KEYBOARD_ID, timestamp);
}

gboolean
cobiwm_window_grab_all_keys (CobiwmWindow  *window,
                           guint32      timestamp)
{
  Window grabwindow;
  gboolean retval;

  if (window->all_keys_grabbed)
    return FALSE;

  if (window->keys_grabbed)
    cobiwm_window_ungrab_keys (window);

  /* Make sure the window is focused, otherwise the grab
   * won't do a lot of good.
   */
  cobiwm_topic (COBIWM_DEBUG_FOCUS,
              "Focusing %s because we're grabbing all its keys\n",
              window->desc);
  cobiwm_window_focus (window, timestamp);

  grabwindow = cobiwm_window_x11_get_toplevel_xwindow (window);

  cobiwm_topic (COBIWM_DEBUG_KEYBINDINGS,
              "Grabbing all keys on window %s\n", window->desc);
  retval = grab_keyboard (grabwindow, timestamp, XIGrabModeAsync);
  if (retval)
    {
      window->keys_grabbed = FALSE;
      window->all_keys_grabbed = TRUE;
      window->grab_on_frame = window->frame != NULL;
    }

  return retval;
}

void
cobiwm_window_ungrab_all_keys (CobiwmWindow *window, guint32 timestamp)
{
  if (window->all_keys_grabbed)
    {
      ungrab_keyboard (timestamp);

      window->grab_on_frame = FALSE;
      window->all_keys_grabbed = FALSE;
      window->keys_grabbed = FALSE;

      /* Re-establish our standard bindings */
      cobiwm_window_grab_keys (window);
    }
}

void
cobiwm_display_freeze_keyboard (CobiwmDisplay *display, guint32 timestamp)
{
  CobiwmBackend *backend = cobiwm_get_backend ();

  if (!COBIWM_IS_BACKEND_X11 (backend))
    return;

  Window window = cobiwm_backend_x11_get_xwindow (COBIWM_BACKEND_X11 (backend));
  grab_keyboard (window, timestamp, XIGrabModeSync);
}

void
cobiwm_display_ungrab_keyboard (CobiwmDisplay *display, guint32 timestamp)
{
  ungrab_keyboard (timestamp);
}

void
cobiwm_display_unfreeze_keyboard (CobiwmDisplay *display, guint32 timestamp)
{
  CobiwmBackend *backend = cobiwm_get_backend ();

  if (!COBIWM_IS_BACKEND_X11 (backend))
    return;

  Display *xdisplay = cobiwm_backend_x11_get_xdisplay (COBIWM_BACKEND_X11 (backend));

  XIAllowEvents (xdisplay, COBIWM_VIRTUAL_CORE_KEYBOARD_ID,
                 XIAsyncDevice, timestamp);
  /* We shouldn't need to unfreeze the pointer device here, however we
   * have to, due to the workaround we do in grab_keyboard().
   */
  XIAllowEvents (xdisplay, COBIWM_VIRTUAL_CORE_POINTER_ID,
                 XIAsyncDevice, timestamp);
}

static gboolean
is_modifier (xkb_keysym_t keysym)
{
  switch (keysym)
    {
    case XKB_KEY_Shift_L:
    case XKB_KEY_Shift_R:
    case XKB_KEY_Control_L:
    case XKB_KEY_Control_R:
    case XKB_KEY_Caps_Lock:
    case XKB_KEY_Shift_Lock:
    case XKB_KEY_Meta_L:
    case XKB_KEY_Meta_R:
    case XKB_KEY_Alt_L:
    case XKB_KEY_Alt_R:
    case XKB_KEY_Super_L:
    case XKB_KEY_Super_R:
    case XKB_KEY_Hyper_L:
    case XKB_KEY_Hyper_R:
      return TRUE;
    default:
      return FALSE;
    }
}

static void
invoke_handler (CobiwmDisplay     *display,
                CobiwmScreen      *screen,
                CobiwmKeyHandler  *handler,
                CobiwmWindow      *window,
                ClutterKeyEvent *event,
                CobiwmKeyBinding  *binding)
{
  if (handler->func)
    (* handler->func) (display, screen,
                       handler->flags & COBIWM_KEY_BINDING_PER_WINDOW ?
                       window : NULL,
                       event,
                       binding,
                       handler->user_data);
  else
    (* handler->default_func) (display, screen,
                               handler->flags & COBIWM_KEY_BINDING_PER_WINDOW ?
                               window: NULL,
                               event,
                               binding,
                               NULL);
}

static gboolean
process_event (CobiwmDisplay          *display,
               CobiwmScreen           *screen,
               CobiwmWindow           *window,
               ClutterKeyEvent      *event)
{
  CobiwmKeyBindingManager *keys = &display->key_binding_manager;
  CobiwmResolvedKeyCombo resolved_combo;
  CobiwmKeyBinding *binding;

  /* we used to have release-based bindings but no longer. */
  if (event->type == CLUTTER_KEY_RELEASE)
    return FALSE;

  resolved_combo_from_event_params (&resolved_combo, keys,
                                    event->hardware_keycode,
                                    event->modifier_state);

  binding = get_keybinding (keys, &resolved_combo);

  if (!binding ||
      binding->handler == NULL ||
      (!window && binding->flags & COBIWM_KEY_BINDING_PER_WINDOW)) {
    cobiwm_topic (COBIWM_DEBUG_KEYBINDINGS, "No handler found for this event in this binding table\n");
    return FALSE;
  }

  cobiwm_topic (COBIWM_DEBUG_KEYBINDINGS,
                "Running handler for %s\n",
                binding->name);

  /* Global keybindings count as a let-the-terminal-lose-focus
   * due to new window mapping until the user starts
   * interacting with the terminal again.
   */
  display->allow_terminal_deactivation = TRUE;

  invoke_handler (display, screen, binding->handler, window, event, binding);

  return TRUE;
}

static gboolean
process_overlay_key (CobiwmDisplay *display,
                     CobiwmScreen *screen,
                     ClutterKeyEvent *event,
                     CobiwmWindow *window)
{
  CobiwmKeyBindingManager *keys = &display->key_binding_manager;
  CobiwmBackend *backend = cobiwm_get_backend ();
  Display *xdisplay;

  if (COBIWM_IS_BACKEND_X11 (backend))
    xdisplay = cobiwm_backend_x11_get_xdisplay (COBIWM_BACKEND_X11 (backend));
  else
    xdisplay = NULL;

  if (keys->overlay_key_only_pressed)
    {
      if (event->hardware_keycode != (int)keys->overlay_resolved_key_combo.keycode)
        {
          keys->overlay_key_only_pressed = FALSE;

          /* OK, the user hit modifier+key rather than pressing and
           * releasing the ovelay key. We want to handle the key
           * sequence "normally". Unfortunately, using
           * XAllowEvents(..., ReplayKeyboard, ...) doesn't quite
           * work, since global keybindings won't be activated ("this
           * time, however, the function ignores any passive grabs at
           * above (toward the root of) the grab_window of the grab
           * just released.") So, we first explicitly check for one of
           * our global keybindings, and if not found, we then replay
           * the event. Other clients with global grabs will be out of
           * luck.
           */
          if (process_event (display, screen, window, event))
            {
              /* As normally, after we've handled a global key
               * binding, we unfreeze the keyboard but keep the grab
               * (this is important for something like cycling
               * windows */

              if (xdisplay)
                XIAllowEvents (xdisplay,
                               clutter_input_device_get_device_id (event->device),
                               XIAsyncDevice, event->time);
            }
          else
            {
              /* Replay the event so it gets delivered to our
               * per-window key bindings or to the application */
              if (xdisplay)
                XIAllowEvents (xdisplay,
                               clutter_input_device_get_device_id (event->device),
                               XIReplayDevice, event->time);

              return FALSE;
            }
        }
      else if (event->type == CLUTTER_KEY_RELEASE)
        {
          keys->overlay_key_only_pressed = FALSE;

          /* We want to unfreeze events, but keep the grab so that if the user
           * starts typing into the overlay we get all the keys */
          if (xdisplay)
            XIAllowEvents (xdisplay,
                           clutter_input_device_get_device_id (event->device),
                           XIAsyncDevice, event->time);
          cobiwm_display_overlay_key_activate (display);
        }
      else
        {
          /* In some rare race condition, cobiwm might not receive the Super_L
           * KeyRelease event because:
           * - the compositor might end the modal mode and call XIUngrabDevice
           *   while the key is still down
           * - passive grabs are only activated on KeyPress and not KeyRelease.
           *
           * In this case, keys->overlay_key_only_pressed might be wrong.
           * Cobiwm still ought to acknowledge events, otherwise the X server
           * will not send the next events.
           *
           * https://bugzilla.gnome.org/show_bug.cgi?id=666101
           */
          if (xdisplay)
            XIAllowEvents (xdisplay,
                           clutter_input_device_get_device_id (event->device),
                           XIAsyncDevice, event->time);
        }

      return TRUE;
    }
  else if (event->type == CLUTTER_KEY_PRESS &&
           event->hardware_keycode == (int)keys->overlay_resolved_key_combo.keycode)
    {
      keys->overlay_key_only_pressed = TRUE;
      /* We keep the keyboard frozen - this allows us to use ReplayKeyboard
       * on the next event if it's not the release of the overlay key */
      if (xdisplay)
        XIAllowEvents (xdisplay,
                       clutter_input_device_get_device_id (event->device),
                       XISyncDevice, event->time);

      return TRUE;
    }
  else
    return FALSE;
}

static gboolean
process_iso_next_group (CobiwmDisplay *display,
                        CobiwmScreen *screen,
                        ClutterKeyEvent *event)
{
  CobiwmKeyBindingManager *keys = &display->key_binding_manager;
  gboolean activate;
  CobiwmResolvedKeyCombo resolved_combo;
  int i;

  if (event->type == CLUTTER_KEY_RELEASE)
    return FALSE;

  activate = FALSE;

  resolved_combo_from_event_params (&resolved_combo, keys,
                                    event->hardware_keycode,
                                    event->modifier_state);

  for (i = 0; i < keys->n_iso_next_group_combos; ++i)
    {
      if (resolved_combo.keycode == keys->iso_next_group_combos[i].keycode &&
          resolved_combo.mask == keys->iso_next_group_combos[i].mask)
        {
          /* If the signal handler returns TRUE the keyboard will
             remain frozen. It's the signal handler's responsibility
             to unfreeze it. */
          if (!cobiwm_display_modifiers_accelerator_activate (display))
            cobiwm_display_unfreeze_keyboard (display, event->time);
          activate = TRUE;
          break;
        }
    }

  return activate;
}

static gboolean
process_key_event (CobiwmDisplay     *display,
                   CobiwmWindow      *window,
                   ClutterKeyEvent *event)
{
  gboolean keep_grab;
  gboolean all_keys_grabbed;
  gboolean handled;
  CobiwmScreen *screen;

  /* window may be NULL */

  screen = display->screen;

  all_keys_grabbed = window ? window->all_keys_grabbed : FALSE;
  if (!all_keys_grabbed)
    {
      handled = process_overlay_key (display, screen, event, window);
      if (handled)
        return TRUE;

      handled = process_iso_next_group (display, screen, event);
      if (handled)
        return TRUE;
    }

  {
    CobiwmBackend *backend = cobiwm_get_backend ();
    if (COBIWM_IS_BACKEND_X11 (backend))
      {
        Display *xdisplay = cobiwm_backend_x11_get_xdisplay (COBIWM_BACKEND_X11 (backend));
        XIAllowEvents (xdisplay,
                       clutter_input_device_get_device_id (event->device),
                       XIAsyncDevice, event->time);
      }
  }

  keep_grab = TRUE;
  if (all_keys_grabbed)
    {
      if (display->grab_op == COBIWM_GRAB_OP_NONE)
        return TRUE;

      /* If we get here we have a global grab, because
       * we're in some special keyboard mode such as window move
       * mode.
       */
      if (window == display->grab_window)
        {
          if (display->grab_op & COBIWM_GRAB_OP_WINDOW_FLAG_KEYBOARD)
            {
              if (display->grab_op == COBIWM_GRAB_OP_KEYBOARD_MOVING)
                {
                  cobiwm_topic (COBIWM_DEBUG_KEYBINDINGS,
                              "Processing event for keyboard move\n");
                  keep_grab = process_keyboard_move_grab (display, screen, window, event);
                }
              else
                {
                  cobiwm_topic (COBIWM_DEBUG_KEYBINDINGS,
                              "Processing event for keyboard resize\n");
                  keep_grab = process_keyboard_resize_grab (display, screen, window, event);
                }
            }
          else
            {
              cobiwm_topic (COBIWM_DEBUG_KEYBINDINGS,
                          "Processing event for mouse-only move/resize\n");
              keep_grab = process_mouse_move_resize_grab (display, screen, window, event);
            }
        }
      if (!keep_grab)
        cobiwm_display_end_grab_op (display, event->time);

      return TRUE;
    }

  /* Do the normal keybindings */
  return process_event (display, screen, window, event);
}

/* Handle a key event. May be called recursively: some key events cause
 * grabs to be ended and then need to be processed again in their own
 * right. This cannot cause infinite recursion because we never call
 * ourselves when there wasn't a grab, and we always clear the grab
 * first; the invariant is enforced using an assertion. See #112560.
 *
 * The return value is whether we handled the key event.
 *
 * FIXME: We need to prove there are no race conditions here.
 * FIXME: Does it correctly handle alt-Tab being followed by another
 * grabbing keypress without letting go of alt?
 * FIXME: An iterative solution would probably be simpler to understand
 * (and help us solve the other fixmes).
 */
gboolean
cobiwm_keybindings_process_event (CobiwmDisplay        *display,
                                CobiwmWindow         *window,
                                const ClutterEvent *event)
{
  CobiwmKeyBindingManager *keys = &display->key_binding_manager;

  switch (event->type)
    {
    case CLUTTER_BUTTON_PRESS:
    case CLUTTER_BUTTON_RELEASE:
      keys->overlay_key_only_pressed = FALSE;
      return FALSE;

    case CLUTTER_KEY_PRESS:
    case CLUTTER_KEY_RELEASE:
      return process_key_event (display, window, (ClutterKeyEvent *) event);

    default:
      return FALSE;
    }
}

static gboolean
process_mouse_move_resize_grab (CobiwmDisplay     *display,
                                CobiwmScreen      *screen,
                                CobiwmWindow      *window,
                                ClutterKeyEvent *event)
{
  /* don't care about releases, but eat them, don't end grab */
  if (event->type == CLUTTER_KEY_RELEASE)
    return TRUE;

  if (event->keyval == CLUTTER_KEY_Escape)
    {
      /* Hide the tiling preview if necessary */
      if (window->tile_mode != COBIWM_TILE_NONE)
        cobiwm_screen_hide_tile_preview (screen);

      /* Restore the original tile mode */
      window->tile_mode = display->grab_tile_mode;
      window->tile_monitor_number = display->grab_tile_monitor_number;

      /* End move or resize and restore to original state.  If the
       * window was a maximized window that had been "shaken loose" we
       * need to remaximize it.  In normal cases, we need to do a
       * moveresize now to get the position back to the original.
       */
      if (window->shaken_loose || window->tile_mode == COBIWM_TILE_MAXIMIZED)
        cobiwm_window_maximize (window, COBIWM_MAXIMIZE_BOTH);
      else if (window->tile_mode != COBIWM_TILE_NONE)
        cobiwm_window_tile (window);
      else
        cobiwm_window_move_resize_frame (display->grab_window,
                                       TRUE,
                                       display->grab_initial_window_pos.x,
                                       display->grab_initial_window_pos.y,
                                       display->grab_initial_window_pos.width,
                                       display->grab_initial_window_pos.height);

      /* End grab */
      return FALSE;
    }

  return TRUE;
}

static gboolean
process_keyboard_move_grab (CobiwmDisplay     *display,
                            CobiwmScreen      *screen,
                            CobiwmWindow      *window,
                            ClutterKeyEvent *event)
{
  gboolean handled;
  CobiwmRectangle frame_rect;
  int x, y;
  int incr;
  gboolean smart_snap;

  handled = FALSE;

  /* don't care about releases, but eat them, don't end grab */
  if (event->type == CLUTTER_KEY_RELEASE)
    return TRUE;

  /* don't end grab on modifier key presses */
  if (is_modifier (event->keyval))
    return TRUE;

  cobiwm_window_get_frame_rect (window, &frame_rect);
  x = frame_rect.x;
  y = frame_rect.y;

  smart_snap = (event->modifier_state & CLUTTER_SHIFT_MASK) != 0;

#define SMALL_INCREMENT 1
#define NORMAL_INCREMENT 10

  if (smart_snap)
    incr = 1;
  else if (event->modifier_state & CLUTTER_CONTROL_MASK)
    incr = SMALL_INCREMENT;
  else
    incr = NORMAL_INCREMENT;

  if (event->keyval == CLUTTER_KEY_Escape)
    {
      /* End move and restore to original state.  If the window was a
       * maximized window that had been "shaken loose" we need to
       * remaximize it.  In normal cases, we need to do a moveresize
       * now to get the position back to the original.
       */
      if (window->shaken_loose)
        cobiwm_window_maximize (window, COBIWM_MAXIMIZE_BOTH);
      else
        cobiwm_window_move_resize_frame (display->grab_window,
                                       TRUE,
                                       display->grab_initial_window_pos.x,
                                       display->grab_initial_window_pos.y,
                                       display->grab_initial_window_pos.width,
                                       display->grab_initial_window_pos.height);
    }

  /* When moving by increments, we still snap to edges if the move
   * to the edge is smaller than the increment. This is because
   * Shift + arrow to snap is sort of a hidden feature. This way
   * people using just arrows shouldn't get too frustrated.
   */
  switch (event->keyval)
    {
    case CLUTTER_KEY_KP_Home:
    case CLUTTER_KEY_KP_Prior:
    case CLUTTER_KEY_Up:
    case CLUTTER_KEY_KP_Up:
      y -= incr;
      handled = TRUE;
      break;
    case CLUTTER_KEY_KP_End:
    case CLUTTER_KEY_KP_Next:
    case CLUTTER_KEY_Down:
    case CLUTTER_KEY_KP_Down:
      y += incr;
      handled = TRUE;
      break;
    }

  switch (event->keyval)
    {
    case CLUTTER_KEY_KP_Home:
    case CLUTTER_KEY_KP_End:
    case CLUTTER_KEY_Left:
    case CLUTTER_KEY_KP_Left:
      x -= incr;
      handled = TRUE;
      break;
    case CLUTTER_KEY_KP_Prior:
    case CLUTTER_KEY_KP_Next:
    case CLUTTER_KEY_Right:
    case CLUTTER_KEY_KP_Right:
      x += incr;
      handled = TRUE;
      break;
    }

  if (handled)
    {
      cobiwm_topic (COBIWM_DEBUG_KEYBINDINGS,
                  "Computed new window location %d,%d due to keypress\n",
                  x, y);

      cobiwm_window_edge_resistance_for_move (window,
                                            &x,
                                            &y,
                                            NULL,
                                            smart_snap,
                                            TRUE);

      cobiwm_window_move_frame (window, TRUE, x, y);
      cobiwm_window_update_keyboard_move (window);
    }

  return handled;
}

static gboolean
process_keyboard_resize_grab_op_change (CobiwmDisplay     *display,
                                        CobiwmScreen      *screen,
                                        CobiwmWindow      *window,
                                        ClutterKeyEvent *event)
{
  gboolean handled;

  handled = FALSE;
  switch (display->grab_op)
    {
    case COBIWM_GRAB_OP_KEYBOARD_RESIZING_UNKNOWN:
      switch (event->keyval)
        {
        case CLUTTER_KEY_Up:
        case CLUTTER_KEY_KP_Up:
          display->grab_op = COBIWM_GRAB_OP_KEYBOARD_RESIZING_N;
          handled = TRUE;
          break;
        case CLUTTER_KEY_Down:
        case CLUTTER_KEY_KP_Down:
          display->grab_op = COBIWM_GRAB_OP_KEYBOARD_RESIZING_S;
          handled = TRUE;
          break;
        case CLUTTER_KEY_Left:
        case CLUTTER_KEY_KP_Left:
          display->grab_op = COBIWM_GRAB_OP_KEYBOARD_RESIZING_W;
          handled = TRUE;
          break;
        case CLUTTER_KEY_Right:
        case CLUTTER_KEY_KP_Right:
          display->grab_op = COBIWM_GRAB_OP_KEYBOARD_RESIZING_E;
          handled = TRUE;
          break;
        }
      break;

    case COBIWM_GRAB_OP_KEYBOARD_RESIZING_S:
      switch (event->keyval)
        {
        case CLUTTER_KEY_Left:
        case CLUTTER_KEY_KP_Left:
          display->grab_op = COBIWM_GRAB_OP_KEYBOARD_RESIZING_W;
          handled = TRUE;
          break;
        case CLUTTER_KEY_Right:
        case CLUTTER_KEY_KP_Right:
          display->grab_op = COBIWM_GRAB_OP_KEYBOARD_RESIZING_E;
          handled = TRUE;
          break;
        }
      break;

    case COBIWM_GRAB_OP_KEYBOARD_RESIZING_N:
      switch (event->keyval)
        {
        case CLUTTER_KEY_Left:
        case CLUTTER_KEY_KP_Left:
          display->grab_op = COBIWM_GRAB_OP_KEYBOARD_RESIZING_W;
          handled = TRUE;
          break;
        case CLUTTER_KEY_Right:
        case CLUTTER_KEY_KP_Right:
          display->grab_op = COBIWM_GRAB_OP_KEYBOARD_RESIZING_E;
          handled = TRUE;
          break;
        }
      break;

    case COBIWM_GRAB_OP_KEYBOARD_RESIZING_W:
      switch (event->keyval)
        {
        case CLUTTER_KEY_Up:
        case CLUTTER_KEY_KP_Up:
          display->grab_op = COBIWM_GRAB_OP_KEYBOARD_RESIZING_N;
          handled = TRUE;
          break;
        case CLUTTER_KEY_Down:
        case CLUTTER_KEY_KP_Down:
          display->grab_op = COBIWM_GRAB_OP_KEYBOARD_RESIZING_S;
          handled = TRUE;
          break;
        }
      break;

    case COBIWM_GRAB_OP_KEYBOARD_RESIZING_E:
      switch (event->keyval)
        {
        case CLUTTER_KEY_Up:
        case CLUTTER_KEY_KP_Up:
          display->grab_op = COBIWM_GRAB_OP_KEYBOARD_RESIZING_N;
          handled = TRUE;
          break;
        case CLUTTER_KEY_Down:
        case CLUTTER_KEY_KP_Down:
          display->grab_op = COBIWM_GRAB_OP_KEYBOARD_RESIZING_S;
          handled = TRUE;
          break;
        }
      break;

    case COBIWM_GRAB_OP_KEYBOARD_RESIZING_SE:
    case COBIWM_GRAB_OP_KEYBOARD_RESIZING_NE:
    case COBIWM_GRAB_OP_KEYBOARD_RESIZING_SW:
    case COBIWM_GRAB_OP_KEYBOARD_RESIZING_NW:
      break;

    default:
      g_assert_not_reached ();
      break;
    }

  if (handled)
    {
      cobiwm_window_update_keyboard_resize (window, TRUE);
      return TRUE;
    }

  return FALSE;
}

static gboolean
process_keyboard_resize_grab (CobiwmDisplay     *display,
                              CobiwmScreen      *screen,
                              CobiwmWindow      *window,
                              ClutterKeyEvent *event)
{
  CobiwmRectangle frame_rect;
  gboolean handled;
  int height_inc;
  int width_inc;
  int width, height;
  gboolean smart_snap;
  int gravity;

  handled = FALSE;

  /* don't care about releases, but eat them, don't end grab */
  if (event->type == CLUTTER_KEY_RELEASE)
    return TRUE;

  /* don't end grab on modifier key presses */
  if (is_modifier (event->keyval))
    return TRUE;

  if (event->keyval == CLUTTER_KEY_Escape)
    {
      /* End resize and restore to original state. */
      cobiwm_window_move_resize_frame (display->grab_window,
                                     TRUE,
                                     display->grab_initial_window_pos.x,
                                     display->grab_initial_window_pos.y,
                                     display->grab_initial_window_pos.width,
                                     display->grab_initial_window_pos.height);

      return FALSE;
    }

  if (process_keyboard_resize_grab_op_change (display, screen, window, event))
    return TRUE;

  width = window->rect.width;
  height = window->rect.height;

  cobiwm_window_get_frame_rect (window, &frame_rect);
  width = frame_rect.width;
  height = frame_rect.height;

  gravity = cobiwm_resize_gravity_from_grab_op (display->grab_op);

  smart_snap = (event->modifier_state & CLUTTER_SHIFT_MASK) != 0;

#define SMALL_INCREMENT 1
#define NORMAL_INCREMENT 10

  if (smart_snap)
    {
      height_inc = 1;
      width_inc = 1;
    }
  else if (event->modifier_state & CLUTTER_CONTROL_MASK)
    {
      width_inc = SMALL_INCREMENT;
      height_inc = SMALL_INCREMENT;
    }
  else
    {
      width_inc = NORMAL_INCREMENT;
      height_inc = NORMAL_INCREMENT;
    }

  /* If this is a resize increment window, make the amount we resize
   * the window by match that amount (well, unless snap resizing...)
   */
  if (window->size_hints.width_inc > 1)
    width_inc = window->size_hints.width_inc;
  if (window->size_hints.height_inc > 1)
    height_inc = window->size_hints.height_inc;

  switch (event->keyval)
    {
    case CLUTTER_KEY_Up:
    case CLUTTER_KEY_KP_Up:
      switch (gravity)
        {
        case NorthGravity:
        case NorthWestGravity:
        case NorthEastGravity:
          /* Move bottom edge up */
          height -= height_inc;
          break;

        case SouthGravity:
        case SouthWestGravity:
        case SouthEastGravity:
          /* Move top edge up */
          height += height_inc;
          break;

        case EastGravity:
        case WestGravity:
        case CenterGravity:
          g_assert_not_reached ();
          break;
        }

      handled = TRUE;
      break;

    case CLUTTER_KEY_Down:
    case CLUTTER_KEY_KP_Down:
      switch (gravity)
        {
        case NorthGravity:
        case NorthWestGravity:
        case NorthEastGravity:
          /* Move bottom edge down */
          height += height_inc;
          break;

        case SouthGravity:
        case SouthWestGravity:
        case SouthEastGravity:
          /* Move top edge down */
          height -= height_inc;
          break;

        case EastGravity:
        case WestGravity:
        case CenterGravity:
          g_assert_not_reached ();
          break;
        }

      handled = TRUE;
      break;

    case CLUTTER_KEY_Left:
    case CLUTTER_KEY_KP_Left:
      switch (gravity)
        {
        case EastGravity:
        case SouthEastGravity:
        case NorthEastGravity:
          /* Move left edge left */
          width += width_inc;
          break;

        case WestGravity:
        case SouthWestGravity:
        case NorthWestGravity:
          /* Move right edge left */
          width -= width_inc;
          break;

        case NorthGravity:
        case SouthGravity:
        case CenterGravity:
          g_assert_not_reached ();
          break;
        }

      handled = TRUE;
      break;

    case CLUTTER_KEY_Right:
    case CLUTTER_KEY_KP_Right:
      switch (gravity)
        {
        case EastGravity:
        case SouthEastGravity:
        case NorthEastGravity:
          /* Move left edge right */
          width -= width_inc;
          break;

        case WestGravity:
        case SouthWestGravity:
        case NorthWestGravity:
          /* Move right edge right */
          width += width_inc;
          break;

        case NorthGravity:
        case SouthGravity:
        case CenterGravity:
          g_assert_not_reached ();
          break;
        }

      handled = TRUE;
      break;

    default:
      break;
    }

  /* fixup hack (just paranoia, not sure it's required) */
  if (height < 1)
    height = 1;
  if (width < 1)
    width = 1;

  if (handled)
    {
      cobiwm_topic (COBIWM_DEBUG_KEYBINDINGS,
                  "Computed new window size due to keypress: "
                  "%dx%d, gravity %s\n",
                  width, height, cobiwm_gravity_to_string (gravity));

      /* Do any edge resistance/snapping */
      cobiwm_window_edge_resistance_for_resize (window,
                                              &width,
                                              &height,
                                              gravity,
                                              NULL,
                                              smart_snap,
                                              TRUE);

      cobiwm_window_resize_frame_with_gravity (window,
                                             TRUE,
                                             width,
                                             height,
                                             gravity);

      cobiwm_window_update_keyboard_resize (window, FALSE);
    }

  return handled;
}

static void
handle_switch_to_workspace (CobiwmDisplay     *display,
                            CobiwmScreen      *screen,
                            CobiwmWindow      *event_window,
                            ClutterKeyEvent *event,
                            CobiwmKeyBinding  *binding,
                            gpointer         dummy)
{
  gint which = binding->handler->data;
  CobiwmWorkspace *workspace;

  if (which < 0)
    {
      /* Negative workspace numbers are directions with respect to the
       * current workspace.
       */

      workspace = cobiwm_workspace_get_neighbor (screen->active_workspace,
                                               which);
    }
  else
    {
      workspace = cobiwm_screen_get_workspace_by_index (screen, which);
    }

  if (workspace)
    {
      cobiwm_workspace_activate (workspace, event->time);
    }
  else
    {
      /* We could offer to create it I suppose */
    }
}


static void
handle_maximize_vertically (CobiwmDisplay     *display,
                            CobiwmScreen      *screen,
                            CobiwmWindow      *window,
                            ClutterKeyEvent *event,
                            CobiwmKeyBinding  *binding,
                            gpointer         dummy)
{
  if (window->has_resize_func)
    {
      if (window->maximized_vertically)
        cobiwm_window_unmaximize (window, COBIWM_MAXIMIZE_VERTICAL);
      else
        cobiwm_window_maximize (window, COBIWM_MAXIMIZE_VERTICAL);
    }
}

static void
handle_maximize_horizontally (CobiwmDisplay     *display,
                              CobiwmScreen      *screen,
                              CobiwmWindow      *window,
                              ClutterKeyEvent *event,
                              CobiwmKeyBinding  *binding,
                              gpointer         dummy)
{
  if (window->has_resize_func)
    {
      if (window->maximized_horizontally)
        cobiwm_window_unmaximize (window, COBIWM_MAXIMIZE_HORIZONTAL);
      else
        cobiwm_window_maximize (window, COBIWM_MAXIMIZE_HORIZONTAL);
    }
}

/*
static void
handle_always_on_top (CobiwmDisplay     *display,
                      CobiwmScreen      *screen,
                      CobiwmWindow      *window,
                      ClutterKeyEvent *event,
                      CobiwmKeyBinding  *binding,
                      gpointer         dummy)
{
  if (window->wm_state_above == FALSE)
    cobiwm_window_make_above (window);
  else
    cobiwm_window_unmake_above (window);
}
*/
static void
handle_move_to_corner_backend (CobiwmDisplay           *display,
                               CobiwmScreen            *screen,
                               CobiwmWindow            *window,
                               int                    gravity)
{
  CobiwmRectangle work_area;
  CobiwmRectangle frame_rect;
  int new_x, new_y;

  cobiwm_window_get_work_area_all_monitors (window, &work_area);
  cobiwm_window_get_frame_rect (window, &frame_rect);

  switch (gravity)
    {
    case NorthWestGravity:
    case WestGravity:
    case SouthWestGravity:
      new_x = work_area.x;
      break;
    case NorthGravity:
    case SouthGravity:
      new_x = frame_rect.x;
      break;
    case NorthEastGravity:
    case EastGravity:
    case SouthEastGravity:
      new_x = work_area.x + work_area.width - frame_rect.width;
      break;
    default:
      g_assert_not_reached ();
    }

  switch (gravity)
    {
    case NorthWestGravity:
    case NorthGravity:
    case NorthEastGravity:
      new_y = work_area.y;
      break;
    case WestGravity:
    case EastGravity:
      new_y = frame_rect.y;
      break;
    case SouthWestGravity:
    case SouthGravity:
    case SouthEastGravity:
      new_y = work_area.y + work_area.height - frame_rect.height;
      break;
    default:
      g_assert_not_reached ();
    }

  cobiwm_window_move_frame (window,
                          TRUE,
                          new_x,
                          new_y);
}

static void
handle_move_to_corner_nw  (CobiwmDisplay     *display,
                           CobiwmScreen      *screen,
                           CobiwmWindow      *window,
                           ClutterKeyEvent *event,
                           CobiwmKeyBinding  *binding,
                           gpointer         dummy)
{
  handle_move_to_corner_backend (display, screen, window, NorthWestGravity);
}

static void
handle_move_to_corner_ne  (CobiwmDisplay     *display,
                           CobiwmScreen      *screen,
                           CobiwmWindow      *window,
                           ClutterKeyEvent *event,
                           CobiwmKeyBinding  *binding,
                           gpointer         dummy)
{
  handle_move_to_corner_backend (display, screen, window, NorthEastGravity);
}

static void
handle_move_to_corner_sw  (CobiwmDisplay     *display,
                           CobiwmScreen      *screen,
                           CobiwmWindow      *window,
                           ClutterKeyEvent *event,
                           CobiwmKeyBinding  *binding,
                           gpointer         dummy)
{
  handle_move_to_corner_backend (display, screen, window, SouthWestGravity);
}

static void
handle_move_to_corner_se  (CobiwmDisplay     *display,
                           CobiwmScreen      *screen,
                           CobiwmWindow      *window,
                           ClutterKeyEvent *event,
                           CobiwmKeyBinding  *binding,
                           gpointer         dummy)
{
  handle_move_to_corner_backend (display, screen, window, SouthEastGravity);
}

static void
handle_move_to_side_n     (CobiwmDisplay     *display,
                           CobiwmScreen      *screen,
                           CobiwmWindow      *window,
                           ClutterKeyEvent *event,
                           CobiwmKeyBinding  *binding,
                           gpointer         dummy)
{
  handle_move_to_corner_backend (display, screen, window, NorthGravity);
}

static void
handle_move_to_side_s     (CobiwmDisplay     *display,
                           CobiwmScreen      *screen,
                           CobiwmWindow      *window,
                           ClutterKeyEvent *event,
                           CobiwmKeyBinding  *binding,
                           gpointer         dummy)
{
  handle_move_to_corner_backend (display, screen, window, SouthGravity);
}

static void
handle_move_to_side_e     (CobiwmDisplay     *display,
                           CobiwmScreen      *screen,
                           CobiwmWindow      *window,
                           ClutterKeyEvent *event,
                           CobiwmKeyBinding  *binding,
                           gpointer         dummy)
{
  handle_move_to_corner_backend (display, screen, window, EastGravity);
}

static void
handle_move_to_side_w     (CobiwmDisplay     *display,
                           CobiwmScreen      *screen,
                           CobiwmWindow      *window,
                           ClutterKeyEvent *event,
                           CobiwmKeyBinding  *binding,
                           gpointer         dummy)
{
  handle_move_to_corner_backend (display, screen, window, WestGravity);
}

static void
handle_move_to_center  (CobiwmDisplay     *display,
                        CobiwmScreen      *screen,
                        CobiwmWindow      *window,
                        ClutterKeyEvent *event,
                        CobiwmKeyBinding  *binding,
                        gpointer         dummy)
{
  CobiwmRectangle work_area;
  CobiwmRectangle frame_rect;

  cobiwm_window_get_work_area_all_monitors (window, &work_area);
  cobiwm_window_get_frame_rect (window, &frame_rect);

  cobiwm_window_move_frame (window,
                          TRUE,
                          work_area.x + (work_area.width  - frame_rect.width ) / 2,
                          work_area.y + (work_area.height - frame_rect.height) / 2);
}

static void
handle_show_desktop (CobiwmDisplay     *display,
                     CobiwmScreen      *screen,
                     CobiwmWindow      *window,
                     ClutterKeyEvent *event,
                     CobiwmKeyBinding  *binding,
                     gpointer         dummy)
{
  if (screen->active_workspace->showing_desktop)
    {
      cobiwm_screen_unshow_desktop (screen);
      cobiwm_workspace_focus_default_window (screen->active_workspace,
                                           NULL,
                                           event->time);
    }
  else
    cobiwm_screen_show_desktop (screen, event->time);
}

static void
handle_panel (CobiwmDisplay     *display,
              CobiwmScreen      *screen,
              CobiwmWindow      *window,
              ClutterKeyEvent *event,
              CobiwmKeyBinding  *binding,
              gpointer         dummy)
{
  CobiwmKeyBindingAction action = binding->handler->data;
  Atom action_atom;
  XClientMessageEvent ev;

  action_atom = None;
  switch (action)
    {
      /* FIXME: The numbers are wrong */
    case COBIWM_KEYBINDING_ACTION_PANEL_MAIN_MENU:
      action_atom = display->atom__GNOME_PANEL_ACTION_MAIN_MENU;
      break;
    case COBIWM_KEYBINDING_ACTION_PANEL_RUN_DIALOG:
      action_atom = display->atom__GNOME_PANEL_ACTION_RUN_DIALOG;
      break;
    default:
      return;
    }

  ev.type = ClientMessage;
  ev.window = screen->xroot;
  ev.message_type = display->atom__GNOME_PANEL_ACTION;
  ev.format = 32;
  ev.data.l[0] = action_atom;
  ev.data.l[1] = event->time;

  cobiwm_topic (COBIWM_DEBUG_KEYBINDINGS,
              "Sending panel message with timestamp %u, and turning mouse_mode "
              "off due to keybinding press\n", event->time);
  display->mouse_mode = FALSE;

  cobiwm_error_trap_push (display);

  /* Release the grab for the panel before sending the event */
  XUngrabKeyboard (display->xdisplay, event->time);

  XSendEvent (display->xdisplay,
	      screen->xroot,
	      False,
	      StructureNotifyMask,
	      (XEvent*) &ev);

  cobiwm_error_trap_pop (display);
}

static void
handle_activate_window_menu (CobiwmDisplay     *display,
                             CobiwmScreen      *screen,
                             CobiwmWindow      *event_window,
                             ClutterKeyEvent *event,
                             CobiwmKeyBinding  *binding,
                             gpointer         dummy)
{
  if (display->focus_window)
    {
      int x, y;
      CobiwmRectangle frame_rect;
      cairo_rectangle_int_t child_rect;

      cobiwm_window_get_frame_rect (display->focus_window, &frame_rect);
      cobiwm_window_get_client_area_rect (display->focus_window, &child_rect);

      x = frame_rect.x + child_rect.x;
      if (cobiwm_get_locale_direction () == COBIWM_LOCALE_DIRECTION_RTL)
        x += child_rect.width;

      y = frame_rect.y + child_rect.y;
      cobiwm_window_show_menu (display->focus_window, COBIWM_WINDOW_MENU_WM, x, y);
    }
}

static void
do_choose_window (CobiwmDisplay     *display,
                  CobiwmScreen      *screen,
                  CobiwmWindow      *event_window,
                  ClutterKeyEvent *event,
                  CobiwmKeyBinding  *binding,
                  gboolean         backward)
{
  CobiwmTabList type = binding->handler->data;
  CobiwmWindow *window;

  cobiwm_topic (COBIWM_DEBUG_KEYBINDINGS,
              "Tab list = %u\n", type);

  window = cobiwm_display_get_tab_next (display,
                                      type,
                                      screen->active_workspace,
                                      NULL,
                                      backward);

  if (window)
    cobiwm_window_activate (window, event->time);
}

static void
handle_switch (CobiwmDisplay     *display,
               CobiwmScreen      *screen,
               CobiwmWindow      *event_window,
               ClutterKeyEvent *event,
               CobiwmKeyBinding  *binding,
               gpointer         dummy)
{
  gboolean backwards = cobiwm_key_binding_is_reversed (binding);
  do_choose_window (display, screen, event_window, event, binding, backwards);
}

static void
handle_toggle_fullscreen  (CobiwmDisplay     *display,
                           CobiwmScreen      *screen,
                           CobiwmWindow      *window,
                           ClutterKeyEvent *event,
                           CobiwmKeyBinding  *binding,
                           gpointer         dummy)
{
  if (window->fullscreen)
    cobiwm_window_unmake_fullscreen (window);
  else if (window->has_fullscreen_func)
    cobiwm_window_make_fullscreen (window);
}

static void
handle_toggle_above       (CobiwmDisplay     *display,
                           CobiwmScreen      *screen,
                           CobiwmWindow      *window,
                           ClutterKeyEvent *event,
                           CobiwmKeyBinding  *binding,
                           gpointer         dummy)
{
  if (window->wm_state_above)
    cobiwm_window_unmake_above (window);
  else
    cobiwm_window_make_above (window);
}

static void
handle_toggle_tiled (CobiwmDisplay     *display,
                     CobiwmScreen      *screen,
                     CobiwmWindow      *window,
                     ClutterKeyEvent *event,
                     CobiwmKeyBinding  *binding,
                     gpointer         dummy)
{
  CobiwmTileMode mode = binding->handler->data;

  if ((COBIWM_WINDOW_TILED_LEFT (window) && mode == COBIWM_TILE_LEFT) ||
      (COBIWM_WINDOW_TILED_RIGHT (window) && mode == COBIWM_TILE_RIGHT))
    {
      window->tile_monitor_number = window->saved_maximize ? window->monitor->number
        : -1;
      window->tile_mode = window->saved_maximize ? COBIWM_TILE_MAXIMIZED
        : COBIWM_TILE_NONE;

      if (window->saved_maximize)
        cobiwm_window_maximize (window, COBIWM_MAXIMIZE_BOTH);
      else
        cobiwm_window_unmaximize (window, COBIWM_MAXIMIZE_BOTH);
    }
  else if (cobiwm_window_can_tile_side_by_side (window))
    {
      window->tile_monitor_number = window->monitor->number;
      window->tile_mode = mode;
      /* Maximization constraints beat tiling constraints, so if the window
       * is maximized, tiling won't have any effect unless we unmaximize it
       * horizontally first; rather than calling cobiwm_window_unmaximize(),
       * we just set the flag and rely on cobiwm_window_tile() syncing it to
       * save an additional roundtrip.
       */
      window->maximized_horizontally = FALSE;
      cobiwm_window_tile (window);
    }
}

static void
handle_toggle_maximized    (CobiwmDisplay     *display,
                            CobiwmScreen      *screen,
                            CobiwmWindow      *window,
                            ClutterKeyEvent *event,
                            CobiwmKeyBinding  *binding,
                            gpointer         dummy)
{
  if (COBIWM_WINDOW_MAXIMIZED (window))
    cobiwm_window_unmaximize (window, COBIWM_MAXIMIZE_BOTH);
  else if (window->has_maximize_func)
    cobiwm_window_maximize (window, COBIWM_MAXIMIZE_BOTH);
}

static void
handle_maximize           (CobiwmDisplay     *display,
                           CobiwmScreen      *screen,
                           CobiwmWindow      *window,
                           ClutterKeyEvent *event,
                           CobiwmKeyBinding  *binding,
                           gpointer         dummy)
{
  if (window->has_maximize_func)
    cobiwm_window_maximize (window, COBIWM_MAXIMIZE_BOTH);
}

static void
handle_unmaximize         (CobiwmDisplay     *display,
                           CobiwmScreen      *screen,
                           CobiwmWindow      *window,
                           ClutterKeyEvent *event,
                           CobiwmKeyBinding  *binding,
                           gpointer         dummy)
{
  if (window->maximized_vertically || window->maximized_horizontally)
    cobiwm_window_unmaximize (window, COBIWM_MAXIMIZE_BOTH);
}

static void
handle_toggle_shaded      (CobiwmDisplay     *display,
                           CobiwmScreen      *screen,
                           CobiwmWindow      *window,
                           ClutterKeyEvent *event,
                           CobiwmKeyBinding  *binding,
                           gpointer         dummy)
{
  if (window->shaded)
    cobiwm_window_unshade (window, event->time);
  else if (window->has_shade_func)
    cobiwm_window_shade (window, event->time);
}

static void
handle_close              (CobiwmDisplay     *display,
                           CobiwmScreen      *screen,
                           CobiwmWindow      *window,
                           ClutterKeyEvent *event,
                           CobiwmKeyBinding  *binding,
                           gpointer         dummy)
{
  if (window->has_close_func)
    cobiwm_window_delete (window, event->time);
}

static void
handle_minimize        (CobiwmDisplay     *display,
                        CobiwmScreen      *screen,
                        CobiwmWindow      *window,
                        ClutterKeyEvent *event,
                        CobiwmKeyBinding  *binding,
                        gpointer         dummy)
{
  if (window->has_minimize_func)
    cobiwm_window_minimize (window);
}

static void
handle_begin_move         (CobiwmDisplay     *display,
                           CobiwmScreen      *screen,
                           CobiwmWindow      *window,
                           ClutterKeyEvent *event,
                           CobiwmKeyBinding  *binding,
                           gpointer         dummy)
{
  if (window->has_move_func)
    {
      cobiwm_window_begin_grab_op (window,
                                 COBIWM_GRAB_OP_KEYBOARD_MOVING,
                                 FALSE,
                                 event->time);
    }
}

static void
handle_begin_resize       (CobiwmDisplay     *display,
                           CobiwmScreen      *screen,
                           CobiwmWindow      *window,
                           ClutterKeyEvent *event,
                           CobiwmKeyBinding  *binding,
                           gpointer         dummy)
{
  if (window->has_resize_func)
    {
      cobiwm_window_begin_grab_op (window,
                                 COBIWM_GRAB_OP_KEYBOARD_RESIZING_UNKNOWN,
                                 FALSE,
                                 event->time);
    }
}

static void
handle_toggle_on_all_workspaces (CobiwmDisplay     *display,
                                 CobiwmScreen      *screen,
                                 CobiwmWindow      *window,
                                 ClutterKeyEvent *event,
                                 CobiwmKeyBinding  *binding,
                                 gpointer         dummy)
{
  if (window->on_all_workspaces_requested)
    cobiwm_window_unstick (window);
  else
    cobiwm_window_stick (window);
}

static void
handle_move_to_workspace  (CobiwmDisplay     *display,
                           CobiwmScreen      *screen,
                           CobiwmWindow      *window,
                           ClutterKeyEvent *event,
                           CobiwmKeyBinding  *binding,
                           gpointer         dummy)
{
  gint which = binding->handler->data;
  gboolean flip = (which < 0);
  CobiwmWorkspace *workspace;

  /* If which is zero or positive, it's a workspace number, and the window
   * should move to the workspace with that number.
   *
   * However, if it's negative, it's a direction with respect to the current
   * position; it's expressed as a member of the CobiwmMotionDirection enum,
   * all of whose members are negative.  Such a change is called a flip.
   */

  if (window->always_sticky)
    return;

  workspace = NULL;
  if (flip)
    {
      workspace = cobiwm_workspace_get_neighbor (screen->active_workspace,
                                               which);
    }
  else
    {
      workspace = cobiwm_screen_get_workspace_by_index (screen, which);
    }

  if (workspace)
    {
      /* Activate second, so the window is never unmapped */
      cobiwm_window_change_workspace (window, workspace);
      if (flip)
        {
          cobiwm_topic (COBIWM_DEBUG_FOCUS,
                      "Resetting mouse_mode to FALSE due to "
                      "handle_move_to_workspace() call with flip set.\n");
          cobiwm_display_clear_mouse_mode (workspace->screen->display);
          cobiwm_workspace_activate_with_focus (workspace,
                                              window,
                                              event->time);
        }
    }
  else
    {
      /* We could offer to create it I suppose */
    }
}

static void
handle_move_to_monitor (CobiwmDisplay    *display,
                        CobiwmScreen     *screen,
                        CobiwmWindow     *window,
		        ClutterKeyEvent *event,
                        CobiwmKeyBinding *binding,
                        gpointer        dummy)
{
  gint which = binding->handler->data;
  const CobiwmMonitorInfo *current, *new;

  current = window->monitor;
  new = cobiwm_screen_get_monitor_neighbor (screen, current->number, which);

  if (new == NULL)
    return;

  cobiwm_window_move_to_monitor (window, new->number);
}

static void
handle_raise_or_lower (CobiwmDisplay     *display,
                       CobiwmScreen      *screen,
		       CobiwmWindow      *window,
		       ClutterKeyEvent *event,
		       CobiwmKeyBinding  *binding,
                       gpointer         dummy)
{
  /* Get window at pointer */

  CobiwmWindow *above = NULL;

  /* Check if top */
  if (cobiwm_stack_get_top (window->screen->stack) == window)
    {
      cobiwm_window_lower (window);
      return;
    }

  /* else check if windows in same layer are intersecting it */

  above = cobiwm_stack_get_above (window->screen->stack, window, TRUE);

  while (above)
    {
      CobiwmRectangle tmp, win_rect, above_rect;

      if (above->mapped)
        {
          cobiwm_window_get_frame_rect (window, &win_rect);
          cobiwm_window_get_frame_rect (above, &above_rect);

          /* Check if obscured */
          if (cobiwm_rectangle_intersect (&win_rect, &above_rect, &tmp))
            {
              cobiwm_window_raise (window);
              return;
            }
        }

      above = cobiwm_stack_get_above (window->screen->stack, above, TRUE);
    }

  /* window is not obscured */
  cobiwm_window_lower (window);
}

static void
handle_raise (CobiwmDisplay     *display,
              CobiwmScreen      *screen,
              CobiwmWindow      *window,
              ClutterKeyEvent *event,
              CobiwmKeyBinding  *binding,
              gpointer         dummy)
{
  cobiwm_window_raise (window);
}

static void
handle_lower (CobiwmDisplay     *display,
              CobiwmScreen      *screen,
              CobiwmWindow      *window,
              ClutterKeyEvent *event,
              CobiwmKeyBinding  *binding,
              gpointer         dummy)
{
  cobiwm_window_lower (window);
}

static void
handle_set_spew_mark (CobiwmDisplay     *display,
                      CobiwmScreen      *screen,
                      CobiwmWindow      *window,
                      ClutterKeyEvent *event,
                      CobiwmKeyBinding  *binding,
                      gpointer         dummy)
{
  cobiwm_verbose ("-- MARK MARK MARK MARK --\n");
}

#ifdef HAVE_NATIVE_BACKEND
static void
handle_switch_vt (CobiwmDisplay     *display,
                  CobiwmScreen      *screen,
                  CobiwmWindow      *window,
                  ClutterKeyEvent *event,
                  CobiwmKeyBinding  *binding,
                  gpointer         dummy)
{
  gint vt = binding->handler->data;
  GError *error = NULL;

  if (!cobiwm_activate_vt (vt, &error))
    {
      g_warning ("Failed to switch VT: %s", error->message);
      g_error_free (error);
    }
}
#endif /* HAVE_NATIVE_BACKEND */

/**
 * cobiwm_keybindings_set_custom_handler:
 * @name: The name of the keybinding to set
 * @handler: (nullable): The new handler function
 * @user_data: User data to pass to the callback
 * @free_data: Will be called when this handler is overridden.
 *
 * Allows users to register a custom handler for a
 * builtin key binding.
 *
 * Returns: %TRUE if the binding known as @name was found,
 * %FALSE otherwise.
 */
gboolean
cobiwm_keybindings_set_custom_handler (const gchar        *name,
                                     CobiwmKeyHandlerFunc  handler,
                                     gpointer            user_data,
                                     GDestroyNotify      free_data)
{
  CobiwmKeyHandler *key_handler = HANDLER (name);

  if (!key_handler)
    return FALSE;

  if (key_handler->user_data_free_func && key_handler->user_data)
    key_handler->user_data_free_func (key_handler->user_data);

  key_handler->func = handler;
  key_handler->user_data = user_data;
  key_handler->user_data_free_func = free_data;

  return TRUE;
}

static void
init_builtin_key_bindings (CobiwmDisplay *display)
{
  GSettings *cobiwm_keybindings = g_settings_new (SCHEMA_COBIWM_KEYBINDINGS);
  GSettings *cobiwm_wayland_keybindings = g_settings_new (SCHEMA_COBIWM_WAYLAND_KEYBINDINGS);

  add_builtin_keybinding (display,
                          "switch-to-workspace-1",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_NONE,
                          COBIWM_KEYBINDING_ACTION_WORKSPACE_1,
                          handle_switch_to_workspace, 0);
  add_builtin_keybinding (display,
                          "switch-to-workspace-2",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_NONE,
                          COBIWM_KEYBINDING_ACTION_WORKSPACE_2,
                          handle_switch_to_workspace, 1);
  add_builtin_keybinding (display,
                          "switch-to-workspace-3",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_NONE,
                          COBIWM_KEYBINDING_ACTION_WORKSPACE_3,
                          handle_switch_to_workspace, 2);
  add_builtin_keybinding (display,
                          "switch-to-workspace-4",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_NONE,
                          COBIWM_KEYBINDING_ACTION_WORKSPACE_4,
                          handle_switch_to_workspace, 3);
  add_builtin_keybinding (display,
                          "switch-to-workspace-5",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_NONE,
                          COBIWM_KEYBINDING_ACTION_WORKSPACE_5,
                          handle_switch_to_workspace, 4);
  add_builtin_keybinding (display,
                          "switch-to-workspace-6",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_NONE,
                          COBIWM_KEYBINDING_ACTION_WORKSPACE_6,
                          handle_switch_to_workspace, 5);
  add_builtin_keybinding (display,
                          "switch-to-workspace-7",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_NONE,
                          COBIWM_KEYBINDING_ACTION_WORKSPACE_7,
                          handle_switch_to_workspace, 6);
  add_builtin_keybinding (display,
                          "switch-to-workspace-8",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_NONE,
                          COBIWM_KEYBINDING_ACTION_WORKSPACE_8,
                          handle_switch_to_workspace, 7);
  add_builtin_keybinding (display,
                          "switch-to-workspace-9",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_NONE,
                          COBIWM_KEYBINDING_ACTION_WORKSPACE_9,
                          handle_switch_to_workspace, 8);
  add_builtin_keybinding (display,
                          "switch-to-workspace-10",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_NONE,
                          COBIWM_KEYBINDING_ACTION_WORKSPACE_10,
                          handle_switch_to_workspace, 9);
  add_builtin_keybinding (display,
                          "switch-to-workspace-11",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_NONE,
                          COBIWM_KEYBINDING_ACTION_WORKSPACE_11,
                          handle_switch_to_workspace, 10);
  add_builtin_keybinding (display,
                          "switch-to-workspace-12",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_NONE,
                          COBIWM_KEYBINDING_ACTION_WORKSPACE_12,
                          handle_switch_to_workspace, 11);

  add_builtin_keybinding (display,
                          "switch-to-workspace-left",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_NONE,
                          COBIWM_KEYBINDING_ACTION_WORKSPACE_LEFT,
                          handle_switch_to_workspace, COBIWM_MOTION_LEFT);

  add_builtin_keybinding (display,
                          "switch-to-workspace-right",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_NONE,
                          COBIWM_KEYBINDING_ACTION_WORKSPACE_RIGHT,
                          handle_switch_to_workspace, COBIWM_MOTION_RIGHT);

  add_builtin_keybinding (display,
                          "switch-to-workspace-up",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_NONE,
                          COBIWM_KEYBINDING_ACTION_WORKSPACE_UP,
                          handle_switch_to_workspace, COBIWM_MOTION_UP);

  add_builtin_keybinding (display,
                          "switch-to-workspace-down",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_NONE,
                          COBIWM_KEYBINDING_ACTION_WORKSPACE_DOWN,
                          handle_switch_to_workspace, COBIWM_MOTION_DOWN);



  /* The ones which have inverses.  These can't be bound to any keystroke
   * containing Shift because Shift will invert their "backward" state.
   *
   * TODO: "NORMAL" and "DOCKS" should be renamed to the same name as their
   * action, for obviousness.
   */

  add_builtin_keybinding (display,
                          "switch-group",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_NONE,
                          COBIWM_KEYBINDING_ACTION_SWITCH_GROUP,
                          handle_switch, COBIWM_TAB_LIST_GROUP);

  add_builtin_keybinding (display,
                          "switch-group-backward",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_IS_REVERSED,
                          COBIWM_KEYBINDING_ACTION_SWITCH_GROUP_BACKWARD,
                          handle_switch, COBIWM_TAB_LIST_GROUP);

  add_builtin_keybinding (display,
                          "switch-windows",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_NONE,
                          COBIWM_KEYBINDING_ACTION_SWITCH_WINDOWS,
                          handle_switch, COBIWM_TAB_LIST_NORMAL);

  add_builtin_keybinding (display,
                          "switch-windows-backward",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_IS_REVERSED,
                          COBIWM_KEYBINDING_ACTION_SWITCH_WINDOWS_BACKWARD,
                          handle_switch, COBIWM_TAB_LIST_NORMAL);

  add_builtin_keybinding (display,
                          "switch-panels",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_NONE,
                          COBIWM_KEYBINDING_ACTION_SWITCH_PANELS,
                          handle_switch, COBIWM_TAB_LIST_DOCKS);

  add_builtin_keybinding (display,
                          "switch-panels-backward",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_IS_REVERSED,
                          COBIWM_KEYBINDING_ACTION_SWITCH_PANELS_BACKWARD,
                          handle_switch, COBIWM_TAB_LIST_DOCKS);

  /***********************************/

  add_builtin_keybinding (display,
                          "show-desktop",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_NONE,
                          COBIWM_KEYBINDING_ACTION_SHOW_DESKTOP,
                          handle_show_desktop, 0);

  add_builtin_keybinding (display,
                          "panel-run-dialog",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_NONE,
                          COBIWM_KEYBINDING_ACTION_PANEL_RUN_DIALOG,
                          handle_panel, COBIWM_KEYBINDING_ACTION_PANEL_RUN_DIALOG);

  add_builtin_keybinding (display,
                          "set-spew-mark",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_NONE,
                          COBIWM_KEYBINDING_ACTION_SET_SPEW_MARK,
                          handle_set_spew_mark, 0);

#ifdef HAVE_NATIVE_BACKEND
  CobiwmBackend *backend = cobiwm_get_backend ();
  if (COBIWM_IS_BACKEND_NATIVE (backend))
    {
      add_builtin_keybinding (display,
                              "switch-to-session-1",
                              cobiwm_wayland_keybindings,
                              COBIWM_KEY_BINDING_NONE,
                              COBIWM_KEYBINDING_ACTION_NONE,
                              handle_switch_vt, 1);

      add_builtin_keybinding (display,
                              "switch-to-session-2",
                              cobiwm_wayland_keybindings,
                              COBIWM_KEY_BINDING_NONE,
                              COBIWM_KEYBINDING_ACTION_NONE,
                              handle_switch_vt, 2);

      add_builtin_keybinding (display,
                              "switch-to-session-3",
                              cobiwm_wayland_keybindings,
                              COBIWM_KEY_BINDING_NONE,
                              COBIWM_KEYBINDING_ACTION_NONE,
                              handle_switch_vt, 3);

      add_builtin_keybinding (display,
                              "switch-to-session-4",
                              cobiwm_wayland_keybindings,
                              COBIWM_KEY_BINDING_NONE,
                              COBIWM_KEYBINDING_ACTION_NONE,
                              handle_switch_vt, 4);

      add_builtin_keybinding (display,
                              "switch-to-session-5",
                              cobiwm_wayland_keybindings,
                              COBIWM_KEY_BINDING_NONE,
                              COBIWM_KEYBINDING_ACTION_NONE,
                              handle_switch_vt, 5);

      add_builtin_keybinding (display,
                              "switch-to-session-6",
                              cobiwm_wayland_keybindings,
                              COBIWM_KEY_BINDING_NONE,
                              COBIWM_KEYBINDING_ACTION_NONE,
                              handle_switch_vt, 6);

      add_builtin_keybinding (display,
                              "switch-to-session-7",
                              cobiwm_wayland_keybindings,
                              COBIWM_KEY_BINDING_NONE,
                              COBIWM_KEYBINDING_ACTION_NONE,
                              handle_switch_vt, 7);

      add_builtin_keybinding (display,
                              "switch-to-session-8",
                              cobiwm_wayland_keybindings,
                              COBIWM_KEY_BINDING_NONE,
                              COBIWM_KEYBINDING_ACTION_NONE,
                              handle_switch_vt, 8);

      add_builtin_keybinding (display,
                              "switch-to-session-9",
                              cobiwm_wayland_keybindings,
                              COBIWM_KEY_BINDING_NONE,
                              COBIWM_KEYBINDING_ACTION_NONE,
                              handle_switch_vt, 9);

      add_builtin_keybinding (display,
                              "switch-to-session-10",
                              cobiwm_wayland_keybindings,
                              COBIWM_KEY_BINDING_NONE,
                              COBIWM_KEYBINDING_ACTION_NONE,
                              handle_switch_vt, 10);

      add_builtin_keybinding (display,
                              "switch-to-session-11",
                              cobiwm_wayland_keybindings,
                              COBIWM_KEY_BINDING_NONE,
                              COBIWM_KEYBINDING_ACTION_NONE,
                              handle_switch_vt, 11);

      add_builtin_keybinding (display,
                              "switch-to-session-12",
                              cobiwm_wayland_keybindings,
                              COBIWM_KEY_BINDING_NONE,
                              COBIWM_KEYBINDING_ACTION_NONE,
                              handle_switch_vt, 12);
    }
#endif /* HAVE_NATIVE_BACKEND */

  /************************ PER WINDOW BINDINGS ************************/

  /* These take a window as an extra parameter; they have no effect
   * if no window is active.
   */

  add_builtin_keybinding (display,
                          "activate-window-menu",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_PER_WINDOW,
                          COBIWM_KEYBINDING_ACTION_ACTIVATE_WINDOW_MENU,
                          handle_activate_window_menu, 0);

  add_builtin_keybinding (display,
                          "toggle-fullscreen",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_PER_WINDOW,
                          COBIWM_KEYBINDING_ACTION_TOGGLE_FULLSCREEN,
                          handle_toggle_fullscreen, 0);

  add_builtin_keybinding (display,
                          "toggle-maximized",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_PER_WINDOW,
                          COBIWM_KEYBINDING_ACTION_TOGGLE_MAXIMIZED,
                          handle_toggle_maximized, 0);

  add_builtin_keybinding (display,
                          "toggle-tiled-left",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_PER_WINDOW,
                          COBIWM_KEYBINDING_ACTION_TOGGLE_TILED_LEFT,
                          handle_toggle_tiled, COBIWM_TILE_LEFT);

  add_builtin_keybinding (display,
                          "toggle-tiled-right",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_PER_WINDOW,
                          COBIWM_KEYBINDING_ACTION_TOGGLE_TILED_RIGHT,
                          handle_toggle_tiled, COBIWM_TILE_RIGHT);

  add_builtin_keybinding (display,
                          "toggle-above",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_PER_WINDOW,
                          COBIWM_KEYBINDING_ACTION_TOGGLE_ABOVE,
                          handle_toggle_above, 0);

  add_builtin_keybinding (display,
                          "maximize",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_PER_WINDOW,
                          COBIWM_KEYBINDING_ACTION_MAXIMIZE,
                          handle_maximize, 0);

  add_builtin_keybinding (display,
                          "unmaximize",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_PER_WINDOW,
                          COBIWM_KEYBINDING_ACTION_UNMAXIMIZE,
                          handle_unmaximize, 0);

  add_builtin_keybinding (display,
                          "toggle-shaded",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_PER_WINDOW,
                          COBIWM_KEYBINDING_ACTION_TOGGLE_SHADED,
                          handle_toggle_shaded, 0);

  add_builtin_keybinding (display,
                          "minimize",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_PER_WINDOW,
                          COBIWM_KEYBINDING_ACTION_MINIMIZE,
                          handle_minimize, 0);

  add_builtin_keybinding (display,
                          "close",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_PER_WINDOW,
                          COBIWM_KEYBINDING_ACTION_CLOSE,
                          handle_close, 0);

  add_builtin_keybinding (display,
                          "begin-move",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_PER_WINDOW,
                          COBIWM_KEYBINDING_ACTION_BEGIN_MOVE,
                          handle_begin_move, 0);

  add_builtin_keybinding (display,
                          "begin-resize",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_PER_WINDOW,
                          COBIWM_KEYBINDING_ACTION_BEGIN_RESIZE,
                          handle_begin_resize, 0);

  add_builtin_keybinding (display,
                          "toggle-on-all-workspaces",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_PER_WINDOW,
                          COBIWM_KEYBINDING_ACTION_TOGGLE_ON_ALL_WORKSPACES,
                          handle_toggle_on_all_workspaces, 0);

  add_builtin_keybinding (display,
                          "move-to-workspace-1",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_PER_WINDOW,
                          COBIWM_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_1,
                          handle_move_to_workspace, 0);

  add_builtin_keybinding (display,
                          "move-to-workspace-2",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_PER_WINDOW,
                          COBIWM_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_2,
                          handle_move_to_workspace, 1);

  add_builtin_keybinding (display,
                          "move-to-workspace-3",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_PER_WINDOW,
                          COBIWM_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_3,
                          handle_move_to_workspace, 2);

  add_builtin_keybinding (display,
                          "move-to-workspace-4",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_PER_WINDOW,
                          COBIWM_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_4,
                          handle_move_to_workspace, 3);

  add_builtin_keybinding (display,
                          "move-to-workspace-5",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_PER_WINDOW,
                          COBIWM_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_5,
                          handle_move_to_workspace, 4);

  add_builtin_keybinding (display,
                          "move-to-workspace-6",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_PER_WINDOW,
                          COBIWM_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_6,
                          handle_move_to_workspace, 5);

  add_builtin_keybinding (display,
                          "move-to-workspace-7",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_PER_WINDOW,
                          COBIWM_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_7,
                          handle_move_to_workspace, 6);

  add_builtin_keybinding (display,
                          "move-to-workspace-8",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_PER_WINDOW,
                          COBIWM_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_8,
                          handle_move_to_workspace, 7);

  add_builtin_keybinding (display,
                          "move-to-workspace-9",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_PER_WINDOW,
                          COBIWM_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_9,
                          handle_move_to_workspace, 8);

  add_builtin_keybinding (display,
                          "move-to-workspace-10",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_PER_WINDOW,
                          COBIWM_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_10,
                          handle_move_to_workspace, 9);

  add_builtin_keybinding (display,
                          "move-to-workspace-11",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_PER_WINDOW,
                          COBIWM_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_11,
                          handle_move_to_workspace, 10);

  add_builtin_keybinding (display,
                          "move-to-workspace-12",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_PER_WINDOW,
                          COBIWM_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_12,
                          handle_move_to_workspace, 11);

  add_builtin_keybinding (display,
                          "move-to-workspace-left",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_PER_WINDOW,
                          COBIWM_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_LEFT,
                          handle_move_to_workspace, COBIWM_MOTION_LEFT);

  add_builtin_keybinding (display,
                          "move-to-workspace-right",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_PER_WINDOW,
                          COBIWM_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_RIGHT,
                          handle_move_to_workspace, COBIWM_MOTION_RIGHT);

  add_builtin_keybinding (display,
                          "move-to-workspace-up",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_PER_WINDOW,
                          COBIWM_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_UP,
                          handle_move_to_workspace, COBIWM_MOTION_UP);

  add_builtin_keybinding (display,
                          "move-to-workspace-down",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_PER_WINDOW,
                          COBIWM_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_DOWN,
                          handle_move_to_workspace, COBIWM_MOTION_DOWN);

  add_builtin_keybinding (display,
                          "move-to-monitor-left",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_PER_WINDOW,
                          COBIWM_KEYBINDING_ACTION_MOVE_TO_MONITOR_LEFT,
                          handle_move_to_monitor, COBIWM_SCREEN_LEFT);

  add_builtin_keybinding (display,
                          "move-to-monitor-right",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_PER_WINDOW,
                          COBIWM_KEYBINDING_ACTION_MOVE_TO_MONITOR_RIGHT,
                          handle_move_to_monitor, COBIWM_SCREEN_RIGHT);

  add_builtin_keybinding (display,
                          "move-to-monitor-down",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_PER_WINDOW,
                          COBIWM_KEYBINDING_ACTION_MOVE_TO_MONITOR_DOWN,
                          handle_move_to_monitor, COBIWM_SCREEN_DOWN);

  add_builtin_keybinding (display,
                          "move-to-monitor-up",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_PER_WINDOW,
                          COBIWM_KEYBINDING_ACTION_MOVE_TO_MONITOR_UP,
                          handle_move_to_monitor, COBIWM_SCREEN_UP);

  add_builtin_keybinding (display,
                          "raise-or-lower",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_PER_WINDOW,
                          COBIWM_KEYBINDING_ACTION_RAISE_OR_LOWER,
                          handle_raise_or_lower, 0);

  add_builtin_keybinding (display,
                          "raise",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_PER_WINDOW,
                          COBIWM_KEYBINDING_ACTION_RAISE,
                          handle_raise, 0);

  add_builtin_keybinding (display,
                          "lower",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_PER_WINDOW,
                          COBIWM_KEYBINDING_ACTION_LOWER,
                          handle_lower, 0);

  add_builtin_keybinding (display,
                          "maximize-vertically",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_PER_WINDOW,
                          COBIWM_KEYBINDING_ACTION_MAXIMIZE_VERTICALLY,
                          handle_maximize_vertically, 0);

  add_builtin_keybinding (display,
                          "maximize-horizontally",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_PER_WINDOW,
                          COBIWM_KEYBINDING_ACTION_MAXIMIZE_HORIZONTALLY,
                          handle_maximize_horizontally, 0);

/*
  add_builtin_keybinding (display,
                          "always-on-top",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_PER_WINDOW,
                          COBIWM_KEYBINDING_ACTION_ALWAYS_ON_TOP,
                          handle_always_on_top, 0);
*/
  add_builtin_keybinding (display,
                          "move-to-corner-nw",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_PER_WINDOW,
                          COBIWM_KEYBINDING_ACTION_MOVE_TO_CORNER_NW,
                          handle_move_to_corner_nw, 0);

  add_builtin_keybinding (display,
                          "move-to-corner-ne",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_PER_WINDOW,
                          COBIWM_KEYBINDING_ACTION_MOVE_TO_CORNER_NE,
                          handle_move_to_corner_ne, 0);

  add_builtin_keybinding (display,
                          "move-to-corner-sw",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_PER_WINDOW,
                          COBIWM_KEYBINDING_ACTION_MOVE_TO_CORNER_SW,
                          handle_move_to_corner_sw, 0);

  add_builtin_keybinding (display,
                          "move-to-corner-se",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_PER_WINDOW,
                          COBIWM_KEYBINDING_ACTION_MOVE_TO_CORNER_SE,
                          handle_move_to_corner_se, 0);

  add_builtin_keybinding (display,
                          "move-to-side-n",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_PER_WINDOW,
                          COBIWM_KEYBINDING_ACTION_MOVE_TO_SIDE_N,
                          handle_move_to_side_n, 0);

  add_builtin_keybinding (display,
                          "move-to-side-s",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_PER_WINDOW,
                          COBIWM_KEYBINDING_ACTION_MOVE_TO_SIDE_S,
                          handle_move_to_side_s, 0);

  add_builtin_keybinding (display,
                          "move-to-side-e",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_PER_WINDOW,
                          COBIWM_KEYBINDING_ACTION_MOVE_TO_SIDE_E,
                          handle_move_to_side_e, 0);

  add_builtin_keybinding (display,
                          "move-to-side-w",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_PER_WINDOW,
                          COBIWM_KEYBINDING_ACTION_MOVE_TO_SIDE_W,
                          handle_move_to_side_w, 0);

  add_builtin_keybinding (display,
                          "move-to-center",
                          cobiwm_keybindings,
                          COBIWM_KEY_BINDING_PER_WINDOW,
                          COBIWM_KEYBINDING_ACTION_MOVE_TO_CENTER,
                          handle_move_to_center, 0);

  g_object_unref (cobiwm_keybindings);
  g_object_unref (cobiwm_wayland_keybindings);
}

void
cobiwm_display_init_keys (CobiwmDisplay *display)
{
  CobiwmKeyBindingManager *keys = &display->key_binding_manager;
  CobiwmKeyHandler *handler;

  /* Keybindings */
  keys->ignored_modifier_mask = 0;
  keys->hyper_mask = 0;
  keys->super_mask = 0;
  keys->cobiwm_mask = 0;

  keys->key_bindings = g_hash_table_new_full (NULL, NULL, NULL, (GDestroyNotify) cobiwm_key_binding_free);
  keys->key_bindings_index = g_hash_table_new (NULL, NULL);

  reload_modmap (keys);

  key_handlers = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                        (GDestroyNotify) key_handler_free);

  handler = g_new0 (CobiwmKeyHandler, 1);
  handler->name = g_strdup ("overlay-key");
  handler->flags = COBIWM_KEY_BINDING_BUILTIN;

  g_hash_table_insert (key_handlers, g_strdup ("overlay-key"), handler);

  handler = g_new0 (CobiwmKeyHandler, 1);
  handler->name = g_strdup ("iso-next-group");
  handler->flags = COBIWM_KEY_BINDING_BUILTIN;

  g_hash_table_insert (key_handlers, g_strdup ("iso-next-group"), handler);

  handler = g_new0 (CobiwmKeyHandler, 1);
  handler->name = g_strdup ("external-grab");
  handler->func = handle_external_grab;
  handler->default_func = handle_external_grab;

  g_hash_table_insert (key_handlers, g_strdup ("external-grab"), handler);

  external_grabs = g_hash_table_new_full (g_str_hash, g_str_equal,
                                          NULL,
                                          (GDestroyNotify)cobiwm_key_grab_free);

  init_builtin_key_bindings (display);

  rebuild_key_binding_table (keys);
  rebuild_special_bindings (keys);

  reload_combos (keys);

  update_window_grab_modifiers (keys);

  /* Keys are actually grabbed in cobiwm_screen_grab_keys() */

  cobiwm_prefs_add_listener (prefs_changed_callback, display);

  {
    CobiwmBackend *backend = cobiwm_get_backend ();

    g_signal_connect (backend, "keymap-changed",
                      G_CALLBACK (on_keymap_changed), display);
  }
}
