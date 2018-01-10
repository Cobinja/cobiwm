/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Simple box operations */

/*
 * Copyright (C) 2005, 2006 Elijah Newren
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

#ifndef COBIWM_BOXES_H
#define COBIWM_BOXES_H

#include <glib-object.h>
#include <common.h>

#define COBIWM_TYPE_RECTANGLE            (cobiwm_rectangle_get_type ())

/**
 * CobiwmRectangle:
 * @x: X coordinate of the top-left corner
 * @y: Y coordinate of the top-left corner
 * @width: Width of the rectangle
 * @height: Height of the rectangle
 */
typedef struct _CobiwmRectangle CobiwmRectangle;
struct _CobiwmRectangle
{
  int x;
  int y;
  int width;
  int height;
};

/**
 * CobiwmStrut:
 * @rect: #CobiwmRectangle the #CobiwmStrut is on
 * @side: #CobiwmSide the #CobiwmStrut is on
 */
typedef struct _CobiwmStrut CobiwmStrut;
struct _CobiwmStrut
{
  CobiwmRectangle rect;
  CobiwmSide side;
};

/**
 * CobiwmEdgeType:
 * @COBIWM_EDGE_WINDOW: Whether the edge belongs to a window
 * @COBIWM_EDGE_MONITOR: Whether the edge belongs to a monitor
 * @COBIWM_EDGE_SCREEN: Whether the edge belongs to a screen
 */
typedef enum
{
  COBIWM_EDGE_WINDOW,
  COBIWM_EDGE_MONITOR,
  COBIWM_EDGE_SCREEN
} CobiwmEdgeType;

/**
 * CobiwmEdge:
 * @rect: #CobiwmRectangle with the bounds of the edge
 * @side_type: Side
 * @edge_type: To what belongs the edge
 */
typedef struct _CobiwmEdge CobiwmEdge;
struct _CobiwmEdge
{
  CobiwmRectangle rect;      /* width or height should be 1 */
  CobiwmSide side_type;
  CobiwmEdgeType  edge_type;
};

GType cobiwm_rectangle_get_type (void);

CobiwmRectangle *cobiwm_rectangle_copy (const CobiwmRectangle *rect);
void           cobiwm_rectangle_free (CobiwmRectangle       *rect);

/* Function to make initializing a rect with a single line of code easy */
CobiwmRectangle                 cobiwm_rect (int x, int y, int width, int height);

/* Basic comparison functions */
int      cobiwm_rectangle_area            (const CobiwmRectangle *rect);
gboolean cobiwm_rectangle_intersect       (const CobiwmRectangle *src1,
                                         const CobiwmRectangle *src2,
                                         CobiwmRectangle       *dest);
gboolean cobiwm_rectangle_equal           (const CobiwmRectangle *src1,
                                         const CobiwmRectangle *src2);

/* Find the bounding box of the union of two rectangles */
void     cobiwm_rectangle_union           (const CobiwmRectangle *rect1,
                                         const CobiwmRectangle *rect2,
                                         CobiwmRectangle       *dest);

/* overlap is similar to intersect but doesn't provide location of
 * intersection information.
 */
gboolean cobiwm_rectangle_overlap         (const CobiwmRectangle *rect1,
                                         const CobiwmRectangle *rect2);

/* vert_overlap means ignore the horizontal location and ask if the
 * vertical parts overlap.  An alternate way to think of it is "Does there
 * exist a way to shift either rect horizontally so that the two rects
 * overlap?"  horiz_overlap is similar.
 */
gboolean cobiwm_rectangle_vert_overlap    (const CobiwmRectangle *rect1,
                                         const CobiwmRectangle *rect2);
gboolean cobiwm_rectangle_horiz_overlap   (const CobiwmRectangle *rect1,
                                         const CobiwmRectangle *rect2);

/* could_fit_rect determines whether "outer_rect" is big enough to contain
 * inner_rect.  contains_rect checks whether it actually contains it.
 */
gboolean cobiwm_rectangle_could_fit_rect  (const CobiwmRectangle *outer_rect,
                                         const CobiwmRectangle *inner_rect);
gboolean cobiwm_rectangle_contains_rect   (const CobiwmRectangle *outer_rect,
                                         const CobiwmRectangle *inner_rect);

#endif /* COBIWM_BOXES_H */
