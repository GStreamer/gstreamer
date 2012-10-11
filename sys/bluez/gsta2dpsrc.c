/*
 *
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2012  Collabora Ltd.
 *
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include "gsta2dpsrc.h"
#include "gstavdtputil.h"

GST_DEBUG_CATEGORY (a2dpsrc_debug);
#define GST_CAT_DEFAULT (a2dpsrc_debug)

enum
{
  PROP_0,
  PROP_TRANSPORT
};

GST_BOILERPLATE (GstA2dpSrc, gst_a2dp_src, GstBin, GST_TYPE_BIN);

static void gst_a2dp_src_finalize (GObject * object);
static void gst_a2dp_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_a2dp_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

static GstStaticPadTemplate gst_a2dp_src_template =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-sbc, "
        "rate = (int) { 16000, 32000, 44100, 48000 }, "
        "channels = (int) [ 1, 2 ], "
        "mode = (string) { \"mono\", \"dual\", "
        "\"stereo\", \"joint\" }, "
        "blocks = (int) { 4, 8, 12, 16 }, "
        "subbands = (int) { 4, 8 }, "
        "allocation = (string) { \"snr\", \"loudness\" }, "
        "bitpool = (int) [ 2, " TEMPLATE_MAX_BITPOOL_STR " ]"));

static void
gst_a2dp_src_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_a2dp_src_template));

  gst_element_class_set_details_simple (element_class,
      "Bluetooth A2DP Source",
      "Source/Audio/Network",
      "Receives and depayloads audio from an A2DP device",
      "Arun Raghavan <arun.raghavan@collabora.co.uk>");
}

static void
gst_a2dp_src_class_init (GstA2dpSrcClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_a2dp_src_finalize);
  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_a2dp_src_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_a2dp_src_get_property);

  g_object_class_install_property (gobject_class, PROP_TRANSPORT,
      g_param_spec_string ("transport",
          "Transport", "Use configured transport", NULL, G_PARAM_READWRITE));

  GST_DEBUG_CATEGORY_INIT (a2dpsrc_debug, "a2dpsrc", 0,
      "Bluetooth A2DP Source");
}

static void
gst_a2dp_src_init (GstA2dpSrc * a2dpsrc, GstA2dpSrcClass * klass)
{
  GstBin *bin = GST_BIN (a2dpsrc);
  GstElement *depay = NULL;
  GstPad *srcpad, *depay_srcpad;

  /* FIXME: We can set up the bin elements here since we only support
   * SBC. When supporting more formats, we would need to only instantiate
   * an avdtpsrc here, wait for it to get to READY, query its srcpad caps
   * and attach the appropriate RTP depayloader. */

  a2dpsrc->avdtpsrc = gst_element_factory_make ("avdtpsrc", NULL);
  if (!a2dpsrc->avdtpsrc) {
    GST_ERROR_OBJECT (a2dpsrc, "Unable to instantiate avdtpsrc");
    goto fail;
  }

  depay = gst_element_factory_make ("rtpsbcdepay", NULL);
  if (!depay) {
    GST_ERROR_OBJECT (a2dpsrc, "Unable to instantiate rtpsbcdepay");
    goto fail;
  }

  if (!gst_bin_add (bin, a2dpsrc->avdtpsrc) || !gst_bin_add (bin, depay) ||
      !gst_element_link (a2dpsrc->avdtpsrc, depay)) {
    GST_ERROR_OBJECT (a2dpsrc, "Unable to add elements to bin");
    goto fail;
  }

  depay_srcpad = gst_element_get_static_pad (depay, "src");
  srcpad = gst_ghost_pad_new ("sink", depay_srcpad);
  gst_object_unref (depay_srcpad);

  if (!gst_element_add_pad (GST_ELEMENT (a2dpsrc), srcpad)) {
    GST_ERROR_OBJECT (a2dpsrc, "Could not add srcpad");
    goto fail;
  }

  /* We keep a copy for easy proxying of properties */
  gst_object_ref (a2dpsrc->avdtpsrc);

  return;

fail:
  if (a2dpsrc->avdtpsrc)
    gst_object_unref (a2dpsrc->avdtpsrc);
  if (depay)
    gst_object_unref (depay);
  if (srcpad)
    gst_object_unref (srcpad);

  return;
}

static void
gst_a2dp_src_finalize (GObject * object)
{
  GstA2dpSrc *a2dpsrc = GST_A2DP_SRC (object);

  gst_object_unref (a2dpsrc->avdtpsrc);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_a2dp_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstA2dpSrc *a2dpsrc = GST_A2DP_SRC (object);

  switch (prop_id) {
    case PROP_TRANSPORT:
      g_object_get_property (G_OBJECT (a2dpsrc->avdtpsrc), "transport", value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_a2dp_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstA2dpSrc *a2dpsrc = GST_A2DP_SRC (object);

  switch (prop_id) {
    case PROP_TRANSPORT:
      g_object_set_property (G_OBJECT (a2dpsrc->avdtpsrc), "transport", value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

gboolean
gst_a2dp_src_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "a2dpsrc", GST_RANK_NONE,
      GST_TYPE_A2DP_SRC);
}
