/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Cobiwm main */

/*
 * Copyright (C) 2001 Havoc Pennington
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

#ifndef COBIWM_MAIN_H
#define COBIWM_MAIN_H

#include <glib.h>

GOptionContext *cobiwm_get_option_context     (void);
void            cobiwm_init                   (void);
int             cobiwm_run                    (void);
void            cobiwm_register_with_session  (void);
gboolean        cobiwm_activate_session       (void);  /* Actually defined in cobiwm-backend.c */
gboolean        cobiwm_get_replace_current_wm (void);  /* Actually defined in util.c */

void            cobiwm_set_wm_name              (const char *wm_name);
void            cobiwm_set_gnome_wm_keybindings (const char *wm_keybindings);

void            cobiwm_restart                (const char *message);
gboolean        cobiwm_is_restart             (void);

/**
 * CobiwmExitCode:
 * @COBIWM_EXIT_SUCCESS: Success
 * @COBIWM_EXIT_ERROR: Error
 */
typedef enum
{
  COBIWM_EXIT_SUCCESS,
  COBIWM_EXIT_ERROR
} CobiwmExitCode;

/* exit immediately */
void cobiwm_exit (CobiwmExitCode code) G_GNUC_NORETURN;

/* g_main_loop_quit() then fall out of main() */
void cobiwm_quit (CobiwmExitCode code);

#endif
