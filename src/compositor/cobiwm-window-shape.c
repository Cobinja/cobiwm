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

#include "config.h"

#include <cobiwm-window-shape.h>

#include <string.h>
#include "region-utils.h"

struct _CobiwmWindowShape
{
  guint ref_count;

  int top, right, bottom, left;
  int n_rectangles;
  cairo_rectangle_int_t *rectangles;
  guint hash;
};

CobiwmWindowShape *
cobiwm_window_shape_new (cairo_region_t *region)
{
  CobiwmWindowShape *shape;
  CobiwmRegionIterator iter;
  cairo_rectangle_int_t extents;
  int max_yspan_y1 = 0;
  int max_yspan_y2 = 0;
  int max_xspan_x1 = -1;
  int max_xspan_x2 = -1;
  guint hash;

  shape = g_slice_new0 (CobiwmWindowShape);
  shape->ref_count = 1;

  cairo_region_get_extents (region, &extents);

  shape->n_rectangles = cairo_region_num_rectangles (region);

  if (shape->n_rectangles == 0)
    {
      shape->rectangles = NULL;
      shape->top = shape->right = shape->bottom = shape->left = 0;
      shape->hash = 0;
      return shape;
    }

  for (cobiwm_region_iterator_init (&iter, region);
       !cobiwm_region_iterator_at_end (&iter);
       cobiwm_region_iterator_next (&iter))
    {
      int max_line_xspan_x1 = -1;
      int max_line_xspan_x2 = -1;

      if (iter.rectangle.width > max_line_xspan_x2 - max_line_xspan_x1)
        {
          max_line_xspan_x1 = iter.rectangle.x;
          max_line_xspan_x2 = iter.rectangle.x + iter.rectangle.width;
        }

      if (iter.line_end)
        {
          if (iter.rectangle.height > max_yspan_y2 - max_yspan_y1)
            {
              max_yspan_y1 = iter.rectangle.y;
              max_yspan_y2 = iter.rectangle.y + iter.rectangle.height;
            }

          if (max_xspan_x1 < 0) /* First line */
            {
              max_xspan_x1 = max_line_xspan_x1;
              max_xspan_x2 = max_line_xspan_x2;
            }
          else
            {
              max_xspan_x1 = MAX (max_xspan_x1, max_line_xspan_x1);
              max_xspan_x2 = MIN (max_xspan_x2, max_line_xspan_x2);

              if (max_xspan_x2 < max_xspan_x1)
                max_xspan_x2 = max_xspan_x1;
            }
        }
    }

#if 0
  g_print ("xspan: %d -> %d, yspan: %d -> %d\n",
           max_xspan_x1, max_xspan_x2,
           max_yspan_y1, max_yspan_y2);
#endif

  shape->top = max_yspan_y1 - extents.y;
  shape->right = extents.x + extents.width - max_xspan_x2;
  shape->bottom = extents.y + extents.height - max_yspan_y2;
  shape->left = max_xspan_x1 - extents.x;

  shape->rectangles = g_new (cairo_rectangle_int_t, shape->n_rectangles);

  hash = 0;
  for (cobiwm_region_iterator_init (&iter, region);
       !cobiwm_region_iterator_at_end (&iter);
       cobiwm_region_iterator_next (&iter))
    {
      int x1, x2, y1, y2;

      x1 = iter.rectangle.x;
      x2 = iter.rectangle.x + iter.rectangle.width;
      y1 = iter.rectangle.y;
      y2 = iter.rectangle.y + iter.rectangle.height;

      if (x1 > max_xspan_x1)
        x1 -= MIN (x1, max_xspan_x2 - 1) - max_xspan_x1;
      if (x2 > max_xspan_x1)
        x2 -= MIN (x2, max_xspan_x2 - 1) - max_xspan_x1;
      if (y1 > max_yspan_y1)
        y1 -= MIN (y1, max_yspan_y2 - 1) - max_yspan_y1;
      if (y2 > max_yspan_y1)
        y2 -= MIN (y2, max_yspan_y2 - 1) - max_yspan_y1;

      shape->rectangles[iter.i].x = x1 - extents.x;
      shape->rectangles[iter.i].y = y1 - extents.y;
      shape->rectangles[iter.i].width = x2 - x1;
      shape->rectangles[iter.i].height = y2 - y1;

#if 0
      g_print ("%d: +%d+%dx%dx%d => +%d+%dx%dx%d\n",
               iter.i, iter.rectangle.x, iter.rectangle.y, iter.rectangle.width, iter.rectangle.height,
               shape->rectangles[iter.i].x, shape->rectangles[iter.i].y,
               hape->rectangles[iter.i].width, shape->rectangles[iter.i].height);
#endif

      hash = hash * 31 + x1 * 17 + x2 * 27 + y1 * 37 + y2 * 43;
    }

  shape->hash = hash;

#if 0
  g_print ("%d %d %d %d: %#x\n\n", shape->top, shape->right, shape->bottom, shape->left, shape->hash);
#endif

  return shape;
}

CobiwmWindowShape *
cobiwm_window_shape_ref (CobiwmWindowShape *shape)
{
  shape->ref_count++;

  return shape;
}

void
cobiwm_window_shape_unref (CobiwmWindowShape *shape)
{
  shape->ref_count--;
  if (shape->ref_count == 0)
    {
      g_free (shape->rectangles);
      g_slice_free (CobiwmWindowShape, shape);
    }
}

guint
cobiwm_window_shape_hash (CobiwmWindowShape *shape)
{
  return shape->hash;
}

gboolean
cobiwm_window_shape_equal (CobiwmWindowShape *shape_a,
                         CobiwmWindowShape *shape_b)
{
  if (shape_a->n_rectangles != shape_b->n_rectangles)
    return FALSE;

  return memcmp (shape_a->rectangles, shape_b->rectangles,
                 sizeof (cairo_rectangle_int_t) * shape_a->n_rectangles) == 0;
}

void
cobiwm_window_shape_get_borders (CobiwmWindowShape *shape,
                               int             *border_top,
                               int             *border_right,
                               int             *border_bottom,
                               int             *border_left)
{
  if (border_top)
    *border_top = shape->top;
  if (border_right)
    *border_right = shape->right;
  if (border_bottom)
    *border_bottom = shape->bottom;
  if (border_left)
    *border_left = shape->left;
}

/**
 * cobiwm_window_shape_to_region:
 * @shape: a #CobiwmWindowShape
 * @center_width: size of the central region horizontally
 * @center_height: size of the central region vertically
 *
 * Converts the shape to to a cairo_region_t using the given width
 * and height for the central scaled region.
 *
 * Return value: a newly created region
 */
cairo_region_t *
cobiwm_window_shape_to_region (CobiwmWindowShape *shape,
                             int              center_width,
                             int              center_height)
{
  cairo_region_t *region;
  int i;

  region = cairo_region_create ();

  for (i = 0; i < shape->n_rectangles; i++)
    {
      cairo_rectangle_int_t rect = shape->rectangles[i];

      if (rect.x <= shape->left && rect.x + rect.width >= shape->left + 1)
        rect.width += center_width;
      else if (rect.x >= shape->left + 1)
        rect.x += center_width;

      if (rect.y <= shape->top && rect.y + rect.height >= shape->top + 1)
        rect.height += center_height;
      else if (rect.y >= shape->top + 1)
        rect.y += center_height;

      cairo_region_union_rectangle (region, &rect);
    }

  return region;
}

G_DEFINE_BOXED_TYPE (CobiwmWindowShape, cobiwm_window_shape,
                     cobiwm_window_shape_ref, cobiwm_window_shape_unref)
