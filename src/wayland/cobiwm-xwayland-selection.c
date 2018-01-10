/*
 * Copyright © 2012 Intel Corporation
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

/* The file is loosely based on xwayland/selection.c from Weston */

#include "config.h"

#define _GNU_SOURCE
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <glib-unix.h>
#include <gio/gunixoutputstream.h>
#include <gio/gunixinputstream.h>
#include <gdk/gdkx.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xfixes.h>
#include <errors.h>
#include "cobiwm-xwayland-private.h"
#include "cobiwm-xwayland-selection-private.h"
#include "cobiwm-wayland-data-device.h"

#define INCR_CHUNK_SIZE (128 * 1024)
#define XDND_VERSION 5

typedef struct {
  CobiwmXWaylandSelection *selection_data;
  GInputStream *stream;
  GCancellable *cancellable;
  CobiwmWindow *window;
  XSelectionRequestEvent request_event;
  guchar buffer[INCR_CHUNK_SIZE];
  gsize buffer_len;
  guint incr : 1;
} WaylandSelectionData;

typedef struct {
  CobiwmXWaylandSelection *selection_data;
  GOutputStream *stream;
  GCancellable *cancellable;
  gchar *mime_type;
  guint incr : 1;
} X11SelectionData;

typedef struct {
  Atom selection_atom;
  Window window;
  Window owner;
  Time timestamp;
  CobiwmWaylandDataSource *source; /* owned by CobiwmWaylandDataDevice */
  WaylandSelectionData *wayland_selection;
  X11SelectionData *x11_selection;

  struct wl_listener ownership_listener;
} CobiwmSelectionBridge;

typedef struct {
  CobiwmSelectionBridge selection;
  CobiwmWaylandSurface *focus_surface;
  Window dnd_window; /* Cobiwm-internal window, acts as peer on wayland drop sites */
  Window dnd_dest; /* X11 drag dest window */
  guint32 last_motion_time;
} CobiwmDndBridge;

struct _CobiwmWaylandDataSourceXWayland
{
  CobiwmWaylandDataSource parent;

  CobiwmSelectionBridge *selection;
  guint has_utf8_string_atom : 1;
};

struct _CobiwmXWaylandSelection {
  CobiwmSelectionBridge clipboard;
  CobiwmSelectionBridge primary;
  CobiwmDndBridge dnd;
};

enum {
  ATOM_DND_SELECTION,
  ATOM_DND_AWARE,
  ATOM_DND_STATUS,
  ATOM_DND_POSITION,
  ATOM_DND_ENTER,
  ATOM_DND_LEAVE,
  ATOM_DND_DROP,
  ATOM_DND_FINISHED,
  ATOM_DND_PROXY,
  ATOM_DND_TYPE_LIST,
  ATOM_DND_ACTION_MOVE,
  ATOM_DND_ACTION_COPY,
  ATOM_DND_ACTION_ASK,
  ATOM_DND_ACTION_PRIVATE,
  N_DND_ATOMS
};

/* Matches order in enum above */
const gchar *atom_names[] = {
  "XdndSelection",
  "XdndAware",
  "XdndStatus",
  "XdndPosition",
  "XdndEnter",
  "XdndLeave",
  "XdndDrop",
  "XdndFinished",
  "XdndProxy",
  "XdndTypeList",
  "XdndActionMove",
  "XdndActionCopy",
  "XdndActionAsk",
  "XdndActionPrivate",
  NULL
};

Atom xdnd_atoms[N_DND_ATOMS];

G_DEFINE_TYPE (CobiwmWaylandDataSourceXWayland, cobiwm_wayland_data_source_xwayland,
               COBIWM_TYPE_WAYLAND_DATA_SOURCE);

/* XDND helpers */
static Atom
action_to_atom (uint32_t action)
{
  if (action & WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY)
    return xdnd_atoms[ATOM_DND_ACTION_COPY];
  else if (action & WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE)
    return xdnd_atoms[ATOM_DND_ACTION_MOVE];
  else if (action & WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK)
    return xdnd_atoms[ATOM_DND_ACTION_ASK];
  else
    return None;
}

static enum wl_data_device_manager_dnd_action
atom_to_action (Atom atom)
{
  if (atom == xdnd_atoms[ATOM_DND_ACTION_COPY] ||
      atom == xdnd_atoms[ATOM_DND_ACTION_PRIVATE])
    return WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY;
  else if (atom == xdnd_atoms[ATOM_DND_ACTION_MOVE])
    return WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE;
  else if (atom == xdnd_atoms[ATOM_DND_ACTION_ASK])
    return WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK;
  else
    return WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE;
}

static void
xdnd_send_enter (CobiwmXWaylandSelection *selection_data,
                 Window                 dest)
{
  CobiwmWaylandCompositor *compositor = cobiwm_wayland_compositor_get_default ();
  CobiwmSelectionBridge *selection = &selection_data->dnd.selection;
  Display *xdisplay = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());
  CobiwmWaylandDataSource *data_source;
  XEvent xev = { 0 };
  gchar **p;
  struct wl_array *source_mime_types;

  data_source = compositor->seat->data_device.dnd_data_source;
  xev.xclient.type = ClientMessage;
  xev.xclient.message_type = xdnd_atoms[ATOM_DND_ENTER];
  xev.xclient.format = 32;
  xev.xclient.window = dest;

  xev.xclient.data.l[0] = selection->window;
  xev.xclient.data.l[1] = XDND_VERSION << 24; /* version */
  xev.xclient.data.l[2] = xev.xclient.data.l[3] = xev.xclient.data.l[4] = 0;

  source_mime_types = cobiwm_wayland_data_source_get_mime_types (data_source);
  if (source_mime_types->size <= 3)
    {
      /* The mimetype atoms fit in this same message */
      gint i = 2;

      wl_array_for_each (p, source_mime_types)
        {
          xev.xclient.data.l[i++] = gdk_x11_get_xatom_by_name (*p);
        }
    }
  else
    {
      /* We have more than 3 mimetypes, we must set up
       * the mimetype list as a XdndTypeList property.
       */
      Atom *atomlist;
      gint i = 0;

      xev.xclient.data.l[1] |= 1;
      atomlist = g_new0 (Atom, source_mime_types->size);

      wl_array_for_each (p, source_mime_types)
        {
          atomlist[i++] = gdk_x11_get_xatom_by_name (*p);
        }

      XChangeProperty (xdisplay, selection->window,
                       xdnd_atoms[ATOM_DND_TYPE_LIST],
                       XA_ATOM, 32, PropModeReplace,
                       (guchar *) atomlist, i);
    }

  XSendEvent (xdisplay, dest, False, NoEventMask, &xev);
}

static void
xdnd_send_leave (CobiwmXWaylandSelection *selection_data,
                 Window                 dest)
{
  CobiwmSelectionBridge *selection = &selection_data->dnd.selection;
  Display *xdisplay = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());
  XEvent xev = { 0 };

  xev.xclient.type = ClientMessage;
  xev.xclient.message_type = xdnd_atoms[ATOM_DND_LEAVE];
  xev.xclient.format = 32;
  xev.xclient.window = dest;
  xev.xclient.data.l[0] = selection->window;

  XSendEvent (xdisplay, dest, False, NoEventMask, &xev);
}

static void
xdnd_send_position (CobiwmXWaylandSelection *selection_data,
                    Window                 dest,
                    uint32_t               time,
                    int                    x,
                    int                    y)
{
  CobiwmWaylandCompositor *compositor = cobiwm_wayland_compositor_get_default ();
  CobiwmSelectionBridge *selection = &selection_data->dnd.selection;
  CobiwmWaylandDataSource *source = compositor->seat->data_device.dnd_data_source;
  Display *xdisplay = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());
  uint32_t action = 0, user_action, actions;
  XEvent xev = { 0 };

  user_action = cobiwm_wayland_data_source_get_user_action (source);
  actions = cobiwm_wayland_data_source_get_actions (source);

  if (user_action & actions)
    action = user_action;
  if (!action)
    action = actions;

  xev.xclient.type = ClientMessage;
  xev.xclient.message_type = xdnd_atoms[ATOM_DND_POSITION];
  xev.xclient.format = 32;
  xev.xclient.window = dest;

  xev.xclient.data.l[0] = selection->window;
  xev.xclient.data.l[1] = 0;
  xev.xclient.data.l[2] = (x << 16) | y;
  xev.xclient.data.l[3] = time;
  xev.xclient.data.l[4] = action_to_atom (action);

  XSendEvent (xdisplay, dest, False, NoEventMask, &xev);
}

static void
xdnd_send_drop (CobiwmXWaylandSelection *selection_data,
                Window                 dest,
                uint32_t               time)
{
  CobiwmSelectionBridge *selection = &selection_data->dnd.selection;
  Display *xdisplay = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());
  XEvent xev = { 0 };

  xev.xclient.type = ClientMessage;
  xev.xclient.message_type = xdnd_atoms[ATOM_DND_DROP];
  xev.xclient.format = 32;
  xev.xclient.window = dest;

  xev.xclient.data.l[0] = selection->window;
  xev.xclient.data.l[2] = time;

  XSendEvent (xdisplay, dest, False, NoEventMask, &xev);
}

static void
xdnd_send_finished (CobiwmXWaylandSelection *selection_data,
                    Window                 dest,
                    gboolean               accepted)
{
  CobiwmDndBridge *selection = &selection_data->dnd;
  Display *xdisplay = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());
  CobiwmWaylandDataSource *source = selection_data->dnd.selection.source;
  uint32_t action = 0;
  XEvent xev = { 0 };

  xev.xclient.type = ClientMessage;
  xev.xclient.message_type = xdnd_atoms[ATOM_DND_FINISHED];
  xev.xclient.format = 32;
  xev.xclient.window = dest;

  xev.xclient.data.l[0] = selection->dnd_window;

  if (accepted)
    {
      action = cobiwm_wayland_data_source_get_current_action (source);
      xev.xclient.data.l[1] = 1; /* Drop successful */
      xev.xclient.data.l[2] = action_to_atom (action);
    }

  XSendEvent (xdisplay, dest, False, NoEventMask, &xev);
}

static void
xdnd_send_status (CobiwmXWaylandSelection *selection_data,
                  Window                 dest,
                  uint32_t               action)
{
  CobiwmDndBridge *selection = &selection_data->dnd;
  Display *xdisplay = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());
  XEvent xev = { 0 };

  xev.xclient.type = ClientMessage;
  xev.xclient.message_type = xdnd_atoms[ATOM_DND_STATUS];
  xev.xclient.format = 32;
  xev.xclient.window = dest;

  xev.xclient.data.l[0] = selection->dnd_window;
  xev.xclient.data.l[1] = 1 << 1; /* Bit 2: dest wants XdndPosition messages */
  xev.xclient.data.l[4] = action_to_atom (action);

  if (xev.xclient.data.l[4])
    xev.xclient.data.l[1] |= 1 << 0; /* Bit 1: dest accepts the drop */

  XSendEvent (xdisplay, dest, False, NoEventMask, &xev);
}

static void
cobiwm_xwayland_init_dnd (CobiwmXWaylandManager *manager)
{
  Display *xdisplay = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());
  CobiwmDndBridge *dnd = &manager->selection_data->dnd;
  XSetWindowAttributes attributes;
  guint32 i, version = XDND_VERSION;

  for (i = 0; i < N_DND_ATOMS; i++)
    xdnd_atoms[i] = gdk_x11_get_xatom_by_name (atom_names[i]);

  attributes.event_mask = PropertyChangeMask | SubstructureNotifyMask;
  attributes.override_redirect = True;

  dnd->dnd_window = XCreateWindow (xdisplay,
                                   gdk_x11_window_get_xid (gdk_get_default_root_window ()),
                                   -1, -1, 1, 1,
                                   0, /* border width */
                                   0, /* depth */
                                   InputOnly, /* class */
                                   CopyFromParent, /* visual */
                                   CWEventMask | CWOverrideRedirect,
                                   &attributes);
  XChangeProperty (xdisplay, dnd->dnd_window,
                   xdnd_atoms[ATOM_DND_AWARE],
                   XA_ATOM, 32, PropModeReplace,
                   (guchar*) &version, 1);
}

static void
cobiwm_xwayland_shutdown_dnd (CobiwmXWaylandManager *manager)
{
  CobiwmDndBridge *dnd = &manager->selection_data->dnd;

  XDestroyWindow (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()),
                  dnd->dnd_window);
  dnd->dnd_window = None;
}

/* X11/Wayland data bridges */

static CobiwmSelectionBridge *
atom_to_selection_bridge (CobiwmWaylandCompositor *compositor,
                          Atom                   selection_atom)
{
  CobiwmXWaylandSelection *selection_data = compositor->xwayland_manager.selection_data;

  if (selection_atom == selection_data->clipboard.selection_atom)
    return &selection_data->clipboard;
  else if (selection_atom == selection_data->primary.selection_atom)
    return &selection_data->primary;
  else if (selection_atom == selection_data->dnd.selection.selection_atom)
    return &selection_data->dnd.selection;
  else
    return NULL;
}

static X11SelectionData *
x11_selection_data_new (CobiwmXWaylandSelection *selection_data,
                        int                    fd,
                        const char            *mime_type)
{
  X11SelectionData *data;

  data = g_slice_new0 (X11SelectionData);
  data->selection_data = selection_data;
  data->stream = g_unix_output_stream_new (fd, TRUE);
  data->cancellable = g_cancellable_new ();
  data->mime_type = g_strdup (mime_type);

  return data;
}

static void
x11_selection_data_free (X11SelectionData *data)
{
  g_cancellable_cancel (data->cancellable);
  g_object_unref (data->cancellable);
  g_object_unref (data->stream);
  g_free (data->mime_type);
  g_slice_free (X11SelectionData, data);
}

static void
x11_selection_data_send_finished (CobiwmSelectionBridge *selection,
                                  gboolean             success)
{
  uint32_t action = cobiwm_wayland_data_source_get_current_action (selection->source);

  if (!selection->x11_selection)
    return;

  if (success && action == WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE)
    {
      Display *xdisplay = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());

      /* Request data deletion on the drag source */
      XConvertSelection (xdisplay,
                         selection->selection_atom,
                         gdk_x11_get_xatom_by_name ("DELETE"),
                         gdk_x11_get_xatom_by_name ("_COBIWM_SELECTION"),
                         selection->window,
                         CurrentTime);
    }

  xdnd_send_finished (selection->x11_selection->selection_data,
                      selection->owner, success);
}

static void
x11_selection_data_finish (CobiwmSelectionBridge *selection,
                           gboolean             success)
{
  if (!selection->x11_selection)
    return;

  if (selection == &selection->x11_selection->selection_data->dnd.selection)
    x11_selection_data_send_finished (selection, success);

  g_clear_pointer (&selection->x11_selection,
                   (GDestroyNotify) x11_selection_data_free);
}

static void
x11_selection_data_close (X11SelectionData *data)
{
  g_output_stream_close (data->stream, data->cancellable, NULL);
}

static void
x11_data_write_cb (GObject      *object,
                   GAsyncResult *res,
                   gpointer      user_data)
{
  CobiwmSelectionBridge *selection = user_data;
  X11SelectionData *data = selection->x11_selection;
  GError *error = NULL;
  gboolean success = TRUE;

  g_output_stream_write_finish (G_OUTPUT_STREAM (object), res, &error);

  if (error)
    {
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        {
          g_error_free (error);
          return;
        }

      g_warning ("Error writing from X11 selection: %s\n", error->message);
      g_error_free (error);
      success = FALSE;
    }

  if (success && data->incr)
    {
      Display *xdisplay = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());
      XDeleteProperty (xdisplay, selection->window,
                       gdk_x11_get_xatom_by_name ("_COBIWM_SELECTION"));
    }
  else
    {
      x11_selection_data_close (selection->x11_selection);
      x11_selection_data_finish (selection, success);
    }
}

static void
x11_selection_data_write (CobiwmSelectionBridge *selection,
                          guchar              *buffer,
                          gulong               len)
{
  X11SelectionData *data = selection->x11_selection;

  g_output_stream_write_async (data->stream, buffer, len,
                               G_PRIORITY_DEFAULT, data->cancellable,
                               x11_data_write_cb, selection);
}

static CobiwmWaylandDataSource *
data_device_get_active_source_for_atom (CobiwmWaylandDataDevice *data_device,
                                        Atom                   selection_atom)
{
  if (selection_atom == gdk_x11_get_xatom_by_name ("CLIPBOARD"))
    return data_device->selection_data_source;
  else if (selection_atom == gdk_x11_get_xatom_by_name ("PRIMARY"))
    return data_device->primary_data_source;
  else if (selection_atom == xdnd_atoms[ATOM_DND_SELECTION])
    return data_device->dnd_data_source;
  else
    return NULL;
}

static WaylandSelectionData *
wayland_selection_data_new (XSelectionRequestEvent *request_event,
                            CobiwmWaylandCompositor  *compositor)
{
  CobiwmWaylandDataDevice *data_device;
  CobiwmWaylandDataSource *wayland_source;
  CobiwmSelectionBridge *selection;
  WaylandSelectionData *data;
  const gchar *mime_type;
  GError *error = NULL;
  int p[2];

  selection = atom_to_selection_bridge (compositor, request_event->selection);

  if (!selection)
    return NULL;

  if (!g_unix_open_pipe (p, FD_CLOEXEC, &error))
    {
      g_critical ("Failed to open pipe: %s\n", error->message);
      g_error_free (error);
      return NULL;
    }

  data_device = &compositor->seat->data_device;
  mime_type = gdk_x11_get_xatom_name (request_event->target);

  if (!g_unix_set_fd_nonblocking (p[0], TRUE, &error) ||
      !g_unix_set_fd_nonblocking (p[1], TRUE, &error))
    {
      if (error)
        {
          g_critical ("Failed to make fds non-blocking: %s\n", error->message);
          g_error_free (error);
        }

      close (p[0]);
      close (p[1]);
      return NULL;
    }

  wayland_source = data_device_get_active_source_for_atom (data_device,
                                                           selection->selection_atom),
  cobiwm_wayland_data_source_send (wayland_source, mime_type, p[1]);

  data = g_slice_new0 (WaylandSelectionData);
  data->request_event = *request_event;
  data->cancellable = g_cancellable_new ();
  data->stream = g_unix_input_stream_new (p[0], TRUE);

  data->window = cobiwm_display_lookup_x_window (cobiwm_get_display (),
                                               data->request_event.requestor);

  if (!data->window)
    {
      /* Not a managed window, set the PropertyChangeMask
       * for INCR deletion notifications.
       */
      XSelectInput (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()),
                    data->request_event.requestor, PropertyChangeMask);
    }

  return data;
}

static void
reply_selection_request (XSelectionRequestEvent *request_event,
                         gboolean                accepted)
{
  Display *xdisplay = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());
  XSelectionEvent event;

  memset(&event, 0, sizeof (XSelectionEvent));
  event.type = SelectionNotify;
  event.time = request_event->time;
  event.requestor = request_event->requestor;
  event.selection = request_event->selection;
  event.target = request_event->target;
  event.property = accepted ? request_event->property : None;

  XSendEvent (xdisplay, request_event->requestor,
              False, NoEventMask, (XEvent *) &event);
}

static void
wayland_selection_data_free (WaylandSelectionData *data)
{
  if (!data->window)
    {
      CobiwmDisplay *display = cobiwm_get_display ();

      cobiwm_error_trap_push (display);
      XSelectInput (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()),
                    data->request_event.requestor, NoEventMask);
      cobiwm_error_trap_pop (display);
    }

  g_cancellable_cancel (data->cancellable);
  g_object_unref (data->cancellable);
  g_object_unref (data->stream);
  g_slice_free (WaylandSelectionData, data);
}

static void
wayland_selection_update_x11_property (WaylandSelectionData *data)
{
  Display *xdisplay = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());

  XChangeProperty (xdisplay,
                   data->request_event.requestor,
                   data->request_event.property,
                   data->request_event.target,
                   8, PropModeReplace,
                   data->buffer, data->buffer_len);
  data->buffer_len = 0;
}

static void
wayland_data_read_cb (GObject      *object,
                      GAsyncResult *res,
                      gpointer      user_data)
{
  CobiwmSelectionBridge *selection = user_data;
  WaylandSelectionData *data = selection->wayland_selection;
  GError *error = NULL;
  gsize bytes_read;

  bytes_read = g_input_stream_read_finish (G_INPUT_STREAM (object),
                                           res, &error);
  if (error)
    {
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        {
          g_error_free (error);
          return;
        }

      g_warning ("Error transfering wayland clipboard to X11: %s\n",
                 error->message);
      g_error_free (error);

      if (data && data->stream == G_INPUT_STREAM (object))
        {
          reply_selection_request (&data->request_event, FALSE);
          g_clear_pointer (&selection->wayland_selection,
                           (GDestroyNotify) wayland_selection_data_free);
        }

      return;
    }

  data->buffer_len = bytes_read;

  if (bytes_read == INCR_CHUNK_SIZE)
    {
      if (!data->incr)
        {
          Display *xdisplay = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());
          guint32 incr_chunk_size = INCR_CHUNK_SIZE;

          /* Not yet in incr */
          data->incr = TRUE;
          XChangeProperty (xdisplay,
                           data->request_event.requestor,
                           data->request_event.property,
                           gdk_x11_get_xatom_by_name ("INCR"),
                           32, PropModeReplace,
                           (guchar *) &incr_chunk_size, 1);
          reply_selection_request (&data->request_event, TRUE);
        }
      else
        wayland_selection_update_x11_property (data);
    }
  else
    {
      if (!data->incr)
        {
          /* Non-incr transfer finished */
          wayland_selection_update_x11_property (data);
          reply_selection_request (&data->request_event, TRUE);
        }
      else if (data->incr)
        {
          /* Incr transfer complete, setting a new property */
          wayland_selection_update_x11_property (data);

          if (bytes_read > 0)
            return;
        }

      g_clear_pointer (&selection->wayland_selection,
                       (GDestroyNotify) wayland_selection_data_free);
    }
}

static void
wayland_selection_data_read (CobiwmSelectionBridge *selection)
{
  WaylandSelectionData *data = selection->wayland_selection;

  g_input_stream_read_async (data->stream, data->buffer,
                             INCR_CHUNK_SIZE, G_PRIORITY_DEFAULT,
                             data->cancellable,
                             wayland_data_read_cb, selection);
}

static void
cobiwm_xwayland_selection_get_incr_chunk (CobiwmWaylandCompositor *compositor,
                                        CobiwmSelectionBridge   *selection)
{
  Display *xdisplay = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());
  gulong nitems_ret, bytes_after_ret;
  guchar *prop_ret;
  int format_ret;
  Atom type_ret;

  XGetWindowProperty (xdisplay,
                      selection->window,
                      gdk_x11_get_xatom_by_name ("_COBIWM_SELECTION"),
                      0, /* offset */
                      0x1fffffff, /* length */
                      False, /* delete */
                      AnyPropertyType,
                      &type_ret,
                      &format_ret,
                      &nitems_ret,
                      &bytes_after_ret,
                      &prop_ret);

  if (nitems_ret > 0)
    {
      x11_selection_data_write (selection, prop_ret, nitems_ret);
    }
  else
    {
      /* Transfer has completed */
      x11_selection_data_close (selection->x11_selection);
      x11_selection_data_finish (selection, TRUE);
    }

  XFree (prop_ret);
}

static void
cobiwm_x11_source_send (CobiwmWaylandDataSource *source,
                      const gchar           *mime_type,
                      gint                   fd)
{
  CobiwmWaylandDataSourceXWayland *source_xwayland =
    COBIWM_WAYLAND_DATA_SOURCE_XWAYLAND (source);
  CobiwmWaylandCompositor *compositor = cobiwm_wayland_compositor_get_default ();
  Display *xdisplay = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());
  CobiwmSelectionBridge *selection = source_xwayland->selection;
  Atom type_atom;

  if (source_xwayland->has_utf8_string_atom &&
      strcmp (mime_type, "text/plain;charset=utf-8") == 0)
    type_atom = gdk_x11_get_xatom_by_name ("UTF8_STRING");
  else
    type_atom = gdk_x11_get_xatom_by_name (mime_type);

  /* Ensure we close previous transactions */
  x11_selection_data_finish (selection, FALSE);

  /* Takes ownership of fd */
  selection->x11_selection =
    x11_selection_data_new (compositor->xwayland_manager.selection_data,
                            fd, mime_type);

  XConvertSelection (xdisplay,
                     selection->selection_atom, type_atom,
                     gdk_x11_get_xatom_by_name ("_COBIWM_SELECTION"),
                     selection->window,
                     CurrentTime);
  XFlush (xdisplay);
}

static void
cobiwm_x11_source_target (CobiwmWaylandDataSource *source,
                        const gchar           *mime_type)
{
  CobiwmWaylandCompositor *compositor = cobiwm_wayland_compositor_get_default ();
  CobiwmWaylandDataSourceXWayland *source_xwayland =
    COBIWM_WAYLAND_DATA_SOURCE_XWAYLAND (source);
  CobiwmSelectionBridge *selection = source_xwayland->selection;
  uint32_t action = 0;

  if (selection->selection_atom == xdnd_atoms[ATOM_DND_SELECTION])
    {
      if (mime_type)
        action = cobiwm_wayland_data_source_get_current_action (source);

      xdnd_send_status (compositor->xwayland_manager.selection_data,
                        selection->owner, action);
    }
}

static void
cobiwm_x11_source_cancel (CobiwmWaylandDataSource *source)
{
  CobiwmWaylandDataSourceXWayland *source_xwayland =
    COBIWM_WAYLAND_DATA_SOURCE_XWAYLAND (source);
  CobiwmSelectionBridge *selection = source_xwayland->selection;

  x11_selection_data_send_finished (selection, FALSE);
  g_clear_pointer (&selection->x11_selection,
                   (GDestroyNotify) x11_selection_data_free);
}

static void
cobiwm_x11_source_action (CobiwmWaylandDataSource *source,
                        uint32_t               action)
{
  CobiwmWaylandCompositor *compositor = cobiwm_wayland_compositor_get_default ();
  CobiwmWaylandDataSourceXWayland *source_xwayland =
    COBIWM_WAYLAND_DATA_SOURCE_XWAYLAND (source);
  CobiwmSelectionBridge *selection = source_xwayland->selection;

  if (selection->selection_atom == xdnd_atoms[ATOM_DND_SELECTION])
    {
      if (!cobiwm_wayland_data_source_has_target (source))
        action = 0;

      xdnd_send_status (compositor->xwayland_manager.selection_data,
                        selection->owner, action);
    }
}

static void
cobiwm_x11_source_drop_performed (CobiwmWaylandDataSource *source)
{
}

static void
cobiwm_x11_source_drag_finished (CobiwmWaylandDataSource *source)
{
  CobiwmWaylandDataSourceXWayland *source_xwayland =
    COBIWM_WAYLAND_DATA_SOURCE_XWAYLAND (source);
  CobiwmSelectionBridge *selection = source_xwayland->selection;

  if (selection->x11_selection)
    x11_selection_data_send_finished (selection, TRUE);
}

static void
cobiwm_wayland_data_source_xwayland_init (CobiwmWaylandDataSourceXWayland *source_xwayland)
{
}

static void
cobiwm_wayland_data_source_xwayland_class_init (CobiwmWaylandDataSourceXWaylandClass *klass)
{
  CobiwmWaylandDataSourceClass *data_source_class =
    COBIWM_WAYLAND_DATA_SOURCE_CLASS (klass);

  data_source_class->send = cobiwm_x11_source_send;
  data_source_class->target = cobiwm_x11_source_target;
  data_source_class->cancel = cobiwm_x11_source_cancel;
  data_source_class->action = cobiwm_x11_source_action;
  data_source_class->drop_performed = cobiwm_x11_source_drop_performed;
  data_source_class->drag_finished = cobiwm_x11_source_drag_finished;
}

static CobiwmWaylandDataSource *
cobiwm_wayland_data_source_xwayland_new (CobiwmSelectionBridge *selection)
{
  CobiwmWaylandDataSourceXWayland *source_xwayland;

  source_xwayland = g_object_new (COBIWM_TYPE_WAYLAND_DATA_SOURCE_XWAYLAND, NULL);
  source_xwayland->selection = selection;

  return COBIWM_WAYLAND_DATA_SOURCE (source_xwayland);
}

static void
cobiwm_x11_drag_dest_focus_in (CobiwmWaylandDataDevice *data_device,
                             CobiwmWaylandSurface    *surface,
                             CobiwmWaylandDataOffer  *offer)
{
  CobiwmWaylandCompositor *compositor = cobiwm_wayland_compositor_get_default ();

  compositor->xwayland_manager.selection_data->dnd.dnd_dest = surface->window->xwindow;
  xdnd_send_enter (compositor->xwayland_manager.selection_data,
                   compositor->xwayland_manager.selection_data->dnd.dnd_dest);
}

static void
cobiwm_x11_drag_dest_focus_out (CobiwmWaylandDataDevice *data_device,
                              CobiwmWaylandSurface    *surface)
{
  CobiwmWaylandCompositor *compositor = cobiwm_wayland_compositor_get_default ();

  xdnd_send_leave (compositor->xwayland_manager.selection_data,
                   compositor->xwayland_manager.selection_data->dnd.dnd_dest);
  compositor->xwayland_manager.selection_data->dnd.dnd_dest = None;
}

static void
cobiwm_x11_drag_dest_motion (CobiwmWaylandDataDevice *data_device,
                           CobiwmWaylandSurface    *surface,
                           const ClutterEvent    *event)
{
  CobiwmWaylandCompositor *compositor = cobiwm_wayland_compositor_get_default ();
  guint32 time;
  gfloat x, y;

  time = clutter_event_get_time (event);
  clutter_event_get_coords (event, &x, &y);
  xdnd_send_position (compositor->xwayland_manager.selection_data,
                      compositor->xwayland_manager.selection_data->dnd.dnd_dest,
                      time, x, y);
}

static void
cobiwm_x11_drag_dest_drop (CobiwmWaylandDataDevice *data_device,
                         CobiwmWaylandSurface    *surface)
{
  CobiwmWaylandCompositor *compositor = cobiwm_wayland_compositor_get_default ();

  xdnd_send_drop (compositor->xwayland_manager.selection_data,
                  compositor->xwayland_manager.selection_data->dnd.dnd_dest,
                  cobiwm_display_get_current_time_roundtrip (cobiwm_get_display ()));
}

static void
cobiwm_x11_drag_dest_update (CobiwmWaylandDataDevice *data_device,
                           CobiwmWaylandSurface    *surface)
{
  CobiwmWaylandCompositor *compositor = cobiwm_wayland_compositor_get_default ();
  CobiwmWaylandSeat *seat = compositor->seat;
  ClutterPoint pos;

  clutter_input_device_get_coords (seat->pointer.device, NULL, &pos);
  xdnd_send_position (compositor->xwayland_manager.selection_data,
                      compositor->xwayland_manager.selection_data->dnd.dnd_dest,
                      clutter_get_current_event_time (),
                      pos.x, pos.y);
}

static const CobiwmWaylandDragDestFuncs cobiwm_x11_drag_dest_funcs = {
  cobiwm_x11_drag_dest_focus_in,
  cobiwm_x11_drag_dest_focus_out,
  cobiwm_x11_drag_dest_motion,
  cobiwm_x11_drag_dest_drop,
  cobiwm_x11_drag_dest_update
};

const CobiwmWaylandDragDestFuncs *
cobiwm_xwayland_selection_get_drag_dest_funcs (void)
{
  return &cobiwm_x11_drag_dest_funcs;
}

static gboolean
cobiwm_xwayland_data_source_fetch_mimetype_list (CobiwmWaylandDataSource *source,
                                               Window                 window,
                                               Atom                   prop)
{
  CobiwmWaylandDataSourceXWayland *source_xwayland =
    COBIWM_WAYLAND_DATA_SOURCE_XWAYLAND (source);
  Display *xdisplay = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());
  gulong nitems_ret, bytes_after_ret, i;
  Atom *atoms, type_ret, utf8_string;
  int format_ret;
  struct wl_array *source_mime_types;

  source_mime_types = cobiwm_wayland_data_source_get_mime_types (source);
  if (source_mime_types->size != 0)
    return TRUE;

  utf8_string = gdk_x11_get_xatom_by_name ("UTF8_STRING");
  XGetWindowProperty (xdisplay, window, prop,
                      0, /* offset */
                      0x1fffffff, /* length */
                      True, /* delete */
                      AnyPropertyType,
                      &type_ret,
                      &format_ret,
                      &nitems_ret,
                      &bytes_after_ret,
                      (guchar **) &atoms);

  if (nitems_ret == 0 || type_ret != XA_ATOM)
    {
      XFree (atoms);
      return FALSE;
    }

  for (i = 0; i < nitems_ret; i++)
    {
      const gchar *mime_type;

      if (atoms[i] == utf8_string)
        {
          cobiwm_wayland_data_source_add_mime_type (source,
                                                  "text/plain;charset=utf-8");
          source_xwayland->has_utf8_string_atom = TRUE;
        }

      mime_type = gdk_x11_get_xatom_name (atoms[i]);
      cobiwm_wayland_data_source_add_mime_type (source, mime_type);
    }

  XFree (atoms);

  return TRUE;
}

static void
cobiwm_xwayland_selection_get_x11_targets (CobiwmWaylandCompositor *compositor,
                                         CobiwmSelectionBridge   *selection)
{
  CobiwmWaylandDataSource *data_source;

  data_source = cobiwm_wayland_data_source_xwayland_new (selection);

  if (cobiwm_xwayland_data_source_fetch_mimetype_list (data_source,
                                                     selection->window,
                                                     gdk_x11_get_xatom_by_name ("_COBIWM_SELECTION")))
    {
      g_clear_object (&selection->source);
      selection->source = data_source;

      if (selection->selection_atom == gdk_x11_get_xatom_by_name ("CLIPBOARD"))
        {
          cobiwm_wayland_data_device_set_selection (&compositor->seat->data_device, data_source,
                                                  wl_display_next_serial (compositor->wayland_display));
        }
      else if (selection->selection_atom == gdk_x11_get_xatom_by_name ("PRIMARY"))
        {
          cobiwm_wayland_data_device_set_primary (&compositor->seat->data_device, data_source,
                                                wl_display_next_serial (compositor->wayland_display));
        }
    }
  else
    g_object_unref (data_source);
}

static void
cobiwm_xwayland_selection_get_x11_data (CobiwmWaylandCompositor *compositor,
                                      CobiwmSelectionBridge   *selection)
{
  Display *xdisplay = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());
  gulong nitems_ret, bytes_after_ret;
  guchar *prop_ret;
  int format_ret;
  Atom type_ret;

  if (!selection->x11_selection)
    return;

  XGetWindowProperty (xdisplay,
                      selection->window,
                      gdk_x11_get_xatom_by_name ("_COBIWM_SELECTION"),
                      0, /* offset */
                      0x1fffffff, /* length */
                      True, /* delete */
                      AnyPropertyType,
                      &type_ret,
                      &format_ret,
                      &nitems_ret,
                      &bytes_after_ret,
                      &prop_ret);

  selection->x11_selection->incr = (type_ret == gdk_x11_get_xatom_by_name ("INCR"));

  if (selection->x11_selection->incr)
    return;

  if (type_ret == gdk_x11_get_xatom_by_name (selection->x11_selection->mime_type))
    x11_selection_data_write (selection, prop_ret, nitems_ret);

  XFree (prop_ret);
}

static gboolean
cobiwm_xwayland_selection_handle_selection_notify (CobiwmWaylandCompositor *compositor,
                                                 XEvent                *xevent)
{
  XSelectionEvent *event = (XSelectionEvent *) xevent;
  CobiwmSelectionBridge *selection;

  selection = atom_to_selection_bridge (compositor, event->selection);

  if (!selection)
    return FALSE;

  /* convert selection failed */
  if (event->property == None)
    {
      g_clear_pointer (&selection->x11_selection,
                       (GDestroyNotify) x11_selection_data_free);
      return FALSE;
    }

  if (event->target == gdk_x11_get_xatom_by_name ("TARGETS"))
    cobiwm_xwayland_selection_get_x11_targets (compositor, selection);
  else
    cobiwm_xwayland_selection_get_x11_data (compositor, selection);

  return TRUE;
}

static void
cobiwm_xwayland_selection_send_targets (CobiwmWaylandCompositor       *compositor,
                                      const CobiwmWaylandDataSource *data_source,
                                      Window                       requestor,
                                      Atom                         property)
{
  Atom *targets;
  gchar **p;
  int i = 0;
  struct wl_array *source_mime_types;

  if (!data_source)
    return;

  source_mime_types = cobiwm_wayland_data_source_get_mime_types (data_source);
  if (source_mime_types->size == 0)
    return;

  /* Make extra room for TIMESTAMP/TARGETS */
  targets = g_new (Atom, source_mime_types->size + 2);

  wl_array_for_each (p, source_mime_types)
    {
      targets[i++] = gdk_x11_get_xatom_by_name (*p);
    }

  targets[i++] = gdk_x11_get_xatom_by_name ("TIMESTAMP");
  targets[i++] = gdk_x11_get_xatom_by_name ("TARGETS");

  XChangeProperty (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()),
                   requestor, property,
                   XA_ATOM, 32, PropModeReplace,
                   (guchar *) targets, i);

  g_free (targets);
}

static void
cobiwm_xwayland_selection_send_timestamp (CobiwmWaylandCompositor *compositor,
                                        Window                 requestor,
                                        Atom                   property,
                                        Time                   timestamp)
{
  Display *xdisplay = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());

  XChangeProperty (xdisplay, requestor, property,
                   XA_INTEGER, 32,
                   PropModeReplace,
                   (guchar *) &timestamp, 1);
}

static void
cobiwm_xwayland_selection_send_incr_chunk (CobiwmWaylandCompositor *compositor,
                                         CobiwmSelectionBridge   *selection)
{
  if (!selection->wayland_selection)
    return;

  if (selection->wayland_selection->buffer_len > 0)
    wayland_selection_update_x11_property (selection->wayland_selection);
  else
    wayland_selection_data_read (selection);
}

static gboolean
handle_incr_chunk (CobiwmWaylandCompositor *compositor,
                   CobiwmSelectionBridge   *selection,
                   XPropertyEvent        *event)
{
  if (selection->x11_selection &&
      selection->x11_selection->incr &&
      event->window == selection->owner &&
      event->state == PropertyNewValue &&
      event->atom == gdk_x11_get_xatom_by_name ("_COBIWM_SELECTION"))
    {
      /* X11 to Wayland */
      cobiwm_xwayland_selection_get_incr_chunk (compositor, selection);
      return TRUE;
    }
  else if (selection->wayland_selection &&
           selection->wayland_selection->incr &&
           event->window == selection->window &&
           event->state == PropertyDelete &&
           event->atom == selection->wayland_selection->request_event.property)
    {
      /* Wayland to X11 */
      cobiwm_xwayland_selection_send_incr_chunk (compositor, selection);
      return TRUE;
    }

  return FALSE;
}

static gboolean
cobiwm_xwayland_selection_handle_property_notify (CobiwmWaylandCompositor *compositor,
                                                XEvent                *xevent)
{
  CobiwmXWaylandSelection *selection_data = compositor->xwayland_manager.selection_data;
  XPropertyEvent *event = (XPropertyEvent *) xevent;

  return handle_incr_chunk (compositor, &selection_data->clipboard, event);
}

static gboolean
cobiwm_xwayland_selection_handle_selection_request (CobiwmWaylandCompositor *compositor,
                                                  XEvent                *xevent)
{
  XSelectionRequestEvent *event = (XSelectionRequestEvent *) xevent;
  CobiwmWaylandDataSource *data_source;
  CobiwmSelectionBridge *selection;

  selection = atom_to_selection_bridge (compositor, event->selection);

  if (!selection)
    return FALSE;

  /* We must fetch from the currently active source, not the Xwayland one */
  data_source = data_device_get_active_source_for_atom (&compositor->seat->data_device,
                                                        selection->selection_atom);
  if (!data_source)
    return FALSE;

  g_clear_pointer (&selection->wayland_selection,
                   (GDestroyNotify) wayland_selection_data_free);

  if (event->target == gdk_x11_get_xatom_by_name ("TARGETS"))
    {
      cobiwm_xwayland_selection_send_targets (compositor,
                                            data_source,
                                            event->requestor,
                                            event->property);
      reply_selection_request (event, TRUE);
    }
  else if (event->target == gdk_x11_get_xatom_by_name ("TIMESTAMP"))
    {
      cobiwm_xwayland_selection_send_timestamp (compositor,
                                              event->requestor, event->property,
                                              selection->timestamp);
      reply_selection_request (event, TRUE);
    }
  else if (data_source && event->target == gdk_x11_get_xatom_by_name ("DELETE"))
    {
      reply_selection_request (event, TRUE);
    }
  else
    {
      if (data_source &&
          cobiwm_wayland_data_source_has_mime_type (data_source,
                                                  gdk_x11_get_xatom_name (event->target)))
        {
          selection->wayland_selection = wayland_selection_data_new (event,
                                                                     compositor);

          if (selection->wayland_selection)
            wayland_selection_data_read (selection);
        }

      if (!selection->wayland_selection)
        reply_selection_request (event, FALSE);
    }

  return TRUE;
}

static CobiwmWaylandSurface *
pick_drop_surface (CobiwmWaylandCompositor *compositor,
                   const ClutterEvent    *event)
{
  CobiwmDisplay *display = cobiwm_get_display ();
  CobiwmWindow *focus_window = NULL;
  ClutterPoint pos;

  clutter_event_get_coords (event, &pos.x, &pos.y);
  focus_window = cobiwm_stack_get_default_focus_window_at_point (display->screen->stack,
                                                               NULL, NULL,
                                                               pos.x, pos.y);
  return focus_window ? focus_window->surface : NULL;
}

static void
repick_drop_surface (CobiwmWaylandCompositor *compositor,
                     CobiwmWaylandDragGrab   *drag_grab,
                     const ClutterEvent    *event)
{
  Display *xdisplay = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());
  CobiwmDndBridge *dnd = &compositor->xwayland_manager.selection_data->dnd;
  CobiwmWaylandSurface *focus = NULL;

  focus = pick_drop_surface (compositor, event);
  dnd->focus_surface = focus;

  if (cobiwm_wayland_drag_grab_get_focus (drag_grab) == focus)
    return;

  if (focus &&
      focus->window->client_type == COBIWM_WINDOW_CLIENT_TYPE_WAYLAND)
    {
      XMapWindow (xdisplay, dnd->dnd_window);
      XMoveResizeWindow (xdisplay, dnd->dnd_window,
                         focus->window->rect.x,
                         focus->window->rect.y,
                         focus->window->rect.width,
                         focus->window->rect.height);
    }
  else
    {
      XMoveResizeWindow (xdisplay, dnd->dnd_window, -1, -1, 1, 1);
      XUnmapWindow (xdisplay, dnd->dnd_window);
    }
}

static void
drag_xgrab_focus (CobiwmWaylandPointerGrab *grab,
                  CobiwmWaylandSurface     *surface)
{
  /* Do not update the focus here. First, the surface may perfectly
   * be the X11 source DnD icon window's, so we can only be fooled
   * here. Second, delaying focus handling to XdndEnter/Leave
   * makes us do the negotiation orderly on the X11 side.
   */
}

static void
drag_xgrab_motion (CobiwmWaylandPointerGrab *grab,
                   const ClutterEvent     *event)
{
  CobiwmWaylandCompositor *compositor = cobiwm_wayland_compositor_get_default ();
  CobiwmDndBridge *dnd = &compositor->xwayland_manager.selection_data->dnd;
  CobiwmWaylandSeat *seat = compositor->seat;

  repick_drop_surface (compositor,
                       (CobiwmWaylandDragGrab *) grab,
                       event);

  dnd->last_motion_time = clutter_event_get_time (event);
  cobiwm_wayland_pointer_send_motion (&seat->pointer, event);
}

static void
drag_xgrab_button (CobiwmWaylandPointerGrab *grab,
                   const ClutterEvent     *event)
{
  CobiwmWaylandCompositor *compositor = cobiwm_wayland_compositor_get_default ();
  CobiwmWaylandSeat *seat = compositor->seat;

  cobiwm_wayland_pointer_send_button (&seat->pointer, event);
}

static const CobiwmWaylandPointerGrabInterface drag_xgrab_interface = {
  drag_xgrab_focus,
  drag_xgrab_motion,
  drag_xgrab_button,
};

static gboolean
cobiwm_xwayland_selection_handle_client_message (CobiwmWaylandCompositor *compositor,
                                               XEvent                *xevent)
{
  XClientMessageEvent *event = (XClientMessageEvent *) xevent;
  CobiwmDndBridge *dnd = &compositor->xwayland_manager.selection_data->dnd;
  CobiwmWaylandSeat *seat = compositor->seat;

  /* Source side messages */
  if (event->window == dnd->selection.window)
    {
      CobiwmWaylandDataSource *data_source;
      uint32_t action = 0;

      data_source = compositor->seat->data_device.dnd_data_source;

      if (!data_source)
        return FALSE;

      if (event->message_type == xdnd_atoms[ATOM_DND_STATUS])
        {
          /* The first bit in data.l[1] is set if the drag was accepted */
          cobiwm_wayland_data_source_set_has_target (data_source,
                                                   (event->data.l[1] & 1) != 0);

          /* data.l[4] contains the action atom */
          if (event->data.l[4])
            action = atom_to_action ((Atom) event->data.l[4]);

          cobiwm_wayland_data_source_set_current_action (data_source, action);
          return TRUE;
        }
      else if (event->message_type == xdnd_atoms[ATOM_DND_FINISHED])
        {
          /* Reject messages mid-grab */
          if (compositor->seat->data_device.current_grab)
            return FALSE;

          cobiwm_wayland_data_source_notify_finish (data_source);
          return TRUE;
        }
    }
  /* Dest side messages */
  else if (dnd->selection.source &&
           compositor->seat->data_device.current_grab &&
           (Window) event->data.l[0] == dnd->selection.owner)
    {
      CobiwmWaylandDragGrab *drag_grab = compositor->seat->data_device.current_grab;
      CobiwmWaylandSurface *drag_focus = cobiwm_wayland_drag_grab_get_focus (drag_grab);

      if (!drag_focus &&
          event->message_type != xdnd_atoms[ATOM_DND_ENTER])
        return FALSE;

      if (event->message_type == xdnd_atoms[ATOM_DND_ENTER])
        {
          /* Bit 1 in data.l[1] determines whether there's 3 or less mimetype
           * atoms (and are thus contained in this same message), or whether
           * there's more than 3 and we need to check the XdndTypeList property
           * for the full list.
           */
          if (!(event->data.l[1] & 1))
            {
              /* Mimetypes are contained in this message */
              const gchar *mimetype;
              gint i;
              struct wl_array *source_mime_types;

              /* We only need to fetch once */
              source_mime_types =
                cobiwm_wayland_data_source_get_mime_types (dnd->selection.source);
              if (source_mime_types->size == 0)
                {
                  for (i = 2; i <= 4; i++)
                    {
                      if (event->data.l[i] == None)
                        break;

                      mimetype = gdk_x11_get_xatom_name (event->data.l[i]);
                      cobiwm_wayland_data_source_add_mime_type (dnd->selection.source,
                                                              mimetype);
                    }
                }
            }
          else
            {
              /* Fetch mimetypes from type list */
              cobiwm_xwayland_data_source_fetch_mimetype_list (dnd->selection.source,
                                                             event->data.l[0],
                                                             xdnd_atoms[ATOM_DND_TYPE_LIST]);
            }

          cobiwm_wayland_drag_grab_set_focus (drag_grab, dnd->focus_surface);
          return TRUE;
        }
      else if (event->message_type == xdnd_atoms[ATOM_DND_POSITION])
        {
          ClutterEvent *motion;
          ClutterPoint pos;
          uint32_t action = 0;

          motion = clutter_event_new (CLUTTER_MOTION);
          clutter_input_device_get_coords (seat->pointer.device, NULL, &pos);
          clutter_event_set_coords (motion, pos.x, pos.y);
          clutter_event_set_device (motion, seat->pointer.device);
          clutter_event_set_source_device (motion, seat->pointer.device);
          clutter_event_set_time (motion, dnd->last_motion_time);

          action = atom_to_action ((Atom) event->data.l[4]);
          cobiwm_wayland_data_source_set_actions (dnd->selection.source, action);

          cobiwm_wayland_surface_drag_dest_motion (drag_focus, motion);
          xdnd_send_status (compositor->xwayland_manager.selection_data,
                            (Window) event->data.l[0],
                            cobiwm_wayland_data_source_get_current_action (dnd->selection.source));

          clutter_event_free (motion);
          return TRUE;
        }
      else if (event->message_type == xdnd_atoms[ATOM_DND_LEAVE])
        {
          cobiwm_wayland_drag_grab_set_focus (drag_grab, NULL);
          return TRUE;
        }
      else if (event->message_type == xdnd_atoms[ATOM_DND_DROP])
        {
          cobiwm_wayland_surface_drag_dest_drop (drag_focus);
          return TRUE;
        }
    }

  return FALSE;
}

static gboolean
cobiwm_xwayland_selection_handle_xfixes_selection_notify (CobiwmWaylandCompositor *compositor,
                                                        XEvent                *xevent)
{
  XFixesSelectionNotifyEvent *event = (XFixesSelectionNotifyEvent *) xevent;
  Display *xdisplay = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());
  CobiwmSelectionBridge *selection;

  selection = atom_to_selection_bridge (compositor, event->selection);

  if (!selection)
    return FALSE;

  if (selection->selection_atom == gdk_x11_get_xatom_by_name ("CLIPBOARD") ||
      selection->selection_atom == gdk_x11_get_xatom_by_name ("PRIMARY"))
    {
      if (event->owner == None)
        {
          if (selection->source && selection->owner != selection->window)
            {
              /* An X client went away, clear the selection */
              g_clear_object (&selection->source);
            }

          selection->owner = None;
        }
      else
        {
          selection->owner = event->owner;

          if (selection->owner == selection->window)
            {
              /* This our own selection window */
              selection->timestamp = event->timestamp;
              return TRUE;
            }

          g_clear_pointer (&selection->x11_selection,
                           (GDestroyNotify) x11_selection_data_free);

          XConvertSelection (xdisplay,
                             event->selection,
                             gdk_x11_get_xatom_by_name ("TARGETS"),
                             gdk_x11_get_xatom_by_name ("_COBIWM_SELECTION"),
                             selection->window,
                             CurrentTime);
          XFlush (xdisplay);
        }
    }
  else if (selection->selection_atom == xdnd_atoms[ATOM_DND_SELECTION])
    {
      CobiwmWaylandDataDevice *data_device = &compositor->seat->data_device;
      CobiwmXWaylandSelection *selection_data = compositor->xwayland_manager.selection_data;

      selection->owner = event->owner;

      if (event->owner != None && event->owner != selection->window)
        {
          CobiwmWaylandSurface *focus;

          focus = compositor->seat->pointer.focus_surface;
          selection->source = cobiwm_wayland_data_source_xwayland_new (selection);
          cobiwm_wayland_data_device_set_dnd_source (&compositor->seat->data_device,
                                                   selection->source);

          cobiwm_wayland_data_device_start_drag (data_device,
                                               wl_resource_get_client (focus->resource),
                                               &drag_xgrab_interface,
                                               focus, selection->source,
                                               NULL);
        }
      else if (event->owner == None)
        {
          cobiwm_wayland_data_device_end_drag (data_device);
          XUnmapWindow (xdisplay, selection_data->dnd.dnd_window);
        }
    }

  return TRUE;
}

gboolean
cobiwm_xwayland_selection_handle_event (XEvent *xevent)
{
  CobiwmWaylandCompositor *compositor;

  compositor = cobiwm_wayland_compositor_get_default ();

  if (!compositor->xwayland_manager.selection_data)
    return FALSE;

  switch (xevent->type)
    {
    case SelectionNotify:
      return cobiwm_xwayland_selection_handle_selection_notify (compositor, xevent);
    case PropertyNotify:
      return cobiwm_xwayland_selection_handle_property_notify (compositor, xevent);
    case SelectionRequest:
      return cobiwm_xwayland_selection_handle_selection_request (compositor, xevent);
    case ClientMessage:
      return cobiwm_xwayland_selection_handle_client_message (compositor, xevent);
    default:
      {
        CobiwmDisplay *display = cobiwm_get_display ();

        if (xevent->type - display->xfixes_event_base == XFixesSelectionNotify)
          return cobiwm_xwayland_selection_handle_xfixes_selection_notify (compositor, xevent);

        return FALSE;
      }
    }
}

static void
cobiwm_selection_bridge_ownership_notify (struct wl_listener *listener,
                                        void               *data)
{
  Display *xdisplay = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());
  CobiwmSelectionBridge *selection =
    wl_container_of (listener, selection, ownership_listener);
  CobiwmWaylandDataSource *owner = data;

  if (!owner && selection->window == selection->owner)
    {
      XSetSelectionOwner (xdisplay, selection->selection_atom,
                          None, selection->timestamp);
    }
  else if (owner && selection->source != owner)
    {
      XSetSelectionOwner (xdisplay,
                          selection->selection_atom,
                          selection->window,
                          CurrentTime);
    }
}

static void
init_selection_bridge (CobiwmSelectionBridge *selection,
                       Atom                 selection_atom,
                       struct wl_signal    *signal)
{
  Display *xdisplay = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());
  XSetWindowAttributes attributes;
  guint mask;

  attributes.event_mask = PropertyChangeMask;

  selection->ownership_listener.notify = cobiwm_selection_bridge_ownership_notify;
  wl_signal_add (signal, &selection->ownership_listener);

  selection->selection_atom = selection_atom;
  selection->window =
    XCreateWindow (xdisplay,
                   gdk_x11_window_get_xid (gdk_get_default_root_window ()),
                   -1, -1, 1, 1, /* position */
                   0, /* border width */
                   0, /* depth */
                   InputOnly, /* class */
                   CopyFromParent, /* visual */
                   CWEventMask,
                   &attributes);

  mask = XFixesSetSelectionOwnerNotifyMask |
    XFixesSelectionWindowDestroyNotifyMask |
    XFixesSelectionClientCloseNotifyMask;

  XFixesSelectSelectionInput (xdisplay, selection->window,
                              selection_atom, mask);
}

static void
shutdown_selection_bridge (CobiwmSelectionBridge *selection)
{
  wl_list_remove (&selection->ownership_listener.link);

  XDestroyWindow (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()),
                  selection->window);
  g_clear_pointer (&selection->wayland_selection,
                   (GDestroyNotify) wayland_selection_data_free);
  g_clear_pointer (&selection->x11_selection,
                   (GDestroyNotify) x11_selection_data_free);
}

void
cobiwm_xwayland_init_selection (void)
{
  CobiwmWaylandCompositor *compositor = cobiwm_wayland_compositor_get_default ();
  CobiwmXWaylandManager *manager = &compositor->xwayland_manager;

  g_assert (manager->selection_data == NULL);

  manager->selection_data = g_slice_new0 (CobiwmXWaylandSelection);

  cobiwm_xwayland_init_dnd (manager);
  init_selection_bridge (&manager->selection_data->clipboard,
                         gdk_x11_get_xatom_by_name ("CLIPBOARD"),
                         &compositor->seat->data_device.selection_ownership_signal);
  init_selection_bridge (&manager->selection_data->primary,
                         gdk_x11_get_xatom_by_name ("PRIMARY"),
                         &compositor->seat->data_device.primary_ownership_signal);
  init_selection_bridge (&manager->selection_data->dnd.selection,
                         xdnd_atoms[ATOM_DND_SELECTION],
                         &compositor->seat->data_device.dnd_ownership_signal);
}

void
cobiwm_xwayland_shutdown_selection (void)
{
  CobiwmWaylandCompositor *compositor = cobiwm_wayland_compositor_get_default ();
  CobiwmXWaylandManager *manager = &compositor->xwayland_manager;
  CobiwmXWaylandSelection *selection = manager->selection_data;

  g_assert (selection != NULL);

  g_clear_object (&selection->clipboard.source);

  cobiwm_xwayland_shutdown_dnd (manager);
  shutdown_selection_bridge (&selection->clipboard);
  shutdown_selection_bridge (&selection->primary);
  shutdown_selection_bridge (&selection->dnd.selection);

  g_slice_free (CobiwmXWaylandSelection, selection);
  manager->selection_data = NULL;
}
