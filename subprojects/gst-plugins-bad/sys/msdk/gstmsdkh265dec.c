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
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGDECE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

 /**
  * SECTION:element-msdkh265dec
  * @title: msdkh265dec
  * @short_description: Intel MSDK H265 decoder
  *
  * H265 video decoder based on Intel MFX
  *
  * ## Example launch line
  * ```
  * gst-launch-1.0 filesrc location=sample.h265 ! h265parse ! msdkh265dec ! glimagesink
  * ```
  *
  * Since: 1.12
  *
  */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "gstmsdkh265dec.h"

GST_DEBUG_CATEGORY_EXTERN (gst_msdkh265dec_debug);
#define GST_CAT_DEFAULT gst_msdkh265dec_debug

#define COMMON_FORMAT \
  "{ NV12, P010_10LE, YUY2, Y210, VUYA, Y410, P012_LE, Y212_LE, Y412_LE, BGRA, BGRx }"

#ifndef _WIN32
#define VA_SRC_CAPS_STR \
    "; " GST_MSDK_CAPS_MAKE_WITH_VA_FEATURE ("{ NV12 }")
#else
#define D3D11_SRC_CAPS_STR \
    ";" GST_MSDK_CAPS_MAKE_WITH_D3D11_FEATURE ("{ NV12 }")
#endif

/* TODO: update both sink and src dynamically */
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h265, "
        "width = (int) [ 1, MAX ], height = (int) [ 1, MAX ], "
        "stream-format = (string) byte-stream , alignment = (string) au ")
    );

#ifndef _WIN32
static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_MSDK_CAPS_STR (COMMON_FORMAT, COMMON_FORMAT)
        VA_SRC_CAPS_STR));
#else
static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_MSDK_CAPS_STR (COMMON_FORMAT, COMMON_FORMAT)
        D3D11_SRC_CAPS_STR));
#endif

#define gst_msdkh265dec_parent_class parent_class
G_DEFINE_TYPE (GstMsdkH265Dec, gst_msdkh265dec, GST_TYPE_MSDKDEC);

static gboolean
gst_msdkh265dec_configure (GstMsdkDec * decoder)
{
  GstMsdkH265Dec *h265dec = GST_MSDKH265DEC (decoder);
  mfxSession session;
  const mfxPluginUID *uid;

  session = gst_msdk_context_get_session (decoder->context);

  if (decoder->hardware)
    uid = &MFX_PLUGINID_HEVCD_HW;
  else
    uid = &MFX_PLUGINID_HEVCD_SW;

  if (!gst_msdk_load_plugin (session, uid, 1, "msdkh265dec"))
    return FALSE;

  decoder->param.mfx.CodecId = MFX_CODEC_HEVC;

  /* This is a deprecated attribute in msdk-2017 version, but some
   * customers still using this for low-latency streaming of non-b-frame
   * encoded streams */
  decoder->param.mfx.DecodedOrder = h265dec->output_order;

#if (MFX_VERSION >= 1025)
  if (decoder->report_error) {
    decoder->error_report.Header.BufferId = MFX_EXTBUFF_DECODE_ERROR_REPORT;
    decoder->error_report.Header.BufferSz = sizeof (decoder->error_report);
    decoder->error_report.ErrorTypes = 0;
    gst_msdkdec_add_bs_extra_param (decoder,
        (mfxExtBuffer *) & decoder->error_report);
  }
#endif

  return TRUE;
}

static void
gst_msdkdec_h265_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMsdkH265Dec *thiz = GST_MSDKH265DEC (object);
#if (MFX_VERSION >= 1025)
  GstMsdkDec *dec = GST_MSDKDEC (object);
#endif
  GstState state;

  GST_OBJECT_LOCK (thiz);
  state = GST_STATE (thiz);

  if (!gst_msdkdec_prop_check_state (state, pspec)) {
    GST_WARNING_OBJECT (thiz, "setting property in wrong state");
    GST_OBJECT_UNLOCK (thiz);
    return;
  }
  switch (prop_id) {
    case GST_MSDKDEC_PROP_OUTPUT_ORDER:
      thiz->output_order = g_value_get_enum (value);
      break;
#if (MFX_VERSION >= 1025)
    case GST_MSDKDEC_PROP_ERROR_REPORT:
      dec->report_error = g_value_get_boolean (value);
      break;
#endif
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (thiz);
  return;
}

static void
gst_msdkdec_h265_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstMsdkH265Dec *thiz = GST_MSDKH265DEC (object);
#if (MFX_VERSION >= 1025)
  GstMsdkDec *dec = GST_MSDKDEC (object);
#endif

  GST_OBJECT_LOCK (thiz);
  switch (prop_id) {
    case GST_MSDKDEC_PROP_OUTPUT_ORDER:
      g_value_set_enum (value, thiz->output_order);
      break;
#if (MFX_VERSION >= 1025)
    case GST_MSDKDEC_PROP_ERROR_REPORT:
      g_value_set_boolean (value, dec->report_error);
      break;
#endif
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (thiz);
}

static void
gst_msdkh265dec_class_init (GstMsdkH265DecClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstMsdkDecClass *decoder_class;

  gobject_class = G_OBJECT_CLASS (klass);
  element_class = GST_ELEMENT_CLASS (klass);
  decoder_class = GST_MSDKDEC_CLASS (klass);

  gobject_class->set_property = gst_msdkdec_h265_set_property;
  gobject_class->get_property = gst_msdkdec_h265_get_property;

  decoder_class->configure = GST_DEBUG_FUNCPTR (gst_msdkh265dec_configure);

  gst_element_class_set_static_metadata (element_class,
      "Intel MSDK H265 decoder",
      "Codec/Decoder/Video/Hardware",
      "H265 video decoder based on " MFX_API_SDK,
      "Scott D Phillips <scott.d.phillips@intel.com>");

  gst_msdkdec_prop_install_output_oder_property (gobject_class);

#if (MFX_VERSION >= 1025)
  gst_msdkdec_prop_install_error_report_property (gobject_class);
#endif

  gst_element_class_add_static_pad_template (element_class, &sink_factory);
  gst_element_class_add_static_pad_template (element_class, &src_factory);
}

static void
gst_msdkh265dec_init (GstMsdkH265Dec * thiz)
{
  thiz->output_order = PROP_OUTPUT_ORDER_DEFAULT;
}
