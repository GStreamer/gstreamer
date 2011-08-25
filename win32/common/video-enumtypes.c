


#include "video-enumtypes.h"

#include "video.h"

/* enumerations from "video.h" */
GType
gst_video_format_get_type (void)
{
  static volatile gsize g_define_type_id__volatile = 0;
  if (g_once_init_enter (&g_define_type_id__volatile)) {
    static const GEnumValue values[] = {
      {GST_VIDEO_FORMAT_UNKNOWN, "GST_VIDEO_FORMAT_UNKNOWN", "unknown"},
      {GST_VIDEO_FORMAT_I420, "GST_VIDEO_FORMAT_I420", "i420"},
      {GST_VIDEO_FORMAT_YV12, "GST_VIDEO_FORMAT_YV12", "yv12"},
      {GST_VIDEO_FORMAT_YUY2, "GST_VIDEO_FORMAT_YUY2", "yuy2"},
      {GST_VIDEO_FORMAT_UYVY, "GST_VIDEO_FORMAT_UYVY", "uyvy"},
      {GST_VIDEO_FORMAT_AYUV, "GST_VIDEO_FORMAT_AYUV", "ayuv"},
      {GST_VIDEO_FORMAT_RGBx, "GST_VIDEO_FORMAT_RGBx", "rgbx"},
      {GST_VIDEO_FORMAT_BGRx, "GST_VIDEO_FORMAT_BGRx", "bgrx"},
      {GST_VIDEO_FORMAT_xRGB, "GST_VIDEO_FORMAT_xRGB", "xrgb"},
      {GST_VIDEO_FORMAT_xBGR, "GST_VIDEO_FORMAT_xBGR", "xbgr"},
      {GST_VIDEO_FORMAT_RGBA, "GST_VIDEO_FORMAT_RGBA", "rgba"},
      {GST_VIDEO_FORMAT_BGRA, "GST_VIDEO_FORMAT_BGRA", "bgra"},
      {GST_VIDEO_FORMAT_ARGB, "GST_VIDEO_FORMAT_ARGB", "argb"},
      {GST_VIDEO_FORMAT_ABGR, "GST_VIDEO_FORMAT_ABGR", "abgr"},
      {GST_VIDEO_FORMAT_RGB, "GST_VIDEO_FORMAT_RGB", "rgb"},
      {GST_VIDEO_FORMAT_BGR, "GST_VIDEO_FORMAT_BGR", "bgr"},
      {GST_VIDEO_FORMAT_Y41B, "GST_VIDEO_FORMAT_Y41B", "y41b"},
      {GST_VIDEO_FORMAT_Y42B, "GST_VIDEO_FORMAT_Y42B", "y42b"},
      {GST_VIDEO_FORMAT_YVYU, "GST_VIDEO_FORMAT_YVYU", "yvyu"},
      {GST_VIDEO_FORMAT_Y444, "GST_VIDEO_FORMAT_Y444", "y444"},
      {GST_VIDEO_FORMAT_v210, "GST_VIDEO_FORMAT_v210", "v210"},
      {GST_VIDEO_FORMAT_v216, "GST_VIDEO_FORMAT_v216", "v216"},
      {GST_VIDEO_FORMAT_NV12, "GST_VIDEO_FORMAT_NV12", "nv12"},
      {GST_VIDEO_FORMAT_NV21, "GST_VIDEO_FORMAT_NV21", "nv21"},
      {GST_VIDEO_FORMAT_GRAY8, "GST_VIDEO_FORMAT_GRAY8", "gray8"},
      {GST_VIDEO_FORMAT_GRAY16_BE, "GST_VIDEO_FORMAT_GRAY16_BE", "gray16-be"},
      {GST_VIDEO_FORMAT_GRAY16_LE, "GST_VIDEO_FORMAT_GRAY16_LE", "gray16-le"},
      {GST_VIDEO_FORMAT_v308, "GST_VIDEO_FORMAT_v308", "v308"},
      {GST_VIDEO_FORMAT_Y800, "GST_VIDEO_FORMAT_Y800", "y800"},
      {GST_VIDEO_FORMAT_Y16, "GST_VIDEO_FORMAT_Y16", "y16"},
      {GST_VIDEO_FORMAT_RGB16, "GST_VIDEO_FORMAT_RGB16", "rgb16"},
      {GST_VIDEO_FORMAT_BGR16, "GST_VIDEO_FORMAT_BGR16", "bgr16"},
      {GST_VIDEO_FORMAT_RGB15, "GST_VIDEO_FORMAT_RGB15", "rgb15"},
      {GST_VIDEO_FORMAT_BGR15, "GST_VIDEO_FORMAT_BGR15", "bgr15"},
      {GST_VIDEO_FORMAT_UYVP, "GST_VIDEO_FORMAT_UYVP", "uyvp"},
      {GST_VIDEO_FORMAT_A420, "GST_VIDEO_FORMAT_A420", "a420"},
      {GST_VIDEO_FORMAT_RGB8_PALETTED, "GST_VIDEO_FORMAT_RGB8_PALETTED",
          "rgb8-paletted"},
      {GST_VIDEO_FORMAT_YUV9, "GST_VIDEO_FORMAT_YUV9", "yuv9"},
      {GST_VIDEO_FORMAT_YVU9, "GST_VIDEO_FORMAT_YVU9", "yvu9"},
      {GST_VIDEO_FORMAT_IYU1, "GST_VIDEO_FORMAT_IYU1", "iyu1"},
      {GST_VIDEO_FORMAT_ARGB64, "GST_VIDEO_FORMAT_ARGB64", "argb64"},
      {GST_VIDEO_FORMAT_AYUV64, "GST_VIDEO_FORMAT_AYUV64", "ayuv64"},
      {GST_VIDEO_FORMAT_r210, "GST_VIDEO_FORMAT_r210", "r210"},
      {0, NULL, NULL}
    };
    GType g_define_type_id = g_enum_register_static ("GstVideoFormat", values);
    g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
  }
  return g_define_type_id__volatile;
}

GType
gst_video_format_flags_get_type (void)
{
  static volatile gsize g_define_type_id__volatile = 0;
  if (g_once_init_enter (&g_define_type_id__volatile)) {
    static const GFlagsValue values[] = {
      {GST_VIDEO_FORMAT_FLAG_YUV, "GST_VIDEO_FORMAT_FLAG_YUV", "yuv"},
      {GST_VIDEO_FORMAT_FLAG_RGB, "GST_VIDEO_FORMAT_FLAG_RGB", "rgb"},
      {GST_VIDEO_FORMAT_FLAG_GRAY, "GST_VIDEO_FORMAT_FLAG_GRAY", "gray"},
      {GST_VIDEO_FORMAT_FLAG_ALPHA, "GST_VIDEO_FORMAT_FLAG_ALPHA", "alpha"},
      {GST_VIDEO_FORMAT_FLAG_LE, "GST_VIDEO_FORMAT_FLAG_LE", "le"},
      {GST_VIDEO_FORMAT_FLAG_PALETTE, "GST_VIDEO_FORMAT_FLAG_PALETTE",
          "palette"},
      {GST_VIDEO_FORMAT_FLAG_COMPLEX, "GST_VIDEO_FORMAT_FLAG_COMPLEX",
          "complex"},
      {0, NULL, NULL}
    };
    GType g_define_type_id =
        g_flags_register_static ("GstVideoFormatFlags", values);
    g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
  }
  return g_define_type_id__volatile;
}

GType
gst_video_flags_get_type (void)
{
  static volatile gsize g_define_type_id__volatile = 0;
  if (g_once_init_enter (&g_define_type_id__volatile)) {
    static const GFlagsValue values[] = {
      {GST_VIDEO_FLAG_NONE, "GST_VIDEO_FLAG_NONE", "none"},
      {GST_VIDEO_FLAG_INTERLACED, "GST_VIDEO_FLAG_INTERLACED", "interlaced"},
      {GST_VIDEO_FLAG_TFF, "GST_VIDEO_FLAG_TFF", "tff"},
      {GST_VIDEO_FLAG_RFF, "GST_VIDEO_FLAG_RFF", "rff"},
      {GST_VIDEO_FLAG_ONEFIELD, "GST_VIDEO_FLAG_ONEFIELD", "onefield"},
      {GST_VIDEO_FLAG_TELECINE, "GST_VIDEO_FLAG_TELECINE", "telecine"},
      {GST_VIDEO_FLAG_PROGRESSIVE, "GST_VIDEO_FLAG_PROGRESSIVE", "progressive"},
      {0, NULL, NULL}
    };
    GType g_define_type_id = g_flags_register_static ("GstVideoFlags", values);
    g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
  }
  return g_define_type_id__volatile;
}

GType
gst_video_chroma_site_get_type (void)
{
  static volatile gsize g_define_type_id__volatile = 0;
  if (g_once_init_enter (&g_define_type_id__volatile)) {
    static const GFlagsValue values[] = {
      {GST_VIDEO_CHROMA_SITE_UNKNOWN, "GST_VIDEO_CHROMA_SITE_UNKNOWN",
          "unknown"},
      {GST_VIDEO_CHROMA_SITE_NONE, "GST_VIDEO_CHROMA_SITE_NONE", "none"},
      {GST_VIDEO_CHROMA_SITE_H_COSITED, "GST_VIDEO_CHROMA_SITE_H_COSITED",
          "h-cosited"},
      {GST_VIDEO_CHROMA_SITE_V_COSITED, "GST_VIDEO_CHROMA_SITE_V_COSITED",
          "v-cosited"},
      {GST_VIDEO_CHROMA_SITE_ALT_LINE, "GST_VIDEO_CHROMA_SITE_ALT_LINE",
          "alt-line"},
      {GST_VIDEO_CHROMA_SITE_COSITED, "GST_VIDEO_CHROMA_SITE_COSITED",
          "cosited"},
      {GST_VIDEO_CHROMA_SITE_JPEG, "GST_VIDEO_CHROMA_SITE_JPEG", "jpeg"},
      {GST_VIDEO_CHROMA_SITE_MPEG2, "GST_VIDEO_CHROMA_SITE_MPEG2", "mpeg2"},
      {GST_VIDEO_CHROMA_SITE_DV, "GST_VIDEO_CHROMA_SITE_DV", "dv"},
      {0, NULL, NULL}
    };
    GType g_define_type_id =
        g_flags_register_static ("GstVideoChromaSite", values);
    g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
  }
  return g_define_type_id__volatile;
}

GType
gst_video_color_range_get_type (void)
{
  static volatile gsize g_define_type_id__volatile = 0;
  if (g_once_init_enter (&g_define_type_id__volatile)) {
    static const GEnumValue values[] = {
      {GST_VIDEO_COLOR_RANGE_UNKNOWN, "GST_VIDEO_COLOR_RANGE_UNKNOWN",
          "unknown"},
      {GST_VIDEO_COLOR_RANGE_0_255, "GST_VIDEO_COLOR_RANGE_0_255", "0-255"},
      {GST_VIDEO_COLOR_RANGE_16_235, "GST_VIDEO_COLOR_RANGE_16_235", "16-235"},
      {0, NULL, NULL}
    };
    GType g_define_type_id =
        g_enum_register_static ("GstVideoColorRange", values);
    g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
  }
  return g_define_type_id__volatile;
}

GType
gst_video_color_matrix_get_type (void)
{
  static volatile gsize g_define_type_id__volatile = 0;
  if (g_once_init_enter (&g_define_type_id__volatile)) {
    static const GEnumValue values[] = {
      {GST_VIDEO_COLOR_MATRIX_UNKNOWN, "GST_VIDEO_COLOR_MATRIX_UNKNOWN",
          "unknown"},
      {GST_VIDEO_COLOR_MATRIX_RGB, "GST_VIDEO_COLOR_MATRIX_RGB", "rgb"},
      {GST_VIDEO_COLOR_MATRIX_BT709, "GST_VIDEO_COLOR_MATRIX_BT709", "bt709"},
      {GST_VIDEO_COLOR_MATRIX_BT601, "GST_VIDEO_COLOR_MATRIX_BT601", "bt601"},
      {GST_VIDEO_COLOR_MATRIX_SMPTE240M, "GST_VIDEO_COLOR_MATRIX_SMPTE240M",
          "smpte240m"},
      {0, NULL, NULL}
    };
    GType g_define_type_id =
        g_enum_register_static ("GstVideoColorMatrix", values);
    g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
  }
  return g_define_type_id__volatile;
}

GType
gst_video_transfer_function_get_type (void)
{
  static volatile gsize g_define_type_id__volatile = 0;
  if (g_once_init_enter (&g_define_type_id__volatile)) {
    static const GEnumValue values[] = {
      {GST_VIDEO_TRANSFER_UNKNOWN, "GST_VIDEO_TRANSFER_UNKNOWN", "unknown"},
      {GST_VIDEO_TRANSFER_GAMMA10, "GST_VIDEO_TRANSFER_GAMMA10", "gamma10"},
      {GST_VIDEO_TRANSFER_GAMMA18, "GST_VIDEO_TRANSFER_GAMMA18", "gamma18"},
      {GST_VIDEO_TRANSFER_GAMMA20, "GST_VIDEO_TRANSFER_GAMMA20", "gamma20"},
      {GST_VIDEO_TRANSFER_GAMMA22, "GST_VIDEO_TRANSFER_GAMMA22", "gamma22"},
      {GST_VIDEO_TRANSFER_BT709, "GST_VIDEO_TRANSFER_BT709", "bt709"},
      {GST_VIDEO_TRANSFER_SMPTE240M, "GST_VIDEO_TRANSFER_SMPTE240M",
          "smpte240m"},
      {GST_VIDEO_TRANSFER_SRGB, "GST_VIDEO_TRANSFER_SRGB", "srgb"},
      {GST_VIDEO_TRANSFER_GAMMA28, "GST_VIDEO_TRANSFER_GAMMA28", "gamma28"},
      {0, NULL, NULL}
    };
    GType g_define_type_id =
        g_enum_register_static ("GstVideoTransferFunction", values);
    g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
  }
  return g_define_type_id__volatile;
}

GType
gst_video_color_primaries_get_type (void)
{
  static volatile gsize g_define_type_id__volatile = 0;
  if (g_once_init_enter (&g_define_type_id__volatile)) {
    static const GEnumValue values[] = {
      {GST_VIDEO_COLOR_PRIMARIES_UNKNOWN, "GST_VIDEO_COLOR_PRIMARIES_UNKNOWN",
          "unknown"},
      {GST_VIDEO_COLOR_PRIMARIES_BT709, "GST_VIDEO_COLOR_PRIMARIES_BT709",
          "bt709"},
      {GST_VIDEO_COLOR_PRIMARIES_BT470M, "GST_VIDEO_COLOR_PRIMARIES_BT470M",
          "bt470m"},
      {GST_VIDEO_COLOR_PRIMARIES_BT470BG, "GST_VIDEO_COLOR_PRIMARIES_BT470BG",
          "bt470bg"},
      {GST_VIDEO_COLOR_PRIMARIES_SMPTE170M,
          "GST_VIDEO_COLOR_PRIMARIES_SMPTE170M", "smpte170m"},
      {GST_VIDEO_COLOR_PRIMARIES_SMPTE240M,
          "GST_VIDEO_COLOR_PRIMARIES_SMPTE240M", "smpte240m"},
      {0, NULL, NULL}
    };
    GType g_define_type_id =
        g_enum_register_static ("GstVideoColorPrimaries", values);
    g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
  }
  return g_define_type_id__volatile;
}

GType
gst_video_buffer_flags_get_type (void)
{
  static volatile gsize g_define_type_id__volatile = 0;
  if (g_once_init_enter (&g_define_type_id__volatile)) {
    static const GFlagsValue values[] = {
      {GST_VIDEO_BUFFER_FLAG_TFF, "GST_VIDEO_BUFFER_FLAG_TFF", "tff"},
      {GST_VIDEO_BUFFER_FLAG_RFF, "GST_VIDEO_BUFFER_FLAG_RFF", "rff"},
      {GST_VIDEO_BUFFER_FLAG_ONEFIELD, "GST_VIDEO_BUFFER_FLAG_ONEFIELD",
          "onefield"},
      {GST_VIDEO_BUFFER_FLAG_PROGRESSIVE, "GST_VIDEO_BUFFER_FLAG_PROGRESSIVE",
          "progressive"},
      {GST_VIDEO_BUFFER_FLAG_LAST, "GST_VIDEO_BUFFER_FLAG_LAST", "last"},
      {0, NULL, NULL}
    };
    GType g_define_type_id =
        g_flags_register_static ("GstVideoBufferFlags", values);
    g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
  }
  return g_define_type_id__volatile;
}
