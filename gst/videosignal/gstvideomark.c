/* GStreamer
 * Copyright (C) <2007> Wim Taymans <wim@fluendo.com>
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
 * Free Software Foundation, Inc., 51 Franklin Street, Suite 500,
 * Boston, MA 02110-1335, USA.
 */
/**
 * SECTION:element-videomark
 * @see_also: #GstVideoDetect
 *
 * This plugin produces #GstVideoMark::pattern-count squares in the bottom left
 * corner of the video frames. The squares have a width and height of 
 * respectively #GstVideoMark:pattern-width and #GstVideoMark:pattern-height.
 * Even squares will be black and odd squares will be white.
 * 
 * After writing the pattern, #GstVideoMark:pattern-data-count squares after the
 * pattern squares are produced as the bitarray given in
 * #GstVideoMark:pattern-data. 1 bits will produce white squares and 0 bits will
 * produce black squares.
 * 
 * The element can be enabled with the #GstVideoMark:enabled property. It is
 * mostly used together with the #GstVideoDetect plugin.
 * 
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch videotestsrc ! videomark ! ximagesink
 * ]| Add the default black/white squares at the bottom left of the video frames.
 * </refsect2>
 *
 * Last reviewed on 2007-06-01 (0.10.6)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideofilter.h>
#include "gstvideomark.h"

GST_DEBUG_CATEGORY_STATIC (gst_video_mark_debug_category);
#define GST_CAT_DEFAULT gst_video_mark_debug_category

/* prototypes */


static void gst_video_mark_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_video_mark_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_video_mark_dispose (GObject * object);
static void gst_video_mark_finalize (GObject * object);

static gboolean gst_video_mark_start (GstBaseTransform * trans);
static gboolean gst_video_mark_stop (GstBaseTransform * trans);
static gboolean gst_video_mark_set_info (GstVideoFilter * filter,
    GstCaps * incaps, GstVideoInfo * in_info, GstCaps * outcaps,
    GstVideoInfo * out_info);
static GstFlowReturn gst_video_mark_transform_frame_ip (GstVideoFilter * filter,
    GstVideoFrame * frame);

enum
{
  PROP_0,
  PROP_PATTERN_WIDTH,
  PROP_PATTERN_HEIGHT,
  PROP_PATTERN_COUNT,
  PROP_PATTERN_DATA_COUNT,
  PROP_PATTERN_DATA,
  PROP_ENABLED,
  PROP_LEFT_OFFSET,
  PROP_BOTTOM_OFFSET
};

#define DEFAULT_PATTERN_WIDTH        4
#define DEFAULT_PATTERN_HEIGHT       16
#define DEFAULT_PATTERN_COUNT        4
#define DEFAULT_PATTERN_DATA_COUNT   5
#define DEFAULT_PATTERN_DATA         10
#define DEFAULT_ENABLED              TRUE
#define DEFAULT_LEFT_OFFSET          0
#define DEFAULT_BOTTOM_OFFSET        0

/* pad templates */

#define VIDEO_CAPS \
    GST_VIDEO_CAPS_MAKE( \
        "{ I420, YV12, Y41B, Y42B, Y444, YUY2, UYVY, AYUV, YVYU }")


/* class initialization */

G_DEFINE_TYPE_WITH_CODE (GstVideoMark, gst_video_mark, GST_TYPE_VIDEO_FILTER,
    GST_DEBUG_CATEGORY_INIT (gst_video_mark_debug_category, "videomark", 0,
        "debug category for videomark element"));

static void
gst_video_mark_class_init (GstVideoMarkClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseTransformClass *base_transform_class =
      GST_BASE_TRANSFORM_CLASS (klass);
  GstVideoFilterClass *video_filter_class = GST_VIDEO_FILTER_CLASS (klass);

  gst_element_class_add_pad_template (GST_ELEMENT_CLASS (klass),
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
          gst_caps_from_string (VIDEO_CAPS)));
  gst_element_class_add_pad_template (GST_ELEMENT_CLASS (klass),
      gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
          gst_caps_from_string (VIDEO_CAPS)));

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "Video marker", "Filter/Effect/Video",
      "Marks a video signal with a pattern", "Wim Taymans <wim@fluendo.com>");

  gobject_class->set_property = gst_video_mark_set_property;
  gobject_class->get_property = gst_video_mark_get_property;
  gobject_class->dispose = gst_video_mark_dispose;
  gobject_class->finalize = gst_video_mark_finalize;
  base_transform_class->start = GST_DEBUG_FUNCPTR (gst_video_mark_start);
  base_transform_class->stop = GST_DEBUG_FUNCPTR (gst_video_mark_stop);
  video_filter_class->set_info = GST_DEBUG_FUNCPTR (gst_video_mark_set_info);
  video_filter_class->transform_frame_ip =
      GST_DEBUG_FUNCPTR (gst_video_mark_transform_frame_ip);

  g_object_class_install_property (gobject_class, PROP_PATTERN_WIDTH,
      g_param_spec_int ("pattern-width", "Pattern width",
          "The width of the pattern markers", 1, G_MAXINT,
          DEFAULT_PATTERN_WIDTH,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PATTERN_HEIGHT,
      g_param_spec_int ("pattern-height", "Pattern height",
          "The height of the pattern markers", 1, G_MAXINT,
          DEFAULT_PATTERN_HEIGHT,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PATTERN_COUNT,
      g_param_spec_int ("pattern-count", "Pattern count",
          "The number of pattern markers", 0, G_MAXINT,
          DEFAULT_PATTERN_COUNT,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PATTERN_DATA_COUNT,
      g_param_spec_int ("pattern-data-count", "Pattern data count",
          "The number of extra data pattern markers", 0, 64,
          DEFAULT_PATTERN_DATA_COUNT,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PATTERN_DATA,
      g_param_spec_uint64 ("pattern-data", "Pattern data",
          "The extra data pattern markers", 0, G_MAXUINT64,
          DEFAULT_PATTERN_DATA,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_ENABLED,
      g_param_spec_boolean ("enabled", "Enabled",
          "Enable or disable the filter",
          DEFAULT_ENABLED,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_LEFT_OFFSET,
      g_param_spec_int ("left-offset", "Left Offset",
          "The offset from the left border where the pattern starts", 0,
          G_MAXINT, DEFAULT_LEFT_OFFSET,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_BOTTOM_OFFSET,
      g_param_spec_int ("bottom-offset", "Bottom Offset",
          "The offset from the bottom border where the pattern starts", 0,
          G_MAXINT, DEFAULT_BOTTOM_OFFSET,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

}

static void
gst_video_mark_init (GstVideoMark * videomark)
{
}

void
gst_video_mark_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVideoMark *videomark = GST_VIDEO_MARK (object);

  GST_DEBUG_OBJECT (videomark, "set_property");

  switch (property_id) {
    case PROP_PATTERN_WIDTH:
      videomark->pattern_width = g_value_get_int (value);
      break;
    case PROP_PATTERN_HEIGHT:
      videomark->pattern_height = g_value_get_int (value);
      break;
    case PROP_PATTERN_COUNT:
      videomark->pattern_count = g_value_get_int (value);
      break;
    case PROP_PATTERN_DATA_COUNT:
      videomark->pattern_data_count = g_value_get_int (value);
      break;
    case PROP_PATTERN_DATA:
      videomark->pattern_data = g_value_get_uint64 (value);
      break;
    case PROP_ENABLED:
      videomark->enabled = g_value_get_boolean (value);
      break;
    case PROP_LEFT_OFFSET:
      videomark->left_offset = g_value_get_int (value);
      break;
    case PROP_BOTTOM_OFFSET:
      videomark->bottom_offset = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_video_mark_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstVideoMark *videomark = GST_VIDEO_MARK (object);

  GST_DEBUG_OBJECT (videomark, "get_property");

  switch (property_id) {
    case PROP_PATTERN_WIDTH:
      g_value_set_int (value, videomark->pattern_width);
      break;
    case PROP_PATTERN_HEIGHT:
      g_value_set_int (value, videomark->pattern_height);
      break;
    case PROP_PATTERN_COUNT:
      g_value_set_int (value, videomark->pattern_count);
      break;
    case PROP_PATTERN_DATA_COUNT:
      g_value_set_int (value, videomark->pattern_data_count);
      break;
    case PROP_PATTERN_DATA:
      g_value_set_uint64 (value, videomark->pattern_data);
      break;
    case PROP_ENABLED:
      g_value_set_boolean (value, videomark->enabled);
      break;
    case PROP_LEFT_OFFSET:
      g_value_set_int (value, videomark->left_offset);
      break;
    case PROP_BOTTOM_OFFSET:
      g_value_set_int (value, videomark->bottom_offset);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_video_mark_dispose (GObject * object)
{
  GstVideoMark *videomark = GST_VIDEO_MARK (object);

  GST_DEBUG_OBJECT (videomark, "dispose");

  /* clean up as possible.  may be called multiple times */

  G_OBJECT_CLASS (gst_video_mark_parent_class)->dispose (object);
}

void
gst_video_mark_finalize (GObject * object)
{
  GstVideoMark *videomark = GST_VIDEO_MARK (object);

  GST_DEBUG_OBJECT (videomark, "finalize");

  /* clean up object here */

  G_OBJECT_CLASS (gst_video_mark_parent_class)->finalize (object);
}

static gboolean
gst_video_mark_start (GstBaseTransform * trans)
{
  GstVideoMark *videomark = GST_VIDEO_MARK (trans);

  GST_DEBUG_OBJECT (videomark, "start");

  return TRUE;
}

static gboolean
gst_video_mark_stop (GstBaseTransform * trans)
{
  GstVideoMark *videomark = GST_VIDEO_MARK (trans);

  GST_DEBUG_OBJECT (videomark, "stop");

  return TRUE;
}

static gboolean
gst_video_mark_set_info (GstVideoFilter * filter, GstCaps * incaps,
    GstVideoInfo * in_info, GstCaps * outcaps, GstVideoInfo * out_info)
{
  GstVideoMark *videomark = GST_VIDEO_MARK (filter);

  GST_DEBUG_OBJECT (videomark, "set_info");

  return TRUE;
}

static void
gst_video_mark_draw_box (GstVideoMark * videomark, guint8 * data,
    gint width, gint height, gint row_stride, gint pixel_stride, guint8 color)
{
  gint i, j;

  for (i = 0; i < height; i++) {
    for (j = 0; j < width; j++) {
      data[pixel_stride * j] = color;
    }
    data += row_stride;
  }
}

static GstFlowReturn
gst_video_mark_yuv (GstVideoMark * videomark, GstVideoFrame * frame)
{
  gint i, pw, ph, row_stride, pixel_stride;
  gint width, height, req_width, req_height;
  guint8 *d;
  guint64 pattern_shift;
  guint8 color;

  width = frame->info.width;
  height = frame->info.height;

  pw = videomark->pattern_width;
  ph = videomark->pattern_height;
  row_stride = GST_VIDEO_FRAME_COMP_STRIDE (frame, 0);
  pixel_stride = GST_VIDEO_FRAME_COMP_PSTRIDE (frame, 0);

  req_width =
      (videomark->pattern_count + videomark->pattern_data_count) * pw +
      videomark->left_offset;
  req_height = videomark->bottom_offset + ph;
  if (req_width > width || req_height > height) {
    GST_ELEMENT_ERROR (videomark, STREAM, WRONG_TYPE, (NULL),
        ("videomark pattern doesn't fit video, need at least %ix%i (stream has %ix%i)",
            req_width, req_height, width, height));
    return GST_FLOW_ERROR;
  }

  /* draw the bottom left pixels */
  for (i = 0; i < videomark->pattern_count; i++) {
    d = GST_VIDEO_FRAME_COMP_DATA (frame, 0);
    /* move to start of bottom left */
    d += row_stride * (height - ph - videomark->bottom_offset) +
        pixel_stride * videomark->left_offset;
    /* move to i-th pattern */
    d += pixel_stride * pw * i;

    if (i & 1)
      /* odd pixels must be white */
      color = 255;
    else
      color = 0;

    /* draw box of width * height */
    gst_video_mark_draw_box (videomark, d, pw, ph, row_stride, pixel_stride,
        color);
  }

  pattern_shift = G_GUINT64_CONSTANT (1) << (videomark->pattern_data_count - 1);

  /* get the data of the pattern */
  for (i = 0; i < videomark->pattern_data_count; i++) {
    d = GST_VIDEO_FRAME_COMP_DATA (frame, 0);
    /* move to start of bottom left, adjust for offsets */
    d += row_stride * (height - ph - videomark->bottom_offset) +
        pixel_stride * videomark->left_offset;
    /* move after the fixed pattern */
    d += pixel_stride * videomark->pattern_count * pw;
    /* move to i-th pattern data */
    d += pixel_stride * pw * i;

    if (videomark->pattern_data & pattern_shift)
      color = 255;
    else
      color = 0;

    gst_video_mark_draw_box (videomark, d, pw, ph, row_stride, pixel_stride,
        color);

    pattern_shift >>= 1;
  }

  return GST_FLOW_OK;
}


static GstFlowReturn
gst_video_mark_transform_frame_ip (GstVideoFilter * filter,
    GstVideoFrame * frame)
{
  GstVideoMark *videomark = GST_VIDEO_MARK (filter);

  GST_DEBUG_OBJECT (videomark, "transform_frame_ip");

  if (videomark->enabled)
    return gst_video_mark_yuv (videomark, frame);

  return GST_FLOW_OK;
}
