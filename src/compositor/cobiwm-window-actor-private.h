/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

#ifndef COBIWM_WINDOW_ACTOR_PRIVATE_H
#define COBIWM_WINDOW_ACTOR_PRIVATE_H

#include <config.h>

#include <X11/extensions/Xdamage.h>
#include <compositor-cobiwm.h>
#include "cobiwm-surface-actor.h"
#include "cobiwm-effect-manager.h"

CobiwmWindowActor *cobiwm_window_actor_new (CobiwmWindow *window);

void cobiwm_window_actor_destroy   (CobiwmWindowActor *self);

void cobiwm_window_actor_show (CobiwmWindowActor *self,
                             CobiwmCompEffect   effect);
void cobiwm_window_actor_hide (CobiwmWindowActor *self,
                             CobiwmCompEffect   effect);

void cobiwm_window_actor_process_x11_damage (CobiwmWindowActor    *self,
                                           XDamageNotifyEvent *event);

void cobiwm_window_actor_pre_paint      (CobiwmWindowActor    *self);
void cobiwm_window_actor_post_paint     (CobiwmWindowActor    *self);
void cobiwm_window_actor_frame_complete (CobiwmWindowActor    *self,
                                       CoglFrameInfo      *frame_info,
                                       gint64              presentation_time);

void cobiwm_window_actor_invalidate_shadow (CobiwmWindowActor *self);

void cobiwm_window_actor_get_shape_bounds (CobiwmWindowActor       *self,
                                          cairo_rectangle_int_t *bounds);

gboolean cobiwm_window_actor_should_unredirect   (CobiwmWindowActor *self);
void     cobiwm_window_actor_set_unredirected    (CobiwmWindowActor *self,
                                                gboolean         unredirected);

gboolean cobiwm_window_actor_effect_in_progress  (CobiwmWindowActor *self);
void     cobiwm_window_actor_sync_actor_geometry (CobiwmWindowActor *self,
                                                gboolean         did_placement);
void     cobiwm_window_actor_sync_visibility     (CobiwmWindowActor *self);
void     cobiwm_window_actor_update_shape        (CobiwmWindowActor *self);
void     cobiwm_window_actor_update_opacity      (CobiwmWindowActor *self);
void     cobiwm_window_actor_mapped              (CobiwmWindowActor *self);
void     cobiwm_window_actor_unmapped            (CobiwmWindowActor *self);
void     cobiwm_window_actor_sync_updates_frozen (CobiwmWindowActor *self);
void     cobiwm_window_actor_queue_frame_drawn   (CobiwmWindowActor *self,
                                                gboolean         no_delay_frame);

void cobiwm_window_actor_effect_completed (CobiwmWindowActor  *actor,
                                         CobiwmEffect  event);

CobiwmSurfaceActor *cobiwm_window_actor_get_surface (CobiwmWindowActor *self);
void cobiwm_window_actor_update_surface (CobiwmWindowActor *self);

#endif /* COBIWM_WINDOW_ACTOR_PRIVATE_H */
