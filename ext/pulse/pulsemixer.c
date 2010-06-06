/*
 *  GStreamer pulseaudio plugin
 *
 *  Copyright (c) 2004-2008 Lennart Poettering
 *
 *  gst-pulse is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as
 *  published by the Free Software Foundation; either version 2.1 of the
 *  License, or (at your option) any later version.
 *
 *  gst-pulse is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with gst-pulse; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 *  USA.
 */

/**
 * SECTION:element-pulsemixer
 * @see_also: pulsesrc, pulsesink
 *
 * This element lets you adjust sound input and output levels for the
 * PulseAudio sound server. It supports the GstMixer interface, which can be
 * used to obtain a list of available mixer tracks. Set the mixer element to
 * READY state before using the GstMixer interface on it.
 *
 * <refsect2>
 * <title>Example pipelines</title>
 * <para>
 * pulsemixer can't be used in a sensible way in gst-launch.
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <stdio.h>

#include "pulsemixer.h"

enum
{
  PROP_SERVER = 1,
  PROP_DEVICE,
  PROP_DEVICE_NAME
};

GST_DEBUG_CATEGORY_EXTERN (pulse_debug);
#define GST_CAT_DEFAULT pulse_debug

static void gst_pulsemixer_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_pulsemixer_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_pulsemixer_finalize (GObject * object);

static GstStateChangeReturn gst_pulsemixer_change_state (GstElement * element,
    GstStateChange transition);

static void gst_pulsemixer_init_interfaces (GType type);

GST_IMPLEMENT_PULSEMIXER_CTRL_METHODS (GstPulseMixer, gst_pulsemixer);
GST_IMPLEMENT_PULSEPROBE_METHODS (GstPulseMixer, gst_pulsemixer);
GST_BOILERPLATE_FULL (GstPulseMixer, gst_pulsemixer, GstElement,
    GST_TYPE_ELEMENT, gst_pulsemixer_init_interfaces);

static gboolean
gst_pulsemixer_interface_supported (GstImplementsInterface
    * iface, GType interface_type)
{
  GstPulseMixer *this = GST_PULSEMIXER (iface);

  if (interface_type == GST_TYPE_MIXER && this->mixer)
    return TRUE;

  if (interface_type == GST_TYPE_PROPERTY_PROBE && this->probe)
    return TRUE;

  return FALSE;
}

static void
gst_pulsemixer_implements_interface_init (GstImplementsInterfaceClass * klass)
{
  klass->supported = gst_pulsemixer_interface_supported;
}

static void
gst_pulsemixer_init_interfaces (GType type)
{
  static const GInterfaceInfo implements_iface_info = {
    (GInterfaceInitFunc) gst_pulsemixer_implements_interface_init,
    NULL,
    NULL,
  };
  static const GInterfaceInfo mixer_iface_info = {
    (GInterfaceInitFunc) gst_pulsemixer_mixer_interface_init,
    NULL,
    NULL,
  };
  static const GInterfaceInfo probe_iface_info = {
    (GInterfaceInitFunc) gst_pulsemixer_property_probe_interface_init,
    NULL,
    NULL,
  };

  g_type_add_interface_static (type, GST_TYPE_IMPLEMENTS_INTERFACE,
      &implements_iface_info);
  g_type_add_interface_static (type, GST_TYPE_MIXER, &mixer_iface_info);
  g_type_add_interface_static (type, GST_TYPE_PROPERTY_PROBE,
      &probe_iface_info);
}

static void
gst_pulsemixer_base_init (gpointer g_class)
{
  gst_element_class_set_details_simple (GST_ELEMENT_CLASS (g_class),
      "PulseAudio Mixer",
      "Generic/Audio",
      "Control sound input and output levels for PulseAudio",
      "Lennart Poettering");
}

static void
gst_pulsemixer_class_init (GstPulseMixerClass * g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);
  GObjectClass *gobject_class = G_OBJECT_CLASS (g_class);

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_pulsemixer_change_state);

  gobject_class->finalize = gst_pulsemixer_finalize;
  gobject_class->get_property = gst_pulsemixer_get_property;
  gobject_class->set_property = gst_pulsemixer_set_property;

  g_object_class_install_property (gobject_class,
      PROP_SERVER,
      g_param_spec_string ("server", "Server",
          "The PulseAudio server to connect to", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_DEVICE,
      g_param_spec_string ("device", "Device",
          "The PulseAudio sink or source to control", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_DEVICE_NAME,
      g_param_spec_string ("device-name", "Device name",
          "Human-readable name of the sound device", NULL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
}

static void
gst_pulsemixer_init (GstPulseMixer * this, GstPulseMixerClass * g_class)
{
  this->mixer = NULL;
  this->server = NULL;
  this->device = NULL;

  this->probe =
      gst_pulseprobe_new (G_OBJECT (this), G_OBJECT_GET_CLASS (this),
      PROP_DEVICE, this->device, TRUE, TRUE);
}

static void
gst_pulsemixer_finalize (GObject * object)
{
  GstPulseMixer *this = GST_PULSEMIXER (object);

  g_free (this->server);
  g_free (this->device);

  if (this->mixer) {
    gst_pulsemixer_ctrl_free (this->mixer);
    this->mixer = NULL;
  }

  if (this->probe) {
    gst_pulseprobe_free (this->probe);
    this->probe = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_pulsemixer_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstPulseMixer *this = GST_PULSEMIXER (object);

  switch (prop_id) {
    case PROP_SERVER:
      g_free (this->server);
      this->server = g_value_dup_string (value);

      if (this->probe)
        gst_pulseprobe_set_server (this->probe, this->server);

      break;

    case PROP_DEVICE:
      g_free (this->device);
      this->device = g_value_dup_string (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_pulsemixer_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstPulseMixer *this = GST_PULSEMIXER (object);

  switch (prop_id) {
    case PROP_SERVER:
      g_value_set_string (value, this->server);
      break;
    case PROP_DEVICE:
      g_value_set_string (value, this->device);
      break;
    case PROP_DEVICE_NAME:
      if (this->mixer) {
        char *t = g_strdup_printf ("%s: %s",
            this->mixer->type == GST_PULSEMIXER_SINK ? "Playback" : "Capture",
            this->mixer->description);
        g_value_take_string (value, t);
      } else
        g_value_set_string (value, NULL);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_pulsemixer_change_state (GstElement * element, GstStateChange transition)
{
  GstPulseMixer *this = GST_PULSEMIXER (element);
  GstStateChangeReturn res;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!this->mixer)
        this->mixer =
            gst_pulsemixer_ctrl_new (G_OBJECT (this), this->server,
            this->device, GST_PULSEMIXER_UNKNOWN);
      break;
    default:
      ;
  }

  res = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      if (this->mixer) {
        gst_pulsemixer_ctrl_free (this->mixer);
        this->mixer = NULL;
      }
      break;
    default:
      ;
  }

  return res;
}
