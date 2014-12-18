/* GStreamer
 * Copyright (C) 2011 David Schleef <ds@entropywave.com>
 * Copyright (C) 2014 Sebastian Dröge <sebastian@centricular.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstdecklinkvideosrc.h"
#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_decklink_video_src_debug);
#define GST_CAT_DEFAULT gst_decklink_video_src_debug

enum
{
  PROP_0,
  PROP_MODE,
  PROP_DEVICE_NUMBER
};

static void gst_decklink_video_src_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_decklink_video_src_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_decklink_video_src_finalize (GObject * object);

static GstStateChangeReturn
gst_decklink_video_src_change_state (GstElement * element,
    GstStateChange transition);
static GstClock *gst_decklink_video_src_provide_clock (GstElement * element);

static GstCaps *gst_decklink_video_src_get_caps (GstBaseSrc * bsrc,
    GstCaps * filter);
static gboolean gst_decklink_video_src_unlock (GstBaseSrc * bsrc);
static gboolean gst_decklink_video_src_unlock_stop (GstBaseSrc * bsrc);

static GstFlowReturn gst_decklink_video_src_create (GstPushSrc * psrc,
    GstBuffer ** buffer);

static gboolean gst_decklink_video_src_open (GstDecklinkVideoSrc * self);
static gboolean gst_decklink_video_src_close (GstDecklinkVideoSrc * self);

#define parent_class gst_decklink_video_src_parent_class
G_DEFINE_TYPE (GstDecklinkVideoSrc, gst_decklink_video_src, GST_TYPE_PUSH_SRC);

static void
gst_decklink_video_src_class_init (GstDecklinkVideoSrcClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseSrcClass *basesrc_class = GST_BASE_SRC_CLASS (klass);
  GstPushSrcClass *pushsrc_class = GST_PUSH_SRC_CLASS (klass);
  GstCaps *templ_caps;

  gobject_class->set_property = gst_decklink_video_src_set_property;
  gobject_class->get_property = gst_decklink_video_src_get_property;
  gobject_class->finalize = gst_decklink_video_src_finalize;

  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_decklink_video_src_change_state);
  element_class->provide_clock =
      GST_DEBUG_FUNCPTR (gst_decklink_video_src_provide_clock);

  basesrc_class->get_caps = GST_DEBUG_FUNCPTR (gst_decklink_video_src_get_caps);
  basesrc_class->unlock = GST_DEBUG_FUNCPTR (gst_decklink_video_src_unlock);
  basesrc_class->unlock_stop =
      GST_DEBUG_FUNCPTR (gst_decklink_video_src_unlock_stop);

  pushsrc_class->create = GST_DEBUG_FUNCPTR (gst_decklink_video_src_create);

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

  templ_caps = gst_decklink_mode_get_template_caps ();
  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS, templ_caps));
  gst_caps_unref (templ_caps);

  gst_element_class_set_static_metadata (element_class, "Decklink Video Source",
      "Video/Src", "Decklink Source", "David Schleef <ds@entropywave.com>, "
      "Sebastian Dröge <sebastian@centricular.com>");

  GST_DEBUG_CATEGORY_INIT (gst_decklink_video_src_debug, "decklinkvideosrc",
      0, "debug category for decklinkvideosrc element");
}

static void
gst_decklink_video_src_init (GstDecklinkVideoSrc * self)
{
  self->mode = GST_DECKLINK_MODE_NTSC;
  self->device_number = 0;

  gst_base_src_set_live (GST_BASE_SRC (self), TRUE);
  gst_base_src_set_format (GST_BASE_SRC (self), GST_FORMAT_TIME);

  g_mutex_init (&self->lock);
  g_cond_init (&self->cond);
}

void
gst_decklink_video_src_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDecklinkVideoSrc *self = GST_DECKLINK_VIDEO_SRC_CAST (object);

  switch (property_id) {
    case PROP_MODE:
      self->mode = (GstDecklinkModeEnum) g_value_get_enum (value);
      break;
    case PROP_DEVICE_NUMBER:
      self->device_number = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_decklink_video_src_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstDecklinkVideoSrc *self = GST_DECKLINK_VIDEO_SRC_CAST (object);

  switch (property_id) {
    case PROP_MODE:
      g_value_set_enum (value, self->mode);
      break;
    case PROP_DEVICE_NUMBER:
      g_value_set_int (value, self->device_number);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_decklink_video_src_finalize (GObject * object)
{
  GstDecklinkVideoSrc *self = GST_DECKLINK_VIDEO_SRC_CAST (object);

  g_mutex_clear (&self->lock);
  g_cond_clear (&self->cond);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstCaps *
gst_decklink_video_src_get_caps (GstBaseSrc * bsrc, GstCaps * filter)
{
  GstDecklinkVideoSrc *self = GST_DECKLINK_VIDEO_SRC_CAST (bsrc);
  GstCaps *mode_caps, *caps;

  mode_caps = gst_decklink_mode_get_caps (self->mode);
  if (filter) {
    caps =
        gst_caps_intersect_full (filter, mode_caps, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (mode_caps);
  } else {
    caps = mode_caps;
  }

  return caps;
}

static void
gst_decklink_video_src_got_frame (GstElement * element,
    IDeckLinkVideoInputFrame * frame, GstClockTime capture_time)
{
  GstDecklinkVideoSrc *self = GST_DECKLINK_VIDEO_SRC_CAST (element);

  GST_LOG_OBJECT (self, "Got video frame at %" GST_TIME_FORMAT, GST_TIME_ARGS (capture_time));

  g_mutex_lock (&self->lock);
  if (!self->flushing) {
    if (self->current_frame)
      self->current_frame->Release ();
    self->current_frame = frame;
    frame->AddRef ();
    self->current_frame_capture_time = capture_time;
    g_cond_signal (&self->cond);
  }
  g_mutex_unlock (&self->lock);
}

typedef struct
{
  IDeckLinkVideoInputFrame *frame;
  IDeckLinkInput *input;
} VideoFrame;

static void
video_frame_free (void *data)
{
  VideoFrame *video_frame = (VideoFrame *) data;

  video_frame->frame->Release ();
  video_frame->input->Release ();
  g_free (video_frame);
}

static GstFlowReturn
gst_decklink_video_src_create (GstPushSrc * bsrc, GstBuffer ** buffer)
{
  GstDecklinkVideoSrc *self = GST_DECKLINK_VIDEO_SRC_CAST (bsrc);
  GstFlowReturn flow_ret = GST_FLOW_OK;
  IDeckLinkVideoInputFrame *frame = NULL;
  GstClockTime capture_time = GST_CLOCK_TIME_NONE;
  const guint8 *data;
  gsize data_size;
  VideoFrame *vf;

  g_mutex_lock (&self->lock);
  while (!self->current_frame && !self->flushing) {
    g_cond_wait (&self->cond, &self->lock);
  }
  frame = self->current_frame;
  capture_time = self->current_frame_capture_time;
  self->current_frame = NULL;
  g_mutex_unlock (&self->lock);

  if (self->flushing) {
    if (frame)
      frame->Release ();
    return GST_FLOW_FLUSHING;
  }

  frame->GetBytes ((gpointer *) & data);

  data_size = self->info.size;


  vf = (VideoFrame *) g_malloc0 (sizeof (VideoFrame));

  *buffer =
      gst_buffer_new_wrapped_full ((GstMemoryFlags) GST_MEMORY_FLAG_READONLY,
      (gpointer) data, data_size, 0, data_size, vf,
      (GDestroyNotify) video_frame_free);

  vf->frame = frame;
  vf->input = self->input->input;
  vf->input->AddRef ();

  GST_BUFFER_TIMESTAMP (*buffer) = capture_time;
  GST_BUFFER_DURATION (*buffer) = gst_util_uint64_scale_int (GST_SECOND,
      self->info.fps_d, self->info.fps_n);

  return flow_ret;
}

static gboolean
gst_decklink_video_src_unlock (GstBaseSrc * bsrc)
{
  GstDecklinkVideoSrc *self = GST_DECKLINK_VIDEO_SRC_CAST (bsrc);

  g_mutex_lock (&self->lock);
  self->flushing = TRUE;
  g_cond_signal (&self->cond);
  g_mutex_unlock (&self->lock);

  return TRUE;
}

static gboolean
gst_decklink_video_src_unlock_stop (GstBaseSrc * bsrc)
{
  GstDecklinkVideoSrc *self = GST_DECKLINK_VIDEO_SRC_CAST (bsrc);

  g_mutex_lock (&self->lock);
  self->flushing = FALSE;
  g_mutex_unlock (&self->lock);

  return TRUE;
}

static gboolean
gst_decklink_video_src_open (GstDecklinkVideoSrc * self)
{
  const GstDecklinkMode *mode;
  GstCaps *caps;
  HRESULT ret;

  GST_DEBUG_OBJECT (self, "Starting");

  self->input =
      gst_decklink_acquire_nth_input (self->device_number,
      GST_ELEMENT_CAST (self), FALSE);
  if (!self->input) {
    GST_ERROR_OBJECT (self, "Failed to acquire input");
    return FALSE;
  }

  mode = gst_decklink_get_mode (self->mode);
  g_assert (mode != NULL);

  ret = self->input->input->EnableVideoInput (mode->mode,
      bmdFormat8BitYUV, bmdVideoOutputFlagDefault);
  if (ret != S_OK) {
    GST_WARNING_OBJECT (self, "Failed to enable video input");
    gst_decklink_release_nth_input (self->device_number,
        GST_ELEMENT_CAST (self), FALSE);
    return FALSE;
  }

  g_mutex_lock (&self->input->lock);
  self->input->got_video_frame = gst_decklink_video_src_got_frame;
  g_mutex_unlock (&self->input->lock);

  caps = gst_decklink_mode_get_caps (self->mode);
  gst_video_info_from_caps (&self->info, caps);
  gst_caps_unref (caps);

  return TRUE;
}

static gboolean
gst_decklink_video_src_close (GstDecklinkVideoSrc * self)
{

  GST_DEBUG_OBJECT (self, "Stopping");

  if (self->input) {
    g_mutex_lock (&self->input->lock);
    self->input->got_video_frame = NULL;
    g_mutex_unlock (&self->input->lock);

    self->input->input->DisableVideoInput ();
    gst_decklink_release_nth_input (self->device_number,
        GST_ELEMENT_CAST (self), FALSE);
    self->input = NULL;
  }

  return TRUE;
}

static GstStateChangeReturn
gst_decklink_video_src_change_state (GstElement * element,
    GstStateChange transition)
{
  GstDecklinkVideoSrc *self = GST_DECKLINK_VIDEO_SRC_CAST (element);
  GstStateChangeReturn ret;


  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_decklink_video_src_open (self)) {
        ret = GST_STATE_CHANGE_FAILURE;
        goto out;
      }
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_element_post_message (element,
          gst_message_new_clock_provide (GST_OBJECT_CAST (element),
              self->input->clock, TRUE));
      self->flushing = FALSE;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:{
      GstClock *clock;

      clock = gst_element_get_clock (GST_ELEMENT_CAST (self));
      if (clock && clock != self->input->clock) {
        gst_clock_set_master (self->input->clock, clock);
      }
      if (clock)
        gst_object_unref (clock);

      break;
    }
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_element_post_message (element,
          gst_message_new_clock_lost (GST_OBJECT_CAST (element),
              self->input->clock));
      gst_clock_set_master (self->input->clock, NULL);
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:{
      HRESULT res;

      GST_DEBUG_OBJECT (self, "Stopping streams");
      res = self->input->input->StopStreams ();
      if (res != S_OK) {
        GST_ELEMENT_ERROR (self, STREAM, FAILED,
            (NULL), ("Failed to stop streams: 0x%08x", res));
        ret = GST_STATE_CHANGE_FAILURE;
      }

      if (self->current_frame) {
        self->current_frame->Release ();
        self->current_frame = NULL;
      }
      break;
    }
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:{
      HRESULT res;

      GST_DEBUG_OBJECT (self, "Starting streams");
      res = self->input->input->StartStreams ();
      if (res != S_OK) {
        GST_ELEMENT_ERROR (self, STREAM, FAILED,
            (NULL), ("Failed to start streams: 0x%08x", res));
        ret = GST_STATE_CHANGE_FAILURE;
      }
      break;
    }
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_decklink_video_src_close (self);
      break;
    default:
      break;
  }
out:

  return ret;
}

static GstClock *
gst_decklink_video_src_provide_clock (GstElement * element)
{
  GstDecklinkVideoSrc *self = GST_DECKLINK_VIDEO_SRC_CAST (element);

  if (!self->input)
    return NULL;

  return GST_CLOCK_CAST (gst_object_ref (self->input->clock));
}
