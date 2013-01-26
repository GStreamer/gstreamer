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
 * SECTION:ges-audio-test-source
 * @short_description: produce a simple test waveform or silence
 * 
 * Outputs a test audio stream using audiotestsrc. The default property values
 * output silence. Useful for testing pipelines, or to fill gaps in an audio
 * track.
 */

#include "ges-internal.h"
#include "ges-track-element.h"
#include "ges-audio-test-source.h"

G_DEFINE_TYPE (GESAudioTestSource, ges_audio_test_source,
    GES_TYPE_TRACK_SOURCE);

struct _GESAudioTestSourcePrivate
{
  gdouble freq;
  gdouble volume;
};

enum
{
  PROP_0,
};

static void ges_audio_test_source_get_property (GObject * object, guint
    property_id, GValue * value, GParamSpec * pspec);

static void ges_audio_test_source_set_property (GObject * object, guint
    property_id, const GValue * value, GParamSpec * pspec);

static GstElement *ges_audio_test_source_create_element (GESTrackElement *
    self);

static void
ges_audio_test_source_class_init (GESAudioTestSourceClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESTrackElementClass *bg_class = GES_TRACK_ELEMENT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GESAudioTestSourcePrivate));

  object_class->get_property = ges_audio_test_source_get_property;
  object_class->set_property = ges_audio_test_source_set_property;

  bg_class->create_element = ges_audio_test_source_create_element;
}

static void
ges_audio_test_source_init (GESAudioTestSource * self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_AUDIO_TEST_SOURCE, GESAudioTestSourcePrivate);

  self->priv->freq = 440;
  self->priv->volume = 0;
}

static void
ges_audio_test_source_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_audio_test_source_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static GstElement *
ges_audio_test_source_create_element (GESTrackElement * trksrc)
{
  GESAudioTestSource *self;
  GstElement *ret;

  self = (GESAudioTestSource *) trksrc;
  ret = gst_element_factory_make ("audiotestsrc", NULL);
  g_object_set (ret, "volume", (gdouble) self->priv->volume, "freq", (gdouble)
      self->priv->freq, NULL);

  return ret;
}

/**
 * ges_audio_test_source_set_freq:
 * @self: a #GESAudioTestSource
 * @freq: The frequency you want to apply on @self
 *
 * Lets you set the frequency applied on the track object
 */
void
ges_audio_test_source_set_freq (GESAudioTestSource * self, gdouble freq)
{
  GstElement *element =
      ges_track_element_get_element (GES_TRACK_ELEMENT (self));

  self->priv->freq = freq;
  if (element)
    g_object_set (element, "freq", (gdouble) freq, NULL);
}

/**
 * ges_audio_test_source_set_volume:
 * @self: a #GESAudioTestSource
 * @volume: The volume you want to apply on @self
 *
 * Sets the volume of the test audio signal.
 */
void
ges_audio_test_source_set_volume (GESAudioTestSource * self, gdouble volume)
{
  GstElement *element =
      ges_track_element_get_element (GES_TRACK_ELEMENT (self));

  self->priv->volume = volume;
  if (element)
    g_object_set (element, "volume", (gdouble) volume, NULL);
}

/**
 * ges_audio_test_source_get_freq:
 * @self: a #GESAudioTestSource
 *
 * Get the current frequency of @self.
 *
 * Returns: The current frequency of @self.
 */
double
ges_audio_test_source_get_freq (GESAudioTestSource * self)
{
  return self->priv->freq;
}

/**
 * ges_audio_test_source_get_volume:
 * @self: a #GESAudioTestSource
 * 
 * Get the current volume of @self.
 *
 * Returns: The current volume of @self
 */
double
ges_audio_test_source_get_volume (GESAudioTestSource * self)
{
  return self->priv->volume;
}

/**
 * ges_audio_test_source_new:
 *
 * Creates a new #GESAudioTestSource.
 *
 * Returns: The newly created #GESAudioTestSource.
 */
GESAudioTestSource *
ges_audio_test_source_new (void)
{
  return g_object_new (GES_TYPE_AUDIO_TEST_SOURCE, "track-type",
      GES_TRACK_TYPE_AUDIO, NULL);
}
