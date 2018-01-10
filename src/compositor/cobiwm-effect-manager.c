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

#include "config.h"
#include "compositor-private.h"
#include "cobiwm-effect-manager.h"
#include "cobiwm-background-group.h"
#include "cobiwm-background-actor.h"
#include "prefs.h"
#include "errors.h"
#include "workspace.h"
#include "window-private.h"

#include <string.h>
#include <stdlib.h>

#include <clutter/clutter.h>
#include <clutter/x11/clutter-x11.h>

#define DESTROY_TIMEOUT   100
#define MINIMIZE_TIMEOUT  250
#define MAP_TIMEOUT       250
#define SWITCH_TIMEOUT    500

#define ACTOR_DATA_KEY "MCCP-Default-actor-data"
#define SCREEN_TILE_PREVIEW_DATA_KEY "MCCP-Default-screen-tile-preview-data"

static GQuark actor_data_quark = 0;
static GQuark screen_tile_preview_data_quark = 0;

struct _CobiwmEffectManager
{
  CobiwmCompositor *compositor;
  ClutterTimeline       *tml_switch_workspace1;
  ClutterTimeline       *tml_switch_workspace2;
  ClutterActor          *desktop1;
  ClutterActor          *desktop2;

  ClutterActor          *background_group;
};

G_DEFINE_TYPE (CobiwmEffectManager, cobiwm_effect_manager, G_TYPE_OBJECT);

typedef struct _ActorPrivate
{
  ClutterActor *orig_parent;

  ClutterTimeline *tml_minimize;
  ClutterTimeline *tml_destroy;
  ClutterTimeline *tml_map;
} ActorPrivate;

typedef struct _EffectCompleteData
{
  ClutterActor *actor;
  CobiwmEffectManager *plugin_mgr;
} EffectCompleteData;

typedef struct _ScreenTilePreview
{
  ClutterActor   *actor;
  GdkRGBA        *preview_color;
  CobiwmRectangle   tile_rect;
} ScreenTilePreview;

static void
free_screen_tile_preview (gpointer data)
{
  ScreenTilePreview *preview = data;

  if (G_LIKELY (preview != NULL)) {
    clutter_actor_destroy (preview->actor);
    g_slice_free (ScreenTilePreview, preview);
  }
}

static void
free_actor_private (gpointer data)
{
  if (G_LIKELY (data != NULL))
    g_slice_free (ActorPrivate, data);
}

static ActorPrivate *
get_actor_private (CobiwmWindowActor *actor)
{
  ActorPrivate *priv = g_object_get_qdata (G_OBJECT (actor), actor_data_quark);

  if (G_UNLIKELY (actor_data_quark == 0))
    actor_data_quark = g_quark_from_static_string (ACTOR_DATA_KEY);

  if (G_UNLIKELY (!priv))
    {
      priv = g_slice_new0 (ActorPrivate);

      g_object_set_qdata_full (G_OBJECT (actor),
                               actor_data_quark, priv,
                               free_actor_private);
    }

  return priv;
}

static ClutterTimeline *
actor_animate (ClutterActor         *actor,
               ClutterAnimationMode  mode,
               guint                 duration,
               const gchar          *first_property,
               ...)
{
  va_list args;
  ClutterTransition *transition;

  clutter_actor_save_easing_state (actor);
  clutter_actor_set_easing_mode (actor, mode);
  clutter_actor_set_easing_duration (actor, duration);

  va_start (args, first_property);
  g_object_set_valist (G_OBJECT (actor), first_property, args);
  va_end (args);

  transition = clutter_actor_get_transition (actor, first_property);

  clutter_actor_restore_easing_state (actor);

  return CLUTTER_TIMELINE (transition);
}

static void
on_dialog_closed (GPid     pid,
                  gint     status,
                  gpointer user_data)
{
  CobiwmMonitorManager *manager;
  gboolean ok;

  ok = g_spawn_check_exit_status (status, NULL);
  manager = cobiwm_monitor_manager_get();
  cobiwm_monitor_manager_confirm_configuration(manager, ok);
}

static ScreenTilePreview *
get_screen_tile_preview (CobiwmScreen *screen)
{
  ScreenTilePreview *preview = g_object_get_qdata (G_OBJECT (screen), screen_tile_preview_data_quark);

  if (G_UNLIKELY (screen_tile_preview_data_quark == 0))
    screen_tile_preview_data_quark = g_quark_from_static_string (SCREEN_TILE_PREVIEW_DATA_KEY);

  if (G_UNLIKELY (!preview))
    {
      preview = g_slice_new0 (ScreenTilePreview);

      preview->actor = clutter_actor_new ();
      clutter_actor_set_background_color (preview->actor, CLUTTER_COLOR_Blue);
      clutter_actor_set_opacity (preview->actor, 100);

      clutter_actor_add_child (cobiwm_get_window_group_for_screen (screen), preview->actor);
      g_object_set_qdata_full (G_OBJECT (screen),
                               screen_tile_preview_data_quark, preview,
                               free_screen_tile_preview);
    }

  return preview;
}

static void
on_switch_workspace_effect_complete (ClutterTimeline *timeline, gpointer data)
{
  CobiwmEffectManager *plugin_mgr = COBIWM_EFFECT_MANAGER (data);
  
  CobiwmScreen *screen = plugin_mgr->compositor->display->screen;
  GList *l = cobiwm_get_window_actors (screen);

  while (l)
    {
      ClutterActor *a = l->data;
      CobiwmWindowActor *window_actor = COBIWM_WINDOW_ACTOR (a);
      ActorPrivate *apriv = get_actor_private (window_actor);

      if (apriv->orig_parent)
        {
          g_object_ref (a);
          clutter_actor_remove_child (clutter_actor_get_parent (a), a);
          clutter_actor_add_child (apriv->orig_parent, a);
          g_object_unref (a);
          apriv->orig_parent = NULL;
        }

      l = l->next;
    }

  clutter_actor_destroy (plugin_mgr->desktop1);
  clutter_actor_destroy (plugin_mgr->desktop2);

  plugin_mgr->tml_switch_workspace1 = NULL;
  plugin_mgr->tml_switch_workspace2 = NULL;
  plugin_mgr->desktop1 = NULL;
  plugin_mgr->desktop2 = NULL;

  cobiwm_switch_workspace_completed (plugin_mgr->compositor);
}

static void
on_monitors_changed (CobiwmScreen *screen,
                     CobiwmEffectManager *plugin_mgr)
{
  int i, n;
  GRand *rand = g_rand_new_with_seed (123456);

  clutter_actor_destroy_all_children (plugin_mgr->background_group);

  n = cobiwm_screen_get_n_monitors (screen);
  for (i = 0; i < n; i++)
    {
      CobiwmRectangle rect;
      ClutterActor *background_actor;
      CobiwmBackground *background;
      ClutterColor color;

      cobiwm_screen_get_monitor_geometry (screen, i, &rect);

      background_actor = cobiwm_background_actor_new (screen, i);

      clutter_actor_set_position (background_actor, rect.x, rect.y);
      clutter_actor_set_size (background_actor, rect.width, rect.height);

      /* Don't use rand() here, mesa calls srand() internally when
         parsing the driconf XML, but it's nice if the colors are
         reproducible.
      */
      clutter_color_init (&color,
                          g_rand_int_range (rand, 0, 255),
                          g_rand_int_range (rand, 0, 255),
                          g_rand_int_range (rand, 0, 255),
                          255);

      background = cobiwm_background_new (screen);
      cobiwm_background_set_color (background, &color);
      cobiwm_background_actor_set_background (COBIWM_BACKGROUND_ACTOR (background_actor), background);
      g_object_unref (background);

      clutter_actor_add_child (plugin_mgr->background_group, background_actor);
    }

  g_rand_free (rand);
}

static void
on_minimize_effect_complete (ClutterTimeline *timeline, EffectCompleteData *data)
{
  /*
   * Must reverse the effect of the effect; must hide it first to ensure
   * that the restoration will not be visible.
   */
  ActorPrivate *apriv;
  CobiwmWindowActor *window_actor = COBIWM_WINDOW_ACTOR (data->actor);

  apriv = get_actor_private (window_actor);
  apriv->tml_minimize = NULL;

  clutter_actor_hide (data->actor);

  /* FIXME - we shouldn't assume the original scale, it should be saved
   * at the start of the effect */
  clutter_actor_set_scale (data->actor, 1.0, 1.0);

  /* Now notify the manager that we are done with this effect */
  cobiwm_window_actor_effect_completed (window_actor, COBIWM_EFFECT_MINIMIZE);

  g_free (data);
}

static void
minimize (CobiwmWindowActor *window_actor)
{
  CobiwmWindowType type;
  CobiwmRectangle icon_geometry;
  CobiwmWindow *cobiwm_window = cobiwm_window_actor_get_cobiwm_window (window_actor);
  ClutterActor *actor  = CLUTTER_ACTOR (window_actor);


  type = cobiwm_window_get_window_type (cobiwm_window);

  if (!cobiwm_window_get_icon_geometry(cobiwm_window, &icon_geometry))
    {
      icon_geometry.x = 0;
      icon_geometry.y = 0;
    }

  if (type == COBIWM_WINDOW_NORMAL)
    {
      EffectCompleteData *data = g_new0 (EffectCompleteData, 1);
      ActorPrivate *apriv = get_actor_private (window_actor);

      apriv->tml_minimize = actor_animate (actor,
                                           CLUTTER_EASE_IN_SINE,
                                           MINIMIZE_TIMEOUT,
                                           "scale-x", 0.0,
                                           "scale-y", 0.0,
                                           "x", (double)icon_geometry.x,
                                           "y", (double)icon_geometry.y,
                                           NULL);
      data->actor = actor;
      g_signal_connect (apriv->tml_minimize, "completed",
                        G_CALLBACK (on_minimize_effect_complete),
                        data);

    }
  else
    cobiwm_window_actor_effect_completed (window_actor, COBIWM_EFFECT_MINIMIZE);
}

static void
on_map_effect_complete (ClutterTimeline *timeline, EffectCompleteData *data)
{
  /*
   * Must reverse the effect of the effect.
   */
  CobiwmWindowActor  *window_actor = COBIWM_WINDOW_ACTOR (data->actor);
  ActorPrivate  *apriv = get_actor_private (window_actor);

  apriv->tml_map = NULL;

  /* Now notify the window actor that we are done with this effect */
  cobiwm_window_actor_effect_completed (window_actor, COBIWM_EFFECT_MAP);

  g_free (data);
}

static void
map (CobiwmWindowActor *window_actor)
{
  CobiwmWindowType type;
  ClutterActor *actor = CLUTTER_ACTOR (window_actor);
  CobiwmWindow *cobiwm_window = cobiwm_window_actor_get_cobiwm_window (window_actor);

  type = cobiwm_window_get_window_type (cobiwm_window);

  if (type == COBIWM_WINDOW_NORMAL)
    {
      EffectCompleteData *data = g_new0 (EffectCompleteData, 1);
      ActorPrivate *apriv = get_actor_private (window_actor);

      clutter_actor_set_pivot_point (actor, 0.5, 0.5);
      clutter_actor_set_opacity (actor, 0);
      clutter_actor_set_scale (actor, 0.5, 0.5);
      clutter_actor_show (actor);

      apriv->tml_map = actor_animate (actor,
                                      CLUTTER_EASE_OUT_QUAD,
                                      MAP_TIMEOUT,
                                      "opacity", 255,
                                      "scale-x", 1.0,
                                      "scale-y", 1.0,
                                      NULL);
      data->actor = actor;
      g_signal_connect (apriv->tml_map, "completed",
                        G_CALLBACK (on_map_effect_complete),
                        data);
    }
  else
    cobiwm_window_actor_effect_completed (window_actor, COBIWM_EFFECT_MAP);
}

static void
on_destroy_effect_complete (ClutterTimeline *timeline, EffectCompleteData *data)
{
  CobiwmWindowActor *window_actor = COBIWM_WINDOW_ACTOR(data->actor);
  ActorPrivate *apriv = get_actor_private(window_actor);

  apriv->tml_destroy = NULL;

  cobiwm_window_actor_effect_completed(window_actor, COBIWM_EFFECT_DESTROY);
}

static void
destroy (CobiwmWindowActor *window_actor)
{
  CobiwmWindowType type;
  ClutterActor *actor = CLUTTER_ACTOR (window_actor);
  CobiwmWindow *cobiwm_window = cobiwm_window_actor_get_cobiwm_window (window_actor);

  type = cobiwm_window_get_window_type (cobiwm_window);

  if (type == COBIWM_WINDOW_NORMAL)
    {
      EffectCompleteData *data = g_new0 (EffectCompleteData, 1);
      ActorPrivate *apriv = get_actor_private (window_actor);

      apriv->tml_destroy = actor_animate (actor,
                                          CLUTTER_EASE_OUT_QUAD,
                                          DESTROY_TIMEOUT,
                                          "opacity", 0,
                                          "scale-x", 0.8,
                                          "scale-y", 0.8,
                                          NULL);
      data->actor = actor;
      g_signal_connect (apriv->tml_destroy, "completed",
                        G_CALLBACK (on_destroy_effect_complete),
                        data);
    }
  else
    cobiwm_window_actor_effect_completed(window_actor, COBIWM_EFFECT_DESTROY);
}

static void
on_confirm_display_change (CobiwmMonitorManager *monitors,
                           CobiwmEffectManager  *plugin_mgr)
{
  cobiwm_effect_manager_confirm_display_change (plugin_mgr);
}

static void
start (CobiwmEffectManager *plugin_mgr)
{
  CobiwmScreen *screen = plugin_mgr->compositor->display->screen;

  plugin_mgr->background_group = cobiwm_background_group_new ();
  clutter_actor_insert_child_below (cobiwm_get_window_group_for_screen (screen),
                                    plugin_mgr->background_group, NULL);

  g_signal_connect (screen, "monitors-changed",
                    G_CALLBACK (on_monitors_changed), plugin_mgr);
  on_monitors_changed (screen, plugin_mgr);

  clutter_actor_show (cobiwm_get_stage_for_screen (screen));
}

CobiwmEffectManager *
cobiwm_effect_manager_new (CobiwmCompositor *compositor)
{
  CobiwmEffectManager *plugin_mgr;
  CobiwmMonitorManager *monitors;

  plugin_mgr = g_new0 (CobiwmEffectManager, 1);
  plugin_mgr->compositor = compositor;

  start(plugin_mgr);

  monitors = cobiwm_monitor_manager_get ();
  g_signal_connect (monitors, "confirm-display-change",
                    G_CALLBACK (on_confirm_display_change), plugin_mgr);

  return plugin_mgr;
}

static void
cobiwm_effect_manager_init (CobiwmEffectManager *plugin_mgr)
{
}

static void
cobiwm_effect_manager_class_init (CobiwmEffectManagerClass *klass) {
}

static void
cobiwm_effect_manager_kill_window_effects (CobiwmEffectManager *plugin_mgr,
                                         CobiwmWindowActor   *actor)
{
  ActorPrivate *apriv;

  apriv = get_actor_private (actor);

  if (apriv->tml_minimize)
    {
      clutter_timeline_stop (apriv->tml_minimize);
      g_signal_emit_by_name (apriv->tml_minimize, "completed", NULL);
    }

  if (apriv->tml_map)
    {
      clutter_timeline_stop (apriv->tml_map);
      g_signal_emit_by_name (apriv->tml_map, "completed", NULL);
    }

  if (apriv->tml_destroy)
    {
      clutter_timeline_stop (apriv->tml_destroy);
      g_signal_emit_by_name (apriv->tml_destroy, "completed", NULL);
    }
}

static void
cobiwm_effect_manager_kill_switch_workspace (CobiwmEffectManager *plugin_mgr)
{
  if (plugin_mgr->tml_switch_workspace1)
    {
      clutter_timeline_stop (plugin_mgr->tml_switch_workspace1);
      clutter_timeline_stop (plugin_mgr->tml_switch_workspace2);
      g_signal_emit_by_name (plugin_mgr->tml_switch_workspace1, "completed", NULL);
    }
}

/*
 * Public method that the compositor hooks into for events that require
 * no additional parameters.
 *
 * Returns TRUE if the plugin handled the event type (i.e.,
 * if the return value is FALSE, there will be no subsequent call to the
 * manager completed() callback, and the compositor must ensure that any
 * appropriate post-effect cleanup is carried out.
 */
gboolean
cobiwm_effect_manager_event_simple (CobiwmEffectManager *plugin_mgr,
                                  CobiwmWindowActor   *actor,
                                  CobiwmEffect   event)
{
  CobiwmDisplay *display = plugin_mgr->compositor->display;
  
  gboolean retval = TRUE;

  if (display->display_opening)
    return FALSE;

  switch (event) {
    case COBIWM_EFFECT_MINIMIZE:
      cobiwm_effect_manager_kill_window_effects(plugin_mgr, actor);
      minimize(actor);
      break;
    case COBIWM_EFFECT_UNMINIMIZE:
      // not yet implemented
      break;
    case COBIWM_EFFECT_MAP:
      cobiwm_effect_manager_kill_window_effects(plugin_mgr, actor);
      map(actor);
      break;
    case COBIWM_EFFECT_DESTROY:
      destroy(actor);
      break;
    default:
      retval = FALSE;
      g_warning ("Incorrect handler called for event %d", event);
    }

  return retval;
}

/*
 * The public method that the compositor hooks into for desktop switching.
 *
 * Returns TRUE if the plugin handled the event type (i.e.,
 * if the return value is FALSE, there will be no subsequent call to the
 * manager completed() callback, and the compositor must ensure that any
 * appropriate post-effect cleanup is carried out.
 */
gboolean
cobiwm_effect_manager_switch_workspace (CobiwmEffectManager   *plugin_mgr,
                                      gint                 from,
                                      gint                 to,
                                      CobiwmMotionDirection  direction)
{
  CobiwmDisplay *display = plugin_mgr->compositor->display;

  if (display->display_opening)
    return FALSE;
  
  cobiwm_effect_manager_kill_switch_workspace(plugin_mgr);
  
  CobiwmScreen *screen;
  GList        *l;
  ClutterActor *workspace0  = clutter_actor_new ();
  ClutterActor *workspace1  = clutter_actor_new ();
  ClutterActor *stage;
  int           screen_width, screen_height;

  screen = plugin_mgr->compositor->display->screen;
  stage = cobiwm_get_stage_for_screen (screen);

  cobiwm_screen_get_size (screen,
                        &screen_width,
                        &screen_height);

  clutter_actor_set_pivot_point (workspace1, 1.0, 1.0);
  clutter_actor_set_position (workspace1,
                              screen_width,
                              screen_height);

  clutter_actor_set_scale (workspace1, 0.0, 0.0);

  clutter_actor_add_child (stage, workspace1);
  clutter_actor_add_child (stage, workspace0);

  if (from == to)
    {
      cobiwm_switch_workspace_completed (plugin_mgr->compositor);
      return FALSE;
    }

  l = g_list_last (cobiwm_get_window_actors (screen));

  while (l)
    {
      CobiwmWindowActor *window_actor = l->data;
      ActorPrivate    *apriv	    = get_actor_private (window_actor);
      ClutterActor    *actor	    = CLUTTER_ACTOR (window_actor);
      CobiwmWorkspace   *workspace;
      gint             win_workspace;

      workspace = cobiwm_window_get_workspace (cobiwm_window_actor_get_cobiwm_window (window_actor));
      win_workspace = cobiwm_workspace_index (workspace);

      if (win_workspace == to || win_workspace == from)
        {
          ClutterActor *parent = win_workspace == to ? workspace1 : workspace0;
          apriv->orig_parent = clutter_actor_get_parent (actor);

          g_object_ref (actor);
          clutter_actor_remove_child (clutter_actor_get_parent (actor), actor);
          clutter_actor_add_child (parent, actor);
          clutter_actor_show (actor);
          clutter_actor_set_child_below_sibling (parent, actor, NULL);
          g_object_unref (actor);
        }
      else if (win_workspace < 0)
        {
          /* Sticky window */
          apriv->orig_parent = NULL;
        }
      else
        {
          /* Window on some other desktop */
          clutter_actor_hide (actor);
          apriv->orig_parent = NULL;
        }

      l = l->prev;
    }

  plugin_mgr->desktop1 = workspace0;
  plugin_mgr->desktop2 = workspace1;

  plugin_mgr->tml_switch_workspace1 = actor_animate (workspace0, CLUTTER_EASE_IN_SINE,
                                               SWITCH_TIMEOUT,
                                               "scale-x", 1.0,
                                               "scale-y", 1.0,
                                               NULL);
  g_signal_connect (plugin_mgr->tml_switch_workspace1,
                    "completed",
                    G_CALLBACK (on_switch_workspace_effect_complete),
                    plugin_mgr);

  plugin_mgr->tml_switch_workspace2 = actor_animate (workspace1, CLUTTER_EASE_IN_SINE,
                                               SWITCH_TIMEOUT,
                                               "scale-x", 0.0,
                                               "scale-y", 0.0,
                                               NULL);

  return TRUE;
}

void
cobiwm_effect_manager_confirm_display_change (CobiwmEffectManager *plugin_mgr)
{
  GPid pid;

  pid = cobiwm_show_dialog ("--question",
                          "Does the display look OK?",
                          "20",
                          NULL,
                          "_Keep This Configuration",
                          "_Restore Previous Configuration",
                          "preferences-desktop-display",
                          0,
                          NULL, NULL);

  g_child_watch_add (pid, on_dialog_closed, plugin_mgr);
}

void
cobiwm_effect_manager_show_tile_preview (CobiwmEffectManager *plugin_mgr,
                                       CobiwmWindow        *window,
                                       CobiwmRectangle     *tile_rect,
                                       int                tile_monitor_number)
{
  CobiwmScreen *screen = plugin_mgr->compositor->display->screen;
  ScreenTilePreview *preview = get_screen_tile_preview (screen);
  ClutterActor *window_actor;

  if (clutter_actor_is_visible (preview->actor)
      && preview->tile_rect.x == tile_rect->x
      && preview->tile_rect.y == tile_rect->y
      && preview->tile_rect.width == tile_rect->width
      && preview->tile_rect.height == tile_rect->height)
    return; /* nothing to do */

  clutter_actor_set_position (preview->actor, tile_rect->x, tile_rect->y);
  clutter_actor_set_size (preview->actor, tile_rect->width, tile_rect->height);

  clutter_actor_show (preview->actor);

  window_actor = CLUTTER_ACTOR (cobiwm_window_get_compositor_private (window));
  clutter_actor_set_child_below_sibling (clutter_actor_get_parent (preview->actor),
                                         preview->actor,
                                         window_actor);

  preview->tile_rect = *tile_rect;
}

void
cobiwm_effect_manager_hide_tile_preview (CobiwmEffectManager *plugin_mgr)
{
  CobiwmScreen *screen = plugin_mgr->compositor->display->screen;
  ScreenTilePreview *preview = get_screen_tile_preview (screen);
  clutter_actor_hide (preview->actor);
}

void
cobiwm_effect_manager_show_window_menu (CobiwmEffectManager  *plugin_mgr,
                                      CobiwmWindow         *window,
                                      CobiwmWindowMenuType  menu,
                                      int                 x,
                                      int                 y)
{
  // TODO implement
}

void
cobiwm_effect_manager_show_window_menu_for_rect (CobiwmEffectManager  *plugin_mgr,
                                               CobiwmWindow         *window,
                                               CobiwmWindowMenuType  menu,
					       CobiwmRectangle      *rect)
{
  // TODO implement
}
