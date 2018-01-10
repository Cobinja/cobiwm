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

#include <config.h>

#include <glib-object.h>

#include <errors.h>
#include "display-private.h"
#include "screen-private.h"
#include "startup-notification-private.h"

/* This should be fairly long, as it should never be required unless
 * apps or .desktop files are buggy, and it's confusing if
 * OpenOffice or whatever seems to stop launching - people
 * might decide they need to launch it again.
 */
#define STARTUP_TIMEOUT 15000000

typedef struct _CobiwmStartupNotificationSequence CobiwmStartupNotificationSequence;
typedef struct _CobiwmStartupNotificationSequenceClass CobiwmStartupNotificationSequenceClass;

enum {
  PROP_SN_0,
  PROP_SN_DISPLAY,
  N_SN_PROPS
};

enum {
  PROP_SEQ_0,
  PROP_SEQ_ID,
  PROP_SEQ_TIMESTAMP,
  N_SEQ_PROPS
};

enum {
  SN_CHANGED,
  N_SN_SIGNALS
};

static guint sn_signals[N_SN_SIGNALS];
static GParamSpec *sn_props[N_SN_PROPS];
static GParamSpec *seq_props[N_SEQ_PROPS];

typedef struct
{
  GSList *list;
  gint64 now;
} CollectTimedOutData;

struct _CobiwmStartupNotification
{
  GObject parent_instance;
  CobiwmDisplay *display;

#ifdef HAVE_STARTUP_NOTIFICATION
  SnDisplay *sn_display;
  SnMonitorContext *sn_context;
#endif

  GSList *startup_sequences;
  guint startup_sequence_timeout;
};

#define COBIWM_TYPE_STARTUP_NOTIFICATION_SEQUENCE \
  (cobiwm_startup_notification_sequence_get_type ())

G_DECLARE_DERIVABLE_TYPE (CobiwmStartupNotificationSequence,
                          cobiwm_startup_notification_sequence,
                          COBIWM, STARTUP_NOTIFICATION_SEQUENCE,
                          GObject)

typedef struct {
  gchar *id;
  gint64 timestamp;
} CobiwmStartupNotificationSequencePrivate;

struct _CobiwmStartupNotificationSequenceClass {
  GObjectClass parent_class;

  void (* complete) (CobiwmStartupNotificationSequence *sequence);
};

G_DEFINE_TYPE (CobiwmStartupNotification,
               cobiwm_startup_notification,
               G_TYPE_OBJECT)
G_DEFINE_TYPE_WITH_PRIVATE (CobiwmStartupNotificationSequence,
                            cobiwm_startup_notification_sequence,
                            G_TYPE_OBJECT)

#ifdef HAVE_STARTUP_NOTIFICATION

enum {
  PROP_SEQ_X11_0,
  PROP_SEQ_X11_SEQ,
  N_SEQ_X11_PROPS
};

struct _CobiwmStartupNotificationSequenceX11 {
  CobiwmStartupNotificationSequence parent_instance;
  SnStartupSequence *seq;
};

static GParamSpec *seq_x11_props[N_SEQ_X11_PROPS];

#define COBIWM_TYPE_STARTUP_NOTIFICATION_SEQUENCE_X11 \
  (cobiwm_startup_notification_sequence_x11_get_type ())

G_DECLARE_FINAL_TYPE (CobiwmStartupNotificationSequenceX11,
                      cobiwm_startup_notification_sequence_x11,
                      COBIWM, STARTUP_NOTIFICATION_SEQUENCE_X11,
                      CobiwmStartupNotificationSequence)

G_DEFINE_TYPE (CobiwmStartupNotificationSequenceX11,
               cobiwm_startup_notification_sequence_x11,
               COBIWM_TYPE_STARTUP_NOTIFICATION_SEQUENCE)

static void cobiwm_startup_notification_ensure_timeout  (CobiwmStartupNotification *sn);

#endif

static void
cobiwm_startup_notification_update_feedback (CobiwmStartupNotification *sn)
{
  CobiwmScreen *screen = sn->display->screen;

  if (sn->startup_sequences != NULL)
    {
      cobiwm_topic (COBIWM_DEBUG_STARTUP,
                  "Setting busy cursor\n");
      cobiwm_screen_set_cursor (screen, COBIWM_CURSOR_BUSY);
    }
  else
    {
      cobiwm_topic (COBIWM_DEBUG_STARTUP,
                  "Setting default cursor\n");
      cobiwm_screen_set_cursor (screen, COBIWM_CURSOR_DEFAULT);
    }
}

static void
cobiwm_startup_notification_sequence_init (CobiwmStartupNotificationSequence *seq)
{
}

static void
cobiwm_startup_notification_sequence_finalize (GObject *object)
{
  CobiwmStartupNotificationSequence *seq;
  CobiwmStartupNotificationSequencePrivate *priv;

  seq = COBIWM_STARTUP_NOTIFICATION_SEQUENCE (object);
  priv = cobiwm_startup_notification_sequence_get_instance_private (seq);
  g_free (priv->id);

  G_OBJECT_CLASS (cobiwm_startup_notification_sequence_parent_class)->finalize (object);
}

static void
cobiwm_startup_notification_sequence_set_property (GObject      *object,
                                                 guint         prop_id,
                                                 const GValue *value,
                                                 GParamSpec   *pspec)
{
  CobiwmStartupNotificationSequence *seq;
  CobiwmStartupNotificationSequencePrivate *priv;

  seq = COBIWM_STARTUP_NOTIFICATION_SEQUENCE (object);
  priv = cobiwm_startup_notification_sequence_get_instance_private (seq);

  switch (prop_id)
    {
    case PROP_SEQ_ID:
      priv->id = g_value_dup_string (value);
      break;
    case PROP_SEQ_TIMESTAMP:
      priv->timestamp = g_value_get_int64 (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
cobiwm_startup_notification_sequence_get_property (GObject    *object,
                                                 guint       prop_id,
                                                 GValue     *value,
                                                 GParamSpec *pspec)
{
  CobiwmStartupNotificationSequence *seq;
  CobiwmStartupNotificationSequencePrivate *priv;

  seq = COBIWM_STARTUP_NOTIFICATION_SEQUENCE (object);
  priv = cobiwm_startup_notification_sequence_get_instance_private (seq);

  switch (prop_id)
    {
    case PROP_SEQ_ID:
      g_value_set_string (value, priv->id);
      break;
    case PROP_SEQ_TIMESTAMP:
      g_value_set_int64 (value, priv->timestamp);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
cobiwm_startup_notification_sequence_class_init (CobiwmStartupNotificationSequenceClass *klass)
{
  GObjectClass *object_class;

  object_class = G_OBJECT_CLASS (klass);
  object_class->finalize = cobiwm_startup_notification_sequence_finalize;
  object_class->set_property = cobiwm_startup_notification_sequence_set_property;
  object_class->get_property = cobiwm_startup_notification_sequence_get_property;

  seq_props[PROP_SEQ_ID] =
    g_param_spec_string ("id",
                         "ID",
                         "ID",
                         NULL,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY);
  seq_props[PROP_SEQ_TIMESTAMP] =
    g_param_spec_int64 ("timestamp",
                        "Timestamp",
                        "Timestamp",
                        G_MININT64, G_MAXINT64, 0,
                        G_PARAM_READWRITE |
                        G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_properties (object_class, N_SEQ_PROPS, seq_props);
}

static const gchar *
cobiwm_startup_notification_sequence_get_id (CobiwmStartupNotificationSequence *seq)
{
  CobiwmStartupNotificationSequencePrivate *priv;

  priv = cobiwm_startup_notification_sequence_get_instance_private (seq);
  return priv->id;
}

#ifdef HAVE_STARTUP_NOTIFICATION
static gint64
cobiwm_startup_notification_sequence_get_timestamp (CobiwmStartupNotificationSequence *seq)
{
  CobiwmStartupNotificationSequencePrivate *priv;

  priv = cobiwm_startup_notification_sequence_get_instance_private (seq);
  return priv->timestamp;
}

static void
cobiwm_startup_notification_sequence_complete (CobiwmStartupNotificationSequence *seq)
{
  CobiwmStartupNotificationSequenceClass *klass;

  klass = COBIWM_STARTUP_NOTIFICATION_SEQUENCE_GET_CLASS (seq);

  if (klass->complete)
    klass->complete (seq);
}
#endif

#ifdef HAVE_STARTUP_NOTIFICATION
static void
cobiwm_startup_notification_sequence_x11_complete (CobiwmStartupNotificationSequence *seq)
{
  CobiwmStartupNotificationSequenceX11 *seq_x11;

  seq_x11 = COBIWM_STARTUP_NOTIFICATION_SEQUENCE_X11 (seq);
  sn_startup_sequence_complete (seq_x11->seq);
}

static void
cobiwm_startup_notification_sequence_x11_finalize (GObject *object)
{
  CobiwmStartupNotificationSequenceX11 *seq;

  seq = COBIWM_STARTUP_NOTIFICATION_SEQUENCE_X11 (object);
  sn_startup_sequence_unref (seq->seq);

  G_OBJECT_CLASS (cobiwm_startup_notification_sequence_x11_parent_class)->finalize (object);
}

static void
cobiwm_startup_notification_sequence_x11_set_property (GObject      *object,
                                                     guint         prop_id,
                                                     const GValue *value,
                                                     GParamSpec   *pspec)
{
  CobiwmStartupNotificationSequenceX11 *seq;

  seq = COBIWM_STARTUP_NOTIFICATION_SEQUENCE_X11 (object);

  switch (prop_id)
    {
    case PROP_SEQ_X11_SEQ:
      seq->seq = g_value_get_pointer (value);
      sn_startup_sequence_ref (seq->seq);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
cobiwm_startup_notification_sequence_x11_get_property (GObject    *object,
                                                     guint       prop_id,
                                                     GValue     *value,
                                                     GParamSpec *pspec)
{
  CobiwmStartupNotificationSequenceX11 *seq;

  seq = COBIWM_STARTUP_NOTIFICATION_SEQUENCE_X11 (object);

  switch (prop_id)
    {
    case PROP_SEQ_X11_SEQ:
      g_value_set_pointer (value, seq->seq);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
cobiwm_startup_notification_sequence_x11_init (CobiwmStartupNotificationSequenceX11 *seq)
{
}

static void
cobiwm_startup_notification_sequence_x11_class_init (CobiwmStartupNotificationSequenceX11Class *klass)
{
  CobiwmStartupNotificationSequenceClass *seq_class;
  GObjectClass *object_class;

  seq_class = COBIWM_STARTUP_NOTIFICATION_SEQUENCE_CLASS (klass);
  seq_class->complete = cobiwm_startup_notification_sequence_x11_complete;

  object_class = G_OBJECT_CLASS (klass);
  object_class->finalize = cobiwm_startup_notification_sequence_x11_finalize;
  object_class->set_property = cobiwm_startup_notification_sequence_x11_set_property;
  object_class->get_property = cobiwm_startup_notification_sequence_x11_get_property;

  seq_x11_props[PROP_SEQ_X11_SEQ] =
    g_param_spec_pointer ("seq",
                          "Sequence",
                          "Sequence",
                          G_PARAM_READWRITE |
                          G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_properties (object_class, N_SEQ_X11_PROPS,
                                     seq_x11_props);
}

static CobiwmStartupNotificationSequence *
cobiwm_startup_notification_sequence_x11_new (SnStartupSequence *seq)
{
  gint64 timestamp;

  timestamp = sn_startup_sequence_get_timestamp (seq) * 1000;
  return g_object_new (COBIWM_TYPE_STARTUP_NOTIFICATION_SEQUENCE_X11,
                       "id", sn_startup_sequence_get_id (seq),
                       "timestamp", timestamp,
                       "seq", seq,
                       NULL);
}

static void
cobiwm_startup_notification_add_sequence_internal (CobiwmStartupNotification         *sn,
                                                 CobiwmStartupNotificationSequence *seq)
{
  sn->startup_sequences = g_slist_prepend (sn->startup_sequences,
                                           g_object_ref (seq));

  cobiwm_startup_notification_ensure_timeout (sn);
  cobiwm_startup_notification_update_feedback (sn);
}

static void
collect_timed_out_foreach (void *element,
                           void *data)
{
  CobiwmStartupNotificationSequence *sequence = element;
  CollectTimedOutData *ctod = data;
  gint64 elapsed, timestamp;

  timestamp = cobiwm_startup_notification_sequence_get_timestamp (sequence);
  elapsed = ctod->now - timestamp;

  cobiwm_topic (COBIWM_DEBUG_STARTUP,
              "Sequence used %ld ms vs. %d max: %s\n",
              elapsed, STARTUP_TIMEOUT,
              cobiwm_startup_notification_sequence_get_id (sequence));

  if (elapsed > STARTUP_TIMEOUT)
    ctod->list = g_slist_prepend (ctod->list, sequence);
}

static gboolean
startup_sequence_timeout (void *data)
{
  CobiwmStartupNotification *sn = data;
  CollectTimedOutData ctod;
  GSList *l;

  ctod.list = NULL;
  ctod.now = g_get_monotonic_time ();
  g_slist_foreach (sn->startup_sequences,
                   collect_timed_out_foreach,
                   &ctod);

  for (l = ctod.list; l != NULL; l = l->next)
    {
      CobiwmStartupNotificationSequence *sequence = l->data;

      cobiwm_topic (COBIWM_DEBUG_STARTUP,
                  "Timed out sequence %s\n",
                  cobiwm_startup_notification_sequence_get_id (sequence));

      cobiwm_startup_notification_sequence_complete (sequence);
    }

  g_slist_free (ctod.list);

  if (sn->startup_sequences != NULL)
    {
      return TRUE;
    }
  else
    {
      /* remove */
      sn->startup_sequence_timeout = 0;
      return FALSE;
    }
}

static void
cobiwm_startup_notification_ensure_timeout (CobiwmStartupNotification *sn)
{
  if (sn->startup_sequence_timeout != 0)
    return;

  /* our timeout just polls every second, instead of bothering
   * to compute exactly when we may next time out
   */
  sn->startup_sequence_timeout = g_timeout_add_seconds (1,
                                                        startup_sequence_timeout,
                                                        sn);
  g_source_set_name_by_id (sn->startup_sequence_timeout,
                           "[cobiwm] startup_sequence_timeout");
}
#endif

static void
cobiwm_startup_notification_remove_sequence_internal (CobiwmStartupNotification         *sn,
                                                    CobiwmStartupNotificationSequence *seq)
{
  sn->startup_sequences = g_slist_remove (sn->startup_sequences, seq);
  cobiwm_startup_notification_update_feedback (sn);

  if (sn->startup_sequences == NULL &&
      sn->startup_sequence_timeout != 0)
    {
      g_source_remove (sn->startup_sequence_timeout);
      sn->startup_sequence_timeout = 0;
    }

  g_object_unref (seq);
}

static CobiwmStartupNotificationSequence *
cobiwm_startup_notification_lookup_sequence (CobiwmStartupNotification *sn,
                                           const gchar             *id)
{
  CobiwmStartupNotificationSequence *seq;
  const gchar *seq_id;
  GSList *l;

  for (l = sn->startup_sequences; l; l = l->next)
    {
      seq = l->data;
      seq_id = cobiwm_startup_notification_sequence_get_id (seq);

      if (g_str_equal (seq_id, id))
        return l->data;
    }

  return NULL;
}

static void
cobiwm_startup_notification_init (CobiwmStartupNotification *sn)
{
}

static void
cobiwm_startup_notification_finalize (GObject *object)
{
  CobiwmStartupNotification *sn = COBIWM_STARTUP_NOTIFICATION (object);

#ifdef HAVE_STARTUP_NOTIFICATION
  sn_monitor_context_unref (sn->sn_context);
  sn_display_unref (sn->sn_display);
#endif

  if (sn->startup_sequence_timeout)
    g_source_remove (sn->startup_sequence_timeout);

  g_slist_foreach (sn->startup_sequences, (GFunc) g_object_unref, NULL);
  g_slist_free (sn->startup_sequences);
  sn->startup_sequences = NULL;

  G_OBJECT_CLASS (cobiwm_startup_notification_parent_class)->finalize (object);
}

static void
cobiwm_startup_notification_set_property (GObject      *object,
                                        guint         prop_id,
                                        const GValue *value,
                                        GParamSpec   *pspec)
{
  CobiwmStartupNotification *sn = COBIWM_STARTUP_NOTIFICATION (object);

  switch (prop_id)
    {
    case PROP_SN_DISPLAY:
      sn->display = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
cobiwm_startup_notification_get_property (GObject    *object,
                                        guint       prop_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
  CobiwmStartupNotification *sn = COBIWM_STARTUP_NOTIFICATION (object);

  switch (prop_id)
    {
    case PROP_SN_DISPLAY:
      g_value_set_object (value, sn->display);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

#ifdef HAVE_STARTUP_NOTIFICATION
static void
sn_error_trap_push (SnDisplay *sn_display,
                    Display   *xdisplay)
{
  CobiwmDisplay *display;
  display = cobiwm_display_for_x_display (xdisplay);
  if (display != NULL)
    cobiwm_error_trap_push (display);
}

static void
sn_error_trap_pop (SnDisplay *sn_display,
                   Display   *xdisplay)
{
  CobiwmDisplay *display;
  display = cobiwm_display_for_x_display (xdisplay);
  if (display != NULL)
    cobiwm_error_trap_pop (display);
}

static void
cobiwm_startup_notification_sn_event (SnMonitorEvent *event,
                                    void           *user_data)
{
  CobiwmStartupNotification *sn = user_data;
  CobiwmStartupNotificationSequence *seq;
  SnStartupSequence *sequence;

  sequence = sn_monitor_event_get_startup_sequence (event);

  sn_startup_sequence_ref (sequence);

  switch (sn_monitor_event_get_type (event))
    {
    case SN_MONITOR_EVENT_INITIATED:
      {
        const char *wmclass;

        wmclass = sn_startup_sequence_get_wmclass (sequence);

        cobiwm_topic (COBIWM_DEBUG_STARTUP,
                    "Received startup initiated for %s wmclass %s\n",
                    sn_startup_sequence_get_id (sequence),
                    wmclass ? wmclass : "(unset)");

        seq = cobiwm_startup_notification_sequence_x11_new (sequence);
        cobiwm_startup_notification_add_sequence_internal (sn, seq);
        g_object_unref (seq);
      }
      break;

    case SN_MONITOR_EVENT_COMPLETED:
      {
        cobiwm_topic (COBIWM_DEBUG_STARTUP,
                    "Received startup completed for %s\n",
                    sn_startup_sequence_get_id (sequence));

        cobiwm_startup_notification_remove_sequence (sn, sn_startup_sequence_get_id (sequence));
      }
      break;

    case SN_MONITOR_EVENT_CHANGED:
      cobiwm_topic (COBIWM_DEBUG_STARTUP,
                  "Received startup changed for %s\n",
                  sn_startup_sequence_get_id (sequence));
      break;

    case SN_MONITOR_EVENT_CANCELED:
      cobiwm_topic (COBIWM_DEBUG_STARTUP,
                  "Received startup canceled for %s\n",
                  sn_startup_sequence_get_id (sequence));
      break;
    }

  g_signal_emit (sn, sn_signals[SN_CHANGED], 0, sequence);

  sn_startup_sequence_unref (sequence);
}
#endif

static void
cobiwm_startup_notification_constructed (GObject *object)
{
  CobiwmStartupNotification *sn = COBIWM_STARTUP_NOTIFICATION (object);

  g_assert (sn->display != NULL);

#ifdef HAVE_STARTUP_NOTIFICATION
  sn->sn_display = sn_display_new (sn->display->xdisplay,
                                   sn_error_trap_push,
                                   sn_error_trap_pop);
  sn->sn_context =
    sn_monitor_context_new (sn->sn_display,
                            sn->display->screen->number,
                            cobiwm_startup_notification_sn_event,
                            sn,
                            NULL);
#endif
  sn->startup_sequences = NULL;
  sn->startup_sequence_timeout = 0;
}

static void
cobiwm_startup_notification_class_init (CobiwmStartupNotificationClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = cobiwm_startup_notification_constructed;
  object_class->finalize = cobiwm_startup_notification_finalize;
  object_class->set_property = cobiwm_startup_notification_set_property;
  object_class->get_property = cobiwm_startup_notification_get_property;

  sn_props[PROP_SN_DISPLAY] =
    g_param_spec_object ("display",
                         "Display",
                         "Display",
                         COBIWM_TYPE_DISPLAY,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  sn_signals[SN_CHANGED] =
    g_signal_new ("changed",
                  COBIWM_TYPE_STARTUP_NOTIFICATION,
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 1, G_TYPE_POINTER);

  g_object_class_install_properties (object_class, N_SN_PROPS, sn_props);
}

CobiwmStartupNotification *
cobiwm_startup_notification_get (CobiwmDisplay *display)
{
  static CobiwmStartupNotification *notification = NULL;

  if (!notification)
    notification = g_object_new (COBIWM_TYPE_STARTUP_NOTIFICATION,
                                 "display", display,
                                 NULL);

  return notification;
}

void
cobiwm_startup_notification_remove_sequence (CobiwmStartupNotification *sn,
                                           const gchar             *id)
{
  CobiwmStartupNotificationSequence *seq;

  seq = cobiwm_startup_notification_lookup_sequence (sn, id);
  if (seq)
    cobiwm_startup_notification_remove_sequence_internal (sn, seq);
}

gboolean
cobiwm_startup_notification_handle_xevent (CobiwmStartupNotification *sn,
                                         XEvent                  *xevent)
{
#ifdef HAVE_STARTUP_NOTIFICATION
  return sn_display_process_event (sn->sn_display, xevent);
#endif
  return FALSE;
}

GSList *
cobiwm_startup_notification_get_sequences (CobiwmStartupNotification *sn)
{
  GSList *sequences = NULL;
#ifdef HAVE_STARTUP_NOTIFICATION
  GSList *l;

  /* We return a list of SnStartupSequences here */
  for (l = sn->startup_sequences; l; l = l->next)
    {
      CobiwmStartupNotificationSequenceX11 *seq_x11;

      if (!COBIWM_IS_STARTUP_NOTIFICATION_SEQUENCE_X11 (l->data))
        continue;

      seq_x11 = COBIWM_STARTUP_NOTIFICATION_SEQUENCE_X11 (l->data);
      sequences = g_slist_prepend (sequences, seq_x11->seq);
    }
#endif

  return sequences;
}
