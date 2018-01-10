/*
 * shaped texture
 *
 * An actor to draw a texture clipped to a list of rectangles
 *
 * Authored By Neil Roberts  <neil@linux.intel.com>
 *
 * Copyright (C) 2008 Intel Corporation
 *               2013 Red Hat, Inc.
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
 */

#ifndef __COBIWM_SHAPED_TEXTURE_PRIVATE_H__
#define __COBIWM_SHAPED_TEXTURE_PRIVATE_H__

#include <cobiwm-shaped-texture.h>

ClutterActor *cobiwm_shaped_texture_new (void);
void cobiwm_shaped_texture_set_texture (CobiwmShapedTexture *stex,
                                      CoglTexture       *texture);
void cobiwm_shaped_texture_set_fallback_size (CobiwmShapedTexture *stex,
                                            guint              fallback_width,
                                            guint              fallback_height);
gboolean cobiwm_shaped_texture_is_obscured (CobiwmShapedTexture *self);
cairo_region_t * cobiwm_shaped_texture_get_opaque_region (CobiwmShapedTexture *stex);

#endif
