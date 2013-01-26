/* GStreamer Editing Services
 * Copyright (C) 2010 Brandon Lewis <brandon.lewis@collabora.co.uk>
 *               2010 Nokia Corporation
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

/**
 * SECTION:ges-track-video-test-source
 * @short_description: produce solid colors and patterns
 */

#include "ges-internal.h"
#include "ges-track-element.h"
#include "ges-track-video-test-source.h"

G_DEFINE_TYPE (GESTrackVideoTestSource, ges_track_video_test_source,
    GES_TYPE_TRACK_SOURCE);

struct _GESTrackVideoTestSourcePrivate
{
  GESVideoTestPattern pattern;
};

static GstElement *ges_track_video_test_source_create_element (GESTrackElement *
    self);

static void
ges_track_video_test_source_class_init (GESTrackVideoTestSourceClass * klass)
{
  GESTrackElementClass *track_element_class = GES_TRACK_ELEMENT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GESTrackVideoTestSourcePrivate));

  track_element_class->create_element =
      ges_track_video_test_source_create_element;
}

static void
ges_track_video_test_source_init (GESTrackVideoTestSource * self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_TRACK_VIDEO_TEST_SOURCE, GESTrackVideoTestSourcePrivate);

  self->priv->pattern = GES_VIDEO_TEST_PATTERN_BLACK;
}

static GstElement *
ges_track_video_test_source_create_element (GESTrackElement * self)
{
  GstElement *ret;
  gint pattern;

  pattern = ((GESTrackVideoTestSource *) self)->priv->pattern;

  ret = gst_element_factory_make ("videotestsrc", NULL);
  g_object_set (ret, "pattern", (gint) pattern, NULL);

  return ret;
}

/**
 * ges_track_video_test_source_set_pattern:
 * @self: a #GESTrackVideoTestSource
 * @pattern: a #GESVideoTestPattern
 *
 * Sets the source to use the given @pattern.
 */
void
ges_track_video_test_source_set_pattern (GESTrackVideoTestSource
    * self, GESVideoTestPattern pattern)
{
  GstElement *element =
      ges_track_element_get_element (GES_TRACK_ELEMENT (self));

  self->priv->pattern = pattern;

  if (element)
    g_object_set (element, "pattern", (gint) pattern, NULL);
}

/**
 * ges_track_video_test_source_get_pattern:
 * @source: a #GESVideoTestPattern
 *
 * Get the video pattern used by the @source.
 *
 * Returns: The video pattern used by the @source.
 */
GESVideoTestPattern
ges_track_video_test_source_get_pattern (GESTrackVideoTestSource * source)
{
  return source->priv->pattern;
}

/**
 * ges_track_video_test_source_new:
 *
 * Creates a new #GESTrackVideoTestSource.
 *
 * Returns: The newly created #GESTrackVideoTestSource, or %NULL if there was an
 * error.
 */
GESTrackVideoTestSource *
ges_track_video_test_source_new (void)
{
  return g_object_new (GES_TYPE_TRACK_VIDEO_TEST_SOURCE, "track-type",
      GES_TRACK_TYPE_VIDEO, NULL);
}
