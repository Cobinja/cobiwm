/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2001 Havoc Pennington
 * Copyright (C) 2002, 2003, 2004 Red Hat, Inc.
 * Copyright (C) 2003, 2004 Rob Adams
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
 * SECTION:display
 * @title: CobiwmDisplay
 * @short_description: Cobiwm X display handler
 *
 * The display is represented as a #CobiwmDisplay struct.
 */

#define _XOPEN_SOURCE 600 /* for gethostname() */

#include <config.h>
#include "display-private.h"
#include "events.h"
#include "util-private.h"
#include <main.h>
#include "screen-private.h"
#include "window-private.h"
#include "frame.h"
#include <errors.h>
#include "keybindings-private.h"
#include <prefs.h>
#include "workspace-private.h"
#include "bell.h"
#include <compositor.h>
#include <compositor-cobiwm.h>
#include <X11/Xatom.h>
#include <cobiwm-enum-types.h>
#include "cobiwm-idle-monitor-dbus.h"
#include "cobiwm-cursor-tracker-private.h"
#include <cobiwm-backend.h>
#include "backends/native/cobiwm-backend-native.h"
#include "backends/x11/cobiwm-backend-x11.h"
#include "backends/cobiwm-stage.h"
#include <clutter/x11/clutter-x11.h>

#ifdef HAVE_RANDR
#include <X11/extensions/Xrandr.h>
#endif
#include <X11/extensions/shape.h>
#include <X11/Xcursor/Xcursor.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/Xfixes.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "x11/events.h"
#include "x11/window-x11.h"
#include "x11/window-props.h"
#include "x11/group-props.h"
#include "x11/xprops.h"

#ifdef HAVE_WAYLAND
#include "wayland/cobiwm-xwayland-private.h"
#endif

/*
 * SECTION:pings
 *
 * Sometimes we want to see whether a window is responding,
 * so we send it a "ping" message and see whether it sends us back a "pong"
 * message within a reasonable time. Here we have a system which lets us
 * nominate one function to be called if we get the pong in time and another
 * function if we don't. The system is rather more complicated than it needs
 * to be, since we only ever use it to destroy windows which are asked to
 * close themselves and don't do so within a reasonable amount of time, and
 * therefore we always use the same callbacks. It's possible that we might
 * use it for other things in future, or on the other hand we might decide
 * that we're never going to do so and simplify it a bit.
 */

/**
 * CobiwmPingData:
 *
 * Describes a ping on a window. When we send a ping to a window, we build
 * one of these structs, and it eventually gets passed to the timeout function
 * or to the function which handles the response from the window. If the window
 * does or doesn't respond to the ping, we use this information to deal with
 * these facts; we have a handler function for each.
 */
typedef struct
{
  CobiwmWindow *window;
  guint32     serial;
  guint       ping_timeout_id;
} CobiwmPingData;

G_DEFINE_TYPE(CobiwmDisplay, cobiwm_display, G_TYPE_OBJECT);

/* Signals */
enum
{
  OVERLAY_KEY,
  ACCELERATOR_ACTIVATED,
  MODIFIERS_ACCELERATOR_ACTIVATED,
  FOCUS_WINDOW,
  WINDOW_CREATED,
  WINDOW_DEMANDS_ATTENTION,
  WINDOW_MARKED_URGENT,
  GRAB_OP_BEGIN,
  GRAB_OP_END,
  SHOW_RESTART_MESSAGE,
  RESTART,
  SHOW_RESIZE_POPUP,
  LAST_SIGNAL
};

enum {
  PROP_0,

  PROP_FOCUS_WINDOW
};

static guint display_signals [LAST_SIGNAL] = { 0 };

/*
 * The display we're managing.  This is a singleton object.  (Historically,
 * this was a list of displays, but there was never any way to add more
 * than one element to it.)  The goofy name is because we don't want it
 * to shadow the parameter in its object methods.
 */
static CobiwmDisplay *the_display = NULL;


static const char *gnome_wm_keybindings = "Cobiwm";
static const char *net_wm_name = "Cobiwm";

static void update_cursor_theme (void);

static void    prefs_changed_callback    (CobiwmPreference pref,
                                          void          *data);

static int mru_cmp (gconstpointer a,
                    gconstpointer b);

static void
cobiwm_display_get_property(GObject         *object,
                          guint            prop_id,
                          GValue          *value,
                          GParamSpec      *pspec)
{
  CobiwmDisplay *display = COBIWM_DISPLAY (object);

  switch (prop_id)
    {
    case PROP_FOCUS_WINDOW:
      g_value_set_object (value, display->focus_window);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
cobiwm_display_set_property(GObject         *object,
                          guint            prop_id,
                          const GValue    *value,
                          GParamSpec      *pspec)
{
  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
cobiwm_display_class_init (CobiwmDisplayClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = cobiwm_display_get_property;
  object_class->set_property = cobiwm_display_set_property;

  display_signals[OVERLAY_KEY] =
    g_signal_new ("overlay-key",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  display_signals[ACCELERATOR_ACTIVATED] =
    g_signal_new ("accelerator-activated",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT);

  /**
   * CobiwmDisplay::modifiers-accelerator-activated:
   * @display: the #CobiwmDisplay instance
   *
   * The ::modifiers-accelerator-activated signal will be emitted when
   * a special modifiers-only keybinding is activated.
   *
   * Returns: %TRUE means that the keyboard device should remain
   *    frozen and %FALSE for the default behavior of unfreezing the
   *    keyboard.
   */
  display_signals[MODIFIERS_ACCELERATOR_ACTIVATED] =
    g_signal_new ("modifiers-accelerator-activated",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  g_signal_accumulator_first_wins, NULL, NULL,
                  G_TYPE_BOOLEAN, 0);

  display_signals[WINDOW_CREATED] =
    g_signal_new ("window-created",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1, COBIWM_TYPE_WINDOW);

  display_signals[WINDOW_DEMANDS_ATTENTION] =
    g_signal_new ("window-demands-attention",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1, COBIWM_TYPE_WINDOW);

  display_signals[WINDOW_MARKED_URGENT] =
    g_signal_new ("window-marked-urgent",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1,
                  COBIWM_TYPE_WINDOW);

  display_signals[GRAB_OP_BEGIN] =
    g_signal_new ("grab-op-begin",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 3,
                  COBIWM_TYPE_SCREEN,
                  COBIWM_TYPE_WINDOW,
                  COBIWM_TYPE_GRAB_OP);

  display_signals[GRAB_OP_END] =
    g_signal_new ("grab-op-end",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 3,
                  COBIWM_TYPE_SCREEN,
                  COBIWM_TYPE_WINDOW,
                  COBIWM_TYPE_GRAB_OP);

  /**
   * CobiwmDisplay::show-restart-message:
   * @display: the #CobiwmDisplay instance
   * @message: (allow-none): The message to display, or %NULL
   *  to clear a previous restart message.
   *
   * The ::show-restart-message signal will be emitted to indicate
   * that the compositor should show a message during restart. This is
   * emitted when cobiwm_restart() is called, either by Cobiwm
   * internally or by the embedding compositor.  The message should be
   * immediately added to the Clutter stage in its final form -
   * ::restart will be emitted to exit the application and leave the
   * stage contents frozen as soon as the the stage is painted again.
   *
   * On case of failure to restart, this signal will be emitted again
   * with %NULL for @message.
   *
   * Returns: %TRUE means the message was added to the stage; %FALSE
   *   indicates that the compositor did not show the message.
   */
  display_signals[SHOW_RESTART_MESSAGE] =
    g_signal_new ("show-restart-message",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  g_signal_accumulator_true_handled,
                  NULL, NULL,
                  G_TYPE_BOOLEAN, 1,
                  G_TYPE_STRING);

  /**
   * CobiwmDisplay::restart:
   * @display: the #CobiwmDisplay instance
   *
   * The ::restart signal is emitted to indicate that compositor
   * should reexec the process. This is
   * emitted when cobiwm_restart() is called, either by Cobiwm
   * internally or by the embedding compositor. See also
   * ::show-restart-message.
   *
   * Returns: %FALSE to indicate that the compositor could not
   *  be restarted. When the compositor is restarted, the signal
   *  should not return.
   */
  display_signals[RESTART] =
    g_signal_new ("restart",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  g_signal_accumulator_true_handled,
                  NULL, NULL,
                  G_TYPE_BOOLEAN, 0);

  display_signals[SHOW_RESIZE_POPUP] =
    g_signal_new ("show-resize-popup",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  g_signal_accumulator_true_handled,
                  NULL, NULL,
                  G_TYPE_BOOLEAN, 4,
                  G_TYPE_BOOLEAN, COBIWM_TYPE_RECTANGLE, G_TYPE_INT, G_TYPE_INT);

  g_object_class_install_property (object_class,
                                   PROP_FOCUS_WINDOW,
                                   g_param_spec_object ("focus-window",
                                                        "Focus window",
                                                        "Currently focused window",
                                                        COBIWM_TYPE_WINDOW,
                                                        G_PARAM_READABLE));
}


/**
 * ping_data_free:
 *
 * Destructor for #CobiwmPingData structs. Will destroy the
 * event source for the struct as well.
 */
static void
ping_data_free (CobiwmPingData *ping_data)
{
  /* Remove the timeout */
  if (ping_data->ping_timeout_id != 0)
    g_source_remove (ping_data->ping_timeout_id);

  g_free (ping_data);
}

void
cobiwm_display_remove_pending_pings_for_window (CobiwmDisplay *display,
                                              CobiwmWindow  *window)
{
  GSList *tmp;
  GSList *dead;

  /* could obviously be more efficient, don't care */

  /* build list to be removed */
  dead = NULL;
  for (tmp = display->pending_pings; tmp; tmp = tmp->next)
    {
      CobiwmPingData *ping_data = tmp->data;

      if (ping_data->window == window)
        dead = g_slist_prepend (dead, ping_data);
    }

  /* remove what we found */
  for (tmp = dead; tmp; tmp = tmp->next)
    {
      CobiwmPingData *ping_data = tmp->data;

      display->pending_pings = g_slist_remove (display->pending_pings, ping_data);
      ping_data_free (ping_data);
    }

  g_slist_free (dead);
}


static void
enable_compositor (CobiwmDisplay *display)
{
  if (!COBIWM_DISPLAY_HAS_COMPOSITE (display) ||
      !COBIWM_DISPLAY_HAS_DAMAGE (display))
    {
      cobiwm_warning ("Missing %s extension required for compositing",
                    !COBIWM_DISPLAY_HAS_COMPOSITE (display) ? "composite" : "damage");
      return;
    }

  int version = (display->composite_major_version * 10) + display->composite_minor_version;
  if (version < 3)
    {
      cobiwm_warning ("Your version of COMPOSITE is too old.");
      return;
    }

  if (!display->compositor)
      display->compositor = cobiwm_compositor_new (display);

  cobiwm_compositor_manage (display->compositor);
}

static void
cobiwm_display_init (CobiwmDisplay *disp)
{
  /* Some stuff could go in here that's currently in _open,
   * but it doesn't really matter. */
}

/**
 * cobiwm_set_wm_name: (skip)
 * @wm_name: value for _NET_WM_NAME
 *
 * Set the value to use for the _NET_WM_NAME property. To take effect,
 * it is necessary to call this function before cobiwm_init().
 */
void
cobiwm_set_wm_name (const char *wm_name)
{
  g_return_if_fail (the_display == NULL);

  net_wm_name = wm_name;
}

/**
 * cobiwm_set_gnome_wm_keybindings: (skip)
 * @wm_keybindings: value for _GNOME_WM_KEYBINDINGS
 *
 * Set the value to use for the _GNOME_WM_KEYBINDINGS property. To take
 * effect, it is necessary to call this function before cobiwm_init().
 */
void
cobiwm_set_gnome_wm_keybindings (const char *wm_keybindings)
{
  g_return_if_fail (the_display == NULL);

  gnome_wm_keybindings = wm_keybindings;
}

void
cobiwm_display_cancel_touch (CobiwmDisplay *display)
{
#ifdef HAVE_WAYLAND
  CobiwmWaylandCompositor *compositor;

  if (!cobiwm_is_wayland_compositor ())
    return;

  compositor = cobiwm_wayland_compositor_get_default ();
  cobiwm_wayland_touch_cancel (&compositor->seat->touch);
#endif
}

static void
gesture_tracker_state_changed (CobiwmGestureTracker   *tracker,
                               ClutterEventSequence *sequence,
                               CobiwmSequenceState     state,
                               CobiwmDisplay          *display)
{
  if (cobiwm_is_wayland_compositor ())
    {
      if (state == COBIWM_SEQUENCE_ACCEPTED)
        cobiwm_display_cancel_touch (display);
    }
  else
    {
      CobiwmBackendX11 *backend = COBIWM_BACKEND_X11 (cobiwm_get_backend ());
      int event_mode;

      if (state == COBIWM_SEQUENCE_ACCEPTED)
        event_mode = XIAcceptTouch;
      else if (state == COBIWM_SEQUENCE_REJECTED)
        event_mode = XIRejectTouch;
      else
        return;

      XIAllowTouchEvents (cobiwm_backend_x11_get_xdisplay (backend),
                          COBIWM_VIRTUAL_CORE_POINTER_ID,
                          clutter_x11_event_sequence_get_touch_detail (sequence),
                          DefaultRootWindow (display->xdisplay), event_mode);
    }
}

static void
on_startup_notification_changed (CobiwmStartupNotification *sn,
                                 gpointer                 sequence,
                                 CobiwmDisplay             *display)
{
  if (!display->screen)
    return;

  g_slist_free (display->screen->startup_sequences);
  display->screen->startup_sequences =
    cobiwm_startup_notification_get_sequences (display->startup_notification);
  g_signal_emit_by_name (display->screen, "startup-sequence-changed", sequence);
}

/**
 * cobiwm_display_open:
 *
 * Opens a new display, sets it up, initialises all the X extensions
 * we will need, and adds it to the list of displays.
 *
 * Returns: %TRUE if the display was opened successfully, and %FALSE
 * otherwise-- that is, if the display doesn't exist or it already
 * has a window manager.
 */
gboolean
cobiwm_display_open (void)
{
  CobiwmDisplay *display;
  Display *xdisplay;
  CobiwmScreen *screen;
  int i;
  guint32 timestamp;

  /* A list of all atom names, so that we can intern them in one go. */
  const char *atom_names[] = {
#define item(x) #x,
#include <x11/atomnames.h>
#undef item
  };
  Atom atoms[G_N_ELEMENTS(atom_names)];

  cobiwm_verbose ("Opening display '%s'\n", XDisplayName (NULL));

  xdisplay = cobiwm_ui_get_display ();

  if (xdisplay == NULL)
    {
      cobiwm_warning (_("Failed to open X Window System display '%s'\n"),
		    XDisplayName (NULL));
      return FALSE;
    }

#ifdef HAVE_WAYLAND
  if (cobiwm_is_wayland_compositor ())
    cobiwm_xwayland_complete_init ();
#endif

  if (cobiwm_is_syncing ())
    XSynchronize (xdisplay, True);

  g_assert (the_display == NULL);
  display = the_display = g_object_new (COBIWM_TYPE_DISPLAY, NULL);

  display->closing = 0;

  /* here we use XDisplayName which is what the user
   * probably put in, vs. DisplayString(display) which is
   * canonicalized by XOpenDisplay()
   */
  display->name = g_strdup (XDisplayName (NULL));
  display->xdisplay = xdisplay;
  display->display_opening = TRUE;

  display->pending_pings = NULL;
  display->autoraise_timeout_id = 0;
  display->autoraise_window = NULL;
  display->focus_window = NULL;
  display->focus_serial = 0;
  display->server_focus_window = None;
  display->server_focus_serial = 0;

  display->mouse_mode = TRUE; /* Only relevant for mouse or sloppy focus */
  display->allow_terminal_deactivation = TRUE; /* Only relevant for when a
                                                  terminal has the focus */

  cobiwm_bell_init (display);

  cobiwm_display_init_keys (display);

  cobiwm_prefs_add_listener (prefs_changed_callback, display);

  cobiwm_verbose ("Creating %d atoms\n", (int) G_N_ELEMENTS (atom_names));
  XInternAtoms (display->xdisplay, (char **)atom_names, G_N_ELEMENTS (atom_names),
                False, atoms);

  i = 0;
#define item(x) display->atom_##x = atoms[i++];
#include <x11/atomnames.h>
#undef item

  display->prop_hooks = NULL;
  cobiwm_display_init_window_prop_hooks (display);
  display->group_prop_hooks = NULL;
  cobiwm_display_init_group_prop_hooks (display);

  /* Offscreen unmapped window used for _NET_SUPPORTING_WM_CHECK,
   * created in screen_new
   */
  display->leader_window = None;
  display->timestamp_pinging_window = None;

  display->monitor_cache_invalidated = TRUE;

  display->groups_by_leader = NULL;

  display->screen = NULL;

  /* Get events */
  cobiwm_display_init_events (display);
  cobiwm_display_init_events_x11 (display);

  display->xids = g_hash_table_new (cobiwm_unsigned_long_hash,
                                        cobiwm_unsigned_long_equal);
  display->stamps = g_hash_table_new (g_int64_hash,
                                      g_int64_equal);
  display->wayland_windows = g_hash_table_new (NULL, NULL);

  i = 0;
  while (i < N_IGNORED_CROSSING_SERIALS)
    {
      display->ignored_crossing_serials[i] = 0;
      ++i;
    }

  display->current_time = CurrentTime;
  display->sentinel_counter = 0;

  display->grab_resize_timeout_id = 0;
  display->grab_have_keyboard = FALSE;

  display->last_bell_time = 0;

  display->grab_op = COBIWM_GRAB_OP_NONE;
  display->grab_window = NULL;
  display->grab_tile_mode = COBIWM_TILE_NONE;
  display->grab_tile_monitor_number = -1;

  display->grab_edge_resistance_data = NULL;

  {
    int major, minor;

    display->have_xsync = FALSE;

    display->xsync_error_base = 0;
    display->xsync_event_base = 0;

    /* I don't think we really have to fill these in */
    major = SYNC_MAJOR_VERSION;
    minor = SYNC_MINOR_VERSION;

    if (!XSyncQueryExtension (display->xdisplay,
                              &display->xsync_event_base,
                              &display->xsync_error_base) ||
        !XSyncInitialize (display->xdisplay,
                          &major, &minor))
      {
        display->xsync_error_base = 0;
        display->xsync_event_base = 0;
      }
    else
      {
        display->have_xsync = TRUE;
        XSyncSetPriority (display->xdisplay, None, 10);
      }

    cobiwm_verbose ("Attempted to init Xsync, found version %d.%d error base %d event base %d\n",
                  major, minor,
                  display->xsync_error_base,
                  display->xsync_event_base);
  }

  {
    display->have_shape = FALSE;

    display->shape_error_base = 0;
    display->shape_event_base = 0;

    if (!XShapeQueryExtension (display->xdisplay,
                               &display->shape_event_base,
                               &display->shape_error_base))
      {
        display->shape_error_base = 0;
        display->shape_event_base = 0;
      }
    else
      display->have_shape = TRUE;

    cobiwm_verbose ("Attempted to init Shape, found error base %d event base %d\n",
                  display->shape_error_base,
                  display->shape_event_base);
  }

  {
    display->have_composite = FALSE;

    display->composite_error_base = 0;
    display->composite_event_base = 0;

    if (!XCompositeQueryExtension (display->xdisplay,
                                   &display->composite_event_base,
                                   &display->composite_error_base))
      {
        display->composite_error_base = 0;
        display->composite_event_base = 0;
      }
    else
      {
        display->composite_major_version = 0;
        display->composite_minor_version = 0;
        if (XCompositeQueryVersion (display->xdisplay,
                                    &display->composite_major_version,
                                    &display->composite_minor_version))
          {
            display->have_composite = TRUE;
          }
        else
          {
            display->composite_major_version = 0;
            display->composite_minor_version = 0;
          }
      }

    cobiwm_verbose ("Attempted to init Composite, found error base %d event base %d "
                  "extn ver %d %d\n",
                  display->composite_error_base,
                  display->composite_event_base,
                  display->composite_major_version,
                  display->composite_minor_version);

    display->have_damage = FALSE;

    display->damage_error_base = 0;
    display->damage_event_base = 0;

    if (!XDamageQueryExtension (display->xdisplay,
                                &display->damage_event_base,
                                &display->damage_error_base))
      {
        display->damage_error_base = 0;
        display->damage_event_base = 0;
      }
    else
      display->have_damage = TRUE;

    cobiwm_verbose ("Attempted to init Damage, found error base %d event base %d\n",
                  display->damage_error_base,
                  display->damage_event_base);

    display->xfixes_error_base = 0;
    display->xfixes_event_base = 0;

    if (XFixesQueryExtension (display->xdisplay,
                              &display->xfixes_event_base,
                              &display->xfixes_error_base))
      {
        int xfixes_major, xfixes_minor;

        XFixesQueryVersion (display->xdisplay, &xfixes_major, &xfixes_minor);

        if (xfixes_major * 100 + xfixes_minor < 500)
          cobiwm_fatal ("Cobiwm requires XFixes 5.0");
      }
    else
      {
        cobiwm_fatal ("Cobiwm requires XFixes 5.0");
      }

    cobiwm_verbose ("Attempted to init XFixes, found error base %d event base %d\n",
                  display->xfixes_error_base,
                  display->xfixes_event_base);
  }

  {
    int major = 2, minor = 3;
    gboolean has_xi = FALSE;

    if (XQueryExtension (display->xdisplay,
                         "XInputExtension",
                         &display->xinput_opcode,
                         &display->xinput_error_base,
                         &display->xinput_event_base))
      {
        if (XIQueryVersion (display->xdisplay, &major, &minor) == Success)
          {
            int version = (major * 10) + minor;
            if (version >= 22)
              has_xi = TRUE;

#ifdef HAVE_XI23
            if (version >= 23)
              display->have_xinput_23 = TRUE;
#endif /* HAVE_XI23 */
          }
      }

    if (!has_xi)
      cobiwm_fatal ("X server doesn't have the XInput extension, version 2.2 or newer\n");
  }

  update_cursor_theme ();

  /* Create the leader window here. Set its properties and
   * use the timestamp from one of the PropertyNotify events
   * that will follow.
   */
  {
    gulong data[1];
    XEvent event;

    /* We only care about the PropertyChangeMask in the next 30 or so lines of
     * code.  Note that gdk will at some point unset the PropertyChangeMask for
     * this window, so we can't rely on it still being set later.  See bug
     * 354213 for details.
     */
    display->leader_window =
      cobiwm_create_offscreen_window (display->xdisplay,
                                    DefaultRootWindow (display->xdisplay),
                                    PropertyChangeMask);

    cobiwm_prop_set_utf8_string_hint (display,
                                    display->leader_window,
                                    display->atom__NET_WM_NAME,
                                    net_wm_name);

    cobiwm_prop_set_utf8_string_hint (display,
                                    display->leader_window,
                                    display->atom__GNOME_WM_KEYBINDINGS,
                                    gnome_wm_keybindings);

    cobiwm_prop_set_utf8_string_hint (display,
                                    display->leader_window,
                                    display->atom__COBIWM_VERSION,
                                    VERSION);

    data[0] = display->leader_window;
    XChangeProperty (display->xdisplay,
                     display->leader_window,
                     display->atom__NET_SUPPORTING_WM_CHECK,
                     XA_WINDOW,
                     32, PropModeReplace, (guchar*) data, 1);

    XWindowEvent (display->xdisplay,
                  display->leader_window,
                  PropertyChangeMask,
                  &event);

    timestamp = event.xproperty.time;

    /* Make it painfully clear that we can't rely on PropertyNotify events on
     * this window, as per bug 354213.
     */
    XSelectInput(display->xdisplay,
                 display->leader_window,
                 NoEventMask);
  }

  /* Make a little window used only for pinging the server for timestamps; note
   * that cobiwm_create_offscreen_window already selects for PropertyChangeMask.
   */
  display->timestamp_pinging_window =
    cobiwm_create_offscreen_window (display->xdisplay,
                                  DefaultRootWindow (display->xdisplay),
                                  PropertyChangeMask);

  display->last_focus_time = timestamp;
  display->last_user_time = timestamp;
  display->compositor = NULL;

  /* Cobiwm used to manage all X screens of the display in a single process, but
   * now it always manages exactly one screen as specified by the DISPLAY
   * environment variable.
   */
  screen = cobiwm_screen_new (display, 0, timestamp);

  if (!screen)
    {
      /* This would typically happen because all the screens already
       * have window managers.
       */
      cobiwm_display_close (display, timestamp);
      return FALSE;
    }

  display->screen = screen;

  display->startup_notification = cobiwm_startup_notification_get (display);
  g_signal_connect (display->startup_notification, "changed",
                    G_CALLBACK (on_startup_notification_changed), display);

  cobiwm_screen_init_workspaces (screen);

  enable_compositor (display);

  cobiwm_screen_create_guard_window (screen);

  /* Set up touch support */
  display->gesture_tracker = cobiwm_gesture_tracker_new ();
  g_signal_connect (display->gesture_tracker, "state-changed",
                    G_CALLBACK (gesture_tracker_state_changed), display);

  /* We know that if cobiwm is running as a Wayland compositor,
   * we start out with no windows.
   */
  if (!cobiwm_is_wayland_compositor ())
    cobiwm_screen_manage_all_windows (screen);

  {
    Window focus;
    int ret_to;

    /* kinda bogus because GetInputFocus has no possible errors */
    cobiwm_error_trap_push (display);

    /* FIXME: This is totally broken; see comment 9 of bug 88194 about this */
    focus = None;
    ret_to = RevertToPointerRoot;
    XGetInputFocus (display->xdisplay, &focus, &ret_to);

    /* Force a new FocusIn (does this work?) */

    /* Use the same timestamp that was passed to cobiwm_screen_new(),
     * as it is the most recent timestamp.
     */
    if (focus == None || focus == PointerRoot)
      /* Just focus the no_focus_window on the first screen */
      cobiwm_display_focus_the_no_focus_window (display,
                                              display->screen,
                                              timestamp);
    else
      {
        CobiwmWindow * window;
        window  = cobiwm_display_lookup_x_window (display, focus);
        if (window)
          cobiwm_display_set_input_focus_window (display, window, FALSE, timestamp);
        else
          /* Just focus the no_focus_window on the first screen */
          cobiwm_display_focus_the_no_focus_window (display,
                                                  display->screen,
                                                  timestamp);
      }

    cobiwm_error_trap_pop (display);
  }

  cobiwm_idle_monitor_init_dbus ();

  /* Done opening new display */
  display->display_opening = FALSE;

  return TRUE;
}

static gint
ptrcmp (gconstpointer a, gconstpointer b)
{
  if (a < b)
    return -1;
  else if (a > b)
    return 1;
  else
    return 0;
}

/**
 * cobiwm_display_list_windows:
 * @display: a #CobiwmDisplay
 * @flags: options for listing
 *
 * Lists windows for the display, the @flags parameter for
 * now determines whether override-redirect windows will be
 * included.
 *
 * Return value: (transfer container): the list of windows.
 */
GSList*
cobiwm_display_list_windows (CobiwmDisplay          *display,
                           CobiwmListWindowsFlags  flags)
{
  GSList *winlist;
  GSList *prev;
  GSList *tmp;
  GHashTableIter iter;
  gpointer key, value;

  winlist = NULL;

  g_hash_table_iter_init (&iter, display->xids);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      CobiwmWindow *window = value;

      if (!COBIWM_IS_WINDOW (window) || window->unmanaging)
        continue;

      if (!window->override_redirect ||
          (flags & COBIWM_LIST_INCLUDE_OVERRIDE_REDIRECT) != 0)
        winlist = g_slist_prepend (winlist, window);
    }

  g_hash_table_iter_init (&iter, display->wayland_windows);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      CobiwmWindow *window = value;

      if (!COBIWM_IS_WINDOW (window) || window->unmanaging)
        continue;

      if (!window->override_redirect ||
          (flags & COBIWM_LIST_INCLUDE_OVERRIDE_REDIRECT) != 0)
        winlist = g_slist_prepend (winlist, window);
    }

  /* Uniquify the list, since both frame windows and plain
   * windows are in the hash
   */
  winlist = g_slist_sort (winlist, ptrcmp);

  prev = NULL;
  tmp = winlist;
  while (tmp != NULL)
    {
      GSList *next;

      next = tmp->next;

      if (next &&
          next->data == tmp->data)
        {
          /* Delete tmp from list */

          if (prev)
            prev->next = next;

          if (tmp == winlist)
            winlist = next;

          g_slist_free_1 (tmp);

          /* leave prev unchanged */
        }
      else
        {
          prev = tmp;
        }

      tmp = next;
    }

  if (flags & COBIWM_LIST_SORTED)
    winlist = g_slist_sort (winlist, mru_cmp);

  return winlist;
}

void
cobiwm_display_close (CobiwmDisplay *display,
                    guint32      timestamp)
{
  g_assert (display != NULL);
  g_assert (display == the_display);

  if (display->closing != 0)
    {
      /* The display's already been closed. */
      return;
    }

  display->closing += 1;

  cobiwm_prefs_remove_listener (prefs_changed_callback, display);

  cobiwm_display_remove_autoraise_callback (display);

  g_clear_object (&display->startup_notification);
  g_clear_object (&display->gesture_tracker);

  if (display->focus_timeout_id)
    g_source_remove (display->focus_timeout_id);
  display->focus_timeout_id = 0;

  /* Stop caring about events */
  cobiwm_display_free_events_x11 (display);
  cobiwm_display_free_events (display);

  cobiwm_screen_free (display->screen, timestamp);

  /* Must be after all calls to cobiwm_window_unmanage() since they
   * unregister windows
   */
  g_hash_table_destroy (display->xids);
  g_hash_table_destroy (display->wayland_windows);

  if (display->leader_window != None)
    XDestroyWindow (display->xdisplay, display->leader_window);

  XFlush (display->xdisplay);

  cobiwm_display_free_window_prop_hooks (display);
  cobiwm_display_free_group_prop_hooks (display);

  g_free (display->name);

  cobiwm_display_shutdown_keys (display);

  if (display->compositor)
    cobiwm_compositor_destroy (display->compositor);

  g_object_unref (display);
  the_display = NULL;

  cobiwm_quit (COBIWM_EXIT_SUCCESS);
}

/**
 * cobiwm_display_for_x_display:
 * @xdisplay: An X display
 *
 * Returns the singleton CobiwmDisplay if @xdisplay matches the X display it's
 * managing; otherwise gives a warning and returns %NULL.  When we were claiming
 * to be able to manage multiple displays, this was supposed to find the
 * display out of the list which matched that display.  Now it's merely an
 * extra sanity check.
 *
 * Returns: The singleton X display, or %NULL if @xdisplay isn't the one
 *          we're managing.
 */
CobiwmDisplay*
cobiwm_display_for_x_display (Display *xdisplay)
{
  if (the_display->xdisplay == xdisplay)
    return the_display;

  cobiwm_warning ("Could not find display for X display %p, probably going to crash\n",
                xdisplay);

  return NULL;
}

/**
 * cobiwm_get_display:
 *
 * Accessor for the singleton CobiwmDisplay.
 *
 * Returns: The only #CobiwmDisplay there is.  This can be %NULL, but only
 *          during startup.
 */
CobiwmDisplay*
cobiwm_get_display (void)
{
  return the_display;
}

static inline gboolean
grab_op_is_window (CobiwmGrabOp op)
{
  return GRAB_OP_GET_BASE_TYPE (op) == COBIWM_GRAB_OP_WINDOW_BASE;
}

gboolean
cobiwm_grab_op_is_mouse (CobiwmGrabOp op)
{
  if (!grab_op_is_window (op))
    return FALSE;

  return (op & COBIWM_GRAB_OP_WINDOW_FLAG_KEYBOARD) == 0;
}

gboolean
cobiwm_grab_op_is_keyboard (CobiwmGrabOp op)
{
  if (!grab_op_is_window (op))
    return FALSE;

  return (op & COBIWM_GRAB_OP_WINDOW_FLAG_KEYBOARD) != 0;
}

gboolean
cobiwm_grab_op_is_resizing (CobiwmGrabOp op)
{
  if (!grab_op_is_window (op))
    return FALSE;

  return (op & COBIWM_GRAB_OP_WINDOW_DIR_MASK) != 0 || op == COBIWM_GRAB_OP_KEYBOARD_RESIZING_UNKNOWN;
}

gboolean
cobiwm_grab_op_is_moving (CobiwmGrabOp op)
{
  if (!grab_op_is_window (op))
    return FALSE;

  return !cobiwm_grab_op_is_resizing (op);
}

/**
 * cobiwm_display_windows_are_interactable:
 * @op: A #CobiwmGrabOp
 *
 * Whether windows can be interacted with.
 */
gboolean
cobiwm_display_windows_are_interactable (CobiwmDisplay *display)
{
  switch (display->event_route)
    {
    case COBIWM_EVENT_ROUTE_NORMAL:
    case COBIWM_EVENT_ROUTE_WAYLAND_POPUP:
      return TRUE;
    default:
      return FALSE;
    }
}

/**
 * cobiwm_display_xserver_time_is_before:
 * @display: a #CobiwmDisplay
 * @time1: An event timestamp
 * @time2: An event timestamp
 *
 * Xserver time can wraparound, thus comparing two timestamps needs to take
 * this into account. If no wraparound has occurred, this is equivalent to
 *   time1 < time2
 * Otherwise, we need to account for the fact that wraparound can occur
 * and the fact that a timestamp of 0 must be special-cased since it
 * means "older than anything else".
 *
 * Note that this is NOT an equivalent for time1 <= time2; if that's what
 * you need then you'll need to swap the order of the arguments and negate
 * the result.
 */
gboolean
cobiwm_display_xserver_time_is_before (CobiwmDisplay   *display,
                                     guint32        time1,
                                     guint32        time2)
{
  return XSERVER_TIME_IS_BEFORE(time1, time2);
}

/**
 * cobiwm_display_get_last_user_time:
 * @display: a #CobiwmDisplay
 *
 * Returns: Timestamp of the last user interaction event with a window
 */
guint32
cobiwm_display_get_last_user_time (CobiwmDisplay *display)
{
  return display->last_user_time;
}

/* Get time of current event, or CurrentTime if none. */
guint32
cobiwm_display_get_current_time (CobiwmDisplay *display)
{
  return display->current_time;
}

static Bool
find_timestamp_predicate (Display  *xdisplay,
                          XEvent   *ev,
                          XPointer  arg)
{
  CobiwmDisplay *display = (CobiwmDisplay *) arg;

  return (ev->type == PropertyNotify &&
          ev->xproperty.atom == display->atom__COBIWM_TIMESTAMP_PING);
}

/* Get a timestamp, even if it means a roundtrip */
guint32
cobiwm_display_get_current_time_roundtrip (CobiwmDisplay *display)
{
  guint32 timestamp;

  timestamp = cobiwm_display_get_current_time (display);
  if (timestamp == CurrentTime)
    {
      XEvent property_event;

      XChangeProperty (display->xdisplay, display->timestamp_pinging_window,
                       display->atom__COBIWM_TIMESTAMP_PING,
                       XA_STRING, 8, PropModeAppend, NULL, 0);
      XIfEvent (display->xdisplay,
                &property_event,
                find_timestamp_predicate,
                (XPointer) display);
      timestamp = property_event.xproperty.time;
    }

  cobiwm_display_sanity_check_timestamps (display, timestamp);

  return timestamp;
}

/**
 * cobiwm_display_add_ignored_crossing_serial:
 * @display: a #CobiwmDisplay
 * @serial: the serial to ignore
 *
 * Save the specified serial and ignore crossing events with that
 * serial for the purpose of focus-follows-mouse. This can be used
 * for certain changes to the window hierarchy that we don't want
 * to change the focus window, even if they cause the pointer to
 * end up in a new window.
 */
void
cobiwm_display_add_ignored_crossing_serial (CobiwmDisplay  *display,
                                          unsigned long serial)
{
  int i;

  /* don't add the same serial more than once */
  if (display->ignored_crossing_serials[N_IGNORED_CROSSING_SERIALS-1] == serial)
    return;

  /* shift serials to the left */
  i = 0;
  while (i < (N_IGNORED_CROSSING_SERIALS - 1))
    {
      display->ignored_crossing_serials[i] = display->ignored_crossing_serials[i+1];
      ++i;
    }
  /* put new one on the end */
  display->ignored_crossing_serials[i] = serial;
}

static gboolean
window_raise_with_delay_callback (void *data)
{
  CobiwmWindow *window = data;

  window->display->autoraise_timeout_id = 0;
  window->display->autoraise_window = NULL;

  /* If we aren't already on top, check whether the pointer is inside
   * the window and raise the window if so.
   */
  if (cobiwm_stack_get_top (window->screen->stack) != window)
    {
      if (cobiwm_window_has_pointer (window))
	cobiwm_window_raise (window);
      else
	cobiwm_topic (COBIWM_DEBUG_FOCUS,
		    "Pointer not inside window, not raising %s\n",
		    window->desc);
    }

  return G_SOURCE_REMOVE;
}

void
cobiwm_display_queue_autoraise_callback (CobiwmDisplay *display,
                                       CobiwmWindow  *window)
{
  cobiwm_topic (COBIWM_DEBUG_FOCUS,
              "Queuing an autoraise timeout for %s with delay %d\n",
              window->desc,
              cobiwm_prefs_get_auto_raise_delay ());

  if (display->autoraise_timeout_id != 0)
    g_source_remove (display->autoraise_timeout_id);

  display->autoraise_timeout_id =
    g_timeout_add_full (G_PRIORITY_DEFAULT,
                        cobiwm_prefs_get_auto_raise_delay (),
                        window_raise_with_delay_callback,
                        window, NULL);
  g_source_set_name_by_id (display->autoraise_timeout_id, "[cobiwm] window_raise_with_delay_callback");
  display->autoraise_window = window;
}

void
cobiwm_display_sync_wayland_input_focus (CobiwmDisplay *display)
{
#ifdef HAVE_WAYLAND
  CobiwmWaylandCompositor *compositor = cobiwm_wayland_compositor_get_default ();
  CobiwmWindow *focus_window = NULL;
  CobiwmBackend *backend = cobiwm_get_backend ();
  CobiwmStage *stage = COBIWM_STAGE (cobiwm_backend_get_stage (backend));

  if (!cobiwm_display_windows_are_interactable (display))
    focus_window = NULL;
  else if (cobiwm_display_xwindow_is_a_no_focus_window (display, display->focus_xwindow))
    focus_window = NULL;
  else if (display->focus_window && display->focus_window->surface)
    focus_window = display->focus_window;
  else
    cobiwm_topic (COBIWM_DEBUG_FOCUS, "Focus change has no effect, because there is no matching wayland surface");

  cobiwm_stage_set_active (stage, focus_window == NULL);
  cobiwm_wayland_compositor_set_input_focus (compositor, focus_window);

  cobiwm_wayland_seat_repick (compositor->seat);
#endif
}

void
cobiwm_display_update_focus_window (CobiwmDisplay *display,
                                  CobiwmWindow  *window,
                                  Window       xwindow,
                                  gulong       serial,
                                  gboolean     focused_by_us)
{
  display->focus_serial = serial;
  display->focused_by_us = focused_by_us;

  if (display->focus_xwindow == xwindow &&
      display->focus_window == window)
    return;

  if (display->focus_window)
    {
      CobiwmWindow *previous;

      cobiwm_topic (COBIWM_DEBUG_FOCUS,
                  "%s is now the previous focus window due to being focused out or unmapped\n",
                  display->focus_window->desc);

      /* Make sure that signals handlers invoked by
       * cobiwm_window_set_focused_internal() don't see
       * display->focus_window->has_focus == FALSE
       */
      previous = display->focus_window;
      display->focus_window = NULL;
      display->focus_xwindow = None;

      cobiwm_window_set_focused_internal (previous, FALSE);
    }

  display->focus_window = window;
  display->focus_xwindow = xwindow;

  if (display->focus_window)
    {
      cobiwm_topic (COBIWM_DEBUG_FOCUS, "* Focus --> %s with serial %lu\n",
                  display->focus_window->desc, serial);
      cobiwm_window_set_focused_internal (display->focus_window, TRUE);
    }
  else
    cobiwm_topic (COBIWM_DEBUG_FOCUS, "* Focus --> NULL with serial %lu\n", serial);

  if (cobiwm_is_wayland_compositor ())
    cobiwm_display_sync_wayland_input_focus (display);

  g_object_notify (G_OBJECT (display), "focus-window");
  cobiwm_display_update_active_window_hint (display);
}

gboolean
cobiwm_display_timestamp_too_old (CobiwmDisplay *display,
                                guint32     *timestamp)
{
  /* FIXME: If Soeren's suggestion in bug 151984 is implemented, it will allow
   * us to sanity check the timestamp here and ensure it doesn't correspond to
   * a future time (though we would want to rename to
   * timestamp_too_old_or_in_future).
   */

  if (*timestamp == CurrentTime)
    {
      *timestamp = cobiwm_display_get_current_time_roundtrip (display);
      return FALSE;
    }
  else if (XSERVER_TIME_IS_BEFORE (*timestamp, display->last_focus_time))
    {
      if (XSERVER_TIME_IS_BEFORE (*timestamp, display->last_user_time))
        return TRUE;
      else
        {
          *timestamp = display->last_focus_time;
          return FALSE;
        }
    }

  return FALSE;
}

static void
request_xserver_input_focus_change (CobiwmDisplay *display,
                                    CobiwmScreen  *screen,
                                    CobiwmWindow  *cobiwm_window,
                                    Window       xwindow,
                                    guint32      timestamp)
{
  gulong serial;

  if (cobiwm_display_timestamp_too_old (display, &timestamp))
    return;

  cobiwm_error_trap_push (display);

  /* In order for cobiwm to know that the focus request succeeded, we track
   * the serial of the "focus request" we made, but if we take the serial
   * of the XSetInputFocus request, then there's no way to determine the
   * difference between focus events as a result of the SetInputFocus and
   * focus events that other clients send around the same time. Ensure that
   * we know which is which by making two requests that the server will
   * process at the same time.
   */
  XGrabServer (display->xdisplay);

  serial = XNextRequest (display->xdisplay);

  XSetInputFocus (display->xdisplay,
                  xwindow,
                  RevertToPointerRoot,
                  timestamp);

  XChangeProperty (display->xdisplay, display->timestamp_pinging_window,
                   display->atom__COBIWM_FOCUS_SET,
                   XA_STRING, 8, PropModeAppend, NULL, 0);

  XUngrabServer (display->xdisplay);
  XFlush (display->xdisplay);

  cobiwm_display_update_focus_window (display,
                                    cobiwm_window,
                                    xwindow,
                                    serial,
                                    TRUE);

  cobiwm_error_trap_pop (display);

  display->last_focus_time = timestamp;

  if (cobiwm_window == NULL || cobiwm_window != display->autoraise_window)
    cobiwm_display_remove_autoraise_callback (display);
}

CobiwmWindow*
cobiwm_display_lookup_x_window (CobiwmDisplay *display,
                              Window       xwindow)
{
  return g_hash_table_lookup (display->xids, &xwindow);
}

void
cobiwm_display_register_x_window (CobiwmDisplay *display,
                                Window      *xwindowp,
                                CobiwmWindow  *window)
{
  g_return_if_fail (g_hash_table_lookup (display->xids, xwindowp) == NULL);

  g_hash_table_insert (display->xids, xwindowp, window);
}

void
cobiwm_display_unregister_x_window (CobiwmDisplay *display,
                                  Window       xwindow)
{
  g_return_if_fail (g_hash_table_lookup (display->xids, &xwindow) != NULL);

  g_hash_table_remove (display->xids, &xwindow);
}

void
cobiwm_display_register_wayland_window (CobiwmDisplay *display,
                                      CobiwmWindow  *window)
{
  g_hash_table_add (display->wayland_windows, window);
}

void
cobiwm_display_unregister_wayland_window (CobiwmDisplay *display,
                                        CobiwmWindow  *window)
{
  g_hash_table_remove (display->wayland_windows, window);
}

CobiwmWindow*
cobiwm_display_lookup_stamp (CobiwmDisplay *display,
                           guint64       stamp)
{
  return g_hash_table_lookup (display->stamps, &stamp);
}

void
cobiwm_display_register_stamp (CobiwmDisplay *display,
                             guint64     *stampp,
                             CobiwmWindow  *window)
{
  g_return_if_fail (g_hash_table_lookup (display->stamps, stampp) == NULL);

  g_hash_table_insert (display->stamps, stampp, window);
}

void
cobiwm_display_unregister_stamp (CobiwmDisplay *display,
                               guint64      stamp)
{
  g_return_if_fail (g_hash_table_lookup (display->stamps, &stamp) != NULL);

  g_hash_table_remove (display->stamps, &stamp);
}

CobiwmWindow*
cobiwm_display_lookup_stack_id (CobiwmDisplay *display,
                              guint64      stack_id)
{
  if (COBIWM_STACK_ID_IS_X11 (stack_id))
    return cobiwm_display_lookup_x_window (display, (Window)stack_id);
  else
    return cobiwm_display_lookup_stamp (display, stack_id);
}

/* We return a pointer into a ring of static buffers. This is to make
 * using this function for debug-logging convenient and avoid tempory
 * strings that must be freed. */
const char *
cobiwm_display_describe_stack_id (CobiwmDisplay *display,
                                guint64      stack_id)
{
  /* 0x<64-bit: 16 characters> (<10 characters of title>)\0' */
  static char buffer[5][32];
  CobiwmWindow *window;
  static int pos = 0;
  char *result;

  result = buffer[pos];
  pos = (pos + 1) % 5;

  window = cobiwm_display_lookup_stack_id (display, stack_id);

  if (window && window->title)
    snprintf (result, sizeof(buffer[0]), "%#" G_GINT64_MODIFIER "x (%.10s)", stack_id, window->title);
  else
    snprintf (result, sizeof(buffer[0]), "%#" G_GINT64_MODIFIER "x", stack_id);

  return result;
}

/* We store sync alarms in the window ID hash table, because they are
 * just more types of XIDs in the same global space, but we have
 * typesafe functions to register/unregister for readability.
 */

CobiwmWindow*
cobiwm_display_lookup_sync_alarm (CobiwmDisplay *display,
                                XSyncAlarm   alarm)
{
  return g_hash_table_lookup (display->xids, &alarm);
}

void
cobiwm_display_register_sync_alarm (CobiwmDisplay *display,
                                  XSyncAlarm  *alarmp,
                                  CobiwmWindow  *window)
{
  g_return_if_fail (g_hash_table_lookup (display->xids, alarmp) == NULL);

  g_hash_table_insert (display->xids, alarmp, window);
}

void
cobiwm_display_unregister_sync_alarm (CobiwmDisplay *display,
                                    XSyncAlarm   alarm)
{
  g_return_if_fail (g_hash_table_lookup (display->xids, &alarm) != NULL);

  g_hash_table_remove (display->xids, &alarm);
}

void
cobiwm_display_notify_window_created (CobiwmDisplay  *display,
                                    CobiwmWindow   *window)
{
  g_signal_emit (display, display_signals[WINDOW_CREATED], 0, window);
}

/**
 * cobiwm_display_xwindow_is_a_no_focus_window:
 * @display: A #CobiwmDisplay
 * @xwindow: An X11 window
 *
 * Returns: %TRUE iff window is one of cobiwm's internal "no focus" windows
 * (there is one per screen) which will have the focus when there is no
 * actual client window focused.
 */
gboolean
cobiwm_display_xwindow_is_a_no_focus_window (CobiwmDisplay *display,
                                           Window xwindow)
{
  return xwindow == display->screen->no_focus_window;
}

static CobiwmCursor
cobiwm_cursor_for_grab_op (CobiwmGrabOp op)
{
  switch (op)
    {
    case COBIWM_GRAB_OP_RESIZING_SE:
    case COBIWM_GRAB_OP_KEYBOARD_RESIZING_SE:
      return COBIWM_CURSOR_SE_RESIZE;
      break;
    case COBIWM_GRAB_OP_RESIZING_S:
    case COBIWM_GRAB_OP_KEYBOARD_RESIZING_S:
      return COBIWM_CURSOR_SOUTH_RESIZE;
      break;
    case COBIWM_GRAB_OP_RESIZING_SW:
    case COBIWM_GRAB_OP_KEYBOARD_RESIZING_SW:
      return COBIWM_CURSOR_SW_RESIZE;
      break;
    case COBIWM_GRAB_OP_RESIZING_N:
    case COBIWM_GRAB_OP_KEYBOARD_RESIZING_N:
      return COBIWM_CURSOR_NORTH_RESIZE;
      break;
    case COBIWM_GRAB_OP_RESIZING_NE:
    case COBIWM_GRAB_OP_KEYBOARD_RESIZING_NE:
      return COBIWM_CURSOR_NE_RESIZE;
      break;
    case COBIWM_GRAB_OP_RESIZING_NW:
    case COBIWM_GRAB_OP_KEYBOARD_RESIZING_NW:
      return COBIWM_CURSOR_NW_RESIZE;
      break;
    case COBIWM_GRAB_OP_RESIZING_W:
    case COBIWM_GRAB_OP_KEYBOARD_RESIZING_W:
      return COBIWM_CURSOR_WEST_RESIZE;
      break;
    case COBIWM_GRAB_OP_RESIZING_E:
    case COBIWM_GRAB_OP_KEYBOARD_RESIZING_E:
      return COBIWM_CURSOR_EAST_RESIZE;
      break;
    case COBIWM_GRAB_OP_MOVING:
    case COBIWM_GRAB_OP_KEYBOARD_MOVING:
    case COBIWM_GRAB_OP_KEYBOARD_RESIZING_UNKNOWN:
      return COBIWM_CURSOR_MOVE_OR_RESIZE_WINDOW;
      break;
    default:
      break;
    }

  return COBIWM_CURSOR_DEFAULT;
}

void
cobiwm_display_update_cursor (CobiwmDisplay *display)
{
  cobiwm_screen_set_cursor (display->screen, cobiwm_cursor_for_grab_op (display->grab_op));
}

static CobiwmWindow *
get_first_freefloating_window (CobiwmWindow *window)
{
  while (cobiwm_window_is_attached_dialog (window))
    window = cobiwm_window_get_transient_for (window);

  /* Attached dialogs should always have a non-NULL transient-for */
  g_assert (window != NULL);

  return window;
}

static CobiwmEventRoute
get_event_route_from_grab_op (CobiwmGrabOp op)
{
  switch (GRAB_OP_GET_BASE_TYPE (op))
    {
    case COBIWM_GRAB_OP_NONE:
      /* begin_grab_op shouldn't be called with COBIWM_GRAB_OP_NONE. */
      g_assert_not_reached ();

    case COBIWM_GRAB_OP_WINDOW_BASE:
      return COBIWM_EVENT_ROUTE_WINDOW_OP;

    case COBIWM_GRAB_OP_COMPOSITOR:
      /* begin_grab_op shouldn't be called with COBIWM_GRAB_OP_COMPOSITOR. */
      g_assert_not_reached ();

    case COBIWM_GRAB_OP_WAYLAND_POPUP:
      return COBIWM_EVENT_ROUTE_WAYLAND_POPUP;

    case COBIWM_GRAB_OP_FRAME_BUTTON:
      return COBIWM_EVENT_ROUTE_FRAME_BUTTON;

    default:
      g_assert_not_reached ();
    }
}

gboolean
cobiwm_display_begin_grab_op (CobiwmDisplay *display,
			    CobiwmScreen  *screen,
                            CobiwmWindow  *window,
                            CobiwmGrabOp   op,
                            gboolean     pointer_already_grabbed,
                            gboolean     frame_action,
                            int          button,
                            gulong       modmask, /* XXX - ignored */
                            guint32      timestamp,
                            int          root_x,
                            int          root_y)
{
  CobiwmBackend *backend = cobiwm_get_backend ();
  CobiwmWindow *grab_window = NULL;
  CobiwmEventRoute event_route;

  g_assert (window != NULL);

  cobiwm_topic (COBIWM_DEBUG_WINDOW_OPS,
              "Doing grab op %u on window %s button %d pointer already grabbed: %d pointer pos %d,%d\n",
              op, window->desc, button, pointer_already_grabbed,
              root_x, root_y);

  if (display->grab_op != COBIWM_GRAB_OP_NONE)
    {
      cobiwm_warning ("Attempt to perform window operation %u on window %s when operation %u on %s already in effect\n",
                    op, window->desc, display->grab_op,
                    display->grab_window ? display->grab_window->desc : "none");
      return FALSE;
    }

  event_route = get_event_route_from_grab_op (op);

  if (event_route == COBIWM_EVENT_ROUTE_WINDOW_OP)
    {
      if (cobiwm_prefs_get_raise_on_click ())
        cobiwm_window_raise (window);
      else
        {
          display->grab_initial_x = root_x;
          display->grab_initial_y = root_y;
          display->grab_threshold_movement_reached = FALSE;
        }
    }

  grab_window = window;

  /* If we're trying to move a window, move the first
   * non-attached dialog instead.
   */
  if (cobiwm_grab_op_is_moving (op))
    grab_window = get_first_freefloating_window (window);

  g_assert (grab_window != NULL);
  g_assert (op != COBIWM_GRAB_OP_NONE);

  display->grab_have_pointer = FALSE;

  if (pointer_already_grabbed)
    display->grab_have_pointer = TRUE;

  /* Since grab operations often happen as a result of implicit
   * pointer operations on the display X11 connection, we need
   * to ungrab here to ensure that the backend's X11 can take
   * the device grab. */
  XIUngrabDevice (display->xdisplay,
                  COBIWM_VIRTUAL_CORE_POINTER_ID,
                  timestamp);
  XSync (display->xdisplay, False);

  if (cobiwm_backend_grab_device (backend, COBIWM_VIRTUAL_CORE_POINTER_ID, timestamp))
    display->grab_have_pointer = TRUE;

  if (!display->grab_have_pointer && !cobiwm_grab_op_is_keyboard (op))
    {
      cobiwm_topic (COBIWM_DEBUG_WINDOW_OPS, "XIGrabDevice() failed\n");
      return FALSE;
    }

  /* Grab keys when beginning window ops; see #126497 */
  if (event_route == COBIWM_EVENT_ROUTE_WINDOW_OP)
    {
      display->grab_have_keyboard = cobiwm_window_grab_all_keys (grab_window, timestamp);

      if (!display->grab_have_keyboard)
        {
          cobiwm_topic (COBIWM_DEBUG_WINDOW_OPS, "grabbing all keys failed, ungrabbing pointer\n");
          cobiwm_backend_ungrab_device (backend, COBIWM_VIRTUAL_CORE_POINTER_ID, timestamp);
          display->grab_have_pointer = FALSE;
          return FALSE;
        }
    }

  display->event_route = event_route;
  display->grab_op = op;
  display->grab_window = grab_window;
  display->grab_button = button;
  display->grab_tile_mode = grab_window->tile_mode;
  display->grab_tile_monitor_number = grab_window->tile_monitor_number;
  display->grab_anchor_root_x = root_x;
  display->grab_anchor_root_y = root_y;
  display->grab_latest_motion_x = root_x;
  display->grab_latest_motion_y = root_y;
  display->grab_last_moveresize_time.tv_sec = 0;
  display->grab_last_moveresize_time.tv_usec = 0;
  display->grab_last_user_action_was_snap = FALSE;
  display->grab_frame_action = frame_action;

  cobiwm_display_update_cursor (display);

  if (display->grab_resize_timeout_id)
    {
      g_source_remove (display->grab_resize_timeout_id);
      display->grab_resize_timeout_id = 0;
    }

  cobiwm_topic (COBIWM_DEBUG_WINDOW_OPS,
              "Grab op %u on window %s successful\n",
              display->grab_op, window ? window->desc : "(null)");

  cobiwm_window_get_frame_rect (display->grab_window,
                              &display->grab_initial_window_pos);
  display->grab_anchor_window_pos = display->grab_initial_window_pos;

  if (cobiwm_is_wayland_compositor ())
    {
      cobiwm_display_sync_wayland_input_focus (display);
      cobiwm_display_cancel_touch (display);
    }

  g_signal_emit (display, display_signals[GRAB_OP_BEGIN], 0,
                 screen, display->grab_window, display->grab_op);

  if (display->event_route == COBIWM_EVENT_ROUTE_WINDOW_OP)
    cobiwm_window_grab_op_began (display->grab_window, display->grab_op);

  return TRUE;
}

void
cobiwm_display_end_grab_op (CobiwmDisplay *display,
                          guint32      timestamp)
{
  CobiwmWindow *grab_window = display->grab_window;
  CobiwmGrabOp grab_op = display->grab_op;

  cobiwm_topic (COBIWM_DEBUG_WINDOW_OPS,
              "Ending grab op %u at time %u\n", grab_op, timestamp);

  if (display->event_route == COBIWM_EVENT_ROUTE_NORMAL ||
      display->event_route == COBIWM_EVENT_ROUTE_COMPOSITOR_GRAB)
    return;

  g_assert (grab_window != NULL);

  g_signal_emit (display, display_signals[GRAB_OP_END], 0,
                 display->screen, grab_window, grab_op);

  /* We need to reset this early, since the
   * cobiwm_window_grab_op_ended callback relies on this being
   * up to date. */
  display->grab_op = COBIWM_GRAB_OP_NONE;

  if (display->event_route == COBIWM_EVENT_ROUTE_WINDOW_OP)
    {
      /* Clear out the edge cache */
      cobiwm_display_cleanup_edges (display);

      /* Only raise the window in orthogonal raise
       * ('do-not-raise-on-click') mode if the user didn't try to move
       * or resize the given window by at least a threshold amount.
       * For raise on click mode, the window was raised at the
       * beginning of the grab_op.
       */
      if (!cobiwm_prefs_get_raise_on_click () &&
          !display->grab_threshold_movement_reached)
        cobiwm_window_raise (display->grab_window);

      cobiwm_window_grab_op_ended (grab_window, grab_op);
    }

  if (display->grab_have_pointer)
    {
      CobiwmBackend *backend = cobiwm_get_backend ();
      cobiwm_backend_ungrab_device (backend, COBIWM_VIRTUAL_CORE_POINTER_ID, timestamp);
    }

  if (display->grab_have_keyboard)
    {
      cobiwm_topic (COBIWM_DEBUG_WINDOW_OPS,
                  "Ungrabbing all keys timestamp %u\n", timestamp);
      cobiwm_window_ungrab_all_keys (grab_window, timestamp);
    }

  display->event_route = COBIWM_EVENT_ROUTE_NORMAL;
  display->grab_window = NULL;
  display->grab_tile_mode = COBIWM_TILE_NONE;
  display->grab_tile_monitor_number = -1;

  cobiwm_display_update_cursor (display);

  if (display->grab_resize_timeout_id)
    {
      g_source_remove (display->grab_resize_timeout_id);
      display->grab_resize_timeout_id = 0;
    }

  if (cobiwm_is_wayland_compositor ())
    cobiwm_display_sync_wayland_input_focus (display);
}

/**
 * cobiwm_display_get_grab_op:
 * @display: The #CobiwmDisplay that the window is on

 * Gets the current grab operation, if any.
 *
 * Return value: the current grab operation, or %COBIWM_GRAB_OP_NONE if
 * Cobiwm doesn't currently have a grab. %COBIWM_GRAB_OP_COMPOSITOR will
 * be returned if a compositor-plugin modal operation is in effect
 * (See cobiwm_begin_modal_for_plugin())
 */
CobiwmGrabOp
cobiwm_display_get_grab_op (CobiwmDisplay *display)
{
  return display->grab_op;
}

void
cobiwm_display_check_threshold_reached (CobiwmDisplay *display,
                                      int          x,
                                      int          y)
{
  /* Don't bother doing the check again if we've already reached the threshold */
  if (cobiwm_prefs_get_raise_on_click () ||
      display->grab_threshold_movement_reached)
    return;

  if (ABS (display->grab_initial_x - x) >= 8 ||
      ABS (display->grab_initial_y - y) >= 8)
    display->grab_threshold_movement_reached = TRUE;
}

void
cobiwm_display_increment_event_serial (CobiwmDisplay *display)
{
  /* We just make some random X request */
  XDeleteProperty (display->xdisplay, display->leader_window,
                   display->atom__MOTIF_WM_HINTS);
}

void
cobiwm_display_update_active_window_hint (CobiwmDisplay *display)
{
  gulong data[1];

  if (display->focus_window)
    data[0] = display->focus_window->xwindow;
  else
    data[0] = None;

  cobiwm_error_trap_push (display);
  XChangeProperty (display->xdisplay, display->screen->xroot,
                   display->atom__NET_ACTIVE_WINDOW,
                   XA_WINDOW,
                   32, PropModeReplace, (guchar*) data, 1);
  cobiwm_error_trap_pop (display);
}

void
cobiwm_display_queue_retheme_all_windows (CobiwmDisplay *display)
{
  GSList* windows;
  GSList *tmp;

  windows = cobiwm_display_list_windows (display, COBIWM_LIST_DEFAULT);
  tmp = windows;
  while (tmp != NULL)
    {
      CobiwmWindow *window = tmp->data;

      cobiwm_window_queue (window, COBIWM_QUEUE_MOVE_RESIZE);
      cobiwm_window_frame_size_changed (window);
      if (window->frame)
        {
          cobiwm_frame_queue_draw (window->frame);
        }

      tmp = tmp->next;
    }

  g_slist_free (windows);
}

void
cobiwm_display_retheme_all (void)
{
  cobiwm_display_queue_retheme_all_windows (cobiwm_get_display ());
}

static void
set_cursor_theme (Display *xdisplay)
{
  XcursorSetTheme (xdisplay, cobiwm_prefs_get_cursor_theme ());
  XcursorSetDefaultSize (xdisplay, cobiwm_prefs_get_cursor_size ());
}

static void
update_cursor_theme (void)
{
  {
    CobiwmDisplay *display = cobiwm_get_display ();
    set_cursor_theme (display->xdisplay);

    if (display->screen)
      cobiwm_screen_update_cursor (display->screen);
  }

  {
    CobiwmBackend *backend = cobiwm_get_backend ();
    if (COBIWM_IS_BACKEND_X11 (backend))
      {
        Display *xdisplay = cobiwm_backend_x11_get_xdisplay (COBIWM_BACKEND_X11 (backend));
        set_cursor_theme (xdisplay);
      }
  }
}

/*
 * Stores whether syncing is currently enabled.
 */
static gboolean is_syncing = FALSE;

/**
 * cobiwm_is_syncing:
 *
 * Returns whether X synchronisation is currently enabled.
 *
 * FIXME: This is *only* called by cobiwm_display_open(), but by that time
 * we have already turned syncing on or off on startup, and we don't
 * have any way to do so while Cobiwm is running, so it's rather
 * pointless.
 *
 * Returns: %TRUE if we must wait for events whenever we send X requests;
 * %FALSE otherwise.
 */
gboolean
cobiwm_is_syncing (void)
{
  return is_syncing;
}

/**
 * cobiwm_set_syncing:
 * @setting: whether to turn syncing on or off
 *
 * A handy way to turn on synchronisation on or off for every display.
 */
void
cobiwm_set_syncing (gboolean setting)
{
  if (setting != is_syncing)
    {
      is_syncing = setting;
      if (cobiwm_get_display ())
        XSynchronize (cobiwm_get_display ()->xdisplay, is_syncing);
    }
}

/*
 * How long, in milliseconds, we should wait after pinging a window
 * before deciding it's not going to get back to us.
 */
#define PING_TIMEOUT_DELAY 5000

/**
 * cobiwm_display_ping_timeout:
 * @data: All the information about this ping. It is a #CobiwmPingData
 *        cast to a #gpointer in order to be passable to a timeout function.
 *        This function will also free this parameter.
 *
 * Does whatever it is we decided to do when a window didn't respond
 * to a ping. We also remove the ping from the display's list of
 * pending pings. This function is called by the event loop when the timeout
 * times out which we created at the start of the ping.
 *
 * Returns: Always returns %FALSE, because this function is called as a
 *          timeout and we don't want to run the timer again.
 */
static gboolean
cobiwm_display_ping_timeout (gpointer data)
{
  CobiwmPingData *ping_data = data;
  CobiwmWindow *window = ping_data->window;
  CobiwmDisplay *display = window->display;

  cobiwm_window_set_alive (window, FALSE);

  ping_data->ping_timeout_id = 0;

  cobiwm_topic (COBIWM_DEBUG_PING,
              "Ping %u on window %s timed out\n",
              ping_data->serial, ping_data->window->desc);

  display->pending_pings = g_slist_remove (display->pending_pings, ping_data);
  ping_data_free (ping_data);

  return FALSE;
}

/**
 * cobiwm_display_ping_window:
 * @display: The #CobiwmDisplay that the window is on
 * @window: The #CobiwmWindow to send the ping to
 * @timestamp: The timestamp of the ping. Used for uniqueness.
 *             Cannot be CurrentTime; use a real timestamp!
 *
 * Sends a ping request to a window. The window must respond to
 * the request within a certain amount of time. If it does, we
 * will call one callback; if the time passes and we haven't had
 * a response, we call a different callback. The window must have
 * the hint showing that it can respond to a ping; if it doesn't,
 * we call the "got a response" callback immediately and return.
 * This function returns straight away after setting things up;
 * the callbacks will be called from the event loop.
 */
void
cobiwm_display_ping_window (CobiwmWindow *window,
			  guint32     serial)
{
  CobiwmDisplay *display = window->display;
  CobiwmPingData *ping_data;

  if (serial == 0)
    {
      cobiwm_warning ("Tried to ping a window with a bad serial! Not allowed.\n");
      return;
    }

  if (!window->can_ping)
    return;

  ping_data = g_new (CobiwmPingData, 1);
  ping_data->window = window;
  ping_data->serial = serial;
  ping_data->ping_timeout_id = g_timeout_add (PING_TIMEOUT_DELAY,
					      cobiwm_display_ping_timeout,
					      ping_data);
  g_source_set_name_by_id (ping_data->ping_timeout_id, "[cobiwm] cobiwm_display_ping_timeout");

  display->pending_pings = g_slist_prepend (display->pending_pings, ping_data);

  cobiwm_topic (COBIWM_DEBUG_PING,
              "Sending ping with serial %u to window %s\n",
              serial, window->desc);

  COBIWM_WINDOW_GET_CLASS (window)->ping (window, serial);
}

/**
 * cobiwm_display_pong_for_serial:
 * @display: the display we got the pong from
 * @serial: the serial in the pong repsonse
 *
 * Process the pong (the response message) from the ping we sent
 * to the window. This involves removing the timeout, calling the
 * reply handler function, and freeing memory.
 */
void
cobiwm_display_pong_for_serial (CobiwmDisplay    *display,
                              guint32         serial)
{
  GSList *tmp;

  cobiwm_topic (COBIWM_DEBUG_PING, "Received a pong with serial %u\n", serial);

  for (tmp = display->pending_pings; tmp; tmp = tmp->next)
    {
      CobiwmPingData *ping_data = tmp->data;

      if (serial == ping_data->serial)
        {
          cobiwm_topic (COBIWM_DEBUG_PING,
                      "Matching ping found for pong %u\n",
                      ping_data->serial);

          /* Remove the ping data from the list */
          display->pending_pings = g_slist_remove (display->pending_pings,
                                                   ping_data);

          /* Remove the timeout */
          if (ping_data->ping_timeout_id != 0)
            {
              g_source_remove (ping_data->ping_timeout_id);
              ping_data->ping_timeout_id = 0;
            }

          cobiwm_window_set_alive (ping_data->window, TRUE);
          ping_data_free (ping_data);
          break;
        }
    }
}

static CobiwmGroup *
get_focused_group (CobiwmDisplay *display)
{
  if (display->focus_window)
    return display->focus_window->group;
  else
    return NULL;
}

#define IN_TAB_CHAIN(w,t) (((t) == COBIWM_TAB_LIST_NORMAL && COBIWM_WINDOW_IN_NORMAL_TAB_CHAIN (w)) \
    || ((t) == COBIWM_TAB_LIST_DOCKS && COBIWM_WINDOW_IN_DOCK_TAB_CHAIN (w)) \
    || ((t) == COBIWM_TAB_LIST_GROUP && COBIWM_WINDOW_IN_GROUP_TAB_CHAIN (w, get_focused_group (w->display))) \
    || ((t) == COBIWM_TAB_LIST_NORMAL_ALL && COBIWM_WINDOW_IN_NORMAL_TAB_CHAIN_TYPE (w)))

static CobiwmWindow*
find_tab_forward (CobiwmDisplay   *display,
                  CobiwmTabList    type,
                  CobiwmWorkspace *workspace,
                  GList         *start,
                  gboolean       skip_first)
{
  GList *tmp;

  g_return_val_if_fail (start != NULL, NULL);
  g_return_val_if_fail (workspace != NULL, NULL);

  tmp = start;
  if (skip_first)
    tmp = tmp->next;

  while (tmp != NULL)
    {
      CobiwmWindow *window = tmp->data;

      if (IN_TAB_CHAIN (window, type))
        return window;

      tmp = tmp->next;
    }

  tmp = workspace->mru_list;
  while (tmp != start)
    {
      CobiwmWindow *window = tmp->data;

      if (IN_TAB_CHAIN (window, type))
        return window;

      tmp = tmp->next;
    }

  return NULL;
}

static CobiwmWindow*
find_tab_backward (CobiwmDisplay   *display,
                   CobiwmTabList    type,
                   CobiwmWorkspace *workspace,
                   GList         *start,
                   gboolean       skip_last)
{
  GList *tmp;

  g_return_val_if_fail (start != NULL, NULL);
  g_return_val_if_fail (workspace != NULL, NULL);

  tmp = start;
  if (skip_last)
    tmp = tmp->prev;
  while (tmp != NULL)
    {
      CobiwmWindow *window = tmp->data;

      if (IN_TAB_CHAIN (window, type))
        return window;

      tmp = tmp->prev;
    }

  tmp = g_list_last (workspace->mru_list);
  while (tmp != start)
    {
      CobiwmWindow *window = tmp->data;

      if (IN_TAB_CHAIN (window, type))
        return window;

      tmp = tmp->prev;
    }

  return NULL;
}

static int
mru_cmp (gconstpointer a,
         gconstpointer b)
{
  guint32 time_a, time_b;

  time_a = cobiwm_window_get_user_time ((CobiwmWindow *)a);
  time_b = cobiwm_window_get_user_time ((CobiwmWindow *)b);

  if (time_a > time_b)
    return -1;
  else if (time_a < time_b)
    return 1;
  else
    return 0;
}

/**
 * cobiwm_display_get_tab_list:
 * @display: a #CobiwmDisplay
 * @type: type of tab list
 * @workspace: (nullable): origin workspace
 *
 * Determine the list of windows that should be displayed for Alt-TAB
 * functionality.  The windows are returned in most recently used order.
 * If @workspace is not %NULL, the list only conains windows that are on
 * @workspace or have the demands-attention hint set; otherwise it contains
 * all windows.
 *
 * Returns: (transfer container) (element-type Cobiwm.Window): List of windows
 */
GList*
cobiwm_display_get_tab_list (CobiwmDisplay   *display,
                           CobiwmTabList    type,
                           CobiwmWorkspace *workspace)
{
  GList *tab_list = NULL;
  GList *global_mru_list = NULL;
  GList *mru_list, *tmp;
  GSList *windows = cobiwm_display_list_windows (display, COBIWM_LIST_DEFAULT);
  GSList *w;

  if (workspace == NULL)
    {
      /* Yay for mixing GList and GSList in the API */
      for (w = windows; w; w = w->next)
        global_mru_list = g_list_prepend (global_mru_list, w->data);
      global_mru_list = g_list_sort (global_mru_list, mru_cmp);
    }

  mru_list = workspace ? workspace->mru_list : global_mru_list;

  /* Windows sellout mode - MRU order. Collect unminimized windows
   * then minimized so minimized windows aren't in the way so much.
   */
  for (tmp = mru_list; tmp; tmp = tmp->next)
    {
      CobiwmWindow *window = tmp->data;

      if (!window->minimized && IN_TAB_CHAIN (window, type))
        tab_list = g_list_prepend (tab_list, window);
    }

  for (tmp = mru_list; tmp; tmp = tmp->next)
    {
      CobiwmWindow *window = tmp->data;

      if (window->minimized && IN_TAB_CHAIN (window, type))
        tab_list = g_list_prepend (tab_list, window);
    }

  tab_list = g_list_reverse (tab_list);

  /* If filtering by workspace, include windows from
   * other workspaces that demand attention
   */
  if (workspace)
    for (w = windows; w; w = w->next)
      {
        CobiwmWindow *l_window = w->data;

        if (l_window->wm_state_demands_attention &&
            l_window->workspace != workspace &&
            IN_TAB_CHAIN (l_window, type))
          tab_list = g_list_prepend (tab_list, l_window);
      }

  g_list_free (global_mru_list);
  g_slist_free (windows);

  return tab_list;
}

/**
 * cobiwm_display_get_tab_next:
 * @display: a #CobiwmDisplay
 * @type: type of tab list
 * @workspace: origin workspace
 * @window: (nullable): starting window
 * @backward: If %TRUE, look for the previous window.
 *
 * Determine the next window that should be displayed for Alt-TAB
 * functionality.
 *
 * Returns: (transfer none): Next window
 *
 */
CobiwmWindow*
cobiwm_display_get_tab_next (CobiwmDisplay   *display,
                           CobiwmTabList    type,
                           CobiwmWorkspace *workspace,
                           CobiwmWindow    *window,
                           gboolean       backward)
{
  gboolean skip;
  GList *tab_list;
  CobiwmWindow *ret;
  tab_list = cobiwm_display_get_tab_list (display, type, workspace);

  if (tab_list == NULL)
    return NULL;

  if (window != NULL)
    {
      g_assert (window->display == display);

      if (backward)
        ret = find_tab_backward (display, type, workspace, g_list_find (tab_list, window), TRUE);
      else
        ret = find_tab_forward (display, type, workspace, g_list_find (tab_list, window), TRUE);
    }
  else
    {
      skip = display->focus_window != NULL &&
             tab_list->data == display->focus_window;
      if (backward)
        ret = find_tab_backward (display, type, workspace, tab_list, skip);
      else
        ret = find_tab_forward (display, type, workspace, tab_list, skip);
    }

  g_list_free (tab_list);
  return ret;
}

/**
 * cobiwm_display_get_tab_current:
 * @display: a #CobiwmDisplay
 * @type: type of tab list
 * @workspace: origin workspace
 *
 * Determine the active window that should be displayed for Alt-TAB.
 *
 * Returns: (transfer none): Current window
 *
 */
CobiwmWindow*
cobiwm_display_get_tab_current (CobiwmDisplay   *display,
                              CobiwmTabList    type,
                              CobiwmWorkspace *workspace)
{
  CobiwmWindow *window;

  window = display->focus_window;

  if (window != NULL &&
      IN_TAB_CHAIN (window, type) &&
      (workspace == NULL ||
       cobiwm_window_located_on_workspace (window, workspace)))
    return window;
  else
    return NULL;
}

int
cobiwm_resize_gravity_from_grab_op (CobiwmGrabOp op)
{
  int gravity;

  gravity = -1;
  switch (op)
    {
    case COBIWM_GRAB_OP_RESIZING_SE:
    case COBIWM_GRAB_OP_KEYBOARD_RESIZING_SE:
      gravity = NorthWestGravity;
      break;
    case COBIWM_GRAB_OP_KEYBOARD_RESIZING_S:
    case COBIWM_GRAB_OP_RESIZING_S:
      gravity = NorthGravity;
      break;
    case COBIWM_GRAB_OP_KEYBOARD_RESIZING_SW:
    case COBIWM_GRAB_OP_RESIZING_SW:
      gravity = NorthEastGravity;
      break;
    case COBIWM_GRAB_OP_KEYBOARD_RESIZING_N:
    case COBIWM_GRAB_OP_RESIZING_N:
      gravity = SouthGravity;
      break;
    case COBIWM_GRAB_OP_KEYBOARD_RESIZING_NE:
    case COBIWM_GRAB_OP_RESIZING_NE:
      gravity = SouthWestGravity;
      break;
    case COBIWM_GRAB_OP_KEYBOARD_RESIZING_NW:
    case COBIWM_GRAB_OP_RESIZING_NW:
      gravity = SouthEastGravity;
      break;
    case COBIWM_GRAB_OP_KEYBOARD_RESIZING_E:
    case COBIWM_GRAB_OP_RESIZING_E:
      gravity = WestGravity;
      break;
    case COBIWM_GRAB_OP_KEYBOARD_RESIZING_W:
    case COBIWM_GRAB_OP_RESIZING_W:
      gravity = EastGravity;
      break;
    case COBIWM_GRAB_OP_KEYBOARD_RESIZING_UNKNOWN:
      gravity = CenterGravity;
      break;
    default:
      break;
    }

  return gravity;
}

void
cobiwm_display_unmanage_screen (CobiwmDisplay *display,
                              CobiwmScreen  *screen,
                              guint32      timestamp)
{
  cobiwm_verbose ("Unmanaging screen %d on display %s\n",
                screen->number, display->name);
  cobiwm_display_close (display, timestamp);
}

void
cobiwm_display_unmanage_windows_for_screen (CobiwmDisplay *display,
                                          CobiwmScreen  *screen,
                                          guint32      timestamp)
{
  GSList *tmp;
  GSList *winlist;

  winlist = cobiwm_display_list_windows (display,
                                       COBIWM_LIST_INCLUDE_OVERRIDE_REDIRECT);
  winlist = g_slist_sort (winlist, cobiwm_display_stack_cmp);
  g_slist_foreach (winlist, (GFunc)g_object_ref, NULL);

  /* Unmanage all windows */
  tmp = winlist;
  while (tmp != NULL)
    {
      CobiwmWindow *window = tmp->data;

      /* Check if already unmanaged for safety - in particular, catch
       * the case where unmanaging a parent window can cause attached
       * dialogs to be (temporarily) unmanaged.
       */
      if (!window->unmanaging)
        cobiwm_window_unmanage (window, timestamp);
      g_object_unref (window);

      tmp = tmp->next;
    }
  g_slist_free (winlist);
}

int
cobiwm_display_stack_cmp (const void *a,
                        const void *b)
{
  CobiwmWindow *aw = (void*) a;
  CobiwmWindow *bw = (void*) b;

  return cobiwm_stack_windows_cmp (aw->screen->stack, aw, bw);
}

/**
 * cobiwm_display_sort_windows_by_stacking:
 * @display: a #CobiwmDisplay
 * @windows: (element-type CobiwmWindow): Set of windows
 *
 * Sorts a set of windows according to their current stacking order. If windows
 * from multiple screens are present in the set of input windows, then all the
 * windows on screen 0 are sorted below all the windows on screen 1, and so forth.
 * Since the stacking order of override-redirect windows isn't controlled by
 * Cobiwmcity, if override-redirect windows are in the input, the result may not
 * correspond to the actual stacking order in the X server.
 *
 * An example of using this would be to sort the list of transient dialogs for a
 * window into their current stacking order.
 *
 * Returns: (transfer container) (element-type CobiwmWindow): Input windows sorted by stacking order, from lowest to highest
 */
GSList *
cobiwm_display_sort_windows_by_stacking (CobiwmDisplay *display,
                                       GSList      *windows)
{
  GSList *copy = g_slist_copy (windows);

  copy = g_slist_sort (copy, cobiwm_display_stack_cmp);

  return copy;
}

static void
prefs_changed_callback (CobiwmPreference pref,
                        void          *data)
{
  CobiwmDisplay *display = data;

  if (pref == COBIWM_PREF_FOCUS_MODE)
    {
      GSList *windows, *l;
      windows = cobiwm_display_list_windows (display, COBIWM_LIST_DEFAULT);

      for (l = windows; l; l = l->next)
        {
          CobiwmWindow *w = l->data;
          cobiwm_display_ungrab_focus_window_button (display, w);
          if (w->type != COBIWM_WINDOW_DOCK)
            cobiwm_display_grab_focus_window_button (display, w);
        }

      g_slist_free (windows);
    }
  else if (pref == COBIWM_PREF_AUDIBLE_BELL)
    {
      cobiwm_bell_set_audible (display, cobiwm_prefs_bell_is_audible ());
    }
  else if (pref == COBIWM_PREF_CURSOR_THEME ||
           pref == COBIWM_PREF_CURSOR_SIZE)
    {
      update_cursor_theme ();
    }
}

void
cobiwm_display_increment_focus_sentinel (CobiwmDisplay *display)
{
  unsigned long data[1];

  data[0] = cobiwm_display_get_current_time (display);

  XChangeProperty (display->xdisplay,
                   display->screen->xroot,
                   display->atom__COBIWM_SENTINEL,
                   XA_CARDINAL,
                   32, PropModeReplace, (guchar*) data, 1);

  display->sentinel_counter += 1;
}

void
cobiwm_display_decrement_focus_sentinel (CobiwmDisplay *display)
{
  display->sentinel_counter -= 1;

  if (display->sentinel_counter < 0)
    display->sentinel_counter = 0;
}

gboolean
cobiwm_display_focus_sentinel_clear (CobiwmDisplay *display)
{
  return (display->sentinel_counter == 0);
}

void
cobiwm_display_sanity_check_timestamps (CobiwmDisplay *display,
                                      guint32      timestamp)
{
  if (XSERVER_TIME_IS_BEFORE (timestamp, display->last_focus_time))
    {
      cobiwm_warning ("last_focus_time (%u) is greater than comparison "
                    "timestamp (%u).  This most likely represents a buggy "
                    "client sending inaccurate timestamps in messages such as "
                    "_NET_ACTIVE_WINDOW.  Trying to work around...\n",
                    display->last_focus_time, timestamp);
      display->last_focus_time = timestamp;
    }
  if (XSERVER_TIME_IS_BEFORE (timestamp, display->last_user_time))
    {
      GSList *windows;
      GSList *tmp;

      cobiwm_warning ("last_user_time (%u) is greater than comparison "
                    "timestamp (%u).  This most likely represents a buggy "
                    "client sending inaccurate timestamps in messages such as "
                    "_NET_ACTIVE_WINDOW.  Trying to work around...\n",
                    display->last_user_time, timestamp);
      display->last_user_time = timestamp;

      windows = cobiwm_display_list_windows (display, COBIWM_LIST_DEFAULT);
      tmp = windows;
      while (tmp != NULL)
        {
          CobiwmWindow *window = tmp->data;

          if (XSERVER_TIME_IS_BEFORE (timestamp, window->net_wm_user_time))
            {
              cobiwm_warning ("%s appears to be one of the offending windows "
                            "with a timestamp of %u.  Working around...\n",
                            window->desc, window->net_wm_user_time);
              cobiwm_window_set_user_time (window, timestamp);
            }

          tmp = tmp->next;
        }

      g_slist_free (windows);
    }
}

void
cobiwm_display_set_input_focus_window (CobiwmDisplay *display,
                                     CobiwmWindow  *window,
                                     gboolean     focus_frame,
                                     guint32      timestamp)
{
  request_xserver_input_focus_change (display,
                                      window->screen,
                                      window,
                                      focus_frame ? window->frame->xwindow : window->xwindow,
                                      timestamp);
}

void
cobiwm_display_set_input_focus_xwindow (CobiwmDisplay *display,
                                      CobiwmScreen  *screen,
                                      Window       window,
                                      guint32      timestamp)
{
  request_xserver_input_focus_change (display,
                                      screen,
                                      NULL,
                                      window,
                                      timestamp);
}

void
cobiwm_display_focus_the_no_focus_window (CobiwmDisplay *display,
                                        CobiwmScreen  *screen,
                                        guint32      timestamp)
{
  request_xserver_input_focus_change (display,
                                      screen,
                                      NULL,
                                      screen->no_focus_window,
                                      timestamp);
}

void
cobiwm_display_remove_autoraise_callback (CobiwmDisplay *display)
{
  if (display->autoraise_timeout_id != 0)
    {
      g_source_remove (display->autoraise_timeout_id);
      display->autoraise_timeout_id = 0;
      display->autoraise_window = NULL;
    }
}

void
cobiwm_display_overlay_key_activate (CobiwmDisplay *display)
{
  g_signal_emit (display, display_signals[OVERLAY_KEY], 0);
}

void
cobiwm_display_accelerator_activate (CobiwmDisplay     *display,
                                   guint            action,
                                   ClutterKeyEvent *event)
{
  g_signal_emit (display, display_signals[ACCELERATOR_ACTIVATED],
                 0, action,
                 clutter_input_device_get_device_id (event->device),
                 event->time);
}

gboolean
cobiwm_display_modifiers_accelerator_activate (CobiwmDisplay *display)
{
  gboolean freeze;

  g_signal_emit (display, display_signals[MODIFIERS_ACCELERATOR_ACTIVATED], 0, &freeze);

  return freeze;
}

/**
 * cobiwm_display_get_xinput_opcode: (skip)
 * @display: a #CobiwmDisplay
 *
 */
int
cobiwm_display_get_xinput_opcode (CobiwmDisplay *display)
{
  return display->xinput_opcode;
}

/**
 * cobiwm_display_supports_extended_barriers:
 * @display: a #CobiwmDisplay
 *
 * Returns: whether pointer barriers can be supported.
 *
 * When running as an X compositor the X server needs XInput 2
 * version 2.3. When running as a display server it is supported
 * when running on the native backend.
 *
 * Clients should use this method to determine whether their
 * interfaces should depend on new barrier features.
 */
gboolean
cobiwm_display_supports_extended_barriers (CobiwmDisplay *display)
{
#ifdef HAVE_NATIVE_BACKEND
  if (COBIWM_IS_BACKEND_NATIVE (cobiwm_get_backend ()))
    return TRUE;
#endif

  if (COBIWM_IS_BACKEND_X11 (cobiwm_get_backend ()))
    {
      return (COBIWM_DISPLAY_HAS_XINPUT_23 (display) &&
              !cobiwm_is_wayland_compositor());
    }

  g_assert_not_reached ();
}

/**
 * cobiwm_display_get_xdisplay: (skip)
 * @display: a #CobiwmDisplay
 *
 */
Display *
cobiwm_display_get_xdisplay (CobiwmDisplay *display)
{
  return display->xdisplay;
}

/**
 * cobiwm_display_get_compositor: (skip)
 * @display: a #CobiwmDisplay
 *
 */
CobiwmCompositor *
cobiwm_display_get_compositor (CobiwmDisplay *display)
{
  return display->compositor;
}

gboolean
cobiwm_display_has_shape (CobiwmDisplay *display)
{
  return COBIWM_DISPLAY_HAS_SHAPE (display);
}

/**
 * cobiwm_display_get_focus_window:
 * @display: a #CobiwmDisplay
 *
 * Get our best guess as to the "currently" focused window (that is,
 * the window that we expect will be focused at the point when the X
 * server processes our next request).
 *
 * Return Value: (transfer none): The current focus window
 */
CobiwmWindow *
cobiwm_display_get_focus_window (CobiwmDisplay *display)
{
  return display->focus_window;
}

int
cobiwm_display_get_damage_event_base (CobiwmDisplay *display)
{
  return display->damage_event_base;
}

int
cobiwm_display_get_shape_event_base (CobiwmDisplay *display)
{
  return display->shape_event_base;
}

/**
 * cobiwm_display_clear_mouse_mode:
 * @display: a #CobiwmDisplay
 *
 * Sets the mouse-mode flag to %FALSE, which means that motion events are
 * no longer ignored in mouse or sloppy focus.
 * This is an internal function. It should be used only for reimplementing
 * keybindings, and only in a manner compatible with core code.
 */
void
cobiwm_display_clear_mouse_mode (CobiwmDisplay *display)
{
  display->mouse_mode = FALSE;
}

Cursor
cobiwm_display_create_x_cursor (CobiwmDisplay *display,
                              CobiwmCursor   cursor)
{
  return cobiwm_cursor_create_x_cursor (display->xdisplay, cursor);
}

CobiwmGestureTracker *
cobiwm_display_get_gesture_tracker (CobiwmDisplay *display)
{
  return display->gesture_tracker;
}

gboolean
cobiwm_display_show_restart_message (CobiwmDisplay *display,
                                   const char  *message)
{
  gboolean result = FALSE;

  g_signal_emit (display,
                 display_signals[SHOW_RESTART_MESSAGE], 0,
                 message, &result);

  return result;
}

gboolean
cobiwm_display_request_restart (CobiwmDisplay *display)
{
  gboolean result = FALSE;

  g_signal_emit (display,
                 display_signals[RESTART], 0,
                 &result);

  return result;
}

gboolean
cobiwm_display_show_resize_popup (CobiwmDisplay *display,
                                gboolean show,
                                CobiwmRectangle *rect,
                                int display_w,
                                int display_h)
{
  gboolean result = FALSE;

  g_signal_emit (display,
                 display_signals[SHOW_RESIZE_POPUP], 0,
                 show, rect, display_w, display_h, &result);

  return result;
}

/**
 * cobiwm_display_is_pointer_emulating_sequence:
 * @display: the display
 * @sequence: (nullable): a #ClutterEventSequence
 *
 * Tells whether the event sequence is the used for pointer emulation
 * and single-touch interaction.
 *
 * Returns: #TRUE if the sequence emulates pointer behavior
 **/
gboolean
cobiwm_display_is_pointer_emulating_sequence (CobiwmDisplay          *display,
                                            ClutterEventSequence *sequence)
{
  if (!sequence)
    return FALSE;

  return display->pointer_emulating_sequence == sequence;
}

void
cobiwm_display_set_alarm_filter (CobiwmDisplay    *display,
                               CobiwmAlarmFilter filter,
                               gpointer        data)
{
  g_return_if_fail (filter == NULL || display->alarm_filter == NULL);

  display->alarm_filter = filter;
  display->alarm_filter_data = data;
}
