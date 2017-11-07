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
#include <gst/audio/audio.h>
#include <stdlib.h>
#include <string.h>

#define ABSDIFF(a,b) (((a) > (b)) ? ((a) - (b)) : ((b) - (a)))

GST_DEBUG_CATEGORY_STATIC (timecodestamper_debug);
#define GST_CAT_DEFAULT timecodestamper_debug

/* GstTimeCodeStamper properties */
enum
{
  PROP_0,
  PROP_SOURCE,
  PROP_DROP_FRAME,
  PROP_DAILY_JAM,
  PROP_POST_MESSAGES,
  PROP_FIRST_TIMECODE,
  PROP_FIRST_NOW,
  PROP_LTC_MAX_OFFSET,
  PROP_TC_ADD
};

#define DEFAULT_OVERRIDE_EXISTING FALSE
#define DEFAULT_DROP_FRAME FALSE
#define DEFAULT_DAILY_JAM NULL
#define DEFAULT_POST_MESSAGES FALSE
#define DEFAULT_FIRST_NOW FALSE
#define DEFAULT_LTC_QUEUE 100
#define DEFAULT_LTC_MAX_OFFSET 250000000

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

static GstStaticPadTemplate gst_timecodestamper_ltc_template =
GST_STATIC_PAD_TEMPLATE ("ltc_sink",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("audio/x-raw,format=U8,rate=[1,max],channels=1")
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
static gboolean gst_timecodestamper_start (GstBaseTransform * trans);
static GstPad *gst_timecodestamper_request_new_pad (GstElement * element,
    GstPadTemplate * temp, const gchar * unused, const GstCaps * caps);
static void gst_timecodestamper_release_pad (GstElement * element,
    GstPad * pad);

static GstFlowReturn gst_timecodestamper_ltcpad_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buffer);
static gboolean gst_timecodestamper_ltcpad_event (GstPad * pad,
    GstObject * parent, GstEvent * event);
static gboolean gst_timecodestamper_ltcpad_query (GstPad * pad,
    GstObject * parent, GstQuery * query);
static gboolean gst_timecodestamper_pad_activatemode (GstPad * pad,
    GstObject * parent, GstPadMode mode, gboolean active);

G_DEFINE_TYPE (GstTimeCodeStamper, gst_timecodestamper,
    GST_TYPE_BASE_TRANSFORM);

GType
gst_timecodestamper_source_get_type (void)
{
  static GType gst_timecodestamper_source_type = 0;
  static const GEnumValue gst_timecodestamper_source[] = {
    {GST_TIME_CODE_STAMPER_NOREPLACE,
        "Internal counter if there is no timecode, "
          "otherwise keep existing one", "noreplace"},
    {GST_TIME_CODE_STAMPER_INTERN,
        "Always timecodestamper's internal counter", "intern"},
    {GST_TIME_CODE_STAMPER_EXISTING,
        "Only existing timecode, frames without timecode "
          "stay without timecode", "existing"},
    {GST_TIME_CODE_STAMPER_LTC,
        "Linear time code from an audio device", "ltc"},
    {GST_TIME_CODE_STAMPER_NRZERO,
        "Zero if there is no timecode, "
          "otherwise keep existing one", "noreplace-zero"},
    {0, NULL, NULL},
  };

  if (!gst_timecodestamper_source_type) {
    gst_timecodestamper_source_type =
        g_enum_register_static ("GstTimeCodeStamperSource",
        gst_timecodestamper_source);
  }
  return gst_timecodestamper_source_type;
}

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
      "Vivia Nikolaidou <vivia@toolsonair.com>");

  gobject_class->set_property = gst_timecodestamper_set_property;
  gobject_class->get_property = gst_timecodestamper_get_property;
  gobject_class->dispose = gst_timecodestamper_dispose;

  g_object_class_install_property (gobject_class, PROP_SOURCE,
      g_param_spec_enum ("timecode-source", "Timecode to use",
          "Choose from what source the timecode should be taken",
          GST_TYPE_TIME_CODE_STAMPER_SOURCE,
          GST_TIME_CODE_STAMPER_INTERN,
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
  g_object_class_install_property (gobject_class, PROP_LTC_MAX_OFFSET,
      g_param_spec_uint64 ("ltc-max-offset",
          "Maximum offset of LTC to video, in nanoseconds",
          "Maximum number of nanoseconds the LTC audio may be ahead "
          "or behind the video. Buffers not in this range are ignored.",
          0, G_MAXUINT64, DEFAULT_LTC_MAX_OFFSET,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_TC_ADD,
      g_param_spec_int ("tc-add",
          "Add this number of frames to LTC or internal timecode.",
          "Add this number of frames to LTC or internal timecode, "
          "useful if there is an offset between your LTC source and video.",
          G_MININT, G_MAXINT, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_timecodestamper_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_timecodestamper_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_timecodestamper_ltc_template));

  element_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_timecodestamper_request_new_pad);
  element_class->release_pad =
      GST_DEBUG_FUNCPTR (gst_timecodestamper_release_pad);

  trans_class->sink_event = GST_DEBUG_FUNCPTR (gst_timecodestamper_sink_event);
  trans_class->stop = GST_DEBUG_FUNCPTR (gst_timecodestamper_stop);
  trans_class->start = GST_DEBUG_FUNCPTR (gst_timecodestamper_start);

  trans_class->transform_ip =
      GST_DEBUG_FUNCPTR (gst_timecodestamper_transform_ip);
}

static void
gst_timecodestamper_init (GstTimeCodeStamper * timecodestamper)
{
  timecodestamper->tc_source = GST_TIME_CODE_STAMPER_INTERN;
  timecodestamper->drop_frame = DEFAULT_DROP_FRAME;
  timecodestamper->current_tc = gst_video_time_code_new_empty ();
  timecodestamper->ltc_current_tc = gst_video_time_code_new_empty ();
  timecodestamper->first_tc = NULL;
  timecodestamper->current_tc->config.latest_daily_jam = DEFAULT_DAILY_JAM;
  timecodestamper->ltc_current_tc->config.latest_daily_jam = DEFAULT_DAILY_JAM;
  timecodestamper->ltc_intern_tc = NULL;
  timecodestamper->post_messages = DEFAULT_POST_MESSAGES;
  timecodestamper->first_tc_now = DEFAULT_FIRST_NOW;
  timecodestamper->is_flushing = FALSE;
  timecodestamper->no_wait = FALSE;
  timecodestamper->ltcpad = NULL;
  g_mutex_init (&timecodestamper->mutex);
#if HAVE_LTC
  timecodestamper->ltc_dec = NULL;
  timecodestamper->ltc_max_offset = DEFAULT_LTC_MAX_OFFSET;
  timecodestamper->tc_add = 0;
  timecodestamper->ltc_first_runtime = 0;
  timecodestamper->ltc_audio_endtime = 0;

  g_cond_init (&timecodestamper->ltc_cond_video);
  g_cond_init (&timecodestamper->ltc_cond_audio);
#endif

  gst_pad_set_activatemode_function (GST_BASE_TRANSFORM_SINK_PAD
      (timecodestamper),
      GST_DEBUG_FUNCPTR (gst_timecodestamper_pad_activatemode));

}

static void
gst_timecodestamper_dispose (GObject * object)
{
  GstTimeCodeStamper *timecodestamper = GST_TIME_CODE_STAMPER (object);

#if HAVE_LTC
  g_cond_clear (&timecodestamper->ltc_cond_video);
  g_cond_clear (&timecodestamper->ltc_cond_audio);
#endif
  g_mutex_clear (&timecodestamper->mutex);

  if (timecodestamper->current_tc != NULL) {
    gst_video_time_code_free (timecodestamper->current_tc);
    timecodestamper->current_tc = NULL;
  }

  if (timecodestamper->ltc_current_tc != NULL) {
    gst_video_time_code_free (timecodestamper->ltc_current_tc);
    timecodestamper->ltc_current_tc = NULL;
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
    case PROP_SOURCE:
      timecodestamper->tc_source = (GstTimeCodeStamperSource)
          g_value_get_enum (value);
      break;
    case PROP_DROP_FRAME:
      timecodestamper->drop_frame = g_value_get_boolean (value);
      break;
    case PROP_DAILY_JAM:
      if (timecodestamper->current_tc->config.latest_daily_jam)
        g_date_time_unref (timecodestamper->current_tc->config.
            latest_daily_jam);
      timecodestamper->current_tc->config.latest_daily_jam =
          g_value_dup_boxed (value);
      timecodestamper->ltc_current_tc->config.latest_daily_jam =
          timecodestamper->current_tc->config.latest_daily_jam !=
          NULL ? g_date_time_ref (timecodestamper->current_tc->config.
          latest_daily_jam)
          : NULL;
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
    case PROP_LTC_MAX_OFFSET:
      timecodestamper->ltc_max_offset = g_value_get_uint64 (value);
      break;
    case PROP_TC_ADD:
      timecodestamper->tc_add = g_value_get_int (value);
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
    case PROP_SOURCE:
      g_value_set_enum (value, timecodestamper->tc_source);
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
    case PROP_LTC_MAX_OFFSET:
      g_value_set_uint64 (value, timecodestamper->ltc_max_offset);
      break;
    case PROP_TC_ADD:
      g_value_set_int (value, timecodestamper->tc_add);
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
          timecodestamper->vinfo.fps_d == 60000)) {
    timecodestamper->current_tc->config.flags |=
        GST_VIDEO_TIME_CODE_FLAGS_DROP_FRAME;
    timecodestamper->ltc_current_tc->config.flags |=
        GST_VIDEO_TIME_CODE_FLAGS_DROP_FRAME;
  } else {
    timecodestamper->current_tc->config.flags &=
        ~GST_VIDEO_TIME_CODE_FLAGS_DROP_FRAME;
    timecodestamper->ltc_current_tc->config.flags &=
        ~GST_VIDEO_TIME_CODE_FLAGS_DROP_FRAME;
  }
}

static void
pad_flushing (GstTimeCodeStamper * timecodestamper)
{
#if HAVE_LTC
  g_mutex_lock (&timecodestamper->mutex);
  timecodestamper->is_flushing = TRUE;
  timecodestamper->no_wait = TRUE;
  g_cond_signal (&timecodestamper->ltc_cond_video);
  g_cond_signal (&timecodestamper->ltc_cond_audio);
  g_mutex_unlock (&timecodestamper->mutex);
#endif
}

static void
pad_flush_stop (GstTimeCodeStamper * timecodestamper)
{
#if HAVE_LTC
  g_mutex_lock (&timecodestamper->mutex);
  timecodestamper->is_flushing = FALSE;
  timecodestamper->no_wait = FALSE;
  g_mutex_unlock (&timecodestamper->mutex);
#endif
}

static gboolean
gst_timecodestamper_stop (GstBaseTransform * trans)
{
  GstTimeCodeStamper *timecodestamper = GST_TIME_CODE_STAMPER (trans);

  gst_video_info_init (&timecodestamper->vinfo);

  if (timecodestamper->ltc_intern_tc != NULL) {
    gst_video_time_code_free (timecodestamper->ltc_intern_tc);
    timecodestamper->ltc_intern_tc = NULL;
  }

  pad_flushing (timecodestamper);

  return TRUE;
}

static gboolean
gst_timecodestamper_start (GstBaseTransform * trans)
{
  GstTimeCodeStamper *timecodestamper = GST_TIME_CODE_STAMPER (trans);

  pad_flush_stop (timecodestamper);

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
  gst_video_time_code_clear (timecodestamper->ltc_current_tc);
  /* FIXME: What if the buffer doesn't contain both top and bottom fields? */
  gst_video_time_code_init (timecodestamper->current_tc,
      timecodestamper->vinfo.fps_n,
      timecodestamper->vinfo.fps_d,
      jam,
      timecodestamper->vinfo.interlace_mode ==
      GST_VIDEO_INTERLACE_MODE_PROGRESSIVE ? 0 :
      GST_VIDEO_TIME_CODE_FLAGS_INTERLACED, 0, 0, 0, 0, 0);
  gst_video_time_code_init (timecodestamper->ltc_current_tc,
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

      g_mutex_lock (&timecodestamper->mutex);

      if (timecodestamper->vinfo.fps_n == 0) {
        g_mutex_unlock (&timecodestamper->mutex);
        return
            GST_BASE_TRANSFORM_CLASS
            (gst_timecodestamper_parent_class)->sink_event (trans, event);
      }
      gst_event_copy_segment (event, &segment);
      if (segment.format != GST_FORMAT_TIME) {
        g_mutex_unlock (&timecodestamper->mutex);
        GST_ERROR_OBJECT (timecodestamper, "Invalid segment format");
        return FALSE;
      }
      if (GST_VIDEO_INFO_FORMAT (&timecodestamper->vinfo) ==
          GST_VIDEO_FORMAT_UNKNOWN) {
        GST_ERROR_OBJECT (timecodestamper,
            "Received segment event without caps");
        g_mutex_unlock (&timecodestamper->mutex);
        return FALSE;
      }

      if (timecodestamper->first_tc_now && !timecodestamper->first_tc) {
        GDateTime *dt = g_date_time_new_now_local ();
        GstVideoTimeCode *tc;

        gst_timecodestamper_set_drop_frame (timecodestamper);

        tc = gst_video_time_code_new_from_date_time (timecodestamper->vinfo.
            fps_n, timecodestamper->vinfo.fps_d, dt,
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
      g_mutex_unlock (&timecodestamper->mutex);
      if (notify)
        g_object_notify (G_OBJECT (timecodestamper), "first-timecode");
      break;
    }
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      g_mutex_lock (&timecodestamper->mutex);
      gst_event_parse_caps (event, &caps);
      if (!gst_video_info_from_caps (&timecodestamper->vinfo, caps)) {
        g_mutex_unlock (&timecodestamper->mutex);
        return FALSE;
      }
      if (timecodestamper->vinfo.fps_n == 0) {
        GST_WARNING_OBJECT (timecodestamper,
            "Non-constant frame rate found. Refusing to create a timecode");
        g_mutex_unlock (&timecodestamper->mutex);
        break;
      }

      gst_timecodestamper_reset_timecode (timecodestamper);
      g_mutex_unlock (&timecodestamper->mutex);
      break;
    }
    case GST_EVENT_FLUSH_START:
      pad_flushing (timecodestamper);
      break;
    case GST_EVENT_FLUSH_STOP:
      pad_flush_stop (timecodestamper);
      break;
    case GST_EVENT_EOS:
      g_mutex_lock (&timecodestamper->mutex);
      timecodestamper->no_wait = TRUE;
      g_cond_signal (&timecodestamper->ltc_cond_audio);
      g_mutex_unlock (&timecodestamper->mutex);
      break;
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
  GstVideoTimeCode *tc = NULL;
  GstVideoTimeCodeMeta *tc_meta = NULL;

#if HAVE_LTC
  GstClockTime frame_runtime, frame_duration;
#endif

  g_mutex_lock (&timecodestamper->mutex);

  if (timecodestamper->vinfo.fps_n == 0 || timecodestamper->vinfo.fps_d == 0) {
    g_mutex_unlock (&timecodestamper->mutex);
    return GST_FLOW_OK;
  }
#if HAVE_LTC

  frame_runtime =
      gst_segment_to_running_time (&vfilter->segment, GST_FORMAT_TIME,
      GST_BUFFER_PTS (buffer));

  frame_duration = gst_util_uint64_scale_int_ceil (GST_SECOND,
      timecodestamper->vinfo.fps_d, timecodestamper->vinfo.fps_n);

  if (timecodestamper->ltc_dec) {

    GstClockTime ltc_runtime;
    SMPTETimecode stc;
    LTCFrameExt ltc_frame;

    while (!timecodestamper->ltc_intern_tc &&
        timecodestamper->ltc_audio_endtime < frame_runtime + 2 * frame_duration
        && timecodestamper->ltc_audio_endtime +
        timecodestamper->ltc_max_offset >= frame_runtime
        && !timecodestamper->no_wait) {
      g_cond_wait (&timecodestamper->ltc_cond_video, &timecodestamper->mutex);
    }

    if (timecodestamper->is_flushing) {
      g_mutex_unlock (&timecodestamper->mutex);
      return GST_FLOW_FLUSHING;
    }

    while (ltc_decoder_read (timecodestamper->ltc_dec, &ltc_frame) == 1) {

      GstVideoTimeCode ltc_intern_tc;
      gint fps_n_div;

      ltc_runtime = timecodestamper->ltc_first_runtime +
          gst_util_uint64_scale (GST_SECOND, ltc_frame.off_start,
          timecodestamper->audio_info.rate);

      ltc_frame_to_time (&stc, &ltc_frame.ltc, 0);
      GST_INFO_OBJECT (timecodestamper,
          "Got LTC timecode %02d:%02d:%02d:%02d", stc.hours, stc.mins,
          stc.secs, stc.frame);
      fps_n_div =
          timecodestamper->vinfo.fps_n / timecodestamper->vinfo.fps_d >
          30 ? 2 : 1;
      gst_video_time_code_init (&ltc_intern_tc,
          timecodestamper->vinfo.fps_n / fps_n_div,
          timecodestamper->vinfo.fps_d,
          timecodestamper->current_tc->config.latest_daily_jam,
          timecodestamper->current_tc->config.flags, stc.hours, stc.mins,
          stc.secs, stc.frame, 0);
      if (!timecodestamper->ltc_intern_tc
          || gst_video_time_code_compare (timecodestamper->ltc_intern_tc,
              &ltc_intern_tc) != 0) {
        if (timecodestamper->ltc_intern_tc) {
          gst_video_time_code_free (timecodestamper->ltc_intern_tc);
          timecodestamper->ltc_intern_tc = NULL;
        }
        /* A timecode frame that starts +/- half a frame to the
         * video frame is considered belonging to that video frame */
        if (ABSDIFF (frame_runtime + frame_duration / 2, ltc_runtime) <
            gst_util_uint64_scale_int_ceil (GST_SECOND,
                timecodestamper->vinfo.fps_d,
                timecodestamper->vinfo.fps_n * 2)) {

          timecodestamper->ltc_intern_tc =
              gst_video_time_code_copy (&ltc_intern_tc);

          gst_video_time_code_clear (timecodestamper->ltc_current_tc);
          gst_video_time_code_init (timecodestamper->ltc_current_tc,
              timecodestamper->current_tc->config.fps_n,
              timecodestamper->current_tc->config.fps_d,
              timecodestamper->current_tc->config.latest_daily_jam,
              timecodestamper->current_tc->config.flags,
              stc.hours, stc.mins, stc.secs, stc.frame * fps_n_div, 0);
          GST_INFO_OBJECT (timecodestamper, "Resynced internal LTC counter");
        }
      } else {
        gst_video_time_code_increment_frame (timecodestamper->ltc_intern_tc);
        break;
      }

      gst_video_time_code_clear (&ltc_intern_tc);

      if (timecodestamper->ltc_intern_tc) {
        gst_video_time_code_increment_frame (timecodestamper->ltc_intern_tc);
      }
    }
    g_cond_signal (&timecodestamper->ltc_cond_audio);
  }
#endif

  switch (timecodestamper->tc_source) {
    case GST_TIME_CODE_STAMPER_NOREPLACE:
      tc_meta = gst_buffer_get_video_time_code_meta (buffer);
      if (tc_meta)
        tc = gst_video_time_code_copy (&tc_meta->tc);
      else
        tc = gst_video_time_code_copy (timecodestamper->current_tc);
    case GST_TIME_CODE_STAMPER_INTERN:
      tc = gst_video_time_code_copy (timecodestamper->current_tc);
      break;
    case GST_TIME_CODE_STAMPER_EXISTING:
      tc_meta = gst_buffer_get_video_time_code_meta (buffer);
      if (tc_meta)
        tc = gst_video_time_code_copy (&tc_meta->tc);
      break;
    case GST_TIME_CODE_STAMPER_LTC:
      tc = gst_video_time_code_copy (timecodestamper->ltc_current_tc);
      break;
    case GST_TIME_CODE_STAMPER_NRZERO:
      tc_meta = gst_buffer_get_video_time_code_meta (buffer);
      if (tc_meta) {
        tc = gst_video_time_code_copy (&tc_meta->tc);
      } else {
        GstVideoTimeCode *t = timecodestamper->current_tc;
        tc = gst_video_time_code_new (t->config.fps_n, t->config.fps_d,
            g_date_time_ref (t->config.latest_daily_jam),
            t->config.flags, 0, 0, 0, 0, 0);
      }
      break;
    default:
      break;
  }

  if (timecodestamper->tc_source != GST_TIME_CODE_STAMPER_EXISTING && !tc_meta) {
    gst_buffer_foreach_meta (buffer, remove_timecode_meta, NULL);
    gst_video_time_code_add_frames (tc, timecodestamper->tc_add);
    gst_buffer_add_video_time_code_meta (buffer, tc);
  }

  gst_video_time_code_increment_frame (timecodestamper->current_tc);
  if (timecodestamper->ltc_intern_tc)
    gst_video_time_code_increment_frame (timecodestamper->ltc_current_tc);

  g_mutex_unlock (&timecodestamper->mutex);

  if (timecodestamper->post_messages && tc) {
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
        "running-time", G_TYPE_UINT64, running_time, "duration",
        G_TYPE_UINT64, duration, "timecode", GST_TYPE_VIDEO_TIME_CODE, tc,
        NULL);
    msg = gst_message_new_element (GST_OBJECT (timecodestamper), s);
    gst_element_post_message (GST_ELEMENT (timecodestamper), msg);
  }

  if (tc)
    gst_video_time_code_free (tc);

  return GST_FLOW_OK;
}

static GstPad *
gst_timecodestamper_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name_templ, const GstCaps * caps)
{
  GstTimeCodeStamper *timecodestamper = GST_TIME_CODE_STAMPER (element);

  timecodestamper->ltcpad = gst_pad_new_from_static_template
      (&gst_timecodestamper_ltc_template, "ltc");

  gst_pad_set_chain_function (timecodestamper->ltcpad,
      GST_DEBUG_FUNCPTR (gst_timecodestamper_ltcpad_chain));
  gst_pad_set_event_function (timecodestamper->ltcpad,
      GST_DEBUG_FUNCPTR (gst_timecodestamper_ltcpad_event));
  gst_pad_set_query_function (timecodestamper->ltcpad,
      GST_DEBUG_FUNCPTR (gst_timecodestamper_ltcpad_query));
  gst_pad_set_activatemode_function (timecodestamper->ltcpad,
      GST_DEBUG_FUNCPTR (gst_timecodestamper_pad_activatemode));

  gst_element_add_pad (element, timecodestamper->ltcpad);

  return timecodestamper->ltcpad;
}

static void
gst_timecodestamper_release_pad (GstElement * element, GstPad * pad)
{
  GstTimeCodeStamper *timecodestamper = GST_TIME_CODE_STAMPER (element);

  if (timecodestamper->ltcpad == pad) {
    gst_element_remove_pad (element, pad);

    timecodestamper->ltcpad = NULL;

#if HAVE_LTC
    g_mutex_lock (&timecodestamper->mutex);
    g_cond_signal (&timecodestamper->ltc_cond_video);
    g_cond_signal (&timecodestamper->ltc_cond_audio);
    g_mutex_unlock (&timecodestamper->mutex);
    ltc_decoder_free (timecodestamper->ltc_dec);
    timecodestamper->ltc_dec = NULL;
#endif
  }
}

static GstFlowReturn
gst_timecodestamper_ltcpad_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buffer)
{
#if HAVE_LTC

  GstTimeCodeStamper *timecodestamper = GST_TIME_CODE_STAMPER (parent);
  GstMapInfo map;
  GstClockTime brt;
  GstFlowReturn fr;

  g_mutex_lock (&timecodestamper->mutex);

  brt = gst_segment_to_running_time (&timecodestamper->ltc_segment,
      GST_FORMAT_TIME, GST_BUFFER_PTS (buffer));
  if (GST_BUFFER_DURATION (buffer) != GST_CLOCK_TIME_NONE) {
    timecodestamper->ltc_audio_endtime = brt + GST_BUFFER_DURATION (buffer);
  } else if (timecodestamper->audio_info.rate > 0) {
    timecodestamper->ltc_audio_endtime = brt +
        gst_buffer_get_size (buffer) /
        GST_AUDIO_INFO_BPF (&timecodestamper->audio_info);
  }

  if (GST_BUFFER_IS_DISCONT (buffer)) {
    ltc_decoder_queue_flush (timecodestamper->ltc_dec);
    timecodestamper->ltc_total = 0;
  }

  if (timecodestamper->ltc_total == 0) {
    timecodestamper->ltc_first_runtime = brt;
  }

  gst_buffer_map (buffer, &map, GST_MAP_READ);
  ltc_decoder_write (timecodestamper->ltc_dec, map.data, map.size,
      timecodestamper->ltc_total);
  timecodestamper->ltc_total += map.size;
  gst_buffer_unmap (buffer, &map);

  g_cond_signal (&timecodestamper->ltc_cond_video);

  while (ltc_decoder_queue_length (timecodestamper->ltc_dec) >
      DEFAULT_LTC_QUEUE / 2 && !timecodestamper->no_wait) {
    g_cond_wait (&timecodestamper->ltc_cond_audio, &timecodestamper->mutex);
  }

  if (timecodestamper->is_flushing)
    fr = GST_FLOW_FLUSHING;
  else
    fr = GST_FLOW_OK;

  g_mutex_unlock (&timecodestamper->mutex);

#endif

  gst_buffer_unref (buffer);
  return fr;
}

static gboolean
gst_timecodestamper_ltcpad_event (GstPad * pad,
    GstObject * parent, GstEvent * event)
{
#if HAVE_LTC
  GstTimeCodeStamper *timecodestamper = GST_TIME_CODE_STAMPER (parent);

  GstCaps *caps;
  gint samples_per_frame = 1920;
#endif

  gboolean ret = TRUE;

  switch (GST_EVENT_TYPE (event)) {
#if HAVE_LTC
    case GST_EVENT_CAPS:
      gst_event_parse_caps (event, &caps);
      ret = gst_audio_info_from_caps (&timecodestamper->audio_info, caps);
      if (ret) {
        if (timecodestamper->vinfo.fps_n) {
          samples_per_frame = timecodestamper->audio_info.rate *
              timecodestamper->vinfo.fps_d / timecodestamper->vinfo.fps_n;
        }
      }

      if (!timecodestamper->ltc_dec) {
        timecodestamper->ltc_dec =
            ltc_decoder_create (samples_per_frame, DEFAULT_LTC_QUEUE);
        timecodestamper->ltc_total = 0;
      }

      break;
    case GST_EVENT_SEGMENT:
      gst_event_copy_segment (event, &timecodestamper->ltc_segment);
      break;

    case GST_EVENT_FLUSH_START:
      pad_flushing (timecodestamper);
      break;
    case GST_EVENT_FLUSH_STOP:
      pad_flush_stop (timecodestamper);
      break;
    case GST_EVENT_EOS:
      g_mutex_lock (&timecodestamper->mutex);
      timecodestamper->no_wait = TRUE;
      g_cond_signal (&timecodestamper->ltc_cond_video);
      g_mutex_unlock (&timecodestamper->mutex);
      break;
#endif

    default:
      break;
  }

  gst_event_unref (event);
  return ret;
}

static gboolean
gst_timecodestamper_ltcpad_query (GstPad * pad,
    GstObject * parent, GstQuery * query)
{
  GstCaps *caps, *filter, *tcaps;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
      gst_query_parse_caps (query, &filter);
      tcaps = gst_pad_get_pad_template_caps (pad);
      if (filter)
        caps = gst_caps_intersect_full (tcaps, filter,
            GST_CAPS_INTERSECT_FIRST);
      else
        caps = gst_caps_ref (tcaps);
      gst_query_set_caps_result (query, caps);
      gst_caps_unref (tcaps);
      gst_caps_unref (caps);
      return TRUE;
    default:
      return FALSE;
  }
}

static gboolean
gst_timecodestamper_pad_activatemode (GstPad * pad,
    GstObject * parent, GstPadMode mode, gboolean active)
{
  GstTimeCodeStamper *timecodestamper = GST_TIME_CODE_STAMPER (parent);

  if (!active) {
    pad_flushing (timecodestamper);
  }

  return TRUE;
}
