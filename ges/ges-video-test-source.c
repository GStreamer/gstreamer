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
 * SECTION:gesvideotestsource
 * @title: GESVideoTestSource
 * @short_description: produce solid colors and patterns
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ges-internal.h"
#include "ges-track-element.h"
#include "ges-video-test-source.h"

#define DEFAULT_VPATTERN GES_VIDEO_TEST_PATTERN_SMPTE

struct _GESVideoTestSourcePrivate
{
  GESVideoTestPattern pattern;
};

G_DEFINE_TYPE_WITH_PRIVATE (GESVideoTestSource, ges_video_test_source,
    GES_TYPE_VIDEO_SOURCE);

static GstElement *ges_video_test_source_create_source (GESTrackElement * self);

static gboolean
get_natural_size (GESVideoSource * source, gint * width, gint * height)
{
  *width = DEFAULT_WIDTH;
  *height = DEFAULT_HEIGHT;

  return TRUE;
}

static void
ges_video_test_source_class_init (GESVideoTestSourceClass * klass)
{
  GESVideoSourceClass *source_class = GES_VIDEO_SOURCE_CLASS (klass);

  source_class->create_source = ges_video_test_source_create_source;
  source_class->ABI.abi.get_natural_size = get_natural_size;
}

static void
ges_video_test_source_init (GESVideoTestSource * self)
{
  self->priv = ges_video_test_source_get_instance_private (self);

  self->priv->pattern = DEFAULT_VPATTERN;
}

static GstElement *
ges_video_test_source_create_source (GESTrackElement * self)
{
  GstCaps *caps;
  gint pattern;
  GstElement *testsrc, *capsfilter, *res;
  const gchar *props[] = { "pattern", NULL };
  GPtrArray *elements;

  testsrc = gst_element_factory_make ("videotestsrc", NULL);
  capsfilter = gst_element_factory_make ("capsfilter", NULL);
  pattern = ((GESVideoTestSource *) self)->priv->pattern;

  g_object_set (testsrc, "pattern", pattern, NULL);

  elements = g_ptr_array_new ();
  g_ptr_array_add (elements, capsfilter);
  caps = gst_caps_new_simple ("video/x-raw",
      "width", G_TYPE_INT, DEFAULT_WIDTH,
      "height", G_TYPE_INT, DEFAULT_HEIGHT,
      "framerate", GST_TYPE_FRACTION, DEFAULT_FRAMERATE_N, DEFAULT_FRAMERATE_D,
      NULL);
  g_object_set (capsfilter, "caps", caps, NULL);
  gst_caps_unref (caps);

  ges_track_element_add_children_props (self, testsrc, NULL, NULL, props);

  res = ges_source_create_topbin ("videotestsrc", testsrc, elements);
  g_ptr_array_free (elements, TRUE);

  return res;
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

  if (element) {
    GValue val = { 0 };

    g_value_init (&val, GES_VIDEO_TEST_PATTERN_TYPE);
    g_value_set_enum (&val, pattern);
    ges_track_element_set_child_property (GES_TRACK_ELEMENT (self), "pattern",
        &val);
  }
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
  GValue val = { 0 };

  ges_track_element_get_child_property (GES_TRACK_ELEMENT (source), "pattern",
      &val);
  return g_value_get_enum (&val);
}

/**
 * ges_video_test_source_new:
 *
 * Creates a new #GESVideoTestSource.
 *
 * Returns: (transfer floating) (nullable): The newly created
 * #GESVideoTestSource, or %NULL if there was an error.
 */
GESVideoTestSource *
ges_video_test_source_new (void)
{
  return g_object_new (GES_TYPE_VIDEO_TEST_SOURCE, "track-type",
      GES_TRACK_TYPE_VIDEO, NULL);
}
