/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Cobiwm X display handler */

/*
 * Copyright (C) 2001 Havoc Pennington
 * Copyright (C) 2002 Red Hat, Inc.
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

#ifndef COBIWM_DISPLAY_PRIVATE_H
#define COBIWM_DISPLAY_PRIVATE_H

#ifndef PACKAGE
#error "config.h not included"
#endif

#include <glib.h>
#include <X11/Xlib.h>
#include <common.h>
#include <boxes.h>
#include <display.h>
#include "keybindings-private.h"
#include "startup-notification-private.h"
#include "cobiwm-gesture-tracker-private.h"
#include <prefs.h>
#include <barrier.h>
#include <clutter/clutter.h>

#ifdef HAVE_STARTUP_NOTIFICATION
#include <libsn/sn.h>
#endif

#include <X11/extensions/sync.h>

typedef struct _CobiwmStack      CobiwmStack;
typedef struct _CobiwmUISlave    CobiwmUISlave;

typedef struct _CobiwmGroupPropHooks  CobiwmGroupPropHooks;
typedef struct _CobiwmWindowPropHooks CobiwmWindowPropHooks;

typedef struct CobiwmEdgeResistanceData CobiwmEdgeResistanceData;

typedef enum {
  COBIWM_LIST_DEFAULT                   = 0,      /* normal windows */
  COBIWM_LIST_INCLUDE_OVERRIDE_REDIRECT = 1 << 0, /* normal and O-R */
  COBIWM_LIST_SORTED                    = 1 << 1, /* sort list by mru */
} CobiwmListWindowsFlags;

#define _NET_WM_STATE_REMOVE        0    /* remove/unset property */
#define _NET_WM_STATE_ADD           1    /* add/set property */
#define _NET_WM_STATE_TOGGLE        2    /* toggle property  */

/* This is basically a bogus number, just has to be large enough
 * to handle the expected case of the alt+tab operation, where
 * we want to ignore serials from UnmapNotify on the tab popup,
 * and the LeaveNotify/EnterNotify from the pointer ungrab. It
 * also has to be big enough to hold ignored serials from the point
 * where we reshape the stage to the point where we get events back.
 */
#define N_IGNORED_CROSSING_SERIALS  10

typedef enum {
  COBIWM_TILE_NONE,
  COBIWM_TILE_LEFT,
  COBIWM_TILE_RIGHT,
  COBIWM_TILE_MAXIMIZED
} CobiwmTileMode;

typedef enum {
  /* Normal interaction where you're interacting with windows.
   * Events go to windows normally. */
  COBIWM_EVENT_ROUTE_NORMAL,

  /* In a window operation like moving or resizing. All events
   * goes to CobiwmWindow, but not to the actual client window. */
  COBIWM_EVENT_ROUTE_WINDOW_OP,

  /* In a compositor grab operation. All events go to the
   * compositor plugin. */
  COBIWM_EVENT_ROUTE_COMPOSITOR_GRAB,

  /* A Wayland application has a popup open. All events go to
   * the Wayland application. */
  COBIWM_EVENT_ROUTE_WAYLAND_POPUP,

  /* The user is clicking on a window button. */
  COBIWM_EVENT_ROUTE_FRAME_BUTTON,
} CobiwmEventRoute;

typedef gboolean (*CobiwmAlarmFilter) (CobiwmDisplay           *display,
                                     XSyncAlarmNotifyEvent *event,
                                     gpointer               data);

struct _CobiwmDisplay
{
  GObject parent_instance;

  char *name;
  Display *xdisplay;

  int clutter_event_filter;

  Window leader_window;
  Window timestamp_pinging_window;

  /* Pull in all the names of atoms as fields; we will intern them when the
   * class is constructed.
   */
#define item(x)  Atom atom_##x;
#include <x11/atomnames.h>
#undef item

  /* The window and serial of the most recent FocusIn event. */
  Window server_focus_window;
  gulong server_focus_serial;

  /* Our best guess as to the "currently" focused window (that is, the
   * window that we expect will be focused at the point when the X
   * server processes our next request), and the serial of the request
   * or event that caused this.
   */
  CobiwmWindow *focus_window;
  /* For windows we've focused that don't necessarily have an X window,
   * like the no_focus_window or the stage X window. */
  Window focus_xwindow;
  gulong focus_serial;

  /* last timestamp passed to XSetInputFocus */
  guint32 last_focus_time;

  /* last user interaction time in any app */
  guint32 last_user_time;

  /* whether we're using mousenav (only relevant for sloppy&mouse focus modes;
   * !mouse_mode means "keynav mode")
   */
  guint mouse_mode : 1;

  /* Helper var used when focus_new_windows setting is 'strict'; only
   * relevant in 'strict' mode and if the focus window is a terminal.
   * In that case, we don't allow new windows to take focus away from
   * a terminal, but if the user explicitly did something that should
   * allow a different window to gain focus (e.g. global keybinding or
   * clicking on a dock), then we will allow the transfer.
   */
  guint allow_terminal_deactivation : 1;

  /* If true, server->focus_serial refers to us changing the focus; in
   * this case, we can ignore focus events that have exactly focus_serial,
   * since we take care to make another request immediately afterwards.
   * But if focus is being changed by another client, we have to accept
   * multiple events with the same serial.
   */
  guint focused_by_us : 1;

  /*< private-ish >*/
  CobiwmScreen *screen;
  GHashTable *xids;
  GHashTable *stamps;
  GHashTable *wayland_windows;

  /* serials of leave/unmap events that may
   * correspond to an enter event we should
   * ignore
   */
  unsigned long ignored_crossing_serials[N_IGNORED_CROSSING_SERIALS];

  guint32 current_time;

  /* We maintain a sequence counter, incremented for each #CobiwmWindow
   * created.  This is exposed by cobiwm_window_get_stable_sequence()
   * but is otherwise not used inside cobiwm.
   *
   * It can be useful to plugins which want to sort windows in a
   * stable fashion.
   */
  guint32 window_sequence_counter;

  /* Pings which we're waiting for a reply from */
  GSList     *pending_pings;

  /* Pending focus change */
  guint       focus_timeout_id;

  /* Pending autoraise */
  guint       autoraise_timeout_id;
  CobiwmWindow* autoraise_window;

  /* Event routing */
  CobiwmEventRoute event_route;

  /* current window operation */
  CobiwmGrabOp  grab_op;
  CobiwmWindow *grab_window;
  int         grab_button;
  int         grab_anchor_root_x;
  int         grab_anchor_root_y;
  CobiwmRectangle grab_anchor_window_pos;
  CobiwmTileMode  grab_tile_mode;
  int           grab_tile_monitor_number;
  int         grab_latest_motion_x;
  int         grab_latest_motion_y;
  guint       grab_have_pointer : 1;
  guint       grab_have_keyboard : 1;
  guint       grab_frame_action : 1;
  CobiwmRectangle grab_initial_window_pos;
  int         grab_initial_x, grab_initial_y;  /* These are only relevant for */
  gboolean    grab_threshold_movement_reached; /* raise_on_click == FALSE.    */
  GTimeVal    grab_last_moveresize_time;
  CobiwmEdgeResistanceData *grab_edge_resistance_data;
  unsigned int grab_last_user_action_was_snap;

  /* we use property updates as sentinels for certain window focus events
   * to avoid some race conditions on EnterNotify events
   */
  int         sentinel_counter;

  int         xkb_base_event_type;
  guint32     last_bell_time;
  int	      grab_resize_timeout_id;

  CobiwmKeyBindingManager key_binding_manager;

  /* Monitor cache */
  unsigned int monitor_cache_invalidated : 1;

  /* Opening the display */
  unsigned int display_opening : 1;

  /* Closing down the display */
  int closing;

  /* Managed by group.c */
  GHashTable *groups_by_leader;

  /* Managed by window-props.c */
  CobiwmWindowPropHooks *prop_hooks_table;
  GHashTable *prop_hooks;
  int n_prop_hooks;

  /* Managed by group-props.c */
  CobiwmGroupPropHooks *group_prop_hooks;

  /* Managed by compositor.c */
  CobiwmCompositor *compositor;

  CobiwmGestureTracker *gesture_tracker;
  ClutterEventSequence *pointer_emulating_sequence;

  CobiwmAlarmFilter alarm_filter;
  gpointer alarm_filter_data;

  int composite_event_base;
  int composite_error_base;
  int composite_major_version;
  int composite_minor_version;
  int damage_event_base;
  int damage_error_base;
  int xfixes_event_base;
  int xfixes_error_base;
  int xinput_error_base;
  int xinput_event_base;
  int xinput_opcode;

  CobiwmStartupNotification *startup_notification;

  int xsync_event_base;
  int xsync_error_base;
  int shape_event_base;
  int shape_error_base;
  unsigned int have_xsync : 1;
#define COBIWM_DISPLAY_HAS_XSYNC(display) ((display)->have_xsync)
  unsigned int have_shape : 1;
#define COBIWM_DISPLAY_HAS_SHAPE(display) ((display)->have_shape)
  unsigned int have_composite : 1;
  unsigned int have_damage : 1;
#define COBIWM_DISPLAY_HAS_COMPOSITE(display) ((display)->have_composite)
#define COBIWM_DISPLAY_HAS_DAMAGE(display) ((display)->have_damage)
#ifdef HAVE_XI23
  gboolean have_xinput_23 : 1;
#define COBIWM_DISPLAY_HAS_XINPUT_23(display) ((display)->have_xinput_23)
#else
#define COBIWM_DISPLAY_HAS_XINPUT_23(display) FALSE
#endif /* HAVE_XI23 */
};

struct _CobiwmDisplayClass
{
  GObjectClass parent_class;
};

#define XSERVER_TIME_IS_BEFORE_ASSUMING_REAL_TIMESTAMPS(time1, time2) \
  ( (( (time1) < (time2) ) && ( (time2) - (time1) < ((guint32)-1)/2 )) ||     \
    (( (time1) > (time2) ) && ( (time1) - (time2) > ((guint32)-1)/2 ))        \
  )
/**
 * XSERVER_TIME_IS_BEFORE:
 *
 * See the docs for cobiwm_display_xserver_time_is_before().
 */
#define XSERVER_TIME_IS_BEFORE(time1, time2)                          \
  ( (time1) == 0 ||                                                     \
    (XSERVER_TIME_IS_BEFORE_ASSUMING_REAL_TIMESTAMPS(time1, time2) && \
     (time2) != 0)                                                      \
  )

gboolean      cobiwm_display_open                (void);
void          cobiwm_display_close               (CobiwmDisplay *display,
                                                guint32      timestamp);

void          cobiwm_display_unmanage_windows_for_screen (CobiwmDisplay *display,
                                                        CobiwmScreen  *screen,
                                                        guint32      timestamp);

/* Utility function to compare the stacking of two windows */
int           cobiwm_display_stack_cmp           (const void *a,
                                                const void *b);

/* A given CobiwmWindow may have various X windows that "belong"
 * to it, such as the frame window.
 */
CobiwmWindow* cobiwm_display_lookup_x_window     (CobiwmDisplay *display,
                                              Window       xwindow);
void        cobiwm_display_register_x_window   (CobiwmDisplay *display,
                                              Window      *xwindowp,
                                              CobiwmWindow  *window);
void        cobiwm_display_unregister_x_window (CobiwmDisplay *display,
                                              Window       xwindow);

/* Each CobiwmWindow is uniquely identified by a 64-bit "stamp"; unlike a
 * a CobiwmWindow *, a stamp will never be recycled
 */
CobiwmWindow* cobiwm_display_lookup_stamp     (CobiwmDisplay *display,
                                           guint64      stamp);
void        cobiwm_display_register_stamp   (CobiwmDisplay *display,
                                           guint64     *stampp,
                                           CobiwmWindow  *window);
void        cobiwm_display_unregister_stamp (CobiwmDisplay *display,
                                           guint64      stamp);

/* A "stack id" is a XID or a stamp */

#define COBIWM_STACK_ID_IS_X11(id) ((id) < G_GUINT64_CONSTANT(0x100000000))
CobiwmWindow* cobiwm_display_lookup_stack_id   (CobiwmDisplay *display,
                                            guint64      stack_id);

/* for debug logging only; returns a human-description of the stack
 * ID - a small number of buffers are recycled, so the result must
 * be used immediately or copied */
const char *cobiwm_display_describe_stack_id (CobiwmDisplay *display,
                                            guint64      stack_id);

void        cobiwm_display_register_wayland_window   (CobiwmDisplay *display,
                                                    CobiwmWindow  *window);
void        cobiwm_display_unregister_wayland_window (CobiwmDisplay *display,
                                                    CobiwmWindow  *window);

CobiwmWindow* cobiwm_display_lookup_sync_alarm     (CobiwmDisplay *display,
                                                XSyncAlarm   alarm);
void        cobiwm_display_register_sync_alarm   (CobiwmDisplay *display,
                                                XSyncAlarm  *alarmp,
                                                CobiwmWindow  *window);
void        cobiwm_display_unregister_sync_alarm (CobiwmDisplay *display,
                                                XSyncAlarm   alarm);

void        cobiwm_display_notify_window_created (CobiwmDisplay  *display,
                                                CobiwmWindow   *window);

GSList*     cobiwm_display_list_windows        (CobiwmDisplay          *display,
                                              CobiwmListWindowsFlags  flags);

CobiwmDisplay* cobiwm_display_for_x_display  (Display     *xdisplay);
CobiwmDisplay* cobiwm_get_display            (void);

Cursor         cobiwm_display_create_x_cursor (CobiwmDisplay *display,
                                             CobiwmCursor   cursor);

void     cobiwm_display_update_cursor (CobiwmDisplay *display);

void    cobiwm_display_check_threshold_reached (CobiwmDisplay *display,
                                              int          x,
                                              int          y);
void     cobiwm_display_grab_window_buttons    (CobiwmDisplay *display,
                                              Window       xwindow);
void     cobiwm_display_ungrab_window_buttons  (CobiwmDisplay *display,
                                              Window       xwindow);

void cobiwm_display_grab_focus_window_button   (CobiwmDisplay *display,
                                              CobiwmWindow  *window);
void cobiwm_display_ungrab_focus_window_button (CobiwmDisplay *display,
                                              CobiwmWindow  *window);

/* Next function is defined in edge-resistance.c */
void cobiwm_display_cleanup_edges              (CobiwmDisplay *display);

/* make a request to ensure the event serial has changed */
void     cobiwm_display_increment_event_serial (CobiwmDisplay *display);

void     cobiwm_display_update_active_window_hint (CobiwmDisplay *display);

/* utility goo */
const char* cobiwm_event_mode_to_string   (int m);
const char* cobiwm_event_detail_to_string (int d);

void cobiwm_display_queue_retheme_all_windows (CobiwmDisplay *display);
void cobiwm_display_retheme_all (void);

void cobiwm_display_ping_window      (CobiwmWindow  *window,
                                    guint32      serial);
void cobiwm_display_pong_for_serial  (CobiwmDisplay *display,
                                    guint32      serial);

int cobiwm_resize_gravity_from_grab_op (CobiwmGrabOp op);

gboolean cobiwm_grab_op_is_moving   (CobiwmGrabOp op);
gboolean cobiwm_grab_op_is_resizing (CobiwmGrabOp op);
gboolean cobiwm_grab_op_is_mouse    (CobiwmGrabOp op);
gboolean cobiwm_grab_op_is_keyboard (CobiwmGrabOp op);

void cobiwm_display_increment_focus_sentinel (CobiwmDisplay *display);
void cobiwm_display_decrement_focus_sentinel (CobiwmDisplay *display);
gboolean cobiwm_display_focus_sentinel_clear (CobiwmDisplay *display);

void cobiwm_display_queue_autoraise_callback  (CobiwmDisplay *display,
                                             CobiwmWindow  *window);
void cobiwm_display_remove_autoraise_callback (CobiwmDisplay *display);

void cobiwm_display_overlay_key_activate (CobiwmDisplay *display);
void cobiwm_display_accelerator_activate (CobiwmDisplay     *display,
                                        guint            action,
                                        ClutterKeyEvent *event);
gboolean cobiwm_display_modifiers_accelerator_activate (CobiwmDisplay *display);

#ifdef HAVE_XI23
gboolean cobiwm_display_process_barrier_xevent (CobiwmDisplay *display,
                                              XIEvent     *event);
#endif /* HAVE_XI23 */

void cobiwm_display_set_input_focus_xwindow (CobiwmDisplay *display,
                                           CobiwmScreen  *screen,
                                           Window       window,
                                           guint32      timestamp);

void cobiwm_display_sync_wayland_input_focus (CobiwmDisplay *display);
void cobiwm_display_update_focus_window (CobiwmDisplay *display,
                                       CobiwmWindow  *window,
                                       Window       xwindow,
                                       gulong       serial,
                                       gboolean     focused_by_us);

void cobiwm_display_sanity_check_timestamps (CobiwmDisplay *display,
                                           guint32      timestamp);
gboolean cobiwm_display_timestamp_too_old (CobiwmDisplay *display,
                                         guint32     *timestamp);

void cobiwm_display_remove_pending_pings_for_window (CobiwmDisplay *display,
                                                   CobiwmWindow  *window);

CobiwmGestureTracker * cobiwm_display_get_gesture_tracker (CobiwmDisplay *display);

gboolean cobiwm_display_show_restart_message (CobiwmDisplay *display,
                                            const char  *message);
gboolean cobiwm_display_request_restart      (CobiwmDisplay *display);

gboolean cobiwm_display_show_resize_popup (CobiwmDisplay *display,
                                         gboolean show,
                                         CobiwmRectangle *rect,
                                         int display_w,
                                         int display_h);

void cobiwm_restart_init (void);
void cobiwm_restart_finish (void);

void cobiwm_display_cancel_touch (CobiwmDisplay *display);

gboolean cobiwm_display_windows_are_interactable (CobiwmDisplay *display);

void cobiwm_display_set_alarm_filter (CobiwmDisplay    *display,
                                    CobiwmAlarmFilter filter,
                                    gpointer        data);

#endif
