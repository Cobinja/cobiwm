/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2013 Red Hat
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
 *
 * Written by:
 *     Owen Taylor <otaylor@redhat.com>
 *     Ray Strode <rstrode@redhat.com>
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

#ifndef __COBIWM_CULLABLE_H__
#define __COBIWM_CULLABLE_H__

#include <clutter/clutter.h>

G_BEGIN_DECLS

#define COBIWM_TYPE_CULLABLE             (cobiwm_cullable_get_type ())
#define COBIWM_CULLABLE(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), COBIWM_TYPE_CULLABLE, CobiwmCullable))
#define COBIWM_IS_CULLABLE(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), COBIWM_TYPE_CULLABLE))
#define COBIWM_CULLABLE_GET_IFACE(obj)   (G_TYPE_INSTANCE_GET_INTERFACE ((obj),  COBIWM_TYPE_CULLABLE, CobiwmCullableInterface))

typedef struct _CobiwmCullable CobiwmCullable;
typedef struct _CobiwmCullableInterface CobiwmCullableInterface;

struct _CobiwmCullableInterface
{
  GTypeInterface g_iface;

  void (* cull_out)      (CobiwmCullable   *cullable,
                          cairo_region_t *unobscured_region,
                          cairo_region_t *clip_region);
  void (* reset_culling) (CobiwmCullable  *cullable);
};

GType cobiwm_cullable_get_type (void);

void cobiwm_cullable_cull_out (CobiwmCullable   *cullable,
                             cairo_region_t *unobscured_region,
                             cairo_region_t *clip_region);
void cobiwm_cullable_reset_culling (CobiwmCullable *cullable);

/* Utility methods for implementations */
void cobiwm_cullable_cull_out_children (CobiwmCullable   *cullable,
                                      cairo_region_t *unobscured_region,
                                      cairo_region_t *clip_region);
void cobiwm_cullable_reset_culling_children (CobiwmCullable *cullable);

G_END_DECLS

#endif /* __COBIWM_CULLABLE_H__ */

