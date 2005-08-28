/* OSS mixer interface element.
 * Copyright (C) 2005 Andrew Vander Wingo <wingo@pobox.com>
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


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstossmixerelement.h"


static GstElementDetails gst_oss_mixer_element_details =
GST_ELEMENT_DETAILS ("OSS Mixer",
    "Generic/Audio",
    "Control sound input and output levels with OSS",
    "Andrew Vander Wingo <wingo@pobox.com>");


GST_BOILERPLATE_WITH_INTERFACE (GstOssMixerElement, gst_oss_mixer_element,
    GstElement, GST_TYPE_ELEMENT, GstMixer, GST_TYPE_MIXER,
    gst_oss_mixer_element);

GST_IMPLEMENT_OSS_MIXER_METHODS (GstOssMixerElement, gst_oss_mixer_element);

static GstElementStateReturn gst_oss_mixer_element_change_state (GstElement *
    element);

static void
gst_oss_mixer_element_base_init (gpointer klass)
{
  gst_element_class_set_details (GST_ELEMENT_CLASS (klass),
      &gst_oss_mixer_element_details);
}

static void
gst_oss_mixer_element_class_init (GstOssMixerElementClass * klass)
{
  GstElementClass *element_class;

  element_class = (GstElementClass *) klass;

  element_class->change_state = gst_oss_mixer_element_change_state;
}

static void
gst_oss_mixer_element_init (GstOssMixerElement * this,
    GstOssMixerElementClass * g_class)
{
  this->mixer = NULL;
}

static GstElementStateReturn
gst_oss_mixer_element_change_state (GstElement * element)
{
  GstOssMixerElement *this = GST_OSS_MIXER_ELEMENT (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      if (!this->mixer) {
        this->mixer = gst_ossmixer_new ("/dev/mixer", GST_OSS_MIXER_ALL);
      }
      break;
    case GST_STATE_READY_TO_NULL:
      if (this->mixer) {
        gst_ossmixer_free (this->mixer);
        this->mixer = NULL;
      }
      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}
