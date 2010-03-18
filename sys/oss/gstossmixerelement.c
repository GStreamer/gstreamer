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

/**
 * SECTION:element-ossmixer
 *
 * This element lets you adjust sound input and output levels with the
 * Open Sound System (OSS). It supports the #GstMixer interface, which can be
 * used to obtain a list of available mixer tracks. Set the mixer element to
 * READY state before using the #GstMixer interface on it.
 *
 * <refsect2>
 * <title>Example pipelines</title>
 * <para>
 * ossmixer can't be used in a sensible way in gst-launch.
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstossmixerelement.h"

GST_DEBUG_CATEGORY_EXTERN (oss_debug);
#define GST_CAT_DEFAULT oss_debug

#define DEFAULT_DEVICE       "/dev/mixer"
#define DEFAULT_DEVICE_NAME  NULL

enum
{
  PROP_0,
  PROP_DEVICE,
  PROP_DEVICE_NAME
};

GST_BOILERPLATE_WITH_INTERFACE (GstOssMixerElement, gst_oss_mixer_element,
    GstElement, GST_TYPE_ELEMENT, GstMixer, GST_TYPE_MIXER,
    gst_oss_mixer_element);

GST_IMPLEMENT_OSS_MIXER_METHODS (GstOssMixerElement, gst_oss_mixer_element);

static GstStateChangeReturn gst_oss_mixer_element_change_state (GstElement *
    element, GstStateChange transition);

static void gst_oss_mixer_element_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_oss_mixer_element_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_oss_mixer_element_finalize (GObject * object);

static void
gst_oss_mixer_element_base_init (gpointer klass)
{
  gst_element_class_set_details_simple (GST_ELEMENT_CLASS (klass), "OSS Mixer",
      "Generic/Audio",
      "Control sound input and output levels with OSS",
      "Andrew Vander Wingo <wingo@pobox.com>");
}

static void
gst_oss_mixer_element_class_init (GstOssMixerElementClass * klass)
{
  GstElementClass *element_class;
  GObjectClass *gobject_class;

  element_class = (GstElementClass *) klass;
  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = gst_oss_mixer_element_finalize;
  gobject_class->set_property = gst_oss_mixer_element_set_property;
  gobject_class->get_property = gst_oss_mixer_element_get_property;

  /**
   * GstOssMixerElement:device
   *
   * OSS mixer device (usually /dev/mixer)
   *
   * Since: 0.10.5
   **/
  g_object_class_install_property (gobject_class, PROP_DEVICE,
      g_param_spec_string ("device", "Device",
          "OSS mixer device (usually /dev/mixer)", DEFAULT_DEVICE,
          G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_DEVICE_NAME,
      g_param_spec_string ("device-name", "Device name",
          "Human-readable name of the sound device", DEFAULT_DEVICE_NAME,
          G_PARAM_READABLE));

  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_oss_mixer_element_change_state);
}

static void
gst_oss_mixer_element_finalize (GObject * obj)
{
  GstOssMixerElement *this = GST_OSS_MIXER_ELEMENT (obj);

  g_free (this->device);

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
gst_oss_mixer_element_init (GstOssMixerElement * this,
    GstOssMixerElementClass * g_class)
{
  this->mixer = NULL;
  this->device = g_strdup (DEFAULT_DEVICE);
}

static void
gst_oss_mixer_element_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstOssMixerElement *this = GST_OSS_MIXER_ELEMENT (object);

  switch (prop_id) {
    case PROP_DEVICE:
      g_free (this->device);
      this->device = g_value_dup_string (value);
      /* make sure we never set NULL */
      if (this->device == NULL) {
        this->device = g_strdup (DEFAULT_DEVICE);
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_oss_mixer_element_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstOssMixerElement *this = GST_OSS_MIXER_ELEMENT (object);

  switch (prop_id) {
    case PROP_DEVICE:
      g_value_set_string (value, this->device);
      break;
    case PROP_DEVICE_NAME:
      if (this->mixer) {
        g_value_set_string (value, this->mixer->cardname);
      } else {
        g_value_set_string (value, NULL);
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_oss_mixer_element_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstOssMixerElement *this = GST_OSS_MIXER_ELEMENT (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!this->mixer) {
        this->mixer = gst_ossmixer_new (this->device, GST_OSS_MIXER_ALL);
        if (!this->mixer)
          goto open_failed;
      }
      break;
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      if (this->mixer) {
        gst_ossmixer_free (this->mixer);
        this->mixer = NULL;
      }
      break;
    default:
      break;
  }
  return ret;

  /* ERRORS */
open_failed:
  {
    GST_ELEMENT_ERROR (element, RESOURCE, OPEN_READ_WRITE, (NULL),
        ("Failed to open oss mixer device '%s'", this->device));
    return GST_STATE_CHANGE_FAILURE;
  }
}
