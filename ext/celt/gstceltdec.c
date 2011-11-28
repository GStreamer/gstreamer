/* GStreamer
 * Copyright (C) 2004 Wim Taymans <wim@fluendo.com>
 * Copyright (C) 2006 Tim-Philipp Müller <tim centricular net>
 * Copyright (C) 2008 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

/*
 * Based on the speexdec element.
 */

/**
 * SECTION:element-celtdec
 * @see_also: celtenc, oggdemux
 *
 * This element decodes a CELT stream to raw integer audio.
 *
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * gst-launch -v filesrc location=celt.ogg ! oggdemux ! celtdec ! audioconvert ! audioresample ! alsasink
 * ]| Decode an Ogg/Celt file. To create an Ogg/Celt file refer to the documentation of celtenc.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gstceltdec.h"
#include <string.h>
#include <gst/tag/tag.h>

GST_DEBUG_CATEGORY_STATIC (celtdec_debug);
#define GST_CAT_DEFAULT celtdec_debug

#define DEC_MAX_FRAME_SIZE 2000

static GstStaticPadTemplate celt_dec_src_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "rate = (int) [ 32000, 64000 ], "
        "channels = (int) [ 1, 2 ], "
        "endianness = (int) BYTE_ORDER, "
        "signed = (boolean) true, " "width = (int) 16, " "depth = (int) 16")
    );

static GstStaticPadTemplate celt_dec_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-celt")
    );

GST_BOILERPLATE (GstCeltDec, gst_celt_dec, GstAudioDecoder,
    GST_TYPE_AUDIO_DECODER);

static gboolean gst_celt_dec_start (GstAudioDecoder * dec);
static gboolean gst_celt_dec_stop (GstAudioDecoder * dec);
static gboolean gst_celt_dec_set_format (GstAudioDecoder * bdec,
    GstCaps * caps);
static GstFlowReturn gst_celt_dec_handle_frame (GstAudioDecoder * dec,
    GstBuffer * buffer);

static void
gst_celt_dec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class,
      &celt_dec_src_factory);
  gst_element_class_add_static_pad_template (element_class,
      &celt_dec_sink_factory);
  gst_element_class_set_details_simple (element_class, "Celt audio decoder",
      "Codec/Decoder/Audio",
      "decode celt streams to audio",
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");
}

static void
gst_celt_dec_class_init (GstCeltDecClass * klass)
{
  GstAudioDecoderClass *gstbase_class;

  gstbase_class = (GstAudioDecoderClass *) klass;

  gstbase_class->start = GST_DEBUG_FUNCPTR (gst_celt_dec_start);
  gstbase_class->stop = GST_DEBUG_FUNCPTR (gst_celt_dec_stop);
  gstbase_class->set_format = GST_DEBUG_FUNCPTR (gst_celt_dec_set_format);
  gstbase_class->handle_frame = GST_DEBUG_FUNCPTR (gst_celt_dec_handle_frame);

  GST_DEBUG_CATEGORY_INIT (celtdec_debug, "celtdec", 0,
      "celt decoding element");
}

static void
gst_celt_dec_reset (GstCeltDec * dec)
{
  dec->packetno = 0;
  dec->frame_size = 0;
  if (dec->state) {
    celt_decoder_destroy (dec->state);
    dec->state = NULL;
  }

  if (dec->mode) {
    celt_mode_destroy (dec->mode);
    dec->mode = NULL;
  }

  gst_buffer_replace (&dec->streamheader, NULL);
  gst_buffer_replace (&dec->vorbiscomment, NULL);
  g_list_foreach (dec->extra_headers, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (dec->extra_headers);
  dec->extra_headers = NULL;

  memset (&dec->header, 0, sizeof (dec->header));
}

static void
gst_celt_dec_init (GstCeltDec * dec, GstCeltDecClass * g_class)
{
  gst_celt_dec_reset (dec);
}

static gboolean
gst_celt_dec_start (GstAudioDecoder * dec)
{
  GstCeltDec *cd = GST_CELT_DEC (dec);

  GST_DEBUG_OBJECT (dec, "start");
  gst_celt_dec_reset (cd);

  /* we know about concealment */
  gst_audio_decoder_set_plc_aware (dec, TRUE);

  return TRUE;
}

static gboolean
gst_celt_dec_stop (GstAudioDecoder * dec)
{
  GstCeltDec *cd = GST_CELT_DEC (dec);

  GST_DEBUG_OBJECT (dec, "stop");
  gst_celt_dec_reset (cd);

  return TRUE;
}

static GstFlowReturn
gst_celt_dec_parse_header (GstCeltDec * dec, GstBuffer * buf)
{
  GstCaps *caps;
  gint error = CELT_OK;

  /* get the header */
  error =
      celt_header_from_packet ((const unsigned char *) GST_BUFFER_DATA (buf),
      GST_BUFFER_SIZE (buf), &dec->header);
  if (error < 0)
    goto invalid_header;

  if (memcmp (dec->header.codec_id, "CELT    ", 8) != 0)
    goto invalid_header;

#ifdef HAVE_CELT_0_7
  dec->mode =
      celt_mode_create (dec->header.sample_rate,
      dec->header.frame_size, &error);
#else
  dec->mode =
      celt_mode_create (dec->header.sample_rate, dec->header.nb_channels,
      dec->header.frame_size, &error);
#endif
  if (!dec->mode)
    goto mode_init_failed;

  /* initialize the decoder */
#ifdef HAVE_CELT_0_11
  dec->state =
      celt_decoder_create_custom (dec->mode, dec->header.nb_channels, &error);
#else
#ifdef HAVE_CELT_0_7
  dec->state = celt_decoder_create (dec->mode, dec->header.nb_channels, &error);
#else
  dec->state = celt_decoder_create (dec->mode);
#endif
#endif
  if (!dec->state)
    goto init_failed;

#ifdef HAVE_CELT_0_8
  dec->frame_size = dec->header.frame_size;
#else
  celt_mode_info (dec->mode, CELT_GET_FRAME_SIZE, &dec->frame_size);
#endif

  /* set caps */
  caps = gst_caps_new_simple ("audio/x-raw-int",
      "rate", G_TYPE_INT, dec->header.sample_rate,
      "channels", G_TYPE_INT, dec->header.nb_channels,
      "signed", G_TYPE_BOOLEAN, TRUE,
      "endianness", G_TYPE_INT, G_BYTE_ORDER,
      "width", G_TYPE_INT, 16, "depth", G_TYPE_INT, 16, NULL);

  GST_DEBUG_OBJECT (dec, "rate=%d channels=%d frame-size=%d",
      dec->header.sample_rate, dec->header.nb_channels, dec->frame_size);

  if (!gst_pad_set_caps (GST_AUDIO_DECODER_SRC_PAD (dec), caps))
    goto nego_failed;

  gst_caps_unref (caps);
  return GST_FLOW_OK;

  /* ERRORS */
invalid_header:
  {
    GST_ELEMENT_ERROR (GST_ELEMENT (dec), STREAM, DECODE,
        (NULL), ("Invalid header"));
    return GST_FLOW_ERROR;
  }
mode_init_failed:
  {
    GST_ELEMENT_ERROR (GST_ELEMENT (dec), STREAM, DECODE,
        (NULL), ("Mode initialization failed: %d", error));
    return GST_FLOW_ERROR;
  }
init_failed:
  {
#ifdef HAVE_CELT_0_7
    GST_ELEMENT_ERROR (GST_ELEMENT (dec), STREAM, DECODE,
        (NULL), ("couldn't initialize decoder: %d", error));
#else
    GST_ELEMENT_ERROR (GST_ELEMENT (dec), STREAM, DECODE,
        (NULL), ("couldn't initialize decoder"));
#endif
    return GST_FLOW_ERROR;
  }
nego_failed:
  {
    GST_ELEMENT_ERROR (GST_ELEMENT (dec), STREAM, DECODE,
        (NULL), ("couldn't negotiate format"));
    gst_caps_unref (caps);
    return GST_FLOW_NOT_NEGOTIATED;
  }
}

static GstFlowReturn
gst_celt_dec_parse_comments (GstCeltDec * dec, GstBuffer * buf)
{
  GstTagList *list;
  gchar *ver, *encoder = NULL;

  list = gst_tag_list_from_vorbiscomment_buffer (buf, NULL, 0, &encoder);

  if (!list) {
    GST_WARNING_OBJECT (dec, "couldn't decode comments");
    list = gst_tag_list_new ();
  }

  if (encoder) {
    gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
        GST_TAG_ENCODER, encoder, NULL);
  }

  gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
      GST_TAG_AUDIO_CODEC, "Celt", NULL);

  ver = g_strndup (dec->header.codec_version, 20);
  g_strstrip (ver);

  if (ver != NULL && *ver != '\0') {
    gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
        GST_TAG_ENCODER_VERSION, ver, NULL);
  }

  if (dec->header.bytes_per_packet > 0) {
    gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
        GST_TAG_BITRATE, (guint) dec->header.bytes_per_packet * 8, NULL);
  }

  GST_INFO_OBJECT (dec, "tags: %" GST_PTR_FORMAT, list);

  gst_element_found_tags_for_pad (GST_ELEMENT (dec),
      GST_AUDIO_DECODER_SRC_PAD (dec), list);

  g_free (encoder);
  g_free (ver);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_celt_dec_parse_data (GstCeltDec * dec, GstBuffer * buf)
{
  GstFlowReturn res = GST_FLOW_OK;
  gint size;
  guint8 *data;
  GstBuffer *outbuf;
  gint16 *out_data;
  gint error = CELT_OK;
  int skip = 0;

  if (!dec->frame_size)
    goto not_negotiated;

  if (G_LIKELY (GST_BUFFER_SIZE (buf))) {
    data = GST_BUFFER_DATA (buf);
    size = GST_BUFFER_SIZE (buf);
  } else {
    /* FIXME ? actually consider how much concealment is needed */
    /* concealment data, pass NULL as the bits parameters */
    GST_DEBUG_OBJECT (dec, "creating concealment data");
    data = NULL;
    size = 0;
  }

  /* FIXME really needed ?; this might lead to skipping samples below
   * which kind of messes with subsequent timestamping */
  if (G_UNLIKELY (GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DISCONT))) {
#ifdef CELT_GET_LOOKAHEAD_REQUEST
    /* what will be 0.11.5, I guess, but no versioning yet in git */
    celt_decoder_ctl (dec->state, CELT_GET_LOOKAHEAD_REQUEST, &skip);
#else
    celt_mode_info (dec->mode, CELT_GET_LOOKAHEAD, &skip);
#endif
  }

  res = gst_pad_alloc_buffer_and_set_caps (GST_AUDIO_DECODER_SRC_PAD (dec),
      GST_BUFFER_OFFSET_NONE, dec->frame_size * dec->header.nb_channels * 2,
      GST_PAD_CAPS (GST_AUDIO_DECODER_SRC_PAD (dec)), &outbuf);

  if (res != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (dec, "buf alloc flow: %s", gst_flow_get_name (res));
    return res;
  }

  out_data = (gint16 *) GST_BUFFER_DATA (outbuf);

  GST_LOG_OBJECT (dec, "decoding frame");

#ifdef HAVE_CELT_0_8
  error = celt_decode (dec->state, data, size, out_data, dec->frame_size);
#else
  error = celt_decode (dec->state, data, size, out_data);
#endif
#ifdef HAVE_CELT_0_11
  if (error < 0) {
#else
  if (error != CELT_OK) {
#endif
    GST_WARNING_OBJECT (dec, "Decoding error: %d", error);
    return GST_FLOW_ERROR;
  }

  if (skip > 0) {
    GST_ERROR_OBJECT (dec, "skipping %d samples", skip);
    GST_BUFFER_DATA (outbuf) = GST_BUFFER_DATA (outbuf) +
        skip * dec->header.nb_channels * 2;
    GST_BUFFER_SIZE (outbuf) = GST_BUFFER_SIZE (outbuf) -
        skip * dec->header.nb_channels * 2;
  }

  res = gst_audio_decoder_finish_frame (GST_AUDIO_DECODER (dec), outbuf, 1);

  if (res != GST_FLOW_OK)
    GST_DEBUG_OBJECT (dec, "flow: %s", gst_flow_get_name (res));

  return res;

  /* ERRORS */
not_negotiated:
  {
    GST_ELEMENT_ERROR (dec, CORE, NEGOTIATION, (NULL),
        ("decoder not initialized"));
    return GST_FLOW_NOT_NEGOTIATED;
  }
}

static gboolean
gst_celt_dec_set_format (GstAudioDecoder * bdec, GstCaps * caps)
{
  GstCeltDec *dec = GST_CELT_DEC (bdec);
  gboolean ret = TRUE;
  GstStructure *s;
  const GValue *streamheader;

  s = gst_caps_get_structure (caps, 0);
  if ((streamheader = gst_structure_get_value (s, "streamheader")) &&
      G_VALUE_HOLDS (streamheader, GST_TYPE_ARRAY) &&
      gst_value_array_get_size (streamheader) >= 2) {
    const GValue *header, *vorbiscomment;
    GstBuffer *buf;
    GstFlowReturn res = GST_FLOW_OK;

    header = gst_value_array_get_value (streamheader, 0);
    if (header && G_VALUE_HOLDS (header, GST_TYPE_BUFFER)) {
      buf = gst_value_get_buffer (header);
      res = gst_celt_dec_parse_header (dec, buf);
      if (res != GST_FLOW_OK)
        goto done;
      gst_buffer_replace (&dec->streamheader, buf);
    }

    vorbiscomment = gst_value_array_get_value (streamheader, 1);
    if (vorbiscomment && G_VALUE_HOLDS (vorbiscomment, GST_TYPE_BUFFER)) {
      buf = gst_value_get_buffer (vorbiscomment);
      res = gst_celt_dec_parse_comments (dec, buf);
      if (res != GST_FLOW_OK)
        goto done;
      gst_buffer_replace (&dec->vorbiscomment, buf);
    }

    g_list_foreach (dec->extra_headers, (GFunc) gst_mini_object_unref, NULL);
    g_list_free (dec->extra_headers);
    dec->extra_headers = NULL;

    if (gst_value_array_get_size (streamheader) > 2) {
      gint i, n;

      n = gst_value_array_get_size (streamheader);
      for (i = 2; i < n; i++) {
        header = gst_value_array_get_value (streamheader, i);
        buf = gst_value_get_buffer (header);
        dec->extra_headers =
            g_list_prepend (dec->extra_headers, gst_buffer_ref (buf));
      }
    }
  }

done:
  return ret;
}

static GstFlowReturn
gst_celt_dec_handle_frame (GstAudioDecoder * bdec, GstBuffer * buf)
{
  GstFlowReturn res;
  GstCeltDec *dec;

  dec = GST_CELT_DEC (bdec);

  /* no fancy draining */
  if (G_UNLIKELY (!buf))
    return GST_FLOW_OK;

  /* If we have the streamheader and vorbiscomment from the caps already
   * ignore them here */
  if (dec->streamheader && dec->vorbiscomment) {
    if (GST_BUFFER_SIZE (dec->streamheader) == GST_BUFFER_SIZE (buf)
        && memcmp (GST_BUFFER_DATA (dec->streamheader), GST_BUFFER_DATA (buf),
            GST_BUFFER_SIZE (buf)) == 0) {
      GST_DEBUG_OBJECT (dec, "found streamheader");
      gst_audio_decoder_finish_frame (bdec, NULL, 1);
      res = GST_FLOW_OK;
    } else if (GST_BUFFER_SIZE (dec->vorbiscomment) == GST_BUFFER_SIZE (buf)
        && memcmp (GST_BUFFER_DATA (dec->vorbiscomment), GST_BUFFER_DATA (buf),
            GST_BUFFER_SIZE (buf)) == 0) {
      GST_DEBUG_OBJECT (dec, "found vorbiscomments");
      gst_audio_decoder_finish_frame (bdec, NULL, 1);
      res = GST_FLOW_OK;
    } else {
      GList *l;

      for (l = dec->extra_headers; l; l = l->next) {
        GstBuffer *header = l->data;
        if (GST_BUFFER_SIZE (header) == GST_BUFFER_SIZE (buf) &&
            memcmp (GST_BUFFER_DATA (header), GST_BUFFER_DATA (buf),
                GST_BUFFER_SIZE (buf)) == 0) {
          GST_DEBUG_OBJECT (dec, "found extra header buffer");
          gst_audio_decoder_finish_frame (bdec, NULL, 1);
          res = GST_FLOW_OK;
          goto done;
        }
      }
      res = gst_celt_dec_parse_data (dec, buf);
    }
  } else {
    /* Otherwise fall back to packet counting and assume that the
     * first two packets are the headers. */
    if (dec->packetno == 0) {
      GST_DEBUG_OBJECT (dec, "counted streamheader");
      res = gst_celt_dec_parse_header (dec, buf);
      gst_audio_decoder_finish_frame (bdec, NULL, 1);
    } else if (dec->packetno == 1) {
      GST_DEBUG_OBJECT (dec, "counted vorbiscomments");
      res = gst_celt_dec_parse_comments (dec, buf);
      gst_audio_decoder_finish_frame (bdec, NULL, 1);
    } else if (dec->packetno <= 1 + dec->header.extra_headers) {
      GST_DEBUG_OBJECT (dec, "counted extra header");
      gst_audio_decoder_finish_frame (bdec, NULL, 1);
      res = GST_FLOW_OK;
    } else {
      res = gst_celt_dec_parse_data (dec, buf);
    }
  }

done:
  dec->packetno++;

  return res;
}
