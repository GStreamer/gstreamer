/* GStreamer
 * Copyright (C) 2022 Seungha Yang <seungha@centricular.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstqsvh264dec.h"
#include <string>
#include <string.h>

#ifdef G_OS_WIN32
#include <gst/d3d11/gstd3d11.h>
#else
#include <gst/va/gstvadisplay_drm.h>
#endif

GST_DEBUG_CATEGORY_EXTERN (gst_qsv_h264_dec_debug);
#define GST_CAT_DEFAULT gst_qsv_h264_dec_debug

typedef struct _GstQsvH264Dec
{
  GstQsvDecoder parent;
} GstQsvH264Dec;

typedef struct _GstQsvH264DecClass
{
  GstQsvDecoderClass parent_class;
} GstQsvH264DecClass;

static void
gst_qsv_h264_dec_class_init (GstQsvH264DecClass * klass, gpointer data)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstQsvDecoderClass *qsvdec_class = GST_QSV_DECODER_CLASS (klass);
  GstQsvDecoderClassData *cdata = (GstQsvDecoderClassData *) data;

  qsvdec_class->codec_id = MFX_CODEC_AVC;
  qsvdec_class->impl_index = cdata->impl_index;
  qsvdec_class->adapter_luid = cdata->adapter_luid;
  if (cdata->display_path) {
    strncpy (qsvdec_class->display_path, cdata->display_path,
        sizeof (qsvdec_class->display_path));
  }

  gst_element_class_set_static_metadata (element_class,
      "Intel Quick Sync Video H.264 Decoder",
      "Codec/Decoder/Video/Hardware",
      "Intel Quick Sync Video H.264 Decoder",
      "Seungha Yang <seungha@centricular.com>");

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
          cdata->sink_caps));
  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
          cdata->src_caps));

  gst_caps_unref (cdata->sink_caps);
  gst_caps_unref (cdata->src_caps);
  g_free (cdata->display_path);
  g_free (cdata);
}

static void
gst_qsv_h264_dec_init (GstQsvH264Dec * self)
{
}

typedef struct
{
  guint width;
  guint height;
} Resolution;

void
gst_qsv_h264_dec_register (GstPlugin * plugin, guint rank, guint impl_index,
    GstObject * device, mfxSession session)
{
  mfxVideoParam param;
  mfxInfoMFX *mfx;
  static const Resolution resolutions_to_check[] = {
    {1280, 720}, {1920, 1088}, {2560, 1440}, {3840, 2160}, {4096, 2160},
    {7680, 4320}, {8192, 4320}
  };
  Resolution max_resolution;

  memset (&param, 0, sizeof (mfxVideoParam));
  memset (&max_resolution, 0, sizeof (Resolution));

  param.AsyncDepth = 4;
  param.IOPattern = MFX_IOPATTERN_OUT_VIDEO_MEMORY;

  mfx = &param.mfx;
  mfx->CodecId = MFX_CODEC_AVC;

  mfx->FrameInfo.FrameRateExtN = 30;
  mfx->FrameInfo.FrameRateExtD = 1;
  mfx->FrameInfo.AspectRatioW = 1;
  mfx->FrameInfo.AspectRatioH = 1;
  mfx->FrameInfo.ChromaFormat = MFX_CHROMAFORMAT_YUV420;
  mfx->FrameInfo.FourCC = MFX_FOURCC_NV12;
  mfx->FrameInfo.BitDepthLuma = 8;
  mfx->FrameInfo.BitDepthChroma = 8;
  mfx->FrameInfo.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;
  mfx->CodecProfile = MFX_PROFILE_AVC_MAIN;

  /* Check max-resolution */
  for (guint i = 0; i < G_N_ELEMENTS (resolutions_to_check); i++) {
    mfx->FrameInfo.Width = GST_ROUND_UP_16 (resolutions_to_check[i].width);
    mfx->FrameInfo.Height = GST_ROUND_UP_16 (resolutions_to_check[i].height);
    mfx->FrameInfo.CropW = resolutions_to_check[i].width;
    mfx->FrameInfo.CropH = resolutions_to_check[i].height;

    if (MFXVideoDECODE_Query (session, &param, &param) != MFX_ERR_NONE)
      break;

    max_resolution.width = resolutions_to_check[i].width;
    max_resolution.height = resolutions_to_check[i].height;
  }

  GST_INFO ("Maximum supported resolution: %dx%d",
      max_resolution.width, max_resolution.height);

  /* To cover both landscape and portrait,
   * select max value (width in this case) */
  guint resolution = MAX (max_resolution.width, max_resolution.height);
  std::string src_caps_str = "video/x-raw, format=(string) NV12";

  src_caps_str += ", width=(int) [ 16, " + std::to_string (resolution) + " ]";
  src_caps_str += ", height=(int) [ 16, " + std::to_string (resolution) + " ]";

  GstCaps *src_caps = gst_caps_from_string (src_caps_str.c_str ());

  /* TODO: Add support for VA */
#ifdef G_OS_WIN32
  GstCaps *d3d11_caps = gst_caps_copy (src_caps);
  GstCapsFeatures *caps_features =
      gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY, nullptr);
  gst_caps_set_features_simple (d3d11_caps, caps_features);
  gst_caps_append (d3d11_caps, src_caps);
  src_caps = d3d11_caps;
#endif

  std::string sink_caps_str = "video/x-h264";
  sink_caps_str += ", width=(int) [ 16, " + std::to_string (resolution) + " ]";
  sink_caps_str += ", height=(int) [ 16, " + std::to_string (resolution) + " ]";

  sink_caps_str += ", stream-format=(string) byte-stream";
  sink_caps_str += ", alignment=(string) au";
  sink_caps_str += ", profile=(string) { high, progressive-high, "
      "constrained-high, main, constrained-baseline, baseline } ";

  GstCaps *sink_caps = gst_caps_from_string (sink_caps_str.c_str ());

  GST_MINI_OBJECT_FLAG_SET (sink_caps, GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);
  GST_MINI_OBJECT_FLAG_SET (src_caps, GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);

  GstQsvDecoderClassData *cdata = g_new0 (GstQsvDecoderClassData, 1);
  cdata->sink_caps = sink_caps;
  cdata->src_caps = src_caps;
  cdata->impl_index = impl_index;

#ifdef G_OS_WIN32
  gint64 device_luid;
  g_object_get (device, "adapter-luid", &device_luid, nullptr);
  cdata->adapter_luid = device_luid;
#else
  gchar *display_path;
  g_object_get (device, "path", &display_path, nullptr);
  cdata->display_path = display_path;
#endif

  GType type;
  gchar *type_name;
  gchar *feature_name;
  GTypeInfo type_info = {
    sizeof (GstQsvH264DecClass),
    nullptr,
    nullptr,
    (GClassInitFunc) gst_qsv_h264_dec_class_init,
    nullptr,
    cdata,
    sizeof (GstQsvH264Dec),
    0,
    (GInstanceInitFunc) gst_qsv_h264_dec_init,
  };

  type_name = g_strdup ("GstQsvH264Dec");
  feature_name = g_strdup ("qsvh264dec");

  gint index = 0;
  while (g_type_from_name (type_name)) {
    index++;
    g_free (type_name);
    g_free (feature_name);
    type_name = g_strdup_printf ("GstQsvH264Device%dDec", index);
    feature_name = g_strdup_printf ("qsvh264device%ddec", index);
  }

  type = g_type_register_static (GST_TYPE_QSV_DECODER, type_name, &type_info,
      (GTypeFlags) 0);

  if (rank > 0 && index != 0)
    rank--;

  if (!gst_element_register (plugin, feature_name, rank, type))
    GST_WARNING ("Failed to register plugin '%s'", type_name);

  g_free (type_name);
  g_free (feature_name);
}
