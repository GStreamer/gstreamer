/* GStreamer
 *
 * Copyright (C) 2008 Sebastian Dröge <sebastian.droege@collabora.co.uk>.
 * Copyright (C) 2009 Mark Nauwelaerts <mark.nauwelaerts@collabora.co.uk>
 * Copyright (C) 2009 Nokia Corporation. All rights reserved.
 *   Contact: Stefan Kost <stefan.kost@nokia.com>
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
 * SECTION:element-flacparse
 * @see_also: flacdec, oggdemux, vorbisparse
 *
 * The flacparse element will parse the header packets of the FLAC
 * stream and put them as the streamheader in the caps. This is used in the
 * multifdsink case where you want to stream live FLAC streams to multiple
 * clients, each client has to receive the streamheaders first before they can
 * consume the FLAC packets.
 *
 * This element also makes sure that the buffers that it pushes out are properly
 * timestamped and that their offset and offset_end are set. The buffers that
 * flacparse outputs have all of the metadata that oggmux expects to receive,
 * which allows you to (for example) remux an ogg/flac or convert a native FLAC
 * format file to an ogg bitstream.
 *
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * gst-launch -v filesrc location=sine.flac ! flacparse ! identity \
 *            ! oggmux ! filesink location=sine-remuxed.ogg
 * ]| This pipeline converts a native FLAC format file to an ogg bitstream.
 * It also illustrates that the streamheader is set in the caps, and that each
 * buffer has the timestamp, duration, offset, and offset_end set.
 * </refsect2>
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstflacparse.h"

#include <string.h>
#include <gst/tag/tag.h>
#include <gst/audio/audio.h>

#include <gst/base/gstbitreader.h>
#include <gst/base/gstbytereader.h>

GST_DEBUG_CATEGORY_STATIC (flacparse_debug);
#define GST_CAT_DEFAULT flacparse_debug

/* CRC-8, poly = x^8 + x^2 + x^1 + x^0, init = 0 */
static const guint8 crc8_table[256] = {
  0x00, 0x07, 0x0E, 0x09, 0x1C, 0x1B, 0x12, 0x15,
  0x38, 0x3F, 0x36, 0x31, 0x24, 0x23, 0x2A, 0x2D,
  0x70, 0x77, 0x7E, 0x79, 0x6C, 0x6B, 0x62, 0x65,
  0x48, 0x4F, 0x46, 0x41, 0x54, 0x53, 0x5A, 0x5D,
  0xE0, 0xE7, 0xEE, 0xE9, 0xFC, 0xFB, 0xF2, 0xF5,
  0xD8, 0xDF, 0xD6, 0xD1, 0xC4, 0xC3, 0xCA, 0xCD,
  0x90, 0x97, 0x9E, 0x99, 0x8C, 0x8B, 0x82, 0x85,
  0xA8, 0xAF, 0xA6, 0xA1, 0xB4, 0xB3, 0xBA, 0xBD,
  0xC7, 0xC0, 0xC9, 0xCE, 0xDB, 0xDC, 0xD5, 0xD2,
  0xFF, 0xF8, 0xF1, 0xF6, 0xE3, 0xE4, 0xED, 0xEA,
  0xB7, 0xB0, 0xB9, 0xBE, 0xAB, 0xAC, 0xA5, 0xA2,
  0x8F, 0x88, 0x81, 0x86, 0x93, 0x94, 0x9D, 0x9A,
  0x27, 0x20, 0x29, 0x2E, 0x3B, 0x3C, 0x35, 0x32,
  0x1F, 0x18, 0x11, 0x16, 0x03, 0x04, 0x0D, 0x0A,
  0x57, 0x50, 0x59, 0x5E, 0x4B, 0x4C, 0x45, 0x42,
  0x6F, 0x68, 0x61, 0x66, 0x73, 0x74, 0x7D, 0x7A,
  0x89, 0x8E, 0x87, 0x80, 0x95, 0x92, 0x9B, 0x9C,
  0xB1, 0xB6, 0xBF, 0xB8, 0xAD, 0xAA, 0xA3, 0xA4,
  0xF9, 0xFE, 0xF7, 0xF0, 0xE5, 0xE2, 0xEB, 0xEC,
  0xC1, 0xC6, 0xCF, 0xC8, 0xDD, 0xDA, 0xD3, 0xD4,
  0x69, 0x6E, 0x67, 0x60, 0x75, 0x72, 0x7B, 0x7C,
  0x51, 0x56, 0x5F, 0x58, 0x4D, 0x4A, 0x43, 0x44,
  0x19, 0x1E, 0x17, 0x10, 0x05, 0x02, 0x0B, 0x0C,
  0x21, 0x26, 0x2F, 0x28, 0x3D, 0x3A, 0x33, 0x34,
  0x4E, 0x49, 0x40, 0x47, 0x52, 0x55, 0x5C, 0x5B,
  0x76, 0x71, 0x78, 0x7F, 0x6A, 0x6D, 0x64, 0x63,
  0x3E, 0x39, 0x30, 0x37, 0x22, 0x25, 0x2C, 0x2B,
  0x06, 0x01, 0x08, 0x0F, 0x1A, 0x1D, 0x14, 0x13,
  0xAE, 0xA9, 0xA0, 0xA7, 0xB2, 0xB5, 0xBC, 0xBB,
  0x96, 0x91, 0x98, 0x9F, 0x8A, 0x8D, 0x84, 0x83,
  0xDE, 0xD9, 0xD0, 0xD7, 0xC2, 0xC5, 0xCC, 0xCB,
  0xE6, 0xE1, 0xE8, 0xEF, 0xFA, 0xFD, 0xF4, 0xF3
};

static guint8
gst_flac_calculate_crc8 (const guint8 * data, guint length)
{
  guint8 crc = 0;

  while (length--) {
    crc = crc8_table[crc ^ *data];
    ++data;
  }

  return crc;
}

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-flac, framed = (boolean) true, "
        "channels = (int) [ 1, 8 ], " "rate = (int) [ 1, 655350 ]")
    );

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-flac, framed = (boolean) false")
    );

static void gst_flac_parse_finalize (GObject * object);

static gboolean gst_flac_parse_start (GstBaseParse * parse);
static gboolean gst_flac_parse_stop (GstBaseParse * parse);
static gboolean gst_flac_parse_check_valid_frame (GstBaseParse * parse,
    GstBuffer * buffer, guint * framesize, gint * skipsize);
static GstFlowReturn gst_flac_parse_parse_frame (GstBaseParse * parse,
    GstBuffer * buffer);
static gint gst_flac_parse_get_frame_overhead (GstBaseParse * parse,
    GstBuffer * buffer);

GST_BOILERPLATE (GstFlacParse, gst_flac_parse, GstBaseParse,
    GST_TYPE_BASE_PARSE);

static void
gst_flac_parse_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));

  gst_element_class_set_details_simple (element_class, "FLAC audio parser",
      "Codec/Parser/Audio",
      "Parses audio with the FLAC lossless audio codec",
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");

  GST_DEBUG_CATEGORY_INIT (flacparse_debug, "flacparse", 0,
      "Flac parser element");
}

static void
gst_flac_parse_class_init (GstFlacParseClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseParseClass *baseparse_class = GST_BASE_PARSE_CLASS (klass);

  gobject_class->finalize = gst_flac_parse_finalize;

  baseparse_class->start = GST_DEBUG_FUNCPTR (gst_flac_parse_start);
  baseparse_class->stop = GST_DEBUG_FUNCPTR (gst_flac_parse_stop);
  baseparse_class->check_valid_frame =
      GST_DEBUG_FUNCPTR (gst_flac_parse_check_valid_frame);
  baseparse_class->parse_frame = GST_DEBUG_FUNCPTR (gst_flac_parse_parse_frame);
  baseparse_class->get_frame_overhead =
      GST_DEBUG_FUNCPTR (gst_flac_parse_get_frame_overhead);
}

static void
gst_flac_parse_init (GstFlacParse * flacparse, GstFlacParseClass * klass)
{
}

static void
gst_flac_parse_finalize (GObject * object)
{
  GstFlacParse *flacparse = GST_FLAC_PARSE (object);

  if (flacparse->tags) {
    gst_tag_list_free (flacparse->tags);
    flacparse->tags = NULL;
  }


  g_list_foreach (flacparse->headers, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (flacparse->headers);
  flacparse->headers = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_flac_parse_start (GstBaseParse * parse)
{
  GstFlacParse *flacparse = GST_FLAC_PARSE (parse);

  flacparse->state = GST_FLAC_PARSE_STATE_INIT;
  flacparse->min_blocksize = 0;
  flacparse->max_blocksize = 0;
  flacparse->min_framesize = 0;
  flacparse->max_framesize = 0;

  flacparse->upstream_length = -1;

  flacparse->samplerate = 0;
  flacparse->channels = 0;
  flacparse->bps = 0;
  flacparse->total_samples = 0;

  flacparse->requested_frame_size = 0;
  flacparse->offset = GST_CLOCK_TIME_NONE;
  flacparse->blocking_strategy = 0;
  flacparse->block_size = 0;
  flacparse->sample_number = 0;

  /* "fLaC" marker */
  gst_base_parse_set_min_frame_size (GST_BASE_PARSE (flacparse), 4);

  return TRUE;
}

static gboolean
gst_flac_parse_stop (GstBaseParse * parse)
{
  GstFlacParse *flacparse = GST_FLAC_PARSE (parse);

  if (flacparse->tags) {
    gst_tag_list_free (flacparse->tags);
    flacparse->tags = NULL;
  }

  g_list_foreach (flacparse->headers, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (flacparse->headers);
  flacparse->headers = NULL;

  return TRUE;
}

static gint
gst_flac_parse_get_frame_size (GstFlacParse * flacparse, GstBuffer * buffer,
    guint * framesize_ret)
{
  GstBitReader reader = GST_BIT_READER_INIT_FROM_BUFFER (buffer);
  guint16 samplerate;
  guint8 tmp;
  gint i;
  guint8 channel_assignment = 0;
  guint8 actual_crc, expected_crc;

  /* Skip 14 bit sync code */
  if (!gst_bit_reader_skip (&reader, 14))
    goto need_more_data;

  /* Must be 0 */
  if (!gst_bit_reader_get_bits_uint8 (&reader, &tmp, 1))
    goto need_more_data;
  else if (tmp != 0)
    goto error;

  /* 0 == fixed block size, 1 == variable block size */
  if (!gst_bit_reader_get_bits_uint8 (&reader, &flacparse->blocking_strategy,
          1))
    goto need_more_data;

  /* block size index, calculation of the real blocksize below */
  if (!gst_bit_reader_get_bits_uint16 (&reader, &flacparse->block_size, 4))
    goto need_more_data;
  else if (flacparse->block_size == 0)
    goto error;

  /* sample rate index, calculation of the real samplerate below */
  if (!gst_bit_reader_get_bits_uint16 (&reader, &samplerate, 4))
    goto need_more_data;
  else if (samplerate == 0x0f)
    goto error;

  /* channel assignment */
  if (!gst_bit_reader_get_bits_uint8 (&reader, &tmp, 4)) {
    goto need_more_data;
  } else if (tmp < 8) {
    if (flacparse->channels && tmp + 1 != flacparse->channels)
      goto error;
    else
      flacparse->channels = tmp + 1;
  } else if (tmp <= 10) {
    if (flacparse->channels && 2 != flacparse->channels)
      goto error;
    else
      flacparse->channels = 2;
    if (tmp == 8)
      channel_assignment = 1;   /* left-side */
    else if (tmp == 9)
      channel_assignment = 2;   /* right-side */
    else
      channel_assignment = 3;   /* mid-side */
  } else if (tmp > 10) {
    goto error;
  }

  /* bits per sample */
  if (!gst_bit_reader_get_bits_uint8 (&reader, &tmp, 3)) {
    goto need_more_data;
  } else if (tmp == 0x03 || tmp == 0x07) {
    goto error;
  } else if (tmp == 0 && flacparse->bps == 0) {
    goto need_streaminfo;
  } else if (tmp == 0x01 && flacparse->bps != 8) {
    if (flacparse->bps && flacparse->bps != 8)
      goto error;
    else
      flacparse->bps = 8;
  } else if (tmp == 0x02 && flacparse->bps != 12) {
    if (flacparse->bps && flacparse->bps != 12)
      goto error;
    else
      flacparse->bps = 12;
  } else if (tmp == 0x04 && flacparse->bps != 16) {
    if (flacparse->bps && flacparse->bps != 16)
      goto error;
    else
      flacparse->bps = 16;
  } else if (tmp == 0x05 && flacparse->bps != 20) {
    if (flacparse->bps && flacparse->bps != 20)
      goto error;
    else
      flacparse->bps = 20;
  } else if (tmp == 0x06 && flacparse->bps != 24) {
    if (flacparse->bps && flacparse->bps != 24)
      goto error;
    else
      flacparse->bps = 24;
  }

  /* reserved, must be 0 */
  if (!gst_bit_reader_get_bits_uint8 (&reader, &tmp, 1))
    goto need_more_data;
  else if (tmp != 0)
    goto error;

  /* read "utf8" encoded sample/frame number */
  {
    guint len = 0;

    tmp = 1;
    while (tmp != 0) {
      if (!gst_bit_reader_get_bits_uint8 (&reader, &tmp, 1))
        goto need_more_data;
      else if (tmp == 1)
        len++;
    }
    if (len == 1)
      goto error;

    flacparse->sample_number = 0;
    if (len == 0) {
      if (!gst_bit_reader_get_bits_uint8 (&reader, &tmp, 7))
        goto need_more_data;
      flacparse->sample_number = tmp;
    } else if ((flacparse->blocking_strategy == 0 && len > 6) ||
        (flacparse->blocking_strategy == 1 && len > 7)) {
      goto error;
    } else {
      if (!gst_bit_reader_get_bits_uint8 (&reader, &tmp, 8 - len - 1))
        goto need_more_data;

      flacparse->sample_number = tmp;
      len -= 1;

      while (len > 0) {
        if (!gst_bit_reader_get_bits_uint8 (&reader, &tmp, 2))
          goto need_more_data;
        else if (tmp != 0x02)
          goto error;

        if (!gst_bit_reader_get_bits_uint8 (&reader, &tmp, 6))
          goto need_more_data;
        flacparse->sample_number <<= 6;
        flacparse->sample_number |= tmp;
        len--;
      }
    }
  }

  /* calculate real blocksize from the blocksize index */
  if (flacparse->block_size == 1)
    flacparse->block_size = 192;
  else if (flacparse->block_size <= 5)
    flacparse->block_size = 576 * (1 << (flacparse->block_size - 2));
  else if (flacparse->block_size <= 15)
    flacparse->block_size = 256 * (1 << (flacparse->block_size - 8));
  else if (flacparse->block_size == 6) {
    if (!gst_bit_reader_get_bits_uint16 (&reader, &flacparse->block_size, 8))
      goto need_more_data;
    flacparse->block_size++;
  } else if (flacparse->block_size == 7) {
    if (!gst_bit_reader_get_bits_uint16 (&reader, &flacparse->block_size, 16))
      goto need_more_data;
    flacparse->block_size++;
  }

  /* calculate the real samplerate from the samplerate index */
  if (samplerate == 0 && flacparse->samplerate == 0) {
    goto need_streaminfo;
  } else if (samplerate == 1) {
    if (flacparse->samplerate == 0)
      flacparse->samplerate = 88200;
    else if (flacparse->samplerate != 88200)
      goto error;
  } else if (samplerate == 2) {
    if (flacparse->samplerate == 0)
      flacparse->samplerate = 176400;
    else if (flacparse->samplerate != 176400)
      goto error;
  } else if (samplerate == 3) {
    if (flacparse->samplerate == 0)
      flacparse->samplerate = 192000;
    else if (flacparse->samplerate != 192000)
      goto error;
  } else if (samplerate == 4) {
    if (flacparse->samplerate == 0)
      flacparse->samplerate = 8000;
    else if (flacparse->samplerate != 8000)
      goto error;
  } else if (samplerate == 5) {
    if (flacparse->samplerate == 0)
      flacparse->samplerate = 16000;
    else if (flacparse->samplerate != 16000)
      goto error;
  } else if (samplerate == 6) {
    if (flacparse->samplerate == 0)
      flacparse->samplerate = 22050;
    else if (flacparse->samplerate != 22050)
      goto error;
  } else if (samplerate == 7) {
    if (flacparse->samplerate == 0)
      flacparse->samplerate = 24000;
    else if (flacparse->samplerate != 24000)
      goto error;
  } else if (samplerate == 8) {
    if (flacparse->samplerate == 0)
      flacparse->samplerate = 32000;
    else if (flacparse->samplerate != 32000)
      goto error;
  } else if (samplerate == 9) {
    if (flacparse->samplerate == 0)
      flacparse->samplerate = 44100;
    else if (flacparse->samplerate != 44100)
      goto error;
  } else if (samplerate == 10) {
    if (flacparse->samplerate == 0)
      flacparse->samplerate = 48000;
    else if (flacparse->samplerate != 48000)
      goto error;
  } else if (samplerate == 11) {
    if (flacparse->samplerate == 0)
      flacparse->samplerate = 96000;
    else if (flacparse->samplerate != 96000)
      goto error;
  } else if (samplerate == 12) {
    if (!gst_bit_reader_get_bits_uint16 (&reader, &samplerate, 8))
      goto need_more_data;
    samplerate *= 1000;
    if (flacparse->samplerate == 0)
      flacparse->samplerate = samplerate;
    else if (flacparse->samplerate != samplerate)
      goto error;
  } else if (samplerate == 13) {
    if (!gst_bit_reader_get_bits_uint16 (&reader, &samplerate, 16))
      goto need_more_data;
    if (flacparse->samplerate == 0)
      flacparse->samplerate = samplerate;
    else if (flacparse->samplerate != samplerate)
      goto error;
  } else if (samplerate == 14) {
    if (!gst_bit_reader_get_bits_uint16 (&reader, &samplerate, 16))
      goto need_more_data;
    samplerate *= 10;
    if (flacparse->samplerate == 0)
      flacparse->samplerate = samplerate;
    else if (flacparse->samplerate != samplerate)
      goto error;
  }

  /* check crc-8 for the header */
  if (!gst_bit_reader_get_bits_uint8 (&reader, &expected_crc, 8))
    goto need_more_data;

  actual_crc =
      gst_flac_calculate_crc8 (GST_BUFFER_DATA (buffer),
      (gst_bit_reader_get_pos (&reader) / 8) - 1);
  if (actual_crc != expected_crc)
    goto error;

  /* parse subframes, one subframe per channel */
  for (i = 0; i < flacparse->channels; i++) {
    guint8 sf_type;
    guint8 cur_bps;

    cur_bps = flacparse->bps;

    /* for mid/side, left/side, right/side the "difference" channel
     * needs and additional bit */
    if (i == 0 && channel_assignment == 2)
      cur_bps++;
    else if (i == 1 && (channel_assignment == 1 || channel_assignment == 3))
      cur_bps++;

    /* must be 0 */
    if (!gst_bit_reader_get_bits_uint8 (&reader, &tmp, 1))
      goto need_more_data;
    else if (tmp != 0)
      goto error;

    /* sub frame type */
    if (!gst_bit_reader_get_bits_uint8 (&reader, &sf_type, 6))
      goto need_more_data;
    else if (((sf_type & 0xfe) == 0x02) ||
        ((sf_type & 0xfc) == 0x04) ||
        ((sf_type & 0xf8) == 0x08 && (sf_type & 0x07) > 4) ||
        ((sf_type & 0xf0) == 0x10))
      goto error;

    /* wasted bits per sample, if 1 the value follows unary coded */
    if (!gst_bit_reader_get_bits_uint8 (&reader, &tmp, 1)) {
      goto need_more_data;
    } else if (tmp != 0) {
      guint wasted = 1;

      tmp = 0;
      while (tmp == 0) {
        if (!gst_bit_reader_get_bits_uint8 (&reader, &tmp, 1))
          goto need_more_data;
        else
          wasted++;
      }
      cur_bps -= wasted;
    }

    /* subframe type: constant */
    if (sf_type == 0x00) {
      if (!gst_bit_reader_skip (&reader, cur_bps))
        goto need_more_data;
      /* subframe type: verbatim */
    } else if (sf_type == 0x01) {
      if (!gst_bit_reader_skip (&reader, cur_bps * flacparse->block_size))
        goto need_more_data;
      /* subframe type: LPC or fixed */
    } else {
      guint8 residual_type;
      guint order = 0;
      guint16 partition_order;
      guint j;

      /* Skip warm-up samples for fixed subframe and calculate order */
      if ((sf_type & 0xf8) == 0x08) {
        order = sf_type & 0x07;

        g_assert (order <= 4);

        if (!gst_bit_reader_skip (&reader, cur_bps * order))
          goto need_more_data;
        /* Skip warm-up samples for LPC subframe, get parameters and calculate order */
      } else if ((sf_type & 0xe0) == 0x20) {
        guint8 prec;

        order = (sf_type & 0x1f) + 1;

        /* warm-up samples */
        if (!gst_bit_reader_skip (&reader, cur_bps * order))
          goto need_more_data;

        /* LPC coefficient precision */
        if (!gst_bit_reader_get_bits_uint8 (&reader, &prec, 4))
          goto need_more_data;
        else if (prec == 0x0f)
          goto error;
        prec++;

        /* LPC coefficient shift */
        if (!gst_bit_reader_skip (&reader, 5))
          goto need_more_data;

        /* LPC coefficients */
        if (!gst_bit_reader_skip (&reader, order * prec))
          goto need_more_data;
      } else {
        g_assert_not_reached ();
      }

      /* residual type: 0 == rice, 1 == rice2 */
      if (!gst_bit_reader_get_bits_uint8 (&reader, &residual_type, 2))
        goto need_more_data;

      if (residual_type & 0x02)
        goto error;

      /* partition order */
      if (!gst_bit_reader_get_bits_uint16 (&reader, &partition_order, 4))
        goto need_more_data;

      partition_order = 1 << partition_order;

      /* 2^partition_order partitions */
      for (j = 0; j < partition_order; j++) {
        guint samples;
        guint8 rice_parameter;

        /* calculate number of samples for the current partition */
        if (partition_order == 1) {
          samples = flacparse->block_size - order;
        } else if (j != 0) {
          samples = flacparse->block_size / partition_order;
        } else {
          samples = flacparse->block_size / partition_order - order;
        }

        /* rice parameter */
        if (!gst_bit_reader_get_bits_uint8 (&reader, &rice_parameter,
                (residual_type == 0) ? 4 : 5))
          goto need_more_data;

        /* if rice parameter has all bits set the samples follow unencoded with the number of bits
         * per sample in the following 5 bits */
        if ((residual_type == 0 && rice_parameter == 0x0f)
            || (residual_type == 1 && rice_parameter == 0x1f)) {
          if (!gst_bit_reader_get_bits_uint8 (&reader, &tmp, 5))
            goto need_more_data;
          if (!gst_bit_reader_skip (&reader, tmp * samples))
            goto need_more_data;
        } else {
          guint k;

          /* read the rice encoded samples */
          for (k = 0; k < samples; k++) {
            tmp = 0;
            while (tmp == 0)
              if (!gst_bit_reader_get_bits_uint8 (&reader, &tmp, 1))
                goto need_more_data;

            if (!gst_bit_reader_skip (&reader, rice_parameter))
              goto need_more_data;
          }
        }
      }
    }
  }

  /* zero padding to byte alignment */
  gst_bit_reader_skip_to_byte (&reader);

  /* Skip crc-16 for the complete frame */
  if (!gst_bit_reader_skip (&reader, 16))
    goto need_more_data;

  *framesize_ret = gst_bit_reader_get_pos (&reader) / 8;

  GST_DEBUG_OBJECT (flacparse, "Parsed frame at offset %" G_GUINT64_FORMAT ":\n"
      "Frame size: %u\n"
      "Block size: %u\n"
      "Sample/Frame number: %" G_GUINT64_FORMAT,
      flacparse->offset, *framesize_ret,
      flacparse->block_size, flacparse->sample_number);

  return 0;

need_more_data:
  {
    gint max;

    /* not enough, if that was all available, give up on frame */
    if (G_UNLIKELY (gst_base_parse_get_drain (GST_BASE_PARSE_CAST (flacparse))))
      goto eos;
    /* otherwise, ask for some more */
    max = flacparse->max_framesize;
    if (!max)
      max = 1 << 24;
    flacparse->requested_frame_size
        = MIN (GST_BUFFER_SIZE (buffer) + 4096, max);
    if (flacparse->requested_frame_size > GST_BUFFER_SIZE (buffer)) {
      GST_DEBUG_OBJECT (flacparse, "Requesting %u bytes",
          flacparse->requested_frame_size);
      return flacparse->requested_frame_size;
    } else {
      GST_DEBUG_OBJECT (flacparse, "Giving up on invalid frame (%d bytes)",
          GST_BUFFER_SIZE (buffer));
      return -1;
    }
  }

need_streaminfo:
  {
    GST_ERROR_OBJECT (flacparse, "Need STREAMINFO");
    return -2;
  }

eos:
  {
    GST_WARNING_OBJECT (flacparse, "EOS");
    return -1;
  }

error:
  {
    GST_WARNING_OBJECT (flacparse, "Invalid frame");
    return -1;
  }
}

static gboolean
gst_flac_parse_check_valid_frame (GstBaseParse * parse, GstBuffer * buffer,
    guint * framesize, gint * skipsize)
{
  GstFlacParse *flacparse = GST_FLAC_PARSE (parse);
  const guint8 *data = GST_BUFFER_DATA (buffer);

  if (G_UNLIKELY (GST_BUFFER_SIZE (buffer) < 4))
    return FALSE;

  if (flacparse->state == GST_FLAC_PARSE_STATE_INIT) {
    if (memcmp (GST_BUFFER_DATA (buffer), "fLaC", 4) == 0) {
      GST_DEBUG_OBJECT (flacparse, "fLaC marker found");
      *framesize = 4;
      return TRUE;
    } else if (data[0] == 0xff && (data[1] >> 2) == 0x3e) {
      GST_DEBUG_OBJECT (flacparse, "Found headerless FLAC");
      /* Minimal size of a frame header */
      gst_base_parse_set_min_frame_size (GST_BASE_PARSE (flacparse), 16);
      flacparse->requested_frame_size = 16;
      flacparse->state = GST_FLAC_PARSE_STATE_GENERATE_HEADERS;
      *skipsize = 0;
      return FALSE;
    } else {
      GST_DEBUG_OBJECT (flacparse, "fLaC marker not found");
      return FALSE;
    }
  } else if (flacparse->state == GST_FLAC_PARSE_STATE_HEADERS) {
    guint size = 4 + ((data[1] << 16) | (data[2] << 8) | (data[3]));

    GST_DEBUG_OBJECT (flacparse, "Found metadata block of size %u", size);
    *framesize = size;
    return TRUE;
  } else {
    if (data[0] == 0xff && (data[1] >> 2) == 0x3e) {
      gint ret = 0;

      flacparse->offset = GST_BUFFER_OFFSET (buffer);
      flacparse->blocking_strategy = 0;
      flacparse->block_size = 0;
      flacparse->sample_number = 0;

      GST_DEBUG_OBJECT (flacparse, "Found sync code");
      ret = gst_flac_parse_get_frame_size (flacparse, buffer, framesize);
      if (ret == 0) {
        ret = *framesize;
        /* if not in sync, also check for next frame header */
        if (!gst_base_parse_get_sync (parse) &&
            !gst_base_parse_get_drain (parse)) {
          GST_DEBUG_OBJECT (flacparse, "Resyncing; checking next sync code");
          if (GST_BUFFER_SIZE (buffer) >= ret + 2) {
            if (data[ret] == 0xff && (data[ret + 1] >> 2) == 0x3e) {
              GST_DEBUG_OBJECT (flacparse, "Found next sync code");
              return TRUE;
            } else {
              GST_DEBUG_OBJECT (flacparse,
                  "No next sync code, rejecting frame");
              return FALSE;
            }
          } else {
            /* request more data for next sync */
            GST_DEBUG_OBJECT (flacparse, "... but not enough data");
            ret += 2;
            gst_base_parse_set_min_frame_size (GST_BASE_PARSE (flacparse), ret);
            flacparse->requested_frame_size = ret;
            return FALSE;
          }
        }
        return TRUE;
      } else if (ret == -1) {
        return FALSE;
      } else if (ret == -2) {
        GST_ELEMENT_ERROR (flacparse, STREAM, FORMAT, (NULL),
            ("Need STREAMINFO for parsing"));
        return FALSE;
      } else if (ret > 0) {
        *skipsize = 0;
        gst_base_parse_set_min_frame_size (GST_BASE_PARSE (flacparse), ret);
        flacparse->requested_frame_size = ret;
        return FALSE;
      }
    } else {
      GstByteReader reader = GST_BYTE_READER_INIT_FROM_BUFFER (buffer);
      gint off;

      off = gst_byte_reader_masked_scan_uint32 (&reader, 0xfffc0000, 0xfff80000,
          0, GST_BUFFER_SIZE (buffer));

      if (off > 0) {
        GST_DEBUG_OBJECT (parse, "Possible sync at buffer offset %d", off);
        *skipsize = off;
        return FALSE;
      } else {
        GST_DEBUG_OBJECT (flacparse, "Sync code not found");
        *skipsize = GST_BUFFER_SIZE (buffer) - 3;
        return FALSE;
      }
    }
  }

  return FALSE;
}

static gboolean
gst_flac_parse_handle_streaminfo (GstFlacParse * flacparse, GstBuffer * buffer)
{
  GstBitReader reader = GST_BIT_READER_INIT_FROM_BUFFER (buffer);

  if (GST_BUFFER_SIZE (buffer) != 4 + 34) {
    GST_ERROR_OBJECT (flacparse, "Invalid metablock size for STREAMINFO: %u",
        GST_BUFFER_SIZE (buffer));
    return FALSE;
  }

  /* Skip metadata block header */
  gst_bit_reader_skip (&reader, 32);

  if (!gst_bit_reader_get_bits_uint16 (&reader, &flacparse->min_blocksize, 16))
    goto error;
  if (flacparse->min_blocksize < 16) {
    GST_ERROR_OBJECT (flacparse, "Invalid minimum block size: %u",
        flacparse->min_blocksize);
    return FALSE;
  }

  if (!gst_bit_reader_get_bits_uint16 (&reader, &flacparse->max_blocksize, 16))
    goto error;
  if (flacparse->max_blocksize < 16) {
    GST_ERROR_OBJECT (flacparse, "Invalid maximum block size: %u",
        flacparse->max_blocksize);
    return FALSE;
  }

  if (!gst_bit_reader_get_bits_uint32 (&reader, &flacparse->min_framesize, 24))
    goto error;
  if (!gst_bit_reader_get_bits_uint32 (&reader, &flacparse->max_framesize, 24))
    goto error;

  if (!gst_bit_reader_get_bits_uint32 (&reader, &flacparse->samplerate, 20))
    goto error;
  if (flacparse->samplerate == 0) {
    GST_ERROR_OBJECT (flacparse, "Invalid sample rate 0");
    return FALSE;
  }

  if (!gst_bit_reader_get_bits_uint8 (&reader, &flacparse->channels, 3))
    goto error;
  flacparse->channels++;
  if (flacparse->channels > 8) {
    GST_ERROR_OBJECT (flacparse, "Invalid number of channels %u",
        flacparse->channels);
    return FALSE;
  }

  if (!gst_bit_reader_get_bits_uint8 (&reader, &flacparse->bps, 5))
    goto error;
  flacparse->bps++;

  if (!gst_bit_reader_get_bits_uint64 (&reader, &flacparse->total_samples, 36))
    goto error;
  if (flacparse->total_samples)
    gst_base_parse_set_duration (GST_BASE_PARSE (flacparse), GST_FORMAT_TIME,
        GST_FRAMES_TO_CLOCK_TIME (flacparse->total_samples,
            flacparse->samplerate));

  GST_DEBUG_OBJECT (flacparse, "STREAMINFO:\n"
      "\tmin/max blocksize: %u/%u,\n"
      "\tmin/max framesize: %u/%u,\n"
      "\tsamplerate: %u,\n"
      "\tchannels: %u,\n"
      "\tbits per sample: %u,\n"
      "\ttotal samples: %" G_GUINT64_FORMAT,
      flacparse->min_blocksize, flacparse->max_blocksize,
      flacparse->min_framesize, flacparse->max_framesize,
      flacparse->samplerate,
      flacparse->channels, flacparse->bps, flacparse->total_samples);

  return TRUE;

error:
  GST_ERROR_OBJECT (flacparse, "Failed to read data");
  return FALSE;
}

static gboolean
gst_flac_parse_handle_vorbiscomment (GstFlacParse * flacparse,
    GstBuffer * buffer)
{
  flacparse->tags = gst_tag_list_from_vorbiscomment_buffer (buffer,
      GST_BUFFER_DATA (buffer), 4, NULL);

  if (flacparse->tags == NULL) {
    GST_ERROR_OBJECT (flacparse, "Invalid vorbiscomment block");
  } else if (gst_tag_list_is_empty (flacparse->tags)) {
    gst_tag_list_free (flacparse->tags);
    flacparse->tags = NULL;
  }

  return TRUE;
}

static gboolean
gst_flac_parse_handle_picture (GstFlacParse * flacparse, GstBuffer * buffer)
{
  GstByteReader reader = GST_BYTE_READER_INIT_FROM_BUFFER (buffer);
  const guint8 *data = GST_BUFFER_DATA (buffer);
  guint32 img_len = 0, img_type = 0;
  guint32 img_mimetype_len = 0, img_description_len = 0;

  if (!gst_byte_reader_get_uint32_be (&reader, &img_type))
    goto error;

  if (!gst_byte_reader_get_uint32_be (&reader, &img_mimetype_len))
    goto error;
  if (!gst_byte_reader_skip (&reader, img_mimetype_len))
    goto error;

  if (!gst_byte_reader_get_uint32_be (&reader, &img_description_len))
    goto error;
  if (!gst_byte_reader_skip (&reader, img_description_len))
    goto error;

  if (!gst_byte_reader_skip (&reader, 4 * 4))
    goto error;

  if (!gst_byte_reader_get_uint32_be (&reader, &img_len))
    goto error;

  if (!flacparse->tags)
    flacparse->tags = gst_tag_list_new ();

  gst_tag_list_add_id3_image (flacparse->tags,
      data + gst_byte_reader_get_pos (&reader), img_len, img_type);

  if (gst_tag_list_is_empty (flacparse->tags)) {
    gst_tag_list_free (flacparse->tags);
    flacparse->tags = NULL;
  }

  return TRUE;

error:
  GST_ERROR_OBJECT (flacparse, "Error reading data");
  return FALSE;
}

static void
_value_array_append_buffer (GValue * array_val, GstBuffer * buf)
{
  GValue value = { 0, };

  g_value_init (&value, GST_TYPE_BUFFER);
  /* copy buffer to avoid problems with circular refcounts */
  buf = gst_buffer_copy (buf);
  /* again, for good measure */
  GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_IN_CAPS);
  gst_value_set_buffer (&value, buf);
  gst_buffer_unref (buf);
  gst_value_array_append_value (array_val, &value);
  g_value_unset (&value);
}

static gboolean
gst_flac_parse_handle_headers (GstFlacParse * flacparse)
{
  GstBuffer *vorbiscomment = NULL;
  GstBuffer *streaminfo = NULL;
  GstBuffer *marker = NULL;
  GValue array = { 0, };
  GstCaps *caps;
  GList *l;

  caps = gst_caps_new_simple ("audio/x-flac",
      "channels", G_TYPE_INT, flacparse->channels,
      "rate", G_TYPE_INT, flacparse->samplerate, NULL);

  if (!flacparse->headers)
    goto push_headers;

  for (l = flacparse->headers; l; l = l->next) {
    GstBuffer *header = l->data;
    const guint8 *data = GST_BUFFER_DATA (header);
    guint size = GST_BUFFER_SIZE (header);

    GST_BUFFER_FLAG_SET (header, GST_BUFFER_FLAG_IN_CAPS);

    if (size == 4 && memcmp (data, "fLaC", 4) == 0) {
      marker = header;
    } else if (size > 1 && (data[0] & 0x7f) == 0) {
      streaminfo = header;
    } else if (size > 1 && (data[0] & 0x7f) == 4) {
      vorbiscomment = header;
    }
  }

  if (marker == NULL || streaminfo == NULL || vorbiscomment == NULL) {
    GST_WARNING_OBJECT (flacparse,
        "missing header %p %p %p, muxing into container "
        "formats may be broken", marker, streaminfo, vorbiscomment);
    goto push_headers;
  }

  g_value_init (&array, GST_TYPE_ARRAY);

  /* add marker including STREAMINFO header */
  {
    GstBuffer *buf;
    guint16 num;

    /* minus one for the marker that is merged with streaminfo here */
    num = g_list_length (flacparse->headers) - 1;

    buf = gst_buffer_new_and_alloc (13 + GST_BUFFER_SIZE (streaminfo));
    GST_BUFFER_DATA (buf)[0] = 0x7f;
    memcpy (GST_BUFFER_DATA (buf) + 1, "FLAC", 4);
    GST_BUFFER_DATA (buf)[5] = 0x01;    /* mapping version major */
    GST_BUFFER_DATA (buf)[6] = 0x00;    /* mapping version minor */
    GST_BUFFER_DATA (buf)[7] = (num & 0xFF00) >> 8;
    GST_BUFFER_DATA (buf)[8] = (num & 0x00FF) >> 0;
    memcpy (GST_BUFFER_DATA (buf) + 9, "fLaC", 4);
    memcpy (GST_BUFFER_DATA (buf) + 13, GST_BUFFER_DATA (streaminfo),
        GST_BUFFER_SIZE (streaminfo));
    _value_array_append_buffer (&array, buf);
    gst_buffer_unref (buf);
  }

  /* add VORBISCOMMENT header */
  _value_array_append_buffer (&array, vorbiscomment);

  /* add other headers, if there are any */
  for (l = flacparse->headers; l; l = l->next) {
    if (GST_BUFFER_CAST (l->data) != marker &&
        GST_BUFFER_CAST (l->data) != streaminfo &&
        GST_BUFFER_CAST (l->data) != vorbiscomment) {
      _value_array_append_buffer (&array, GST_BUFFER_CAST (l->data));
    }
  }

  gst_structure_set_value (gst_caps_get_structure (caps, 0),
      "streamheader", &array);
  g_value_unset (&array);

push_headers:

  gst_pad_set_caps (GST_BASE_PARSE_SRC_PAD (GST_BASE_PARSE (flacparse)), caps);
  gst_caps_unref (caps);

  /* push header buffers; update caps, so when we push the first buffer the
   * negotiated caps will change to caps that include the streamheader field */
  for (l = flacparse->headers; l != NULL; l = l->next) {
    GstBuffer *buf = GST_BUFFER (l->data);
    GstFlowReturn ret;

    l->data = NULL;
    buf = gst_buffer_make_metadata_writable (buf);
    gst_buffer_set_caps (buf,
        GST_PAD_CAPS (GST_BASE_PARSE_SRC_PAD (GST_BASE_PARSE (flacparse))));

    ret = gst_base_parse_push_buffer (GST_BASE_PARSE (flacparse), buf);
    if (ret != GST_FLOW_OK)
      return FALSE;
  }
  g_list_free (flacparse->headers);
  flacparse->headers = NULL;

  /* Push tags */
  if (flacparse->tags)
    gst_element_found_tags (GST_ELEMENT (flacparse),
        gst_tag_list_copy (flacparse->tags));

  return TRUE;
}

static gboolean
gst_flac_parse_generate_headers (GstFlacParse * flacparse)
{
  GstBuffer *marker, *streaminfo, *vorbiscomment;
  guint8 *data;

  marker = gst_buffer_new_and_alloc (4);
  memcpy (GST_BUFFER_DATA (marker), "fLaC", 4);
  GST_BUFFER_TIMESTAMP (marker) = GST_CLOCK_TIME_NONE;
  GST_BUFFER_DURATION (marker) = GST_CLOCK_TIME_NONE;
  GST_BUFFER_OFFSET (marker) = 0;
  GST_BUFFER_OFFSET_END (marker) = 0;
  flacparse->headers = g_list_append (flacparse->headers, marker);

  streaminfo = gst_buffer_new_and_alloc (4 + 34);
  data = GST_BUFFER_DATA (streaminfo);
  memset (data, 0, 4 + 34);

  /* metadata block header */
  data[0] = 0x00;               /* is_last = 0; type = 0; */
  data[1] = 0x00;               /* length = 34; */
  data[2] = 0x00;
  data[3] = 0x22;

  /* streaminfo */

  data[4] = (flacparse->block_size >> 8) & 0xff;        /* min blocksize = blocksize; */
  data[5] = (flacparse->block_size) & 0xff;
  data[6] = (flacparse->block_size >> 8) & 0xff;        /* max blocksize = blocksize; */
  data[7] = (flacparse->block_size) & 0xff;

  data[8] = 0x00;               /* min framesize = 0; */
  data[9] = 0x00;
  data[10] = 0x00;
  data[11] = 0x00;              /* max framesize = 0; */
  data[12] = 0x00;
  data[13] = 0x00;

  data[14] = (flacparse->samplerate >> 12) & 0xff;
  data[15] = (flacparse->samplerate >> 4) & 0xff;
  data[16] = (flacparse->samplerate >> 0) & 0xf0;

  data[16] |= (flacparse->channels - 1) << 1;

  data[16] |= ((flacparse->bps - 1) >> 4) & 0x01;
  data[17] = (((flacparse->bps - 1)) & 0x0f) << 4;

  {
    gint64 duration;
    GstFormat fmt = GST_FORMAT_TIME;

    if (gst_pad_query_peer_duration (GST_BASE_PARSE_SINK_PAD (GST_BASE_PARSE
                (flacparse)), &fmt, &duration) && fmt == GST_FORMAT_TIME) {
      duration = GST_CLOCK_TIME_TO_FRAMES (duration, flacparse->samplerate);

      data[17] |= (duration >> 32) & 0xff;
      data[18] |= (duration >> 24) & 0xff;
      data[19] |= (duration >> 16) & 0xff;
      data[20] |= (duration >> 8) & 0xff;
      data[21] |= (duration >> 0) & 0xff;
    }
  }
  /* MD5 = 0; */

  GST_BUFFER_TIMESTAMP (streaminfo) = GST_CLOCK_TIME_NONE;
  GST_BUFFER_DURATION (streaminfo) = GST_CLOCK_TIME_NONE;
  GST_BUFFER_OFFSET (streaminfo) = 0;
  GST_BUFFER_OFFSET_END (streaminfo) = 0;
  flacparse->headers = g_list_append (flacparse->headers, streaminfo);

  /* empty vorbiscomment */
  {
    GstTagList *taglist = gst_tag_list_new ();
    guchar header[4];
    guint size;

    header[0] = 0x84;           /* is_last = 1; type = 4; */

    vorbiscomment =
        gst_tag_list_to_vorbiscomment_buffer (taglist, header, sizeof (header),
        NULL);
    gst_tag_list_free (taglist);

    /* Get rid of framing bit */
    if (GST_BUFFER_DATA (vorbiscomment)[GST_BUFFER_SIZE (vorbiscomment) - 1] ==
        1) {
      GstBuffer *sub;

      sub =
          gst_buffer_create_sub (vorbiscomment, 0,
          GST_BUFFER_SIZE (vorbiscomment) - 1);
      gst_buffer_unref (vorbiscomment);
      vorbiscomment = sub;
    }

    size = GST_BUFFER_SIZE (vorbiscomment) - 4;
    GST_BUFFER_DATA (vorbiscomment)[1] = ((size & 0xFF0000) >> 16);
    GST_BUFFER_DATA (vorbiscomment)[2] = ((size & 0x00FF00) >> 8);
    GST_BUFFER_DATA (vorbiscomment)[3] = (size & 0x0000FF);

    GST_BUFFER_TIMESTAMP (vorbiscomment) = GST_CLOCK_TIME_NONE;
    GST_BUFFER_DURATION (vorbiscomment) = GST_CLOCK_TIME_NONE;
    GST_BUFFER_OFFSET (vorbiscomment) = 0;
    GST_BUFFER_OFFSET_END (vorbiscomment) = 0;
    flacparse->headers = g_list_append (flacparse->headers, vorbiscomment);
  }

  return TRUE;
}

static GstFlowReturn
gst_flac_parse_parse_frame (GstBaseParse * parse, GstBuffer * buffer)
{
  GstFlacParse *flacparse = GST_FLAC_PARSE (parse);
  const guint8 *data = GST_BUFFER_DATA (buffer);

  if (flacparse->state == GST_FLAC_PARSE_STATE_INIT) {
    GST_BUFFER_TIMESTAMP (buffer) = GST_CLOCK_TIME_NONE;
    GST_BUFFER_DURATION (buffer) = GST_CLOCK_TIME_NONE;
    GST_BUFFER_OFFSET (buffer) = 0;
    GST_BUFFER_OFFSET_END (buffer) = 0;

    /* 32 bits metadata block */
    gst_base_parse_set_min_frame_size (GST_BASE_PARSE (flacparse), 4);
    flacparse->state = GST_FLAC_PARSE_STATE_HEADERS;

    flacparse->headers =
        g_list_append (flacparse->headers, gst_buffer_ref (buffer));

    return GST_BASE_PARSE_FLOW_DROPPED;
  } else if (flacparse->state == GST_FLAC_PARSE_STATE_HEADERS) {
    gboolean is_last = ((data[0] & 0x80) == 0x80);
    guint type = (data[0] & 0x7F);

    if (type == 127) {
      GST_WARNING_OBJECT (flacparse, "Invalid metadata block type");
      return GST_BASE_PARSE_FLOW_DROPPED;
    }

    GST_DEBUG_OBJECT (flacparse, "Handling metadata block of type %u", type);

    switch (type) {
      case 0:                  /* STREAMINFO */
        if (!gst_flac_parse_handle_streaminfo (flacparse, buffer))
          return GST_FLOW_ERROR;
        break;
      case 3:                  /* SEEKTABLE */
        /* TODO: handle seektables */
        break;
      case 4:                  /* VORBIS_COMMENT */
        if (!gst_flac_parse_handle_vorbiscomment (flacparse, buffer))
          return GST_FLOW_ERROR;
        break;
      case 6:                  /* PICTURE */
        if (!gst_flac_parse_handle_picture (flacparse, buffer))
          return GST_FLOW_ERROR;
        break;
      case 1:                  /* PADDING */
      case 2:                  /* APPLICATION */
      case 5:                  /* CUESHEET */
      default:                 /* RESERVED */
        break;
    }

    GST_BUFFER_TIMESTAMP (buffer) = GST_CLOCK_TIME_NONE;
    GST_BUFFER_DURATION (buffer) = GST_CLOCK_TIME_NONE;
    GST_BUFFER_OFFSET (buffer) = 0;
    GST_BUFFER_OFFSET_END (buffer) = 0;

    if (is_last) {
      flacparse->headers =
          g_list_append (flacparse->headers, gst_buffer_ref (buffer));

      if (!gst_flac_parse_handle_headers (flacparse))
        return GST_FLOW_ERROR;

      /* Minimal size of a frame header */
      gst_base_parse_set_min_frame_size (GST_BASE_PARSE (flacparse), MAX (16,
              flacparse->min_framesize));
      flacparse->requested_frame_size = MAX (16, flacparse->min_framesize);
      flacparse->state = GST_FLAC_PARSE_STATE_DATA;

      /* DROPPED because we pushed all headers manually already */
      return GST_BASE_PARSE_FLOW_DROPPED;
    } else {
      flacparse->headers =
          g_list_append (flacparse->headers, gst_buffer_ref (buffer));
      return GST_BASE_PARSE_FLOW_DROPPED;
    }
  } else {
    if (flacparse->offset != GST_BUFFER_OFFSET (buffer)) {
      gint ret;
      guint framesize;

      flacparse->offset = GST_BUFFER_OFFSET (buffer);
      ret = gst_flac_parse_get_frame_size (flacparse, buffer, &framesize);
      if (ret != 0) {
        GST_ERROR_OBJECT (flacparse,
            "Baseclass didn't provide a complete frame");
        return GST_FLOW_ERROR;
      }
    }

    if (flacparse->block_size == 0) {
      GST_ERROR_OBJECT (flacparse, "Unparsed frame");
      return GST_FLOW_ERROR;
    }

    if (flacparse->state == GST_FLAC_PARSE_STATE_GENERATE_HEADERS) {
      if (flacparse->blocking_strategy == 1) {
        GST_WARNING_OBJECT (flacparse,
            "Generating headers for variable blocksize streams not supported");

        if (!gst_flac_parse_handle_headers (flacparse))
          return GST_FLOW_ERROR;
      } else {
        GST_DEBUG_OBJECT (flacparse, "Generating headers");

        if (!gst_flac_parse_generate_headers (flacparse))
          return GST_FLOW_ERROR;

        if (!gst_flac_parse_handle_headers (flacparse))
          return GST_FLOW_ERROR;
      }
      flacparse->state = GST_FLAC_PARSE_STATE_DATA;
    }

    /* also cater for oggmux metadata */
    if (flacparse->blocking_strategy == 0) {
      GST_BUFFER_TIMESTAMP (buffer) =
          gst_util_uint64_scale (flacparse->sample_number,
          flacparse->block_size * GST_SECOND, flacparse->samplerate);
      GST_BUFFER_OFFSET_END (buffer) =
          flacparse->sample_number * flacparse->block_size +
          flacparse->block_size;
    } else {
      GST_BUFFER_TIMESTAMP (buffer) =
          gst_util_uint64_scale (flacparse->sample_number, GST_SECOND,
          flacparse->samplerate);
      GST_BUFFER_OFFSET_END (buffer) =
          flacparse->sample_number + flacparse->block_size;
    }
    GST_BUFFER_DURATION (buffer) =
        GST_FRAMES_TO_CLOCK_TIME (flacparse->block_size, flacparse->samplerate);
    GST_BUFFER_OFFSET (buffer) =
        GST_BUFFER_TIMESTAMP (buffer) + GST_BUFFER_DURATION (buffer);

    /* Minimal size of a frame header */
    gst_base_parse_set_min_frame_size (GST_BASE_PARSE (flacparse), MAX (16,
            flacparse->min_framesize));
    flacparse->requested_frame_size = MAX (16, flacparse->min_framesize);

    flacparse->offset = -1;
    flacparse->blocking_strategy = 0;
    flacparse->block_size = 0;
    flacparse->sample_number = 0;
    return GST_FLOW_OK;
  }
}

static gint
gst_flac_parse_get_frame_overhead (GstBaseParse * parse, GstBuffer * buffer)
{
  GstFlacParse *flacparse = GST_FLAC_PARSE (parse);

  if (flacparse->state != GST_FLAC_PARSE_STATE_DATA)
    return -1;
  else
    /* To simplify, we just assume that it's a fixed size header and ignore
     * subframe headers. The first could lead us to being off by 88 bits and
     * the second even less, so the total inaccuracy is negligible. */
    return 7;
}
