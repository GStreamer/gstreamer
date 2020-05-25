/* GStreamer
 * Copyright (C) 2019 Seungha Yang <seungha.yang@navercorp.com>
 * Copyright (C) 2020 Seungha Yang <seungha@centricular.com>
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

#include "gstmfutils.h"
#include <wrl.h>

using namespace Microsoft::WRL;

G_BEGIN_DECLS

GST_DEBUG_CATEGORY_EXTERN (gst_mf_utils_debug);
#define GST_CAT_DEFAULT gst_mf_utils_debug

G_END_DECLS

#define MAKE_RAW_FORMAT_CAPS(format) \
    "video/x-raw, format = (string) " format

/* No GUID is defined for "Y16 " in mfapi.h, but it's used by several devices */
DEFINE_MEDIATYPE_GUID (MFVideoFormat_Y16, FCC ('Y16 '));

static struct
{
  const GUID &mf_format;
  const gchar *caps_string;
  GstVideoFormat format;
} raw_video_format_map[] = {
  {MFVideoFormat_RGB32,  MAKE_RAW_FORMAT_CAPS ("BGRx"),  GST_VIDEO_FORMAT_BGRx},
  {MFVideoFormat_ARGB32, MAKE_RAW_FORMAT_CAPS ("BGRA"),  GST_VIDEO_FORMAT_BGRA},
  {MFVideoFormat_RGB24,  MAKE_RAW_FORMAT_CAPS ("BGR"),   GST_VIDEO_FORMAT_BGR},
  {MFVideoFormat_RGB555, MAKE_RAW_FORMAT_CAPS ("RGB15"), GST_VIDEO_FORMAT_RGB15},
  {MFVideoFormat_RGB565, MAKE_RAW_FORMAT_CAPS ("RGB16"), GST_VIDEO_FORMAT_RGB16},
  {MFVideoFormat_AYUV,   MAKE_RAW_FORMAT_CAPS ("VUYA"),  GST_VIDEO_FORMAT_VUYA},
  {MFVideoFormat_YUY2,   MAKE_RAW_FORMAT_CAPS ("YUY2"),  GST_VIDEO_FORMAT_YUY2},
  {MFVideoFormat_YVYU,   MAKE_RAW_FORMAT_CAPS ("YVYU"),  GST_VIDEO_FORMAT_YVYU},
  {MFVideoFormat_UYVY,   MAKE_RAW_FORMAT_CAPS ("UYVY"),  GST_VIDEO_FORMAT_UYVY},
  {MFVideoFormat_NV12,   MAKE_RAW_FORMAT_CAPS ("NV12"),  GST_VIDEO_FORMAT_NV12},
  {MFVideoFormat_YV12,   MAKE_RAW_FORMAT_CAPS ("YV12"),  GST_VIDEO_FORMAT_YV12},
  {MFVideoFormat_I420,   MAKE_RAW_FORMAT_CAPS ("I420"),  GST_VIDEO_FORMAT_I420},
  {MFVideoFormat_IYUV,   MAKE_RAW_FORMAT_CAPS ("I420"),  GST_VIDEO_FORMAT_I420},
  {MFVideoFormat_P010,   MAKE_RAW_FORMAT_CAPS ("P010"),  GST_VIDEO_FORMAT_P010_10LE},
  {MFVideoFormat_P016,   MAKE_RAW_FORMAT_CAPS ("P016"),  GST_VIDEO_FORMAT_P016_LE},
  {MFVideoFormat_v210,   MAKE_RAW_FORMAT_CAPS ("v210"),  GST_VIDEO_FORMAT_v210},
  {MFVideoFormat_v216,   MAKE_RAW_FORMAT_CAPS ("v216"),  GST_VIDEO_FORMAT_v216},
  {MFVideoFormat_Y16,    MAKE_RAW_FORMAT_CAPS ("GRAY16_LE"),  GST_VIDEO_FORMAT_GRAY16_LE},
};

static struct
{
  const GUID &mf_format;
  const gchar *caps_string;
} encoded_video_format_map[] = {
  {MFVideoFormat_H264, "video/x-h264"},
  {MFVideoFormat_HEVC, "video/x-h265"},
  {MFVideoFormat_H265, "video/x-h265"},
  {MFVideoFormat_VP80, "video/x-vp8"},
  {MFVideoFormat_VP90, "video/x-vp9"},
};

GstVideoFormat
gst_mf_video_subtype_to_video_format (const GUID * subtype)
{
  gint i;
  for (i = 0; i < G_N_ELEMENTS (raw_video_format_map); i++) {
    if (IsEqualGUID (raw_video_format_map[i].mf_format, *subtype))
      return raw_video_format_map[i].format;
  }

  return GST_VIDEO_FORMAT_UNKNOWN;
}

const GUID *
gst_mf_video_subtype_from_video_format (GstVideoFormat format)
{
  gint i;
  for (i = 0; i < G_N_ELEMENTS (raw_video_format_map); i++) {
    if (raw_video_format_map[i].format == format)
      return &raw_video_format_map[i].mf_format;
  }

  return NULL;
}

static GstCaps *
gst_mf_media_type_to_video_caps (IMFMediaType * media_type)
{
  HRESULT hr;
  GstCaps *caps = NULL;
  gint i;
  guint32 width = 0;
  guint32 height = 0;
  guint32 num, den;
  guint32 val;
  gchar *str;
  GUID subtype;
  GstVideoChromaSite chroma_site;
  GstVideoColorimetry colorimetry;
  gboolean raw_format = TRUE;

  hr = media_type->GetGUID (MF_MT_SUBTYPE, &subtype);
  if (FAILED (hr)) {
    GST_WARNING ("Failed to get subtype, hr: 0x%x", (guint) hr);
    return NULL;
  }

  for (i = 0; i < G_N_ELEMENTS (raw_video_format_map); i++) {
    if (IsEqualGUID (raw_video_format_map[i].mf_format, subtype)) {
      caps = gst_caps_from_string (raw_video_format_map[i].caps_string);
      break;
    }
  }

  if (!caps) {
    for (i = 0; i < G_N_ELEMENTS (encoded_video_format_map); i++) {
      if (IsEqualGUID (encoded_video_format_map[i].mf_format, subtype)) {
        caps = gst_caps_from_string (encoded_video_format_map[i].caps_string);
        raw_format = FALSE;
        break;
      }
    }
  }

  if (!caps) {
    GST_WARNING ("Unknown format %" GST_FOURCC_FORMAT,
        GST_FOURCC_ARGS (subtype.Data1));
    return NULL;
  }

  if (raw_format) {
    hr = MFGetAttributeSize (media_type, MF_MT_FRAME_SIZE, &width, &height);
    if (FAILED (hr) || !width || !height) {
      GST_WARNING ("Couldn't get frame size, hr: 0x%x", (guint) hr);
      gst_caps_unref (caps);

      return NULL;
    }
  }

  if (width > 0 && height > 0) {
    gst_caps_set_simple (caps, "width", G_TYPE_INT, width,
        "height", G_TYPE_INT, height, NULL);
  }

  hr = MFGetAttributeRatio (media_type, MF_MT_FRAME_RATE, &num, &den);
  if (SUCCEEDED (hr) && num > 0 && den > 0)
    gst_caps_set_simple (caps, "framerate", GST_TYPE_FRACTION, num, den, NULL);

  hr = MFGetAttributeRatio (media_type, MF_MT_PIXEL_ASPECT_RATIO, &num, &den);
  if (SUCCEEDED (hr) && num > 0 && den > 0)
    gst_caps_set_simple (caps,
        "pixel-aspect-ratio", GST_TYPE_FRACTION, num, den, NULL);

  colorimetry.range = GST_VIDEO_COLOR_RANGE_UNKNOWN;
  colorimetry.matrix = GST_VIDEO_COLOR_MATRIX_UNKNOWN;
  colorimetry.transfer = GST_VIDEO_TRANSFER_UNKNOWN;
  colorimetry.primaries = GST_VIDEO_COLOR_PRIMARIES_UNKNOWN;

  hr = media_type->GetUINT32 (MF_MT_VIDEO_NOMINAL_RANGE, &val);
  if (SUCCEEDED (hr)) {
    switch (val) {
      case MFNominalRange_0_255:
        colorimetry.range = GST_VIDEO_COLOR_RANGE_0_255;
        break;
      case MFNominalRange_16_235:
        colorimetry.range = GST_VIDEO_COLOR_RANGE_16_235;
        break;
      default:
        break;
    }
  }

  hr = media_type->GetUINT32 (MF_MT_VIDEO_PRIMARIES, &val);
  if (SUCCEEDED (hr)) {
    switch (val) {
      case MFVideoPrimaries_BT709:
        colorimetry.primaries = GST_VIDEO_COLOR_PRIMARIES_BT709;
        break;
      case MFVideoPrimaries_BT470_2_SysM:
        colorimetry.primaries = GST_VIDEO_COLOR_PRIMARIES_BT470M;
        break;
      case MFVideoPrimaries_BT470_2_SysBG:
        colorimetry.primaries = GST_VIDEO_COLOR_PRIMARIES_BT470BG;
        break;
      case MFVideoPrimaries_SMPTE170M:
        colorimetry.primaries = GST_VIDEO_COLOR_PRIMARIES_SMPTE170M;
        break;
      case MFVideoPrimaries_SMPTE240M:
        colorimetry.primaries = GST_VIDEO_COLOR_PRIMARIES_SMPTE240M;
        break;
      case MFVideoPrimaries_EBU3213:
        colorimetry.primaries = GST_VIDEO_COLOR_PRIMARIES_EBU3213;
        break;
      case MFVideoPrimaries_BT2020:
        colorimetry.primaries = GST_VIDEO_COLOR_PRIMARIES_BT2020;
        break;
      default:
        GST_FIXME ("unhandled color primaries %d", val);
        break;
    }
  }

  hr = media_type->GetUINT32 (MF_MT_YUV_MATRIX, &val);
  if (SUCCEEDED (hr)) {
    switch (val) {
      case MFVideoTransferMatrix_BT709:
        colorimetry.matrix = GST_VIDEO_COLOR_MATRIX_BT709;
        break;
      case MFVideoTransferMatrix_BT601:
        colorimetry.matrix = GST_VIDEO_COLOR_MATRIX_BT601;
        break;
      case MFVideoTransferMatrix_SMPTE240M:
        colorimetry.matrix = GST_VIDEO_COLOR_MATRIX_SMPTE240M;
        break;
      case MFVideoTransferMatrix_BT2020_10:
      case MFVideoTransferMatrix_BT2020_12:
        colorimetry.matrix = GST_VIDEO_COLOR_MATRIX_BT2020;
        break;
      default:
        GST_FIXME ("unhandled color matrix %d", val);
        break;
    }
  }

  hr = media_type->GetUINT32 (MF_MT_TRANSFER_FUNCTION, &val);
  if (SUCCEEDED (hr)) {
    switch (val) {
      case MFVideoTransFunc_10:
        colorimetry.transfer = GST_VIDEO_TRANSFER_GAMMA10;
        break;
      case MFVideoTransFunc_18:
        colorimetry.transfer = GST_VIDEO_TRANSFER_GAMMA18;
        break;
      case MFVideoTransFunc_20:
        colorimetry.transfer = GST_VIDEO_TRANSFER_GAMMA20;
        break;
      case MFVideoTransFunc_22:
        colorimetry.transfer = GST_VIDEO_TRANSFER_GAMMA22;
        break;
      case MFVideoTransFunc_709:
      case MFVideoTransFunc_709_sym:
        colorimetry.transfer = GST_VIDEO_TRANSFER_BT709;
        break;
      case MFVideoTransFunc_240M:
        colorimetry.transfer = GST_VIDEO_TRANSFER_SMPTE240M;
        break;
      case MFVideoTransFunc_sRGB:
        colorimetry.transfer = GST_VIDEO_TRANSFER_SRGB;
        break;
      case MFVideoTransFunc_28:
        colorimetry.transfer = GST_VIDEO_TRANSFER_GAMMA28;
        break;
      case MFVideoTransFunc_Log_100:
        colorimetry.transfer = GST_VIDEO_TRANSFER_LOG100;
        break;
      case MFVideoTransFunc_Log_316:
        colorimetry.transfer = GST_VIDEO_TRANSFER_LOG316;
        break;
      case MFVideoTransFunc_2020_const:
      case MFVideoTransFunc_2020:
        colorimetry.transfer = GST_VIDEO_TRANSFER_BT2020_10;
        break;
      case MFVideoTransFunc_2084:
        colorimetry.transfer = GST_VIDEO_TRANSFER_SMPTE2084;
        break;
      case MFVideoTransFunc_HLG:
        colorimetry.transfer = GST_VIDEO_TRANSFER_ARIB_STD_B67;
        break;
      default:
        GST_FIXME ("unhandled color transfer %d", val);
        break;
    }
  }

  str = gst_video_colorimetry_to_string (&colorimetry);
  if (str) {
    gst_caps_set_simple (caps, "colorimetry", G_TYPE_STRING, str, NULL);
    g_free (str);
    str = NULL;
  }

  chroma_site = GST_VIDEO_CHROMA_SITE_UNKNOWN;

  hr = media_type->GetUINT32 (MF_MT_VIDEO_CHROMA_SITING, &val);
  if (SUCCEEDED (hr)) {
    GST_LOG ("have chroma site 0x%x", val);

    if ((val & MFVideoChromaSubsampling_MPEG2) ==
        MFVideoChromaSubsampling_MPEG2) {
      chroma_site = GST_VIDEO_CHROMA_SITE_MPEG2;
    } else if ((val & MFVideoChromaSubsampling_DV_PAL) ==
        MFVideoChromaSubsampling_DV_PAL) {
      chroma_site = GST_VIDEO_CHROMA_SITE_DV;
    } else if ((val & MFVideoChromaSubsampling_Cosited) ==
        MFVideoChromaSubsampling_Cosited) {
      chroma_site = GST_VIDEO_CHROMA_SITE_COSITED;
    } else {
      GST_FIXME ("unhandled chroma site 0x%x", val);
    }
  }

  if (chroma_site != GST_VIDEO_CHROMA_SITE_UNKNOWN)
    gst_caps_set_simple (caps, "chroma-site", G_TYPE_STRING,
        gst_video_chroma_to_string (chroma_site), NULL);

  return caps;
}

GstCaps *
gst_mf_media_type_to_caps (IMFMediaType * media_type)
{
  GUID major_type;
  HRESULT hr;

  g_return_val_if_fail (media_type != NULL, NULL);

  hr = media_type->GetMajorType (&major_type);
  if (FAILED (hr)) {
    GST_WARNING ("failed to get major type, hr: 0x%x", (guint) hr);
    return NULL;
  }

  if (IsEqualGUID (major_type, MFMediaType_Video))
    return gst_mf_media_type_to_video_caps (media_type);

  return NULL;
}

static gchar *
gst_mf_hr_to_string (HRESULT hr)
{
  DWORD flags;
  gchar *ret_text;
  LPTSTR error_text = NULL;

  flags = FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER
      | FORMAT_MESSAGE_IGNORE_INSERTS;
  FormatMessage (flags, NULL, hr, MAKELANGID (LANG_NEUTRAL, SUBLANG_DEFAULT),
      (LPTSTR) & error_text, 0, NULL);

#ifdef UNICODE
  ret_text = g_utf16_to_utf8 ((const gunichar2  *) error_text,
      -1, NULL, NULL, NULL);
#else
  ret_text = g_strdup (error_text);
#endif

  LocalFree (error_text);
  return ret_text;
}

gboolean
_gst_mf_result (HRESULT hr, GstDebugCategory * cat, const gchar * file,
    const gchar * function, gint line)
{
#ifndef GST_DISABLE_GST_DEBUG
  gboolean ret = TRUE;

  if (FAILED (hr)) {
    gchar *error_text = NULL;

    error_text = gst_mf_hr_to_string (hr);
    gst_debug_log (cat, GST_LEVEL_WARNING, file, function, line,
        NULL, "MediaFoundation call failed: 0x%x, %s", (guint) hr, error_text);
    g_free (error_text);

    ret = FALSE;
  }

  return ret;
#else
  return SUCCEEDED (hr);
#endif
}

