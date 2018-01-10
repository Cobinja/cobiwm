/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * CobiwmWindowShape
 *
 * Extracted invariant window shape
 *
 * Copyright (C) 2010 Red Hat, Inc.
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

#ifndef __COBIWM_WINDOW_SHAPE_H__
#define __COBIWM_WINDOW_SHAPE_H__

#include <cairo.h>
#include <glib-object.h>

GType cobiwm_window_shape_get_type (void) G_GNUC_CONST;

/**
 * CobiwmWindowShape:
 * #CobiwmWindowShape represents a 9-sliced region with borders on all sides that
 * are unscaled, and a constant central region that is scaled. For example,
 * the regions representing two windows that are rounded rectangles,
 * with the same corner radius but different sizes, have the
 * same CobiwmWindowShape.
 *
 * #CobiwmWindowShape is designed to be used as part of a hash table key, so has
 * efficient hash and equal functions.
 */
typedef struct _CobiwmWindowShape CobiwmWindowShape;

CobiwmWindowShape *  cobiwm_window_shape_new         (cairo_region_t  *region);
CobiwmWindowShape *  cobiwm_window_shape_ref         (CobiwmWindowShape *shape);
void               cobiwm_window_shape_unref       (CobiwmWindowShape *shape);
guint              cobiwm_window_shape_hash        (CobiwmWindowShape *shape);
gboolean           cobiwm_window_shape_equal       (CobiwmWindowShape *shape_a,
                                                  CobiwmWindowShape *shape_b);
void               cobiwm_window_shape_get_borders (CobiwmWindowShape *shape,
                                                  int             *border_top,
                                                  int             *border_right,
                                                  int             *border_bottom,
                                                  int             *border_left);
cairo_region_t    *cobiwm_window_shape_to_region   (CobiwmWindowShape *shape,
                                                  int              center_width,
                                                  int              center_height);

#endif /* __COBIWM_WINDOW_SHAPE_H __*/

