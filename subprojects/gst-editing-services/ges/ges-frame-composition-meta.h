/* GStreamer
 * Copyright (C) 2013 Mathieu Duponchelle <mduponchelle1@gmail.com>
 * Copyright (C) 2013 Thibault Saunier <thibault.saunier@collabora.com>
 * Copyright (C) 2020 Thibault Saunier <tsaunier@igalia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Suite 500,
 * Boston, MA 02110-1335, USA.
 */

#pragma once

#include <ges/ges-types.h>
#include <glib-object.h>
#include <gst/gst.h>

G_BEGIN_DECLS

/**
 * ges_frame_composition_meta_api_get_type: (attributes doc.skip=true)
 */
GES_API
GType ges_frame_composition_meta_api_get_type (void);

/**
 * GES_TYPE_META_FRAME_COMPOSITION: (attributes doc.skip=true)
 */
#define GES_TYPE_META_FRAME_COMPOSITION (ges_frame_composition_meta_api_get_type())

/**
 * GESFrameCompositionMeta:
 * @meta: the parent #GstMeta.
 * @posx: The desired x position.
 * @posy: The desired y position.
 * @height: The desired height of the video.
 * @width: The desired width of the video.
 * @zorder: The desired z order.
 * @operator:The blending operator for the source.
 *
 * Metadata type that holds information about the positioning, size,
 * transparency and composition operator of a video frame in the timeline
 * composition.
 *
 * Since: 1.24
 */
typedef struct _GESFrameCompositionMeta GESFrameCompositionMeta;
struct _GESFrameCompositionMeta {
  GstMeta meta;

  gdouble alpha;
  gint posx;
  gint posy;
  gint height;
  gint width;
  guint zorder;
  gint operator;
};

GES_API
GESFrameCompositionMeta * ges_buffer_add_frame_composition_meta (GstBuffer * buffer);

G_END_DECLS
