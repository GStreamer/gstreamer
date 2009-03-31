/*
 * GStreamer
 * Copyright (C) 2009 Carl-Anton Ingmarsson <ca.ingmarsson@gmail.com>
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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
 * SECTION:element-vdpaumpegdecoder
 *
 * FIXME:Describe vdpaumpegdecoder here.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m fakesrc ! vdpaumpegdecoder ! fakesink silent=TRUE
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>
#include <string.h>

#include "mpegutil.h"
#include "gstvdpaumpegdecoder.h"

GST_DEBUG_CATEGORY_STATIC (gst_vdpau_mpeg_decoder_debug);
#define GST_CAT_DEFAULT gst_vdpau_mpeg_decoder_debug

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_SILENT
};

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpeg, mpegversion = (int) [ 1, 2 ], "
        "systemstream = (boolean) false, parsed = (boolean) true")
    );

GST_BOILERPLATE (GstVdpauMpegDecoder, gst_vdpau_mpeg_decoder, GstVdpauDecoder,
    GST_TYPE_VDPAU_DECODER);

static void gst_vdpau_mpeg_decoder_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_vdpau_mpeg_decoder_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static gboolean
gst_vdpau_mpeg_decoder_set_caps (GstVdpauDecoder * dec, GstCaps * caps)
{
  GstVdpauMpegDecoder *mpeg_dec;
  GstStructure *structure;
  const GValue *value;
  GstBuffer *codec_data;
  MPEGSeqHdr hdr = { 0, };
  VdpDecoderProfile profile;
  VdpauFunctions *f;
  VdpStatus status;

  mpeg_dec = GST_VDPAU_MPEG_DECODER (dec);

  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_int (structure, "mpegversion", &mpeg_dec->version);
  if (mpeg_dec->version == 1)
    profile = VDP_DECODER_PROFILE_MPEG1;

  value = gst_structure_get_value (structure, "codec_data");
  codec_data = gst_value_get_buffer (value);
  mpeg_util_parse_sequence_hdr (&hdr, GST_BUFFER_DATA (codec_data),
      GST_BUFFER_DATA (codec_data) + GST_BUFFER_SIZE (codec_data));
  if (mpeg_dec->version != 1) {
    switch (hdr.profile) {
      case 5:
        profile = VDP_DECODER_PROFILE_MPEG2_SIMPLE;
        break;
      default:
        profile = VDP_DECODER_PROFILE_MPEG2_MAIN;
        break;
    }
  }
  memcpy (&mpeg_dec->vdp_info.intra_quantizer_matrix,
      &hdr.intra_quantizer_matrix, 64);
  memcpy (&mpeg_dec->vdp_info.non_intra_quantizer_matrix,
      &hdr.non_intra_quantizer_matrix, 64);

  f = dec->functions;
  status = f->vdp_decoder_create (dec->device, profile, dec->width,
      dec->height, 2, &mpeg_dec->decoder);
  if (status != VDP_STATUS_OK) {
    GST_ELEMENT_ERROR (mpeg_dec, RESOURCE, READ,
        ("Could not create vdpau decoder"),
        ("Error returned from vdpau was: %s",
            f->vdp_get_error_string (status)));
    return FALSE;
  }
  return TRUE;
}

static GstFlowReturn
gst_vdpau_mpeg_decoder_decode (GstVdpauMpegDecoder * mpeg_dec)
{
  GstVdpauDecoder *dec;
  GstBuffer *buffer;
  VdpVideoSurface surface;
  VdpauFunctions *f;
  VdpBitstreamBuffer vbit[1];
  VdpStatus status;
  GstFlowReturn ret;

  dec = GST_VDPAU_DECODER (mpeg_dec);

  buffer = gst_adapter_take_buffer (mpeg_dec->adapter,
      gst_adapter_available (mpeg_dec->adapter));

  if (mpeg_dec->vdp_info.picture_coding_type == P_FRAME) {
    mpeg_dec->p_buffer = buffer;
  }

  surface =
      gst_vdpau_decoder_create_video_surface (GST_VDPAU_DECODER (mpeg_dec));

  f = dec->functions;

  vbit[0].struct_version = VDP_BITSTREAM_BUFFER_VERSION;
  vbit[0].bitstream = GST_BUFFER_DATA (buffer);
  vbit[0].bitstream_bytes = GST_BUFFER_SIZE (buffer);

  status = f->vdp_decoder_render (mpeg_dec->decoder, surface,
      (VdpPictureInfo *) & mpeg_dec->vdp_info, 1, vbit);
  gst_buffer_unref (buffer);
  mpeg_dec->vdp_info.slice_count = 0;

  if (status != VDP_STATUS_OK) {
    GST_ELEMENT_ERROR (mpeg_dec, RESOURCE, READ,
        ("Could not decode"),
        ("Error returned from vdpau was: %s",
            f->vdp_get_error_string (status)));

    if (mpeg_dec->vdp_info.forward_reference != VDP_INVALID_HANDLE)
      f->vdp_video_surface_destroy (mpeg_dec->vdp_info.forward_reference);

    f->vdp_video_surface_destroy (surface);

    return GST_FLOW_ERROR;
  }

  ret =
      gst_vdpau_decoder_push_video_surface (GST_VDPAU_DECODER (mpeg_dec),
      surface);

  if (mpeg_dec->vdp_info.forward_reference != VDP_INVALID_HANDLE)
    f->vdp_video_surface_destroy (mpeg_dec->vdp_info.forward_reference);

  mpeg_dec->vdp_info.forward_reference = surface;

  return ret;
}

static gboolean
gst_vdpau_mpeg_decoder_parse_picture_coding (GstVdpauMpegDecoder * mpeg_dec,
    guint8 * data, guint8 * end)
{
  GstVdpauDecoder *dec;
  MPEGPictureExt pic_ext;
  VdpPictureInfoMPEG1Or2 *info;

  dec = GST_VDPAU_DECODER (mpeg_dec);
  info = &mpeg_dec->vdp_info;

  if (!mpeg_util_parse_picture_coding_extension (&pic_ext, data, end))
    return FALSE;

  memcpy (&mpeg_dec->vdp_info.f_code, &pic_ext.f_code, 4);

  info->intra_dc_precision = pic_ext.intra_dc_precision;
  info->picture_structure = pic_ext.picture_structure;
  info->top_field_first = pic_ext.top_field_first;
  info->frame_pred_frame_dct = pic_ext.frame_pred_frame_dct;
  info->concealment_motion_vectors = pic_ext.concealment_motion_vectors;
  info->q_scale_type = pic_ext.q_scale_type;
  info->intra_vlc_format = pic_ext.intra_vlc_format;

  return TRUE;
}

static gboolean
gst_vdpau_mpeg_decoder_parse_picture (GstVdpauMpegDecoder * mpeg_dec,
    guint8 * data, guint8 * end)
{
  GstVdpauDecoder *dec;
  MPEGPictureHdr pic_hdr;

  dec = GST_VDPAU_DECODER (mpeg_dec);

  if (!mpeg_util_parse_picture_hdr (&pic_hdr, data, end))
    return FALSE;

  mpeg_dec->vdp_info.picture_coding_type = pic_hdr.pic_type;


  if (pic_hdr.pic_type == I_FRAME &&
      mpeg_dec->vdp_info.forward_reference != VDP_INVALID_HANDLE) {
    dec->functions->vdp_video_surface_destroy (mpeg_dec->vdp_info.
        forward_reference);
    mpeg_dec->vdp_info.forward_reference = VDP_INVALID_HANDLE;
  }

  if (mpeg_dec->version == 1) {
    mpeg_dec->vdp_info.full_pel_forward_vector =
        pic_hdr.full_pel_forward_vector;
    mpeg_dec->vdp_info.full_pel_backward_vector =
        pic_hdr.full_pel_backward_vector;
    memcpy (&mpeg_dec->vdp_info.f_code, &pic_hdr.f_code, 4);
  }

  return TRUE;
}

static gboolean
gst_vdpau_mpeg_decoder_parse_gop (GstVdpauMpegDecoder * mpeg_dec, guint8 * data,
    guint8 * end)
{
  MPEGPictureGOP gop;

  if (!mpeg_util_parse_picture_gop (&gop, data, end))
    return FALSE;

  return TRUE;
}

static gboolean
gst_vdpau_mpeg_decoder_parse_quant_matrix (GstVdpauMpegDecoder * mpeg_dec,
    guint8 * data, guint8 * end)
{
  MPEGQuantMatrix qm;

  if (!mpeg_util_parse_quant_matrix (&qm, data, end))
    return FALSE;

  memcpy (&mpeg_dec->vdp_info.intra_quantizer_matrix,
      &qm.intra_quantizer_matrix, 64);
  memcpy (&mpeg_dec->vdp_info.non_intra_quantizer_matrix,
      &qm.non_intra_quantizer_matrix, 64);
  return TRUE;
}

static GstFlowReturn
gst_vdpau_mpeg_decoder_chain (GstPad * pad, GstBuffer * buffer)
{
  GstVdpauMpegDecoder *mpeg_dec;
  guint8 *data, *end;
  guint32 sync_word = 0xffffffff;

  mpeg_dec = GST_VDPAU_MPEG_DECODER (GST_OBJECT_PARENT (pad));

  data = GST_BUFFER_DATA (buffer);
  end = GST_BUFFER_DATA (buffer) + GST_BUFFER_SIZE (buffer);

  while ((data = mpeg_util_find_start_code (&sync_word, data, end))) {
    guint8 *packet_start;
    guint8 *packet_end;

    packet_start = data - 3;
    packet_end = mpeg_util_find_start_code (&sync_word, data, end);
    if (packet_end)
      packet_end -= 3;
    else
      packet_end = end;

    if (data[0] >= MPEG_PACKET_SLICE_MIN && data[0] <= MPEG_PACKET_SLICE_MAX) {
      GstBuffer *subbuf;

      GST_DEBUG_OBJECT (mpeg_dec, "MPEG_PACKET_SLICE");
      subbuf =
          gst_buffer_create_sub (buffer,
          packet_start - GST_BUFFER_DATA (buffer), packet_end - packet_start);
      gst_adapter_push (mpeg_dec->adapter, subbuf);
      mpeg_dec->vdp_info.slice_count++;
    } else if (mpeg_dec->vdp_info.slice_count > 0) {
      if (gst_vdpau_mpeg_decoder_decode (mpeg_dec) != GST_FLOW_OK)
        return GST_FLOW_ERROR;
    }

    switch (data[0]) {
      case MPEG_PACKET_PICTURE:
        GST_DEBUG_OBJECT (mpeg_dec, "MPEG_PACKET_PICTURE");
        gst_vdpau_mpeg_decoder_parse_picture (mpeg_dec, packet_start,
            packet_end);
        break;
      case MPEG_PACKET_SEQUENCE:
        GST_DEBUG_OBJECT (mpeg_dec, "MPEG_PACKET_SEQUENCE");
        break;
      case MPEG_PACKET_EXTENSION:
        GST_DEBUG_OBJECT (mpeg_dec, "MPEG_PACKET_EXTENSION");
        switch (read_bits (data + 1, 0, 4)) {
          case MPEG_PACKET_EXT_PICTURE_CODING:
            gst_vdpau_mpeg_decoder_parse_picture_coding (mpeg_dec, packet_start,
                packet_end);
            break;
          default:
            break;
        }
        break;
      case MPEG_PACKET_EXT_QUANT_MATRIX:
        GST_DEBUG_OBJECT (mpeg_dec, "MPEG_PACKET_EXT_QUANT_MATRIX");
        gst_vdpau_mpeg_decoder_parse_quant_matrix (mpeg_dec, packet_start,
            packet_end);
        break;
      case MPEG_PACKET_GOP:
        GST_DEBUG_OBJECT (mpeg_dec, "MPEG_PACKET_GOP");
        gst_vdpau_mpeg_decoder_parse_gop (mpeg_dec, packet_start, packet_end);
        break;
      default:
        break;
    }
  }

  return GST_FLOW_OK;
}

/* GObject vmethod implementations */

static void
gst_vdpau_mpeg_decoder_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_set_details_simple (element_class,
      "VdpauMpegDecoder",
      "Decoder",
      "decode mpeg stream with vdpau",
      "Carl-Anton Ingmarsson <ca.ingmarsson@gmail.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
}

/* initialize the vdpaumpegdecoder's class */
static void
gst_vdpau_mpeg_decoder_class_init (GstVdpauMpegDecoderClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstVdpauDecoderClass *vdpaudec_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  vdpaudec_class = (GstVdpauDecoderClass *) klass;

  gobject_class->set_property = gst_vdpau_mpeg_decoder_set_property;
  gobject_class->get_property = gst_vdpau_mpeg_decoder_get_property;

  g_object_class_install_property (gobject_class, PROP_SILENT,
      g_param_spec_boolean ("silent", "Silent", "Produce verbose output ?",
          FALSE, G_PARAM_READWRITE));

  vdpaudec_class->set_caps = gst_vdpau_mpeg_decoder_set_caps;
}

static void
gst_vdpau_mpeg_decoder_init_info (VdpPictureInfoMPEG1Or2 * vdp_info)
{
  vdp_info->forward_reference = VDP_INVALID_HANDLE;
  vdp_info->backward_reference = VDP_INVALID_HANDLE;
  vdp_info->slice_count = 0;
  vdp_info->picture_structure = 0;
  vdp_info->picture_coding_type = 0;
  vdp_info->intra_dc_precision = 0;
  vdp_info->frame_pred_frame_dct = 0;
  vdp_info->concealment_motion_vectors = 0;
}

static void
gst_vdpau_mpeg_decoder_init (GstVdpauMpegDecoder * mpeg_dec,
    GstVdpauMpegDecoderClass * gclass)
{
  GstVdpauDecoder *dec;

  dec = GST_VDPAU_DECODER (mpeg_dec);

  mpeg_dec->silent = FALSE;
  mpeg_dec->decoder = VDP_INVALID_HANDLE;
  gst_vdpau_mpeg_decoder_init_info (&mpeg_dec->vdp_info);

  mpeg_dec->adapter = gst_adapter_new ();

  gst_pad_set_chain_function (dec->sink, gst_vdpau_mpeg_decoder_chain);
}

static void
gst_vdpau_mpeg_decoder_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVdpauMpegDecoder *filter = GST_VDPAU_MPEG_DECODER (object);

  switch (prop_id) {
    case PROP_SILENT:
      filter->silent = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vdpau_mpeg_decoder_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVdpauMpegDecoder *filter = GST_VDPAU_MPEG_DECODER (object);

  switch (prop_id) {
    case PROP_SILENT:
      g_value_set_boolean (value, filter->silent);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
vdpaumpegdecoder_init (GstPlugin * vdpaumpegdecoder)
{
  /* debug category for fltering log messages
   *
   * exchange the string 'Template vdpaumpegdecoder' with your description
   */
  GST_DEBUG_CATEGORY_INIT (gst_vdpau_mpeg_decoder_debug, "vdpaumpegdecoder",
      0, "Template vdpaumpegdecoder");

  return gst_element_register (vdpaumpegdecoder, "vdpaumpegdecoder",
      GST_RANK_NONE, GST_TYPE_VDPAU_MPEG_DECODER);
}

/* gstreamer looks for this structure to register vdpaumpegdecoders
 *
 * exchange the string 'Template vdpaumpegdecoder' with your vdpaumpegdecoder description
 */
GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "vdpaumpegdecoder",
    "Template vdpaumpegdecoder",
    vdpaumpegdecoder_init,
    VERSION, "LGPL", "GStreamer", "http://gstreamer.net/")
