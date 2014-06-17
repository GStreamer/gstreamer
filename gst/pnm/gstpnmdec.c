/* GStreamer PNM decoder
 * Copyright (C) 2009 Lutz Mueller <lutz@users.sourceforge.net>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:element-pnmdec
 *
 * Decodes pnm images.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch filesrc location=test.pnm ! pnmdec ! videoconvert ! autovideosink
 * ]| The above pipeline reads a pnm file and renders it to the screen.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstpnmdec.h"
#include "gstpnmutils.h"

#include <gst/gstutils.h>
#include <gst/video/video.h>
#include <gst/base/gstbytereader.h>
#include <string.h>
#include <stdio.h>

static gboolean gst_pnmdec_start (GstVideoDecoder * decoder);
static GstFlowReturn gst_pnmdec_parse (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame, GstAdapter * adapter, gboolean at_eos);
static GstFlowReturn gst_pnmdec_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame);
static GstFlowReturn
gst_pnmdec_parse_ascii (GstPnmdec * s, const guint8 * b, guint bs);

G_DEFINE_TYPE (GstPnmdec, gst_pnmdec, GST_TYPE_VIDEO_DECODER);

static GstStaticPadTemplate gst_pnmdec_src_pad_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("RGB") "; "
        GST_VIDEO_CAPS_MAKE ("GRAY8")));

static GstStaticPadTemplate gst_pnmdec_sink_pad_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (MIME_ALL));


static void
gst_pnmdec_class_init (GstPnmdecClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstVideoDecoderClass *vdec_class = (GstVideoDecoderClass *) klass;

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_pnmdec_src_pad_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_pnmdec_sink_pad_template));
  gst_element_class_set_static_metadata (element_class, "PNM image decoder",
      "Codec/Decoder/Image",
      "Decodes images in portable pixmap/graymap/bitmap/anymamp (PNM) format",
      "Lutz Mueller <lutz@users.sourceforge.net>");

  vdec_class->start = gst_pnmdec_start;
  vdec_class->parse = gst_pnmdec_parse;
  vdec_class->handle_frame = gst_pnmdec_handle_frame;
}

static void
gst_pnmdec_flush (GstPnmdec * s)
{
  s->mngr.info.width = 0;
  s->mngr.info.height = 0;
  s->mngr.info.fields = 0;
  s->mngr.info.max = 0;
  s->size = 0;
  s->current_size = 0;
  if (s->buf) {
    gst_buffer_unref (s->buf);
    s->buf = NULL;
  }
}

static void
gst_pnmdec_init (GstPnmdec * s)
{
  /* Initialize decoder */
  s->buf = NULL;
  gst_pnmdec_flush (s);
}

static GstFlowReturn
gst_pnmdec_parse_ascii (GstPnmdec * s, const guint8 * b, guint bs)
{
  GScanner *scanner;
  guint i = 0;
  guint target;
  GstMapInfo map;
  guint8 *outdata;

  target = s->size - s->current_size;

  gst_buffer_map (s->buf, &map, GST_MAP_WRITE);

  /* leave the number of bytes already parsed */
  outdata = map.data + s->current_size;
  if (!bs) {
    goto drop_ok;
  }

  if (s->last_byte) {
    while (*b >= '0' && *b <= '9') {
      s->last_byte = 10 * s->last_byte + *b - '0';
      b++;
      if (!--bs) {
        goto drop_error;
      }
    }
    if (s->last_byte > 255) {
      GST_DEBUG_OBJECT (s, "Corrupt ASCII encoded PNM file.");
      goto drop_error;
    }
  }

  if (s->last_byte) {
    outdata[i++] = s->last_byte;
    s->last_byte = 0;
  }

  scanner = g_scanner_new (NULL);
  g_scanner_input_text (scanner, (gchar *) b, bs);
  while (!g_scanner_eof (scanner)) {
    switch (g_scanner_get_next_token (scanner)) {
      case G_TOKEN_INT:
        if (i == target) {
          GST_DEBUG_OBJECT (s, "PNM file contains too much data.");
          goto drop_error;
        }
        outdata[i++] = scanner->value.v_int;
        break;
      default:
        /* Should we care? */ ;
    }
  }
  g_scanner_destroy (scanner);

  /* If we didn't get the whole image, handle the last byte with care. */
  if (i && i < target && b[bs - 1] > '0' && b[bs - 1] <= '9') {
    s->last_byte = outdata[--i];
  }

  /* Update the number of bytes parsed in this scan */
  s->current_size += i;
  gst_buffer_unmap (s->buf, &map);

  return GST_FLOW_OK;
drop_ok:
  gst_buffer_unmap (s->buf, &map);
  return GST_FLOW_OK;

drop_error:
  gst_buffer_unmap (s->buf, &map);

  return GST_FLOW_ERROR;
}

static GstFlowReturn
gst_pnmdec_handle_frame (GstVideoDecoder * decoder, GstVideoCodecFrame * frame)
{
  GstPnmdec *s = (GstPnmdec *) decoder;
  GstMapInfo imap, omap;
  guint i_rowstride;
  guint o_rowstride;
  GstFlowReturn r = GST_FLOW_OK;
  gint bytes, i;

  r = gst_video_decoder_allocate_output_frame (decoder, frame);
  if (r != GST_FLOW_OK) {
    goto out;
  }

  if (s->mngr.info.encoding == GST_PNM_ENCODING_ASCII) {
    /* In case of ASCII parsed data is stored in buf, so input needs to be
       taken from here for frame processing */
    gst_buffer_map (s->buf, &imap, GST_MAP_READ);
  } else {
    gst_buffer_map (frame->input_buffer, &imap, GST_MAP_READ);
  }
  gst_buffer_map (frame->output_buffer, &omap, GST_MAP_WRITE);

  gst_buffer_copy_into (frame->output_buffer, frame->input_buffer,
      GST_BUFFER_COPY_METADATA, 0, 0);

  if (s->mngr.info.type == GST_PNM_TYPE_BITMAP) {
    bytes = (s->mngr.info.width * s->mngr.info.height + 7) / 8;
    for (i = 0; i < bytes; i++) {
      omap.data[i * 8] = (imap.data[i] & 0x80) ? 0 : 255;
      omap.data[i * 8 + 1] = (imap.data[i] & 0x40) ? 0 : 255;
      omap.data[i * 8 + 2] = (imap.data[i] & 0x20) ? 0 : 255;
      omap.data[i * 8 + 3] = (imap.data[i] & 0x10) ? 0 : 255;
      omap.data[i * 8 + 4] = (imap.data[i] & 0x08) ? 0 : 255;
      omap.data[i * 8 + 5] = (imap.data[i] & 0x04) ? 0 : 255;
      omap.data[i * 8 + 6] = (imap.data[i] & 0x02) ? 0 : 255;
      omap.data[i * 8 + 7] = (imap.data[i] & 0x01) ? 0 : 255;
    }
  } else
    /* Need to convert from PNM rowstride to GStreamer rowstride */
  if (s->mngr.info.width % 4 != 0) {
    if (s->mngr.info.type == GST_PNM_TYPE_PIXMAP) {
      i_rowstride = 3 * s->mngr.info.width;
      o_rowstride = GST_ROUND_UP_4 (i_rowstride);
    } else {
      i_rowstride = s->mngr.info.width;
      o_rowstride = GST_ROUND_UP_4 (i_rowstride);
    }

    for (i = 0; i < s->mngr.info.height; i++)
      memcpy (omap.data + i * o_rowstride, imap.data + i * i_rowstride,
          i_rowstride);
  } else {
    memcpy (omap.data, imap.data, s->size);
  }

  if (s->mngr.info.encoding == GST_PNM_ENCODING_ASCII) {
    gst_buffer_unmap (s->buf, &imap);
  } else {
    gst_buffer_unmap (frame->input_buffer, &imap);
  }
  gst_buffer_unmap (frame->output_buffer, &omap);

  r = gst_video_decoder_finish_frame (GST_VIDEO_DECODER (s), frame);

out:
  gst_pnmdec_flush (s);

  return r;
}

static GstFlowReturn
gst_pnmdec_parse (GstVideoDecoder * decoder, GstVideoCodecFrame * frame,
    GstAdapter * adapter, gboolean at_eos)
{
  gsize size;
  GstPnmdec *s = GST_PNMDEC (decoder);
  GstFlowReturn r = GST_FLOW_OK;
  guint offset = 0;
  GstVideoFormat format = GST_VIDEO_FORMAT_UNKNOWN;
  const guint8 *raw_data;
  GstVideoCodecState *output_state;

  GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT (frame);

  size = gst_adapter_available (adapter);
  if (size < 8) {
    goto need_more_data;
  }
  raw_data = gst_adapter_map (adapter, size);

  if (s->mngr.info.fields != GST_PNM_INFO_FIELDS_ALL) {
    GstPnmInfoMngrResult res;

    res = gst_pnm_info_mngr_scan (&s->mngr, raw_data, size);

    switch (res) {
      case GST_PNM_INFO_MNGR_RESULT_FAILED:
        r = GST_FLOW_ERROR;
        goto out;
      case GST_PNM_INFO_MNGR_RESULT_READING:
        r = GST_FLOW_OK;
        goto out;
      case GST_PNM_INFO_MNGR_RESULT_FINISHED:
        switch (s->mngr.info.type) {
          case GST_PNM_TYPE_BITMAP:
            if (s->mngr.info.encoding == GST_PNM_ENCODING_ASCII) {
              r = GST_FLOW_ERROR;
              goto out;
            }
            s->size = s->mngr.info.width * s->mngr.info.height * 1;
            format = GST_VIDEO_FORMAT_GRAY8;
            break;
          case GST_PNM_TYPE_GRAYMAP:
            s->size = s->mngr.info.width * s->mngr.info.height * 1;
            format = GST_VIDEO_FORMAT_GRAY8;
            break;
          case GST_PNM_TYPE_PIXMAP:
            s->size = s->mngr.info.width * s->mngr.info.height * 3;
            format = GST_VIDEO_FORMAT_RGB;
            break;
        }
        output_state =
            gst_video_decoder_set_output_state (GST_VIDEO_DECODER (s), format,
            s->mngr.info.width, s->mngr.info.height, NULL);
        gst_video_codec_state_unref (output_state);
        if (gst_video_decoder_negotiate (GST_VIDEO_DECODER (s)) == FALSE) {
          r = GST_FLOW_NOT_NEGOTIATED;
          goto out;
        }

        if (s->mngr.info.encoding == GST_PNM_ENCODING_ASCII) {
          s->mngr.data_offset++;
          /* It is not possible to know the size of input ascii data to parse.
             So we have to parse and know the number of pixels parsed and
             then finally decide when we have full frame */
          s->buf = gst_buffer_new_and_alloc (s->size);
        }
        offset = s->mngr.data_offset;
        gst_adapter_flush (adapter, offset);
        size = size - offset;
    }
  }

  if (s->mngr.info.encoding == GST_PNM_ENCODING_ASCII) {
    /* Parse ASCII data dn populate s->current_size with the number of 
       bytes actually parsed from the input data */
    r = gst_pnmdec_parse_ascii (s, raw_data + offset, size);
  } else {
    /* Bitmap Contains 8 pixels in a byte */
    if (s->mngr.info.type == GST_PNM_TYPE_BITMAP)
      s->current_size += (size * 8);
    else
      s->current_size += size;
  }

  gst_video_decoder_add_to_frame (decoder, size);
  if (s->size <= s->current_size) {
    goto have_full_frame;
  }

need_more_data:
  return GST_VIDEO_DECODER_FLOW_NEED_DATA;

have_full_frame:
  return gst_video_decoder_have_frame (decoder);

out:
  return r;
}

static gboolean
gst_pnmdec_start (GstVideoDecoder * decoder)
{
  GstPnmdec *pnmdec = (GstPnmdec *) decoder;

  gst_video_decoder_set_packetized (GST_VIDEO_DECODER (pnmdec), FALSE);
  return TRUE;
}
