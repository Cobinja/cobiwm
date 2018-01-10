/*
 * Copyright © 2011 Kristian Høgsberg
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

/* The file is based on src/data-device.c from Weston */

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <glib.h>
#include <glib-unix.h>

#include "cobiwm-wayland-data-device.h"
#include "cobiwm-wayland-data-device-private.h"
#include "cobiwm-wayland-seat.h"
#include "cobiwm-wayland-pointer.h"
#include "cobiwm-wayland-private.h"
#include "cobiwm-dnd-actor-private.h"

#include "gtk-primary-selection-server-protocol.h"

#define ROOTWINDOW_DROP_MIME "application/x-rootwindow-drop"

#define ALL_ACTIONS (WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY | \
                     WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE | \
                     WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK)

struct _CobiwmWaylandDataOffer
{
  struct wl_resource *resource;
  CobiwmWaylandDataSource *source;
  struct wl_listener source_destroy_listener;
  uint32_t dnd_actions;
  enum wl_data_device_manager_dnd_action preferred_dnd_action;
};

typedef struct _CobiwmWaylandDataSourcePrivate
{
  CobiwmWaylandDataOffer *offer;
  struct wl_array mime_types;
  gboolean has_target;
  uint32_t dnd_actions;
  enum wl_data_device_manager_dnd_action user_dnd_action;
  enum wl_data_device_manager_dnd_action current_dnd_action;
  CobiwmWaylandSeat *seat;
  guint actions_set : 1;
  guint in_ask : 1;
} CobiwmWaylandDataSourcePrivate;

typedef struct _CobiwmWaylandDataSourceWayland
{
  CobiwmWaylandDataSource parent;

  struct wl_resource *resource;
} CobiwmWaylandDataSourceWayland;

typedef struct _CobiwmWaylandDataSourcePrimary
{
  CobiwmWaylandDataSourceWayland parent;

  struct wl_resource *resource;
} CobiwmWaylandDataSourcePrimary;

G_DEFINE_TYPE_WITH_PRIVATE (CobiwmWaylandDataSource, cobiwm_wayland_data_source,
                            G_TYPE_OBJECT);
G_DEFINE_TYPE (CobiwmWaylandDataSourceWayland, cobiwm_wayland_data_source_wayland,
               COBIWM_TYPE_WAYLAND_DATA_SOURCE);
G_DEFINE_TYPE (CobiwmWaylandDataSourcePrimary, cobiwm_wayland_data_source_primary,
               COBIWM_TYPE_WAYLAND_DATA_SOURCE);

static CobiwmWaylandDataSource *
cobiwm_wayland_data_source_wayland_new (struct wl_resource *resource);
static CobiwmWaylandDataSource *
cobiwm_wayland_data_source_primary_new (struct wl_resource *resource);

static void
drag_grab_data_source_destroyed (gpointer data, GObject *where_the_object_was);

static void
unbind_resource (struct wl_resource *resource)
{
  wl_list_remove (wl_resource_get_link (resource));
}

static gboolean
cobiwm_wayland_source_get_in_ask (CobiwmWaylandDataSource *source)
{
  CobiwmWaylandDataSourcePrivate *priv =
    cobiwm_wayland_data_source_get_instance_private (source);

  return priv->in_ask;
}

static void
cobiwm_wayland_source_update_in_ask (CobiwmWaylandDataSource *source)
{
  CobiwmWaylandDataSourcePrivate *priv =
    cobiwm_wayland_data_source_get_instance_private (source);

  priv->in_ask =
    priv->current_dnd_action == WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK;
}

static enum wl_data_device_manager_dnd_action
data_offer_choose_action (CobiwmWaylandDataOffer *offer)
{
  CobiwmWaylandDataSource *source = offer->source;
  uint32_t actions, user_action, available_actions;

  actions = cobiwm_wayland_data_source_get_actions (source);
  user_action = cobiwm_wayland_data_source_get_user_action (source);

  available_actions = actions & offer->dnd_actions;

  if (!available_actions)
    return WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE;

  /* If the user is forcing an action, go for it */
  if ((user_action & available_actions) != 0)
    return user_action;

  /* If the dest side has a preferred DnD action, use it */
  if ((offer->preferred_dnd_action & available_actions) != 0)
    return offer->preferred_dnd_action;

  /* Use the first found action, in bit order */
  return 1 << (ffs (available_actions) - 1);
}

static void
data_offer_update_action (CobiwmWaylandDataOffer *offer)
{
  enum wl_data_device_manager_dnd_action current_action, action;
  CobiwmWaylandDataSource *source;

  if (!offer->source)
    return;

  source = offer->source;
  current_action = cobiwm_wayland_data_source_get_current_action (source);
  action = data_offer_choose_action (offer);

  if (current_action == action)
    return;

  cobiwm_wayland_data_source_set_current_action (source, action);

  if (!cobiwm_wayland_source_get_in_ask (source) &&
      wl_resource_get_version (offer->resource) >=
      WL_DATA_OFFER_ACTION_SINCE_VERSION)
    wl_data_offer_send_action (offer->resource, action);
}

static void
cobiwm_wayland_data_source_target (CobiwmWaylandDataSource *source,
                                 const char *mime_type)
{
  if (COBIWM_WAYLAND_DATA_SOURCE_GET_CLASS (source)->target)
    COBIWM_WAYLAND_DATA_SOURCE_GET_CLASS (source)->target (source, mime_type);
}

void
cobiwm_wayland_data_source_send (CobiwmWaylandDataSource *source,
                               const char *mime_type,
                               int fd)
{
  COBIWM_WAYLAND_DATA_SOURCE_GET_CLASS (source)->send (source, mime_type, fd);
}

gboolean
cobiwm_wayland_data_source_has_target (CobiwmWaylandDataSource *source)
{
  CobiwmWaylandDataSourcePrivate *priv =
    cobiwm_wayland_data_source_get_instance_private (source);

  return priv->has_target;
}

static void
cobiwm_wayland_data_source_set_seat (CobiwmWaylandDataSource *source,
                                   CobiwmWaylandSeat       *seat)
{
  CobiwmWaylandDataSourcePrivate *priv =
    cobiwm_wayland_data_source_get_instance_private (source);

  priv->seat = seat;
}

static CobiwmWaylandSeat *
cobiwm_wayland_data_source_get_seat (CobiwmWaylandDataSource *source)
{
  CobiwmWaylandDataSourcePrivate *priv =
    cobiwm_wayland_data_source_get_instance_private (source);

  return priv->seat;
}

void
cobiwm_wayland_data_source_set_has_target (CobiwmWaylandDataSource *source,
                                         gboolean has_target)
{
  CobiwmWaylandDataSourcePrivate *priv =
    cobiwm_wayland_data_source_get_instance_private (source);

  priv->has_target = has_target;
}

struct wl_array *
cobiwm_wayland_data_source_get_mime_types (const CobiwmWaylandDataSource *source)
{
  CobiwmWaylandDataSourcePrivate *priv =
    cobiwm_wayland_data_source_get_instance_private ((CobiwmWaylandDataSource *)source);

  return &priv->mime_types;
}

static void
cobiwm_wayland_data_source_cancel (CobiwmWaylandDataSource *source)
{
  COBIWM_WAYLAND_DATA_SOURCE_GET_CLASS (source)->cancel (source);
}

uint32_t
cobiwm_wayland_data_source_get_actions (CobiwmWaylandDataSource *source)
{
  CobiwmWaylandDataSourcePrivate *priv =
    cobiwm_wayland_data_source_get_instance_private (source);

  return priv->dnd_actions;
}

enum wl_data_device_manager_dnd_action
cobiwm_wayland_data_source_get_user_action (CobiwmWaylandDataSource *source)
{
  CobiwmWaylandDataSourcePrivate *priv =
    cobiwm_wayland_data_source_get_instance_private (source);

  if (!priv->seat)
    return WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE;

  return priv->user_dnd_action;
}

enum wl_data_device_manager_dnd_action
cobiwm_wayland_data_source_get_current_action (CobiwmWaylandDataSource *source)
{
  CobiwmWaylandDataSourcePrivate *priv =
    cobiwm_wayland_data_source_get_instance_private (source);

  return priv->current_dnd_action;
}

static void
cobiwm_wayland_data_source_set_current_offer (CobiwmWaylandDataSource *source,
                                            CobiwmWaylandDataOffer  *offer)
{
  CobiwmWaylandDataSourcePrivate *priv =
    cobiwm_wayland_data_source_get_instance_private (source);

  priv->offer = offer;
}

static CobiwmWaylandDataOffer *
cobiwm_wayland_data_source_get_current_offer (CobiwmWaylandDataSource *source)
{
  CobiwmWaylandDataSourcePrivate *priv =
    cobiwm_wayland_data_source_get_instance_private (source);

  return priv->offer;
}

void
cobiwm_wayland_data_source_set_current_action (CobiwmWaylandDataSource                  *source,
                                             enum wl_data_device_manager_dnd_action  action)
{
  CobiwmWaylandDataSourcePrivate *priv =
    cobiwm_wayland_data_source_get_instance_private (source);

  if (priv->current_dnd_action == action)
    return;

  priv->current_dnd_action = action;

  if (!cobiwm_wayland_source_get_in_ask (source))
    COBIWM_WAYLAND_DATA_SOURCE_GET_CLASS (source)->action (source, action);
}

void
cobiwm_wayland_data_source_set_actions (CobiwmWaylandDataSource *source,
                                      uint32_t               dnd_actions)
{
  CobiwmWaylandDataSourcePrivate *priv =
    cobiwm_wayland_data_source_get_instance_private (source);

  priv->dnd_actions = dnd_actions;
  priv->actions_set = TRUE;
}

static void
cobiwm_wayland_data_source_set_user_action (CobiwmWaylandDataSource                  *source,
                                          enum wl_data_device_manager_dnd_action  action)
{
  CobiwmWaylandDataSourcePrivate *priv =
    cobiwm_wayland_data_source_get_instance_private (source);
  CobiwmWaylandDataOffer *offer;

  if (priv->user_dnd_action == action)
    return;

  priv->user_dnd_action = action;
  offer = cobiwm_wayland_data_source_get_current_offer (source);

  if (offer)
    data_offer_update_action (offer);
}

static void
data_offer_accept (struct wl_client *client,
                   struct wl_resource *resource,
                   guint32 serial,
                   const char *mime_type)
{
  CobiwmWaylandDataOffer *offer = wl_resource_get_user_data (resource);

  /* FIXME: Check that client is currently focused by the input
   * device that is currently dragging this data source.  Should
   * this be a wl_data_device request? */

  if (offer->source)
    {
      cobiwm_wayland_data_source_target (offer->source, mime_type);
      cobiwm_wayland_data_source_set_has_target (offer->source,
                                               mime_type != NULL);
    }
}

static void
data_offer_receive (struct wl_client *client, struct wl_resource *resource,
                    const char *mime_type, int32_t fd)
{
  CobiwmWaylandDataOffer *offer = wl_resource_get_user_data (resource);

  if (offer->source)
    cobiwm_wayland_data_source_send (offer->source, mime_type, fd);
  else
    close (fd);
}

static void
default_destructor (struct wl_client   *client,
                    struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
data_offer_finish (struct wl_client   *client,
		   struct wl_resource *resource)
{
  CobiwmWaylandDataOffer *offer = wl_resource_get_user_data (resource);
  enum wl_data_device_manager_dnd_action current_action;

  if (!offer->source ||
      offer != cobiwm_wayland_data_source_get_current_offer (offer->source))
    return;

  if (cobiwm_wayland_data_source_get_seat (offer->source) ||
      !cobiwm_wayland_data_source_has_target (offer->source))
    {
      wl_resource_post_error (offer->resource,
                              WL_DATA_OFFER_ERROR_INVALID_FINISH,
                              "premature finish request");
      return;
    }

  current_action = cobiwm_wayland_data_source_get_current_action (offer->source);

  if (current_action == WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE ||
      current_action == WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK)
    {
      wl_resource_post_error (offer->resource,
                              WL_DATA_OFFER_ERROR_INVALID_OFFER,
                              "offer finished with an invalid action");
      return;
    }

  cobiwm_wayland_data_source_notify_finish (offer->source);
}

static void
data_offer_set_actions (struct wl_client   *client,
                        struct wl_resource *resource,
                        uint32_t            dnd_actions,
                        uint32_t            preferred_action)
{
  CobiwmWaylandDataOffer *offer = wl_resource_get_user_data (resource);

  if (dnd_actions & ~(ALL_ACTIONS))
    {
      wl_resource_post_error (offer->resource,
                              WL_DATA_OFFER_ERROR_INVALID_ACTION_MASK,
                              "invalid actions mask %x", dnd_actions);
      return;
    }

  if (preferred_action &&
      (!(preferred_action & dnd_actions) ||
       __builtin_popcount (preferred_action) > 1))
    {
      wl_resource_post_error (offer->resource,
                              WL_DATA_OFFER_ERROR_INVALID_ACTION,
                              "invalid action %x", preferred_action);
      return;
    }

  offer->dnd_actions = dnd_actions;
  offer->preferred_dnd_action = preferred_action;

  data_offer_update_action (offer);
}

static const struct wl_data_offer_interface data_offer_interface = {
  data_offer_accept,
  data_offer_receive,
  default_destructor,
  data_offer_finish,
  data_offer_set_actions,
};

static void
primary_offer_receive (struct wl_client *client, struct wl_resource *resource,
                       const char *mime_type, int32_t fd)
{
  CobiwmWaylandDataOffer *offer = wl_resource_get_user_data (resource);
  CobiwmWaylandDataSource *source = offer->source;
  CobiwmWaylandSeat *seat;

  if (!offer->source)
    {
      close (fd);
      return;
    }

  seat = cobiwm_wayland_data_source_get_seat (source);

  if (wl_resource_get_client (offer->resource) !=
      cobiwm_wayland_keyboard_get_focus_client (&seat->keyboard))
    {
      close (fd);
      return;
    }

  cobiwm_wayland_data_source_send (offer->source, mime_type, fd);
}

static const struct gtk_primary_selection_offer_interface primary_offer_interface = {
  primary_offer_receive,
  default_destructor,
};

static void
cobiwm_wayland_data_source_notify_drop_performed (CobiwmWaylandDataSource *source)
{
  COBIWM_WAYLAND_DATA_SOURCE_GET_CLASS (source)->drop_performed (source);
}

void
cobiwm_wayland_data_source_notify_finish (CobiwmWaylandDataSource *source)
{
  COBIWM_WAYLAND_DATA_SOURCE_GET_CLASS (source)->drag_finished (source);
}

static void
destroy_data_offer (struct wl_resource *resource)
{
  CobiwmWaylandDataOffer *offer = wl_resource_get_user_data (resource);
  CobiwmWaylandSeat *seat;

  if (offer->source)
    {
      seat = cobiwm_wayland_data_source_get_seat (offer->source);

      if (offer == cobiwm_wayland_data_source_get_current_offer (offer->source))
        {
          if (seat && seat->data_device.dnd_data_source == offer->source &&
              wl_resource_get_version (offer->resource) <
              WL_DATA_OFFER_ACTION_SINCE_VERSION)
            cobiwm_wayland_data_source_notify_finish (offer->source);
          else
            {
              cobiwm_wayland_data_source_cancel (offer->source);
              cobiwm_wayland_data_source_set_current_offer (offer->source, NULL);
            }
        }

      g_object_remove_weak_pointer (G_OBJECT (offer->source),
                                    (gpointer *)&offer->source);
      offer->source = NULL;
    }

  cobiwm_display_sync_wayland_input_focus (cobiwm_get_display ());
  g_slice_free (CobiwmWaylandDataOffer, offer);
}

static void
destroy_primary_offer (struct wl_resource *resource)
{
  CobiwmWaylandDataOffer *offer = wl_resource_get_user_data (resource);

  if (offer->source)
    {
      if (offer == cobiwm_wayland_data_source_get_current_offer (offer->source))
        {
          cobiwm_wayland_data_source_cancel (offer->source);
          cobiwm_wayland_data_source_set_current_offer (offer->source, NULL);
        }

      g_object_remove_weak_pointer (G_OBJECT (offer->source),
                                    (gpointer *)&offer->source);
      offer->source = NULL;
    }

  cobiwm_display_sync_wayland_input_focus (cobiwm_get_display ());
  g_slice_free (CobiwmWaylandDataOffer, offer);
}

static struct wl_resource *
cobiwm_wayland_data_source_send_offer (CobiwmWaylandDataSource *source,
                                     struct wl_resource *target)
{
  CobiwmWaylandDataSourcePrivate *priv =
    cobiwm_wayland_data_source_get_instance_private (source);
  CobiwmWaylandDataOffer *offer = g_slice_new0 (CobiwmWaylandDataOffer);
  char **p;

  offer->source = source;
  g_object_add_weak_pointer (G_OBJECT (source), (gpointer *)&offer->source);
  offer->resource = wl_resource_create (wl_resource_get_client (target),
                                        &wl_data_offer_interface,
                                        wl_resource_get_version (target), 0);
  wl_resource_set_implementation (offer->resource,
                                  &data_offer_interface,
                                  offer,
                                  destroy_data_offer);

  wl_data_device_send_data_offer (target, offer->resource);

  wl_array_for_each (p, &priv->mime_types)
    wl_data_offer_send_offer (offer->resource, *p);

  data_offer_update_action (offer);
  cobiwm_wayland_data_source_set_current_offer (source, offer);

  return offer->resource;
}

static struct wl_resource *
cobiwm_wayland_data_source_send_primary_offer (CobiwmWaylandDataSource *source,
					     struct wl_resource    *target)
{
  CobiwmWaylandDataSourcePrivate *priv =
    cobiwm_wayland_data_source_get_instance_private (source);
  CobiwmWaylandDataOffer *offer = g_slice_new0 (CobiwmWaylandDataOffer);
  char **p;

  offer->source = source;
  g_object_add_weak_pointer (G_OBJECT (source), (gpointer *)&offer->source);
  offer->resource = wl_resource_create (wl_resource_get_client (target),
                                        &gtk_primary_selection_offer_interface,
                                        wl_resource_get_version (target), 0);
  wl_resource_set_implementation (offer->resource,
                                  &primary_offer_interface,
                                  offer,
                                  destroy_primary_offer);

  gtk_primary_selection_device_send_data_offer (target, offer->resource);

  wl_array_for_each (p, &priv->mime_types)
    gtk_primary_selection_offer_send_offer (offer->resource, *p);

  cobiwm_wayland_data_source_set_current_offer (source, offer);

  return offer->resource;
}

static void
data_source_offer (struct wl_client *client,
                   struct wl_resource *resource, const char *type)
{
  CobiwmWaylandDataSource *source = wl_resource_get_user_data (resource);

  if (!cobiwm_wayland_data_source_add_mime_type (source, type))
    wl_resource_post_no_memory (resource);
}

static void
data_source_set_actions (struct wl_client   *client,
                         struct wl_resource *resource,
                         uint32_t            dnd_actions)
{
  CobiwmWaylandDataSource *source = wl_resource_get_user_data (resource);
  CobiwmWaylandDataSourcePrivate *priv =
    cobiwm_wayland_data_source_get_instance_private (source);
  CobiwmWaylandDataSourceWayland *source_wayland =
    COBIWM_WAYLAND_DATA_SOURCE_WAYLAND (source);

  if (priv->actions_set)
    {
      wl_resource_post_error (source_wayland->resource,
                              WL_DATA_SOURCE_ERROR_INVALID_ACTION_MASK,
                              "cannot set actions more than once");
      return;
    }

  if (dnd_actions & ~(ALL_ACTIONS))
    {
      wl_resource_post_error (source_wayland->resource,
                              WL_DATA_SOURCE_ERROR_INVALID_ACTION_MASK,
                              "invalid actions mask %x", dnd_actions);
      return;
    }

  if (cobiwm_wayland_data_source_get_seat (source))
    {
      wl_resource_post_error (source_wayland->resource,
                              WL_DATA_SOURCE_ERROR_INVALID_ACTION_MASK,
                              "invalid action change after "
                              "wl_data_device.start_drag");
      return;
    }

  cobiwm_wayland_data_source_set_actions (source, dnd_actions);
}

static struct wl_data_source_interface data_source_interface = {
  data_source_offer,
  default_destructor,
  data_source_set_actions
};

static void
primary_source_offer (struct wl_client   *client,
                      struct wl_resource *resource,
                      const char         *type)
{
  CobiwmWaylandDataSource *source = wl_resource_get_user_data (resource);

  if (!cobiwm_wayland_data_source_add_mime_type (source, type))
    wl_resource_post_no_memory (resource);
}

static struct gtk_primary_selection_source_interface primary_source_interface = {
  primary_source_offer,
  default_destructor,
};

struct _CobiwmWaylandDragGrab {
  CobiwmWaylandPointerGrab  generic;

  CobiwmWaylandKeyboardGrab keyboard_grab;

  CobiwmWaylandSeat        *seat;
  struct wl_client       *drag_client;

  CobiwmWaylandSurface     *drag_focus;
  struct wl_resource     *drag_focus_data_device;
  struct wl_listener      drag_focus_listener;

  CobiwmWaylandSurface     *drag_surface;
  struct wl_listener      drag_icon_listener;

  CobiwmWaylandDataSource  *drag_data_source;

  ClutterActor           *feedback_actor;

  CobiwmWaylandSurface     *drag_origin;
  struct wl_listener      drag_origin_listener;

  int                     drag_start_x, drag_start_y;
  ClutterModifierType     buttons;

  guint                   need_initial_focus : 1;
};

static void
destroy_drag_focus (struct wl_listener *listener, void *data)
{
  CobiwmWaylandDragGrab *grab = wl_container_of (listener, grab, drag_focus_listener);

  grab->drag_focus_data_device = NULL;
  grab->drag_focus = NULL;
}

static void
cobiwm_wayland_drag_grab_set_source (CobiwmWaylandDragGrab   *drag_grab,
                                   CobiwmWaylandDataSource *source)
{
  if (drag_grab->drag_data_source)
    g_object_weak_unref (G_OBJECT (drag_grab->drag_data_source),
                         drag_grab_data_source_destroyed,
                         drag_grab);

  drag_grab->drag_data_source = source;

  if (source)
    g_object_weak_ref (G_OBJECT (source),
                       drag_grab_data_source_destroyed,
                       drag_grab);
}

static void
cobiwm_wayland_drag_source_fake_acceptance (CobiwmWaylandDataSource *source,
                                          const gchar           *mimetype)
{
  uint32_t actions, user_action, action = 0;

  actions = cobiwm_wayland_data_source_get_actions (source);
  user_action = cobiwm_wayland_data_source_get_user_action (source);

  /* Pick a suitable action */
  if ((user_action & actions) != 0)
    action = user_action;
  else if (actions != 0)
    action = 1 << (ffs (actions) - 1);

  /* Bail out if there is none, source didn't cooperate */
  if (action == 0)
    return;

  cobiwm_wayland_data_source_target (source, mimetype);
  cobiwm_wayland_data_source_set_current_action (source, action);
  cobiwm_wayland_data_source_set_has_target (source, TRUE);
}

void
cobiwm_wayland_drag_grab_set_focus (CobiwmWaylandDragGrab *drag_grab,
                                  CobiwmWaylandSurface  *surface)
{
  CobiwmWaylandSeat *seat = drag_grab->seat;
  CobiwmWaylandDataSource *source = drag_grab->drag_data_source;
  struct wl_client *client;
  struct wl_resource *data_device_resource, *offer = NULL;

  if (!drag_grab->need_initial_focus &&
      drag_grab->drag_focus == surface)
    return;

  drag_grab->need_initial_focus = FALSE;

  if (drag_grab->drag_focus)
    {
      cobiwm_wayland_surface_drag_dest_focus_out (drag_grab->drag_focus);
      drag_grab->drag_focus = NULL;
    }

  if (source)
    cobiwm_wayland_data_source_set_current_offer (source, NULL);

  if (!surface && source &&
      cobiwm_wayland_data_source_has_mime_type (source, ROOTWINDOW_DROP_MIME))
    cobiwm_wayland_drag_source_fake_acceptance (source, ROOTWINDOW_DROP_MIME);
  else if (source)
    cobiwm_wayland_data_source_target (source, NULL);

  if (!surface)
    return;

  if (!source &&
      wl_resource_get_client (surface->resource) != drag_grab->drag_client)
    return;

  client = wl_resource_get_client (surface->resource);

  data_device_resource = wl_resource_find_for_client (&seat->data_device.resource_list, client);

  if (source && data_device_resource)
    offer = cobiwm_wayland_data_source_send_offer (source, data_device_resource);

  drag_grab->drag_focus = surface;
  drag_grab->drag_focus_data_device = data_device_resource;

  cobiwm_wayland_surface_drag_dest_focus_in (drag_grab->drag_focus,
                                           offer ? wl_resource_get_user_data (offer) : NULL);
}

CobiwmWaylandSurface *
cobiwm_wayland_drag_grab_get_focus (CobiwmWaylandDragGrab *drag_grab)
{
  return drag_grab->drag_focus;
}

static void
drag_grab_focus (CobiwmWaylandPointerGrab *grab,
                 CobiwmWaylandSurface     *surface)
{
  CobiwmWaylandDragGrab *drag_grab = (CobiwmWaylandDragGrab*) grab;

  cobiwm_wayland_drag_grab_set_focus (drag_grab, surface);
}

static void
data_source_update_user_dnd_action (CobiwmWaylandDataSource *source,
                                    ClutterModifierType    modifiers)
{
  enum wl_data_device_manager_dnd_action user_dnd_action = 0;

  if (modifiers & CLUTTER_SHIFT_MASK)
    user_dnd_action = WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE;
  else if (modifiers & CLUTTER_CONTROL_MASK)
    user_dnd_action = WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY;
  else if (modifiers & (CLUTTER_MOD1_MASK | CLUTTER_BUTTON2_MASK))
    user_dnd_action = WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK;

  cobiwm_wayland_data_source_set_user_action (source, user_dnd_action);
}

static void
drag_grab_motion (CobiwmWaylandPointerGrab *grab,
		  const ClutterEvent     *event)
{
  CobiwmWaylandDragGrab *drag_grab = (CobiwmWaylandDragGrab*) grab;

  if (drag_grab->drag_focus)
    cobiwm_wayland_surface_drag_dest_motion (drag_grab->drag_focus, event);

  if (drag_grab->drag_surface)
    cobiwm_feedback_actor_update (COBIWM_FEEDBACK_ACTOR (drag_grab->feedback_actor),
                                event);
}

static void
data_device_end_drag_grab (CobiwmWaylandDragGrab *drag_grab)
{
  cobiwm_wayland_drag_grab_set_focus (drag_grab, NULL);

  if (drag_grab->drag_origin)
    {
      drag_grab->drag_origin = NULL;
      wl_list_remove (&drag_grab->drag_origin_listener.link);
    }

  if (drag_grab->drag_surface)
    {
      drag_grab->drag_surface = NULL;
      wl_list_remove (&drag_grab->drag_icon_listener.link);
    }

  cobiwm_wayland_drag_grab_set_source (drag_grab, NULL);

  if (drag_grab->feedback_actor)
    {
      clutter_actor_remove_all_children (drag_grab->feedback_actor);
      clutter_actor_destroy (drag_grab->feedback_actor);
    }

  drag_grab->seat->data_device.current_grab = NULL;

  /* There might be other grabs created in result to DnD actions like popups
   * on "ask" actions, we must not reset those, only our own.
   */
  if (drag_grab->generic.pointer->grab == (CobiwmWaylandPointerGrab *) drag_grab)
    {
      cobiwm_wayland_pointer_end_grab (drag_grab->generic.pointer);
      cobiwm_wayland_keyboard_end_grab (drag_grab->keyboard_grab.keyboard);
    }

  g_slice_free (CobiwmWaylandDragGrab, drag_grab);
}

static gboolean
on_fake_read_hup (GIOChannel   *channel,
                  GIOCondition  condition,
                  gpointer      data)
{
  CobiwmWaylandDataSource *source = data;

  cobiwm_wayland_data_source_notify_finish (source);
  g_io_channel_shutdown (channel, FALSE, NULL);
  g_io_channel_unref (channel);

  return G_SOURCE_REMOVE;
}

static void
cobiwm_wayland_data_source_fake_read (CobiwmWaylandDataSource *source,
                                    const gchar           *mimetype)
{
  GIOChannel *channel;
  int p[2];

  if (!g_unix_open_pipe (p, FD_CLOEXEC, NULL))
    {
      cobiwm_wayland_data_source_notify_finish (source);
      return;
    }

  if (!g_unix_set_fd_nonblocking (p[0], TRUE, NULL) ||
      !g_unix_set_fd_nonblocking (p[1], TRUE, NULL))
    {
      cobiwm_wayland_data_source_notify_finish (source);
      close (p[0]);
      close (p[1]);
      return;
    }

  cobiwm_wayland_data_source_send (source, mimetype, p[1]);
  channel = g_io_channel_unix_new (p[0]);
  g_io_add_watch (channel, G_IO_HUP, on_fake_read_hup, source);
}

static void
drag_grab_button (CobiwmWaylandPointerGrab *grab,
		  const ClutterEvent     *event)
{
  CobiwmWaylandDragGrab *drag_grab = (CobiwmWaylandDragGrab*) grab;
  CobiwmWaylandSeat *seat = drag_grab->seat;
  ClutterEventType event_type = clutter_event_type (event);

  if (drag_grab->generic.pointer->grab_button == clutter_event_get_button (event) &&
      event_type == CLUTTER_BUTTON_RELEASE)
    {
      CobiwmWaylandDataSource *source = drag_grab->drag_data_source;
      gboolean success;

      if (drag_grab->drag_focus && source &&
          cobiwm_wayland_data_source_has_target (source) &&
          cobiwm_wayland_data_source_get_current_action (source))
        {
          /* Detach the data source from the grab, it's meant to live longer */
          cobiwm_wayland_drag_grab_set_source (drag_grab, NULL);
          cobiwm_wayland_data_source_set_seat (source, NULL);

          cobiwm_wayland_surface_drag_dest_drop (drag_grab->drag_focus);
          cobiwm_wayland_data_source_notify_drop_performed (source);

          cobiwm_wayland_source_update_in_ask (source);
          success = TRUE;
        }
      else if (!drag_grab->drag_focus && source &&
               cobiwm_wayland_data_source_has_target (source) &&
               cobiwm_wayland_data_source_get_current_action (source) &&
               cobiwm_wayland_data_source_has_mime_type (source, ROOTWINDOW_DROP_MIME))
        {
          /* Perform a fake read, that will lead to notify_finish() being called */
          cobiwm_wayland_data_source_fake_read (source, ROOTWINDOW_DROP_MIME);
          success = TRUE;
        }
      else
        {
          cobiwm_wayland_data_source_cancel (source);
          cobiwm_wayland_data_source_set_current_offer (source, NULL);
          cobiwm_wayland_data_device_set_dnd_source (&seat->data_device, NULL);
          success= FALSE;
        }

      /* Finish drag and let actor self-destruct */
      cobiwm_dnd_actor_drag_finish (COBIWM_DND_ACTOR (drag_grab->feedback_actor), success);
      drag_grab->feedback_actor = NULL;
    }

  if (seat->pointer.button_count == 0 &&
      event_type == CLUTTER_BUTTON_RELEASE)
    data_device_end_drag_grab (drag_grab);
}

static const CobiwmWaylandPointerGrabInterface drag_grab_interface = {
  drag_grab_focus,
  drag_grab_motion,
  drag_grab_button,
};

static gboolean
keyboard_drag_grab_key (CobiwmWaylandKeyboardGrab *grab,
                        const ClutterEvent      *event)
{
  return FALSE;
}

static void
keyboard_drag_grab_modifiers (CobiwmWaylandKeyboardGrab *grab,
                              ClutterModifierType      modifiers)
{
  CobiwmWaylandDragGrab *drag_grab;

  drag_grab = wl_container_of (grab, drag_grab, keyboard_grab);

  /* The modifiers here just contain keyboard modifiers, mix it with the
   * mouse button modifiers we got when starting the drag operation.
   */
  modifiers |= drag_grab->buttons;

  if (drag_grab->drag_data_source)
    {
      data_source_update_user_dnd_action (drag_grab->drag_data_source, modifiers);

      if (drag_grab->drag_focus)
        cobiwm_wayland_surface_drag_dest_update (drag_grab->drag_focus);
    }
}

static const CobiwmWaylandKeyboardGrabInterface keyboard_drag_grab_interface = {
  keyboard_drag_grab_key,
  keyboard_drag_grab_modifiers
};

static void
destroy_data_device_origin (struct wl_listener *listener, void *data)
{
  CobiwmWaylandDragGrab *drag_grab =
    wl_container_of (listener, drag_grab, drag_origin_listener);

  drag_grab->drag_origin = NULL;
  cobiwm_wayland_data_device_set_dnd_source (&drag_grab->seat->data_device, NULL);
  data_device_end_drag_grab (drag_grab);
}

static void
drag_grab_data_source_destroyed (gpointer data, GObject *where_the_object_was)
{
  CobiwmWaylandDragGrab *drag_grab = data;

  drag_grab->drag_data_source = NULL;
  cobiwm_wayland_data_device_set_dnd_source (&drag_grab->seat->data_device, NULL);
  data_device_end_drag_grab (drag_grab);
}

static void
destroy_data_device_icon (struct wl_listener *listener, void *data)
{
  CobiwmWaylandDragGrab *drag_grab =
    wl_container_of (listener, drag_grab, drag_icon_listener);

  drag_grab->drag_surface = NULL;

  if (drag_grab->feedback_actor)
    clutter_actor_remove_all_children (drag_grab->feedback_actor);
}

void
cobiwm_wayland_data_device_start_drag (CobiwmWaylandDataDevice                 *data_device,
                                     struct wl_client                      *client,
                                     const CobiwmWaylandPointerGrabInterface *funcs,
                                     CobiwmWaylandSurface                    *surface,
                                     CobiwmWaylandDataSource                 *source,
                                     CobiwmWaylandSurface                    *icon_surface)
{
  CobiwmWaylandSeat *seat = wl_container_of (data_device, seat, data_device);
  CobiwmWaylandDragGrab *drag_grab;
  ClutterPoint pos, surface_pos;
  ClutterModifierType modifiers;

  data_device->current_grab = drag_grab = g_slice_new0 (CobiwmWaylandDragGrab);

  drag_grab->generic.interface = funcs;
  drag_grab->generic.pointer = &seat->pointer;

  drag_grab->keyboard_grab.interface = &keyboard_drag_grab_interface;
  drag_grab->keyboard_grab.keyboard = &seat->keyboard;

  drag_grab->drag_client = client;
  drag_grab->seat = seat;

  drag_grab->drag_origin = surface;
  drag_grab->drag_origin_listener.notify = destroy_data_device_origin;
  wl_resource_add_destroy_listener (surface->resource,
                                    &drag_grab->drag_origin_listener);

  clutter_actor_transform_stage_point (CLUTTER_ACTOR (cobiwm_surface_actor_get_texture (surface->surface_actor)),
                                       seat->pointer.grab_x,
                                       seat->pointer.grab_y,
                                       &surface_pos.x, &surface_pos.y);
  drag_grab->drag_start_x = surface_pos.x;
  drag_grab->drag_start_y = surface_pos.y;

  drag_grab->need_initial_focus = TRUE;

  modifiers = clutter_input_device_get_modifier_state (seat->pointer.device);
  drag_grab->buttons = modifiers &
    (CLUTTER_BUTTON1_MASK | CLUTTER_BUTTON2_MASK | CLUTTER_BUTTON3_MASK |
     CLUTTER_BUTTON4_MASK | CLUTTER_BUTTON5_MASK);

  cobiwm_wayland_drag_grab_set_source (drag_grab, source);
  cobiwm_wayland_data_device_set_dnd_source (data_device,
                                           drag_grab->drag_data_source);
  data_source_update_user_dnd_action (source, modifiers);

  if (icon_surface)
    {
      drag_grab->drag_surface = icon_surface;

      drag_grab->drag_icon_listener.notify = destroy_data_device_icon;
      wl_resource_add_destroy_listener (icon_surface->resource,
                                        &drag_grab->drag_icon_listener);

      drag_grab->feedback_actor = cobiwm_dnd_actor_new (CLUTTER_ACTOR (drag_grab->drag_origin->surface_actor),
                                                      drag_grab->drag_start_x,
                                                      drag_grab->drag_start_y);
      cobiwm_feedback_actor_set_anchor (COBIWM_FEEDBACK_ACTOR (drag_grab->feedback_actor),
                                      0, 0);
      clutter_actor_add_child (drag_grab->feedback_actor,
                               CLUTTER_ACTOR (drag_grab->drag_surface->surface_actor));

      clutter_input_device_get_coords (seat->pointer.device, NULL, &pos);
      cobiwm_feedback_actor_set_position (COBIWM_FEEDBACK_ACTOR (drag_grab->feedback_actor),
                                        pos.x, pos.y);
    }

  cobiwm_wayland_pointer_start_grab (&seat->pointer, (CobiwmWaylandPointerGrab*) drag_grab);
  cobiwm_wayland_data_source_set_seat (source, seat);
}

void
cobiwm_wayland_data_device_end_drag (CobiwmWaylandDataDevice *data_device)
{
  if (data_device->current_grab)
    data_device_end_drag_grab (data_device->current_grab);
}

static void
data_device_start_drag (struct wl_client *client,
                        struct wl_resource *resource,
                        struct wl_resource *source_resource,
                        struct wl_resource *origin_resource,
                        struct wl_resource *icon_resource, guint32 serial)
{
  CobiwmWaylandDataDevice *data_device = wl_resource_get_user_data (resource);
  CobiwmWaylandSeat *seat = wl_container_of (data_device, seat, data_device);
  CobiwmWaylandSurface *surface = NULL, *icon_surface = NULL;
  CobiwmWaylandDataSource *drag_source = NULL;

  if (origin_resource)
    surface = wl_resource_get_user_data (origin_resource);

  if (!surface)
    return;

  if (seat->pointer.button_count == 0 ||
      seat->pointer.grab_serial != serial ||
      !seat->pointer.focus_surface ||
      seat->pointer.focus_surface != surface)
    return;

  /* FIXME: Check that the data source type array isn't empty. */

  if (data_device->current_grab ||
      seat->pointer.grab != &seat->pointer.default_grab)
    return;

  if (icon_resource)
    icon_surface = wl_resource_get_user_data (icon_resource);
  if (source_resource)
    drag_source = wl_resource_get_user_data (source_resource);

  if (icon_resource &&
      !cobiwm_wayland_surface_assign_role (icon_surface,
                                         COBIWM_TYPE_WAYLAND_SURFACE_ROLE_DND))
    {
      wl_resource_post_error (resource, WL_DATA_DEVICE_ERROR_ROLE,
                              "wl_surface@%d already has a different role",
                              wl_resource_get_id (icon_resource));
      return;
    }

  cobiwm_wayland_pointer_set_focus (&seat->pointer, NULL);
  cobiwm_wayland_data_device_start_drag (data_device, client,
                                       &drag_grab_interface,
                                       surface, drag_source, icon_surface);

  cobiwm_wayland_keyboard_set_focus (&seat->keyboard, NULL);
  cobiwm_wayland_keyboard_start_grab (&seat->keyboard,
                                    &seat->data_device.current_grab->keyboard_grab);
}

static void
selection_data_source_destroyed (gpointer data, GObject *object_was_here)
{
  CobiwmWaylandDataDevice *data_device = data;
  CobiwmWaylandSeat *seat = wl_container_of (data_device, seat, data_device);
  struct wl_resource *data_device_resource;
  struct wl_client *focus_client = NULL;

  data_device->selection_data_source = NULL;

  focus_client = cobiwm_wayland_keyboard_get_focus_client (&seat->keyboard);
  if (focus_client)
    {
      data_device_resource = wl_resource_find_for_client (&data_device->resource_list, focus_client);
      if (data_device_resource)
        wl_data_device_send_selection (data_device_resource, NULL);
    }

  wl_signal_emit (&data_device->selection_ownership_signal, NULL);
}

static void
cobiwm_wayland_source_send (CobiwmWaylandDataSource *source,
                          const gchar           *mime_type,
                          gint                   fd)
{
  CobiwmWaylandDataSourceWayland *source_wayland =
    COBIWM_WAYLAND_DATA_SOURCE_WAYLAND (source);

  wl_data_source_send_send (source_wayland->resource, mime_type, fd);
  close (fd);
}

static void
cobiwm_wayland_source_target (CobiwmWaylandDataSource *source,
                            const gchar           *mime_type)
{
  CobiwmWaylandDataSourceWayland *source_wayland =
    COBIWM_WAYLAND_DATA_SOURCE_WAYLAND (source);

  wl_data_source_send_target (source_wayland->resource, mime_type);
}

static void
cobiwm_wayland_source_cancel (CobiwmWaylandDataSource *source)
{
  CobiwmWaylandDataSourceWayland *source_wayland =
    COBIWM_WAYLAND_DATA_SOURCE_WAYLAND (source);

  wl_data_source_send_cancelled (source_wayland->resource);
}

static void
cobiwm_wayland_source_action (CobiwmWaylandDataSource                  *source,
                            enum wl_data_device_manager_dnd_action  action)
{
  CobiwmWaylandDataSourceWayland *source_wayland =
    COBIWM_WAYLAND_DATA_SOURCE_WAYLAND (source);

  if (wl_resource_get_version (source_wayland->resource) >=
      WL_DATA_SOURCE_ACTION_SINCE_VERSION)
    wl_data_source_send_action (source_wayland->resource, action);
}

static void
cobiwm_wayland_source_drop_performed (CobiwmWaylandDataSource *source)
{
  CobiwmWaylandDataSourceWayland *source_wayland =
    COBIWM_WAYLAND_DATA_SOURCE_WAYLAND (source);

  if (wl_resource_get_version (source_wayland->resource) >=
      WL_DATA_SOURCE_DND_DROP_PERFORMED_SINCE_VERSION)
    wl_data_source_send_dnd_drop_performed (source_wayland->resource);
}

static void
cobiwm_wayland_source_drag_finished (CobiwmWaylandDataSource *source)
{
  CobiwmWaylandDataSourceWayland *source_wayland =
    COBIWM_WAYLAND_DATA_SOURCE_WAYLAND (source);
  enum wl_data_device_manager_dnd_action action;

  if (cobiwm_wayland_source_get_in_ask (source))
    {
      action = cobiwm_wayland_data_source_get_current_action (source);
      cobiwm_wayland_source_action (source, action);
    }

  if (wl_resource_get_version (source_wayland->resource) >=
      WL_DATA_SOURCE_DND_FINISHED_SINCE_VERSION)
    wl_data_source_send_dnd_finished (source_wayland->resource);
}

static void
cobiwm_wayland_source_finalize (GObject *object)
{
  G_OBJECT_CLASS (cobiwm_wayland_data_source_parent_class)->finalize (object);
}

static void
cobiwm_wayland_data_source_wayland_init (CobiwmWaylandDataSourceWayland *source_wayland)
{
}

static void
cobiwm_wayland_data_source_wayland_class_init (CobiwmWaylandDataSourceWaylandClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  CobiwmWaylandDataSourceClass *data_source_class =
    COBIWM_WAYLAND_DATA_SOURCE_CLASS (klass);

  object_class->finalize = cobiwm_wayland_source_finalize;

  data_source_class->send = cobiwm_wayland_source_send;
  data_source_class->target = cobiwm_wayland_source_target;
  data_source_class->cancel = cobiwm_wayland_source_cancel;
  data_source_class->action = cobiwm_wayland_source_action;
  data_source_class->drop_performed = cobiwm_wayland_source_drop_performed;
  data_source_class->drag_finished = cobiwm_wayland_source_drag_finished;
}

static void
cobiwm_wayland_data_source_primary_send (CobiwmWaylandDataSource *source,
                                       const gchar           *mime_type,
                                       gint                   fd)
{
  CobiwmWaylandDataSourcePrimary *source_primary;

  source_primary = COBIWM_WAYLAND_DATA_SOURCE_PRIMARY (source);
  gtk_primary_selection_source_send_send (source_primary->resource,
                                          mime_type, fd);
  close (fd);
}

static void
cobiwm_wayland_data_source_primary_cancel (CobiwmWaylandDataSource *source)
{
  CobiwmWaylandDataSourcePrimary *source_primary;

  source_primary = COBIWM_WAYLAND_DATA_SOURCE_PRIMARY (source);
  gtk_primary_selection_source_send_cancelled (source_primary->resource);
}

static void
cobiwm_wayland_data_source_primary_init (CobiwmWaylandDataSourcePrimary *source_primary)
{
}

static void
cobiwm_wayland_data_source_primary_class_init (CobiwmWaylandDataSourcePrimaryClass *klass)
{
  CobiwmWaylandDataSourceClass *data_source_class =
    COBIWM_WAYLAND_DATA_SOURCE_CLASS (klass);

  data_source_class->send = cobiwm_wayland_data_source_primary_send;
  data_source_class->cancel = cobiwm_wayland_data_source_primary_cancel;
}

static void
cobiwm_wayland_data_source_finalize (GObject *object)
{
  CobiwmWaylandDataSource *source = COBIWM_WAYLAND_DATA_SOURCE (object);
  CobiwmWaylandDataSourcePrivate *priv =
    cobiwm_wayland_data_source_get_instance_private (source);
  char **pos;

  wl_array_for_each (pos, &priv->mime_types)
    g_free (*pos);
  wl_array_release (&priv->mime_types);

  G_OBJECT_CLASS (cobiwm_wayland_data_source_parent_class)->finalize (object);
}

static void
cobiwm_wayland_data_source_init (CobiwmWaylandDataSource *source)
{
  CobiwmWaylandDataSourcePrivate *priv =
    cobiwm_wayland_data_source_get_instance_private (source);

  wl_array_init (&priv->mime_types);
  priv->current_dnd_action = -1;
}

static void
cobiwm_wayland_data_source_class_init (CobiwmWaylandDataSourceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = cobiwm_wayland_data_source_finalize;
}

static void
cobiwm_wayland_drag_dest_focus_in (CobiwmWaylandDataDevice *data_device,
                                 CobiwmWaylandSurface    *surface,
                                 CobiwmWaylandDataOffer  *offer)
{
  CobiwmWaylandDragGrab *grab = data_device->current_grab;
  struct wl_display *display;
  struct wl_client *client;
  uint32_t source_actions;
  wl_fixed_t sx, sy;

  if (!grab->drag_focus_data_device)
    return;

  client = wl_resource_get_client (surface->resource);
  display = wl_client_get_display (client);

  grab->drag_focus_listener.notify = destroy_drag_focus;
  wl_resource_add_destroy_listener (grab->drag_focus_data_device,
                                    &grab->drag_focus_listener);

  if (wl_resource_get_version (offer->resource) >=
      WL_DATA_OFFER_SOURCE_ACTIONS_SINCE_VERSION)
    {
      source_actions = cobiwm_wayland_data_source_get_actions (offer->source);
      wl_data_offer_send_source_actions (offer->resource, source_actions);
    }

  cobiwm_wayland_pointer_get_relative_coordinates (grab->generic.pointer,
                                                 surface, &sx, &sy);
  wl_data_device_send_enter (grab->drag_focus_data_device,
                             wl_display_next_serial (display),
                             surface->resource, sx, sy, offer->resource);
}

static void
cobiwm_wayland_drag_dest_focus_out (CobiwmWaylandDataDevice *data_device,
                                  CobiwmWaylandSurface    *surface)
{
  CobiwmWaylandDragGrab *grab = data_device->current_grab;

  if (!grab->drag_focus_data_device)
    return;

  wl_data_device_send_leave (grab->drag_focus_data_device);
  wl_list_remove (&grab->drag_focus_listener.link);
  grab->drag_focus_data_device = NULL;
}

static void
cobiwm_wayland_drag_dest_motion (CobiwmWaylandDataDevice *data_device,
                               CobiwmWaylandSurface    *surface,
                               const ClutterEvent    *event)
{
  CobiwmWaylandDragGrab *grab = data_device->current_grab;
  wl_fixed_t sx, sy;

  if (!grab->drag_focus_data_device)
    return;

  cobiwm_wayland_pointer_get_relative_coordinates (grab->generic.pointer,
                                                 grab->drag_focus,
                                                 &sx, &sy);
  wl_data_device_send_motion (grab->drag_focus_data_device,
                              clutter_event_get_time (event),
                              sx, sy);
}

static void
cobiwm_wayland_drag_dest_drop (CobiwmWaylandDataDevice *data_device,
                             CobiwmWaylandSurface    *surface)
{
  CobiwmWaylandDragGrab *grab = data_device->current_grab;

  if (!grab->drag_focus_data_device)
    return;

  wl_data_device_send_drop (grab->drag_focus_data_device);
}

static void
cobiwm_wayland_drag_dest_update (CobiwmWaylandDataDevice *data_device,
                               CobiwmWaylandSurface    *surface)
{
}

static const CobiwmWaylandDragDestFuncs cobiwm_wayland_drag_dest_funcs = {
  cobiwm_wayland_drag_dest_focus_in,
  cobiwm_wayland_drag_dest_focus_out,
  cobiwm_wayland_drag_dest_motion,
  cobiwm_wayland_drag_dest_drop,
  cobiwm_wayland_drag_dest_update
};

const CobiwmWaylandDragDestFuncs *
cobiwm_wayland_data_device_get_drag_dest_funcs (void)
{
  return &cobiwm_wayland_drag_dest_funcs;
}

void
cobiwm_wayland_data_device_set_dnd_source (CobiwmWaylandDataDevice *data_device,
                                         CobiwmWaylandDataSource *source)
{
  if (data_device->dnd_data_source == source)
    return;

  if (data_device->dnd_data_source)
    g_object_remove_weak_pointer (G_OBJECT (data_device->dnd_data_source),
                                  (gpointer *)&data_device->dnd_data_source);

  data_device->dnd_data_source = source;

  if (source)
    g_object_add_weak_pointer (G_OBJECT (data_device->dnd_data_source),
                               (gpointer *)&data_device->dnd_data_source);

  wl_signal_emit (&data_device->dnd_ownership_signal, source);
}

void
cobiwm_wayland_data_device_set_selection (CobiwmWaylandDataDevice *data_device,
                                        CobiwmWaylandDataSource *source,
                                        guint32 serial)
{
  CobiwmWaylandSeat *seat = wl_container_of (data_device, seat, data_device);
  struct wl_resource *data_device_resource, *offer;
  struct wl_client *focus_client;

  if (data_device->selection_data_source &&
      data_device->selection_serial - serial < UINT32_MAX / 2)
    return;

  if (data_device->selection_data_source)
    {
      cobiwm_wayland_data_source_cancel (data_device->selection_data_source);
      g_object_weak_unref (G_OBJECT (data_device->selection_data_source),
                           selection_data_source_destroyed,
                           data_device);
      data_device->selection_data_source = NULL;
    }

  data_device->selection_data_source = source;
  data_device->selection_serial = serial;

  focus_client = cobiwm_wayland_keyboard_get_focus_client (&seat->keyboard);
  if (focus_client)
    {
      data_device_resource = wl_resource_find_for_client (&data_device->resource_list, focus_client);
      if (data_device_resource)
        {
          if (data_device->selection_data_source)
            {
              offer = cobiwm_wayland_data_source_send_offer (data_device->selection_data_source, data_device_resource);
              wl_data_device_send_selection (data_device_resource, offer);
            }
          else
            {
              wl_data_device_send_selection (data_device_resource, NULL);
            }
        }
    }

  if (source)
    {
      cobiwm_wayland_data_source_set_seat (source, seat);
      g_object_weak_ref (G_OBJECT (source),
                         selection_data_source_destroyed,
                         data_device);
    }

  wl_signal_emit (&data_device->selection_ownership_signal, source);
}

static void
data_device_set_selection (struct wl_client *client,
                           struct wl_resource *resource,
                           struct wl_resource *source_resource,
                           guint32 serial)
{
  CobiwmWaylandDataDevice *data_device = wl_resource_get_user_data (resource);
  CobiwmWaylandDataSourcePrivate *priv;
  CobiwmWaylandDataSource *source;

  if (source_resource)
    source = wl_resource_get_user_data (source_resource);
  else
    source = NULL;

  if (source)
    {
      priv = cobiwm_wayland_data_source_get_instance_private (source);

      if (priv->actions_set)
        {
          wl_resource_post_error(source_resource,
                                 WL_DATA_SOURCE_ERROR_INVALID_SOURCE,
                                 "cannot set drag-and-drop source as selection");
          return;
        }
    }

  /* FIXME: Store serial and check against incoming serial here. */
  cobiwm_wayland_data_device_set_selection (data_device, source, serial);
}

static const struct wl_data_device_interface data_device_interface = {
  data_device_start_drag,
  data_device_set_selection,
  default_destructor,
};

static void
primary_source_destroyed (gpointer  data,
                          GObject  *object_was_here)
{
  CobiwmWaylandDataDevice *data_device = data;
  CobiwmWaylandSeat *seat = wl_container_of (data_device, seat, data_device);
  struct wl_client *focus_client = NULL;

  data_device->primary_data_source = NULL;

  focus_client = cobiwm_wayland_keyboard_get_focus_client (&seat->keyboard);
  if (focus_client)
    {
      struct wl_resource *data_device_resource;

      data_device_resource = wl_resource_find_for_client (&data_device->primary_resource_list, focus_client);
      if (data_device_resource)
        gtk_primary_selection_device_send_selection (data_device_resource, NULL);
    }

  wl_signal_emit (&data_device->primary_ownership_signal, NULL);
}

void
cobiwm_wayland_data_device_set_primary (CobiwmWaylandDataDevice *data_device,
                                      CobiwmWaylandDataSource *source,
                                      guint32                serial)
{
  CobiwmWaylandSeat *seat = wl_container_of (data_device, seat, data_device);
  struct wl_resource *data_device_resource, *offer;
  struct wl_client *focus_client;

  if (COBIWM_IS_WAYLAND_DATA_SOURCE_PRIMARY (source))
    {
      struct wl_resource *resource;

      resource = COBIWM_WAYLAND_DATA_SOURCE_PRIMARY (source)->resource;

      if (wl_resource_get_client (resource) !=
          cobiwm_wayland_keyboard_get_focus_client (&seat->keyboard))
        return;
    }

  if (data_device->primary_data_source &&
      data_device->primary_serial - serial < UINT32_MAX / 2)
    return;

  if (data_device->primary_data_source)
    {
      cobiwm_wayland_data_source_cancel (data_device->primary_data_source);
      g_object_weak_unref (G_OBJECT (data_device->primary_data_source),
                           primary_source_destroyed,
                           data_device);
    }

  data_device->primary_data_source = source;
  data_device->primary_serial = serial;

  focus_client = cobiwm_wayland_keyboard_get_focus_client (&seat->keyboard);
  if (focus_client)
    {
      data_device_resource = wl_resource_find_for_client (&data_device->primary_resource_list, focus_client);
      if (data_device_resource)
        {
          if (data_device->primary_data_source)
            {
              offer = cobiwm_wayland_data_source_send_primary_offer (data_device->primary_data_source,
                                                                   data_device_resource);
              gtk_primary_selection_device_send_selection (data_device_resource, offer);
            }
          else
            {
              gtk_primary_selection_device_send_selection (data_device_resource, NULL);
            }
        }
    }

  if (source)
    {
      cobiwm_wayland_data_source_set_seat (source, seat);
      g_object_weak_ref (G_OBJECT (source),
                         primary_source_destroyed,
                         data_device);
    }

  wl_signal_emit (&data_device->primary_ownership_signal, source);
}

static void
primary_device_set_selection (struct wl_client   *client,
                              struct wl_resource *resource,
                              struct wl_resource *source_resource,
                              uint32_t            serial)
{
  CobiwmWaylandDataDevice *data_device = wl_resource_get_user_data (resource);
  CobiwmWaylandDataSource *source;

  source = wl_resource_get_user_data (source_resource);
  cobiwm_wayland_data_device_set_primary (data_device, source, serial);
}

static const struct gtk_primary_selection_device_interface primary_device_interface = {
  primary_device_set_selection,
  default_destructor,
};

static void
destroy_data_source (struct wl_resource *resource)
{
  CobiwmWaylandDataSourceWayland *source = wl_resource_get_user_data (resource);

  source->resource = NULL;
  g_object_unref (source);
}

static void
create_data_source (struct wl_client *client,
                    struct wl_resource *resource, guint32 id)
{
  struct wl_resource *source_resource;

  source_resource = wl_resource_create (client, &wl_data_source_interface,
                                        wl_resource_get_version (resource), id);
  cobiwm_wayland_data_source_wayland_new (source_resource);
}

static void
get_data_device (struct wl_client *client,
                 struct wl_resource *manager_resource,
                 guint32 id, struct wl_resource *seat_resource)
{
  CobiwmWaylandSeat *seat = wl_resource_get_user_data (seat_resource);
  struct wl_resource *cr;

  cr = wl_resource_create (client, &wl_data_device_interface, wl_resource_get_version (manager_resource), id);
  wl_resource_set_implementation (cr, &data_device_interface, &seat->data_device, unbind_resource);
  wl_list_insert (&seat->data_device.resource_list, wl_resource_get_link (cr));
}

static const struct wl_data_device_manager_interface manager_interface = {
  create_data_source,
  get_data_device
};

static void
destroy_primary_source (struct wl_resource *resource)
{
  CobiwmWaylandDataSourcePrimary *source = wl_resource_get_user_data (resource);

  source->resource = NULL;
  g_object_unref (source);
}

static void
primary_device_manager_create_source (struct wl_client   *client,
                                      struct wl_resource *manager_resource,
                                      guint32             id)
{
  struct wl_resource *source_resource;

  source_resource =
    wl_resource_create (client, &gtk_primary_selection_source_interface,
                        wl_resource_get_version (manager_resource),
                        id);
  cobiwm_wayland_data_source_primary_new (source_resource);
}

static void
primary_device_manager_get_device (struct wl_client   *client,
                                   struct wl_resource *manager_resource,
                                   guint32             id,
                                   struct wl_resource *seat_resource)
{
  CobiwmWaylandSeat *seat = wl_resource_get_user_data (seat_resource);
  struct wl_resource *cr;

  cr = wl_resource_create (client, &gtk_primary_selection_device_interface,
                           wl_resource_get_version (manager_resource), id);
  wl_resource_set_implementation (cr, &primary_device_interface,
                                  &seat->data_device, unbind_resource);
  wl_list_insert (&seat->data_device.primary_resource_list, wl_resource_get_link (cr));
}

static const struct gtk_primary_selection_device_manager_interface primary_manager_interface = {
  primary_device_manager_create_source,
  primary_device_manager_get_device,
  default_destructor,
};

static void
bind_manager (struct wl_client *client,
              void *data, guint32 version, guint32 id)
{
  struct wl_resource *resource;
  resource = wl_resource_create (client, &wl_data_device_manager_interface, version, id);
  wl_resource_set_implementation (resource, &manager_interface, NULL, NULL);
}

static void
bind_primary_manager (struct wl_client *client,
                      void             *data,
                      uint32_t          version,
                      uint32_t          id)
{
  struct wl_resource *resource;

  resource = wl_resource_create (client, &gtk_primary_selection_device_manager_interface,
                                 version, id);
  wl_resource_set_implementation (resource, &primary_manager_interface, NULL, NULL);
}

void
cobiwm_wayland_data_device_manager_init (CobiwmWaylandCompositor *compositor)
{
  if (wl_global_create (compositor->wayland_display,
			&wl_data_device_manager_interface,
			COBIWM_WL_DATA_DEVICE_MANAGER_VERSION,
			NULL, bind_manager) == NULL)
    g_error ("Could not create data_device");

  if (wl_global_create (compositor->wayland_display,
			&gtk_primary_selection_device_manager_interface,
			1, NULL, bind_primary_manager) == NULL)
    g_error ("Could not create data_device");
}

void
cobiwm_wayland_data_device_init (CobiwmWaylandDataDevice *data_device)
{
  wl_list_init (&data_device->resource_list);
  wl_list_init (&data_device->primary_resource_list);
  wl_signal_init (&data_device->selection_ownership_signal);
  wl_signal_init (&data_device->primary_ownership_signal);
  wl_signal_init (&data_device->dnd_ownership_signal);
}

void
cobiwm_wayland_data_device_set_keyboard_focus (CobiwmWaylandDataDevice *data_device)
{
  CobiwmWaylandSeat *seat = wl_container_of (data_device, seat, data_device);
  struct wl_client *focus_client;
  struct wl_resource *data_device_resource, *offer;
  CobiwmWaylandDataSource *source;

  focus_client = cobiwm_wayland_keyboard_get_focus_client (&seat->keyboard);

  if (focus_client == data_device->focus_client)
    return;

  data_device->focus_client = focus_client;

  if (!focus_client)
    return;

  data_device_resource = wl_resource_find_for_client (&data_device->resource_list, focus_client);
  if (data_device_resource)
    {
      source = data_device->selection_data_source;
      if (source)
        {
          offer = cobiwm_wayland_data_source_send_offer (source, data_device_resource);
          wl_data_device_send_selection (data_device_resource, offer);
        }
      else
        {
          wl_data_device_send_selection (data_device_resource, NULL);
        }
    }

  data_device_resource = wl_resource_find_for_client (&data_device->primary_resource_list, focus_client);
  if (data_device_resource)
    {
      source = data_device->primary_data_source;
      if (source)
        {
          offer = cobiwm_wayland_data_source_send_primary_offer (source, data_device_resource);
          gtk_primary_selection_device_send_selection (data_device_resource, offer);
        }
      else
        {
          gtk_primary_selection_device_send_selection (data_device_resource, NULL);
        }
    }
}

gboolean
cobiwm_wayland_data_device_is_dnd_surface (CobiwmWaylandDataDevice *data_device,
                                         CobiwmWaylandSurface    *surface)
{
  return data_device->current_grab &&
    data_device->current_grab->drag_surface == surface;
}

gboolean
cobiwm_wayland_data_source_has_mime_type (const CobiwmWaylandDataSource *source,
                                        const gchar                 *mime_type)
{
  CobiwmWaylandDataSourcePrivate *priv =
    cobiwm_wayland_data_source_get_instance_private ((CobiwmWaylandDataSource *)source);
  gchar **p;

  wl_array_for_each (p, &priv->mime_types)
    {
      if (g_strcmp0 (mime_type, *p) == 0)
        return TRUE;
    }

  return FALSE;
}

static CobiwmWaylandDataSource *
cobiwm_wayland_data_source_wayland_new (struct wl_resource *resource)
{
  CobiwmWaylandDataSourceWayland *source_wayland =
   g_object_new (COBIWM_TYPE_WAYLAND_DATA_SOURCE_WAYLAND, NULL);

  source_wayland->resource = resource;
  wl_resource_set_implementation (resource, &data_source_interface,
                                  source_wayland, destroy_data_source);

  return COBIWM_WAYLAND_DATA_SOURCE (source_wayland);
}

static CobiwmWaylandDataSource *
cobiwm_wayland_data_source_primary_new (struct wl_resource *resource)
{
  CobiwmWaylandDataSourcePrimary *source_primary =
    g_object_new (COBIWM_TYPE_WAYLAND_DATA_SOURCE_PRIMARY, NULL);

  source_primary->resource = resource;
  wl_resource_set_implementation (resource, &primary_source_interface,
                                  source_primary, destroy_primary_source);

  return COBIWM_WAYLAND_DATA_SOURCE (source_primary);
}

gboolean
cobiwm_wayland_data_source_add_mime_type (CobiwmWaylandDataSource *source,
                                        const gchar           *mime_type)
{
  CobiwmWaylandDataSourcePrivate *priv =
    cobiwm_wayland_data_source_get_instance_private (source);
  gchar **pos;

  pos = wl_array_add (&priv->mime_types, sizeof (*pos));

  if (pos)
    {
      *pos = g_strdup (mime_type);
      return *pos != NULL;
    }

  return FALSE;
}
