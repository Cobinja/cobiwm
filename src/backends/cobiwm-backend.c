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

#include <stdlib.h>

#include <cobiwm-backend.h>
#include "cobiwm-backend-private.h"

#include "backends/x11/cobiwm-backend-x11.h"
#include "cobiwm-cursor-tracker-private.h"
#include "cobiwm-stage.h"

#ifdef HAVE_NATIVE_BACKEND
#include "backends/native/cobiwm-backend-native.h"
#endif

#include "backends/cobiwm-idle-monitor-private.h"

#include "backends/cobiwm-monitor-manager-dummy.h"

static CobiwmBackend *_backend;

/**
 * cobiwm_get_backend:
 *
 * Accessor for the singleton CobiwmBackend.
 *
 * Returns: (transfer none): The only #CobiwmBackend there is.
 */
CobiwmBackend *
cobiwm_get_backend (void)
{
  return _backend;
}

struct _CobiwmBackendPrivate
{
  CobiwmMonitorManager *monitor_manager;
  CobiwmCursorRenderer *cursor_renderer;

  ClutterActor *stage;

  guint device_update_idle_id;
};
typedef struct _CobiwmBackendPrivate CobiwmBackendPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (CobiwmBackend, cobiwm_backend, G_TYPE_OBJECT);

static void
cobiwm_backend_finalize (GObject *object)
{
  CobiwmBackend *backend = COBIWM_BACKEND (object);
  CobiwmBackendPrivate *priv = cobiwm_backend_get_instance_private (backend);

  g_clear_object (&priv->monitor_manager);

  if (priv->device_update_idle_id)
    g_source_remove (priv->device_update_idle_id);

  g_hash_table_destroy (backend->device_monitors);

  G_OBJECT_CLASS (cobiwm_backend_parent_class)->finalize (object);
}

static void
cobiwm_backend_sync_screen_size (CobiwmBackend *backend)
{
  CobiwmBackendPrivate *priv = cobiwm_backend_get_instance_private (backend);
  int width, height;

  cobiwm_monitor_manager_get_screen_size (priv->monitor_manager, &width, &height);

  COBIWM_BACKEND_GET_CLASS (backend)->update_screen_size (backend, width, height);
}

static void
center_pointer (CobiwmBackend *backend)
{
  CobiwmBackendPrivate *priv = cobiwm_backend_get_instance_private (backend);
  CobiwmMonitorInfo *monitors, *primary;
  guint n_monitors;

  monitors = cobiwm_monitor_manager_get_monitor_infos (priv->monitor_manager, &n_monitors);
  primary = &monitors[cobiwm_monitor_manager_get_primary_index (priv->monitor_manager)];
  cobiwm_backend_warp_pointer (backend,
                             primary->rect.x + primary->rect.width / 2,
                             primary->rect.y + primary->rect.height / 2);
}

static void
on_monitors_changed (CobiwmMonitorManager *monitors,
                     gpointer user_data)
{
  CobiwmBackend *backend = COBIWM_BACKEND (user_data);
  ClutterDeviceManager *manager = clutter_device_manager_get_default ();
  ClutterInputDevice *device = clutter_device_manager_get_core_device (manager, CLUTTER_POINTER_DEVICE);
  ClutterPoint point;

  cobiwm_backend_sync_screen_size (backend);

  if (clutter_input_device_get_coords (device, NULL, &point))
    {
      /* If we're outside all monitors, warp the pointer back inside */
      if (cobiwm_monitor_manager_get_monitor_at_point (monitors, point.x, point.y) < 0)
        center_pointer (backend);
    }
}

static CobiwmIdleMonitor *
cobiwm_backend_create_idle_monitor (CobiwmBackend *backend,
                                  int          device_id)
{
  return COBIWM_BACKEND_GET_CLASS (backend)->create_idle_monitor (backend, device_id);
}

static void
create_device_monitor (CobiwmBackend *backend,
                       int          device_id)
{
  CobiwmIdleMonitor *idle_monitor;

  g_assert (g_hash_table_lookup (backend->device_monitors, &device_id) == NULL);

  idle_monitor = cobiwm_backend_create_idle_monitor (backend, device_id);
  g_hash_table_insert (backend->device_monitors, &idle_monitor->device_id, idle_monitor);
}

static void
destroy_device_monitor (CobiwmBackend *backend,
                        int          device_id)
{
  g_hash_table_remove (backend->device_monitors, &device_id);
}

static void
on_device_added (ClutterDeviceManager *device_manager,
                 ClutterInputDevice   *device,
                 gpointer              user_data)
{
  CobiwmBackend *backend = COBIWM_BACKEND (user_data);
  int device_id = clutter_input_device_get_device_id (device);

  create_device_monitor (backend, device_id);
}

static inline gboolean
device_is_slave_touchscreen (ClutterInputDevice *device)
{
  return (clutter_input_device_get_device_mode (device) != CLUTTER_INPUT_MODE_MASTER &&
          clutter_input_device_get_device_type (device) == CLUTTER_TOUCHSCREEN_DEVICE);
}

static inline gboolean
check_has_pointing_device (ClutterDeviceManager *manager)
{
  const GSList *devices;

  devices = clutter_device_manager_peek_devices (manager);

  for (; devices; devices = devices->next)
    {
      ClutterInputDevice *device = devices->data;

      if (clutter_input_device_get_device_mode (device) == CLUTTER_INPUT_MODE_MASTER)
        continue;
      if (clutter_input_device_get_device_type (device) == CLUTTER_TOUCHSCREEN_DEVICE ||
          clutter_input_device_get_device_type (device) == CLUTTER_KEYBOARD_DEVICE)
        continue;

      return TRUE;
    }

  return FALSE;
}

static inline gboolean
check_has_slave_touchscreen (ClutterDeviceManager *manager)
{
  const GSList *devices;

  devices = clutter_device_manager_peek_devices (manager);

  for (; devices; devices = devices->next)
    {
      ClutterInputDevice *device = devices->data;

      if (clutter_input_device_get_device_mode (device) != CLUTTER_INPUT_MODE_MASTER &&
          clutter_input_device_get_device_type (device) == CLUTTER_TOUCHSCREEN_DEVICE)
        return TRUE;
    }

  return FALSE;
}

static void
on_device_removed (ClutterDeviceManager *device_manager,
                   ClutterInputDevice   *device,
                   gpointer              user_data)
{
  CobiwmBackend *backend = COBIWM_BACKEND (user_data);
  int device_id = clutter_input_device_get_device_id (device);

  destroy_device_monitor (backend, device_id);

  /* If the device the user last interacted goes away, check again pointer
   * visibility.
   */
  if (backend->current_device_id == device_id)
    {
      CobiwmCursorTracker *cursor_tracker = cobiwm_cursor_tracker_get_for_screen (NULL);
      gboolean has_touchscreen, has_pointing_device;
      ClutterInputDeviceType device_type;

      device_type = clutter_input_device_get_device_type (device);
      has_touchscreen = check_has_slave_touchscreen (device_manager);

      if (device_type == CLUTTER_TOUCHSCREEN_DEVICE && has_touchscreen)
        {
          /* There's more touchscreens left, keep the pointer hidden */
          cobiwm_cursor_tracker_set_pointer_visible (cursor_tracker, FALSE);
        }
      else if (device_type != CLUTTER_KEYBOARD_DEVICE)
        {
          has_pointing_device = check_has_pointing_device (device_manager);
          cobiwm_cursor_tracker_set_pointer_visible (cursor_tracker,
                                                   has_pointing_device &&
                                                   !has_touchscreen);
        }
    }
}

static CobiwmMonitorManager *
create_monitor_manager (CobiwmBackend *backend)
{
  if (g_getenv ("COBIWM_DUMMY_MONITORS"))
    return g_object_new (COBIWM_TYPE_MONITOR_MANAGER_DUMMY, NULL);

  return COBIWM_BACKEND_GET_CLASS (backend)->create_monitor_manager (backend);
}

static void
cobiwm_backend_real_post_init (CobiwmBackend *backend)
{
  CobiwmBackendPrivate *priv = cobiwm_backend_get_instance_private (backend);

  priv->stage = cobiwm_stage_new ();
  clutter_actor_realize (priv->stage);
  COBIWM_BACKEND_GET_CLASS (backend)->select_stage_events (backend);

  priv->monitor_manager = create_monitor_manager (backend);

  g_signal_connect (priv->monitor_manager, "monitors-changed",
                    G_CALLBACK (on_monitors_changed), backend);
  cobiwm_backend_sync_screen_size (backend);

  priv->cursor_renderer = COBIWM_BACKEND_GET_CLASS (backend)->create_cursor_renderer (backend);

  backend->device_monitors = g_hash_table_new_full (g_int_hash, g_int_equal,
                                                    NULL, (GDestroyNotify) g_object_unref);

  {
    CobiwmCursorTracker *cursor_tracker;
    ClutterDeviceManager *manager;
    gboolean has_touchscreen = FALSE;
    GSList *devices, *l;

    /* Create the core device monitor. */
    create_device_monitor (backend, 0);

    manager = clutter_device_manager_get_default ();
    g_signal_connect_object (manager, "device-added",
                             G_CALLBACK (on_device_added), backend, 0);
    g_signal_connect_object (manager, "device-removed",
                             G_CALLBACK (on_device_removed), backend, 0);

    devices = clutter_device_manager_list_devices (manager);

    for (l = devices; l != NULL; l = l->next)
      {
        ClutterInputDevice *device = l->data;
        on_device_added (manager, device, backend);
        has_touchscreen |= device_is_slave_touchscreen (device);
      }

    cursor_tracker = cobiwm_cursor_tracker_get_for_screen (NULL);
    cobiwm_cursor_tracker_set_pointer_visible (cursor_tracker, !has_touchscreen);

    g_slist_free (devices);
  }

  center_pointer (backend);
}

static CobiwmCursorRenderer *
cobiwm_backend_real_create_cursor_renderer (CobiwmBackend *backend)
{
  return cobiwm_cursor_renderer_new ();
}

static gboolean
cobiwm_backend_real_grab_device (CobiwmBackend *backend,
                               int          device_id,
                               uint32_t     timestamp)
{
  /* Do nothing */
  return TRUE;
}

static gboolean
cobiwm_backend_real_ungrab_device (CobiwmBackend *backend,
                                 int          device_id,
                                 uint32_t     timestamp)
{
  /* Do nothing */
  return TRUE;
}

static void
cobiwm_backend_real_update_screen_size (CobiwmBackend *backend,
                                      int width, int height)
{
  CobiwmBackendPrivate *priv = cobiwm_backend_get_instance_private (backend);

  clutter_actor_set_size (priv->stage, width, height);
}

static void
cobiwm_backend_real_select_stage_events (CobiwmBackend *backend)
{
  /* Do nothing */
}

static gboolean
cobiwm_backend_real_get_relative_motion_deltas (CobiwmBackend *backend,
                                             const         ClutterEvent *event,
                                             double        *dx,
                                             double        *dy,
                                             double        *dx_unaccel,
                                             double        *dy_unaccel)
{
  return FALSE;
}

static void
cobiwm_backend_class_init (CobiwmBackendClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = cobiwm_backend_finalize;

  klass->post_init = cobiwm_backend_real_post_init;
  klass->create_cursor_renderer = cobiwm_backend_real_create_cursor_renderer;
  klass->grab_device = cobiwm_backend_real_grab_device;
  klass->ungrab_device = cobiwm_backend_real_ungrab_device;
  klass->update_screen_size = cobiwm_backend_real_update_screen_size;
  klass->select_stage_events = cobiwm_backend_real_select_stage_events;
  klass->get_relative_motion_deltas = cobiwm_backend_real_get_relative_motion_deltas;

  g_signal_new ("keymap-changed",
                G_TYPE_FROM_CLASS (object_class),
                G_SIGNAL_RUN_LAST,
                0,
                NULL, NULL, NULL,
                G_TYPE_NONE, 0);
  g_signal_new ("keymap-layout-group-changed",
                G_TYPE_FROM_CLASS (object_class),
                G_SIGNAL_RUN_LAST,
                0,
                NULL, NULL, NULL,
                G_TYPE_NONE, 1, G_TYPE_UINT);
  g_signal_new ("last-device-changed",
                G_TYPE_FROM_CLASS (object_class),
                G_SIGNAL_RUN_LAST,
                0,
                NULL, NULL, NULL,
                G_TYPE_NONE, 1, G_TYPE_INT);
}

static void
cobiwm_backend_init (CobiwmBackend *backend)
{
  _backend = backend;
}

static void
cobiwm_backend_post_init (CobiwmBackend *backend)
{
  COBIWM_BACKEND_GET_CLASS (backend)->post_init (backend);
}

/**
 * cobiwm_backend_get_idle_monitor: (skip)
 */
CobiwmIdleMonitor *
cobiwm_backend_get_idle_monitor (CobiwmBackend *backend,
                               int          device_id)
{
  return g_hash_table_lookup (backend->device_monitors, &device_id);
}

/**
 * cobiwm_backend_get_monitor_manager: (skip)
 */
CobiwmMonitorManager *
cobiwm_backend_get_monitor_manager (CobiwmBackend *backend)
{
  CobiwmBackendPrivate *priv = cobiwm_backend_get_instance_private (backend);

  return priv->monitor_manager;
}

/**
 * cobiwm_backend_get_cursor_renderer: (skip)
 */
CobiwmCursorRenderer *
cobiwm_backend_get_cursor_renderer (CobiwmBackend *backend)
{
  CobiwmBackendPrivate *priv = cobiwm_backend_get_instance_private (backend);

  return priv->cursor_renderer;
}

/**
 * cobiwm_backend_grab_device: (skip)
 */
gboolean
cobiwm_backend_grab_device (CobiwmBackend *backend,
                          int          device_id,
                          uint32_t     timestamp)
{
  return COBIWM_BACKEND_GET_CLASS (backend)->grab_device (backend, device_id, timestamp);
}

/**
 * cobiwm_backend_ungrab_device: (skip)
 */
gboolean
cobiwm_backend_ungrab_device (CobiwmBackend *backend,
                            int          device_id,
                            uint32_t     timestamp)
{
  return COBIWM_BACKEND_GET_CLASS (backend)->ungrab_device (backend, device_id, timestamp);
}

/**
 * cobiwm_backend_warp_pointer: (skip)
 */
void
cobiwm_backend_warp_pointer (CobiwmBackend *backend,
                           int          x,
                           int          y)
{
  COBIWM_BACKEND_GET_CLASS (backend)->warp_pointer (backend, x, y);
}

void
cobiwm_backend_set_keymap (CobiwmBackend *backend,
                         const char  *layouts,
                         const char  *variants,
                         const char  *options)
{
  COBIWM_BACKEND_GET_CLASS (backend)->set_keymap (backend, layouts, variants, options);
}

/**
 * cobiwm_backend_get_keymap: (skip)
 */
struct xkb_keymap *
cobiwm_backend_get_keymap (CobiwmBackend *backend)

{
  return COBIWM_BACKEND_GET_CLASS (backend)->get_keymap (backend);
}

void
cobiwm_backend_lock_layout_group (CobiwmBackend *backend,
                                guint idx)
{
  COBIWM_BACKEND_GET_CLASS (backend)->lock_layout_group (backend, idx);
}

/**
 * cobiwm_backend_get_stage:
 * @backend: A #CobiwmBackend
 *
 * Gets the global #ClutterStage that's managed by this backend.
 *
 * Returns: (transfer none): the #ClutterStage
 */
ClutterActor *
cobiwm_backend_get_stage (CobiwmBackend *backend)
{
  CobiwmBackendPrivate *priv = cobiwm_backend_get_instance_private (backend);
  return priv->stage;
}

static gboolean
update_last_device (CobiwmBackend *backend)
{
  CobiwmCursorTracker *cursor_tracker = cobiwm_cursor_tracker_get_for_screen (NULL);
  CobiwmBackendPrivate *priv = cobiwm_backend_get_instance_private (backend);
  ClutterInputDeviceType device_type;
  ClutterDeviceManager *manager;
  ClutterInputDevice *device;

  priv->device_update_idle_id = 0;
  manager = clutter_device_manager_get_default ();
  device = clutter_device_manager_get_device (manager,
                                              backend->current_device_id);
  device_type = clutter_input_device_get_device_type (device);

  g_signal_emit_by_name (backend, "last-device-changed",
                         backend->current_device_id);

  switch (device_type)
    {
    case CLUTTER_KEYBOARD_DEVICE:
      break;
    case CLUTTER_TOUCHSCREEN_DEVICE:
      cobiwm_cursor_tracker_set_pointer_visible (cursor_tracker, FALSE);
      break;
    default:
      cobiwm_cursor_tracker_set_pointer_visible (cursor_tracker, TRUE);
      break;
    }

  return G_SOURCE_REMOVE;
}

void
cobiwm_backend_update_last_device (CobiwmBackend *backend,
                                 int          device_id)
{
  CobiwmBackendPrivate *priv = cobiwm_backend_get_instance_private (backend);
  ClutterDeviceManager *manager;
  ClutterInputDevice *device;

  if (backend->current_device_id == device_id)
    return;

  manager = clutter_device_manager_get_default ();
  device = clutter_device_manager_get_device (manager, device_id);

  if (!device ||
      clutter_input_device_get_device_mode (device) == CLUTTER_INPUT_MODE_MASTER)
    return;

  backend->current_device_id = device_id;

  if (priv->device_update_idle_id == 0)
    {
      priv->device_update_idle_id =
        g_idle_add ((GSourceFunc) update_last_device, backend);
      g_source_set_name_by_id (priv->device_update_idle_id,
                               "[cobiwm] update_last_device");
    }
}

gboolean
cobiwm_backend_get_relative_motion_deltas (CobiwmBackend *backend,
                                         const        ClutterEvent *event,
                                         double       *dx,
                                         double       *dy,
                                         double       *dx_unaccel,
                                         double       *dy_unaccel)
{
  CobiwmBackendClass *klass = COBIWM_BACKEND_GET_CLASS (backend);
  return klass->get_relative_motion_deltas (backend,
                                            event,
                                            dx, dy,
                                            dx_unaccel, dy_unaccel);
}

void
cobiwm_backend_set_client_pointer_constraint (CobiwmBackend           *backend,
                                            CobiwmPointerConstraint *constraint)
{
  g_assert (!constraint || (constraint && !backend->client_pointer_constraint));

  g_clear_object (&backend->client_pointer_constraint);
  if (constraint)
    backend->client_pointer_constraint = g_object_ref (constraint);
}

static GType
get_backend_type (void)
{
#if defined(CLUTTER_WINDOWING_X11)
  if (clutter_check_windowing_backend (CLUTTER_WINDOWING_X11))
    return COBIWM_TYPE_BACKEND_X11;
#endif

#if defined(CLUTTER_WINDOWING_EGL) && defined(HAVE_NATIVE_BACKEND)
  if (clutter_check_windowing_backend (CLUTTER_WINDOWING_EGL))
    return COBIWM_TYPE_BACKEND_NATIVE;
#endif

  g_assert_not_reached ();
}

static void
cobiwm_create_backend (void)
{
  /* cobiwm_backend_init() above install the backend globally so
   * so cobiwm_get_backend() works even during initialization. */
  g_object_new (get_backend_type (), NULL);
}

/* Cobiwm is responsible for pulling events off the X queue, so Clutter
 * doesn't need (and shouldn't) run its normal event source which polls
 * the X fd, but we do have to deal with dispatching events that accumulate
 * in the clutter queue. This happens, for example, when clutter generate
 * enter/leave events on mouse motion - several events are queued in the
 * clutter queue but only one dispatched. It could also happen because of
 * explicit calls to clutter_event_put(). We add a very simple custom
 * event loop source which is simply responsible for pulling events off
 * of the queue and dispatching them before we block for new events.
 */

static gboolean
event_prepare (GSource    *source,
               gint       *timeout_)
{
  *timeout_ = -1;

  return clutter_events_pending ();
}

static gboolean
event_check (GSource *source)
{
  return clutter_events_pending ();
}

static gboolean
event_dispatch (GSource    *source,
                GSourceFunc callback,
                gpointer    user_data)
{
  ClutterEvent *event = clutter_event_get ();

  if (event)
    {
      clutter_do_event (event);
      clutter_event_free (event);
    }

  return TRUE;
}

static GSourceFuncs event_funcs = {
  event_prepare,
  event_check,
  event_dispatch
};

/**
 * cobiwm_clutter_init: (skip)
 */
void
cobiwm_clutter_init (void)
{
  ClutterSettings *clutter_settings;
  GSource *source;

  cobiwm_create_backend ();

  if (clutter_init (NULL, NULL) != CLUTTER_INIT_SUCCESS)
    {
      g_warning ("Unable to initialize Clutter.\n");
      exit (1);
    }

  /*
   * XXX: We cannot handle high dpi scaling yet, so fix the scale to 1
   * for now.
   */
  clutter_settings = clutter_settings_get_default ();
  g_object_set (clutter_settings, "window-scaling-factor", 1, NULL);

  source = g_source_new (&event_funcs, sizeof (GSource));
  g_source_attach (source, NULL);
  g_source_unref (source);

  cobiwm_backend_post_init (_backend);
}
