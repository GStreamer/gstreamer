/* GStreamer
 * Copyright (C) 2011 David Schleef <ds@entropywave.com>
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
 * SECTION:element-gstdecklinksink
 *
 * The decklinksink element is a sink element for BlackMagic DeckLink
 * cards.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 -v videotestsrc ! decklinksink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/video/video.h>
#include "gstdecklink.h"
#include "gstdecklinksink.h"
#include <string.h>

/* FIXME:
 *  - handle ALLOCATION query
 *  - provide buffer pool with suitable strides/alignment for video
 *  - handle video meta
 */

GST_DEBUG_CATEGORY_STATIC (gst_decklink_sink_debug_category);
#define GST_CAT_DEFAULT gst_decklink_sink_debug_category

static void gst_decklink_sink_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_decklink_sink_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_decklink_sink_finalize (GObject * object);

static GstStateChangeReturn
gst_decklink_sink_change_state (GstElement * element,
    GstStateChange transition);

static GstFlowReturn gst_decklink_sink_videosink_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buffer);
static gboolean gst_decklink_sink_videosink_event (GstPad * pad,
    GstObject * parent, GstEvent * event);
static gboolean gst_decklink_sink_videosink_query (GstPad * pad,
    GstObject * parent, GstQuery * query);

static GstFlowReturn gst_decklink_sink_audiosink_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buffer);
static gboolean gst_decklink_sink_audiosink_event (GstPad * pad,
    GstObject * parent, GstEvent * event);
static gboolean gst_decklink_sink_audiosink_query (GstPad * pad,
    GstObject * parent, GstQuery * query);

#ifdef _MSC_VER
/* COM initialization/uninitialization thread */
static void gst_decklink_sink_com_thread (GstDecklinkSink * sink);
#endif /* _MSC_VER */

enum
{
  PROP_0,
  PROP_MODE,
  PROP_DEVICE_NUMBER
};

/* pad templates */

/* the video sink pad template is created on the fly */

static GstStaticPadTemplate gst_decklink_sink_audiosink_template =
GST_STATIC_PAD_TEMPLATE ("audiosink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw, format=S16LE, channels=2, rate=48000, "
        "layout=interleaved")
    );

#define parent_class gst_decklink_sink_parent_class
G_DEFINE_TYPE (GstDecklinkSink, gst_decklink_sink, GST_TYPE_ELEMENT);

static void
gst_decklink_sink_class_init (GstDecklinkSinkClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_decklink_sink_set_property;
  gobject_class->get_property = gst_decklink_sink_get_property;
  gobject_class->finalize = gst_decklink_sink_finalize;

  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_decklink_sink_change_state);

  g_object_class_install_property (gobject_class, PROP_MODE,
      g_param_spec_enum ("mode", "Playback Mode",
          "Video Mode to use for playback",
          GST_TYPE_DECKLINK_MODE, GST_DECKLINK_MODE_NTSC,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              G_PARAM_CONSTRUCT)));

  g_object_class_install_property (gobject_class, PROP_DEVICE_NUMBER,
      g_param_spec_int ("device-number", "Device number",
          "Output device instance to use", 0, G_MAXINT, 0,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              G_PARAM_CONSTRUCT)));

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("videosink", GST_PAD_SINK, GST_PAD_ALWAYS,
          gst_decklink_mode_get_template_caps ()));

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_decklink_sink_audiosink_template));

  gst_element_class_set_static_metadata (element_class, "Decklink Sink",
      "Video/Sink", "Decklink Sink", "David Schleef <ds@entropywave.com>");

  GST_DEBUG_CATEGORY_INIT (gst_decklink_sink_debug_category, "decklinksink", 0,
      "debug category for decklinksink element");
}

static void
gst_decklink_sink_init (GstDecklinkSink * decklinksink)
{
  GstDecklinkSinkClass *decklinksink_class;

  decklinksink_class = GST_DECKLINK_SINK_GET_CLASS (decklinksink);

  decklinksink->videosinkpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template
      (GST_ELEMENT_CLASS (decklinksink_class), "videosink"), "videosink");
  gst_pad_set_chain_function (decklinksink->videosinkpad,
      GST_DEBUG_FUNCPTR (gst_decklink_sink_videosink_chain));
  gst_pad_set_event_function (decklinksink->videosinkpad,
      GST_DEBUG_FUNCPTR (gst_decklink_sink_videosink_event));
  gst_pad_set_query_function (decklinksink->videosinkpad,
      GST_DEBUG_FUNCPTR (gst_decklink_sink_videosink_query));
  gst_element_add_pad (GST_ELEMENT (decklinksink), decklinksink->videosinkpad);

  decklinksink->audiosinkpad =
      gst_pad_new_from_static_template (&gst_decklink_sink_audiosink_template,
      "audiosink");
  gst_pad_set_chain_function (decklinksink->audiosinkpad,
      GST_DEBUG_FUNCPTR (gst_decklink_sink_audiosink_chain));
  gst_pad_set_event_function (decklinksink->audiosinkpad,
      GST_DEBUG_FUNCPTR (gst_decklink_sink_audiosink_event));
  gst_pad_set_query_function (decklinksink->audiosinkpad,
      GST_DEBUG_FUNCPTR (gst_decklink_sink_audiosink_query));
  gst_element_add_pad (GST_ELEMENT (decklinksink), decklinksink->audiosinkpad);

  GST_OBJECT_FLAG_SET (decklinksink, GST_ELEMENT_FLAG_SINK);

  g_cond_init (&decklinksink->cond);
  g_mutex_init (&decklinksink->mutex);
  g_mutex_init (&decklinksink->audio_mutex);
  g_cond_init (&decklinksink->audio_cond);

  decklinksink->mode = GST_DECKLINK_MODE_NTSC;
  decklinksink->device_number = 0;

  decklinksink->callback = new Output;
  decklinksink->callback->decklinksink = decklinksink;

#ifdef _MSC_VER
  g_mutex_init (&decklinksink->com_init_lock);
  g_mutex_init (&decklinksink->com_deinit_lock);
  g_cond_init (&decklinksink->com_initialized);
  g_cond_init (&decklinksink->com_uninitialize);
  g_cond_init (&decklinksink->com_uninitialized);

  g_mutex_lock (&decklinksink->com_init_lock);

  /* create the COM initialization thread */
  g_thread_create ((GThreadFunc) gst_decklink_sink_com_thread,
      decklinksink, FALSE, NULL);

  /* wait until the COM thread signals that COM has been initialized */
  g_cond_wait (&decklinksink->com_initialized, &decklinksink->com_init_lock);
  g_mutex_unlock (&decklinksink->com_init_lock);
#endif /* _MSC_VER */
}

void
gst_decklink_sink_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDecklinkSink *decklinksink;

  g_return_if_fail (GST_IS_DECKLINK_SINK (object));
  decklinksink = GST_DECKLINK_SINK (object);

  switch (property_id) {
    case PROP_MODE:
      decklinksink->mode = (GstDecklinkModeEnum) g_value_get_enum (value);
      break;
    case PROP_DEVICE_NUMBER:
      decklinksink->device_number = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_decklink_sink_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstDecklinkSink *decklinksink;

  g_return_if_fail (GST_IS_DECKLINK_SINK (object));
  decklinksink = GST_DECKLINK_SINK (object);

  switch (property_id) {
    case PROP_MODE:
      g_value_set_enum (value, decklinksink->mode);
      break;
    case PROP_DEVICE_NUMBER:
      g_value_set_int (value, decklinksink->device_number);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

#ifdef _MSC_VER
static void
gst_decklink_sink_com_thread (GstDecklinkSink * sink)
{
  HRESULT res;

  g_mutex_lock (sink->com_init_lock);

  /* Initialize COM with a MTA for this process. This thread will
   * be the first one to enter the apartement and the last one to leave
   * it, unitializing COM properly */

  res = CoInitializeEx (0, COINIT_MULTITHREADED);
  if (res == S_FALSE)
    GST_WARNING_OBJECT (sink,
        "COM has been already initialized in the same process");
  else if (res == RPC_E_CHANGED_MODE)
    GST_WARNING_OBJECT (sink, "The concurrency model of COM has changed.");
  else
    GST_INFO_OBJECT (sink, "COM intialized succesfully");

  sink->comInitialized = TRUE;

  /* Signal other threads waiting on this condition that COM was initialized */
  g_cond_signal (sink->com_initialized);

  g_mutex_unlock (sink->com_init_lock);

  /* Wait until the unitialize condition is met to leave the COM apartement */
  g_mutex_lock (sink->com_deinit_lock);
  g_cond_wait (sink->com_uninitialize, sink->com_deinit_lock);

  CoUninitialize ();
  GST_INFO_OBJECT (sink, "COM unintialized succesfully");
  sink->comInitialized = FALSE;
  g_cond_signal (sink->com_uninitialized);
  g_mutex_unlock (sink->com_deinit_lock);
}
#endif /* _MSC_VER */

void
gst_decklink_sink_finalize (GObject * object)
{
  GstDecklinkSink *decklinksink;

  decklinksink = GST_DECKLINK_SINK (object);

  g_cond_clear (&decklinksink->cond);
  g_mutex_clear (&decklinksink->mutex);
  g_cond_clear (&decklinksink->audio_cond);
  g_mutex_clear (&decklinksink->audio_mutex);

  delete decklinksink->callback;

#ifdef _MSC_VER
  /* signal the COM thread that it should uninitialize COM */
  if (decklinksink->comInitialized) {
    g_mutex_lock (&decklinksink->com_deinit_lock);
    g_cond_signal (&decklinksink->com_uninitialize);
    g_cond_wait (&decklinksink->com_uninitialized,
        &decklinksink->com_deinit_lock);
    g_mutex_unlock (&decklinksink->com_deinit_lock);
  }

  g_mutex_clear (&decklinksink->com_init_lock);
  g_mutex_clear (&decklinksink->com_deinit_lock);
  g_cond_clear (&decklinksink->com_initialized);
  g_cond_clear (&decklinksink->com_uninitialize);
  g_cond_clear (&decklinksink->com_uninitialized);
#endif /* _MSC_VER */

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* FIXME: post error messages for the misc. failures */
static gboolean
gst_decklink_sink_start (GstDecklinkSink * decklinksink)
{
  HRESULT ret;
  const GstDecklinkMode *mode;
  BMDAudioSampleType sample_depth;

  decklinksink->decklink =
      gst_decklink_get_nth_device (decklinksink->device_number);
  if (!decklinksink->decklink) {
    GST_WARNING ("failed to get device %d", decklinksink->device_number);
    return FALSE;
  }

  decklinksink->output =
      gst_decklink_get_nth_output (decklinksink->device_number);
  if (!decklinksink->decklink) {
    GST_WARNING ("no output for device %d", decklinksink->device_number);
    return FALSE;
  }

  decklinksink->output->SetAudioCallback (decklinksink->callback);

  mode = gst_decklink_get_mode (decklinksink->mode);

  ret = decklinksink->output->EnableVideoOutput (mode->mode,
      bmdVideoOutputFlagDefault);
  if (ret != S_OK) {
    GST_WARNING ("failed to enable video output");
    return FALSE;
  }
  //decklinksink->video_enabled = TRUE;

  decklinksink->output->SetScheduledFrameCompletionCallback (decklinksink->
      callback);

  sample_depth = bmdAudioSampleType16bitInteger;
  ret = decklinksink->output->EnableAudioOutput (bmdAudioSampleRate48kHz,
      sample_depth, 2, bmdAudioOutputStreamContinuous);
  if (ret != S_OK) {
    GST_WARNING ("failed to enable audio output");
    return FALSE;
  }
  decklinksink->audio_adapter = gst_adapter_new ();

  decklinksink->num_frames = 0;

  return TRUE;
}

static gboolean
gst_decklink_sink_force_stop (GstDecklinkSink * decklinksink)
{
  g_mutex_lock (&decklinksink->mutex);
  decklinksink->stop = TRUE;
  g_cond_signal (&decklinksink->cond);
  g_mutex_unlock (&decklinksink->mutex);

  g_mutex_lock (&decklinksink->audio_mutex);
  g_cond_signal (&decklinksink->audio_cond);
  g_mutex_unlock (&decklinksink->audio_mutex);

  return TRUE;
}

static gboolean
gst_decklink_sink_stop (GstDecklinkSink * decklinksink)
{
  decklinksink->output->StopScheduledPlayback (0, NULL, 0);
  decklinksink->output->DisableAudioOutput ();
  decklinksink->output->DisableVideoOutput ();

  return TRUE;
}

static GstStateChangeReturn
gst_decklink_sink_change_state (GstElement * element, GstStateChange transition)
{
  GstDecklinkSink *decklinksink;
  GstStateChangeReturn ret;

  decklinksink = GST_DECKLINK_SINK (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_decklink_sink_start (decklinksink)) {
        ret = GST_STATE_CHANGE_FAILURE;
        goto out;
      }
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      gst_decklink_sink_force_stop (decklinksink);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_decklink_sink_stop (decklinksink);
      break;
    default:
      break;
  }

out:
  return ret;
}

static GstFlowReturn
gst_decklink_sink_videosink_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer)
{
  GstDecklinkSink *decklinksink;
  IDeckLinkMutableVideoFrame *frame;
  void *data;
  GstFlowReturn flow_ret;
  const GstDecklinkMode *mode;
  HRESULT ret;

  decklinksink = GST_DECKLINK_SINK (parent);

#if 0
  if (!decklinksink->video_enabled) {
    HRESULT ret;
    ret = decklinksink->output->EnableVideoOutput (decklinksink->display_mode,
        bmdVideoOutputFlagDefault);
    if (ret != S_OK) {
      GST_WARNING ("failed to enable video output");
      //return FALSE;
    }
    decklinksink->video_enabled = TRUE;
  }
#endif

  mode = gst_decklink_get_mode (decklinksink->mode);

  ret = decklinksink->output->CreateVideoFrame (mode->width,
      mode->height, mode->width * 2, decklinksink->pixel_format,
      bmdFrameFlagDefault, &frame);
  if (ret != S_OK) {
    GST_ELEMENT_ERROR (decklinksink, STREAM, FAILED,
        (NULL), ("Failed to create video frame: 0x%08x", ret));
    return GST_FLOW_ERROR;
  }

  frame->GetBytes (&data);
  gst_buffer_extract (buffer, 0, data, gst_buffer_get_size (buffer));
  gst_buffer_unref (buffer);

  g_mutex_lock (&decklinksink->mutex);
  while (decklinksink->queued_frames > 2 && !decklinksink->stop) {
    g_cond_wait (&decklinksink->cond, &decklinksink->mutex);
  }
  if (!decklinksink->stop) {
    decklinksink->queued_frames++;
  }
  g_mutex_unlock (&decklinksink->mutex);

  if (!decklinksink->stop) {
    ret = decklinksink->output->ScheduleVideoFrame (frame,
        decklinksink->num_frames * mode->fps_d, mode->fps_d, mode->fps_n);
    if (ret != S_OK) {
      GST_ELEMENT_ERROR (decklinksink, STREAM, FAILED,
          (NULL), ("Failed to schedule frame: 0x%08x", ret));
      flow_ret = GST_FLOW_ERROR;
      goto out;
    }

    decklinksink->num_frames++;

    if (!decklinksink->sched_started) {
      ret = decklinksink->output->StartScheduledPlayback (0, mode->fps_d, 1.0);
      if (ret != S_OK) {
        GST_ELEMENT_ERROR (decklinksink, STREAM, FAILED,
            (NULL), ("Failed to start scheduled playback: 0x%08x", ret));
        flow_ret = GST_FLOW_ERROR;
        goto out;
      }
      decklinksink->sched_started = TRUE;
    }

    flow_ret = GST_FLOW_OK;
  } else {
    flow_ret = GST_FLOW_FLUSHING;
  }

out:
  frame->Release ();

  return flow_ret;
}

static gboolean
gst_decklink_sink_videosink_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  gboolean res;
  GstDecklinkSink *decklinksink;

  decklinksink = GST_DECKLINK_SINK (parent);

  GST_DEBUG_OBJECT (pad, "event: %" GST_PTR_FORMAT, event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:{
      decklinksink->pixel_format = bmdFormat8BitYUV;
      res = TRUE;
      /* FIXME: this makes no sense, template caps don't contain v210 */
#if 0
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      ret = gst_video_format_parse_caps (caps, &format, &width, &height);
      if (ret) {
        if (format == GST_VIDEO_FORMAT_v210) {
          decklinksink->pixel_format = bmdFormat10BitYUV;
        } else {
          decklinksink->pixel_format = bmdFormat8BitYUV;
        }
      }
#endif
      break;
    }
    case GST_EVENT_EOS:
      /* FIXME: EOS aggregation with audio pad looks wrong */
      decklinksink->video_eos = TRUE;
      decklinksink->video_seqnum = gst_event_get_seqnum (event);
      {
        GstMessage *message;

        message = gst_message_new_eos (GST_OBJECT_CAST (decklinksink));
        gst_message_set_seqnum (message, decklinksink->video_seqnum);
        gst_element_post_message (GST_ELEMENT_CAST (decklinksink), message);
      }
      res = gst_pad_event_default (pad, parent, event);
      break;
    default:
      res = gst_pad_event_default (pad, parent, event);
      break;
  }

  return res;
}

static gboolean
gst_decklink_sink_videosink_query (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  gboolean res;
  GstDecklinkSink *decklinksink;

  decklinksink = GST_DECKLINK_SINK (parent);

  GST_DEBUG_OBJECT (decklinksink, "query");

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:{
      GstCaps *mode_caps, *filter, *caps;

      /* FIXME: do we change mode if incoming caps change? If yes, we
       * should probably return the template caps instead */
      mode_caps = gst_decklink_mode_get_caps (decklinksink->mode);
      gst_query_parse_caps (query, &filter);
      if (filter) {
        caps =
            gst_caps_intersect_full (filter, mode_caps,
            GST_CAPS_INTERSECT_FIRST);
        gst_caps_unref (mode_caps);
      } else {
        caps = mode_caps;
      }
      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);
      res = TRUE;
      break;
    }
    default:
      res = gst_pad_query_default (pad, parent, query);
      break;
  }

  return res;
}

static GstFlowReturn
gst_decklink_sink_audiosink_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer)
{
  GstDecklinkSink *decklinksink;
  GstFlowReturn ret;

  decklinksink = GST_DECKLINK_SINK (parent);

  if (decklinksink->stop)
    return GST_FLOW_FLUSHING;

  g_mutex_lock (&decklinksink->audio_mutex);
  while (!decklinksink->stop &&
      gst_adapter_available (decklinksink->audio_adapter) > 1600 * 4 * 2) {
    g_cond_wait (&decklinksink->audio_cond, &decklinksink->audio_mutex);
  }
  gst_adapter_push (decklinksink->audio_adapter, buffer);
  g_mutex_unlock (&decklinksink->audio_mutex);

  ret = GST_FLOW_OK;
  return ret;
}

static gboolean
gst_decklink_sink_audiosink_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  gboolean res;
  GstDecklinkSink *decklinksink;

  decklinksink = GST_DECKLINK_SINK (parent);

  GST_DEBUG_OBJECT (pad, "event: %" GST_PTR_FORMAT, event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      /* FIXME: EOS aggregation with video pad looks wrong */
      decklinksink->audio_eos = TRUE;
      decklinksink->audio_seqnum = gst_event_get_seqnum (event);
      res = gst_pad_event_default (pad, parent, event);
      break;
    default:
      res = gst_pad_event_default (pad, parent, event);
      break;
  }

  return res;
}

static gboolean
gst_decklink_sink_audiosink_query (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  gboolean res;

  GST_DEBUG_OBJECT (pad, "query: %" GST_PTR_FORMAT, query);

  switch (GST_QUERY_TYPE (query)) {
    default:
      res = gst_pad_query_default (pad, parent, query);
      break;
  }

  return res;
}

HRESULT
    Output::ScheduledFrameCompleted (IDeckLinkVideoFrame * completedFrame,
    BMDOutputFrameCompletionResult result)
{
  GST_DEBUG ("ScheduledFrameCompleted");

  g_mutex_lock (&decklinksink->mutex);
  g_cond_signal (&decklinksink->cond);
  decklinksink->queued_frames--;
  g_mutex_unlock (&decklinksink->mutex);

  return S_OK;
}

HRESULT
Output::ScheduledPlaybackHasStopped ()
{
  GST_DEBUG ("ScheduledPlaybackHasStopped");
  return S_OK;
}

HRESULT
Output::RenderAudioSamples (bool preroll)
{
  uint32_t samplesWritten;
  HRESULT ret;

  // guint64 samplesToWrite;

  if (decklinksink->stop) {
    GST_DEBUG ("decklinksink->stop set TRUE!");
    decklinksink->output->BeginAudioPreroll ();
    // running = true;
  } else {
    gconstpointer data;
    int n;

    g_mutex_lock (&decklinksink->audio_mutex);

    n = gst_adapter_available (decklinksink->audio_adapter);
    if (n > 0) {
      data = gst_adapter_map (decklinksink->audio_adapter, n);

      ret = decklinksink->output->ScheduleAudioSamples ((void *) data, n / 4,
          0, 0, &samplesWritten);

      gst_adapter_unmap (decklinksink->audio_adapter);
      gst_adapter_flush (decklinksink->audio_adapter, samplesWritten * 4);
      if (ret != S_OK) {
        GST_ELEMENT_ERROR (decklinksink, STREAM, FAILED,
            (NULL), ("Failed to schedule audio samples: 0x%08x", ret));
      } else {
        GST_DEBUG ("wrote %d samples, %d available", samplesWritten, n / 4);
      }

      g_cond_signal (&decklinksink->audio_cond);
    } else {
      if (decklinksink->audio_eos) {
        GstMessage *message;

        message = gst_message_new_eos (GST_OBJECT_CAST (decklinksink));
        gst_message_set_seqnum (message, decklinksink->audio_seqnum);
        gst_element_post_message (GST_ELEMENT_CAST (decklinksink), message);
      }
    }
    g_mutex_unlock (&decklinksink->audio_mutex);

  }

  GST_DEBUG ("RenderAudioSamples");

  return S_OK;
}
