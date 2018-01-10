/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; c-basic-offset: 2; -*- */

#ifndef __COBIWM_BARRIER_H__
#define __COBIWM_BARRIER_H__

#include <glib-object.h>

#include <display.h>

G_BEGIN_DECLS

#define COBIWM_TYPE_BARRIER            (cobiwm_barrier_get_type ())
#define COBIWM_BARRIER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), COBIWM_TYPE_BARRIER, CobiwmBarrier))
#define COBIWM_BARRIER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  COBIWM_TYPE_BARRIER, CobiwmBarrierClass))
#define COBIWM_IS_BARRIER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), COBIWM_TYPE_BARRIER))
#define COBIWM_IS_BARRIER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  COBIWM_TYPE_BARRIER))
#define COBIWM_BARRIER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  COBIWM_TYPE_BARRIER, CobiwmBarrierClass))

typedef struct _CobiwmBarrier        CobiwmBarrier;
typedef struct _CobiwmBarrierClass   CobiwmBarrierClass;
typedef struct _CobiwmBarrierPrivate CobiwmBarrierPrivate;

typedef struct _CobiwmBarrierEvent   CobiwmBarrierEvent;

/**
 * CobiwmBarrier:
 *
 * The <structname>CobiwmBarrier</structname> structure contains
 * only private data and should be accessed using the provided API
 *
 **/
struct _CobiwmBarrier
{
  GObject parent;

  CobiwmBarrierPrivate *priv;
};

/**
 * CobiwmBarrierClass:
 *
 * The <structname>CobiwmBarrierClass</structname> structure contains only
 * private data.
 */
struct _CobiwmBarrierClass
{
  /*< private >*/
  GObjectClass parent_class;
};

GType cobiwm_barrier_get_type (void) G_GNUC_CONST;

gboolean cobiwm_barrier_is_active (CobiwmBarrier *barrier);
void cobiwm_barrier_destroy (CobiwmBarrier *barrier);
void cobiwm_barrier_release (CobiwmBarrier      *barrier,
                           CobiwmBarrierEvent *event);

/**
 * CobiwmBarrierDirection:
 * @COBIWM_BARRIER_DIRECTION_POSITIVE_X: Positive direction in the X axis
 * @COBIWM_BARRIER_DIRECTION_POSITIVE_Y: Positive direction in the Y axis
 * @COBIWM_BARRIER_DIRECTION_NEGATIVE_X: Negative direction in the X axis
 * @COBIWM_BARRIER_DIRECTION_NEGATIVE_Y: Negative direction in the Y axis
 */

/* Keep in sync with XFixes */
typedef enum {
  COBIWM_BARRIER_DIRECTION_POSITIVE_X = 1 << 0,
  COBIWM_BARRIER_DIRECTION_POSITIVE_Y = 1 << 1,
  COBIWM_BARRIER_DIRECTION_NEGATIVE_X = 1 << 2,
  COBIWM_BARRIER_DIRECTION_NEGATIVE_Y = 1 << 3,
} CobiwmBarrierDirection;

/**
 * CobiwmBarrierEvent:
 * @event_id: A unique integer ID identifying a
 * consecutive series of motions at or along the barrier
 * @time: Server time, in milliseconds
 * @dt: Server time, in milliseconds, since the last event
 * sent for this barrier
 * @x: The cursor X position in screen coordinates
 * @y: The cursor Y position in screen coordinates.
 * @dx: If the cursor hadn't been constrained, the delta
 * of X movement past the barrier, in screen coordinates
 * @dy: If the cursor hadn't been constrained, the delta
 * of X movement past the barrier, in screen coordinates
 * @released: A boolean flag, %TRUE if this event generated
 * by the pointer leaving the barrier as a result of a client
 * calling cobiwm_barrier_release() (will be set only for
 * CobiwmBarrier::leave signals)
 * @grabbed: A boolean flag, %TRUE if the pointer was grabbed
 * at the time this event was sent
 */
struct _CobiwmBarrierEvent {
  /* < private > */
  volatile guint ref_count;

  /* < public > */
  int event_id;
  int dt;
  guint32 time;
  double x;
  double y;
  double dx;
  double dy;
  gboolean released;
  gboolean grabbed;
};

#define COBIWM_TYPE_BARRIER_EVENT (cobiwm_barrier_event_get_type ())
GType cobiwm_barrier_event_get_type (void) G_GNUC_CONST;

G_END_DECLS

#endif /* __COBIWM_BARRIER_H__ */
