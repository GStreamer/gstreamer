/* GStreamer Intel MSDK plugin
 * Copyright (c) 2016, Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif
#include <mfxstructures.h>
#include <mfxjpeg.h>

#include "gstmsdkmjpegenc.h"

GST_DEBUG_CATEGORY_EXTERN (gst_msdkmjpegenc_debug);
#define GST_CAT_DEFAULT gst_msdkmjpegenc_debug

enum
{
  PROP_0,
  PROP_QUALITY
};

#define DEFAULT_QUALITY 85

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("image/jpeg, "
        "framerate = (fraction) [0/1, MAX], "
        "width = (int) [ 1, MAX ], height = (int) [ 1, MAX ]")
    );

#define gst_msdkmjpegenc_parent_class parent_class
G_DEFINE_TYPE (GstMsdkMJPEGEnc, gst_msdkmjpegenc, GST_TYPE_MSDKENC);

static gboolean
gst_msdkmjpegenc_set_format (GstMsdkEnc * encoder)
{
  return TRUE;
}

static gboolean
gst_msdkmjpegenc_configure (GstMsdkEnc * encoder)
{
  GstMsdkMJPEGEnc *mjpegenc = GST_MSDKMJPEGENC (encoder);

  encoder->param.mfx.CodecId = MFX_CODEC_JPEG;
  encoder->param.mfx.Quality = mjpegenc->quality;
  encoder->param.mfx.Interleaved = 1;
  encoder->param.mfx.RestartInterval = 0;
  encoder->param.mfx.BufferSizeInKB = 3072;

  return TRUE;
}

static GstCaps *
gst_msdkmjpegenc_set_src_caps (GstMsdkEnc * encoder)
{
  GstCaps *caps;

  caps = gst_caps_from_string ("image/jpeg");

  return caps;
}

static void
gst_msdkmjpegenc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstMsdkMJPEGEnc *thiz = GST_MSDKMJPEGENC (object);

  GST_OBJECT_LOCK (thiz);
  switch (prop_id) {
    case PROP_QUALITY:
      g_value_set_uint (value, thiz->quality);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (thiz);
}

static void
gst_msdkmjpegenc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMsdkMJPEGEnc *thiz = GST_MSDKMJPEGENC (object);

  GST_OBJECT_LOCK (thiz);
  switch (prop_id) {
    case PROP_QUALITY:
      thiz->quality = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (thiz);
}

static void
gst_msdkmjpegenc_class_init (GstMsdkMJPEGEncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstMsdkEncClass *encoder_class;

  gobject_class = G_OBJECT_CLASS (klass);
  element_class = GST_ELEMENT_CLASS (klass);
  encoder_class = GST_MSDKENC_CLASS (klass);

  encoder_class->set_format = gst_msdkmjpegenc_set_format;
  encoder_class->configure = gst_msdkmjpegenc_configure;
  encoder_class->set_src_caps = gst_msdkmjpegenc_set_src_caps;

  gobject_class->get_property = gst_msdkmjpegenc_get_property;
  gobject_class->set_property = gst_msdkmjpegenc_set_property;

  g_object_class_install_property (gobject_class, PROP_QUALITY,
      g_param_spec_uint ("quality", "Quality", "Quality of encoding",
          0, 100, DEFAULT_QUALITY, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (element_class,
      "Intel MSDK MJPEG encoder",
      "Codec/Encoder/Video",
      "MJPEG video encoder based on Intel Media SDK",
      "Scott D Phillips <scott.d.phillips@intel.com>");

  gst_element_class_add_static_pad_template (element_class, &src_factory);
}

static void
gst_msdkmjpegenc_init (GstMsdkMJPEGEnc * thiz)
{
  thiz->quality = DEFAULT_QUALITY;
}
