/* GStreamer Editing Services
 * Copyright (C) 2010 Brandon Lewis <brandon.lewis@collabora.co.uk>
 *               2010 Nokia Corporation
 * Copyright (C) 2020 Igalia S.L
 *     Author: 2020 Thibault Saunier <tsaunier@igalia.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#pragma once

#include <glib-object.h>
#include <ges/ges-enums.h>
#include <ges/ges-types.h>
#include <ges/ges-video-source.h>

G_BEGIN_DECLS

#define GES_TYPE_VIDEO_TEST_SOURCE ges_video_test_source_get_type()
GES_DECLARE_TYPE(VideoTestSource, video_test_source, VIDEO_TEST_SOURCE);

/**
 * GESVideoTestSource:
 *
 * ### Children Properties
 *
 *  {{ libs/GESVideoTestSource-children-props.md }}
 */
struct _GESVideoTestSource {
  /*< private >*/
  GESVideoSource parent;

  GESVideoTestSourcePrivate *priv;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

struct _GESVideoTestSourceClass {
  GESVideoSourceClass parent_class;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

GES_API void
ges_video_test_source_set_pattern(GESVideoTestSource *self,
					GESVideoTestPattern pattern);
GES_API GESVideoTestPattern
ges_video_test_source_get_pattern (GESVideoTestSource *source);

G_END_DECLS
