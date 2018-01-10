/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * cobiwm-background-actor.h:  for painting the root window background
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

#ifndef COBIWM_BACKGROUND_H
#define COBIWM_BACKGROUND_H

#include <clutter/clutter.h>
#include <gsettings-desktop-schemas/gdesktop-enums.h>
#include <screen.h>

/**
 * CobiwmBackground:
 *
 * This class handles tracking and painting the root window background.
 * By integrating with #CobiwmWindowGroup we can avoid painting parts of
 * the background that are obscured by other windows.
 */

#define COBIWM_TYPE_BACKGROUND            (cobiwm_background_get_type ())
#define COBIWM_BACKGROUND(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), COBIWM_TYPE_BACKGROUND, CobiwmBackground))
#define COBIWM_BACKGROUND_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), COBIWM_TYPE_BACKGROUND, CobiwmBackgroundClass))
#define COBIWM_IS_BACKGROUND(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), COBIWM_TYPE_BACKGROUND))
#define COBIWM_IS_BACKGROUND_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), COBIWM_TYPE_BACKGROUND))
#define COBIWM_BACKGROUND_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), COBIWM_TYPE_BACKGROUND, CobiwmBackgroundClass))

typedef struct _CobiwmBackground        CobiwmBackground;
typedef struct _CobiwmBackgroundClass   CobiwmBackgroundClass;
typedef struct _CobiwmBackgroundPrivate CobiwmBackgroundPrivate;

struct _CobiwmBackgroundClass
{
  /*< private >*/
  GObjectClass parent_class;
};

struct _CobiwmBackground
{
  GObject parent;

  CobiwmBackgroundPrivate *priv;
};

void cobiwm_background_refresh_all (void);

GType cobiwm_background_get_type (void);

CobiwmBackground *cobiwm_background_new  (CobiwmScreen *screen);

void cobiwm_background_set_color    (CobiwmBackground            *self,
                                   ClutterColor              *color);
void cobiwm_background_set_gradient (CobiwmBackground            *self,
                                   GDesktopBackgroundShading  shading_direction,
                                   ClutterColor              *color,
                                   ClutterColor              *second_color);
void cobiwm_background_set_file     (CobiwmBackground            *self,
                                   GFile                     *file,
                                   GDesktopBackgroundStyle    style);
void cobiwm_background_set_blend    (CobiwmBackground            *self,
                                   GFile                     *file1,
                                   GFile                     *file2,
                                   double                     blend_factor,
                                   GDesktopBackgroundStyle    style);

#endif /* COBIWM_BACKGROUND_H */
