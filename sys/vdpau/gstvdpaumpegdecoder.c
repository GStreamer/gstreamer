/*
 * GStreamer
 * Copyright (C) 2009 Carl-Anton Ingmarsson <ca.ingmarsson@gmail.com>
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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
 * SECTION:element-vdpaumpegdecoder
 *
 * FIXME:Describe vdpaumpegdecoder here.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m fakesrc ! vdpaumpegdecoder ! fakesink silent=TRUE
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>

#include "mpegutil.h"
#include "gstvdpaumpegdecoder.h"

GST_DEBUG_CATEGORY_STATIC (gst_vdpau_mpeg_decoder_debug);
#define GST_CAT_DEFAULT gst_vdpau_mpeg_decoder_debug

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_SILENT
};

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpeg, mpegversion = (int) [ 1, 2 ], "
        "systemstream = (boolean) false, parsed = (boolean) true")
    );

GST_BOILERPLATE (GstVdpauMpegDecoder, gst_vdpau_mpeg_decoder, GstVdpauDecoder,
    GST_TYPE_VDPAU_DECODER);

static void gst_vdpau_mpeg_decoder_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_vdpau_mpeg_decoder_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static gboolean
gst_vdpau_mpeg_decoder_set_caps (GstVdpauDecoder * dec, GstCaps * caps)
{
  GstVdpauMpegDecoder *mpeg_dec;
  GstStructure *structure;
  gint version;

  mpeg_dec = GST_VDPAU_MPEG_DECODER (dec);

  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_int (structure, "mpegversion", &version);
  if (version == 1)
    mpeg_dec->profile = VDP_DECODER_PROFILE_MPEG1;

  else {
    const GValue *value;
    GstBuffer *codec_data;
    MPEGSeqHdr hdr = { 0, };

    value = gst_structure_get_value (structure, "codec_data");
    codec_data = gst_value_get_buffer (value);
    mpeg_util_parse_sequence_hdr (&hdr, GST_BUFFER_DATA (codec_data),
        GST_BUFFER_DATA (codec_data) + GST_BUFFER_SIZE (codec_data));
    switch (hdr.profile) {
      case 5:
        mpeg_dec->profile = VDP_DECODER_PROFILE_MPEG2_SIMPLE;
        break;
      default:
        mpeg_dec->profile = VDP_DECODER_PROFILE_MPEG2_MAIN;
        break;
    }
  }

  return TRUE;
}

/* GObject vmethod implementations */

static void
gst_vdpau_mpeg_decoder_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_set_details_simple (element_class,
      "VdpauMpegDecoder",
      "Decoder",
      "decode mpeg stream with vdpau",
      "Carl-Anton Ingmarsson <ca.ingmarsson@gmail.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
}

/* initialize the vdpaumpegdecoder's class */
static void
gst_vdpau_mpeg_decoder_class_init (GstVdpauMpegDecoderClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstVdpauDecoderClass *vdpaudec_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  vdpaudec_class = (GstVdpauDecoderClass *) klass;

  gobject_class->set_property = gst_vdpau_mpeg_decoder_set_property;
  gobject_class->get_property = gst_vdpau_mpeg_decoder_get_property;

  g_object_class_install_property (gobject_class, PROP_SILENT,
      g_param_spec_boolean ("silent", "Silent", "Produce verbose output ?",
          FALSE, G_PARAM_READWRITE));

  vdpaudec_class->set_caps = gst_vdpau_mpeg_decoder_set_caps;
}

static void
gst_vdpau_mpeg_decoder_init (GstVdpauMpegDecoder * filter,
    GstVdpauMpegDecoderClass * gclass)
{
  filter->silent = FALSE;
}

static void
gst_vdpau_mpeg_decoder_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVdpauMpegDecoder *filter = GST_VDPAU_MPEG_DECODER (object);

  switch (prop_id) {
    case PROP_SILENT:
      filter->silent = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vdpau_mpeg_decoder_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVdpauMpegDecoder *filter = GST_VDPAU_MPEG_DECODER (object);

  switch (prop_id) {
    case PROP_SILENT:
      g_value_set_boolean (value, filter->silent);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
vdpaumpegdecoder_init (GstPlugin * vdpaumpegdecoder)
{
  /* debug category for fltering log messages
   *
   * exchange the string 'Template vdpaumpegdecoder' with your description
   */
  GST_DEBUG_CATEGORY_INIT (gst_vdpau_mpeg_decoder_debug, "vdpaumpegdecoder",
      0, "Template vdpaumpegdecoder");

  return gst_element_register (vdpaumpegdecoder, "vdpaumpegdecoder",
      GST_RANK_NONE, GST_TYPE_VDPAU_MPEG_DECODER);
}

/* gstreamer looks for this structure to register vdpaumpegdecoders
 *
 * exchange the string 'Template vdpaumpegdecoder' with your vdpaumpegdecoder description
 */
GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "vdpaumpegdecoder",
    "Template vdpaumpegdecoder",
    vdpaumpegdecoder_init,
    VERSION, "LGPL", "GStreamer", "http://gstreamer.net/")
