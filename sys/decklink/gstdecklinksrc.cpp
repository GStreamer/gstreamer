/* GStreamer
 * Copyright (C) 2011 David Schleef <ds@schleef.org>
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
 * SECTION:element-gstdecklinksrc
 *
 * The decklinksrc element is a source element for Blackmagic
 * Decklink cards.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 -v decklinksrc ! videoconvert ! xvimagesink
 * ]|
 * 
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include "gstdecklink.h"
#include "gstdecklinksrc.h"
#include "capture.h"
#include <string.h>


GST_DEBUG_CATEGORY (gst_decklink_src_debug_category);
#define GST_CAT_DEFAULT gst_decklink_src_debug_category

typedef struct _VideoFrame VideoFrame;
struct _VideoFrame
{
  IDeckLinkVideoInputFrame *frame;
  IDeckLinkInput *input;
};

static void gst_decklink_src_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_decklink_src_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_decklink_src_finalize (GObject * object);

static GstStateChangeReturn
gst_decklink_src_change_state (GstElement * element, GstStateChange transition);
static gboolean gst_decklink_src_send_event (GstElement * element,
    GstEvent * event);

static gboolean gst_decklink_src_audio_src_query (GstPad * pad,
    GstObject * parent, GstQuery * query);
static gboolean gst_decklink_src_video_src_query (GstPad * pad,
    GstObject * parent, GstQuery * query);

static void gst_decklink_src_task (void *priv);

#ifdef _MSC_VER
/* COM initialization/uninitialization thread */
static void gst_decklink_src_com_thread (GstDecklinkSrc * src);
#endif /* _MSC_VER */

enum
{
  PROP_0,
  PROP_MODE,
  PROP_CONNECTION,
  PROP_AUDIO_INPUT,
  PROP_DEVICE_NUMBER
};

static GstStaticPadTemplate gst_decklink_src_audio_src_template =
GST_STATIC_PAD_TEMPLATE ("audiosrc",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw, format=S16LE, channels=2, rate=48000, "
        "layout=interleaved")
    );

/* the video source pad template is created on the fly */

#define parent_class gst_decklink_src_parent_class
G_DEFINE_TYPE (GstDecklinkSrc, gst_decklink_src, GST_TYPE_ELEMENT);

static void
gst_decklink_src_class_init (GstDecklinkSrcClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_decklink_src_set_property;
  gobject_class->get_property = gst_decklink_src_get_property;
  gobject_class->finalize = gst_decklink_src_finalize;

  element_class->send_event = GST_DEBUG_FUNCPTR (gst_decklink_src_send_event);
  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_decklink_src_change_state);

  g_object_class_install_property (gobject_class, PROP_MODE,
      g_param_spec_enum ("mode", "Mode", "Video mode to use for capture",
          GST_TYPE_DECKLINK_MODE, GST_DECKLINK_MODE_NTSC,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              G_PARAM_CONSTRUCT)));

  g_object_class_install_property (gobject_class, PROP_CONNECTION,
      g_param_spec_enum ("connection", "Connection",
          "Video input connection to use",
          GST_TYPE_DECKLINK_CONNECTION, GST_DECKLINK_CONNECTION_SDI,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              G_PARAM_CONSTRUCT)));

  g_object_class_install_property (gobject_class, PROP_AUDIO_INPUT,
      g_param_spec_enum ("audio-input", "Audio Input",
          "Audio input connection",
          GST_TYPE_DECKLINK_AUDIO_CONNECTION,
          GST_DECKLINK_AUDIO_CONNECTION_AUTO,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              G_PARAM_CONSTRUCT)));

  g_object_class_install_property (gobject_class, PROP_DEVICE_NUMBER,
      g_param_spec_int ("device-number", "Device number",
          "Capture device instance to use", 0, G_MAXINT, 0,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              G_PARAM_CONSTRUCT)));

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_decklink_src_audio_src_template));

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("videosrc", GST_PAD_SRC, GST_PAD_ALWAYS,
          gst_decklink_mode_get_template_caps ()));

  gst_element_class_set_static_metadata (element_class, "Decklink source",
      "Source/Video", "DeckLink Source", "David Schleef <ds@entropywave.com>");
}

static void
gst_decklink_src_init (GstDecklinkSrc * decklinksrc)
{
  GstDecklinkSrcClass *decklinksrc_class;

  decklinksrc_class = GST_DECKLINK_SRC_GET_CLASS (decklinksrc);

  g_rec_mutex_init (&decklinksrc->task_mutex);
  decklinksrc->task = gst_task_new (gst_decklink_src_task, decklinksrc, NULL);
  gst_task_set_lock (decklinksrc->task, &decklinksrc->task_mutex);

  decklinksrc->audiosrcpad =
      gst_pad_new_from_static_template (&gst_decklink_src_audio_src_template,
      "audiosrc");
  gst_pad_set_query_function (decklinksrc->audiosrcpad,
      GST_DEBUG_FUNCPTR (gst_decklink_src_audio_src_query));
  gst_element_add_pad (GST_ELEMENT (decklinksrc), decklinksrc->audiosrcpad);



  decklinksrc->videosrcpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template
      (GST_ELEMENT_CLASS (decklinksrc_class), "videosrc"), "videosrc");
  gst_pad_set_query_function (decklinksrc->videosrcpad,
      GST_DEBUG_FUNCPTR (gst_decklink_src_video_src_query));
  gst_element_add_pad (GST_ELEMENT (decklinksrc), decklinksrc->videosrcpad);

  GST_OBJECT_FLAG_SET (decklinksrc, GST_ELEMENT_FLAG_SOURCE);

  g_cond_init (&decklinksrc->cond);
  g_mutex_init (&decklinksrc->mutex);

  /* FIXME: turn this into a property? */
  decklinksrc->copy_data = TRUE;
  decklinksrc->mode = GST_DECKLINK_MODE_NTSC;
  decklinksrc->connection = GST_DECKLINK_CONNECTION_SDI;
  decklinksrc->audio_connection = GST_DECKLINK_AUDIO_CONNECTION_AUTO;
  decklinksrc->device_number = 0;

  decklinksrc->stop = FALSE;
  decklinksrc->dropped_frames = 0;
  decklinksrc->dropped_frames_old = 0;
  decklinksrc->frame_num = -1;  /* -1 so will be 0 after incrementing */

#ifdef _MSC_VER
  g_mutex_init (&decklinksrc->com_init_lock);
  g_mutex_init (&decklinksrc->com_deinit_lock);
  g_cond_init (&decklinksrc->com_initialized);
  g_cond_init (&decklinksrc->com_uninitialize);
  g_cond_init (&decklinksrc->com_uninitialized);

  g_mutex_lock (&decklinksrc->com_init_lock);

  /* create the COM initialization thread */
  g_thread_create ((GThreadFunc) gst_decklink_src_com_thread,
      decklinksrc, FALSE, NULL);

  /* wait until the COM thread signals that COM has been initialized */
  g_cond_wait (&decklinksrc->com_initialized, &decklinksrc->com_init_lock);
  g_mutex_unlock (&decklinksrc->com_init_lock);
#endif /* _MSC_VER */

  GST_DEBUG_CATEGORY_INIT (gst_decklink_src_debug_category, "decklinksrc", 0,
      "debug category for decklinksrc element");
}

void
gst_decklink_src_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDecklinkSrc *decklinksrc;

  decklinksrc = GST_DECKLINK_SRC (object);

  switch (property_id) {
    case PROP_MODE:
      decklinksrc->mode = (GstDecklinkModeEnum) g_value_get_enum (value);
      break;
    case PROP_CONNECTION:
      decklinksrc->connection =
          (GstDecklinkConnectionEnum) g_value_get_enum (value);
      break;
    case PROP_AUDIO_INPUT:
      decklinksrc->audio_connection =
          (GstDecklinkAudioConnectionEnum) g_value_get_enum (value);
      break;
    case PROP_DEVICE_NUMBER:
      decklinksrc->device_number = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_decklink_src_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstDecklinkSrc *decklinksrc;

  decklinksrc = GST_DECKLINK_SRC (object);

  switch (property_id) {
    case PROP_MODE:
      g_value_set_enum (value, decklinksrc->mode);
      break;
    case PROP_CONNECTION:
      g_value_set_enum (value, decklinksrc->connection);
      break;
    case PROP_AUDIO_INPUT:
      g_value_set_enum (value, decklinksrc->audio_connection);
      break;
    case PROP_DEVICE_NUMBER:
      g_value_set_int (value, decklinksrc->device_number);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

#ifdef _MSC_VER
static void
gst_decklink_src_com_thread (GstDecklinkSrc * src)
{
  HRESULT res;

  g_mutex_lock (src->com_init_lock);

  /* Initialize COM with a MTA for this process. This thread will
   * be the first one to enter the apartement and the last one to leave
   * it, unitializing COM properly */

  res = CoInitializeEx (0, COINIT_MULTITHREADED);
  if (res == S_FALSE)
    GST_WARNING_OBJECT (src,
        "COM has been already initialized in the same process");
  else if (res == RPC_E_CHANGED_MODE)
    GST_WARNING_OBJECT (src, "The concurrency model of COM has changed.");
  else
    GST_INFO_OBJECT (src, "COM intialized succesfully");

  src->comInitialized = TRUE;

  /* Signal other threads waiting on this condition that COM was initialized */
  g_cond_signal (src->com_initialized);

  g_mutex_unlock (src->com_init_lock);

  /* Wait until the unitialize condition is met to leave the COM apartement */
  g_mutex_lock (src->com_deinit_lock);
  g_cond_wait (src->com_uninitialize, src->com_deinit_lock);

  CoUninitialize ();
  GST_INFO_OBJECT (src, "COM unintialized succesfully");
  src->comInitialized = FALSE;
  g_cond_signal (src->com_uninitialized);
  g_mutex_unlock (src->com_deinit_lock);
}
#endif /* _MSC_VER */

void
gst_decklink_src_finalize (GObject * object)
{
  GstDecklinkSrc *decklinksrc;

  g_return_if_fail (GST_IS_DECKLINK_SRC (object));
  decklinksrc = GST_DECKLINK_SRC (object);

  /* clean up object here */

  g_cond_clear (&decklinksrc->cond);
  g_mutex_clear (&decklinksrc->mutex);
  gst_task_set_lock (decklinksrc->task, NULL);
  g_object_unref (decklinksrc->task);

#ifdef _MSC_VER
  /* signal the COM thread that it should uninitialize COM */
  if (decklinksrc->comInitialized) {
    g_mutex_lock (&decklinksrc->com_deinit_lock);
    g_cond_signal (&decklinksrc->com_uninitialize);
    g_cond_wait (&decklinksrc->com_uninitialized, &decklinksrc->com_deinit_lock);
    g_mutex_unlock (&decklinksrc->com_deinit_lock);
  }

  g_mutex_clear (&decklinksrc->com_init_lock);
  g_mutex_clear (&decklinksrc->com_deinit_lock);
  g_cond_clear (&decklinksrc->com_initialized);
  g_cond_clear (&decklinksrc->com_uninitialize);
  g_cond_clear (&decklinksrc->com_uninitialized);
#endif /* _MSC_VER */

  g_rec_mutex_clear (&decklinksrc->task_mutex);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* events sent to this element directly, mainly from the application */
static gboolean
gst_decklink_src_send_event (GstElement * element, GstEvent * event)
{
  GstDecklinkSrc *src;
  gboolean result = FALSE;

  src = GST_DECKLINK_SRC (element);

  GST_DEBUG_OBJECT (src, "handling event %p %" GST_PTR_FORMAT, event, event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      g_atomic_int_set (&src->pending_eos, TRUE);
      GST_INFO_OBJECT (src, "EOS pending");
      g_cond_signal (&src->cond);
      result = TRUE;
      break;
      break;
    case GST_EVENT_TAG:
    case GST_EVENT_CUSTOM_DOWNSTREAM:
    case GST_EVENT_CUSTOM_BOTH:
      /* Insert TAG, CUSTOM_DOWNSTREAM, CUSTOM_BOTH in the dataflow */
      GST_OBJECT_LOCK (src);
      src->pending_events = g_list_append (src->pending_events, event);
      g_atomic_int_set (&src->have_events, TRUE);
      GST_OBJECT_UNLOCK (src);
      event = NULL;
      result = TRUE;
      break;
    case GST_EVENT_CUSTOM_DOWNSTREAM_OOB:
    case GST_EVENT_CUSTOM_BOTH_OOB:
      /* insert a random custom event into the pipeline */
      GST_DEBUG_OBJECT (src, "pushing custom OOB event downstream");
      result = gst_pad_push_event (src->videosrcpad, gst_event_ref (event));
      result |= gst_pad_push_event (src->audiosrcpad, event);
      /* we gave away the ref to the event in the push */
      event = NULL;
      break;
    case GST_EVENT_CUSTOM_UPSTREAM:
      /* drop */
    case GST_EVENT_SEGMENT:
      /* sending random SEGMENT downstream can break sync - drop */
    default:
      GST_LOG_OBJECT (src, "dropping %s event", GST_EVENT_TYPE_NAME (event));
      break;
  }

  /* if we still have a ref to the event, unref it now */
  if (event)
    gst_event_unref (event);

  return result;
}

/* FIXME: post error messages for the misc. failures */
static gboolean
gst_decklink_src_start (GstElement * element)
{
  GstDecklinkSrc *decklinksrc = GST_DECKLINK_SRC (element);
  DeckLinkCaptureDelegate *delegate;
  BMDAudioSampleType sample_depth;
  int channels;
  HRESULT ret;
  const GstDecklinkMode *mode;
  IDeckLinkConfiguration *config;
  BMDVideoConnection conn;
  BMDAudioConnection aconn;

  GST_DEBUG_OBJECT (decklinksrc, "start");

  decklinksrc->decklink = gst_decklink_get_nth_device (decklinksrc->device_number);
  if (decklinksrc->decklink == NULL) {
    return FALSE;
  }

  decklinksrc->input = gst_decklink_get_nth_input (decklinksrc->device_number);
  if (decklinksrc->input == NULL) {
    GST_ERROR ("no input source for device %i", decklinksrc->device_number);
    return FALSE;
  }

  delegate = new DeckLinkCaptureDelegate ();
  delegate->priv = decklinksrc;
  ret = decklinksrc->input->SetCallback (delegate);
  if (ret != S_OK) {
    GST_ERROR ("set callback failed (input source)");
    return FALSE;
  }

  decklinksrc->config = gst_decklink_get_nth_config (decklinksrc->device_number);
  config = decklinksrc->config;
  if (decklinksrc->config == NULL) {
    GST_ERROR ("no config for device %i", decklinksrc->device_number);
    return FALSE;
  }

  switch (decklinksrc->connection) {
    default:
    case GST_DECKLINK_CONNECTION_SDI:
      conn = bmdVideoConnectionSDI;
      aconn = bmdAudioConnectionEmbedded;
      break;
    case GST_DECKLINK_CONNECTION_HDMI:
      conn = bmdVideoConnectionHDMI;
      aconn = bmdAudioConnectionEmbedded;
      break;
    case GST_DECKLINK_CONNECTION_OPTICAL_SDI:
      conn = bmdVideoConnectionOpticalSDI;
      aconn = bmdAudioConnectionEmbedded;
      break;
    case GST_DECKLINK_CONNECTION_COMPONENT:
      conn = bmdVideoConnectionComponent;
      aconn = bmdAudioConnectionAnalog;
      break;
    case GST_DECKLINK_CONNECTION_COMPOSITE:
      conn = bmdVideoConnectionComposite;
      aconn = bmdAudioConnectionAnalog;
      break;
    case GST_DECKLINK_CONNECTION_SVIDEO:
      conn = bmdVideoConnectionSVideo;
      aconn = bmdAudioConnectionAnalog;
      break;
  }

  ret = config->SetInt (bmdDeckLinkConfigVideoInputConnection, conn);
  if (ret != S_OK) {
    GST_ERROR ("set configuration (input source)");
    return FALSE;
  }

  if (decklinksrc->connection == GST_DECKLINK_CONNECTION_COMPOSITE) {
    ret = config->SetInt (bmdDeckLinkConfigAnalogVideoInputFlags,
        bmdAnalogVideoFlagCompositeSetup75);
    if (ret != S_OK) {
      GST_ERROR ("set configuration (composite setup)");
      return FALSE;
    }
  }

  switch (decklinksrc->audio_connection) {
    default:
    case GST_DECKLINK_AUDIO_CONNECTION_AUTO:
      /* set above */
      break;
    case GST_DECKLINK_AUDIO_CONNECTION_EMBEDDED:
      aconn = bmdAudioConnectionEmbedded;
      break;
    case GST_DECKLINK_AUDIO_CONNECTION_AES_EBU:
      aconn = bmdAudioConnectionAESEBU;
      break;
    case GST_DECKLINK_AUDIO_CONNECTION_ANALOG:
      aconn = bmdAudioConnectionAnalog;
      break;
  }
  ret = config->SetInt (bmdDeckLinkConfigAudioInputConnection, aconn);
  if (ret != S_OK) {
    GST_ERROR ("set configuration (audio input connection)");
    return FALSE;
  }

  mode = gst_decklink_get_mode (decklinksrc->mode);

  ret = decklinksrc->input->EnableVideoInput (mode->mode, bmdFormat8BitYUV, 0);
  if (ret != S_OK) {
    GST_ERROR ("enable video input failed");
    return FALSE;
  }

  sample_depth = bmdAudioSampleType16bitInteger;
  channels = 2;
  ret = decklinksrc->input->EnableAudioInput (bmdAudioSampleRate48kHz,
      sample_depth, channels);
  if (ret != S_OK) {
    GST_ERROR ("enable video input failed");
    return FALSE;
  }

  ret = decklinksrc->input->StartStreams ();
  if (ret != S_OK) {
    GST_ERROR ("start streams failed");
    return FALSE;
  }

  g_rec_mutex_lock (&decklinksrc->task_mutex);
  gst_task_start (decklinksrc->task);
  g_rec_mutex_unlock (&decklinksrc->task_mutex);

  return TRUE;
}

static gboolean
gst_decklink_src_stop (GstElement * element)
{
  GstDecklinkSrc *decklinksrc = GST_DECKLINK_SRC (element);

  gst_task_stop (decklinksrc->task);

  g_mutex_lock (&decklinksrc->mutex);
  decklinksrc->stop = TRUE;
  g_cond_signal (&decklinksrc->cond);
  g_mutex_unlock (&decklinksrc->mutex);

  gst_task_join (decklinksrc->task);

  decklinksrc->input->StopStreams ();
  decklinksrc->input->DisableVideoInput ();
  decklinksrc->input->DisableAudioInput ();

  g_list_free_full (decklinksrc->pending_events,
      (GDestroyNotify) gst_mini_object_unref);
  decklinksrc->pending_events = NULL;
  decklinksrc->have_events = FALSE;
  decklinksrc->pending_eos = FALSE;

  return TRUE;
}

static GstStateChangeReturn
gst_decklink_src_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  gboolean no_preroll = FALSE;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_decklink_src_start (element)) {
        ret = GST_STATE_CHANGE_FAILURE;
        goto out;
      }
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      no_preroll = TRUE;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      no_preroll = TRUE;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_decklink_src_stop (element);
      break;
    default:
      break;
  }

  if (no_preroll && ret == GST_STATE_CHANGE_SUCCESS)
    ret = GST_STATE_CHANGE_NO_PREROLL;

out:
  return ret;
}

static gboolean
gst_decklink_src_audio_src_query (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  gboolean res;

  GST_DEBUG_OBJECT (pad, "query: %" GST_PTR_FORMAT, query);

  switch (GST_QUERY_TYPE (query)) {
    /* FIXME: report live-ness and latency for audio too */
    case GST_QUERY_LATENCY:
      GST_FIXME_OBJECT (parent, "should report live-ness and audio latency");
      res = gst_pad_query_default (pad, parent, query);
      break;
    default:
      res = gst_pad_query_default (pad, parent, query);
      break;
  }

  return res;
}

static gboolean
gst_decklink_src_video_src_query (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  GstDecklinkSrc *decklinksrc;
  gboolean ret = FALSE;

  decklinksrc = GST_DECKLINK_SRC (parent);

  GST_DEBUG_OBJECT (pad, "query: %" GST_PTR_FORMAT, query);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:{
      GstClockTime min_latency, max_latency;
      const GstDecklinkMode *mode;

      /* device must be open */
      if (decklinksrc->decklink == NULL) {
        GST_WARNING_OBJECT (decklinksrc,
            "Can't give latency since device isn't open !");
        goto done;
      }

      mode = gst_decklink_get_mode (decklinksrc->mode);

      /* min latency is the time to capture one frame */
      min_latency =
          gst_util_uint64_scale_int (GST_SECOND, mode->fps_d, mode->fps_n);

      /* max latency is total duration of the frame buffer */
      max_latency = 2 * min_latency;

      GST_DEBUG_OBJECT (decklinksrc,
          "report latency min %" GST_TIME_FORMAT " max %" GST_TIME_FORMAT,
          GST_TIME_ARGS (min_latency), GST_TIME_ARGS (max_latency));

      /* we are always live, the min latency is 1 frame and the max latency is
       * the complete buffer of frames. */
      gst_query_set_latency (query, TRUE, min_latency, max_latency);

      ret = TRUE;
      break;
    }
    default:
      ret = gst_pad_query_default (pad, parent, query);
      break;
  }

done:
  return ret;
}

static void
video_frame_free (void *data)
{
  VideoFrame *video_frame = (VideoFrame *) data;

  video_frame->frame->Release ();
  video_frame->input->Release ();
  g_free (video_frame);
}

static void
gst_decklink_src_send_initial_events (GstDecklinkSrc * src)
{
  GstSegment segment;
  GstEvent *event;
  guint group_id;
  guint32 audio_id, video_id;
  gchar stream_id[9];

  /* stream-start */
  audio_id = g_random_int ();
  video_id = g_random_int ();
  while (video_id == audio_id)
    video_id = g_random_int ();

  group_id = gst_util_group_id_next ();
  g_snprintf (stream_id, sizeof (stream_id), "%08x", audio_id);
  event = gst_event_new_stream_start (stream_id);
  gst_event_set_group_id (event, group_id);
  gst_pad_push_event (src->audiosrcpad, event);

  g_snprintf (stream_id, sizeof (stream_id), "%08x", video_id);
  event = gst_event_new_stream_start (stream_id);
  gst_event_set_group_id (event, group_id);
  gst_pad_push_event (src->videosrcpad, event);

  /* caps */
  gst_pad_push_event (src->audiosrcpad,
      gst_event_new_caps (gst_caps_new_simple ("audio/x-raw",
          "format", G_TYPE_STRING, "S16LE", "channels", G_TYPE_INT, 2,
          "rate", G_TYPE_INT, 48000, "layout", G_TYPE_STRING, "interleaved",
          NULL)));

  gst_pad_push_event (src->videosrcpad,
      gst_event_new_caps (gst_decklink_mode_get_caps (src->mode)));

  /* segment */
  gst_segment_init (&segment, GST_FORMAT_TIME);
  event = gst_event_new_segment (&segment);
  gst_pad_push_event (src->videosrcpad, gst_event_ref (event));
  gst_pad_push_event (src->audiosrcpad, event);
}

static void
gst_decklink_src_task (void *priv)
{
  GstDecklinkSrc *decklinksrc = GST_DECKLINK_SRC (priv);
  GstBuffer *buffer;
  GstBuffer *audio_buffer;
  IDeckLinkVideoInputFrame *video_frame;
  IDeckLinkAudioInputPacket *audio_frame;
  void *data;
  gsize data_size;
  int n_samples;
  GstFlowReturn video_flow, audio_flow, flow;
  const GstDecklinkMode *mode;
  gboolean discont = FALSE;

  GST_DEBUG_OBJECT (decklinksrc, "task");

  g_mutex_lock (&decklinksrc->mutex);
  while (decklinksrc->video_frame == NULL && !decklinksrc->stop &&
      !decklinksrc->pending_eos) {
    g_cond_wait (&decklinksrc->cond, &decklinksrc->mutex);
  }
  video_frame = decklinksrc->video_frame;
  audio_frame = decklinksrc->audio_frame;
  decklinksrc->video_frame = NULL;
  decklinksrc->audio_frame = NULL;
  g_mutex_unlock (&decklinksrc->mutex);

  if (decklinksrc->stop) {
    if (video_frame)
      video_frame->Release ();
    if (audio_frame)
      audio_frame->Release ();
    GST_DEBUG ("stopping task");
    return;
  }

  if (g_atomic_int_compare_and_exchange (&decklinksrc->pending_eos, TRUE,
      FALSE)) {
    GST_INFO_OBJECT (decklinksrc, "EOS pending");
    flow = GST_FLOW_EOS;
    goto pause;
  }

  /* warning on dropped frames */
  /* FIXME: post QoS message */
  if (decklinksrc->dropped_frames - decklinksrc->dropped_frames_old > 0) {
    GST_ELEMENT_WARNING (decklinksrc, RESOURCE, READ,
        ("Dropped %d frame(s), for a total of %d frame(s)",
            decklinksrc->dropped_frames - decklinksrc->dropped_frames_old,
            decklinksrc->dropped_frames), (NULL));
    decklinksrc->dropped_frames_old = decklinksrc->dropped_frames;
    /* FIXME: discont = TRUE; ? */
  }

  if (!decklinksrc->started) {
    gst_decklink_src_send_initial_events (decklinksrc);
    decklinksrc->started = TRUE;
  }

  if (g_atomic_int_get (&decklinksrc->have_events)) {
    GList *l;

    GST_OBJECT_LOCK (decklinksrc);
    for (l = decklinksrc->pending_events; l != NULL; l = l->next) {
      GstEvent *event = GST_EVENT (l->data);

      GST_DEBUG_OBJECT (decklinksrc, "pushing %s event",
          GST_EVENT_TYPE_NAME (event));
      gst_pad_push_event (decklinksrc->videosrcpad, gst_event_ref (event));
      gst_pad_push_event (decklinksrc->audiosrcpad, event);
      l->data = NULL;
    }
    g_list_free (decklinksrc->pending_events);
    decklinksrc->pending_events = NULL;
    g_atomic_int_set (&decklinksrc->have_events, FALSE);
    GST_OBJECT_UNLOCK (decklinksrc);
  }

  mode = gst_decklink_get_mode (decklinksrc->mode);

  video_frame->GetBytes (&data);

  data_size = mode->width * mode->height * 2;

  if (decklinksrc->copy_data) {
    buffer = gst_buffer_new_and_alloc (data_size);

    gst_buffer_fill (buffer, 0, data, data_size);

    video_frame->Release ();
  } else {
    VideoFrame *vf;

    vf = (VideoFrame *) g_malloc0 (sizeof (VideoFrame));

    buffer = gst_buffer_new_wrapped_full ((GstMemoryFlags) 0, data, data_size,
        0, data_size, vf, (GDestroyNotify) video_frame_free);

    vf->frame = video_frame;
    vf->input = decklinksrc->input;
    vf->input->AddRef ();
  }

  GST_BUFFER_TIMESTAMP (buffer) =
      gst_util_uint64_scale_int (decklinksrc->frame_num * GST_SECOND,
      mode->fps_d, mode->fps_n);
  GST_BUFFER_DURATION (buffer) =
      gst_util_uint64_scale_int ((decklinksrc->frame_num + 1) * GST_SECOND,
      mode->fps_d, mode->fps_n) - GST_BUFFER_TIMESTAMP (buffer);
  GST_BUFFER_OFFSET (buffer) = decklinksrc->frame_num;
  GST_BUFFER_OFFSET_END (buffer) = decklinksrc->frame_num; /* FIXME: +1? */

  /* FIXME: set video meta */

  if (decklinksrc->frame_num == 0)
    discont = TRUE;

  if (discont)
    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DISCONT);
  else
    GST_BUFFER_FLAG_UNSET (buffer, GST_BUFFER_FLAG_DISCONT);

  video_flow = gst_pad_push (decklinksrc->videosrcpad, buffer);

  if (gst_pad_is_linked (decklinksrc->audiosrcpad)) {
    n_samples = audio_frame->GetSampleFrameCount ();
    audio_frame->GetBytes (&data);
    audio_buffer = gst_buffer_new_and_alloc (n_samples * 2 * 2);
    gst_buffer_fill (audio_buffer, 0, data, n_samples * 2 * 2);

    GST_BUFFER_TIMESTAMP (audio_buffer) =
        gst_util_uint64_scale_int (decklinksrc->num_audio_samples * GST_SECOND,
        1, 48000);
    /* FIXME: should be next_timestamp - timestamp for perfect stream */
    GST_BUFFER_DURATION (audio_buffer) =
        gst_util_uint64_scale_int (n_samples * GST_SECOND, 1, 48000);
    GST_BUFFER_OFFSET (audio_buffer) = decklinksrc->num_audio_samples;
    GST_BUFFER_OFFSET_END (audio_buffer) =
        GST_BUFFER_OFFSET (audio_buffer) + n_samples;

    decklinksrc->num_audio_samples += n_samples;

    if (discont)
      GST_BUFFER_FLAG_SET (audio_buffer, GST_BUFFER_FLAG_DISCONT);
    else
      GST_BUFFER_FLAG_UNSET (audio_buffer, GST_BUFFER_FLAG_DISCONT);

    audio_flow = gst_pad_push (decklinksrc->audiosrcpad, audio_buffer);
  } else {
    audio_flow = GST_FLOW_NOT_LINKED;
  }

  if (audio_flow == GST_FLOW_NOT_LINKED)
    flow = video_flow;
  else if (video_flow == GST_FLOW_NOT_LINKED)
    flow = audio_flow;
  else if (video_flow == GST_FLOW_FLUSHING || audio_flow == GST_FLOW_FLUSHING)
    flow = GST_FLOW_FLUSHING;
  else if (video_flow < GST_FLOW_EOS)
    flow = video_flow;
  else if (audio_flow < GST_FLOW_EOS)
    flow = audio_flow;
  else if (video_flow == GST_FLOW_EOS || audio_flow == GST_FLOW_EOS)
    flow = GST_FLOW_EOS;
  else
    flow = video_flow;

  if (flow != GST_FLOW_OK)
    goto pause;

done:

  if (audio_frame)
    audio_frame->Release ();

  return;

pause:
  {
    const gchar *reason = gst_flow_get_name (flow);
    GstEvent *event = NULL;

    GST_DEBUG_OBJECT (decklinksrc, "pausing task, reason %s", reason);
    gst_task_pause (decklinksrc->task);
    if (flow == GST_FLOW_EOS) {
      /* perform EOS logic (very crude, we don't even keep a GstSegment) */
      event = gst_event_new_eos ();
    } else if (flow == GST_FLOW_NOT_LINKED || flow < GST_FLOW_EOS) {
      event = gst_event_new_eos ();
      /* for fatal errors we post an error message, post the error
       * first so the app knows about the error first.
       * Also don't do this for FLUSHING because it happens
       * due to flushing and posting an error message because of
       * that is the wrong thing to do, e.g. when we're doing
       * a flushing seek. */
      GST_ELEMENT_ERROR (decklinksrc, STREAM, FAILED,
          ("Internal data flow error."),
          ("streaming task paused, reason %s (%d)", reason, flow));
    }
    if (event != NULL) {
      GST_INFO_OBJECT (decklinksrc->videosrcpad, "pushing EOS event");
      gst_pad_push_event (decklinksrc->videosrcpad, gst_event_ref (event));
      GST_INFO_OBJECT (decklinksrc->audiosrcpad, "pushing EOS event");
      gst_pad_push_event (decklinksrc->audiosrcpad, event);
    }
    goto done;
  }
}

#if 0
/* former device probe code, redux */
static void
gst_decklinksrc_list_devices (void)
{
  IDeckLinkIterator *iterator;
  IDeckLink *decklink;
  int n_devices;

  n_devices = 0;
  iterator = CreateDeckLinkIteratorInstance ();
  if (iterator) {
    while (iterator->Next (&decklink) == S_OK) {
      n_devices++;
    }
  }
  iterator->Release();

  g_print ("%d devices\n", n_devices);
}
#endif
