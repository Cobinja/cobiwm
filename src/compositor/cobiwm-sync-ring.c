/*
 * This is based on an original C++ implementation for compiz that
 * carries the following copyright notice:
 *
 *
 * Copyright © 2011 NVIDIA Corporation
 *
 * Permission to use, copy, modify, distribute, and sell this software
 * and its documentation for any purpose is hereby granted without
 * fee, provided that the above copyright notice appear in all copies
 * and that both that copyright notice and this permission notice
 * appear in supporting documentation, and that the name of NVIDIA
 * Corporation not be used in advertising or publicity pertaining to
 * distribution of the software without specific, written prior
 * permission.  NVIDIA Corporation makes no representations about the
 * suitability of this software for any purpose. It is provided "as
 * is" without express or implied warranty.
 *
 * NVIDIA CORPORATION DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS
 * SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL NVIDIA CORPORATION BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 *
 * Authors: James Jones <jajones@nvidia.com>
 */

#include <string.h>

#include <GL/gl.h>
#include <GL/glx.h>
#include <X11/extensions/sync.h>

#include <cogl/cogl.h>
#include <clutter/clutter.h>

#include <util.h>

#include "cobiwm-sync-ring.h"

/* Theory of operation:
 *
 * We use a ring of NUM_SYNCS fence objects. On each frame we advance
 * to the next fence in the ring. For each fence we do:
 *
 * 1. fence is XSyncTriggerFence()'d and glWaitSync()'d
 * 2. NUM_SYNCS / 2 frames later, fence should be triggered
 * 3. fence is XSyncResetFence()'d
 * 4. NUM_SYNCS / 2 frames later, fence should be reset
 * 5. go back to 1 and re-use fence
 *
 * glClientWaitSync() and XAlarms are used in steps 2 and 4,
 * respectively, to double-check the expectections.
 */

#define NUM_SYNCS 10
#define MAX_SYNC_WAIT_TIME (1 * 1000 * 1000 * 1000) /* one sec */
#define MAX_REBOOT_ATTEMPTS 2

typedef enum
{
  COBIWM_SYNC_STATE_READY,
  COBIWM_SYNC_STATE_WAITING,
  COBIWM_SYNC_STATE_DONE,
  COBIWM_SYNC_STATE_RESET_PENDING,
} CobiwmSyncState;

typedef struct
{
  Display *xdisplay;

  XSyncFence xfence;
  GLsync gl_x11_sync;
  GLsync gpu_fence;

  XSyncCounter xcounter;
  XSyncAlarm xalarm;
  XSyncValue next_counter_value;

  CobiwmSyncState state;
} CobiwmSync;

typedef struct
{
  Display *xdisplay;
  int xsync_event_base;
  int xsync_error_base;

  GHashTable *alarm_to_sync;

  CobiwmSync *syncs_array[NUM_SYNCS];
  guint current_sync_idx;
  CobiwmSync *current_sync;
  guint warmup_syncs;

  guint reboots;
} CobiwmSyncRing;

static CobiwmSyncRing cobiwm_sync_ring = { 0 };

static XSyncValue SYNC_VALUE_ZERO;
static XSyncValue SYNC_VALUE_ONE;

static const char*      (*cobiwm_gl_get_string) (GLenum name);
static void             (*cobiwm_gl_get_integerv) (GLenum  pname,
                                                 GLint  *params);
static const char*      (*cobiwm_gl_get_stringi) (GLenum name,
                                                GLuint index);
static void             (*cobiwm_gl_delete_sync) (GLsync sync);
static GLenum           (*cobiwm_gl_client_wait_sync) (GLsync sync,
                                                     GLbitfield flags,
                                                     GLuint64 timeout);
static void             (*cobiwm_gl_wait_sync) (GLsync sync,
                                              GLbitfield flags,
                                              GLuint64 timeout);
static GLsync           (*cobiwm_gl_import_sync) (GLenum external_sync_type,
                                                GLintptr external_sync,
                                                GLbitfield flags);
static GLsync           (*cobiwm_gl_fence_sync) (GLenum condition,
                                               GLbitfield flags);

static CobiwmSyncRing *
cobiwm_sync_ring_get (void)
{
  if (cobiwm_sync_ring.reboots > MAX_REBOOT_ATTEMPTS)
    return NULL;

  return &cobiwm_sync_ring;
}

static gboolean
load_gl_symbol (const char  *name,
                void       **func)
{
  *func = cogl_get_proc_address (name);
  if (!*func)
    {
      cobiwm_verbose ("CobiwmSyncRing: failed to resolve required GL symbol \"%s\"\n", name);
      return FALSE;
    }
  return TRUE;
}

static gboolean
check_gl_extensions (void)
{
  ClutterBackend *backend;
  CoglContext *cogl_context;
  CoglDisplay *cogl_display;
  CoglRenderer *cogl_renderer;

  backend = clutter_get_default_backend ();
  cogl_context = clutter_backend_get_cogl_context (backend);
  cogl_display = cogl_context_get_display (cogl_context);
  cogl_renderer = cogl_display_get_renderer (cogl_display);

  switch (cogl_renderer_get_driver (cogl_renderer))
    {
    case COGL_DRIVER_GL3:
      {
        int num_extensions, i;
        gboolean arb_sync = FALSE;
        gboolean x11_sync_object = FALSE;

        cobiwm_gl_get_integerv (GL_NUM_EXTENSIONS, &num_extensions);

        for (i = 0; i < num_extensions; ++i)
          {
            const char *ext = cobiwm_gl_get_stringi (GL_EXTENSIONS, i);

            if (g_strcmp0 ("GL_ARB_sync", ext) == 0)
              arb_sync = TRUE;
            else if (g_strcmp0 ("GL_EXT_x11_sync_object", ext) == 0)
              x11_sync_object = TRUE;
          }

        return arb_sync && x11_sync_object;
      }
    case COGL_DRIVER_GL:
      {
        const char *extensions = cobiwm_gl_get_string (GL_EXTENSIONS);
        return (extensions != NULL &&
                strstr (extensions, "GL_ARB_sync") != NULL &&
                strstr (extensions, "GL_EXT_x11_sync_object") != NULL);
      }
    default:
      break;
    }

  return FALSE;
}

static gboolean
load_required_symbols (void)
{
  static gboolean success = FALSE;

  if (success)
    return TRUE;

  /* We don't link against libGL directly because cogl may want to
   * use something else. This assumes that cogl has been initialized
   * and dynamically loaded libGL at this point.
   */

  if (!load_gl_symbol ("glGetString", (void **) &cobiwm_gl_get_string))
    goto out;
  if (!load_gl_symbol ("glGetIntegerv", (void **) &cobiwm_gl_get_integerv))
    goto out;
  if (!load_gl_symbol ("glGetStringi", (void **) &cobiwm_gl_get_stringi))
    goto out;

  if (!check_gl_extensions ())
    {
      cobiwm_verbose ("CobiwmSyncRing: couldn't find required GL extensions\n");
      goto out;
    }

  if (!load_gl_symbol ("glDeleteSync", (void **) &cobiwm_gl_delete_sync))
    goto out;
  if (!load_gl_symbol ("glClientWaitSync", (void **) &cobiwm_gl_client_wait_sync))
    goto out;
  if (!load_gl_symbol ("glWaitSync", (void **) &cobiwm_gl_wait_sync))
    goto out;
  if (!load_gl_symbol ("glImportSyncEXT", (void **) &cobiwm_gl_import_sync))
    goto out;
  if (!load_gl_symbol ("glFenceSync", (void **) &cobiwm_gl_fence_sync))
    goto out;

  success = TRUE;
 out:
  return success;
}

static void
cobiwm_sync_insert (CobiwmSync *self)
{
  g_return_if_fail (self->state == COBIWM_SYNC_STATE_READY);

  XSyncTriggerFence (self->xdisplay, self->xfence);
  XFlush (self->xdisplay);

  cobiwm_gl_wait_sync (self->gl_x11_sync, 0, GL_TIMEOUT_IGNORED);
  self->gpu_fence = cobiwm_gl_fence_sync (GL_SYNC_GPU_COMMANDS_COMPLETE, 0);

  self->state = COBIWM_SYNC_STATE_WAITING;
}

static GLenum
cobiwm_sync_check_update_finished (CobiwmSync *self,
                                 GLuint64  timeout)
{
  GLenum status = GL_WAIT_FAILED;

  switch (self->state)
    {
    case COBIWM_SYNC_STATE_DONE:
      status = GL_ALREADY_SIGNALED;
      break;
    case COBIWM_SYNC_STATE_WAITING:
      status = cobiwm_gl_client_wait_sync (self->gpu_fence, 0, timeout);
      if (status == GL_ALREADY_SIGNALED || status == GL_CONDITION_SATISFIED)
        {
          self->state = COBIWM_SYNC_STATE_DONE;
          cobiwm_gl_delete_sync (self->gpu_fence);
          self->gpu_fence = 0;
        }
      break;
    default:
      break;
    }

  g_warn_if_fail (status != GL_WAIT_FAILED);

  return status;
}

static void
cobiwm_sync_reset (CobiwmSync *self)
{
  XSyncAlarmAttributes attrs;
  int overflow;

  g_return_if_fail (self->state == COBIWM_SYNC_STATE_DONE);

  XSyncResetFence (self->xdisplay, self->xfence);

  attrs.trigger.wait_value = self->next_counter_value;

  XSyncChangeAlarm (self->xdisplay, self->xalarm, XSyncCAValue, &attrs);
  XSyncSetCounter (self->xdisplay, self->xcounter, self->next_counter_value);

  XSyncValueAdd (&self->next_counter_value,
                 self->next_counter_value,
                 SYNC_VALUE_ONE,
                 &overflow);

  self->state = COBIWM_SYNC_STATE_RESET_PENDING;
}

static void
cobiwm_sync_handle_event (CobiwmSync              *self,
                        XSyncAlarmNotifyEvent *event)
{
  g_return_if_fail (event->alarm == self->xalarm);
  g_return_if_fail (self->state == COBIWM_SYNC_STATE_RESET_PENDING);

  self->state = COBIWM_SYNC_STATE_READY;
}

static CobiwmSync *
cobiwm_sync_new (Display *xdisplay)
{
  CobiwmSync *self;
  XSyncAlarmAttributes attrs;

  self = g_malloc0 (sizeof (CobiwmSync));

  self->xdisplay = xdisplay;

  self->xfence = XSyncCreateFence (xdisplay, DefaultRootWindow (xdisplay), FALSE);
  self->gl_x11_sync = 0;
  self->gpu_fence = 0;

  self->xcounter = XSyncCreateCounter (xdisplay, SYNC_VALUE_ZERO);

  attrs.trigger.counter = self->xcounter;
  attrs.trigger.value_type = XSyncAbsolute;
  attrs.trigger.wait_value = SYNC_VALUE_ONE;
  attrs.trigger.test_type = XSyncPositiveTransition;
  attrs.events = TRUE;
  self->xalarm = XSyncCreateAlarm (xdisplay,
                                   XSyncCACounter |
                                   XSyncCAValueType |
                                   XSyncCAValue |
                                   XSyncCATestType |
                                   XSyncCAEvents,
                                   &attrs);

  XSyncIntToValue (&self->next_counter_value, 1);

  self->state = COBIWM_SYNC_STATE_READY;

  return self;
}

static void
cobiwm_sync_import (CobiwmSync *self)
{
  g_return_if_fail (self->gl_x11_sync == 0);
  self->gl_x11_sync = cobiwm_gl_import_sync (GL_SYNC_X11_FENCE_EXT, self->xfence, 0);
}

static Bool
alarm_event_predicate (Display  *dpy,
                       XEvent   *event,
                       XPointer  data)
{
  CobiwmSyncRing *ring = cobiwm_sync_ring_get ();

  if (!ring)
    return False;

  if (event->type == ring->xsync_event_base + XSyncAlarmNotify)
    {
      if (((CobiwmSync *) data)->xalarm == ((XSyncAlarmNotifyEvent *) event)->alarm)
        return True;
    }
  return False;
}

static void
cobiwm_sync_free (CobiwmSync *self)
{
  /* When our assumptions don't hold, something has gone wrong but we
   * don't know what, so we reboot the ring. While doing that, we
   * trigger fences before deleting them to try to get ourselves out
   * of a potentially stuck GPU state.
   */
  switch (self->state)
    {
    case COBIWM_SYNC_STATE_WAITING:
      cobiwm_gl_delete_sync (self->gpu_fence);
      break;
    case COBIWM_SYNC_STATE_DONE:
      /* nothing to do */
      break;
    case COBIWM_SYNC_STATE_RESET_PENDING:
      {
        XEvent event;
        XIfEvent (self->xdisplay, &event, alarm_event_predicate, (XPointer) self);
        cobiwm_sync_handle_event (self, (XSyncAlarmNotifyEvent *) &event);
      }
      /* fall through */
    case COBIWM_SYNC_STATE_READY:
      XSyncTriggerFence (self->xdisplay, self->xfence);
      XFlush (self->xdisplay);
      break;
    default:
      break;
    }

  cobiwm_gl_delete_sync (self->gl_x11_sync);
  XSyncDestroyFence (self->xdisplay, self->xfence);
  XSyncDestroyCounter (self->xdisplay, self->xcounter);
  XSyncDestroyAlarm (self->xdisplay, self->xalarm);

  g_free (self);
}

gboolean
cobiwm_sync_ring_init (Display *xdisplay)
{
  gint major, minor;
  guint i;
  CobiwmSyncRing *ring = cobiwm_sync_ring_get ();

  if (!ring)
    return FALSE;

  g_return_val_if_fail (xdisplay != NULL, FALSE);
  g_return_val_if_fail (ring->xdisplay == NULL, FALSE);

  if (!load_required_symbols ())
    return FALSE;

  if (!XSyncQueryExtension (xdisplay, &ring->xsync_event_base, &ring->xsync_error_base) ||
      !XSyncInitialize (xdisplay, &major, &minor))
    return FALSE;

  XSyncIntToValue (&SYNC_VALUE_ZERO, 0);
  XSyncIntToValue (&SYNC_VALUE_ONE, 1);

  ring->xdisplay = xdisplay;

  ring->alarm_to_sync = g_hash_table_new (NULL, NULL);

  for (i = 0; i < NUM_SYNCS; ++i)
    {
      CobiwmSync *sync = cobiwm_sync_new (ring->xdisplay);
      ring->syncs_array[i] = sync;
      g_hash_table_replace (ring->alarm_to_sync, (gpointer) sync->xalarm, sync);
    }
  /* Since the connection we create the X fences on isn't the same as
   * the one used for the GLX context, we need to XSync() here to
   * ensure glImportSync() succeeds. */
  XSync (xdisplay, False);
  for (i = 0; i < NUM_SYNCS; ++i)
    cobiwm_sync_import (ring->syncs_array[i]);

  ring->current_sync_idx = 0;
  ring->current_sync = ring->syncs_array[0];
  ring->warmup_syncs = 0;

  return TRUE;
}

void
cobiwm_sync_ring_destroy (void)
{
  guint i;
  CobiwmSyncRing *ring = cobiwm_sync_ring_get ();

  if (!ring)
    return;

  g_return_if_fail (ring->xdisplay != NULL);

  ring->current_sync_idx = 0;
  ring->current_sync = NULL;
  ring->warmup_syncs = 0;

  for (i = 0; i < NUM_SYNCS; ++i)
    cobiwm_sync_free (ring->syncs_array[i]);

  g_hash_table_destroy (ring->alarm_to_sync);

  ring->xsync_event_base = 0;
  ring->xsync_error_base = 0;
  ring->xdisplay = NULL;
}

static gboolean
cobiwm_sync_ring_reboot (Display *xdisplay)
{
  CobiwmSyncRing *ring = cobiwm_sync_ring_get ();

  if (!ring)
    return FALSE;

  cobiwm_sync_ring_destroy ();

  ring->reboots += 1;

  if (!cobiwm_sync_ring_get ())
    {
      cobiwm_warning ("CobiwmSyncRing: Too many reboots -- disabling\n");
      return FALSE;
    }

  return cobiwm_sync_ring_init (xdisplay);
}

gboolean
cobiwm_sync_ring_after_frame (void)
{
  CobiwmSyncRing *ring = cobiwm_sync_ring_get ();

  if (!ring)
    return FALSE;

  g_return_val_if_fail (ring->xdisplay != NULL, FALSE);

  if (ring->warmup_syncs >= NUM_SYNCS / 2)
    {
      guint reset_sync_idx = (ring->current_sync_idx + NUM_SYNCS - (NUM_SYNCS / 2)) % NUM_SYNCS;
      CobiwmSync *sync_to_reset = ring->syncs_array[reset_sync_idx];

      GLenum status = cobiwm_sync_check_update_finished (sync_to_reset, 0);
      if (status == GL_TIMEOUT_EXPIRED)
        {
          cobiwm_warning ("CobiwmSyncRing: We should never wait for a sync -- add more syncs?\n");
          status = cobiwm_sync_check_update_finished (sync_to_reset, MAX_SYNC_WAIT_TIME);
        }

      if (status != GL_ALREADY_SIGNALED && status != GL_CONDITION_SATISFIED)
        {
          cobiwm_warning ("CobiwmSyncRing: Timed out waiting for sync object.\n");
          return cobiwm_sync_ring_reboot (ring->xdisplay);
        }

      cobiwm_sync_reset (sync_to_reset);
    }
  else
    {
      ring->warmup_syncs += 1;
    }

  ring->current_sync_idx += 1;
  ring->current_sync_idx %= NUM_SYNCS;

  ring->current_sync = ring->syncs_array[ring->current_sync_idx];

  return TRUE;
}

gboolean
cobiwm_sync_ring_insert_wait (void)
{
  CobiwmSyncRing *ring = cobiwm_sync_ring_get ();

  if (!ring)
    return FALSE;

  g_return_val_if_fail (ring->xdisplay != NULL, FALSE);

  if (ring->current_sync->state != COBIWM_SYNC_STATE_READY)
    {
      cobiwm_warning ("CobiwmSyncRing: Sync object is not ready -- were events handled properly?\n");
      if (!cobiwm_sync_ring_reboot (ring->xdisplay))
        return FALSE;
    }

  cobiwm_sync_insert (ring->current_sync);

  return TRUE;
}

void
cobiwm_sync_ring_handle_event (XEvent *xevent)
{
  XSyncAlarmNotifyEvent *event;
  CobiwmSync *sync;
  CobiwmSyncRing *ring = cobiwm_sync_ring_get ();

  if (!ring)
    return;

  g_return_if_fail (ring->xdisplay != NULL);

  if (xevent->type != (ring->xsync_event_base + XSyncAlarmNotify))
    return;

  event = (XSyncAlarmNotifyEvent *) xevent;

  sync = g_hash_table_lookup (ring->alarm_to_sync, (gpointer) event->alarm);
  if (sync)
    cobiwm_sync_handle_event (sync, event);
}
