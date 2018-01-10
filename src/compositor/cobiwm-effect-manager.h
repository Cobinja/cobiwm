/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (c) 2008 Intel Corp.
 *
 * Author: Tomas Frydrych <tf@linux.intel.com>
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

#ifndef COBIWM_EFFECT_MANAGER_H_
#define COBIWM_EFFECT_MANAGER_H_

#include <types.h>
#include <screen.h>
#include <cobiwm-plugin.h>

typedef enum {
  COBIWM_EFFECT_NONE,
  COBIWM_EFFECT_MINIMIZE,
  COBIWM_EFFECT_MAP,
  COBIWM_EFFECT_DESTROY,
  COBIWM_EFFECT_SWITCH_WORKSPACE,
  COBIWM_EFFECT_UNMINIMIZE,
  COBIWM_EFFECT_SIZE_CHANGE,
} CobiwmEffect;
 
#define COBIWM_TYPE_EFFECT_MANAGER            (cobiwm_effect_manager_get_type ())
#define COBIWM_EFFECT_MANAGER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), COBIWM_TYPE_EFFECT_MANAGER, CobiwmEffectManager))
#define COBIWM_EFFECT_MANAGER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  COBIWM_TYPE_EFFECT_MANAGER, CobiwmEffectManagerClass))
#define COBIWM_IS_EFFECT_MANAGER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), COBIWM_TYPE_EFFECT_MANAGER))
#define COBIWM_IS_EFFECT_MANAGER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  COBIWM_TYPE_EFFECT_MANAGER))
#define COBIWM_EFFECT_MANAGER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  COBIWM_TYPE_EFFECT_MANAGER, CobiwmEffectManagerClass))

typedef struct _CobiwmEffectManager CobiwmEffectManager;
typedef struct _CobiwmEffectManagerClass   CobiwmEffectManagerClass;

struct _CobiwmEffectManagerClass
{
  GObjectClass parent_class;
};

CobiwmEffectManager *cobiwm_effect_manager_new (CobiwmCompositor *compositor);

gboolean cobiwm_effect_manager_event_simple (CobiwmEffectManager *mgr,
                                           CobiwmWindowActor   *actor,
                                           CobiwmEffect   event);

gboolean cobiwm_effect_manager_switch_workspace (CobiwmEffectManager   *mgr,
                                               gint                 from,
                                               gint                 to,
                                               CobiwmMotionDirection  direction);

void     cobiwm_effect_manager_confirm_display_change (CobiwmEffectManager *mgr);

void cobiwm_effect_manager_show_tile_preview (CobiwmEffectManager *mgr,
                                                CobiwmWindow        *window,
                                                CobiwmRectangle     *tile_rect,
                                                int                tile_monitor_number);
void cobiwm_effect_manager_hide_tile_preview (CobiwmEffectManager *mgr);

void cobiwm_effect_manager_show_window_menu (CobiwmEffectManager  *mgr,
                                           CobiwmWindow         *window,
                                           CobiwmWindowMenuType  menu,
                                           int                 x,
                                           int                 y);

void cobiwm_effect_manager_show_window_menu_for_rect (CobiwmEffectManager  *mgr,
		                                    CobiwmWindow         *window,
						    CobiwmWindowMenuType  menu,
						    CobiwmRectangle      *rect);

GType cobiwm_effect_manager_get_type (void);
#endif
