/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Cobiwm main() */

/*
 * Copyright (C) 2001 Havoc Pennington
 * Copyright (C) 2006 Elijah Newren
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
 * SECTION:main
 * @title: Main
 * @short_description: Program startup.
 *
 * Functions which parse the command-line arguments, create the display,
 * kick everything off and then close down Cobiwm when it's time to go.
 *
 *
 *
 * Cobiwm - a boring window manager for the adult in you
 *
 * Many window managers are like Marshmallow Froot Loops; Cobiwm
 * is like Frosted Flakes: it's still plain old corn, but dusted
 * with some sugar.
 *
 * The best way to get a handle on how the whole system fits together
 * is discussed in doc/code-overview.txt; if you're looking for functions
 * to investigate, read main(), cobiwm_display_open(), and event_callback().
 */

#define _GNU_SOURCE
#define _XOPEN_SOURCE /* for putenv() and some signal-related functions */

#include "config.h"
#include "main.h"
#include "util-private.h"
#include "display-private.h"
#include "errors.h"
#include "ui.h"
#include "prefs.h"
#include "compositor.h"
#include "cobiwm-backend.h"
#include "cobiwm-effect-manager.h"

#include <glib-object.h>
#include <glib-unix.h>

#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <time.h>
#include <unistd.h>
#include <glib.h>
#include <glib/gi18n-lib.h>

#include <clutter/clutter.h>

#ifdef HAVE_INTROSPECTION
#include <girepository.h>
#endif

#include "x11/session.h"

#ifdef HAVE_WAYLAND
#include "wayland/cobiwm-wayland.h"
# endif

#if defined(HAVE_NATIVE_BACKEND) && defined(HAVE_WAYLAND)
#include <systemd/sd-login.h>
#endif

/*
 * The exit code we'll return to our parent process when we eventually die.
 */
static CobiwmExitCode cobiwm_exit_code = COBIWM_EXIT_SUCCESS;

static const char *plugin = "default";

/*
 * Handle on the main loop, so that we have an easy way of shutting Cobiwm
 * down.
 */
static GMainLoop *cobiwm_main_loop = NULL;

static void prefs_changed_callback (CobiwmPreference pref,
                                    gpointer       data);

static gboolean print_version (const gchar    *option_name,
                               const gchar    *value,
                               gpointer        data,
                               GError        **error);

/**
 * cobiwm_print_compilation_info:
 *
 * Prints a list of which configure script options were used to
 * build this copy of Cobiwm. This is actually always called
 * on startup, but it's all no-op unless we're in verbose mode
 * (see cobiwm_set_verbose()).
 */
static void
cobiwm_print_compilation_info (void)
{
#ifdef HAVE_RANDR
  cobiwm_verbose ("Compiled with randr extension\n");
#else
  cobiwm_verbose ("Compiled without randr extension\n");
#endif
#ifdef HAVE_STARTUP_NOTIFICATION
  cobiwm_verbose ("Compiled with startup notification\n");
#else
  cobiwm_verbose ("Compiled without startup notification\n");
#endif
}

/**
 * cobiwm_print_self_identity:
 *
 * Prints the version number, the current timestamp (not the
 * build date), the locale, the character encoding, and a list
 * of configure script options that were used to build this
 * copy of Cobiwm. This is actually always called
 * on startup, but it's all no-op unless we're in verbose mode
 * (see cobiwm_set_verbose()).
 */
static void
cobiwm_print_self_identity (void)
{
  char buf[256];
  GDate d;
  const char *charset;

  /* Version and current date. */
  g_date_clear (&d, 1);
  g_date_set_time_t (&d, time (NULL));
  g_date_strftime (buf, sizeof (buf), "%x", &d);
  cobiwm_verbose ("Cobiwm version %s running on %s\n",
    VERSION, buf);

  /* Locale and encoding. */
  g_get_charset (&charset);
  cobiwm_verbose ("Running in locale \"%s\" with encoding \"%s\"\n",
    setlocale (LC_ALL, NULL), charset);

  /* Compilation settings. */
  cobiwm_print_compilation_info ();
}

/*
 * The set of possible options that can be set on Cobiwm's
 * command line.
 */
static gchar    *opt_save_file;
static gchar    *opt_display_name;
static gchar    *opt_client_id;
static gboolean  opt_replace_wm;
static gboolean  opt_disable_sm;
static gboolean  opt_sync;
#ifdef HAVE_WAYLAND
static gboolean  opt_wayland;
static gboolean  opt_nested;
#endif
#ifdef HAVE_NATIVE_BACKEND
static gboolean  opt_display_server;
#endif

static GOptionEntry cobiwm_options[] = {
  {
    "version", 0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK,
    print_version,
    N_("Print version"),
    NULL
  },
  {
    "cobiwm-plugin", 0, 0, G_OPTION_ARG_STRING,
    &plugin,
    N_("Cobiwm plugin to use"),
    "PLUGIN",
  },
  {
    "sm-disable", 0, 0, G_OPTION_ARG_NONE,
    &opt_disable_sm,
    N_("Disable connection to session manager"),
    NULL
  },
  {
    "replace", 'r', 0, G_OPTION_ARG_NONE,
    &opt_replace_wm,
    N_("Replace the running window manager"),
    NULL
  },
  {
    "sm-client-id", 0, 0, G_OPTION_ARG_STRING,
    &opt_client_id,
    N_("Specify session management ID"),
    "ID"
  },
  {
    "display", 'd', 0, G_OPTION_ARG_STRING,
    &opt_display_name, N_("X Display to use"),
    "DISPLAY"
  },
  {
    "sm-save-file", 0, 0, G_OPTION_ARG_FILENAME,
    &opt_save_file,
    N_("Initialize session from savefile"),
    "FILE"
  },
  {
    "sync", 0, 0, G_OPTION_ARG_NONE,
    &opt_sync,
    N_("Make X calls synchronous"),
    NULL
  },
#ifdef HAVE_WAYLAND
  {
    "wayland", 0, 0, G_OPTION_ARG_NONE,
    &opt_wayland,
    N_("Run as a wayland compositor"),
    NULL
  },
  {
    "nested", 0, 0, G_OPTION_ARG_NONE,
    &opt_nested,
    N_("Run as a nested compositor"),
    NULL
  },
#endif
#ifdef HAVE_NATIVE_BACKEND
  {
    "display-server", 0, 0, G_OPTION_ARG_NONE,
    &opt_display_server,
    N_("Run as a full display server, rather than nested")
  },
#endif
  {NULL}
};

/**
 * cobiwm_get_option_context: (skip)
 *
 * Returns a #GOptionContext initialized with cobiwm-related options.
 * Parse the command-line args with this before calling cobiwm_init().
 *
 * Return value: the #GOptionContext
 */
GOptionContext *
cobiwm_get_option_context (void)
{
  GOptionContext *ctx;

  if (setlocale (LC_ALL, "") == NULL)
    cobiwm_warning ("Locale not understood by C library, internationalization will not work\n");
  bindtextdomain (GETTEXT_PACKAGE, COBIWM_LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");

  ctx = g_option_context_new (NULL);
  g_option_context_add_main_entries (ctx, cobiwm_options, GETTEXT_PACKAGE);
  return ctx;
}

/**
 * cobiwm_select_display:
 *
 * Selects which display Cobiwm should use. It first tries to use
 * @display_name as the display. If @display_name is %NULL then
 * try to use the environment variable COBIWM_DISPLAY. If that
 * also is %NULL, use the default - :0.0
 */
static void
cobiwm_select_display (char *display_arg)
{
  const char *display_name;

  if (display_arg)
    display_name = (const char *) display_arg;
  else
    display_name = g_getenv ("COBIWM_DISPLAY");

  if (display_name)
    g_setenv ("DISPLAY", display_name, TRUE);
}

static void
cobiwm_finalize (void)
{
  CobiwmDisplay *display = cobiwm_get_display ();

  if (display)
    cobiwm_display_close (display,
                        CurrentTime); /* I doubt correct timestamps matter here */

#ifdef HAVE_WAYLAND
  if (cobiwm_is_wayland_compositor ())
    cobiwm_wayland_finalize ();
#endif
}

static gboolean
on_sigterm (gpointer user_data)
{
  cobiwm_quit (EXIT_SUCCESS);

  return G_SOURCE_REMOVE;
}

#if defined(HAVE_WAYLAND) && defined(HAVE_NATIVE_BACKEND)
static char *
find_logind_session_type (void)
{
  char **sessions;
  char *session_id;
  char *session_type;
  int ret, i;

  ret = sd_pid_get_session (0, &session_id);

  if (ret == 0 && session_id != NULL)
    {
      ret = sd_session_get_type (session_id, &session_type);
      free (session_id);

      if (ret < 0)
        session_type = NULL;

      goto out;
    }
  session_type = NULL;

  ret = sd_uid_get_sessions (getuid (), TRUE, &sessions);

  if (ret < 0 || sessions == NULL)
    goto out;

  for (i = 0; sessions[i] != NULL; i++)
    {
      ret = sd_session_get_type (sessions[i], &session_type);

      if (ret < 0)
        continue;

      if (g_strcmp0 (session_type, "x11") == 0||
          g_strcmp0 (session_type, "wayland") == 0)
        break;

      g_clear_pointer (&session_type, (GDestroyNotify) free);
    }

  for (i = 0; sessions[i] != NULL; i++)
    free (sessions[i]);
  free (sessions);

out:
  return session_type;
}

static gboolean
check_for_wayland_session_type (void)
{
  char *session_type = NULL;
  gboolean is_wayland = FALSE;

  session_type = find_logind_session_type ();

  if (session_type != NULL)
    {
      is_wayland = g_strcmp0 (session_type, "wayland") == 0;
      free (session_type);
    }

  return is_wayland;
}
#endif

static void
init_backend (void)
{
#ifdef HAVE_WAYLAND
  gboolean run_as_wayland_compositor = opt_wayland;

#ifdef HAVE_NATIVE_BACKEND
  if (opt_nested && opt_display_server)
    {
      cobiwm_warning ("Can't run both as nested and as a display server\n");
      cobiwm_exit (COBIWM_EXIT_ERROR);
    }

  if (!run_as_wayland_compositor)
    run_as_wayland_compositor = check_for_wayland_session_type ();

#ifdef CLUTTER_WINDOWING_EGL
  if (opt_display_server || (run_as_wayland_compositor && !opt_nested))
    clutter_set_windowing_backend (CLUTTER_WINDOWING_EGL);
  else
#endif
#endif
#endif
    clutter_set_windowing_backend (CLUTTER_WINDOWING_X11);

#ifdef HAVE_WAYLAND
  cobiwm_set_is_wayland_compositor (run_as_wayland_compositor);
#endif
}

/**
 * cobiwm_init: (skip)
 *
 * Initialize cobiwm. Call this after cobiwm_get_option_context() and before cobiwm_run().
 */
void
cobiwm_init (void)
{
  struct sigaction act;
  sigset_t empty_mask;

  sigemptyset (&empty_mask);
  act.sa_handler = SIG_IGN;
  act.sa_mask    = empty_mask;
  act.sa_flags   = 0;
  if (sigaction (SIGPIPE,  &act, NULL) < 0)
    g_printerr ("Failed to register SIGPIPE handler: %s\n",
                g_strerror (errno));
#ifdef SIGXFSZ
  if (sigaction (SIGXFSZ,  &act, NULL) < 0)
    g_printerr ("Failed to register SIGXFSZ handler: %s\n",
                g_strerror (errno));
#endif

  g_unix_signal_add (SIGTERM, on_sigterm, NULL);

  if (g_getenv ("COBIWM_VERBOSE"))
    cobiwm_set_verbose (TRUE);
  if (g_getenv ("COBIWM_DEBUG"))
    cobiwm_set_debugging (TRUE);

  init_backend ();

  if (g_get_home_dir ())
    if (chdir (g_get_home_dir ()) < 0)
      cobiwm_warning ("Could not change to home directory %s.\n",
                    g_get_home_dir ());

  cobiwm_print_self_identity ();

#ifdef HAVE_INTROSPECTION
  g_irepository_prepend_search_path (COBIWM_PKGLIBDIR);
#endif

#ifdef HAVE_WAYLAND
  if (cobiwm_is_wayland_compositor ())
    cobiwm_wayland_pre_clutter_init ();
#endif

  /* NB: When running as a hybrid wayland compositor we run our own headless X
   * server so the user can't control the X display to connect too. */
  if (!cobiwm_is_wayland_compositor ())
    cobiwm_select_display (opt_display_name);

  cobiwm_clutter_init ();

#ifdef HAVE_WAYLAND
  /* Bring up Wayland. This also launches Xwayland and sets DISPLAY as well... */
  if (cobiwm_is_wayland_compositor ())
    cobiwm_wayland_init ();
#endif

  cobiwm_set_syncing (opt_sync || (g_getenv ("COBIWM_SYNC") != NULL));

  if (opt_replace_wm)
    cobiwm_set_replace_current_wm (TRUE);

  if (opt_save_file && opt_client_id)
    cobiwm_fatal ("Can't specify both SM save file and SM client id\n");

  cobiwm_main_loop = g_main_loop_new (NULL, FALSE);

  cobiwm_ui_init ();

  cobiwm_restart_init ();
}

/**
 * cobiwm_register_with_session:
 *
 * Registers cobiwm with the session manager.  Call this after completing your own
 * initialization.
 *
 * This should be called when the session manager can safely continue to the
 * next phase of startup and potentially display windows.
 */
void
cobiwm_register_with_session (void)
{
  if (!opt_disable_sm)
    {
      if (opt_client_id == NULL)
        {
          const gchar *desktop_autostart_id;

          desktop_autostart_id = g_getenv ("DESKTOP_AUTOSTART_ID");

          if (desktop_autostart_id != NULL)
            opt_client_id = g_strdup (desktop_autostart_id);
        }

      /* Unset DESKTOP_AUTOSTART_ID in order to avoid child processes to
       * use the same client id. */
      g_unsetenv ("DESKTOP_AUTOSTART_ID");

      cobiwm_session_init (opt_client_id, opt_save_file);
    }
  /* Free memory possibly allocated by the argument parsing which are
   * no longer needed.
   */
  g_free (opt_save_file);
  g_free (opt_display_name);
  g_free (opt_client_id);
}

/**
 * cobiwm_run: (skip)
 *
 * Runs cobiwm. Call this after completing initialization that doesn't require
 * an event loop.
 *
 * Return value: cobiwm's exit status
 */
int
cobiwm_run (void)
{
  /* Load prefs */
  cobiwm_prefs_init ();
  cobiwm_prefs_add_listener (prefs_changed_callback, NULL);

  if (!cobiwm_display_open ())
    cobiwm_exit (COBIWM_EXIT_ERROR);

  g_main_loop_run (cobiwm_main_loop);

  cobiwm_finalize ();

  return cobiwm_exit_code;
}

/**
 * cobiwm_quit:
 * @code: The success or failure code to return to the calling process.
 *
 * Stops Cobiwm. This tells the event loop to stop processing; it is
 * rather dangerous to use this because this will leave the user with
 * no window manager. We generally do this only if, for example, the
 * session manager asks us to; we assume the session manager knows
 * what it's talking about.
 */
void
cobiwm_quit (CobiwmExitCode code)
{
  if (g_main_loop_is_running (cobiwm_main_loop))
    {
      cobiwm_exit_code = code;
      g_main_loop_quit (cobiwm_main_loop);
    }
}

/**
 * prefs_changed_callback:
 * @pref:  Which preference has changed
 * @data:  Arbitrary data (which we ignore)
 *
 * Called on pref changes. (One of several functions of its kind and purpose.)
 *
 * FIXME: Why are these particular prefs handled in main.c and not others?
 *        Should they be?
 */
static void
prefs_changed_callback (CobiwmPreference pref,
                        gpointer       data)
{
  switch (pref)
    {
    case COBIWM_PREF_DRAGGABLE_BORDER_WIDTH:
      cobiwm_display_retheme_all ();
      break;

    default:
      /* handled elsewhere or otherwise */
      break;
    }
}

static gboolean
print_version (const gchar    *option_name,
               const gchar    *value,
               gpointer        data,
               GError        **error)
{
  const int latest_year = 2011;

  g_print (_("cobiwm %s\n"
             "Copyright (C) 2001-%d Havoc Pennington, Red Hat, Inc., and others\n"
             "This is free software; see the source for copying conditions.\n"
             "There is NO warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n"),
           VERSION, latest_year);
  exit (0);
}

int
main (int argc, char **argv)
{
  GOptionContext *ctx;
  GError *error = NULL;

  ctx = cobiwm_get_option_context ();
  g_option_context_add_main_entries (ctx, cobiwm_options, GETTEXT_PACKAGE);
  if (!g_option_context_parse (ctx, &argc, &argv, &error))
    {
      g_printerr ("cobiwm: %s\n", error->message);
      exit (1);
    }
  g_option_context_free (ctx);

  cobiwm_init ();
  cobiwm_register_with_session ();
  return cobiwm_run ();
}
