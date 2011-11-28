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
 * SECTION:element-videodetect
 * @see_also: #GstVideoMark
 *
 * This plugin detects #GstVideoDetect:pattern-count squares in the bottom left
 * corner of the video frames. The squares have a width and height of
 * respectively #GstVideoDetect:pattern-width and #GstVideoDetect:pattern-height.
 * Even squares must be black and odd squares must be white.
 * 
 * When the pattern has been found, #GstVideoDetect:pattern-data-count squares
 * after the pattern squares are read as a bitarray. White squares represent a 1
 * bit and black squares a 0 bit. The bitarray will will included in the element
 * message that is posted (see below).
 * 
 * After the pattern has been found and the data pattern has been read, an
 * element message called <classname>&quot;GstVideoDetect&quot;</classname> will
 * be posted on the bus. If the pattern is no longer found in the frame, the
 * same element message is posted with the have-pattern field set to #FALSE.
 * The message is only posted if the #GstVideoDetect:message property is #TRUE.
 * 
 * The message's structure contains these fields:
 * <itemizedlist>
 * <listitem>
 *   <para>
 *   #gboolean
 *   <classname>&quot;have-pattern&quot;</classname>:
 *   if the pattern was found. This field will be set to #TRUE for as long as
 *   the pattern was found in the frame and set to FALSE for the first frame
 *   that does not contain the pattern anymore.
 *   </para>
 * </listitem>
 * <listitem>
 *   <para>
 *   #GstClockTime
 *   <classname>&quot;timestamp&quot;</classname>:
 *   the timestamp of the buffer that triggered the message.
 *   </para>
 * </listitem>
 * <listitem>
 *   <para>
 *   #GstClockTime
 *   <classname>&quot;stream-time&quot;</classname>:
 *   the stream time of the buffer.
 *   </para>
 * </listitem>
 * <listitem>
 *   <para>
 *   #GstClockTime
 *   <classname>&quot;running-time&quot;</classname>:
 *   the running_time of the buffer.
 *   </para>
 * </listitem>
 * <listitem>
 *   <para>
 *   #GstClockTime
 *   <classname>&quot;duration&quot;</classname>:
 *   the duration of the buffer.
 *   </para>
 * </listitem>
 * <listitem>
 *   <para>
 *   #guint64
 *   <classname>&quot;data-uint64&quot;</classname>:
 *   the data-pattern found after the pattern or 0 when have-signal is #FALSE.
 *   </para>
 * </listitem>
 * <listitem>
 *   <para>
 *   #guint
 *   <classname>&quot;data&quot;</classname>:
 *   the data-pattern found after the pattern or 0 when have-signal is #FALSE.
 *   </para>
 * </listitem>
 * </itemizedlist>
 * 
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch videotestsrc ! videodetect ! ffmpegcolorspace ! ximagesink
 * ]|
 * </refsect2>
 *
 * Last reviewed on 2007-05-30 (0.10.5)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvideodetect.h"

#include <string.h>
#include <math.h>

#include <gst/video/video.h>

/* GstVideoDetect signals and args */

#define DEFAULT_MESSAGE              TRUE
#define DEFAULT_PATTERN_WIDTH        4
#define DEFAULT_PATTERN_HEIGHT       16
#define DEFAULT_PATTERN_COUNT        4
#define DEFAULT_PATTERN_DATA_COUNT   5
#define DEFAULT_PATTERN_CENTER       0.5
#define DEFAULT_PATTERN_SENSITIVITY  0.3
#define DEFAULT_LEFT_OFFSET          0
#define DEFAULT_BOTTOM_OFFSET        0

enum
{
  PROP_0,
  PROP_MESSAGE,
  PROP_PATTERN_WIDTH,
  PROP_PATTERN_HEIGHT,
  PROP_PATTERN_COUNT,
  PROP_PATTERN_DATA_COUNT,
  PROP_PATTERN_CENTER,
  PROP_PATTERN_SENSITIVITY,
  PROP_LEFT_OFFSET,
  PROP_BOTTOM_OFFSET
};

GST_DEBUG_CATEGORY_STATIC (video_detect_debug);
#define GST_CAT_DEFAULT video_detect_debug

static GstStaticPadTemplate gst_video_detect_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV
        ("{ I420, YV12, Y41B, Y42B, Y444, YUY2, UYVY, AYUV, YVYU }"))
    );

static GstStaticPadTemplate gst_video_detect_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV
        ("{ I420, YV12, Y41B, Y42B, Y444, YUY2, UYVY, AYUV, YVYU }"))
    );

static GstVideoFilterClass *parent_class = NULL;

static gboolean
gst_video_detect_set_caps (GstBaseTransform * btrans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstVideoDetect *vf;
  GstStructure *in_s;
  guint32 fourcc;
  gboolean ret;

  vf = GST_VIDEO_DETECT (btrans);

  in_s = gst_caps_get_structure (incaps, 0);

  ret = gst_structure_get_int (in_s, "width", &vf->width);
  ret &= gst_structure_get_int (in_s, "height", &vf->height);
  ret &= gst_structure_get_fourcc (in_s, "format", &fourcc);

  if (ret)
    vf->format = gst_video_format_from_fourcc (fourcc);

  return ret;
}

static void
gst_video_detect_post_message (GstVideoDetect * videodetect, GstBuffer * buffer,
    guint64 data)
{
  GstBaseTransform *trans;
  GstMessage *m;
  guint64 duration, timestamp, running_time, stream_time;

  trans = GST_BASE_TRANSFORM_CAST (videodetect);

  /* get timestamps */
  timestamp = GST_BUFFER_TIMESTAMP (buffer);
  duration = GST_BUFFER_DURATION (buffer);
  running_time = gst_segment_to_running_time (&trans->segment, GST_FORMAT_TIME,
      timestamp);
  stream_time = gst_segment_to_stream_time (&trans->segment, GST_FORMAT_TIME,
      timestamp);

  /* post message */
  m = gst_message_new_element (GST_OBJECT_CAST (videodetect),
      gst_structure_new ("GstVideoDetect",
          "have-pattern", G_TYPE_BOOLEAN, videodetect->in_pattern,
          "timestamp", G_TYPE_UINT64, timestamp,
          "stream-time", G_TYPE_UINT64, stream_time,
          "running-time", G_TYPE_UINT64, running_time,
          "duration", G_TYPE_UINT64, duration,
          "data-uint64", G_TYPE_UINT64, data,
          "data", G_TYPE_UINT, (guint) MIN (data, G_MAXINT), NULL));
  gst_element_post_message (GST_ELEMENT_CAST (videodetect), m);
}

static gdouble
gst_video_detect_calc_brightness (GstVideoDetect * videodetect, guint8 * data,
    gint width, gint height, gint row_stride, gint pixel_stride)
{
  gint i, j;
  guint64 sum;

  sum = 0;
  for (i = 0; i < height; i++) {
    for (j = 0; j < width; j++) {
      sum += data[pixel_stride * j];
    }
    data += row_stride;
  }
  return sum / (255.0 * width * height);
}

static void
gst_video_detect_yuv (GstVideoDetect * videodetect, GstBuffer * buffer)
{
  GstVideoFormat format;
  gdouble brightness;
  gint i, pw, ph, row_stride, pixel_stride, offset;
  gint width, height, req_width, req_height;
  guint8 *d, *data;
  guint64 pattern_data;

  data = GST_BUFFER_DATA (buffer);

  format = videodetect->format;
  width = videodetect->width;
  height = videodetect->height;

  pw = videodetect->pattern_width;
  ph = videodetect->pattern_height;
  row_stride = gst_video_format_get_row_stride (format, 0, width);
  pixel_stride = gst_video_format_get_pixel_stride (format, 0);
  offset = gst_video_format_get_component_offset (format, 0, width, height);

  req_width =
      (videodetect->pattern_count + videodetect->pattern_data_count) * pw +
      videodetect->left_offset;
  req_height = videodetect->bottom_offset + ph;
  if (req_width > width || req_height > height) {
    goto no_pattern;
  }

  /* analyse the bottom left pixels */
  for (i = 0; i < videodetect->pattern_count; i++) {
    d = data + offset;
    /* move to start of bottom left, adjust for offsets */
    d += row_stride * (height - ph - videodetect->bottom_offset) +
        pixel_stride * videodetect->left_offset;
    /* move to i-th pattern */
    d += pixel_stride * pw * i;

    /* calc brightness of width * height box */
    brightness = gst_video_detect_calc_brightness (videodetect, d, pw, ph,
        row_stride, pixel_stride);

    GST_DEBUG_OBJECT (videodetect, "brightness %f", brightness);

    if (i & 1) {
      /* odd pixels must be white, all pixels darker than the center +
       * sensitivity are considered wrong. */
      if (brightness <
          (videodetect->pattern_center + videodetect->pattern_sensitivity))
        goto no_pattern;
    } else {
      /* even pixels must be black, pixels lighter than the center - sensitivity
       * are considered wrong. */
      if (brightness >
          (videodetect->pattern_center - videodetect->pattern_sensitivity))
        goto no_pattern;
    }
  }
  GST_DEBUG_OBJECT (videodetect, "found pattern");

  pattern_data = 0;

  /* get the data of the pattern */
  for (i = 0; i < videodetect->pattern_data_count; i++) {
    d = data + offset;
    /* move to start of bottom left, adjust for offsets */
    d += row_stride * (height - ph - videodetect->bottom_offset) +
        pixel_stride * videodetect->left_offset;
    /* move after the fixed pattern */
    d += pixel_stride * (videodetect->pattern_count * pw);
    /* move to i-th pattern data */
    d += pixel_stride * pw * i;

    /* calc brightness of width * height box */
    brightness = gst_video_detect_calc_brightness (videodetect, d, pw, ph,
        row_stride, pixel_stride);
    /* update pattern, we just use the center to decide between black and white. */
    pattern_data <<= 1;
    if (brightness > videodetect->pattern_center)
      pattern_data |= 1;
  }

  GST_DEBUG_OBJECT (videodetect, "have data %" G_GUINT64_FORMAT, pattern_data);

  videodetect->in_pattern = TRUE;
  gst_video_detect_post_message (videodetect, buffer, pattern_data);

  return;

no_pattern:
  {
    GST_DEBUG_OBJECT (videodetect, "no pattern found");
    if (videodetect->in_pattern) {
      videodetect->in_pattern = FALSE;
      gst_video_detect_post_message (videodetect, buffer, 0);
    }
    return;
  }
}

static GstFlowReturn
gst_video_detect_transform_ip (GstBaseTransform * trans, GstBuffer * buf)
{
  GstVideoDetect *videodetect;
  GstFlowReturn ret = GST_FLOW_OK;

  videodetect = GST_VIDEO_DETECT (trans);

  gst_video_detect_yuv (videodetect, buf);

  return ret;
}

static void
gst_video_detect_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVideoDetect *videodetect;

  videodetect = GST_VIDEO_DETECT (object);

  switch (prop_id) {
    case PROP_MESSAGE:
      videodetect->message = g_value_get_boolean (value);
      break;
    case PROP_PATTERN_WIDTH:
      videodetect->pattern_width = g_value_get_int (value);
      break;
    case PROP_PATTERN_HEIGHT:
      videodetect->pattern_height = g_value_get_int (value);
      break;
    case PROP_PATTERN_COUNT:
      videodetect->pattern_count = g_value_get_int (value);
      break;
    case PROP_PATTERN_DATA_COUNT:
      videodetect->pattern_data_count = g_value_get_int (value);
      break;
    case PROP_PATTERN_CENTER:
      videodetect->pattern_center = g_value_get_double (value);
      break;
    case PROP_PATTERN_SENSITIVITY:
      videodetect->pattern_sensitivity = g_value_get_double (value);
      break;
    case PROP_LEFT_OFFSET:
      videodetect->left_offset = g_value_get_int (value);
      break;
    case PROP_BOTTOM_OFFSET:
      videodetect->bottom_offset = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_video_detect_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstVideoDetect *videodetect;

  videodetect = GST_VIDEO_DETECT (object);

  switch (prop_id) {
    case PROP_MESSAGE:
      g_value_set_boolean (value, videodetect->message);
      break;
    case PROP_PATTERN_WIDTH:
      g_value_set_int (value, videodetect->pattern_width);
      break;
    case PROP_PATTERN_HEIGHT:
      g_value_set_int (value, videodetect->pattern_height);
      break;
    case PROP_PATTERN_COUNT:
      g_value_set_int (value, videodetect->pattern_count);
      break;
    case PROP_PATTERN_DATA_COUNT:
      g_value_set_int (value, videodetect->pattern_data_count);
      break;
    case PROP_PATTERN_CENTER:
      g_value_set_double (value, videodetect->pattern_center);
      break;
    case PROP_PATTERN_SENSITIVITY:
      g_value_set_double (value, videodetect->pattern_sensitivity);
      break;
    case PROP_LEFT_OFFSET:
      g_value_set_int (value, videodetect->left_offset);
      break;
    case PROP_BOTTOM_OFFSET:
      g_value_set_int (value, videodetect->bottom_offset);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_video_detect_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class, "Video detecter",
      "Filter/Effect/Video",
      "Detect patterns in a video signal", "Wim Taymans <wim@fluendo.com>");

  gst_element_class_add_static_pad_template (element_class,
      &gst_video_detect_sink_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_video_detect_src_template);
}

static void
gst_video_detect_class_init (gpointer klass, gpointer class_data)
{
  GObjectClass *gobject_class;
  GstBaseTransformClass *trans_class;

  gobject_class = (GObjectClass *) klass;
  trans_class = (GstBaseTransformClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_video_detect_set_property;
  gobject_class->get_property = gst_video_detect_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_MESSAGE,
      g_param_spec_boolean ("message", "Message",
          "Post statics messages",
          DEFAULT_MESSAGE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
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
          "The number of extra data pattern markers", 0, G_MAXINT,
          DEFAULT_PATTERN_DATA_COUNT,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PATTERN_CENTER,
      g_param_spec_double ("pattern-center", "Pattern center",
          "The center of the black/white separation (0.0 = lowest, 1.0 highest)",
          0.0, 1.0, DEFAULT_PATTERN_CENTER,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PATTERN_SENSITIVITY,
      g_param_spec_double ("pattern-sensitivity", "Pattern sensitivity",
          "The sensitivity around the center for detecting the markers "
          "(0.0 = lowest, 1.0 highest)", 0.0, 1.0, DEFAULT_PATTERN_SENSITIVITY,
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

  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_video_detect_set_caps);
  trans_class->transform_ip = GST_DEBUG_FUNCPTR (gst_video_detect_transform_ip);
  trans_class->passthrough_on_same_caps = TRUE;

  GST_DEBUG_CATEGORY_INIT (video_detect_debug, "videodetect", 0,
      "Video detect");
}

static void
gst_video_detect_init (GTypeInstance * instance, gpointer g_class)
{
  GstVideoDetect *videodetect;

  videodetect = GST_VIDEO_DETECT (instance);

  GST_DEBUG_OBJECT (videodetect, "gst_video_detect_init");

  videodetect->in_pattern = FALSE;
}

GType
gst_video_detect_get_type (void)
{
  static GType video_detect_type = 0;

  if (!video_detect_type) {
    static const GTypeInfo video_detect_info = {
      sizeof (GstVideoDetectClass),
      gst_video_detect_base_init,
      NULL,
      gst_video_detect_class_init,
      NULL,
      NULL,
      sizeof (GstVideoDetect),
      0,
      gst_video_detect_init,
    };

    video_detect_type = g_type_register_static (GST_TYPE_VIDEO_FILTER,
        "GstVideoDetect", &video_detect_info, 0);
  }
  return video_detect_type;
}
