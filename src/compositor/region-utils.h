/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Utilities for region manipulation
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

#ifndef __COBIWM_REGION_UTILS_H__
#define __COBIWM_REGION_UTILS_H__

#include <clutter/clutter.h>

#include <cairo.h>
#include <glib.h>

/**
 * CobiwmRegionIterator:
 * @region: region being iterated
 * @rectangle: current rectangle
 * @line_start: whether the current rectangle starts a horizontal band
 * @line_end: whether the current rectangle ends a horizontal band
 *
 * cairo_region_t is a yx banded region; sometimes its useful to iterate through
 * such a region treating the start and end of each horizontal band in a distinct
 * fashion.
 *
 * Usage:
 *
 *  CobiwmRegionIterator iter;
 *  for (cobiwm_region_iterator_init (&iter, region);
 *       !cobiwm_region_iterator_at_end (&iter);
 *       cobiwm_region_iterator_next (&iter))
 *  {
 *    [ Use iter.rectangle, iter.line_start, iter.line_end ]
 *  }
 */
typedef struct _CobiwmRegionIterator CobiwmRegionIterator;

struct _CobiwmRegionIterator {
  cairo_region_t *region;
  cairo_rectangle_int_t rectangle;
  gboolean line_start;
  gboolean line_end;
  int i;

  /*< private >*/
  int n_rectangles;
  cairo_rectangle_int_t next_rectangle;
};

typedef struct _CobiwmRegionBuilder CobiwmRegionBuilder;

#define COBIWM_REGION_BUILDER_MAX_LEVELS 16
struct _CobiwmRegionBuilder {
  /* To merge regions in binary tree order, we need to keep track of
   * the regions that we've already merged together at different
   * levels of the tree. We fill in an array in the pattern:
   *
   * |a  |
   * |b  |a  |
   * |c  |   |ab |
   * |d  |c  |ab |
   * |e  |   |   |abcd|
   */
  cairo_region_t *levels[COBIWM_REGION_BUILDER_MAX_LEVELS];
  int n_levels;
};

void     cobiwm_region_builder_init       (CobiwmRegionBuilder *builder);
void     cobiwm_region_builder_add_rectangle (CobiwmRegionBuilder *builder,
                                            int                x,
                                            int                y,
                                            int                width,
                                            int                height);
cairo_region_t * cobiwm_region_builder_finish (CobiwmRegionBuilder *builder);

void     cobiwm_region_iterator_init      (CobiwmRegionIterator *iter,
                                         cairo_region_t     *region);
gboolean cobiwm_region_iterator_at_end    (CobiwmRegionIterator *iter);
void     cobiwm_region_iterator_next      (CobiwmRegionIterator *iter);

cairo_region_t *cobiwm_region_scale (cairo_region_t *region, int scale);

cairo_region_t *cobiwm_make_border_region (cairo_region_t *region,
                                         int             x_amount,
                                         int             y_amount,
                                         gboolean        flip);

#endif /* __COBIWM_REGION_UTILS_H__ */
