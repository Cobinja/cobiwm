/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/**
 * SECTION:cobiwm-background-group
 * @title: CobiwmBackgroundGroup
 * @short_description: Container for background actors
 *
 * This class is a subclass of ClutterActor with special handling for
 * CobiwmBackgroundActor/CobiwmBackgroundGroup when painting children.
 * It makes sure to only draw the parts of the backgrounds not
 * occluded by opaque windows.
 *
 * See #CobiwmWindowGroup for more information behind the motivation,
 * and details on implementation.
 */

#include <config.h>

#include <cobiwm-background-group.h>
#include "cobiwm-cullable.h"

static void cullable_iface_init (CobiwmCullableInterface *iface);

G_DEFINE_TYPE_WITH_CODE (CobiwmBackgroundGroup, cobiwm_background_group, CLUTTER_TYPE_ACTOR,
                         G_IMPLEMENT_INTERFACE (COBIWM_TYPE_CULLABLE, cullable_iface_init));

static void
cobiwm_background_group_class_init (CobiwmBackgroundGroupClass *klass)
{
}

static void
cobiwm_background_group_cull_out (CobiwmCullable   *cullable,
                                cairo_region_t *unobscured_region,
                                cairo_region_t *clip_region)
{
  cobiwm_cullable_cull_out_children (cullable, unobscured_region, clip_region);
}

static void
cobiwm_background_group_reset_culling (CobiwmCullable *cullable)
{
  cobiwm_cullable_reset_culling_children (cullable);
}

static void
cullable_iface_init (CobiwmCullableInterface *iface)
{
  iface->cull_out = cobiwm_background_group_cull_out;
  iface->reset_culling = cobiwm_background_group_reset_culling;
}

static void
cobiwm_background_group_init (CobiwmBackgroundGroup *self)
{
}

ClutterActor *
cobiwm_background_group_new (void)
{
  CobiwmBackgroundGroup *background_group;

  background_group = g_object_new (COBIWM_TYPE_BACKGROUND_GROUP, NULL);

  return CLUTTER_ACTOR (background_group);
}
