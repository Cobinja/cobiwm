/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2014 Red Hat, Inc.
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

/*
 * SECTION:restart
 * @short_description: Smoothly restart the compositor
 *
 * There are some cases where we need to restart Cobiwm in order
 * to deal with changes in state - the particular case inspiring
 * this is enabling or disabling stereo output. To make this
 * fairly smooth for the user, we need to do two things:
 *
 *  - Display a message to the user and make sure that it is
 *    actually painted before we exit.
 *  - Use a helper program so that the Composite Overlay Window
 *    isn't unmapped and mapped.
 *
 * This handles both of these.
 */

#include <config.h>

#include <clutter/clutter.h>
#include <gio/gunixinputstream.h>

#include <main.h>
#include "ui.h"
#include "util-private.h"
#include "display-private.h"

static gboolean restart_helper_started = FALSE;
static gboolean restart_message_shown = FALSE;
static gboolean is_restart = FALSE;

void
cobiwm_restart_init (void)
{
  Display *xdisplay = cobiwm_ui_get_display ();
  Atom atom_restart_helper = XInternAtom (xdisplay, "_COBIWM_RESTART_HELPER", False);
  Window restart_helper_window = None;

  restart_helper_window = XGetSelectionOwner (xdisplay, atom_restart_helper);
  if (restart_helper_window)
    is_restart = TRUE;
}

static void
restart_check_ready (void)
{
  if (restart_helper_started && restart_message_shown)
    cobiwm_display_request_restart (cobiwm_get_display ());
}

static void
restart_helper_read_line_callback (GObject      *source_object,
                                   GAsyncResult *res,
                                   gpointer      user_data)
{
  GError *error = NULL;
  gsize length;
  char *line = g_data_input_stream_read_line_finish_utf8 (G_DATA_INPUT_STREAM (source_object),
                                                          res,
                                                          &length, &error);
  if (line == NULL)
    {
      cobiwm_warning ("Failed to read output from restart helper%s%s\n",
                    error ? ": " : NULL,
                    error ? error->message : NULL);
    }
  else
    g_free (line); /* We don't actually care what the restart helper outputs */

  g_object_unref (source_object);

  restart_helper_started = TRUE;
  restart_check_ready ();
}

static gboolean
restart_message_painted (gpointer data)
{
  restart_message_shown = TRUE;
  restart_check_ready ();

  return FALSE;
}

/**
 * cobiwm_restart:
 * @message: message to display to the user.
 *
 * Starts the process of restarting the compositor. Note that Cobiwm's
 * involvement here is to make the restart visually smooth for the
 * user - it cannot itself safely reexec a program that embeds libmuttter.
 * So in order for this to work, the compositor must handle two
 * signals -  CobiwmDisplay::show-restart-message, to display the
 * message passed here on the Clutter stage, and ::restart to actually
 * reexec the compositor.
 */
void
cobiwm_restart (const char *message)
{
  CobiwmDisplay *display = cobiwm_get_display();
  GInputStream *unix_stream;
  GDataInputStream *data_stream;
  GError *error = NULL;
  int helper_out_fd;

  static const char * const helper_argv[] = {
    COBIWM_LIBEXECDIR "/cobiwm-restart-helper", NULL
  };

  if (cobiwm_display_show_restart_message (display, message))
    {
      /* Wait until the stage was painted */
      clutter_threads_add_repaint_func_full (CLUTTER_REPAINT_FLAGS_POST_PAINT,
                                             restart_message_painted,
                                             NULL, NULL);
    }
  else
    {
      /* Can't show the message, show the message as soon as the
       * restart helper starts
       */
      restart_message_painted (NULL);
    }

  /* We also need to wait for the restart helper to get its
   * reference to the Composite Overlay Window.
   */
  if (!g_spawn_async_with_pipes (NULL, /* working directory */
                                 (char **)helper_argv,
                                 NULL, /* envp */
                                 G_SPAWN_DEFAULT,
                                 NULL, NULL, /* child_setup */
                                 NULL, /* child_pid */
                                 NULL, /* standard_input */
                                 &helper_out_fd,
                                 NULL, /* standard_error */
                                 &error))
    {
      cobiwm_warning ("Failed to start restart helper: %s\n", error->message);
      goto error;
    }

  unix_stream = g_unix_input_stream_new (helper_out_fd, TRUE);
  data_stream = g_data_input_stream_new (unix_stream);
  g_object_unref (unix_stream);

  g_data_input_stream_read_line_async (data_stream, G_PRIORITY_DEFAULT,
                                       NULL, restart_helper_read_line_callback,
                                       &error);
  if (error != NULL)
    {
      cobiwm_warning ("Failed to read from restart helper: %s\n", error->message);
      g_object_unref (data_stream);
      goto error;
    }

  return;

 error:
  /* If starting the restart helper fails, then we just go ahead and restart
   * immediately. We won't get a smooth transition, since the overlay window
   * will be destroyed and recreated, but otherwise it will work fine.
   */
  restart_helper_started = TRUE;
  restart_check_ready ();

  return;
}

void
cobiwm_restart_finish (void)
{
  if (is_restart)
    {
      Display *xdisplay = cobiwm_display_get_xdisplay (cobiwm_get_display ());
      Atom atom_restart_helper = XInternAtom (xdisplay, "_COBIWM_RESTART_HELPER", False);
      XSetSelectionOwner (xdisplay, atom_restart_helper, None, CurrentTime);
    }
}

/**
 * cobiwm_is_restart:
 *
 * Returns %TRUE if this instance of Cobiwm comes from Cobiwm
 * restarting itself (for example to enable/disable stereo.)
 * See cobiwm_restart(). If this is the case, any startup visuals
 * or animations should be suppressed.
 */
gboolean
cobiwm_is_restart (void)
{
  return is_restart;
}
