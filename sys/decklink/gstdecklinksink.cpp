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
 * gst-launch -v videotestsrc ! decklinksink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/interfaces/propertyprobe.h>
#include "gstdecklink.h"
#include "gstdecklinksink.h"
#include <string.h>


GST_DEBUG_CATEGORY_STATIC (gst_decklink_sink_debug_category);
#define GST_CAT_DEFAULT gst_decklink_sink_debug_category

/* prototypes */


static void gst_decklink_sink_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_decklink_sink_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_decklink_sink_dispose (GObject * object);
static void gst_decklink_sink_finalize (GObject * object);

static GstStateChangeReturn
gst_decklink_sink_change_state (GstElement * element,
    GstStateChange transition);
static GstClock *gst_decklink_sink_provide_clock (GstElement * element);
static gboolean gst_decklink_sink_set_clock (GstElement * element,
    GstClock * clock);
static GstIndex *gst_decklink_sink_get_index (GstElement * element);
static void gst_decklink_sink_set_index (GstElement * element,
    GstIndex * index);
static gboolean gst_decklink_sink_send_event (GstElement * element,
    GstEvent * event);
static gboolean gst_decklink_sink_query (GstElement * element,
    GstQuery * query);

static GstCaps *gst_decklink_sink_videosink_getcaps (GstPad * pad);
static gboolean gst_decklink_sink_videosink_setcaps (GstPad * pad,
    GstCaps * caps);
static gboolean gst_decklink_sink_videosink_acceptcaps (GstPad * pad,
    GstCaps * caps);
static gboolean gst_decklink_sink_videosink_activate (GstPad * pad);
static gboolean gst_decklink_sink_videosink_activatepush (GstPad * pad,
    gboolean active);
static gboolean gst_decklink_sink_videosink_activatepull (GstPad * pad,
    gboolean active);
static GstPadLinkReturn gst_decklink_sink_videosink_link (GstPad * pad,
    GstPad * peer);
static GstFlowReturn gst_decklink_sink_videosink_chain (GstPad * pad,
    GstBuffer * buffer);
static GstFlowReturn gst_decklink_sink_videosink_chainlist (GstPad * pad,
    GstBufferList * bufferlist);
static gboolean gst_decklink_sink_videosink_event (GstPad * pad,
    GstEvent * event);
static gboolean gst_decklink_sink_videosink_query (GstPad * pad,
    GstQuery * query);
static GstFlowReturn gst_decklink_sink_videosink_bufferalloc (GstPad * pad,
    guint64 offset, guint size, GstCaps * caps, GstBuffer ** buf);
static GstIterator *gst_decklink_sink_videosink_iterintlink (GstPad * pad);


static GstCaps *gst_decklink_sink_audiosink_getcaps (GstPad * pad);
static gboolean gst_decklink_sink_audiosink_setcaps (GstPad * pad,
    GstCaps * caps);
static gboolean gst_decklink_sink_audiosink_acceptcaps (GstPad * pad,
    GstCaps * caps);
static gboolean gst_decklink_sink_audiosink_activate (GstPad * pad);
static gboolean gst_decklink_sink_audiosink_activatepush (GstPad * pad,
    gboolean active);
static gboolean gst_decklink_sink_audiosink_activatepull (GstPad * pad,
    gboolean active);
static GstPadLinkReturn gst_decklink_sink_audiosink_link (GstPad * pad,
    GstPad * peer);
static GstFlowReturn gst_decklink_sink_audiosink_chain (GstPad * pad,
    GstBuffer * buffer);
static GstFlowReturn gst_decklink_sink_audiosink_chainlist (GstPad * pad,
    GstBufferList * bufferlist);
static gboolean gst_decklink_sink_audiosink_event (GstPad * pad,
    GstEvent * event);
static gboolean gst_decklink_sink_audiosink_query (GstPad * pad,
    GstQuery * query);
static GstFlowReturn gst_decklink_sink_audiosink_bufferalloc (GstPad * pad,
    guint64 offset, guint size, GstCaps * caps, GstBuffer ** buf);
static GstIterator *gst_decklink_sink_audiosink_iterintlink (GstPad * pad);

static void
gst_decklink_sink_property_probe_interface_init (GstPropertyProbeInterface *
    iface);

#ifdef _MSC_VER
/* COM initialization/uninitialization thread */
static void gst_decklink_sink_com_thread (GstDecklinkSink * sink);
#endif /* _MSC_VER */

enum
{
  PROP_0,
  PROP_MODE,
  PROP_DEVICE
};

/* pad templates */

/* the video sink pad template is created on the fly */

static GstStaticPadTemplate gst_decklink_sink_audiosink_template =
GST_STATIC_PAD_TEMPLATE ("audiosink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int,width=16,depth=16,channels=2,rate=48000")
    );


/* class initialization */

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_decklink_sink_debug_category, "decklinksink", 0, \
      "debug category for decklinksink element");

static void
gst_decklink_sink_init_interfaces (GType type)
{
  static const GInterfaceInfo decklink_sink_propertyprobe_info = {
    (GInterfaceInitFunc) gst_decklink_sink_property_probe_interface_init,
    NULL,
    NULL,
  };

  GST_DEBUG_CATEGORY_INIT (gst_decklink_sink_debug_category, "decklinksink", 0,
      "debug category for decklinksink element");

  g_type_add_interface_static (type, GST_TYPE_PROPERTY_PROBE,
      &decklink_sink_propertyprobe_info);
}


GST_BOILERPLATE_FULL (GstDecklinkSink, gst_decklink_sink, GstElement,
    GST_TYPE_ELEMENT, gst_decklink_sink_init_interfaces);

static void
gst_decklink_sink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstPadTemplate *pad_template;

  pad_template =
      gst_pad_template_new ("videosink", GST_PAD_SINK, GST_PAD_ALWAYS,
      gst_decklink_mode_get_template_caps ());
  gst_element_class_add_pad_template (element_class, pad_template);
  gst_object_unref (pad_template);
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_decklink_sink_audiosink_template));

  gst_element_class_set_metadata (element_class, "Decklink Sink",
      "Video/Sink", "Decklink Sink", "David Schleef <ds@entropywave.com>");
}

static void
gst_decklink_sink_class_init (GstDecklinkSinkClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_decklink_sink_set_property;
  gobject_class->get_property = gst_decklink_sink_get_property;
  gobject_class->dispose = gst_decklink_sink_dispose;
  gobject_class->finalize = gst_decklink_sink_finalize;
  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_decklink_sink_change_state);
  element_class->provide_clock =
      GST_DEBUG_FUNCPTR (gst_decklink_sink_provide_clock);
  element_class->set_clock = GST_DEBUG_FUNCPTR (gst_decklink_sink_set_clock);
  element_class->get_index = GST_DEBUG_FUNCPTR (gst_decklink_sink_get_index);
  element_class->set_index = GST_DEBUG_FUNCPTR (gst_decklink_sink_set_index);
  element_class->send_event = GST_DEBUG_FUNCPTR (gst_decklink_sink_send_event);
  element_class->query = GST_DEBUG_FUNCPTR (gst_decklink_sink_query);

  g_object_class_install_property (gobject_class, PROP_MODE,
      g_param_spec_enum ("mode", "Playback Mode",
          "Video Mode to use for playback",
          GST_TYPE_DECKLINK_MODE, GST_DECKLINK_MODE_NTSC,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              G_PARAM_CONSTRUCT)));

  g_object_class_install_property (gobject_class, PROP_DEVICE,
      g_param_spec_int ("device", "Device", "Capture device instance to use",
          0, G_MAXINT, 0,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              G_PARAM_CONSTRUCT)));

}

static void
gst_decklink_sink_init (GstDecklinkSink * decklinksink,
    GstDecklinkSinkClass * decklinksink_class)
{

  decklinksink->videosinkpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template
      (GST_ELEMENT_CLASS (decklinksink_class), "videosink"), "videosink");
  gst_pad_set_getcaps_function (decklinksink->videosinkpad,
      GST_DEBUG_FUNCPTR (gst_decklink_sink_videosink_getcaps));
  gst_pad_set_setcaps_function (decklinksink->videosinkpad,
      GST_DEBUG_FUNCPTR (gst_decklink_sink_videosink_setcaps));
  gst_pad_set_acceptcaps_function (decklinksink->videosinkpad,
      GST_DEBUG_FUNCPTR (gst_decklink_sink_videosink_acceptcaps));
  gst_pad_set_activate_function (decklinksink->videosinkpad,
      GST_DEBUG_FUNCPTR (gst_decklink_sink_videosink_activate));
  gst_pad_set_activatepush_function (decklinksink->videosinkpad,
      GST_DEBUG_FUNCPTR (gst_decklink_sink_videosink_activatepush));
  gst_pad_set_activatepull_function (decklinksink->videosinkpad,
      GST_DEBUG_FUNCPTR (gst_decklink_sink_videosink_activatepull));
  gst_pad_set_link_function (decklinksink->videosinkpad,
      GST_DEBUG_FUNCPTR (gst_decklink_sink_videosink_link));
  gst_pad_set_chain_function (decklinksink->videosinkpad,
      GST_DEBUG_FUNCPTR (gst_decklink_sink_videosink_chain));
  gst_pad_set_chain_list_function (decklinksink->videosinkpad,
      GST_DEBUG_FUNCPTR (gst_decklink_sink_videosink_chainlist));
  gst_pad_set_event_function (decklinksink->videosinkpad,
      GST_DEBUG_FUNCPTR (gst_decklink_sink_videosink_event));
  gst_pad_set_query_function (decklinksink->videosinkpad,
      GST_DEBUG_FUNCPTR (gst_decklink_sink_videosink_query));
  gst_pad_set_bufferalloc_function (decklinksink->videosinkpad,
      GST_DEBUG_FUNCPTR (gst_decklink_sink_videosink_bufferalloc));
  gst_pad_set_iterate_internal_links_function (decklinksink->videosinkpad,
      GST_DEBUG_FUNCPTR (gst_decklink_sink_videosink_iterintlink));
  gst_element_add_pad (GST_ELEMENT (decklinksink), decklinksink->videosinkpad);



  decklinksink->audiosinkpad =
      gst_pad_new_from_static_template (&gst_decklink_sink_audiosink_template,
      "audiosink");
  gst_pad_set_getcaps_function (decklinksink->audiosinkpad,
      GST_DEBUG_FUNCPTR (gst_decklink_sink_audiosink_getcaps));
  gst_pad_set_setcaps_function (decklinksink->audiosinkpad,
      GST_DEBUG_FUNCPTR (gst_decklink_sink_audiosink_setcaps));
  gst_pad_set_acceptcaps_function (decklinksink->audiosinkpad,
      GST_DEBUG_FUNCPTR (gst_decklink_sink_audiosink_acceptcaps));
  gst_pad_set_activate_function (decklinksink->audiosinkpad,
      GST_DEBUG_FUNCPTR (gst_decklink_sink_audiosink_activate));
  gst_pad_set_activatepush_function (decklinksink->audiosinkpad,
      GST_DEBUG_FUNCPTR (gst_decklink_sink_audiosink_activatepush));
  gst_pad_set_activatepull_function (decklinksink->audiosinkpad,
      GST_DEBUG_FUNCPTR (gst_decklink_sink_audiosink_activatepull));
  gst_pad_set_link_function (decklinksink->audiosinkpad,
      GST_DEBUG_FUNCPTR (gst_decklink_sink_audiosink_link));
  gst_pad_set_chain_function (decklinksink->audiosinkpad,
      GST_DEBUG_FUNCPTR (gst_decklink_sink_audiosink_chain));
  gst_pad_set_chain_list_function (decklinksink->audiosinkpad,
      GST_DEBUG_FUNCPTR (gst_decklink_sink_audiosink_chainlist));
  gst_pad_set_event_function (decklinksink->audiosinkpad,
      GST_DEBUG_FUNCPTR (gst_decklink_sink_audiosink_event));
  gst_pad_set_query_function (decklinksink->audiosinkpad,
      GST_DEBUG_FUNCPTR (gst_decklink_sink_audiosink_query));
  gst_pad_set_bufferalloc_function (decklinksink->audiosinkpad,
      GST_DEBUG_FUNCPTR (gst_decklink_sink_audiosink_bufferalloc));
  gst_pad_set_iterate_internal_links_function (decklinksink->audiosinkpad,
      GST_DEBUG_FUNCPTR (gst_decklink_sink_audiosink_iterintlink));
  gst_element_add_pad (GST_ELEMENT (decklinksink), decklinksink->audiosinkpad);

  GST_OBJECT_FLAG_SET (decklinksink, GST_ELEMENT_IS_SINK);

  decklinksink->cond = g_cond_new ();
  decklinksink->mutex = g_mutex_new ();
  decklinksink->audio_mutex = g_mutex_new ();
  decklinksink->audio_cond = g_cond_new ();

  decklinksink->mode = GST_DECKLINK_MODE_NTSC;
  decklinksink->device = 0;

  decklinksink->callback = new Output;
  decklinksink->callback->decklinksink = decklinksink;

#ifdef _MSC_VER
  decklinksink->com_init_lock = g_mutex_new ();
  decklinksink->com_deinit_lock = g_mutex_new ();
  decklinksink->com_initialized = g_cond_new ();
  decklinksink->com_uninitialize = g_cond_new ();
  decklinksink->com_uninitialized = g_cond_new ();

  g_mutex_lock (decklinksink->com_init_lock);

  /* create the COM initialization thread */
  g_thread_create ((GThreadFunc) gst_decklink_sink_com_thread,
      decklinksink, FALSE, NULL);

  /* wait until the COM thread signals that COM has been initialized */
  g_cond_wait (decklinksink->com_initialized, decklinksink->com_init_lock);
  g_mutex_unlock (decklinksink->com_init_lock);
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
    case PROP_DEVICE:
      decklinksink->device = g_value_get_int (value);
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
    case PROP_DEVICE:
      g_value_set_int (value, decklinksink->device);
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
gst_decklink_sink_dispose (GObject * object)
{
  g_return_if_fail (GST_IS_DECKLINK_SINK (object));

  /* clean up as possible.  may be called multiple times */

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

void
gst_decklink_sink_finalize (GObject * object)
{
  GstDecklinkSink *decklinksink;

  g_return_if_fail (GST_IS_DECKLINK_SINK (object));
  decklinksink = GST_DECKLINK_SINK (object);

  /* clean up object here */
  g_cond_free (decklinksink->cond);
  g_mutex_free (decklinksink->mutex);
  g_cond_free (decklinksink->audio_cond);
  g_mutex_free (decklinksink->audio_mutex);

  delete decklinksink->callback;

#ifdef _MSC_VER
  /* signal the COM thread that it should uninitialize COM */
  if (decklinksink->comInitialized) {
    g_mutex_lock (decklinksink->com_deinit_lock);
    g_cond_signal (decklinksink->com_uninitialize);
    g_cond_wait (decklinksink->com_uninitialized,
        decklinksink->com_deinit_lock);
    g_mutex_unlock (decklinksink->com_deinit_lock);
  }

  g_mutex_free (decklinksink->com_init_lock);
  g_mutex_free (decklinksink->com_deinit_lock);
  g_cond_free (decklinksink->com_initialized);
  g_cond_free (decklinksink->com_uninitialize);
  g_cond_free (decklinksink->com_uninitialized);
#endif /* _MSC_VER */

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_decklink_sink_start (GstDecklinkSink * decklinksink)
{
  HRESULT ret;
  const GstDecklinkMode *mode;
  BMDAudioSampleType sample_depth;

  decklinksink->decklink = gst_decklink_get_nth_device (decklinksink->device);
  if (!decklinksink->decklink) {
    GST_WARNING ("failed to get device %d", decklinksink->device);
    return FALSE;
  }

  ret = decklinksink->decklink->QueryInterface (IID_IDeckLinkOutput,
      (void **) &decklinksink->output);
  if (ret != S_OK) {
    GST_WARNING ("selected device does not have output interface");
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
  g_mutex_lock (decklinksink->mutex);
  decklinksink->stop = TRUE;
  g_cond_signal (decklinksink->cond);
  g_mutex_unlock (decklinksink->mutex);

  g_mutex_lock (decklinksink->audio_mutex);
  g_cond_signal (decklinksink->audio_cond);
  g_mutex_unlock (decklinksink->audio_mutex);

  return TRUE;
}

static gboolean
gst_decklink_sink_stop (GstDecklinkSink * decklinksink)
{
  decklinksink->output->StopScheduledPlayback (0, NULL, 0);
  decklinksink->output->DisableAudioOutput ();
  decklinksink->output->DisableVideoOutput ();

  decklinksink->output->Release ();
  decklinksink->output = NULL;
  decklinksink->decklink->Release ();
  decklinksink->decklink = NULL;

  return TRUE;
}

static GstStateChangeReturn
gst_decklink_sink_change_state (GstElement * element, GstStateChange transition)
{
  GstDecklinkSink *decklinksink;
  GstStateChangeReturn ret;

  g_return_val_if_fail (GST_IS_DECKLINK_SINK (element),
      GST_STATE_CHANGE_FAILURE);
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

static GstClock *
gst_decklink_sink_provide_clock (GstElement * element)
{

  return NULL;
}

static gboolean
gst_decklink_sink_set_clock (GstElement * element, GstClock * clock)
{

  return GST_ELEMENT_CLASS (parent_class)->set_clock (element, clock);
}

static GstIndex *
gst_decklink_sink_get_index (GstElement * element)
{

  return NULL;
}

static void
gst_decklink_sink_set_index (GstElement * element, GstIndex * index)
{

}

static gboolean
gst_decklink_sink_send_event (GstElement * element, GstEvent * event)
{

  return TRUE;
}

static gboolean
gst_decklink_sink_query (GstElement * element, GstQuery * query)
{

  return FALSE;
}

static GstCaps *
gst_decklink_sink_videosink_getcaps (GstPad * pad)
{
  GstDecklinkSink *decklinksink;
  GstCaps *caps;

  decklinksink = GST_DECKLINK_SINK (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (decklinksink, "getcaps");

  caps = gst_decklink_mode_get_caps (decklinksink->mode);

  gst_object_unref (decklinksink);
  return caps;
}

static gboolean
gst_decklink_sink_videosink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstDecklinkSink *decklinksink;
  gboolean ret;
  GstVideoFormat format;
  int width;
  int height;

  decklinksink = GST_DECKLINK_SINK (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (decklinksink, "setcaps");

  ret = gst_video_format_parse_caps (caps, &format, &width, &height);
  if (ret) {
    if (format == GST_VIDEO_FORMAT_v210) {
      decklinksink->pixel_format = bmdFormat10BitYUV;
    } else {
      decklinksink->pixel_format = bmdFormat8BitYUV;
    }
  }


  gst_object_unref (decklinksink);
  return TRUE;
}

static gboolean
gst_decklink_sink_videosink_acceptcaps (GstPad * pad, GstCaps * caps)
{
  GstDecklinkSink *decklinksink;

  decklinksink = GST_DECKLINK_SINK (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (decklinksink, "acceptcaps");


  gst_object_unref (decklinksink);
  return TRUE;
}

static gboolean
gst_decklink_sink_videosink_activate (GstPad * pad)
{
  GstDecklinkSink *decklinksink;
  gboolean ret;

  decklinksink = GST_DECKLINK_SINK (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (decklinksink, "activate");

  if (gst_pad_check_pull_range (pad)) {
    GST_DEBUG_OBJECT (pad, "activating pull");
    ret = gst_pad_activate_pull (pad, TRUE);
  } else {
    GST_DEBUG_OBJECT (pad, "activating push");
    ret = gst_pad_activate_push (pad, TRUE);
  }

  gst_object_unref (decklinksink);
  return ret;
}

static gboolean
gst_decklink_sink_videosink_activatepush (GstPad * pad, gboolean active)
{
  GstDecklinkSink *decklinksink;

  decklinksink = GST_DECKLINK_SINK (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (decklinksink, "activatepush");


  gst_object_unref (decklinksink);
  return TRUE;
}

static gboolean
gst_decklink_sink_videosink_activatepull (GstPad * pad, gboolean active)
{
  GstDecklinkSink *decklinksink;

  decklinksink = GST_DECKLINK_SINK (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (decklinksink, "activatepull");


  gst_object_unref (decklinksink);
  return TRUE;
}

static GstPadLinkReturn
gst_decklink_sink_videosink_link (GstPad * pad, GstPad * peer)
{
  GstDecklinkSink *decklinksink;

  decklinksink = GST_DECKLINK_SINK (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (decklinksink, "link");


  gst_object_unref (decklinksink);
  return GST_PAD_LINK_OK;
}

static GstFlowReturn
gst_decklink_sink_videosink_chain (GstPad * pad, GstBuffer * buffer)
{
  GstDecklinkSink *decklinksink;
  IDeckLinkMutableVideoFrame *frame;
  void *data;
  GstFlowReturn ret;
  const GstDecklinkMode *mode;

  decklinksink = GST_DECKLINK_SINK (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (decklinksink, "chain");

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

  decklinksink->output->CreateVideoFrame (mode->width,
      mode->height, mode->width * 2, decklinksink->pixel_format,
      bmdFrameFlagDefault, &frame);

  frame->GetBytes (&data);
  memcpy (data, GST_BUFFER_DATA (buffer), GST_BUFFER_SIZE (buffer));
  gst_buffer_unref (buffer);

  g_mutex_lock (decklinksink->mutex);
  while (decklinksink->queued_frames > 2 && !decklinksink->stop) {
    g_cond_wait (decklinksink->cond, decklinksink->mutex);
  }
  if (!decklinksink->stop) {
    decklinksink->queued_frames++;
  }
  g_mutex_unlock (decklinksink->mutex);

  if (!decklinksink->stop) {
    decklinksink->output->ScheduleVideoFrame (frame,
        decklinksink->num_frames * mode->fps_d, mode->fps_d, mode->fps_n);
    decklinksink->num_frames++;

    if (!decklinksink->sched_started) {
      decklinksink->output->StartScheduledPlayback (0, mode->fps_d, 1.0);
      decklinksink->sched_started = TRUE;
    }

    ret = GST_FLOW_OK;
  } else {
    ret = GST_FLOW_FLUSHING;
  }

  frame->Release ();

  gst_object_unref (decklinksink);
  return ret;
}

static GstFlowReturn
gst_decklink_sink_videosink_chainlist (GstPad * pad, GstBufferList * bufferlist)
{
  GstDecklinkSink *decklinksink;

  decklinksink = GST_DECKLINK_SINK (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (decklinksink, "chainlist");


  gst_object_unref (decklinksink);
  return GST_FLOW_OK;
}

static gboolean
gst_decklink_sink_videosink_event (GstPad * pad, GstEvent * event)
{
  gboolean res;
  GstDecklinkSink *decklinksink;

  decklinksink = GST_DECKLINK_SINK (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (decklinksink, "event");

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      decklinksink->video_eos = TRUE;
      decklinksink->video_seqnum = gst_event_get_seqnum (event);
      {
        GstMessage *message;

        message = gst_message_new_eos (GST_OBJECT_CAST (decklinksink));
        gst_message_set_seqnum (message, decklinksink->video_seqnum);
        gst_element_post_message (GST_ELEMENT_CAST (decklinksink), message);
      }

      break;
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }

  gst_object_unref (decklinksink);
  return res;
}

static gboolean
gst_decklink_sink_videosink_query (GstPad * pad, GstQuery * query)
{
  gboolean res;
  GstDecklinkSink *decklinksink;

  decklinksink = GST_DECKLINK_SINK (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (decklinksink, "query");

  switch (GST_QUERY_TYPE (query)) {
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }

  gst_object_unref (decklinksink);
  return res;
}

static GstFlowReturn
gst_decklink_sink_videosink_bufferalloc (GstPad * pad, guint64 offset,
    guint size, GstCaps * caps, GstBuffer ** buf)
{
  GstDecklinkSink *decklinksink;

  decklinksink = GST_DECKLINK_SINK (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (decklinksink, "bufferalloc");


  *buf = gst_buffer_new_and_alloc (size);
  gst_buffer_set_caps (*buf, caps);

  gst_object_unref (decklinksink);
  return GST_FLOW_OK;
}

static GstIterator *
gst_decklink_sink_videosink_iterintlink (GstPad * pad)
{
  GstDecklinkSink *decklinksink;
  GstIterator *iter;

  decklinksink = GST_DECKLINK_SINK (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (decklinksink, "iterintlink");

  iter = gst_pad_iterate_internal_links_default (pad);

  gst_object_unref (decklinksink);
  return iter;
}


static GstCaps *
gst_decklink_sink_audiosink_getcaps (GstPad * pad)
{
  GstDecklinkSink *decklinksink;
  GstCaps *caps;

  decklinksink = GST_DECKLINK_SINK (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (decklinksink, "getcaps");

  caps = gst_caps_copy (gst_pad_get_pad_template_caps (pad));

  gst_object_unref (decklinksink);
  return caps;
}

static gboolean
gst_decklink_sink_audiosink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstDecklinkSink *decklinksink;

  decklinksink = GST_DECKLINK_SINK (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (decklinksink, "setcaps");

  gst_object_unref (decklinksink);
  return TRUE;
}

static gboolean
gst_decklink_sink_audiosink_acceptcaps (GstPad * pad, GstCaps * caps)
{
  GstDecklinkSink *decklinksink;

  decklinksink = GST_DECKLINK_SINK (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (decklinksink, "acceptcaps");


  gst_object_unref (decklinksink);
  return TRUE;
}

static gboolean
gst_decklink_sink_audiosink_activate (GstPad * pad)
{
  GstDecklinkSink *decklinksink;
  gboolean ret;

  decklinksink = GST_DECKLINK_SINK (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (decklinksink, "activate");

  if (gst_pad_check_pull_range (pad)) {
    GST_DEBUG_OBJECT (pad, "activating pull");
    ret = gst_pad_activate_pull (pad, TRUE);
  } else {
    GST_DEBUG_OBJECT (pad, "activating push");
    ret = gst_pad_activate_push (pad, TRUE);
  }

  gst_object_unref (decklinksink);
  return ret;
}

static gboolean
gst_decklink_sink_audiosink_activatepush (GstPad * pad, gboolean active)
{
  GstDecklinkSink *decklinksink;

  decklinksink = GST_DECKLINK_SINK (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (decklinksink, "activatepush");


  gst_object_unref (decklinksink);
  return TRUE;
}

static gboolean
gst_decklink_sink_audiosink_activatepull (GstPad * pad, gboolean active)
{
  GstDecklinkSink *decklinksink;

  decklinksink = GST_DECKLINK_SINK (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (decklinksink, "activatepull");


  gst_object_unref (decklinksink);
  return TRUE;
}

static GstPadLinkReturn
gst_decklink_sink_audiosink_link (GstPad * pad, GstPad * peer)
{
  GstDecklinkSink *decklinksink;

  decklinksink = GST_DECKLINK_SINK (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (decklinksink, "link");


  gst_object_unref (decklinksink);
  return GST_PAD_LINK_OK;
}

static GstFlowReturn
gst_decklink_sink_audiosink_chain (GstPad * pad, GstBuffer * buffer)
{
  GstDecklinkSink *decklinksink;
  GstFlowReturn ret;

  decklinksink = GST_DECKLINK_SINK (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (decklinksink, "chain");

  if (decklinksink->stop) {
    return GST_FLOW_WRONG_STATE;
  }

  g_mutex_lock (decklinksink->audio_mutex);
  while (!decklinksink->stop &&
      gst_adapter_available (decklinksink->audio_adapter) > 1600 * 4 * 2) {
    g_cond_wait (decklinksink->audio_cond, decklinksink->audio_mutex);
  }
  gst_adapter_push (decklinksink->audio_adapter, buffer);
  g_mutex_unlock (decklinksink->audio_mutex);

  gst_object_unref (decklinksink);

  ret = GST_FLOW_OK;
  return ret;
}

static GstFlowReturn
gst_decklink_sink_audiosink_chainlist (GstPad * pad, GstBufferList * bufferlist)
{
  GstDecklinkSink *decklinksink;

  decklinksink = GST_DECKLINK_SINK (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (decklinksink, "chainlist");


  gst_object_unref (decklinksink);
  return GST_FLOW_OK;
}

static gboolean
gst_decklink_sink_audiosink_event (GstPad * pad, GstEvent * event)
{
  gboolean res;
  GstDecklinkSink *decklinksink;

  decklinksink = GST_DECKLINK_SINK (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (decklinksink, "event");

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      decklinksink->audio_eos = TRUE;
      decklinksink->audio_seqnum = gst_event_get_seqnum (event);
      break;
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }

  gst_object_unref (decklinksink);
  return res;
}

static gboolean
gst_decklink_sink_audiosink_query (GstPad * pad, GstQuery * query)
{
  gboolean res;
  GstDecklinkSink *decklinksink;

  decklinksink = GST_DECKLINK_SINK (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (decklinksink, "query");

  switch (GST_QUERY_TYPE (query)) {
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }

  gst_object_unref (decklinksink);
  return res;
}

static GstFlowReturn
gst_decklink_sink_audiosink_bufferalloc (GstPad * pad, guint64 offset,
    guint size, GstCaps * caps, GstBuffer ** buf)
{
  GstDecklinkSink *decklinksink;

  decklinksink = GST_DECKLINK_SINK (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (decklinksink, "bufferalloc");


  *buf = gst_buffer_new_and_alloc (size);
  gst_buffer_set_caps (*buf, caps);

  gst_object_unref (decklinksink);
  return GST_FLOW_OK;
}

static GstIterator *
gst_decklink_sink_audiosink_iterintlink (GstPad * pad)
{
  GstDecklinkSink *decklinksink;
  GstIterator *iter;

  decklinksink = GST_DECKLINK_SINK (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (decklinksink, "iterintlink");

  iter = gst_pad_iterate_internal_links_default (pad);

  gst_object_unref (decklinksink);
  return iter;
}



HRESULT
    Output::ScheduledFrameCompleted (IDeckLinkVideoFrame * completedFrame,
    BMDOutputFrameCompletionResult result)
{
  GST_DEBUG ("ScheduledFrameCompleted");

  g_mutex_lock (decklinksink->mutex);
  g_cond_signal (decklinksink->cond);
  decklinksink->queued_frames--;
  g_mutex_unlock (decklinksink->mutex);

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

  // guint64 samplesToWrite;

  if (decklinksink->stop) {
    GST_DEBUG ("decklinksink->stop set TRUE!");
    decklinksink->output->BeginAudioPreroll ();
    // running = true;
  } else {
    int n;
    const guint8 *data;

    g_mutex_lock (decklinksink->audio_mutex);

    n = gst_adapter_available (decklinksink->audio_adapter);
    if (n > 0) {
      data = gst_adapter_peek (decklinksink->audio_adapter, n);

      decklinksink->output->ScheduleAudioSamples ((void *) data, n / 4,
          0, 0, &samplesWritten);

      gst_adapter_flush (decklinksink->audio_adapter, samplesWritten * 4);
      GST_DEBUG ("wrote %d samples, %d available", samplesWritten, n / 4);

      g_cond_signal (decklinksink->audio_cond);
    } else {
      if (decklinksink->audio_eos) {
        GstMessage *message;

        message = gst_message_new_eos (GST_OBJECT_CAST (decklinksink));
        gst_message_set_seqnum (message, decklinksink->audio_seqnum);
        gst_element_post_message (GST_ELEMENT_CAST (decklinksink), message);
      }
    }
    g_mutex_unlock (decklinksink->audio_mutex);

  }

  GST_DEBUG ("RenderAudioSamples");

  return S_OK;
}


static const GList *
gst_decklink_sink_probe_get_properties (GstPropertyProbe * probe)
{
  GObjectClass *klass = G_OBJECT_GET_CLASS (probe);
  static GList *list = NULL;
  static gsize init = 0;

  if (g_once_init_enter (&init)) {
    list = g_list_append (NULL, g_object_class_find_property (klass, "device"));

    g_once_init_leave (&init, 1);
  }

  return list;
}


static gboolean probed = FALSE;
static int n_devices;

static void
gst_decklink_sink_class_probe_devices (GstElementClass * klass)
{
  IDeckLinkIterator *iterator;
  IDeckLink *decklink;

  n_devices = 0;
  iterator = CreateDeckLinkIteratorInstance ();
  if (iterator) {
    while (iterator->Next (&decklink) == S_OK) {
      n_devices++;
    }
  }

  probed = TRUE;
}

static void
gst_decklink_sink_probe_probe_property (GstPropertyProbe * probe,
    guint prop_id, const GParamSpec * pspec)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (probe);

  switch (prop_id) {
    case PROP_DEVICE:
      gst_decklink_sink_class_probe_devices (klass);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (probe, prop_id, pspec);
      break;
  }
}

static gboolean
gst_decklink_sink_probe_needs_probe (GstPropertyProbe * probe,
    guint prop_id, const GParamSpec * pspec)
{
  gboolean ret = FALSE;

  switch (prop_id) {
    case PROP_DEVICE:
      ret = !probed;
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (probe, prop_id, pspec);
      break;
  }
  return ret;
}

static GValueArray *
gst_decklink_sink_class_list_devices (GstElementClass * klass)
{
  GValueArray *array;
  GValue value = {
  0};
  GList *item;
  int i;

  array = g_value_array_new (n_devices);
  g_value_init (&value, G_TYPE_INT);
  for (i = 0; i < n_devices; i++) {
    g_value_set_int (&value, i);
    g_value_array_append (array, &value);

    item = item->next;
  }
  g_value_unset (&value);

  return array;
}

static GValueArray *
gst_decklink_sink_probe_get_values (GstPropertyProbe * probe,
    guint prop_id, const GParamSpec * pspec)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (probe);
  GValueArray *array = NULL;

  switch (prop_id) {
    case PROP_DEVICE:
      array = gst_decklink_sink_class_list_devices (klass);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (probe, prop_id, pspec);
      break;
  }

  return array;
}

static void
gst_decklink_sink_property_probe_interface_init (GstPropertyProbeInterface *
    iface)
{
  iface->get_properties = gst_decklink_sink_probe_get_properties;
  iface->probe_property = gst_decklink_sink_probe_probe_property;
  iface->needs_probe = gst_decklink_sink_probe_needs_probe;
  iface->get_values = gst_decklink_sink_probe_get_values;
}
