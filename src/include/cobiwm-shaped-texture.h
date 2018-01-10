/*
 * shaped texture
 *
 * An actor to draw a texture clipped to a list of rectangles
 *
 * Authored By Neil Roberts  <neil@linux.intel.com>
 *
 * Copyright (C) 2008 Intel Corporation
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

#ifndef __COBIWM_SHAPED_TEXTURE_H__
#define __COBIWM_SHAPED_TEXTURE_H__

#include <clutter/clutter.h>
#include <X11/Xlib.h>

G_BEGIN_DECLS

#define COBIWM_TYPE_SHAPED_TEXTURE            (cobiwm_shaped_texture_get_type())
#define COBIWM_SHAPED_TEXTURE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj),COBIWM_TYPE_SHAPED_TEXTURE, CobiwmShapedTexture))
#define COBIWM_SHAPED_TEXTURE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), COBIWM_TYPE_SHAPED_TEXTURE, CobiwmShapedTextureClass))
#define COBIWM_IS_SHAPED_TEXTURE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), COBIWM_TYPE_SHAPED_TEXTURE))
#define COBIWM_IS_SHAPED_TEXTURE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), COBIWM_TYPE_SHAPED_TEXTURE))
#define COBIWM_SHAPED_TEXTURE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), COBIWM_TYPE_SHAPED_TEXTURE, CobiwmShapedTextureClass))

typedef struct _CobiwmShapedTexture        CobiwmShapedTexture;
typedef struct _CobiwmShapedTextureClass   CobiwmShapedTextureClass;
typedef struct _CobiwmShapedTexturePrivate CobiwmShapedTexturePrivate;

struct _CobiwmShapedTextureClass
{
  /*< private >*/
  ClutterActorClass parent_class;
};

/**
 * CobiwmShapedTexture:
 *
 * The <structname>CobiwmShapedTexture</structname> structure contains
 * only private data and should be accessed using the provided API
 */
struct _CobiwmShapedTexture
{
  /*< private >*/
  ClutterActor parent;

  CobiwmShapedTexturePrivate *priv;
};

GType cobiwm_shaped_texture_get_type (void) G_GNUC_CONST;

void cobiwm_shaped_texture_set_create_mipmaps (CobiwmShapedTexture *stex,
					     gboolean           create_mipmaps);

gboolean cobiwm_shaped_texture_update_area (CobiwmShapedTexture *stex,
                                          int                x,
                                          int                y,
                                          int                width,
                                          int                height);

CoglTexture * cobiwm_shaped_texture_get_texture (CobiwmShapedTexture *stex);

void cobiwm_shaped_texture_set_mask_texture (CobiwmShapedTexture *stex,
                                           CoglTexture       *mask_texture);
void cobiwm_shaped_texture_set_opaque_region (CobiwmShapedTexture *stex,
                                            cairo_region_t    *opaque_region);

cairo_surface_t * cobiwm_shaped_texture_get_image (CobiwmShapedTexture     *stex,
                                                 cairo_rectangle_int_t *clip);

G_END_DECLS

#endif /* __COBIWM_SHAPED_TEXTURE_H__ */
