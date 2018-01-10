/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; c-basic-offset: 2; -*- */

/**
 * SECTION:barrier
 * @Title: CobiwmBarrier
 * @Short_Description: Pointer barriers
 */

#include "config.h"

#include <glib-object.h>

#include <util.h>
#include <barrier.h>
#include "backends/native/cobiwm-backend-native.h"
#include "backends/native/cobiwm-barrier-native.h"
#include "backends/x11/cobiwm-backend-x11.h"
#include "backends/x11/cobiwm-barrier-x11.h"
#include "cobiwm-enum-types.h"

G_DEFINE_TYPE (CobiwmBarrier, cobiwm_barrier, G_TYPE_OBJECT)
G_DEFINE_TYPE (CobiwmBarrierImpl, cobiwm_barrier_impl, G_TYPE_OBJECT)

enum {
  PROP_0,

  PROP_DISPLAY,

  PROP_X1,
  PROP_Y1,
  PROP_X2,
  PROP_Y2,
  PROP_DIRECTIONS,

  PROP_LAST,
};

static GParamSpec *obj_props[PROP_LAST];

enum {
  HIT,
  LEFT,

  LAST_SIGNAL,
};

static guint obj_signals[LAST_SIGNAL];


static void
cobiwm_barrier_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
  CobiwmBarrier *barrier = COBIWM_BARRIER (object);
  CobiwmBarrierPrivate *priv = barrier->priv;
  switch (prop_id)
    {
    case PROP_DISPLAY:
      g_value_set_object (value, priv->display);
      break;
    case PROP_X1:
      g_value_set_int (value, priv->border.line.a.x);
      break;
    case PROP_Y1:
      g_value_set_int (value, priv->border.line.a.y);
      break;
    case PROP_X2:
      g_value_set_int (value, priv->border.line.b.x);
      break;
    case PROP_Y2:
      g_value_set_int (value, priv->border.line.b.y);
      break;
    case PROP_DIRECTIONS:
      g_value_set_flags (value,
                         cobiwm_border_get_allows_directions (&priv->border));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
cobiwm_barrier_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
  CobiwmBarrier *barrier = COBIWM_BARRIER (object);
  CobiwmBarrierPrivate *priv = barrier->priv;
  switch (prop_id)
    {
    case PROP_DISPLAY:
      priv->display = g_value_get_object (value);
      break;
    case PROP_X1:
      priv->border.line.a.x = g_value_get_int (value);
      break;
    case PROP_Y1:
      priv->border.line.a.y = g_value_get_int (value);
      break;
    case PROP_X2:
      priv->border.line.b.x = g_value_get_int (value);
      break;
    case PROP_Y2:
      priv->border.line.b.y = g_value_get_int (value);
      break;
    case PROP_DIRECTIONS:
      cobiwm_border_set_allows_directions (&priv->border,
                                         g_value_get_flags (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
cobiwm_barrier_dispose (GObject *object)
{
  CobiwmBarrier *barrier = COBIWM_BARRIER (object);

  if (cobiwm_barrier_is_active (barrier))
    {
      cobiwm_bug ("CobiwmBarrier %p was destroyed while it was still active.",
                barrier);
    }

  G_OBJECT_CLASS (cobiwm_barrier_parent_class)->dispose (object);
}

gboolean
cobiwm_barrier_is_active (CobiwmBarrier *barrier)
{
  CobiwmBarrierImpl *impl = barrier->priv->impl;

  if (impl)
    return COBIWM_BARRIER_IMPL_GET_CLASS (impl)->is_active (impl);
  else
    return FALSE;
}

/**
 * cobiwm_barrier_release:
 * @barrier: The barrier to release
 * @event: The event to release the pointer for
 *
 * In XI2.3, pointer barriers provide a feature where they can
 * be temporarily released so that the pointer goes through
 * them. Pass a #CobiwmBarrierEvent to release the barrier for
 * this event sequence.
 */
void
cobiwm_barrier_release (CobiwmBarrier      *barrier,
                      CobiwmBarrierEvent *event)
{
  CobiwmBarrierImpl *impl = barrier->priv->impl;

  if (impl)
    COBIWM_BARRIER_IMPL_GET_CLASS (impl)->release (impl, event);
}

static void
cobiwm_barrier_constructed (GObject *object)
{
  CobiwmBarrier *barrier = COBIWM_BARRIER (object);
  CobiwmBarrierPrivate *priv = barrier->priv;

  g_return_if_fail (priv->border.line.a.x == priv->border.line.b.x ||
                    priv->border.line.a.y == priv->border.line.b.y);

#if defined(HAVE_NATIVE_BACKEND)
  if (COBIWM_IS_BACKEND_NATIVE (cobiwm_get_backend ()))
    priv->impl = cobiwm_barrier_impl_native_new (barrier);
#endif
#if defined(HAVE_XI23)
  if (COBIWM_IS_BACKEND_X11 (cobiwm_get_backend ()) &&
      !cobiwm_is_wayland_compositor ())
    priv->impl = cobiwm_barrier_impl_x11_new (barrier);
#endif

  if (priv->impl == NULL)
    g_warning ("Created a non-working barrier");

  /* Take a ref that we'll release in destroy() so that the object stays
   * alive while active. */
  g_object_ref (barrier);

  G_OBJECT_CLASS (cobiwm_barrier_parent_class)->constructed (object);
}

static void
cobiwm_barrier_class_init (CobiwmBarrierClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = cobiwm_barrier_get_property;
  object_class->set_property = cobiwm_barrier_set_property;
  object_class->dispose = cobiwm_barrier_dispose;
  object_class->constructed = cobiwm_barrier_constructed;

  obj_props[PROP_DISPLAY] =
    g_param_spec_object ("display",
                         "Display",
                         "The display to construct the pointer barrier on",
                         COBIWM_TYPE_DISPLAY,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  obj_props[PROP_X1] =
    g_param_spec_int ("x1",
                      "X1",
                      "The first X coordinate of the barrier",
                      0, G_MAXSHORT, 0,
                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  obj_props[PROP_Y1] =
    g_param_spec_int ("y1",
                      "Y1",
                      "The first Y coordinate of the barrier",
                      0, G_MAXSHORT, 0,
                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  obj_props[PROP_X2] =
    g_param_spec_int ("x2",
                      "X2",
                      "The second X coordinate of the barrier",
                      0, G_MAXSHORT, G_MAXSHORT,
                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  obj_props[PROP_Y2] =
    g_param_spec_int ("y2",
                      "Y2",
                      "The second Y coordinate of the barrier",
                      0, G_MAXSHORT, G_MAXSHORT,
                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  obj_props[PROP_DIRECTIONS] =
    g_param_spec_flags ("directions",
                        "Directions",
                        "A set of directions to let the pointer through",
                        COBIWM_TYPE_BARRIER_DIRECTION,
                        0,
                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST, obj_props);

  /**
   * CobiwmBarrier::hit:
   * @barrier: The #CobiwmBarrier that was hit
   * @event: A #CobiwmBarrierEvent that has the details of how
   * the barrier was hit.
   *
   * When a pointer barrier is hit, this will trigger. This
   * requires an XI2-enabled server.
   */
  obj_signals[HIT] =
    g_signal_new ("hit",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_FIRST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1,
                  COBIWM_TYPE_BARRIER_EVENT);

  /**
   * CobiwmBarrier::left:
   * @barrier: The #CobiwmBarrier that was left
   * @event: A #CobiwmBarrierEvent that has the details of how
   * the barrier was left.
   *
   * When a pointer barrier hitbox was left, this will trigger.
   * This requires an XI2-enabled server.
   */
  obj_signals[LEFT] =
    g_signal_new ("left",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_FIRST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1,
                  COBIWM_TYPE_BARRIER_EVENT);

  g_type_class_add_private (object_class, sizeof(CobiwmBarrierPrivate));
}

void
cobiwm_barrier_destroy (CobiwmBarrier *barrier)
{
  CobiwmBarrierImpl *impl = barrier->priv->impl;

  if (impl)
    return COBIWM_BARRIER_IMPL_GET_CLASS (impl)->destroy (impl);

  g_object_unref (barrier);
}

static void
cobiwm_barrier_init (CobiwmBarrier *barrier)
{
  barrier->priv = G_TYPE_INSTANCE_GET_PRIVATE (barrier, COBIWM_TYPE_BARRIER, CobiwmBarrierPrivate);
}

void
_cobiwm_barrier_emit_hit_signal (CobiwmBarrier      *barrier,
                               CobiwmBarrierEvent *event)
{
  g_signal_emit (barrier, obj_signals[HIT], 0, event);
}

void
_cobiwm_barrier_emit_left_signal (CobiwmBarrier      *barrier,
                                CobiwmBarrierEvent *event)
{
  g_signal_emit (barrier, obj_signals[LEFT], 0, event);
}

static void
cobiwm_barrier_impl_class_init (CobiwmBarrierImplClass *klass)
{
  klass->is_active = NULL;
  klass->release = NULL;
  klass->destroy = NULL;
}

static void
cobiwm_barrier_impl_init (CobiwmBarrierImpl *impl)
{
}

static CobiwmBarrierEvent *
cobiwm_barrier_event_ref (CobiwmBarrierEvent *event)
{
  g_return_val_if_fail (event != NULL, NULL);
  g_return_val_if_fail (event->ref_count > 0, NULL);

  g_atomic_int_inc ((volatile int *)&event->ref_count);
  return event;
}

void
cobiwm_barrier_event_unref (CobiwmBarrierEvent *event)
{
  g_return_if_fail (event != NULL);
  g_return_if_fail (event->ref_count > 0);

  if (g_atomic_int_dec_and_test ((volatile int *)&event->ref_count))
    g_slice_free (CobiwmBarrierEvent, event);
}

G_DEFINE_BOXED_TYPE (CobiwmBarrierEvent,
                     cobiwm_barrier_event,
                     cobiwm_barrier_event_ref,
                     cobiwm_barrier_event_unref)
