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

#ifndef COBIWM_MONITOR_MANAGER_KMS_H
#define COBIWM_MONITOR_MANAGER_KMS_H

#include "cobiwm-monitor-manager-private.h"

#define COBIWM_TYPE_MONITOR_MANAGER_KMS            (cobiwm_monitor_manager_kms_get_type ())
#define COBIWM_MONITOR_MANAGER_KMS(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), COBIWM_TYPE_MONITOR_MANAGER_KMS, CobiwmMonitorManagerKms))
#define COBIWM_MONITOR_MANAGER_KMS_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  COBIWM_TYPE_MONITOR_MANAGER_KMS, CobiwmMonitorManagerKmsClass))
#define COBIWM_IS_MONITOR_MANAGER_KMS(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), COBIWM_TYPE_MONITOR_MANAGER_KMS))
#define COBIWM_IS_MONITOR_MANAGER_KMS_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  COBIWM_TYPE_MONITOR_MANAGER_KMS))
#define COBIWM_MONITOR_MANAGER_KMS_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  COBIWM_TYPE_MONITOR_MANAGER_KMS, CobiwmMonitorManagerKmsClass))

typedef struct _CobiwmMonitorManagerKmsClass    CobiwmMonitorManagerKmsClass;
typedef struct _CobiwmMonitorManagerKms         CobiwmMonitorManagerKms;

GType cobiwm_monitor_manager_kms_get_type (void);

#endif /* COBIWM_MONITOR_MANAGER_KMS_H */
