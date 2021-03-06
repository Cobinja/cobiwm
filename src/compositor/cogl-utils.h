/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Utilities for use with Cogl
 *
 * Copyright 2010 Red Hat, Inc.
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

#ifndef __COBIWM_COGL_UTILS_H__
#define __COBIWM_COGL_UTILS_H__

#include <cogl/cogl.h>

CoglPipeline * cobiwm_create_texture_pipeline (CoglTexture *texture);

typedef enum {
  COBIWM_TEXTURE_FLAGS_NONE = 0,
  COBIWM_TEXTURE_ALLOW_SLICING = 1 << 1
} CobiwmTextureFlags;

CoglTexture *cobiwm_create_texture (int                   width,
                                  int                   height,
                                  CoglTextureComponents components,
                                  CobiwmTextureFlags      flags);

#endif /* __COBIWM_COGL_UTILS_H__ */
