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
 * SECTION:ges-video-test-source
 * @short_description: produce solid colors and patterns
 */

#include "ges-internal.h"
#include "ges-track-element.h"
#include "ges-video-test-source.h"

G_DEFINE_TYPE (GESVideoTestSource, ges_video_test_source, GES_TYPE_SOURCE);

#define DEFAULT_VPATTERN GES_VIDEO_TEST_PATTERN_SMPTE

struct _GESVideoTestSourcePrivate
{
  GESVideoTestPattern pattern;
};

static GstElement *ges_video_test_source_create_source (GESTrackElement * self);

static void
ges_video_test_source_class_init (GESVideoTestSourceClass * klass)
{
  GESSourceClass *source_class = GES_SOURCE_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GESVideoTestSourcePrivate));

  source_class->create_source = ges_video_test_source_create_source;
}

static void
ges_video_test_source_init (GESVideoTestSource * self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_VIDEO_TEST_SOURCE, GESVideoTestSourcePrivate);

  self->priv->pattern = DEFAULT_VPATTERN;
}

static GstElement *
ges_video_test_source_create_source (GESTrackElement * self)
{
  gint pattern;
  GstElement *ret;
  gchar *bin_desc;


  pattern = ((GESVideoTestSource *) self)->priv->pattern;
  bin_desc =
      g_strdup_printf
      ("videotestsrc pattern=%i name=testsrc ! capsfilter caps=video/x-raw",
      pattern);
  ret = gst_parse_bin_from_description (bin_desc, TRUE, NULL);
  g_free (bin_desc);

  return ret;
}

/**
 * ges_video_test_source_set_pattern:
 * @self: a #GESVideoTestSource
 * @pattern: a #GESVideoTestPattern
 *
 * Sets the source to use the given @pattern.
 */
void
ges_video_test_source_set_pattern (GESVideoTestSource
    * self, GESVideoTestPattern pattern)
{
  GstElement *element =
      ges_track_element_get_element (GES_TRACK_ELEMENT (self));

  self->priv->pattern = pattern;

  if (element)
    gst_child_proxy_set (GST_CHILD_PROXY (element), "testsrc::pattern",
        (gint) pattern, NULL);
}

/**
 * ges_video_test_source_get_pattern:
 * @source: a #GESVideoTestPattern
 *
 * Get the video pattern used by the @source.
 *
 * Returns: The video pattern used by the @source.
 */
GESVideoTestPattern
ges_video_test_source_get_pattern (GESVideoTestSource * source)
{
  return source->priv->pattern;
}

/**
 * ges_video_test_source_new:
 *
 * Creates a new #GESVideoTestSource.
 *
 * Returns: The newly created #GESVideoTestSource, or %NULL if there was an
 * error.
 */
GESVideoTestSource *
ges_video_test_source_new (void)
{
  return g_object_new (GES_TYPE_VIDEO_TEST_SOURCE, "track-type",
      GES_TRACK_TYPE_VIDEO, NULL);
}
