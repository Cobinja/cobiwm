/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * CobiwmBackgroundImageCache:
 *
 * Simple cache for background textures loaded from files
 *
 * Copyright 2014 Red Hat, Inc.
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

#ifndef __COBIWM_BACKGROUND_IMAGE_H__
#define __COBIWM_BACKGROUND_IMAGE_H__

#include <glib-object.h>
#include <cogl/cogl.h>

#define COBIWM_TYPE_BACKGROUND_IMAGE            (cobiwm_background_image_get_type ())
#define COBIWM_BACKGROUND_IMAGE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), COBIWM_TYPE_BACKGROUND_IMAGE, CobiwmBackgroundImage))
#define COBIWM_BACKGROUND_IMAGE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  COBIWM_TYPE_BACKGROUND_IMAGE, CobiwmBackgroundImageClass))
#define COBIWM_IS_BACKGROUND_IMAGE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), COBIWM_TYPE_BACKGROUND_IMAGE))
#define COBIWM_IS_BACKGROUND_IMAGE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  COBIWM_TYPE_BACKGROUND_IMAGE))
#define COBIWM_BACKGROUND_IMAGE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  COBIWM_TYPE_BACKGROUND_IMAGE, CobiwmBackgroundImageClass))

/**
 * CobiwmBackgroundImage:
 *
 * #CobiwmBackgroundImage is an object that represents a loaded or loading background image.
 */
typedef struct _CobiwmBackgroundImage      CobiwmBackgroundImage;
typedef struct _CobiwmBackgroundImageClass CobiwmBackgroundImageClass;

GType cobiwm_background_image_get_type (void);

gboolean     cobiwm_background_image_is_loaded   (CobiwmBackgroundImage *image);
gboolean     cobiwm_background_image_get_success (CobiwmBackgroundImage *image);
CoglTexture *cobiwm_background_image_get_texture (CobiwmBackgroundImage *image);

#define COBIWM_TYPE_BACKGROUND_IMAGE_CACHE            (cobiwm_background_image_cache_get_type ())
#define COBIWM_BACKGROUND_IMAGE_CACHE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), COBIWM_TYPE_BACKGROUND_IMAGE_CACHE, CobiwmBackgroundImageCache))
#define COBIWM_BACKGROUND_IMAGE_CACHE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  COBIWM_TYPE_BACKGROUND_IMAGE_CACHE, CobiwmBackgroundImageCacheClass))
#define COBIWM_IS_BACKGROUND_IMAGE_CACHE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), COBIWM_TYPE_BACKGROUND_IMAGE_CACHE))
#define COBIWM_IS_BACKGROUND_IMAGE_CACHE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  COBIWM_TYPE_BACKGROUND_IMAGE_CACHE))
#define COBIWM_BACKGROUND_IMAGE_CACHE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  COBIWM_TYPE_BACKGROUND_IMAGE_CACHE, CobiwmBackgroundImageCacheClass))

/**
 * CobiwmBackgroundImageCache:
 *
 * #CobiwmBackgroundImageCache caches loading of textures for backgrounds; there's actually
 * nothing background specific about it, other than it is tuned to work well for
 * large images as typically are used for backgrounds.
 */
typedef struct _CobiwmBackgroundImageCache      CobiwmBackgroundImageCache;
typedef struct _CobiwmBackgroundImageCacheClass CobiwmBackgroundImageCacheClass;

CobiwmBackgroundImageCache *cobiwm_background_image_cache_get_default (void);

GType cobiwm_background_image_cache_get_type (void);

CobiwmBackgroundImage *cobiwm_background_image_cache_load  (CobiwmBackgroundImageCache *cache,
                                                        GFile                    *file);
void                 cobiwm_background_image_cache_purge (CobiwmBackgroundImageCache *cache,
                                                        GFile                    *file);

#endif /* __COBIWM_BACKGROUND_IMAGE_H__ */
