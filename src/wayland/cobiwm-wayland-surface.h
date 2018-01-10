/*
 * Copyright (C) 2013 Red Hat, Inc.
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
 */

#ifndef COBIWM_WAYLAND_SURFACE_H
#define COBIWM_WAYLAND_SURFACE_H

#include <wayland-server.h>
#include <xkbcommon/xkbcommon.h>
#include <clutter/clutter.h>

#include <glib.h>
#include <cairo.h>

#include <cobiwm-cursor-tracker.h>
#include "cobiwm-wayland-types.h"
#include "cobiwm-surface-actor.h"
#include "backends/cobiwm-monitor-manager-private.h"
#include "cobiwm-wayland-pointer-constraints.h"

typedef struct _CobiwmWaylandPendingState CobiwmWaylandPendingState;

#define COBIWM_TYPE_WAYLAND_SURFACE (cobiwm_wayland_surface_get_type ())
G_DECLARE_FINAL_TYPE (CobiwmWaylandSurface,
                      cobiwm_wayland_surface,
                      COBIWM, WAYLAND_SURFACE,
                      GObject);

#define COBIWM_TYPE_WAYLAND_SURFACE_ROLE (cobiwm_wayland_surface_role_get_type ())
G_DECLARE_DERIVABLE_TYPE (CobiwmWaylandSurfaceRole, cobiwm_wayland_surface_role,
                          COBIWM, WAYLAND_SURFACE_ROLE, GObject);

#define COBIWM_TYPE_WAYLAND_PENDING_STATE (cobiwm_wayland_pending_state_get_type ())
G_DECLARE_FINAL_TYPE (CobiwmWaylandPendingState,
                      cobiwm_wayland_pending_state,
                      COBIWM, WAYLAND_PENDING_STATE,
                      GObject);

struct _CobiwmWaylandSurfaceRoleClass
{
  GObjectClass parent_class;

  void (*assigned) (CobiwmWaylandSurfaceRole *surface_role);
  void (*pre_commit) (CobiwmWaylandSurfaceRole  *surface_role,
                      CobiwmWaylandPendingState *pending);
  void (*commit) (CobiwmWaylandSurfaceRole  *surface_role,
                  CobiwmWaylandPendingState *pending);
  gboolean (*is_on_output) (CobiwmWaylandSurfaceRole *surface_role,
                            CobiwmMonitorInfo        *monitor);
};

struct _CobiwmWaylandSerial {
  gboolean set;
  uint32_t value;
};

#define COBIWM_TYPE_WAYLAND_SURFACE_ROLE_SUBSURFACE (cobiwm_wayland_surface_role_subsurface_get_type ())
G_DECLARE_FINAL_TYPE (CobiwmWaylandSurfaceRoleSubsurface,
                      cobiwm_wayland_surface_role_subsurface,
                      COBIWM, WAYLAND_SURFACE_ROLE_SUBSURFACE,
                      CobiwmWaylandSurfaceRole);

#define COBIWM_TYPE_WAYLAND_SURFACE_ROLE_XDG_SURFACE (cobiwm_wayland_surface_role_xdg_surface_get_type ())
G_DECLARE_FINAL_TYPE (CobiwmWaylandSurfaceRoleXdgSurface,
                      cobiwm_wayland_surface_role_xdg_surface,
                      COBIWM, WAYLAND_SURFACE_ROLE_XDG_SURFACE,
                      CobiwmWaylandSurfaceRole);

#define COBIWM_TYPE_WAYLAND_SURFACE_ROLE_XDG_POPUP (cobiwm_wayland_surface_role_xdg_popup_get_type ())
G_DECLARE_FINAL_TYPE (CobiwmWaylandSurfaceRoleXdgPopup,
                      cobiwm_wayland_surface_role_xdg_popup,
                      COBIWM, WAYLAND_SURFACE_ROLE_XDG_POPUP,
                      CobiwmWaylandSurfaceRole);

#define COBIWM_TYPE_WAYLAND_SURFACE_ROLE_WL_SHELL_SURFACE (cobiwm_wayland_surface_role_wl_shell_surface_get_type ())
G_DECLARE_FINAL_TYPE (CobiwmWaylandSurfaceRoleWlShellSurface,
                      cobiwm_wayland_surface_role_wl_shell_surface,
                      COBIWM, WAYLAND_SURFACE_ROLE_WL_SHELL_SURFACE,
                      CobiwmWaylandSurfaceRole);

#define COBIWM_TYPE_WAYLAND_SURFACE_ROLE_DND (cobiwm_wayland_surface_role_dnd_get_type ())
G_DECLARE_FINAL_TYPE (CobiwmWaylandSurfaceRoleDND,
                      cobiwm_wayland_surface_role_dnd,
                      COBIWM, WAYLAND_SURFACE_ROLE_DND,
                      CobiwmWaylandSurfaceRole);

struct _CobiwmWaylandPendingState
{
  GObject parent;

  /* wl_surface.attach */
  gboolean newly_attached;
  CobiwmWaylandBuffer *buffer;
  gulong buffer_destroy_handler_id;
  int32_t dx;
  int32_t dy;

  int scale;

  /* wl_surface.damage */
  cairo_region_t *damage;

  cairo_region_t *input_region;
  gboolean input_region_set;
  cairo_region_t *opaque_region;
  gboolean opaque_region_set;

  /* wl_surface.frame */
  struct wl_list frame_callback_list;

  CobiwmRectangle new_geometry;
  gboolean has_new_geometry;
};

struct _CobiwmWaylandDragDestFuncs
{
  void (* focus_in)  (CobiwmWaylandDataDevice *data_device,
                      CobiwmWaylandSurface    *surface,
                      CobiwmWaylandDataOffer  *offer);
  void (* focus_out) (CobiwmWaylandDataDevice *data_device,
                      CobiwmWaylandSurface    *surface);
  void (* motion)    (CobiwmWaylandDataDevice *data_device,
                      CobiwmWaylandSurface    *surface,
                      const ClutterEvent    *event);
  void (* drop)      (CobiwmWaylandDataDevice *data_device,
                      CobiwmWaylandSurface    *surface);
  void (* update)    (CobiwmWaylandDataDevice *data_device,
                      CobiwmWaylandSurface    *surface);
};

struct _CobiwmWaylandSurface
{
  GObject parent;

  /* Generic stuff */
  struct wl_resource *resource;
  CobiwmWaylandCompositor *compositor;
  CobiwmSurfaceActor *surface_actor;
  CobiwmWaylandSurfaceRole *role;
  CobiwmWindow *window;
  cairo_region_t *input_region;
  cairo_region_t *opaque_region;
  int scale;
  int32_t offset_x, offset_y;
  GList *subsurfaces;
  GHashTable *outputs_to_destroy_notify_id;

  /* Buffer reference state. */
  struct {
    CobiwmWaylandBuffer *buffer;
    unsigned int use_count;
  } buffer_ref;

  /* Buffer renderer state. */
  gboolean buffer_held;

  /* List of pending frame callbacks that needs to stay queued longer than one
   * commit sequence, such as when it has not yet been assigned a role.
   */
  struct wl_list pending_frame_callback_list;

  /* Intermediate state for when no role has been assigned. */
  struct {
    CobiwmWaylandBuffer *buffer;
  } unassigned;

  struct {
    const CobiwmWaylandDragDestFuncs *funcs;
  } dnd;

  /* All the pending state that wl_surface.commit will apply. */
  CobiwmWaylandPendingState *pending;

  /* Extension resources. */
  struct wl_resource *xdg_surface;
  struct wl_resource *xdg_popup;
  struct wl_resource *wl_shell_surface;
  struct wl_resource *gtk_surface;
  struct wl_resource *wl_subsurface;

  /* xdg_surface stuff */
  struct wl_resource *xdg_shell_resource;
  CobiwmWaylandSerial acked_configure_serial;
  gboolean has_set_geometry;
  gboolean is_modal;

  /* xdg_popup */
  struct {
    CobiwmWaylandSurface *parent;
    struct wl_listener parent_destroy_listener;

    CobiwmWaylandPopup *popup;
    struct wl_listener destroy_listener;
  } popup;

  /* wl_subsurface stuff. */
  struct {
    CobiwmWaylandSurface *parent;
    struct wl_listener parent_destroy_listener;

    int x;
    int y;

    /* When the surface is synchronous, its state will be applied
     * when the parent is committed. This is done by moving the
     * "real" pending state below to here when this surface is
     * committed and in synchronous mode.
     *
     * When the parent surface is committed, we apply the pending
     * state here.
     */
    gboolean synchronous;
    CobiwmWaylandPendingState *pending;

    int32_t pending_x;
    int32_t pending_y;
    gboolean pending_pos;
    GSList *pending_placement_ops;
  } sub;
};

void                cobiwm_wayland_shell_init     (CobiwmWaylandCompositor *compositor);

CobiwmWaylandSurface *cobiwm_wayland_surface_create (CobiwmWaylandCompositor *compositor,
                                                 struct wl_client      *client,
                                                 struct wl_resource    *compositor_resource,
                                                 guint32                id);

gboolean            cobiwm_wayland_surface_assign_role (CobiwmWaylandSurface *surface,
                                                      GType               role_type);

CobiwmWaylandBuffer  *cobiwm_wayland_surface_get_buffer (CobiwmWaylandSurface *surface);

void                cobiwm_wayland_surface_ref_buffer_use_count (CobiwmWaylandSurface *surface);

void                cobiwm_wayland_surface_unref_buffer_use_count (CobiwmWaylandSurface *surface);

void                cobiwm_wayland_surface_set_window (CobiwmWaylandSurface *surface,
                                                     CobiwmWindow         *window);

void                cobiwm_wayland_surface_configure_notify (CobiwmWaylandSurface *surface,
                                                           int                 width,
                                                           int                 height,
                                                           CobiwmWaylandSerial  *sent_serial);

void                cobiwm_wayland_surface_ping (CobiwmWaylandSurface *surface,
                                               guint32             serial);
void                cobiwm_wayland_surface_delete (CobiwmWaylandSurface *surface);

void                cobiwm_wayland_surface_popup_done (CobiwmWaylandSurface *surface);

/* Drag dest functions */
void                cobiwm_wayland_surface_drag_dest_focus_in  (CobiwmWaylandSurface   *surface,
                                                              CobiwmWaylandDataOffer *offer);
void                cobiwm_wayland_surface_drag_dest_motion    (CobiwmWaylandSurface   *surface,
                                                              const ClutterEvent   *event);
void                cobiwm_wayland_surface_drag_dest_focus_out (CobiwmWaylandSurface   *surface);
void                cobiwm_wayland_surface_drag_dest_drop      (CobiwmWaylandSurface   *surface);
void                cobiwm_wayland_surface_drag_dest_update    (CobiwmWaylandSurface   *surface);

void                cobiwm_wayland_surface_update_outputs (CobiwmWaylandSurface *surface);

CobiwmWindow *        cobiwm_wayland_surface_get_toplevel_window (CobiwmWaylandSurface *surface);

void                cobiwm_wayland_surface_queue_pending_frame_callbacks (CobiwmWaylandSurface *surface);

void                cobiwm_wayland_surface_queue_pending_state_frame_callbacks (CobiwmWaylandSurface      *surface,
                                                                              CobiwmWaylandPendingState *pending);

void                cobiwm_wayland_surface_get_relative_coordinates (CobiwmWaylandSurface *surface,
                                                                   float               abs_x,
                                                                   float               abs_y,
                                                                   float              *sx,
                                                                   float              *sy);

void                cobiwm_wayland_surface_get_absolute_coordinates (CobiwmWaylandSurface *surface,
                                                                   float               sx,
                                                                   float               sy,
                                                                   float              *x,
                                                                   float              *y);

CobiwmWaylandSurface * cobiwm_wayland_surface_role_get_surface (CobiwmWaylandSurfaceRole *role);

cairo_region_t *    cobiwm_wayland_surface_calculate_input_region (CobiwmWaylandSurface *surface);

#endif
