/* GStreamer
 * Copyright (C) 2006 David Schleef <ds@schleef.org>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstbasevideocodec.h"

#include <string.h>
#include <math.h>

GST_DEBUG_CATEGORY (basevideocodec_debug);
#define GST_CAT_DEFAULT basevideocodec_debug

/* GstBaseVideoCodec signals and args */
enum
{
  LAST_SIGNAL
};

enum
{
  ARG_0
};

static void gst_base_video_codec_finalize (GObject * object);

static GstStateChangeReturn gst_base_video_codec_change_state (GstElement *
    element, GstStateChange transition);


GST_BOILERPLATE (GstBaseVideoCodec, gst_base_video_codec, GstElement,
    GST_TYPE_ELEMENT);

static void
gst_base_video_codec_base_init (gpointer g_class)
{
  GST_DEBUG_CATEGORY_INIT (basevideocodec_debug, "basevideocodec", 0,
      "Base Video Codec");

}

static void
gst_base_video_codec_class_init (GstBaseVideoCodecClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;

  gobject_class = G_OBJECT_CLASS (klass);
  element_class = GST_ELEMENT_CLASS (klass);

  gobject_class->finalize = gst_base_video_codec_finalize;

  element_class->change_state = gst_base_video_codec_change_state;
}

static void
gst_base_video_codec_init (GstBaseVideoCodec * base_video_codec,
    GstBaseVideoCodecClass * klass)
{
  GstPadTemplate *pad_template;

  GST_DEBUG_OBJECT (base_video_codec, "gst_base_video_codec_init");

  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (klass), "sink");
  g_return_if_fail (pad_template != NULL);

  base_video_codec->sinkpad = gst_pad_new_from_template (pad_template, "sink");
  gst_element_add_pad (GST_ELEMENT (base_video_codec),
      base_video_codec->sinkpad);

  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (klass), "src");
  g_return_if_fail (pad_template != NULL);

  base_video_codec->srcpad = gst_pad_new_from_template (pad_template, "src");
  gst_element_add_pad (GST_ELEMENT (base_video_codec),
      base_video_codec->srcpad);

  gst_segment_init (&base_video_codec->segment, GST_FORMAT_TIME);

  g_static_rec_mutex_init (&base_video_codec->stream_lock);
}

static void
gst_base_video_codec_reset (GstBaseVideoCodec * base_video_codec)
{
  GList *g;

  GST_DEBUG_OBJECT (base_video_codec, "reset");

  GST_BASE_VIDEO_CODEC_STREAM_LOCK (base_video_codec);
  for (g = base_video_codec->frames; g; g = g_list_next (g)) {
    gst_base_video_codec_free_frame ((GstVideoFrame *) g->data);
  }
  g_list_free (base_video_codec->frames);
  base_video_codec->frames = NULL;

  base_video_codec->bytes = 0;
  base_video_codec->time = 0;

  gst_buffer_replace (&base_video_codec->state.codec_data, NULL);
  gst_caps_replace (&base_video_codec->state.caps, NULL);
  memset (&base_video_codec->state, 0, sizeof (GstVideoState));
  base_video_codec->state.format = GST_VIDEO_FORMAT_UNKNOWN;
  GST_BASE_VIDEO_CODEC_STREAM_UNLOCK (base_video_codec);
}

static void
gst_base_video_codec_finalize (GObject * object)
{
  GstBaseVideoCodec *base_video_codec = GST_BASE_VIDEO_CODEC (object);

  g_static_rec_mutex_free (&base_video_codec->stream_lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstStateChangeReturn
gst_base_video_codec_change_state (GstElement * element,
    GstStateChange transition)
{
  GstBaseVideoCodec *base_video_codec = GST_BASE_VIDEO_CODEC (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_base_video_codec_reset (base_video_codec);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  ret = parent_class->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_base_video_codec_reset (base_video_codec);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}

GstVideoFrame *
gst_base_video_codec_new_frame (GstBaseVideoCodec * base_video_codec)
{
  GstVideoFrame *frame;

  frame = g_slice_new0 (GstVideoFrame);

  GST_BASE_VIDEO_CODEC_STREAM_LOCK (base_video_codec);
  frame->system_frame_number = base_video_codec->system_frame_number;
  base_video_codec->system_frame_number++;
  GST_BASE_VIDEO_CODEC_STREAM_UNLOCK (base_video_codec);

  return frame;
}

void
gst_base_video_codec_free_frame (GstVideoFrame * frame)
{
  g_return_if_fail (frame != NULL);

  if (frame->sink_buffer) {
    gst_buffer_unref (frame->sink_buffer);
  }

  if (frame->src_buffer) {
    gst_buffer_unref (frame->src_buffer);
  }

  g_list_foreach (frame->events, (GFunc) gst_event_unref, NULL);
  g_list_free (frame->events);

  if (frame->coder_hook_destroy_notify && frame->coder_hook)
    frame->coder_hook_destroy_notify (frame->coder_hook);

  g_slice_free (GstVideoFrame, frame);
}
