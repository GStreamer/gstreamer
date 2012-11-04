/*
 * GStreamer - SunAudio mixer
 * Copyright (C) 2005,2006 Sun Microsystems, Inc.,
 *               Brian Cameron <brian.cameron@sun.com>
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
 * SECTION:element-sunaudiomixer
 *
 * sunaudiomixer is an mixer that controls the sound input and output
 * levels with the Sun Audio interface available in Solaris.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstsunaudiomixer.h"

GST_BOILERPLATE_WITH_INTERFACE (GstSunAudioMixer, gst_sunaudiomixer,
    GstElement, GST_TYPE_ELEMENT, GstMixer, GST_TYPE_MIXER, gst_sunaudiomixer);

GST_IMPLEMENT_SUNAUDIO_MIXER_CTRL_METHODS (GstSunAudioMixer, gst_sunaudiomixer);

static GstStateChangeReturn gst_sunaudiomixer_change_state (GstElement *
    element, GstStateChange transition);

static void
gst_sunaudiomixer_base_init (gpointer klass)
{
  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "Sun Audio Mixer", "Generic/Audio",
      "Control sound input and output levels with Sun Audio",
      "Brian Cameron <brian.cameron@sun.com>");
}

static void
gst_sunaudiomixer_class_init (GstSunAudioMixerClass * klass)
{
  GstElementClass *element_class;

  element_class = (GstElementClass *) klass;

  element_class->change_state = gst_sunaudiomixer_change_state;
}

static void
gst_sunaudiomixer_init (GstSunAudioMixer * this,
    GstSunAudioMixerClass * g_class)
{
  this->mixer = NULL;
}

static GstStateChangeReturn
gst_sunaudiomixer_change_state (GstElement * element, GstStateChange transition)
{
  GstSunAudioMixer *this = GST_SUNAUDIO_MIXER (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!this->mixer) {
        const char *audiodev;

        audiodev = g_getenv ("AUDIODEV");
        if (audiodev == NULL) {
          this->mixer = gst_sunaudiomixer_ctrl_new ("/dev/audioctl");
        } else {
          gchar *device = g_strdup_printf ("%sctl", audiodev);

          this->mixer = gst_sunaudiomixer_ctrl_new (device);
          g_free (device);
        }
      }
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      if (this->mixer) {
        gst_sunaudiomixer_ctrl_free (this->mixer);
        this->mixer = NULL;
      }
      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  return GST_STATE_CHANGE_SUCCESS;
}
