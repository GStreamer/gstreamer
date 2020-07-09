/*
 *  gstvaapiencode_jpeg.c - VA-API JPEG encoder
 *
 *  Copyright (C) 2015 Intel Corporation
 *    Author: Sreerenj Balachandran <sreerenj.balachandran@intel.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

/**
 * SECTION:element-vaapijpegenc
 * @short_description: A VA-API based JPEG image encoder
 *
 * Encodes raw images into JPEG images.
 *
 * ## Example launch line
 *
 * |[
 *  gst-launch-1.0 -ev videotestsrc num-buffers=1 ! timeoverlay ! vaapijpegenc ! filesink location=test.jpg
 * ]|
 */

#include "gstcompat.h"
#include <gst/vaapi/gstvaapidisplay.h>
#include <gst/vaapi/gstvaapiencoder_jpeg.h>
#include "gstvaapiencode_jpeg.h"
#include "gstvaapipluginutil.h"
#include "gstvaapivideomemory.h"

#define GST_PLUGIN_NAME "vaapijpegenc"
#define GST_PLUGIN_DESC "A VA-API based JPEG video encoder"

GST_DEBUG_CATEGORY_STATIC (gst_vaapi_jpeg_encode_debug);
#define GST_CAT_DEFAULT gst_vaapi_jpeg_encode_debug

#define GST_CODEC_CAPS                          \
  "image/jpeg"

#define EXTRA_FORMATS { GST_VIDEO_FORMAT_BGRA, }

/* jpeg encode */
GST_VAAPI_ENCODE_REGISTER_TYPE (jpeg, JPEG, Jpeg, EXTRA_FORMATS, NULL);

static void
gst_vaapiencode_jpeg_init (GstVaapiEncodeJpeg * encode)
{
  /* nothing to do here */
}

static void
gst_vaapiencode_jpeg_finalize (GObject * object)
{
  G_OBJECT_CLASS (gst_vaapiencode_jpeg_parent_class)->finalize (object);
}

static GstCaps *
gst_vaapiencode_jpeg_get_caps (GstVaapiEncode * base_encode)
{
  GstCaps *caps;

  caps = gst_caps_from_string (GST_CODEC_CAPS);

  return caps;
}

static GstVaapiEncoder *
gst_vaapiencode_jpeg_alloc_encoder (GstVaapiEncode * base,
    GstVaapiDisplay * display)
{
  return gst_vaapi_encoder_jpeg_new (display);
}

static void
gst_vaapiencode_jpeg_class_init (GstVaapiEncodeJpegClass * klass, gpointer data)
{
  GObjectClass *const object_class = G_OBJECT_CLASS (klass);
  GstElementClass *const element_class = GST_ELEMENT_CLASS (klass);
  GstVaapiEncodeClass *const encode_class = GST_VAAPIENCODE_CLASS (klass);
  GstCaps *sink_caps = ((GstVaapiEncodeInitData *) data)->sink_caps;
  GstCaps *src_caps = ((GstVaapiEncodeInitData *) data)->src_caps;
  GstPadTemplate *templ;
  GstCaps *static_caps;
  gpointer encoder_class;

  object_class->finalize = gst_vaapiencode_jpeg_finalize;
  object_class->set_property = gst_vaapiencode_set_property_subclass;
  object_class->get_property = gst_vaapiencode_get_property_subclass;

  encode_class->get_caps = gst_vaapiencode_jpeg_get_caps;
  encode_class->alloc_encoder = gst_vaapiencode_jpeg_alloc_encoder;

  gst_element_class_set_static_metadata (element_class,
      "VA-API JPEG encoder",
      "Codec/Encoder/Image/Hardware",
      GST_PLUGIN_DESC,
      "Sreerenj Balachandran <sreerenj.balachandran@intel.com>");

  /* sink pad */
  g_assert (sink_caps);
  static_caps = gst_caps_from_string (GST_VAAPI_ENCODE_STATIC_SINK_CAPS);
  templ =
      gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS, sink_caps);
  gst_pad_template_set_documentation_caps (templ, static_caps);
  gst_element_class_add_pad_template (element_class, templ);
  gst_caps_unref (static_caps);
  gst_caps_unref (sink_caps);

  /* src pad */
  g_assert (src_caps);
  static_caps = gst_caps_from_string (GST_CODEC_CAPS);
  templ = gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS, src_caps);
  gst_pad_template_set_documentation_caps (templ, static_caps);
  gst_element_class_add_pad_template (element_class, templ);
  gst_caps_unref (static_caps);
  gst_caps_unref (src_caps);

  encoder_class = g_type_class_ref (GST_TYPE_VAAPI_ENCODER_JPEG);
  g_assert (encoder_class);
  gst_vaapiencode_class_install_properties (encode_class, encoder_class);
  g_type_class_unref (encoder_class);
}
