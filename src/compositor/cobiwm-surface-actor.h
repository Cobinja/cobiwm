/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

#ifndef COBIWM_SURFACE_ACTOR_PRIVATE_H
#define COBIWM_SURFACE_ACTOR_PRIVATE_H

#include <config.h>

#include <cobiwm-shaped-texture.h>
#include <window.h>

G_BEGIN_DECLS

#define COBIWM_TYPE_SURFACE_ACTOR            (cobiwm_surface_actor_get_type())
#define COBIWM_SURFACE_ACTOR(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), COBIWM_TYPE_SURFACE_ACTOR, CobiwmSurfaceActor))
#define COBIWM_SURFACE_ACTOR_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), COBIWM_TYPE_SURFACE_ACTOR, CobiwmSurfaceActorClass))
#define COBIWM_IS_SURFACE_ACTOR(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), COBIWM_TYPE_SURFACE_ACTOR))
#define COBIWM_IS_SURFACE_ACTOR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), COBIWM_TYPE_SURFACE_ACTOR))
#define COBIWM_SURFACE_ACTOR_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), COBIWM_TYPE_SURFACE_ACTOR, CobiwmSurfaceActorClass))

typedef struct _CobiwmSurfaceActor        CobiwmSurfaceActor;
typedef struct _CobiwmSurfaceActorClass   CobiwmSurfaceActorClass;
typedef struct _CobiwmSurfaceActorPrivate CobiwmSurfaceActorPrivate;

struct _CobiwmSurfaceActorClass
{
  /*< private >*/
  ClutterActorClass parent_class;

  void     (* process_damage)    (CobiwmSurfaceActor *actor,
                                  int x, int y, int width, int height);
  void     (* pre_paint)         (CobiwmSurfaceActor *actor);
  gboolean (* is_visible)        (CobiwmSurfaceActor *actor);

  gboolean (* should_unredirect) (CobiwmSurfaceActor *actor);
  void     (* set_unredirected)  (CobiwmSurfaceActor *actor,
                                  gboolean          unredirected);
  gboolean (* is_unredirected)   (CobiwmSurfaceActor *actor);

  CobiwmWindow *(* get_window)      (CobiwmSurfaceActor *actor);
};

struct _CobiwmSurfaceActor
{
  ClutterActor            parent;

  CobiwmSurfaceActorPrivate *priv;
};

GType cobiwm_surface_actor_get_type (void);

cairo_surface_t *cobiwm_surface_actor_get_image (CobiwmSurfaceActor      *self,
                                               cairo_rectangle_int_t *clip);

CobiwmShapedTexture *cobiwm_surface_actor_get_texture (CobiwmSurfaceActor *self);
CobiwmWindow        *cobiwm_surface_actor_get_window  (CobiwmSurfaceActor *self);

gboolean cobiwm_surface_actor_is_obscured (CobiwmSurfaceActor *self);

void cobiwm_surface_actor_set_input_region (CobiwmSurfaceActor *self,
                                          cairo_region_t   *region);
void cobiwm_surface_actor_set_opaque_region (CobiwmSurfaceActor *self,
                                           cairo_region_t   *region);
cairo_region_t * cobiwm_surface_actor_get_opaque_region (CobiwmSurfaceActor *self);

void cobiwm_surface_actor_process_damage (CobiwmSurfaceActor *actor,
                                        int x, int y, int width, int height);
void cobiwm_surface_actor_pre_paint (CobiwmSurfaceActor *actor);
gboolean cobiwm_surface_actor_is_argb32 (CobiwmSurfaceActor *actor);
gboolean cobiwm_surface_actor_is_visible (CobiwmSurfaceActor *actor);

void cobiwm_surface_actor_set_frozen (CobiwmSurfaceActor *actor,
                                    gboolean          frozen);

gboolean cobiwm_surface_actor_should_unredirect (CobiwmSurfaceActor *actor);
void cobiwm_surface_actor_set_unredirected (CobiwmSurfaceActor *actor,
                                          gboolean          unredirected);
gboolean cobiwm_surface_actor_is_unredirected (CobiwmSurfaceActor *actor);

G_END_DECLS

#endif /* COBIWM_SURFACE_ACTOR_PRIVATE_H */
