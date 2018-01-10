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

#ifndef COBIWM_BOXES_PRIVATE_H
#define COBIWM_BOXES_PRIVATE_H

#include <glib-object.h>
#include <common.h>
#include <boxes.h>

#define BOX_LEFT(box)    ((box).x)                /* Leftmost pixel of rect */
#define BOX_RIGHT(box)   ((box).x + (box).width)  /* One pixel past right   */
#define BOX_TOP(box)     ((box).y)                /* Topmost pixel of rect  */
#define BOX_BOTTOM(box)  ((box).y + (box).height) /* One pixel past bottom  */

typedef enum
{
  FIXED_DIRECTION_NONE = 0,
  FIXED_DIRECTION_X    = 1 << 0,
  FIXED_DIRECTION_Y    = 1 << 1,
} FixedDirections;

/* Output functions -- note that the output buffer had better be big enough:
 *   rect_to_string:   RECT_LENGTH
 *   region_to_string: (RECT_LENGTH+strlen(separator_string)) *
 *                     g_list_length (region)
 *   edge_to_string:   EDGE_LENGTH
 *   edge_list_to_...: (EDGE_LENGTH+strlen(separator_string)) *
 *                     g_list_length (edge_list)
 */
#define RECT_LENGTH 27
#define EDGE_LENGTH 37
char* cobiwm_rectangle_to_string        (const CobiwmRectangle *rect,
                                       char                *output);
char* cobiwm_rectangle_region_to_string (GList               *region,
                                       const char          *separator_string,
                                       char                *output);
char* cobiwm_rectangle_edge_to_string   (const CobiwmEdge      *edge,
                                       char                *output);
char* cobiwm_rectangle_edge_list_to_string (
                                       GList               *edge_list,
                                       const char          *separator_string,
                                       char                *output);

/* Resize old_rect to the given new_width and new_height, but store the
 * result in rect.  NOTE THAT THIS IS RESIZE ONLY SO IT CANNOT BE USED FOR
 * A MOVERESIZE OPERATION (that simplies the routine a little bit as it
 * means there's no difference between NorthWestGravity and StaticGravity.
 * Also, I lied a little bit--technically, you could use it in a MoveResize
 * operation if you muck with old_rect just right).
 */
void cobiwm_rectangle_resize_with_gravity (const CobiwmRectangle *old_rect,
                                         CobiwmRectangle       *rect,
                                         int                  gravity,
                                         int                  new_width,
                                         int                  new_height);

/* find a list of rectangles with the property that a window is contained
 * in the given region if and only if it is contained in one of the
 * rectangles in the list.
 *
 * In this case, the region is given by taking basic_rect, removing from
 * it the intersections with all the rectangles in the all_struts list,
 * then expanding all the rectangles in the resulting list by the given
 * amounts on each side.
 *
 * See boxes.c for more details.
 */
GList*   cobiwm_rectangle_get_minimal_spanning_set_for_region (
                                         const CobiwmRectangle *basic_rect,
                                         const GSList        *all_struts);

/* Expand all rectangles in region by the given amount on each side */
GList*   cobiwm_rectangle_expand_region   (GList               *region,
                                         const int            left_expand,
                                         const int            right_expand,
                                         const int            top_expand,
                                         const int            bottom_expand);
/* Same as for cobiwm_rectangle_expand_region except that rectangles not at
 * least min_x or min_y in size are not expanded in that direction
 */
GList*   cobiwm_rectangle_expand_region_conditionally (
                                         GList                *region,
                                         const int            left_expand,
                                         const int            right_expand,
                                         const int            top_expand,
                                         const int            bottom_expand,
                                         const int            min_x,
                                         const int            min_y);

/* Expand rect in direction to the size of expand_to, and then clip out any
 * overlapping struts oriented orthognal to the expansion direction.  (Think
 * horizontal or vertical maximization)
 */
void     cobiwm_rectangle_expand_to_avoiding_struts (
                                         CobiwmRectangle       *rect,
                                         const CobiwmRectangle *expand_to,
                                         const CobiwmDirection  direction,
                                         const GSList        *all_struts);

/* Free the list created by
 *   cobiwm_rectangle_get_minimal_spanning_set_for_region()
 * or
 *   cobiwm_rectangle_find_onscreen_edges ()
 * or
 *   cobiwm_rectangle_find_nonintersected_monitor_edges()
 */
void     cobiwm_rectangle_free_list_and_elements (GList *filled_list);

/* could_fit_in_region determines whether one of the spanning_rects is
 * big enough to contain rect.  contained_in_region checks whether one
 * actually contains it.
 */
gboolean cobiwm_rectangle_could_fit_in_region (
                                         const GList         *spanning_rects,
                                         const CobiwmRectangle *rect);
gboolean cobiwm_rectangle_contained_in_region (
                                         const GList         *spanning_rects,
                                         const CobiwmRectangle *rect);
gboolean cobiwm_rectangle_overlaps_with_region (
                                         const GList         *spanning_rects,
                                         const CobiwmRectangle *rect);

/* Make the rectangle small enough to fit into one of the spanning_rects,
 * but make it no smaller than min_size.
 */
void     cobiwm_rectangle_clamp_to_fit_into_region (
                                         const GList         *spanning_rects,
                                         FixedDirections      fixed_directions,
                                         CobiwmRectangle       *rect,
                                         const CobiwmRectangle *min_size);

/* Clip the rectangle so that it fits into one of the spanning_rects, assuming
 * it overlaps with at least one of them
 */
void     cobiwm_rectangle_clip_to_region  (const GList         *spanning_rects,
                                         FixedDirections      fixed_directions,
                                         CobiwmRectangle       *rect);

/* Shove the rectangle into one of the spanning_rects, assuming it fits in
 * one of them.
 */
void     cobiwm_rectangle_shove_into_region(
                                         const GList         *spanning_rects,
                                         FixedDirections      fixed_directions,
                                         CobiwmRectangle       *rect);

/* Finds the point on the line connecting (x1,y1) to (x2,y2) which is closest
 * to (px, py).  Useful for finding an optimal rectangle size when given a
 * range between two sizes that are all candidates.
 */
void cobiwm_rectangle_find_linepoint_closest_to_point (double x1,    double y1,
                                                     double x2,    double y2,
                                                     double px,    double py,
                                                     double *valx, double *valy);

/***************************************************************************/
/*                                                                         */
/* Switching gears to code for edges instead of just rectangles            */
/*                                                                         */
/***************************************************************************/

/* Return whether an edge overlaps or is adjacent to the rectangle in the
 * nonzero-width dimension of the edge.
 */
gboolean cobiwm_rectangle_edge_aligns (const CobiwmRectangle *rect,
                                     const CobiwmEdge      *edge);

/* Compare two edges, so that sorting functions can put a list of edges in
 * canonical order.
 */
gint   cobiwm_rectangle_edge_cmp (gconstpointer a, gconstpointer b);

/* Compare two edges, so that sorting functions can put a list of edges in
 * order.  This function doesn't separate left edges first, then right edges,
 * etc., but rather compares only upon location.
 */
gint   cobiwm_rectangle_edge_cmp_ignore_type (gconstpointer a, gconstpointer b);

/* Removes an parts of edges in the given list that intersect any box in the
 * given rectangle list.  Returns the result.
 */
GList* cobiwm_rectangle_remove_intersections_with_boxes_from_edges (
                                           GList *edges,
                                           const GSList *rectangles);

/* Finds all the edges of an onscreen region, returning a GList* of
 * CobiwmEdgeRect's.
 */
GList* cobiwm_rectangle_find_onscreen_edges (const CobiwmRectangle *basic_rect,
                                           const GSList        *all_struts);

/* Finds edges between adjacent monitors which are not covered by the given
 * struts.
 */
GList* cobiwm_rectangle_find_nonintersected_monitor_edges (
                                           const GList         *monitor_rects,
                                           const GSList        *all_struts);

#endif /* COBIWM_BOXES_PRIVATE_H */
