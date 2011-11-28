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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
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

#include "gstvideomark.h"

#include <string.h>
#include <math.h>

#include <gst/video/video.h>

/* GstVideoMark signals and args */

#define DEFAULT_PATTERN_WIDTH        4
#define DEFAULT_PATTERN_HEIGHT       16
#define DEFAULT_PATTERN_COUNT        4
#define DEFAULT_PATTERN_DATA_COUNT   5
#define DEFAULT_PATTERN_DATA         10
#define DEFAULT_ENABLED              TRUE
#define DEFAULT_LEFT_OFFSET          0
#define DEFAULT_BOTTOM_OFFSET        0

enum
{
  PROP_0,
  PROP_PATTERN_WIDTH,
  PROP_PATTERN_HEIGHT,
  PROP_PATTERN_COUNT,
  PROP_PATTERN_DATA_COUNT,
  PROP_PATTERN_DATA,
  PROP_PATTERN_DATA_64,
  PROP_ENABLED,
  PROP_LEFT_OFFSET,
  PROP_BOTTOM_OFFSET
};

GST_DEBUG_CATEGORY_STATIC (video_mark_debug);
#define GST_CAT_DEFAULT video_mark_debug

static GstStaticPadTemplate gst_video_mark_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV
        ("{ I420, YV12, Y41B, Y42B, Y444, YUY2, UYVY, AYUV, YVYU }"))
    );

static GstStaticPadTemplate gst_video_mark_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV
        ("{ I420, YV12, Y41B, Y42B, Y444, YUY2, UYVY, AYUV, YVYU }"))
    );

static GstVideoFilterClass *parent_class = NULL;

static gboolean
gst_video_mark_set_caps (GstBaseTransform * btrans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstVideoMark *vf;
  GstStructure *in_s;
  guint32 fourcc;
  gboolean ret;

  vf = GST_VIDEO_MARK (btrans);

  in_s = gst_caps_get_structure (incaps, 0);

  ret = gst_structure_get_int (in_s, "width", &vf->width);
  ret &= gst_structure_get_int (in_s, "height", &vf->height);
  ret &= gst_structure_get_fourcc (in_s, "format", &fourcc);

  if (ret)
    vf->format = gst_video_format_from_fourcc (fourcc);

  return ret;
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
gst_video_mark_yuv (GstVideoMark * videomark, GstBuffer * buffer)
{
  GstVideoFormat format;
  gint i, pw, ph, row_stride, pixel_stride, offset;
  gint width, height, req_width, req_height;
  guint8 *d, *data;
  guint64 pattern_shift;
  guint8 color;

  data = GST_BUFFER_DATA (buffer);

  format = videomark->format;
  width = videomark->width;
  height = videomark->height;

  pw = videomark->pattern_width;
  ph = videomark->pattern_height;
  row_stride = gst_video_format_get_row_stride (format, 0, width);
  pixel_stride = gst_video_format_get_pixel_stride (format, 0);
  offset = gst_video_format_get_component_offset (format, 0, width, height);

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
    d = data + offset;
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
    d = data + offset;
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
gst_video_mark_transform_ip (GstBaseTransform * trans, GstBuffer * buf)
{
  GstVideoMark *videomark;
  GstFlowReturn ret = GST_FLOW_OK;

  videomark = GST_VIDEO_MARK (trans);

  if (videomark->enabled)
    return gst_video_mark_yuv (videomark, buf);

  return ret;
}

static void
gst_video_mark_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVideoMark *videomark;

  videomark = GST_VIDEO_MARK (object);

  switch (prop_id) {
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
    case PROP_PATTERN_DATA_64:
      videomark->pattern_data = g_value_get_uint64 (value);
      break;
    case PROP_PATTERN_DATA:
      videomark->pattern_data = g_value_get_int (value);
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
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_video_mark_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstVideoMark *videomark;

  videomark = GST_VIDEO_MARK (object);

  switch (prop_id) {
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
    case PROP_PATTERN_DATA_64:
      g_value_set_uint64 (value, videomark->pattern_data);
      break;
    case PROP_PATTERN_DATA:
      g_value_set_int (value, MIN (videomark->pattern_data, G_MAXINT));
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
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_video_mark_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class, "Video marker",
      "Filter/Effect/Video",
      "Marks a video signal with a pattern", "Wim Taymans <wim@fluendo.com>");

  gst_element_class_add_static_pad_template (element_class,
      &gst_video_mark_sink_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_video_mark_src_template);
}

static void
gst_video_mark_class_init (gpointer klass, gpointer class_data)
{
  GObjectClass *gobject_class;
  GstBaseTransformClass *trans_class;

  gobject_class = (GObjectClass *) klass;
  trans_class = (GstBaseTransformClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_video_mark_set_property;
  gobject_class->get_property = gst_video_mark_get_property;

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
  g_object_class_install_property (gobject_class, PROP_PATTERN_DATA_64,
      g_param_spec_uint64 ("pattern-data-uint64", "Pattern data",
          "The extra data pattern markers", 0, G_MAXUINT64,
          DEFAULT_PATTERN_DATA,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PATTERN_DATA,
      g_param_spec_int ("pattern-data", "Pattern data",
          "The extra data pattern markers", 0, G_MAXINT,
          DEFAULT_PATTERN_DATA, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
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

  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_video_mark_set_caps);
  trans_class->transform_ip = GST_DEBUG_FUNCPTR (gst_video_mark_transform_ip);

  GST_DEBUG_CATEGORY_INIT (video_mark_debug, "videomark", 0, "Video mark");
}

static void
gst_video_mark_init (GTypeInstance * instance, gpointer g_class)
{
  GstVideoMark *videomark;

  videomark = GST_VIDEO_MARK (instance);

  GST_DEBUG_OBJECT (videomark, "gst_video_mark_init");
}

GType
gst_video_mark_get_type (void)
{
  static GType video_mark_type = 0;

  if (!video_mark_type) {
    static const GTypeInfo video_mark_info = {
      sizeof (GstVideoMarkClass),
      gst_video_mark_base_init,
      NULL,
      gst_video_mark_class_init,
      NULL,
      NULL,
      sizeof (GstVideoMark),
      0,
      gst_video_mark_init,
    };

    video_mark_type = g_type_register_static (GST_TYPE_VIDEO_FILTER,
        "GstVideoMark", &video_mark_info, 0);
  }
  return video_mark_type;
}
