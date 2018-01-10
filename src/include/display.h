/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2008 Iain Holmes
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

#ifndef COBIWM_DISPLAY_H
#define COBIWM_DISPLAY_H

#include <glib-object.h>
#include <X11/Xlib.h>

#include <types.h>
#include <prefs.h>
#include <common.h>

/**
 * CobiwmTabList:
 * @COBIWM_TAB_LIST_NORMAL: Normal windows
 * @COBIWM_TAB_LIST_DOCKS: Dock windows
 * @COBIWM_TAB_LIST_GROUP: Groups
 * @COBIWM_TAB_LIST_NORMAL_ALL: All windows
 */
typedef enum
{
  COBIWM_TAB_LIST_NORMAL,
  COBIWM_TAB_LIST_DOCKS,
  COBIWM_TAB_LIST_GROUP,
  COBIWM_TAB_LIST_NORMAL_ALL
} CobiwmTabList;

/**
 * CobiwmTabShowType:
 * @COBIWM_TAB_SHOW_ICON: Show icon (Alt-Tab mode)
 * @COBIWM_TAB_SHOW_INSTANTLY: Show instantly (Alt-Esc mode)
 */
typedef enum
{
  COBIWM_TAB_SHOW_ICON,      /* Alt-Tab mode */
  COBIWM_TAB_SHOW_INSTANTLY  /* Alt-Esc mode */
} CobiwmTabShowType;

typedef struct _CobiwmDisplayClass CobiwmDisplayClass;

#define COBIWM_TYPE_DISPLAY              (cobiwm_display_get_type ())
#define COBIWM_DISPLAY(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), COBIWM_TYPE_DISPLAY, CobiwmDisplay))
#define COBIWM_DISPLAY_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), COBIWM_TYPE_DISPLAY, CobiwmDisplayClass))
#define COBIWM_IS_DISPLAY(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), COBIWM_TYPE_DISPLAY))
#define COBIWM_IS_DISPLAY_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), COBIWM_TYPE_DISPLAY))
#define COBIWM_DISPLAY_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), COBIWM_TYPE_DISPLAY, CobiwmDisplayClass))

GType cobiwm_display_get_type (void) G_GNUC_CONST;

#define cobiwm_XFree(p) do { if ((p)) XFree ((p)); } while (0)

int cobiwm_display_get_xinput_opcode (CobiwmDisplay *display);
gboolean cobiwm_display_supports_extended_barriers (CobiwmDisplay *display);
Display *cobiwm_display_get_xdisplay (CobiwmDisplay *display);
CobiwmCompositor *cobiwm_display_get_compositor (CobiwmDisplay *display);

gboolean cobiwm_display_has_shape (CobiwmDisplay *display);

CobiwmWindow *cobiwm_display_get_focus_window (CobiwmDisplay *display);

gboolean  cobiwm_display_xwindow_is_a_no_focus_window (CobiwmDisplay *display,
                                                     Window xwindow);

int cobiwm_display_get_damage_event_base (CobiwmDisplay *display);
int cobiwm_display_get_shape_event_base (CobiwmDisplay *display);

gboolean cobiwm_display_xserver_time_is_before (CobiwmDisplay *display,
                                              guint32      time1,
                                              guint32      time2);

guint32 cobiwm_display_get_last_user_time (CobiwmDisplay *display);
guint32 cobiwm_display_get_current_time (CobiwmDisplay *display);
guint32 cobiwm_display_get_current_time_roundtrip (CobiwmDisplay *display);

GList* cobiwm_display_get_tab_list (CobiwmDisplay   *display,
                                  CobiwmTabList    type,
                                  CobiwmWorkspace *workspace);

CobiwmWindow* cobiwm_display_get_tab_next (CobiwmDisplay   *display,
                                       CobiwmTabList    type,
                                       CobiwmWorkspace *workspace,
                                       CobiwmWindow    *window,
                                       gboolean       backward);

CobiwmWindow* cobiwm_display_get_tab_current (CobiwmDisplay   *display,
                                          CobiwmTabList    type,
                                          CobiwmWorkspace *workspace);

gboolean cobiwm_display_begin_grab_op (CobiwmDisplay *display,
                                     CobiwmScreen  *screen,
                                     CobiwmWindow  *window,
                                     CobiwmGrabOp   op,
                                     gboolean     pointer_already_grabbed,
                                     gboolean     frame_action,
                                     int          button,
                                     gulong       modmask,
                                     guint32      timestamp,
                                     int          root_x,
                                     int          root_y);
void     cobiwm_display_end_grab_op   (CobiwmDisplay *display,
                                     guint32      timestamp);

CobiwmGrabOp cobiwm_display_get_grab_op (CobiwmDisplay *display);

guint cobiwm_display_add_keybinding    (CobiwmDisplay         *display,
                                      const char          *name,
                                      GSettings           *settings,
                                      CobiwmKeyBindingFlags  flags,
                                      CobiwmKeyHandlerFunc   handler,
                                      gpointer             user_data,
                                      GDestroyNotify       free_data);
gboolean cobiwm_display_remove_keybinding (CobiwmDisplay         *display,
                                         const char          *name);

guint    cobiwm_display_grab_accelerator   (CobiwmDisplay *display,
                                          const char  *accelerator);
gboolean cobiwm_display_ungrab_accelerator (CobiwmDisplay *display,
                                          guint        action_id);

guint cobiwm_display_get_keybinding_action (CobiwmDisplay  *display,
                                          unsigned int  keycode,
                                          unsigned long mask);

/* cobiwm_display_set_input_focus_window is like XSetInputFocus, except
 * that (a) it can't detect timestamps later than the current time,
 * since Cobiwm isn't part of the XServer, and thus gives erroneous
 * behavior in this circumstance (so don't do it), (b) it uses
 * display->last_focus_time since we don't have access to the true
 * Xserver one, (c) it makes use of display->user_time since checking
 * whether a window should be allowed to be focused should depend
 * on user_time events (see bug 167358, comment 15 in particular)
 */
void cobiwm_display_set_input_focus_window   (CobiwmDisplay *display,
                                            CobiwmWindow  *window,
                                            gboolean     focus_frame,
                                            guint32      timestamp);

/* cobiwm_display_focus_the_no_focus_window is called when the
 * designated no_focus_window should be focused, but is otherwise the
 * same as cobiwm_display_set_input_focus_window
 */
void cobiwm_display_focus_the_no_focus_window (CobiwmDisplay *display,
                                             CobiwmScreen  *screen,
                                             guint32      timestamp);

GSList *cobiwm_display_sort_windows_by_stacking (CobiwmDisplay *display,
                                               GSList      *windows);

void cobiwm_display_add_ignored_crossing_serial (CobiwmDisplay  *display,
                                               unsigned long serial);

void cobiwm_display_unmanage_screen (CobiwmDisplay *display,
                                   CobiwmScreen  *screen,
                                   guint32      timestamp);

void cobiwm_display_clear_mouse_mode (CobiwmDisplay *display);

void cobiwm_display_freeze_keyboard (CobiwmDisplay *display,
                                   guint32      timestamp);
void cobiwm_display_ungrab_keyboard (CobiwmDisplay *display,
                                   guint32      timestamp);
void cobiwm_display_unfreeze_keyboard (CobiwmDisplay *display,
                                     guint32      timestamp);
gboolean cobiwm_display_is_pointer_emulating_sequence (CobiwmDisplay          *display,
                                                     ClutterEventSequence *sequence);

#endif
