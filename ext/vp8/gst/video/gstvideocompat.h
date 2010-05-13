/*
 * Copyright 2007 David A. Schleef
 *
 * This comes solely from commit 1c3e012fc3e4b9aba19d44855bf8c8bf6ec44cd5
 * to gst-plugins-base.
 */

#ifndef __GST_VIDEOCOMPAT_H__
#define __GST_VIDEOCOMPAT_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/base/gstadapter.h>

/* uh, yeah */
#if GST_VERSION_MICRO < 15

G_BEGIN_DECLS

typedef enum {
  GST_VIDEO_FORMAT_UNKNOWN,
  GST_VIDEO_FORMAT_I420,
  GST_VIDEO_FORMAT_YV12,
  GST_VIDEO_FORMAT_YUY2,
  GST_VIDEO_FORMAT_UYVY,
  GST_VIDEO_FORMAT_AYUV,
  GST_VIDEO_FORMAT_RGBx,
  GST_VIDEO_FORMAT_BGRx,
  GST_VIDEO_FORMAT_xRGB,
  GST_VIDEO_FORMAT_xBGR
} GstVideoFormat;

 
gboolean gst_video_format_parse_caps (GstCaps *caps, GstVideoFormat *format,
    int *width, int *height);
gboolean gst_video_parse_caps_framerate (GstCaps *caps,
    int *fps_n, int *fps_d);
gboolean gst_video_parse_caps_pixel_aspect_ratio (GstCaps *caps,
    int *par_n, int *par_d);
GstCaps * gst_video_format_new_caps (GstVideoFormat format,
    int width, int height, int framerate_n, int framerate_d,
    int par_n, int par_d);
GstVideoFormat gst_video_format_from_fourcc (guint32 fourcc);
guint32 gst_video_format_to_fourcc (GstVideoFormat format);
GstVideoFormat gst_video_format_from_rgb32_masks (int red_mask, int green_mask, int blue_mask);
gboolean gst_video_format_is_rgb (GstVideoFormat format);
gboolean gst_video_format_is_yuv (GstVideoFormat format);
gboolean gst_video_format_has_alpha (GstVideoFormat format);
int gst_video_format_get_row_stride (GstVideoFormat format, int component,
    int width);
int gst_video_format_get_pixel_stride (GstVideoFormat format, int component);
int gst_video_format_get_component_width (GstVideoFormat format, int component,
    int width);
int gst_video_format_get_component_height (GstVideoFormat format, int component,
    int height);
int gst_video_format_get_component_offset (GstVideoFormat format, int component,
    int width, int height);
int gst_video_format_get_size (GstVideoFormat format, int width, int height);
gboolean gst_video_format_convert (GstVideoFormat format, int width, int height,
    int fps_n, int fps_d,
    GstFormat src_format, gint64 src_value,
    GstFormat dest_format, gint64 * dest_value);

void
gst_adapter_copy (GstAdapter * adapter, guint8 * dest, guint offset, guint size);

G_END_DECLS
#endif

#endif /* __GST_VIDEO_H__ */

