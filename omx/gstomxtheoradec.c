/*
 * Copyright (C) 2013, Collabora Ltd.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

#include "gstomxtheoradec.h"

GST_DEBUG_CATEGORY_STATIC (gst_omx_theora_dec_debug_category);
#define GST_CAT_DEFAULT gst_omx_theora_dec_debug_category

/* prototypes */
static gboolean gst_omx_theora_dec_is_format_change (GstOMXVideoDec * dec,
    GstOMXPort * port, GstVideoCodecState * state);
static gboolean gst_omx_theora_dec_set_format (GstOMXVideoDec * dec,
    GstOMXPort * port, GstVideoCodecState * state);
static GstFlowReturn gst_omx_theora_dec_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame);
static gboolean gst_omx_theora_dec_stop (GstVideoDecoder * decoder);

enum
{
  PROP_0
};

/* class initialization */

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_omx_theora_dec_debug_category, "omxtheoradec", 0, \
      "debug category for gst-omx video decoder base class");

G_DEFINE_TYPE_WITH_CODE (GstOMXTheoraDec, gst_omx_theora_dec,
    GST_TYPE_OMX_VIDEO_DEC, DEBUG_INIT);

static void
gst_omx_theora_dec_class_init (GstOMXTheoraDecClass * klass)
{
  GstVideoDecoderClass *gstvideodec_class = GST_VIDEO_DECODER_CLASS (klass);
  GstOMXVideoDecClass *videodec_class = GST_OMX_VIDEO_DEC_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  videodec_class->is_format_change =
      GST_DEBUG_FUNCPTR (gst_omx_theora_dec_is_format_change);
  videodec_class->set_format =
      GST_DEBUG_FUNCPTR (gst_omx_theora_dec_set_format);

  videodec_class->cdata.default_sink_template_caps = "video/x-theora, "
      "width=(int) [1,MAX], " "height=(int) [1,MAX]";

  gstvideodec_class->handle_frame = gst_omx_theora_dec_handle_frame;
  gstvideodec_class->stop = gst_omx_theora_dec_stop;

  gst_element_class_set_static_metadata (element_class,
      "OpenMAX Theora Video Decoder",
      "Codec/Decoder/Video/Hardware",
      "Decode Theora video streams",
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");

  gst_omx_set_default_role (&videodec_class->cdata, "video_decoder.theora");
}

static void
gst_omx_theora_dec_init (GstOMXTheoraDec * self)
{
}

static gboolean
gst_omx_theora_dec_is_format_change (GstOMXVideoDec * dec,
    GstOMXPort * port, GstVideoCodecState * state)
{
  return FALSE;
}

static gboolean
gst_omx_theora_dec_set_format (GstOMXVideoDec * dec, GstOMXPort * port,
    GstVideoCodecState * state)
{
  gboolean ret;
  OMX_PARAM_PORTDEFINITIONTYPE port_def;

  gst_omx_port_get_port_definition (port, &port_def);
  port_def.format.video.eCompressionFormat = OMX_VIDEO_CodingTheora;
  ret = gst_omx_port_update_port_definition (port, &port_def) == OMX_ErrorNone;

  return ret;
}

static GstFlowReturn
gst_omx_theora_dec_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame)
{
  GstOMXTheoraDec *self = GST_OMX_THEORA_DEC (decoder);

  if (GST_BUFFER_FLAG_IS_SET (frame->input_buffer, GST_BUFFER_FLAG_HEADER)) {
    guint16 size;
    GstBuffer *sbuf;

    if (!self->header) {
      self->header = gst_buffer_new ();
      gst_buffer_copy_into (self->header, frame->input_buffer,
          GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS, 0, -1);
    }

    size = gst_buffer_get_size (frame->input_buffer);
    size = GUINT16_TO_BE (size);
    sbuf = gst_buffer_new_and_alloc (2);
    gst_buffer_fill (sbuf, 0, &size, 2);
    self->header = gst_buffer_append (self->header, sbuf);

    self->header =
        gst_buffer_append (self->header, gst_buffer_ref (frame->input_buffer));

    gst_video_decoder_drop_frame (GST_VIDEO_DECODER (self), frame);

    return GST_FLOW_OK;
  }

  if (self->header) {
    gst_buffer_replace (&GST_OMX_VIDEO_DEC (self)->codec_data, self->header);
    gst_buffer_unref (self->header);
    self->header = NULL;
  }

  return
      GST_VIDEO_DECODER_CLASS (gst_omx_theora_dec_parent_class)->handle_frame
      (GST_VIDEO_DECODER (self), frame);
}

static gboolean
gst_omx_theora_dec_stop (GstVideoDecoder * decoder)
{
  GstOMXTheoraDec *self = GST_OMX_THEORA_DEC (decoder);

  gst_buffer_replace (&self->header, NULL);

  return TRUE;
}
