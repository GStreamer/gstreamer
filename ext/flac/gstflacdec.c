/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2006,2011> Tim-Philipp Müller <tim centricular net>
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
 * gst-launch-0.11 filesrc location=media/small/dark.441-16-s.flac ! flacparse ! flacdec ! audioconvert ! audioresample ! autoaudiosink
 * ]|
 * |[
 * gst-launch-0.11 souphttpsrc location=http://gstreamer.freedesktop.org/media/small/dark.441-16-s.flac ! flacparse ! flacdec ! audioconvert ! audioresample ! queue min-threshold-buffers=10 ! autoaudiosink
 * ]|
 * </refsect2>
 */

/* FIXME: remove all granulepos handling if there's any left */

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

static GstStateChangeReturn gst_flac_dec_change_state (GstElement * element,
    GstStateChange transition);
static const GstQueryType *gst_flac_dec_get_src_query_types (GstPad * pad);
static const GstQueryType *gst_flac_dec_get_sink_query_types (GstPad * pad);
static gboolean gst_flac_dec_sink_query (GstPad * pad, GstQuery * query);
static gboolean gst_flac_dec_src_query (GstPad * pad, GstQuery * query);
static gboolean gst_flac_dec_convert_src (GstPad * pad, GstFormat src_format,
    gint64 src_value, GstFormat * dest_format, gint64 * dest_value);
static gboolean gst_flac_dec_sink_activate (GstPad * sinkpad);
static gboolean gst_flac_dec_sink_activate_push (GstPad * sinkpad,
    gboolean active);
static gboolean gst_flac_dec_sink_event (GstPad * pad, GstEvent * event);
static GstFlowReturn gst_flac_dec_chain (GstPad * pad, GstBuffer * buf);

static void gst_flac_dec_reset_decoders (GstFlacDec * flacdec);
static void gst_flac_dec_setup_decoder (GstFlacDec * flacdec);

static FLAC__StreamDecoderReadStatus
gst_flac_dec_read_stream (const FLAC__StreamDecoder * decoder,
    FLAC__byte buffer[], size_t * bytes, void *client_data);
static FLAC__StreamDecoderWriteStatus
gst_flac_dec_write_stream (const FLAC__StreamDecoder * decoder,
    const FLAC__Frame * frame,
    const FLAC__int32 * const buffer[], void *client_data);
static void gst_flac_dec_metadata_cb (const FLAC__StreamDecoder *
    decoder, const FLAC__StreamMetadata * metadata, void *client_data);
static void gst_flac_dec_error_cb (const FLAC__StreamDecoder *
    decoder, FLAC__StreamDecoderErrorStatus status, void *client_data);

#define gst_flac_dec_parent_class parent_class
G_DEFINE_TYPE (GstFlacDec, gst_flac_dec, GST_TYPE_ELEMENT);

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
#define FORMATS "{ S8LE, S16LE, S32LE } "
#else
#define FORMATS "{ S8BE, S16BE, S32BE } "
#endif

/* FIXME 0.11: Use width=32 for all depths and let audioconvert
 * handle the conversions instead of doing it ourself.
 */
#define GST_FLAC_DEC_SRC_CAPS                             \
    "audio/x-raw, "                                       \
    "format = (string) " FORMATS ", "                     \
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
gst_flac_dec_class_init (GstFlacDecClass * klass)
{
  GstElementClass *gstelement_class;
  GObjectClass *gobject_class;

  gstelement_class = (GstElementClass *) klass;
  gobject_class = (GObjectClass *) klass;

  GST_DEBUG_CATEGORY_INIT (flacdec_debug, "flacdec", 0, "flac decoder");

  gobject_class->finalize = gst_flac_dec_finalize;

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_flac_dec_change_state);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&flac_dec_src_factory));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&flac_dec_sink_factory));

  gst_element_class_set_details_simple (gstelement_class, "FLAC audio decoder",
      "Codec/Decoder/Audio",
      "Decodes FLAC lossless audio streams", "Wim Taymans <wim@fluendo.com>");
}

static void
gst_flac_dec_init (GstFlacDec * flacdec)
{
  flacdec->sinkpad =
      gst_pad_new_from_static_template (&flac_dec_sink_factory, "sink");
  gst_pad_set_activate_function (flacdec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_flac_dec_sink_activate));
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

  if (flacdec->tags) {
    gst_tag_list_free (flacdec->tags);
    flacdec->tags = NULL;
  }

  flacdec->segment.position = 0;
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

      GST_DEBUG_OBJECT (flacdec, "total samples = %" G_GINT64_FORMAT, samples);

      /* in framed mode the demuxer/parser upstream has already pushed a
       * newsegment event in TIME format which we've passed on */
      if (samples > 0 && !flacdec->framed) {
        gint64 duration;
        GstSegment seg;

        flacdec->segment.duration = samples;

        /* convert duration to time */
        duration = gst_util_uint64_scale_int (samples, GST_SECOND,
            flacdec->sample_rate);

        gst_segment_init (&seg, GST_FORMAT_TIME);
        seg.rate = flacdec->segment.rate;
        seg.applied_rate = flacdec->segment.applied_rate;
        seg.start = 0;
        seg.stop = duration;
        seg.time = 0;
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
  gpointer data;
  gsize size;
  const gchar *format;

  GST_LOG_OBJECT (flacdec, "samples in frame header: %d", samples);

  /* if a DEFAULT segment is configured, don't send samples past the end
   * of the segment */
  if (flacdec->segment.format == GST_FORMAT_DEFAULT &&
      flacdec->segment.stop != -1 &&
      flacdec->segment.position >= 0 &&
      flacdec->segment.position + samples > flacdec->segment.stop) {
    samples = flacdec->segment.stop - flacdec->segment.position;
    GST_DEBUG_OBJECT (flacdec,
        "clipping last buffer to %d samples because of segment", samples);
  }

  if (depth == 0) {
    if (flacdec->depth < 4 || flacdec->depth > 32) {
      GST_ERROR_OBJECT (flacdec, "unsupported depth %d from STREAMINFO",
          flacdec->depth);
      ret = GST_FLOW_ERROR;
      goto done;
    }

    depth = flacdec->depth;
    if (depth < 9)
      depth = 8;
    else if (depth < 17)
      depth = 16;
    else
      depth = 32;
  }

  switch (depth) {
    case 8:
      width = 8;
      format = GST_AUDIO_NE (S8);
      break;
    case 12:
    case 16:
      width = 16;
      format = GST_AUDIO_NE (S16);
      break;
    case 20:
    case 24:
    case 32:
      width = 32;
      format = GST_AUDIO_NE (S32);
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

  if (!gst_pad_has_current_caps (flacdec->srcpad)) {
    GstCaps *caps;

    GST_DEBUG_OBJECT (flacdec, "Negotiating %d Hz @ %d channels",
        frame->header.sample_rate, channels);

    caps = gst_caps_new_simple ("audio/x-raw",
        "format", G_TYPE_STRING, format,
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

  if (flacdec->tags) {
    gst_element_found_tags_for_pad (GST_ELEMENT (flacdec), flacdec->srcpad,
        flacdec->tags);
    flacdec->tags = NULL;
  }

  GST_LOG_OBJECT (flacdec, "alloc_buffer_and_set_caps");
  outbuf = gst_buffer_new_allocate (NULL, samples * channels * (width / 8), 0);

  GST_BUFFER_OFFSET (outbuf) = flacdec->segment.position;

  if (flacdec->cur_granulepos != GST_BUFFER_OFFSET_NONE) {
    /* this should be fine since it should be one flac frame per ogg packet */
    flacdec->segment.position = flacdec->cur_granulepos - samples;
    GST_LOG_OBJECT (flacdec, "granulepos = %" G_GINT64_FORMAT ", samples = %u",
        flacdec->cur_granulepos, samples);
  }

  GST_BUFFER_TIMESTAMP (outbuf) =
      gst_util_uint64_scale_int (flacdec->segment.position, GST_SECOND,
      frame->header.sample_rate);

  /* get next timestamp to calculate the duration */
  next = gst_util_uint64_scale_int (flacdec->segment.position + samples,
      GST_SECOND, frame->header.sample_rate);

  GST_BUFFER_DURATION (outbuf) = next - GST_BUFFER_TIMESTAMP (outbuf);

  data = gst_buffer_map (outbuf, &size, NULL, GST_MAP_WRITE);
  if (width == 8) {
    gint8 *outbuffer = (gint8 *) data;

    for (i = 0; i < samples; i++) {
      for (j = 0; j < channels; j++) {
        *outbuffer++ = (gint8) buffer[j][i];
      }
    }
  } else if (width == 16) {
    gint16 *outbuffer = (gint16 *) data;

    for (i = 0; i < samples; i++) {
      for (j = 0; j < channels; j++) {
        *outbuffer++ = (gint16) buffer[j][i];
      }
    }
  } else if (width == 32) {
    gint32 *outbuffer = (gint32 *) data;

    for (i = 0; i < samples; i++) {
      for (j = 0; j < channels; j++) {
        *outbuffer++ = (gint32) buffer[j][i];
      }
    }
  } else {
    g_assert_not_reached ();
  }
  gst_buffer_unmap (outbuf, data, size);

  GST_DEBUG_OBJECT (flacdec, "pushing %d samples at offset %" G_GINT64_FORMAT
      " (%" GST_TIME_FORMAT " + %" GST_TIME_FORMAT ")",
      samples, GST_BUFFER_OFFSET (outbuf),
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (outbuf)));

  ret = gst_pad_push (flacdec->srcpad, outbuf);
  GST_DEBUG_OBJECT (flacdec, "returned %s", gst_flow_get_name (ret));
  flacdec->segment.position += samples;

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

static gboolean
gst_flac_dec_sink_event (GstPad * pad, GstEvent * event)
{
  GstFlacDec *dec;
  gboolean res;

  dec = GST_FLAC_DEC (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
      res = TRUE;
      gst_event_unref (event);
      break;
    case GST_EVENT_FLUSH_STOP:{
      if (dec->init == FALSE) {
        FLAC__stream_decoder_flush (dec->decoder);
        gst_adapter_clear (dec->adapter);
      }
      res = gst_pad_push_event (dec->srcpad, event);
      break;
    }
    case GST_EVENT_SEGMENT:{
      GstSegment seg;
      gint64 start, stop;

      gst_event_copy_segment (event, &seg);

      if (seg.format == GST_FORMAT_TIME) {
        GstFormat dformat = GST_FORMAT_DEFAULT;

        GST_DEBUG_OBJECT (dec, "newsegment event in TIME format => framed");
        dec->framed = TRUE;
        res = gst_pad_push_event (dec->srcpad, event);

        /* this won't work for the first newsegment event though ... */
        if (gst_flac_dec_convert_src (dec->srcpad, GST_FORMAT_TIME, seg.start,
                &dformat, &start) && start != -1 &&
            gst_flac_dec_convert_src (dec->srcpad, GST_FORMAT_TIME, seg.stop,
                &dformat, &stop) && stop != -1) {

          seg.start = start;
          seg.stop = stop;
          dec->segment = seg;

          GST_DEBUG_OBJECT (dec, "segment %" GST_SEGMENT_FORMAT, &dec->segment);
        } else {
          GST_WARNING_OBJECT (dec, "couldn't convert time => samples");
        }
      } else if (seg.format == GST_FORMAT_BYTES || TRUE) {
        /* FIXME: error out or post warning, we require parsed input */
        gst_event_unref (event);
        res = FALSE;
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
      ", end_offset=%" G_GINT64_FORMAT ", size=%" G_GSIZE_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)), GST_BUFFER_OFFSET (buf),
      GST_BUFFER_OFFSET_END (buf), gst_buffer_get_size (buf));

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
    guint8 *data;
    gsize size;

    /* check if this is a flac audio frame (rather than a header or junk) */
    data = gst_buffer_map (buf, &size, NULL, GST_MAP_READ);
    got_audio_frame = gst_flac_dec_scan_got_frame (dec, data, size, &unused);
    gst_buffer_unmap (buf, data, size);

    /* oggdemux will set granulepos in OFFSET_END instead of timestamp */
    if (G_LIKELY (got_audio_frame)) {
      /* old oggdemux for now */
      if (!GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
        dec->cur_granulepos = GST_BUFFER_OFFSET_END (buf);
      } else {
        GstFormat dformat = GST_FORMAT_DEFAULT;
        gint64 position;

        /* upstream (e.g. demuxer) presents us time,
         * convert to default samples */
        gst_flac_dec_convert_src (dec->srcpad, GST_FORMAT_TIME,
            GST_BUFFER_TIMESTAMP (buf), &dformat, &position);
        dec->segment.position = position;
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
                flacdec->segment.position, &fmt, &pos)) {
          GST_DEBUG_OBJECT (flacdec, "failed to convert position into %s "
              "format", gst_format_get_name (fmt));
          res = FALSE;
          goto done;
        }
      } else {
        pos = flacdec->segment.position;
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
gst_flac_dec_sink_activate (GstPad * sinkpad)
{
  GST_DEBUG_OBJECT (sinkpad, "activating push");
  return gst_pad_activate_push (sinkpad, TRUE);
}

static gboolean
gst_flac_dec_sink_activate_push (GstPad * sinkpad, gboolean active)
{
  GstFlacDec *dec = GST_FLAC_DEC (GST_OBJECT_PARENT (sinkpad));

  if (active) {
    gst_flac_dec_setup_decoder (dec);
    dec->got_headers = FALSE;
  }
  return TRUE;
}

static GstStateChangeReturn
gst_flac_dec_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstFlacDec *flacdec = GST_FLAC_DEC (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
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
