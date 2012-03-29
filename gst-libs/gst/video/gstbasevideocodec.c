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

/**
 * SECTION:gstbasevideocodec
 * @short_description: Base class and objects for video codecs
 *
 **/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/**
 * SECTION:gstbasevideocodec
 * @short_description: Base class for video codecs
 * @see_also: #GstBaseVideoDecoder , #GstBaseVideoEncoder
 */

/* FIXME 0.11: suppress warnings for deprecated API such as GStaticRecMutex
 * with newer GLib versions (>= 2.31.0) */
#define GLIB_DISABLE_DEPRECATION_WARNINGS

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

static GstElementClass *parent_class = NULL;

G_DEFINE_BOXED_TYPE (GstVideoFrameState, gst_video_frame_state,
    (GBoxedCopyFunc) gst_video_frame_state_ref,
    (GBoxedFreeFunc) gst_video_frame_state_unref);

/* NOTE (Edward): Do not use G_DEFINE_* because we need to have
 * a GClassInitFunc called with the target class (which the macros
 * don't handle).
 */
static void gst_base_video_codec_class_init (GstBaseVideoCodecClass * klass);
static void gst_base_video_codec_init (GstBaseVideoCodec * dec,
    GstBaseVideoCodecClass * klass);

GType
gst_base_video_codec_get_type (void)
{
  static volatile gsize base_video_codec_type = 0;

  if (g_once_init_enter (&base_video_codec_type)) {
    GType _type;
    static const GTypeInfo base_video_codec_info = {
      sizeof (GstBaseVideoCodecClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_base_video_codec_class_init,
      NULL,
      NULL,
      sizeof (GstBaseVideoCodec),
      0,
      (GInstanceInitFunc) gst_base_video_codec_init,
    };

    _type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstBaseVideoCodec", &base_video_codec_info, G_TYPE_FLAG_ABSTRACT);
    g_once_init_leave (&base_video_codec_type, _type);
  }
  return base_video_codec_type;
}

static void
gst_base_video_codec_class_init (GstBaseVideoCodecClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;

  gobject_class = G_OBJECT_CLASS (klass);
  element_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = gst_base_video_codec_finalize;

  element_class->change_state = gst_base_video_codec_change_state;

  GST_DEBUG_CATEGORY_INIT (basevideocodec_debug, "basevideocodec", 0,
      "Base Video Codec");
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

  g_rec_mutex_init (&base_video_codec->stream_lock);
}

static void
gst_base_video_codec_reset (GstBaseVideoCodec * base_video_codec)
{
  GList *g;

  GST_DEBUG_OBJECT (base_video_codec, "reset");

  GST_BASE_VIDEO_CODEC_STREAM_LOCK (base_video_codec);
  for (g = base_video_codec->frames; g; g = g_list_next (g)) {
    gst_video_frame_state_unref ((GstVideoFrameState *) g->data);
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

  g_rec_mutex_clear (&base_video_codec->stream_lock);

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

/**
 * gst_base_video_codec_append_frame:
 * @codec: a #GstBaseVideoCodec
 * @frame: the #GstVideoFrameState to append
 *
 * Appends a frame to the list of frames handled by the codec.
 *
 * Note: This should normally not be used by implementations.
 **/
void
gst_base_video_codec_append_frame (GstBaseVideoCodec * codec,
    GstVideoFrameState * frame)
{
  g_return_if_fail (frame != NULL);

  gst_video_frame_state_ref (frame);
  codec->frames = g_list_append (codec->frames, frame);
}

void
gst_base_video_codec_remove_frame (GstBaseVideoCodec * codec,
    GstVideoFrameState * frame)
{
  GList *link;

  g_return_if_fail (frame != NULL);

  link = g_list_find (codec->frames, frame);
  if (link) {
    gst_video_frame_state_unref ((GstVideoFrameState *) link->data);
    codec->frames = g_list_delete_link (codec->frames, link);
  }
}

static void
_gst_video_frame_state_free (GstVideoFrameState * frame)
{
  g_return_if_fail (frame != NULL);

  GST_LOG ("Freeing frame %p (sfn:%d)", frame, frame->system_frame_number);

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

  g_slice_free (GstVideoFrameState, frame);
}

/**
 * gst_base_video_codec_new_frame:
 * @base_video_codec: a #GstBaseVideoCodec
 *
 * Creates a new #GstVideoFrameState for usage in decoders or encoders.
 *
 * Returns: (transfer full): The new #GstVideoFrameState, call
 * #gst_video_frame_state_unref() when done with it.
 */
GstVideoFrameState *
gst_base_video_codec_new_frame (GstBaseVideoCodec * base_video_codec)
{
  GstVideoFrameState *frame;

  frame = g_slice_new0 (GstVideoFrameState);

  frame->ref_count = 1;

  GST_BASE_VIDEO_CODEC_STREAM_LOCK (base_video_codec);
  frame->system_frame_number = base_video_codec->system_frame_number;
  base_video_codec->system_frame_number++;
  GST_BASE_VIDEO_CODEC_STREAM_UNLOCK (base_video_codec);

  GST_LOG_OBJECT (base_video_codec, "Created new frame %p (sfn:%d)",
      frame, frame->system_frame_number);

  return frame;
}

/**
 * gst_video_frame_state_ref:
 * @frame: a #GstVideoFrameState
 *
 * Increases the refcount of the given frame by one.
 *
 * Returns: @buf
 */
GstVideoFrameState *
gst_video_frame_state_ref (GstVideoFrameState * frame)
{
  g_return_val_if_fail (frame != NULL, NULL);

  g_atomic_int_inc (&frame->ref_count);

  return frame;
}

/**
 * gst_video_frame_state_unref:
 * @frame: a #GstVideoFrameState
 *
 * Decreases the refcount of the frame. If the refcount reaches 0, the frame
 * will be freed.
 */
void
gst_video_frame_state_unref (GstVideoFrameState * frame)
{
  g_return_if_fail (frame != NULL);
  g_return_if_fail (frame->ref_count > 0);

  if (g_atomic_int_dec_and_test (&frame->ref_count)) {
    _gst_video_frame_state_free (frame);
  }
}
