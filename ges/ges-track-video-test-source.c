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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:ges-track-video-test-source
 * @short_description: produce solid colors and patterns
 */

#include "ges-internal.h"
#include "ges-track-object.h"
#include "ges-track-video-test-source.h"

G_DEFINE_TYPE (GESTrackVideoTestSource, ges_track_video_test_source,
    GES_TYPE_TRACK_SOURCE);

struct _GESTrackVideoTestSourcePrivate
{
  /*  Dummy variable */
  void *nothing;
};

static GstElement *ges_track_video_test_source_create_element (GESTrackSource *
    self);

static void
ges_track_video_test_source_class_init (GESTrackVideoTestSourceClass * klass)
{
  GESTrackSourceClass *track_source_class;

  g_type_class_add_private (klass, sizeof (GESTrackVideoTestSourcePrivate));

  track_source_class = GES_TRACK_SOURCE_CLASS (klass);

  track_source_class->create_element =
      ges_track_video_test_source_create_element;
}

static void
ges_track_video_test_source_init (GESTrackVideoTestSource * self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_TRACK_VIDEO_TEST_SOURCE, GESTrackVideoTestSourcePrivate);

  self->pattern = GES_VIDEO_TEST_PATTERN_BLACK;
}

static GstElement *
ges_track_video_test_source_create_element (GESTrackSource * self)
{
  GstElement *ret;
  gint pattern;

  pattern = ((GESTrackVideoTestSource *) self)->pattern;

  ret = gst_element_factory_make ("videotestsrc", NULL);
  g_object_set (ret, "pattern", (gint) pattern, NULL);

  return ret;
}

void
ges_track_video_test_source_set_pattern (GESTrackVideoTestSource
    * self, GESVideoTestPattern pattern)
{
  GstElement *element;

  element = GES_TRACK_SOURCE (self)->element;
  self->pattern = pattern;

  if (element)
    g_object_set (element, "pattern", (gint) pattern, NULL);
}

GESTrackVideoTestSource *
ges_track_video_test_source_new (void)
{
  return g_object_new (GES_TYPE_TRACK_VIDEO_TEST_SOURCE, NULL);
}
