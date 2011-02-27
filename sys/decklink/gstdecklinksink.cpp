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
#include <gst/gst.h>
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
static gboolean gst_decklink_sink_videosink_setcaps (GstPad * pad, GstCaps * caps);
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
static gboolean gst_decklink_sink_videosink_event (GstPad * pad, GstEvent * event);
static gboolean gst_decklink_sink_videosink_query (GstPad * pad, GstQuery * query);
static GstFlowReturn gst_decklink_sink_videosink_bufferalloc (GstPad * pad,
    guint64 offset, guint size, GstCaps * caps, GstBuffer ** buf);
static GstIterator *gst_decklink_sink_videosink_iterintlink (GstPad * pad);


static GstCaps *gst_decklink_sink_audiosink_getcaps (GstPad * pad);
static gboolean gst_decklink_sink_audiosink_setcaps (GstPad * pad, GstCaps * caps);
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
static gboolean gst_decklink_sink_audiosink_event (GstPad * pad, GstEvent * event);
static gboolean gst_decklink_sink_audiosink_query (GstPad * pad, GstQuery * query);
static GstFlowReturn gst_decklink_sink_audiosink_bufferalloc (GstPad * pad,
    guint64 offset, guint size, GstCaps * caps, GstBuffer ** buf);
static GstIterator *gst_decklink_sink_audiosink_iterintlink (GstPad * pad);


enum
{
  PROP_0
};

/* pad templates */

#define MODE(w,h,n,d,i) \
  "video/x-raw-yuv,format=(fourcc)UYVY,width=" #w ",height=" #h \
  ",framerate=" #n "/" #d ",interlaced=" #i

static GstStaticPadTemplate gst_decklink_sink_videosink_template =
GST_STATIC_PAD_TEMPLATE ("videosink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (
      MODE(720,486,30000,1001,true)
    ));
#if 0
      MODE(720,486,24000,1001,true) ";"
      MODE(720,576,25,1,true) ";"
      MODE(1920,1080,24000,1001,false) ";"
      MODE(1920,1080,24,1,false) ";"
      MODE(1920,1080,25,1,false) ";"
      MODE(1920,1080,30000,1001,false) ";"
      MODE(1920,1080,30,1,false) ";"
      MODE(1920,1080,25,1,true) ";"
      MODE(1920,1080,30000,1001,true) ";"
      MODE(1920,1080,30,1,true) ";"
      MODE(1280,720,50,1,true) ";"
      MODE(1280,720,60000,1001,true) ";"
      MODE(1280,720,60,1,true)
#endif

static GstStaticPadTemplate gst_decklink_sink_audiosink_template =
GST_STATIC_PAD_TEMPLATE ("audiosink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int,width=16,depth=16,channels=2,rate=48000")
    );

typedef struct _DecklinkMode DecklinkMode;
struct _DecklinkMode {
  BMDDisplayMode mode;
  int width;
  int height;
  int fps_n;
  int fps_d;
  gboolean interlaced;
};

static DecklinkMode modes[] = {
  { bmdModeNTSC, 720,486,30000,1001,true },
  { bmdModeNTSC2398, 720,486,24000,1001,true },
  { bmdModePAL, 720,576,25,1,true },
  { bmdModeHD1080p2398, 1920,1080,24000,1001,false },
  { bmdModeHD1080p24, 1920,1080,24,1,false },
  { bmdModeHD1080p25, 1920,1080,25,1,false },
  { bmdModeHD1080p2997, 1920,1080,30000,1001,false },
  { bmdModeHD1080p30, 1920,1080,30,1,false },
  { bmdModeHD1080i50, 1920,1080,25,1,true },
  { bmdModeHD1080i5994, 1920,1080,30000,1001,true },
  { bmdModeHD1080i6000, 1920,1080,30,1,true },
  { bmdModeHD720p50, 1280,720,50,1,true },
  { bmdModeHD720p5994, 1280,720,60000,1001,true },
  { bmdModeHD720p60, 1280,720,60,1,true }
};


/* class initialization */

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_decklink_sink_debug_category, "decklinksink", 0, \
      "debug category for decklinksink element");

GST_BOILERPLATE_FULL (GstDecklinkSink, gst_decklink_sink, GstElement,
    GST_TYPE_ELEMENT, DEBUG_INIT);

static void
gst_decklink_sink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_decklink_sink_videosink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_decklink_sink_audiosink_template));

  gst_element_class_set_details_simple (element_class, "Decklink Sink",
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

}

static void
gst_decklink_sink_init (GstDecklinkSink * decklinksink,
    GstDecklinkSinkClass * decklinksink_class)
{

  decklinksink->videosinkpad =
      gst_pad_new_from_static_template (&gst_decklink_sink_videosink_template,
      "sink");
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


  decklinksink->cond = g_cond_new();
  decklinksink->mutex = g_mutex_new();

  decklinksink->mode = 0;

  decklinksink->width = modes[decklinksink->mode].width;
  decklinksink->height = modes[decklinksink->mode].height;
  decklinksink->fps_n = modes[decklinksink->mode].fps_n;
  decklinksink->fps_d = modes[decklinksink->mode].fps_d;
  decklinksink->interlaced = modes[decklinksink->mode].interlaced;
  decklinksink->bmd_mode = modes[decklinksink->mode].mode;

  decklinksink->callback = new Output;
  decklinksink->callback->decklinksink = decklinksink;
}

void
gst_decklink_sink_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDecklinkSink *decklinksink;

  g_return_if_fail (GST_IS_DECKLINK_SINK (object));
  decklinksink = GST_DECKLINK_SINK (object);

  switch (property_id) {
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
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_decklink_sink_dispose (GObject * object)
{
  GstDecklinkSink *decklinksink;

  g_return_if_fail (GST_IS_DECKLINK_SINK (object));
  decklinksink = GST_DECKLINK_SINK (object);

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

  delete decklinksink->callback;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_decklink_sink_start (GstDecklinkSink *decklinksink)
{
  IDeckLinkIterator *iterator;
  HRESULT ret;
  IDeckLinkDisplayModeIterator *mode_iterator;
  IDeckLinkDisplayMode *mode;
  BMDTimeValue fps_n;
  BMDTimeScale fps_d;

  iterator = CreateDeckLinkIteratorInstance ();
  if (iterator == NULL) {
    GST_ERROR("no driver");
    return FALSE;
  }

  ret = iterator->Next (&decklinksink->decklink);
  if (ret != S_OK) {
    GST_ERROR("no card");
    return FALSE;
  }

  ret = decklinksink->decklink->QueryInterface (IID_IDeckLinkOutput,
      (void **)&decklinksink->output);
  if (ret != S_OK) {
    GST_ERROR ("no output");
    return FALSE;
  }

  decklinksink->output->SetAudioCallback (decklinksink->callback);

  ret = decklinksink->output->GetDisplayModeIterator (&mode_iterator);
  if (ret != S_OK) {
    GST_ERROR ("failed to get display mode iterator");
    return FALSE;
  }

  while (mode_iterator->Next (&mode) == S_OK) {
    break;
  }
  if (!mode) {
    GST_ERROR ("bad mode");
    return FALSE;
  }

  decklinksink->width = mode->GetWidth ();
  decklinksink->height = mode->GetHeight ();
  mode->GetFrameRate (&fps_n, &fps_d);
  decklinksink->fps_n = fps_n;
  decklinksink->fps_d = fps_d;

  decklinksink->display_mode = mode->GetDisplayMode ();

  ret = decklinksink->output->EnableVideoOutput (decklinksink->display_mode,
      bmdVideoOutputFlagDefault);
  if (ret != S_OK) {
    GST_ERROR ("failed to enable video output");
    return FALSE;
  }
  //decklinksink->video_enabled = TRUE;

  decklinksink->output->SetScheduledFrameCompletionCallback (decklinksink->callback);

  if (0) {
    ret = decklinksink->output->EnableAudioOutput (bmdAudioSampleRate48kHz,
        16, 2, bmdAudioOutputStreamContinuous);
    if (ret != S_OK) {
      GST_ERROR ("failed to enable audio output");
      return FALSE;
    }
  }

  decklinksink->num_frames = 0;

  return TRUE;
}

static gboolean
gst_decklink_sink_force_stop (GstDecklinkSink *decklinksink)
{
  g_mutex_lock (decklinksink->mutex);
  decklinksink->stop = TRUE;
  g_cond_signal (decklinksink->cond);
  g_mutex_unlock (decklinksink->mutex);

  return TRUE;
}

static gboolean
gst_decklink_sink_stop (GstDecklinkSink *decklinksink)
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

  return TRUE;
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

  caps = gst_caps_copy (gst_pad_get_pad_template_caps (pad));

  gst_object_unref (decklinksink);
  return caps;
}

static gboolean
gst_decklink_sink_videosink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstDecklinkSink *decklinksink;

  decklinksink = GST_DECKLINK_SINK (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (decklinksink, "setcaps");


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

  decklinksink = GST_DECKLINK_SINK (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (decklinksink, "chain");

#if 0
  if (!decklinksink->video_enabled) {
    HRESULT ret;
    ret = decklinksink->output->EnableVideoOutput (decklinksink->display_mode,
        bmdVideoOutputFlagDefault);
    if (ret != S_OK) {
      GST_ERROR ("failed to enable video output");
      //return FALSE;
    }
    decklinksink->video_enabled = TRUE;
  }
#endif

  decklinksink->output->CreateVideoFrame (decklinksink->width,
      decklinksink->height, decklinksink->width * 2, bmdFormat8BitYUV,
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
        decklinksink->num_frames * decklinksink->fps_n,
        decklinksink->fps_n, decklinksink->fps_d);
    decklinksink->num_frames++;

    if (!decklinksink->sched_started) {
      decklinksink->output->StartScheduledPlayback (0, 100, 1.0);
      decklinksink->sched_started = TRUE;
    }

    ret = GST_FLOW_OK;
  } else {
    ret = GST_FLOW_WRONG_STATE;
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
gst_decklink_sink_videosink_bufferalloc (GstPad * pad, guint64 offset, guint size,
    GstCaps * caps, GstBuffer ** buf)
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

  decklinksink = GST_DECKLINK_SINK (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (decklinksink, "chain");


  gst_object_unref (decklinksink);
  return GST_FLOW_OK;
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
gst_decklink_sink_audiosink_bufferalloc (GstPad * pad, guint64 offset, guint size,
    GstCaps * caps, GstBuffer ** buf)
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
  GST_DEBUG("ScheduledFrameCompleted");

  g_mutex_lock (decklinksink->mutex);
  g_cond_signal (decklinksink->cond);
  decklinksink->queued_frames--;
  g_mutex_unlock (decklinksink->mutex);

  return S_OK;
}

HRESULT
Output::ScheduledPlaybackHasStopped ()
{
  GST_ERROR("ScheduledPlaybackHasStopped");
  return S_OK;
}

HRESULT
Output::RenderAudioSamples (bool preroll)
{
  GST_ERROR("RenderAudioSamples");
  
  return S_OK;
}

