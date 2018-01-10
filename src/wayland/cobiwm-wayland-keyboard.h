/*
 * Wayland Support
 *
 * Copyright (C) 2013 Intel Corporation
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

/*
 * Copyright © 2008-2011 Kristian Høgsberg
 * Copyright © 2012 Collabora, Ltd.
 *
 * Permission to use, copy, modify, distribute, and sell this software and
 * its documentation for any purpose is hereby granted without fee, provided
 * that the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of the copyright holders not be used in
 * advertising or publicity pertaining to distribution of the software
 * without specific, written prior permission.  The copyright holders make
 * no representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS
 * SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
 * RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF
 * CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef COBIWM_WAYLAND_KEYBOARD_H
#define COBIWM_WAYLAND_KEYBOARD_H

#include <clutter/clutter.h>
#include <wayland-server.h>
#include <xkbcommon/xkbcommon.h>

struct _CobiwmWaylandKeyboardGrabInterface
{
  gboolean (*key)       (CobiwmWaylandKeyboardGrab *grab,
                         const ClutterEvent      *event);
  void     (*modifiers) (CobiwmWaylandKeyboardGrab *grab,
                         ClutterModifierType      modifiers);
};

struct _CobiwmWaylandKeyboardGrab
{
  const CobiwmWaylandKeyboardGrabInterface *interface;
  CobiwmWaylandKeyboard *keyboard;
};

typedef struct
{
  struct xkb_keymap *keymap;
  struct xkb_state *state;
  int keymap_fd;
  size_t keymap_size;
  char *keymap_area;
} CobiwmWaylandXkbInfo;

struct _CobiwmWaylandKeyboard
{
  struct wl_display *display;

  struct wl_list resource_list;
  struct wl_list focus_resource_list;

  CobiwmWaylandSurface *focus_surface;
  struct wl_listener focus_surface_listener;
  uint32_t focus_serial;
  uint32_t key_serial;

  CobiwmWaylandXkbInfo xkb_info;
  enum xkb_state_component mods_changed;

  CobiwmWaylandKeyboardGrab *grab;
  CobiwmWaylandKeyboardGrab default_grab;

  GSettings *settings;
};

void cobiwm_wayland_keyboard_init (CobiwmWaylandKeyboard *keyboard,
                                 struct wl_display   *display);

void cobiwm_wayland_keyboard_release (CobiwmWaylandKeyboard *keyboard);

void cobiwm_wayland_keyboard_update (CobiwmWaylandKeyboard *keyboard,
                                   const ClutterKeyEvent *event);

gboolean cobiwm_wayland_keyboard_handle_event (CobiwmWaylandKeyboard *keyboard,
                                             const ClutterKeyEvent *event);
void cobiwm_wayland_keyboard_update_key_state (CobiwmWaylandKeyboard *compositor,
                                             char                *key_vector,
                                             int                  key_vector_len,
                                             int                  offset);

void cobiwm_wayland_keyboard_set_focus (CobiwmWaylandKeyboard *keyboard,
                                      CobiwmWaylandSurface *surface);

struct wl_client * cobiwm_wayland_keyboard_get_focus_client (CobiwmWaylandKeyboard *keyboard);

void cobiwm_wayland_keyboard_create_new_resource (CobiwmWaylandKeyboard *keyboard,
                                                struct wl_client    *client,
                                                struct wl_resource  *seat_resource,
                                                uint32_t id);

gboolean cobiwm_wayland_keyboard_can_popup (CobiwmWaylandKeyboard *keyboard,
                                          uint32_t             serial);

void cobiwm_wayland_keyboard_start_grab (CobiwmWaylandKeyboard     *keyboard,
                                       CobiwmWaylandKeyboardGrab *grab);
void cobiwm_wayland_keyboard_end_grab   (CobiwmWaylandKeyboard     *keyboard);

#endif /* COBIWM_WAYLAND_KEYBOARD_H */
