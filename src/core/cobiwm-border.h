/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2015 Red Hat
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Written by:
 *     Jonas Ådahl <jadahl@gmail.com>
 */

#ifndef COBIWM_BORDER_H
#define COBIWM_BORDER_H

#include <glib.h>

typedef enum
{
  COBIWM_BORDER_MOTION_DIRECTION_POSITIVE_X = 1 << 0,
  COBIWM_BORDER_MOTION_DIRECTION_POSITIVE_Y = 1 << 1,
  COBIWM_BORDER_MOTION_DIRECTION_NEGATIVE_X = 1 << 2,
  COBIWM_BORDER_MOTION_DIRECTION_NEGATIVE_Y = 1 << 3,
} CobiwmBorderMotionDirection;

typedef struct _CobiwmVector2
{
  float x;
  float y;
} CobiwmVector2;

typedef struct _CobiwmLine2
{
  CobiwmVector2 a;
  CobiwmVector2 b;
} CobiwmLine2;

typedef struct _CobiwmBorder
{
  CobiwmLine2 line;
  CobiwmBorderMotionDirection blocking_directions;
} CobiwmBorder;

static inline CobiwmVector2
cobiwm_vector2_subtract (const CobiwmVector2 a,
                       const CobiwmVector2 b)
{
  return (CobiwmVector2) {
    .x = a.x - b.x,
    .y = a.y - b.y,
  };
}

gboolean
cobiwm_line2_intersects_with (const CobiwmLine2 *line1,
                            const CobiwmLine2 *line2,
                            CobiwmVector2     *intersection);

gboolean
cobiwm_border_is_horizontal (CobiwmBorder *border);

gboolean
cobiwm_border_is_blocking_directions (CobiwmBorder               *border,
                                    CobiwmBorderMotionDirection directions);

unsigned int
cobiwm_border_get_allows_directions (CobiwmBorder *border);

void
cobiwm_border_set_allows_directions (CobiwmBorder *border, unsigned int directions);

#endif /* COBIWM_BORDER_H */
