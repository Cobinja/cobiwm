/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

#ifndef COBIWM_COMPOSITOR_PRIVATE_H
#define COBIWM_COMPOSITOR_PRIVATE_H

#include <X11/extensions/Xfixes.h>

#include <compositor.h>
#include <display.h>
#include "cobiwm-effect-manager.h"
#include "cobiwm-window-actor-private.h"
#include <clutter/clutter.h>

struct _CobiwmCompositor
{
  CobiwmDisplay    *display;

  guint           pre_paint_func_id;
  guint           post_paint_func_id;

  gint64          server_time_query_time;
  gint64          server_time_offset;

  guint           server_time_is_monotonic_time : 1;
  guint           no_mipmaps  : 1;

  ClutterActor          *stage, *window_group, *top_window_group, *feedback_group;
  ClutterActor          *background_actor;
  GList                 *windows;
  Window                 output;

  CoglOnscreen          *onscreen;
  CoglFrameClosure      *frame_closure;

  /* Used for unredirecting fullscreen windows */
  guint                  disable_unredirect_count;
  CobiwmWindow            *unredirected_window;

  gint                   switch_workspace_in_progress;

  CobiwmEffectManager *plugin_mgr;

  gboolean frame_has_updated_xsurfaces;
  gboolean have_x11_sync_object;
  gboolean have_swap_events;
};

/* Wait 2ms after vblank before starting to draw next frame */
#define COBIWM_SYNC_DELAY 2

void cobiwm_switch_workspace_completed (CobiwmCompositor *compositor);

gint64 cobiwm_compositor_monotonic_time_to_server_time (CobiwmDisplay *display,
                                                      gint64       monotonic_time);

void cobiwm_compositor_flash_window (CobiwmCompositor *compositor,
                                   CobiwmWindow     *window);

#endif /* COBIWM_COMPOSITOR_PRIVATE_H */
