/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2001, 2002 Havoc Pennington
 * Copyright (C) 2002, 2003 Red Hat Inc.
 * Some ICCCM manager selection code derived from fvwm2,
 * Copyright (C) 2001 Dominik Vogt, Matthias Clasen, and fvwm2 team
 * Copyright (C) 2003 Rob Adams
 * Copyright (C) 2004-2006 Elijah Newren
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

#ifndef COBIWM_STARTUP_NOTIFICATION_PRIVATE_H
#define COBIWM_STARTUP_NOTIFICATION_PRIVATE_H

#include "display-private.h"

#define COBIWM_TYPE_STARTUP_NOTIFICATION (cobiwm_startup_notification_get_type ())

G_DECLARE_FINAL_TYPE (CobiwmStartupNotification,
                      cobiwm_startup_notification,
                      COBIWM, STARTUP_NOTIFICATION,
                      GObject)

CobiwmStartupNotification *
         cobiwm_startup_notification_get             (CobiwmDisplay             *display);

gboolean cobiwm_startup_notification_handle_xevent   (CobiwmStartupNotification *sn,
                                                    XEvent                  *xevent);

void     cobiwm_startup_notification_remove_sequence (CobiwmStartupNotification *sn,
                                                    const gchar             *id);

GSList * cobiwm_startup_notification_get_sequences   (CobiwmStartupNotification *sn);

#endif /* COBIWM_STARTUP_NOTIFICATION_PRIVATE_H */
