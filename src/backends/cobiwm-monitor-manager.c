/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2001, 2002 Havoc Pennington
 * Copyright (C) 2002, 2003 Red Hat Inc.
 * Some ICCCM manager selection code derived from fvwm2,
 * Copyright (C) 2001 Dominik Vogt, Matthias Clasen, and fvwm2 team
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

#include "config.h"

#include "cobiwm-monitor-manager-private.h"

#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <clutter/clutter.h>

#include <main.h>
#include "util-private.h"
#include <errors.h>
#include "edid.h"
#include "cobiwm-monitor-config.h"
#include "backends/x11/cobiwm-monitor-manager-xrandr.h"
#include "cobiwm-backend-private.h"

enum {
  CONFIRM_DISPLAY_CHANGE,
  SIGNALS_LAST
};

/* Array index matches CobiwmMonitorTransform */
static gfloat transform_matrices[][6] = {
  {  1,  0,  0,  0,  1,  0 }, /* normal */
  {  0, -1,  1,  1,  0,  0 }, /* 90° */
  { -1,  0,  1,  0, -1,  1 }, /* 180° */
  {  0,  1,  0, -1,  0,  1 }, /* 270° */
  { -1,  0,  1,  0,  1,  0 }, /* normal flipped */
  {  0,  1,  0,  1,  0,  0 }, /* 90° flipped */
  {  1,  0,  0,  0, -1,  1 }, /* 180° flipped */
  {  0, -1,  1, -1,  0,  1 }, /* 270° flipped */
};

static int signals[SIGNALS_LAST];

static void cobiwm_monitor_manager_display_config_init (CobiwmDBusDisplayConfigIface *iface);

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (CobiwmMonitorManager, cobiwm_monitor_manager, COBIWM_DBUS_TYPE_DISPLAY_CONFIG_SKELETON,
                                  G_IMPLEMENT_INTERFACE (COBIWM_DBUS_TYPE_DISPLAY_CONFIG, cobiwm_monitor_manager_display_config_init));

static void initialize_dbus_interface (CobiwmMonitorManager *manager);

static void
cobiwm_monitor_manager_init (CobiwmMonitorManager *manager)
{
}

/*
 * rules for constructing a tiled monitor
 * 1. find a tile_group_id
 * 2. iterate over all outputs for that tile group id
 * 3. see if output has a crtc and if it is configured for the tile size
 * 4. calculate the total tile size
 * 5. set tile finished size
 * 6. check for more tile_group_id
*/
static void
construct_tile_monitor (CobiwmMonitorManager *manager,
                        GArray *monitor_infos,
                        guint32 tile_group_id)
{
  CobiwmMonitorInfo info;
  unsigned i;

  for (i = 0; i < monitor_infos->len; i++)
    {
      CobiwmMonitorInfo *pinfo = &g_array_index (monitor_infos, CobiwmMonitorInfo, i);

      if (pinfo->tile_group_id == tile_group_id)
        return;
    }

  /* didn't find it */
  info.number = monitor_infos->len;
  info.tile_group_id = tile_group_id;
  info.is_presentation = FALSE;
  info.refresh_rate = 0.0;
  info.width_mm = 0;
  info.height_mm = 0;
  info.is_primary = FALSE;
  info.rect.x = INT_MAX;
  info.rect.y = INT_MAX;
  info.rect.width = 0;
  info.rect.height = 0;
  info.winsys_id = 0;
  info.n_outputs = 0;
  info.monitor_winsys_xid = 0;

  for (i = 0; i < manager->n_outputs; i++)
    {
      CobiwmOutput *output = &manager->outputs[i];

      if (!output->tile_info.group_id)
        continue;

      if (output->tile_info.group_id != tile_group_id)
        continue;

      if (!output->crtc)
        continue;

      if (output->crtc->rect.width != (int)output->tile_info.tile_w ||
          output->crtc->rect.height != (int)output->tile_info.tile_h)
        continue;

      if (output->tile_info.loc_h_tile == 0 && output->tile_info.loc_v_tile == 0)
        {
          info.refresh_rate = output->crtc->current_mode->refresh_rate;
          info.width_mm = output->width_mm;
          info.height_mm = output->height_mm;
          info.winsys_id = output->winsys_id;
        }

      /* hack */
      if (output->crtc->rect.x < info.rect.x)
        info.rect.x = output->crtc->rect.x;
      if (output->crtc->rect.y < info.rect.y)
        info.rect.y = output->crtc->rect.y;

      if (output->tile_info.loc_h_tile == 0)
        info.rect.height += output->tile_info.tile_h;

      if (output->tile_info.loc_v_tile == 0)
        info.rect.width += output->tile_info.tile_w;

      if (info.n_outputs > COBIWM_MAX_OUTPUTS_PER_MONITOR)
        continue;

      info.outputs[info.n_outputs++] = output;
    }

  /* if we don't have a winsys id, i.e. we haven't found tile 0,0
     don't try and add this to the monitor infos */
  if (!info.winsys_id)
    return;

  g_array_append_val (monitor_infos, info);
}

/*
 * make_logical_config:
 *
 * Turn outputs and CRTCs into logical CobiwmMonitorInfo,
 * that will be used by the core and API layer (CobiwmScreen
 * and friends)
 */
static void
make_logical_config (CobiwmMonitorManager *manager)
{
  CobiwmMonitorManagerClass *manager_class = COBIWM_MONITOR_MANAGER_GET_CLASS (manager);
  GArray *monitor_infos;
  unsigned int i, j;

  monitor_infos = g_array_sized_new (FALSE, TRUE, sizeof (CobiwmMonitorInfo),
                                     manager->n_crtcs);

  /* Walk the list of CobiwmCRTCs, and build a CobiwmMonitorInfo
     for each of them, unless they reference a rectangle that
     is already there.
  */
  /* for tiling we need to work out how many tiled outputs there are */
  for (i = 0; i < manager->n_outputs; i++)
    {
      CobiwmOutput *output = &manager->outputs[i];

      if (output->tile_info.group_id)
        construct_tile_monitor (manager, monitor_infos, output->tile_info.group_id);
    }

  for (i = 0; i < manager->n_crtcs; i++)
    {
      CobiwmCRTC *crtc = &manager->crtcs[i];

      /* Ignore CRTCs not in use */
      if (crtc->current_mode == NULL)
        continue;

      for (j = 0; j < monitor_infos->len; j++)
        {
          CobiwmMonitorInfo *info = &g_array_index (monitor_infos, CobiwmMonitorInfo, j);
          if (cobiwm_rectangle_contains_rect (&info->rect,
                                            &crtc->rect))
            {
              crtc->logical_monitor = info;
              break;
            }
        }

      if (crtc->logical_monitor == NULL)
        {
          CobiwmMonitorInfo info;

          info.number = monitor_infos->len;
          info.tile_group_id = 0;
          info.rect = crtc->rect;
          info.refresh_rate = crtc->current_mode->refresh_rate;
          info.scale = 1;
          info.is_primary = FALSE;
          /* This starts true because we want
             is_presentation only if all outputs are
             marked as such (while for primary it's enough
             that any is marked)
          */
          info.is_presentation = TRUE;
          info.in_fullscreen = -1;
          info.winsys_id = 0;
          info.n_outputs = 0;
          info.monitor_winsys_xid = 0;
          g_array_append_val (monitor_infos, info);

          crtc->logical_monitor = &g_array_index (monitor_infos, CobiwmMonitorInfo,
                                                  info.number);
        }
    }

  /* Now walk the list of outputs applying extended properties (primary
     and presentation)
  */
  for (i = 0; i < manager->n_outputs; i++)
    {
      CobiwmOutput *output;
      CobiwmMonitorInfo *info;

      output = &manager->outputs[i];

      /* Ignore outputs that are not active */
      if (output->crtc == NULL)
        continue;

      if (output->tile_info.group_id)
        continue;

      /* We must have a logical monitor on every CRTC at this point */
      g_assert (output->crtc->logical_monitor != NULL);

      info = output->crtc->logical_monitor;

      info->is_primary = info->is_primary || output->is_primary;
      info->is_presentation = info->is_presentation && output->is_presentation;

      info->width_mm = output->width_mm;
      info->height_mm = output->height_mm;

      info->outputs[0] = output;
      info->n_outputs = 1;

      if (output->is_primary || info->winsys_id == 0)
        {
          info->scale = output->scale;
          info->winsys_id = output->winsys_id;
        }

      if (info->is_primary)
        manager->primary_monitor_index = info->number;
    }

  manager->n_monitor_infos = monitor_infos->len;
  manager->monitor_infos = (void*)g_array_free (monitor_infos, FALSE);

  if (manager_class->add_monitor)
    for (i = 0; i < manager->n_monitor_infos; i++)
      manager_class->add_monitor (manager, &manager->monitor_infos[i]);
}

static void
power_save_mode_changed (CobiwmMonitorManager *manager,
                         GParamSpec         *pspec,
                         gpointer            user_data)
{
  CobiwmMonitorManagerClass *klass;
  int mode = cobiwm_dbus_display_config_get_power_save_mode (COBIWM_DBUS_DISPLAY_CONFIG (manager));

  if (mode == COBIWM_POWER_SAVE_UNSUPPORTED)
    return;

  /* If DPMS is unsupported, force the property back. */
  if (manager->power_save_mode == COBIWM_POWER_SAVE_UNSUPPORTED)
    {
      cobiwm_dbus_display_config_set_power_save_mode (COBIWM_DBUS_DISPLAY_CONFIG (manager), COBIWM_POWER_SAVE_UNSUPPORTED);
      return;
    }

  klass = COBIWM_MONITOR_MANAGER_GET_CLASS (manager);
  if (klass->set_power_save_mode)
    klass->set_power_save_mode (manager, mode);

  manager->power_save_mode = mode;
}

static void
cobiwm_monitor_manager_constructed (GObject *object)
{
  CobiwmMonitorManager *manager = COBIWM_MONITOR_MANAGER (object);

  g_signal_connect_object (manager, "notify::power-save-mode",
                           G_CALLBACK (power_save_mode_changed), manager, 0);

  manager->in_init = TRUE;

  manager->config = cobiwm_monitor_config_new ();

  cobiwm_monitor_manager_read_current_config (manager);

  if (!cobiwm_monitor_config_apply_stored (manager->config, manager))
    cobiwm_monitor_config_make_default (manager->config, manager);

  /* Under XRandR, we don't rebuild our data structures until we see
     the RRScreenNotify event, but at least at startup we want to have
     the right configuration immediately.

     The other backends keep the data structures always updated,
     so this is not needed.
  */
  if (COBIWM_IS_MONITOR_MANAGER_XRANDR (manager))
    cobiwm_monitor_manager_read_current_config (manager);

  make_logical_config (manager);
  initialize_dbus_interface (manager);

  manager->in_init = FALSE;
}

void
cobiwm_monitor_manager_clear_output (CobiwmOutput *output)
{
  g_free (output->name);
  g_free (output->vendor);
  g_free (output->product);
  g_free (output->serial);
  g_free (output->modes);
  g_free (output->possible_crtcs);
  g_free (output->possible_clones);

  if (output->driver_notify)
    output->driver_notify (output);

  memset (output, 0, sizeof (*output));
}

static void
cobiwm_monitor_manager_free_output_array (CobiwmOutput *old_outputs,
                                        int         n_old_outputs)
{
  int i;

  for (i = 0; i < n_old_outputs; i++)
    cobiwm_monitor_manager_clear_output (&old_outputs[i]);

  g_free (old_outputs);
}

void
cobiwm_monitor_manager_clear_mode (CobiwmMonitorMode *mode)
{
  g_free (mode->name);

  if (mode->driver_notify)
    mode->driver_notify (mode);

  memset (mode, 0, sizeof (*mode));
}

static void
cobiwm_monitor_manager_free_mode_array (CobiwmMonitorMode *old_modes,
                                      int              n_old_modes)
{
  int i;

  for (i = 0; i < n_old_modes; i++)
    cobiwm_monitor_manager_clear_mode (&old_modes[i]);

  g_free (old_modes);
}

void
cobiwm_monitor_manager_clear_crtc (CobiwmCRTC *crtc)
{
  if (crtc->driver_notify)
    crtc->driver_notify (crtc);

  memset (crtc, 0, sizeof (*crtc));
}

static void
cobiwm_monitor_manager_free_crtc_array (CobiwmCRTC *old_crtcs,
                                      int       n_old_crtcs)
{
  int i;

  for (i = 0; i < n_old_crtcs; i++)
    cobiwm_monitor_manager_clear_crtc (&old_crtcs[i]);

  g_free (old_crtcs);
}

static void
cobiwm_monitor_manager_finalize (GObject *object)
{
  CobiwmMonitorManager *manager = COBIWM_MONITOR_MANAGER (object);

  cobiwm_monitor_manager_free_output_array (manager->outputs, manager->n_outputs);
  cobiwm_monitor_manager_free_mode_array (manager->modes, manager->n_modes);
  cobiwm_monitor_manager_free_crtc_array (manager->crtcs, manager->n_crtcs);
  g_free (manager->monitor_infos);

  G_OBJECT_CLASS (cobiwm_monitor_manager_parent_class)->finalize (object);
}

static void
cobiwm_monitor_manager_dispose (GObject *object)
{
  CobiwmMonitorManager *manager = COBIWM_MONITOR_MANAGER (object);

  if (manager->dbus_name_id != 0)
    {
      g_bus_unown_name (manager->dbus_name_id);
      manager->dbus_name_id = 0;
    }

  G_OBJECT_CLASS (cobiwm_monitor_manager_parent_class)->dispose (object);
}

static GBytes *
cobiwm_monitor_manager_real_read_edid (CobiwmMonitorManager *manager,
                                     CobiwmOutput         *output)
{
  return NULL;
}

static char *
cobiwm_monitor_manager_real_get_edid_file (CobiwmMonitorManager *manager,
                                         CobiwmOutput         *output)
{
  return NULL;
}

static void
cobiwm_monitor_manager_class_init (CobiwmMonitorManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = cobiwm_monitor_manager_constructed;
  object_class->dispose = cobiwm_monitor_manager_dispose;
  object_class->finalize = cobiwm_monitor_manager_finalize;

  klass->get_edid_file = cobiwm_monitor_manager_real_get_edid_file;
  klass->read_edid = cobiwm_monitor_manager_real_read_edid;

  signals[CONFIRM_DISPLAY_CHANGE] =
    g_signal_new ("confirm-display-change",
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST,
		  0,
                  NULL, NULL, NULL,
		  G_TYPE_NONE, 0);
}

static const double known_diagonals[] = {
    12.1,
    13.3,
    15.6
};

static char *
diagonal_to_str (double d)
{
  unsigned int i;

  for (i = 0; i < G_N_ELEMENTS (known_diagonals); i++)
    {
      double delta;

      delta = fabs(known_diagonals[i] - d);
      if (delta < 0.1)
        return g_strdup_printf ("%0.1lf\"", known_diagonals[i]);
    }

  return g_strdup_printf ("%d\"", (int) (d + 0.5));
}

static char *
make_display_name (CobiwmMonitorManager *manager,
                   CobiwmOutput         *output)
{
  g_autofree char *inches = NULL;
  g_autofree char *vendor_name = NULL;

  if (cobiwm_output_is_laptop (output))
      return g_strdup (_("Built-in display"));

  if (output->width_mm > 0 && output->height_mm > 0)
    {
      double d = sqrt (output->width_mm * output->width_mm +
                       output->height_mm * output->height_mm);
      inches = diagonal_to_str (d / 25.4);
    }

  if (g_strcmp0 (output->vendor, "unknown") != 0)
    {
      if (!manager->pnp_ids)
        manager->pnp_ids = gnome_pnp_ids_new ();

      vendor_name = gnome_pnp_ids_get_pnp_id (manager->pnp_ids,
                                              output->vendor);

      if (!vendor_name)
        vendor_name = g_strdup (output->vendor);
    }
  else
    {
      if (inches != NULL)
        vendor_name = g_strdup (_("Unknown"));
      else
        vendor_name = g_strdup (_("Unknown Display"));
    }

  if (inches != NULL)
    {
      /* TRANSLATORS: this is a monitor vendor name, followed by a
       * size in inches, like 'Dell 15"'
       */
      return g_strdup_printf (_("%s %s"), vendor_name, inches);
    }
  else
    {
      return g_strdup (vendor_name);
    }
}

static const char *
get_connector_type_name (CobiwmConnectorType connector_type)
{
  switch (connector_type)
    {
    case COBIWM_CONNECTOR_TYPE_Unknown: return "Unknown";
    case COBIWM_CONNECTOR_TYPE_VGA: return "VGA";
    case COBIWM_CONNECTOR_TYPE_DVII: return "DVII";
    case COBIWM_CONNECTOR_TYPE_DVID: return "DVID";
    case COBIWM_CONNECTOR_TYPE_DVIA: return "DVIA";
    case COBIWM_CONNECTOR_TYPE_Composite: return "Composite";
    case COBIWM_CONNECTOR_TYPE_SVIDEO: return "SVIDEO";
    case COBIWM_CONNECTOR_TYPE_LVDS: return "LVDS";
    case COBIWM_CONNECTOR_TYPE_Component: return "Component";
    case COBIWM_CONNECTOR_TYPE_9PinDIN: return "9PinDIN";
    case COBIWM_CONNECTOR_TYPE_DisplayPort: return "DisplayPort";
    case COBIWM_CONNECTOR_TYPE_HDMIA: return "HDMIA";
    case COBIWM_CONNECTOR_TYPE_HDMIB: return "HDMIB";
    case COBIWM_CONNECTOR_TYPE_TV: return "TV";
    case COBIWM_CONNECTOR_TYPE_eDP: return "eDP";
    case COBIWM_CONNECTOR_TYPE_VIRTUAL: return "VIRTUAL";
    case COBIWM_CONNECTOR_TYPE_DSI: return "DSI";
    default: g_assert_not_reached ();
    }
}

static gboolean
cobiwm_monitor_manager_handle_get_resources (CobiwmDBusDisplayConfig *skeleton,
                                           GDBusMethodInvocation *invocation)
{
  CobiwmMonitorManager *manager = COBIWM_MONITOR_MANAGER (skeleton);
  CobiwmMonitorManagerClass *manager_class = COBIWM_MONITOR_MANAGER_GET_CLASS (skeleton);
  GVariantBuilder crtc_builder, output_builder, mode_builder;
  unsigned int i, j;

  g_variant_builder_init (&crtc_builder, G_VARIANT_TYPE ("a(uxiiiiiuaua{sv})"));
  g_variant_builder_init (&output_builder, G_VARIANT_TYPE ("a(uxiausauaua{sv})"));
  g_variant_builder_init (&mode_builder, G_VARIANT_TYPE ("a(uxuud)"));

  for (i = 0; i < manager->n_crtcs; i++)
    {
      CobiwmCRTC *crtc = &manager->crtcs[i];
      GVariantBuilder transforms;

      g_variant_builder_init (&transforms, G_VARIANT_TYPE ("au"));
      for (j = 0; j <= COBIWM_MONITOR_TRANSFORM_FLIPPED_270; j++)
        if (crtc->all_transforms & (1 << j))
          g_variant_builder_add (&transforms, "u", j);

      g_variant_builder_add (&crtc_builder, "(uxiiiiiuaua{sv})",
                             i, /* ID */
                             (gint64)crtc->crtc_id,
                             (int)crtc->rect.x,
                             (int)crtc->rect.y,
                             (int)crtc->rect.width,
                             (int)crtc->rect.height,
                             (int)(crtc->current_mode ? crtc->current_mode - manager->modes : -1),
                             (guint32)crtc->transform,
                             &transforms,
                             NULL /* properties */);
    }

  for (i = 0; i < manager->n_outputs; i++)
    {
      CobiwmOutput *output = &manager->outputs[i];
      GVariantBuilder crtcs, modes, clones, properties;
      GBytes *edid;
      char *edid_file;

      g_variant_builder_init (&crtcs, G_VARIANT_TYPE ("au"));
      for (j = 0; j < output->n_possible_crtcs; j++)
        g_variant_builder_add (&crtcs, "u",
                               (unsigned)(output->possible_crtcs[j] - manager->crtcs));

      g_variant_builder_init (&modes, G_VARIANT_TYPE ("au"));
      for (j = 0; j < output->n_modes; j++)
        g_variant_builder_add (&modes, "u",
                               (unsigned)(output->modes[j] - manager->modes));

      g_variant_builder_init (&clones, G_VARIANT_TYPE ("au"));
      for (j = 0; j < output->n_possible_clones; j++)
        g_variant_builder_add (&clones, "u",
                               (unsigned)(output->possible_clones[j] - manager->outputs));

      g_variant_builder_init (&properties, G_VARIANT_TYPE ("a{sv}"));
      g_variant_builder_add (&properties, "{sv}", "vendor",
                             g_variant_new_string (output->vendor));
      g_variant_builder_add (&properties, "{sv}", "product",
                             g_variant_new_string (output->product));
      g_variant_builder_add (&properties, "{sv}", "serial",
                             g_variant_new_string (output->serial));
      g_variant_builder_add (&properties, "{sv}", "width-mm",
                             g_variant_new_int32 (output->width_mm));
      g_variant_builder_add (&properties, "{sv}", "height-mm",
                             g_variant_new_int32 (output->height_mm));
      g_variant_builder_add (&properties, "{sv}", "display-name",
                             g_variant_new_take_string (make_display_name (manager, output)));
      g_variant_builder_add (&properties, "{sv}", "backlight",
                             g_variant_new_int32 (output->backlight));
      g_variant_builder_add (&properties, "{sv}", "min-backlight-step",
                             g_variant_new_int32 ((output->backlight_max - output->backlight_min) ?
                                                  100 / (output->backlight_max - output->backlight_min) : -1));
      g_variant_builder_add (&properties, "{sv}", "primary",
                             g_variant_new_boolean (output->is_primary));
      g_variant_builder_add (&properties, "{sv}", "presentation",
                             g_variant_new_boolean (output->is_presentation));
      g_variant_builder_add (&properties, "{sv}", "connector-type",
                             g_variant_new_string (get_connector_type_name (output->connector_type)));
      g_variant_builder_add (&properties, "{sv}", "underscanning",
                             g_variant_new_boolean (output->is_underscanning));
      g_variant_builder_add (&properties, "{sv}", "supports-underscanning",
                             g_variant_new_boolean (output->supports_underscanning));

      edid_file = manager_class->get_edid_file (manager, output);
      if (edid_file)
        {
          g_variant_builder_add (&properties, "{sv}", "edid-file",
                                 g_variant_new_take_string (edid_file));
        }
      else
        {
          edid = manager_class->read_edid (manager, output);

          if (edid)
            {
              g_variant_builder_add (&properties, "{sv}", "edid",
                                     g_variant_new_from_bytes (G_VARIANT_TYPE ("ay"),
                                                               edid, TRUE));
              g_bytes_unref (edid);
            }
        }

      if (output->tile_info.group_id)
        {
          g_variant_builder_add (&properties, "{sv}", "tile",
                                 g_variant_new ("(uuuuuuuu)",
                                                output->tile_info.group_id,
                                                output->tile_info.flags,
                                                output->tile_info.max_h_tiles,
                                                output->tile_info.max_v_tiles,
                                                output->tile_info.loc_h_tile,
                                                output->tile_info.loc_v_tile,
                                                output->tile_info.tile_w,
                                                output->tile_info.tile_h));
        }

      g_variant_builder_add (&output_builder, "(uxiausauaua{sv})",
                             i, /* ID */
                             (gint64)output->winsys_id,
                             (int)(output->crtc ? output->crtc - manager->crtcs : -1),
                             &crtcs,
                             output->name,
                             &modes,
                             &clones,
                             &properties);
    }

  for (i = 0; i < manager->n_modes; i++)
    {
      CobiwmMonitorMode *mode = &manager->modes[i];

      g_variant_builder_add (&mode_builder, "(uxuud)",
                             i, /* ID */
                             (gint64)mode->mode_id,
                             (guint32)mode->width,
                             (guint32)mode->height,
                             (double)mode->refresh_rate);
    }

  cobiwm_dbus_display_config_complete_get_resources (skeleton,
                                                   invocation,
                                                   manager->serial,
                                                   g_variant_builder_end (&crtc_builder),
                                                   g_variant_builder_end (&output_builder),
                                                   g_variant_builder_end (&mode_builder),
                                                   manager->max_screen_width,
                                                   manager->max_screen_height);
  return TRUE;
}

static gboolean
output_can_config (CobiwmOutput      *output,
                   CobiwmCRTC        *crtc,
                   CobiwmMonitorMode *mode)
{
  unsigned int i;
  gboolean ok = FALSE;

  for (i = 0; i < output->n_possible_crtcs && !ok; i++)
    ok = output->possible_crtcs[i] == crtc;

  if (!ok)
    return FALSE;

  if (mode == NULL)
    return TRUE;

  ok = FALSE;
  for (i = 0; i < output->n_modes && !ok; i++)
    ok = output->modes[i] == mode;

  return ok;
}

static gboolean
output_can_clone (CobiwmOutput *output,
                  CobiwmOutput *clone)
{
  unsigned int i;
  gboolean ok = FALSE;

  for (i = 0; i < output->n_possible_clones && !ok; i++)
    ok = output->possible_clones[i] == clone;

  return ok;
}

void
cobiwm_monitor_manager_apply_configuration (CobiwmMonitorManager *manager,
                                          CobiwmCRTCInfo       **crtcs,
                                          unsigned int         n_crtcs,
                                          CobiwmOutputInfo     **outputs,
                                          unsigned int         n_outputs)
{
  COBIWM_MONITOR_MANAGER_GET_CLASS (manager)->apply_configuration (manager,
                                                                 crtcs, n_crtcs,
                                                                 outputs, n_outputs);
}

static gboolean
save_config_timeout (gpointer user_data)
{
  CobiwmMonitorManager *manager = user_data;

  cobiwm_monitor_config_restore_previous (manager->config, manager);

  manager->persistent_timeout_id = 0;
  return G_SOURCE_REMOVE;
}

static gboolean
cobiwm_monitor_manager_handle_apply_configuration  (CobiwmDBusDisplayConfig *skeleton,
                                                  GDBusMethodInvocation *invocation,
                                                  guint                  serial,
                                                  gboolean               persistent,
                                                  GVariant              *crtcs,
                                                  GVariant              *outputs)
{
  CobiwmMonitorManager *manager = COBIWM_MONITOR_MANAGER (skeleton);
  GVariantIter crtc_iter, output_iter, *nested_outputs;
  GVariant *properties;
  guint crtc_id;
  int new_mode, x, y;
  int new_screen_width, new_screen_height;
  guint transform;
  guint output_index;
  GPtrArray *crtc_infos, *output_infos;

  if (serial != manager->serial)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_ACCESS_DENIED,
                                             "The requested configuration is based on stale information");
      return TRUE;
    }

  crtc_infos = g_ptr_array_new_full (g_variant_n_children (crtcs),
                                     (GDestroyNotify) cobiwm_crtc_info_free);
  output_infos = g_ptr_array_new_full (g_variant_n_children (outputs),
                                       (GDestroyNotify) cobiwm_output_info_free);

  /* Validate all arguments */
  new_screen_width = 0; new_screen_height = 0;
  g_variant_iter_init (&crtc_iter, crtcs);
  while (g_variant_iter_loop (&crtc_iter, "(uiiiuaua{sv})",
                              &crtc_id, &new_mode, &x, &y, &transform,
                              &nested_outputs, NULL))
    {
      CobiwmCRTCInfo *crtc_info;
      CobiwmOutput *first_output;
      CobiwmCRTC *crtc;
      CobiwmMonitorMode *mode;

      crtc_info = g_slice_new (CobiwmCRTCInfo);
      crtc_info->outputs = g_ptr_array_new ();

      if (crtc_id >= manager->n_crtcs)
        {
          g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                                 G_DBUS_ERROR_INVALID_ARGS,
                                                 "Invalid CRTC id");
          return TRUE;
        }
      crtc = &manager->crtcs[crtc_id];
      crtc_info->crtc = crtc;

      if (new_mode != -1 && (new_mode < 0 || (unsigned)new_mode >= manager->n_modes))
        {
          g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                                 G_DBUS_ERROR_INVALID_ARGS,
                                                 "Invalid mode id");
          return TRUE;
        }
      mode = new_mode != -1 ? &manager->modes[new_mode] : NULL;
      crtc_info->mode = mode;

      if (mode)
        {
          int width, height;

          if (cobiwm_monitor_transform_is_rotated (transform))
            {
              width = mode->height;
              height = mode->width;
            }
          else
            {
              width = mode->width;
              height = mode->height;
            }

          if (x < 0 ||
              x + width > manager->max_screen_width ||
              y < 0 ||
              y + height > manager->max_screen_height)
            {
              g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                                     G_DBUS_ERROR_INVALID_ARGS,
                                                     "Invalid CRTC geometry");
              return TRUE;
            }

          new_screen_width = MAX (new_screen_width, x + width);
          new_screen_height = MAX (new_screen_height, y + height);
          crtc_info->x = x;
          crtc_info->y = y;
        }
      else
        {
          crtc_info->x = 0;
          crtc_info->y = 0;
        }

      if (transform > COBIWM_MONITOR_TRANSFORM_FLIPPED_270 ||
          ((crtc->all_transforms & (1 << transform)) == 0))
        {
          g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                                 G_DBUS_ERROR_INVALID_ARGS,
                                                 "Invalid transform");
          return TRUE;
        }
      crtc_info->transform = transform;

      first_output = NULL;
      while (g_variant_iter_loop (nested_outputs, "u", &output_index))
        {
          CobiwmOutput *output;

          if (output_index >= manager->n_outputs)
            {
              g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                                     G_DBUS_ERROR_INVALID_ARGS,
                                                     "Invalid output id");
              return TRUE;
            }
          output = &manager->outputs[output_index];

          if (!output_can_config (output, crtc, mode))
            {
              g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                                     G_DBUS_ERROR_INVALID_ARGS,
                                                     "Output cannot be assigned to this CRTC or mode");
              return TRUE;
            }
          g_ptr_array_add (crtc_info->outputs, output);

          if (first_output)
            {
              if (!output_can_clone (output, first_output))
                {
                  g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                                         G_DBUS_ERROR_INVALID_ARGS,
                                                         "Outputs cannot be cloned");
                  return TRUE;
                }
            }
          else
            first_output = output;
        }

      if (!first_output && mode)
        {
          g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                                 G_DBUS_ERROR_INVALID_ARGS,
                                                 "Mode specified without outputs?");
          return TRUE;
        }

      g_ptr_array_add (crtc_infos, crtc_info);
    }

  if (new_screen_width == 0 || new_screen_height == 0)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_INVALID_ARGS,
                                             "Refusing to disable all outputs");
      return TRUE;
    }

  g_variant_iter_init (&output_iter, outputs);
  while (g_variant_iter_loop (&output_iter, "(u@a{sv})", &output_index, &properties))
    {
      CobiwmOutputInfo *output_info;
      gboolean primary, presentation, underscanning;

      if (output_index >= manager->n_outputs)
        {
          g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                                 G_DBUS_ERROR_INVALID_ARGS,
                                                 "Invalid output id");
          return TRUE;
        }

      output_info = g_slice_new0 (CobiwmOutputInfo);
      output_info->output = &manager->outputs[output_index];

      if (g_variant_lookup (properties, "primary", "b", &primary))
        output_info->is_primary = primary;

      if (g_variant_lookup (properties, "presentation", "b", &presentation))
        output_info->is_presentation = presentation;

      if (g_variant_lookup (properties, "underscanning", "b", &underscanning))
        output_info->is_underscanning = underscanning;

      g_ptr_array_add (output_infos, output_info);
    }

  /* If we were in progress of making a persistent change and we see a
     new request, it's likely that the old one failed in some way, so
     don't save it, but also don't queue for restoring it.
  */
  if (manager->persistent_timeout_id && persistent)
    {
      g_source_remove (manager->persistent_timeout_id);
      manager->persistent_timeout_id = 0;
    }

  cobiwm_monitor_manager_apply_configuration (manager,
                                            (CobiwmCRTCInfo**)crtc_infos->pdata,
                                            crtc_infos->len,
                                            (CobiwmOutputInfo**)output_infos->pdata,
                                            output_infos->len);

  g_ptr_array_unref (crtc_infos);
  g_ptr_array_unref (output_infos);

  /* Update CobiwmMonitorConfig data structures immediately so that we
     don't revert the change at the next XRandR event, then ask the plugin
     manager (through CobiwmScreen) to confirm the display change with the
     appropriate UI. Then wait 20 seconds and if not confirmed, revert the
     configuration.
  */
  cobiwm_monitor_config_update_current (manager->config, manager);
  if (persistent)
    {
      manager->persistent_timeout_id = g_timeout_add_seconds (20, save_config_timeout, manager);
      g_source_set_name_by_id (manager->persistent_timeout_id, "[cobiwm] save_config_timeout");
      g_signal_emit (manager, signals[CONFIRM_DISPLAY_CHANGE], 0);
    }

  cobiwm_dbus_display_config_complete_apply_configuration (skeleton, invocation);
  return TRUE;
}

void
cobiwm_monitor_manager_confirm_configuration (CobiwmMonitorManager *manager,
                                            gboolean            ok)
{
  if (!manager->persistent_timeout_id)
    {
      /* too late */
      return;
    }

  g_source_remove (manager->persistent_timeout_id);
  manager->persistent_timeout_id = 0;

  if (ok)
    cobiwm_monitor_config_make_persistent (manager->config);
  else
    cobiwm_monitor_config_restore_previous (manager->config, manager);
}

static gboolean
cobiwm_monitor_manager_handle_change_backlight  (CobiwmDBusDisplayConfig *skeleton,
                                               GDBusMethodInvocation *invocation,
                                               guint                  serial,
                                               guint                  output_index,
                                               gint                   value)
{
  CobiwmMonitorManager *manager = COBIWM_MONITOR_MANAGER (skeleton);
  CobiwmOutput *output;

  if (serial != manager->serial)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_ACCESS_DENIED,
                                             "The requested configuration is based on stale information");
      return TRUE;
    }

  if (output_index >= manager->n_outputs)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_INVALID_ARGS,
                                             "Invalid output id");
      return TRUE;
    }
  output = &manager->outputs[output_index];

  if (value < 0 || value > 100)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_INVALID_ARGS,
                                             "Invalid backlight value");
      return TRUE;
    }

  if (output->backlight == -1 ||
      (output->backlight_min == 0 && output->backlight_max == 0))
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_INVALID_ARGS,
                                             "Output does not support changing backlight");
      return TRUE;
    }

  COBIWM_MONITOR_MANAGER_GET_CLASS (manager)->change_backlight (manager, output, value);

  cobiwm_dbus_display_config_complete_change_backlight (skeleton, invocation, output->backlight);
  return TRUE;
}

static gboolean
cobiwm_monitor_manager_handle_get_crtc_gamma  (CobiwmDBusDisplayConfig *skeleton,
                                             GDBusMethodInvocation *invocation,
                                             guint                  serial,
                                             guint                  crtc_id)
{
  CobiwmMonitorManager *manager = COBIWM_MONITOR_MANAGER (skeleton);
  CobiwmMonitorManagerClass *klass;
  CobiwmCRTC *crtc;
  gsize size;
  unsigned short *red;
  unsigned short *green;
  unsigned short *blue;
  GBytes *red_bytes, *green_bytes, *blue_bytes;
  GVariant *red_v, *green_v, *blue_v;

  if (serial != manager->serial)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_ACCESS_DENIED,
                                             "The requested configuration is based on stale information");
      return TRUE;
    }

  if (crtc_id >= manager->n_crtcs)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_INVALID_ARGS,
                                             "Invalid crtc id");
      return TRUE;
    }
  crtc = &manager->crtcs[crtc_id];

  klass = COBIWM_MONITOR_MANAGER_GET_CLASS (manager);
  if (klass->get_crtc_gamma)
    klass->get_crtc_gamma (manager, crtc, &size, &red, &green, &blue);
  else
    {
      size = 0;
      red = green = blue = NULL;
    }

  red_bytes = g_bytes_new_take (red, size * sizeof (unsigned short));
  green_bytes = g_bytes_new_take (green, size * sizeof (unsigned short));
  blue_bytes = g_bytes_new_take (blue, size * sizeof (unsigned short));

  red_v = g_variant_new_from_bytes (G_VARIANT_TYPE ("aq"), red_bytes, TRUE);
  green_v = g_variant_new_from_bytes (G_VARIANT_TYPE ("aq"), green_bytes, TRUE);
  blue_v = g_variant_new_from_bytes (G_VARIANT_TYPE ("aq"), blue_bytes, TRUE);

  cobiwm_dbus_display_config_complete_get_crtc_gamma (skeleton, invocation,
                                                    red_v, green_v, blue_v);

  g_bytes_unref (red_bytes);
  g_bytes_unref (green_bytes);
  g_bytes_unref (blue_bytes);

  return TRUE;
}

static gboolean
cobiwm_monitor_manager_handle_set_crtc_gamma  (CobiwmDBusDisplayConfig *skeleton,
                                             GDBusMethodInvocation *invocation,
                                             guint                  serial,
                                             guint                  crtc_id,
                                             GVariant              *red_v,
                                             GVariant              *green_v,
                                             GVariant              *blue_v)
{
  CobiwmMonitorManager *manager = COBIWM_MONITOR_MANAGER (skeleton);
  CobiwmMonitorManagerClass *klass;
  CobiwmCRTC *crtc;
  gsize size, dummy;
  unsigned short *red;
  unsigned short *green;
  unsigned short *blue;
  GBytes *red_bytes, *green_bytes, *blue_bytes;

  if (serial != manager->serial)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_ACCESS_DENIED,
                                             "The requested configuration is based on stale information");
      return TRUE;
    }

  if (crtc_id >= manager->n_crtcs)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_INVALID_ARGS,
                                             "Invalid crtc id");
      return TRUE;
    }
  crtc = &manager->crtcs[crtc_id];

  red_bytes = g_variant_get_data_as_bytes (red_v);
  green_bytes = g_variant_get_data_as_bytes (green_v);
  blue_bytes = g_variant_get_data_as_bytes (blue_v);

  size = g_bytes_get_size (red_bytes) / sizeof (unsigned short);
  red = (unsigned short*) g_bytes_get_data (red_bytes, &dummy);
  green = (unsigned short*) g_bytes_get_data (green_bytes, &dummy);
  blue = (unsigned short*) g_bytes_get_data (blue_bytes, &dummy);

  klass = COBIWM_MONITOR_MANAGER_GET_CLASS (manager);
  if (klass->set_crtc_gamma)
    klass->set_crtc_gamma (manager, crtc, size, red, green, blue);

  cobiwm_dbus_display_config_complete_set_crtc_gamma (skeleton, invocation);

  g_bytes_unref (red_bytes);
  g_bytes_unref (green_bytes);
  g_bytes_unref (blue_bytes);

  return TRUE;
}

static void
cobiwm_monitor_manager_display_config_init (CobiwmDBusDisplayConfigIface *iface)
{
  iface->handle_get_resources = cobiwm_monitor_manager_handle_get_resources;
  iface->handle_apply_configuration = cobiwm_monitor_manager_handle_apply_configuration;
  iface->handle_change_backlight = cobiwm_monitor_manager_handle_change_backlight;
  iface->handle_get_crtc_gamma = cobiwm_monitor_manager_handle_get_crtc_gamma;
  iface->handle_set_crtc_gamma = cobiwm_monitor_manager_handle_set_crtc_gamma;
}

static void
on_bus_acquired (GDBusConnection *connection,
                 const char      *name,
                 gpointer         user_data)
{
  CobiwmMonitorManager *manager = user_data;

  g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (manager),
                                    connection,
                                    "/org/gnome/Cobiwm/DisplayConfig",
                                    NULL);
}

static void
on_name_acquired (GDBusConnection *connection,
                  const char      *name,
                  gpointer         user_data)
{
  cobiwm_topic (COBIWM_DEBUG_DBUS, "Acquired name %s\n", name);
}

static void
on_name_lost (GDBusConnection *connection,
              const char      *name,
              gpointer         user_data)
{
  cobiwm_topic (COBIWM_DEBUG_DBUS, "Lost or failed to acquire name %s\n", name);
}

static void
initialize_dbus_interface (CobiwmMonitorManager *manager)
{
  manager->dbus_name_id = g_bus_own_name (G_BUS_TYPE_SESSION,
                                          "org.Cobiwm.DisplayConfig",
                                          G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT |
                                          (cobiwm_get_replace_current_wm () ?
                                           G_BUS_NAME_OWNER_FLAGS_REPLACE : 0),
                                          on_bus_acquired,
                                          on_name_acquired,
                                          on_name_lost,
                                          g_object_ref (manager),
                                          g_object_unref);
}

/**
 * cobiwm_monitor_manager_get:
 *
 * Accessor for the singleton CobiwmMonitorManager.
 *
 * Returns: (transfer none): The only #CobiwmMonitorManager there is.
 */
CobiwmMonitorManager *
cobiwm_monitor_manager_get (void)
{
  CobiwmBackend *backend = cobiwm_get_backend ();

  return cobiwm_backend_get_monitor_manager (backend);
}

CobiwmMonitorInfo *
cobiwm_monitor_manager_get_monitor_infos (CobiwmMonitorManager *manager,
                                        unsigned int       *n_infos)
{
  *n_infos = manager->n_monitor_infos;
  return manager->monitor_infos;
}

CobiwmOutput *
cobiwm_monitor_manager_get_outputs (CobiwmMonitorManager *manager,
                                  unsigned int       *n_outputs)
{
  *n_outputs = manager->n_outputs;
  return manager->outputs;
}

void
cobiwm_monitor_manager_get_resources (CobiwmMonitorManager  *manager,
                                    CobiwmMonitorMode    **modes,
                                    unsigned int        *n_modes,
                                    CobiwmCRTC           **crtcs,
                                    unsigned int        *n_crtcs,
                                    CobiwmOutput         **outputs,
                                    unsigned int        *n_outputs)
{
  if (modes)
    {
      *modes = manager->modes;
      *n_modes = manager->n_modes;
    }
  if (crtcs)
    {
      *crtcs = manager->crtcs;
      *n_crtcs = manager->n_crtcs;
    }
  if (outputs)
    {
      *outputs = manager->outputs;
      *n_outputs = manager->n_outputs;
    }
}

int
cobiwm_monitor_manager_get_primary_index (CobiwmMonitorManager *manager)
{
  return manager->primary_monitor_index;
}

void
cobiwm_monitor_manager_get_screen_size (CobiwmMonitorManager *manager,
                                      int                *width,
                                      int                *height)
{
  *width = manager->screen_width;
  *height = manager->screen_height;
}

void
cobiwm_monitor_manager_get_screen_limits (CobiwmMonitorManager *manager,
                                        int                *width,
                                        int                *height)
{
  *width = manager->max_screen_width;
  *height = manager->max_screen_height;
}

void
cobiwm_monitor_manager_read_current_config (CobiwmMonitorManager *manager)
{
  CobiwmOutput *old_outputs;
  CobiwmCRTC *old_crtcs;
  CobiwmMonitorMode *old_modes;
  unsigned int n_old_outputs, n_old_crtcs, n_old_modes;

  /* Some implementations of read_current use the existing information
   * we have available, so don't free the old configuration until after
   * read_current finishes. */
  old_outputs = manager->outputs;
  n_old_outputs = manager->n_outputs;
  old_crtcs = manager->crtcs;
  n_old_crtcs = manager->n_crtcs;
  old_modes = manager->modes;
  n_old_modes = manager->n_modes;

  manager->serial++;
  COBIWM_MONITOR_MANAGER_GET_CLASS (manager)->read_current (manager);

  cobiwm_monitor_manager_free_output_array (old_outputs, n_old_outputs);
  cobiwm_monitor_manager_free_mode_array (old_modes, n_old_modes);
  cobiwm_monitor_manager_free_crtc_array (old_crtcs, n_old_crtcs);
}

void
cobiwm_monitor_manager_rebuild_derived (CobiwmMonitorManager *manager)
{
  CobiwmMonitorManagerClass *manager_class = COBIWM_MONITOR_MANAGER_GET_CLASS (manager);
  CobiwmMonitorInfo *old_monitor_infos;
  unsigned old_n_monitor_infos;
  unsigned i, j;
  old_monitor_infos = manager->monitor_infos;
  old_n_monitor_infos = manager->n_monitor_infos;

  if (manager->in_init)
    return;

  make_logical_config (manager);

  if (manager_class->delete_monitor)
    {
      for (i = 0; i < old_n_monitor_infos; i++)
        {
          gboolean delete_mon = TRUE;
          for (j = 0; j < manager->n_monitor_infos; j++)
            {
              if (manager->monitor_infos[j].monitor_winsys_xid == old_monitor_infos[i].monitor_winsys_xid)
                {
                  delete_mon = FALSE;
                  break;
                }
            }
          if (delete_mon)
            manager_class->delete_monitor (manager, old_monitor_infos[i].monitor_winsys_xid);
        }
    }
  g_signal_emit_by_name (manager, "monitors-changed");

  g_free (old_monitor_infos);
}

void
cobiwm_output_parse_edid (CobiwmOutput *cobiwm_output,
                        GBytes     *edid)
{
  MonitorInfo *parsed_edid;
  gsize len;

  if (!edid)
    goto out;

  parsed_edid = decode_edid (g_bytes_get_data (edid, &len));

  if (parsed_edid)
    {
      cobiwm_output->vendor = g_strndup (parsed_edid->manufacturer_code, 4);
      if (!g_utf8_validate (cobiwm_output->vendor, -1, NULL))
        g_clear_pointer (&cobiwm_output->vendor, g_free);

      cobiwm_output->product = g_strndup (parsed_edid->dsc_product_name, 14);
      if (!g_utf8_validate (cobiwm_output->product, -1, NULL) ||
          cobiwm_output->product[0] == '\0')
        {
          g_clear_pointer (&cobiwm_output->product, g_free);
          cobiwm_output->product = g_strdup_printf ("0x%04x", (unsigned) parsed_edid->product_code);
        }

      cobiwm_output->serial = g_strndup (parsed_edid->dsc_serial_number, 14);
      if (!g_utf8_validate (cobiwm_output->serial, -1, NULL) ||
          cobiwm_output->serial[0] == '\0')
        {
          g_clear_pointer (&cobiwm_output->serial, g_free);
          cobiwm_output->serial = g_strdup_printf ("0x%08x", parsed_edid->serial_number);
        }

      g_free (parsed_edid);
    }

 out:
  if (!cobiwm_output->vendor)
    cobiwm_output->vendor = g_strdup ("unknown");
  if (!cobiwm_output->product)
    cobiwm_output->product = g_strdup ("unknown");
  if (!cobiwm_output->serial)
    cobiwm_output->serial = g_strdup ("unknown");
}

gboolean
cobiwm_output_is_laptop (CobiwmOutput *output)
{
  /* FIXME: extend with better heuristics */
  switch (output->connector_type)
    {
    case COBIWM_CONNECTOR_TYPE_eDP:
    case COBIWM_CONNECTOR_TYPE_LVDS:
    case COBIWM_CONNECTOR_TYPE_DSI:
      return TRUE;
    default:
      return FALSE;
    }
}

void
cobiwm_monitor_manager_on_hotplug (CobiwmMonitorManager *manager)
{
  gboolean applied_config = FALSE;

  /* If the monitor has hotplug_mode_update (which is used by VMs), don't bother
   * applying our stored configuration, because it's likely the user just resizing
   * the window.
   */
  if (!cobiwm_monitor_manager_has_hotplug_mode_update (manager))
    {
      if (cobiwm_monitor_config_apply_stored (manager->config, manager))
        applied_config = TRUE;
    }

  /* If we haven't applied any configuration, apply the default configuration. */
  if (!applied_config)
    cobiwm_monitor_config_make_default (manager->config, manager);
}

static gboolean
calculate_viewport_matrix (CobiwmMonitorManager *manager,
                           CobiwmOutput         *output,
                           gfloat              viewport[6])
{
  gfloat x, y, width, height;

  if (!output->crtc)
    return FALSE;

  x = (float) output->crtc->rect.x / manager->screen_width;
  y = (float) output->crtc->rect.y / manager->screen_height;
  width  = (float) output->crtc->rect.width / manager->screen_width;
  height = (float) output->crtc->rect.height / manager->screen_height;

  viewport[0] = width;
  viewport[1] = 0.0f;
  viewport[2] = x;
  viewport[3] = 0.0f;
  viewport[4] = height;
  viewport[5] = y;

  return TRUE;
}

static inline void
multiply_matrix (float a[6],
		 float b[6],
		 float res[6])
{
  res[0] = a[0] * b[0] + a[1] * b[3];
  res[1] = a[0] * b[1] + a[1] * b[4];
  res[2] = a[0] * b[2] + a[1] * b[5] + a[2];
  res[3] = a[3] * b[0] + a[4] * b[3];
  res[4] = a[3] * b[1] + a[4] * b[4];
  res[5] = a[3] * b[2] + a[4] * b[5] + a[5];
}

gboolean
cobiwm_monitor_manager_get_monitor_matrix (CobiwmMonitorManager *manager,
                                         CobiwmOutput         *output,
                                         gfloat              matrix[6])
{
  gfloat viewport[9];

  if (!calculate_viewport_matrix (manager, output, viewport))
    return FALSE;

  multiply_matrix (viewport, transform_matrices[output->crtc->transform],
                   matrix);
  return TRUE;
}

/**
 * cobiwm_monitor_manager_get_output_geometry:
 * @manager: A #CobiwmMonitorManager
 * @id: A valid #CobiwmOutput id
 *
 * Returns: The monitor index or -1 if @id isn't valid or the output
 * isn't associated with a logical monitor.
 */
gint
cobiwm_monitor_manager_get_monitor_for_output (CobiwmMonitorManager *manager,
                                             guint               id)
{
  CobiwmOutput *output;
  guint i;

  g_return_val_if_fail (COBIWM_IS_MONITOR_MANAGER (manager), -1);
  g_return_val_if_fail (id < manager->n_outputs, -1);

  output = &manager->outputs[id];
  if (!output || !output->crtc)
    return -1;

  for (i = 0; i < manager->n_monitor_infos; i++)
    if (cobiwm_rectangle_contains_rect (&manager->monitor_infos[i].rect,
                                      &output->crtc->rect))
      return i;

  return -1;
}

gint
cobiwm_monitor_manager_get_monitor_at_point (CobiwmMonitorManager *manager,
                                           gfloat              x,
                                           gfloat              y)
{
  unsigned int i;

  for (i = 0; i < manager->n_monitor_infos; i++)
    {
      CobiwmMonitorInfo *monitor = &manager->monitor_infos[i];
      int left, right, top, bottom;

      left = monitor->rect.x;
      right = left + monitor->rect.width;
      top = monitor->rect.y;
      bottom = top + monitor->rect.height;

      if ((x >= left) && (x < right) && (y >= top) && (y < bottom))
	return i;
    }

  return -1;
}

gboolean
cobiwm_monitor_manager_get_is_builtin_display_on (CobiwmMonitorManager *manager)
{
  g_return_val_if_fail (COBIWM_IS_MONITOR_MANAGER (manager), FALSE);

  return cobiwm_monitor_config_get_is_builtin_display_on (manager->config);
}
