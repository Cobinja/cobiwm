/*
 * Copyright © 2008 Kristian Høgsberg
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#ifndef COBIWM_WAYLAND_DATA_DEVICE_H
#define COBIWM_WAYLAND_DATA_DEVICE_H

#include <wayland-server.h>
#include <glib-object.h>

#include "cobiwm-wayland-types.h"

typedef struct _CobiwmWaylandDragGrab CobiwmWaylandDragGrab;
typedef struct _CobiwmWaylandDataSourceFuncs CobiwmWaylandDataSourceFuncs;

#define COBIWM_TYPE_WAYLAND_DATA_SOURCE (cobiwm_wayland_data_source_get_type ())
G_DECLARE_DERIVABLE_TYPE (CobiwmWaylandDataSource, cobiwm_wayland_data_source,
                          COBIWM, WAYLAND_DATA_SOURCE, GObject);

struct _CobiwmWaylandDataSourceClass
{
  GObjectClass parent_class;

  void (* send)    (CobiwmWaylandDataSource *source,
                    const gchar           *mime_type,
                    gint                   fd);
  void (* target)  (CobiwmWaylandDataSource *source,
                    const gchar           *mime_type);
  void (* cancel)  (CobiwmWaylandDataSource *source);

  void (* action)         (CobiwmWaylandDataSource *source,
                           uint32_t               action);
  void (* drop_performed) (CobiwmWaylandDataSource *source);
  void (* drag_finished)  (CobiwmWaylandDataSource *source);
};

struct _CobiwmWaylandDataDevice
{
  uint32_t selection_serial;
  uint32_t primary_serial;
  CobiwmWaylandDataSource *selection_data_source;
  CobiwmWaylandDataSource *dnd_data_source;
  CobiwmWaylandDataSource *primary_data_source;
  struct wl_listener selection_data_source_listener;
  struct wl_list resource_list;
  struct wl_list primary_resource_list;
  CobiwmWaylandDragGrab *current_grab;
  struct wl_client *focus_client;

  struct wl_signal selection_ownership_signal;
  struct wl_signal dnd_ownership_signal;
  struct wl_signal primary_ownership_signal;
};

void cobiwm_wayland_data_device_manager_init (CobiwmWaylandCompositor *compositor);

void cobiwm_wayland_data_device_init (CobiwmWaylandDataDevice *data_device);

void cobiwm_wayland_data_device_set_keyboard_focus (CobiwmWaylandDataDevice *data_device);

gboolean cobiwm_wayland_data_device_is_dnd_surface (CobiwmWaylandDataDevice *data_device,
                                                  CobiwmWaylandSurface    *surface);

void cobiwm_wayland_data_device_set_dnd_source     (CobiwmWaylandDataDevice *data_device,
                                                  CobiwmWaylandDataSource *source);
void cobiwm_wayland_data_device_set_selection      (CobiwmWaylandDataDevice *data_device,
                                                  CobiwmWaylandDataSource *source,
                                                  guint32 serial);
void cobiwm_wayland_data_device_set_primary        (CobiwmWaylandDataDevice *data_device,
                                                  CobiwmWaylandDataSource *source,
                                                  guint32                serial);

gboolean cobiwm_wayland_data_source_add_mime_type  (CobiwmWaylandDataSource *source,
                                                  const gchar           *mime_type);

gboolean cobiwm_wayland_data_source_has_mime_type  (const CobiwmWaylandDataSource *source,
                                                  const gchar                 *mime_type);

struct wl_array *
         cobiwm_wayland_data_source_get_mime_types (const CobiwmWaylandDataSource *source);

gboolean cobiwm_wayland_data_source_has_target     (CobiwmWaylandDataSource *source);

void     cobiwm_wayland_data_source_set_has_target (CobiwmWaylandDataSource *source,
                                                  gboolean               has_target);

void     cobiwm_wayland_data_source_send           (CobiwmWaylandDataSource *source,
                                                  const gchar           *mime_type,
                                                  gint                   fd);

void     cobiwm_wayland_data_source_notify_finish  (CobiwmWaylandDataSource *source);

uint32_t cobiwm_wayland_data_source_get_actions        (CobiwmWaylandDataSource *source);
uint32_t cobiwm_wayland_data_source_get_user_action    (CobiwmWaylandDataSource *source);
uint32_t cobiwm_wayland_data_source_get_current_action (CobiwmWaylandDataSource *source);

void     cobiwm_wayland_data_source_set_actions        (CobiwmWaylandDataSource *source,
                                                      uint32_t               dnd_actions);
void     cobiwm_wayland_data_source_set_current_action (CobiwmWaylandDataSource *source,
                                                      uint32_t               action);

const CobiwmWaylandDragDestFuncs *
         cobiwm_wayland_data_device_get_drag_dest_funcs (void);

void     cobiwm_wayland_data_device_start_drag     (CobiwmWaylandDataDevice                 *data_device,
                                                  struct wl_client                      *client,
                                                  const CobiwmWaylandPointerGrabInterface *funcs,
                                                  CobiwmWaylandSurface                    *surface,
                                                  CobiwmWaylandDataSource                 *source,
                                                  CobiwmWaylandSurface                    *icon_surface);

void     cobiwm_wayland_data_device_end_drag       (CobiwmWaylandDataDevice                 *data_device);

void     cobiwm_wayland_drag_grab_set_focus        (CobiwmWaylandDragGrab             *drag_grab,
                                                  CobiwmWaylandSurface              *surface);
CobiwmWaylandSurface *
         cobiwm_wayland_drag_grab_get_focus        (CobiwmWaylandDragGrab             *drag_grab);

#endif /* COBIWM_WAYLAND_DATA_DEVICE_H */
