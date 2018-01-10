/*
 * Copyright (C) 2012 Intel Corporation
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

#ifndef COBIWM_STAGE_H
#define COBIWM_STAGE_H

#include <clutter/clutter.h>

#include "cobiwm-cursor.h"
#include <boxes.h>

G_BEGIN_DECLS

#define COBIWM_TYPE_STAGE            (cobiwm_stage_get_type ())
#define COBIWM_STAGE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), COBIWM_TYPE_STAGE, CobiwmStage))
#define COBIWM_STAGE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  COBIWM_TYPE_STAGE, CobiwmStageClass))
#define COBIWM_IS_STAGE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), COBIWM_TYPE_STAGE))
#define COBIWM_IS_STAGE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  COBIWM_TYPE_STAGE))
#define COBIWM_STAGE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  COBIWM_TYPE_STAGE, CobiwmStageClass))

typedef struct _CobiwmStage      CobiwmStage;
typedef struct _CobiwmStageClass CobiwmStageClass;

struct _CobiwmStageClass
{
  ClutterStageClass parent_class;
};

struct _CobiwmStage
{
  ClutterStage parent;
};

GType             cobiwm_stage_get_type                (void) G_GNUC_CONST;

ClutterActor     *cobiwm_stage_new                     (void);

void cobiwm_stage_set_cursor (CobiwmStage     *stage,
                            CoglTexture   *texture,
                            CobiwmRectangle *rect);

void cobiwm_stage_set_active (CobiwmStage *stage,
                            gboolean   is_active);
G_END_DECLS

#endif /* COBIWM_STAGE_H */
