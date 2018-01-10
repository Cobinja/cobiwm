/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/**
 * \file stack-tracker.h  Track stacking order for compositor
 *
 * CobiwmStackTracker maintains the most accurate view we have at a
 * given point of time of the ordering of the children of the root
 * window (including override-redirect windows.) This is used to order
 * the windows when the compositor draws them.
 *
 * By contrast, CobiwmStack is responsible for keeping track of how we
 * think that windows *should* be ordered.  For windows we manage
 * (non-override-redirect windows), the two stacking orders will be
 * the same.
 */

/*
 * Copyright (C) 2009 Red Hat, Inc.
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

#ifndef COBIWM_STACK_TRACKER_H
#define COBIWM_STACK_TRACKER_H

#include <screen.h>
#include <window.h>

typedef struct _CobiwmStackTracker CobiwmStackTracker;

CobiwmStackTracker *cobiwm_stack_tracker_new  (CobiwmScreen       *screen);
void              cobiwm_stack_tracker_free (CobiwmStackTracker *tracker);

/* These functions are called when we make an X call that changes the
 * stacking order; this allows CobiwmStackTracker to predict stacking
 * order before it receives events back from the X server */
void cobiwm_stack_tracker_record_add             (CobiwmStackTracker *tracker,
                                                guint64           window,
                                                gulong            serial);
void cobiwm_stack_tracker_record_remove          (CobiwmStackTracker *tracker,
                                                guint64           window,
                                                gulong            serial);

/* We also have functions that also go ahead and do the work
 */
void cobiwm_stack_tracker_lower           (CobiwmStackTracker *tracker,
                                         guint64           window);

void cobiwm_stack_tracker_restack_managed (CobiwmStackTracker *tracker,
                                         const guint64    *windows,
                                         int               n_windows);
void cobiwm_stack_tracker_restack_at_bottom (CobiwmStackTracker *tracker,
                                           const guint64    *new_order,
                                           int               n_new_order);

/* These functions are used to update the stack when we get events
 * reflecting changes to the stacking order */
void cobiwm_stack_tracker_create_event    (CobiwmStackTracker    *tracker,
					 XCreateWindowEvent  *event);
void cobiwm_stack_tracker_destroy_event   (CobiwmStackTracker    *tracker,
					 XDestroyWindowEvent *event);
void cobiwm_stack_tracker_reparent_event  (CobiwmStackTracker    *tracker,
					 XReparentEvent      *event);
void cobiwm_stack_tracker_configure_event (CobiwmStackTracker    *tracker,
					 XConfigureEvent     *event);

void cobiwm_stack_tracker_get_stack  (CobiwmStackTracker *tracker,
                                    guint64         **windows,
                                    int              *n_entries);

void cobiwm_stack_tracker_sync_stack       (CobiwmStackTracker *tracker);
void cobiwm_stack_tracker_queue_sync_stack (CobiwmStackTracker *tracker);

#endif /* COBIWM_STACK_TRACKER_H */
