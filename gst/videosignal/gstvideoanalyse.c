/* GStreamer
 * Copyright (C) <2006> Wim Taymans <wim@fluendo.com>
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
 * SECTION:element-videoanalyse
 *
 * This plugin analyses every video frame and if the #GstVideoAnalyse:message
 * property is #TRUE, posts an element message with video statistics called
 * <classname>&quot;GstVideoAnalyse&quot;</classname>.
 *
 * The message's structure contains these fields:
 * <itemizedlist>
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
 *   #gdouble
 *   <classname>&quot;brightness&quot;</classname>:
 *   the average brightness of the frame.
 *   </para>
 * </listitem>
 * <listitem>
 *   <para>
 *   #gdouble
 *   <classname>&quot;brightness-variance&quot;</classname>:
 *   the brightness variance of the frame.
 *   </para>
 * </listitem>
 * </itemizedlist>
 * 
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -m videotestsrc ! videoanalyse ! ffmpegcolorspace ! ximagesink
 * ]| This pipeline emits messages to the console for each frame that has been analysed. 
 * </refsect2>
 *
 * Last reviewed on 2007-05-30 (0.10.5)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvideoanalyse.h"

#include <string.h>
#include <math.h>

#include <gst/video/video.h>

/* GstVideoAnalyse signals and args */

#define DEFAULT_MESSAGE		TRUE

enum
{
  PROP_0,
  PROP_MESSAGE
};

GST_DEBUG_CATEGORY_STATIC (video_analyse_debug);
#define GST_CAT_DEFAULT video_analyse_debug

static GstStaticPadTemplate gst_video_analyse_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("{ I420, YV12 }"))
    );

static GstStaticPadTemplate gst_video_analyse_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("{ I420, YV12 }"))
    );

static GstVideoFilterClass *parent_class = NULL;

static gboolean
gst_video_analyse_set_caps (GstBaseTransform * btrans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstVideoAnalyse *vf;
  GstStructure *in_s;
  gboolean ret;

  vf = GST_VIDEO_ANALYSE (btrans);

  in_s = gst_caps_get_structure (incaps, 0);

  ret = gst_structure_get_int (in_s, "width", &vf->width);
  ret &= gst_structure_get_int (in_s, "height", &vf->height);

  return ret;
}

/* Useful macros */
#define GST_VIDEO_I420_Y_ROWSTRIDE(width) (GST_ROUND_UP_4(width))
#define GST_VIDEO_I420_U_ROWSTRIDE(width) (GST_ROUND_UP_8(width)/2)
#define GST_VIDEO_I420_V_ROWSTRIDE(width) ((GST_ROUND_UP_8(GST_VIDEO_I420_Y_ROWSTRIDE(width)))/2)

#define GST_VIDEO_I420_Y_OFFSET(w,h) (0)
#define GST_VIDEO_I420_U_OFFSET(w,h) (GST_VIDEO_I420_Y_OFFSET(w,h)+(GST_VIDEO_I420_Y_ROWSTRIDE(w)*GST_ROUND_UP_2(h)))
#define GST_VIDEO_I420_V_OFFSET(w,h) (GST_VIDEO_I420_U_OFFSET(w,h)+(GST_VIDEO_I420_U_ROWSTRIDE(w)*GST_ROUND_UP_2(h)/2))

#define GST_VIDEO_I420_SIZE(w,h)     (GST_VIDEO_I420_V_OFFSET(w,h)+(GST_VIDEO_I420_V_ROWSTRIDE(w)*GST_ROUND_UP_2(h)/2))

static void
gst_video_analyse_post_message (GstVideoAnalyse * videoanalyse,
    GstBuffer * buffer)
{
  GstBaseTransform *trans;
  GstMessage *m;
  guint64 duration, timestamp, running_time, stream_time;

  trans = GST_BASE_TRANSFORM_CAST (videoanalyse);

  /* get timestamps */
  timestamp = GST_BUFFER_TIMESTAMP (buffer);
  duration = GST_BUFFER_DURATION (buffer);
  running_time = gst_segment_to_running_time (&trans->segment, GST_FORMAT_TIME,
      timestamp);
  stream_time = gst_segment_to_stream_time (&trans->segment, GST_FORMAT_TIME,
      timestamp);

  m = gst_message_new_element (GST_OBJECT_CAST (videoanalyse),
      gst_structure_new ("GstVideoAnalyse",
          "timestamp", G_TYPE_UINT64, timestamp,
          "stream-time", G_TYPE_UINT64, stream_time,
          "running-time", G_TYPE_UINT64, running_time,
          "duration", G_TYPE_UINT64, duration,
          "brightness", G_TYPE_DOUBLE, videoanalyse->brightness,
          "brightness-variance", G_TYPE_DOUBLE, videoanalyse->brightness_var,
          NULL));

  gst_element_post_message (GST_ELEMENT_CAST (videoanalyse), m);
}

static void
gst_video_analyse_420 (GstVideoAnalyse * videoanalyse, guint8 * data,
    gint width, gint height)
{
  guint64 sum;
  gint avg, diff;
  gint i, j;
  guint8 *d;

  d = data;
  sum = 0;
  /* do brightness as average of pixel brightness in 0.0 to 1.0 */
  for (i = 0; i < height; i++) {
    for (j = 0; j < width; j++) {
      sum += d[j];
    }
    d += GST_VIDEO_I420_Y_ROWSTRIDE (width);
  }
  avg = sum / (width * height);
  videoanalyse->brightness = sum / (255.0 * width * height);

  d = data;
  sum = 0;
  /* do variance */
  for (i = 0; i < height; i++) {
    for (j = 0; j < width; j++) {
      diff = (avg - d[j]);
      sum += diff * diff;
    }
    d += GST_VIDEO_I420_Y_ROWSTRIDE (width);
  }
  videoanalyse->brightness_var = sum / (255.0 * 255.0 * width * height);
}

static GstFlowReturn
gst_video_analyse_transform_ip (GstBaseTransform * trans, GstBuffer * buf)
{
  GstVideoAnalyse *videoanalyse;
  GstFlowReturn ret = GST_FLOW_OK;
  guint8 *data;

  videoanalyse = GST_VIDEO_ANALYSE (trans);

  data = GST_BUFFER_DATA (buf);

  gst_video_analyse_420 (videoanalyse, data, videoanalyse->width,
      videoanalyse->height);

  if (videoanalyse->message)
    gst_video_analyse_post_message (videoanalyse, buf);

  return ret;
}

static void
gst_video_analyse_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVideoAnalyse *videoanalyse;

  videoanalyse = GST_VIDEO_ANALYSE (object);

  switch (prop_id) {
    case PROP_MESSAGE:
      videoanalyse->message = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_video_analyse_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstVideoAnalyse *videoanalyse;

  videoanalyse = GST_VIDEO_ANALYSE (object);

  switch (prop_id) {
    case PROP_MESSAGE:
      g_value_set_boolean (value, videoanalyse->message);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_video_analyse_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class, "Video analyser",
      "Filter/Analyzer/Video",
      "Analyse video signal", "Wim Taymans <wim@fluendo.com>");

  gst_element_class_add_static_pad_template (element_class,
      &gst_video_analyse_sink_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_video_analyse_src_template);
}

static void
gst_video_analyse_class_init (gpointer klass, gpointer class_data)
{
  GObjectClass *gobject_class;
  GstBaseTransformClass *trans_class;

  gobject_class = (GObjectClass *) klass;
  trans_class = (GstBaseTransformClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_video_analyse_set_property;
  gobject_class->get_property = gst_video_analyse_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_MESSAGE,
      g_param_spec_boolean ("message", "Message",
          "Post statics messages",
          DEFAULT_MESSAGE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_video_analyse_set_caps);
  trans_class->transform_ip =
      GST_DEBUG_FUNCPTR (gst_video_analyse_transform_ip);
  trans_class->passthrough_on_same_caps = TRUE;
}

static void
gst_video_analyse_init (GTypeInstance * instance, gpointer g_class)
{
  GstVideoAnalyse *videoanalyse;

  videoanalyse = GST_VIDEO_ANALYSE (instance);

  GST_DEBUG_OBJECT (videoanalyse, "gst_video_analyse_init");
}

GType
gst_video_analyse_get_type (void)
{
  static GType video_analyse_type = 0;

  if (!video_analyse_type) {
    static const GTypeInfo video_analyse_info = {
      sizeof (GstVideoAnalyseClass),
      gst_video_analyse_base_init,
      NULL,
      gst_video_analyse_class_init,
      NULL,
      NULL,
      sizeof (GstVideoAnalyse),
      0,
      gst_video_analyse_init,
    };

    video_analyse_type = g_type_register_static (GST_TYPE_VIDEO_FILTER,
        "GstVideoAnalyse", &video_analyse_info, 0);

    GST_DEBUG_CATEGORY_INIT (video_analyse_debug, "videoanalyse", 0,
        "Video Analyse element");
  }
  return video_analyse_type;
}
