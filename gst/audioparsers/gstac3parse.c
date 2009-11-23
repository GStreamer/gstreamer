/* GStreamer AC3 parser
 * Copyright (C) 2009 Tim-Philipp Müller <tim centricular net>
 * Copyright (C) 2009 Mark Nauwelaerts <mnauw users sf net>
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
 * SECTION:gstac3parse
 * @short_description: AC3 parser
 * @see_also: #GstAmrParse, #GstAACParse
 *
 * <refsect2>
 * <para>
 * This is an AC3 parser.
 * </para>
 * <title>Example launch line</title>
 * <para>
 * <programlisting>
 * gst-launch filesrc location=abc.ac3 ! ac3parse ! a52dec ! audioresample ! audioconvert ! autoaudiosink
 * </programlisting>
 * </para>
 * </refsect2>
 */

/* TODO:
 *  - add support for audio/x-private1-ac3 as well
 *  - should accept framed and unframed input (needs decodebin fixes first)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "gstac3parse.h"
#include <gst/base/gstbytereader.h>
#include <gst/base/gstbitreader.h>

GST_DEBUG_CATEGORY_STATIC (ac3_parse_debug);
#define GST_CAT_DEFAULT ac3_parse_debug

static const struct
{
  const guint bit_rate;         /* nominal bit rate */
  const guint frame_size[3];    /* frame size for 32kHz, 44kHz, and 48kHz */
} frmsizcod_table[38] = {
  {
    32, {
  64, 69, 96}}, {
    32, {
  64, 70, 96}}, {
    40, {
  80, 87, 120}}, {
    40, {
  80, 88, 120}}, {
    48, {
  96, 104, 144}}, {
    48, {
  96, 105, 144}}, {
    56, {
  112, 121, 168}}, {
    56, {
  112, 122, 168}}, {
    64, {
  128, 139, 192}}, {
    64, {
  128, 140, 192}}, {
    80, {
  160, 174, 240}}, {
    80, {
  160, 175, 240}}, {
    96, {
  192, 208, 288}}, {
    96, {
  192, 209, 288}}, {
    112, {
  224, 243, 336}}, {
    112, {
  224, 244, 336}}, {
    128, {
  256, 278, 384}}, {
    128, {
  256, 279, 384}}, {
    160, {
  320, 348, 480}}, {
    160, {
  320, 349, 480}}, {
    192, {
  384, 417, 576}}, {
    192, {
  384, 418, 576}}, {
    224, {
  448, 487, 672}}, {
    224, {
  448, 488, 672}}, {
    256, {
  512, 557, 768}}, {
    256, {
  512, 558, 768}}, {
    320, {
  640, 696, 960}}, {
    320, {
  640, 697, 960}}, {
    384, {
  768, 835, 1152}}, {
    384, {
  768, 836, 1152}}, {
    448, {
  896, 975, 1344}}, {
    448, {
  896, 976, 1344}}, {
    512, {
  1024, 1114, 1536}}, {
    512, {
  1024, 1115, 1536}}, {
    576, {
  1152, 1253, 1728}}, {
    576, {
  1152, 1254, 1728}}, {
    640, {
  1280, 1393, 1920}}, {
    640, {
  1280, 1394, 1920}}
};

static const guint fscod_rates[4] = { 48000, 44100, 32000, 0 };
static const guint acmod_chans[8] = { 2, 1, 2, 3, 3, 4, 4, 5 };
static const guint numblks[4] = { 1, 2, 3, 6 };

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-ac3, framed = (boolean) true, "
        " channels = (int) [ 1, 6 ], rate = (int) [ 32000, 48000 ]; "
        "audio/ac3, framed = (boolean) true, "
        " channels = (int) [ 1, 6 ], rate = (int) [ 32000, 48000 ] "));

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-ac3, framed = (boolean) false; "
        "audio/ac3, framed = (boolean) false "));

static void gst_ac3_parse_finalize (GObject * object);

static gboolean gst_ac3_parse_start (GstBaseParse * parse);
static gboolean gst_ac3_parse_stop (GstBaseParse * parse);
static gboolean gst_ac3_parse_is_seekable (GstBaseParse * parse);
static gboolean gst_ac3_parse_check_valid_frame (GstBaseParse * parse,
    GstBuffer * buffer, guint * size, gint * skipsize);
static GstFlowReturn gst_ac3_parse_parse_frame (GstBaseParse * parse,
    GstBuffer * buf);

GST_BOILERPLATE (GstAc3Parse, gst_ac3_parse, GstBaseParse, GST_TYPE_BASE_PARSE);

static void
gst_ac3_parse_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));

  gst_element_class_set_details_simple (element_class,
      "AC3 audio stream parser", "Codec/Parser/Audio",
      "AC3 parser", "Tim-Philipp Müller <tim centricular net>");
}

static void
gst_ac3_parse_class_init (GstAc3ParseClass * klass)
{
  GstBaseParseClass *parse_class = GST_BASE_PARSE_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (ac3_parse_debug, "ac3parse", 0,
      "AC3 audio stream parser");

  object_class->finalize = gst_ac3_parse_finalize;

  parse_class->start = GST_DEBUG_FUNCPTR (gst_ac3_parse_start);
  parse_class->stop = GST_DEBUG_FUNCPTR (gst_ac3_parse_stop);
  parse_class->check_valid_frame =
      GST_DEBUG_FUNCPTR (gst_ac3_parse_check_valid_frame);
  parse_class->parse_frame = GST_DEBUG_FUNCPTR (gst_ac3_parse_parse_frame);
  parse_class->is_seekable = GST_DEBUG_FUNCPTR (gst_ac3_parse_is_seekable);
}

static void
gst_ac3_parse_reset (GstAc3Parse * ac3parse)
{
  ac3parse->channels = -1;
  ac3parse->sample_rate = -1;
}

static void
gst_ac3_parse_init (GstAc3Parse * ac3parse, GstAc3ParseClass * klass)
{
  gst_base_parse_set_min_frame_size (GST_BASE_PARSE (ac3parse), 64 * 2);
  gst_ac3_parse_reset (ac3parse);
}

static void
gst_ac3_parse_finalize (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_ac3_parse_start (GstBaseParse * parse)
{
  GstAc3Parse *ac3parse = GST_AC3_PARSE (parse);

  GST_DEBUG_OBJECT (parse, "starting");

  gst_ac3_parse_reset (ac3parse);

  return TRUE;
}

static gboolean
gst_ac3_parse_stop (GstBaseParse * parse)
{
  GST_DEBUG_OBJECT (parse, "stopping");

  return TRUE;
}

static gboolean
gst_ac3_parse_is_seekable (GstBaseParse * parse)
{
  return TRUE;
}

static gboolean
gst_ac3_parse_frame_header_ac3 (GstAc3Parse * ac3parse, GstBuffer * buf,
    guint * frame_size, guint * rate, guint * chans, guint * blks, guint * sid)
{
  GstBitReader bits = GST_BIT_READER_INIT_FROM_BUFFER (buf);
  guint8 fscod, frmsizcod, bsid, bsmod, acmod, lfe_on;

  gst_bit_reader_skip (&bits, 16 + 16);
  gst_bit_reader_get_bits_uint8 (&bits, &fscod, 2);
  gst_bit_reader_get_bits_uint8 (&bits, &frmsizcod, 6);

  if (G_UNLIKELY (fscod == 3 || frmsizcod >= G_N_ELEMENTS (frmsizcod_table))) {
    GST_DEBUG_OBJECT (ac3parse, "bad fscod=%d frmsizcod=%d", fscod, frmsizcod);
    return FALSE;
  }

  gst_bit_reader_get_bits_uint8 (&bits, &bsid, 5);
  gst_bit_reader_get_bits_uint8 (&bits, &bsmod, 3);
  gst_bit_reader_get_bits_uint8 (&bits, &acmod, 3);

  /* FIXME: are other bsids ok as well? check spec */
  if (bsid != 8 && bsid != 6) {
    GST_DEBUG_OBJECT (ac3parse, "unexpected bsid=%d", bsid);
    return FALSE;
  }

  if ((acmod & 0x1) && (acmod != 0x1))  /* 3 front channels */
    gst_bit_reader_skip (&bits, 2);
  if ((acmod & 0x4))            /* if a surround channel exists */
    gst_bit_reader_skip (&bits, 2);
  if (acmod == 0x2)             /* if in 2/0 mode */
    gst_bit_reader_skip (&bits, 2);

  gst_bit_reader_get_bits_uint8 (&bits, &lfe_on, 1);

  if (frame_size)
    *frame_size = frmsizcod_table[frmsizcod].frame_size[fscod] * 2;
  if (rate)
    *rate = fscod_rates[fscod];
  if (chans)
    *chans = acmod_chans[acmod] + lfe_on;
  if (blks)
    *blks = 6;
  if (sid)
    *sid = 0;

  return TRUE;
}

static gboolean
gst_ac3_parse_frame_header_eac3 (GstAc3Parse * ac3parse, GstBuffer * buf,
    guint * frame_size, guint * rate, guint * chans, guint * blks, guint * sid)
{
  GstBitReader bits = GST_BIT_READER_INIT_FROM_BUFFER (buf);
  guint16 frmsiz, sample_rate, blocks;
  guint8 strmtyp, fscod, fscod2, acmod, lfe_on, strmid, numblkscod;

  gst_bit_reader_skip (&bits, 16 + 16);
  gst_bit_reader_get_bits_uint8 (&bits, &strmtyp, 2);   /* strmtyp     */
  if (G_UNLIKELY (strmtyp == 3)) {
    GST_DEBUG_OBJECT (ac3parse, "bad strmtyp %d", strmtyp);
    return FALSE;
  }

  gst_bit_reader_get_bits_uint8 (&bits, &strmid, 3);    /* substreamid */
  gst_bit_reader_get_bits_uint16 (&bits, &frmsiz, 11);  /* frmsiz      */
  gst_bit_reader_get_bits_uint8 (&bits, &fscod, 2);     /* fscod       */
  if (fscod == 3) {
    gst_bit_reader_get_bits_uint8 (&bits, &fscod2, 2);  /* fscod2      */
    if (G_UNLIKELY (fscod2 == 3)) {
      GST_DEBUG_OBJECT (ac3parse, "invalid fscod2");
      return FALSE;
    }
    sample_rate = fscod_rates[fscod2] / 2;
    blocks = 6;
  } else {
    gst_bit_reader_get_bits_uint8 (&bits, &numblkscod, 2);      /* numblkscod  */
    sample_rate = fscod_rates[fscod];
    blocks = numblks[numblkscod];
  }

  gst_bit_reader_get_bits_uint8 (&bits, &acmod, 3);     /* acmod       */
  gst_bit_reader_get_bits_uint8 (&bits, &lfe_on, 1);    /* lfeon       */

  gst_bit_reader_skip (&bits, 5);       /* bsid        */

  if (frame_size)
    *frame_size = (frmsiz + 1) * 2;
  if (rate)
    *rate = sample_rate;
  if (chans)
    *chans = acmod_chans[acmod] + lfe_on;
  if (blks)
    *blks = blocks;
  if (sid)
    *sid = (strmtyp & 0x1) << 3 | strmid;

  return TRUE;
}

static gboolean
gst_ac3_parse_frame_header (GstAc3Parse * parse, GstBuffer * buf,
    guint * framesize, guint * rate, guint * chans, guint * blocks, guint * sid)
{
  GstBitReader bits = GST_BIT_READER_INIT_FROM_BUFFER (buf);
  guint16 sync;
  guint8 bsid;

  GST_MEMDUMP_OBJECT (parse, "AC3 frame sync", GST_BUFFER_DATA (buf), 16);

  gst_bit_reader_get_bits_uint16 (&bits, &sync, 16);
  gst_bit_reader_skip (&bits, 16 + 8);
  gst_bit_reader_peek_bits_uint8 (&bits, &bsid, 5);

  if (G_UNLIKELY (sync != 0x0b77))
    return FALSE;

  if (bsid <= 10) {
    return gst_ac3_parse_frame_header_ac3 (parse, buf, framesize, rate, chans,
        blocks, sid);
  } else if (bsid <= 16) {
    return gst_ac3_parse_frame_header_eac3 (parse, buf, framesize, rate, chans,
        blocks, sid);
  } else {
    GST_DEBUG_OBJECT (parse, "unexpected bsid %d", bsid);
    return FALSE;
  }
}

static gboolean
gst_ac3_parse_check_valid_frame (GstBaseParse * parse, GstBuffer * buf,
    guint * framesize, gint * skipsize)
{
  GstByteReader reader = GST_BYTE_READER_INIT_FROM_BUFFER (buf);
  GstAc3Parse *ac3parse = GST_AC3_PARSE (parse);
  gint off;
  gboolean sync, drain;

  if (G_UNLIKELY (GST_BUFFER_SIZE (buf) < 6))
    return FALSE;

  off = gst_byte_reader_masked_scan_uint32 (&reader, 0xffff0000, 0x0b770000,
      0, GST_BUFFER_SIZE (buf));

  GST_LOG_OBJECT (parse, "possible sync at buffer offset %d", off);

  /* didn't find anything that looks like a sync word, skip */
  if (off < 0) {
    *skipsize = GST_BUFFER_SIZE (buf) - 2;
    return FALSE;
  }

  /* possible frame header, but not at offset 0? skip bytes before sync */
  if (off > 0) {
    *skipsize = off;
    return FALSE;
  }

  /* make sure the values in the frame header look sane */
  if (!gst_ac3_parse_frame_header (ac3parse, buf, framesize, NULL, NULL,
          NULL, NULL)) {
    *skipsize = off + 2;
    return FALSE;
  }

  GST_LOG_OBJECT (parse, "got frame");

  sync = gst_base_parse_get_sync (parse);
  drain = gst_base_parse_get_drain (parse);

  if (!sync && !drain) {
    guint16 word;

    GST_DEBUG_OBJECT (ac3parse, "resyncing; checking next frame syncword");

    if (!gst_byte_reader_skip (&reader, *framesize) ||
        !gst_byte_reader_get_uint16_be (&reader, &word)) {
      GST_DEBUG_OBJECT (ac3parse, "... but not sufficient data");
      gst_base_parse_set_min_frame_size (parse, *framesize + 6);
      *skipsize = 0;
      return FALSE;
    } else {
      if (word != 0x0b77) {
        GST_DEBUG_OBJECT (ac3parse, "0x%x not OK", word);
        *skipsize = off + 2;
        return FALSE;
      } else {
        /* ok, got sync now, let's assume constant frame size */
        gst_base_parse_set_min_frame_size (parse, *framesize);
      }
    }
  }

  return TRUE;
}

static GstFlowReturn
gst_ac3_parse_parse_frame (GstBaseParse * parse, GstBuffer * buf)
{
  GstAc3Parse *ac3parse = GST_AC3_PARSE (parse);
  guint fsize, rate, chans, blocks, sid;

  if (!gst_ac3_parse_frame_header (ac3parse, buf, &fsize, &rate, &chans,
          &blocks, &sid))
    goto broken_header;

  GST_LOG_OBJECT (parse, "size: %u, rate: %u, chans: %u", fsize, rate, chans);

  if (G_UNLIKELY (sid)) {
    GST_LOG_OBJECT (parse, "sid: %d", sid);
    GST_BUFFER_FLAG_SET (buf, GST_BASE_PARSE_BUFFER_FLAG_NO_FRAME);
  }

  if (G_UNLIKELY (ac3parse->sample_rate != rate || ac3parse->channels != chans)) {
    GstCaps *caps = gst_caps_new_simple ("audio/x-ac3",
        "framed", G_TYPE_BOOLEAN, TRUE, "rate", G_TYPE_INT, rate,
        "channels", G_TYPE_INT, chans, NULL);
    gst_buffer_set_caps (buf, caps);
    gst_pad_set_caps (GST_BASE_PARSE_SRC_PAD (parse), caps);
    gst_caps_unref (caps);

    ac3parse->sample_rate = rate;
    ac3parse->channels = chans;

    gst_base_parse_set_frame_props (parse, rate, 256 * blocks, 50);
  }

  return GST_FLOW_OK;

/* ERRORS */
broken_header:
  {
    /* this really shouldn't ever happen */
    GST_ELEMENT_ERROR (parse, STREAM, DECODE, (NULL), (NULL));
    return GST_FLOW_ERROR;
  }
}
