/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

#ifndef COBIWM_WINDOW_GROUP_H
#define COBIWM_WINDOW_GROUP_H

#include <clutter/clutter.h>

#include <screen.h>

/**
 * CobiwmWindowGroup:
 *
 * This class is a subclass of ClutterActor with special handling for
 * #CobiwmCullable when painting children. It uses code similar to
 * cobiwm_cullable_cull_out_children(), but also has additional special
 * cases for the undirected window, and similar.
 */

#define COBIWM_TYPE_WINDOW_GROUP            (cobiwm_window_group_get_type ())
#define COBIWM_WINDOW_GROUP(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), COBIWM_TYPE_WINDOW_GROUP, CobiwmWindowGroup))
#define COBIWM_WINDOW_GROUP_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), COBIWM_TYPE_WINDOW_GROUP, CobiwmWindowGroupClass))
#define COBIWM_IS_WINDOW_GROUP(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), COBIWM_TYPE_WINDOW_GROUP))
#define COBIWM_IS_WINDOW_GROUP_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), COBIWM_TYPE_WINDOW_GROUP))
#define COBIWM_WINDOW_GROUP_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), COBIWM_TYPE_WINDOW_GROUP, CobiwmWindowGroupClass))

typedef struct _CobiwmWindowGroup        CobiwmWindowGroup;
typedef struct _CobiwmWindowGroupClass   CobiwmWindowGroupClass;
typedef struct _CobiwmWindowGroupPrivate CobiwmWindowGroupPrivate;

GType cobiwm_window_group_get_type (void);

ClutterActor *cobiwm_window_group_new (CobiwmScreen *screen);

gboolean cobiwm_window_group_actor_is_untransformed (ClutterActor *actor,
                                                   int          *x_origin,
                                                   int          *y_origin);
#endif /* COBIWM_WINDOW_GROUP_H */
