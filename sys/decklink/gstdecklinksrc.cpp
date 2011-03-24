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
 * gst-launch -v decklinksrc ! xvimagesink
 * ]|
 * 
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include "gstdecklinksrc.h"
#include "capture.h"
#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_decklink_src_debug_category);
#define GST_CAT_DEFAULT gst_decklink_src_debug_category

/* prototypes */


static void gst_decklink_src_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_decklink_src_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_decklink_src_dispose (GObject * object);
static void gst_decklink_src_finalize (GObject * object);

static GstPad *gst_decklink_src_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name);
static void gst_decklink_src_release_pad (GstElement * element, GstPad * pad);
static GstStateChangeReturn
gst_decklink_src_change_state (GstElement * element, GstStateChange transition);
static GstClock *gst_decklink_src_provide_clock (GstElement * element);
static gboolean gst_decklink_src_set_clock (GstElement * element,
    GstClock * clock);
static GstIndex *gst_decklink_src_get_index (GstElement * element);
static void gst_decklink_src_set_index (GstElement * element, GstIndex * index);
static gboolean gst_decklink_src_send_event (GstElement * element,
    GstEvent * event);
static gboolean gst_decklink_src_query (GstElement * element, GstQuery * query);

static GstCaps *gst_decklink_src_audio_src_getcaps (GstPad * pad);
static gboolean gst_decklink_src_audio_src_setcaps (GstPad * pad, GstCaps * caps);
static gboolean gst_decklink_src_audio_src_acceptcaps (GstPad * pad, GstCaps * caps);
static void gst_decklink_src_audio_src_fixatecaps (GstPad * pad, GstCaps * caps);
static gboolean gst_decklink_src_audio_src_activate (GstPad * pad);
static gboolean gst_decklink_src_audio_src_activatepush (GstPad * pad,
    gboolean active);
static gboolean gst_decklink_src_audio_src_activatepull (GstPad * pad,
    gboolean active);
static GstPadLinkReturn gst_decklink_src_audio_src_link (GstPad * pad, GstPad * peer);
static GstFlowReturn gst_decklink_src_audio_src_getrange (GstPad * pad,
    guint64 offset, guint length, GstBuffer ** buffer);
static gboolean gst_decklink_src_audio_src_event (GstPad * pad, GstEvent * event);
static gboolean gst_decklink_src_audio_src_query (GstPad * pad, GstQuery * query);
static GstIterator *gst_decklink_src_audio_src_iterintlink (GstPad * pad);


static GstCaps *gst_decklink_src_video_src_getcaps (GstPad * pad);
static gboolean gst_decklink_src_video_src_setcaps (GstPad * pad, GstCaps * caps);
static gboolean gst_decklink_src_video_src_acceptcaps (GstPad * pad, GstCaps * caps);
static void gst_decklink_src_video_src_fixatecaps (GstPad * pad, GstCaps * caps);
static gboolean gst_decklink_src_video_src_activate (GstPad * pad);
static gboolean gst_decklink_src_video_src_activatepush (GstPad * pad,
    gboolean active);
static gboolean gst_decklink_src_video_src_activatepull (GstPad * pad,
    gboolean active);
static GstPadLinkReturn gst_decklink_src_video_src_link (GstPad * pad, GstPad * peer);
static GstFlowReturn gst_decklink_src_video_src_getrange (GstPad * pad,
    guint64 offset, guint length, GstBuffer ** buffer);
static gboolean gst_decklink_src_video_src_event (GstPad * pad, GstEvent * event);
static gboolean gst_decklink_src_video_src_query (GstPad * pad, GstQuery * query);
static GstIterator *gst_decklink_src_video_src_iterintlink (GstPad * pad);

static void gst_decklink_src_task (void *priv);

enum
{
  PROP_0
};

/* pad templates */

static GstStaticPadTemplate gst_decklink_src_audio_src_template =
GST_STATIC_PAD_TEMPLATE ("audiosrc",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int,width=16,depth=16,channels=2,rate=48000")
    );

#define MODE(w,h,n,d,i) \
  "video/x-raw-yuv,format=(fourcc)UYVY,width=" #w ",height=" #h \
  ",framerate=" #n "/" #d ",interlaced=" #i

static GstStaticPadTemplate gst_decklink_src_video_src_template =
GST_STATIC_PAD_TEMPLATE ("videosrc",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (
      MODE(720,486,30000,1001,true) ";"
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
    ));

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
  GST_DEBUG_CATEGORY_INIT (gst_decklink_src_debug_category, "decklinksrc", 0, \
      "debug category for decklinksrc element");

GST_BOILERPLATE_FULL (GstDecklinkSrc, gst_decklink_src, GstElement,
    GST_TYPE_ELEMENT, DEBUG_INIT);

static void
gst_decklink_src_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_decklink_src_audio_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_decklink_src_video_src_template));

  gst_element_class_set_details_simple (element_class, "Decklink source",
      "Source/Video", "DeckLink Source", "David Schleef <ds@entropywave.com>");
}

static void
gst_decklink_src_class_init (GstDecklinkSrcClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_decklink_src_set_property;
  gobject_class->get_property = gst_decklink_src_get_property;
  gobject_class->dispose = gst_decklink_src_dispose;
  gobject_class->finalize = gst_decklink_src_finalize;
  element_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_decklink_src_request_new_pad);
  element_class->release_pad = GST_DEBUG_FUNCPTR (gst_decklink_src_release_pad);
  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_decklink_src_change_state);
  element_class->provide_clock =
      GST_DEBUG_FUNCPTR (gst_decklink_src_provide_clock);
  element_class->set_clock = GST_DEBUG_FUNCPTR (gst_decklink_src_set_clock);
  element_class->get_index = GST_DEBUG_FUNCPTR (gst_decklink_src_get_index);
  element_class->set_index = GST_DEBUG_FUNCPTR (gst_decklink_src_set_index);
  element_class->send_event = GST_DEBUG_FUNCPTR (gst_decklink_src_send_event);
  element_class->query = GST_DEBUG_FUNCPTR (gst_decklink_src_query);

}

static void
gst_decklink_src_init (GstDecklinkSrc * decklinksrc,
    GstDecklinkSrcClass * decklinksrc_class)
{
  g_static_rec_mutex_init (&decklinksrc->task_mutex);
  decklinksrc->task = gst_task_create (gst_decklink_src_task, decklinksrc);
  gst_task_set_lock (decklinksrc->task, &decklinksrc->task_mutex);

  decklinksrc->audiosrcpad =
      gst_pad_new_from_static_template (&gst_decklink_src_audio_src_template, "audiosrc");
  gst_pad_set_getcaps_function (decklinksrc->audiosrcpad,
      GST_DEBUG_FUNCPTR (gst_decklink_src_audio_src_getcaps));
  gst_pad_set_setcaps_function (decklinksrc->audiosrcpad,
      GST_DEBUG_FUNCPTR (gst_decklink_src_audio_src_setcaps));
  gst_pad_set_acceptcaps_function (decklinksrc->audiosrcpad,
      GST_DEBUG_FUNCPTR (gst_decklink_src_audio_src_acceptcaps));
  gst_pad_set_fixatecaps_function (decklinksrc->audiosrcpad,
      GST_DEBUG_FUNCPTR (gst_decklink_src_audio_src_fixatecaps));
  gst_pad_set_activate_function (decklinksrc->audiosrcpad,
      GST_DEBUG_FUNCPTR (gst_decklink_src_audio_src_activate));
  gst_pad_set_activatepush_function (decklinksrc->audiosrcpad,
      GST_DEBUG_FUNCPTR (gst_decklink_src_audio_src_activatepush));
  gst_pad_set_activatepull_function (decklinksrc->audiosrcpad,
      GST_DEBUG_FUNCPTR (gst_decklink_src_audio_src_activatepull));
  gst_pad_set_link_function (decklinksrc->audiosrcpad,
      GST_DEBUG_FUNCPTR (gst_decklink_src_audio_src_link));
  gst_pad_set_getrange_function (decklinksrc->audiosrcpad,
      GST_DEBUG_FUNCPTR (gst_decklink_src_audio_src_getrange));
  gst_pad_set_event_function (decklinksrc->audiosrcpad,
      GST_DEBUG_FUNCPTR (gst_decklink_src_audio_src_event));
  gst_pad_set_query_function (decklinksrc->audiosrcpad,
      GST_DEBUG_FUNCPTR (gst_decklink_src_audio_src_query));
  gst_pad_set_iterate_internal_links_function (decklinksrc->audiosrcpad,
      GST_DEBUG_FUNCPTR (gst_decklink_src_audio_src_iterintlink));
  gst_element_add_pad (GST_ELEMENT (decklinksrc), decklinksrc->audiosrcpad);



  decklinksrc->videosrcpad =
      gst_pad_new_from_static_template (&gst_decklink_src_video_src_template, "videosrc");
  gst_pad_set_getcaps_function (decklinksrc->videosrcpad,
      GST_DEBUG_FUNCPTR (gst_decklink_src_video_src_getcaps));
  gst_pad_set_setcaps_function (decklinksrc->videosrcpad,
      GST_DEBUG_FUNCPTR (gst_decklink_src_video_src_setcaps));
  gst_pad_set_acceptcaps_function (decklinksrc->videosrcpad,
      GST_DEBUG_FUNCPTR (gst_decklink_src_video_src_acceptcaps));
  gst_pad_set_fixatecaps_function (decklinksrc->videosrcpad,
      GST_DEBUG_FUNCPTR (gst_decklink_src_video_src_fixatecaps));
  gst_pad_set_activate_function (decklinksrc->videosrcpad,
      GST_DEBUG_FUNCPTR (gst_decklink_src_video_src_activate));
  gst_pad_set_activatepush_function (decklinksrc->videosrcpad,
      GST_DEBUG_FUNCPTR (gst_decklink_src_video_src_activatepush));
  gst_pad_set_activatepull_function (decklinksrc->videosrcpad,
      GST_DEBUG_FUNCPTR (gst_decklink_src_video_src_activatepull));
  gst_pad_set_link_function (decklinksrc->videosrcpad,
      GST_DEBUG_FUNCPTR (gst_decklink_src_video_src_link));
  gst_pad_set_getrange_function (decklinksrc->videosrcpad,
      GST_DEBUG_FUNCPTR (gst_decklink_src_video_src_getrange));
  gst_pad_set_event_function (decklinksrc->videosrcpad,
      GST_DEBUG_FUNCPTR (gst_decklink_src_video_src_event));
  gst_pad_set_query_function (decklinksrc->videosrcpad,
      GST_DEBUG_FUNCPTR (gst_decklink_src_video_src_query));
  gst_pad_set_iterate_internal_links_function (decklinksrc->videosrcpad,
      GST_DEBUG_FUNCPTR (gst_decklink_src_video_src_iterintlink));
  gst_element_add_pad (GST_ELEMENT (decklinksrc), decklinksrc->videosrcpad);


  decklinksrc->cond = g_cond_new();
  decklinksrc->mutex = g_mutex_new();

  decklinksrc->copy_data = TRUE;
  decklinksrc->mode = 0;

  decklinksrc->width = modes[decklinksrc->mode].width;
  decklinksrc->height = modes[decklinksrc->mode].height;
  decklinksrc->fps_n = modes[decklinksrc->mode].fps_n;
  decklinksrc->fps_d = modes[decklinksrc->mode].fps_d;
  decklinksrc->interlaced = modes[decklinksrc->mode].interlaced;
  decklinksrc->bmd_mode = modes[decklinksrc->mode].mode;

}

void
gst_decklink_src_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDecklinkSrc *decklinksrc;

  g_return_if_fail (GST_IS_DECKLINK_SRC (object));
  decklinksrc = GST_DECKLINK_SRC (object);

  switch (property_id) {
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

  g_return_if_fail (GST_IS_DECKLINK_SRC (object));
  decklinksrc = GST_DECKLINK_SRC (object);

  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_decklink_src_dispose (GObject * object)
{
  GstDecklinkSrc *decklinksrc;

  g_return_if_fail (GST_IS_DECKLINK_SRC (object));
  decklinksrc = GST_DECKLINK_SRC (object);

  /* clean up as possible.  may be called multiple times */

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

void
gst_decklink_src_finalize (GObject * object)
{
  GstDecklinkSrc *decklinksrc;

  g_return_if_fail (GST_IS_DECKLINK_SRC (object));
  decklinksrc = GST_DECKLINK_SRC (object);

  /* clean up object here */

  g_cond_free (decklinksrc->cond);
  g_mutex_free (decklinksrc->mutex);
  gst_task_set_lock (decklinksrc->task, NULL);
  g_object_unref (decklinksrc->task);
  if (decklinksrc->audio_caps) {
    gst_caps_unref (decklinksrc->audio_caps);
  }
  if (decklinksrc->video_caps) {
    gst_caps_unref (decklinksrc->video_caps);
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}



static GstPad *
gst_decklink_src_request_new_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * name)
{

  return NULL;
}

static void
gst_decklink_src_release_pad (GstElement * element, GstPad * pad)
{

}

static gboolean
gst_decklink_src_start (GstElement * element)
{
  GstDecklinkSrc *decklinksrc = GST_DECKLINK_SRC (element);
  IDeckLinkIterator *iterator;
  DeckLinkCaptureDelegate *delegate;
  IDeckLinkDisplayModeIterator *mode_iterator;
  IDeckLinkDisplayMode *mode;
  int i;
  int sample_depth;
  int channels;
  BMDVideoInputFlags input_flags;
  BMDDisplayMode selected_mode;
  BMDPixelFormat pixel_format;
  HRESULT ret;

  GST_DEBUG_OBJECT (decklinksrc, "start");

  iterator = CreateDeckLinkIteratorInstance ();
  if (iterator == NULL) {
    GST_ERROR("no driver");
    return FALSE;
  }

  ret = iterator->Next (&decklinksrc->decklink);
  if (ret != S_OK) {
    GST_ERROR("no card");
    return FALSE;
  }

  ret = decklinksrc->decklink->QueryInterface (IID_IDeckLinkInput,
      (void **) &decklinksrc->input);
  if (ret != S_OK) {
    GST_ERROR ("query interface failed");
    return FALSE;
  }

  delegate = new DeckLinkCaptureDelegate ();
  delegate->priv = decklinksrc;
  decklinksrc->input->SetCallback (delegate);

  ret = decklinksrc->input->GetDisplayModeIterator (&mode_iterator);
  if (ret != S_OK) {
    GST_ERROR("failed to get display mode iterator");
    return FALSE;
  }

  i = 0;
  while (mode_iterator->Next (&mode) == S_OK) {
    const char *mode_name;

    mode->GetName (&mode_name);

    GST_ERROR("%d: mode name: %s", i, mode_name);

    mode->Release ();
    i++;
  }

  pixel_format = bmdFormat8BitYUV;
  selected_mode = decklinksrc->bmd_mode;
  input_flags = 0;
  ret = decklinksrc->input->EnableVideoInput (selected_mode, pixel_format,
      input_flags);
  if (ret != S_OK){
    GST_ERROR("enable video input failed");
    return FALSE;
  }

  sample_depth = 16;
  channels = 2;
  ret = decklinksrc->input->EnableAudioInput (bmdAudioSampleRate48kHz, sample_depth,
      channels);
  if (ret != S_OK){
    GST_ERROR("enable video input failed");
    return FALSE;
  }

  ret = decklinksrc->input->StartStreams ();
  if (ret != S_OK) {
    GST_ERROR("start streams failed");
    return FALSE;
  }

  g_static_rec_mutex_lock (&decklinksrc->task_mutex);
  gst_task_start (decklinksrc->task);
  g_static_rec_mutex_unlock (&decklinksrc->task_mutex);

  return TRUE;
}

static gboolean
gst_decklink_src_stop (GstElement * element)
{
  GstDecklinkSrc *decklinksrc = GST_DECKLINK_SRC (element);

  gst_task_stop (decklinksrc->task);

  g_mutex_lock (decklinksrc->mutex);
  decklinksrc->stop = TRUE;
  g_cond_signal (decklinksrc->cond);
  g_mutex_unlock (decklinksrc->mutex);

  gst_task_join (decklinksrc->task);

  return TRUE;
}

static GstStateChangeReturn
gst_decklink_src_change_state (GstElement * element, GstStateChange transition)
{
  GstDecklinkSrc *decklinksrc;
  GstStateChangeReturn ret;
  gboolean no_preroll = FALSE;

  g_return_val_if_fail (GST_IS_DECKLINK_SRC (element),
      GST_STATE_CHANGE_FAILURE);
  decklinksrc = GST_DECKLINK_SRC (element);

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

static GstClock *
gst_decklink_src_provide_clock (GstElement * element)
{

  return NULL;
}

static gboolean
gst_decklink_src_set_clock (GstElement * element, GstClock * clock)
{

  return TRUE;
}

static GstIndex *
gst_decklink_src_get_index (GstElement * element)
{

  return NULL;
}

static void
gst_decklink_src_set_index (GstElement * element, GstIndex * index)
{

}

static gboolean
gst_decklink_src_send_event (GstElement * element, GstEvent * event)
{

  return TRUE;
}

static gboolean
gst_decklink_src_query (GstElement * element, GstQuery * query)
{

  return FALSE;
}

static GstCaps *
gst_decklink_src_audio_src_getcaps (GstPad * pad)
{
  GstDecklinkSrc *decklinksrc;
  GstCaps *caps;

  decklinksrc = GST_DECKLINK_SRC (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (decklinksrc, "getcaps");

  caps = gst_caps_copy (gst_pad_get_pad_template_caps (pad));

  gst_object_unref (decklinksrc);
  return caps;
}

static gboolean
gst_decklink_src_audio_src_setcaps (GstPad * pad, GstCaps * caps)
{
  GstDecklinkSrc *decklinksrc;

  decklinksrc = GST_DECKLINK_SRC (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (decklinksrc, "setcaps");


  gst_object_unref (decklinksrc);
  return TRUE;
}

static gboolean
gst_decklink_src_audio_src_acceptcaps (GstPad * pad, GstCaps * caps)
{
  GstDecklinkSrc *decklinksrc;

  decklinksrc = GST_DECKLINK_SRC (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (decklinksrc, "acceptcaps");


  gst_object_unref (decklinksrc);
  return TRUE;
}

static void
gst_decklink_src_audio_src_fixatecaps (GstPad * pad, GstCaps * caps)
{
  GstDecklinkSrc *decklinksrc;

  decklinksrc = GST_DECKLINK_SRC (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (decklinksrc, "fixatecaps");


  gst_object_unref (decklinksrc);
}

static gboolean
gst_decklink_src_audio_src_activate (GstPad * pad)
{
  GstDecklinkSrc *decklinksrc;
  gboolean ret;

  decklinksrc = GST_DECKLINK_SRC (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (decklinksrc, "activate");

  if (gst_pad_check_pull_range (pad)) {
    GST_DEBUG_OBJECT (pad, "activating pull");
    ret = gst_pad_activate_pull (pad, TRUE);
  } else {
    GST_DEBUG_OBJECT (pad, "activating push");
    ret = gst_pad_activate_push (pad, TRUE);
  }

  gst_object_unref (decklinksrc);
  return ret;
}

static gboolean
gst_decklink_src_audio_src_activatepush (GstPad * pad, gboolean active)
{
  GstDecklinkSrc *decklinksrc;

  decklinksrc = GST_DECKLINK_SRC (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (decklinksrc, "activatepush");


  gst_object_unref (decklinksrc);
  return TRUE;
}

static gboolean
gst_decklink_src_audio_src_activatepull (GstPad * pad, gboolean active)
{
  GstDecklinkSrc *decklinksrc;

  decklinksrc = GST_DECKLINK_SRC (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (decklinksrc, "activatepull");


  gst_object_unref (decklinksrc);
  return TRUE;
}

static GstPadLinkReturn
gst_decklink_src_audio_src_link (GstPad * pad, GstPad * peer)
{
  GstDecklinkSrc *decklinksrc;

  decklinksrc = GST_DECKLINK_SRC (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (decklinksrc, "link");


  gst_object_unref (decklinksrc);
  return GST_PAD_LINK_OK;
}

static GstFlowReturn
gst_decklink_src_audio_src_getrange (GstPad * pad, guint64 offset, guint length,
    GstBuffer ** buffer)
{
  GstDecklinkSrc *decklinksrc;

  decklinksrc = GST_DECKLINK_SRC (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (decklinksrc, "getrange");


  gst_object_unref (decklinksrc);
  return GST_FLOW_OK;
}

static gboolean
gst_decklink_src_audio_src_event (GstPad * pad, GstEvent * event)
{
  gboolean res;
  GstDecklinkSrc *decklinksrc;

  decklinksrc = GST_DECKLINK_SRC (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (decklinksrc, "event");

  switch (GST_EVENT_TYPE (event)) {
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }

  gst_object_unref (decklinksrc);
  return res;
}

static gboolean
gst_decklink_src_audio_src_query (GstPad * pad, GstQuery * query)
{
  gboolean res;
  GstDecklinkSrc *decklinksrc;

  decklinksrc = GST_DECKLINK_SRC (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (decklinksrc, "query");

  switch (GST_QUERY_TYPE (query)) {
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }

  gst_object_unref (decklinksrc);
  return res;
}

static GstIterator *
gst_decklink_src_audio_src_iterintlink (GstPad * pad)
{
  GstDecklinkSrc *decklinksrc;
  GstIterator *iter;

  decklinksrc = GST_DECKLINK_SRC (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (decklinksrc, "iterintlink");

  iter = gst_pad_iterate_internal_links_default (pad);

  gst_object_unref (decklinksrc);
  return iter;
}


static GstCaps *
gst_decklink_src_video_src_getcaps (GstPad * pad)
{
  GstDecklinkSrc *decklinksrc;
  GstCaps *caps;

  decklinksrc = GST_DECKLINK_SRC (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (decklinksrc, "getcaps");

  caps = gst_caps_copy (gst_pad_get_pad_template_caps (pad));

  gst_object_unref (decklinksrc);
  return caps;
}

static gboolean
gst_decklink_src_video_src_setcaps (GstPad * pad, GstCaps * caps)
{
  GstDecklinkSrc *decklinksrc;

  decklinksrc = GST_DECKLINK_SRC (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (decklinksrc, "setcaps");


  gst_object_unref (decklinksrc);
  return TRUE;
}

static gboolean
gst_decklink_src_video_src_acceptcaps (GstPad * pad, GstCaps * caps)
{
  GstDecklinkSrc *decklinksrc;

  decklinksrc = GST_DECKLINK_SRC (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (decklinksrc, "acceptcaps");


  gst_object_unref (decklinksrc);
  return TRUE;
}

static void
gst_decklink_src_video_src_fixatecaps (GstPad * pad, GstCaps * caps)
{
  GstDecklinkSrc *decklinksrc;

  decklinksrc = GST_DECKLINK_SRC (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (decklinksrc, "fixatecaps");


  gst_object_unref (decklinksrc);
}

static gboolean
gst_decklink_src_video_src_activate (GstPad * pad)
{
  GstDecklinkSrc *decklinksrc;
  gboolean ret;

  decklinksrc = GST_DECKLINK_SRC (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (decklinksrc, "activate");

  if (gst_pad_check_pull_range (pad)) {
    GST_DEBUG_OBJECT (pad, "activating pull");
    ret = gst_pad_activate_pull (pad, TRUE);
  } else {
    GST_DEBUG_OBJECT (pad, "activating push");
    ret = gst_pad_activate_push (pad, TRUE);
  }

  gst_object_unref (decklinksrc);
  return ret;
}

static gboolean
gst_decklink_src_video_src_activatepush (GstPad * pad, gboolean active)
{
  GstDecklinkSrc *decklinksrc;

  decklinksrc = GST_DECKLINK_SRC (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (decklinksrc, "activatepush");


  gst_object_unref (decklinksrc);
  return TRUE;
}

static gboolean
gst_decklink_src_video_src_activatepull (GstPad * pad, gboolean active)
{
  GstDecklinkSrc *decklinksrc;

  decklinksrc = GST_DECKLINK_SRC (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (decklinksrc, "activatepull");


  gst_object_unref (decklinksrc);
  return TRUE;
}

static GstPadLinkReturn
gst_decklink_src_video_src_link (GstPad * pad, GstPad * peer)
{
  GstDecklinkSrc *decklinksrc;

  decklinksrc = GST_DECKLINK_SRC (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (decklinksrc, "link");


  gst_object_unref (decklinksrc);
  return GST_PAD_LINK_OK;
}

static GstFlowReturn
gst_decklink_src_video_src_getrange (GstPad * pad, guint64 offset, guint length,
    GstBuffer ** buffer)
{
  GstDecklinkSrc *decklinksrc;

  decklinksrc = GST_DECKLINK_SRC (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (decklinksrc, "getrange");


  gst_object_unref (decklinksrc);
  return GST_FLOW_OK;
}

static gboolean
gst_decklink_src_video_src_event (GstPad * pad, GstEvent * event)
{
  gboolean res;
  GstDecklinkSrc *decklinksrc;

  decklinksrc = GST_DECKLINK_SRC (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (decklinksrc, "event");

  switch (GST_EVENT_TYPE (event)) {
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }

  gst_object_unref (decklinksrc);
  return res;
}

static gboolean
gst_decklink_src_video_src_query (GstPad * pad, GstQuery * query)
{
  gboolean res;
  GstDecklinkSrc *decklinksrc;

  decklinksrc = GST_DECKLINK_SRC (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (decklinksrc, "query");

  switch (GST_QUERY_TYPE (query)) {
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }

  gst_object_unref (decklinksrc);
  return res;
}

static GstIterator *
gst_decklink_src_video_src_iterintlink (GstPad * pad)
{
  GstDecklinkSrc *decklinksrc;
  GstIterator *iter;

  decklinksrc = GST_DECKLINK_SRC (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (decklinksrc, "iterintlink");

  iter = gst_pad_iterate_internal_links_default (pad);

  gst_object_unref (decklinksrc);
  return iter;
}


static void
video_frame_free (void *data)
{
  IDeckLinkVideoInputFrame *video_frame = (IDeckLinkVideoInputFrame *)data;

  video_frame->Release ();
}

static void
gst_decklink_src_task (void *priv)
{
  GstDecklinkSrc *decklinksrc = GST_DECKLINK_SRC (priv);
  GstBuffer *buffer;
  GstBuffer *audio_buffer;
  IDeckLinkVideoInputFrame *video_frame;
  IDeckLinkAudioInputPacket *audio_frame;
  int dropped_frames;
  void *data;
  int n_samples;
  GstFlowReturn ret;

  GST_DEBUG_OBJECT (decklinksrc, "task");

  g_mutex_lock (decklinksrc->mutex);
  while (decklinksrc->video_frame == NULL && !decklinksrc->stop) {
    g_cond_wait (decklinksrc->cond, decklinksrc->mutex);
  }
  video_frame = decklinksrc->video_frame;
  audio_frame = decklinksrc->audio_frame;
  dropped_frames = decklinksrc->dropped_frames;
  decklinksrc->video_frame = NULL;
  decklinksrc->audio_frame = NULL;
  g_mutex_unlock (decklinksrc->mutex);

  if (decklinksrc->stop) {
    GST_ERROR("stopping task");
    return;
  }

  if (dropped_frames > 0) {
    GST_ELEMENT_ERROR(decklinksrc, RESOURCE, READ, (NULL), (NULL));
    /* ERROR */
    return;
  }

  video_frame->GetBytes (&data);
  if (decklinksrc->copy_data) {
    buffer = gst_buffer_new_and_alloc (decklinksrc->width * decklinksrc->height * 2);

    memcpy (GST_BUFFER_DATA (buffer), data, decklinksrc->width * decklinksrc->height * 2);

    video_frame->Release ();
  } else {
    buffer = gst_buffer_new ();
    GST_BUFFER_SIZE (buffer) = decklinksrc->width * decklinksrc->height * 2;

    GST_BUFFER_DATA (buffer) = (guint8 *)data;

    GST_BUFFER_FREE_FUNC (buffer) = video_frame_free;
    GST_BUFFER_MALLOCDATA (buffer) = (guint8 *)video_frame;
  }

  GST_BUFFER_TIMESTAMP (buffer) =
    gst_util_uint64_scale_int (decklinksrc->num_frames * GST_SECOND,
        decklinksrc->fps_d, decklinksrc->fps_n);
  GST_BUFFER_DURATION (buffer) =
    gst_util_uint64_scale_int ((decklinksrc->num_frames + 1) * GST_SECOND,
        decklinksrc->fps_d, decklinksrc->fps_n) -
    GST_BUFFER_TIMESTAMP (buffer);
  GST_BUFFER_OFFSET (buffer) = decklinksrc->num_frames;
  if (decklinksrc->num_frames == 0) {
    GST_BUFFER_FLAG_SET(buffer, GST_BUFFER_FLAG_DISCONT);
  }
  decklinksrc->num_frames ++;

  if (decklinksrc->video_caps == NULL) {
    decklinksrc->video_caps = gst_caps_new_simple ("video/x-raw-yuv",
        "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC('U','Y','V','Y'),
        "width", G_TYPE_INT, decklinksrc->width,
        "height", G_TYPE_INT, decklinksrc->height,
        "framerate", GST_TYPE_FRACTION,
        decklinksrc->fps_n, decklinksrc->fps_d,
        "interlaced", G_TYPE_BOOLEAN, decklinksrc->interlaced,
        NULL);
  }
  gst_buffer_set_caps (buffer, decklinksrc->video_caps);

  ret = gst_pad_push (decklinksrc->videosrcpad, buffer);
  if (ret != GST_FLOW_OK) {
    GST_ELEMENT_ERROR(decklinksrc, CORE, NEGOTIATION, (NULL), (NULL));
  }


  n_samples = audio_frame->GetSampleFrameCount();
  audio_frame->GetBytes (&data);
  audio_buffer = gst_buffer_new_and_alloc (n_samples * 2 * 2);
  memcpy (GST_BUFFER_DATA (audio_buffer), data, n_samples * 2 * 2);
  audio_frame->Release ();

  GST_BUFFER_TIMESTAMP (audio_buffer) =
    gst_util_uint64_scale_int (decklinksrc->num_audio_samples * GST_SECOND,
        1, 48000);
  GST_BUFFER_DURATION (audio_buffer) =
    gst_util_uint64_scale_int ((decklinksrc->num_audio_samples + n_samples) * GST_SECOND,
        1, 48000) - GST_BUFFER_TIMESTAMP (audio_buffer);
  decklinksrc->num_audio_samples += n_samples;

  if (decklinksrc->audio_caps == NULL) {
    decklinksrc->audio_caps = gst_caps_new_simple ("audio/x-raw-int",
        "endianness", G_TYPE_INT, G_LITTLE_ENDIAN,
        "signed", G_TYPE_BOOLEAN, TRUE,
        "depth", G_TYPE_INT, 16,
        "width", G_TYPE_INT, 16,
        "channels", G_TYPE_INT, 2,
        "rate", G_TYPE_INT, 48000,
        NULL);
  }
  gst_buffer_set_caps (audio_buffer, decklinksrc->audio_caps);

  ret = gst_pad_push (decklinksrc->audiosrcpad, audio_buffer);
  if (ret != GST_FLOW_OK) {
    GST_ELEMENT_ERROR(decklinksrc, CORE, NEGOTIATION, (NULL), (NULL));
  }
}


