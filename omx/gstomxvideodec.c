/*
 * Copyright (C) 2011, Hewlett-Packard Development Company, L.P.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>, Collabora Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <string.h>

#include "gstomxvideodec.h"

GST_DEBUG_CATEGORY_STATIC (gst_omx_video_dec_debug_category);
#define GST_CAT_DEFAULT gst_omx_video_dec_debug_category

typedef struct _BufferIdentification BufferIdentification;
struct _BufferIdentification
{
  guint64 timestamp;
};

static void
buffer_identification_free (BufferIdentification * id)
{
  g_slice_free (BufferIdentification, id);
}

/* prototypes */
static void gst_omx_video_dec_finalize (GObject * object);

static GstStateChangeReturn
gst_omx_video_dec_change_state (GstElement * element,
    GstStateChange transition);

static gboolean gst_omx_video_dec_start (GstBaseVideoDecoder * decoder);
static gboolean gst_omx_video_dec_stop (GstBaseVideoDecoder * decoder);
static gboolean gst_omx_video_dec_set_format (GstBaseVideoDecoder * decoder,
    GstVideoState * state);
static gboolean gst_omx_video_dec_reset (GstBaseVideoDecoder * decoder);
static GstFlowReturn gst_omx_video_dec_parse_data (GstBaseVideoDecoder *
    decoder, gboolean at_eos);
static GstFlowReturn gst_omx_video_dec_handle_frame (GstBaseVideoDecoder *
    decoder, GstVideoFrame * frame);
static GstFlowReturn gst_omx_video_dec_finish (GstBaseVideoDecoder * decoder);

enum
{
  PROP_0
};

/* class initialization */

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_omx_video_dec_debug_category, "omxvideodec", 0, \
      "debug category for gst-omx video decoder base class");

GST_BOILERPLATE_FULL (GstOMXVideoDec, gst_omx_video_dec, GstBaseVideoDecoder,
    GST_TYPE_BASE_VIDEO_DECODER, DEBUG_INIT);

static void
gst_omx_video_dec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstOMXVideoDecClass *videodec_class = GST_OMX_VIDEO_DEC_CLASS (g_class);
  GKeyFile *config;
  const gchar *element_name;
  GError *err;
  gchar *core_name, *component_name, *component_role;
  gint in_port_index, out_port_index;
  gchar *template_caps;
  GstPadTemplate *templ;
  GstCaps *caps;
  gchar **hacks;

  element_name =
      g_type_get_qdata (G_TYPE_FROM_CLASS (g_class),
      gst_omx_element_name_quark);
  /* This happens for the base class and abstract subclasses */
  if (!element_name)
    return;

  config = gst_omx_get_configuration ();

  /* This will always succeed, see check in plugin_init */
  core_name = g_key_file_get_string (config, element_name, "core-name", NULL);
  g_assert (core_name != NULL);
  videodec_class->core_name = core_name;
  component_name =
      g_key_file_get_string (config, element_name, "component-name", NULL);
  g_assert (component_name != NULL);
  videodec_class->component_name = component_name;

  /* If this fails we simply don't set a role */
  if ((component_role =
          g_key_file_get_string (config, element_name, "component-role",
              NULL))) {
    GST_DEBUG ("Using component-role '%s' for element '%s'", component_role,
        element_name);
    videodec_class->component_role = component_role;
  }


  /* Now set the inport/outport indizes and assume sane defaults */
  err = NULL;
  in_port_index =
      g_key_file_get_integer (config, element_name, "in-port-index", &err);
  if (err != NULL) {
    GST_DEBUG ("No 'in-port-index' set for element '%s', assuming 0: %s",
        element_name, err->message);
    in_port_index = 0;
    g_error_free (err);
  }
  videodec_class->in_port_index = in_port_index;

  err = NULL;
  out_port_index =
      g_key_file_get_integer (config, element_name, "out-port-index", &err);
  if (err != NULL) {
    GST_DEBUG ("No 'out-port-index' set for element '%s', assuming 1: %s",
        element_name, err->message);
    out_port_index = 1;
    g_error_free (err);
  }
  videodec_class->out_port_index = out_port_index;

  /* Add pad templates */
  err = NULL;
  if (!(template_caps =
          g_key_file_get_string (config, element_name, "sink-template-caps",
              &err))) {
    GST_DEBUG
        ("No sink template caps specified for element '%s', using default '%s'",
        element_name, videodec_class->default_sink_template_caps);
    caps = gst_caps_from_string (videodec_class->default_sink_template_caps);
    g_assert (caps != NULL);
    g_error_free (err);
  } else {
    caps = gst_caps_from_string (template_caps);
    if (!caps) {
      GST_DEBUG
          ("Could not parse sink template caps '%s' for element '%s', using default '%s'",
          template_caps, element_name,
          videodec_class->default_sink_template_caps);
      caps = gst_caps_from_string (videodec_class->default_sink_template_caps);
      g_assert (caps != NULL);
    }
  }
  templ = gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS, caps);
  g_free (template_caps);
  gst_element_class_add_pad_template (element_class, templ);
  gst_object_unref (templ);

  err = NULL;
  if (!(template_caps =
          g_key_file_get_string (config, element_name, "src-template-caps",
              &err))) {
    GST_DEBUG
        ("No src template caps specified for element '%s', using default '%s'",
        element_name, videodec_class->default_src_template_caps);
    caps = gst_caps_from_string (videodec_class->default_src_template_caps);
    g_assert (caps != NULL);
    g_error_free (err);
  } else {
    caps = gst_caps_from_string (template_caps);
    if (!caps) {
      GST_DEBUG
          ("Could not parse src template caps '%s' for element '%s', using default '%s'",
          template_caps, element_name,
          videodec_class->default_src_template_caps);
      caps = gst_caps_from_string (videodec_class->default_src_template_caps);
      g_assert (caps != NULL);
    }
  }
  templ = gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS, caps);
  g_free (template_caps);
  gst_element_class_add_pad_template (element_class, templ);
  gst_object_unref (templ);

  if ((hacks =
          g_key_file_get_string_list (config, element_name, "hacks", NULL,
              NULL))) {
#ifndef GST_DISABLE_GST_DEBUG
    gchar **walk = hacks;

    while (*walk) {
      GST_DEBUG ("Using hack: %s", *walk);
      walk++;
    }
#endif

    videodec_class->hacks = gst_omx_parse_hacks (hacks);
  }
}

static void
gst_omx_video_dec_class_init (GstOMXVideoDecClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseVideoDecoderClass *base_video_decoder_class =
      GST_BASE_VIDEO_DECODER_CLASS (klass);

  gobject_class->finalize = gst_omx_video_dec_finalize;

  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_omx_video_dec_change_state);

  base_video_decoder_class->start = GST_DEBUG_FUNCPTR (gst_omx_video_dec_start);
  base_video_decoder_class->stop = GST_DEBUG_FUNCPTR (gst_omx_video_dec_stop);
  base_video_decoder_class->reset = GST_DEBUG_FUNCPTR (gst_omx_video_dec_reset);
  base_video_decoder_class->set_format =
      GST_DEBUG_FUNCPTR (gst_omx_video_dec_set_format);
  base_video_decoder_class->parse_data =
      GST_DEBUG_FUNCPTR (gst_omx_video_dec_parse_data);
  base_video_decoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_omx_video_dec_handle_frame);
  base_video_decoder_class->finish =
      GST_DEBUG_FUNCPTR (gst_omx_video_dec_finish);

  klass->default_src_template_caps = "video/x-raw-yuv, "
      "width = " GST_VIDEO_SIZE_RANGE ", "
      "height = " GST_VIDEO_SIZE_RANGE ", " "framerate = " GST_VIDEO_FPS_RANGE;
}

static void
gst_omx_video_dec_init (GstOMXVideoDec * self, GstOMXVideoDecClass * klass)
{
  GST_BASE_VIDEO_DECODER (self)->packetized = TRUE;
}

static gboolean
gst_omx_video_dec_open (GstOMXVideoDec * self)
{
  GstOMXVideoDecClass *klass = GST_OMX_VIDEO_DEC_GET_CLASS (self);

  GST_DEBUG_OBJECT (self, "Opening decoder");

  self->component =
      gst_omx_component_new (GST_OBJECT_CAST (self), klass->core_name,
      klass->component_name, klass->component_role, klass->hacks);
  self->started = FALSE;

  if (!self->component)
    return FALSE;

  if (gst_omx_component_get_state (self->component,
          GST_CLOCK_TIME_NONE) != OMX_StateLoaded)
    return FALSE;

  self->in_port =
      gst_omx_component_add_port (self->component, klass->in_port_index);
  self->out_port =
      gst_omx_component_add_port (self->component, klass->out_port_index);

  if (!self->in_port || !self->out_port)
    return FALSE;

  GST_DEBUG_OBJECT (self, "Opened decoder");

  return TRUE;
}

static gboolean
gst_omx_video_dec_close (GstOMXVideoDec * self)
{
  OMX_STATETYPE state;

  GST_DEBUG_OBJECT (self, "Closing decoder");

  state = gst_omx_component_get_state (self->component, 0);
  if (state > OMX_StateLoaded || state == OMX_StateInvalid) {
    if (state > OMX_StateIdle) {
      gst_omx_component_set_state (self->component, OMX_StateIdle);
      gst_omx_component_get_state (self->component, 5 * GST_SECOND);
    }
    gst_omx_component_set_state (self->component, OMX_StateLoaded);
    gst_omx_port_deallocate_buffers (self->in_port);
    gst_omx_port_deallocate_buffers (self->out_port);
    if (state > OMX_StateLoaded)
      gst_omx_component_get_state (self->component, 5 * GST_SECOND);
  }

  self->in_port = NULL;
  self->out_port = NULL;
  if (self->component)
    gst_omx_component_free (self->component);
  self->component = NULL;

  self->started = FALSE;

  GST_DEBUG_OBJECT (self, "Closed decoder");

  return TRUE;
}

static void
gst_omx_video_dec_finalize (GObject * object)
{
  /* GstOMXVideoDec *self = GST_OMX_VIDEO_DEC (object); */

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstStateChangeReturn
gst_omx_video_dec_change_state (GstElement * element, GstStateChange transition)
{
  GstOMXVideoDec *self;
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  g_return_val_if_fail (GST_IS_OMX_VIDEO_DEC (element),
      GST_STATE_CHANGE_FAILURE);
  self = GST_OMX_VIDEO_DEC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_omx_video_dec_open (self))
        ret = GST_STATE_CHANGE_FAILURE;
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (self->in_port)
        gst_omx_port_set_flushing (self->in_port, FALSE);
      if (self->out_port)
        gst_omx_port_set_flushing (self->out_port, FALSE);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (self->in_port)
        gst_omx_port_set_flushing (self->in_port, TRUE);
      if (self->out_port)
        gst_omx_port_set_flushing (self->out_port, TRUE);
      break;
    default:
      break;
  }

  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      if (!gst_omx_video_dec_close (self))
        ret = GST_STATE_CHANGE_FAILURE;
      break;
    default:
      break;
  }

  return ret;
}

#define MAX_FRAME_DIST_TICKS  (5 * OMX_TICKS_PER_SECOND)
#define MAX_FRAME_DIST_FRAMES (100)

static GstVideoFrame *
_find_nearest_frame (GstOMXVideoDec * self, GstOMXBuffer * buf)
{
  GList *l, *best_l = NULL;
  GList *finish_frames = NULL;
  GstVideoFrame *best = NULL;
  guint64 best_timestamp = 0;
  guint64 best_diff = G_MAXUINT64;
  BufferIdentification *best_id = NULL;

  GST_BASE_VIDEO_CODEC_STREAM_LOCK (self);
  for (l = GST_BASE_VIDEO_CODEC (self)->frames; l; l = l->next) {
    GstVideoFrame *tmp = l->data;
    BufferIdentification *id = tmp->coder_hook;
    guint64 timestamp, diff;

    /* This happens for frames that were just added but
     * which were not passed to the component yet. Ignore
     * them here!
     */
    if (!id)
      continue;

    timestamp = id->timestamp;

    if (timestamp > buf->omx_buf->nTimeStamp)
      diff = timestamp - buf->omx_buf->nTimeStamp;
    else
      diff = buf->omx_buf->nTimeStamp - timestamp;

    if (best == NULL || diff < best_diff) {
      best = tmp;
      best_timestamp = timestamp;
      best_diff = diff;
      best_l = l;
      best_id = id;

      /* For frames without timestamp we simply take the first frame */
      if ((buf->omx_buf->nTimeStamp == 0 && timestamp == 0) || diff == 0)
        break;
    }
  }

  if (best_id) {
    for (l = GST_BASE_VIDEO_CODEC (self)->frames; l && l != best_l; l = l->next) {
      GstVideoFrame *tmp = l->data;
      BufferIdentification *id = tmp->coder_hook;
      guint64 diff_ticks, diff_frames;

      if (id->timestamp > best_timestamp)
        break;

      if (id->timestamp == 0 || best_timestamp == 0)
        diff_ticks = 0;
      else
        diff_ticks = best_timestamp - id->timestamp;
      diff_frames = best->system_frame_number - tmp->system_frame_number;

      if (diff_ticks > MAX_FRAME_DIST_TICKS
          || diff_frames > MAX_FRAME_DIST_FRAMES) {
        finish_frames = g_list_prepend (finish_frames, tmp);
      }
    }
  }

  GST_BASE_VIDEO_CODEC_STREAM_UNLOCK (self);

  if (finish_frames) {
    g_warning ("Too old frames, bug in decoder -- please file a bug");
    for (l = finish_frames; l; l = l->next) {
      gst_base_video_decoder_finish_frame (GST_BASE_VIDEO_DECODER (self),
          l->data);
    }
  }

  return best;
}

static gboolean
gst_omx_video_dec_fill_buffer (GstOMXVideoDec * self, GstOMXBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstVideoState *state = &GST_BASE_VIDEO_CODEC (self)->state;
  OMX_PARAM_PORTDEFINITIONTYPE *port_def = &self->out_port->port_def;
  gboolean ret = FALSE;

  if (state->width != port_def->format.video.nFrameWidth ||
      state->height != port_def->format.video.nFrameHeight) {
    GST_ERROR_OBJECT (self, "Width or height do not match");
    goto done;
  }

  /* Same strides and everything */
  if (GST_BUFFER_SIZE (outbuf) == inbuf->omx_buf->nFilledLen) {
    memcpy (GST_BUFFER_DATA (outbuf),
        inbuf->omx_buf->pBuffer + inbuf->omx_buf->nOffset,
        inbuf->omx_buf->nFilledLen);
    ret = TRUE;
    goto done;
  }
  /* Different strides */

  switch (state->format) {
    case GST_VIDEO_FORMAT_I420:{
      gint i, j, height;
      guint8 *src, *dest;
      gint src_stride, dest_stride;

      for (i = 0; i < 3; i++) {
        if (i == 0) {
          src_stride = port_def->format.video.nStride;
          dest_stride =
              gst_video_format_get_row_stride (state->format, 0, state->width);
        } else {
          src_stride = port_def->format.video.nStride / 2;
          dest_stride =
              gst_video_format_get_row_stride (state->format, 1, state->width);
        }

        src = inbuf->omx_buf->pBuffer + inbuf->omx_buf->nOffset;
        if (i > 0)
          src +=
              port_def->format.video.nSliceHeight *
              port_def->format.video.nStride;
        if (i == 2)
          src +=
              (port_def->format.video.nSliceHeight / 2) *
              (port_def->format.video.nStride / 2);

        dest =
            GST_BUFFER_DATA (outbuf) +
            gst_video_format_get_component_offset (state->format, i,
            state->width, state->height);

        height =
            gst_video_format_get_component_height (state->format, i,
            state->height);

        for (j = 0; j < height; j++) {
          memcpy (dest, src, MIN (src_stride, dest_stride));
          src += src_stride;
          dest += dest_stride;
        }
      }
      ret = TRUE;
      break;
    }
    case GST_VIDEO_FORMAT_NV12:{
      gint i, j, height;
      guint8 *src, *dest;
      gint src_stride, dest_stride;

      for (i = 0; i < 2; i++) {
        if (i == 0) {
          src_stride = port_def->format.video.nStride;
          dest_stride =
              gst_video_format_get_row_stride (state->format, 0, state->width);
        } else {
          src_stride = port_def->format.video.nStride;
          dest_stride =
              gst_video_format_get_row_stride (state->format, 1, state->width);
        }

        src = inbuf->omx_buf->pBuffer + inbuf->omx_buf->nOffset;
        if (i == 1)
          src +=
              port_def->format.video.nSliceHeight *
              port_def->format.video.nStride;

        dest =
            GST_BUFFER_DATA (outbuf) +
            gst_video_format_get_component_offset (state->format, i,
            state->width, state->height);

        height =
            gst_video_format_get_component_height (state->format, i,
            state->height);
        for (j = 0; j < height; j++) {
          memcpy (dest, src, MIN (src_stride, dest_stride));
          src += src_stride;
          dest += dest_stride;
        }
      }
      ret = TRUE;
      break;
    }
    default:
      GST_ERROR_OBJECT (self, "Unsupported format");
      goto done;
      break;
  }


done:
  if (ret) {
    GST_BUFFER_TIMESTAMP (outbuf) =
        gst_util_uint64_scale (inbuf->omx_buf->nTimeStamp, GST_SECOND,
        OMX_TICKS_PER_SECOND);
    if (inbuf->omx_buf->nTickCount != 0)
      GST_BUFFER_DURATION (outbuf) =
          gst_util_uint64_scale (inbuf->omx_buf->nTickCount, GST_SECOND,
          OMX_TICKS_PER_SECOND);
  }

  return ret;
}

static void
gst_omx_video_dec_loop (GstOMXVideoDec * self)
{
  GstOMXPort *port = self->out_port;
  GstOMXBuffer *buf = NULL;
  GstVideoFrame *frame;
  GstFlowReturn flow_ret = GST_FLOW_OK;
  GstOMXAcquireBufferReturn acq_return;

  acq_return = gst_omx_port_acquire_buffer (port, &buf);
  if (acq_return == GST_OMX_ACQUIRE_BUFFER_ERROR) {
    goto component_error;
  } else if (acq_return == GST_OMX_ACQUIRE_BUFFER_FLUSHING) {
    goto flushing;
  } else if (acq_return == GST_OMX_ACQUIRE_BUFFER_RECONFIGURE) {
    if (gst_omx_port_reconfigure (self->out_port) != OMX_ErrorNone)
      goto reconfigure_error;
    /* And restart the loop */
    return;
  }

  if (!GST_PAD_CAPS (GST_BASE_VIDEO_CODEC_SRC_PAD (self))
      || acq_return == GST_OMX_ACQUIRE_BUFFER_RECONFIGURED) {
    GstVideoState *state = &GST_BASE_VIDEO_CODEC (self)->state;
    OMX_PARAM_PORTDEFINITIONTYPE port_def;

    GST_DEBUG_OBJECT (self, "Port settings have changed, updating caps");

    gst_omx_port_get_port_definition (port, &port_def);
    g_assert (port_def.format.video.eCompressionFormat ==
        OMX_VIDEO_CodingUnused);

    switch (port_def.format.video.eColorFormat) {
      case OMX_COLOR_FormatYUV420Planar:
        state->format = GST_VIDEO_FORMAT_I420;
        break;
      case OMX_COLOR_FormatYUV420SemiPlanar:
        state->format = GST_VIDEO_FORMAT_NV12;
        break;
      default:
        GST_ERROR_OBJECT (self, "Unsupported color format: %d",
            port_def.format.video.eColorFormat);
        if (buf)
          gst_omx_port_release_buffer (self->out_port, buf);
        goto caps_failed;
        break;
    }

    state->width = port_def.format.video.nFrameWidth;
    state->height = port_def.format.video.nFrameHeight;

    /* Take framerate and pixel-aspect-ratio from sinkpad caps */

    if (!gst_base_video_decoder_set_src_caps (GST_BASE_VIDEO_DECODER (self))) {
      if (buf)
        gst_omx_port_release_buffer (self->out_port, buf);
      goto caps_failed;
    }

    /* Now get a buffer */
    if (acq_return != GST_OMX_ACQUIRE_BUFFER_OK)
      return;
  }

  g_assert (acq_return == GST_OMX_ACQUIRE_BUFFER_OK && buf != NULL);

  GST_DEBUG_OBJECT (self, "Handling buffer: 0x%08x %lu", buf->omx_buf->nFlags,
      buf->omx_buf->nTimeStamp);

  frame = _find_nearest_frame (self, buf);
  if (!frame && buf->omx_buf->nFilledLen > 0) {
    GstBuffer *outbuf;

    /* This sometimes happens at EOS or if the input is not properly framed,
     * let's handle it gracefully by allocating a new buffer for the current
     * caps and filling it
     */

    GST_ERROR_OBJECT (self, "No corresponding frame found");

    outbuf =
        gst_base_video_decoder_alloc_src_buffer (GST_BASE_VIDEO_DECODER (self));

    if (!gst_omx_video_dec_fill_buffer (self, buf, outbuf)) {
      gst_buffer_unref (outbuf);
      goto invalid_buffer;
    }
  } else if (buf->omx_buf->nFilledLen > 0) {
    if (GST_BASE_VIDEO_CODEC (self)->state.bytes_per_picture == 0) {
      /* FIXME: If the sinkpad caps change we have currently no way
       * to allocate new src buffers because basevideodecoder assumes
       * that the caps on both pads are equivalent all the time
       */
      GST_WARNING_OBJECT (self,
          "Caps change pending and still have buffers for old caps -- dropping");
    } else
        if (gst_base_video_decoder_alloc_src_frame (GST_BASE_VIDEO_DECODER
            (self), frame) == GST_FLOW_OK) {
      /* FIXME: This currently happens because of a race condition too.
       * We first need to reconfigure the output port and then the input
       * port if both need reconfiguration.
       */
      if (!gst_omx_video_dec_fill_buffer (self, buf, frame->src_buffer)) {
        gst_buffer_replace (&frame->src_buffer, NULL);
        gst_base_video_decoder_finish_frame (GST_BASE_VIDEO_DECODER (self),
            frame);
        goto invalid_buffer;
      }
    }
    flow_ret =
        gst_base_video_decoder_finish_frame (GST_BASE_VIDEO_DECODER (self),
        frame);
  } else if (frame != NULL) {
    flow_ret =
        gst_base_video_decoder_finish_frame (GST_BASE_VIDEO_DECODER (self),
        frame);
  }

  if (flow_ret == GST_FLOW_OK && (buf->omx_buf->nFlags & OMX_BUFFERFLAG_EOS))
    flow_ret = GST_FLOW_UNEXPECTED;

  gst_omx_port_release_buffer (port, buf);

  if (flow_ret != GST_FLOW_OK)
    goto flow_error;

  return;

component_error:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL),
        ("OpenMAX component in error state %s (0x%08x)",
            gst_omx_component_get_last_error_string (self->component),
            gst_omx_component_get_last_error (self->component)));
    gst_pad_push_event (GST_BASE_VIDEO_CODEC_SRC_PAD (self),
        gst_event_new_eos ());
    gst_pad_pause_task (GST_BASE_VIDEO_CODEC_SRC_PAD (self));
    return;
  }

flushing:
  {
    GST_DEBUG_OBJECT (self, "Flushing -- stopping task");
    gst_pad_pause_task (GST_BASE_VIDEO_CODEC_SRC_PAD (self));
    return;
  }

flow_error:
  {
    if (flow_ret == GST_FLOW_UNEXPECTED) {
      GST_DEBUG_OBJECT (self, "EOS");

      gst_pad_push_event (GST_BASE_VIDEO_CODEC_SRC_PAD (self),
          gst_event_new_eos ());
      gst_pad_pause_task (GST_BASE_VIDEO_CODEC_SRC_PAD (self));
    } else if (flow_ret == GST_FLOW_NOT_LINKED
        || flow_ret < GST_FLOW_UNEXPECTED) {
      GST_ELEMENT_ERROR (self, STREAM, FAILED, ("Internal data stream error."),
          ("stream stopped, reason %s", gst_flow_get_name (flow_ret)));

      gst_pad_push_event (GST_BASE_VIDEO_CODEC_SRC_PAD (self),
          gst_event_new_eos ());
      gst_pad_pause_task (GST_BASE_VIDEO_CODEC_SRC_PAD (self));
    }
    return;
  }

reconfigure_error:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS, (NULL),
        ("Unable to reconfigure output port"));
    gst_pad_push_event (GST_BASE_VIDEO_CODEC_SRC_PAD (self),
        gst_event_new_eos ());
    gst_pad_pause_task (GST_BASE_VIDEO_CODEC_SRC_PAD (self));
    return;
  }

invalid_buffer:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS, (NULL),
        ("Invalid sized input buffer"));
    gst_pad_push_event (GST_BASE_VIDEO_CODEC_SRC_PAD (self),
        gst_event_new_eos ());
    gst_pad_pause_task (GST_BASE_VIDEO_CODEC_SRC_PAD (self));
    return;
  }

caps_failed:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS, (NULL), ("Failed to set caps"));
    gst_pad_push_event (GST_BASE_VIDEO_CODEC_SRC_PAD (self),
        gst_event_new_eos ());
    gst_pad_pause_task (GST_BASE_VIDEO_CODEC_SRC_PAD (self));
    return;
  }
}

static gboolean
gst_omx_video_dec_start (GstBaseVideoDecoder * decoder)
{
  GstOMXVideoDec *self;
  gboolean ret;

  self = GST_OMX_VIDEO_DEC (decoder);

  ret =
      gst_pad_start_task (GST_BASE_VIDEO_CODEC_SRC_PAD (self),
      (GstTaskFunction) gst_omx_video_dec_loop, self);

  return ret;
}

static gboolean
gst_omx_video_dec_stop (GstBaseVideoDecoder * decoder)
{
  GstOMXVideoDec *self;

  self = GST_OMX_VIDEO_DEC (decoder);

  GST_DEBUG_OBJECT (self, "Stopping decoder");

  gst_pad_stop_task (GST_BASE_VIDEO_CODEC_SRC_PAD (decoder));

  if (gst_omx_component_get_state (self->component, 0) > OMX_StateIdle)
    gst_omx_component_set_state (self->component, OMX_StateIdle);

  gst_omx_port_set_flushing (self->in_port, TRUE);
  gst_omx_port_set_flushing (self->out_port, TRUE);

  gst_omx_component_get_state (self->component, 5 * GST_SECOND);

  gst_buffer_replace (&self->codec_data, NULL);

  GST_DEBUG_OBJECT (self, "Stopped decoder");

  return TRUE;
}

static gboolean
gst_omx_video_dec_negotiate (GstOMXVideoDec * self)
{
  GstOMXPort *port = self->out_port;
  GstVideoState *state = &GST_BASE_VIDEO_CODEC (self)->state;
  OMX_VIDEO_PARAM_PORTFORMATTYPE param;
  OMX_ERRORTYPE err;
  GstCaps *comp_supported_caps;
  const GstCaps *templ_caps;
  GstCaps *peer_caps, *intersection;
  GstVideoFormat format;
  gint old_index;
  GstStructure *s;
  guint32 fourcc;

  templ_caps =
      gst_pad_get_pad_template_caps (GST_BASE_VIDEO_CODEC_SRC_PAD (self));
  peer_caps = gst_pad_peer_get_caps (GST_BASE_VIDEO_CODEC_SRC_PAD (self));
  if (peer_caps) {
    intersection = gst_caps_intersect (templ_caps, peer_caps);
    gst_caps_unref (peer_caps);
  } else {
    intersection = gst_caps_copy (templ_caps);
  }

  GST_OMX_INIT_STRUCT (&param);
  param.nPortIndex = port->index;
  param.nIndex = 0;
  if (state->fps_n == 0)
    param.xFramerate = 0;
  else
    param.xFramerate = (state->fps_n << 16) / (state->fps_d);

  old_index = -1;
  comp_supported_caps = gst_caps_new_empty ();
  do {
    err =
        gst_omx_component_get_parameter (self->component,
        OMX_IndexParamVideoPortFormat, &param);

    /* FIXME: Workaround for Bellagio that simply always
     * returns the same value regardless of nIndex and
     * never returns OMX_ErrorNoMore
     */
    if (old_index == param.nIndex)
      break;

    if (err == OMX_ErrorNone) {
      switch (param.eColorFormat) {
        case OMX_COLOR_FormatYUV420Planar:
          gst_caps_append_structure (comp_supported_caps,
              gst_structure_new ("video/x-raw-yuv",
                  "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC ('I', '4', '2',
                      '0'), NULL));
          break;
        case OMX_COLOR_FormatYUV420SemiPlanar:
          gst_caps_append_structure (comp_supported_caps,
              gst_structure_new ("video/x-raw-yuv",
                  "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC ('N', 'V', '1',
                      '2'), NULL));
          break;
        default:
          break;
      }
    }
    old_index = param.nIndex++;
  } while (err == OMX_ErrorNone);

  if (!gst_caps_is_empty (comp_supported_caps)) {
    GstCaps *tmp;

    tmp = gst_caps_intersect (comp_supported_caps, intersection);
    gst_caps_unref (intersection);
    intersection = tmp;
  }

  if (gst_caps_is_empty (intersection)) {
    GST_ERROR_OBJECT (self, "Empty caps");
    return FALSE;
  }

  gst_caps_truncate (intersection);

  s = gst_caps_get_structure (intersection, 0);
  if (!gst_structure_get_fourcc (s, "format", &fourcc) ||
      (format =
          gst_video_format_from_fourcc (fourcc)) == GST_VIDEO_FORMAT_UNKNOWN) {
    GST_ERROR_OBJECT (self, "Invalid caps: %" GST_PTR_FORMAT, intersection);
    return FALSE;
  }

  switch (format) {
    case GST_VIDEO_FORMAT_I420:
      param.eColorFormat = OMX_COLOR_FormatYUV420Planar;
      break;
    case GST_VIDEO_FORMAT_NV12:
      param.eColorFormat = OMX_COLOR_FormatYUV420SemiPlanar;
      break;
    default:
      GST_ERROR_OBJECT (self, "Unknown color format: %u", format);
      return FALSE;
      break;
  }

  /* Reset framerate, we only care about the color format here */
  param.xFramerate = 0;

  err =
      gst_omx_component_set_parameter (self->component,
      OMX_IndexParamVideoPortFormat, &param);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Failed to set video port format: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
  }

  return (err == OMX_ErrorNone);
}

static gboolean
gst_omx_video_dec_set_format (GstBaseVideoDecoder * decoder,
    GstVideoState * state)
{
  GstOMXVideoDec *self;
  GstOMXVideoDecClass *klass;
  gboolean is_format_change = FALSE;
  gboolean needs_disable = FALSE;
  OMX_PARAM_PORTDEFINITIONTYPE port_def;

  self = GST_OMX_VIDEO_DEC (decoder);
  klass = GST_OMX_VIDEO_DEC_GET_CLASS (decoder);

  GST_DEBUG_OBJECT (self, "Setting new caps %" GST_PTR_FORMAT, state->caps);

  gst_omx_port_get_port_definition (self->in_port, &port_def);

  /* Check if the caps change is a real format change or if only irrelevant
   * parts of the caps have changed or nothing at all.
   */
  is_format_change |= port_def.format.video.nFrameWidth != state->width;
  is_format_change |= port_def.format.video.nFrameHeight != state->height;
  is_format_change |= (port_def.format.video.xFramerate == 0
      && state->fps_n != 0)
      || (port_def.format.video.xFramerate !=
      (state->fps_n << 16) / (state->fps_d));
  is_format_change |= (self->codec_data != state->codec_data);
  if (klass->is_format_change)
    is_format_change |= klass->is_format_change (self, self->in_port, state);

  needs_disable =
      gst_omx_component_get_state (self->component,
      GST_CLOCK_TIME_NONE) != OMX_StateLoaded;
  /* If the component is not in Loaded state and a real format change happens
   * we have to disable the port and re-allocate all buffers. If no real
   * format change happened we can just exit here.
   */
  if (needs_disable && !is_format_change) {
    GST_DEBUG_OBJECT (self,
        "Already running and caps did not change the format");
    return TRUE;
  }

  if (needs_disable && is_format_change) {
    if (klass->hacks & GST_OMX_HACK_NO_COMPONENT_RECONFIGURE) {
      GST_BASE_VIDEO_CODEC_STREAM_UNLOCK (self);
      gst_omx_video_dec_stop (GST_BASE_VIDEO_DECODER (self));
      gst_omx_video_dec_close (self);

      /* FIXME: Workaround for 
       * https://bugzilla.gnome.org/show_bug.cgi?id=654529
       */
      g_list_foreach (GST_BASE_VIDEO_CODEC (self)->frames,
          (GFunc) gst_base_video_codec_free_frame, NULL);
      g_list_free (GST_BASE_VIDEO_CODEC (self)->frames);
      GST_BASE_VIDEO_CODEC (self)->frames = NULL;

      GST_BASE_VIDEO_CODEC_STREAM_LOCK (self);
      if (!gst_omx_video_dec_open (self))
        return FALSE;
      needs_disable = FALSE;
    } else {
      if (gst_omx_port_manual_reconfigure (self->in_port,
              TRUE) != OMX_ErrorNone)
        return FALSE;
      if (gst_omx_port_set_enabled (self->in_port, FALSE) != OMX_ErrorNone)
        return FALSE;
    }
  }

  port_def.format.video.nFrameWidth = state->width;
  port_def.format.video.nFrameHeight = state->height;
  if (state->fps_n == 0)
    port_def.format.video.xFramerate = 0;
  else
    port_def.format.video.xFramerate = (state->fps_n << 16) / (state->fps_d);

  if (!gst_omx_port_update_port_definition (self->in_port, &port_def))
    return FALSE;
  if (!gst_omx_port_update_port_definition (self->out_port, NULL))
    return FALSE;

  if (klass->set_format) {
    if (!klass->set_format (self, self->in_port, state)) {
      GST_ERROR_OBJECT (self, "Subclass failed to set the new format");
      return FALSE;
    }
  }

  gst_buffer_replace (&self->codec_data, state->codec_data);

  if (!gst_omx_video_dec_negotiate (self))
    return FALSE;

  if (needs_disable) {
    if (gst_omx_port_set_enabled (self->in_port, TRUE) != OMX_ErrorNone)
      return FALSE;
    if (gst_omx_port_manual_reconfigure (self->in_port, FALSE) != OMX_ErrorNone)
      return FALSE;
  } else {
    if (gst_omx_component_set_state (self->component,
            OMX_StateIdle) != OMX_ErrorNone)
      return FALSE;

    /* Need to allocate buffers to reach Idle state */
    if (gst_omx_port_allocate_buffers (self->in_port) != OMX_ErrorNone)
      return FALSE;
    if (gst_omx_port_allocate_buffers (self->out_port) != OMX_ErrorNone)
      return FALSE;

    if (gst_omx_component_get_state (self->component,
            GST_CLOCK_TIME_NONE) != OMX_StateIdle)
      return FALSE;

    if (gst_omx_component_set_state (self->component,
            OMX_StateExecuting) != OMX_ErrorNone)
      return FALSE;

    if (gst_omx_component_get_state (self->component,
            GST_CLOCK_TIME_NONE) != OMX_StateExecuting)
      return FALSE;
  }

  /* Unset flushing to allow ports to accept data again */
  gst_omx_port_set_flushing (self->in_port, FALSE);
  gst_omx_port_set_flushing (self->out_port, FALSE);

  if (gst_omx_component_get_last_error (self->component) != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Component in error state: %s (0x%08x)",
        gst_omx_component_get_last_error_string (self->component),
        gst_omx_component_get_last_error (self->component));
    return FALSE;
  }

  /* Start the srcpad loop again */
  gst_pad_start_task (GST_BASE_VIDEO_CODEC_SRC_PAD (self),
      (GstTaskFunction) gst_omx_video_dec_loop, decoder);

  return TRUE;
}

static gboolean
gst_omx_video_dec_reset (GstBaseVideoDecoder * decoder)
{
  GstOMXVideoDec *self;

  self = GST_OMX_VIDEO_DEC (decoder);

  GST_DEBUG_OBJECT (self, "Resetting decoder");

  /* FIXME: Workaround for 
   * https://bugzilla.gnome.org/show_bug.cgi?id=654529
   */
  GST_BASE_VIDEO_CODEC_STREAM_LOCK (self);
  g_list_foreach (GST_BASE_VIDEO_CODEC (self)->frames,
      (GFunc) gst_base_video_codec_free_frame, NULL);
  g_list_free (GST_BASE_VIDEO_CODEC (self)->frames);
  GST_BASE_VIDEO_CODEC (self)->frames = NULL;
  GST_BASE_VIDEO_CODEC_STREAM_UNLOCK (self);

  if (self->started) {
    gst_omx_port_set_flushing (self->in_port, TRUE);
    gst_omx_port_set_flushing (self->out_port, TRUE);

    /* Wait until the srcpad loop is finished */
    GST_PAD_STREAM_LOCK (GST_BASE_VIDEO_CODEC_SRC_PAD (self));
    GST_PAD_STREAM_UNLOCK (GST_BASE_VIDEO_CODEC_SRC_PAD (self));

    gst_omx_port_set_flushing (self->in_port, FALSE);
    gst_omx_port_set_flushing (self->out_port, FALSE);
  }

  /* Start the srcpad loop again */
  gst_pad_start_task (GST_BASE_VIDEO_CODEC_SRC_PAD (self),
      (GstTaskFunction) gst_omx_video_dec_loop, decoder);

  GST_DEBUG_OBJECT (self, "Reset decoder");

  return TRUE;
}

static GstFlowReturn
gst_omx_video_dec_parse_data (GstBaseVideoDecoder * decoder, gboolean at_eos)
{
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_omx_video_dec_handle_frame (GstBaseVideoDecoder * decoder,
    GstVideoFrame * frame)
{
  GstOMXAcquireBufferReturn acq_ret = GST_OMX_ACQUIRE_BUFFER_ERROR;
  GstOMXVideoDec *self;
  GstOMXVideoDecClass *klass;
  GstOMXBuffer *buf;
  GstBuffer *codec_data = NULL;
  guint offset = 0;
  GstClockTime timestamp, duration, timestamp_offset = 0;

  self = GST_OMX_VIDEO_DEC (decoder);
  klass = GST_OMX_VIDEO_DEC_GET_CLASS (self);

  GST_DEBUG_OBJECT (self, "Handling frame");

  timestamp = frame->presentation_timestamp;
  duration = frame->presentation_duration;

  if (klass->prepare_frame) {
    GstFlowReturn ret;

    ret = klass->prepare_frame (self, frame);
    if (ret != GST_FLOW_OK) {
      GST_ERROR_OBJECT (self, "Preparing frame failed: %s",
          gst_flow_get_name (ret));
      return ret;
    }
  }

  while (offset < GST_BUFFER_SIZE (frame->sink_buffer)) {
    /* Make sure to release the base class stream lock, otherwise
     * _loop() can't call _finish_frame() and we might block forever
     * because no input buffers are released */
    GST_BASE_VIDEO_CODEC_STREAM_UNLOCK (self);
    acq_ret = gst_omx_port_acquire_buffer (self->in_port, &buf);
    GST_BASE_VIDEO_CODEC_STREAM_LOCK (self);

    if (acq_ret == GST_OMX_ACQUIRE_BUFFER_ERROR) {
      goto component_error;
    } else if (acq_ret == GST_OMX_ACQUIRE_BUFFER_FLUSHING) {
      goto flushing;
    } else if (acq_ret == GST_OMX_ACQUIRE_BUFFER_RECONFIGURE) {
      if (gst_omx_port_reconfigure (self->in_port) != OMX_ErrorNone)
        goto reconfigure_error;
      /* Now get a new buffer and fill it */
      continue;
    } else if (acq_ret == GST_OMX_ACQUIRE_BUFFER_RECONFIGURED) {
      /* TODO: Anything to do here? Don't think so */
      continue;
    }

    g_assert (acq_ret == GST_OMX_ACQUIRE_BUFFER_OK && buf != NULL);

    if (buf->omx_buf->nAllocLen - buf->omx_buf->nOffset <= 0)
      goto full_buffer;

    if (self->codec_data) {
      codec_data = self->codec_data;

      if (buf->omx_buf->nAllocLen - buf->omx_buf->nOffset <
          GST_BUFFER_SIZE (codec_data)) {
        gst_omx_port_release_buffer (self->in_port, buf);
        goto too_large_codec_data;
      }

      buf->omx_buf->nFlags |= OMX_BUFFERFLAG_CODECCONFIG;
      buf->omx_buf->nFilledLen = GST_BUFFER_SIZE (codec_data);
      memcpy (buf->omx_buf->pBuffer + buf->omx_buf->nOffset,
          GST_BUFFER_DATA (codec_data), GST_BUFFER_SIZE (codec_data));

      self->started = TRUE;
      gst_omx_port_release_buffer (self->in_port, buf);
      gst_buffer_replace (&self->codec_data, NULL);
      /* Acquire new buffer for the actual frame */
      continue;
    }

    /* Now handle the frame */

    /* Copy the buffer content in chunks of size as requested
     * by the port */
    buf->omx_buf->nFilledLen =
        MIN (GST_BUFFER_SIZE (frame->sink_buffer) - offset,
        buf->omx_buf->nAllocLen - buf->omx_buf->nOffset);
    memcpy (buf->omx_buf->pBuffer + buf->omx_buf->nOffset,
        GST_BUFFER_DATA (frame->sink_buffer) + offset,
        buf->omx_buf->nFilledLen);

    /* Interpolate timestamps if we're passing the buffer
     * in multiple chunks */
    if (offset != 0 && duration != GST_CLOCK_TIME_NONE) {
      timestamp_offset =
          gst_util_uint64_scale (offset, duration,
          GST_BUFFER_SIZE (frame->sink_buffer));
    }

    if (timestamp != GST_CLOCK_TIME_NONE) {
      buf->omx_buf->nTimeStamp =
          gst_util_uint64_scale (timestamp + timestamp_offset,
          OMX_TICKS_PER_SECOND, GST_SECOND);
    }
    if (duration != GST_CLOCK_TIME_NONE) {
      buf->omx_buf->nTickCount =
          gst_util_uint64_scale (buf->omx_buf->nFilledLen, duration,
          GST_BUFFER_SIZE (frame->sink_buffer));
    }

    if (offset == 0) {
      BufferIdentification *id = g_slice_new0 (BufferIdentification);

      if (!GST_BUFFER_FLAG_IS_SET (frame->sink_buffer,
              GST_BUFFER_FLAG_DELTA_UNIT))
        buf->omx_buf->nFlags |= OMX_BUFFERFLAG_SYNCFRAME;

      id->timestamp = buf->omx_buf->nTimeStamp;
      frame->coder_hook = id;
      frame->coder_hook_destroy_notify =
          (GDestroyNotify) buffer_identification_free;
    }

    /* TODO: Set flags
     *   - OMX_BUFFERFLAG_DECODEONLY for buffers that are outside
     *     the segment
     *   - OMX_BUFFERFLAG_ENDOFFRAME for parsed input
     */

    offset += buf->omx_buf->nFilledLen;
    self->started = TRUE;
    gst_omx_port_release_buffer (self->in_port, buf);
  }

  return GST_FLOW_OK;

full_buffer:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL),
        ("Got OpenMAX buffer with no free space (%p, %u/%u)", buf,
            buf->omx_buf->nOffset, buf->omx_buf->nAllocLen));
    gst_omx_port_release_buffer (self->in_port, buf);
    return GST_FLOW_ERROR;
  }

too_large_codec_data:
  {
    GST_ELEMENT_ERROR (self, STREAM, FORMAT, (NULL),
        ("codec_data larger than supported by OpenMAX port (%u > %u)",
            GST_BUFFER_SIZE (codec_data), self->in_port->port_def.nBufferSize));
    return GST_FLOW_ERROR;
  }

component_error:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL),
        ("OpenMAX component in error state %s (0x%08x)",
            gst_omx_component_get_last_error_string (self->component),
            gst_omx_component_get_last_error (self->component)));
    return GST_FLOW_ERROR;
  }

flushing:
  {
    GST_DEBUG_OBJECT (self, "Flushing -- returning WRONG_STATE");
    return GST_FLOW_WRONG_STATE;
  }
reconfigure_error:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS, (NULL),
        ("Unable to reconfigure input port"));
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_omx_video_dec_finish (GstBaseVideoDecoder * decoder)
{
  GstOMXVideoDec *self;
  GstOMXBuffer *buf;
  GstOMXAcquireBufferReturn acq_ret;

  self = GST_OMX_VIDEO_DEC (decoder);

  GST_DEBUG_OBJECT (self, "Sending EOS to the component");

  /* Send an EOS buffer to the component and let the base
   * class drop the EOS event. We will send it later when
   * the EOS buffer arrives on the output port. */
  acq_ret = gst_omx_port_acquire_buffer (self->in_port, &buf);
  if (acq_ret == GST_OMX_ACQUIRE_BUFFER_OK) {
    buf->omx_buf->nFlags |= OMX_BUFFERFLAG_EOS;
    gst_omx_port_release_buffer (self->in_port, buf);
  }

  return GST_BASE_VIDEO_DECODER_FLOW_DROPPED;
}
