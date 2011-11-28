/* GStreamer
 * (c) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * (c) 2006 Jürg Billeter <j@bitron.ch>
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
 * SECTION:element-halaudiosink
 *
 * HalAudioSink allows access to output of sound devices by specifying the
 * corresponding persistent Unique Device Id (UDI) from the Hardware Abstraction
 * Layer (HAL) in the #GstHalAudioSink:udi property.
 * It currently always embeds alsasink or osssink as HAL doesn't support other
 * sound systems yet. You can also specify the UDI of a device that has ALSA or
 * OSS subdevices. If both are present ALSA is preferred.
 *
 * <refsect2>
 * <title>Examples</title>
 * |[
 * hal-find-by-property --key alsa.type --string playback
 * ]| list the UDIs of all your ALSA output devices
 * |[
 * gst-launch -v audiotestsrc ! halaudiosink udi=/org/freedesktop/Hal/devices/pci_8086_27d8_alsa_playback_0
 * ]| test your soundcard by playing a test signal on the specified sound device.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gsthalelements.h"
#include "gsthalaudiosink.h"

static void gst_hal_audio_sink_dispose (GObject * object);
static GstStateChangeReturn
gst_hal_audio_sink_change_state (GstElement * element,
    GstStateChange transition);

enum
{
  PROP_0,
  PROP_UDI
};

GST_BOILERPLATE (GstHalAudioSink, gst_hal_audio_sink, GstBin, GST_TYPE_BIN);

static void gst_hal_audio_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_hal_audio_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void
gst_hal_audio_sink_base_init (gpointer klass)
{
  GstElementClass *eklass = GST_ELEMENT_CLASS (klass);

  static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
      GST_PAD_SINK,
      GST_PAD_ALWAYS,
      GST_STATIC_CAPS_ANY);

  gst_element_class_add_static_pad_template (eklass, &sink_template);
  gst_element_class_set_details_simple (eklass, "HAL audio sink",
      "Sink/Audio",
      "Audio sink for sound device access via HAL",
      "Jürg Billeter <j@bitron.ch>");
}

static void
gst_hal_audio_sink_class_init (GstHalAudioSinkClass * klass)
{
  GObjectClass *oklass = G_OBJECT_CLASS (klass);
  GstElementClass *eklass = GST_ELEMENT_CLASS (klass);

  oklass->set_property = gst_hal_audio_sink_set_property;
  oklass->get_property = gst_hal_audio_sink_get_property;
  oklass->dispose = gst_hal_audio_sink_dispose;
  eklass->change_state = gst_hal_audio_sink_change_state;

  g_object_class_install_property (oklass, PROP_UDI,
      g_param_spec_string ("udi",
          "UDI", "Unique Device Id", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

/*
 * Hack to make negotiation work.
 */

static void
gst_hal_audio_sink_reset (GstHalAudioSink * sink)
{
  GstPad *targetpad;

  /* fakesink */
  if (sink->kid) {
    gst_element_set_state (sink->kid, GST_STATE_NULL);
    gst_bin_remove (GST_BIN (sink), sink->kid);
  }
  sink->kid = gst_element_factory_make ("fakesink", "testsink");
  gst_bin_add (GST_BIN (sink), sink->kid);

  targetpad = gst_element_get_static_pad (sink->kid, "sink");
  gst_ghost_pad_set_target (GST_GHOST_PAD (sink->pad), targetpad);
  gst_object_unref (targetpad);
}

static void
gst_hal_audio_sink_init (GstHalAudioSink * sink, GstHalAudioSinkClass * g_class)
{
  sink->pad = gst_ghost_pad_new_no_target ("sink", GST_PAD_SINK);
  gst_element_add_pad (GST_ELEMENT (sink), sink->pad);

  gst_hal_audio_sink_reset (sink);
}

static void
gst_hal_audio_sink_dispose (GObject * object)
{
  GstHalAudioSink *sink = GST_HAL_AUDIO_SINK (object);

  if (sink->udi) {
    g_free (sink->udi);
    sink->udi = NULL;
  }

  GST_CALL_PARENT (G_OBJECT_CLASS, dispose, (object));
}

static gboolean
do_toggle_element (GstHalAudioSink * sink)
{
  GstPad *targetpad;

  /* kill old element */
  if (sink->kid) {
    GST_DEBUG_OBJECT (sink, "Removing old kid");
    gst_element_set_state (sink->kid, GST_STATE_NULL);
    gst_bin_remove (GST_BIN (sink), sink->kid);
    sink->kid = NULL;
  }

  GST_DEBUG_OBJECT (sink, "Creating new kid");
  if (!sink->udi)
    GST_INFO_OBJECT (sink, "No UDI set for device, using default one");

  if (!(sink->kid = gst_hal_get_audio_sink (sink->udi))) {
    GST_ELEMENT_ERROR (sink, LIBRARY, SETTINGS, (NULL),
        ("Failed to render audio sink from Hal"));
    return FALSE;
  }
  gst_element_set_state (sink->kid, GST_STATE (sink));
  gst_bin_add (GST_BIN (sink), sink->kid);

  /* re-attach ghostpad */
  GST_DEBUG_OBJECT (sink, "Creating new ghostpad");
  targetpad = gst_element_get_static_pad (sink->kid, "sink");
  gst_ghost_pad_set_target (GST_GHOST_PAD (sink->pad), targetpad);
  gst_object_unref (targetpad);
  GST_DEBUG_OBJECT (sink, "done changing hal audio sink");

  return TRUE;
}

static void
gst_hal_audio_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstHalAudioSink *this = GST_HAL_AUDIO_SINK (object);

  GST_OBJECT_LOCK (this);

  switch (prop_id) {
    case PROP_UDI:
      if (this->udi)
        g_free (this->udi);
      this->udi = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (this);
}

static void
gst_hal_audio_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstHalAudioSink *this = GST_HAL_AUDIO_SINK (object);

  GST_OBJECT_LOCK (this);

  switch (prop_id) {
    case PROP_UDI:
      g_value_set_string (value, this->udi);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (this);
}

static GstStateChangeReturn
gst_hal_audio_sink_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstHalAudioSink *sink = GST_HAL_AUDIO_SINK (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!do_toggle_element (sink))
        return GST_STATE_CHANGE_FAILURE;
      break;
    default:
      break;
  }

  ret = GST_CALL_PARENT_WITH_DEFAULT (GST_ELEMENT_CLASS, change_state,
      (element, transition), GST_STATE_CHANGE_SUCCESS);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_hal_audio_sink_reset (sink);
      break;
    default:
      break;
  }

  return ret;
}
