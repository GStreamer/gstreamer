/* GStreamer
 * Copyright (C) 2008 David Schleef <ds@schleef.org>
 * Copyright (C) 2012 Collabora Ltd.
 *	Author : Edward Hervey <edward@collabora.com>
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

#include "gstvideoutils.h"

#include <string.h>

GType
gst_video_codec_frame_get_type (void)
{
  static volatile gsize type = 0;

  if (g_once_init_enter (&type)) {
    GType _type;

    _type = g_boxed_type_register_static ("GstVideoCodecFrame",
        (GBoxedCopyFunc) gst_video_codec_frame_ref,
        (GBoxedFreeFunc) gst_video_codec_frame_unref);
    g_once_init_leave (&type, _type);
  }
  return (GType) type;
}



static void
_gst_video_codec_frame_free (GstVideoCodecFrame * frame)
{
  g_return_if_fail (frame != NULL);

  if (frame->input_buffer) {
    gst_buffer_unref (frame->input_buffer);
  }

  if (frame->output_buffer) {
    gst_buffer_unref (frame->output_buffer);
  }

  g_list_foreach (frame->events, (GFunc) gst_event_unref, NULL);
  g_list_free (frame->events);

  if (frame->coder_hook_destroy_notify && frame->coder_hook)
    frame->coder_hook_destroy_notify (frame->coder_hook);

  g_slice_free (GstVideoCodecFrame, frame);
}

/**
 * gst_video_codec_frame_set_hook:
 * @frame: a #GstVideoCodecFrame
 * @hook: private data
 * @notify: (closure hook): a #GDestroyNotify
 *
 * Sets the #GDestroyNotify that will be called (along with the @hook) when
 * the frame is freed.
 *
 * If a @hook was previously set, then the previous set @notify will be called
 * before the @hook is replaced.
 */
void
gst_video_codec_frame_set_hook (GstVideoCodecFrame * frame, void *hook,
    GDestroyNotify notify)
{
  if (frame->coder_hook_destroy_notify && frame->coder_hook)
    frame->coder_hook_destroy_notify (frame->coder_hook);

  frame->coder_hook = hook;
  frame->coder_hook_destroy_notify = notify;
}

/**
 * gst_video_codec_frame_ref:
 * @frame: a #GstVideoCodecFrame
 *
 * Increases the refcount of the given frame by one.
 *
 * Returns: @buf
 */
GstVideoCodecFrame *
gst_video_codec_frame_ref (GstVideoCodecFrame * frame)
{
  g_return_val_if_fail (frame != NULL, NULL);

  g_atomic_int_inc (&frame->ref_count);

  return frame;
}

/**
 * gst_video_codec_frame_unref:
 * @frame: a #GstVideoCodecFrame
 *
 * Decreases the refcount of the frame. If the refcount reaches 0, the frame
 * will be freed.
 */
void
gst_video_codec_frame_unref (GstVideoCodecFrame * frame)
{
  g_return_if_fail (frame != NULL);
  g_return_if_fail (frame->ref_count > 0);

  if (g_atomic_int_dec_and_test (&frame->ref_count)) {
    _gst_video_codec_frame_free (frame);
  }
}


/**
 * gst_video_codec_state_ref:
 * @state: a #GstVideoCodecState
 *
 * Increases the refcount of the given state by one.
 *
 * Returns: @buf
 */
GstVideoCodecState *
gst_video_codec_state_ref (GstVideoCodecState * state)
{
  g_return_val_if_fail (state != NULL, NULL);

  g_atomic_int_inc (&state->ref_count);

  return state;
}

static void
_gst_video_codec_state_free (GstVideoCodecState * state)
{
  if (state->caps)
    gst_caps_unref (state->caps);
  if (state->codec_data)
    gst_buffer_unref (state->codec_data);
}

/**
 * gst_video_codec_state_unref:
 * @state: a #GstVideoCodecState
 *
 * Decreases the refcount of the state. If the refcount reaches 0, the state
 * will be freed.
 */
void
gst_video_codec_state_unref (GstVideoCodecState * state)
{
  g_return_if_fail (state != NULL);
  g_return_if_fail (state->ref_count > 0);

  if (g_atomic_int_dec_and_test (&state->ref_count)) {
    _gst_video_codec_state_free (state);
  }
}

GType
gst_video_codec_state_get_type (void)
{
  static volatile gsize type = 0;

  if (g_once_init_enter (&type)) {
    GType _type;

    _type = g_boxed_type_register_static ("GstVideoCodecState",
        (GBoxedCopyFunc) gst_video_codec_state_ref,
        (GBoxedFreeFunc) gst_video_codec_state_unref);
    g_once_init_leave (&type, _type);
  }
  return (GType) type;
}
