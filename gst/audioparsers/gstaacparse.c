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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:element-aacparse
 * @short_description: AAC parser
 * @see_also: #GstAmrParse
 *
 * <refsect2>
 * <para>
 * This is an AAC parser. It can handle both ADIF and ADTS stream formats.
 * The parser inherits from #GstBaseParse and therefore in only needs to
 * implement AAC-specific functionality.
 * </para>
 * <para>
 * As ADIF format is not framed, it is not seekable. From the same reason
 * stream duration cannot be calculated either. Instead, AAC clips that are
 * in ADTS format can be seeked, and parser also is able to calculate their
 * playback position and clip duration.
 * </para>
 * <title>Example launch line</title>
 * <para>
 * <programlisting>
 * gst-launch filesrc location=abc.aac ! aacparse ! faad ! audioresample ! audioconvert ! alsasink
 * </programlisting>
 * </para>
 * </refsect2>
 */

#include <string.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstaacparse.h"


static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg, "
        "framed = (boolean) true, " "mpegversion = (int) { 2, 4 };"));

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg, "
        "framed = (boolean) false, " "mpegversion = (int) { 2, 4 };"));

GST_DEBUG_CATEGORY_STATIC (gst_aacparse_debug);
#define GST_CAT_DEFAULT gst_aacparse_debug


#define ADIF_MAX_SIZE 40        /* Should be enough */
#define ADTS_MAX_SIZE 10        /* Should be enough */


#define AAC_FRAME_DURATION(parse) (GST_SECOND/parse->frames_per_sec)

static void gst_aacparse_finalize (GObject * object);

gboolean gst_aacparse_start (GstBaseParse * parse);
gboolean gst_aacparse_stop (GstBaseParse * parse);

static gboolean gst_aacparse_sink_setcaps (GstBaseParse * parse,
    GstCaps * caps);

gboolean gst_aacparse_check_valid_frame (GstBaseParse * parse,
    GstBuffer * buffer, guint * size, gint * skipsize);

GstFlowReturn gst_aacparse_parse_frame (GstBaseParse * parse,
    GstBuffer * buffer);

gboolean gst_aacparse_convert (GstBaseParse * parse,
    GstFormat src_format,
    gint64 src_value, GstFormat dest_format, gint64 * dest_value);

gboolean gst_aacparse_is_seekable (GstBaseParse * parse);

gboolean gst_aacparse_event (GstBaseParse * parse, GstEvent * event);

#define _do_init(bla) \
    GST_DEBUG_CATEGORY_INIT (gst_aacparse_debug, "aacparse", 0, \
    "AAC audio stream parser");

GST_BOILERPLATE_FULL (GstAacParse, gst_aacparse, GstBaseParse,
    GST_TYPE_BASE_PARSE, _do_init);

static inline gint
gst_aacparse_get_sample_rate_from_index (guint sr_idx)
{
  static const guint aac_sample_rates[] = { 96000, 88200, 64000, 48000, 44100,
    32000, 24000, 22050, 16000, 12000, 11025, 8000
  };

  if (sr_idx < G_N_ELEMENTS (aac_sample_rates))
    return aac_sample_rates[sr_idx];
  GST_WARNING ("Invalid sample rate index %u", sr_idx);
  return 0;
}

/**
 * gst_aacparse_base_init:
 * @klass: #GstElementClass.
 *
 */
static void
gst_aacparse_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstElementDetails details = GST_ELEMENT_DETAILS ("AAC audio stream parser",
      "Codec/Parser/Audio",
      "Advanced Audio Coding parser",
      "Stefan Kost <stefan.kost@nokia.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));

  gst_element_class_set_details (element_class, &details);
}


/**
 * gst_aacparse_class_init:
 * @klass: #GstAacParseClass.
 *
 */
static void
gst_aacparse_class_init (GstAacParseClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstBaseParseClass *parse_class = GST_BASE_PARSE_CLASS (klass);

  object_class->finalize = gst_aacparse_finalize;

  parse_class->start = GST_DEBUG_FUNCPTR (gst_aacparse_start);
  parse_class->stop = GST_DEBUG_FUNCPTR (gst_aacparse_stop);
  parse_class->set_sink_caps = GST_DEBUG_FUNCPTR (gst_aacparse_sink_setcaps);
  parse_class->is_seekable = GST_DEBUG_FUNCPTR (gst_aacparse_is_seekable);
  parse_class->parse_frame = GST_DEBUG_FUNCPTR (gst_aacparse_parse_frame);
  parse_class->check_valid_frame =
      GST_DEBUG_FUNCPTR (gst_aacparse_check_valid_frame);
}


/**
 * gst_aacparse_init:
 * @aacparse: #GstAacParse.
 * @klass: #GstAacParseClass.
 *
 */
static void
gst_aacparse_init (GstAacParse * aacparse, GstAacParseClass * klass)
{
  /* init rest */
  gst_base_parse_set_min_frame_size (GST_BASE_PARSE (aacparse), 1024);
  GST_DEBUG ("initialized");
}


/**
 * gst_aacparse_finalize:
 * @object:
 *
 */
static void
gst_aacparse_finalize (GObject * object)
{
  GstAacParse *aacparse;

  aacparse = GST_AACPARSE (object);
  G_OBJECT_CLASS (parent_class)->finalize (object);
}


/**
 * gst_aacparse_set_src_caps:
 * @aacparse: #GstAacParse.
 * @sink_caps: (proposed) caps of sink pad
 *
 * Set source pad caps according to current knowledge about the
 * audio stream.
 *
 * Returns: TRUE if caps were successfully set.
 */
static gboolean
gst_aacparse_set_src_caps (GstAacParse * aacparse, GstCaps * sink_caps)
{
  GstStructure *s;
  GstCaps *src_caps = NULL;
  gboolean res = FALSE;
  const gchar *stream_format;

  GST_DEBUG_OBJECT (aacparse, "sink caps: %" GST_PTR_FORMAT, sink_caps);
  if (sink_caps)
    src_caps = gst_caps_copy (sink_caps);
  else
    src_caps = gst_caps_new_simple ("audio/mpeg", NULL);

  gst_caps_set_simple (src_caps, "framed", G_TYPE_BOOLEAN, TRUE,
      "mpegversion", G_TYPE_INT, aacparse->mpegversion, NULL);

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
    default:
      stream_format = NULL;
  }

  s = gst_caps_get_structure (src_caps, 0);
  if (!gst_structure_has_field (s, "rate") && aacparse->sample_rate > 0)
    gst_structure_set (s, "rate", G_TYPE_INT, aacparse->sample_rate, NULL);
  if (!gst_structure_has_field (s, "channels") && aacparse->channels > 0)
    gst_structure_set (s, "channels", G_TYPE_INT, aacparse->channels, NULL);
  if (!gst_structure_has_field (s, "stream-format") && stream_format)
    gst_structure_set (s, "stream-format", G_TYPE_STRING, stream_format, NULL);

  GST_DEBUG_OBJECT (aacparse, "setting src caps: %" GST_PTR_FORMAT, src_caps);

  gst_pad_use_fixed_caps (GST_BASE_PARSE (aacparse)->srcpad);
  res = gst_pad_set_caps (GST_BASE_PARSE (aacparse)->srcpad, src_caps);
  gst_caps_unref (src_caps);
  return res;
}


/**
 * gst_aacparse_sink_setcaps:
 * @sinkpad: GstPad
 * @caps: GstCaps
 *
 * Implementation of "set_sink_caps" vmethod in #GstBaseParse class.
 *
 * Returns: TRUE on success.
 */
static gboolean
gst_aacparse_sink_setcaps (GstBaseParse * parse, GstCaps * caps)
{
  GstAacParse *aacparse;
  GstStructure *structure;
  gchar *caps_str;

  aacparse = GST_AACPARSE (parse);
  structure = gst_caps_get_structure (caps, 0);
  caps_str = gst_caps_to_string (caps);

  GST_DEBUG_OBJECT (aacparse, "setcaps: %s", caps_str);
  g_free (caps_str);

  /* This is needed at least in case of RTP
   * Parses the codec_data information to get ObjectType,
   * number of channels and samplerate */
  if (gst_structure_has_field (structure, "codec_data")) {

    const GValue *value = gst_structure_get_value (structure, "codec_data");

    if (value) {
      GstBuffer *buf = gst_value_get_buffer (value);
      const guint8 *buffer = GST_BUFFER_DATA (buf);
      guint sr_idx;

      sr_idx = ((buffer[0] & 0x07) << 1) | ((buffer[1] & 0x80) >> 7);
      aacparse->object_type = (buffer[0] & 0xf8) >> 3;
      aacparse->sample_rate = gst_aacparse_get_sample_rate_from_index (sr_idx);
      aacparse->channels = (buffer[1] & 0x78) >> 3;
      aacparse->header_type = DSPAAC_HEADER_NONE;
      aacparse->mpegversion = 4;

      GST_DEBUG ("codec_data: object_type=%d, sample_rate=%d, channels=%d",
          aacparse->object_type, aacparse->sample_rate, aacparse->channels);

      /* arrange for metadata and get out of the way */
      gst_aacparse_set_src_caps (aacparse, caps);
      gst_base_parse_set_passthrough (parse, TRUE);
    } else
      return FALSE;
  }

  return TRUE;
}


/**
 * gst_aacparse_adts_get_frame_len:
 * @data: block of data containing an ADTS header.
 *
 * This function calculates ADTS frame length from the given header.
 *
 * Returns: size of the ADTS frame.
 */
static inline guint
gst_aacparse_adts_get_frame_len (const guint8 * data)
{
  return ((data[3] & 0x03) << 11) | (data[4] << 3) | ((data[5] & 0xe0) >> 5);
}


/**
 * gst_aacparse_check_adts_frame:
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
gst_aacparse_check_adts_frame (GstAacParse * aacparse,
    const guint8 * data,
    const guint avail, guint * framesize, guint * needed_data)
{
  if (G_UNLIKELY (avail < 2))
    return FALSE;

  if ((data[0] == 0xff) && ((data[1] & 0xf6) == 0xf0)) {
    *framesize = gst_aacparse_adts_get_frame_len (data);

    /* In EOS mode this is enough. No need to examine the data further */
    if (gst_base_parse_get_drain (GST_BASE_PARSE (aacparse))) {
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
      guint nextlen = gst_aacparse_adts_get_frame_len (data + (*framesize));

      GST_LOG ("ADTS frame found, len: %d bytes", *framesize);
      gst_base_parse_set_min_frame_size (GST_BASE_PARSE (aacparse),
          nextlen + ADTS_MAX_SIZE);
      return TRUE;
    }
  }
  return FALSE;
}


/**
 * gst_aacparse_detect_stream:
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
gst_aacparse_detect_stream (GstAacParse * aacparse,
    const guint8 * data, const guint avail, guint * framesize, gint * skipsize)
{
  gboolean found = FALSE;
  guint need_data = 0;
  guint i = 0;

  GST_DEBUG_OBJECT (aacparse, "Parsing header data");

  /* FIXME: No need to check for ADIF if we are not in the beginning of the
     stream */

  /* Can we even parse the header? */
  if (avail < ADTS_MAX_SIZE)
    return FALSE;

  for (i = 0; i < avail - 4; i++) {
    if (((data[i] == 0xff) && ((data[i + 1] & 0xf6) == 0xf0)) ||
        strncmp ((char *) data + i, "ADIF", 4) == 0) {
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

  if (gst_aacparse_check_adts_frame (aacparse, data, avail,
          framesize, &need_data)) {
    gint sr_idx;
    GST_INFO ("ADTS ID: %d, framesize: %d", (data[1] & 0x08) >> 3, *framesize);

    aacparse->header_type = DSPAAC_HEADER_ADTS;
    sr_idx = (data[2] & 0x3c) >> 2;

    aacparse->sample_rate = gst_aacparse_get_sample_rate_from_index (sr_idx);
    aacparse->mpegversion = (data[1] & 0x08) ? 2 : 4;
    aacparse->object_type = (data[2] & 0xc0) >> 6;
    aacparse->channels = ((data[2] & 0x01) << 2) | ((data[3] & 0xc0) >> 6);
    aacparse->bitrate = ((data[5] & 0x1f) << 6) | ((data[6] & 0xfc) >> 2);

    gst_base_parse_set_frame_props (GST_BASE_PARSE (aacparse),
        aacparse->sample_rate, 1024, 50);

    GST_DEBUG ("ADTS: samplerate %d, channels %d, bitrate %d, objtype %d",
        aacparse->sample_rate, aacparse->channels, aacparse->bitrate,
        aacparse->object_type);

    return TRUE;
  } else if (need_data) {
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
    aacparse->sample_rate = gst_aacparse_get_sample_rate_from_index (sr_idx);

    /* baseparse is not given any fps,
     * so it will give up on timestamps, seeking, etc */

    /* FIXME: Can we assume this? */
    aacparse->channels = 2;

    GST_INFO ("ADIF: br=%d, samplerate=%d, objtype=%d",
        aacparse->bitrate, aacparse->sample_rate, aacparse->object_type);

    gst_base_parse_set_min_frame_size (GST_BASE_PARSE (aacparse), 512);

    *framesize = avail;
    return TRUE;
  }

  /* This should never happen */
  return FALSE;
}


/**
 * gst_aacparse_check_valid_frame:
 * @parse: #GstBaseParse.
 * @buffer: #GstBuffer.
 * @framesize: If the buffer contains a valid frame, its size will be put here
 * @skipsize: How much data parent class should skip in order to find the
 *            frame header.
 *
 * Implementation of "check_valid_frame" vmethod in #GstBaseParse class.
 *
 * Returns: TRUE if buffer contains a valid frame.
 */
gboolean
gst_aacparse_check_valid_frame (GstBaseParse * parse,
    GstBuffer * buffer, guint * framesize, gint * skipsize)
{
  const guint8 *data;
  GstAacParse *aacparse;
  gboolean ret = FALSE;
  gboolean sync;

  aacparse = GST_AACPARSE (parse);
  data = GST_BUFFER_DATA (buffer);

  sync = gst_base_parse_get_sync (parse);

  if (aacparse->header_type == DSPAAC_HEADER_ADIF ||
      aacparse->header_type == DSPAAC_HEADER_NONE) {
    /* There is nothing to parse */
    *framesize = GST_BUFFER_SIZE (buffer);
    ret = TRUE;

  } else if (aacparse->header_type == DSPAAC_HEADER_NOT_PARSED || sync == FALSE) {

    ret = gst_aacparse_detect_stream (aacparse, data, GST_BUFFER_SIZE (buffer),
        framesize, skipsize);

  } else if (aacparse->header_type == DSPAAC_HEADER_ADTS) {
    guint needed_data = 1024;

    ret = gst_aacparse_check_adts_frame (aacparse, data,
        GST_BUFFER_SIZE (buffer), framesize, &needed_data);

    if (!ret) {
      GST_DEBUG ("buffer didn't contain valid frame");
      gst_base_parse_set_min_frame_size (GST_BASE_PARSE (aacparse),
          needed_data);
    }

  } else {
    GST_DEBUG ("buffer didn't contain valid frame");
    gst_base_parse_set_min_frame_size (GST_BASE_PARSE (aacparse), 1024);
  }

  return ret;
}


/**
 * gst_aacparse_parse_frame:
 * @parse: #GstBaseParse.
 * @buffer: #GstBuffer.
 *
 * Implementation of "parse_frame" vmethod in #GstBaseParse class.
 *
 * Returns: GST_FLOW_OK if frame was successfully parsed and can be pushed
 *          forward. Otherwise appropriate error is returned.
 */
GstFlowReturn
gst_aacparse_parse_frame (GstBaseParse * parse, GstBuffer * buffer)
{
  GstAacParse *aacparse;
  GstFlowReturn ret = GST_FLOW_OK;

  aacparse = GST_AACPARSE (parse);

  if (!aacparse->src_caps_set) {
    if (!gst_aacparse_set_src_caps (aacparse,
            GST_PAD_CAPS (GST_BASE_PARSE (aacparse)->sinkpad))) {
      /* If linking fails, we need to return appropriate error */
      ret = GST_FLOW_NOT_LINKED;
    }
    aacparse->src_caps_set = TRUE;
  }

  gst_buffer_set_caps (buffer, GST_PAD_CAPS (parse->srcpad));
  return ret;
}


/**
 * gst_aacparse_start:
 * @parse: #GstBaseParse.
 *
 * Implementation of "start" vmethod in #GstBaseParse class.
 *
 * Returns: TRUE if startup succeeded.
 */
gboolean
gst_aacparse_start (GstBaseParse * parse)
{
  GstAacParse *aacparse;

  aacparse = GST_AACPARSE (parse);
  GST_DEBUG ("start");
  aacparse->src_caps_set = FALSE;
  gst_base_parse_set_passthrough (parse, FALSE);
  return TRUE;
}


/**
 * gst_aacparse_stop:
 * @parse: #GstBaseParse.
 *
 * Implementation of "stop" vmethod in #GstBaseParse class.
 *
 * Returns: TRUE is stopping succeeded.
 */
gboolean
gst_aacparse_stop (GstBaseParse * parse)
{
  GstAacParse *aacparse;

  aacparse = GST_AACPARSE (parse);
  GST_DEBUG ("stop");
  return TRUE;
}


/**
 * gst_aacparse_is_seekable:
 * @parse: #GstBaseParse.
 *
 * Implementation of "is_seekable" vmethod in #GstBaseParse class.
 *
 * Returns: TRUE if the current stream is seekable.
 */
gboolean
gst_aacparse_is_seekable (GstBaseParse * parse)
{
  GstAacParse *aacparse;

  aacparse = GST_AACPARSE (parse);
  GST_DEBUG_OBJECT (aacparse, "IS_SEEKABLE: %d",
      aacparse->header_type != DSPAAC_HEADER_ADIF);

  /* Not seekable if ADIF header is found */
  return (aacparse->header_type != DSPAAC_HEADER_ADIF);
}
