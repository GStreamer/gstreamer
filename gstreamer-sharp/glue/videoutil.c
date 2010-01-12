#include <gst/gst.h>
#include <gst/video/video.h>

GstCaps *
gstsharp_gst_videoutil_get_template_caps (GstVideoFormat fmt) {
  if (gst_video_format_is_yuv (fmt)) {
    guint32 fourcc = gst_video_format_to_fourcc (fmt);
    GstCaps *caps;

    if (fourcc == 0)
      return NULL;

    caps = gst_caps_from_string (GST_VIDEO_CAPS_YUV ("AYUV"));
    gst_caps_set_simple (caps, "format", GST_TYPE_FOURCC, fourcc, NULL);
    return caps;
  } else {
    switch (fmt) {
      case GST_VIDEO_FORMAT_ABGR:
        return gst_caps_from_string (GST_VIDEO_CAPS_ABGR);
      case GST_VIDEO_FORMAT_ARGB:
        return gst_caps_from_string (GST_VIDEO_CAPS_ARGB);
      case GST_VIDEO_FORMAT_BGR:
        return gst_caps_from_string (GST_VIDEO_CAPS_BGR);
      case GST_VIDEO_FORMAT_BGRA:
        return gst_caps_from_string (GST_VIDEO_CAPS_BGRA);
      case GST_VIDEO_FORMAT_BGRx:
        return gst_caps_from_string (GST_VIDEO_CAPS_BGRx);
      case GST_VIDEO_FORMAT_RGB:
        return gst_caps_from_string (GST_VIDEO_CAPS_RGB);
      case GST_VIDEO_FORMAT_RGBA:
        return gst_caps_from_string (GST_VIDEO_CAPS_RGBA);
      case GST_VIDEO_FORMAT_RGBx:
        return gst_caps_from_string (GST_VIDEO_CAPS_RGBx);
      case GST_VIDEO_FORMAT_xBGR:
        return gst_caps_from_string (GST_VIDEO_CAPS_xBGR);
      case GST_VIDEO_FORMAT_xRGB:
        return gst_caps_from_string (GST_VIDEO_CAPS_xRGB);
      default:
        return NULL;
    }
  }

  return NULL;
}
