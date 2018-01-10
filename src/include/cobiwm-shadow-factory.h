/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * CobiwmShadowFactory:
 *
 * Create and cache shadow textures for arbitrary window shapes
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

#ifndef __COBIWM_SHADOW_FACTORY_H__
#define __COBIWM_SHADOW_FACTORY_H__

#include <cairo.h>
#include <clutter/clutter.h>
#include <cobiwm-window-shape.h>

GType cobiwm_shadow_get_type (void) G_GNUC_CONST;

/**
 * CobiwmShadowParams:
 * @radius: the radius (gaussian standard deviation) of the shadow
 * @top_fade: if >= 0, the shadow doesn't extend above the top
 *  of the shape, and fades out over the given number of pixels
 * @x_offset: horizontal offset of the shadow with respect to the
 *  shape being shadowed, in pixels
 * @y_offset: vertical offset of the shadow with respect to the
 *  shape being shadowed, in pixels
 * @opacity: opacity of the shadow, from 0 to 255
 *
 * The #CobiwmShadowParams structure holds information about how to draw
 * a particular style of shadow.
 */

typedef struct _CobiwmShadowParams CobiwmShadowParams;

struct _CobiwmShadowParams
{
  int radius;
  int top_fade;
  int x_offset;
  int y_offset;
  guint8 opacity;
};

#define COBIWM_TYPE_SHADOW_FACTORY            (cobiwm_shadow_factory_get_type ())
#define COBIWM_SHADOW_FACTORY(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), COBIWM_TYPE_SHADOW_FACTORY, CobiwmShadowFactory))
#define COBIWM_SHADOW_FACTORY_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  COBIWM_TYPE_SHADOW_FACTORY, CobiwmShadowFactoryClass))
#define COBIWM_IS_SHADOW_FACTORY(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), COBIWM_TYPE_SHADOW_FACTORY))
#define COBIWM_IS_SHADOW_FACTORY_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  COBIWM_TYPE_SHADOW_FACTORY))
#define COBIWM_SHADOW_FACTORY_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  COBIWM_TYPE_SHADOW_FACTORY, CobiwmShadowFactoryClass))

/**
 * CobiwmShadowFactory:
 *
 * #CobiwmShadowFactory is used to create window shadows. It caches shadows internally
 * so that multiple shadows created for the same shape with the same radius will
 * share the same CobiwmShadow.
 */
typedef struct _CobiwmShadowFactory      CobiwmShadowFactory;
typedef struct _CobiwmShadowFactoryClass CobiwmShadowFactoryClass;

CobiwmShadowFactory *cobiwm_shadow_factory_get_default (void);

GType cobiwm_shadow_factory_get_type (void);

void cobiwm_shadow_factory_set_params (CobiwmShadowFactory *factory,
                                     const char        *class_name,
                                     gboolean           focused,
                                     CobiwmShadowParams  *params);
void cobiwm_shadow_factory_get_params (CobiwmShadowFactory *factory,
                                     const char        *class_name,
                                     gboolean           focused,
                                     CobiwmShadowParams  *params);

/**
 * CobiwmShadow:
 * #CobiwmShadow holds a shadow texture along with information about how to
 * apply that texture to draw a window texture. (E.g., it knows how big the
 * unscaled borders are on each side of the shadow texture.)
 */
typedef struct _CobiwmShadow CobiwmShadow;

CobiwmShadow *cobiwm_shadow_ref         (CobiwmShadow            *shadow);
void        cobiwm_shadow_unref       (CobiwmShadow            *shadow);
void        cobiwm_shadow_paint       (CobiwmShadow            *shadow,
                                     int                    window_x,
                                     int                    window_y,
                                     int                    window_width,
                                     int                    window_height,
                                     guint8                 opacity,
                                     cairo_region_t        *clip,
                                     gboolean               clip_strictly);
void        cobiwm_shadow_get_bounds  (CobiwmShadow            *shadow,
                                     int                    window_x,
                                     int                    window_y,
                                     int                    window_width,
                                     int                    window_height,
                                     cairo_rectangle_int_t *bounds);

CobiwmShadowFactory *cobiwm_shadow_factory_new (void);

CobiwmShadow *cobiwm_shadow_factory_get_shadow (CobiwmShadowFactory *factory,
                                            CobiwmWindowShape   *shape,
                                            int                width,
                                            int                height,
                                            const char        *class_name,
                                            gboolean           focused);

#endif /* __COBIWM_SHADOW_FACTORY_H__ */
