/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

#ifndef COBIWM_BACKGROUND_PRIVATE_H
#define COBIWM_BACKGROUND_PRIVATE_H

#include <config.h>

#include "cobiwm-background-private.h"

CoglTexture *cobiwm_background_get_texture (CobiwmBackground         *self,
                                          int                     monitor_index,
                                          cairo_rectangle_int_t  *texture_area,
                                          CoglPipelineWrapMode   *wrap_mode);

#endif /* COBIWM_BACKGROUND_PRIVATE_H */
