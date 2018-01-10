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

#include "config.h"

#include <string.h>
#include <stdlib.h>

#include "cobiwm-backend-x11.h"

#include <clutter/x11/clutter-x11.h>

#include <X11/extensions/sync.h>
#include <X11/XKBlib.h>
#include <X11/extensions/XKBrules.h>
#include <X11/Xlib-xcb.h>
#include <xkbcommon/xkbcommon-x11.h>

#include "cobiwm-idle-monitor-xsync.h"
#include "cobiwm-monitor-manager-xrandr.h"
#include "backends/cobiwm-monitor-manager-dummy.h"
#include "backends/x11/nested/cobiwm-cursor-renderer-x11-nested.h"
#include "cobiwm-cursor-renderer-x11.h"
#ifdef HAVE_WAYLAND
#include "wayland/cobiwm-wayland.h"
#endif

#include <util.h>
#include "display-private.h"
#include "compositor/compositor-private.h"

typedef enum {
  /* We're a traditional CM running under the host. */
  COBIWM_BACKEND_X11_MODE_COMPOSITOR,

  /* We're a nested X11 client */
  COBIWM_BACKEND_X11_MODE_NESTED,
} CobiwmBackendX11Mode;

struct _CobiwmBackendX11Private
{
  /* The host X11 display */
  Display *xdisplay;
  xcb_connection_t *xcb;
  GSource *source;

  CobiwmBackendX11Mode mode;

  int xsync_event_base;
  int xsync_error_base;

  int xinput_opcode;
  int xinput_event_base;
  int xinput_error_base;
  Time latest_evtime;

  uint8_t xkb_event_base;
  uint8_t xkb_error_base;

  struct xkb_keymap *keymap;
  gchar *keymap_layouts;
  gchar *keymap_variants;
  gchar *keymap_options;
  int locked_group;
};
typedef struct _CobiwmBackendX11Private CobiwmBackendX11Private;

static void apply_keymap (CobiwmBackendX11 *x11);

G_DEFINE_TYPE_WITH_PRIVATE (CobiwmBackendX11, cobiwm_backend_x11, COBIWM_TYPE_BACKEND);

static void
handle_alarm_notify (CobiwmBackend *backend,
                     XEvent      *event)
{
  GHashTableIter iter;
  gpointer value;

  g_hash_table_iter_init (&iter, backend->device_monitors);
  while (g_hash_table_iter_next (&iter, NULL, &value))
    {
      CobiwmIdleMonitor *device_monitor = COBIWM_IDLE_MONITOR (value);
      cobiwm_idle_monitor_xsync_handle_xevent (device_monitor, (XSyncAlarmNotifyEvent*) event);
    }
}

static void
translate_device_event (CobiwmBackendX11 *x11,
                        XIDeviceEvent  *device_event)
{
  CobiwmBackendX11Private *priv = cobiwm_backend_x11_get_instance_private (x11);
  Window stage_window = cobiwm_backend_x11_get_xwindow (x11);

  if (device_event->event != stage_window)
    {
      /* This codepath should only ever trigger as an X11 compositor,
       * and never under nested, as under nested all backend events
       * should be reported with respect to the stage window. */
      g_assert (priv->mode == COBIWM_BACKEND_X11_MODE_COMPOSITOR);

      device_event->event = stage_window;

      /* As an X11 compositor, the stage window is always at 0,0, so
       * using root coordinates will give us correct stage coordinates
       * as well... */
      device_event->event_x = device_event->root_x;
      device_event->event_y = device_event->root_y;
    }

  if (!device_event->send_event && device_event->time != CurrentTime)
    {
      if (device_event->time < priv->latest_evtime)
        {
          /* Emulated pointer events received after XIRejectTouch is received
           * on a passive touch grab will contain older timestamps, update those
           * so we dont get InvalidTime at grabs.
           */
          device_event->time = priv->latest_evtime;
        }

      /* Update the internal latest evtime, for any possible later use */
      priv->latest_evtime = device_event->time;
    }
}

static void
translate_crossing_event (CobiwmBackendX11 *x11,
                          XIEnterEvent   *enter_event)
{
  CobiwmBackendX11Private *priv = cobiwm_backend_x11_get_instance_private (x11);

  /* Throw out weird events generated by grabs. */
  if (enter_event->mode == XINotifyGrab ||
      enter_event->mode == XINotifyUngrab)
    {
      enter_event->event = None;
      return;
    }

  Window stage_window = cobiwm_backend_x11_get_xwindow (x11);
  if (enter_event->event != stage_window &&
      priv->mode == COBIWM_BACKEND_X11_MODE_COMPOSITOR)
    {
      enter_event->event = cobiwm_backend_x11_get_xwindow (x11);
      enter_event->event_x = enter_event->root_x;
      enter_event->event_y = enter_event->root_y;
    }
}

static void
handle_device_change (CobiwmBackendX11 *x11,
                      XIEvent        *event)
{
  XIDeviceChangedEvent *device_changed;

  if (event->evtype != XI_DeviceChanged)
    return;

  device_changed = (XIDeviceChangedEvent *) event;

  if (device_changed->reason != XISlaveSwitch)
    return;

  cobiwm_backend_update_last_device (COBIWM_BACKEND (x11),
                                   device_changed->sourceid);
}

/* Clutter makes the assumption that there is only one X window
 * per stage, which is a valid assumption to make for a generic
 * application toolkit. As such, it will ignore any events sent
 * to the a stage that isn't its X window.
 *
 * When running as an X window manager, we need to respond to
 * events from lots of windows. Trick Clutter into translating
 * these events by pretending we got an event on the stage window.
 */
static void
maybe_spoof_event_as_stage_event (CobiwmBackendX11 *x11,
                                  XIEvent        *input_event)
{
  switch (input_event->evtype)
    {
    case XI_Motion:
    case XI_ButtonPress:
    case XI_ButtonRelease:
    case XI_KeyPress:
    case XI_KeyRelease:
    case XI_TouchBegin:
    case XI_TouchUpdate:
    case XI_TouchEnd:
      translate_device_event (x11, (XIDeviceEvent *) input_event);
      break;
    case XI_Enter:
    case XI_Leave:
      translate_crossing_event (x11, (XIEnterEvent *) input_event);
      break;
    default:
      break;
    }
}

static void
handle_input_event (CobiwmBackendX11 *x11,
                    XEvent         *event)
{
  CobiwmBackendX11Private *priv = cobiwm_backend_x11_get_instance_private (x11);

  if (event->type == GenericEvent &&
      event->xcookie.extension == priv->xinput_opcode)
    {
      XIEvent *input_event = (XIEvent *) event->xcookie.data;

      if (input_event->evtype == XI_DeviceChanged)
        handle_device_change (x11, input_event);
      else
        maybe_spoof_event_as_stage_event (x11, input_event);
    }
}

static void
keymap_changed (CobiwmBackend *backend)
{
  CobiwmBackendX11 *x11 = COBIWM_BACKEND_X11 (backend);
  CobiwmBackendX11Private *priv = cobiwm_backend_x11_get_instance_private (x11);

  if (priv->keymap)
    {
      xkb_keymap_unref (priv->keymap);
      priv->keymap = NULL;
    }

  g_signal_emit_by_name (backend, "keymap-changed", 0);
}

static void
handle_host_xevent (CobiwmBackend *backend,
                    XEvent      *event)
{
  CobiwmBackendX11 *x11 = COBIWM_BACKEND_X11 (backend);
  CobiwmBackendX11Private *priv = cobiwm_backend_x11_get_instance_private (x11);

  XGetEventData (priv->xdisplay, &event->xcookie);

  if (priv->mode == COBIWM_BACKEND_X11_MODE_NESTED && event->type == FocusIn)
    {
#ifdef HAVE_WAYLAND
      Window xwin = cobiwm_backend_x11_get_xwindow(x11);
      XEvent xev;

      if (event->xfocus.window == xwin)
        {
          /* Since we've selected for KeymapStateMask, every FocusIn is followed immediately
           * by a KeymapNotify event */
          XMaskEvent(priv->xdisplay, KeymapStateMask, &xev);
          CobiwmWaylandCompositor *compositor = cobiwm_wayland_compositor_get_default ();
          cobiwm_wayland_compositor_update_key_state (compositor, xev.xkeymap.key_vector, 32, 8);
        }
#else
      g_assert_not_reached ();
#endif
    }

  if (event->type == (priv->xsync_event_base + XSyncAlarmNotify))
    handle_alarm_notify (backend, event);

  if (event->type == priv->xkb_event_base)
    {
      XkbEvent *xkb_ev = (XkbEvent *) event;

      if (xkb_ev->any.device == COBIWM_VIRTUAL_CORE_KEYBOARD_ID)
        {
          switch (xkb_ev->any.xkb_type)
            {
            case XkbNewKeyboardNotify:
            case XkbMapNotify:
              keymap_changed (backend);
              break;
            case XkbStateNotify:
              if (xkb_ev->state.changed & XkbGroupLockMask)
                {
                  if (priv->locked_group != xkb_ev->state.locked_group)
                    XkbLockGroup (priv->xdisplay, XkbUseCoreKbd, priv->locked_group);
                }
              break;
            default:
              break;
            }
        }
    }

  {
    CobiwmMonitorManager *manager = cobiwm_backend_get_monitor_manager (backend);
    if (!(COBIWM_IS_MONITOR_MANAGER_XRANDR (manager) &&
        cobiwm_monitor_manager_xrandr_handle_xevent (COBIWM_MONITOR_MANAGER_XRANDR (manager), event))) {
      handle_input_event (x11, event);
      clutter_x11_handle_event (event);
    }
  }

  XFreeEventData (priv->xdisplay, &event->xcookie);
}

typedef struct {
  GSource base;
  GPollFD event_poll_fd;
  CobiwmBackend *backend;
} XEventSource;

static gboolean
x_event_source_prepare (GSource *source,
                        int     *timeout)
{
  XEventSource *x_source = (XEventSource *) source;
  CobiwmBackend *backend = x_source->backend;
  CobiwmBackendX11 *x11 = COBIWM_BACKEND_X11 (backend);
  CobiwmBackendX11Private *priv = cobiwm_backend_x11_get_instance_private (x11);

  *timeout = -1;

  return XPending (priv->xdisplay);
}

static gboolean
x_event_source_check (GSource *source)
{
  XEventSource *x_source = (XEventSource *) source;
  CobiwmBackend *backend = x_source->backend;
  CobiwmBackendX11 *x11 = COBIWM_BACKEND_X11 (backend);
  CobiwmBackendX11Private *priv = cobiwm_backend_x11_get_instance_private (x11);

  return XPending (priv->xdisplay);
}

static gboolean
x_event_source_dispatch (GSource     *source,
                         GSourceFunc  callback,
                         gpointer     user_data)
{
  XEventSource *x_source = (XEventSource *) source;
  CobiwmBackend *backend = x_source->backend;
  CobiwmBackendX11 *x11 = COBIWM_BACKEND_X11 (backend);
  CobiwmBackendX11Private *priv = cobiwm_backend_x11_get_instance_private (x11);

  while (XPending (priv->xdisplay))
    {
      XEvent event;

      XNextEvent (priv->xdisplay, &event);

      handle_host_xevent (backend, &event);
    }

  return TRUE;
}

static GSourceFuncs x_event_funcs = {
  x_event_source_prepare,
  x_event_source_check,
  x_event_source_dispatch,
};

static GSource *
x_event_source_new (CobiwmBackend *backend)
{
  CobiwmBackendX11 *x11 = COBIWM_BACKEND_X11 (backend);
  CobiwmBackendX11Private *priv = cobiwm_backend_x11_get_instance_private (x11);
  GSource *source;
  XEventSource *x_source;

  source = g_source_new (&x_event_funcs, sizeof (XEventSource));
  x_source = (XEventSource *) source;
  x_source->backend = backend;
  x_source->event_poll_fd.fd = ConnectionNumber (priv->xdisplay);
  x_source->event_poll_fd.events = G_IO_IN;
  g_source_add_poll (source, &x_source->event_poll_fd);

  g_source_attach (source, NULL);
  return source;
}

static void
take_touch_grab (CobiwmBackend *backend)
{
  CobiwmBackendX11 *x11 = COBIWM_BACKEND_X11 (backend);
  CobiwmBackendX11Private *priv = cobiwm_backend_x11_get_instance_private (x11);
  unsigned char mask_bits[XIMaskLen (XI_LASTEVENT)] = { 0 };
  XIEventMask mask = { COBIWM_VIRTUAL_CORE_POINTER_ID, sizeof (mask_bits), mask_bits };
  XIGrabModifiers mods = { XIAnyModifier, 0 };

  XISetMask (mask.mask, XI_TouchBegin);
  XISetMask (mask.mask, XI_TouchUpdate);
  XISetMask (mask.mask, XI_TouchEnd);

  XIGrabTouchBegin (priv->xdisplay, COBIWM_VIRTUAL_CORE_POINTER_ID,
                    DefaultRootWindow (priv->xdisplay),
                    False, &mask, 1, &mods);
}

static void
on_device_added (ClutterDeviceManager *device_manager,
                 ClutterInputDevice   *device,
                 gpointer              user_data)
{
  CobiwmBackendX11 *x11 = COBIWM_BACKEND_X11 (user_data);

  if (clutter_input_device_get_device_type (device) == CLUTTER_KEYBOARD_DEVICE)
    apply_keymap (x11);
}

static void
cobiwm_backend_x11_post_init (CobiwmBackend *backend)
{
  CobiwmBackendX11 *x11 = COBIWM_BACKEND_X11 (backend);
  CobiwmBackendX11Private *priv = cobiwm_backend_x11_get_instance_private (x11);
  int major, minor;
  gboolean has_xi = FALSE;

  priv->xdisplay = clutter_x11_get_default_display ();

  priv->source = x_event_source_new (backend);

  if (!XSyncQueryExtension (priv->xdisplay, &priv->xsync_event_base, &priv->xsync_error_base) ||
      !XSyncInitialize (priv->xdisplay, &major, &minor))
    cobiwm_fatal ("Could not initialize XSync");

  if (XQueryExtension (priv->xdisplay,
                       "XInputExtension",
                       &priv->xinput_opcode,
                       &priv->xinput_error_base,
                       &priv->xinput_event_base))
    {
      major = 2; minor = 3;
      if (XIQueryVersion (priv->xdisplay, &major, &minor) == Success)
        {
          int version = (major * 10) + minor;
          if (version >= 22)
            has_xi = TRUE;
        }
    }

  if (!has_xi)
    cobiwm_fatal ("X server doesn't have the XInput extension, version 2.2 or newer\n");

  /* We only take the passive touch grab if we are a X11 compositor */
  if (priv->mode == COBIWM_BACKEND_X11_MODE_COMPOSITOR)
    take_touch_grab (backend);

  priv->xcb = XGetXCBConnection (priv->xdisplay);
  if (!xkb_x11_setup_xkb_extension (priv->xcb,
                                    XKB_X11_MIN_MAJOR_XKB_VERSION,
                                    XKB_X11_MIN_MINOR_XKB_VERSION,
                                    XKB_X11_SETUP_XKB_EXTENSION_NO_FLAGS,
                                    NULL, NULL,
                                    &priv->xkb_event_base,
                                    &priv->xkb_error_base))
    cobiwm_fatal ("X server doesn't have the XKB extension, version %d.%d or newer\n",
                XKB_X11_MIN_MAJOR_XKB_VERSION, XKB_X11_MIN_MINOR_XKB_VERSION);

  g_signal_connect_object (clutter_device_manager_get_default (), "device-added",
                           G_CALLBACK (on_device_added), backend, 0);

  COBIWM_BACKEND_CLASS (cobiwm_backend_x11_parent_class)->post_init (backend);
}

static CobiwmIdleMonitor *
cobiwm_backend_x11_create_idle_monitor (CobiwmBackend *backend,
                                      int          device_id)
{
  return g_object_new (COBIWM_TYPE_IDLE_MONITOR_XSYNC,
                       "device-id", device_id,
                       NULL);
}

static CobiwmMonitorManager *
cobiwm_backend_x11_create_monitor_manager (CobiwmBackend *backend)
{
  CobiwmBackendX11 *x11 = COBIWM_BACKEND_X11 (backend);
  CobiwmBackendX11Private *priv = cobiwm_backend_x11_get_instance_private (x11);

  switch (priv->mode)
    {
    case COBIWM_BACKEND_X11_MODE_COMPOSITOR:
      return g_object_new (COBIWM_TYPE_MONITOR_MANAGER_XRANDR, NULL);
    case COBIWM_BACKEND_X11_MODE_NESTED:
      return g_object_new (COBIWM_TYPE_MONITOR_MANAGER_DUMMY, NULL);
    default:
      g_assert_not_reached ();
    }
}

static CobiwmCursorRenderer *
cobiwm_backend_x11_create_cursor_renderer (CobiwmBackend *backend)
{
  CobiwmBackendX11 *x11 = COBIWM_BACKEND_X11 (backend);
  CobiwmBackendX11Private *priv = cobiwm_backend_x11_get_instance_private (x11);

  switch (priv->mode)
    {
    case COBIWM_BACKEND_X11_MODE_COMPOSITOR:
      return g_object_new (COBIWM_TYPE_CURSOR_RENDERER_X11, NULL);
      break;
    case COBIWM_BACKEND_X11_MODE_NESTED:
      return g_object_new (COBIWM_TYPE_CURSOR_RENDERER_X11_NESTED, NULL);
      break;
    default:
      g_assert_not_reached ();
    }
}

static gboolean
cobiwm_backend_x11_grab_device (CobiwmBackend *backend,
                              int          device_id,
                              uint32_t     timestamp)
{
  CobiwmBackendX11 *x11 = COBIWM_BACKEND_X11 (backend);
  CobiwmBackendX11Private *priv = cobiwm_backend_x11_get_instance_private (x11);
  unsigned char mask_bits[XIMaskLen (XI_LASTEVENT)] = { 0 };
  XIEventMask mask = { XIAllMasterDevices, sizeof (mask_bits), mask_bits };
  int ret;

  if (timestamp != CurrentTime)
    timestamp = MAX (timestamp, priv->latest_evtime);

  XISetMask (mask.mask, XI_ButtonPress);
  XISetMask (mask.mask, XI_ButtonRelease);
  XISetMask (mask.mask, XI_Enter);
  XISetMask (mask.mask, XI_Leave);
  XISetMask (mask.mask, XI_Motion);
  XISetMask (mask.mask, XI_KeyPress);
  XISetMask (mask.mask, XI_KeyRelease);

  ret = XIGrabDevice (priv->xdisplay, device_id,
                      cobiwm_backend_x11_get_xwindow (x11),
                      timestamp,
                      None,
                      XIGrabModeAsync, XIGrabModeAsync,
                      False, /* owner_events */
                      &mask);

  return (ret == Success);
}

static gboolean
cobiwm_backend_x11_ungrab_device (CobiwmBackend *backend,
                                int          device_id,
                                uint32_t     timestamp)
{
  CobiwmBackendX11 *x11 = COBIWM_BACKEND_X11 (backend);
  CobiwmBackendX11Private *priv = cobiwm_backend_x11_get_instance_private (x11);
  int ret;

  ret = XIUngrabDevice (priv->xdisplay, device_id, timestamp);

  return (ret == Success);
}

static void
cobiwm_backend_x11_warp_pointer (CobiwmBackend *backend,
                               int          x,
                               int          y)
{
  CobiwmBackendX11 *x11 = COBIWM_BACKEND_X11 (backend);
  CobiwmBackendX11Private *priv = cobiwm_backend_x11_get_instance_private (x11);

  XIWarpPointer (priv->xdisplay,
                 COBIWM_VIRTUAL_CORE_POINTER_ID,
                 None,
                 cobiwm_backend_x11_get_xwindow (x11),
                 0, 0, 0, 0,
                 x, y);
}

static void
get_xkbrf_var_defs (Display           *xdisplay,
                    const char        *layouts,
                    const char        *variants,
                    const char        *options,
                    char             **rules_p,
                    XkbRF_VarDefsRec  *var_defs)
{
  char *rules = NULL;

  /* Get it from the X property or fallback on defaults */
  if (!XkbRF_GetNamesProp (xdisplay, &rules, var_defs) || !rules)
    {
      rules = strdup (DEFAULT_XKB_RULES_FILE);
      var_defs->model = strdup (DEFAULT_XKB_MODEL);
      var_defs->layout = NULL;
      var_defs->variant = NULL;
      var_defs->options = NULL;
    }

  /* Swap in our new options... */
  free (var_defs->layout);
  var_defs->layout = strdup (layouts);
  free (var_defs->variant);
  var_defs->variant = strdup (variants);
  free (var_defs->options);
  var_defs->options = strdup (options);

  /* Sometimes, the property is a file path, and sometimes it's
     not. Normalize it so it's always a file path. */
  if (rules[0] == '/')
    *rules_p = g_strdup (rules);
  else
    *rules_p = g_build_filename (XKB_BASE, "rules", rules, NULL);

  free (rules);
}

static void
free_xkbrf_var_defs (XkbRF_VarDefsRec *var_defs)
{
  free (var_defs->model);
  free (var_defs->layout);
  free (var_defs->variant);
  free (var_defs->options);
}

static void
free_xkb_component_names (XkbComponentNamesRec *p)
{
  free (p->keymap);
  free (p->keycodes);
  free (p->types);
  free (p->compat);
  free (p->symbols);
  free (p->geometry);
}

static void
upload_xkb_description (Display              *xdisplay,
                        const gchar          *rules_file_path,
                        XkbRF_VarDefsRec     *var_defs,
                        XkbComponentNamesRec *comp_names)
{
  XkbDescRec *xkb_desc;
  gchar *rules_file;

  /* Upload it to the X server using the same method as setxkbmap */
  xkb_desc = XkbGetKeyboardByName (xdisplay,
                                   XkbUseCoreKbd,
                                   comp_names,
                                   XkbGBN_AllComponentsMask,
                                   XkbGBN_AllComponentsMask &
                                   (~XkbGBN_GeometryMask), True);
  if (!xkb_desc)
    {
      g_warning ("Couldn't upload new XKB keyboard description");
      return;
    }

  XkbFreeKeyboard (xkb_desc, 0, True);

  rules_file = g_path_get_basename (rules_file_path);

  if (!XkbRF_SetNamesProp (xdisplay, rules_file, var_defs))
    g_warning ("Couldn't update the XKB root window property");

  g_free (rules_file);
}

static void
apply_keymap (CobiwmBackendX11 *x11)
{
  CobiwmBackendX11Private *priv = cobiwm_backend_x11_get_instance_private (x11);
  XkbRF_RulesRec *xkb_rules;
  XkbRF_VarDefsRec xkb_var_defs = { 0 };
  gchar *rules_file_path;

  if (!priv->keymap_layouts ||
      !priv->keymap_variants ||
      !priv->keymap_options)
    return;

  get_xkbrf_var_defs (priv->xdisplay,
                      priv->keymap_layouts,
                      priv->keymap_variants,
                      priv->keymap_options,
                      &rules_file_path,
                      &xkb_var_defs);

  xkb_rules = XkbRF_Load (rules_file_path, NULL, True, True);
  if (xkb_rules)
    {
      XkbComponentNamesRec xkb_comp_names = { 0 };

      XkbRF_GetComponents (xkb_rules, &xkb_var_defs, &xkb_comp_names);
      upload_xkb_description (priv->xdisplay, rules_file_path, &xkb_var_defs, &xkb_comp_names);

      free_xkb_component_names (&xkb_comp_names);
      XkbRF_Free (xkb_rules, True);
    }
  else
    {
      g_warning ("Couldn't load XKB rules");
    }

  free_xkbrf_var_defs (&xkb_var_defs);
  g_free (rules_file_path);
}

static void
cobiwm_backend_x11_set_keymap (CobiwmBackend *backend,
                             const char  *layouts,
                             const char  *variants,
                             const char  *options)
{
  CobiwmBackendX11 *x11 = COBIWM_BACKEND_X11 (backend);
  CobiwmBackendX11Private *priv = cobiwm_backend_x11_get_instance_private (x11);

  g_free (priv->keymap_layouts);
  priv->keymap_layouts = g_strdup (layouts);
  g_free (priv->keymap_variants);
  priv->keymap_variants = g_strdup (variants);
  g_free (priv->keymap_options);
  priv->keymap_options = g_strdup (options);

  apply_keymap (x11);
}

static struct xkb_keymap *
cobiwm_backend_x11_get_keymap (CobiwmBackend *backend)
{
  CobiwmBackendX11 *x11 = COBIWM_BACKEND_X11 (backend);
  CobiwmBackendX11Private *priv = cobiwm_backend_x11_get_instance_private (x11);

  if (priv->keymap == NULL)
    {
      struct xkb_context *context = xkb_context_new (XKB_CONTEXT_NO_FLAGS);
      priv->keymap = xkb_x11_keymap_new_from_device (context,
                                                     priv->xcb,
                                                     xkb_x11_get_core_keyboard_device_id (priv->xcb),
                                                     XKB_KEYMAP_COMPILE_NO_FLAGS);
      if (priv->keymap == NULL)
        priv->keymap = xkb_keymap_new_from_names (context, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);

      xkb_context_unref (context);
    }

  return priv->keymap;
}

static void
cobiwm_backend_x11_lock_layout_group (CobiwmBackend *backend,
                                    guint        idx)
{
  CobiwmBackendX11 *x11 = COBIWM_BACKEND_X11 (backend);
  CobiwmBackendX11Private *priv = cobiwm_backend_x11_get_instance_private (x11);

  priv->locked_group = idx;
  XkbLockGroup (priv->xdisplay, XkbUseCoreKbd, idx);
}

static void
cobiwm_backend_x11_update_screen_size (CobiwmBackend *backend,
                                     int width, int height)
{
  CobiwmBackendX11 *x11 = COBIWM_BACKEND_X11 (backend);
  CobiwmBackendX11Private *priv = cobiwm_backend_x11_get_instance_private (x11);

  if (priv->mode == COBIWM_BACKEND_X11_MODE_NESTED)
    {
      /* For a nested wayland session, we want to go through Clutter to update the
       * toplevel window size, rather than doing it directly.
       */
      COBIWM_BACKEND_CLASS (cobiwm_backend_x11_parent_class)->update_screen_size (backend, width, height);
    }
  else
    {
      Window xwin = cobiwm_backend_x11_get_xwindow (x11);
      XResizeWindow (priv->xdisplay, xwin, width, height);
    }
}

static void
cobiwm_backend_x11_select_stage_events (CobiwmBackend *backend)
{
  CobiwmBackendX11 *x11 = COBIWM_BACKEND_X11 (backend);
  CobiwmBackendX11Private *priv = cobiwm_backend_x11_get_instance_private (x11);
  Window xwin = cobiwm_backend_x11_get_xwindow (x11);
  unsigned char mask_bits[XIMaskLen (XI_LASTEVENT)] = { 0 };
  XIEventMask mask = { XIAllMasterDevices, sizeof (mask_bits), mask_bits };

  XISetMask (mask.mask, XI_KeyPress);
  XISetMask (mask.mask, XI_KeyRelease);
  XISetMask (mask.mask, XI_ButtonPress);
  XISetMask (mask.mask, XI_ButtonRelease);
  XISetMask (mask.mask, XI_Enter);
  XISetMask (mask.mask, XI_Leave);
  XISetMask (mask.mask, XI_FocusIn);
  XISetMask (mask.mask, XI_FocusOut);
  XISetMask (mask.mask, XI_Motion);

  if (priv->mode == COBIWM_BACKEND_X11_MODE_NESTED)
    {
      /* When we're an X11 compositor, we can't take these events or else
       * replaying events from our passive root window grab will cause
       * them to come back to us.
       *
       * When we're a nested application, we want to behave like any other
       * application, so select these events like normal apps do.
       */
      XISetMask (mask.mask, XI_TouchBegin);
      XISetMask (mask.mask, XI_TouchEnd);
      XISetMask (mask.mask, XI_TouchUpdate);
    }

  XISelectEvents (priv->xdisplay, xwin, &mask, 1);

  if (priv->mode == COBIWM_BACKEND_X11_MODE_NESTED)
    {
      /* We have no way of tracking key changes when the stage doesn't have
       * focus, so we select for KeymapStateMask so that we get a complete
       * dump of the keyboard state in a KeymapNotify event that immediately
       * follows each FocusIn (and EnterNotify, but we ignore that.)
       */
      XWindowAttributes xwa;

      XGetWindowAttributes(priv->xdisplay, xwin, &xwa);
      XSelectInput(priv->xdisplay, xwin,
                   xwa.your_event_mask | FocusChangeMask | KeymapStateMask);
    }
}

static void
cobiwm_backend_x11_class_init (CobiwmBackendX11Class *klass)
{
  CobiwmBackendClass *backend_class = COBIWM_BACKEND_CLASS (klass);

  backend_class->post_init = cobiwm_backend_x11_post_init;
  backend_class->create_idle_monitor = cobiwm_backend_x11_create_idle_monitor;
  backend_class->create_monitor_manager = cobiwm_backend_x11_create_monitor_manager;
  backend_class->create_cursor_renderer = cobiwm_backend_x11_create_cursor_renderer;
  backend_class->grab_device = cobiwm_backend_x11_grab_device;
  backend_class->ungrab_device = cobiwm_backend_x11_ungrab_device;
  backend_class->warp_pointer = cobiwm_backend_x11_warp_pointer;
  backend_class->set_keymap = cobiwm_backend_x11_set_keymap;
  backend_class->get_keymap = cobiwm_backend_x11_get_keymap;
  backend_class->lock_layout_group = cobiwm_backend_x11_lock_layout_group;
  backend_class->update_screen_size = cobiwm_backend_x11_update_screen_size;
  backend_class->select_stage_events = cobiwm_backend_x11_select_stage_events;
}

static void
cobiwm_backend_x11_init (CobiwmBackendX11 *x11)
{
  CobiwmBackendX11Private *priv = cobiwm_backend_x11_get_instance_private (x11);

  /* We do X11 event retrieval ourselves */
  clutter_x11_disable_event_retrieval ();

  if (cobiwm_is_wayland_compositor ())
    priv->mode = COBIWM_BACKEND_X11_MODE_NESTED;
  else
    priv->mode = COBIWM_BACKEND_X11_MODE_COMPOSITOR;
}

Display *
cobiwm_backend_x11_get_xdisplay (CobiwmBackendX11 *x11)
{
  CobiwmBackendX11Private *priv = cobiwm_backend_x11_get_instance_private (x11);

  return priv->xdisplay;
}

Window
cobiwm_backend_x11_get_xwindow (CobiwmBackendX11 *x11)
{
  ClutterActor *stage = cobiwm_backend_get_stage (COBIWM_BACKEND (x11));
  return clutter_x11_get_stage_window (CLUTTER_STAGE (stage));
}