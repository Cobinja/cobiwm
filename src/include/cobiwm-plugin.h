/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (c) 2008 Intel Corp.
 *
 * Author: Tomas Frydrych <tf@linux.intel.com>
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

#ifndef COBIWM_PLUGIN_H_
#define COBIWM_PLUGIN_H_

#include <types.h>
#include <compositor.h>
#include <compositor-cobiwm.h>
#include <cobiwm-version.h>

#include <clutter/clutter.h>
#include <X11/extensions/Xfixes.h>
#include <gmodule.h>

#define COBIWM_TYPE_PLUGIN            (cobiwm_plugin_get_type ())
#define COBIWM_PLUGIN(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), COBIWM_TYPE_PLUGIN, CobiwmPlugin))
#define COBIWM_PLUGIN_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  COBIWM_TYPE_PLUGIN, CobiwmPluginClass))
#define COBIWM_IS_PLUGIN(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), COBIWM_TYPE_PLUGIN))
#define COBIWM_IS_PLUGIN_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  COBIWM_TYPE_PLUGIN))
#define COBIWM_PLUGIN_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  COBIWM_TYPE_PLUGIN, CobiwmPluginClass))

typedef struct _CobiwmPlugin        CobiwmPlugin;
typedef struct _CobiwmPluginClass   CobiwmPluginClass;
typedef struct _CobiwmPluginVersion CobiwmPluginVersion;
typedef struct _CobiwmPluginInfo    CobiwmPluginInfo;
typedef struct _CobiwmPluginPrivate CobiwmPluginPrivate;

struct _CobiwmPlugin
{
  GObject parent;

  CobiwmPluginPrivate *priv;
};

/**
 * CobiwmPluginClass:
 * @start: virtual function called when the compositor starts managing a screen
 * @minimize: virtual function called when a window is minimized
 * @size_change: virtual function called when a window changes size to/from constraints
 * @map: virtual function called when a window is mapped
 * @destroy: virtual function called when a window is destroyed
 * @switch_workspace: virtual function called when the user switches to another
 * workspace
 * @kill_window_effects: virtual function called when the effects on a window
 * need to be killed prematurely; the plugin must call the completed() callback
 * as if the effect terminated naturally
 * @kill_switch_workspace: virtual function called when the workspace-switching
 * effect needs to be killed prematurely
 * @xevent_filter: virtual function called when handling each event
 * @keybinding_filter: virtual function called when handling each keybinding
 * @plugin_info: virtual function that returns information about the
 * #CobiwmPlugin
 */
struct _CobiwmPluginClass
{
  /*< private >*/
  GObjectClass parent_class;

  /*< public >*/

  /**
   * CobiwmPluginClass::start:
   *
   * Virtual function called when the compositor starts managing a screen
   */
  void (*start)            (CobiwmPlugin         *plugin);

  /**
   * CobiwmPluginClass::minimize:
   * @actor: a #CobiwmWindowActor
   *
   * Virtual function called when the window represented by @actor is minimized.
   */
  void (*minimize)         (CobiwmPlugin         *plugin,
                            CobiwmWindowActor    *actor);

  /**
   * CobiwmPluginClass::unminimize:
   * @actor: a #CobiwmWindowActor
   *
   * Virtual function called when the window represented by @actor is unminimized.
   */
  void (*unminimize)       (CobiwmPlugin         *plugin,
                            CobiwmWindowActor    *actor);

  void (*size_change)      (CobiwmPlugin         *plugin,
                            CobiwmWindowActor    *actor,
                            CobiwmSizeChange      which_change,
                            CobiwmRectangle      *old_frame_rect,
                            CobiwmRectangle      *old_buffer_rect);

  /**
   * CobiwmPluginClass::map:
   * @actor: a #CobiwmWindowActor
   *
   * Virtual function called when the window represented by @actor is mapped.
   */
  void (*map)              (CobiwmPlugin         *plugin,
                            CobiwmWindowActor    *actor);

  /**
   * CobiwmPluginClass::destroy:
   * @actor: a #CobiwmWindowActor
   *
   * Virtual function called when the window represented by @actor is destroyed.
   */
  void (*destroy)          (CobiwmPlugin         *plugin,
                            CobiwmWindowActor    *actor);

  /**
   * CobiwmPluginClass::switch_workspace:
   * @from: origin workspace
   * @to: destination workspace
   * @direction: a #CobiwmMotionDirection
   *
   * Virtual function called when the window represented by @actor is destroyed.
   */
  void (*switch_workspace) (CobiwmPlugin         *plugin,
                            gint                from,
                            gint                to,
                            CobiwmMotionDirection direction);

  void (*show_tile_preview) (CobiwmPlugin      *plugin,
                             CobiwmWindow      *window,
                             CobiwmRectangle   *tile_rect,
                             int              tile_monitor_number);
  void (*hide_tile_preview) (CobiwmPlugin      *plugin);

  void (*show_window_menu)  (CobiwmPlugin         *plugin,
                             CobiwmWindow         *window,
                             CobiwmWindowMenuType  menu,
                             int                 x,
                             int                 y);

  void (*show_window_menu_for_rect)  (CobiwmPlugin         *plugin,
		                      CobiwmWindow         *window,
				      CobiwmWindowMenuType  menu,
				      CobiwmRectangle      *rect);

  /**
   * CobiwmPluginClass::kill_window_effects:
   * @actor: a #CobiwmWindowActor
   *
   * Virtual function called when the effects on @actor need to be killed
   * prematurely; the plugin must call the completed() callback as if the effect
   * terminated naturally.
   */
  void (*kill_window_effects)      (CobiwmPlugin      *plugin,
                                    CobiwmWindowActor *actor);

  /**
   * CobiwmPluginClass::kill_switch_workspace:
   *
   * Virtual function called when the workspace-switching effect needs to be
   * killed prematurely.
   */
  void (*kill_switch_workspace)    (CobiwmPlugin     *plugin);

  /**
   * CobiwmPluginClass::xevent_filter:
   * @event: (type xlib.XEvent):
   *
   * Virtual function called when handling each event.
   *
   * Returns: %TRUE if the plugin handled the event type (i.e., if the return
   * value is %FALSE, there will be no subsequent call to the manager
   * completed() callback, and the compositor must ensure that any appropriate
   * post-effect cleanup is carried out.
   */
  gboolean (*xevent_filter) (CobiwmPlugin       *plugin,
                             XEvent           *event);

  /**
   * CobiwmPluginClass::keybinding_filter:
   * @binding: a #CobiwmKeyBinding
   *
   * Virtual function called when handling each keybinding.
   *
   * Returns: %TRUE if the plugin handled the keybinding.
   */
  gboolean (*keybinding_filter) (CobiwmPlugin     *plugin,
                                 CobiwmKeyBinding *binding);

  /**
   * CobiwmPluginClass::confirm_display_config:
   * @plugin: a #CobiwmPlugin
   *
   * Virtual function called when the display configuration changes.
   * The common way to implement this function is to show some form
   * of modal dialog that should ask the user if everything was ok.
   *
   * When confirmed by the user, the plugin must call cobiwm_plugin_complete_display_change()
   * to make the configuration permanent. If that function is not
   * called within the timeout, the previous configuration will be
   * reapplied.
   */
  void (*confirm_display_change) (CobiwmPlugin *plugin);

  /**
   * CobiwmPluginClass::plugin_info:
   * @plugin: a #CobiwmPlugin
   *
   * Virtual function that returns information about the #CobiwmPlugin.
   *
   * Returns: a #CobiwmPluginInfo.
   */
  const CobiwmPluginInfo * (*plugin_info) (CobiwmPlugin *plugin);

};

/**
 * CobiwmPluginInfo:
 * @name: name of the plugin
 * @version: version of the plugin
 * @author: author of the plugin
 * @license: license of the plugin
 * @description: description of the plugin
 */
struct _CobiwmPluginInfo
{
  const gchar *name;
  const gchar *version;
  const gchar *author;
  const gchar *license;
  const gchar *description;
};

GType cobiwm_plugin_get_type (void);

const CobiwmPluginInfo * cobiwm_plugin_get_info (CobiwmPlugin *plugin);

/**
 * CobiwmPluginVersion:
 * @version_major: major component of the version number of Cobiwm with which the plugin was compiled
 * @version_minor: minor component of the version number of Cobiwm with which the plugin was compiled
 * @version_micro: micro component of the version number of Cobiwm with which the plugin was compiled
 * @version_api: version of the plugin API
 */
struct _CobiwmPluginVersion
{
  /*
   * Version information; the first three numbers match the Cobiwm version
   * with which the plugin was compiled (see clutter-plugins/simple.c for sample
   * code).
   */
  guint version_major;
  guint version_minor;
  guint version_micro;

  /*
   * Version of the plugin API; this is unrelated to the matacity version
   * per se. The API version is checked by the plugin manager and must match
   * the one used by it (see clutter-plugins/default.c for sample code).
   */
  guint version_api;
};

/*
 * Convenience macro to set up the plugin type. Based on GEdit.
 */
#define COBIWM_PLUGIN_DECLARE(ObjectName, object_name)                    \
  G_MODULE_EXPORT CobiwmPluginVersion cobiwm_plugin_version =               \
    {                                                                   \
      COBIWM_MAJOR_VERSION,                                               \
      COBIWM_MINOR_VERSION,                                               \
      COBIWM_MICRO_VERSION,                                               \
      COBIWM_PLUGIN_API_VERSION                                           \
    };                                                                  \
                                                                        \
  static GType g_define_type_id = 0;                                    \
                                                                        \
  /* Prototypes */                                                      \
  G_MODULE_EXPORT                                                       \
  GType object_name##_get_type (void);                                  \
                                                                        \
  G_MODULE_EXPORT                                                       \
  GType object_name##_register_type (GTypeModule *type_module);         \
                                                                        \
  G_MODULE_EXPORT                                                       \
  GType cobiwm_plugin_register_type (GTypeModule *type_module);           \
                                                                        \
  GType                                                                 \
  object_name##_get_type ()                                             \
  {                                                                     \
    return g_define_type_id;                                            \
  }                                                                     \
                                                                        \
  static void object_name##_init (ObjectName *self);                    \
  static void object_name##_class_init (ObjectName##Class *klass);      \
  static gpointer object_name##_parent_class = NULL;                    \
  static void object_name##_class_intern_init (gpointer klass)          \
  {                                                                     \
    object_name##_parent_class = g_type_class_peek_parent (klass);      \
    object_name##_class_init ((ObjectName##Class *) klass);             \
  }                                                                     \
                                                                        \
  GType                                                                 \
  object_name##_register_type (GTypeModule *type_module)                \
  {                                                                     \
    static const GTypeInfo our_info =                                   \
      {                                                                 \
        sizeof (ObjectName##Class),                                     \
        NULL, /* base_init */                                           \
        NULL, /* base_finalize */                                       \
        (GClassInitFunc) object_name##_class_intern_init,               \
        NULL,                                                           \
        NULL, /* class_data */                                          \
        sizeof (ObjectName),                                            \
        0, /* n_preallocs */                                            \
        (GInstanceInitFunc) object_name##_init                          \
      };                                                                \
                                                                        \
    g_define_type_id = g_type_module_register_type (type_module,        \
                                                    COBIWM_TYPE_PLUGIN,   \
                                                    #ObjectName,        \
                                                    &our_info,          \
                                                    0);                 \
                                                                        \
                                                                        \
    return g_define_type_id;                                            \
  }                                                                     \
                                                                        \
  G_MODULE_EXPORT GType                                                 \
  cobiwm_plugin_register_type (GTypeModule *type_module)                  \
  {                                                                     \
    return object_name##_register_type (type_module);                   \
  }                                                                     \

void
cobiwm_plugin_switch_workspace_completed (CobiwmPlugin *plugin);

void
cobiwm_plugin_minimize_completed (CobiwmPlugin      *plugin,
                                CobiwmWindowActor *actor);

void
cobiwm_plugin_unminimize_completed (CobiwmPlugin      *plugin,
                                  CobiwmWindowActor *actor);

void
cobiwm_plugin_size_change_completed (CobiwmPlugin      *plugin,
                                   CobiwmWindowActor *actor);

void
cobiwm_plugin_map_completed (CobiwmPlugin      *plugin,
                           CobiwmWindowActor *actor);

void
cobiwm_plugin_destroy_completed (CobiwmPlugin      *plugin,
                               CobiwmWindowActor *actor);

void
cobiwm_plugin_complete_display_change (CobiwmPlugin *plugin,
                                     gboolean    ok);

/**
 * CobiwmModalOptions:
 * @COBIWM_MODAL_POINTER_ALREADY_GRABBED: if set the pointer is already
 *   grabbed by the plugin and should not be grabbed again.
 * @COBIWM_MODAL_KEYBOARD_ALREADY_GRABBED: if set the keyboard is already
 *   grabbed by the plugin and should not be grabbed again.
 *
 * Options that can be provided when calling cobiwm_plugin_begin_modal().
 */
typedef enum {
  COBIWM_MODAL_POINTER_ALREADY_GRABBED = 1 << 0,
  COBIWM_MODAL_KEYBOARD_ALREADY_GRABBED = 1 << 1
} CobiwmModalOptions;

gboolean
cobiwm_plugin_begin_modal (CobiwmPlugin      *plugin,
                         CobiwmModalOptions options,
                         guint32          timestamp);

void
cobiwm_plugin_end_modal (CobiwmPlugin *plugin,
                       guint32     timestamp);

CobiwmScreen *cobiwm_plugin_get_screen        (CobiwmPlugin *plugin);

void _cobiwm_plugin_set_compositor (CobiwmPlugin *plugin, CobiwmCompositor *compositor);

#endif /* COBIWM_PLUGIN_H_ */
