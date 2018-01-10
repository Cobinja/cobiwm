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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef COBIWM_UTIL_H
#define COBIWM_UTIL_H

#include <glib.h>
#include <glib-object.h>

#include <common.h>

gboolean cobiwm_is_verbose  (void);
gboolean cobiwm_is_debugging (void);
gboolean cobiwm_is_syncing (void);
gboolean cobiwm_is_wayland_compositor (void);

void cobiwm_debug_spew_real (const char *format,
                           ...) G_GNUC_PRINTF (1, 2);
void cobiwm_verbose_real    (const char *format,
                           ...) G_GNUC_PRINTF (1, 2);

void cobiwm_bug        (const char *format,
                      ...) G_GNUC_PRINTF (1, 2);
void cobiwm_warning    (const char *format,
                      ...) G_GNUC_PRINTF (1, 2);
void cobiwm_fatal      (const char *format,
                      ...) G_GNUC_PRINTF (1, 2);

/**
 * CobiwmDebugTopic:
 * @COBIWM_DEBUG_VERBOSE: verbose logging
 * @COBIWM_DEBUG_FOCUS: focus
 * @COBIWM_DEBUG_WORKAREA: workarea
 * @COBIWM_DEBUG_STACK: stack
 * @COBIWM_DEBUG_THEMES: themes
 * @COBIWM_DEBUG_SM: session management
 * @COBIWM_DEBUG_EVENTS: events
 * @COBIWM_DEBUG_WINDOW_STATE: window state
 * @COBIWM_DEBUG_WINDOW_OPS: window operations
 * @COBIWM_DEBUG_GEOMETRY: geometry
 * @COBIWM_DEBUG_PLACEMENT: window placement
 * @COBIWM_DEBUG_PING: ping
 * @COBIWM_DEBUG_XINERAMA: Xinerama
 * @COBIWM_DEBUG_KEYBINDINGS: keybindings
 * @COBIWM_DEBUG_SYNC: sync
 * @COBIWM_DEBUG_ERRORS: errors
 * @COBIWM_DEBUG_STARTUP: startup
 * @COBIWM_DEBUG_PREFS: preferences
 * @COBIWM_DEBUG_GROUPS: groups
 * @COBIWM_DEBUG_RESIZING: resizing
 * @COBIWM_DEBUG_SHAPES: shapes
 * @COBIWM_DEBUG_COMPOSITOR: compositor
 * @COBIWM_DEBUG_EDGE_RESISTANCE: edge resistance
 */
typedef enum
{
  COBIWM_DEBUG_VERBOSE         = -1,
  COBIWM_DEBUG_FOCUS           = 1 << 0,
  COBIWM_DEBUG_WORKAREA        = 1 << 1,
  COBIWM_DEBUG_STACK           = 1 << 2,
  COBIWM_DEBUG_THEMES          = 1 << 3,
  COBIWM_DEBUG_SM              = 1 << 4,
  COBIWM_DEBUG_EVENTS          = 1 << 5,
  COBIWM_DEBUG_WINDOW_STATE    = 1 << 6,
  COBIWM_DEBUG_WINDOW_OPS      = 1 << 7,
  COBIWM_DEBUG_GEOMETRY        = 1 << 8,
  COBIWM_DEBUG_PLACEMENT       = 1 << 9,
  COBIWM_DEBUG_PING            = 1 << 10,
  COBIWM_DEBUG_XINERAMA        = 1 << 11,
  COBIWM_DEBUG_KEYBINDINGS     = 1 << 12,
  COBIWM_DEBUG_SYNC            = 1 << 13,
  COBIWM_DEBUG_ERRORS          = 1 << 14,
  COBIWM_DEBUG_STARTUP         = 1 << 15,
  COBIWM_DEBUG_PREFS           = 1 << 16,
  COBIWM_DEBUG_GROUPS          = 1 << 17,
  COBIWM_DEBUG_RESIZING        = 1 << 18,
  COBIWM_DEBUG_SHAPES          = 1 << 19,
  COBIWM_DEBUG_COMPOSITOR      = 1 << 20,
  COBIWM_DEBUG_EDGE_RESISTANCE = 1 << 21,
  COBIWM_DEBUG_DBUS            = 1 << 22
} CobiwmDebugTopic;

void cobiwm_topic_real      (CobiwmDebugTopic topic,
                           const char    *format,
                           ...) G_GNUC_PRINTF (2, 3);
void cobiwm_add_verbose_topic    (CobiwmDebugTopic topic);
void cobiwm_remove_verbose_topic (CobiwmDebugTopic topic);

void cobiwm_push_no_msg_prefix (void);
void cobiwm_pop_no_msg_prefix  (void);

gint  cobiwm_unsigned_long_equal (gconstpointer v1,
                                gconstpointer v2);
guint cobiwm_unsigned_long_hash  (gconstpointer v);

const char* cobiwm_frame_type_to_string (CobiwmFrameType type);
const char* cobiwm_gravity_to_string (int gravity);

char* cobiwm_external_binding_name_for_action (guint keybinding_action);

char* cobiwm_g_utf8_strndup (const gchar *src, gsize n);

void  cobiwm_free_gslist_and_elements (GSList *list_to_deep_free);

GPid cobiwm_show_dialog (const char *type,
                       const char *message,
                       const char *timeout,
                       const char *display,
                       const char *ok_text,
                       const char *cancel_text,
                       const char *icon_name,
                       const int transient_for,
                       GSList *columns,
                       GSList *entries);

/* To disable verbose mode, we make these functions into no-ops */
#ifdef WITH_VERBOSE_MODE

#define cobiwm_debug_spew cobiwm_debug_spew_real
#define cobiwm_verbose    cobiwm_verbose_real
#define cobiwm_topic      cobiwm_topic_real

#else

#  ifdef G_HAVE_ISO_VARARGS
#    define cobiwm_debug_spew(...)
#    define cobiwm_verbose(...)
#    define cobiwm_topic(...)
#  elif defined(G_HAVE_GNUC_VARARGS)
#    define cobiwm_debug_spew(format...)
#    define cobiwm_verbose(format...)
#    define cobiwm_topic(format...)
#  else
#    error "This compiler does not support varargs macros and thus verbose mode can't be disabled meaningfully"
#  endif

#endif /* !WITH_VERBOSE_MODE */

/**
 * CobiwmLaterType:
 * @COBIWM_LATER_RESIZE: call in a resize processing phase that is done
 *   before GTK+ repainting (including window borders) is done.
 * @COBIWM_LATER_CALC_SHOWING: used by Cobiwm to compute which windows should be mapped
 * @COBIWM_LATER_CHECK_FULLSCREEN: used by Cobiwm to see if there's a fullscreen window
 * @COBIWM_LATER_SYNC_STACK: used by Cobiwm to send it's idea of the stacking order to the server
 * @COBIWM_LATER_BEFORE_REDRAW: call before the stage is redrawn
 * @COBIWM_LATER_IDLE: call at a very low priority (can be blocked
 *    by running animations or redrawing applications)
 **/
typedef enum {
  COBIWM_LATER_RESIZE,
  COBIWM_LATER_CALC_SHOWING,
  COBIWM_LATER_CHECK_FULLSCREEN,
  COBIWM_LATER_SYNC_STACK,
  COBIWM_LATER_BEFORE_REDRAW,
  COBIWM_LATER_IDLE
} CobiwmLaterType;

guint cobiwm_later_add    (CobiwmLaterType  when,
                         GSourceFunc    func,
                         gpointer       data,
                         GDestroyNotify notify);
void  cobiwm_later_remove (guint          later_id);

typedef enum
{
  COBIWM_LOCALE_DIRECTION_LTR,
  COBIWM_LOCALE_DIRECTION_RTL,
} CobiwmLocaleDirection;

CobiwmLocaleDirection cobiwm_get_locale_direction (void);

#endif /* COBIWM_UTIL_H */


