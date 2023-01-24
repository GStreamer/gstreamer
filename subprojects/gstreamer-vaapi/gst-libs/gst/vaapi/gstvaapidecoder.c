/*
 *  gstvaapidecoder.c - VA decoder abstraction
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@splitted-desktop.com>
 *  Copyright (C) 2011-2014 Intel Corporation
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

/**
 * SECTION:gstvaapidecoder
 * @short_description: VA decoder abstraction
 */

#include "sysdeps.h"
#include "gstvaapicompat.h"
#include "gstvaapidecoder.h"
#include "gstvaapidecoder_priv.h"
#include "gstvaapiparser_frame.h"
#include "gstvaapisurfaceproxy_priv.h"
#include "gstvaapiutils.h"

#define DEBUG 1
#include "gstvaapidebug.h"

enum
{
  PROP_DISPLAY = 1,
  PROP_CAPS,
  N_PROPERTIES
};
static GParamSpec *g_properties[N_PROPERTIES] = { NULL, };

G_DEFINE_TYPE (GstVaapiDecoder, gst_vaapi_decoder, GST_TYPE_OBJECT);

static void drop_frame (GstVaapiDecoder * decoder, GstVideoCodecFrame * frame);

static void
parser_state_reset (GstVaapiParserState * ps)
{

  if (ps->input_adapter)
    gst_adapter_clear (ps->input_adapter);
  if (ps->output_adapter)
    gst_adapter_clear (ps->output_adapter);
  ps->current_adapter = NULL;

  if (ps->next_unit_pending) {
    gst_vaapi_decoder_unit_clear (&ps->next_unit);
    ps->next_unit_pending = FALSE;
  }

  ps->current_frame_number = 0;
  ps->input_offset1 = ps->input_offset2 = 0;
  ps->at_eos = FALSE;
}

static void
parser_state_finalize (GstVaapiParserState * ps)
{
  if (ps->input_adapter) {
    gst_adapter_clear (ps->input_adapter);
    g_object_unref (ps->input_adapter);
    ps->input_adapter = NULL;
  }

  if (ps->output_adapter) {
    gst_adapter_clear (ps->output_adapter);
    g_object_unref (ps->output_adapter);
    ps->output_adapter = NULL;
  }

  if (ps->next_unit_pending) {
    gst_vaapi_decoder_unit_clear (&ps->next_unit);
    ps->next_unit_pending = FALSE;
  }
}

static gboolean
parser_state_init (GstVaapiParserState * ps)
{
  memset (ps, 0, sizeof (*ps));

  ps->input_adapter = gst_adapter_new ();
  if (!ps->input_adapter)
    return FALSE;

  ps->output_adapter = gst_adapter_new ();
  if (!ps->output_adapter)
    return FALSE;
  return TRUE;
}

static void
parser_state_prepare (GstVaapiParserState * ps, GstAdapter * adapter)
{
  /* XXX: check we really have a continuity from the previous call */
  if (ps->current_adapter != adapter)
    goto reset;
  return;

reset:
  ps->current_adapter = adapter;
  ps->input_offset1 = -1;
  ps->input_offset2 = -1;
}

static gboolean
push_buffer (GstVaapiDecoder * decoder, GstBuffer * buffer)
{
  if (!buffer) {
    buffer = gst_buffer_new ();
    if (!buffer)
      return FALSE;
    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_EOS);
  }

  GST_DEBUG ("queue encoded data buffer %p (%zu bytes)",
      buffer, gst_buffer_get_size (buffer));

  g_async_queue_push (decoder->buffers, buffer);
  return TRUE;
}

static GstBuffer *
pop_buffer (GstVaapiDecoder * decoder)
{
  GstBuffer *buffer;

  buffer = g_async_queue_try_pop (decoder->buffers);
  if (!buffer)
    return NULL;

  GST_DEBUG ("dequeue buffer %p for decoding (%zu bytes)",
      buffer, gst_buffer_get_size (buffer));

  return buffer;
}

static GstVaapiDecoderStatus
do_parse (GstVaapiDecoder * decoder,
    GstVideoCodecFrame * base_frame, GstAdapter * adapter, gboolean at_eos,
    guint * got_unit_size_ptr, gboolean * got_frame_ptr)
{
  GstVaapiParserState *const ps = &decoder->parser_state;
  GstVaapiParserFrame *frame;
  GstVaapiDecoderUnit *unit;
  GstVaapiDecoderStatus status;

  *got_unit_size_ptr = 0;
  *got_frame_ptr = FALSE;

  frame = gst_video_codec_frame_get_user_data (base_frame);
  if (!frame) {
    GstVideoCodecState *const codec_state = decoder->codec_state;
    frame = gst_vaapi_parser_frame_new (codec_state->info.width,
        codec_state->info.height);
    if (!frame)
      return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
    gst_video_codec_frame_set_user_data (base_frame,
        frame, (GDestroyNotify) gst_vaapi_mini_object_unref);
  }

  parser_state_prepare (ps, adapter);

  unit = &ps->next_unit;
  if (ps->next_unit_pending) {
    ps->next_unit_pending = FALSE;
    goto got_unit;
  }
  gst_vaapi_decoder_unit_init (unit);

  ps->current_frame = base_frame;
  status = GST_VAAPI_DECODER_GET_CLASS (decoder)->parse (decoder,
      adapter, at_eos, unit);
  if (status != GST_VAAPI_DECODER_STATUS_SUCCESS) {
    if (at_eos && frame->units->len > 0 &&
        status == GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA) {
      /* XXX: assume the frame is complete at <EOS> */
      *got_frame_ptr = TRUE;
      return GST_VAAPI_DECODER_STATUS_SUCCESS;
    }
    return status;
  }

  if (GST_VAAPI_DECODER_UNIT_IS_FRAME_START (unit) && frame->units->len > 0) {
    ps->next_unit_pending = TRUE;
    *got_frame_ptr = TRUE;
    return GST_VAAPI_DECODER_STATUS_SUCCESS;
  }

got_unit:
  gst_vaapi_parser_frame_append_unit (frame, unit);
  *got_unit_size_ptr = unit->size;
  *got_frame_ptr = GST_VAAPI_DECODER_UNIT_IS_FRAME_END (unit);
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
do_decode_units (GstVaapiDecoder * decoder, GArray * units)
{
  GstVaapiDecoderClass *const klass = GST_VAAPI_DECODER_GET_CLASS (decoder);
  GstVaapiDecoderStatus status;
  guint i;

  for (i = 0; i < units->len; i++) {
    GstVaapiDecoderUnit *const unit =
        &g_array_index (units, GstVaapiDecoderUnit, i);
    if (GST_VAAPI_DECODER_UNIT_IS_SKIPPED (unit))
      continue;
    status = klass->decode (decoder, unit);
    if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
      return status;
  }
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
do_decode_1 (GstVaapiDecoder * decoder, GstVaapiParserFrame * frame)
{
  GstVaapiDecoderClass *const klass = GST_VAAPI_DECODER_GET_CLASS (decoder);
  GstVaapiDecoderStatus status;

  if (frame->pre_units->len > 0) {
    status = do_decode_units (decoder, frame->pre_units);
    if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
      return status;
  }

  if (frame->units->len > 0) {
    if (klass->start_frame) {
      GstVaapiDecoderUnit *const unit =
          &g_array_index (frame->units, GstVaapiDecoderUnit, 0);
      status = klass->start_frame (decoder, unit);
      if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
        return status;
    }

    status = do_decode_units (decoder, frame->units);
    if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
      return status;

    if (klass->end_frame) {
      status = klass->end_frame (decoder);
      if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
        return status;
    }
  }

  if (frame->post_units->len > 0) {
    status = do_decode_units (decoder, frame->post_units);
    if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
      return status;
  }

  /* Drop frame if there is no slice data unit in there */
  if (G_UNLIKELY (frame->units->len == 0))
    return (GstVaapiDecoderStatus) GST_VAAPI_DECODER_STATUS_DROP_FRAME;
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static inline GstVaapiDecoderStatus
do_decode (GstVaapiDecoder * decoder, GstVideoCodecFrame * base_frame)
{
  GstVaapiParserState *const ps = &decoder->parser_state;
  GstVaapiParserFrame *const frame = base_frame->user_data;
  GstVaapiDecoderStatus status;

  ps->current_frame = base_frame;

  gst_vaapi_parser_frame_ref (frame);
  status = do_decode_1 (decoder, frame);
  gst_vaapi_parser_frame_unref (frame);

  switch ((guint) status) {
    case GST_VAAPI_DECODER_STATUS_DROP_FRAME:
      drop_frame (decoder, base_frame);
      status = GST_VAAPI_DECODER_STATUS_SUCCESS;
      break;
  }
  return status;
}

static GstVaapiDecoderStatus
decode_step (GstVaapiDecoder * decoder)
{
  GstVaapiParserState *const ps = &decoder->parser_state;
  GstVaapiDecoderStatus status;
  GstBuffer *buffer;
  gboolean got_frame;
  guint got_unit_size, input_size;

  /* Fill adapter with all buffers we have in the queue */
  for (;;) {
    buffer = pop_buffer (decoder);
    if (!buffer)
      break;

    ps->at_eos = GST_BUFFER_IS_EOS (buffer);
    if (!ps->at_eos)
      gst_adapter_push (ps->input_adapter, buffer);
  }

  /* Parse and decode all decode units */
  input_size = gst_adapter_available (ps->input_adapter);
  if (input_size == 0) {
    if (ps->at_eos)
      return GST_VAAPI_DECODER_STATUS_END_OF_STREAM;
    return GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;
  }

  do {
    if (!ps->current_frame) {
      ps->current_frame = g_new0 (GstVideoCodecFrame, 1);
      if (!ps->current_frame)
        return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
      ps->current_frame->ref_count = 1;
      ps->current_frame->system_frame_number = ps->current_frame_number++;
    }

    status = do_parse (decoder, ps->current_frame, ps->input_adapter,
        ps->at_eos, &got_unit_size, &got_frame);
    GST_DEBUG ("parse frame (status = %d)", status);
    if (status != GST_VAAPI_DECODER_STATUS_SUCCESS) {
      if (status == GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA && ps->at_eos)
        status = GST_VAAPI_DECODER_STATUS_END_OF_STREAM;
      break;
    }

    if (got_unit_size > 0) {
      buffer = gst_adapter_take_buffer (ps->input_adapter, got_unit_size);
      input_size -= got_unit_size;

      if (gst_adapter_available (ps->output_adapter) == 0) {
        ps->current_frame->pts = gst_adapter_prev_pts (ps->input_adapter, NULL);
      }
      gst_adapter_push (ps->output_adapter, buffer);
    }

    if (got_frame) {
      ps->current_frame->input_buffer =
          gst_adapter_take_buffer (ps->output_adapter,
          gst_adapter_available (ps->output_adapter));

      status = do_decode (decoder, ps->current_frame);
      GST_DEBUG ("decode frame (status = %d)", status);

      gst_video_codec_frame_unref (ps->current_frame);
      ps->current_frame = NULL;
      break;
    }
  } while (input_size > 0);
  return status;
}

static void
drop_frame (GstVaapiDecoder * decoder, GstVideoCodecFrame * frame)
{
  GST_DEBUG ("drop frame %d", frame->system_frame_number);

  /* no surface proxy */
  gst_video_codec_frame_set_user_data (frame, NULL, NULL);

  frame->pts = GST_CLOCK_TIME_NONE;
  GST_VIDEO_CODEC_FRAME_FLAG_SET (frame,
      GST_VIDEO_CODEC_FRAME_FLAG_DECODE_ONLY);

  g_async_queue_push (decoder->frames, gst_video_codec_frame_ref (frame));
}

static inline void
push_frame (GstVaapiDecoder * decoder, GstVideoCodecFrame * frame)
{
  GstVaapiSurfaceProxy *const proxy = frame->user_data;

  GST_DEBUG ("push frame %d (surface 0x%08x)", frame->system_frame_number,
      (guint32) GST_VAAPI_SURFACE_PROXY_SURFACE_ID (proxy));

  g_async_queue_push (decoder->frames, gst_video_codec_frame_ref (frame));
}

static inline GstVideoCodecFrame *
pop_frame (GstVaapiDecoder * decoder, guint64 timeout)
{
  GstVideoCodecFrame *frame;
  GstVaapiSurfaceProxy *proxy;

  if (G_LIKELY (timeout > 0))
    frame = g_async_queue_timeout_pop (decoder->frames, timeout);
  else
    frame = g_async_queue_try_pop (decoder->frames);
  if (!frame)
    return NULL;

  proxy = frame->user_data;
  GST_DEBUG ("pop frame %d (surface 0x%08x)", frame->system_frame_number,
      (proxy ? (guint32) GST_VAAPI_SURFACE_PROXY_SURFACE_ID (proxy) :
          VA_INVALID_ID));

  return frame;
}

static gboolean
set_caps (GstVaapiDecoder * decoder, const GstCaps * caps)
{
  GstVideoCodecState *const codec_state = decoder->codec_state;
  GstStructure *const structure = gst_caps_get_structure (caps, 0);
  const GValue *v_codec_data;

  decoder->codec = gst_vaapi_get_codec_from_caps (caps);
  if (!decoder->codec)
    return FALSE;

  if (!gst_video_info_from_caps (&codec_state->info, caps))
    return FALSE;

  if (codec_state->caps)
    gst_caps_unref (codec_state->caps);
  codec_state->caps = gst_caps_copy (caps);

  v_codec_data = gst_structure_get_value (structure, "codec_data");
  if (v_codec_data)
    gst_buffer_replace (&codec_state->codec_data,
        gst_value_get_buffer (v_codec_data));
  return TRUE;
}

static inline GstCaps *
get_caps (GstVaapiDecoder * decoder)
{
  return GST_VAAPI_DECODER_CODEC_STATE (decoder)->caps;
}

static void
notify_codec_state_changed (GstVaapiDecoder * decoder)
{
  if (decoder->codec_state_changed_func)
    decoder->codec_state_changed_func (decoder, decoder->codec_state,
        decoder->codec_state_changed_data);
}

static void
gst_vaapi_decoder_finalize (GObject * object)
{
  GstVaapiDecoder *const decoder = GST_VAAPI_DECODER (object);

  gst_video_codec_state_unref (decoder->codec_state);
  decoder->codec_state = NULL;

  parser_state_finalize (&decoder->parser_state);

  if (decoder->buffers) {
    g_async_queue_unref (decoder->buffers);
    decoder->buffers = NULL;
  }

  if (decoder->frames) {
    g_async_queue_unref (decoder->frames);
    decoder->frames = NULL;
  }

  if (decoder->context) {
    gst_vaapi_context_unref (decoder->context);
    decoder->context = NULL;
  }
  decoder->va_context = VA_INVALID_ID;

  gst_vaapi_display_replace (&decoder->display, NULL);
  decoder->va_display = NULL;

  G_OBJECT_CLASS (gst_vaapi_decoder_parent_class)->finalize (object);
}

static void
gst_vaapi_decoder_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVaapiDecoder *const decoder = GST_VAAPI_DECODER (object);

  switch (property_id) {
    case PROP_DISPLAY:
      g_assert (decoder->display == NULL);
      decoder->display = g_value_dup_object (value);
      g_assert (decoder->display != NULL);
      decoder->va_display = GST_VAAPI_DISPLAY_VADISPLAY (decoder->display);
      break;
    case PROP_CAPS:{
      GstCaps *caps = g_value_get_boxed (value);
      if (!set_caps (decoder, caps)) {
        GST_WARNING_OBJECT (decoder, "failed to set caps %" GST_PTR_FORMAT,
            caps);
      }
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
gst_vaapi_decoder_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstVaapiDecoder *const decoder = GST_VAAPI_DECODER (object);

  switch (property_id) {
    case PROP_DISPLAY:
      g_value_set_object (value, decoder->display);
      break;
    case PROP_CAPS:
      g_value_set_boxed (value, gst_caps_ref (get_caps (decoder)));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
gst_vaapi_decoder_class_init (GstVaapiDecoderClass * klass)
{
  GObjectClass *const object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = gst_vaapi_decoder_set_property;
  object_class->get_property = gst_vaapi_decoder_get_property;
  object_class->finalize = gst_vaapi_decoder_finalize;

  /**
   * GstVaapiDecoder:display:
   *
   * #GstVaapiDisplay to be used.
   */
  g_properties[PROP_DISPLAY] =
      g_param_spec_object ("display", "Gst VA-API Display",
      "The VA-API display object to use", GST_TYPE_VAAPI_DISPLAY,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_NAME);

  /**
   * GstCaps:caps:
   *
   * #GstCaps the caps describing the media to process.
   */
  g_properties[PROP_CAPS] =
      g_param_spec_boxed ("caps", "Caps",
      "The caps describing the media to process", GST_TYPE_CAPS,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_NAME);

  g_object_class_install_properties (object_class, N_PROPERTIES, g_properties);
}

static void
gst_vaapi_decoder_init (GstVaapiDecoder * decoder)
{
  GstVideoCodecState *codec_state;

  parser_state_init (&decoder->parser_state);

  codec_state = g_new0 (GstVideoCodecState, 1);
  codec_state->ref_count = 1;
  gst_video_info_init (&codec_state->info);

  decoder->va_context = VA_INVALID_ID;
  decoder->codec_state = codec_state;
  decoder->buffers = g_async_queue_new_full ((GDestroyNotify) gst_buffer_unref);
  decoder->frames = g_async_queue_new_full ((GDestroyNotify)
      gst_video_codec_frame_unref);
}

/**
 * gst_vaapi_decoder_replace:
 * @old_decoder_ptr: a pointer to a #GstVaapiDecoder
 * @new_decoder: a #GstVaapiDecoder
 *
 * Atomically replaces the decoder decoder held in @old_decoder_ptr
 * with @new_decoder. This means that @old_decoder_ptr shall reference
 * a valid decoder. However, @new_decoder can be NULL.
 */
void
gst_vaapi_decoder_replace (GstVaapiDecoder ** old_decoder_ptr,
    GstVaapiDecoder * new_decoder)
{
  gst_object_replace ((GstObject **) old_decoder_ptr, GST_OBJECT (new_decoder));
}

/**
 * gst_vaapi_decoder_get_user_data:
 * @decoder: a #GstVaapiDecoder
 *
 * Retrieves the user-defined data associated with the @decoder, if any.
 *
 * Return value: the user-defined data associated with the @decoder
 */
gpointer
gst_vaapi_decoder_get_user_data (GstVaapiDecoder * decoder)
{
  g_return_val_if_fail (decoder != NULL, NULL);

  return decoder->user_data;
}

/**
 * gst_vaapi_decoder_set_user_data:
 * @decoder: a #GstVaapiDecoder
 * @user_data: the pointer to user-defined data
 *
 * Associates user-defined @user_data to the @decoder. Retrieve the
 * attached value with gst_vaapi_decoder_get_user_data() function.
 */
void
gst_vaapi_decoder_set_user_data (GstVaapiDecoder * decoder, gpointer user_data)
{
  g_return_if_fail (decoder != NULL);

  decoder->user_data = user_data;
}

/**
 * gst_vaapi_decoder_get_codec:
 * @decoder: a #GstVaapiDecoder
 *
 * Retrieves the @decoder codec type.
 *
 * Return value: the #GstVaapiCodec type for @decoder
 */
GstVaapiCodec
gst_vaapi_decoder_get_codec (GstVaapiDecoder * decoder)
{
  g_return_val_if_fail (decoder != NULL, (GstVaapiCodec) 0);

  return decoder->codec;
}

/**
 * gst_vaapi_decoder_get_codec_state:
 * @decoder: a #GstVaapiDecoder
 *
 * Retrieves the @decoder codec state. The decoder owns the returned
 * #GstVideoCodecState structure, so use gst_video_codec_state_ref()
 * whenever necessary.
 *
 * Return value: the #GstVideoCodecState object for @decoder
 */
GstVideoCodecState *
gst_vaapi_decoder_get_codec_state (GstVaapiDecoder * decoder)
{
  g_return_val_if_fail (decoder != NULL, NULL);

  return GST_VAAPI_DECODER_CODEC_STATE (decoder);
}

/**
 * gst_vaapi_decoder_set_codec_state_changed_func:
 * @decoder: a #GstVaapiDecoder
 * @func: the function to call when codec state changed
 * @user_data: a pointer to user-defined data
 *
 * Sets @func as the function to call whenever the @decoder codec
 * state changes.
 */
void
gst_vaapi_decoder_set_codec_state_changed_func (GstVaapiDecoder * decoder,
    GstVaapiDecoderStateChangedFunc func, gpointer user_data)
{
  g_return_if_fail (decoder != NULL);

  decoder->codec_state_changed_func = func;
  decoder->codec_state_changed_data = user_data;
}

/**
 * gst_vaapi_decoder_get_caps:
 * @decoder: a #GstVaapiDecoder
 *
 * Retrieves the @decoder caps. The decoder owns the returned caps, so
 * use gst_caps_ref() whenever necessary.
 *
 * Returns: (transfer none): the @decoder caps
 */
GstCaps *
gst_vaapi_decoder_get_caps (GstVaapiDecoder * decoder)
{
  return get_caps (decoder);
}

/**
 * gst_vaapi_decoder_put_buffer:
 * @decoder: a #GstVaapiDecoder
 * @buf: a #GstBuffer
 *
 * Queues a #GstBuffer to the HW decoder. The decoder holds a
 * reference to @buf.
 *
 * Caller can notify an End-Of-Stream with @buf set to %NULL. However,
 * if an empty buffer is passed, i.e. a buffer with %NULL data pointer
 * or size equals to zero, then the function ignores this buffer and
 * returns %TRUE.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_decoder_put_buffer (GstVaapiDecoder * decoder, GstBuffer * buf)
{
  g_return_val_if_fail (decoder != NULL, FALSE);

  if (buf) {
    if (gst_buffer_get_size (buf) == 0)
      return TRUE;
    buf = gst_buffer_ref (buf);
  }
  return push_buffer (decoder, buf);
}

/**
 * gst_vaapi_decoder_get_surface:
 * @decoder: a #GstVaapiDecoder
 * @out_proxy_ptr: the next decoded surface as a #GstVaapiSurfaceProxy
 *
 * Flushes encoded buffers to the decoder and returns a decoded
 * surface, if any.
 *
 * On successful return, *@out_proxy_ptr contains the decoded surface
 * as a #GstVaapiSurfaceProxy. The caller owns this object, so
 * gst_vaapi_surface_proxy_unref() shall be called after usage.
 *
 * Return value: a #GstVaapiDecoderStatus
 */
GstVaapiDecoderStatus
gst_vaapi_decoder_get_surface (GstVaapiDecoder * decoder,
    GstVaapiSurfaceProxy ** out_proxy_ptr)
{
  GstVideoCodecFrame *frame;
  GstVaapiDecoderStatus status;

  g_return_val_if_fail (decoder != NULL,
      GST_VAAPI_DECODER_STATUS_ERROR_INVALID_PARAMETER);
  g_return_val_if_fail (out_proxy_ptr != NULL,
      GST_VAAPI_DECODER_STATUS_ERROR_INVALID_PARAMETER);

  do {
    frame = pop_frame (decoder, 0);
    while (frame) {
      if (!GST_VIDEO_CODEC_FRAME_IS_DECODE_ONLY (frame)) {
        GstVaapiSurfaceProxy *const proxy = frame->user_data;
        proxy->timestamp = frame->pts;
        proxy->duration = frame->duration;
        *out_proxy_ptr = gst_vaapi_surface_proxy_ref (proxy);
        gst_video_codec_frame_unref (frame);
        return GST_VAAPI_DECODER_STATUS_SUCCESS;
      }
      gst_video_codec_frame_unref (frame);
      frame = pop_frame (decoder, 0);
    }
    status = decode_step (decoder);
  } while (status == GST_VAAPI_DECODER_STATUS_SUCCESS);

  *out_proxy_ptr = NULL;
  return status;
}

/**
 * gst_vaapi_decoder_get_frame:
 * @decoder: a #GstVaapiDecoder
 * @out_frame_ptr: the next decoded frame as a #GstVideoCodecFrame
 *
 * On successful return, *@out_frame_ptr contains the next decoded
 * frame available as a #GstVideoCodecFrame. The caller owns this
 * object, so gst_video_codec_frame_unref() shall be called after
 * usage. Otherwise, @GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA is
 * returned if no decoded frame is available.
 *
 * The actual surface is available as a #GstVaapiSurfaceProxy attached
 * to the user-data anchor of the output frame. Ownership of the proxy
 * is transferred to the frame.
 *
 * This is equivalent to gst_vaapi_decoder_get_frame_with_timeout()
 * with a timeout value of zero.
 *
 * Return value: a #GstVaapiDecoderStatus
 */
GstVaapiDecoderStatus
gst_vaapi_decoder_get_frame (GstVaapiDecoder * decoder,
    GstVideoCodecFrame ** out_frame_ptr)
{
  return gst_vaapi_decoder_get_frame_with_timeout (decoder, out_frame_ptr, 0);
}

/**
 * gst_vaapi_decoder_get_frame_with_timeout:
 * @decoder: a #GstVaapiDecoder
 * @out_frame_ptr: the next decoded frame as a #GstVideoCodecFrame
 * @timeout: the number of microseconds to wait for the frame, at most
 *
 * On successful return, *@out_frame_ptr contains the next decoded
 * frame available as a #GstVideoCodecFrame. The caller owns this
 * object, so gst_video_codec_frame_unref() shall be called after
 * usage. Otherwise, @GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA is
 * returned if no decoded frame is available.
 *
 * The actual surface is available as a #GstVaapiSurfaceProxy attached
 * to the user-data anchor of the output frame. Ownership of the proxy
 * is transferred to the frame.
 *
 * Return value: a #GstVaapiDecoderStatus
 */
GstVaapiDecoderStatus
gst_vaapi_decoder_get_frame_with_timeout (GstVaapiDecoder * decoder,
    GstVideoCodecFrame ** out_frame_ptr, guint64 timeout)
{
  GstVideoCodecFrame *out_frame;

  g_return_val_if_fail (decoder != NULL,
      GST_VAAPI_DECODER_STATUS_ERROR_INVALID_PARAMETER);
  g_return_val_if_fail (out_frame_ptr != NULL,
      GST_VAAPI_DECODER_STATUS_ERROR_INVALID_PARAMETER);

  out_frame = pop_frame (decoder, timeout);
  if (!out_frame)
    return GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;

  *out_frame_ptr = out_frame;
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

void
gst_vaapi_decoder_set_picture_size (GstVaapiDecoder * decoder,
    guint width, guint height)
{
  GstVideoCodecState *const codec_state = decoder->codec_state;
  gboolean size_changed = FALSE;

  if (codec_state->info.width != width) {
    GST_DEBUG ("picture width changed to %d", width);
    codec_state->info.width = width;
    gst_caps_set_simple (codec_state->caps, "width", G_TYPE_INT, width, NULL);
    size_changed = TRUE;
  }

  if (codec_state->info.height != height) {
    GST_DEBUG ("picture height changed to %d", height);
    codec_state->info.height = height;
    gst_caps_set_simple (codec_state->caps, "height", G_TYPE_INT, height, NULL);
    size_changed = TRUE;
  }

  if (size_changed)
    notify_codec_state_changed (decoder);
}

void
gst_vaapi_decoder_set_framerate (GstVaapiDecoder * decoder,
    guint fps_n, guint fps_d)
{
  GstVideoCodecState *const codec_state = decoder->codec_state;

  if (!fps_n || !fps_d)
    return;

  if (codec_state->info.fps_n != fps_n || codec_state->info.fps_d != fps_d) {
    GST_DEBUG ("framerate changed to %u/%u", fps_n, fps_d);
    codec_state->info.fps_n = fps_n;
    codec_state->info.fps_d = fps_d;
    gst_caps_set_simple (codec_state->caps,
        "framerate", GST_TYPE_FRACTION, fps_n, fps_d, NULL);
    notify_codec_state_changed (decoder);
  }
}

void
gst_vaapi_decoder_set_pixel_aspect_ratio (GstVaapiDecoder * decoder,
    guint par_n, guint par_d)
{
  GstVideoCodecState *const codec_state = decoder->codec_state;

  if (!par_n || !par_d)
    return;

  if (codec_state->info.par_n != par_n || codec_state->info.par_d != par_d) {
    GST_DEBUG ("pixel-aspect-ratio changed to %u/%u", par_n, par_d);
    codec_state->info.par_n = par_n;
    codec_state->info.par_d = par_d;
    gst_caps_set_simple (codec_state->caps,
        "pixel-aspect-ratio", GST_TYPE_FRACTION, par_n, par_d, NULL);
    notify_codec_state_changed (decoder);
  }
}

static const gchar *
gst_interlace_mode_to_string (GstVideoInterlaceMode mode)
{
  switch (mode) {
    case GST_VIDEO_INTERLACE_MODE_PROGRESSIVE:
      return "progressive";
    case GST_VIDEO_INTERLACE_MODE_INTERLEAVED:
      return "interleaved";
    case GST_VIDEO_INTERLACE_MODE_MIXED:
      return "mixed";
    default:
      return "<unknown>";
  }
}

void
gst_vaapi_decoder_set_interlace_mode (GstVaapiDecoder * decoder,
    GstVideoInterlaceMode mode)
{
  GstVideoCodecState *const codec_state = decoder->codec_state;

  if (codec_state->info.interlace_mode != mode) {
    GST_DEBUG ("interlace mode changed to %s",
        gst_interlace_mode_to_string (mode));
    codec_state->info.interlace_mode = mode;
    gst_caps_set_simple (codec_state->caps, "interlaced",
        G_TYPE_BOOLEAN, mode != GST_VIDEO_INTERLACE_MODE_PROGRESSIVE, NULL);
    notify_codec_state_changed (decoder);
  }
}

void
gst_vaapi_decoder_set_interlaced (GstVaapiDecoder * decoder,
    gboolean interlaced)
{
  gst_vaapi_decoder_set_interlace_mode (decoder,
      (interlaced ?
          GST_VIDEO_INTERLACE_MODE_INTERLEAVED :
          GST_VIDEO_INTERLACE_MODE_PROGRESSIVE));
}

void
gst_vaapi_decoder_set_multiview_mode (GstVaapiDecoder * decoder,
    gint views, GstVideoMultiviewMode mv_mode, GstVideoMultiviewFlags mv_flags)
{
  GstVideoCodecState *const codec_state = decoder->codec_state;
  GstVideoInfo *info = &codec_state->info;

  if (GST_VIDEO_INFO_VIEWS (info) != views ||
      GST_VIDEO_INFO_MULTIVIEW_MODE (info) != mv_mode ||
      GST_VIDEO_INFO_MULTIVIEW_FLAGS (info) != mv_flags) {
    const gchar *mv_mode_str =
        gst_video_multiview_mode_to_caps_string (mv_mode);

    GST_DEBUG ("Multiview mode changed to %s flags 0x%x views %d",
        mv_mode_str, mv_flags, views);
    GST_VIDEO_INFO_MULTIVIEW_MODE (info) = mv_mode;
    GST_VIDEO_INFO_MULTIVIEW_FLAGS (info) = mv_flags;
    GST_VIDEO_INFO_VIEWS (info) = views;

    gst_caps_set_simple (codec_state->caps, "multiview-mode",
        G_TYPE_STRING, mv_mode_str,
        "multiview-flags", GST_TYPE_VIDEO_MULTIVIEW_FLAGSET, mv_flags,
        GST_FLAG_SET_MASK_EXACT, "views", G_TYPE_INT, views, NULL);

    notify_codec_state_changed (decoder);
  }
}

gboolean
gst_vaapi_decoder_ensure_context (GstVaapiDecoder * decoder,
    GstVaapiContextInfo * cip)
{
  gst_vaapi_decoder_set_picture_size (decoder, cip->width, cip->height);

  cip->usage = GST_VAAPI_CONTEXT_USAGE_DECODE;
  if (decoder->context) {
    if (!gst_vaapi_context_reset (decoder->context, cip))
      return FALSE;
  } else {
    decoder->context = gst_vaapi_context_new (decoder->display, cip);
    if (!decoder->context)
      return FALSE;
  }
  decoder->va_context = gst_vaapi_context_get_id (decoder->context);
  return TRUE;
}

void
gst_vaapi_decoder_push_frame (GstVaapiDecoder * decoder,
    GstVideoCodecFrame * frame)
{
  push_frame (decoder, frame);
}

GstVaapiDecoderStatus
gst_vaapi_decoder_parse (GstVaapiDecoder * decoder,
    GstVideoCodecFrame * base_frame, GstAdapter * adapter, gboolean at_eos,
    guint * got_unit_size_ptr, gboolean * got_frame_ptr)
{
  g_return_val_if_fail (decoder != NULL,
      GST_VAAPI_DECODER_STATUS_ERROR_INVALID_PARAMETER);
  g_return_val_if_fail (base_frame != NULL,
      GST_VAAPI_DECODER_STATUS_ERROR_INVALID_PARAMETER);
  g_return_val_if_fail (adapter != NULL,
      GST_VAAPI_DECODER_STATUS_ERROR_INVALID_PARAMETER);
  g_return_val_if_fail (got_unit_size_ptr != NULL,
      GST_VAAPI_DECODER_STATUS_ERROR_INVALID_PARAMETER);
  g_return_val_if_fail (got_frame_ptr != NULL,
      GST_VAAPI_DECODER_STATUS_ERROR_INVALID_PARAMETER);

  return do_parse (decoder, base_frame, adapter, at_eos,
      got_unit_size_ptr, got_frame_ptr);
}

GstVaapiDecoderStatus
gst_vaapi_decoder_decode (GstVaapiDecoder * decoder, GstVideoCodecFrame * frame)
{
  g_return_val_if_fail (decoder != NULL,
      GST_VAAPI_DECODER_STATUS_ERROR_INVALID_PARAMETER);
  g_return_val_if_fail (frame != NULL,
      GST_VAAPI_DECODER_STATUS_ERROR_INVALID_PARAMETER);
  g_return_val_if_fail (frame->user_data != NULL,
      GST_VAAPI_DECODER_STATUS_ERROR_INVALID_PARAMETER);

  return do_decode (decoder, frame);
}

/* This function really marks the end of input,
 * so that the decoder will drain out any pending
 * frames on calls to gst_vaapi_decoder_get_frame_with_timeout() */
GstVaapiDecoderStatus
gst_vaapi_decoder_flush (GstVaapiDecoder * decoder)
{
  GstVaapiDecoderClass *klass;

  g_return_val_if_fail (decoder != NULL,
      GST_VAAPI_DECODER_STATUS_ERROR_INVALID_PARAMETER);

  klass = GST_VAAPI_DECODER_GET_CLASS (decoder);

  if (klass->flush)
    return klass->flush (decoder);

  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

/* Reset the decoder instance to a clean state,
 * clearing any pending decode state, without
 * reallocating the entire decoder */
GstVaapiDecoderStatus
gst_vaapi_decoder_reset (GstVaapiDecoder * decoder)
{
  GstVaapiDecoderClass *klass;
  GstVaapiDecoderStatus ret = GST_VAAPI_DECODER_STATUS_SUCCESS;

  g_return_val_if_fail (decoder != NULL,
      GST_VAAPI_DECODER_STATUS_ERROR_INVALID_PARAMETER);

  klass = GST_VAAPI_DECODER_GET_CLASS (decoder);

  GST_DEBUG ("Resetting decoder");

  if (klass->reset) {
    ret = klass->reset (decoder);
  } else {
    GST_WARNING_OBJECT (decoder, "missing reset() implementation");
  }

  if (ret != GST_VAAPI_DECODER_STATUS_SUCCESS)
    return ret;

  /* Clear any buffers and frame in the queues */
  {
    GstVideoCodecFrame *frame;
    GstBuffer *buffer;

    while ((frame = g_async_queue_try_pop (decoder->frames)) != NULL)
      gst_video_codec_frame_unref (frame);

    while ((buffer = g_async_queue_try_pop (decoder->buffers)) != NULL)
      gst_buffer_unref (buffer);
  }

  parser_state_reset (&decoder->parser_state);

  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

GstVaapiDecoderStatus
gst_vaapi_decoder_decode_codec_data (GstVaapiDecoder * decoder)
{
  GstVaapiDecoderClass *const klass = GST_VAAPI_DECODER_GET_CLASS (decoder);
  GstBuffer *const codec_data = GST_VAAPI_DECODER_CODEC_DATA (decoder);
  GstVaapiDecoderStatus status;
  GstMapInfo map_info;
  const guchar *buf;
  guint buf_size;

  if (!codec_data)
    return GST_VAAPI_DECODER_STATUS_SUCCESS;

  /* FIXME: add a meaningful error code? */
  if (!klass->decode_codec_data)
    return GST_VAAPI_DECODER_STATUS_SUCCESS;

  if (!gst_buffer_map (codec_data, &map_info, GST_MAP_READ)) {
    GST_ERROR ("failed to map buffer");
    return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
  }

  buf = map_info.data;
  buf_size = map_info.size;
  if (G_LIKELY (buf && buf_size > 0))
    status = klass->decode_codec_data (decoder, buf, buf_size);
  else
    status = GST_VAAPI_DECODER_STATUS_SUCCESS;
  gst_buffer_unmap (codec_data, &map_info);
  return status;
}

/**
 * gst_vaapi_decoder_update_caps:
 * @decoder: a #GstVaapiDecoder
 * @caps: a #GstCaps
 *
 * If @caps is compatible with the current caps, or they have the same
 * codec, the caps are updated internally.
 *
 * This method will not call codec_state_changed() callback, since
 * this function is intended to run sync and during the set_format()
 * vmethod.
 *
 * Returns: %TRUE if the caps were updated internally.
 **/
gboolean
gst_vaapi_decoder_update_caps (GstVaapiDecoder * decoder, GstCaps * caps)
{
  GstCaps *decoder_caps;
  GstVaapiCodec codec;

  g_return_val_if_fail (decoder != NULL, FALSE);
  g_return_val_if_fail (caps != NULL, FALSE);

  decoder_caps = get_caps (decoder);
  if (!decoder_caps)
    return FALSE;

  if (gst_caps_is_always_compatible (caps, decoder_caps))
    return set_caps (decoder, caps);

  codec = gst_vaapi_get_codec_from_caps (caps);
  if (codec == 0)
    return FALSE;
  if (codec == decoder->codec) {
    if (set_caps (decoder, caps)) {
      return
          gst_vaapi_decoder_decode_codec_data (decoder) ==
          GST_VAAPI_DECODER_STATUS_SUCCESS;
    }
  }

  return FALSE;
}

/**
 * gst_vaapi_decoder_get_surface_attributres:
 * @decoder: a #GstVaapiDecoder instances
 * @min_width (out): the minimal surface width
 * @min_height (out): the minimal surface height
 * @max_width (out): the maximal surface width
 * @max_height (out): the maximal surface height
 *
 * Fetches the valid surface's attributes for the current context.
 *
 * Returns: a #GArray of valid formats we get or %NULL if failed.
 **/
GArray *
gst_vaapi_decoder_get_surface_attributes (GstVaapiDecoder * decoder,
    gint * min_width, gint * min_height, gint * max_width, gint * max_height,
    guint * mem_types)
{
  gboolean ret;
  GstVaapiConfigSurfaceAttributes attribs = { 0, };

  g_return_val_if_fail (decoder != NULL, FALSE);

  if (!decoder->context)
    return NULL;

  ret = gst_vaapi_context_get_surface_attributes (decoder->context, &attribs);
  if (ret)
    attribs.formats = gst_vaapi_context_get_surface_formats (decoder->context);

  if (!attribs.formats)
    return NULL;
  if (attribs.formats->len == 0) {
    g_array_unref (attribs.formats);
    return NULL;
  }

  if (min_width)
    *min_width = attribs.min_width;
  if (min_height)
    *min_height = attribs.min_height;
  if (max_width)
    *max_width = attribs.max_width;
  if (max_height)
    *max_height = attribs.max_height;
  if (mem_types)
    *mem_types = attribs.mem_types;
  return attribs.formats;
}
