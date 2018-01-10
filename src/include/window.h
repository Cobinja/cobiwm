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

#ifndef COBIWM_WINDOW_H
#define COBIWM_WINDOW_H

#include <glib-object.h>
#include <cairo.h>
#include <X11/Xlib.h>

#include <boxes.h>
#include <types.h>

/**
 * CobiwmWindowType:
 * @COBIWM_WINDOW_NORMAL: Normal
 * @COBIWM_WINDOW_DESKTOP: Desktop
 * @COBIWM_WINDOW_DOCK: Dock
 * @COBIWM_WINDOW_DIALOG: Dialog
 * @COBIWM_WINDOW_MODAL_DIALOG: Modal dialog
 * @COBIWM_WINDOW_TOOLBAR: Toolbar
 * @COBIWM_WINDOW_MENU: Menu
 * @COBIWM_WINDOW_UTILITY: Utility
 * @COBIWM_WINDOW_SPLASHSCREEN: Splashcreen
 * @COBIWM_WINDOW_DROPDOWN_MENU: Dropdown menu
 * @COBIWM_WINDOW_POPUP_MENU: Popup menu
 * @COBIWM_WINDOW_TOOLTIP: Tooltip
 * @COBIWM_WINDOW_NOTIFICATION: Notification
 * @COBIWM_WINDOW_COMBO: Combobox
 * @COBIWM_WINDOW_DND: Drag and drop
 * @COBIWM_WINDOW_OVERRIDE_OTHER: Other override-redirect window type
 */
typedef enum
{
  COBIWM_WINDOW_NORMAL,
  COBIWM_WINDOW_DESKTOP,
  COBIWM_WINDOW_DOCK,
  COBIWM_WINDOW_DIALOG,
  COBIWM_WINDOW_MODAL_DIALOG,
  COBIWM_WINDOW_TOOLBAR,
  COBIWM_WINDOW_MENU,
  COBIWM_WINDOW_UTILITY,
  COBIWM_WINDOW_SPLASHSCREEN,

  /* override redirect window types: */
  COBIWM_WINDOW_DROPDOWN_MENU,
  COBIWM_WINDOW_POPUP_MENU,
  COBIWM_WINDOW_TOOLTIP,
  COBIWM_WINDOW_NOTIFICATION,
  COBIWM_WINDOW_COMBO,
  COBIWM_WINDOW_DND,
  COBIWM_WINDOW_OVERRIDE_OTHER
} CobiwmWindowType;

/**
 * CobiwmMaximizeFlags:
 * @COBIWM_MAXIMIZE_HORIZONTAL: Horizontal
 * @COBIWM_MAXIMIZE_VERTICAL: Vertical
 * @COBIWM_MAXIMIZE_BOTH: Both
 */
typedef enum
{
  COBIWM_MAXIMIZE_HORIZONTAL = 1 << 0,
  COBIWM_MAXIMIZE_VERTICAL   = 1 << 1,
  COBIWM_MAXIMIZE_BOTH       = (1 << 0 | 1 << 1),
} CobiwmMaximizeFlags;

/**
 * CobiwmWindowClientType:
 * @COBIWM_WINDOW_CLIENT_TYPE_WAYLAND: A Wayland based window
 * @COBIWM_WINDOW_CLIENT_TYPE_X11: An X11 based window
 */
typedef enum {
  COBIWM_WINDOW_CLIENT_TYPE_WAYLAND,
  COBIWM_WINDOW_CLIENT_TYPE_X11
} CobiwmWindowClientType;

#define COBIWM_TYPE_WINDOW            (cobiwm_window_get_type ())
#define COBIWM_WINDOW(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), COBIWM_TYPE_WINDOW, CobiwmWindow))
#define COBIWM_WINDOW_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  COBIWM_TYPE_WINDOW, CobiwmWindowClass))
#define COBIWM_IS_WINDOW(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), COBIWM_TYPE_WINDOW))
#define COBIWM_IS_WINDOW_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  COBIWM_TYPE_WINDOW))
#define COBIWM_WINDOW_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  COBIWM_TYPE_WINDOW, CobiwmWindowClass))

typedef struct _CobiwmWindowClass   CobiwmWindowClass;

GType cobiwm_window_get_type (void);

CobiwmFrame *cobiwm_window_get_frame (CobiwmWindow *window);
gboolean cobiwm_window_has_focus (CobiwmWindow *window);
gboolean cobiwm_window_appears_focused (CobiwmWindow *window);
gboolean cobiwm_window_is_shaded (CobiwmWindow *window);
gboolean cobiwm_window_is_override_redirect (CobiwmWindow *window);
gboolean cobiwm_window_is_skip_taskbar (CobiwmWindow *window);
void cobiwm_window_get_buffer_rect (const CobiwmWindow *window, CobiwmRectangle *rect);

void cobiwm_window_get_frame_rect (const CobiwmWindow *window, CobiwmRectangle *rect);

void cobiwm_window_client_rect_to_frame_rect (CobiwmWindow    *window,
                                            CobiwmRectangle *client_rect,
                                            CobiwmRectangle *frame_rect);
void cobiwm_window_frame_rect_to_client_rect (CobiwmWindow    *window,
                                            CobiwmRectangle *frame_rect,
                                            CobiwmRectangle *client_rect);

CobiwmScreen *cobiwm_window_get_screen (CobiwmWindow *window);
CobiwmDisplay *cobiwm_window_get_display (CobiwmWindow *window);
Window cobiwm_window_get_xwindow (CobiwmWindow *window);
CobiwmWindowType cobiwm_window_get_window_type (CobiwmWindow *window);
CobiwmWorkspace *cobiwm_window_get_workspace (CobiwmWindow *window);
int      cobiwm_window_get_monitor (CobiwmWindow *window);
gboolean cobiwm_window_is_on_all_workspaces (CobiwmWindow *window);
gboolean cobiwm_window_located_on_workspace (CobiwmWindow    *window,
                                           CobiwmWorkspace *workspace);
gboolean cobiwm_window_is_hidden (CobiwmWindow *window);
void     cobiwm_window_activate  (CobiwmWindow *window,guint32 current_time);
void     cobiwm_window_activate_with_workspace (CobiwmWindow    *window,
                                              guint32        current_time,
                                              CobiwmWorkspace *workspace);
const char * cobiwm_window_get_description (CobiwmWindow *window);
const char * cobiwm_window_get_wm_class (CobiwmWindow *window);
const char * cobiwm_window_get_wm_class_instance (CobiwmWindow *window);
gboolean    cobiwm_window_showing_on_its_workspace (CobiwmWindow *window);

const char * cobiwm_window_get_gtk_theme_variant (CobiwmWindow *window);
const char * cobiwm_window_get_gtk_application_id (CobiwmWindow *window);
const char * cobiwm_window_get_gtk_unique_bus_name (CobiwmWindow *window);
const char * cobiwm_window_get_gtk_application_object_path (CobiwmWindow *window);
const char * cobiwm_window_get_gtk_window_object_path (CobiwmWindow *window);
const char * cobiwm_window_get_gtk_app_menu_object_path (CobiwmWindow *window);
const char * cobiwm_window_get_gtk_menubar_object_path (CobiwmWindow *window);

void cobiwm_window_move_frame(CobiwmWindow *window, gboolean user_op, int root_x_nw, int root_y_nw);
void cobiwm_window_move_resize_frame (CobiwmWindow *window, gboolean user_op, int root_x_nw, int root_y_nw, int w, int h);
void cobiwm_window_move_to_monitor (CobiwmWindow *window, int monitor);

void cobiwm_window_set_demands_attention (CobiwmWindow *window);
void cobiwm_window_unset_demands_attention (CobiwmWindow *window);

const char* cobiwm_window_get_startup_id (CobiwmWindow *window);
void cobiwm_window_change_workspace_by_index (CobiwmWindow *window,
                                            gint        space_index,
                                            gboolean    append);
void cobiwm_window_change_workspace          (CobiwmWindow  *window,
                                            CobiwmWorkspace *workspace);
GObject *cobiwm_window_get_compositor_private (CobiwmWindow *window);
void cobiwm_window_set_compositor_private (CobiwmWindow *window, GObject *priv);
const char *cobiwm_window_get_role (CobiwmWindow *window);
CobiwmStackLayer cobiwm_window_get_layer (CobiwmWindow *window);
CobiwmWindow* cobiwm_window_find_root_ancestor    (CobiwmWindow *window);
gboolean cobiwm_window_is_ancestor_of_transient (CobiwmWindow            *window,
                                               CobiwmWindow            *transient);

typedef gboolean (*CobiwmWindowForeachFunc) (CobiwmWindow *window,
                                           void       *user_data);

void     cobiwm_window_foreach_transient        (CobiwmWindow            *window,
                                               CobiwmWindowForeachFunc  func,
                                               void                  *user_data);
void     cobiwm_window_foreach_ancestor         (CobiwmWindow            *window,
                                               CobiwmWindowForeachFunc  func,
                                               void                  *user_data);

CobiwmMaximizeFlags cobiwm_window_get_maximized (CobiwmWindow *window);
gboolean          cobiwm_window_is_fullscreen (CobiwmWindow *window);
gboolean          cobiwm_window_is_screen_sized (CobiwmWindow *window);
gboolean          cobiwm_window_is_monitor_sized (CobiwmWindow *window);
gboolean          cobiwm_window_is_on_primary_monitor (CobiwmWindow *window);
gboolean          cobiwm_window_requested_bypass_compositor (CobiwmWindow *window);
gboolean          cobiwm_window_requested_dont_bypass_compositor (CobiwmWindow *window);
gint             *cobiwm_window_get_all_monitors (CobiwmWindow *window, gsize *length);

gboolean cobiwm_window_get_icon_geometry (CobiwmWindow    *window,
                                        CobiwmRectangle *rect);
void cobiwm_window_set_icon_geometry (CobiwmWindow    *window,
                                    CobiwmRectangle *rect);
void cobiwm_window_maximize   (CobiwmWindow        *window,
                             CobiwmMaximizeFlags  directions);
void cobiwm_window_unmaximize (CobiwmWindow        *window,
                             CobiwmMaximizeFlags  directions);
void        cobiwm_window_minimize           (CobiwmWindow  *window);
void        cobiwm_window_unminimize         (CobiwmWindow  *window);
void        cobiwm_window_raise              (CobiwmWindow  *window);
void        cobiwm_window_lower              (CobiwmWindow  *window);
const char *cobiwm_window_get_title (CobiwmWindow *window);
CobiwmWindow *cobiwm_window_get_transient_for (CobiwmWindow *window);
void        cobiwm_window_delete             (CobiwmWindow  *window,
                                            guint32      timestamp);
guint       cobiwm_window_get_stable_sequence (CobiwmWindow *window);
guint32     cobiwm_window_get_user_time (CobiwmWindow *window);
int         cobiwm_window_get_pid (CobiwmWindow *window);
const char *cobiwm_window_get_client_machine (CobiwmWindow *window);
gboolean    cobiwm_window_is_remote (CobiwmWindow *window);
gboolean    cobiwm_window_is_attached_dialog (CobiwmWindow *window);
const char *cobiwm_window_get_cobiwm_hints (CobiwmWindow *window);

CobiwmFrameType cobiwm_window_get_frame_type (CobiwmWindow *window);

cairo_region_t *cobiwm_window_get_frame_bounds (CobiwmWindow *window);

CobiwmWindow *cobiwm_window_get_tile_match (CobiwmWindow *window);

void        cobiwm_window_make_fullscreen    (CobiwmWindow  *window);
void        cobiwm_window_unmake_fullscreen  (CobiwmWindow  *window);
void        cobiwm_window_make_above         (CobiwmWindow  *window);
void        cobiwm_window_unmake_above       (CobiwmWindow  *window);
void        cobiwm_window_shade              (CobiwmWindow  *window,
                                            guint32      timestamp);
void        cobiwm_window_unshade            (CobiwmWindow  *window,
                                            guint32      timestamp);
void        cobiwm_window_stick              (CobiwmWindow  *window);
void        cobiwm_window_unstick            (CobiwmWindow  *window);

void        cobiwm_window_kill               (CobiwmWindow  *window);
void        cobiwm_window_focus              (CobiwmWindow  *window,
                                            guint32      timestamp);

void        cobiwm_window_check_alive        (CobiwmWindow  *window,
                                            guint32      timestamp);

void cobiwm_window_get_work_area_current_monitor (CobiwmWindow    *window,
                                                CobiwmRectangle *area);
void cobiwm_window_get_work_area_for_monitor     (CobiwmWindow    *window,
                                                int            which_monitor,
                                                CobiwmRectangle *area);
void cobiwm_window_get_work_area_all_monitors    (CobiwmWindow    *window,
                                                CobiwmRectangle *area);

void cobiwm_window_begin_grab_op (CobiwmWindow *window,
                                CobiwmGrabOp  op,
                                gboolean    frame_action,
                                guint32     timestamp);

gboolean cobiwm_window_can_maximize (CobiwmWindow *window);
gboolean cobiwm_window_can_minimize (CobiwmWindow *window);
gboolean cobiwm_window_can_shade (CobiwmWindow *window);
gboolean cobiwm_window_can_close (CobiwmWindow *window);
gboolean cobiwm_window_is_always_on_all_workspaces (CobiwmWindow *window);
gboolean cobiwm_window_is_above (CobiwmWindow *window);
gboolean cobiwm_window_allows_move (CobiwmWindow *window);
gboolean cobiwm_window_allows_resize (CobiwmWindow *window);
gboolean cobiwm_window_is_client_decorated (CobiwmWindow *window);

gboolean cobiwm_window_titlebar_is_onscreen    (CobiwmWindow *window);
void     cobiwm_window_shove_titlebar_onscreen (CobiwmWindow *window);

#endif
