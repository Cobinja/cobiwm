/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/**
 * \file screen-private.h  Handling of monitor configuration
 *
 * Managing multiple monitors
 * This file contains structures and functions that handle
 * multiple monitors, including reading the current configuration
 * and available hardware, and applying it.
 *
 * This interface is private to cobiwm, API users should look
 * at CobiwmScreen instead.
 */

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

#ifndef COBIWM_MONITOR_PRIVATE_H
#define COBIWM_MONITOR_PRIVATE_H

#include <cogl/cogl.h>
#include <libgnome-desktop/gnome-pnp-ids.h>

#include "display-private.h"
#include <screen.h>
#include "stack-tracker.h"
#include <cobiwm-monitor-manager.h>

#include "cobiwm-display-config-shared.h"
#include "cobiwm-dbus-display-config.h"
#include "cobiwm-cursor.h"

typedef struct _CobiwmMonitorConfigClass    CobiwmMonitorConfigClass;
typedef struct _CobiwmMonitorConfig         CobiwmMonitorConfig;

typedef struct _CobiwmCRTC CobiwmCRTC;
typedef struct _CobiwmOutput CobiwmOutput;
typedef struct _CobiwmMonitorMode CobiwmMonitorMode;
typedef struct _CobiwmMonitorInfo CobiwmMonitorInfo;
typedef struct _CobiwmCRTCInfo CobiwmCRTCInfo;
typedef struct _CobiwmOutputInfo CobiwmOutputInfo;
typedef struct _CobiwmTileInfo CobiwmTileInfo;

typedef enum {
  COBIWM_MONITOR_TRANSFORM_NORMAL,
  COBIWM_MONITOR_TRANSFORM_90,
  COBIWM_MONITOR_TRANSFORM_180,
  COBIWM_MONITOR_TRANSFORM_270,
  COBIWM_MONITOR_TRANSFORM_FLIPPED,
  COBIWM_MONITOR_TRANSFORM_FLIPPED_90,
  COBIWM_MONITOR_TRANSFORM_FLIPPED_180,
  COBIWM_MONITOR_TRANSFORM_FLIPPED_270,
} CobiwmMonitorTransform;

/* This matches the values in drm_mode.h */
typedef enum {
  COBIWM_CONNECTOR_TYPE_Unknown = 0,
  COBIWM_CONNECTOR_TYPE_VGA = 1,
  COBIWM_CONNECTOR_TYPE_DVII = 2,
  COBIWM_CONNECTOR_TYPE_DVID = 3,
  COBIWM_CONNECTOR_TYPE_DVIA = 4,
  COBIWM_CONNECTOR_TYPE_Composite = 5,
  COBIWM_CONNECTOR_TYPE_SVIDEO = 6,
  COBIWM_CONNECTOR_TYPE_LVDS = 7,
  COBIWM_CONNECTOR_TYPE_Component = 8,
  COBIWM_CONNECTOR_TYPE_9PinDIN = 9,
  COBIWM_CONNECTOR_TYPE_DisplayPort = 10,
  COBIWM_CONNECTOR_TYPE_HDMIA = 11,
  COBIWM_CONNECTOR_TYPE_HDMIB = 12,
  COBIWM_CONNECTOR_TYPE_TV = 13,
  COBIWM_CONNECTOR_TYPE_eDP = 14,
  COBIWM_CONNECTOR_TYPE_VIRTUAL = 15,
  COBIWM_CONNECTOR_TYPE_DSI = 16,
} CobiwmConnectorType;

struct _CobiwmTileInfo {
  guint32 group_id;
  guint32 flags;
  guint32 max_h_tiles;
  guint32 max_v_tiles;
  guint32 loc_h_tile;
  guint32 loc_v_tile;
  guint32 tile_w;
  guint32 tile_h;
};

struct _CobiwmOutput
{
  /* The CRTC driving this output, NULL if the output is not enabled */
  CobiwmCRTC *crtc;
  /* The low-level ID of this output, used to apply back configuration */
  glong winsys_id;
  char *name;
  char *vendor;
  char *product;
  char *serial;
  int width_mm;
  int height_mm;
  CoglSubpixelOrder subpixel_order;
  int scale;

  CobiwmConnectorType connector_type;

  CobiwmMonitorMode *preferred_mode;
  CobiwmMonitorMode **modes;
  unsigned int n_modes;

  CobiwmCRTC **possible_crtcs;
  unsigned int n_possible_crtcs;

  CobiwmOutput **possible_clones;
  unsigned int n_possible_clones;

  int backlight;
  int backlight_min;
  int backlight_max;

  /* Used when changing configuration */
  gboolean is_dirty;

  /* The low-level bits used to build the high-level info
     in CobiwmMonitorInfo

     XXX: flags maybe?
     There is a lot of code that uses MonitorInfo->is_primary,
     but nobody uses CobiwmOutput yet
  */
  gboolean is_primary;
  gboolean is_presentation;
  gboolean is_underscanning;
  gboolean supports_underscanning;

  gpointer driver_private;
  GDestroyNotify driver_notify;

  /* get a new preferred mode on hotplug events, to handle dynamic guest resizing */
  gboolean hotplug_mode_update;
  gint suggested_x;
  gint suggested_y;

  CobiwmTileInfo tile_info;
};

struct _CobiwmCRTC
{
  glong crtc_id;
  CobiwmRectangle rect;
  CobiwmMonitorMode *current_mode;
  CobiwmMonitorTransform transform;
  unsigned int all_transforms;

  /* Only used to build the logical configuration
     from the HW one
  */
  CobiwmMonitorInfo *logical_monitor;

  /* Used when changing configuration */
  gboolean is_dirty;

  /* Used by cursor renderer backend */
  void *cursor_renderer_private;

  gpointer driver_private;
  GDestroyNotify driver_notify;
};

struct _CobiwmMonitorMode
{
  /* The low-level ID of this mode, used to apply back configuration */
  glong mode_id;
  char *name;

  int width;
  int height;
  float refresh_rate;

  gpointer driver_private;
  GDestroyNotify driver_notify;
};

#define COBIWM_MAX_OUTPUTS_PER_MONITOR 4
/**
 * CobiwmMonitorInfo:
 *
 * A structure with high-level information about monitors.
 * This corresponds to a subset of the compositor coordinate space.
 * Clones are only reported once, irrespective of the way
 * they're implemented (two CRTCs configured for the same
 * coordinates or one CRTCs driving two outputs). Inactive CRTCs
 * are ignored, and so are disabled outputs.
 */
struct _CobiwmMonitorInfo
{
  int number;
  int xinerama_index;
  CobiwmRectangle rect;
  /* for tiled monitors these are calculated, from untiled just copied */
  float refresh_rate;
  int width_mm;
  int height_mm;
  gboolean is_primary;
  gboolean is_presentation; /* XXX: not yet used */
  gboolean in_fullscreen;
  int scale;

  /* The primary or first output for this monitor, 0 if we can't figure out.
     It can be matched to a winsys_id of a CobiwmOutput.

     This is used as an opaque token on reconfiguration when switching from
     clone to extened, to decide on what output the windows should go next
     (it's an attempt to keep windows on the same monitor, and preferably on
     the primary one).
  */
  glong winsys_id;

  guint32 tile_group_id;

  int monitor_winsys_xid;
  int n_outputs;
  CobiwmOutput *outputs[COBIWM_MAX_OUTPUTS_PER_MONITOR];
};

/*
 * CobiwmCRTCInfo:
 * This represents the writable part of a CRTC, as deserialized from DBus
 * or built by CobiwmMonitorConfig
 *
 * Note: differently from the other structures in this file, CobiwmCRTCInfo
 * is handled by pointer. This is to accomodate the usage in CobiwmMonitorConfig
 */
struct _CobiwmCRTCInfo {
  CobiwmCRTC                 *crtc;
  CobiwmMonitorMode          *mode;
  int                       x;
  int                       y;
  CobiwmMonitorTransform      transform;
  GPtrArray                *outputs;
};

/*
 * CobiwmOutputInfo:
 * this is the same as CobiwmCRTCInfo, but for outputs
 */
struct _CobiwmOutputInfo {
  CobiwmOutput  *output;
  gboolean     is_primary;
  gboolean     is_presentation;
  gboolean     is_underscanning;
};

#define COBIWM_TYPE_MONITOR_MANAGER            (cobiwm_monitor_manager_get_type ())
#define COBIWM_MONITOR_MANAGER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), COBIWM_TYPE_MONITOR_MANAGER, CobiwmMonitorManager))
#define COBIWM_MONITOR_MANAGER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  COBIWM_TYPE_MONITOR_MANAGER, CobiwmMonitorManagerClass))
#define COBIWM_IS_MONITOR_MANAGER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), COBIWM_TYPE_MONITOR_MANAGER))
#define COBIWM_IS_MONITOR_MANAGER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  COBIWM_TYPE_MONITOR_MANAGER))
#define COBIWM_MONITOR_MANAGER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  COBIWM_TYPE_MONITOR_MANAGER, CobiwmMonitorManagerClass))

struct _CobiwmMonitorManager
{
  CobiwmDBusDisplayConfigSkeleton parent_instance;

  /* XXX: this structure is very badly
     packed, but I like the logical organization
     of fields */

  gboolean in_init;
  unsigned int serial;

  CobiwmPowerSave power_save_mode;

  int max_screen_width;
  int max_screen_height;
  int screen_width;
  int screen_height;

  /* Outputs refer to physical screens,
     CRTCs refer to stuff that can drive outputs
     (like encoders, but less tied to the HW),
     while monitor_infos refer to logical ones.
  */
  CobiwmOutput *outputs;
  unsigned int n_outputs;

  CobiwmMonitorMode *modes;
  unsigned int n_modes;

  CobiwmCRTC *crtcs;
  unsigned int n_crtcs;

  CobiwmMonitorInfo *monitor_infos;
  unsigned int n_monitor_infos;
  int primary_monitor_index;

  int dbus_name_id;

  int persistent_timeout_id;
  CobiwmMonitorConfig *config;

  GnomePnpIds *pnp_ids;
};

struct _CobiwmMonitorManagerClass
{
  CobiwmDBusDisplayConfigSkeletonClass parent_class;

  void (*read_current) (CobiwmMonitorManager *);

  char* (*get_edid_file) (CobiwmMonitorManager *,
                          CobiwmOutput         *);
  GBytes* (*read_edid) (CobiwmMonitorManager *,
                        CobiwmOutput         *);

  void (*apply_configuration) (CobiwmMonitorManager  *,
                               CobiwmCRTCInfo       **,
                               unsigned int         ,
                               CobiwmOutputInfo     **,
                               unsigned int);

  void (*set_power_save_mode) (CobiwmMonitorManager *,
                               CobiwmPowerSave);

  void (*change_backlight) (CobiwmMonitorManager *,
                            CobiwmOutput         *,
                            int);

  void (*get_crtc_gamma) (CobiwmMonitorManager  *,
                          CobiwmCRTC            *,
                          gsize               *,
                          unsigned short     **,
                          unsigned short     **,
                          unsigned short     **);
  void (*set_crtc_gamma) (CobiwmMonitorManager *,
                          CobiwmCRTC           *,
                          gsize               ,
                          unsigned short     *,
                          unsigned short     *,
                          unsigned short     *);

  void (*add_monitor) (CobiwmMonitorManager *,
                       CobiwmMonitorInfo *);

  void (*delete_monitor) (CobiwmMonitorManager *,
                          int monitor_winsys_xid);

};

void                cobiwm_monitor_manager_rebuild_derived   (CobiwmMonitorManager *manager);

CobiwmMonitorInfo    *cobiwm_monitor_manager_get_monitor_infos (CobiwmMonitorManager *manager,
							    unsigned int       *n_infos);

CobiwmOutput         *cobiwm_monitor_manager_get_outputs       (CobiwmMonitorManager *manager,
							    unsigned int       *n_outputs);

void                cobiwm_monitor_manager_get_resources     (CobiwmMonitorManager  *manager,
                                                            CobiwmMonitorMode    **modes,
                                                            unsigned int        *n_modes,
                                                            CobiwmCRTC           **crtcs,
                                                            unsigned int        *n_crtcs,
                                                            CobiwmOutput         **outputs,
                                                            unsigned int        *n_outputs);

int                 cobiwm_monitor_manager_get_primary_index (CobiwmMonitorManager *manager);

void                cobiwm_monitor_manager_get_screen_size   (CobiwmMonitorManager *manager,
                                                            int                *width,
                                                            int                *height);

void                cobiwm_monitor_manager_get_screen_limits (CobiwmMonitorManager *manager,
                                                            int                *width,
                                                            int                *height);

void                cobiwm_monitor_manager_apply_configuration (CobiwmMonitorManager  *manager,
                                                              CobiwmCRTCInfo       **crtcs,
                                                              unsigned int         n_crtcs,
                                                              CobiwmOutputInfo     **outputs,
                                                              unsigned int         n_outputs);

void                cobiwm_monitor_manager_confirm_configuration (CobiwmMonitorManager *manager,
                                                                gboolean            ok);

void               cobiwm_output_parse_edid (CobiwmOutput *output,
                                           GBytes     *edid);
gboolean           cobiwm_output_is_laptop  (CobiwmOutput *output);

void               cobiwm_crtc_info_free   (CobiwmCRTCInfo   *info);
void               cobiwm_output_info_free (CobiwmOutputInfo *info);

gboolean           cobiwm_monitor_manager_has_hotplug_mode_update (CobiwmMonitorManager *manager);
void               cobiwm_monitor_manager_read_current_config (CobiwmMonitorManager *manager);
void               cobiwm_monitor_manager_on_hotplug (CobiwmMonitorManager *manager);

gboolean           cobiwm_monitor_manager_get_monitor_matrix (CobiwmMonitorManager *manager,
                                                            CobiwmOutput         *output,
                                                            gfloat              matrix[6]);

gint               cobiwm_monitor_manager_get_monitor_at_point (CobiwmMonitorManager *manager,
                                                              gfloat              x,
                                                              gfloat              y);

void cobiwm_monitor_manager_clear_output (CobiwmOutput *output);
void cobiwm_monitor_manager_clear_mode (CobiwmMonitorMode *mode);
void cobiwm_monitor_manager_clear_crtc (CobiwmCRTC *crtc);

/* Returns true if transform causes width and height to be inverted
   This is true for the odd transforms in the enum */
static inline gboolean
cobiwm_monitor_transform_is_rotated (CobiwmMonitorTransform transform)
{
  return (transform % 2);
}

#endif
