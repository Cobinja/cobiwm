/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

#ifndef COBIWM_BACKGROUND_GROUP_H
#define COBIWM_BACKGROUND_GROUP_H

#include <clutter/clutter.h>

#define COBIWM_TYPE_BACKGROUND_GROUP            (cobiwm_background_group_get_type ())
#define COBIWM_BACKGROUND_GROUP(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), COBIWM_TYPE_BACKGROUND_GROUP, CobiwmBackgroundGroup))
#define COBIWM_BACKGROUND_GROUP_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), COBIWM_TYPE_BACKGROUND_GROUP, CobiwmBackgroundGroupClass))
#define COBIWM_IS_BACKGROUND_GROUP(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), COBIWM_TYPE_BACKGROUND_GROUP))
#define COBIWM_IS_BACKGROUND_GROUP_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), COBIWM_TYPE_BACKGROUND_GROUP))
#define COBIWM_BACKGROUND_GROUP_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), COBIWM_TYPE_BACKGROUND_GROUP, CobiwmBackgroundGroupClass))

typedef struct _CobiwmBackgroundGroup        CobiwmBackgroundGroup;
typedef struct _CobiwmBackgroundGroupClass   CobiwmBackgroundGroupClass;
typedef struct _CobiwmBackgroundGroupPrivate CobiwmBackgroundGroupPrivate;

struct _CobiwmBackgroundGroupClass
{
  /*< private >*/
  ClutterActorClass parent_class;
};

struct _CobiwmBackgroundGroup
{
  /*< private >*/
  ClutterActor parent;

  CobiwmBackgroundGroupPrivate *priv;
};

GType cobiwm_background_group_get_type (void);

ClutterActor *cobiwm_background_group_new (void);

#endif /* COBIWM_BACKGROUND_GROUP_H */
