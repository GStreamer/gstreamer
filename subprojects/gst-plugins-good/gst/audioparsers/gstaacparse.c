/* GStreamer AAC parser plugin
 * Copyright (C) 2008 Nokia Corporation. All rights reserved.
 *
 * Contact: Stefan Kost <stefan.kost@nokia.com>
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
 * SECTION:element-aacparse
 * @title: aacparse
 * @short_description: AAC parser
 * @see_also: #GstAmrParse
 *
 * This is an AAC parser which handles both ADIF and ADTS stream formats.
 *
 * As ADIF format is not framed, it is not seekable and stream duration cannot
 * be determined either. However, ADTS format AAC clips can be seeked, and parser
 * can also estimate playback position and clip duration.
 *
 * ## Example launch line
 * |[
 * gst-launch-1.0 filesrc location=abc.aac ! aacparse ! faad ! audioresample ! audioconvert ! alsasink
 * ]|
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include <gst/base/gstbitreader.h>
#include <gst/base/gstbitwriter.h>
#include <gst/pbutils/pbutils.h>
#include "gstaudioparserselements.h"
#include "gstaacparse.h"


static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg, "
        "framed = (boolean) true, " "mpegversion = (int) { 2, 4 }, "
        "stream-format = (string) { raw, adts, adif, loas };"));

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg, mpegversion = (int) { 2, 4 };"));

GST_DEBUG_CATEGORY_STATIC (aacparse_debug);
#define GST_CAT_DEFAULT aacparse_debug


#define ADIF_MAX_SIZE 40        /* Should be enough */
#define ADTS_MAX_SIZE 10        /* Should be enough */
#define LOAS_MAX_SIZE 3         /* Should be enough */
#define RAW_MAX_SIZE  1         /* Correct framing is required */

#define ADTS_HEADERS_LENGTH 7UL /* Total byte-length of fixed and variable
                                   headers prepended during raw to ADTS
                                   conversion */

#define AAC_FRAME_DURATION(parse) (GST_SECOND/parse->frames_per_sec)

static const gint loas_channels_table[16] = {
  0, 1, 2, 3, 4, 5, 6, 8,
  0, 0, 0, 7, 8, 0, 8, 0
};

static gboolean gst_aac_parse_start (GstBaseParse * parse);
static gboolean gst_aac_parse_stop (GstBaseParse * parse);

static gboolean gst_aac_parse_sink_setcaps (GstBaseParse * parse,
    GstCaps * caps);
static GstCaps *gst_aac_parse_sink_getcaps (GstBaseParse * parse,
    GstCaps * filter);

static GstFlowReturn gst_aac_parse_handle_frame (GstBaseParse * parse,
    GstBaseParseFrame * frame, gint * skipsize);
static GstFlowReturn gst_aac_parse_pre_push_frame (GstBaseParse * parse,
    GstBaseParseFrame * frame);
static gboolean gst_aac_parse_src_event (GstBaseParse * parse,
    GstEvent * event);

static gboolean gst_aac_parse_read_audio_specific_config (GstAacParse *
    aacparse, GstBitReader * br, gint * object_type, gint * sample_rate,
    gint * channels, gint * frame_samples);

static void gst_aac_parse_dispose (GObject * object);


#define gst_aac_parse_parent_class parent_class
G_DEFINE_TYPE (GstAacParse, gst_aac_parse, GST_TYPE_BASE_PARSE);
GST_ELEMENT_REGISTER_DEFINE (aacparse, "aacparse",
    GST_RANK_PRIMARY + 1, GST_TYPE_AAC_PARSE);

/**
 * gst_aac_parse_class_init:
 * @klass: #GstAacParseClass.
 *
 */
static void
gst_aac_parse_class_init (GstAacParseClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseParseClass *parse_class = GST_BASE_PARSE_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (aacparse_debug, "aacparse", 0,
      "AAC audio stream parser");

  gst_element_class_add_static_pad_template (element_class, &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);

  gst_element_class_set_static_metadata (element_class,
      "AAC audio stream parser", "Codec/Parser/Audio",
      "Advanced Audio Coding parser", "Stefan Kost <stefan.kost@nokia.com>");

  parse_class->start = GST_DEBUG_FUNCPTR (gst_aac_parse_start);
  parse_class->stop = GST_DEBUG_FUNCPTR (gst_aac_parse_stop);
  parse_class->set_sink_caps = GST_DEBUG_FUNCPTR (gst_aac_parse_sink_setcaps);
  parse_class->get_sink_caps = GST_DEBUG_FUNCPTR (gst_aac_parse_sink_getcaps);
  parse_class->handle_frame = GST_DEBUG_FUNCPTR (gst_aac_parse_handle_frame);
  parse_class->pre_push_frame =
      GST_DEBUG_FUNCPTR (gst_aac_parse_pre_push_frame);
  parse_class->src_event = GST_DEBUG_FUNCPTR (gst_aac_parse_src_event);

  object_class->dispose = gst_aac_parse_dispose;
}


/**
 * gst_aac_parse_init:
 * @aacparse: #GstAacParse.
 * @klass: #GstAacParseClass.
 *
 */
static void
gst_aac_parse_init (GstAacParse * aacparse)
{
  GST_DEBUG ("initialized");
  GST_PAD_SET_ACCEPT_INTERSECT (GST_BASE_PARSE_SINK_PAD (aacparse));
  GST_PAD_SET_ACCEPT_TEMPLATE (GST_BASE_PARSE_SINK_PAD (aacparse));

  aacparse->last_parsed_sample_rate = 0;
  aacparse->last_parsed_channels = 0;
}

/**
 * gst_aac_parse_dispose:
 * @aacparse: #GstAacParse.
 * @klass: #GstAacParseClass.
 *
 */
static void
gst_aac_parse_dispose (GObject * object)
{
  GstAacParse *aacparse = GST_AAC_PARSE (object);

  g_clear_pointer (&aacparse->pce, g_free);
  g_clear_pointer (&aacparse->pce_comment, g_free);

  G_OBJECT_CLASS (gst_aac_parse_parent_class)->dispose (object);
}

/**
 * gst_aac_parse_set_src_caps:
 * @aacparse: #GstAacParse.
 * @sink_caps: (proposed) caps of sink pad
 *
 * Set source pad caps according to current knowledge about the
 * audio stream.
 *
 * Returns: TRUE if caps were successfully set.
 */
static gboolean
gst_aac_parse_set_src_caps (GstAacParse * aacparse, GstCaps * sink_caps)
{
  GstStructure *s;
  GstCaps *src_caps = NULL, *peercaps;
  gboolean res = FALSE;
  const gchar *stream_format;
  guint8 codec_data[2];
  guint16 codec_data_data;
  gint sample_rate_idx;

  GST_DEBUG_OBJECT (aacparse, "sink caps: %" GST_PTR_FORMAT, sink_caps);
  if (sink_caps)
    src_caps = gst_caps_copy (sink_caps);
  else
    src_caps = gst_caps_new_empty_simple ("audio/mpeg");

  gst_caps_set_simple (src_caps, "framed", G_TYPE_BOOLEAN, TRUE,
      "mpegversion", G_TYPE_INT, aacparse->mpegversion, NULL);

  aacparse->output_header_type = aacparse->header_type;
  switch (aacparse->header_type) {
    case DSPAAC_HEADER_NONE:
      stream_format = "raw";
      break;
    case DSPAAC_HEADER_ADTS:
      stream_format = "adts";
      break;
    case DSPAAC_HEADER_ADIF:
      stream_format = "adif";
      break;
    case DSPAAC_HEADER_LOAS:
      stream_format = "loas";
      break;
    default:
      stream_format = NULL;
  }

  /* Generate codec data to be able to set profile/level on the caps.
   * The codec_data data is according to AudioSpecificConfig,
   * ISO/IEC 14496-3, 1.6.2.1 */
  sample_rate_idx =
      gst_codec_utils_aac_get_index_from_sample_rate (aacparse->sample_rate);
  if (sample_rate_idx < 0)
    goto not_a_known_rate;
  codec_data_data =
      (aacparse->object_type << 11) |
      (sample_rate_idx << 7) | (aacparse->channels << 3);
  GST_WRITE_UINT16_BE (codec_data, codec_data_data);
  gst_codec_utils_aac_caps_set_level_and_profile (src_caps, codec_data, 2);

  s = gst_caps_get_structure (src_caps, 0);
  if (aacparse->sample_rate > 0)
    gst_structure_set (s, "rate", G_TYPE_INT, aacparse->sample_rate, NULL);
  if (aacparse->channels > 0)
    gst_structure_set (s, "channels", G_TYPE_INT, aacparse->channels, NULL);
  if (stream_format)
    gst_structure_set (s, "stream-format", G_TYPE_STRING, stream_format, NULL);

  peercaps = gst_pad_peer_query_caps (GST_BASE_PARSE_SRC_PAD (aacparse), NULL);
  if (peercaps && !gst_caps_can_intersect (src_caps, peercaps)) {
    GstCaps *convcaps = gst_caps_copy (src_caps);
    GstStructure *cs = gst_caps_get_structure (convcaps, 0);

    GST_DEBUG_OBJECT (aacparse, "Caps do not intersect: parsed %" GST_PTR_FORMAT
        " and peer %" GST_PTR_FORMAT, src_caps, peercaps);

    if (aacparse->header_type == DSPAAC_HEADER_ADTS) {
      GstBuffer *codec_data_buffer = gst_buffer_new_and_alloc (2);

      gst_buffer_fill (codec_data_buffer, 0, codec_data, 2);
      gst_structure_set (cs, "stream-format", G_TYPE_STRING, "raw",
          "codec_data", GST_TYPE_BUFFER, codec_data_buffer, NULL);

      if (gst_caps_can_intersect (convcaps, peercaps)) {
        GST_DEBUG_OBJECT (aacparse, "Converting from ADTS to raw");
        aacparse->output_header_type = DSPAAC_HEADER_NONE;
        gst_caps_replace (&src_caps, convcaps);
      }

      gst_buffer_unref (codec_data_buffer);
    } else if (aacparse->header_type == DSPAAC_HEADER_NONE) {
      gst_structure_set (cs, "stream-format", G_TYPE_STRING, "adts", NULL);
      gst_structure_remove_field (cs, "codec_data");

      if (gst_caps_can_intersect (convcaps, peercaps)) {
        GST_DEBUG_OBJECT (aacparse, "Converting from raw to ADTS");
        aacparse->output_header_type = DSPAAC_HEADER_ADTS;
        gst_caps_replace (&src_caps, convcaps);
      }
    }

    gst_caps_unref (convcaps);
  }
  if (peercaps)
    gst_caps_unref (peercaps);

  GST_DEBUG_OBJECT (aacparse, "setting src caps: %" GST_PTR_FORMAT, src_caps);

  res = gst_pad_set_caps (GST_BASE_PARSE (aacparse)->srcpad, src_caps);
  gst_caps_unref (src_caps);
  return res;

not_a_known_rate:
  GST_ERROR_OBJECT (aacparse, "Not a known sample rate: %d",
      aacparse->sample_rate);
  gst_caps_unref (src_caps);
  return FALSE;
}


/**
 * gst_aac_parse_sink_setcaps:
 * @sinkpad: GstPad
 * @caps: GstCaps
 *
 * Implementation of "set_sink_caps" vmethod in #GstBaseParse class.
 *
 * Returns: TRUE on success.
 */
static gboolean
gst_aac_parse_sink_setcaps (GstBaseParse * parse, GstCaps * caps)
{
  GstAacParse *aacparse;
  GstStructure *structure;
  gchar *caps_str;
  const GValue *value;

  aacparse = GST_AAC_PARSE (parse);
  structure = gst_caps_get_structure (caps, 0);
  caps_str = gst_caps_to_string (caps);

  GST_DEBUG_OBJECT (aacparse, "setcaps: %s", caps_str);
  g_free (caps_str);

  /* This is needed at least in case of RTP
   * Parses the codec_data information to get ObjectType,
   * number of channels and samplerate */
  value = gst_structure_get_value (structure, "codec_data");
  if (value) {
    GstBuffer *buf = gst_value_get_buffer (value);

    if (buf && gst_buffer_get_size (buf) >= 2) {
      GstMapInfo map;
      GstBitReader br;

      if (!gst_buffer_map (buf, &map, GST_MAP_READ))
        return FALSE;
      gst_bit_reader_init (&br, map.data, map.size);
      gst_aac_parse_read_audio_specific_config (aacparse, &br,
          &aacparse->object_type, &aacparse->sample_rate, &aacparse->channels,
          &aacparse->frame_samples);

      aacparse->header_type = DSPAAC_HEADER_NONE;
      aacparse->mpegversion = 4;
      gst_buffer_unmap (buf, &map);

      GST_DEBUG ("codec_data: object_type=%d, sample_rate=%d, channels=%d, "
          "samples=%d", aacparse->object_type, aacparse->sample_rate,
          aacparse->channels, aacparse->frame_samples);

      /* arrange for metadata and get out of the way */
      gst_aac_parse_set_src_caps (aacparse, caps);
      if (aacparse->header_type == aacparse->output_header_type)
        gst_base_parse_set_passthrough (parse, TRUE);

      /* input is already correctly framed */
      gst_base_parse_set_min_frame_size (parse, RAW_MAX_SIZE);
    } else {
      return FALSE;
    }

    /* caps info overrides */
    gst_structure_get_int (structure, "rate", &aacparse->sample_rate);
    gst_structure_get_int (structure, "channels", &aacparse->channels);
  } else {
    const gchar *stream_format =
        gst_structure_get_string (structure, "stream-format");

    if (g_strcmp0 (stream_format, "raw") == 0) {
      GST_ERROR_OBJECT (parse, "Need codec_data for raw AAC");
      return FALSE;
    } else {
      aacparse->sample_rate = 0;
      aacparse->channels = 0;
      aacparse->header_type = DSPAAC_HEADER_NOT_PARSED;
      gst_base_parse_set_passthrough (parse, FALSE);
    }
  }
  return TRUE;
}


/**
 * gst_aac_parse_adts_get_frame_len:
 * @data: block of data containing an ADTS header.
 *
 * This function calculates ADTS frame length from the given header.
 *
 * Returns: size of the ADTS frame.
 */
static inline guint
gst_aac_parse_adts_get_frame_len (const guint8 * data)
{
  return ((data[3] & 0x03) << 11) | (data[4] << 3) | ((data[5] & 0xe0) >> 5);
}


/**
 * gst_aac_parse_check_adts_frame:
 * @aacparse: #GstAacParse.
 * @data: Data to be checked.
 * @avail: Amount of data passed.
 * @framesize: If valid ADTS frame was found, this will be set to tell the
 *             found frame size in bytes.
 * @needed_data: If frame was not found, this may be set to tell how much
 *               more data is needed in the next round to detect the frame
 *               reliably. This may happen when a frame header candidate
 *               is found but it cannot be guaranteed to be the header without
 *               peeking the following data.
 *
 * Check if the given data contains contains ADTS frame. The algorithm
 * will examine ADTS frame header and calculate the frame size. Also, another
 * consecutive ADTS frame header need to be present after the found frame.
 * Otherwise the data is not considered as a valid ADTS frame. However, this
 * "extra check" is omitted when EOS has been received. In this case it is
 * enough when data[0] contains a valid ADTS header.
 *
 * This function may set the #needed_data to indicate that a possible frame
 * candidate has been found, but more data (#needed_data bytes) is needed to
 * be absolutely sure. When this situation occurs, FALSE will be returned.
 *
 * When a valid frame is detected, this function will use
 * gst_base_parse_set_min_frame_size() function from #GstBaseParse class
 * to set the needed bytes for next frame.This way next data chunk is already
 * of correct size.
 *
 * Returns: TRUE if the given data contains a valid ADTS header.
 */
static gboolean
gst_aac_parse_check_adts_frame (GstAacParse * aacparse,
    const guint8 * data, const guint avail, gboolean drain,
    guint * framesize, guint * needed_data)
{
  guint crc_size;

  *needed_data = 0;

  /* Absolute minimum to perform the ADTS syncword,
     layer and sampling frequency tests */
  if (G_UNLIKELY (avail < 3)) {
    *needed_data = 3;
    return FALSE;
  }

  /* Syncword and layer tests */
  if ((data[0] == 0xff) && ((data[1] & 0xf6) == 0xf0)) {

    /* Sampling frequency test */
    if (G_UNLIKELY ((data[2] & 0x3C) >> 2 == 15))
      return FALSE;

    /* This looks like an ADTS frame header but
       we need at least 6 bytes to proceed */
    if (G_UNLIKELY (avail < 6)) {
      *needed_data = 6;
      return FALSE;
    }

    *framesize = gst_aac_parse_adts_get_frame_len (data);

    /* If frame has CRC, it needs 2 bytes
       for it at the end of the header */
    crc_size = (data[1] & 0x01) ? 0 : 2;

    /* CRC size test */
    if (*framesize < 7 + crc_size) {
      *needed_data = 7 + crc_size;
      return FALSE;
    }

    /* In EOS mode this is enough. No need to examine the data further.
       We also relax the check when we have sync, on the assumption that
       if we're not looking at random data, we have a much higher chance
       to get the correct sync, and this avoids losing two frames when
       a single bit corruption happens. */
    if (drain || !GST_BASE_PARSE_LOST_SYNC (aacparse)) {
      return TRUE;
    }

    if (*framesize + ADTS_MAX_SIZE > avail) {
      /* We have found a possible frame header candidate, but can't be
         sure since we don't have enough data to check the next frame */
      GST_DEBUG ("NEED MORE DATA: we need %d, available %d",
          *framesize + ADTS_MAX_SIZE, avail);
      *needed_data = *framesize + ADTS_MAX_SIZE;
      gst_base_parse_set_min_frame_size (GST_BASE_PARSE (aacparse),
          *framesize + ADTS_MAX_SIZE);
      return FALSE;
    }

    if ((data[*framesize] == 0xff) && ((data[*framesize + 1] & 0xf6) == 0xf0)) {
      guint nextlen = gst_aac_parse_adts_get_frame_len (data + (*framesize));

      GST_LOG ("ADTS frame found, len: %d bytes", *framesize);
      gst_base_parse_set_min_frame_size (GST_BASE_PARSE (aacparse),
          nextlen + ADTS_MAX_SIZE);
      return TRUE;
    }
  }
  return FALSE;
}

static gboolean
gst_aac_parse_latm_get_value (GstAacParse * aacparse, GstBitReader * br,
    guint32 * value)
{
  guint8 bytes, i, byte;

  *value = 0;
  if (!gst_bit_reader_get_bits_uint8 (br, &bytes, 2))
    return FALSE;
  for (i = 0; i <= bytes; ++i) {
    *value <<= 8;
    if (!gst_bit_reader_get_bits_uint8 (br, &byte, 8))
      return FALSE;
    *value += byte;
  }
  return TRUE;
}

static gboolean
gst_aac_parse_get_audio_object_type (GstAacParse * aacparse, GstBitReader * br,
    guint8 * audio_object_type)
{
  /* ISO/IEC 14496-3 Table 1.16 - Syntax of GetAudioObjectType() */
  if (!gst_bit_reader_get_bits_uint8 (br, audio_object_type, 5))
    return FALSE;
  if (*audio_object_type == 31) {
    if (!gst_bit_reader_get_bits_uint8 (br, audio_object_type, 6))
      return FALSE;
    *audio_object_type += 32;
  }
  GST_LOG_OBJECT (aacparse, "audio object type %u", *audio_object_type);
  return TRUE;
}

static gboolean
gst_aac_parse_get_audio_sample_rate (GstAacParse * aacparse, GstBitReader * br,
    gint * sample_rate)
{
  guint8 sampling_frequency_index;
  if (!gst_bit_reader_get_bits_uint8 (br, &sampling_frequency_index, 4))
    return FALSE;
  GST_LOG_OBJECT (aacparse, "sampling_frequency_index: %u",
      sampling_frequency_index);
  if (sampling_frequency_index == 0xf) {
    guint32 sampling_rate;
    if (!gst_bit_reader_get_bits_uint32 (br, &sampling_rate, 24))
      return FALSE;
    *sample_rate = sampling_rate;
  } else {
    *sample_rate =
        gst_codec_utils_aac_get_sample_rate_from_index
        (sampling_frequency_index);
    if (!*sample_rate)
      return FALSE;
  }
  aacparse->last_parsed_sample_rate = *sample_rate;
  GST_LOG_OBJECT (aacparse, "sample rate: %d", *sample_rate);
  return TRUE;
}

static gboolean
gst_aac_parse_program_config_element (GstAacParse * aacparse,
    GstBitReader * br, gint * channels)
{
  /* ISO/IEC 13818-7 Table 25 - Syntax of program_config_element() */
  guint8 element_instance_tag;
  guint8 profile;
  guint8 sampling_frequency_index;
  guint8 num_front_channel_elements;
  guint8 num_side_channel_elements;
  guint8 num_back_channel_elements;
  guint8 num_lfe_channel_elements;
  guint8 num_assoc_data_elements;
  guint8 num_valid_cc_elements;
  guint8 mono_mixdown_present;
  guint8 mono_mixdown_element_number = 0;
  guint8 stereo_mixdown_present;
  guint8 stereo_mixdown_element_number = 0;
  guint8 matrix_mixdown_idx_present;
  guint8 matrix_mixdown_idx = 0;
  guint8 pseudo_surround_enable = 0;
  guint8 comment_field_bytes;
  guint start_pos, end_pos;
  guint pce_bits, pce_whole_bytes, pce_trailing_bits;
  guint i;

  start_pos = gst_bit_reader_get_pos (br);
  GST_LOG_OBJECT (aacparse, "Started parsing PCE at pos %u", start_pos);

  if (!gst_bit_reader_get_bits_uint8 (br, &element_instance_tag, 4))
    return FALSE;
  if (!gst_bit_reader_get_bits_uint8 (br, &profile, 2))
    return FALSE;
  if (!gst_bit_reader_get_bits_uint8 (br, &sampling_frequency_index, 4))
    return FALSE;

  GST_LOG_OBJECT (aacparse, "Instance tag %d, profile %d, freq %d",
      element_instance_tag, profile, sampling_frequency_index);

  if (!gst_bit_reader_get_bits_uint8 (br, &num_front_channel_elements, 4))
    return FALSE;
  if (!gst_bit_reader_get_bits_uint8 (br, &num_side_channel_elements, 4))
    return FALSE;
  if (!gst_bit_reader_get_bits_uint8 (br, &num_back_channel_elements, 4))
    return FALSE;
  if (!gst_bit_reader_get_bits_uint8 (br, &num_lfe_channel_elements, 2))
    return FALSE;
  if (!gst_bit_reader_get_bits_uint8 (br, &num_assoc_data_elements, 3))
    return FALSE;
  if (!gst_bit_reader_get_bits_uint8 (br, &num_valid_cc_elements, 4))
    return FALSE;

  GST_LOG_OBJECT (aacparse,
      "Elements: front %d side %d back %d lfe %d assoc %d cc %d",
      num_front_channel_elements, num_side_channel_elements,
      num_back_channel_elements, num_lfe_channel_elements,
      num_assoc_data_elements, num_valid_cc_elements);

  if (!gst_bit_reader_get_bits_uint8 (br, &mono_mixdown_present, 1))
    return FALSE;
  if (mono_mixdown_present) {
    if (!gst_bit_reader_get_bits_uint8 (br, &mono_mixdown_element_number, 4))
      return FALSE;
  }

  GST_LOG_OBJECT (aacparse, "Mono mixdown %spresent (element %d)",
      mono_mixdown_present ? "" : "not ", mono_mixdown_element_number);

  if (!gst_bit_reader_get_bits_uint8 (br, &stereo_mixdown_present, 1))
    return FALSE;
  if (stereo_mixdown_present) {
    if (!gst_bit_reader_get_bits_uint8 (br, &stereo_mixdown_element_number, 4))
      return FALSE;
  }

  GST_LOG_OBJECT (aacparse, "Stereo mixdown %spresent (element %d)",
      stereo_mixdown_present ? "" : "not ", stereo_mixdown_element_number);

  if (!gst_bit_reader_get_bits_uint8 (br, &matrix_mixdown_idx_present, 1))
    return FALSE;
  if (matrix_mixdown_idx_present) {
    if (!gst_bit_reader_get_bits_uint8 (br, &matrix_mixdown_idx, 2))
      return FALSE;
    if (!gst_bit_reader_get_bits_uint8 (br, &pseudo_surround_enable, 1))
      return FALSE;
  }

  GST_LOG_OBJECT (aacparse,
      "Matrix mixdown %spresent (index %d, pseudo-surround %s)",
      matrix_mixdown_idx_present ? "" : "not ", matrix_mixdown_idx,
      pseudo_surround_enable ? "on" : "off");

  *channels = 0;

  for (i = 0; i < num_front_channel_elements; i++) {
    guint8 front_channel_is_cpe;
    guint8 front_channel_tag_select;

    if (!gst_bit_reader_get_bits_uint8 (br, &front_channel_is_cpe, 1))
      return FALSE;
    if (!gst_bit_reader_get_bits_uint8 (br, &front_channel_tag_select, 4))
      return FALSE;

    GST_LOG_OBJECT (aacparse, "Front channel element %d (%s, instance tag %d)",
        i, front_channel_is_cpe ? "channel pair" : "single channel",
        front_channel_tag_select);

    *channels += front_channel_is_cpe ? 2 : 1;
  }

  for (i = 0; i < num_side_channel_elements; i++) {
    guint8 side_channel_is_cpe;
    guint8 side_channel_tag_select;

    if (!gst_bit_reader_get_bits_uint8 (br, &side_channel_is_cpe, 1))
      return FALSE;
    if (!gst_bit_reader_get_bits_uint8 (br, &side_channel_tag_select, 4))
      return FALSE;

    GST_LOG_OBJECT (aacparse, "Side channel element %d (%s, instance tag %d)",
        i, side_channel_is_cpe ? "channel pair" : "single channel",
        side_channel_tag_select);

    *channels += side_channel_is_cpe ? 2 : 1;
  }

  for (i = 0; i < num_back_channel_elements; i++) {
    guint8 back_channel_is_cpe;
    guint8 back_channel_tag_select;

    if (!gst_bit_reader_get_bits_uint8 (br, &back_channel_is_cpe, 1))
      return FALSE;
    if (!gst_bit_reader_get_bits_uint8 (br, &back_channel_tag_select, 4))
      return FALSE;

    GST_LOG_OBJECT (aacparse, "Back channel element %d (%s, instance tag %d)",
        i, back_channel_is_cpe ? "channel pair" : "single channel",
        back_channel_tag_select);

    *channels += back_channel_is_cpe ? 2 : 1;
  }

  for (i = 0; i < num_lfe_channel_elements; i++) {
    guint8 lfe_channel_tag_select;

    if (!gst_bit_reader_get_bits_uint8 (br, &lfe_channel_tag_select, 4))
      return FALSE;

    GST_LOG_OBJECT (aacparse, "LFE channel element %d (instance tag %d)",
        i, lfe_channel_tag_select);

    *channels += 1;
  }

  for (i = 0; i < num_assoc_data_elements; i++) {
    guint8 assoc_data_tag_select;

    if (!gst_bit_reader_get_bits_uint8 (br, &assoc_data_tag_select, 4))
      return FALSE;

    GST_LOG_OBJECT (aacparse, "Assoc data element %d (instance tag %d)",
        i, assoc_data_tag_select);
  }

  for (i = 0; i < num_valid_cc_elements; i++) {
    guint8 cc_element_is_ind_sw;
    guint8 valid_cc_element_tag_select;

    if (!gst_bit_reader_get_bits_uint8 (br, &cc_element_is_ind_sw, 1))
      return FALSE;
    if (!gst_bit_reader_get_bits_uint8 (br, &valid_cc_element_tag_select, 4))
      return FALSE;

    GST_LOG_OBJECT (aacparse,
        "Valid CC element %d (%sindependently switched, instance tag %d)",
        i, cc_element_is_ind_sw ? "" : "not ", valid_cc_element_tag_select);
  }

  pce_bits = gst_bit_reader_get_pos (br) - start_pos;
  pce_whole_bytes = pce_bits / 8;
  pce_trailing_bits = pce_bits % 8;

  /* byte_alignment():
   * "For PCEs within a raw_data_block(), align with respect to the first
   *  bit of the raw_data_block(). For PCEs within the adif_header(),
   *  align with respect to the first bit of the header."
   */
  GST_LOG_OBJECT (aacparse, "%u PCE bits so far", pce_bits);

  g_free (aacparse->pce);
  aacparse->pce = g_malloc0 (pce_whole_bytes + (pce_trailing_bits ? 1 : 0));
  aacparse->pce_bits = pce_bits;

  gst_bit_reader_set_pos (br, start_pos);

  for (i = 0; i < pce_whole_bytes; i++) {
    if (!gst_bit_reader_get_bits_uint8 (br, &aacparse->pce[i], 8))
      return FALSE;
  }

  if (pce_trailing_bits) {
    guint8 trailing_byte;

    if (!gst_bit_reader_get_bits_uint8 (br, &trailing_byte, pce_trailing_bits))
      return FALSE;

    /* Shift padding to end */
    trailing_byte <<= 8 - pce_trailing_bits;

    aacparse->pce[pce_whole_bytes] = trailing_byte;
  }

  GST_LOG_OBJECT (aacparse, "Saved PCE (%u bits)", pce_bits);

  /* byte_alignment():
   * "For PCEs within a raw_data_block(), align with respect to the first
   *  bit of the raw_data_block(). For PCEs within the adif_header(),
   *  align with respect to the first bit of the header."
   */
  if (br->bit > 0) {
    GST_LOG_OBJECT (aacparse, "Expecting %u alignment bits", 8 - br->bit);
    if (!gst_bit_reader_skip_to_byte (br))
      return FALSE;
  }

  if (!gst_bit_reader_get_bits_uint8 (br, &comment_field_bytes, 8))
    return FALSE;

  GST_LOG_OBJECT (aacparse, "%d comment field bytes", comment_field_bytes);

  g_clear_pointer (&aacparse->pce_comment, g_free);
  aacparse->pce_comment_bytes = comment_field_bytes;

  if (comment_field_bytes) {
    /* Null-terminate for printing */
    aacparse->pce_comment = g_malloc0 (comment_field_bytes + 1);

    for (i = 0; i < comment_field_bytes; i++) {
      if (!gst_bit_reader_get_bits_uint8 (br, &aacparse->pce_comment[i], 8))
        return FALSE;
    }

    GST_LOG_OBJECT (aacparse, "Saved PCE comment: %s", aacparse->pce_comment);
  }

  end_pos = gst_bit_reader_get_pos (br);
  GST_LOG_OBJECT (aacparse,
      "Finished parsing PCE at pos %u (%u bits, %d channels)",
      end_pos, end_pos - start_pos, *channels);

  return TRUE;
}

static gboolean
gst_aac_parse_ga_specific_config (GstAacParse * aacparse, GstBitReader * br,
    gint * frame_samples, gint * channels, guint8 channel_configuration,
    guint8 audio_object_type)
{
  /* ISO/IEC 14496-3 Table 4.1 - Syntax of GASpecificConfig() */
  guint8 frame_length_flag;
  guint8 depends_on_core_coder;
  guint8 extension_flag;
  gint frame_length;

  GST_LOG_OBJECT (aacparse, "Started parsing GASpecificConfig at pos %u",
      gst_bit_reader_get_pos (br));

  if (!gst_bit_reader_get_bits_uint8 (br, &frame_length_flag, 1))
    return FALSE;
  if (!gst_bit_reader_get_bits_uint8 (br, &depends_on_core_coder, 1))
    return FALSE;

  switch (audio_object_type) {
    case 23:                   /* ER AAC LD */
      frame_length = frame_length_flag ? 480 : 512;
      break;
    case 3:                    /* AAC SSR */
      frame_length = 256;
      break;
    default:
      frame_length = frame_length_flag ? 960 : 1024;
      break;
  }

  GST_LOG_OBJECT (aacparse, "Frame length %d, core coder %sused",
      frame_length, depends_on_core_coder ? "" : "not ");

  if (frame_samples)
    *frame_samples = frame_length;

  if (depends_on_core_coder) {
    guint16 core_coder_delay;

    if (!gst_bit_reader_get_bits_uint16 (br, &core_coder_delay, 14))
      return FALSE;
    GST_LOG_OBJECT (aacparse, "Core coder delay %d samples", core_coder_delay);
  }

  if (!gst_bit_reader_get_bits_uint8 (br, &extension_flag, 1))
    return FALSE;
  GST_LOG_OBJECT (aacparse, "Extension flag %d", extension_flag);

  if (!channel_configuration) {
    if (!gst_aac_parse_program_config_element (aacparse, br, channels))
      return FALSE;
  }

  if (audio_object_type == 6 || audio_object_type == 20) {
    guint8 layer_nr;

    if (!gst_bit_reader_get_bits_uint8 (br, &layer_nr, 3))
      return FALSE;
    GST_LOG_OBJECT (aacparse, "Layer number %d", layer_nr);
  }

  if (extension_flag) {
    guint8 extension_flag_3;

    if (audio_object_type == 22) {
      guint8 num_of_sub_frame;
      guint16 layer_length;

      if (!gst_bit_reader_get_bits_uint8 (br, &num_of_sub_frame, 5))
        return FALSE;
      if (!gst_bit_reader_get_bits_uint16 (br, &layer_length, 11))
        return FALSE;

      GST_LOG_OBJECT (aacparse,
          "%d sub frames, average large-step layer length %d bytes",
          num_of_sub_frame, layer_length);
    }

    switch (audio_object_type) {
      case 17:
      case 19:
      case 20:
      case 23:{
        guint8 aac_section_data_resilience_flag;
        guint8 aac_scalefactor_data_resilience_flag;
        guint8 aac_spectral_data_resilience_flag;

        if (!gst_bit_reader_get_bits_uint8 (br,
                &aac_section_data_resilience_flag, 1))
          return FALSE;
        if (!gst_bit_reader_get_bits_uint8 (br,
                &aac_scalefactor_data_resilience_flag, 1))
          return FALSE;
        if (!gst_bit_reader_get_bits_uint8 (br,
                &aac_spectral_data_resilience_flag, 1))
          return FALSE;

        GST_LOG_OBJECT (aacparse,
            "Resilience flags: section %d, scalefactor %d, spectral %d",
            aac_section_data_resilience_flag,
            aac_scalefactor_data_resilience_flag,
            aac_spectral_data_resilience_flag);
      }
      default:
        break;
    }

    if (!gst_bit_reader_get_bits_uint8 (br, &extension_flag_3, 1))
      return FALSE;
    GST_LOG_OBJECT (aacparse, "Future extension flag %d", extension_flag);
  }

  GST_LOG_OBJECT (aacparse, "Finished parsing GASpecificConfig at pos %u",
      gst_bit_reader_get_pos (br));

  return TRUE;
}

static gboolean
gst_aac_parse_read_audio_specific_config (GstAacParse * aacparse,
    GstBitReader * br, gint * object_type, gint * sample_rate, gint * channels,
    gint * frame_samples)
{
  /* ISO/IEC 14496-3 Table 1.15 - Syntax of AudioSpecificConfig() */
  guint8 audio_object_type, extension_audio_object_type;
  guint8 channel_configuration, extension_channel_configuration;
  guint8 sbr_present_flag = -1, ps_present_flag = -1;

  GST_LOG_OBJECT (aacparse, "Started parsing AudioSpecificConfig at pos %u",
      gst_bit_reader_get_pos (br));

  if (!gst_aac_parse_get_audio_object_type (aacparse, br, &audio_object_type))
    return FALSE;
  if (object_type)
    *object_type = audio_object_type;

  if (!gst_aac_parse_get_audio_sample_rate (aacparse, br, sample_rate))
    return FALSE;

  if (!gst_bit_reader_get_bits_uint8 (br, &channel_configuration, 4))
    return FALSE;
  *channels = loas_channels_table[channel_configuration];
  GST_LOG_OBJECT (aacparse, "channel_configuration: %d", channel_configuration);

  if (audio_object_type == 5 || audio_object_type == 29) {
    extension_audio_object_type = 5;
    sbr_present_flag = 1;
    if (audio_object_type == 29) {
      ps_present_flag = 1;

      /* Parametric stereo. If we have a one-channel configuration, we can
       * override it to stereo */
      if (*channels == 1)
        *channels = 2;
    }

    GST_LOG_OBJECT (aacparse, "SBR %spresent, parametric stereo %spresent",
        sbr_present_flag == 1 ? "" : "not ",
        ps_present_flag == 1 ? "" : "not ");

    GST_LOG_OBJECT (aacparse, "Rereading sampling rate (was %d)", *sample_rate);
    if (!gst_aac_parse_get_audio_sample_rate (aacparse, br, sample_rate))
      return FALSE;

    GST_LOG_OBJECT (aacparse, "Rereading object type (was %d)",
        audio_object_type);
    if (!gst_aac_parse_get_audio_object_type (aacparse, br, &audio_object_type))
      return FALSE;

    if (audio_object_type == 22) {
      if (!gst_bit_reader_get_bits_uint8 (br, &extension_channel_configuration,
              4))
        return FALSE;
      GST_LOG_OBJECT (aacparse, "extension channel_configuration: %d",
          extension_channel_configuration);
      *channels = loas_channels_table[extension_channel_configuration];
      if (!*channels)
        return FALSE;
    }
  } else {
    extension_audio_object_type = 0;
  }

  GST_LOG_OBJECT (aacparse, "Extension audio object type %d",
      extension_audio_object_type);

  GST_LOG_OBJECT (aacparse, "So far: %d Hz, %d channels",
      *sample_rate, *channels);

  /* Names from Table 1.3 - Audio Profiles definition */
  switch (audio_object_type) {
    case 0:                    /* Null */
      GST_WARNING_OBJECT (aacparse, "Got null audio object type");
      break;
    case 1:                    /* AAC main */
    case 2:                    /* AAC LC */
    case 3:                    /* AAC SSR */
    case 4:                    /* AAC LTP */
    case 6:                    /* AAC Scalable */
    case 7:                    /* TwinVQ */
    case 17:                   /* ER AAC LC */
    case 19:                   /* ER AAC LTP */
    case 20:                   /* ER AAC Scalable */
    case 21:                   /* ER TwinVQ */
    case 22:                   /* ER BSAC */
    case 23:                   /* ER AAC LD */
      if (!gst_aac_parse_ga_specific_config (aacparse, br, frame_samples,
              channels, channel_configuration, audio_object_type)) {
        GST_WARNING_OBJECT (aacparse, "Error parsing GASpecificConfig");
        return FALSE;
      }
      break;
    case 8:                    /* CELP */
      GST_WARNING_OBJECT (aacparse, "CelpSpecificConfig not supported");
      break;
    case 9:                    /* HVXC */
      GST_WARNING_OBJECT (aacparse, "HvxcSpecificConfig not supported");
      break;
    case 12:                   /* TTSI */
      GST_WARNING_OBJECT (aacparse, "TTSSpecificConfig not supported");
      break;
    case 13:                   /* Main synthetic */
    case 14:                   /* Wavetable synthesis */
    case 15:                   /* General MIDI */
    case 16:                   /* Algorithmic Synthesis and Audio FX */
      GST_WARNING_OBJECT (aacparse,
          "StructuredAudioSpecificConfig not supported");
      break;
    case 24:                   /* ER CELP */
      GST_WARNING_OBJECT (aacparse,
          "ErrorResilientCelpSpecificConfig not supported");
      break;
    case 25:                   /* ER HVXC */
      GST_WARNING_OBJECT (aacparse,
          "ErrorResilientHvxcSpecificConfig not supported");
      break;
    case 26:                   /* ER HILN */
    case 27:                   /* ER Parametric */
      GST_WARNING_OBJECT (aacparse, "ParametricSpecificConfig not supported");
      break;
    case 28:                   /* SSC */
      GST_WARNING_OBJECT (aacparse, "SSCSpecificConfig not supported");
      break;
    case 30:{                  /* MPEG Surround */
      guint8 sac_payload_embedding;

      if (!gst_bit_reader_get_bits_uint8 (br, &sac_payload_embedding, 1))
        return FALSE;
      GST_WARNING_OBJECT (aacparse,
          "SpatialSpecificConfig not supported (sacPayloadEmbedding %d)",
          sac_payload_embedding);
      break;
    }
    case 32:                   /* Layer-1 */
    case 33:                   /* Layer-2 */
    case 34:                   /* Layer-3 */
      GST_WARNING_OBJECT (aacparse, "MPEG_1_2_SpecificConfig not supported");
      break;
    case 35:                   /* DST */
      GST_WARNING_OBJECT (aacparse, "DSTSpecificConfig not supported");
      break;
    case 36:{                  /* ALS */
      guint8 fill_bits;

      if (!gst_bit_reader_get_bits_uint8 (br, &fill_bits, 5))
        return FALSE;
      GST_WARNING_OBJECT (aacparse,
          "ALSSpecificConfig not supported (fill bits %d)", fill_bits);
      break;
    }
    case 37:                   /* SLS */
    case 38:                   /* SLS non-core */
      GST_WARNING_OBJECT (aacparse, "SLSSpecificConfig not supported");
      break;
    case 39:                   /* ER AAC ELD */
      GST_WARNING_OBJECT (aacparse, "ELDSpecificConfig not supported");
      break;
    case 40:                   /* SMR Simple */
    case 41:                   /* SMR Main */
      GST_WARNING_OBJECT (aacparse,
          "SymbolicMusicSpecificConfig not supported");
      break;

    case 5:                    /* SBR */
    case 29:                   /* PS */
      break;

    case 10:                   /* (reserved) */
    case 11:                   /* (reserved) */
    case 18:                   /* (reserved) */
    case 31:                   /* (escape) */
    default:
      GST_WARNING_OBJECT (aacparse, "Unexpected audio object type %d",
          audio_object_type);
      break;
  }

  switch (audio_object_type) {
    case 17:
    case 19:
    case 20:
    case 21:
    case 22:
    case 23:
    case 24:
    case 25:
    case 26:
    case 27:
    case 39:{
      guint8 ep_config;

      if (!gst_bit_reader_get_bits_uint8 (br, &ep_config, 2))
        return FALSE;

      if (ep_config == 2 || ep_config == 3) {
        GST_WARNING_OBJECT (aacparse,
            "ErrorProtectionSpecificConfig not supported");
        break;
      }

      GST_LOG_OBJECT (aacparse, "Error robust configuration %d", ep_config);
    }
    default:
      break;
  }

  if (extension_audio_object_type != 5 &&
      gst_bit_reader_get_remaining (br) >= 16) {
    guint16 sync_extension_type;

    if (!gst_bit_reader_get_bits_uint16 (br, &sync_extension_type, 11))
      return FALSE;
    GST_LOG_OBJECT (aacparse, "Sync extension type %d", sync_extension_type);

    if (sync_extension_type == 0x2b7) {
      if (!gst_aac_parse_get_audio_object_type (aacparse, br,
              &extension_audio_object_type))
        return FALSE;

      if (extension_audio_object_type == 5) {
        if (!gst_bit_reader_get_bits_uint8 (br, &sbr_present_flag, 1))
          return FALSE;
        GST_LOG_OBJECT (aacparse, "SBR %spresent",
            sbr_present_flag == 1 ? "" : "not ");

        if (sbr_present_flag == 1) {
          GST_LOG_OBJECT (aacparse, "Rereading sampling rate (was %d)",
              *sample_rate);
          if (!gst_aac_parse_get_audio_sample_rate (aacparse, br, sample_rate))
            return FALSE;

          if (gst_bit_reader_get_remaining (br) >= 12) {
            if (!gst_bit_reader_get_bits_uint16 (br, &sync_extension_type, 11))
              return FALSE;
            GST_LOG_OBJECT (aacparse, "Sync extension type %d",
                sync_extension_type);

            if (sync_extension_type == 0x548) {
              if (!gst_bit_reader_get_bits_uint8 (br, &ps_present_flag, 1))
                return FALSE;
              GST_LOG_OBJECT (aacparse, "Parametric stereo %spresent",
                  ps_present_flag == 1 ? "" : "not ");

              /* Parametric stereo, again */
              if (ps_present_flag == 1 && *channels == 1)
                *channels = 2;
            }
          }
        }
      }

      if (extension_audio_object_type == 22) {
        if (!gst_bit_reader_get_bits_uint8 (br, &sbr_present_flag, 1))
          return FALSE;
        GST_LOG_OBJECT (aacparse, "SBR %spresent",
            sbr_present_flag == 1 ? "" : "not ");

        if (sbr_present_flag == 1) {
          GST_LOG_OBJECT (aacparse, "Rereading sampling rate (was %d)",
              *sample_rate);
          if (!gst_aac_parse_get_audio_sample_rate (aacparse, br, sample_rate))
            return FALSE;
        }

        if (!gst_bit_reader_get_bits_uint8 (br,
                &extension_channel_configuration, 4))
          return FALSE;
        GST_LOG_OBJECT (aacparse, "extension channel_configuration: %d",
            extension_channel_configuration);
        *channels = loas_channels_table[extension_channel_configuration];
        if (!*channels)
          return FALSE;
      }
    }
  }

  if (!*channels)
    return FALSE;

  GST_INFO_OBJECT (aacparse,
      "Finished parsing AudioSpecificConfig at pos %u (%d Hz, %d channels, %u bits remaining)",
      gst_bit_reader_get_pos (br), *sample_rate, *channels,
      gst_bit_reader_get_remaining (br));

  aacparse->last_parsed_channels = *channels;
  return TRUE;
}


static gboolean
gst_aac_parse_read_loas_config (GstAacParse * aacparse, const guint8 * data,
    guint avail, gint * sample_rate, gint * channels, gint * version)
{
  GstBitReader br;
  guint8 u8, v, vA;

  /* No version in the bitstream, but the spec has LOAS in the MPEG-4 section */
  if (version)
    *version = 4;

  gst_bit_reader_init (&br, data, avail);

  /* skip sync word (11 bits) and size (13 bits) */
  if (!gst_bit_reader_skip (&br, 11 + 13))
    return FALSE;

  /* First bit is "use last config" */
  if (!gst_bit_reader_get_bits_uint8 (&br, &u8, 1))
    return FALSE;
  if (u8) {
    GST_LOG_OBJECT (aacparse, "Frame uses previous config");
    if (!aacparse->last_parsed_sample_rate || !aacparse->last_parsed_channels) {
      GST_DEBUG_OBJECT (aacparse,
          "No previous config to use. We'll look for more data.");
      return FALSE;
    }
    *sample_rate = aacparse->last_parsed_sample_rate;
    *channels = aacparse->last_parsed_channels;
    return TRUE;
  }

  GST_DEBUG_OBJECT (aacparse, "Frame contains new config");

  /* audioMuxVersion */
  if (!gst_bit_reader_get_bits_uint8 (&br, &v, 1))
    return FALSE;
  if (v) {
    /* audioMuxVersionA */
    if (!gst_bit_reader_get_bits_uint8 (&br, &vA, 1))
      return FALSE;
  } else
    vA = 0;

  GST_LOG_OBJECT (aacparse, "v %d, vA %d", v, vA);
  if (vA == 0) {
    guint8 same_time, subframes, num_program, prog;
    if (v == 1) {
      guint32 value;
      /* taraBufferFullness */
      if (!gst_aac_parse_latm_get_value (aacparse, &br, &value))
        return FALSE;
    }
    if (!gst_bit_reader_get_bits_uint8 (&br, &same_time, 1))
      return FALSE;
    if (!gst_bit_reader_get_bits_uint8 (&br, &subframes, 6))
      return FALSE;
    if (!gst_bit_reader_get_bits_uint8 (&br, &num_program, 4))
      return FALSE;
    GST_LOG_OBJECT (aacparse, "same_time %d, subframes %d, num_program %d",
        same_time, subframes, num_program);

    for (prog = 0; prog <= num_program; ++prog) {
      guint8 num_layer, layer;
      if (!gst_bit_reader_get_bits_uint8 (&br, &num_layer, 3))
        return FALSE;
      GST_LOG_OBJECT (aacparse, "Program %d: %d layers", prog, num_layer);

      for (layer = 0; layer <= num_layer; ++layer) {
        guint8 use_same_config;
        if (prog == 0 && layer == 0) {
          use_same_config = 0;
        } else {
          if (!gst_bit_reader_get_bits_uint8 (&br, &use_same_config, 1))
            return FALSE;
        }
        if (!use_same_config) {
          if (v == 0) {
            if (!gst_aac_parse_read_audio_specific_config (aacparse, &br, NULL,
                    sample_rate, channels, NULL))
              return FALSE;
          } else {
            guint32 asc_len;
            if (!gst_aac_parse_latm_get_value (aacparse, &br, &asc_len))
              return FALSE;
            if (!gst_aac_parse_read_audio_specific_config (aacparse, &br, NULL,
                    sample_rate, channels, NULL))
              return FALSE;
            if (!gst_bit_reader_skip (&br, asc_len))
              return FALSE;
          }
        }
      }
    }
    GST_LOG_OBJECT (aacparse, "More data ignored");
  } else {
    GST_WARNING_OBJECT (aacparse, "Spec says \"TBD\"...");
    return FALSE;
  }
  return TRUE;
}

/**
 * gst_aac_parse_loas_get_frame_len:
 * @data: block of data containing a LOAS header.
 *
 * This function calculates LOAS frame length from the given header.
 *
 * Returns: size of the LOAS frame.
 */
static inline guint
gst_aac_parse_loas_get_frame_len (const guint8 * data)
{
  return (((data[1] & 0x1f) << 8) | data[2]) + 3;
}


/**
 * gst_aac_parse_check_loas_frame:
 * @aacparse: #GstAacParse.
 * @data: Data to be checked.
 * @avail: Amount of data passed.
 * @framesize: If valid LOAS frame was found, this will be set to tell the
 *             found frame size in bytes.
 * @needed_data: If frame was not found, this may be set to tell how much
 *               more data is needed in the next round to detect the frame
 *               reliably. This may happen when a frame header candidate
 *               is found but it cannot be guaranteed to be the header without
 *               peeking the following data.
 *
 * Check if the given data contains contains LOAS frame. The algorithm
 * will examine LOAS frame header and calculate the frame size. Also, another
 * consecutive LOAS frame header need to be present after the found frame.
 * Otherwise the data is not considered as a valid LOAS frame. However, this
 * "extra check" is omitted when EOS has been received. In this case it is
 * enough when data[0] contains a valid LOAS header.
 *
 * This function may set the #needed_data to indicate that a possible frame
 * candidate has been found, but more data (#needed_data bytes) is needed to
 * be absolutely sure. When this situation occurs, FALSE will be returned.
 *
 * When a valid frame is detected, this function will use
 * gst_base_parse_set_min_frame_size() function from #GstBaseParse class
 * to set the needed bytes for next frame.This way next data chunk is already
 * of correct size.
 *
 * LOAS can have three different formats, if I read the spec correctly. Only
 * one of them is supported here, as the two samples I have use this one.
 *
 * Returns: TRUE if the given data contains a valid LOAS header.
 */
static gboolean
gst_aac_parse_check_loas_frame (GstAacParse * aacparse,
    const guint8 * data, const guint avail, gboolean drain,
    guint * framesize, guint * needed_data)
{
  *needed_data = 0;

  /* 3 byte header */
  if (G_UNLIKELY (avail < 3)) {
    *needed_data = 3;
    return FALSE;
  }

  if ((data[0] == 0x56) && ((data[1] & 0xe0) == 0xe0)) {
    *framesize = gst_aac_parse_loas_get_frame_len (data);
    GST_DEBUG_OBJECT (aacparse, "Found possible %u byte LOAS frame",
        *framesize);

    /* In EOS mode this is enough. No need to examine the data further.
       We also relax the check when we have sync, on the assumption that
       if we're not looking at random data, we have a much higher chance
       to get the correct sync, and this avoids losing two frames when
       a single bit corruption happens. */
    if (drain || !GST_BASE_PARSE_LOST_SYNC (aacparse)) {
      return TRUE;
    }

    if (*framesize + LOAS_MAX_SIZE > avail) {
      /* We have found a possible frame header candidate, but can't be
         sure since we don't have enough data to check the next frame */
      GST_DEBUG ("NEED MORE DATA: we need %d, available %d",
          *framesize + LOAS_MAX_SIZE, avail);
      *needed_data = *framesize + LOAS_MAX_SIZE;
      gst_base_parse_set_min_frame_size (GST_BASE_PARSE (aacparse),
          *framesize + LOAS_MAX_SIZE);
      return FALSE;
    }

    if ((data[*framesize] == 0x56) && ((data[*framesize + 1] & 0xe0) == 0xe0)) {
      guint nextlen = gst_aac_parse_loas_get_frame_len (data + (*framesize));

      GST_LOG ("LOAS frame found, len: %d bytes", *framesize);
      gst_base_parse_set_min_frame_size (GST_BASE_PARSE (aacparse),
          nextlen + LOAS_MAX_SIZE);
      return TRUE;
    } else {
      GST_DEBUG_OBJECT (aacparse, "That was a false positive");
    }
  }
  return FALSE;
}

/* caller ensure sufficient data */
static inline void
gst_aac_parse_parse_adts_header (GstAacParse * aacparse, const guint8 * data,
    const guint avail, gint * rate, gint * channels, gint * object,
    gint * version)
{
  g_assert (avail >= 4);

  if (rate) {
    gint sr_idx = (data[2] & 0x3c) >> 2;

    *rate = gst_codec_utils_aac_get_sample_rate_from_index (sr_idx);
  }
  if (channels) {
    guint16 channel_index;
    channel_index = ((data[2] & 0x01) << 2) | ((data[3] & 0xc0) >> 6);
    *channels = loas_channels_table[channel_index];
  }

  if (version)
    *version = (data[1] & 0x08) ? 2 : 4;
  if (object)
    *object = ((data[2] & 0xc0) >> 6) + 1;

  if (channels && *channels == 0) {
    GstBitReader br;
    guint8 id_syn_ele;

    g_assert (avail >= 8);
    gst_bit_reader_init (&br, &data[7], avail - 7);

    if (!gst_bit_reader_get_bits_uint8 (&br, &id_syn_ele, 3))
      goto err;

    if (id_syn_ele != 5 /* ID_PCE */ ) {
      GST_ERROR_OBJECT (aacparse,
          "ADTS has 0 channels but first element is not PCE");
      return;
    }

    if (!gst_aac_parse_program_config_element (aacparse, &br, channels))
      goto err;
  }

  return;

err:
  GST_ERROR_OBJECT (aacparse, "Error reading ADTS header");
}

/**
 * gst_aac_parse_detect_stream:
 * @aacparse: #GstAacParse.
 * @data: A block of data that needs to be examined for stream characteristics.
 * @avail: Size of the given datablock.
 * @framesize: If valid stream was found, this will be set to tell the
 *             first frame size in bytes.
 * @skipsize: If valid stream was found, this will be set to tell the first
 *            audio frame position within the given data.
 *
 * Examines the given piece of data and try to detect the format of it. It
 * checks for "ADIF" header (in the beginning of the clip) and ADTS frame
 * header. If the stream is detected, TRUE will be returned and #framesize
 * is set to indicate the found frame size. Additionally, #skipsize might
 * be set to indicate the number of bytes that need to be skipped, a.k.a. the
 * position of the frame inside given data chunk.
 *
 * Returns: TRUE on success.
 */
static gboolean
gst_aac_parse_detect_stream (GstAacParse * aacparse,
    const guint8 * data, const guint avail, gboolean drain,
    guint * framesize, gint * skipsize)
{
  gboolean found = FALSE;
  guint need_data_adts = 0, need_data_loas;
  guint i = 0;

  GST_DEBUG_OBJECT (aacparse, "Parsing header data");

  /* FIXME: No need to check for ADIF if we are not in the beginning of the
     stream */

  /* Can we even parse the header? */
  if (avail < MAX (ADTS_MAX_SIZE, LOAS_MAX_SIZE)) {
    GST_DEBUG_OBJECT (aacparse, "Not enough data to check");
    return FALSE;
  }

  for (i = 0; i < avail - 4; i++) {
    if (((data[i] == 0xff) && ((data[i + 1] & 0xf6) == 0xf0)) ||
        ((data[i] == 0x56) && ((data[i + 1] & 0xe0) == 0xe0)) ||
        strncmp ((char *) data + i, "ADIF", 4) == 0) {
      GST_DEBUG_OBJECT (aacparse, "Found signature at offset %u", i);
      found = TRUE;

      if (i) {
        /* Trick: tell the parent class that we didn't find the frame yet,
           but make it skip 'i' amount of bytes. Next time we arrive
           here we have full frame in the beginning of the data. */
        *skipsize = i;
        return FALSE;
      }
      break;
    }
  }
  if (!found) {
    if (i)
      *skipsize = i;
    return FALSE;
  }

  if (gst_aac_parse_check_adts_frame (aacparse, data, avail, drain,
          framesize, &need_data_adts)) {
    gint rate, channels;

    GST_INFO ("ADTS ID: %d, framesize: %d", (data[1] & 0x08) >> 3, *framesize);

    gst_aac_parse_parse_adts_header (aacparse, data, avail, &rate, &channels,
        &aacparse->object_type, &aacparse->mpegversion);

    if (!channels || !framesize) {
      GST_DEBUG_OBJECT (aacparse, "impossible ADTS configuration");
      return FALSE;
    }

    aacparse->header_type = DSPAAC_HEADER_ADTS;
    gst_base_parse_set_frame_rate (GST_BASE_PARSE (aacparse), rate,
        aacparse->frame_samples, 2, 2);

    GST_DEBUG ("ADTS: samplerate %d, channels %d, objtype %d, version %d",
        rate, channels, aacparse->object_type, aacparse->mpegversion);

    gst_base_parse_set_syncable (GST_BASE_PARSE (aacparse), TRUE);

    return TRUE;
  }

  if (gst_aac_parse_check_loas_frame (aacparse, data, avail, drain,
          framesize, &need_data_loas)) {
    gint rate = 0, channels = 0;

    GST_INFO ("LOAS, framesize: %d", *framesize);

    aacparse->header_type = DSPAAC_HEADER_LOAS;

    if (!gst_aac_parse_read_loas_config (aacparse, data, avail, &rate,
            &channels, &aacparse->mpegversion)) {
      /* This is pretty normal when skipping data at the start of
       * random stream (MPEG-TS capture for example) */
      GST_LOG_OBJECT (aacparse, "Error reading LOAS config");
      return FALSE;
    }

    if (rate && channels) {
      gst_base_parse_set_frame_rate (GST_BASE_PARSE (aacparse), rate,
          aacparse->frame_samples, 2, 2);

      /* Don't store the sample rate and channels yet -
       * this is just format detection. */
      GST_DEBUG ("LOAS: samplerate %d, channels %d, objtype %d, version %d",
          rate, channels, aacparse->object_type, aacparse->mpegversion);
    }

    gst_base_parse_set_syncable (GST_BASE_PARSE (aacparse), TRUE);

    return TRUE;
  }

  if (need_data_adts || need_data_loas) {
    /* This tells the parent class not to skip any data */
    *skipsize = 0;
    return FALSE;
  }

  if (avail < ADIF_MAX_SIZE)
    return FALSE;

  if (memcmp (data + i, "ADIF", 4) == 0) {
    const guint8 *adif;
    int skip_size = 0;
    int bitstream_type;
    int sr_idx;
    GstCaps *sinkcaps;

    aacparse->header_type = DSPAAC_HEADER_ADIF;
    aacparse->mpegversion = 4;

    /* Skip the "ADIF" bytes */
    adif = data + i + 4;

    /* copyright string */
    if (adif[0] & 0x80)
      skip_size += 9;           /* skip 9 bytes */

    bitstream_type = adif[0 + skip_size] & 0x10;
    aacparse->bitrate =
        ((unsigned int) (adif[0 + skip_size] & 0x0f) << 19) |
        ((unsigned int) adif[1 + skip_size] << 11) |
        ((unsigned int) adif[2 + skip_size] << 3) |
        ((unsigned int) adif[3 + skip_size] & 0xe0);

    /* CBR */
    if (bitstream_type == 0) {
#if 0
      /* Buffer fullness parsing. Currently not needed... */
      guint num_elems = 0;
      guint fullness = 0;

      num_elems = (adif[3 + skip_size] & 0x1e);
      GST_INFO ("ADIF num_config_elems: %d", num_elems);

      fullness = ((unsigned int) (adif[3 + skip_size] & 0x01) << 19) |
          ((unsigned int) adif[4 + skip_size] << 11) |
          ((unsigned int) adif[5 + skip_size] << 3) |
          ((unsigned int) (adif[6 + skip_size] & 0xe0) >> 5);

      GST_INFO ("ADIF buffer fullness: %d", fullness);
#endif
      aacparse->object_type = ((adif[6 + skip_size] & 0x01) << 1) |
          ((adif[7 + skip_size] & 0x80) >> 7);
      sr_idx = (adif[7 + skip_size] & 0x78) >> 3;
    }
    /* VBR */
    else {
      aacparse->object_type = (adif[4 + skip_size] & 0x18) >> 3;
      sr_idx = ((adif[4 + skip_size] & 0x07) << 1) |
          ((adif[5 + skip_size] & 0x80) >> 7);
    }

    /* FIXME: This gives totally wrong results. Duration calculation cannot
       be based on this */
    aacparse->sample_rate =
        gst_codec_utils_aac_get_sample_rate_from_index (sr_idx);

    /* baseparse is not given any fps,
     * so it will give up on timestamps, seeking, etc */

    /* FIXME: Can we assume this? */
    aacparse->channels = 2;

    GST_INFO ("ADIF: br=%d, samplerate=%d, objtype=%d",
        aacparse->bitrate, aacparse->sample_rate, aacparse->object_type);

    gst_base_parse_set_min_frame_size (GST_BASE_PARSE (aacparse), 512);

    /* arrange for metadata and get out of the way */
    sinkcaps = gst_pad_get_current_caps (GST_BASE_PARSE_SINK_PAD (aacparse));
    gst_aac_parse_set_src_caps (aacparse, sinkcaps);
    if (sinkcaps)
      gst_caps_unref (sinkcaps);

    /* not syncable, not easily seekable (unless we push data from start */
    gst_base_parse_set_syncable (GST_BASE_PARSE_CAST (aacparse), FALSE);
    gst_base_parse_set_passthrough (GST_BASE_PARSE_CAST (aacparse), TRUE);
    gst_base_parse_set_average_bitrate (GST_BASE_PARSE_CAST (aacparse), 0);

    *framesize = avail;
    return TRUE;
  }

  /* This should never happen */
  return FALSE;
}

/**
 * gst_aac_parse_get_audio_profile_object_type
 * @aacparse: #GstAacParse.
 *
 * Gets the MPEG-2 profile or the MPEG-4 object type value corresponding to the
 * mpegversion and profile of @aacparse's src pad caps, according to the
 * values defined by table 1.A.11 in ISO/IEC 14496-3.
 *
 * Returns: the profile or object type value corresponding to @aacparse's src
 * pad caps, if such a value exists; otherwise G_MAXUINT8.
 */
static guint8
gst_aac_parse_get_audio_profile_object_type (GstAacParse * aacparse)
{
  GstCaps *srccaps;
  GstStructure *srcstruct;
  const gchar *profile;
  guint8 ret;

  srccaps = gst_pad_get_current_caps (GST_BASE_PARSE_SRC_PAD (aacparse));
  if (G_UNLIKELY (srccaps == NULL)) {
    return G_MAXUINT8;
  }

  srcstruct = gst_caps_get_structure (srccaps, 0);
  profile = gst_structure_get_string (srcstruct, "profile");
  if (G_UNLIKELY (profile == NULL)) {
    gst_caps_unref (srccaps);
    return G_MAXUINT8;
  }

  if (g_strcmp0 (profile, "main") == 0) {
    ret = (guint8) 0U;
  } else if (g_strcmp0 (profile, "lc") == 0) {
    ret = (guint8) 1U;
  } else if (g_strcmp0 (profile, "ssr") == 0) {
    ret = (guint8) 2U;
  } else if (g_strcmp0 (profile, "ltp") == 0) {
    if (G_LIKELY (aacparse->mpegversion == 4))
      ret = (guint8) 3U;
    else
      ret = G_MAXUINT8;         /* LTP Object Type allowed only for MPEG-4 */
  } else {
    ret = G_MAXUINT8;
  }

  gst_caps_unref (srccaps);
  return ret;
}

/**
 * gst_aac_parse_get_audio_channel_configuration
 * @num_channels: number of audio channels.
 *
 * Gets the Channel Configuration value, as defined by table 1.19 in ISO/IEC
 * 14496-3, for a given number of audio channels.
 *
 * Returns: the Channel Configuration value corresponding to @num_channels, if
 * such a value exists; otherwise G_MAXUINT8.
 */
static guint8
gst_aac_parse_get_audio_channel_configuration (gint num_channels)
{
  if (num_channels >= 1 && num_channels <= 6)   /* Mono up to & including 5.1 */
    return (guint8) num_channels;
  else if (num_channels == 8)   /* 7.1 */
    return (guint8) 7U;
  else
    return G_MAXUINT8;

  /* FIXME: Add support for configurations 11, 12 and 14 from
   * ISO/IEC 14496-3:2009/PDAM 4 based on the actual channel layout
   */
}

/**
 * gst_aac_parse_prepend_adts_headers:
 * @aacparse: #GstAacParse.
 * @frame: raw AAC frame to which ADTS headers shall be prepended.
 *
 * Prepends ADTS headers to a raw AAC audio frame.
 *
 * Returns: TRUE if ADTS headers were successfully prepended; FALSE otherwise.
 */
static gboolean
gst_aac_parse_prepend_adts_headers (GstAacParse * aacparse,
    GstBaseParseFrame * frame)
{
  GstMemory *mem;
  guint8 *adts_prefix;
  gsize buf_size;
  gsize frame_size;
  guint8 id, profile, channel_configuration, sampling_frequency_index;
  guint pce_bytes = 0, adts_prefix_length;
  GstBitWriter bw;
  gint i;

  id = (aacparse->mpegversion == 4) ? 0 : 1;

  profile = gst_aac_parse_get_audio_profile_object_type (aacparse);
  if (profile == G_MAXUINT8) {
    GST_ERROR_OBJECT (aacparse, "Unsupported audio profile or object type");
    return FALSE;
  }
  channel_configuration =
      gst_aac_parse_get_audio_channel_configuration (aacparse->channels);
  if (channel_configuration == G_MAXUINT8) {
    GST_ERROR_OBJECT (aacparse, "Unsupported number of channels");
    return FALSE;
  }

  sampling_frequency_index = i =
      gst_codec_utils_aac_get_index_from_sample_rate (aacparse->sample_rate);
  if (i == -1) {
    GST_ERROR_OBJECT (aacparse, "Unsupported sampling frequency");
    return FALSE;
  }

  if (aacparse->pce_bits) {
    /* raw_data_block for PCE:
     * id_syn_ele (3 bits) + program_config_element + alignment +
     *   comment_field_bytes (8 bits) + comment_field_data */
    pce_bytes = (3 + aacparse->pce_bits + 7 + 8) / 8;
    pce_bytes += aacparse->pce_comment_bytes;
    channel_configuration = 0;
  } else if (channel_configuration == 0) {
    GST_ERROR_OBJECT (aacparse, "Channel configuration 0 without PCE");
    return FALSE;
  }

  adts_prefix_length = ADTS_HEADERS_LENGTH + pce_bytes;

  buf_size = gst_buffer_get_size (frame->buffer);
  frame_size = adts_prefix_length + buf_size;

  if (G_UNLIKELY (frame_size >= 0x4000)) {
    GST_ERROR_OBJECT (aacparse, "Frame size is too big for ADTS");
    return FALSE;
  }

  adts_prefix = (guint8 *) g_malloc0 (adts_prefix_length);
  gst_bit_writer_init_with_data (&bw, adts_prefix, adts_prefix_length, FALSE);

  /* Note: no error correction bits are added to the resulting ADTS frames */

  /* Table 1.A.6 - Syntax of adts_fixed_header() */
  if (!gst_bit_writer_put_bits_uint16 (&bw, 0xFFF, 12)) /* syncword */
    goto err_free;
  if (!gst_bit_writer_put_bits_uint8 (&bw, id, 1))
    goto err_free;
  if (!gst_bit_writer_put_bits_uint8 (&bw, 0, 2))       /* layer */
    goto err_free;
  if (!gst_bit_writer_put_bits_uint8 (&bw, 1, 1))       /* protection_absent */
    goto err_free;
  if (!gst_bit_writer_put_bits_uint8 (&bw, profile, 2))
    goto err_free;
  if (!gst_bit_writer_put_bits_uint8 (&bw, sampling_frequency_index, 4))
    goto err_free;
  if (!gst_bit_writer_put_bits_uint8 (&bw, 1, 1))       /* private_bit */
    goto err_free;
  if (!gst_bit_writer_put_bits_uint8 (&bw, channel_configuration, 3))
    goto err_free;
  if (!gst_bit_writer_put_bits_uint8 (&bw, 1, 1))       /* original_copy */
    goto err_free;
  if (!gst_bit_writer_put_bits_uint8 (&bw, 1, 1))       /* home */
    goto err_free;

  /* Table 1.A.7 - Syntax of adts_variable_header() */
  if (!gst_bit_writer_put_bits_uint8 (&bw, 0, 1))       /* copyright_identification_bit */
    goto err_free;
  if (!gst_bit_writer_put_bits_uint8 (&bw, 0, 1))       /* copyright_identification_start */
    goto err_free;
  if (!gst_bit_writer_put_bits_uint16 (&bw, frame_size, 13))
    goto err_free;
  if (!gst_bit_writer_put_bits_uint16 (&bw, 0x7FF, 11)) /* adts_buffer_fullness */
    goto err_free;
  if (!gst_bit_writer_put_bits_uint8 (&bw, 0, 2))       /* number_of_raw_data_blocks_in_frame */
    goto err_free;

  g_assert (gst_bit_writer_get_size (&bw) == ADTS_HEADERS_LENGTH * 8);

  /* We're byte-aligned now */

  if (pce_bytes) {
    guint pce_whole_bytes, pce_trailing_bits, i;

    pce_whole_bytes = aacparse->pce_bits / 8;
    pce_trailing_bits = aacparse->pce_bits % 8;

    GST_TRACE_OBJECT (aacparse, "Writing PCE (%u bytes)", pce_bytes);

    if (!gst_bit_writer_put_bits_uint8 (&bw, 5, 3))     /* id_syn_ele = ID_PCE */
      goto err_free;

    /* FIXME: gst_bit_writer_put_bytes asserts when unaligned */
    for (i = 0; i < pce_whole_bytes; i++) {
      if (!gst_bit_writer_put_bits_uint8 (&bw, aacparse->pce[i], 8))
        goto err_free;
    }

    if (pce_trailing_bits) {
      guint8 trailing_byte = aacparse->pce[pce_whole_bytes];

      /* Unshift padding from end */
      trailing_byte >>= 8 - pce_trailing_bits;

      if (!gst_bit_writer_put_bits_uint8 (&bw, trailing_byte,
              pce_trailing_bits))
        goto err_free;
    }

    /* byte_alignment():
     * "For PCEs within a raw_data_block(), align with respect to the first
     *  bit of the raw_data_block(). For PCEs within the adif_header(),
     *  align with respect to the first bit of the header."
     */
    if (!gst_bit_writer_align_bytes (&bw, 0))
      goto err_free;

    if (!gst_bit_writer_put_bits_uint8 (&bw, aacparse->pce_comment_bytes, 8))
      goto err_free;

    if (aacparse->pce_comment_bytes) {
      if (!gst_bit_writer_put_bytes (&bw, aacparse->pce_comment,
              aacparse->pce_comment_bytes))
        goto err_free;
    }
  }

  g_assert (gst_bit_writer_get_size (&bw) == adts_prefix_length * 8);

  mem = gst_memory_new_wrapped (0, adts_prefix, adts_prefix_length, 0,
      adts_prefix_length, adts_prefix, g_free);
  frame->out_buffer = gst_buffer_copy (frame->buffer);
  gst_buffer_prepend_memory (frame->out_buffer, mem);

  return TRUE;

err_free:
  GST_ERROR_OBJECT (aacparse, "Failure writing PCE");
  g_free (adts_prefix);
  return FALSE;
}

/**
 * gst_aac_parse_check_valid_frame:
 * @parse: #GstBaseParse.
 * @frame: #GstBaseParseFrame.
 * @skipsize: How much data parent class should skip in order to find the
 *            frame header.
 *
 * Implementation of "handle_frame" vmethod in #GstBaseParse class.
 *
 * Also determines frame overhead.
 * ADTS streams have a 7 byte header in each frame. MP4 and ADIF streams don't have
 * a per-frame header. LOAS has 3 bytes.
 *
 * We're making a couple of simplifying assumptions:
 *
 * 1. We count Program Configuration Elements rather than searching for them
 *    in the streams to discount them - the overhead is negligible.
 *
 * 2. We ignore CRC. This has a worst-case impact of (num_raw_blocks + 1)*16
 *    bits, which should still not be significant enough to warrant the
 *    additional parsing through the headers
 *
 * Returns: a #GstFlowReturn.
 */
static GstFlowReturn
gst_aac_parse_handle_frame (GstBaseParse * parse,
    GstBaseParseFrame * frame, gint * skipsize)
{
  GstMapInfo map;
  GstAacParse *aacparse;
  gboolean ret = FALSE;
  gboolean lost_sync;
  GstBuffer *buffer;
  guint framesize;
  gint rate = 0, channels = 0;

  aacparse = GST_AAC_PARSE (parse);
  buffer = frame->buffer;

  gst_buffer_map (buffer, &map, GST_MAP_READ);

  *skipsize = -1;
  lost_sync = GST_BASE_PARSE_LOST_SYNC (parse);

  if (aacparse->header_type == DSPAAC_HEADER_ADIF ||
      aacparse->header_type == DSPAAC_HEADER_NONE) {
    /* There is nothing to parse */
    framesize = map.size;
    ret = TRUE;

  } else if (aacparse->header_type == DSPAAC_HEADER_NOT_PARSED || lost_sync) {

    ret = gst_aac_parse_detect_stream (aacparse, map.data, map.size,
        GST_BASE_PARSE_DRAINING (parse), &framesize, skipsize);

  } else if (aacparse->header_type == DSPAAC_HEADER_ADTS) {
    guint needed_data = 1024;

    ret = gst_aac_parse_check_adts_frame (aacparse, map.data, map.size,
        GST_BASE_PARSE_DRAINING (parse), &framesize, &needed_data);

    if (!ret && needed_data) {
      GST_DEBUG ("buffer didn't contain valid frame");
      *skipsize = 0;
      gst_base_parse_set_min_frame_size (GST_BASE_PARSE (aacparse),
          needed_data);
    }

  } else if (aacparse->header_type == DSPAAC_HEADER_LOAS) {
    guint needed_data = 1024;

    ret = gst_aac_parse_check_loas_frame (aacparse, map.data,
        map.size, GST_BASE_PARSE_DRAINING (parse), &framesize, &needed_data);

    if (!ret && needed_data) {
      GST_DEBUG ("buffer didn't contain valid frame");
      *skipsize = 0;
      gst_base_parse_set_min_frame_size (GST_BASE_PARSE (aacparse),
          needed_data);
    }

  } else {
    GST_DEBUG ("buffer didn't contain valid frame");
    gst_base_parse_set_min_frame_size (GST_BASE_PARSE (aacparse),
        ADTS_MAX_SIZE);
  }

  if (G_UNLIKELY (!ret))
    goto exit;

  ret = framesize <= map.size;

  if (aacparse->header_type == DSPAAC_HEADER_ADTS) {
    /* see above */
    frame->overhead = 7;

    gst_aac_parse_parse_adts_header (aacparse, map.data, map.size,
        &rate, &channels, NULL, NULL);

    GST_LOG_OBJECT (aacparse, "rate: %d, chans: %d", rate, channels);

    if (G_UNLIKELY (rate != aacparse->sample_rate
            || channels != aacparse->channels)) {
      aacparse->sample_rate = rate;
      aacparse->channels = channels;

      gst_aac_parse_set_src_caps (aacparse, NULL);

      gst_base_parse_set_frame_rate (GST_BASE_PARSE (aacparse),
          aacparse->sample_rate, aacparse->frame_samples, 2, 2);
    }
  } else if (aacparse->header_type == DSPAAC_HEADER_LOAS) {
    gboolean setcaps = FALSE;

    /* see above */
    frame->overhead = 3;

    if (!gst_aac_parse_read_loas_config (aacparse, map.data, map.size, &rate,
            &channels, NULL) || !rate || !channels) {
      /* This is pretty normal when skipping data at the start of
       * random stream (MPEG-TS capture for example) */
      GST_DEBUG_OBJECT (aacparse, "Error reading LOAS config. Skipping.");
      /* Since we don't fully parse the LOAS config, we don't know for sure
       * how much to skip. Just skip 1 to end up to the next marker and
       * resume parsing from there */
      *skipsize = 1;
      goto exit;
    }

    if (G_UNLIKELY (rate != aacparse->sample_rate
            || channels != aacparse->channels)) {
      aacparse->sample_rate = rate;
      aacparse->channels = channels;
      setcaps = TRUE;
      GST_INFO_OBJECT (aacparse, "New LOAS config: %d Hz, %d channels", rate,
          channels);
    }

    /* We want to set caps both at start, and when rate/channels change.
       Since only some LOAS frames have that info, we may receive frames
       before knowing about rate/channels. */
    if (setcaps
        || !gst_pad_has_current_caps (GST_BASE_PARSE_SRC_PAD (aacparse))) {
      gst_aac_parse_set_src_caps (aacparse, NULL);

      gst_base_parse_set_frame_rate (GST_BASE_PARSE (aacparse),
          aacparse->sample_rate, aacparse->frame_samples, 2, 2);
    }
  }

  if (aacparse->header_type == DSPAAC_HEADER_NONE
      && aacparse->output_header_type == DSPAAC_HEADER_ADTS) {
    if (!gst_aac_parse_prepend_adts_headers (aacparse, frame)) {
      GST_ERROR_OBJECT (aacparse, "Failed to prepend ADTS headers to frame");
      gst_buffer_unmap (buffer, &map);
      return GST_FLOW_ERROR;
    }
  }

exit:
  gst_buffer_unmap (buffer, &map);

  if (ret) {
    /* found, skip if needed */
    if (*skipsize > 0)
      return GST_FLOW_OK;
    *skipsize = 0;
  } else {
    if (*skipsize < 0)
      *skipsize = 1;
  }

  if (ret) {
    return gst_base_parse_finish_frame (parse, frame, framesize);
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_aac_parse_pre_push_frame (GstBaseParse * parse, GstBaseParseFrame * frame)
{
  GstAacParse *aacparse = GST_AAC_PARSE (parse);

  if (!aacparse->sent_codec_tag) {
    GstTagList *taglist;
    GstCaps *caps;

    /* codec tag */
    caps = gst_pad_get_current_caps (GST_BASE_PARSE_SRC_PAD (parse));
    if (caps == NULL) {
      if (GST_PAD_IS_FLUSHING (GST_BASE_PARSE_SRC_PAD (parse))) {
        GST_INFO_OBJECT (parse, "Src pad is flushing");
        return GST_FLOW_FLUSHING;
      } else {
        GST_INFO_OBJECT (parse, "Src pad is not negotiated!");
        return GST_FLOW_NOT_NEGOTIATED;
      }
    }

    taglist = gst_tag_list_new_empty ();
    gst_pb_utils_add_codec_description_to_tag_list (taglist,
        GST_TAG_AUDIO_CODEC, caps);
    gst_caps_unref (caps);

    gst_base_parse_merge_tags (parse, taglist, GST_TAG_MERGE_REPLACE);
    gst_tag_list_unref (taglist);

    /* also signals the end of first-frame processing */
    aacparse->sent_codec_tag = TRUE;
  }

  /* As a special case, we can remove the ADTS framing and output raw AAC. */
  if (aacparse->header_type == DSPAAC_HEADER_ADTS
      && aacparse->output_header_type == DSPAAC_HEADER_NONE) {
    guint header_size;
    GstMapInfo map;
    frame->out_buffer = gst_buffer_make_writable (frame->buffer);
    frame->buffer = NULL;
    gst_buffer_map (frame->out_buffer, &map, GST_MAP_READ);
    header_size = (map.data[1] & 1) ? 7 : 9;    /* optional CRC */
    gst_buffer_unmap (frame->out_buffer, &map);
    gst_buffer_resize (frame->out_buffer, header_size,
        gst_buffer_get_size (frame->out_buffer) - header_size);
  }

  frame->flags |= GST_BASE_PARSE_FRAME_FLAG_CLIP;

  return GST_FLOW_OK;
}


/**
 * gst_aac_parse_start:
 * @parse: #GstBaseParse.
 *
 * Implementation of "start" vmethod in #GstBaseParse class.
 *
 * Returns: TRUE if startup succeeded.
 */
static gboolean
gst_aac_parse_start (GstBaseParse * parse)
{
  GstAacParse *aacparse;

  aacparse = GST_AAC_PARSE (parse);
  GST_DEBUG ("start");
  aacparse->frame_samples = 1024;
  gst_base_parse_set_min_frame_size (GST_BASE_PARSE (aacparse), ADTS_MAX_SIZE);
  aacparse->sent_codec_tag = FALSE;
  aacparse->last_parsed_channels = 0;
  aacparse->last_parsed_sample_rate = 0;
  aacparse->object_type = 0;
  aacparse->bitrate = 0;
  aacparse->header_type = DSPAAC_HEADER_NOT_PARSED;
  aacparse->output_header_type = DSPAAC_HEADER_NOT_PARSED;
  aacparse->channels = 0;
  aacparse->sample_rate = 0;
  g_clear_pointer (&aacparse->pce, g_free);
  g_clear_pointer (&aacparse->pce_comment, g_free);
  aacparse->pce_bits = 0;
  aacparse->pce_comment_bytes = 0;
  return TRUE;
}


/**
 * gst_aac_parse_stop:
 * @parse: #GstBaseParse.
 *
 * Implementation of "stop" vmethod in #GstBaseParse class.
 *
 * Returns: TRUE is stopping succeeded.
 */
static gboolean
gst_aac_parse_stop (GstBaseParse * parse)
{
  GST_DEBUG ("stop");
  return TRUE;
}

static void
remove_fields (GstCaps * caps)
{
  guint i, n;

  n = gst_caps_get_size (caps);
  for (i = 0; i < n; i++) {
    GstStructure *s = gst_caps_get_structure (caps, i);

    gst_structure_remove_field (s, "framed");
  }
}

static void
add_conversion_fields (GstCaps * caps)
{
  guint i, n;

  n = gst_caps_get_size (caps);
  for (i = 0; i < n; i++) {
    GstStructure *s = gst_caps_get_structure (caps, i);

    if (gst_structure_has_field (s, "stream-format")) {
      const GValue *v = gst_structure_get_value (s, "stream-format");

      if (G_VALUE_HOLDS_STRING (v)) {
        const gchar *str = g_value_get_string (v);

        if (strcmp (str, "adts") == 0 || strcmp (str, "raw") == 0) {
          GValue va = G_VALUE_INIT;
          GValue vs = G_VALUE_INIT;

          g_value_init (&va, GST_TYPE_LIST);
          g_value_init (&vs, G_TYPE_STRING);
          g_value_set_string (&vs, "adts");
          gst_value_list_append_value (&va, &vs);
          g_value_set_string (&vs, "raw");
          gst_value_list_append_value (&va, &vs);
          gst_structure_set_value (s, "stream-format", &va);
          g_value_unset (&va);
          g_value_unset (&vs);
        }
      } else if (GST_VALUE_HOLDS_LIST (v)) {
        gboolean contains_raw = FALSE;
        gboolean contains_adts = FALSE;
        guint m = gst_value_list_get_size (v), j;

        for (j = 0; j < m; j++) {
          const GValue *ve = gst_value_list_get_value (v, j);
          const gchar *str;

          if (G_VALUE_HOLDS_STRING (ve) && (str = g_value_get_string (ve))) {
            if (strcmp (str, "adts") == 0)
              contains_adts = TRUE;
            else if (strcmp (str, "raw") == 0)
              contains_raw = TRUE;
          }
        }

        if (contains_adts || contains_raw) {
          GValue va = G_VALUE_INIT;
          GValue vs = G_VALUE_INIT;

          g_value_init (&va, GST_TYPE_LIST);
          g_value_init (&vs, G_TYPE_STRING);
          g_value_copy (v, &va);

          if (!contains_raw) {
            g_value_set_string (&vs, "raw");
            gst_value_list_append_value (&va, &vs);
          }
          if (!contains_adts) {
            g_value_set_string (&vs, "adts");
            gst_value_list_append_value (&va, &vs);
          }

          gst_structure_set_value (s, "stream-format", &va);

          g_value_unset (&vs);
          g_value_unset (&va);
        }
      }
    }
  }
}

static GstCaps *
gst_aac_parse_sink_getcaps (GstBaseParse * parse, GstCaps * filter)
{
  GstCaps *peercaps, *templ;
  GstCaps *res;

  templ = gst_pad_get_pad_template_caps (GST_BASE_PARSE_SINK_PAD (parse));

  if (filter) {
    GstCaps *fcopy = gst_caps_copy (filter);
    /* Remove the fields we convert */
    remove_fields (fcopy);
    add_conversion_fields (fcopy);
    peercaps = gst_pad_peer_query_caps (GST_BASE_PARSE_SRC_PAD (parse), fcopy);
    gst_caps_unref (fcopy);
  } else
    peercaps = gst_pad_peer_query_caps (GST_BASE_PARSE_SRC_PAD (parse), NULL);

  if (peercaps) {
    peercaps = gst_caps_make_writable (peercaps);
    /* Remove the fields we convert */
    remove_fields (peercaps);
    add_conversion_fields (peercaps);

    res = gst_caps_intersect_full (peercaps, templ, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (peercaps);
    gst_caps_unref (templ);
  } else {
    res = templ;
  }

  if (filter) {
    GstCaps *intersection;

    intersection =
        gst_caps_intersect_full (filter, res, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (res);
    res = intersection;
  }

  return res;
}

static gboolean
gst_aac_parse_src_event (GstBaseParse * parse, GstEvent * event)
{
  GstAacParse *aacparse = GST_AAC_PARSE (parse);

  if (GST_EVENT_TYPE (event) == GST_EVENT_FLUSH_STOP) {
    aacparse->last_parsed_channels = 0;
    aacparse->last_parsed_sample_rate = 0;
    g_clear_pointer (&aacparse->pce, g_free);
    g_clear_pointer (&aacparse->pce_comment, g_free);
    aacparse->pce_bits = 0;
    aacparse->pce_comment_bytes = 0;
  }

  return GST_BASE_PARSE_CLASS (parent_class)->src_event (parse, event);
}
