/*
 * Wayland Support
 *
 * Copyright (C) 2013 Intel Corporation
 * Copyright (C) 2015 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

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

#include "config.h"

#include "cobiwm-wayland-popup.h"

#include "cobiwm-wayland-pointer.h"
#include "cobiwm-wayland-private.h"
#include "cobiwm-wayland-surface.h"

struct _CobiwmWaylandPopupGrab
{
  CobiwmWaylandPointerGrab  generic;

  struct wl_client       *grab_client;
  struct wl_list          all_popups;
};

struct _CobiwmWaylandPopup
{
  CobiwmWaylandPopupGrab *grab;
  CobiwmWaylandSurface   *surface;
  struct wl_listener    surface_destroy_listener;
  struct wl_signal      destroy_signal;

  struct wl_list        link;
};

static void
popup_grab_focus (CobiwmWaylandPointerGrab *grab,
		  CobiwmWaylandSurface     *surface)
{
  CobiwmWaylandPopupGrab *popup_grab = (CobiwmWaylandPopupGrab*)grab;

  /* Popup grabs are in owner-events mode (ie, events for the same client
     are reported as normal) */
  if (surface &&
      wl_resource_get_client (surface->resource) == popup_grab->grab_client)
    cobiwm_wayland_pointer_set_focus (grab->pointer, surface);
  else
    cobiwm_wayland_pointer_set_focus (grab->pointer, NULL);
}

static void
popup_grab_motion (CobiwmWaylandPointerGrab *grab,
		   const ClutterEvent     *event)
{
  cobiwm_wayland_pointer_send_motion (grab->pointer, event);
}

static void
popup_grab_button (CobiwmWaylandPointerGrab *grab,
		   const ClutterEvent     *event)
{
  CobiwmWaylandPointer *pointer = grab->pointer;

  if (pointer->focus_surface)
    cobiwm_wayland_pointer_send_button (grab->pointer, event);
  else if (clutter_event_type (event) == CLUTTER_BUTTON_RELEASE &&
	   pointer->button_count == 0)
    cobiwm_wayland_pointer_end_popup_grab (grab->pointer);
}

static CobiwmWaylandPointerGrabInterface popup_grab_interface = {
  popup_grab_focus,
  popup_grab_motion,
  popup_grab_button
};

CobiwmWaylandPopupGrab *
cobiwm_wayland_popup_grab_create (CobiwmWaylandPointer *pointer,
                                struct wl_client   *client)
{
  CobiwmWaylandPopupGrab *grab;

  grab = g_slice_new0 (CobiwmWaylandPopupGrab);
  grab->generic.interface = &popup_grab_interface;
  grab->generic.pointer = pointer;
  grab->grab_client = client;
  wl_list_init (&grab->all_popups);

  return grab;
}

void
cobiwm_wayland_popup_grab_destroy (CobiwmWaylandPopupGrab *grab)
{
  g_slice_free (CobiwmWaylandPopupGrab, grab);
}

void
cobiwm_wayland_popup_grab_begin (CobiwmWaylandPopupGrab *grab,
                               CobiwmWaylandSurface   *surface)
{
  CobiwmWaylandPointer *pointer = grab->generic.pointer;
  CobiwmWindow *window = surface->window;

  cobiwm_wayland_pointer_start_grab (pointer, (CobiwmWaylandPointerGrab*)grab);
  cobiwm_display_begin_grab_op (window->display,
                              window->screen,
                              window,
                              COBIWM_GRAB_OP_WAYLAND_POPUP,
                              FALSE, /* pointer_already_grabbed */
                              FALSE, /* frame_action */
                              1, /* button. XXX? */
                              0, /* modmask */
                              cobiwm_display_get_current_time_roundtrip (
                                window->display),
                              pointer->grab_x,
                              pointer->grab_y);
}

void
cobiwm_wayland_popup_grab_end (CobiwmWaylandPopupGrab *grab)
{
  CobiwmWaylandPopup *popup, *tmp;

  g_assert (grab->generic.interface == &popup_grab_interface);

  wl_list_for_each_safe (popup, tmp, &grab->all_popups, link)
    {
      cobiwm_wayland_surface_popup_done (popup->surface);
      cobiwm_wayland_popup_destroy (popup);
    }

  {
    CobiwmDisplay *display = cobiwm_get_display ();
    cobiwm_display_end_grab_op (display,
                              cobiwm_display_get_current_time_roundtrip (display));
  }

  cobiwm_wayland_pointer_end_grab (grab->generic.pointer);
}

CobiwmWaylandSurface *
cobiwm_wayland_popup_grab_get_top_popup (CobiwmWaylandPopupGrab *grab)
{
  CobiwmWaylandPopup *popup;

  g_assert (!wl_list_empty (&grab->all_popups));
  popup = wl_container_of (grab->all_popups.next, popup, link);

  return popup->surface;
}

gboolean
cobiwm_wayland_pointer_grab_is_popup_grab (CobiwmWaylandPointerGrab *grab)
{
  return grab->interface == &popup_grab_interface;
}

void
cobiwm_wayland_popup_destroy (CobiwmWaylandPopup *popup)
{
  wl_signal_emit (&popup->destroy_signal, popup);

  wl_list_remove (&popup->surface_destroy_listener.link);
  wl_list_remove (&popup->link);
  g_slice_free (CobiwmWaylandPopup, popup);
}

void
cobiwm_wayland_popup_dismiss (CobiwmWaylandPopup *popup)
{
  CobiwmWaylandPopupGrab *popup_grab = popup->grab;

  cobiwm_wayland_popup_destroy (popup);

  if (wl_list_empty (&popup_grab->all_popups))
    cobiwm_wayland_pointer_end_popup_grab (popup_grab->generic.pointer);
}

CobiwmWaylandSurface *
cobiwm_wayland_popup_get_top_popup (CobiwmWaylandPopup *popup)
{
  return cobiwm_wayland_popup_grab_get_top_popup (popup->grab);
}

struct wl_signal *
cobiwm_wayland_popup_get_destroy_signal (CobiwmWaylandPopup *popup)
{
  return &popup->destroy_signal;
}

static void
on_popup_surface_destroy (struct wl_listener *listener,
			  void               *data)
{
  CobiwmWaylandPopup *popup =
    wl_container_of (listener, popup, surface_destroy_listener);

  cobiwm_wayland_popup_dismiss (popup);
}

CobiwmWaylandPopup *
cobiwm_wayland_popup_create (CobiwmWaylandSurface   *surface,
                           CobiwmWaylandPopupGrab *grab)
{
  CobiwmWaylandPopup *popup;

  /* Don't allow creating popups if the grab has a different client. */
  if (grab->grab_client != wl_resource_get_client (surface->resource))
    return NULL;

  popup = g_slice_new0 (CobiwmWaylandPopup);
  popup->grab = grab;
  popup->surface = surface;
  popup->surface_destroy_listener.notify = on_popup_surface_destroy;
  wl_signal_init (&popup->destroy_signal);

  if (surface->xdg_popup)
    {
      wl_resource_add_destroy_listener (surface->xdg_popup,
                                        &popup->surface_destroy_listener);
    }
  else if (surface->wl_shell_surface)
    {
      wl_resource_add_destroy_listener (surface->wl_shell_surface,
                                        &popup->surface_destroy_listener);
    }

  wl_list_insert (&grab->all_popups, &popup->link);

  return popup;
}
