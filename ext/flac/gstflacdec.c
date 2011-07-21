/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2006> Tim-Philipp Müller <tim centricular net>
 * Copyright (C) <2006> Jan Schmidt <thaytan at mad scientist com>
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
 * SECTION:element-flacdec
 * @see_also: #GstFlacEnc
 *
 * flacdec decodes FLAC streams.
 * <ulink url="http://flac.sourceforge.net/">FLAC</ulink>
 * is a Free Lossless Audio Codec.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch filesrc location=media/small/dark.441-16-s.flac ! flacdec ! audioconvert ! audioresample ! autoaudiosink
 * ]|
 * |[
 * gst-launch gnomevfssrc location=http://gstreamer.freedesktop.org/media/small/dark.441-16-s.flac ! flacdec ! audioconvert ! audioresample ! queue min-threshold-buffers=10 ! autoaudiosink
 * ]|
 * </refsect2>
 */

/* TODO: add seeking when operating chain-based with unframed input */
/* FIXME: demote/remove granulepos handling and make more time-centric */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>

#include "gstflacdec.h"
#include <gst/gst-i18n-plugin.h>
#include <gst/gsttagsetter.h>
#include <gst/base/gsttypefindhelper.h>
#include <gst/audio/multichannel.h>
#include <gst/tag/tag.h>

/* Taken from http://flac.sourceforge.net/format.html#frame_header */
static const GstAudioChannelPosition channel_positions[8][8] = {
  {GST_AUDIO_CHANNEL_POSITION_FRONT_MONO},
  {GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
      GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT}, {
        GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
      GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER}, {
        GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
      GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT}, {
        GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
        GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
      GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT}, {
        GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
        GST_AUDIO_CHANNEL_POSITION_LFE,
        GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
      GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT},
  /* FIXME: 7/8 channel layouts are not defined in the FLAC specs */
  {
        GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
        GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
        GST_AUDIO_CHANNEL_POSITION_LFE,
      GST_AUDIO_CHANNEL_POSITION_REAR_CENTER}, {
        GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
        GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
        GST_AUDIO_CHANNEL_POSITION_LFE,
        GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT,
      GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT}
};

GST_DEBUG_CATEGORY_STATIC (flacdec_debug);
#define GST_CAT_DEFAULT flacdec_debug

static void gst_flac_dec_finalize (GObject * object);
static void gst_flac_dec_loop (GstPad * pad);

static GstStateChangeReturn gst_flac_dec_change_state (GstElement * element,
    GstStateChange transition);
static const GstQueryType *gst_flac_dec_get_src_query_types (GstPad * pad);
static const GstQueryType *gst_flac_dec_get_sink_query_types (GstPad * pad);
static gboolean gst_flac_dec_sink_query (GstPad * pad, GstQuery * query);
static gboolean gst_flac_dec_src_query (GstPad * pad, GstQuery * query);
static gboolean gst_flac_dec_convert_src (GstPad * pad, GstFormat src_format,
    gint64 src_value, GstFormat * dest_format, gint64 * dest_value);
static gboolean gst_flac_dec_src_event (GstPad * pad, GstEvent * event);
static gboolean gst_flac_dec_sink_activate (GstPad * sinkpad);
static gboolean gst_flac_dec_sink_activate_pull (GstPad * sinkpad,
    gboolean active);
static gboolean gst_flac_dec_sink_activate_push (GstPad * sinkpad,
    gboolean active);
static gboolean gst_flac_dec_sink_event (GstPad * pad, GstEvent * event);
static GstFlowReturn gst_flac_dec_chain (GstPad * pad, GstBuffer * buf);

static void gst_flac_dec_reset_decoders (GstFlacDec * flacdec);
static void gst_flac_dec_setup_decoder (GstFlacDec * flacdec);

static FLAC__StreamDecoderReadStatus
gst_flac_dec_read_seekable (const FLAC__StreamDecoder * decoder,
    FLAC__byte buffer[], size_t * bytes, void *client_data);
static FLAC__StreamDecoderReadStatus
gst_flac_dec_read_stream (const FLAC__StreamDecoder * decoder,
    FLAC__byte buffer[], size_t * bytes, void *client_data);
static FLAC__StreamDecoderSeekStatus
gst_flac_dec_seek (const FLAC__StreamDecoder * decoder,
    FLAC__uint64 position, void *client_data);
static FLAC__StreamDecoderTellStatus
gst_flac_dec_tell (const FLAC__StreamDecoder * decoder,
    FLAC__uint64 * position, void *client_data);
static FLAC__StreamDecoderLengthStatus
gst_flac_dec_length (const FLAC__StreamDecoder * decoder,
    FLAC__uint64 * length, void *client_data);
static FLAC__bool gst_flac_dec_eof (const FLAC__StreamDecoder * decoder,
    void *client_data);
static FLAC__StreamDecoderWriteStatus
gst_flac_dec_write_stream (const FLAC__StreamDecoder * decoder,
    const FLAC__Frame * frame,
    const FLAC__int32 * const buffer[], void *client_data);
static void gst_flac_dec_metadata_cb (const FLAC__StreamDecoder *
    decoder, const FLAC__StreamMetadata * metadata, void *client_data);
static void gst_flac_dec_error_cb (const FLAC__StreamDecoder *
    decoder, FLAC__StreamDecoderErrorStatus status, void *client_data);

GST_BOILERPLATE (GstFlacDec, gst_flac_dec, GstElement, GST_TYPE_ELEMENT);

/* FIXME 0.11: Use width=32 for all depths and let audioconvert
 * handle the conversions instead of doing it ourself.
 */
#define GST_FLAC_DEC_SRC_CAPS                             \
    "audio/x-raw-int, "                                   \
    "endianness = (int) BYTE_ORDER, "                     \
    "signed = (boolean) true, "                           \
    "width = (int) { 8, 16, 32 }, "                       \
    "depth = (int) [ 4, 32 ], "                           \
    "rate = (int) [ 1, 655350 ], "                        \
    "channels = (int) [ 1, 8 ]"

static GstStaticPadTemplate flac_dec_src_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_FLAC_DEC_SRC_CAPS));
static GstStaticPadTemplate flac_dec_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-flac")
    );

static void
gst_flac_dec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&flac_dec_src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&flac_dec_sink_factory));
  gst_element_class_set_details_simple (element_class, "FLAC audio decoder",
      "Codec/Decoder/Audio",
      "Decodes FLAC lossless audio streams", "Wim Taymans <wim@fluendo.com>");

  GST_DEBUG_CATEGORY_INIT (flacdec_debug, "flacdec", 0, "flac decoder");
}

static void
gst_flac_dec_class_init (GstFlacDecClass * klass)
{
  GstElementClass *gstelement_class;
  GObjectClass *gobject_class;

  gstelement_class = (GstElementClass *) klass;
  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = gst_flac_dec_finalize;

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_flac_dec_change_state);
}

static void
gst_flac_dec_init (GstFlacDec * flacdec, GstFlacDecClass * klass)
{
  flacdec->sinkpad =
      gst_pad_new_from_static_template (&flac_dec_sink_factory, "sink");
  gst_pad_set_activate_function (flacdec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_flac_dec_sink_activate));
  gst_pad_set_activatepull_function (flacdec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_flac_dec_sink_activate_pull));
  gst_pad_set_activatepush_function (flacdec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_flac_dec_sink_activate_push));
  gst_pad_set_query_type_function (flacdec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_flac_dec_get_sink_query_types));
  gst_pad_set_query_function (flacdec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_flac_dec_sink_query));
  gst_pad_set_event_function (flacdec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_flac_dec_sink_event));
  gst_pad_set_chain_function (flacdec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_flac_dec_chain));
  gst_element_add_pad (GST_ELEMENT (flacdec), flacdec->sinkpad);

  flacdec->srcpad =
      gst_pad_new_from_static_template (&flac_dec_src_factory, "src");
  gst_pad_set_query_type_function (flacdec->srcpad,
      GST_DEBUG_FUNCPTR (gst_flac_dec_get_src_query_types));
  gst_pad_set_query_function (flacdec->srcpad,
      GST_DEBUG_FUNCPTR (gst_flac_dec_src_query));
  gst_pad_set_event_function (flacdec->srcpad,
      GST_DEBUG_FUNCPTR (gst_flac_dec_src_event));
  gst_pad_use_fixed_caps (flacdec->srcpad);
  gst_element_add_pad (GST_ELEMENT (flacdec), flacdec->srcpad);

  gst_flac_dec_reset_decoders (flacdec);
}

static void
gst_flac_dec_reset_decoders (GstFlacDec * flacdec)
{
  /* Clean up the decoder */
  if (flacdec->decoder) {
    FLAC__stream_decoder_delete (flacdec->decoder);
    flacdec->decoder = NULL;
  }

  if (flacdec->adapter) {
    gst_adapter_clear (flacdec->adapter);
    g_object_unref (flacdec->adapter);
    flacdec->adapter = NULL;
  }

  if (flacdec->close_segment) {
    gst_event_unref (flacdec->close_segment);
    flacdec->close_segment = NULL;
  }
  if (flacdec->start_segment) {
    gst_event_unref (flacdec->start_segment);
    flacdec->start_segment = NULL;
  }
  if (flacdec->tags) {
    gst_tag_list_free (flacdec->tags);
    flacdec->tags = NULL;
  }
  if (flacdec->pending) {
    gst_buffer_unref (flacdec->pending);
    flacdec->pending = NULL;
  }

  flacdec->segment.last_stop = 0;
  flacdec->offset = 0;
  flacdec->init = TRUE;
}

static void
gst_flac_dec_setup_decoder (GstFlacDec * dec)
{
  gst_flac_dec_reset_decoders (dec);

  dec->tags = gst_tag_list_new ();
  gst_tag_list_add (dec->tags, GST_TAG_MERGE_REPLACE,
      GST_TAG_AUDIO_CODEC, "FLAC", NULL);

  dec->adapter = gst_adapter_new ();

  dec->decoder = FLAC__stream_decoder_new ();

  /* no point calculating since it's never checked here */
  FLAC__stream_decoder_set_md5_checking (dec->decoder, false);
  FLAC__stream_decoder_set_metadata_respond (dec->decoder,
      FLAC__METADATA_TYPE_VORBIS_COMMENT);
  FLAC__stream_decoder_set_metadata_respond (dec->decoder,
      FLAC__METADATA_TYPE_PICTURE);
}

static void
gst_flac_dec_finalize (GObject * object)
{
  GstFlacDec *flacdec;

  flacdec = GST_FLAC_DEC (object);

  gst_flac_dec_reset_decoders (flacdec);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}


static gboolean
gst_flac_dec_update_metadata (GstFlacDec * flacdec,
    const FLAC__StreamMetadata * metadata)
{
  GstTagList *list;
  guint num, i;

  if (flacdec->tags)
    list = flacdec->tags;
  else
    flacdec->tags = list = gst_tag_list_new ();

  num = metadata->data.vorbis_comment.num_comments;
  GST_DEBUG_OBJECT (flacdec, "%u tag(s) found", num);

  for (i = 0; i < num; ++i) {
    gchar *vc, *name, *value;

    vc = g_strndup ((gchar *) metadata->data.vorbis_comment.comments[i].entry,
        metadata->data.vorbis_comment.comments[i].length);

    if (gst_tag_parse_extended_comment (vc, &name, NULL, &value, TRUE)) {
      GST_DEBUG_OBJECT (flacdec, "%s : %s", name, value);
      if (value && strlen (value))
        gst_vorbis_tag_add (list, name, value);
      g_free (name);
      g_free (value);
    }

    g_free (vc);
  }

  return TRUE;
}

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
gst_flac_calculate_crc8 (guint8 * data, guint length)
{
  guint8 crc = 0;

  while (length--) {
    crc = crc8_table[crc ^ *data];
    ++data;
  }

  return crc;
}

static gboolean
gst_flac_dec_scan_got_frame (GstFlacDec * flacdec, guint8 * data, guint size,
    gint64 * last_sample_num)
{
  guint headerlen;
  guint sr_from_end = 0;        /* can be 0, 8 or 16 */
  guint bs_from_end = 0;        /* can be 0, 8 or 16 */
  guint32 val = 0;
  guint8 bs, sr, ca, ss, pb;

  if (size < 10)
    return FALSE;

  /* sync */
  if (data[0] != 0xFF || (data[1] & 0xFC) != 0xF8)
    return FALSE;
  if (data[1] & 1) {
    GST_WARNING_OBJECT (flacdec, "Variable block size FLAC unsupported");
    return FALSE;
  }

  bs = (data[2] & 0xF0) >> 4;   /* blocksize marker   */
  sr = (data[2] & 0x0F);        /* samplerate marker  */
  ca = (data[3] & 0xF0) >> 4;   /* channel assignment */
  ss = (data[3] & 0x0F) >> 1;   /* sample size marker */
  pb = (data[3] & 0x01);        /* padding bit        */

  GST_LOG_OBJECT (flacdec,
      "got sync, bs=%x,sr=%x,ca=%x,ss=%x,pb=%x", bs, sr, ca, ss, pb);

  if (bs == 0 || sr == 0x0F || ca >= 0x0B || ss == 0x03 || ss == 0x07) {
    return FALSE;
  }

  /* read block size from end of header? */
  if (bs == 6)
    bs_from_end = 8;
  else if (bs == 7)
    bs_from_end = 16;

  /* read sample rate from end of header? */
  if (sr == 0x0C)
    sr_from_end = 8;
  else if (sr == 0x0D || sr == 0x0E)
    sr_from_end = 16;

  /* FIXME: This is can be 36 bit if variable block size is used,
   * fortunately not encoder supports this yet and we check for that
   * above.
   */
  val = (guint32) g_utf8_get_char_validated ((gchar *) data + 4, -1);

  if (val == (guint32) - 1 || val == (guint32) - 2) {
    GST_LOG_OBJECT (flacdec, "failed to read sample/frame");
    return FALSE;
  }

  headerlen = 4 + g_unichar_to_utf8 ((gunichar) val, NULL) +
      (bs_from_end / 8) + (sr_from_end / 8);

  if (gst_flac_calculate_crc8 (data, headerlen) != data[headerlen]) {
    GST_LOG_OBJECT (flacdec, "invalid checksum");
    return FALSE;
  }

  if (flacdec->min_blocksize == flacdec->max_blocksize) {
    *last_sample_num = (val + 1) * flacdec->min_blocksize;
  } else {
    *last_sample_num = 0;       /* FIXME: + length of last block in samples */
  }

  /* FIXME: only valid for fixed block size streams */
  GST_DEBUG_OBJECT (flacdec, "frame number: %" G_GINT64_FORMAT,
      *last_sample_num);

  if (flacdec->sample_rate > 0 && *last_sample_num != 0) {
    GST_DEBUG_OBJECT (flacdec, "last sample %" G_GINT64_FORMAT " = %"
        GST_TIME_FORMAT, *last_sample_num,
        GST_TIME_ARGS (*last_sample_num * GST_SECOND / flacdec->sample_rate));
  }

  return TRUE;
}

#define SCANBLOCK_SIZE  (64*1024)

static void
gst_flac_dec_scan_for_last_block (GstFlacDec * flacdec, gint64 * samples)
{
  GstFormat format = GST_FORMAT_BYTES;
  gint64 file_size, offset;

  GST_INFO_OBJECT (flacdec, "total number of samples unknown, scanning file");

  if (!gst_pad_query_peer_duration (flacdec->sinkpad, &format, &file_size)) {
    GST_WARNING_OBJECT (flacdec, "failed to query upstream size!");
    return;
  }

  if (flacdec->min_blocksize != flacdec->max_blocksize) {
    GST_WARNING_OBJECT (flacdec, "scanning for last sample only works "
        "for FLAC files with constant blocksize");
    return;
  }

  GST_DEBUG_OBJECT (flacdec, "upstream size: %" G_GINT64_FORMAT, file_size);

  offset = file_size - 1;
  while (offset >= MAX (SCANBLOCK_SIZE / 2, file_size / 2)) {
    GstFlowReturn flow;
    GstBuffer *buf = NULL;
    guint8 *data;
    guint size;

    /* divide by 2 = not very sophisticated way to deal with overlapping */
    offset -= SCANBLOCK_SIZE / 2;
    GST_LOG_OBJECT (flacdec, "looking for frame at %" G_GINT64_FORMAT
        "-%" G_GINT64_FORMAT, offset, offset + SCANBLOCK_SIZE);

    flow = gst_pad_pull_range (flacdec->sinkpad, offset, SCANBLOCK_SIZE, &buf);
    if (flow != GST_FLOW_OK) {
      GST_DEBUG_OBJECT (flacdec, "flow = %s", gst_flow_get_name (flow));
      return;
    }

    size = GST_BUFFER_SIZE (buf);
    data = GST_BUFFER_DATA (buf);

    while (size > 16) {
      if (gst_flac_dec_scan_got_frame (flacdec, data, size, samples)) {
        GST_DEBUG_OBJECT (flacdec, "frame sync at offset %" G_GINT64_FORMAT,
            offset + GST_BUFFER_SIZE (buf) - size);
        gst_buffer_unref (buf);
        return;
      }
      ++data;
      --size;
    }

    gst_buffer_unref (buf);
  }
}

static void
gst_flac_extract_picture_buffer (GstFlacDec * dec,
    const FLAC__StreamMetadata * metadata)
{
  FLAC__StreamMetadata_Picture picture;
  GstTagList *tags;

  g_return_if_fail (metadata->type == FLAC__METADATA_TYPE_PICTURE);

  GST_LOG_OBJECT (dec, "Got PICTURE block");
  picture = metadata->data.picture;

  GST_DEBUG_OBJECT (dec, "declared MIME type is: '%s'",
      GST_STR_NULL (picture.mime_type));
  GST_DEBUG_OBJECT (dec, "image data is %u bytes", picture.data_length);

  tags = gst_tag_list_new ();

  gst_tag_list_add_id3_image (tags, (guint8 *) picture.data,
      picture.data_length, picture.type);

  if (!gst_tag_list_is_empty (tags)) {
    gst_element_found_tags_for_pad (GST_ELEMENT (dec), dec->srcpad, tags);
  } else {
    GST_DEBUG_OBJECT (dec, "problem parsing PICTURE block, skipping");
    gst_tag_list_free (tags);
  }
}

static void
gst_flac_dec_metadata_cb (const FLAC__StreamDecoder * decoder,
    const FLAC__StreamMetadata * metadata, void *client_data)
{
  GstFlacDec *flacdec = GST_FLAC_DEC (client_data);

  GST_LOG_OBJECT (flacdec, "metadata type: %d", metadata->type);

  switch (metadata->type) {
    case FLAC__METADATA_TYPE_STREAMINFO:{
      gint64 samples;
      guint depth;

      samples = metadata->data.stream_info.total_samples;

      flacdec->min_blocksize = metadata->data.stream_info.min_blocksize;
      flacdec->max_blocksize = metadata->data.stream_info.max_blocksize;
      flacdec->sample_rate = metadata->data.stream_info.sample_rate;
      flacdec->depth = depth = metadata->data.stream_info.bits_per_sample;
      flacdec->channels = metadata->data.stream_info.channels;

      if (depth < 9)
        flacdec->width = 8;
      else if (depth < 17)
        flacdec->width = 16;
      else
        flacdec->width = 32;

      GST_DEBUG_OBJECT (flacdec, "blocksize: min=%u, max=%u",
          flacdec->min_blocksize, flacdec->max_blocksize);
      GST_DEBUG_OBJECT (flacdec, "sample rate: %u, channels: %u",
          flacdec->sample_rate, flacdec->channels);
      GST_DEBUG_OBJECT (flacdec, "depth: %u, width: %u", flacdec->depth,
          flacdec->width);

      /* Only scan for last block in pull-mode, since it uses pull_range() */
      if (samples == 0 && !flacdec->streaming) {
        gst_flac_dec_scan_for_last_block (flacdec, &samples);
      }

      GST_DEBUG_OBJECT (flacdec, "total samples = %" G_GINT64_FORMAT, samples);

      /* in framed mode the demuxer/parser upstream has already pushed a
       * newsegment event in TIME format which we've passed on */
      if (samples > 0 && !flacdec->framed) {
        gint64 duration;

        gst_segment_set_duration (&flacdec->segment, GST_FORMAT_DEFAULT,
            samples);

        /* convert duration to time */
        duration = gst_util_uint64_scale_int (samples, GST_SECOND,
            flacdec->sample_rate);

        /* fixme, at this time we could seek to the queued seek event if we have
         * any */
        if (flacdec->start_segment)
          gst_event_unref (flacdec->start_segment);
        flacdec->start_segment =
            gst_event_new_new_segment_full (FALSE,
            flacdec->segment.rate, flacdec->segment.applied_rate,
            GST_FORMAT_TIME, 0, duration, 0);
      }
      break;
    }
    case FLAC__METADATA_TYPE_PICTURE:{
      gst_flac_extract_picture_buffer (flacdec, metadata);
      break;
    }
    case FLAC__METADATA_TYPE_VORBIS_COMMENT:
      gst_flac_dec_update_metadata (flacdec, metadata);
      break;
    default:
      break;
  }
}

static void
gst_flac_dec_error_cb (const FLAC__StreamDecoder * d,
    FLAC__StreamDecoderErrorStatus status, void *client_data)
{
  const gchar *error;
  GstFlacDec *dec;

  dec = GST_FLAC_DEC (client_data);

  switch (status) {
    case FLAC__STREAM_DECODER_ERROR_STATUS_LOST_SYNC:
      /* Ignore this error and keep processing */
      return;
    case FLAC__STREAM_DECODER_ERROR_STATUS_BAD_HEADER:
      error = "bad header";
      break;
    case FLAC__STREAM_DECODER_ERROR_STATUS_FRAME_CRC_MISMATCH:
      error = "CRC mismatch";
      break;
    default:
      error = "unknown error";
      break;
  }

  GST_ELEMENT_ERROR (dec, STREAM, DECODE, (NULL), ("%s (%d)", error, status));
  dec->last_flow = GST_FLOW_ERROR;
}

static FLAC__StreamDecoderSeekStatus
gst_flac_dec_seek (const FLAC__StreamDecoder * decoder,
    FLAC__uint64 position, void *client_data)
{
  GstFlacDec *flacdec;

  flacdec = GST_FLAC_DEC (client_data);

  GST_DEBUG_OBJECT (flacdec, "seek %" G_GUINT64_FORMAT, (guint64) position);
  flacdec->offset = position;

  return FLAC__STREAM_DECODER_SEEK_STATUS_OK;
}

static FLAC__StreamDecoderTellStatus
gst_flac_dec_tell (const FLAC__StreamDecoder * decoder,
    FLAC__uint64 * position, void *client_data)
{
  GstFlacDec *flacdec;

  flacdec = GST_FLAC_DEC (client_data);

  *position = flacdec->offset;

  GST_DEBUG_OBJECT (flacdec, "tell %" G_GINT64_FORMAT, (gint64) * position);

  return FLAC__STREAM_DECODER_TELL_STATUS_OK;
}

static FLAC__StreamDecoderLengthStatus
gst_flac_dec_length (const FLAC__StreamDecoder * decoder,
    FLAC__uint64 * length, void *client_data)
{
  GstFlacDec *flacdec;
  GstFormat fmt = GST_FORMAT_BYTES;
  gint64 len;
  GstPad *peer;

  flacdec = GST_FLAC_DEC (client_data);

  if (!(peer = gst_pad_get_peer (flacdec->sinkpad)))
    return FLAC__STREAM_DECODER_LENGTH_STATUS_ERROR;

  gst_pad_query_duration (peer, &fmt, &len);
  gst_object_unref (peer);
  if (fmt != GST_FORMAT_BYTES || len == -1)
    return FLAC__STREAM_DECODER_LENGTH_STATUS_ERROR;

  *length = len;

  GST_DEBUG_OBJECT (flacdec, "encoded byte length %" G_GINT64_FORMAT,
      (gint64) * length);

  return FLAC__STREAM_DECODER_LENGTH_STATUS_OK;
}

static FLAC__bool
gst_flac_dec_eof (const FLAC__StreamDecoder * decoder, void *client_data)
{
  GstFlacDec *flacdec;
  GstFormat fmt;
  GstPad *peer;
  gboolean ret = FALSE;
  gint64 len;

  flacdec = GST_FLAC_DEC (client_data);

  if (!(peer = gst_pad_get_peer (flacdec->sinkpad))) {
    GST_WARNING_OBJECT (flacdec, "no peer pad, returning EOF");
    return TRUE;
  }

  fmt = GST_FORMAT_BYTES;
  if (gst_pad_query_duration (peer, &fmt, &len) && fmt == GST_FORMAT_BYTES &&
      len != -1 && flacdec->offset >= len) {
    GST_DEBUG_OBJECT (flacdec,
        "offset=%" G_GINT64_FORMAT ", len=%" G_GINT64_FORMAT
        ", returning EOF", flacdec->offset, len);
    ret = TRUE;
  }

  gst_object_unref (peer);

  return ret;
}

static FLAC__StreamDecoderReadStatus
gst_flac_dec_read_seekable (const FLAC__StreamDecoder * decoder,
    FLAC__byte buffer[], size_t * bytes, void *client_data)
{
  GstFlowReturn flow;
  GstFlacDec *flacdec;
  GstBuffer *buf;

  flacdec = GST_FLAC_DEC (client_data);

  flow = gst_pad_pull_range (flacdec->sinkpad, flacdec->offset, *bytes, &buf);

  GST_PAD_STREAM_LOCK (flacdec->sinkpad);
  flacdec->pull_flow = flow;
  GST_PAD_STREAM_UNLOCK (flacdec->sinkpad);

  if (G_UNLIKELY (flow != GST_FLOW_OK)) {
    GST_INFO_OBJECT (flacdec, "pull_range flow: %s", gst_flow_get_name (flow));
    if (flow == GST_FLOW_UNEXPECTED)
      return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
    else
      return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
  }

  GST_DEBUG_OBJECT (flacdec, "Read %d bytes at %" G_GUINT64_FORMAT,
      GST_BUFFER_SIZE (buf), flacdec->offset);
  memcpy (buffer, GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));
  *bytes = GST_BUFFER_SIZE (buf);
  gst_buffer_unref (buf);
  flacdec->offset += *bytes;

  return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
}

static FLAC__StreamDecoderReadStatus
gst_flac_dec_read_stream (const FLAC__StreamDecoder * decoder,
    FLAC__byte buffer[], size_t * bytes, void *client_data)
{
  GstFlacDec *dec = GST_FLAC_DEC (client_data);
  guint len;

  len = MIN (gst_adapter_available (dec->adapter), *bytes);

  if (len == 0) {
    GST_LOG_OBJECT (dec, "0 bytes available at the moment");
    return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
  }

  GST_LOG_OBJECT (dec, "feeding %u bytes to decoder (available=%u, bytes=%u)",
      len, gst_adapter_available (dec->adapter), (guint) * bytes);
  gst_adapter_copy (dec->adapter, buffer, 0, len);
  *bytes = len;

  gst_adapter_flush (dec->adapter, len);

  return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
}

static FLAC__StreamDecoderWriteStatus
gst_flac_dec_write (GstFlacDec * flacdec, const FLAC__Frame * frame,
    const FLAC__int32 * const buffer[])
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *outbuf;
  guint depth = frame->header.bits_per_sample;
  guint width;
  guint sample_rate = frame->header.sample_rate;
  guint channels = frame->header.channels;
  guint samples = frame->header.blocksize;
  guint j, i;
  GstClockTime next;

  GST_LOG_OBJECT (flacdec, "samples in frame header: %d", samples);

  /* if a DEFAULT segment is configured, don't send samples past the end
   * of the segment */
  if (flacdec->segment.format == GST_FORMAT_DEFAULT &&
      flacdec->segment.stop != -1 &&
      flacdec->segment.last_stop >= 0 &&
      flacdec->segment.last_stop + samples > flacdec->segment.stop) {
    samples = flacdec->segment.stop - flacdec->segment.last_stop;
    GST_DEBUG_OBJECT (flacdec,
        "clipping last buffer to %d samples because of segment", samples);
  }

  switch (depth) {
    case 8:
      width = 8;
      break;
    case 12:
    case 16:
      width = 16;
      break;
    case 20:
    case 24:
    case 32:
      width = 32;
      break;
    case 0:
      if (flacdec->depth < 4 || flacdec->depth > 32) {
        GST_ERROR_OBJECT (flacdec, "unsupported depth %d from STREAMINFO",
            flacdec->depth);
        ret = GST_FLOW_ERROR;
        goto done;
      }

      depth = flacdec->depth;
      if (depth < 9)
        width = 8;
      else if (depth < 17)
        width = 16;
      else
        width = 32;

      break;
    default:
      GST_ERROR_OBJECT (flacdec, "unsupported depth %d", depth);
      ret = GST_FLOW_ERROR;
      goto done;
  }

  if (sample_rate == 0) {
    if (flacdec->sample_rate != 0) {
      sample_rate = flacdec->sample_rate;
    } else {
      GST_ERROR_OBJECT (flacdec, "unknown sample rate");
      ret = GST_FLOW_ERROR;
      goto done;
    }
  }

  if (!GST_PAD_CAPS (flacdec->srcpad)) {
    GstCaps *caps;

    GST_DEBUG_OBJECT (flacdec, "Negotiating %d Hz @ %d channels",
        frame->header.sample_rate, channels);

    caps = gst_caps_new_simple ("audio/x-raw-int",
        "endianness", G_TYPE_INT, G_BYTE_ORDER,
        "signed", G_TYPE_BOOLEAN, TRUE,
        "width", G_TYPE_INT, width,
        "depth", G_TYPE_INT, depth,
        "rate", G_TYPE_INT, frame->header.sample_rate,
        "channels", G_TYPE_INT, channels, NULL);

    if (channels > 2) {
      GstStructure *s = gst_caps_get_structure (caps, 0);

      gst_audio_set_channel_positions (s, channel_positions[channels - 1]);
    }

    flacdec->depth = depth;
    flacdec->width = width;
    flacdec->channels = channels;
    flacdec->sample_rate = sample_rate;

    gst_pad_set_caps (flacdec->srcpad, caps);
    gst_caps_unref (caps);
  }

  if (flacdec->close_segment) {
    GST_DEBUG_OBJECT (flacdec, "pushing close segment");
    gst_pad_push_event (flacdec->srcpad, flacdec->close_segment);
    flacdec->close_segment = NULL;
  }
  if (flacdec->start_segment) {
    GST_DEBUG_OBJECT (flacdec, "pushing start segment");
    gst_pad_push_event (flacdec->srcpad, flacdec->start_segment);
    flacdec->start_segment = NULL;
  }

  if (flacdec->tags) {
    gst_element_found_tags_for_pad (GST_ELEMENT (flacdec), flacdec->srcpad,
        flacdec->tags);
    flacdec->tags = NULL;
  }

  if (flacdec->pending) {
    GST_DEBUG_OBJECT (flacdec,
        "pushing pending samples at offset %" G_GINT64_FORMAT " (%"
        GST_TIME_FORMAT " + %" GST_TIME_FORMAT ")",
        GST_BUFFER_OFFSET (flacdec->pending),
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (flacdec->pending)),
        GST_TIME_ARGS (GST_BUFFER_DURATION (flacdec->pending)));
    /* Pending buffer was always allocated from the seeking thread,
     * which means it wasn't gst_buffer_alloc'd. Do so now to let
     * downstream negotiation work on older basetransform */
    ret = gst_pad_alloc_buffer_and_set_caps (flacdec->srcpad,
        GST_BUFFER_OFFSET (flacdec->pending),
        GST_BUFFER_SIZE (flacdec->pending),
        GST_BUFFER_CAPS (flacdec->pending), &outbuf);
    if (ret == GST_FLOW_OK) {
      gst_pad_push (flacdec->srcpad, flacdec->pending);
      gst_buffer_unref (outbuf);
    }

    outbuf = flacdec->pending = NULL;
    flacdec->segment.last_stop += flacdec->pending_samples;
    flacdec->pending_samples = 0;
  }

  if (flacdec->seeking) {
    GST_DEBUG_OBJECT (flacdec, "a pad_alloc would block here, do normal alloc");
    outbuf = gst_buffer_new_and_alloc (samples * channels * (width / 8));
    gst_buffer_set_caps (outbuf, GST_PAD_CAPS (flacdec->srcpad));
    GST_BUFFER_OFFSET (outbuf) = flacdec->segment.last_stop;
  } else {
    GST_LOG_OBJECT (flacdec, "alloc_buffer_and_set_caps");
    ret = gst_pad_alloc_buffer_and_set_caps (flacdec->srcpad,
        flacdec->segment.last_stop, samples * channels * (width / 8),
        GST_PAD_CAPS (flacdec->srcpad), &outbuf);

    if (ret != GST_FLOW_OK) {
      GST_DEBUG_OBJECT (flacdec, "gst_pad_alloc_buffer() returned %s",
          gst_flow_get_name (ret));
      goto done;
    }
  }

  if (flacdec->cur_granulepos != GST_BUFFER_OFFSET_NONE) {
    /* this should be fine since it should be one flac frame per ogg packet */
    flacdec->segment.last_stop = flacdec->cur_granulepos - samples;
    GST_LOG_OBJECT (flacdec, "granulepos = %" G_GINT64_FORMAT ", samples = %u",
        flacdec->cur_granulepos, samples);
  }

  GST_BUFFER_TIMESTAMP (outbuf) =
      gst_util_uint64_scale_int (flacdec->segment.last_stop, GST_SECOND,
      frame->header.sample_rate);

  /* get next timestamp to calculate the duration */
  next = gst_util_uint64_scale_int (flacdec->segment.last_stop + samples,
      GST_SECOND, frame->header.sample_rate);

  GST_BUFFER_DURATION (outbuf) = next - GST_BUFFER_TIMESTAMP (outbuf);

  if (width == 8) {
    gint8 *outbuffer = (gint8 *) GST_BUFFER_DATA (outbuf);

    for (i = 0; i < samples; i++) {
      for (j = 0; j < channels; j++) {
        *outbuffer++ = (gint8) buffer[j][i];
      }
    }
  } else if (width == 16) {
    gint16 *outbuffer = (gint16 *) GST_BUFFER_DATA (outbuf);

    for (i = 0; i < samples; i++) {
      for (j = 0; j < channels; j++) {
        *outbuffer++ = (gint16) buffer[j][i];
      }
    }
  } else if (width == 32) {
    gint32 *outbuffer = (gint32 *) GST_BUFFER_DATA (outbuf);

    for (i = 0; i < samples; i++) {
      for (j = 0; j < channels; j++) {
        *outbuffer++ = (gint32) buffer[j][i];
      }
    }
  } else {
    g_assert_not_reached ();
  }

  if (!flacdec->seeking) {
    GST_DEBUG_OBJECT (flacdec, "pushing %d samples at offset %" G_GINT64_FORMAT
        " (%" GST_TIME_FORMAT " + %" GST_TIME_FORMAT ")",
        samples, GST_BUFFER_OFFSET (outbuf),
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)),
        GST_TIME_ARGS (GST_BUFFER_DURATION (outbuf)));

    if (flacdec->discont) {
      GST_DEBUG_OBJECT (flacdec, "marking discont");
      outbuf = gst_buffer_make_metadata_writable (outbuf);
      GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DISCONT);
      flacdec->discont = FALSE;
    }
    ret = gst_pad_push (flacdec->srcpad, outbuf);
    GST_DEBUG_OBJECT (flacdec, "returned %s", gst_flow_get_name (ret));
    flacdec->segment.last_stop += samples;
  } else {
    GST_DEBUG_OBJECT (flacdec,
        "not pushing %d samples at offset %" G_GINT64_FORMAT
        " (in seek)", samples, GST_BUFFER_OFFSET (outbuf));
    gst_buffer_replace (&flacdec->pending, outbuf);
    gst_buffer_unref (outbuf);
    flacdec->pending_samples = samples;
    ret = GST_FLOW_OK;
  }

  if (ret != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (flacdec, "gst_pad_push() returned %s",
        gst_flow_get_name (ret));
  }

done:


  /* we act on the flow return value later in the loop function, as we don't
   * want to mess up the internal decoder state by returning ABORT when the
   * error is in fact non-fatal (like a pad in flushing mode) and we want
   * to continue later. So just pretend everything's dandy and act later. */
  flacdec->last_flow = ret;

  return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

static FLAC__StreamDecoderWriteStatus
gst_flac_dec_write_stream (const FLAC__StreamDecoder * decoder,
    const FLAC__Frame * frame,
    const FLAC__int32 * const buffer[], void *client_data)
{
  return gst_flac_dec_write (GST_FLAC_DEC (client_data), frame, buffer);
}

static void
gst_flac_dec_loop (GstPad * sinkpad)
{
  GstFlacDec *flacdec;
  FLAC__StreamDecoderState s;
  FLAC__StreamDecoderInitStatus is;

  flacdec = GST_FLAC_DEC (GST_OBJECT_PARENT (sinkpad));

  GST_LOG_OBJECT (flacdec, "entering loop");

  if (flacdec->eos) {
    GST_DEBUG_OBJECT (flacdec, "Seeked after end of file");

    if (flacdec->close_segment) {
      GST_DEBUG_OBJECT (flacdec, "pushing close segment");
      gst_pad_push_event (flacdec->srcpad, flacdec->close_segment);
      flacdec->close_segment = NULL;
    }
    if (flacdec->start_segment) {
      GST_DEBUG_OBJECT (flacdec, "pushing start segment");
      gst_pad_push_event (flacdec->srcpad, flacdec->start_segment);
      flacdec->start_segment = NULL;
    }

    if (flacdec->tags) {
      gst_element_found_tags_for_pad (GST_ELEMENT (flacdec), flacdec->srcpad,
          flacdec->tags);
      flacdec->tags = NULL;
    }

    if ((flacdec->segment.flags & GST_SEEK_FLAG_SEGMENT) == 0) {
      goto eos_and_pause;
    } else {
      goto segment_done_and_pause;
    }
  }

  if (flacdec->init) {
    GST_DEBUG_OBJECT (flacdec, "initializing new decoder");
    is = FLAC__stream_decoder_init_stream (flacdec->decoder,
        gst_flac_dec_read_seekable, gst_flac_dec_seek, gst_flac_dec_tell,
        gst_flac_dec_length, gst_flac_dec_eof, gst_flac_dec_write_stream,
        gst_flac_dec_metadata_cb, gst_flac_dec_error_cb, flacdec);
    if (is != FLAC__STREAM_DECODER_INIT_STATUS_OK)
      goto analyze_state;

    /*    FLAC__seekable_decoder_process_metadata (flacdec->decoder); */
    flacdec->init = FALSE;
  }

  flacdec->cur_granulepos = GST_BUFFER_OFFSET_NONE;

  flacdec->last_flow = GST_FLOW_OK;

  GST_LOG_OBJECT (flacdec, "processing single");
  FLAC__stream_decoder_process_single (flacdec->decoder);

analyze_state:

  GST_LOG_OBJECT (flacdec, "done processing, checking encoder state");
  s = FLAC__stream_decoder_get_state (flacdec->decoder);
  switch (s) {
    case FLAC__STREAM_DECODER_SEARCH_FOR_METADATA:
    case FLAC__STREAM_DECODER_READ_METADATA:
    case FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC:
    case FLAC__STREAM_DECODER_READ_FRAME:
    {
      GST_DEBUG_OBJECT (flacdec, "everything ok");

      if (flacdec->last_flow < GST_FLOW_UNEXPECTED ||
          flacdec->last_flow == GST_FLOW_NOT_LINKED) {
        GST_ELEMENT_ERROR (flacdec, STREAM, FAILED,
            (_("Internal data stream error.")),
            ("stream stopped, reason %s",
                gst_flow_get_name (flacdec->last_flow)));
        goto eos_and_pause;
      } else if (flacdec->last_flow == GST_FLOW_UNEXPECTED) {
        goto eos_and_pause;
      } else if (flacdec->last_flow != GST_FLOW_OK) {
        goto pause;
      }

      /* check if we're at the end of a configured segment */
      if (flacdec->segment.stop != -1 &&
          flacdec->segment.last_stop > 0 &&
          flacdec->segment.last_stop >= flacdec->segment.stop) {
        GST_DEBUG_OBJECT (flacdec, "reached end of the configured segment");

        if ((flacdec->segment.flags & GST_SEEK_FLAG_SEGMENT) == 0) {
          goto eos_and_pause;
        } else {
          goto segment_done_and_pause;
        }

        g_assert_not_reached ();
      }

      return;
    }

    case FLAC__STREAM_DECODER_END_OF_STREAM:{
      GST_DEBUG_OBJECT (flacdec, "EOS");
      FLAC__stream_decoder_reset (flacdec->decoder);

      if ((flacdec->segment.flags & GST_SEEK_FLAG_SEGMENT) != 0) {
        if (flacdec->segment.duration > 0) {
          flacdec->segment.stop = flacdec->segment.duration;
        } else {
          flacdec->segment.stop = flacdec->segment.last_stop;
        }
        goto segment_done_and_pause;
      }

      goto eos_and_pause;
    }

      /* gst_flac_dec_read_seekable() returned ABORTED */
    case FLAC__STREAM_DECODER_ABORTED:
    {
      GST_INFO_OBJECT (flacdec, "read aborted: last pull_range flow = %s",
          gst_flow_get_name (flacdec->pull_flow));
      if (flacdec->pull_flow == GST_FLOW_WRONG_STATE) {
        /* it seems we need to flush the decoder here to reset the decoder
         * state after the abort for FLAC__stream_decoder_seek_absolute()
         * to work properly */
        GST_DEBUG_OBJECT (flacdec, "flushing decoder to reset decoder state");
        FLAC__stream_decoder_flush (flacdec->decoder);
        goto pause;
      }
      /* fall through */
    }
    case FLAC__STREAM_DECODER_OGG_ERROR:
    case FLAC__STREAM_DECODER_SEEK_ERROR:
    case FLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR:
    case FLAC__STREAM_DECODER_UNINITIALIZED:
    default:{
      /* fixme: this error sucks -- should try to figure out when/if an more
         specific error was already sent via the callback */
      GST_ELEMENT_ERROR (flacdec, STREAM, DECODE, (NULL),
          ("%s", FLAC__StreamDecoderStateString[s]));
      goto eos_and_pause;
    }
  }

  return;

segment_done_and_pause:
  {
    gint64 stop_time;

    stop_time = gst_util_uint64_scale_int (flacdec->segment.stop,
        GST_SECOND, flacdec->sample_rate);

    GST_DEBUG_OBJECT (flacdec, "posting SEGMENT_DONE message, stop time %"
        GST_TIME_FORMAT, GST_TIME_ARGS (stop_time));

    gst_element_post_message (GST_ELEMENT (flacdec),
        gst_message_new_segment_done (GST_OBJECT (flacdec),
            GST_FORMAT_TIME, stop_time));

    goto pause;
  }
eos_and_pause:
  {
    GST_DEBUG_OBJECT (flacdec, "sending EOS event");
    flacdec->running = FALSE;
    gst_pad_push_event (flacdec->srcpad, gst_event_new_eos ());
    /* fall through to pause */
  }
pause:
  {
    GST_DEBUG_OBJECT (flacdec, "pausing");
    gst_pad_pause_task (sinkpad);
    return;
  }
}

static gboolean
gst_flac_dec_sink_event (GstPad * pad, GstEvent * event)
{
  GstFlacDec *dec;
  gboolean res;

  dec = GST_FLAC_DEC (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:{
      if (dec->init == FALSE) {
        FLAC__stream_decoder_flush (dec->decoder);
        gst_adapter_clear (dec->adapter);
      }
      res = gst_pad_push_event (dec->srcpad, event);
      break;
    }
    case GST_EVENT_NEWSEGMENT:{
      GstFormat fmt;
      gboolean update;
      gdouble rate, applied_rate;
      gint64 cur, stop, time;

      gst_event_parse_new_segment_full (event, &update, &rate, &applied_rate,
          &fmt, &cur, &stop, &time);

      if (fmt == GST_FORMAT_TIME) {
        GstFormat dformat = GST_FORMAT_DEFAULT;

        GST_DEBUG_OBJECT (dec, "newsegment event in TIME format => framed");
        dec->framed = TRUE;
        res = gst_pad_push_event (dec->srcpad, event);

        /* this won't work for the first newsegment event though ... */
        if (gst_flac_dec_convert_src (dec->srcpad, GST_FORMAT_TIME, cur,
                &dformat, &cur) && cur != -1 &&
            gst_flac_dec_convert_src (dec->srcpad, GST_FORMAT_TIME, stop,
                &dformat, &stop) && stop != -1) {
          gst_segment_set_newsegment_full (&dec->segment, update, rate,
              applied_rate, dformat, cur, stop, time);
          GST_DEBUG_OBJECT (dec, "segment %" GST_SEGMENT_FORMAT, &dec->segment);
        } else {
          GST_WARNING_OBJECT (dec, "couldn't convert time => samples");
        }
      } else if (fmt == GST_FORMAT_BYTES || TRUE) {
        GST_DEBUG_OBJECT (dec, "newsegment event in %s format => not framed",
            gst_format_get_name (fmt));
        dec->framed = FALSE;

        /* prepare generic newsegment event, for some reason our metadata
         * callback where we usually set this up is not being called in
         * push mode */
        if (dec->start_segment)
          gst_event_unref (dec->start_segment);
        dec->start_segment = gst_event_new_new_segment (FALSE, 1.0,
            GST_FORMAT_TIME, 0, -1, 0);

        gst_event_unref (event);
        res = TRUE;
      }
      break;
    }
    case GST_EVENT_EOS:{
      GST_LOG_OBJECT (dec, "EOS, with %u bytes available in adapter",
          gst_adapter_available (dec->adapter));
      if (dec->init == FALSE) {
        if (gst_adapter_available (dec->adapter) > 0) {
          FLAC__stream_decoder_process_until_end_of_stream (dec->decoder);
        }
        FLAC__stream_decoder_flush (dec->decoder);
      }
      gst_adapter_clear (dec->adapter);
      res = gst_pad_push_event (dec->srcpad, event);
      break;
    }
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }

  gst_object_unref (dec);

  return res;
}

static gboolean
gst_flac_dec_chain_parse_headers (GstFlacDec * dec)
{
  guint8 marker[4];
  guint avail, off;

  avail = gst_adapter_available (dec->adapter);
  if (avail < 4)
    return FALSE;

  gst_adapter_copy (dec->adapter, marker, 0, 4);
  if (strncmp ((const gchar *) marker, "fLaC", 4) != 0) {
    GST_ERROR_OBJECT (dec, "Unexpected header, expected fLaC header");
    return TRUE;                /* abort header parsing */
  }

  GST_DEBUG_OBJECT (dec, "fLaC header          : len           4 @ %7u", 0);

  off = 4;
  while (avail > (off + 1 + 3)) {
    gboolean is_last;
    guint8 mb_hdr[4];
    guint len, block_type;

    gst_adapter_copy (dec->adapter, mb_hdr, off, 4);

    is_last = ((mb_hdr[0] & 0x80) == 0x80);
    block_type = mb_hdr[0] & 0x7f;
    len = GST_READ_UINT24_BE (mb_hdr + 1);
    GST_DEBUG_OBJECT (dec, "Metadata block type %u: len %7u + 4 @ %7u%s",
        block_type, len, off, (is_last) ? " (last)" : "");
    off += 4 + len;

    if (is_last)
      break;

    if (off >= avail) {
      GST_LOG_OBJECT (dec, "Need more data: next offset %u > avail %u", off,
          avail);
      return FALSE;
    }
  }

  /* want metadata blocks plus at least one frame */
  return (off + FLAC__MAX_BLOCK_SIZE >= avail);
}

static GstFlowReturn
gst_flac_dec_chain (GstPad * pad, GstBuffer * buf)
{
  FLAC__StreamDecoderInitStatus s;
  GstFlacDec *dec;
  gboolean got_audio_frame;

  dec = GST_FLAC_DEC (GST_PAD_PARENT (pad));

  GST_LOG_OBJECT (dec,
      "buffer with ts=%" GST_TIME_FORMAT ", offset=%" G_GINT64_FORMAT
      ", end_offset=%" G_GINT64_FORMAT ", size=%u",
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)), GST_BUFFER_OFFSET (buf),
      GST_BUFFER_OFFSET_END (buf), GST_BUFFER_SIZE (buf));

  if (dec->init) {
    GST_DEBUG_OBJECT (dec, "initializing decoder");
    s = FLAC__stream_decoder_init_stream (dec->decoder,
        gst_flac_dec_read_stream, NULL, NULL, NULL, NULL,
        gst_flac_dec_write_stream, gst_flac_dec_metadata_cb,
        gst_flac_dec_error_cb, dec);
    if (s != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
      GST_ELEMENT_ERROR (GST_ELEMENT (dec), LIBRARY, INIT, (NULL), (NULL));
      return GST_FLOW_ERROR;
    }
    GST_DEBUG_OBJECT (dec, "initialized (framed=%d)", dec->framed);
    dec->init = FALSE;
  } else if (GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DISCONT)) {
    /* Clear the adapter and the decoder */
    gst_adapter_clear (dec->adapter);
    FLAC__stream_decoder_flush (dec->decoder);
  }

  if (dec->framed) {
    gint64 unused;

    /* check if this is a flac audio frame (rather than a header or junk) */
    got_audio_frame = gst_flac_dec_scan_got_frame (dec, GST_BUFFER_DATA (buf),
        GST_BUFFER_SIZE (buf), &unused);

    /* oggdemux will set granulepos in OFFSET_END instead of timestamp */
    if (G_LIKELY (got_audio_frame)) {
      /* old oggdemux for now */
      if (!GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
        dec->cur_granulepos = GST_BUFFER_OFFSET_END (buf);
      } else {
        GstFormat dformat = GST_FORMAT_DEFAULT;

        /* upstream (e.g. demuxer) presents us time,
         * convert to default samples */
        gst_flac_dec_convert_src (dec->srcpad, GST_FORMAT_TIME,
            GST_BUFFER_TIMESTAMP (buf), &dformat, &dec->segment.last_stop);
        dec->cur_granulepos = GST_BUFFER_OFFSET_NONE;
      }
    }
  } else {
    dec->cur_granulepos = GST_BUFFER_OFFSET_NONE;
    got_audio_frame = TRUE;
  }

  gst_adapter_push (dec->adapter, buf);
  buf = NULL;

  dec->last_flow = GST_FLOW_OK;

  if (!dec->framed) {
    if (G_UNLIKELY (!dec->got_headers)) {
      if (!gst_flac_dec_chain_parse_headers (dec)) {
        GST_LOG_OBJECT (dec, "don't have metadata blocks yet, need more data");
        goto out;
      }
      GST_INFO_OBJECT (dec, "have all metadata blocks now");
      dec->got_headers = TRUE;
    }

    /* wait until we have at least 64kB because libflac's StreamDecoder
     * interface is a bit dumb it seems (if we don't have as much data as
     * it wants it will call our read callback repeatedly and the only
     * way to stop that is to error out or EOS, which will affect the
     * decoder state). And the decoder seems to always ask for MAX_BLOCK_SIZE
     * bytes rather than the max. block size from the header). Requiring
     * MAX_BLOCK_SIZE bytes here should make sure it always gets enough data
     * to decode at least one block */
    while (gst_adapter_available (dec->adapter) >= FLAC__MAX_BLOCK_SIZE &&
        dec->last_flow == GST_FLOW_OK) {
      GST_LOG_OBJECT (dec, "%u bytes available",
          gst_adapter_available (dec->adapter));
      if (!FLAC__stream_decoder_process_single (dec->decoder)) {
        GST_DEBUG_OBJECT (dec, "process_single failed");
        break;
      }

      if (FLAC__stream_decoder_get_state (dec->decoder) ==
          FLAC__STREAM_DECODER_ABORTED) {
        GST_WARNING_OBJECT (dec, "Read callback caused internal abort");
        dec->last_flow = GST_FLOW_ERROR;
        break;
      }
    }
  } else if (dec->framed && got_audio_frame) {
    /* framed - there should always be enough data to decode something */
    GST_LOG_OBJECT (dec, "%u bytes available",
        gst_adapter_available (dec->adapter));
    if (G_UNLIKELY (!dec->got_headers)) {
      /* The first time we get audio data, we know we got all the headers.
       * We then loop until all the metadata is processed, then do an extra
       * "process_single" step for the audio frame. */
      GST_DEBUG_OBJECT (dec,
          "First audio frame, ensuring all metadata is processed");
      if (!FLAC__stream_decoder_process_until_end_of_metadata (dec->decoder)) {
        GST_DEBUG_OBJECT (dec, "process_until_end_of_metadata failed");
      }
      GST_DEBUG_OBJECT (dec,
          "All metadata is now processed, reading to process audio data");
      dec->got_headers = TRUE;
    }
    if (!FLAC__stream_decoder_process_single (dec->decoder)) {
      GST_DEBUG_OBJECT (dec, "process_single failed");
    }
  } else {
    GST_DEBUG_OBJECT (dec, "don't have all headers yet");
  }

out:

  return dec->last_flow;
}

static gboolean
gst_flac_dec_convert_sink (GstFlacDec * dec, GstFormat src_format,
    gint64 src_value, GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = TRUE;

  if (dec->width == 0 || dec->channels == 0 || dec->sample_rate == 0) {
    /* no frame decoded yet */
    GST_DEBUG_OBJECT (dec, "cannot convert: not set up yet");
    return FALSE;
  }

  switch (src_format) {
    case GST_FORMAT_BYTES:{
      res = FALSE;
      break;
    }
    case GST_FORMAT_DEFAULT:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          res = FALSE;
          break;
        case GST_FORMAT_TIME:
          /* granulepos = sample */
          *dest_value = gst_util_uint64_scale_int (src_value, GST_SECOND,
              dec->sample_rate);
          break;
        default:
          res = FALSE;
          break;
      }
      break;
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          res = FALSE;
          break;
        case GST_FORMAT_DEFAULT:
          *dest_value = gst_util_uint64_scale_int (src_value,
              dec->sample_rate, GST_SECOND);
          break;
        default:
          res = FALSE;
          break;
      }
      break;
    default:
      res = FALSE;
      break;
  }
  return res;
}

static const GstQueryType *
gst_flac_dec_get_sink_query_types (GstPad * pad)
{
  static const GstQueryType types[] = {
    GST_QUERY_CONVERT,
    0,
  };

  return types;
}

static gboolean
gst_flac_dec_sink_query (GstPad * pad, GstQuery * query)
{
  GstFlacDec *dec;
  gboolean res = FALSE;

  dec = GST_FLAC_DEC (gst_pad_get_parent (pad));

  GST_LOG_OBJECT (dec, "%s query", GST_QUERY_TYPE_NAME (query));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONVERT:{
      GstFormat src_fmt, dest_fmt;

      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, NULL);

      res = gst_flac_dec_convert_sink (dec, src_fmt, src_val, &dest_fmt,
          &dest_val);

      if (res) {
        gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      }
      GST_LOG_OBJECT (dec, "conversion %s", (res) ? "ok" : "FAILED");
      break;
    }

    default:{
      res = gst_pad_query_default (pad, query);
      break;
    }
  }

  gst_object_unref (dec);
  return res;
}

static gboolean
gst_flac_dec_convert_src (GstPad * pad, GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  GstFlacDec *flacdec = GST_FLAC_DEC (GST_PAD_PARENT (pad));
  gboolean res = TRUE;
  guint bytes_per_sample;
  guint scale = 1;

  if (flacdec->width == 0 || flacdec->channels == 0 ||
      flacdec->sample_rate == 0) {
    /* no frame decoded yet */
    GST_DEBUG_OBJECT (flacdec, "cannot convert: not set up yet");
    return FALSE;
  }

  bytes_per_sample = flacdec->channels * (flacdec->width / 8);

  switch (src_format) {
    case GST_FORMAT_BYTES:{
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          *dest_value =
              gst_util_uint64_scale_int (src_value, 1, bytes_per_sample);
          break;
        case GST_FORMAT_TIME:
        {
          gint byterate = bytes_per_sample * flacdec->sample_rate;

          *dest_value = gst_util_uint64_scale_int (src_value, GST_SECOND,
              byterate);
          break;
        }
        default:
          res = FALSE;
      }
      break;
    }
    case GST_FORMAT_DEFAULT:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          *dest_value = src_value * bytes_per_sample;
          break;
        case GST_FORMAT_TIME:
          *dest_value = gst_util_uint64_scale_int (src_value, GST_SECOND,
              flacdec->sample_rate);
          break;
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          scale = bytes_per_sample;
        case GST_FORMAT_DEFAULT:
          *dest_value = gst_util_uint64_scale_int_round (src_value,
              scale * flacdec->sample_rate, GST_SECOND);
          break;
        default:
          res = FALSE;
      }
      break;
    default:
      res = FALSE;
  }
  return res;
}

static const GstQueryType *
gst_flac_dec_get_src_query_types (GstPad * pad)
{
  static const GstQueryType types[] = {
    GST_QUERY_POSITION,
    GST_QUERY_DURATION,
    GST_QUERY_CONVERT,
    GST_QUERY_SEEKING,
    0,
  };

  return types;
}

static gboolean
gst_flac_dec_src_query (GstPad * pad, GstQuery * query)
{
  GstFlacDec *flacdec;
  gboolean res = TRUE;
  GstPad *peer;

  flacdec = GST_FLAC_DEC (gst_pad_get_parent (pad));
  peer = gst_pad_get_peer (flacdec->sinkpad);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:{
      GstFormat fmt;
      gint64 pos;

      gst_query_parse_position (query, &fmt, NULL);

      /* there might be a demuxer in front of us who can handle this */
      if (fmt == GST_FORMAT_TIME && (res = gst_pad_query (peer, query)))
        break;

      if (fmt != GST_FORMAT_DEFAULT) {
        if (!gst_flac_dec_convert_src (flacdec->srcpad, GST_FORMAT_DEFAULT,
                flacdec->segment.last_stop, &fmt, &pos)) {
          GST_DEBUG_OBJECT (flacdec, "failed to convert position into %s "
              "format", gst_format_get_name (fmt));
          res = FALSE;
          goto done;
        }
      } else {
        pos = flacdec->segment.last_stop;
      }

      gst_query_set_position (query, fmt, pos);

      GST_DEBUG_OBJECT (flacdec, "returning position %" G_GUINT64_FORMAT
          " (format: %s)", pos, gst_format_get_name (fmt));

      res = TRUE;
      break;
    }

    case GST_QUERY_DURATION:{
      GstFormat fmt;
      gint64 len;

      gst_query_parse_duration (query, &fmt, NULL);

      /* try any demuxers or parsers before us first */
      if ((fmt == GST_FORMAT_TIME || fmt == GST_FORMAT_DEFAULT) &&
          peer != NULL && gst_pad_query (peer, query)) {
        gst_query_parse_duration (query, NULL, &len);
        GST_DEBUG_OBJECT (flacdec, "peer returned duration %" GST_TIME_FORMAT,
            GST_TIME_ARGS (len));
        res = TRUE;
        goto done;
      }

      if (flacdec->segment.duration == 0 || flacdec->segment.duration == -1) {
        GST_DEBUG_OBJECT (flacdec, "duration not known yet");
        res = FALSE;
        goto done;
      }

      /* convert total number of samples to request format */
      if (fmt != GST_FORMAT_DEFAULT) {
        if (!gst_flac_dec_convert_src (flacdec->srcpad, GST_FORMAT_DEFAULT,
                flacdec->segment.duration, &fmt, &len)) {
          GST_DEBUG_OBJECT (flacdec, "failed to convert duration into %s "
              "format", gst_format_get_name (fmt));
          res = FALSE;
          goto done;
        }
      } else {
        len = flacdec->segment.duration;
      }

      gst_query_set_duration (query, fmt, len);

      GST_DEBUG_OBJECT (flacdec, "returning duration %" G_GUINT64_FORMAT
          " (format: %s)", len, gst_format_get_name (fmt));

      res = TRUE;
      break;
    }

    case GST_QUERY_CONVERT:{
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, NULL);

      res = gst_flac_dec_convert_src (pad, src_fmt, src_val, &dest_fmt,
          &dest_val);

      if (res) {
        gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      }

      break;
    }
    case GST_QUERY_SEEKING:{
      GstFormat fmt;
      gboolean seekable = FALSE;

      res = TRUE;
      /* If upstream can handle the query we're done */
      seekable = gst_pad_peer_query (flacdec->sinkpad, query);
      if (seekable)
        gst_query_parse_seeking (query, NULL, &seekable, NULL, NULL);
      if (seekable)
        goto done;

      gst_query_parse_seeking (query, &fmt, NULL, NULL, NULL);
      if ((fmt != GST_FORMAT_TIME && fmt != GST_FORMAT_DEFAULT) ||
          flacdec->streaming) {
        gst_query_set_seeking (query, fmt, FALSE, -1, -1);
      } else {
        gst_query_set_seeking (query, GST_FORMAT_TIME, TRUE, 0, -1);
      }
      break;
    }

    default:{
      res = gst_pad_query_default (pad, query);
      break;
    }
  }

done:

  if (peer)
    gst_object_unref (peer);

  gst_object_unref (flacdec);

  return res;
}

static gboolean
gst_flac_dec_handle_seek_event (GstFlacDec * flacdec, GstEvent * event)
{
  FLAC__bool seek_ok;
  GstSeekFlags seek_flags;
  GstSeekType start_type;
  GstSeekType stop_type;
  GstSegment segment;
  GstFormat seek_format;
  gboolean only_update = FALSE;
  gboolean flush;
  gdouble rate;
  gint64 start, last_stop;
  gint64 stop;

  if (flacdec->streaming) {
    GST_DEBUG_OBJECT (flacdec, "seeking in streaming mode not implemented yet");
    return FALSE;
  }

  gst_event_parse_seek (event, &rate, &seek_format, &seek_flags, &start_type,
      &start, &stop_type, &stop);

  if (seek_format != GST_FORMAT_DEFAULT && seek_format != GST_FORMAT_TIME) {
    GST_DEBUG_OBJECT (flacdec,
        "seeking is only supported in TIME or DEFAULT format");
    return FALSE;
  }

  if (rate < 0.0) {
    GST_DEBUG_OBJECT (flacdec,
        "only forward playback supported, rate %f not allowed", rate);
    return FALSE;
  }

  if (seek_format != GST_FORMAT_DEFAULT) {
    GstFormat target_format = GST_FORMAT_DEFAULT;

    if (start_type != GST_SEEK_TYPE_NONE &&
        !gst_flac_dec_convert_src (flacdec->srcpad, seek_format, start,
            &target_format, &start)) {
      GST_DEBUG_OBJECT (flacdec, "failed to convert start to DEFAULT format");
      return FALSE;
    }

    if (stop_type != GST_SEEK_TYPE_NONE &&
        !gst_flac_dec_convert_src (flacdec->srcpad, seek_format, stop,
            &target_format, &stop)) {
      GST_DEBUG_OBJECT (flacdec, "failed to convert stop to DEFAULT format");
      return FALSE;
    }
  }

  /* Check if we seeked after the end of file */
  if (start_type != GST_SEEK_TYPE_NONE && flacdec->segment.duration > 0 &&
      start >= flacdec->segment.duration) {
    flacdec->eos = TRUE;
  } else {
    flacdec->eos = FALSE;
  }

  flush = ((seek_flags & GST_SEEK_FLAG_FLUSH) == GST_SEEK_FLAG_FLUSH);

  if (flush) {
    /* flushing seek, clear the pipeline of stuff, we need a newsegment after
     * this. */
    GST_DEBUG_OBJECT (flacdec, "flushing");
    gst_pad_push_event (flacdec->sinkpad, gst_event_new_flush_start ());
    gst_pad_push_event (flacdec->srcpad, gst_event_new_flush_start ());
  } else {
    /* non flushing seek, pause the task */
    GST_DEBUG_OBJECT (flacdec, "stopping task");
    gst_pad_stop_task (flacdec->sinkpad);
  }

  /* acquire the stream lock, this either happens when the streaming thread
   * stopped because of the flush or when the task is paused after the loop
   * function finished an iteration, which can never happen when it's blocked
   * downstream in PAUSED, for example */
  GST_PAD_STREAM_LOCK (flacdec->sinkpad);

  /* start seek with clear state to avoid seeking thread pushing segments/data.
   * Note current state may have some pending,
   * e.g. multi-sink seek leads to immediate subsequent seek events */
  if (flacdec->start_segment) {
    gst_event_unref (flacdec->start_segment);
    flacdec->start_segment = NULL;
  }
  gst_buffer_replace (&flacdec->pending, NULL);
  flacdec->pending_samples = 0;

  /* save a segment copy until we know the seek worked. The idea is that
   * when the seek fails, we want to restore with what we were doing. */
  segment = flacdec->segment;

  /* update the segment with the seek values, last_stop will contain the new
   * position we should seek to */
  gst_segment_set_seek (&flacdec->segment, rate, GST_FORMAT_DEFAULT,
      seek_flags, start_type, start, stop_type, stop, &only_update);

  GST_DEBUG_OBJECT (flacdec,
      "configured segment: [%" G_GINT64_FORMAT "-%" G_GINT64_FORMAT
      "] = [%" GST_TIME_FORMAT "-%" GST_TIME_FORMAT "]",
      flacdec->segment.start, flacdec->segment.stop,
      GST_TIME_ARGS (flacdec->segment.start * GST_SECOND /
          flacdec->sample_rate),
      GST_TIME_ARGS (flacdec->segment.stop * GST_SECOND /
          flacdec->sample_rate));

  GST_DEBUG_OBJECT (flacdec, "performing seek to sample %" G_GINT64_FORMAT,
      flacdec->segment.last_stop);

  /* flush sinkpad again because we need to pull and push buffers while doing
   * the seek */
  if (flush) {
    GST_DEBUG_OBJECT (flacdec, "flushing stop");
    gst_pad_push_event (flacdec->sinkpad, gst_event_new_flush_stop ());
    gst_pad_push_event (flacdec->srcpad, gst_event_new_flush_stop ());
  }

  /* mark ourselves as seeking because the above lines will trigger some
   * callbacks that need to behave differently when seeking */
  flacdec->seeking = TRUE;

  if (!flacdec->eos) {
    GST_LOG_OBJECT (flacdec, "calling seek_absolute");
    seek_ok = FLAC__stream_decoder_seek_absolute (flacdec->decoder,
        flacdec->segment.last_stop);
    GST_LOG_OBJECT (flacdec, "done with seek_absolute, seek_ok=%d", seek_ok);
  } else {
    GST_LOG_OBJECT (flacdec, "not seeking, seeked after end of file");
    seek_ok = TRUE;
  }

  flacdec->seeking = FALSE;

  GST_DEBUG_OBJECT (flacdec, "performed seek to sample %" G_GINT64_FORMAT,
      flacdec->segment.last_stop);

  if (!seek_ok) {
    GST_WARNING_OBJECT (flacdec, "seek failed");
    /* seek failed, restore the segment and start streaming again with
     * the previous segment values */
    flacdec->segment = segment;
  } else if (!flush && flacdec->running) {
    /* we are running the current segment and doing a non-flushing seek, 
     * close the segment first based on the last_stop. */
    GST_DEBUG_OBJECT (flacdec, "closing running segment %" G_GINT64_FORMAT
        " to %" G_GINT64_FORMAT, segment.start, segment.last_stop);

    /* convert the old segment values to time to close the old segment */
    start = gst_util_uint64_scale_int (segment.start, GST_SECOND,
        flacdec->sample_rate);
    last_stop =
        gst_util_uint64_scale_int (segment.last_stop, GST_SECOND,
        flacdec->sample_rate);

    /* queue the segment for sending in the stream thread, start and time are
     * always the same. */
    if (flacdec->close_segment)
      gst_event_unref (flacdec->close_segment);
    flacdec->close_segment =
        gst_event_new_new_segment_full (TRUE,
        segment.rate, segment.applied_rate, GST_FORMAT_TIME,
        start, last_stop, start);
  }

  if (seek_ok) {
    /* seek succeeded, flacdec->segment contains the new positions */
    GST_DEBUG_OBJECT (flacdec, "seek successful");
  }

  /* convert the (new) segment values to time, we will need them to generate the
   * new segment events. */
  start = gst_util_uint64_scale_int (flacdec->segment.start, GST_SECOND,
      flacdec->sample_rate);
  last_stop = gst_util_uint64_scale_int (flacdec->segment.last_stop, GST_SECOND,
      flacdec->sample_rate);

  /* for deriving a stop position for the playback segment from the seek
   * segment, we must take the duration when the stop is not set */
  if (flacdec->segment.stop != -1)
    stop = gst_util_uint64_scale_int (flacdec->segment.stop, GST_SECOND,
        flacdec->sample_rate);
  else
    stop = gst_util_uint64_scale_int (flacdec->segment.duration, GST_SECOND,
        flacdec->sample_rate);

  /* notify start of new segment when we were asked to do so. */
  if (flacdec->segment.flags & GST_SEEK_FLAG_SEGMENT) {
    /* last_stop contains the position we start from */
    gst_element_post_message (GST_ELEMENT (flacdec),
        gst_message_new_segment_start (GST_OBJECT (flacdec),
            GST_FORMAT_TIME, last_stop));
  }

  /* if the seek was ok or (when it failed) we are flushing, we need to send out
   * a new segment. If we did not flush and the seek failed, we simply do
   * nothing here and continue where we were. */
  if (seek_ok || flush) {
    GST_DEBUG_OBJECT (flacdec, "Creating newsegment from %" GST_TIME_FORMAT
        " to %" GST_TIME_FORMAT, GST_TIME_ARGS (last_stop),
        GST_TIME_ARGS (stop));
    /* now replace the old segment so that we send it in the stream thread the
     * next time it is scheduled. */
    if (flacdec->start_segment)
      gst_event_unref (flacdec->start_segment);
    flacdec->start_segment =
        gst_event_new_new_segment_full (FALSE,
        flacdec->segment.rate, flacdec->segment.applied_rate, GST_FORMAT_TIME,
        last_stop, stop, last_stop);
  }

  /* we'll generate a discont on the next buffer */
  flacdec->discont = TRUE;
  /* the task is running again now */
  flacdec->running = TRUE;
  gst_pad_start_task (flacdec->sinkpad,
      (GstTaskFunction) gst_flac_dec_loop, flacdec->sinkpad);

  GST_PAD_STREAM_UNLOCK (flacdec->sinkpad);

  return seek_ok;
}

static gboolean
gst_flac_dec_src_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  GstFlacDec *flacdec;

  flacdec = GST_FLAC_DEC (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:{
      GST_DEBUG_OBJECT (flacdec, "received seek event %p", event);
      /* first, see if we're before a demuxer that
       * might handle the seek for us */
      gst_event_ref (event);
      res = gst_pad_event_default (pad, event);
      /* if not, try to handle it ourselves */
      if (!res) {
        GST_DEBUG_OBJECT (flacdec, "default failed, handling ourselves");
        res = gst_flac_dec_handle_seek_event (flacdec, event);
      }
      gst_event_unref (event);
      break;
    }
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }

  gst_object_unref (flacdec);

  return res;
}

static gboolean
gst_flac_dec_sink_activate (GstPad * sinkpad)
{
  if (gst_pad_check_pull_range (sinkpad))
    return gst_pad_activate_pull (sinkpad, TRUE);

  return gst_pad_activate_push (sinkpad, TRUE);
}

static gboolean
gst_flac_dec_sink_activate_push (GstPad * sinkpad, gboolean active)
{
  GstFlacDec *dec = GST_FLAC_DEC (GST_OBJECT_PARENT (sinkpad));

  if (active) {
    gst_flac_dec_setup_decoder (dec);
    dec->streaming = TRUE;
    dec->got_headers = FALSE;
  }
  return TRUE;
}

static gboolean
gst_flac_dec_sink_activate_pull (GstPad * sinkpad, gboolean active)
{
  gboolean res;

  if (active) {
    GstFlacDec *flacdec;

    flacdec = GST_FLAC_DEC (GST_PAD_PARENT (sinkpad));

    flacdec->offset = 0;
    gst_flac_dec_setup_decoder (flacdec);
    flacdec->running = TRUE;
    flacdec->streaming = FALSE;

    res = gst_pad_start_task (sinkpad, (GstTaskFunction) gst_flac_dec_loop,
        sinkpad);
  } else {
    res = gst_pad_stop_task (sinkpad);
  }
  return res;
}

static GstStateChangeReturn
gst_flac_dec_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstFlacDec *flacdec = GST_FLAC_DEC (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      flacdec->eos = FALSE;
      flacdec->seeking = FALSE;
      flacdec->channels = 0;
      flacdec->depth = 0;
      flacdec->width = 0;
      flacdec->sample_rate = 0;
      gst_segment_init (&flacdec->segment, GST_FORMAT_DEFAULT);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_segment_init (&flacdec->segment, GST_FORMAT_UNDEFINED);
      gst_flac_dec_reset_decoders (flacdec);
      break;
    default:
      break;
  }

  return ret;
}
