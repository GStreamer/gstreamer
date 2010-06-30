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
 * SECTION:ges-track-audio-test-source
 * @short_description: Base Class for single-media sources
 */

#include "ges-internal.h"
#include "ges-track-object.h"
#include "ges-track-audio-test-source.h"

G_DEFINE_TYPE (GESTrackAudioTestSource, ges_track_audio_test_source,
    GES_TYPE_TRACK_SOURCE);

enum
{
  PROP_0,
};

static void ges_track_audio_test_source_dispose (GObject * object);

static void ges_track_audio_test_source_finalize (GObject * object);

static void ges_track_audio_test_source_get_property (GObject * object, guint
    property_id, GValue * value, GParamSpec * pspec);

static void ges_track_audio_test_source_set_property (GObject * object, guint
    property_id, const GValue * value, GParamSpec * pspec);

static GstElement *ges_track_audio_test_source_create_element (GESTrackSource *
    self);

static void
ges_track_audio_test_source_class_init (GESTrackAudioTestSourceClass * klass)
{
  GObjectClass *object_class;
  GESTrackSourceClass *bg_class;

  object_class = G_OBJECT_CLASS (klass);
  bg_class = GES_TRACK_SOURCE_CLASS (klass);

  object_class->get_property = ges_track_audio_test_source_get_property;
  object_class->set_property = ges_track_audio_test_source_set_property;
  object_class->dispose = ges_track_audio_test_source_dispose;
  object_class->finalize = ges_track_audio_test_source_finalize;

  bg_class->create_element = ges_track_audio_test_source_create_element;
}

static void
ges_track_audio_test_source_init (GESTrackAudioTestSource * self)
{
}

static void
ges_track_audio_test_source_dispose (GObject * object)
{
  G_OBJECT_CLASS (ges_track_audio_test_source_parent_class)->dispose (object);
}

static void
ges_track_audio_test_source_finalize (GObject * object)
{
  G_OBJECT_CLASS (ges_track_audio_test_source_parent_class)->finalize (object);
}

static void
ges_track_audio_test_source_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_track_audio_test_source_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static GstElement *
ges_track_audio_test_source_create_element (GESTrackSource * self)
{
  GstElement *ret;
  ret = gst_element_factory_make ("audiotestsrc", NULL);
  g_object_set (ret, "volume", (gdouble) 0, NULL);
  return ret;
}

GESTrackAudioTestSource *
ges_track_audio_test_source_new (void)
{
  return g_object_new (GES_TYPE_TRACK_AUDIO_TEST_SOURCE, NULL);
}
