/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2014 Red Hat
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
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

#ifndef COBIWM_CURSOR_RENDERER_NATIVE_H
#define COBIWM_CURSOR_RENDERER_NATIVE_H

#include "cobiwm-cursor-renderer.h"

#define COBIWM_TYPE_CURSOR_RENDERER_NATIVE             (cobiwm_cursor_renderer_native_get_type ())
#define COBIWM_CURSOR_RENDERER_NATIVE(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), COBIWM_TYPE_CURSOR_RENDERER_NATIVE, CobiwmCursorRendererNative))
#define COBIWM_CURSOR_RENDERER_NATIVE_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass),  COBIWM_TYPE_CURSOR_RENDERER_NATIVE, CobiwmCursorRendererNativeClass))
#define COBIWM_IS_CURSOR_RENDERER_NATIVE(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), COBIWM_TYPE_CURSOR_RENDERER_NATIVE))
#define COBIWM_IS_CURSOR_RENDERER_NATIVE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass),  COBIWM_TYPE_CURSOR_RENDERER_NATIVE))
#define COBIWM_CURSOR_RENDERER_NATIVE_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj),  COBIWM_TYPE_CURSOR_RENDERER_NATIVE, CobiwmCursorRendererNativeClass))

typedef struct _CobiwmCursorRendererNative        CobiwmCursorRendererNative;
typedef struct _CobiwmCursorRendererNativeClass   CobiwmCursorRendererNativeClass;

struct _CobiwmCursorRendererNative
{
  CobiwmCursorRenderer parent;
};

struct _CobiwmCursorRendererNativeClass
{
  CobiwmCursorRendererClass parent_class;
};

GType cobiwm_cursor_renderer_native_get_type (void) G_GNUC_CONST;

struct gbm_device * cobiwm_cursor_renderer_native_get_gbm_device (CobiwmCursorRendererNative *renderer);
void cobiwm_cursor_renderer_native_get_cursor_size (CobiwmCursorRendererNative *native, uint64_t *width, uint64_t *height);
void cobiwm_cursor_renderer_native_force_update (CobiwmCursorRendererNative *renderer);

#endif /* COBIWM_CURSOR_RENDERER_NATIVE_H */
