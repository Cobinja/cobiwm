/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2001 Havoc Pennington
 * Copyright (C) 2003 Rob Adams
 * Copyright (C) 2004-2006 Elijah Newren
 * Copyright (C) 2013 Red Hat Inc.
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

#ifndef COBIWM_MONITOR_CONFIG_H
#define COBIWM_MONITOR_CONFIG_H

#include "cobiwm-monitor-manager-private.h"

#define COBIWM_TYPE_MONITOR_CONFIG            (cobiwm_monitor_config_get_type ())
#define COBIWM_MONITOR_CONFIG(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), COBIWM_TYPE_MONITOR_CONFIG, CobiwmMonitorConfig))
#define COBIWM_MONITOR_CONFIG_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  COBIWM_TYPE_MONITOR_CONFIG, CobiwmMonitorConfigClass))
#define COBIWM_IS_MONITOR_CONFIG(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), COBIWM_TYPE_MONITOR_CONFIG))
#define COBIWM_IS_MONITOR_CONFIG_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  COBIWM_TYPE_MONITOR_CONFIG))
#define COBIWM_MONITOR_CONFIG_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  COBIWM_TYPE_MONITOR_CONFIG, CobiwmMonitorConfigClass))

GType cobiwm_monitor_config_get_type (void) G_GNUC_CONST;

CobiwmMonitorConfig *cobiwm_monitor_config_new (void);

gboolean           cobiwm_monitor_config_apply_stored (CobiwmMonitorConfig  *config,
                                                     CobiwmMonitorManager *manager);

void               cobiwm_monitor_config_make_default (CobiwmMonitorConfig  *config,
                                                     CobiwmMonitorManager *manager);

void               cobiwm_monitor_config_update_current (CobiwmMonitorConfig  *config,
                                                       CobiwmMonitorManager *manager);
void               cobiwm_monitor_config_make_persistent (CobiwmMonitorConfig *config);

void               cobiwm_monitor_config_restore_previous (CobiwmMonitorConfig  *config,
                                                         CobiwmMonitorManager *manager);

gboolean           cobiwm_monitor_config_get_is_builtin_display_on (CobiwmMonitorConfig *config);

#endif /* COBIWM_MONITOR_CONFIG_H */
