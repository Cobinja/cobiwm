/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/**
 * SECTION:compositor
 * @Title: CobiwmCompositor
 * @Short_Description: Compositor API
 *
 * At a high-level, a window is not-visible or visible. When a
 * window is added (with cobiwm_compositor_add_window()) it is not visible.
 * cobiwm_compositor_show_window() indicates a transition from not-visible to
 * visible. Some of the reasons for this:
 *
 * - Window newly created
 * - Window is unminimized
 * - Window is moved to the current desktop
 * - Window was made sticky
 *
 * cobiwm_compositor_hide_window() indicates that the window has transitioned from
 * visible to not-visible. Some reasons include:
 *
 * - Window was destroyed
 * - Window is minimized
 * - Window is moved to a different desktop
 * - Window no longer sticky.
 *
 * Note that combinations are possible - a window might have first
 * been minimized and then moved to a different desktop. The 'effect' parameter
 * to cobiwm_compositor_show_window() and cobiwm_compositor_hide_window() is a hint
 * as to the appropriate effect to show the user and should not
 * be considered to be indicative of a state change.
 *
 * When the active workspace is changed, cobiwm_compositor_switch_workspace() is
 * called first, then cobiwm_compositor_show_window() and
 * cobiwm_compositor_hide_window() are called individually for each window
 * affected, with an effect of COBIWM_COMP_EFFECT_NONE.
 * If hiding windows will affect the switch workspace animation, the
 * compositor needs to delay hiding the windows until the switch
 * workspace animation completes.
 *
 * # Containers #
 *
 * There's two containers in the stage that are used to place window actors, here
 * are listed in the order in which they are painted:
 *
 * - window group, accessible with cobiwm_get_window_group_for_screen()
 * - top window group, accessible with cobiwm_get_top_window_group_for_screen()
 *
 * Cobiwm will place actors representing windows in the window group, except for
 * override-redirect windows (ie. popups and menus) which will be placed in the
 * top window group.
 */

#include <config.h>

#include <clutter/x11/clutter-x11.h>

#include "core.h"
#include <screen.h>
#include <errors.h>
#include <window.h>
#include "compositor-private.h"
#include <compositor-cobiwm.h>
#include <prefs.h>
#include <main.h>
#include <cobiwm-backend.h>
#include <cobiwm-background-actor.h>
#include <cobiwm-background-group.h>
#include <cobiwm-shadow-factory.h>
#include "cobiwm-window-actor-private.h"
#include "cobiwm-window-group.h"
#include "window-private.h" /* to check window->hidden */
#include "display-private.h" /* for cobiwm_display_lookup_x_window() and cobiwm_display_cancel_touch() */
#include "util-private.h"
#include "frame.h"
#include <X11/extensions/shape.h>
#include <X11/extensions/Xcomposite.h>
#include "cobiwm-sync-ring.h"

#include "backends/x11/cobiwm-backend-x11.h"

#ifdef HAVE_WAYLAND
#include "wayland/cobiwm-wayland-private.h"
#endif

static void sync_actor_stacking (CobiwmCompositor *compositor);

static void
cobiwm_finish_workspace_switch (CobiwmCompositor *compositor)
{
  GList *l;

  /* Finish hiding and showing actors for the new workspace */
  for (l = compositor->windows; l; l = l->next)
    cobiwm_window_actor_sync_visibility (l->data);

  /* Fix up stacking order. */
  sync_actor_stacking (compositor);
}

void
cobiwm_switch_workspace_completed (CobiwmCompositor *compositor)
{
  /* FIXME -- must redo stacking order */
  compositor->switch_workspace_in_progress--;
  if (compositor->switch_workspace_in_progress < 0)
    {
      g_warning ("Error in workspace_switch accounting!");
      compositor->switch_workspace_in_progress = 0;
    }

  if (!compositor->switch_workspace_in_progress)
    cobiwm_finish_workspace_switch (compositor);
}

void
cobiwm_compositor_destroy (CobiwmCompositor *compositor)
{
  clutter_threads_remove_repaint_func (compositor->pre_paint_func_id);
  clutter_threads_remove_repaint_func (compositor->post_paint_func_id);

  if (compositor->have_x11_sync_object)
    cobiwm_sync_ring_destroy ();
}

static void
process_damage (CobiwmCompositor     *compositor,
                XDamageNotifyEvent *event,
                CobiwmWindow         *window)
{
  CobiwmWindowActor *window_actor = COBIWM_WINDOW_ACTOR (cobiwm_window_get_compositor_private (window));
  cobiwm_window_actor_process_x11_damage (window_actor, event);

  compositor->frame_has_updated_xsurfaces = TRUE;
}

/* compat helper */
static CobiwmCompositor *
get_compositor_for_screen (CobiwmScreen *screen)
{
  return screen->display->compositor;
}

/**
 * cobiwm_get_stage_for_screen:
 * @screen: a #CobiwmScreen
 *
 * Returns: (transfer none): The #ClutterStage for the screen
 */
ClutterActor *
cobiwm_get_stage_for_screen (CobiwmScreen *screen)
{
  CobiwmCompositor *compositor = get_compositor_for_screen (screen);
  return compositor->stage;
}

/**
 * cobiwm_get_window_group_for_screen:
 * @screen: a #CobiwmScreen
 *
 * Returns: (transfer none): The window group corresponding to @screen
 */
ClutterActor *
cobiwm_get_window_group_for_screen (CobiwmScreen *screen)
{
  CobiwmCompositor *compositor = get_compositor_for_screen (screen);
  return compositor->window_group;
}

/**
 * cobiwm_get_top_window_group_for_screen:
 * @screen: a #CobiwmScreen
 *
 * Returns: (transfer none): The top window group corresponding to @screen
 */
ClutterActor *
cobiwm_get_top_window_group_for_screen (CobiwmScreen *screen)
{
  CobiwmCompositor *compositor = get_compositor_for_screen (screen);
  return compositor->top_window_group;
}

/**
 * cobiwm_get_feedback_group_for_screen:
 * @screen: a #CobiwmScreen
 *
 * Returns: (transfer none): The feedback group corresponding to @screen
 */
ClutterActor *
cobiwm_get_feedback_group_for_screen (CobiwmScreen *screen)
{
  CobiwmCompositor *compositor = get_compositor_for_screen (screen);
  return compositor->feedback_group;
}

/**
 * cobiwm_get_window_actors:
 * @screen: a #CobiwmScreen
 *
 * Returns: (transfer none) (element-type Clutter.Actor): The set of #CobiwmWindowActor on @screen
 */
GList *
cobiwm_get_window_actors (CobiwmScreen *screen)
{
  CobiwmCompositor *compositor = get_compositor_for_screen (screen);
  return compositor->windows;
}

void
cobiwm_set_stage_input_region (CobiwmScreen   *screen,
                             XserverRegion region)
{
  /* As a wayland compositor we can simply ignore all this trickery
   * for setting an input region on the stage for capturing events in
   * clutter since all input comes to us first and we get to choose
   * who else sees them.
   */
  if (!cobiwm_is_wayland_compositor ())
    {
      CobiwmDisplay *display = screen->display;
      CobiwmCompositor *compositor = display->compositor;
      Display *xdpy = cobiwm_display_get_xdisplay (display);
      Window xstage = clutter_x11_get_stage_window (CLUTTER_STAGE (compositor->stage));

      XFixesSetWindowShapeRegion (xdpy, xstage, ShapeInput, 0, 0, region);

      /* It's generally a good heuristic that when a crossing event is generated because
       * we reshape the overlay, we don't want it to affect focus-follows-mouse focus -
       * it's not the user doing something, it's the environment changing under the user.
       */
      cobiwm_display_add_ignored_crossing_serial (display, XNextRequest (xdpy));
      XFixesSetWindowShapeRegion (xdpy, compositor->output, ShapeInput, 0, 0, region);
    }
}

void
cobiwm_empty_stage_input_region (CobiwmScreen *screen)
{
  /* Using a static region here is a bit hacky, but Cobiwmcity never opens more than
   * one XDisplay, so it works fine. */
  static XserverRegion region = None;

  if (region == None)
    {
      CobiwmDisplay  *display = cobiwm_screen_get_display (screen);
      Display      *xdpy    = cobiwm_display_get_xdisplay (display);
      region = XFixesCreateRegion (xdpy, NULL, 0);
    }

  cobiwm_set_stage_input_region (screen, region);
}

void
cobiwm_focus_stage_window (CobiwmScreen *screen,
                         guint32     timestamp)
{
  ClutterStage *stage;
  Window window;

  stage = CLUTTER_STAGE (cobiwm_get_stage_for_screen (screen));
  if (!stage)
    return;

  window = clutter_x11_get_stage_window (stage);

  if (window == None)
    return;

  cobiwm_display_set_input_focus_xwindow (screen->display,
                                        screen,
                                        window,
                                        timestamp);
}

gboolean
cobiwm_stage_is_focused (CobiwmScreen *screen)
{
  ClutterStage *stage;
  Window window;

  if (cobiwm_is_wayland_compositor ())
    return TRUE;

  stage = CLUTTER_STAGE (cobiwm_get_stage_for_screen (screen));
  if (!stage)
    return FALSE;

  window = clutter_x11_get_stage_window (stage);

  if (window == None)
    return FALSE;

  return (screen->display->focus_xwindow == window);
}

static void
after_stage_paint (ClutterStage *stage,
                   gpointer      data)
{
  CobiwmCompositor *compositor = data;
  GList *l;

  for (l = compositor->windows; l; l = l->next)
    cobiwm_window_actor_post_paint (l->data);

#ifdef HAVE_WAYLAND
  if (cobiwm_is_wayland_compositor ())
    cobiwm_wayland_compositor_paint_finished (cobiwm_wayland_compositor_get_default ());
#endif
}

static void
redirect_windows (CobiwmScreen *screen)
{
  CobiwmDisplay *display       = cobiwm_screen_get_display (screen);
  Display     *xdisplay      = cobiwm_display_get_xdisplay (display);
  Window       xroot         = cobiwm_screen_get_xroot (screen);
  int          screen_number = cobiwm_screen_get_screen_number (screen);
  guint        n_retries;
  guint        max_retries;

  if (cobiwm_get_replace_current_wm ())
    max_retries = 5;
  else
    max_retries = 1;

  n_retries = 0;

  /* Some compositors (like old versions of Cobiwm) might not properly unredirect
   * subwindows before destroying the WM selection window; so we wait a while
   * for such a compositor to exit before giving up.
   */
  while (TRUE)
    {
      cobiwm_error_trap_push (display);
      XCompositeRedirectSubwindows (xdisplay, xroot, CompositeRedirectManual);
      XSync (xdisplay, FALSE);

      if (!cobiwm_error_trap_pop_with_return (display))
        break;

      if (n_retries == max_retries)
        {
          /* This probably means that a non-WM compositor like xcompmgr is running;
           * we have no way to get it to exit */
          cobiwm_fatal (_("Another compositing manager is already running on screen %i on display \"%s\"."),
                      screen_number, display->name);
        }

      n_retries++;
      g_usleep (G_USEC_PER_SEC);
    }
}

void
cobiwm_compositor_manage (CobiwmCompositor *compositor)
{
  CobiwmDisplay *display = compositor->display;
  Display *xdisplay = display->xdisplay;
  CobiwmScreen *screen = display->screen;
  CobiwmBackend *backend = cobiwm_get_backend ();

  cobiwm_screen_set_cm_selection (display->screen);

  compositor->stage = cobiwm_backend_get_stage (backend);

  /* We use connect_after() here to accomodate code in GNOME Shell that,
   * when benchmarking drawing performance, connects to ::after-paint
   * and calls glFinish(). The timing information from that will be
   * more accurate if we hold off until that completes before we signal
   * apps to begin drawing the next frame. If there are no other
   * connections to ::after-paint, connect() vs. connect_after() doesn't
   * matter.
   */
  g_signal_connect_after (CLUTTER_STAGE (compositor->stage), "after-paint",
                          G_CALLBACK (after_stage_paint), compositor);

  clutter_stage_set_sync_delay (CLUTTER_STAGE (compositor->stage), COBIWM_SYNC_DELAY);

  compositor->window_group = cobiwm_window_group_new (screen);
  compositor->top_window_group = cobiwm_window_group_new (screen);
  compositor->feedback_group = cobiwm_window_group_new (screen);

  clutter_actor_add_child (compositor->stage, compositor->window_group);
  clutter_actor_add_child (compositor->stage, compositor->top_window_group);
  clutter_actor_add_child (compositor->stage, compositor->feedback_group);

  if (cobiwm_is_wayland_compositor ())
    {
      /* NB: When running as a wayland compositor we don't need an X
       * composite overlay window, and we don't need to play any input
       * region tricks to redirect events into clutter. */
      compositor->output = None;
    }
  else
    {
      Window xwin;

      compositor->output = screen->composite_overlay_window;

      xwin = cobiwm_backend_x11_get_xwindow (COBIWM_BACKEND_X11 (backend));

      XReparentWindow (xdisplay, xwin, compositor->output, 0, 0);

      cobiwm_empty_stage_input_region (screen);

      /* Make sure there isn't any left-over output shape on the
       * overlay window by setting the whole screen to be an
       * output region.
       *
       * Note: there doesn't seem to be any real chance of that
       *  because the X server will destroy the overlay window
       *  when the last client using it exits.
       */
      XFixesSetWindowShapeRegion (xdisplay, compositor->output, ShapeBounding, 0, 0, None);

      /* Map overlay window before redirecting windows offscreen so we catch their
       * contents until we show the stage.
       */
      XMapWindow (xdisplay, compositor->output);

      compositor->have_x11_sync_object = cobiwm_sync_ring_init (xdisplay);
    }

  redirect_windows (display->screen);

  compositor->plugin_mgr = cobiwm_effect_manager_new (compositor);
}

void
cobiwm_compositor_unmanage (CobiwmCompositor *compositor)
{
  if (!cobiwm_is_wayland_compositor ())
    {
      CobiwmDisplay *display = compositor->display;
      Display *xdisplay = cobiwm_display_get_xdisplay (display);
      Window xroot = display->screen->xroot;

      /* This is the most important part of cleanup - we have to do this
       * before giving up the window manager selection or the next
       * window manager won't be able to redirect subwindows */
      XCompositeUnredirectSubwindows (xdisplay, xroot, CompositeRedirectManual);
    }
}

/**
 * cobiwm_shape_cow_for_window:
 * @compositor: A #CobiwmCompositor
 * @window: (nullable): A #CobiwmWindow to shape the COW for
 *
 * Sets an bounding shape on the COW so that the given window
 * is exposed. If @window is %NULL it clears the shape again.
 *
 * Used so we can unredirect windows, by shaping away the part
 * of the COW, letting the raw window be seen through below.
 */
static void
cobiwm_shape_cow_for_window (CobiwmCompositor *compositor,
                           CobiwmWindow *window)
{
  CobiwmDisplay *display = compositor->display;
  Display *xdisplay = cobiwm_display_get_xdisplay (display);

  if (window == NULL)
    XFixesSetWindowShapeRegion (xdisplay, compositor->output, ShapeBounding, 0, 0, None);
  else
    {
      XserverRegion output_region;
      XRectangle screen_rect, window_bounds;
      int width, height;
      CobiwmRectangle rect;

      cobiwm_window_get_frame_rect (window, &rect);

      window_bounds.x = rect.x;
      window_bounds.y = rect.y;
      window_bounds.width = rect.width;
      window_bounds.height = rect.height;

      cobiwm_screen_get_size (display->screen, &width, &height);
      screen_rect.x = 0;
      screen_rect.y = 0;
      screen_rect.width = width;
      screen_rect.height = height;

      output_region = XFixesCreateRegion (xdisplay, &window_bounds, 1);

      XFixesInvertRegion (xdisplay, output_region, &screen_rect, output_region);
      XFixesSetWindowShapeRegion (xdisplay, compositor->output, ShapeBounding, 0, 0, output_region);
      XFixesDestroyRegion (xdisplay, output_region);
    }
}

static void
set_unredirected_window (CobiwmCompositor *compositor,
                         CobiwmWindow     *window)
{
  if (compositor->unredirected_window == window)
    return;

  if (compositor->unredirected_window != NULL)
    {
      CobiwmWindowActor *window_actor = COBIWM_WINDOW_ACTOR (cobiwm_window_get_compositor_private (compositor->unredirected_window));
      cobiwm_window_actor_set_unredirected (window_actor, FALSE);
    }

  cobiwm_shape_cow_for_window (compositor, window);
  compositor->unredirected_window = window;

  if (compositor->unredirected_window != NULL)
    {
      CobiwmWindowActor *window_actor = COBIWM_WINDOW_ACTOR (cobiwm_window_get_compositor_private (compositor->unredirected_window));
      cobiwm_window_actor_set_unredirected (window_actor, TRUE);
    }
}

void
cobiwm_compositor_add_window (CobiwmCompositor    *compositor,
                            CobiwmWindow        *window)
{
  CobiwmDisplay *display = compositor->display;

  cobiwm_error_trap_push (display);

  cobiwm_window_actor_new (window);
  sync_actor_stacking (compositor);

  cobiwm_error_trap_pop (display);
}

void
cobiwm_compositor_remove_window (CobiwmCompositor *compositor,
                               CobiwmWindow     *window)
{
  CobiwmWindowActor *window_actor = COBIWM_WINDOW_ACTOR (cobiwm_window_get_compositor_private (window));

  if (compositor->unredirected_window == window)
    set_unredirected_window (compositor, NULL);

  cobiwm_window_actor_destroy (window_actor);
}

void
cobiwm_compositor_sync_updates_frozen (CobiwmCompositor *compositor,
                                     CobiwmWindow     *window)
{
  CobiwmWindowActor *window_actor = COBIWM_WINDOW_ACTOR (cobiwm_window_get_compositor_private (window));
  cobiwm_window_actor_sync_updates_frozen (window_actor);
}

void
cobiwm_compositor_queue_frame_drawn (CobiwmCompositor *compositor,
                                   CobiwmWindow     *window,
                                   gboolean        no_delay_frame)
{
  CobiwmWindowActor *window_actor = COBIWM_WINDOW_ACTOR (cobiwm_window_get_compositor_private (window));
  cobiwm_window_actor_queue_frame_drawn (window_actor, no_delay_frame);
}

void
cobiwm_compositor_window_shape_changed (CobiwmCompositor *compositor,
                                      CobiwmWindow     *window)
{
  CobiwmWindowActor *window_actor;
  window_actor = COBIWM_WINDOW_ACTOR (cobiwm_window_get_compositor_private (window));
  if (!window_actor)
    return;

  cobiwm_window_actor_update_shape (window_actor);
}

void
cobiwm_compositor_window_opacity_changed (CobiwmCompositor *compositor,
                                        CobiwmWindow     *window)
{
  CobiwmWindowActor *window_actor;
  window_actor = COBIWM_WINDOW_ACTOR (cobiwm_window_get_compositor_private (window));
  if (!window_actor)
    return;

  cobiwm_window_actor_update_opacity (window_actor);
}

void
cobiwm_compositor_window_surface_changed (CobiwmCompositor *compositor,
                                        CobiwmWindow     *window)
{
  CobiwmWindowActor *window_actor;
  window_actor = COBIWM_WINDOW_ACTOR (cobiwm_window_get_compositor_private (window));
  if (!window_actor)
    return;

  cobiwm_window_actor_update_surface (window_actor);
}

/**
 * cobiwm_compositor_process_event: (skip)
 * @compositor:
 * @event:
 * @window:
 *
 */
gboolean
cobiwm_compositor_process_event (CobiwmCompositor *compositor,
                               XEvent         *event,
                               CobiwmWindow     *window)
{
  if (!cobiwm_is_wayland_compositor () &&
      event->type == cobiwm_display_get_damage_event_base (compositor->display) + XDamageNotify)
    {
      /* Core code doesn't handle damage events, so we need to extract the CobiwmWindow
       * ourselves
       */
      if (window == NULL)
        {
          Window xwin = ((XDamageNotifyEvent *) event)->drawable;
          window = cobiwm_display_lookup_x_window (compositor->display, xwin);
        }

      if (window)
        process_damage (compositor, (XDamageNotifyEvent *) event, window);
    }

  if (compositor->have_x11_sync_object)
    cobiwm_sync_ring_handle_event (event);

  /* Clutter needs to know about MapNotify events otherwise it will
     think the stage is invisible */
  if (!cobiwm_is_wayland_compositor () && event->type == MapNotify)
    clutter_x11_handle_event (event);

  /* The above handling is basically just "observing" the events, so we return
   * FALSE to indicate that the event should not be filtered out; if we have
   * GTK+ windows in the same process, GTK+ needs the ConfigureNotify event, for example.
   */
  return FALSE;
}

void
cobiwm_compositor_show_window (CobiwmCompositor *compositor,
			     CobiwmWindow	    *window,
                             CobiwmCompEffect  effect)
{
  CobiwmWindowActor *window_actor = COBIWM_WINDOW_ACTOR (cobiwm_window_get_compositor_private (window));
 cobiwm_window_actor_show (window_actor, effect);
}

void
cobiwm_compositor_hide_window (CobiwmCompositor *compositor,
                             CobiwmWindow     *window,
                             CobiwmCompEffect  effect)
{
  CobiwmWindowActor *window_actor = COBIWM_WINDOW_ACTOR (cobiwm_window_get_compositor_private (window));
  cobiwm_window_actor_hide (window_actor, effect);
}

void
cobiwm_compositor_switch_workspace (CobiwmCompositor     *compositor,
                                  CobiwmWorkspace      *from,
                                  CobiwmWorkspace      *to,
                                  CobiwmMotionDirection direction)
{
  gint to_indx, from_indx;

  to_indx   = cobiwm_workspace_index (to);
  from_indx = cobiwm_workspace_index (from);

  compositor->switch_workspace_in_progress++;

  if (!cobiwm_effect_manager_switch_workspace (compositor->plugin_mgr,
                                             from_indx,
                                             to_indx,
                                             direction))
    {
      compositor->switch_workspace_in_progress--;

      /* We have to explicitely call this to fix up stacking order of the
       * actors; this is because the abs stacking position of actors does not
       * necessarily change during the window hiding/unhiding, only their
       * relative position toward the destkop window.
       */
      cobiwm_finish_workspace_switch (compositor);
    }
}

static void
sync_actor_stacking (CobiwmCompositor *compositor)
{
  GList *children;
  GList *expected_window_node;
  GList *tmp;
  GList *old;
  GList *backgrounds;
  gboolean has_windows;
  gboolean reordered;

  /* NB: The first entries in the lists are stacked the lowest */

  /* Restacking will trigger full screen redraws, so it's worth a
   * little effort to make sure we actually need to restack before
   * we go ahead and do it */

  children = clutter_actor_get_children (compositor->window_group);
  has_windows = FALSE;
  reordered = FALSE;

  /* We allow for actors in the window group other than the actors we
   * know about, but it's up to a plugin to try and keep them stacked correctly
   * (we really need extra API to make that reliable.)
   */

  /* First we collect a list of all backgrounds, and check if they're at the
   * bottom. Then we check if the window actors are in the correct sequence */
  backgrounds = NULL;
  expected_window_node = compositor->windows;
  for (old = children; old != NULL; old = old->next)
    {
      ClutterActor *actor = old->data;

      if (COBIWM_IS_BACKGROUND_GROUP (actor) ||
          COBIWM_IS_BACKGROUND_ACTOR (actor))
        {
          backgrounds = g_list_prepend (backgrounds, actor);

          if (has_windows)
            reordered = TRUE;
        }
      else if (COBIWM_IS_WINDOW_ACTOR (actor) && !reordered)
        {
          has_windows = TRUE;

          if (expected_window_node != NULL && actor == expected_window_node->data)
            expected_window_node = expected_window_node->next;
          else
            reordered = TRUE;
        }
    }

  g_list_free (children);

  if (!reordered)
    {
      g_list_free (backgrounds);
      return;
    }

  /* reorder the actors by lowering them in turn to the bottom of the stack.
   * windows first, then background.
   *
   * We reorder the actors even if they're not parented to the window group,
   * to allow stacking to work with intermediate actors (eg during effects)
   */
  for (tmp = g_list_last (compositor->windows); tmp != NULL; tmp = tmp->prev)
    {
      ClutterActor *actor = tmp->data, *parent;

      parent = clutter_actor_get_parent (actor);
      clutter_actor_set_child_below_sibling (parent, actor, NULL);
    }

  /* we prepended the backgrounds above so the last actor in the list
   * should get lowered to the bottom last.
   */
  for (tmp = backgrounds; tmp != NULL; tmp = tmp->next)
    {
      ClutterActor *actor = tmp->data, *parent;

      parent = clutter_actor_get_parent (actor);
      clutter_actor_set_child_below_sibling (parent, actor, NULL);
    }
  g_list_free (backgrounds);
}

void
cobiwm_compositor_sync_stack (CobiwmCompositor  *compositor,
			    GList	    *stack)
{
  GList *old_stack;

  /* This is painful because hidden windows that we are in the process
   * of animating out of existence. They'll be at the bottom of the
   * stack of X windows, but we want to leave them in their old position
   * until the animation effect finishes.
   */

  /* Sources: first window is the highest */
  stack = g_list_copy (stack); /* The new stack of CobiwmWindow */
  old_stack = g_list_reverse (compositor->windows); /* The old stack of CobiwmWindowActor */
  compositor->windows = NULL;

  while (TRUE)
    {
      CobiwmWindowActor *old_actor = NULL, *stack_actor = NULL, *actor;
      CobiwmWindow *old_window = NULL, *stack_window = NULL, *window;

      /* Find the remaining top actor in our existing stack (ignoring
       * windows that have been hidden and are no longer animating) */
      while (old_stack)
        {
          old_actor = old_stack->data;
          old_window = cobiwm_window_actor_get_cobiwm_window (old_actor);

          if ((old_window->hidden || old_window->unmanaging) &&
              !cobiwm_window_actor_effect_in_progress (old_actor))
            {
              old_stack = g_list_delete_link (old_stack, old_stack);
              old_actor = NULL;
            }
          else
            break;
        }

      /* And the remaining top actor in the new stack */
      while (stack)
        {
          stack_window = stack->data;
          stack_actor = COBIWM_WINDOW_ACTOR (cobiwm_window_get_compositor_private (stack_window));
          if (!stack_actor)
            {
              cobiwm_verbose ("Failed to find corresponding CobiwmWindowActor "
                            "for window %s\n", cobiwm_window_get_description (stack_window));
              stack = g_list_delete_link (stack, stack);
            }
          else
            break;
        }

      if (!old_actor && !stack_actor) /* Nothing more to stack */
        break;

      /* We usually prefer the window in the new stack, but if if we
       * found a hidden window in the process of being animated out
       * of existence in the old stack we use that instead. We've
       * filtered out non-animating hidden windows above.
       */
      if (old_actor &&
          (!stack_actor || old_window->hidden || old_window->unmanaging))
        {
          actor = old_actor;
          window = old_window;
        }
      else
        {
          actor = stack_actor;
          window = stack_window;
        }

      /* OK, we know what actor we want next. Add it to our window
       * list, and remove it from both source lists. (It will
       * be at the front of at least one, hopefully it will be
       * near the front of the other.)
       */
      compositor->windows = g_list_prepend (compositor->windows, actor);

      stack = g_list_remove (stack, window);
      old_stack = g_list_remove (old_stack, actor);
    }

  sync_actor_stacking (compositor);
}

void
cobiwm_compositor_sync_window_geometry (CobiwmCompositor *compositor,
				      CobiwmWindow *window,
                                      gboolean did_placement)
{
  CobiwmWindowActor *window_actor = COBIWM_WINDOW_ACTOR (cobiwm_window_get_compositor_private (window));
  cobiwm_window_actor_sync_actor_geometry (window_actor, did_placement);
}

static void
frame_callback (CoglOnscreen  *onscreen,
                CoglFrameEvent event,
                CoglFrameInfo *frame_info,
                void          *user_data)
{
  CobiwmCompositor *compositor = user_data;
  GList *l;

  if (event == COGL_FRAME_EVENT_COMPLETE)
    {
      gint64 presentation_time_cogl = cogl_frame_info_get_presentation_time (frame_info);
      gint64 presentation_time;

      if (presentation_time_cogl != 0)
        {
          /* Cogl reports presentation in terms of its own clock, which is
           * guaranteed to be in nanoseconds but with no specified base. The
           * normal case with the open source GPU drivers on Linux 3.8 and
           * newer is that the base of cogl_get_clock_time() is that of
           * clock_gettime(CLOCK_MONOTONIC), so the same as g_get_monotonic_time),
           * but there's no exposure of that through the API. clock_gettime()
           * is fairly fast, so calling it twice and subtracting to get a
           * nearly-zero number is acceptable, if a litle ugly.
           */
          CoglContext *context = cogl_framebuffer_get_context (COGL_FRAMEBUFFER (onscreen));
          gint64 current_cogl_time = cogl_get_clock_time (context);
          gint64 current_monotonic_time = g_get_monotonic_time ();

          presentation_time =
            current_monotonic_time + (presentation_time_cogl - current_cogl_time) / 1000;
        }
      else
        {
          presentation_time = 0;
        }

      for (l = compositor->windows; l; l = l->next)
        cobiwm_window_actor_frame_complete (l->data, frame_info, presentation_time);
    }
}

static gboolean
cobiwm_pre_paint_func (gpointer data)
{
  GList *l;
  CobiwmWindowActor *top_window;
  CobiwmCompositor *compositor = data;

  if (compositor->onscreen == NULL)
    {
      compositor->onscreen = COGL_ONSCREEN (cogl_get_draw_framebuffer ());
      compositor->frame_closure = cogl_onscreen_add_frame_callback (compositor->onscreen,
                                                                    frame_callback,
                                                                    compositor,
                                                                    NULL);
    }

  if (compositor->windows == NULL)
    return TRUE;

  top_window = g_list_last (compositor->windows)->data;

  if (cobiwm_window_actor_should_unredirect (top_window) &&
      compositor->disable_unredirect_count == 0)
    set_unredirected_window (compositor, cobiwm_window_actor_get_cobiwm_window (top_window));
  else
    set_unredirected_window (compositor, NULL);

  for (l = compositor->windows; l; l = l->next)
    cobiwm_window_actor_pre_paint (l->data);

  if (compositor->frame_has_updated_xsurfaces)
    {
      /* We need to make sure that any X drawing that happens before
       * the XDamageSubtract() for each window above is visible to
       * subsequent GL rendering; the standardized way to do this is
       * GL_EXT_X11_sync_object. Since this isn't implemented yet in
       * mesa, we also have a path that relies on the implementation
       * of the open source drivers.
       *
       * Anything else, we just hope for the best.
       *
       * Xorg and open source driver specifics:
       *
       * The X server makes sure to flush drawing to the kernel before
       * sending out damage events, but since we use
       * DamageReportBoundingBox there may be drawing between the last
       * damage event and the XDamageSubtract() that needs to be
       * flushed as well.
       *
       * Xorg always makes sure that drawing is flushed to the kernel
       * before writing events or responses to the client, so any
       * round trip request at this point is sufficient to flush the
       * GLX buffers.
       */
      if (compositor->have_x11_sync_object)
        compositor->have_x11_sync_object = cobiwm_sync_ring_insert_wait ();
      else
        XSync (compositor->display->xdisplay, False);
    }

  return TRUE;
}

static gboolean
cobiwm_post_paint_func (gpointer data)
{
  CobiwmCompositor *compositor = data;

  if (compositor->frame_has_updated_xsurfaces)
    {
      if (compositor->have_x11_sync_object)
        compositor->have_x11_sync_object = cobiwm_sync_ring_after_frame ();

      compositor->frame_has_updated_xsurfaces = FALSE;
    }

  return TRUE;
}

static void
on_shadow_factory_changed (CobiwmShadowFactory *factory,
                           CobiwmCompositor    *compositor)
{
  GList *l;

  for (l = compositor->windows; l; l = l->next)
    cobiwm_window_actor_invalidate_shadow (l->data);
}

static gboolean
has_swap_event (CobiwmCompositor *compositor)
{
  ClutterBackend *backend;
  CoglContext *cogl_context;
  CoglDisplay *cogl_display;
  int glxErrorBase, glxEventBase;
  
  
  backend = clutter_get_default_backend ();
  cogl_context = clutter_backend_get_cogl_context (backend);
  cogl_display = cogl_context_get_display (cogl_context);
  
  CoglRenderer *renderer = cogl_display_get_renderer (cogl_display);
  const char * (* query_extensions_string) (Display *dpy, int screen);
  Bool (* query_extension) (Display *dpy, int *error, int *event);
  CobiwmScreen *screen;
  Display *xdisplay;
  const char *glx_extensions;

  /* We will only get swap events if Cogl is using GLX */
  if (cogl_renderer_get_winsys_id (renderer) != COGL_WINSYS_ID_GLX)
    return FALSE;

  screen = compositor->display->screen;

  xdisplay = compositor->display->xdisplay;

  query_extensions_string =
    (void *) cogl_get_proc_address ("glXQueryExtensionsString");
  query_extension =
    (void *) cogl_get_proc_address ("glXQueryExtension");

  query_extension (xdisplay,
                   &glxErrorBase,
                   &glxEventBase);

  glx_extensions =
    query_extensions_string (xdisplay,
                             cobiwm_screen_get_screen_number (screen));

  return strstr (glx_extensions, "GLX_INTEL_swap_event") != NULL;
}

/**
 * cobiwm_compositor_new: (skip)
 * @display:
 *
 */
CobiwmCompositor *
cobiwm_compositor_new (CobiwmDisplay *display)
{
  CobiwmCompositor        *compositor;

  compositor = g_new0 (CobiwmCompositor, 1);
  compositor->display = display;

  if (g_getenv("COBIWM_DISABLE_MIPMAPS"))
    compositor->no_mipmaps = TRUE;

  g_signal_connect (cobiwm_shadow_factory_get_default (),
                    "changed",
                    G_CALLBACK (on_shadow_factory_changed),
                    compositor);

  compositor->pre_paint_func_id =
    clutter_threads_add_repaint_func_full (CLUTTER_REPAINT_FLAGS_PRE_PAINT,
                                           cobiwm_pre_paint_func,
                                           compositor,
                                           NULL);
  compositor->post_paint_func_id =
    clutter_threads_add_repaint_func_full (CLUTTER_REPAINT_FLAGS_POST_PAINT,
                                           cobiwm_post_paint_func,
                                           compositor,
                                           NULL);
  
  // check for swap event
  compositor->have_swap_events = has_swap_event(compositor);
  
  return compositor;
}

/**
 * cobiwm_get_overlay_window: (skip)
 * @screen: a #CobiwmScreen
 *
 */
Window
cobiwm_get_overlay_window (CobiwmScreen *screen)
{
  CobiwmCompositor *compositor = get_compositor_for_screen (screen);
  return compositor->output;
}

/**
 * cobiwm_disable_unredirect_for_screen:
 * @screen: a #CobiwmScreen
 *
 * Disables unredirection, can be usefull in situations where having
 * unredirected windows is undesireable like when recording a video.
 *
 */
void
cobiwm_disable_unredirect_for_screen (CobiwmScreen *screen)
{
  CobiwmCompositor *compositor = get_compositor_for_screen (screen);
  compositor->disable_unredirect_count++;
}

/**
 * cobiwm_enable_unredirect_for_screen:
 * @screen: a #CobiwmScreen
 *
 * Enables unredirection which reduces the overhead for apps like games.
 *
 */
void
cobiwm_enable_unredirect_for_screen (CobiwmScreen *screen)
{
  CobiwmCompositor *compositor = get_compositor_for_screen (screen);
  if (compositor->disable_unredirect_count == 0)
    g_warning ("Called enable_unredirect_for_screen while unredirection is enabled.");
  if (compositor->disable_unredirect_count > 0)
    compositor->disable_unredirect_count--;
}

#define FLASH_TIME_MS 50

static void
flash_out_completed (ClutterTimeline *timeline,
                     gboolean         is_finished,
                     gpointer         user_data)
{
  ClutterActor *flash = CLUTTER_ACTOR (user_data);
  clutter_actor_destroy (flash);
}

void
cobiwm_compositor_flash_screen (CobiwmCompositor *compositor,
                              CobiwmScreen     *screen)
{
  ClutterActor *stage;
  ClutterActor *flash;
  ClutterTransition *transition;
  gfloat width, height;

  stage = cobiwm_get_stage_for_screen (screen);
  clutter_actor_get_size (stage, &width, &height);

  flash = clutter_actor_new ();
  clutter_actor_set_background_color (flash, CLUTTER_COLOR_Black);
  clutter_actor_set_size (flash, width, height);
  clutter_actor_set_opacity (flash, 0);
  clutter_actor_add_child (stage, flash);

  clutter_actor_save_easing_state (flash);
  clutter_actor_set_easing_mode (flash, CLUTTER_EASE_IN_QUAD);
  clutter_actor_set_easing_duration (flash, FLASH_TIME_MS);
  clutter_actor_set_opacity (flash, 192);

  transition = clutter_actor_get_transition (flash, "opacity");
  clutter_timeline_set_auto_reverse (CLUTTER_TIMELINE (transition), TRUE);
  clutter_timeline_set_repeat_count (CLUTTER_TIMELINE (transition), 2);

  g_signal_connect (transition, "stopped",
                    G_CALLBACK (flash_out_completed), flash);

  clutter_actor_restore_easing_state (flash);
}

static void
window_flash_out_completed (ClutterTimeline *timeline,
                            gboolean         is_finished,
                            gpointer         user_data)
{
  ClutterActor *flash = CLUTTER_ACTOR (user_data);
  clutter_actor_destroy (flash);
}

void
cobiwm_compositor_flash_window (CobiwmCompositor *compositor,
                              CobiwmWindow     *window)
{
  ClutterActor *window_actor =
    CLUTTER_ACTOR (cobiwm_window_get_compositor_private (window));
  ClutterActor *flash;
  ClutterTransition *transition;

  flash = clutter_actor_new ();
  clutter_actor_set_background_color (flash, CLUTTER_COLOR_Black);
  clutter_actor_set_size (flash, window->rect.width, window->rect.height);
  clutter_actor_set_position (flash,
                              window->custom_frame_extents.left,
                              window->custom_frame_extents.top);
  clutter_actor_set_opacity (flash, 0);
  clutter_actor_add_child (window_actor, flash);

  clutter_actor_save_easing_state (flash);
  clutter_actor_set_easing_mode (flash, CLUTTER_EASE_IN_QUAD);
  clutter_actor_set_easing_duration (flash, FLASH_TIME_MS);
  clutter_actor_set_opacity (flash, 192);

  transition = clutter_actor_get_transition (flash, "opacity");
  clutter_timeline_set_auto_reverse (CLUTTER_TIMELINE (transition), TRUE);
  clutter_timeline_set_repeat_count (CLUTTER_TIMELINE (transition), 2);

  g_signal_connect (transition, "stopped",
                    G_CALLBACK (window_flash_out_completed), flash);

  clutter_actor_restore_easing_state (flash);
}

/**
 * cobiwm_compositor_monotonic_time_to_server_time:
 * @display: a #CobiwmDisplay
 * @monotonic_time: time in the units of g_get_monotonic_time()
 *
 * _NET_WM_FRAME_DRAWN and _NET_WM_FRAME_TIMINGS messages represent time
 * as a "high resolution server time" - this is the server time interpolated
 * to microsecond resolution. The advantage of this time representation
 * is that if  X server is running on the same computer as a client, and
 * the Xserver uses 'clock_gettime(CLOCK_MONOTONIC, ...)' for the server
 * time, the client can detect this, and all such clients will share a
 * a time representation with high accuracy. If there is not a common
 * time source, then the time synchronization will be less accurate.
 */
gint64
cobiwm_compositor_monotonic_time_to_server_time (CobiwmDisplay *display,
                                               gint64       monotonic_time)
{
  CobiwmCompositor *compositor = display->compositor;

  if (compositor->server_time_query_time == 0 ||
      (!compositor->server_time_is_monotonic_time &&
       monotonic_time > compositor->server_time_query_time + 10*1000*1000)) /* 10 seconds */
    {
      guint32 server_time = cobiwm_display_get_current_time_roundtrip (display);
      gint64 server_time_usec = (gint64)server_time * 1000;
      gint64 current_monotonic_time = g_get_monotonic_time ();
      compositor->server_time_query_time = current_monotonic_time;

      /* If the server time is within a second of the monotonic time,
       * we assume that they are identical. This seems like a big margin,
       * but we want to be as robust as possible even if the system
       * is under load and our processing of the server response is
       * delayed.
       */
      if (server_time_usec > current_monotonic_time - 1000*1000 &&
          server_time_usec < current_monotonic_time + 1000*1000)
        compositor->server_time_is_monotonic_time = TRUE;

      compositor->server_time_offset = server_time_usec - current_monotonic_time;
    }

  if (compositor->server_time_is_monotonic_time)
    return monotonic_time;
  else
    return monotonic_time + compositor->server_time_offset;
}

void
cobiwm_compositor_show_tile_preview (CobiwmCompositor *compositor,
                                   CobiwmWindow     *window,
                                   CobiwmRectangle  *tile_rect,
                                   int             tile_monitor_number)
{
  cobiwm_effect_manager_show_tile_preview (compositor->plugin_mgr,
                                         window, tile_rect, tile_monitor_number);
}

void
cobiwm_compositor_hide_tile_preview (CobiwmCompositor *compositor)
{
  cobiwm_effect_manager_hide_tile_preview (compositor->plugin_mgr);
}

void
cobiwm_compositor_show_window_menu (CobiwmCompositor     *compositor,
                                  CobiwmWindow         *window,
                                  CobiwmWindowMenuType  menu,
                                  int                 x,
                                  int                 y)
{
  cobiwm_effect_manager_show_window_menu (compositor->plugin_mgr, window, menu, x, y);
}

void
cobiwm_compositor_show_window_menu_for_rect (CobiwmCompositor     *compositor,
                                           CobiwmWindow         *window,
                                           CobiwmWindowMenuType  menu,
					   CobiwmRectangle      *rect)
{
  cobiwm_effect_manager_show_window_menu_for_rect (compositor->plugin_mgr, window, menu, rect);
}
