/* GStreamer
 * (c) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * (c) 2005 Tim-Philipp Müller <tim centricular net>
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
 * SECTION:element-halaudiosrc
 *
 * HalAudioSrc allows access to input of sound devices by specifying the
 * corresponding persistent Unique Device Id (UDI) from the Hardware Abstraction
 * Layer (HAL) in the #GstHalAudioSrc:udi property.
 * It currently always embeds alsasrc or osssrc as HAL doesn't support other
 * sound systems yet. You can also specify the UDI of a device that has ALSA or
 * OSS subdevices. If both are present ALSA is preferred.
 *
 * <refsect2>
 * <title>Examples</title>
 * |[
 * hal-find-by-property --key alsa.type --string capture
 * ]| list the UDIs of all your ALSA input devices
 * |[
 * gst-launch -v halaudiosrc udi=/org/freedesktop/Hal/devices/pci_8086_27d8_alsa_capture_0 ! autoaudiosink
 * ]| You should now hear yourself with a small delay if you have a microphone
 * connected to the specified sound device.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gsthalelements.h"
#include "gsthalaudiosrc.h"

static void gst_hal_audio_src_dispose (GObject * object);
static GstStateChangeReturn
gst_hal_audio_src_change_state (GstElement * element,
    GstStateChange transition);

enum
{
  PROP_0,
  PROP_UDI
};

GST_BOILERPLATE (GstHalAudioSrc, gst_hal_audio_src, GstBin, GST_TYPE_BIN);

static void gst_hal_audio_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_hal_audio_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void
gst_hal_audio_src_base_init (gpointer klass)
{
  GstElementClass *eklass = GST_ELEMENT_CLASS (klass);

  static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
      GST_PAD_SRC,
      GST_PAD_ALWAYS,
      GST_STATIC_CAPS_ANY);

  gst_element_class_add_static_pad_template (eklass, &src_template);
  gst_element_class_set_details_simple (eklass, "HAL audio source",
      "Source/Audio",
      "Audio source for sound device access via HAL",
      "Jürg Billeter <j@bitron.ch>");
}

static void
gst_hal_audio_src_class_init (GstHalAudioSrcClass * klass)
{
  GObjectClass *oklass = G_OBJECT_CLASS (klass);
  GstElementClass *eklass = GST_ELEMENT_CLASS (klass);

  oklass->set_property = gst_hal_audio_src_set_property;
  oklass->get_property = gst_hal_audio_src_get_property;
  oklass->dispose = gst_hal_audio_src_dispose;
  eklass->change_state = gst_hal_audio_src_change_state;

  g_object_class_install_property (oklass, PROP_UDI,
      g_param_spec_string ("udi",
          "UDI", "Unique Device Id", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

/*
 * Hack to make negotiation work.
 */

static void
gst_hal_audio_src_reset (GstHalAudioSrc * src)
{
  GstPad *targetpad;

  /* fakesrc */
  if (src->kid) {
    gst_element_set_state (src->kid, GST_STATE_NULL);
    gst_bin_remove (GST_BIN (src), src->kid);
  }
  src->kid = gst_element_factory_make ("fakesrc", "testsrc");
  gst_bin_add (GST_BIN (src), src->kid);

  targetpad = gst_element_get_static_pad (src->kid, "src");
  gst_ghost_pad_set_target (GST_GHOST_PAD (src->pad), targetpad);
  gst_object_unref (targetpad);
}

static void
gst_hal_audio_src_init (GstHalAudioSrc * src, GstHalAudioSrcClass * g_class)
{
  src->pad = gst_ghost_pad_new_no_target ("src", GST_PAD_SRC);
  gst_element_add_pad (GST_ELEMENT (src), src->pad);

  gst_hal_audio_src_reset (src);
}

static void
gst_hal_audio_src_dispose (GObject * object)
{
  GstHalAudioSrc *src = GST_HAL_AUDIO_SRC (object);

  if (src->udi) {
    g_free (src->udi);
    src->udi = NULL;
  }

  GST_CALL_PARENT (G_OBJECT_CLASS, dispose, (object));
}

static gboolean
do_toggle_element (GstHalAudioSrc * src)
{
  GstPad *targetpad;

  /* kill old element */
  if (src->kid) {
    GST_DEBUG_OBJECT (src, "Removing old kid");
    gst_element_set_state (src->kid, GST_STATE_NULL);
    gst_bin_remove (GST_BIN (src), src->kid);
    src->kid = NULL;
  }

  GST_DEBUG_OBJECT (src, "Creating new kid");
  if (!src->udi)
    GST_INFO_OBJECT (src, "No UDI set for device, using default one");

  if (!(src->kid = gst_hal_get_audio_src (src->udi))) {
    GST_ELEMENT_ERROR (src, LIBRARY, SETTINGS, (NULL),
        ("Failed to render audio source from Hal"));
    return FALSE;
  }
  gst_element_set_state (src->kid, GST_STATE (src));
  gst_bin_add (GST_BIN (src), src->kid);

  /* re-attach ghostpad */
  GST_DEBUG_OBJECT (src, "Creating new ghostpad");
  targetpad = gst_element_get_static_pad (src->kid, "src");
  gst_ghost_pad_set_target (GST_GHOST_PAD (src->pad), targetpad);
  gst_object_unref (targetpad);
  GST_DEBUG_OBJECT (src, "done changing hal audio source");

  return TRUE;
}

static void
gst_hal_audio_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstHalAudioSrc *this = GST_HAL_AUDIO_SRC (object);

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
gst_hal_audio_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstHalAudioSrc *this = GST_HAL_AUDIO_SRC (object);

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
gst_hal_audio_src_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstHalAudioSrc *src = GST_HAL_AUDIO_SRC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!do_toggle_element (src))
        return GST_STATE_CHANGE_FAILURE;
      break;
    default:
      break;
  }

  ret = GST_CALL_PARENT_WITH_DEFAULT (GST_ELEMENT_CLASS, change_state,
      (element, transition), GST_STATE_CHANGE_SUCCESS);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_hal_audio_src_reset (src);
      break;
    default:
      break;
  }

  return ret;
}
