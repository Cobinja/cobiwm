#ifndef _COBIWM_SYNC_RING_H_
#define _COBIWM_SYNC_RING_H_

#include <glib.h>

#include <X11/Xlib.h>

gboolean cobiwm_sync_ring_init (Display *dpy);
void cobiwm_sync_ring_destroy (void);
gboolean cobiwm_sync_ring_after_frame (void);
gboolean cobiwm_sync_ring_insert_wait (void);
void cobiwm_sync_ring_handle_event (XEvent *event);

#endif  /* _COBIWM_SYNC_RING_H_ */
