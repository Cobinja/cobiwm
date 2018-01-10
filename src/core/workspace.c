/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2001 Havoc Pennington
 * Copyright (C) 2003 Rob Adams
 * Copyright (C) 2004, 2005 Elijah Newren
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
 * SECTION:workspace
 * @title:CobiwmWorkspace
 * @short_description:Workspaces
 *
 * A workspace is a set of windows which all live on the same
 * screen.  (You may also see the name "desktop" around the place,
 * which is the EWMH's name for the same thing.)  Only one workspace
 * of a screen may be active at once; all windows on all other workspaces
 * are unmapped.
 */

#include <config.h>
#include "screen-private.h"
#include "workspace.h"
#include "workspace-private.h"
#include "boxes-private.h"
#include "errors.h"
#include "prefs.h"

#include "compositor.h"

#include <X11/Xatom.h>
#include <string.h>
#include <glib.h>
#ifdef HAVE_LIBCANBERRA
#include <canberra-gtk.h>
#endif

void cobiwm_workspace_queue_calc_showing   (CobiwmWorkspace *workspace);
static void focus_ancestor_or_top_window (CobiwmWorkspace *workspace,
                                          CobiwmWindow    *not_this_one,
                                          guint32        timestamp);
static void free_this                    (gpointer candidate,
                                          gpointer dummy);

G_DEFINE_TYPE (CobiwmWorkspace, cobiwm_workspace, G_TYPE_OBJECT);

enum {
  PROP_0,

  PROP_N_WINDOWS,
  PROP_WORKSPACE_INDEX,

  LAST_PROP,
};

static GParamSpec *obj_props[LAST_PROP];

enum
{
  WINDOW_ADDED,
  WINDOW_REMOVED,

  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static void
cobiwm_workspace_finalize (GObject *object)
{
  /* Actual freeing done in cobiwm_workspace_remove() for now */
  G_OBJECT_CLASS (cobiwm_workspace_parent_class)->finalize (object);
}

static void
cobiwm_workspace_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
cobiwm_workspace_get_property (GObject      *object,
                             guint         prop_id,
                             GValue       *value,
                             GParamSpec   *pspec)
{
  CobiwmWorkspace *ws = COBIWM_WORKSPACE (object);

  switch (prop_id)
    {
    case PROP_N_WINDOWS:
      /*
       * This is reliable, but not very efficient; should we store
       * the list lenth ?
       */
      g_value_set_uint (value, g_list_length (ws->windows));
      break;
    case PROP_WORKSPACE_INDEX:
      g_value_set_uint (value, cobiwm_workspace_index (ws));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
cobiwm_workspace_class_init (CobiwmWorkspaceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  object_class->finalize     = cobiwm_workspace_finalize;
  object_class->get_property = cobiwm_workspace_get_property;
  object_class->set_property = cobiwm_workspace_set_property;

  signals[WINDOW_ADDED] = g_signal_new ("window-added",
                                        G_TYPE_FROM_CLASS (klass),
                                        G_SIGNAL_RUN_LAST,
                                        0,
                                        NULL, NULL, NULL,
                                        G_TYPE_NONE, 1,
                                        COBIWM_TYPE_WINDOW);
  signals[WINDOW_REMOVED] = g_signal_new ("window-removed",
                                          G_TYPE_FROM_CLASS (klass),
                                          G_SIGNAL_RUN_LAST,
                                          0,
                                          NULL, NULL, NULL,
                                          G_TYPE_NONE, 1,
                                          COBIWM_TYPE_WINDOW);

  obj_props[PROP_N_WINDOWS] = g_param_spec_uint ("n-windows",
                                                 "N Windows",
                                                 "Number of windows",
                                                 0, G_MAXUINT, 0,
                                                 G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  obj_props[PROP_WORKSPACE_INDEX] = g_param_spec_uint ("workspace-index",
                                                       "Workspace index",
                                                       "The workspace's index",
                                                       0, G_MAXUINT, 0,
                                                       G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, LAST_PROP, obj_props);
}

static void
cobiwm_workspace_init (CobiwmWorkspace *workspace)
{
}

CobiwmWorkspace*
cobiwm_workspace_new (CobiwmScreen *screen)
{
  CobiwmWorkspace *workspace;
  GSList *windows, *l;

  workspace = g_object_new (COBIWM_TYPE_WORKSPACE, NULL);

  workspace->screen = screen;
  workspace->screen->workspaces =
    g_list_append (workspace->screen->workspaces, workspace);
  workspace->windows = NULL;
  workspace->mru_list = NULL;

  workspace->work_areas_invalid = TRUE;
  workspace->work_area_monitor = NULL;
  workspace->work_area_screen.x = 0;
  workspace->work_area_screen.y = 0;
  workspace->work_area_screen.width = 0;
  workspace->work_area_screen.height = 0;

  workspace->screen_region = NULL;
  workspace->monitor_region = NULL;
  workspace->screen_edges = NULL;
  workspace->monitor_edges = NULL;
  workspace->list_containing_self = g_list_prepend (NULL, workspace);

  workspace->builtin_struts = NULL;
  workspace->all_struts = NULL;

  workspace->showing_desktop = FALSE;

  /* make sure sticky windows are in our mru_list */
  windows = cobiwm_display_list_windows (screen->display, COBIWM_LIST_SORTED);
  for (l = windows; l; l = l->next)
    if (cobiwm_window_located_on_workspace (l->data, workspace))
      cobiwm_workspace_add_window (workspace, l->data);
  g_slist_free (windows);

  return workspace;
}

/* Foreach function for workspace_free_struts() */
static void
free_this (gpointer candidate, gpointer dummy)
{
  g_free (candidate);
}

/**
 * workspace_free_all_struts:
 * @workspace: The workspace.
 *
 * Frees the combined struts list of a workspace.
 */
static void
workspace_free_all_struts (CobiwmWorkspace *workspace)
{
  if (workspace->all_struts == NULL)
    return;

  g_slist_foreach (workspace->all_struts, free_this, NULL);
  g_slist_free (workspace->all_struts);
  workspace->all_struts = NULL;
}

/**
 * workspace_free_builtin_struts:
 * @workspace: The workspace.
 *
 * Frees the struts list set with cobiwm_workspace_set_builtin_struts
 */
static void
workspace_free_builtin_struts (CobiwmWorkspace *workspace)
{
  if (workspace->builtin_struts == NULL)
    return;

  g_slist_foreach (workspace->builtin_struts, free_this, NULL);
  g_slist_free (workspace->builtin_struts);
  workspace->builtin_struts = NULL;
}

/* Ensure that the workspace is empty by making sure that
 * all of our windows are on-all-workspaces. */
static void
assert_workspace_empty (CobiwmWorkspace *workspace)
{
  GList *l;
  for (l = workspace->windows; l != NULL; l = l->next)
    {
      CobiwmWindow *window = l->data;
      g_assert (window->on_all_workspaces);
    }
}

void
cobiwm_workspace_remove (CobiwmWorkspace *workspace)
{
  CobiwmScreen *screen;
  int i;

  g_return_if_fail (workspace != workspace->screen->active_workspace);

  assert_workspace_empty (workspace);

  screen = workspace->screen;

  workspace->screen->workspaces =
    g_list_remove (workspace->screen->workspaces, workspace);

  g_free (workspace->work_area_monitor);

  g_list_free (workspace->mru_list);
  g_list_free (workspace->list_containing_self);

  workspace_free_builtin_struts (workspace);

  /* screen.c:update_num_workspaces(), which calls us, removes windows from
   * workspaces first, which can cause the workareas on the workspace to be
   * invalidated (and hence for struts/regions/edges to be freed).
   * So, no point trying to double free it; that causes a crash
   * anyway.  #361804.
   */

  if (!workspace->work_areas_invalid)
    {
      workspace_free_all_struts (workspace);
      for (i = 0; i < screen->n_monitor_infos; i++)
        cobiwm_rectangle_free_list_and_elements (workspace->monitor_region[i]);
      g_free (workspace->monitor_region);
      cobiwm_rectangle_free_list_and_elements (workspace->screen_region);
      cobiwm_rectangle_free_list_and_elements (workspace->screen_edges);
      cobiwm_rectangle_free_list_and_elements (workspace->monitor_edges);
    }

  g_object_unref (workspace);

  /* don't bother to reset names, pagers can just ignore
   * extra ones
   */
}

void
cobiwm_workspace_add_window (CobiwmWorkspace *workspace,
                           CobiwmWindow    *window)
{
  g_assert (g_list_find (workspace->mru_list, window) == NULL);
  workspace->mru_list = g_list_prepend (workspace->mru_list, window);

  workspace->windows = g_list_prepend (workspace->windows, window);

  if (window->struts)
    {
      cobiwm_topic (COBIWM_DEBUG_WORKAREA,
                  "Invalidating work area of workspace %d since we're adding window %s to it\n",
                  cobiwm_workspace_index (workspace), window->desc);
      cobiwm_workspace_invalidate_work_area (workspace);
    }

  g_signal_emit (workspace, signals[WINDOW_ADDED], 0, window);
  g_object_notify_by_pspec (G_OBJECT (workspace), obj_props[PROP_N_WINDOWS]);
}

void
cobiwm_workspace_remove_window (CobiwmWorkspace *workspace,
                              CobiwmWindow    *window)
{
  workspace->windows = g_list_remove (workspace->windows, window);

  workspace->mru_list = g_list_remove (workspace->mru_list, window);
  g_assert (g_list_find (workspace->mru_list, window) == NULL);

  if (window->struts)
    {
      cobiwm_topic (COBIWM_DEBUG_WORKAREA,
                  "Invalidating work area of workspace %d since we're removing window %s from it\n",
                  cobiwm_workspace_index (workspace), window->desc);
      cobiwm_workspace_invalidate_work_area (workspace);
    }

  g_signal_emit (workspace, signals[WINDOW_REMOVED], 0, window);
  g_object_notify (G_OBJECT (workspace), "n-windows");
}

void
cobiwm_workspace_relocate_windows (CobiwmWorkspace *workspace,
                                 CobiwmWorkspace *new_home)
{
  GList *copy, *l;

  g_return_if_fail (workspace != new_home);

  /* can't modify list we're iterating over */
  copy = g_list_copy (workspace->windows);

  for (l = copy; l != NULL; l = l->next)
    {
      CobiwmWindow *window = l->data;

      if (!window->on_all_workspaces)
        cobiwm_window_change_workspace (window, new_home);
    }

  g_list_free (copy);

  assert_workspace_empty (workspace);
}

void
cobiwm_workspace_queue_calc_showing  (CobiwmWorkspace *workspace)
{
  GList *l;

  for (l = workspace->windows; l != NULL; l = l->next)
    cobiwm_window_queue (l->data, COBIWM_QUEUE_CALC_SHOWING);
}

static void
workspace_switch_sound(CobiwmWorkspace *from,
                       CobiwmWorkspace *to)
{
#ifdef HAVE_LIBCANBERRA
  CobiwmWorkspaceLayout layout;
  int i, nw, x, y, fi, ti;
  const char *e;

  nw = cobiwm_screen_get_n_workspaces(from->screen);
  fi = cobiwm_workspace_index(from);
  ti = cobiwm_workspace_index(to);

  cobiwm_screen_calc_workspace_layout(from->screen,
                                    nw,
                                    fi,
                                    &layout);

  for (i = 0; i < nw; i++)
    if (layout.grid[i] == ti)
      break;

  if (i >= nw)
    {
      cobiwm_bug("Failed to find destination workspace in layout\n");
      goto finish;
    }

  y = i / layout.cols;
  x = i % layout.cols;

  /* We priorize horizontal over vertical movements here. The
     rationale for this is that horizontal movements are probably more
     interesting for sound effects because speakers are usually
     positioned on a horizontal and not a vertical axis. i.e. your
     spatial "Woosh!" effects will easily be able to encode horizontal
     movement but not such much vertical movement. */

  if (x < layout.current_col)
    e = "desktop-switch-left";
  else if (x > layout.current_col)
    e = "desktop-switch-right";
  else if (y < layout.current_row)
    e = "desktop-switch-up";
  else if (y > layout.current_row)
    e = "desktop-switch-down";
  else
    {
      cobiwm_bug("Uh, origin and destination workspace at same logic position!\n");
      goto finish;
    }

  ca_context_play(ca_gtk_context_get(), 1,
                  CA_PROP_EVENT_ID, e,
                  CA_PROP_EVENT_DESCRIPTION, "Desktop switched",
                  CA_PROP_CANBERRA_CACHE_CONTROL, "permanent",
                  NULL);

 finish:
  cobiwm_screen_free_workspace_layout (&layout);
#endif /* HAVE_LIBCANBERRA */
}

/**
 * cobiwm_workspace_activate_with_focus:
 * @workspace: a #CobiwmWorkspace
 * @focus_this: the #CobiwmWindow to be focused, or %NULL
 * @timestamp: timestamp for @focus_this
 *
 * Switches to @workspace and possibly activates the window @focus_this.
 *
 * The window @focus_this is activated by calling cobiwm_window_activate()
 * which will unminimize it and transient parents, raise it and give it
 * the focus.
 *
 * If a window is currently being moved by the user, it will be
 * moved to @workspace.
 *
 * The advantage of calling this function instead of cobiwm_workspace_activate()
 * followed by cobiwm_window_activate() is that it happens as a unit, so
 * no other window gets focused first before @focus_this.
 */
void
cobiwm_workspace_activate_with_focus (CobiwmWorkspace *workspace,
                                    CobiwmWindow    *focus_this,
                                    guint32        timestamp)
{
  CobiwmWorkspace  *old;
  CobiwmWindow     *move_window;
  CobiwmScreen     *screen;
  CobiwmDisplay    *display;
  CobiwmCompositor *comp;
  CobiwmWorkspaceLayout layout1, layout2;
  gint num_workspaces, current_space, new_space;
  CobiwmMotionDirection direction;

  cobiwm_verbose ("Activating workspace %d\n",
                cobiwm_workspace_index (workspace));

  if (workspace->screen->active_workspace == workspace)
    return;

  /* Free any cached pointers to the workspaces's edges from
   * a current resize or move operation */
  cobiwm_display_cleanup_edges (workspace->screen->display);

  if (workspace->screen->active_workspace)
    workspace_switch_sound (workspace->screen->active_workspace, workspace);

  /* Note that old can be NULL; e.g. when starting up */
  old = workspace->screen->active_workspace;

  workspace->screen->active_workspace = workspace;

  cobiwm_screen_set_active_workspace_hint (workspace->screen);

  /* If the "show desktop" mode is active for either the old workspace
   * or the new one *but not both*, then update the
   * _net_showing_desktop hint
   */
  if (old && (old->showing_desktop != workspace->showing_desktop))
    cobiwm_screen_update_showing_desktop_hint (workspace->screen);

  if (old == NULL)
    return;

  move_window = NULL;
  if (cobiwm_grab_op_is_moving (workspace->screen->display->grab_op))
    move_window = workspace->screen->display->grab_window;

  if (move_window != NULL)
    {
      /* We put the window on the new workspace, flip spaces,
       * then remove from old workspace, so the window
       * never gets unmapped and we maintain the button grab
       * on it.
       *
       * \bug  This comment appears to be the reverse of what happens
       */
      if (!cobiwm_window_located_on_workspace (move_window, workspace))
        cobiwm_window_change_workspace (move_window, workspace);
    }

  cobiwm_workspace_queue_calc_showing (old);
  cobiwm_workspace_queue_calc_showing (workspace);

   /*
    * Notify the compositor that the active workspace is changing.
    */
   screen = workspace->screen;
   display = cobiwm_screen_get_display (screen);
   comp = cobiwm_display_get_compositor (display);
   direction = 0;

   current_space = cobiwm_workspace_index (old);
   new_space     = cobiwm_workspace_index (workspace);
   num_workspaces = cobiwm_screen_get_n_workspaces (workspace->screen);
   cobiwm_screen_calc_workspace_layout (workspace->screen, num_workspaces,
                                      current_space, &layout1);

   cobiwm_screen_calc_workspace_layout (workspace->screen, num_workspaces,
                                      new_space, &layout2);

   if (cobiwm_get_locale_direction () == COBIWM_LOCALE_DIRECTION_RTL)
     {
       if (layout1.current_col > layout2.current_col)
         direction = COBIWM_MOTION_RIGHT;
       else if (layout1.current_col < layout2.current_col)
         direction = COBIWM_MOTION_LEFT;
     }
   else
    {
       if (layout1.current_col < layout2.current_col)
         direction = COBIWM_MOTION_RIGHT;
       else if (layout1.current_col > layout2.current_col)
         direction = COBIWM_MOTION_LEFT;
    }

   if (layout1.current_row < layout2.current_row)
     {
       if (!direction)
         direction = COBIWM_MOTION_DOWN;
       else if (direction == COBIWM_MOTION_RIGHT)
         direction = COBIWM_MOTION_DOWN_RIGHT;
       else
         direction = COBIWM_MOTION_DOWN_LEFT;
     }

   if (layout1.current_row > layout2.current_row)
     {
       if (!direction)
         direction = COBIWM_MOTION_UP;
       else if (direction == COBIWM_MOTION_RIGHT)
         direction = COBIWM_MOTION_UP_RIGHT;
       else
         direction = COBIWM_MOTION_UP_LEFT;
     }

   cobiwm_screen_free_workspace_layout (&layout1);
   cobiwm_screen_free_workspace_layout (&layout2);

   cobiwm_compositor_switch_workspace (comp, old, workspace, direction);

  /* This needs to be done after telling the compositor we are switching
   * workspaces since focusing a window will cause it to be immediately
   * shown and that would confuse the compositor if it didn't know we
   * were in a workspace switch.
   */
  if (focus_this)
    {
      cobiwm_window_activate (focus_this, timestamp);
    }
  else if (move_window)
    {
      cobiwm_window_raise (move_window);
    }
  else
    {
      cobiwm_topic (COBIWM_DEBUG_FOCUS, "Focusing default window on new workspace\n");
      cobiwm_workspace_focus_default_window (workspace, NULL, timestamp);
    }

   /* Emit switched signal from screen.c */
   cobiwm_screen_workspace_switched (screen, current_space, new_space, direction);
}

void
cobiwm_workspace_activate (CobiwmWorkspace *workspace,
                         guint32        timestamp)
{
  cobiwm_workspace_activate_with_focus (workspace, NULL, timestamp);
}

int
cobiwm_workspace_index (CobiwmWorkspace *workspace)
{
  int ret;

  ret = g_list_index (workspace->screen->workspaces, workspace);

  if (ret < 0)
    cobiwm_bug ("Workspace does not exist to index!\n");

  return ret;
}

void
cobiwm_workspace_index_changed (CobiwmWorkspace *workspace)
{
  GList *l;
  for (l = workspace->windows; l != NULL; l = l->next)
    {
      CobiwmWindow *win = l->data;
      cobiwm_window_current_workspace_changed (win);
    }

  g_object_notify_by_pspec (G_OBJECT (workspace), obj_props[PROP_WORKSPACE_INDEX]);
}

/**
 * cobiwm_workspace_list_windows:
 * @workspace: a #CobiwmWorkspace
 *
 * Gets windows contained on the workspace, including workspace->windows
 * and also sticky windows. Override-redirect windows are not included.
 *
 * Return value: (transfer container) (element-type CobiwmWindow): the list of windows.
 */
GList*
cobiwm_workspace_list_windows (CobiwmWorkspace *workspace)
{
  GSList *display_windows, *l;
  GList *workspace_windows;

  display_windows = cobiwm_display_list_windows (workspace->screen->display,
                                               COBIWM_LIST_DEFAULT);

  workspace_windows = NULL;
  for (l = display_windows; l != NULL; l = l->next)
    {
      CobiwmWindow *window = l->data;

      if (cobiwm_window_located_on_workspace (window, workspace))
        workspace_windows = g_list_prepend (workspace_windows,
                                            window);
    }

  g_slist_free (display_windows);

  return workspace_windows;
}

void
cobiwm_workspace_invalidate_work_area (CobiwmWorkspace *workspace)
{
  GList *windows, *l;
  int i;

  if (workspace->work_areas_invalid)
    {
      cobiwm_topic (COBIWM_DEBUG_WORKAREA,
                  "Work area for workspace %d is already invalid\n",
                  cobiwm_workspace_index (workspace));
      return;
    }

  cobiwm_topic (COBIWM_DEBUG_WORKAREA,
              "Invalidating work area for workspace %d\n",
              cobiwm_workspace_index (workspace));

  /* If we are in the middle of a resize or move operation, we
   * might have cached pointers to the workspace's edges */
  if (workspace == workspace->screen->active_workspace)
    cobiwm_display_cleanup_edges (workspace->screen->display);

  g_free (workspace->work_area_monitor);
  workspace->work_area_monitor = NULL;

  workspace_free_all_struts (workspace);

  for (i = 0; i < workspace->screen->n_monitor_infos; i++)
    cobiwm_rectangle_free_list_and_elements (workspace->monitor_region[i]);
  g_free (workspace->monitor_region);
  cobiwm_rectangle_free_list_and_elements (workspace->screen_region);
  cobiwm_rectangle_free_list_and_elements (workspace->screen_edges);
  cobiwm_rectangle_free_list_and_elements (workspace->monitor_edges);
  workspace->monitor_region = NULL;
  workspace->screen_region = NULL;
  workspace->screen_edges = NULL;
  workspace->monitor_edges = NULL;

  workspace->work_areas_invalid = TRUE;

  /* redo the size/position constraints on all windows */
  windows = cobiwm_workspace_list_windows (workspace);

  for (l = windows; l != NULL; l = l->next)
    {
      CobiwmWindow *w = l->data;
      cobiwm_window_queue (w, COBIWM_QUEUE_MOVE_RESIZE);
    }

  g_list_free (windows);

  cobiwm_screen_queue_workarea_recalc (workspace->screen);
}

static CobiwmStrut *
copy_strut(CobiwmStrut *original)
{
  return g_memdup(original, sizeof(CobiwmStrut));
}

static GSList *
copy_strut_list(GSList *original)
{
  GSList *result = NULL;

  for (; original != NULL; original = original->next)
    result = g_slist_prepend (result, copy_strut (original->data));

  return g_slist_reverse (result);
}

static void
ensure_work_areas_validated (CobiwmWorkspace *workspace)
{
  GList         *windows;
  GList         *tmp;
  CobiwmRectangle  work_area;
  int            i;  /* C89 absolutely sucks... */

  if (!workspace->work_areas_invalid)
    return;

  g_assert (workspace->all_struts == NULL);
  g_assert (workspace->monitor_region == NULL);
  g_assert (workspace->screen_region == NULL);
  g_assert (workspace->screen_edges == NULL);
  g_assert (workspace->monitor_edges == NULL);

  /* STEP 1: Get the list of struts */

  workspace->all_struts = copy_strut_list (workspace->builtin_struts);

  windows = cobiwm_workspace_list_windows (workspace);
  for (tmp = windows; tmp != NULL; tmp = tmp->next)
    {
      CobiwmWindow *win = tmp->data;
      GSList *s_iter;

      for (s_iter = win->struts; s_iter != NULL; s_iter = s_iter->next) {
        workspace->all_struts = g_slist_prepend (workspace->all_struts,
                                                 copy_strut(s_iter->data));
      }
    }
  g_list_free (windows);

  /* STEP 2: Get the maximal/spanning rects for the onscreen and
   *         on-single-monitor regions
   */
  g_assert (workspace->monitor_region == NULL);
  g_assert (workspace->screen_region   == NULL);

  workspace->monitor_region = g_new (GList*,
                                      workspace->screen->n_monitor_infos);
  for (i = 0; i < workspace->screen->n_monitor_infos; i++)
    {
      workspace->monitor_region[i] =
        cobiwm_rectangle_get_minimal_spanning_set_for_region (
          &workspace->screen->monitor_infos[i].rect,
          workspace->all_struts);
    }
  workspace->screen_region =
    cobiwm_rectangle_get_minimal_spanning_set_for_region (
      &workspace->screen->rect,
      workspace->all_struts);

  /* STEP 3: Get the work areas (region-to-maximize-to) for the screen and
   *         monitors.
   */
  work_area = workspace->screen->rect;  /* start with the screen */
  if (workspace->screen_region == NULL)
    work_area = cobiwm_rect (0, 0, -1, -1);
  else
    cobiwm_rectangle_clip_to_region (workspace->screen_region,
                                   FIXED_DIRECTION_NONE,
                                   &work_area);

  /* Lots of paranoia checks, forcing work_area_screen to be sane */
#define MIN_SANE_AREA 100
  if (work_area.width < MIN_SANE_AREA)
    {
      cobiwm_warning ("struts occupy an unusually large percentage of the screen; "
                    "available remaining width = %d < %d",
                    work_area.width, MIN_SANE_AREA);
      if (work_area.width < 1)
        {
          work_area.x = (workspace->screen->rect.width - MIN_SANE_AREA)/2;
          work_area.width = MIN_SANE_AREA;
        }
      else
        {
          int amount = (MIN_SANE_AREA - work_area.width)/2;
          work_area.x     -=   amount;
          work_area.width += 2*amount;
        }
    }
  if (work_area.height < MIN_SANE_AREA)
    {
      cobiwm_warning ("struts occupy an unusually large percentage of the screen; "
                    "available remaining height = %d < %d",
                    work_area.height, MIN_SANE_AREA);
      if (work_area.height < 1)
        {
          work_area.y = (workspace->screen->rect.height - MIN_SANE_AREA)/2;
          work_area.height = MIN_SANE_AREA;
        }
      else
        {
          int amount = (MIN_SANE_AREA - work_area.height)/2;
          work_area.y      -=   amount;
          work_area.height += 2*amount;
        }
    }
  workspace->work_area_screen = work_area;
  cobiwm_topic (COBIWM_DEBUG_WORKAREA,
              "Computed work area for workspace %d: %d,%d %d x %d\n",
              cobiwm_workspace_index (workspace),
              workspace->work_area_screen.x,
              workspace->work_area_screen.y,
              workspace->work_area_screen.width,
              workspace->work_area_screen.height);

  /* Now find the work areas for each monitor */
  g_free (workspace->work_area_monitor);
  workspace->work_area_monitor = g_new (CobiwmRectangle,
                                         workspace->screen->n_monitor_infos);

  for (i = 0; i < workspace->screen->n_monitor_infos; i++)
    {
      work_area = workspace->screen->monitor_infos[i].rect;

      if (workspace->monitor_region[i] == NULL)
        /* FIXME: constraints.c untested with this, but it might be nice for
         * a screen reader or magnifier.
         */
        work_area = cobiwm_rect (work_area.x, work_area.y, -1, -1);
      else
        cobiwm_rectangle_clip_to_region (workspace->monitor_region[i],
                                       FIXED_DIRECTION_NONE,
                                       &work_area);

      workspace->work_area_monitor[i] = work_area;
      cobiwm_topic (COBIWM_DEBUG_WORKAREA,
                  "Computed work area for workspace %d "
                  "monitor %d: %d,%d %d x %d\n",
                  cobiwm_workspace_index (workspace),
                  i,
                  workspace->work_area_monitor[i].x,
                  workspace->work_area_monitor[i].y,
                  workspace->work_area_monitor[i].width,
                  workspace->work_area_monitor[i].height);
    }

  /* STEP 4: Make sure the screen_region is nonempty (separate from step 2
   *         since it relies on step 3).
   */
  if (workspace->screen_region == NULL)
    {
      CobiwmRectangle *nonempty_region;
      nonempty_region = g_new (CobiwmRectangle, 1);
      *nonempty_region = workspace->work_area_screen;
      workspace->screen_region = g_list_prepend (NULL, nonempty_region);
    }

  /* STEP 5: Cache screen and monitor edges for edge resistance and snapping */
  g_assert (workspace->screen_edges    == NULL);
  g_assert (workspace->monitor_edges  == NULL);
  workspace->screen_edges =
    cobiwm_rectangle_find_onscreen_edges (&workspace->screen->rect,
                                        workspace->all_struts);
  tmp = NULL;
  for (i = 0; i < workspace->screen->n_monitor_infos; i++)
    tmp = g_list_prepend (tmp, &workspace->screen->monitor_infos[i].rect);
  workspace->monitor_edges =
    cobiwm_rectangle_find_nonintersected_monitor_edges (tmp,
                                                       workspace->all_struts);
  g_list_free (tmp);

  /* We're all done, YAAY!  Record that everything has been validated. */
  workspace->work_areas_invalid = FALSE;
}

static gboolean
strut_lists_equal (GSList *l,
                   GSList *m)
{
  for (; l && m; l = l->next, m = m->next)
    {
      CobiwmStrut *a = l->data;
      CobiwmStrut *b = m->data;

      if (a->side != b->side ||
          !cobiwm_rectangle_equal (&a->rect, &b->rect))
        return FALSE;
    }

  return l == NULL && m == NULL;
}

/**
 * cobiwm_workspace_set_builtin_struts:
 * @workspace: a #CobiwmWorkspace
 * @struts: (element-type Cobiwm.Strut) (transfer none): list of #CobiwmStrut
 *
 * Sets a list of struts that will be used in addition to the struts
 * of the windows in the workspace when computing the work area of
 * the workspace.
 */
void
cobiwm_workspace_set_builtin_struts (CobiwmWorkspace *workspace,
                                   GSList        *struts)
{
  CobiwmScreen *screen = workspace->screen;
  GSList *l;

  for (l = struts; l; l = l->next)
    {
      CobiwmStrut *strut = l->data;
      int idx = cobiwm_screen_get_monitor_index_for_rect (screen, &strut->rect);

      switch (strut->side)
        {
        case COBIWM_SIDE_TOP:
          if (cobiwm_screen_get_monitor_neighbor (screen, idx, COBIWM_SCREEN_UP))
            continue;

          strut->rect.height += strut->rect.y;
          strut->rect.y = 0;
          break;
        case COBIWM_SIDE_BOTTOM:
          if (cobiwm_screen_get_monitor_neighbor (screen, idx, COBIWM_SCREEN_DOWN))
            continue;

          strut->rect.height = screen->rect.height - strut->rect.y;
          break;
        case COBIWM_SIDE_LEFT:
          if (cobiwm_screen_get_monitor_neighbor (screen, idx, COBIWM_SCREEN_LEFT))
            continue;

          strut->rect.width += strut->rect.x;
          strut->rect.x = 0;
          break;
        case COBIWM_SIDE_RIGHT:
          if (cobiwm_screen_get_monitor_neighbor (screen, idx, COBIWM_SCREEN_RIGHT))
            continue;

          strut->rect.width = screen->rect.width - strut->rect.x;
          break;
        }
    }

  /* Reordering doesn't actually matter, so we don't catch all
   * no-impact changes, but this is just a (possibly unnecessary
   * anyways) optimization */
  if (strut_lists_equal (struts, workspace->builtin_struts))
    return;

  workspace_free_builtin_struts (workspace);
  workspace->builtin_struts = copy_strut_list (struts);

  cobiwm_workspace_invalidate_work_area (workspace);
}

/**
 * cobiwm_workspace_get_work_area_for_monitor:
 * @workspace: a #CobiwmWorkspace
 * @which_monitor: a monitor index
 * @area: (out): location to store the work area
 *
 * Stores the work area for @which_monitor on @workspace
 * in @area.
 */
void
cobiwm_workspace_get_work_area_for_monitor (CobiwmWorkspace *workspace,
                                          int            which_monitor,
                                          CobiwmRectangle *area)
{
  g_assert (which_monitor >= 0);

  ensure_work_areas_validated (workspace);
  g_assert (which_monitor < workspace->screen->n_monitor_infos);

  *area = workspace->work_area_monitor[which_monitor];
}

/**
 * cobiwm_workspace_get_work_area_all_monitors:
 * @workspace: a #CobiwmWorkspace
 * @area: (out): location to store the work area
 *
 * Stores the work area in @area.
 */
void
cobiwm_workspace_get_work_area_all_monitors (CobiwmWorkspace *workspace,
                                           CobiwmRectangle *area)
{
  ensure_work_areas_validated (workspace);

  *area = workspace->work_area_screen;
}

GList*
cobiwm_workspace_get_onscreen_region (CobiwmWorkspace *workspace)
{
  ensure_work_areas_validated (workspace);

  return workspace->screen_region;
}

GList*
cobiwm_workspace_get_onmonitor_region (CobiwmWorkspace *workspace,
                                     int            which_monitor)
{
  ensure_work_areas_validated (workspace);

  return workspace->monitor_region[which_monitor];
}

#ifdef WITH_VERBOSE_MODE
static const char *
cobiwm_motion_direction_to_string (CobiwmMotionDirection direction)
{
  switch (direction)
    {
    case COBIWM_MOTION_UP:
      return "Up";
    case COBIWM_MOTION_DOWN:
      return "Down";
    case COBIWM_MOTION_LEFT:
      return "Left";
    case COBIWM_MOTION_RIGHT:
      return "Right";
    case COBIWM_MOTION_UP_RIGHT:
      return "Up-Right";
    case COBIWM_MOTION_DOWN_RIGHT:
      return "Down-Right";
    case COBIWM_MOTION_UP_LEFT:
      return "Up-Left";
    case COBIWM_MOTION_DOWN_LEFT:
      return "Down-Left";
    }

  return "Unknown";
}
#endif /* WITH_VERBOSE_MODE */

/**
 * cobiwm_workspace_get_neighbor:
 * @workspace: a #CobiwmWorkspace
 * @direction: a #CobiwmMotionDirection, relative to @workspace
 *
 * Calculate and retrive the workspace that is next to @workspace,
 * according to @direction and the current workspace layout, as set
 * by cobiwm_screen_override_workspace_layout().
 *
 * Returns: (transfer none): the workspace next to @workspace, or
 *   @workspace itself if the neighbor would be outside the layout
 */
CobiwmWorkspace*
cobiwm_workspace_get_neighbor (CobiwmWorkspace      *workspace,
                             CobiwmMotionDirection direction)
{
  CobiwmWorkspaceLayout layout;
  int i, current_space, num_workspaces;
  gboolean ltr;

  current_space = cobiwm_workspace_index (workspace);
  num_workspaces = cobiwm_screen_get_n_workspaces (workspace->screen);
  cobiwm_screen_calc_workspace_layout (workspace->screen, num_workspaces,
                                     current_space, &layout);

  cobiwm_verbose ("Getting neighbor of %d in direction %s\n",
                current_space, cobiwm_motion_direction_to_string (direction));

  ltr = (cobiwm_get_locale_direction () == COBIWM_LOCALE_DIRECTION_LTR);

  switch (direction)
    {
    case COBIWM_MOTION_LEFT:
      layout.current_col -= ltr ? 1 : -1;
      break;
    case COBIWM_MOTION_RIGHT:
      layout.current_col += ltr ? 1 : -1;
      break;
    case COBIWM_MOTION_UP:
      layout.current_row -= 1;
      break;
    case COBIWM_MOTION_DOWN:
      layout.current_row += 1;
      break;
    default:;
    }

  if (layout.current_col < 0)
    layout.current_col = 0;
  if (layout.current_col >= layout.cols)
    layout.current_col = layout.cols - 1;
  if (layout.current_row < 0)
    layout.current_row = 0;
  if (layout.current_row >= layout.rows)
    layout.current_row = layout.rows - 1;

  i = layout.grid[layout.current_row * layout.cols + layout.current_col];

  if (i < 0)
    i = current_space;

  if (i >= num_workspaces)
    cobiwm_bug ("calc_workspace_layout left an invalid (too-high) workspace number %d in the grid\n",
              i);

  cobiwm_verbose ("Neighbor workspace is %d at row %d col %d\n",
                i, layout.current_row, layout.current_col);

  cobiwm_screen_free_workspace_layout (&layout);

  return cobiwm_screen_get_workspace_by_index (workspace->screen, i);
}

const char*
cobiwm_workspace_get_name (CobiwmWorkspace *workspace)
{
  return cobiwm_prefs_get_workspace_name (cobiwm_workspace_index (workspace));
}

void
cobiwm_workspace_focus_default_window (CobiwmWorkspace *workspace,
                                     CobiwmWindow    *not_this_one,
                                     guint32        timestamp)
{
  if (timestamp == CurrentTime)
    cobiwm_warning ("CurrentTime used to choose focus window; "
                  "focus window may not be correct.\n");

  if (cobiwm_prefs_get_focus_mode () == G_DESKTOP_FOCUS_MODE_CLICK ||
      !workspace->screen->display->mouse_mode)
    focus_ancestor_or_top_window (workspace, not_this_one, timestamp);
  else
    {
      CobiwmWindow * window;
      window = cobiwm_screen_get_mouse_window (workspace->screen, not_this_one);
      if (window &&
          window->type != COBIWM_WINDOW_DOCK &&
          window->type != COBIWM_WINDOW_DESKTOP)
        {
          if (timestamp == CurrentTime)
            {

              /* We would like for this to never happen.  However, if
               * it does happen then we kludge since using CurrentTime
               * can mean ugly race conditions--and we can avoid these
               * by allowing EnterNotify events (which come with
               * timestamps) to handle focus.
               */

              cobiwm_topic (COBIWM_DEBUG_FOCUS,
                          "Not focusing mouse window %s because EnterNotify events should handle that\n", window->desc);
            }
          else
            {
              cobiwm_topic (COBIWM_DEBUG_FOCUS,
                          "Focusing mouse window %s\n", window->desc);
              cobiwm_window_focus (window, timestamp);
            }

          if (workspace->screen->display->autoraise_window != window &&
              cobiwm_prefs_get_auto_raise ())
            {
              cobiwm_display_queue_autoraise_callback (workspace->screen->display,
                                                     window);
            }
        }
      else if (cobiwm_prefs_get_focus_mode () == G_DESKTOP_FOCUS_MODE_SLOPPY)
        focus_ancestor_or_top_window (workspace, not_this_one, timestamp);
      else if (cobiwm_prefs_get_focus_mode () == G_DESKTOP_FOCUS_MODE_MOUSE)
        {
          cobiwm_topic (COBIWM_DEBUG_FOCUS,
                      "Setting focus to no_focus_window, since no valid "
                      "window to focus found.\n");
          cobiwm_display_focus_the_no_focus_window (workspace->screen->display,
                                                  workspace->screen,
                                                  timestamp);
        }
    }
}

static gboolean
record_ancestor (CobiwmWindow *window,
                 void       *data)
{
  CobiwmWindow **result = data;

  *result = window;
  return FALSE; /* quit with the first ancestor we find */
}

/* Focus ancestor of not_this_one if there is one */
static void
focus_ancestor_or_top_window (CobiwmWorkspace *workspace,
                              CobiwmWindow    *not_this_one,
                              guint32        timestamp)
{
  CobiwmWindow *window = NULL;

  if (not_this_one)
    cobiwm_topic (COBIWM_DEBUG_FOCUS,
                "Focusing MRU window excluding %s\n", not_this_one->desc);
  else
    cobiwm_topic (COBIWM_DEBUG_FOCUS,
                "Focusing MRU window\n");

  /* First, check to see if we need to focus an ancestor of a window */
  if (not_this_one)
    {
      CobiwmWindow *ancestor;
      ancestor = NULL;
      cobiwm_window_foreach_ancestor (not_this_one, record_ancestor, &ancestor);
      if (ancestor != NULL &&
          cobiwm_window_located_on_workspace (ancestor, workspace) &&
          cobiwm_window_showing_on_its_workspace (ancestor))
        {
          cobiwm_topic (COBIWM_DEBUG_FOCUS,
                      "Focusing %s, ancestor of %s\n",
                      ancestor->desc, not_this_one->desc);

          cobiwm_window_focus (ancestor, timestamp);

          /* Also raise the window if in click-to-focus */
          if (cobiwm_prefs_get_focus_mode () == G_DESKTOP_FOCUS_MODE_CLICK)
            cobiwm_window_raise (ancestor);

          return;
        }
    }

  window = cobiwm_stack_get_default_focus_window (workspace->screen->stack,
                                                workspace,
                                                not_this_one);

  if (window)
    {
      cobiwm_topic (COBIWM_DEBUG_FOCUS,
                  "Focusing workspace MRU window %s\n", window->desc);

      cobiwm_window_focus (window, timestamp);

      /* Also raise the window if in click-to-focus */
      if (cobiwm_prefs_get_focus_mode () == G_DESKTOP_FOCUS_MODE_CLICK)
        cobiwm_window_raise (window);
    }
  else
    {
      cobiwm_topic (COBIWM_DEBUG_FOCUS, "No MRU window to focus found; focusing no_focus_window.\n");
      cobiwm_display_focus_the_no_focus_window (workspace->screen->display,
                                              workspace->screen,
                                              timestamp);
    }
}

/**
 * cobiwm_workspace_get_screen:
 * @workspace: a #CobiwmWorkspace
 *
 * Gets the #CobiwmScreen that the workspace is part of.
 *
 * Return value: (transfer none): the #CobiwmScreen for the workspace
 */
CobiwmScreen *
cobiwm_workspace_get_screen (CobiwmWorkspace *workspace)
{
  return workspace->screen;
}

