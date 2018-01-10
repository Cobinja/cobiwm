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

/*
 * Portions of this file are derived from gnome-desktop/libgnome-desktop/gnome-rr-config.c
 *
 * Copyright 2007, 2008, Red Hat, Inc.
 * Copyright 2010 Giovanni Campagna
 *
 * Author: Soren Sandmann <sandmann@redhat.com>
 */

#include "config.h"

#include "boxes-private.h"
#include "cobiwm-monitor-config.h"

#include <string.h>
#include <clutter/clutter.h>
#include <libupower-glib/upower.h>

#include <main.h>
#include <errors.h>

/* These structures represent the intended/persistent configuration,
   as stored in the monitors.xml file.
*/

typedef struct {
  char *connector;
  char *vendor;
  char *product;
  char *serial;
} CobiwmOutputKey;

/* Keep this structure packed, so that we
   can use memcmp */
typedef struct {
  gboolean enabled;
  CobiwmRectangle rect;
  float refresh_rate;
  CobiwmMonitorTransform transform;

  gboolean is_primary;
  gboolean is_presentation;
  gboolean is_underscanning;
} CobiwmOutputConfig;

typedef struct {
  guint refcount;
  CobiwmOutputKey *keys;
  CobiwmOutputConfig *outputs;
  unsigned int n_outputs;
} CobiwmConfiguration;

struct _CobiwmMonitorConfig {
  GObject parent_instance;

  GHashTable *configs;
  CobiwmConfiguration *current;
  gboolean current_is_for_laptop_lid;
  CobiwmConfiguration *previous;

  GFile *user_file;
  GFile *system_file;
  GCancellable *save_cancellable;

  UpClient *up_client;
  gboolean lid_is_closed;
};

struct _CobiwmMonitorConfigClass {
  GObjectClass parent;
};

G_DEFINE_TYPE (CobiwmMonitorConfig, cobiwm_monitor_config, G_TYPE_OBJECT);

static gboolean cobiwm_monitor_config_assign_crtcs (CobiwmConfiguration  *config,
                                                  CobiwmMonitorManager *manager,
                                                  GPtrArray          *crtcs,
                                                  GPtrArray          *outputs);

static void     power_client_changed_cb (UpClient   *client,
                                         GParamSpec *pspec,
                                         gpointer    user_data);

static void
free_output_key (CobiwmOutputKey *key)
{
  g_free (key->connector);
  g_free (key->vendor);
  g_free (key->product);
  g_free (key->serial);
}

static void
config_clear (CobiwmConfiguration *config)
{
  unsigned int i;

  for (i = 0; i < config->n_outputs; i++)
    free_output_key (&config->keys[i]);

  g_free (config->keys);
  g_free (config->outputs);
}

static CobiwmConfiguration *
config_ref (CobiwmConfiguration *config)
{
  config->refcount++;
  return config;
}

static void
config_unref (CobiwmConfiguration *config)
{
  if (--config->refcount == 0)
    {
      config_clear (config);
      g_slice_free (CobiwmConfiguration, config);
    }
}

static CobiwmConfiguration *
config_new (void)
{
  CobiwmConfiguration *config = g_slice_new0 (CobiwmConfiguration);
  config->refcount = 1;
  return config;
}

static unsigned long
output_key_hash (const CobiwmOutputKey *key)
{
  return g_str_hash (key->connector) ^
    g_str_hash (key->vendor) ^
    g_str_hash (key->product) ^
    g_str_hash (key->serial);
}

static gboolean
output_key_equal (const CobiwmOutputKey *one,
                  const CobiwmOutputKey *two)
{
  return strcmp (one->connector, two->connector) == 0 &&
    strcmp (one->vendor, two->vendor) == 0 &&
    strcmp (one->product, two->product) == 0 &&
    strcmp (one->serial, two->serial) == 0;
}

static gboolean
output_config_equal (const CobiwmOutputConfig *one,
                     const CobiwmOutputConfig *two)
{
  return memcmp (one, two, sizeof (CobiwmOutputConfig)) == 0;
}

static unsigned int
config_hash (gconstpointer data)
{
  const CobiwmConfiguration *config = data;
  unsigned int i, hash;

  hash = 0;
  for (i = 0; i < config->n_outputs; i++)
    hash ^= output_key_hash (&config->keys[i]);

  return hash;
}

static gboolean
config_equal (gconstpointer one,
              gconstpointer two)
{
  const CobiwmConfiguration *c_one = one;
  const CobiwmConfiguration *c_two = two;
  unsigned int i;
  gboolean ok;

  if (c_one->n_outputs != c_two->n_outputs)
    return FALSE;

  ok = TRUE;
  for (i = 0; i < c_one->n_outputs && ok; i++)
    ok = output_key_equal (&c_one->keys[i],
                           &c_two->keys[i]);

  return ok;
}

static gboolean
config_equal_full (gconstpointer one,
                   gconstpointer two)
{
  const CobiwmConfiguration *c_one = one;
  const CobiwmConfiguration *c_two = two;
  unsigned int i;
  gboolean ok;

  if (c_one->n_outputs != c_two->n_outputs)
    return FALSE;

  ok = TRUE;
  for (i = 0; i < c_one->n_outputs && ok; i++)
    {
      ok = output_key_equal (&c_one->keys[i],
                             &c_two->keys[i]);
      ok = ok && output_config_equal (&c_one->outputs[i],
                                      &c_two->outputs[i]);
    }

  return ok;
}

static void
cobiwm_monitor_config_init (CobiwmMonitorConfig *self)
{
  const char *filename;
  char *path;
  const char * const *system_dirs;

  self->configs = g_hash_table_new_full (config_hash, config_equal, NULL, (GDestroyNotify) config_unref);

  filename = g_getenv ("COBIWM_MONITOR_FILENAME");
  if (filename == NULL)
    filename = "monitors.xml";

  path = g_build_filename (g_get_user_config_dir (), filename, NULL);
  self->user_file = g_file_new_for_path (path);
  g_free (path);

  for (system_dirs = g_get_system_config_dirs (); !self->system_file && *system_dirs; system_dirs++)
    {
      path = g_build_filename (*system_dirs, filename, NULL);
      if (g_file_test (path, G_FILE_TEST_EXISTS))
        self->system_file = g_file_new_for_path (path);
      g_free (path);
    }

  self->up_client = up_client_new ();
  self->lid_is_closed = up_client_get_lid_is_closed (self->up_client);

  g_signal_connect_object (self->up_client, "notify::lid-is-closed",
                           G_CALLBACK (power_client_changed_cb), self, 0);
}

static void
cobiwm_monitor_config_finalize (GObject *object)
{
  CobiwmMonitorConfig *self = COBIWM_MONITOR_CONFIG (object);

  g_hash_table_destroy (self->configs);
}

static void
cobiwm_monitor_config_class_init (CobiwmMonitorConfigClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = cobiwm_monitor_config_finalize;
}

typedef enum {
  STATE_INITIAL,
  STATE_MONITORS,
  STATE_CONFIGURATION,
  STATE_OUTPUT,
  STATE_OUTPUT_FIELD,
  STATE_CLONE
} ParserState;

typedef struct {
  CobiwmMonitorConfig *config;
  ParserState state;
  int unknown_count;

  GArray *key_array;
  GArray *output_array;
  CobiwmOutputKey key;
  CobiwmOutputConfig output;

  char *output_field;
} ConfigParser;

static void
handle_start_element (GMarkupParseContext  *context,
                      const char           *element_name,
                      const char          **attribute_names,
                      const char          **attribute_values,
                      gpointer              user_data,
                      GError              **error)
{
  ConfigParser *parser = user_data;

  switch (parser->state)
    {
    case STATE_INITIAL:
      {
        char *version;

        if (strcmp (element_name, "monitors") != 0)
          {
            g_set_error (error, G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ELEMENT,
                         "Invalid document element %s", element_name);
            return;
          }

        if (!g_markup_collect_attributes (element_name, attribute_names, attribute_values,
                                          error,
                                          G_MARKUP_COLLECT_STRING, "version", &version,
                                          G_MARKUP_COLLECT_INVALID))
          return;

        if (strcmp (version, "1") != 0)
          {
            g_set_error (error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                         "Invalid or unsupported version %s", version);
            return;
          }

        parser->state = STATE_MONITORS;
        return;
      }

    case STATE_MONITORS:
      {
        if (strcmp (element_name, "configuration") != 0)
          {
            g_set_error (error, G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ELEMENT,
                         "Invalid toplevel element %s", element_name);
            return;
          }

        parser->key_array = g_array_new (FALSE, FALSE, sizeof (CobiwmOutputKey));
        parser->output_array = g_array_new (FALSE, FALSE, sizeof (CobiwmOutputConfig));
        parser->state = STATE_CONFIGURATION;
        return;
      }

    case STATE_CONFIGURATION:
      {
        if (strcmp (element_name, "clone") == 0 && parser->unknown_count == 0)
          {
            parser->state = STATE_CLONE;
          }
        else if (strcmp (element_name, "output") == 0 && parser->unknown_count == 0)
          {
            char *name;

            if (!g_markup_collect_attributes (element_name, attribute_names, attribute_values,
                                              error,
                                              G_MARKUP_COLLECT_STRING, "name", &name,
                                              G_MARKUP_COLLECT_INVALID))
              return;

            memset (&parser->key, 0, sizeof (CobiwmOutputKey));
            memset (&parser->output, 0, sizeof (CobiwmOutputConfig));

            parser->key.connector = g_strdup (name);
            parser->state = STATE_OUTPUT;
          }
        else
          {
            parser->unknown_count++;
          }

        return;
      }

    case STATE_OUTPUT:
      {
        if ((strcmp (element_name, "vendor") == 0 ||
             strcmp (element_name, "product") == 0 ||
             strcmp (element_name, "serial") == 0 ||
             strcmp (element_name, "width") == 0 ||
             strcmp (element_name, "height") == 0 ||
             strcmp (element_name, "rate") == 0 ||
             strcmp (element_name, "x") == 0 ||
             strcmp (element_name, "y") == 0 ||
             strcmp (element_name, "rotation") == 0 ||
             strcmp (element_name, "reflect_x") == 0 ||
             strcmp (element_name, "reflect_y") == 0 ||
             strcmp (element_name, "primary") == 0 ||
             strcmp (element_name, "presentation") == 0 ||
             strcmp (element_name, "underscanning") == 0) && parser->unknown_count == 0)
          {
            parser->state = STATE_OUTPUT_FIELD;

            parser->output_field = g_strdup (element_name);
          }
        else
          {
            parser->unknown_count++;
          }

        return;
      }

    case STATE_CLONE:
    case STATE_OUTPUT_FIELD:
      {
        g_set_error (error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                     "Unexpected element %s", element_name);
        return;
      }

    default:
      g_assert_not_reached ();
    }
}

static void
handle_end_element (GMarkupParseContext  *context,
                    const char           *element_name,
                    gpointer              user_data,
                    GError              **error)
{
  ConfigParser *parser = user_data;

  switch (parser->state)
    {
    case STATE_MONITORS:
      {
        parser->state = STATE_INITIAL;
        return;
      }

    case STATE_CONFIGURATION:
      {
        if (strcmp (element_name, "configuration") == 0 && parser->unknown_count == 0)
          {
            CobiwmConfiguration *config = g_slice_new (CobiwmConfiguration);

            g_assert (parser->key_array->len == parser->output_array->len);

            config->n_outputs = parser->key_array->len;
            config->keys = (void*)g_array_free (parser->key_array, FALSE);
            config->outputs = (void*)g_array_free (parser->output_array, FALSE);

            g_hash_table_replace (parser->config->configs, config, config);

            parser->key_array = NULL;
            parser->output_array = NULL;
            parser->state = STATE_MONITORS;
          }
        else
          {
            parser->unknown_count--;

            g_assert (parser->unknown_count >= 0);
          }

        return;
      }

    case STATE_OUTPUT:
      {
        if (strcmp (element_name, "output") == 0 && parser->unknown_count == 0)
          {
            if (parser->key.vendor == NULL ||
                parser->key.product == NULL ||
                parser->key.serial == NULL)
              {
                /* Disconnected output, ignore */
                free_output_key (&parser->key);
              }
            else
              {
                if (parser->output.rect.width == 0 ||
                    parser->output.rect.height == 0)
                  parser->output.enabled = FALSE;
                else
                  parser->output.enabled = TRUE;

                g_array_append_val (parser->key_array, parser->key);
                g_array_append_val (parser->output_array, parser->output);
              }

            memset (&parser->key, 0, sizeof (CobiwmOutputKey));
            memset (&parser->output, 0, sizeof (CobiwmOutputConfig));

            parser->state = STATE_CONFIGURATION;
          }
        else
          {
            parser->unknown_count--;

            g_assert (parser->unknown_count >= 0);
          }

        return;
      }

    case STATE_CLONE:
      {
        parser->state = STATE_CONFIGURATION;
        return;
      }

    case STATE_OUTPUT_FIELD:
      {
        g_free (parser->output_field);
        parser->output_field = NULL;

        parser->state = STATE_OUTPUT;
        return;
      }

    case STATE_INITIAL:
    default:
      g_assert_not_reached ();
    }
}

static void
read_int (const char  *text,
          gsize        text_len,
          gint        *field,
          GError     **error)
{
  char buf[64];
  gint64 v;
  char *end;

  strncpy (buf, text, text_len);
  buf[MIN (63, text_len)] = 0;

  v = g_ascii_strtoll (buf, &end, 10);

  /* Limit reasonable values (actual limits are a lot smaller that these) */
  if (*end || v < 0 || v > G_MAXINT16)
    g_set_error (error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                 "Expected a number, got %s", buf);
  else
    *field = v;
}

static void
read_float (const char  *text,
            gsize        text_len,
            gfloat      *field,
            GError     **error)
{
  char buf[64];
  gfloat v;
  char *end;

  strncpy (buf, text, text_len);
  buf[MIN (63, text_len)] = 0;

  v = g_ascii_strtod (buf, &end);

  /* Limit reasonable values (actual limits are a lot smaller that these) */
  if (*end)
    g_set_error (error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                 "Expected a number, got %s", buf);
  else
    *field = v;
}

static gboolean
read_bool (const char  *text,
           gsize        text_len,
           GError     **error)
{
  if (strncmp (text, "no", text_len) == 0)
    return FALSE;
  else if (strncmp (text, "yes", text_len) == 0)
    return TRUE;
  else
    g_set_error (error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                 "Invalid boolean value %.*s", (int)text_len, text);

  return FALSE;
}

static gboolean
is_all_whitespace (const char *text,
                   gsize       text_len)
{
  gsize i;

  for (i = 0; i < text_len; i++)
    if (!g_ascii_isspace (text[i]))
      return FALSE;

  return TRUE;
}

static void
handle_text (GMarkupParseContext *context,
             const gchar         *text,
             gsize                text_len,
             gpointer             user_data,
             GError             **error)
{
  ConfigParser *parser = user_data;

  switch (parser->state)
    {
    case STATE_MONITORS:
      {
        if (!is_all_whitespace (text, text_len))
          g_set_error (error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                       "Unexpected content at this point");
        return;
      }

    case STATE_CONFIGURATION:
      {
        if (parser->unknown_count == 0)
          {
            if (!is_all_whitespace (text, text_len))
              g_set_error (error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                           "Unexpected content at this point");
          }
        else
          {
            /* Handling unknown element, ignore */
          }

        return;
      }

    case STATE_OUTPUT:
      {
        if (parser->unknown_count == 0)
          {
            if (!is_all_whitespace (text, text_len))
              g_set_error (error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                           "Unexpected content at this point");
          }
        else
          {
            /* Handling unknown element, ignore */
          }
        return;
      }

    case STATE_CLONE:
      {
        /* Ignore the clone flag */
        return;
      }

    case STATE_OUTPUT_FIELD:
      {
        if (strcmp (parser->output_field, "vendor") == 0)
          parser->key.vendor = g_strndup (text, text_len);
        else if (strcmp (parser->output_field, "product") == 0)
          parser->key.product = g_strndup (text, text_len);
        else if (strcmp (parser->output_field, "serial") == 0)
          parser->key.serial = g_strndup (text, text_len);
        else if (strcmp (parser->output_field, "width") == 0)
          read_int (text, text_len, &parser->output.rect.width, error);
        else if (strcmp (parser->output_field, "height") == 0)
          read_int (text, text_len, &parser->output.rect.height, error);
        else if (strcmp (parser->output_field, "rate") == 0)
          read_float (text, text_len, &parser->output.refresh_rate, error);
        else if (strcmp (parser->output_field, "x") == 0)
          read_int (text, text_len, &parser->output.rect.x, error);
        else if (strcmp (parser->output_field, "y") == 0)
          read_int (text, text_len, &parser->output.rect.y, error);
        else if (strcmp (parser->output_field, "rotation") == 0)
          {
            if (strncmp (text, "normal", text_len) == 0)
              parser->output.transform = COBIWM_MONITOR_TRANSFORM_NORMAL;
            else if (strncmp (text, "left", text_len) == 0)
              parser->output.transform = COBIWM_MONITOR_TRANSFORM_90;
            else if (strncmp (text, "upside_down", text_len) == 0)
              parser->output.transform = COBIWM_MONITOR_TRANSFORM_180;
            else if (strncmp (text, "right", text_len) == 0)
              parser->output.transform = COBIWM_MONITOR_TRANSFORM_270;
            else
              g_set_error (error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                           "Invalid rotation type %.*s", (int)text_len, text);
          }
        else if (strcmp (parser->output_field, "reflect_x") == 0)
          parser->output.transform += read_bool (text, text_len, error) ?
            COBIWM_MONITOR_TRANSFORM_FLIPPED : 0;
        else if (strcmp (parser->output_field, "reflect_y") == 0)
          {
            /* FIXME (look at the rotation map in monitor.c) */
            if (read_bool (text, text_len, error))
              g_set_error (error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                           "Y reflection is not supported");
          }
        else if (strcmp (parser->output_field, "primary") == 0)
          parser->output.is_primary = read_bool (text, text_len, error);
        else if (strcmp (parser->output_field, "presentation") == 0)
          parser->output.is_presentation = read_bool (text, text_len, error);
        else if (strcmp (parser->output_field, "underscanning") == 0)
          parser->output.is_underscanning = read_bool (text, text_len, error);
        else
          g_assert_not_reached ();
        return;
      }

    case STATE_INITIAL:
    default:
      g_assert_not_reached ();
    }
}

static const GMarkupParser config_parser = {
  .start_element = handle_start_element,
  .end_element = handle_end_element,
  .text = handle_text,
};

static gboolean
load_config_file (CobiwmMonitorConfig *self, GFile *file)
{
  char *contents;
  gsize size;
  gboolean ok;
  GError *error;
  GMarkupParseContext *context;
  ConfigParser parser;

  /* Note: we're explicitly loading this file synchronously because
     we don't want to leave the default configuration on for even a frame, ie we
     want atomic modeset as much as possible.

     This function is called only at early initialization anyway, before
     we connect to X or create the wayland socket.
  */

  error = NULL;
  ok = g_file_load_contents (file, NULL, &contents, &size, NULL, &error);
  if (!ok)
    {

      g_error_free (error);
      return FALSE;
    }

  memset (&parser, 0, sizeof (ConfigParser));
  parser.config = self;
  parser.state = STATE_INITIAL;

  context = g_markup_parse_context_new (&config_parser,
                                        G_MARKUP_TREAT_CDATA_AS_TEXT |
                                        G_MARKUP_PREFIX_ERROR_POSITION,
                                        &parser, NULL);
  ok = g_markup_parse_context_parse (context, contents, size, &error);

  if (!ok)
    {
      cobiwm_warning ("Failed to parse stored monitor configuration: %s\n", error->message);

      g_error_free (error);

      if (parser.key_array)
        g_array_free (parser.key_array, TRUE);
      if (parser.output_array)
        g_array_free (parser.output_array, TRUE);

      free_output_key (&parser.key);
    }

  g_markup_parse_context_free (context);
  g_free (contents);

  return ok;
}

static void
cobiwm_monitor_config_load (CobiwmMonitorConfig *self)
{
  if (self->user_file && load_config_file (self, self->user_file))
    return;
  if (self->system_file && load_config_file (self, self->system_file))
    return;
}

CobiwmMonitorConfig *
cobiwm_monitor_config_new (void)
{
  CobiwmMonitorConfig *self;

  self = g_object_new (COBIWM_TYPE_MONITOR_CONFIG, NULL);
  cobiwm_monitor_config_load (self);

  return self;
}

static void
init_key_from_output (CobiwmOutputKey *key,
                      CobiwmOutput    *output)
{
  key->connector = g_strdup (output->name);
  key->product = g_strdup (output->product);
  key->vendor = g_strdup (output->vendor);
  key->serial = g_strdup (output->serial);
}

static void
make_config_key (CobiwmConfiguration *key,
                 CobiwmOutput        *outputs,
                 unsigned           n_outputs,
                 unsigned           skip)
{
  unsigned int o, i;

  key->outputs = NULL;
  key->keys = g_new0 (CobiwmOutputKey, n_outputs);

  for (o = 0, i = 0; i < n_outputs; o++, i++)
    if (i == skip)
      o--;
    else
      init_key_from_output (&key->keys[o], &outputs[i]);

  key->n_outputs = o;
}

gboolean
cobiwm_monitor_manager_has_hotplug_mode_update (CobiwmMonitorManager *manager)
{
  CobiwmOutput *outputs;
  unsigned n_outputs;
  unsigned int i;

  outputs = cobiwm_monitor_manager_get_outputs (manager, &n_outputs);

  for (i = 0; i < n_outputs; i++)
    if (outputs[i].hotplug_mode_update)
      return TRUE;

  return FALSE;
}

static CobiwmConfiguration *
cobiwm_monitor_config_get_stored (CobiwmMonitorConfig *self,
				CobiwmOutput        *outputs,
				unsigned           n_outputs)
{
  CobiwmConfiguration key;
  CobiwmConfiguration *stored;

  if (n_outputs == 0)
    return NULL;

  make_config_key (&key, outputs, n_outputs, -1);
  stored = g_hash_table_lookup (self->configs, &key);

  config_clear (&key);
  return stored;
}

static void
set_current (CobiwmMonitorConfig *self,
             CobiwmConfiguration *config)
{
  g_clear_pointer (&self->previous, (GDestroyNotify) config_unref);
  self->previous = self->current;
  self->current = config_ref (config);
}

static gboolean
apply_configuration (CobiwmMonitorConfig  *self,
                     CobiwmConfiguration  *config,
		     CobiwmMonitorManager *manager)
{
  g_autoptr(GPtrArray) crtcs = NULL;
  g_autoptr(GPtrArray) outputs = NULL;

  crtcs = g_ptr_array_new_full (config->n_outputs, (GDestroyNotify)cobiwm_crtc_info_free);
  outputs = g_ptr_array_new_full (config->n_outputs, (GDestroyNotify)cobiwm_output_info_free);

  if (!cobiwm_monitor_config_assign_crtcs (config, manager, crtcs, outputs))
    return FALSE;

  cobiwm_monitor_manager_apply_configuration (manager,
                                            (CobiwmCRTCInfo**)crtcs->pdata, crtcs->len,
                                            (CobiwmOutputInfo**)outputs->pdata, outputs->len);

  set_current (self, config);

  /* If true, we'll be overridden at the end of this call
   * inside turn_off_laptop_display / apply_configuration_with_lid */
  self->current_is_for_laptop_lid = FALSE;

  return TRUE;
}

static gboolean
key_is_laptop (CobiwmOutputKey *key)
{
  /* FIXME: extend with better heuristics */
  return g_str_has_prefix (key->connector, "LVDS") ||
    g_str_has_prefix (key->connector, "lvds") ||
    g_str_has_prefix (key->connector, "Lvds") ||
    g_str_has_prefix (key->connector, "LCD")  || /* some versions of fglrx, sigh */
    g_str_has_prefix (key->connector, "DSI") ||
    g_str_has_prefix (key->connector, "eDP");
}

static gboolean
laptop_display_is_on (CobiwmConfiguration *config)
{
  unsigned int i;

  for (i = 0; i < config->n_outputs; i++)
    {
      CobiwmOutputKey *key = &config->keys[i];
      CobiwmOutputConfig *output = &config->outputs[i];

      if (key_is_laptop (key) && output->enabled)
        return TRUE;
    }

  return FALSE;
}

static gboolean
multiple_outputs_are_enabled (CobiwmConfiguration *config)
{
  unsigned int i, enabled;

  enabled = 0;
  for (i = 0; i < config->n_outputs; i++)
    if (config->outputs[i].enabled)
      enabled++;

  return enabled > 1;
}

static CobiwmConfiguration *
make_laptop_lid_config (CobiwmConfiguration  *reference)
{
  CobiwmConfiguration *new;
  unsigned int i;
  gboolean has_primary;
  int x_after, y_after;
  int x_offset, y_offset;

  g_assert (multiple_outputs_are_enabled (reference));

  new = config_new ();
  new->n_outputs = reference->n_outputs;
  new->keys = g_new0 (CobiwmOutputKey, reference->n_outputs);
  new->outputs = g_new0 (CobiwmOutputConfig, reference->n_outputs);

  x_after = G_MAXINT; y_after = G_MAXINT;
  x_offset = 0; y_offset = 0;
  for (i = 0; i < new->n_outputs; i++)
    {
      CobiwmOutputKey *current_key = &reference->keys[i];
      CobiwmOutputConfig *current_output = &reference->outputs[i];

      new->keys[i].connector = g_strdup (current_key->connector);
      new->keys[i].vendor = g_strdup (current_key->vendor);
      new->keys[i].product = g_strdup (current_key->product);
      new->keys[i].serial = g_strdup (current_key->serial);

      if (key_is_laptop (current_key))
        {
          new->outputs[i].enabled = FALSE;
          x_after = current_output->rect.x;
          y_after = current_output->rect.y;
          x_offset = current_output->rect.width;
          y_offset = current_output->rect.height;
        }
      else
        new->outputs[i] = *current_output;
    }
  for (i = 0; i < new->n_outputs; i++)
    {
      if (new->outputs[i].enabled)
        {
          if (new->outputs[i].rect.x > x_after)
            new->outputs[i].rect.x -= x_offset;
          if (new->outputs[i].rect.y > y_after)
            new->outputs[i].rect.y -= y_offset;
        }
    }

  has_primary = FALSE;
  for (i = 0; i < new->n_outputs; i++)
    {
      if (new->outputs[i].is_primary)
        {
          has_primary = TRUE;
          break;
        }
    }
  if (!has_primary)
    new->outputs[0].is_primary = TRUE;

  return new;
}

static gboolean
apply_configuration_with_lid (CobiwmMonitorConfig  *self,
                              CobiwmConfiguration  *config,
                              CobiwmMonitorManager *manager)
{
  if (self->lid_is_closed &&
      multiple_outputs_are_enabled (config) &&
      laptop_display_is_on (config))
    {
      CobiwmConfiguration *laptop_lid_config = make_laptop_lid_config (config);
      if (apply_configuration (self, laptop_lid_config, manager))
        {
          self->current_is_for_laptop_lid = TRUE;
          config_unref (laptop_lid_config);
          return TRUE;
        }
      else
        {
          config_unref (laptop_lid_config);
          return FALSE;
        }
    }
  else
    return apply_configuration (self, config, manager);
}

gboolean
cobiwm_monitor_config_get_is_builtin_display_on (CobiwmMonitorConfig *self)
{
  g_return_val_if_fail (COBIWM_IS_MONITOR_CONFIG (self), FALSE);

  if (self->current)
    return laptop_display_is_on (self->current);

  return FALSE;
}

gboolean
cobiwm_monitor_config_apply_stored (CobiwmMonitorConfig  *self,
				  CobiwmMonitorManager *manager)
{
  CobiwmOutput *outputs;
  CobiwmConfiguration *stored;
  unsigned n_outputs;

  outputs = cobiwm_monitor_manager_get_outputs (manager, &n_outputs);
  stored = cobiwm_monitor_config_get_stored (self, outputs, n_outputs);

  if (stored)
    return apply_configuration_with_lid (self, stored, manager);
  else
    return FALSE;
}

/*
 * Tries to find the primary output according to the current layout,
 * or failing that, an output that is good to be a primary (LVDS or eDP,
 * which are internal monitors), or failing that, the one with the
 * best resolution
 */
static int
find_primary_output (CobiwmOutput *outputs,
                     unsigned    n_outputs)
{
  unsigned i;
  int best;
  int best_width, best_height;

  g_assert (n_outputs >= 1);

  for (i = 0; i < n_outputs; i++)
    {
      if (outputs[i].is_primary)
        return i;
    }

  for (i = 0; i < n_outputs; i++)
    {
      if (cobiwm_output_is_laptop (&outputs[i]))
        return i;
    }

  best = -1;
  best_width = 0; best_height = 0;
  for (i = 0; i < n_outputs; i++)
    {
      if (outputs[i].preferred_mode->width * outputs[i].preferred_mode->height >
          best_width * best_height)
        {
          best = i;
          best_width = outputs[i].preferred_mode->width;
          best_height = outputs[i].preferred_mode->height;
        }
    }

  return best;
}

static void
init_config_from_preferred_mode (CobiwmOutputConfig *config,
                                 CobiwmOutput *output)
{
  config->enabled = TRUE;
  config->rect.x = 0;
  config->rect.y = 0;
  config->rect.width = output->preferred_mode->width;
  config->rect.height = output->preferred_mode->height;
  config->refresh_rate = output->preferred_mode->refresh_rate;
  config->transform = COBIWM_MONITOR_TRANSFORM_NORMAL;
  config->is_primary = FALSE;
  config->is_presentation = FALSE;
}

/* This function handles configuring the outputs when the driver provides a
 * suggested layout position for each output. This is done in recent versions
 * of qxl and allows displays to be aligned on the guest in the same order as
 * they are aligned on the client.
 */
static gboolean
make_suggested_config (CobiwmMonitorConfig *self,
                       CobiwmOutput        *outputs,
                       unsigned           n_outputs,
                       int                max_width,
                       int                max_height,
                       CobiwmConfiguration *config)
{
  unsigned int i;
  int primary;
  GList *region = NULL;

  g_return_val_if_fail (config != NULL, FALSE);
  primary = find_primary_output (outputs, n_outputs);

  for (i = 0; i < n_outputs; i++)
    {
      gboolean is_primary = ((int)i == primary);

      if (outputs[i].suggested_x < 0 || outputs[i].suggested_y < 0)
          return FALSE;

      init_config_from_preferred_mode (&config->outputs[i], &outputs[i]);
      config->outputs[i].is_primary = is_primary;

      config->outputs[i].rect.x = outputs[i].suggested_x;
      config->outputs[i].rect.y = outputs[i].suggested_y;

      /* Reject the configuration if the suggested positions result in
       * overlapping displays */
      if (cobiwm_rectangle_overlaps_with_region (region, &config->outputs[i].rect))
        {
          g_warning ("Overlapping outputs, rejecting suggested configuration");
          g_list_free (region);
          return FALSE;
        }

      region = g_list_prepend (region, &config->outputs[i].rect);
    }

  g_list_free (region);
  return TRUE;
}

static void
config_one_untiled_output (CobiwmOutput *outputs,
                           CobiwmConfiguration *config,
                           int idx, gboolean is_primary,
                           int *x, unsigned long *output_configured_bitmap)
{
  CobiwmOutput *output = &outputs[idx];

  if (*output_configured_bitmap & (1 << idx))
    return;

  init_config_from_preferred_mode (&config->outputs[idx], output);
  config->outputs[idx].is_primary = is_primary;
  config->outputs[idx].rect.x = *x;
  *x += config->outputs[idx].rect.width;
  *output_configured_bitmap |= (1 << idx);
}

static void
config_one_tiled_group (CobiwmOutput *outputs,
                        CobiwmConfiguration *config,
                        int base_idx, gboolean is_primary,
                        int n_outputs,
                        int *x, unsigned long *output_configured_bitmap)
{
  guint32 num_h_tile, num_v_tile, ht, vt;
  int j;
  int cur_x, cur_y, addx = 0;

  if (*output_configured_bitmap & (1 << base_idx))
      return;

  if (outputs[base_idx].tile_info.group_id == 0)
    return;

  cur_x = cur_y = 0;
  num_h_tile = outputs[base_idx].tile_info.max_h_tiles;
  num_v_tile = outputs[base_idx].tile_info.max_v_tiles;

  /* iterate over horizontal tiles */
  cur_x = *x;
  for (ht = 0; ht < num_h_tile; ht++)
    {
      cur_y = 0;
      addx = 0;
      for (vt = 0; vt < num_v_tile; vt++)
        {
          for (j = 0; j < n_outputs; j++)
            {
              if (outputs[j].tile_info.group_id != outputs[base_idx].tile_info.group_id)
                continue;

              if (outputs[j].tile_info.loc_h_tile != ht ||
                  outputs[j].tile_info.loc_v_tile != vt)
                continue;

              if (ht == 0 && vt == 0 && is_primary)
                config->outputs[j].is_primary = TRUE;

              init_config_from_preferred_mode (&config->outputs[j], &outputs[j]);
              config->outputs[j].rect.x = cur_x;
              config->outputs[j].rect.y = cur_y;

              *output_configured_bitmap |= (1 << j);
              cur_y += outputs[j].tile_info.tile_h;
              if (vt == 0)
                addx += outputs[j].tile_info.tile_w;
            }
        }
      cur_x += addx;
    }
  *x = cur_x;

}

static void
make_linear_config (CobiwmMonitorConfig *self,
                    CobiwmOutput        *outputs,
                    unsigned           n_outputs,
                    int                max_width,
                    int                max_height,
                    CobiwmConfiguration *config)
{
  unsigned long output_configured_bitmap = 0;
  unsigned i;
  int x;
  int primary;

  g_return_if_fail (config != NULL);

  primary = find_primary_output (outputs, n_outputs);

  x = 0;
  /* set the primary up first at 0 */
  if (outputs[primary].tile_info.group_id)
    {
      config_one_tiled_group (outputs, config, primary, TRUE, n_outputs,
                              &x, &output_configured_bitmap);
    }
  else
    {
      config_one_untiled_output (outputs, config, primary, TRUE,
                                 &x, &output_configured_bitmap);
    }

  /* then add other tiled monitors */
  for (i = 0; i < n_outputs; i++)
    {
      config_one_tiled_group (outputs, config, i, FALSE, n_outputs,
                              &x, &output_configured_bitmap);
    }

  /* then add remaining monitors */
  for (i = 0; i < n_outputs; i++)
    {
      config_one_untiled_output (outputs, config, i, FALSE,
                                 &x, &output_configured_bitmap);

    }
}

/* Search for a configuration that includes one less screen, then add the new
 * one as a presentation screen in preferred mode.
 *
 * XXX: but presentation mode is not implemented in the control-center or in
 * cobiwm core, so let's do extended for now.
  */
static gboolean
extend_stored_config (CobiwmMonitorConfig *self,
                      CobiwmOutput        *outputs,
                      unsigned           n_outputs,
                      int                max_width,
                      int                max_height,
                      CobiwmConfiguration *config)
{
  int x, y;
  unsigned i, j;

  x = 0;
  y = 0;
  for (i = 0; i < n_outputs; i++)
    {
      CobiwmConfiguration key;
      CobiwmConfiguration *ref;

      make_config_key (&key, outputs, n_outputs, i);
      ref = g_hash_table_lookup (self->configs, &key);
      config_clear (&key);

      if (ref)
        {
          for (j = 0; j < n_outputs; j++)
            {
              if (j < i)
                {
                  g_assert (output_key_equal (&config->keys[j], &ref->keys[j]));
                  config->outputs[j] = ref->outputs[j];
                  x = MAX (x, ref->outputs[j].rect.x + ref->outputs[j].rect.width);
                  y = MAX (y, ref->outputs[j].rect.y + ref->outputs[j].rect.height);
                }
              else if (j > i)
                {
                  g_assert (output_key_equal (&config->keys[j], &ref->keys[j - 1]));
                  config->outputs[j] = ref->outputs[j - 1];
                  x = MAX (x, ref->outputs[j - 1].rect.x + ref->outputs[j - 1].rect.width);
                  y = MAX (y, ref->outputs[j - 1].rect.y + ref->outputs[j - 1].rect.height);
                }
              else
                {
                  init_config_from_preferred_mode (&config->outputs[j], &outputs[0]);
                }
            }

          /* Place the new output at the right end of the screen, if it fits,
             otherwise below it, otherwise disable it (or apply_configuration will fail) */
          if (x + config->outputs[i].rect.width <= max_width)
            config->outputs[i].rect.x = x;
          else if (y + config->outputs[i].rect.height <= max_height)
            config->outputs[i].rect.y = y;
          else
            config->outputs[i].enabled = FALSE;

          return TRUE;
        }
    }

  return FALSE;
}

static CobiwmConfiguration *
make_default_config (CobiwmMonitorConfig *self,
                     CobiwmOutput        *outputs,
                     unsigned           n_outputs,
                     int                max_width,
                     int                max_height,
                     gboolean           use_stored_config)
{
  CobiwmConfiguration *ret = NULL;
  unsigned i;

  ret = config_new ();
  make_config_key (ret, outputs, n_outputs, -1);
  ret->outputs = g_new0 (CobiwmOutputConfig, n_outputs);

  /* Special case the simple case: one output, primary at preferred mode,
     nothing else to do */
  if (n_outputs == 1)
    {
      init_config_from_preferred_mode (&ret->outputs[0], &outputs[0]);
      ret->outputs[0].is_primary = TRUE;
      goto check_limits;
    }

  if (make_suggested_config (self, outputs, n_outputs, max_width, max_height, ret))
      goto check_limits;

  if (use_stored_config &&
      extend_stored_config (self, outputs, n_outputs, max_width, max_height, ret))
      goto check_limits;

  make_linear_config (self, outputs, n_outputs, max_width, max_height, ret);

check_limits:
  /* Disable outputs that would go beyond framebuffer limits */
  for (i = 0; i < n_outputs; i++)
    {
        if ((ret->outputs[i].rect.x + ret->outputs[i].rect.width > max_width)
            || (ret->outputs[i].rect.y + ret->outputs[i].rect.height > max_height))
          ret->outputs[i].enabled = FALSE;
    }

  return ret;
}

static gboolean
ensure_at_least_one_output (CobiwmMonitorConfig  *self,
                            CobiwmMonitorManager *manager,
                            CobiwmOutput         *outputs,
                            unsigned            n_outputs)
{
  CobiwmConfiguration *config;
  int primary;
  unsigned i;

  /* Check that we have at least one active output */
  for (i = 0; i < n_outputs; i++)
    if (outputs[i].crtc != NULL)
      return TRUE;

  /* Oh no, we don't! Activate the primary one and disable everything else */

  config = config_new ();
  make_config_key (config, outputs, n_outputs, -1);
  config->outputs = g_new0 (CobiwmOutputConfig, n_outputs);

  primary = find_primary_output (outputs, n_outputs);

  for (i = 0; i < n_outputs; i++)
    {
      gboolean is_primary = ((int)i == primary);

      if (is_primary)
        {
          init_config_from_preferred_mode (&config->outputs[i], &outputs[0]);
          config->outputs[i].is_primary = TRUE;
        }
      else
        {
          config->outputs[i].enabled = FALSE;
        }
    }

  apply_configuration (self, config, manager);
  config_unref (config);
  return FALSE;
}

void
cobiwm_monitor_config_make_default (CobiwmMonitorConfig  *self,
				  CobiwmMonitorManager *manager)
{
  CobiwmOutput *outputs;
  CobiwmConfiguration *default_config;
  unsigned n_outputs;
  gboolean ok = FALSE;
  int max_width, max_height;
  gboolean use_stored_config;

  outputs = cobiwm_monitor_manager_get_outputs (manager, &n_outputs);
  cobiwm_monitor_manager_get_screen_limits (manager, &max_width, &max_height);

  if (n_outputs == 0)
    {
      cobiwm_verbose ("No output connected, not applying configuration\n");
      return;
    }

  /* if the device has hotplug_mode_update, it's possible that the
   * current display configuration does not match a stored configuration.
   * Since extend_existing_config() tries to build a configuration that is
   * based on a previously-stored configuration, it's quite likely that the
   * resulting config will fail. Even if it doesn't fail, it may result in
   * an unexpected configuration, so don't attempt to use a stored config
   * in this situation. */
  use_stored_config = !cobiwm_monitor_manager_has_hotplug_mode_update (manager);
  default_config = make_default_config (self, outputs, n_outputs, max_width, max_height, use_stored_config);

  if (default_config != NULL)
    {
      ok = apply_configuration_with_lid (self, default_config, manager);
      config_unref (default_config);
    }

  if (!ok)
    {
      cobiwm_warning ("Could not make default configuration for current output layout, leaving unconfigured\n");
      if (ensure_at_least_one_output (self, manager, outputs, n_outputs))
        cobiwm_monitor_config_update_current (self, manager);
    }
}

static void
init_config_from_output (CobiwmOutputConfig *config,
                         CobiwmOutput       *output)
{
  config->enabled = (output->crtc != NULL);

  if (!config->enabled)
    return;

  config->rect = output->crtc->rect;
  config->refresh_rate = output->crtc->current_mode->refresh_rate;
  config->transform = output->crtc->transform;
  config->is_primary = output->is_primary;
  config->is_presentation = output->is_presentation;
  config->is_underscanning = output->is_underscanning;
}

void
cobiwm_monitor_config_update_current (CobiwmMonitorConfig  *self,
                                    CobiwmMonitorManager *manager)
{
  CobiwmOutput *outputs;
  unsigned n_outputs;
  CobiwmConfiguration *current;
  unsigned int i;

  outputs = cobiwm_monitor_manager_get_outputs (manager, &n_outputs);

  current = config_new ();
  current->n_outputs = n_outputs;
  current->outputs = g_new0 (CobiwmOutputConfig, n_outputs);
  current->keys = g_new0 (CobiwmOutputKey, n_outputs);

  for (i = 0; i < current->n_outputs; i++)
    {
      init_key_from_output (&current->keys[i], &outputs[i]);
      init_config_from_output (&current->outputs[i], &outputs[i]);
    }

  if (self->current && config_equal_full (current, self->current))
    {
      config_unref (current);
      return;
    }

  set_current (self, current);
}

void
cobiwm_monitor_config_restore_previous (CobiwmMonitorConfig  *self,
                                      CobiwmMonitorManager *manager)
{
  if (self->previous)
    {
      /* The user chose to restore the previous configuration. In this
       * case, restore the previous configuration. */
      CobiwmConfiguration *prev_config = config_ref (self->previous);
      gboolean ok = apply_configuration (self, prev_config, manager);
      config_unref (prev_config);

      /* After this, self->previous contains the rejected configuration.
       * Since it was rejected, nuke it. */
      g_clear_pointer (&self->previous, (GDestroyNotify) config_unref);

      if (ok)
        return;
    }

  if (!cobiwm_monitor_config_apply_stored (self, manager))
    cobiwm_monitor_config_make_default (self, manager);
}

static void
turn_off_laptop_display (CobiwmMonitorConfig  *self,
                         CobiwmMonitorManager *manager)
{
  CobiwmConfiguration *new;

  if (!multiple_outputs_are_enabled (self->current))
    return;

  new = make_laptop_lid_config (self->current);
  apply_configuration (self, new, manager);
  config_unref (new);
  self->current_is_for_laptop_lid = TRUE;
}

static void
power_client_changed_cb (UpClient   *client,
                         GParamSpec *pspec,
                         gpointer    user_data)
{
  CobiwmMonitorManager *manager = cobiwm_monitor_manager_get ();
  CobiwmMonitorConfig *self = user_data;
  gboolean is_closed;

  is_closed = up_client_get_lid_is_closed (self->up_client);

  if (is_closed != self->lid_is_closed)
    {
      self->lid_is_closed = is_closed;

      if (is_closed)
        turn_off_laptop_display (self, manager);
      else if (self->current_is_for_laptop_lid)
        cobiwm_monitor_config_restore_previous (self, manager);
    }
}

typedef struct {
  CobiwmMonitorConfig *config;
  GString *buffer;
} SaveClosure;

static void
saved_cb (GObject      *object,
          GAsyncResult *result,
          gpointer      user_data)
{
  SaveClosure *closure = user_data;
  GError *error;
  gboolean ok;

  error = NULL;
  ok = g_file_replace_contents_finish (G_FILE (object), result, NULL, &error);
  if (!ok)
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        cobiwm_warning ("Saving monitor configuration failed: %s\n", error->message);

      g_error_free (error);
    }

  g_clear_object (&closure->config->save_cancellable);
  g_object_unref (closure->config);
  g_string_free (closure->buffer, TRUE);

  g_slice_free (SaveClosure, closure);
}

static void
cobiwm_monitor_config_save (CobiwmMonitorConfig *self)
{
  static const char * const rotation_map[4] = {
    "normal",
    "left",
    "upside_down",
    "right"
  };
  SaveClosure *closure;
  GString *buffer;
  GHashTableIter iter;
  CobiwmConfiguration *config;
  unsigned int i;

  if (self->save_cancellable)
    {
      g_cancellable_cancel (self->save_cancellable);
      g_object_unref (self->save_cancellable);
      self->save_cancellable = NULL;
    }

  self->save_cancellable = g_cancellable_new ();

  buffer = g_string_new ("<monitors version=\"1\">\n");

  g_hash_table_iter_init (&iter, self->configs);
  while (g_hash_table_iter_next (&iter, (gpointer*) &config, NULL))
    {
      /* Note: we don't distinguish clone vs non-clone here, that's
         something for the UI (ie gnome-control-center) to handle,
         and our configurations are more complex anyway.
      */

      g_string_append (buffer,
                       "  <configuration>\n"
                       "    <clone>no</clone>\n");

      for (i = 0; i < config->n_outputs; i++)
        {
          CobiwmOutputKey *key = &config->keys[i];
          CobiwmOutputConfig *output = &config->outputs[i];

          g_string_append_printf (buffer,
                                  "    <output name=\"%s\">\n"
                                  "      <vendor>%s</vendor>\n"
                                  "      <product>%s</product>\n"
                                  "      <serial>%s</serial>\n",
                                  key->connector, key->vendor,
                                  key->product, key->serial);

          if (output->enabled)
            {
              char refresh_rate[G_ASCII_DTOSTR_BUF_SIZE];

              g_ascii_dtostr (refresh_rate, sizeof (refresh_rate), output->refresh_rate);
              g_string_append_printf (buffer,
                                      "      <width>%d</width>\n"
                                      "      <height>%d</height>\n"
                                      "      <rate>%s</rate>\n"
                                      "      <x>%d</x>\n"
                                      "      <y>%d</y>\n"
                                      "      <rotation>%s</rotation>\n"
                                      "      <reflect_x>%s</reflect_x>\n"
                                      "      <reflect_y>no</reflect_y>\n"
                                      "      <primary>%s</primary>\n"
                                      "      <presentation>%s</presentation>\n"
                                      "      <underscanning>%s</underscanning>\n",
                                      output->rect.width,
                                      output->rect.height,
                                      refresh_rate,
                                      output->rect.x,
                                      output->rect.y,
                                      rotation_map[output->transform & 0x3],
                                      output->transform >= COBIWM_MONITOR_TRANSFORM_FLIPPED ? "yes" : "no",
                                      output->is_primary ? "yes" : "no",
                                      output->is_presentation ? "yes" : "no",
                                      output->is_underscanning ? "yes" : "no");
            }

          g_string_append (buffer, "    </output>\n");
        }

      g_string_append (buffer, "  </configuration>\n");
    }

  g_string_append (buffer, "</monitors>\n");

  closure = g_slice_new (SaveClosure);
  closure->config = g_object_ref (self);
  closure->buffer = buffer;

  g_file_replace_contents_async (self->user_file,
                                 buffer->str, buffer->len,
                                 NULL, /* etag */
                                 TRUE,
                                 G_FILE_CREATE_REPLACE_DESTINATION,
                                 self->save_cancellable,
                                 saved_cb, closure);
}

void
cobiwm_monitor_config_make_persistent (CobiwmMonitorConfig *self)
{
  g_hash_table_replace (self->configs, self->current, config_ref (self->current));
  cobiwm_monitor_config_save (self);
}

/*
 * CRTC assignment
 */
typedef struct
{
  CobiwmConfiguration  *config;
  CobiwmMonitorManager *manager;
  GHashTable         *info;
} CrtcAssignment;

static gboolean
output_can_clone (CobiwmOutput *output,
                  CobiwmOutput *clone)
{
  unsigned int i;

  for (i = 0; i < output->n_possible_clones; i++)
    if (output->possible_clones[i] == clone)
      return TRUE;

  return FALSE;
}

static gboolean
can_clone (CobiwmCRTCInfo *info,
	   CobiwmOutput   *output)
{
  unsigned int i;

  for (i = 0; i < info->outputs->len; ++i)
    {
      CobiwmOutput *clone = info->outputs->pdata[i];

	if (!output_can_clone (clone, output))
	    return FALSE;
    }

    return TRUE;
}

static gboolean
crtc_can_drive_output (CobiwmCRTC   *crtc,
                       CobiwmOutput *output)
{
  unsigned int i;

  for (i = 0; i < output->n_possible_crtcs; i++)
    if (output->possible_crtcs[i] == crtc)
      return TRUE;

  return FALSE;
}

static gboolean
output_supports_mode (CobiwmOutput      *output,
                      CobiwmMonitorMode *mode)
{
  unsigned int i;

  for (i = 0; i < output->n_modes; i++)
    if (output->modes[i] == mode)
      return TRUE;

  return FALSE;
}

static gboolean
crtc_assignment_assign (CrtcAssignment       *assign,
			CobiwmCRTC             *crtc,
			CobiwmMonitorMode      *mode,
			int                   x,
			int                   y,
			CobiwmMonitorTransform  transform,
			CobiwmOutput           *output)
{
  CobiwmCRTCInfo *info = g_hash_table_lookup (assign->info, crtc);

  if (!crtc_can_drive_output (crtc, output))
    return FALSE;

  if (!output_supports_mode (output, mode))
    return FALSE;

  if ((crtc->all_transforms & (1 << transform)) == 0)
    return FALSE;

  if (info)
    {
      if (!(info->mode == mode	&&
            info->x == x		&&
            info->y == y		&&
            info->transform == transform))
        return FALSE;

      if (!can_clone (info, output))
        return FALSE;

      g_ptr_array_add (info->outputs, output);
      return TRUE;
    }
  else
    {
      info = g_slice_new0 (CobiwmCRTCInfo);

      info->crtc = crtc;
      info->mode = mode;
      info->x = x;
      info->y = y;
      info->transform = transform;
      info->outputs = g_ptr_array_new ();

      g_ptr_array_add (info->outputs, output);
      g_hash_table_insert (assign->info, crtc, info);

      return TRUE;
    }
}

static void
crtc_assignment_unassign (CrtcAssignment *assign,
                          CobiwmCRTC       *crtc,
                          CobiwmOutput     *output)
{
  CobiwmCRTCInfo *info = g_hash_table_lookup (assign->info, crtc);

  if (info)
    {
      g_ptr_array_remove (info->outputs, output);

      if (info->outputs->len == 0)
        g_hash_table_remove (assign->info, crtc);
    }
}

static CobiwmOutput *
find_output_by_key (CobiwmOutput    *outputs,
                    unsigned int   n_outputs,
                    CobiwmOutputKey *key)
{
  unsigned int i;

  for (i = 0; i < n_outputs; i++)
    {
      if (strcmp (outputs[i].name, key->connector) == 0)
        {
          /* This should be checked a lot earlier! */

          g_warn_if_fail (strcmp (outputs[i].vendor, key->vendor) == 0 &&
                          strcmp (outputs[i].product, key->product) == 0 &&
                          strcmp (outputs[i].serial, key->serial) == 0);
          return &outputs[i];
        }
    }

  /* Just to satisfy GCC - this is a fatal error if occurs */
  return NULL;
}

/* Check whether the given set of settings can be used
 * at the same time -- ie. whether there is an assignment
 * of CRTC's to outputs.
 *
 * Brute force - the number of objects involved is small
 * enough that it doesn't matter.
 */
static gboolean
real_assign_crtcs (CrtcAssignment     *assignment,
                   unsigned int        output_num)
{
  CobiwmMonitorMode *modes;
  CobiwmCRTC *crtcs;
  CobiwmOutput *outputs;
  unsigned int n_crtcs, n_modes, n_outputs;
  CobiwmOutputKey *output_key;
  CobiwmOutputConfig *output_config;
  unsigned int i;

  if (output_num == assignment->config->n_outputs)
    return TRUE;

  output_key = &assignment->config->keys[output_num];
  output_config = &assignment->config->outputs[output_num];

  /* It is always allowed for an output to be turned off */
  if (!output_config->enabled)
    return real_assign_crtcs (assignment, output_num + 1);

  cobiwm_monitor_manager_get_resources (assignment->manager,
                                      &modes, &n_modes,
                                      &crtcs, &n_crtcs,
                                      &outputs, &n_outputs);

  for (i = 0; i < n_crtcs; i++)
    {
      CobiwmCRTC *crtc = &crtcs[i];
      unsigned int pass;

      /* Make two passes, one where frequencies must match, then
       * one where they don't have to
       */
      for (pass = 0; pass < 2; pass++)
	{
          CobiwmOutput *output = find_output_by_key (outputs, n_outputs, output_key);
          unsigned int j;

          for (j = 0; j < n_modes; j++)
	    {
              CobiwmMonitorMode *mode = &modes[j];
              int width, height;

              if (cobiwm_monitor_transform_is_rotated (output_config->transform))
                {
                  width = mode->height;
                  height = mode->width;
                }
              else
                {
                  width = mode->width;
                  height = mode->height;
                }

              if (width == output_config->rect.width &&
                  height == output_config->rect.height &&
                  (pass == 1 || mode->refresh_rate == output_config->refresh_rate))
		{
                  cobiwm_verbose ("CRTC %ld: trying mode %dx%d@%fHz with output at %dx%d@%fHz (transform %d) (pass %d)\n",
                                crtc->crtc_id,
                                mode->width, mode->height, mode->refresh_rate,
                                output_config->rect.width, output_config->rect.height, output_config->refresh_rate,
                                output_config->transform,
                                pass);

                  if (crtc_assignment_assign (assignment, crtc, &modes[j],
                                              output_config->rect.x, output_config->rect.y,
                                              output_config->transform,
                                              output))
                    {
                      if (real_assign_crtcs (assignment, output_num + 1))
                        return TRUE;

                      crtc_assignment_unassign (assignment, crtc, output);
                    }
                }
            }
	}
    }

  return FALSE;
}

static gboolean
cobiwm_monitor_config_assign_crtcs (CobiwmConfiguration  *config,
                                  CobiwmMonitorManager *manager,
                                  GPtrArray          *crtcs,
                                  GPtrArray          *outputs)
{
  CrtcAssignment assignment;
  GHashTableIter iter;
  CobiwmCRTC *crtc;
  CobiwmCRTCInfo *info;
  unsigned int i;
  CobiwmOutput *all_outputs;
  unsigned int n_outputs;

  assignment.config = config;
  assignment.manager = manager;
  assignment.info = g_hash_table_new_full (NULL, NULL, NULL, (GDestroyNotify)cobiwm_crtc_info_free);

  if (!real_assign_crtcs (&assignment, 0))
    {
      cobiwm_warning ("Could not assign CRTC to outputs, ignoring configuration\n");

      g_hash_table_destroy (assignment.info);
      return FALSE;
    }

  g_hash_table_iter_init (&iter, assignment.info);
  while (g_hash_table_iter_next (&iter, (void**)&crtc, (void**)&info))
    {
      g_hash_table_iter_steal (&iter);
      g_ptr_array_add (crtcs, info);
    }

  all_outputs = cobiwm_monitor_manager_get_outputs (manager,
                                                  &n_outputs);
  if (n_outputs != config->n_outputs)
    {
      g_hash_table_destroy (assignment.info);
      return FALSE;
    }

  for (i = 0; i < n_outputs; i++)
    {
      CobiwmOutputInfo *output_info = g_slice_new (CobiwmOutputInfo);
      CobiwmOutputConfig *output_config = &config->outputs[i];

      output_info->output = find_output_by_key (all_outputs, n_outputs,
                                                &config->keys[i]);
      output_info->is_primary = output_config->is_primary;
      output_info->is_presentation = output_config->is_presentation;
      output_info->is_underscanning = output_config->is_underscanning;

      g_ptr_array_add (outputs, output_info);
    }

  g_hash_table_destroy (assignment.info);
  return TRUE;
}

void
cobiwm_crtc_info_free (CobiwmCRTCInfo *info)
{
  g_ptr_array_free (info->outputs, TRUE);
  g_slice_free (CobiwmCRTCInfo, info);
}

void
cobiwm_output_info_free (CobiwmOutputInfo *info)
{
  g_slice_free (CobiwmOutputInfo, info);
}
