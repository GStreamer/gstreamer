/*
 * GStreamer
 * Copyright (C) 2016 Vivia Nikolaidou <vivia@toolsonair.com>
 *
 * gsttimecodestamper.c
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

/**
 * SECTION:element-timecodestamper
 * @title: timecodestamper
 * @short_description: Attach a timecode into incoming video frames
 *
 * This element attaches a timecode into every incoming video frame. It starts
 * counting from the stream time of each segment start, which it converts into
 * a timecode.
 *
 * ## Example launch line
 * |[
 * gst-launch-1.0 videotestsrc ! timecodestamper ! autovideosink
 * ]|
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gsttimecodestamper.h"

#include <gst/gst.h>
#include <gst/video/video.h>
#include <stdlib.h>
#include <string.h>

GST_DEBUG_CATEGORY_STATIC (timecodestamper_debug);
#define GST_CAT_DEFAULT timecodestamper_debug

/* GstTimeCodeStamper properties */
enum
{
  PROP_0,
  PROP_OVERRIDE_EXISTING,
  PROP_DROP_FRAME,
  PROP_DAILY_JAM,
  PROP_POST_MESSAGES,
  PROP_FIRST_TIMECODE,
  PROP_FIRST_NOW
};

#define DEFAULT_OVERRIDE_EXISTING FALSE
#define DEFAULT_DROP_FRAME FALSE
#define DEFAULT_DAILY_JAM NULL
#define DEFAULT_POST_MESSAGES FALSE
#define DEFAULT_FIRST_NOW FALSE

static GstStaticPadTemplate gst_timecodestamper_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw")
    );

static GstStaticPadTemplate gst_timecodestamper_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw")
    );

static void gst_timecodestamper_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_timecodestamper_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_timecodestamper_dispose (GObject * object);
static gboolean gst_timecodestamper_sink_event (GstBaseTransform * trans,
    GstEvent * event);
static GstFlowReturn gst_timecodestamper_transform_ip (GstBaseTransform *
    vfilter, GstBuffer * buffer);
static gboolean gst_timecodestamper_stop (GstBaseTransform * trans);

G_DEFINE_TYPE (GstTimeCodeStamper, gst_timecodestamper,
    GST_TYPE_BASE_TRANSFORM);

static void
gst_timecodestamper_class_init (GstTimeCodeStamperClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *element_class = (GstElementClass *) klass;
  GstBaseTransformClass *trans_class = (GstBaseTransformClass *) klass;

  GST_DEBUG_CATEGORY_INIT (timecodestamper_debug, "timecodestamper", 0,
      "timecodestamper");
  gst_element_class_set_static_metadata (element_class, "Timecode stamper",
      "Filter/Video", "Attaches a timecode meta into each video frame",
      "Vivia Nikolaidou <vivia@toolsonair.com");

  gobject_class->set_property = gst_timecodestamper_set_property;
  gobject_class->get_property = gst_timecodestamper_get_property;
  gobject_class->dispose = gst_timecodestamper_dispose;

  g_object_class_install_property (gobject_class, PROP_OVERRIDE_EXISTING,
      g_param_spec_boolean ("override-existing", "Override existing timecode",
          "If set to true, any existing timecode will be overridden",
          DEFAULT_OVERRIDE_EXISTING,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_DROP_FRAME,
      g_param_spec_boolean ("drop-frame", "Override existing timecode",
          "Use drop-frame timecodes for 29.97 and 59.94 FPS",
          DEFAULT_DROP_FRAME, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_DAILY_JAM,
      g_param_spec_boxed ("daily-jam",
          "Daily jam",
          "The daily jam of the timecode",
          G_TYPE_DATE_TIME, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_POST_MESSAGES,
      g_param_spec_boolean ("post-messages", "Post element message",
          "Post element message containing the current timecode",
          DEFAULT_POST_MESSAGES, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_FIRST_TIMECODE,
      g_param_spec_boxed ("first-timecode",
          "Timecode at the first frame",
          "If set, take this timecode for the first frame and increment from "
          "it. Only the values itself are taken, flags and frame rate are "
          "always determined by timecodestamper itself. "
          "If unset (and to-now is also not set), the timecode will start at 0",
          GST_TYPE_VIDEO_TIME_CODE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_FIRST_NOW,
      g_param_spec_boolean ("first-timecode-to-now",
          "Sets first timecode to system time",
          "If true and first-timecode is unset, set it to system time "
          "automatically when the first media segment is received.",
          DEFAULT_FIRST_NOW, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_timecodestamper_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_timecodestamper_src_template));

  trans_class->sink_event = GST_DEBUG_FUNCPTR (gst_timecodestamper_sink_event);
  trans_class->stop = GST_DEBUG_FUNCPTR (gst_timecodestamper_stop);

  trans_class->transform_ip =
      GST_DEBUG_FUNCPTR (gst_timecodestamper_transform_ip);
}

static void
gst_timecodestamper_init (GstTimeCodeStamper * timecodestamper)
{
  timecodestamper->override_existing = DEFAULT_OVERRIDE_EXISTING;
  timecodestamper->drop_frame = DEFAULT_DROP_FRAME;
  timecodestamper->current_tc = gst_video_time_code_new_empty ();
  timecodestamper->first_tc = NULL;
  timecodestamper->current_tc->config.latest_daily_jam = DEFAULT_DAILY_JAM;
  timecodestamper->post_messages = DEFAULT_POST_MESSAGES;
  timecodestamper->first_tc_now = DEFAULT_FIRST_NOW;
}

static void
gst_timecodestamper_dispose (GObject * object)
{
  GstTimeCodeStamper *timecodestamper = GST_TIME_CODE_STAMPER (object);

  if (timecodestamper->current_tc != NULL) {
    gst_video_time_code_free (timecodestamper->current_tc);
    timecodestamper->current_tc = NULL;
  }

  if (timecodestamper->first_tc != NULL) {
    gst_video_time_code_free (timecodestamper->first_tc);
    timecodestamper->first_tc = NULL;
  }

  G_OBJECT_CLASS (gst_timecodestamper_parent_class)->dispose (object);
}

static void
gst_timecodestamper_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstTimeCodeStamper *timecodestamper = GST_TIME_CODE_STAMPER (object);

  switch (prop_id) {
    case PROP_OVERRIDE_EXISTING:
      timecodestamper->override_existing = g_value_get_boolean (value);
      break;
    case PROP_DROP_FRAME:
      timecodestamper->drop_frame = g_value_get_boolean (value);
      break;
    case PROP_DAILY_JAM:
      if (timecodestamper->current_tc->config.latest_daily_jam)
        g_date_time_unref (timecodestamper->current_tc->
            config.latest_daily_jam);
      timecodestamper->current_tc->config.latest_daily_jam =
          g_value_dup_boxed (value);
      break;
    case PROP_POST_MESSAGES:
      timecodestamper->post_messages = g_value_get_boolean (value);
      break;
    case PROP_FIRST_TIMECODE:
      if (timecodestamper->first_tc)
        gst_video_time_code_free (timecodestamper->first_tc);
      timecodestamper->first_tc = g_value_dup_boxed (value);
      break;
    case PROP_FIRST_NOW:
      timecodestamper->first_tc_now = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_timecodestamper_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstTimeCodeStamper *timecodestamper = GST_TIME_CODE_STAMPER (object);

  switch (prop_id) {
    case PROP_OVERRIDE_EXISTING:
      g_value_set_boolean (value, timecodestamper->override_existing);
      break;
    case PROP_DROP_FRAME:
      g_value_set_boolean (value, timecodestamper->drop_frame);
      break;
    case PROP_DAILY_JAM:
      g_value_set_boxed (value,
          timecodestamper->current_tc->config.latest_daily_jam);
      break;
    case PROP_POST_MESSAGES:
      g_value_set_boolean (value, timecodestamper->post_messages);
      break;
    case PROP_FIRST_TIMECODE:
      g_value_set_boxed (value, timecodestamper->first_tc);
      break;
    case PROP_FIRST_NOW:
      g_value_set_boolean (value, timecodestamper->first_tc_now);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_timecodestamper_set_drop_frame (GstTimeCodeStamper * timecodestamper)
{
  if (timecodestamper->drop_frame && timecodestamper->vinfo.fps_d == 1001 &&
      (timecodestamper->vinfo.fps_n == 30000 ||
          timecodestamper->vinfo.fps_d == 60000))
    timecodestamper->current_tc->config.flags |=
        GST_VIDEO_TIME_CODE_FLAGS_DROP_FRAME;
  else
    timecodestamper->current_tc->config.flags &=
        ~GST_VIDEO_TIME_CODE_FLAGS_DROP_FRAME;
}

static gboolean
gst_timecodestamper_stop (GstBaseTransform * trans)
{
  GstTimeCodeStamper *timecodestamper = GST_TIME_CODE_STAMPER (trans);

  gst_video_info_init (&timecodestamper->vinfo);

  return TRUE;
}

/* Must be called with object lock */
static void
gst_timecodestamper_reset_timecode (GstTimeCodeStamper * timecodestamper)
{
  GDateTime *jam = NULL;

  if (timecodestamper->first_tc &&
      timecodestamper->first_tc->config.latest_daily_jam)
    jam = g_date_time_ref (timecodestamper->first_tc->config.latest_daily_jam);
  else if (timecodestamper->current_tc->config.latest_daily_jam)
    jam =
        g_date_time_ref (timecodestamper->current_tc->config.latest_daily_jam);
  gst_video_time_code_clear (timecodestamper->current_tc);
  /* FIXME: What if the buffer doesn't contain both top and bottom fields? */
  gst_video_time_code_init (timecodestamper->current_tc,
      timecodestamper->vinfo.fps_n,
      timecodestamper->vinfo.fps_d,
      jam,
      timecodestamper->vinfo.interlace_mode ==
      GST_VIDEO_INTERLACE_MODE_PROGRESSIVE ? 0 :
      GST_VIDEO_TIME_CODE_FLAGS_INTERLACED, 0, 0, 0, 0, 0);
  if (jam)
    g_date_time_unref (jam);
  if (timecodestamper->first_tc) {
    timecodestamper->current_tc->hours = timecodestamper->first_tc->hours;
    timecodestamper->current_tc->minutes = timecodestamper->first_tc->minutes;
    timecodestamper->current_tc->seconds = timecodestamper->first_tc->seconds;
    timecodestamper->current_tc->frames = timecodestamper->first_tc->frames;
    timecodestamper->current_tc->field_count =
        timecodestamper->first_tc->field_count;
  }
  gst_timecodestamper_set_drop_frame (timecodestamper);
}

static gboolean
gst_timecodestamper_sink_event (GstBaseTransform * trans, GstEvent * event)
{
  gboolean ret = FALSE;
  GstTimeCodeStamper *timecodestamper = GST_TIME_CODE_STAMPER (trans);

  GST_DEBUG_OBJECT (trans, "received event %" GST_PTR_FORMAT, event);
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEGMENT:
    {
      GstSegment segment;
      guint64 frames;
      gchar *tc_str;
      gboolean notify = FALSE;

      GST_OBJECT_LOCK (timecodestamper);

      gst_event_copy_segment (event, &segment);
      if (segment.format != GST_FORMAT_TIME) {
        GST_OBJECT_UNLOCK (timecodestamper);
        GST_ERROR_OBJECT (timecodestamper, "Invalid segment format");
        return FALSE;
      }
      if (GST_VIDEO_INFO_FORMAT (&timecodestamper->vinfo) ==
          GST_VIDEO_FORMAT_UNKNOWN) {
        GST_ERROR_OBJECT (timecodestamper,
            "Received segment event without caps");
        GST_OBJECT_UNLOCK (timecodestamper);
        return FALSE;
      }

      if (timecodestamper->first_tc_now && !timecodestamper->first_tc) {
        GDateTime *dt = g_date_time_new_now_local ();
        GstVideoTimeCode *tc;

        gst_timecodestamper_set_drop_frame (timecodestamper);

        tc = gst_video_time_code_new_from_date_time (timecodestamper->
            vinfo.fps_n, timecodestamper->vinfo.fps_d, dt,
            timecodestamper->current_tc->config.flags, 0);

        g_date_time_unref (dt);

        timecodestamper->first_tc = tc;
        notify = TRUE;
      }

      frames =
          gst_util_uint64_scale (segment.time, timecodestamper->vinfo.fps_n,
          timecodestamper->vinfo.fps_d * GST_SECOND);
      gst_timecodestamper_reset_timecode (timecodestamper);
      gst_video_time_code_add_frames (timecodestamper->current_tc, frames);
      GST_DEBUG_OBJECT (timecodestamper,
          "Got %" G_GUINT64_FORMAT " frames when segment time is %"
          GST_TIME_FORMAT, frames, GST_TIME_ARGS (segment.time));
      tc_str = gst_video_time_code_to_string (timecodestamper->current_tc);
      GST_DEBUG_OBJECT (timecodestamper, "New timecode is %s", tc_str);
      g_free (tc_str);
      GST_OBJECT_UNLOCK (timecodestamper);
      if (notify)
        g_object_notify (G_OBJECT (timecodestamper), "first-timecode");
      break;
    }
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      GST_OBJECT_LOCK (timecodestamper);
      gst_event_parse_caps (event, &caps);
      if (!gst_video_info_from_caps (&timecodestamper->vinfo, caps)) {
        GST_OBJECT_UNLOCK (timecodestamper);
        return FALSE;
      }
      gst_timecodestamper_reset_timecode (timecodestamper);
      GST_OBJECT_UNLOCK (timecodestamper);
      break;
    }
    default:
      break;
  }
  ret =
      GST_BASE_TRANSFORM_CLASS (gst_timecodestamper_parent_class)->sink_event
      (trans, event);
  return ret;
}

static gboolean
remove_timecode_meta (GstBuffer * buffer, GstMeta ** meta, gpointer user_data)
{
  if (meta && *meta && (*meta)->info->api == GST_VIDEO_TIME_CODE_META_API_TYPE) {
    *meta = NULL;
  }

  return TRUE;
}

static GstFlowReturn
gst_timecodestamper_transform_ip (GstBaseTransform * vfilter,
    GstBuffer * buffer)
{
  GstTimeCodeStamper *timecodestamper = GST_TIME_CODE_STAMPER (vfilter);
  GstVideoTimeCodeMeta *tc_meta;
  GstVideoTimeCode *tc;

  GST_OBJECT_LOCK (timecodestamper);
  tc_meta = gst_buffer_get_video_time_code_meta (buffer);
  if (tc_meta && !timecodestamper->override_existing) {
    GST_OBJECT_UNLOCK (timecodestamper);
    tc = gst_video_time_code_copy (&tc_meta->tc);
    goto beach;
  } else if (timecodestamper->override_existing) {
    gst_buffer_foreach_meta (buffer, remove_timecode_meta, NULL);
  }

  gst_buffer_add_video_time_code_meta (buffer, timecodestamper->current_tc);
  tc = gst_video_time_code_copy (timecodestamper->current_tc);
  gst_video_time_code_increment_frame (timecodestamper->current_tc);
  GST_OBJECT_UNLOCK (timecodestamper);

beach:
  if (timecodestamper->post_messages) {
    GstClockTime stream_time, running_time, duration;
    GstStructure *s;
    GstMessage *msg;

    running_time =
        gst_segment_to_running_time (&vfilter->segment, GST_FORMAT_TIME,
        GST_BUFFER_PTS (buffer));
    stream_time =
        gst_segment_to_stream_time (&vfilter->segment, GST_FORMAT_TIME,
        GST_BUFFER_PTS (buffer));
    duration =
        gst_util_uint64_scale_int (GST_SECOND, timecodestamper->vinfo.fps_d,
        timecodestamper->vinfo.fps_n);
    s = gst_structure_new ("timecodestamper", "timestamp", G_TYPE_UINT64,
        GST_BUFFER_PTS (buffer), "stream-time", G_TYPE_UINT64, stream_time,
        "running-time", G_TYPE_UINT64, running_time, "duration", G_TYPE_UINT64,
        duration, "timecode", GST_TYPE_VIDEO_TIME_CODE, tc, NULL);
    msg = gst_message_new_element (GST_OBJECT (timecodestamper), s);
    gst_element_post_message (GST_ELEMENT (timecodestamper), msg);
  }
  gst_video_time_code_free (tc);
  return GST_FLOW_OK;
}
