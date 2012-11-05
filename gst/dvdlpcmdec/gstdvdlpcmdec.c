/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2005> Jan Schmidt <jan@noraisin.net>
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
/* Element-Checklist-Version: TODO */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <string.h>

#include "gstdvdlpcmdec.h"
#include <gst/audio/audio.h>

GST_DEBUG_CATEGORY_STATIC (dvdlpcm_debug);
#define GST_CAT_DEFAULT dvdlpcm_debug

static GstStaticPadTemplate gst_dvdlpcmdec_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-private1-lpcm; "
        "audio/x-lpcm, "
        "width = (int) { 16, 20, 24 }, "
        "rate = (int) { 32000, 44100, 48000, 96000 }, "
        "channels = (int) [ 1, 8 ], "
        "dynamic_range = (int) [ 0, 255 ], "
        "emphasis = (boolean) { TRUE, FALSE }, "
        "mute = (boolean) { TRUE, FALSE } ")
    );

static GstStaticPadTemplate gst_dvdlpcmdec_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw, "
        "format = (string) { S16BE, S24BE }, "
        "layout = (string) interleaved, "
        "rate = (int) { 32000, 44100, 48000, 96000 }, "
        "channels = (int) [ 1, 8 ]")
    );

/* DvdLpcmDec signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0
      /* FILL ME */
};

static void gst_dvdlpcmdec_base_init (gpointer g_class);
static void gst_dvdlpcmdec_class_init (GstDvdLpcmDecClass * klass);
static void gst_dvdlpcmdec_init (GstDvdLpcmDec * dvdlpcmdec);

static GstFlowReturn gst_dvdlpcmdec_chain_raw (GstPad * pad, GstObject * parent,
    GstBuffer * buffer);
static GstFlowReturn gst_dvdlpcmdec_chain_dvd (GstPad * pad, GstObject * parent,
    GstBuffer * buffer);
static gboolean gst_dvdlpcmdec_setcaps (GstPad * pad, GstCaps * caps);
static gboolean dvdlpcmdec_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);

static GstStateChangeReturn gst_dvdlpcmdec_change_state (GstElement * element,
    GstStateChange transition);

static GstElementClass *parent_class = NULL;

GType
gst_dvdlpcmdec_get_type (void)
{
  static GType dvdlpcmdec_type = 0;

  if (!dvdlpcmdec_type) {
    static const GTypeInfo dvdlpcmdec_info = {
      sizeof (GstDvdLpcmDecClass),
      gst_dvdlpcmdec_base_init,
      NULL,
      (GClassInitFunc) gst_dvdlpcmdec_class_init,
      NULL,
      NULL,
      sizeof (GstDvdLpcmDec),
      0,
      (GInstanceInitFunc) gst_dvdlpcmdec_init,
    };

    dvdlpcmdec_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstDvdLpcmDec",
        &dvdlpcmdec_info, 0);
  }
  return dvdlpcmdec_type;
}

static void
gst_dvdlpcmdec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_dvdlpcmdec_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_dvdlpcmdec_src_template));
  gst_element_class_set_static_metadata (element_class,
      "DVD LPCM Audio decoder", "Codec/Decoder/Audio",
      "Decode DVD LPCM frames into standard PCM audio",
      "Jan Schmidt <jan@noraisin.net>, Michael Smith <msmith@fluendo.com>");
}

static void
gst_dvdlpcmdec_class_init (GstDvdLpcmDecClass * klass)
{
  GstElementClass *gstelement_class;

  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gstelement_class->change_state = gst_dvdlpcmdec_change_state;
}

static void
gst_dvdlpcm_reset (GstDvdLpcmDec * dvdlpcmdec)
{
  gst_audio_info_init (&dvdlpcmdec->info);
  dvdlpcmdec->dynamic_range = 0;
  dvdlpcmdec->emphasis = FALSE;
  dvdlpcmdec->mute = FALSE;
  dvdlpcmdec->timestamp = GST_CLOCK_TIME_NONE;

  dvdlpcmdec->header = 0;

  gst_segment_init (&dvdlpcmdec->segment, GST_FORMAT_UNDEFINED);
}

static void
gst_dvdlpcmdec_init (GstDvdLpcmDec * dvdlpcmdec)
{
  dvdlpcmdec->sinkpad =
      gst_pad_new_from_static_template (&gst_dvdlpcmdec_sink_template, "sink");
  gst_pad_set_event_function (dvdlpcmdec->sinkpad, dvdlpcmdec_sink_event);
  gst_element_add_pad (GST_ELEMENT (dvdlpcmdec), dvdlpcmdec->sinkpad);

  dvdlpcmdec->srcpad =
      gst_pad_new_from_static_template (&gst_dvdlpcmdec_src_template, "src");
  gst_pad_use_fixed_caps (dvdlpcmdec->srcpad);
  gst_element_add_pad (GST_ELEMENT (dvdlpcmdec), dvdlpcmdec->srcpad);

  gst_dvdlpcm_reset (dvdlpcmdec);
}

static const GstAudioChannelPosition channel_positions[][8] = {
  {GST_AUDIO_CHANNEL_POSITION_MONO},
  {GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
      GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT},
  {GST_AUDIO_CHANNEL_POSITION_INVALID},
  {GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
      GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT},
  {GST_AUDIO_CHANNEL_POSITION_INVALID},
  {GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
        GST_AUDIO_CHANNEL_POSITION_LFE1, GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
      GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT},
  {GST_AUDIO_CHANNEL_POSITION_INVALID},
  {GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
        GST_AUDIO_CHANNEL_POSITION_LFE1, GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
        GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT,
      GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT},
  {GST_AUDIO_CHANNEL_POSITION_INVALID}
};

static void
gst_dvdlpcmdec_send_tags (GstDvdLpcmDec * dvdlpcmdec)
{
  GstTagList *taglist;
  guint bitrate;
  gint bpf, rate;

  bpf = GST_AUDIO_INFO_BPF (&dvdlpcmdec->info);
  rate = GST_AUDIO_INFO_RATE (&dvdlpcmdec->info);

  bitrate = bpf * 8 * rate;

  taglist = gst_tag_list_new (GST_TAG_AUDIO_CODEC, "LPCM Audio",
      GST_TAG_BITRATE, bitrate, NULL);

  gst_pad_push_event (dvdlpcmdec->srcpad, gst_event_new_tag (taglist));
}

static gboolean
gst_dvdlpcmdec_set_outcaps (GstDvdLpcmDec * dvdlpcmdec)
{
  gboolean res = TRUE;
  GstCaps *src_caps;

  /* Build caps to set on the src pad, which we know from the incoming caps */
  src_caps = gst_audio_info_to_caps (&dvdlpcmdec->info);

  res = gst_pad_set_caps (dvdlpcmdec->srcpad, src_caps);
  if (res) {
    GST_DEBUG_OBJECT (dvdlpcmdec, "Successfully set output caps: %"
        GST_PTR_FORMAT, src_caps);

    gst_dvdlpcmdec_send_tags (dvdlpcmdec);
  } else {
    GST_DEBUG_OBJECT (dvdlpcmdec, "Failed to set output caps: %"
        GST_PTR_FORMAT, src_caps);
  }

  gst_caps_unref (src_caps);

  return res;
}

static gboolean
gst_dvdlpcmdec_setcaps (GstPad * pad, GstCaps * caps)
{
  GstStructure *structure;
  gboolean res = TRUE;
  GstDvdLpcmDec *dvdlpcmdec;
  GstAudioFormat format;
  gint rate, channels, width;
  const GstAudioChannelPosition *position;

  g_return_val_if_fail (caps != NULL, FALSE);
  g_return_val_if_fail (pad != NULL, FALSE);

  dvdlpcmdec = GST_DVDLPCMDEC (gst_pad_get_parent (pad));

  structure = gst_caps_get_structure (caps, 0);

  /* If we have the DVD structured LPCM (including header) then we wait
   * for incoming data before creating the output pad caps */
  if (gst_structure_has_name (structure, "audio/x-private1-lpcm")) {
    gst_pad_set_chain_function (dvdlpcmdec->sinkpad, gst_dvdlpcmdec_chain_dvd);
    goto done;
  }

  gst_pad_set_chain_function (dvdlpcmdec->sinkpad, gst_dvdlpcmdec_chain_raw);

  res &= gst_structure_get_int (structure, "rate", &rate);
  res &= gst_structure_get_int (structure, "channels", &channels);
  res &= gst_structure_get_int (structure, "width", &width);
  res &= gst_structure_get_int (structure, "dynamic_range",
      &dvdlpcmdec->dynamic_range);
  res &= gst_structure_get_boolean (structure, "emphasis",
      &dvdlpcmdec->emphasis);
  res &= gst_structure_get_boolean (structure, "mute", &dvdlpcmdec->mute);

  if (!res)
    goto caps_parse_error;

  switch (width) {
    case 24:
    case 20:
      format = GST_AUDIO_FORMAT_S24BE;
      break;
    case 16:
      format = GST_AUDIO_FORMAT_S16BE;
      break;
    default:
      format = GST_AUDIO_FORMAT_UNKNOWN;
      break;
  }

  gst_audio_info_set_format (&dvdlpcmdec->info, format, rate, channels, NULL);
  if (channels < 9
      && channel_positions[channels - 1][0] !=
      GST_AUDIO_CHANNEL_POSITION_INVALID) {
    dvdlpcmdec->info.flags &= ~GST_AUDIO_FLAG_UNPOSITIONED;
    position = channel_positions[channels - 1];
    dvdlpcmdec->lpcm_layout = position;
    memcpy (dvdlpcmdec->info.position, position,
        sizeof (GstAudioChannelPosition) * channels);
    gst_audio_channel_positions_to_valid_order (dvdlpcmdec->info.position,
        channels);
  }

  dvdlpcmdec->width = width;

  res = gst_dvdlpcmdec_set_outcaps (dvdlpcmdec);

done:
  gst_object_unref (dvdlpcmdec);
  return res;

  /* ERRORS */
caps_parse_error:
  {
    GST_DEBUG_OBJECT (dvdlpcmdec, "Couldn't get parameters; missing caps?");
    gst_object_unref (dvdlpcmdec);
    return FALSE;
  }
}

static void
update_timestamps (GstDvdLpcmDec * dvdlpcmdec, GstBuffer * buf, int samples)
{
  gboolean take_buf_ts = FALSE;
  gint rate;

  rate = GST_AUDIO_INFO_RATE (&dvdlpcmdec->info);

  GST_BUFFER_DURATION (buf) = gst_util_uint64_scale (samples, GST_SECOND, rate);

  if (GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
    if (GST_CLOCK_TIME_IS_VALID (dvdlpcmdec->timestamp)) {
      GstClockTimeDiff one_sample = GST_SECOND / rate;
      GstClockTimeDiff diff = GST_CLOCK_DIFF (GST_BUFFER_TIMESTAMP (buf),
          dvdlpcmdec->timestamp);

      if (diff > one_sample || diff < -one_sample)
        take_buf_ts = TRUE;
    } else {
      take_buf_ts = TRUE;
    }
  } else if (!GST_CLOCK_TIME_IS_VALID (dvdlpcmdec->timestamp)) {
    dvdlpcmdec->timestamp = 0;
  }

  if (take_buf_ts) {
    /* Take buffer timestamp */
    dvdlpcmdec->timestamp = GST_BUFFER_TIMESTAMP (buf);
  } else {
    GST_BUFFER_TIMESTAMP (buf) = dvdlpcmdec->timestamp;
  }

  dvdlpcmdec->timestamp += GST_BUFFER_DURATION (buf);

  GST_LOG_OBJECT (dvdlpcmdec, "Updated timestamp to %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)));
}

static void
parse_header (GstDvdLpcmDec * dec, guint32 header)
{
  GstAudioFormat format;
  gint rate, channels, width;

  /* We don't actually use 'dynamic range', 'mute', or 'emphasis' currently,
   * but parse them out */
  dec->dynamic_range = header & 0xff;

  dec->mute = (header & 0x400000) != 0;
  dec->emphasis = (header & 0x800000) != 0;

  /* These two bits tell us the bit depth */
  switch (header & 0xC000) {
    case 0x8000:
      /* 24 bits in 3 bytes */
      format = GST_AUDIO_FORMAT_S24BE;
      width = 24;
      break;
    case 0x4000:
      /* 20 bits in 3 bytes */
      format = GST_AUDIO_FORMAT_S24BE;
      width = 20;
      break;
    default:
      format = GST_AUDIO_FORMAT_S16BE;
      width = 16;
      break;
  }

  dec->width = width;

  /* Only four sample rates supported */
  switch (header & 0x3000) {
    case 0x0000:
      rate = 48000;
      break;
    case 0x1000:
      rate = 96000;
      break;
    case 0x2000:
      rate = 44100;
      break;
    case 0x3000:
      rate = 32000;
      break;
    default:
      rate = 0;
      break;
  }

  /* And, of course, the number of channels (up to 8) */
  channels = ((header >> 8) & 0x7) + 1;

  gst_audio_info_set_format (&dec->info, format, rate, channels, NULL);
  if (channels < 9
      && channel_positions[channels - 1][0] !=
      GST_AUDIO_CHANNEL_POSITION_INVALID) {
    const GstAudioChannelPosition *position;

    dec->info.flags &= ~GST_AUDIO_FLAG_UNPOSITIONED;
    position = channel_positions[channels - 1];
    dec->lpcm_layout = position;
    memcpy (dec->info.position, position,
        sizeof (GstAudioChannelPosition) * channels);
    gst_audio_channel_positions_to_valid_order (dec->info.position, channels);
  }
}

static GstFlowReturn
gst_dvdlpcmdec_chain_dvd (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstDvdLpcmDec *dvdlpcmdec;
  GstMapInfo map;
  guint8 *data;
  gsize size;
  guint first_access;
  guint32 header;
  GstBuffer *subbuf;
  GstFlowReturn ret = GST_FLOW_OK;
  gint off, len;
  gint rate, channels;

  dvdlpcmdec = GST_DVDLPCMDEC (parent);

  gst_buffer_map (buf, &map, GST_MAP_READ);
  data = map.data;
  size = map.size;

  if (size < 5)
    goto too_small;

  /* We have a 5 byte header, now.
   * The first two bytes are a (big endian) 16 bit offset into our buffer.
   * The buffer timestamp refers to this offset.
   *
   * The other three bytes are a (big endian) number in which the header is
   * encoded.
   */
  first_access = (data[0] << 8) | data[1];
  if (first_access > size)
    goto invalid_data;

  /* Don't keep the 'frame number' low 5 bits of the first byte */
  header = ((data[2] & 0xC0) << 16) | (data[3] << 8) | data[4];

  /* see if we have a new header */
  if (header != dvdlpcmdec->header) {
    parse_header (dvdlpcmdec, header);

    if (!gst_dvdlpcmdec_set_outcaps (dvdlpcmdec))
      goto negotiation_failed;

    dvdlpcmdec->header = header;
  }

  GST_LOG_OBJECT (dvdlpcmdec, "first_access %d, buffer length %" G_GSIZE_FORMAT,
      first_access, size);

  rate = GST_AUDIO_INFO_RATE (&dvdlpcmdec->info);
  channels = GST_AUDIO_INFO_CHANNELS (&dvdlpcmdec->info);

  /* After first_access, we have an additional 3 bytes of data we've parsed and
   * don't want to handle; this is included within the value of first_access.
   * So a first_access value of between 1 and 3 is just broken, we treat that
   * the same as zero. first_access == 4 means we only need to create a single
   * sub-buffer, greater than that we need to create two. */

  /* skip access unit bytes and info */
  off = 5;

  if (first_access > 4) {
    guint samples = 0;
    GstClockTime ts;

    /* length of first buffer */
    len = first_access - 4;

    GST_LOG_OBJECT (dvdlpcmdec, "Creating first sub-buffer off %d, len %d",
        off, len);

    /* see if we need a subbuffer without timestamp */
    if (off + len > size)
      goto bad_first_access;

    subbuf = gst_buffer_copy_region (buf, GST_BUFFER_COPY_ALL, off, len);

    /* If we don't have a stored timestamp from the last packet,
     * (it's straight after a new-segment, but we have one on the
     * first access buffer, then calculate the timestamp to align
     * this buffer to just before the first_access buffer */
    if (!GST_CLOCK_TIME_IS_VALID (dvdlpcmdec->timestamp) &&
        GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
      switch (dvdlpcmdec->width) {
        case 16:
          samples = len / channels / 2;
          break;
        case 20:
          samples = (len / channels) * 2 / 5;
          break;
        case 24:
          samples = len / channels / 3;
          break;
      }
    }
    if (samples != 0) {
      ts = gst_util_uint64_scale (samples, GST_SECOND, rate);
      if (ts < GST_BUFFER_TIMESTAMP (buf))
        GST_BUFFER_TIMESTAMP (subbuf) = GST_BUFFER_TIMESTAMP (buf) - ts;
      else
        GST_BUFFER_TIMESTAMP (subbuf) = 0;
    } else {
      GST_BUFFER_TIMESTAMP (subbuf) = GST_CLOCK_TIME_NONE;
    }

    ret = gst_dvdlpcmdec_chain_raw (pad, parent, subbuf);
    if (ret != GST_FLOW_OK)
      goto done;

    /* then the buffer with new timestamp */
    off += len;
    len = size - off;

    GST_LOG_OBJECT (dvdlpcmdec, "Creating next sub-buffer off %d, len %d", off,
        len);

    if (len > 0) {
      subbuf = gst_buffer_copy_region (buf, GST_BUFFER_COPY_ALL, off, len);
      GST_BUFFER_TIMESTAMP (subbuf) = GST_BUFFER_TIMESTAMP (buf);

      ret = gst_dvdlpcmdec_chain_raw (pad, parent, subbuf);
    }
  } else {
    GST_LOG_OBJECT (dvdlpcmdec,
        "Creating single sub-buffer off %d, len %" G_GSIZE_FORMAT, off,
        size - off);
    subbuf = gst_buffer_copy_region (buf, GST_BUFFER_COPY_ALL, off, size - off);
    GST_BUFFER_TIMESTAMP (subbuf) = GST_BUFFER_TIMESTAMP (buf);
    ret = gst_dvdlpcmdec_chain_raw (pad, parent, subbuf);
  }

done:
  gst_buffer_unmap (buf, &map);
  gst_buffer_unref (buf);

  return ret;

  /* ERRORS */
too_small:
  {
    /* Buffer is too small */
    GST_ELEMENT_WARNING (dvdlpcmdec, STREAM, DECODE,
        ("Invalid data found parsing LPCM packet"),
        ("LPCM packet was too small. Dropping"));
    ret = GST_FLOW_OK;
    goto done;
  }
invalid_data:
  {
    GST_ELEMENT_WARNING (dvdlpcmdec, STREAM, DECODE,
        ("Invalid data found parsing LPCM packet"),
        ("LPCM packet contained invalid first access. Dropping"));
    ret = GST_FLOW_OK;
    goto done;
  }
negotiation_failed:
  {
    GST_ELEMENT_ERROR (dvdlpcmdec, STREAM, FORMAT, (NULL),
        ("Failed to configure output format"));
    ret = GST_FLOW_NOT_NEGOTIATED;
    goto done;
  }
bad_first_access:
  {
    GST_WARNING_OBJECT (pad, "Bad first_access parameter in buffer");
    GST_ELEMENT_ERROR (dvdlpcmdec, STREAM, DECODE,
        (NULL),
        ("first_access parameter out of range: bad buffer from demuxer"));
    ret = GST_FLOW_ERROR;
    goto done;
  }
}

static GstFlowReturn
gst_dvdlpcmdec_chain_raw (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstDvdLpcmDec *dvdlpcmdec;
  gsize size;
  GstFlowReturn ret;
  guint samples = 0;
  gint rate, channels;

  dvdlpcmdec = GST_DVDLPCMDEC (parent);

  size = gst_buffer_get_size (buf);

  GST_LOG_OBJECT (dvdlpcmdec,
      "got buffer %p of size %" G_GSIZE_FORMAT " with ts %" GST_TIME_FORMAT,
      buf, size, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)));

  rate = GST_AUDIO_INFO_RATE (&dvdlpcmdec->info);
  channels = GST_AUDIO_INFO_CHANNELS (&dvdlpcmdec->info);

  if (rate == 0)
    goto not_negotiated;

  if (GST_BUFFER_TIMESTAMP_IS_VALID (buf))
    dvdlpcmdec->timestamp = GST_BUFFER_TIMESTAMP (buf);

  /* We don't currently do anything at all regarding emphasis, mute or
   * dynamic_range - I'm not sure what they're for */
  switch (dvdlpcmdec->width) {
    case 16:
    {
      /* We can just pass 16-bits straight through intact, once we set
       * appropriate things on the buffer */
      samples = size / channels / 2;
      if (samples < 1)
        goto drop;
      buf = gst_buffer_make_writable (buf);
      break;
    }
    case 20:
    {
      /* Allocate a new buffer and copy 20-bit width to 24-bit */
      gint64 samples = size * 8 / 20;
      gint64 count = size / 10;
      gint64 i;
      GstMapInfo srcmap, destmap;
      guint8 *src;
      guint8 *dest;
      GstBuffer *outbuf;

      if (samples < 1)
        goto drop;

      outbuf = gst_buffer_new_allocate (NULL, samples * 3, NULL);
      gst_buffer_copy_into (outbuf, buf, GST_BUFFER_COPY_TIMESTAMPS, 0, -1);

      /* adjust samples so we can calc the new timestamp */
      samples = samples / channels;

      gst_buffer_map (buf, &srcmap, GST_MAP_READ);
      gst_buffer_map (outbuf, &destmap, GST_MAP_WRITE);
      src = srcmap.data;
      dest = destmap.data;

      /* Copy 20-bit LPCM format to 24-bit buffers, with 0x00 in the lowest
       * nibble. Note that the first 2 bytes are already correct */
      for (i = 0; i < count; i++) {
        dest[0] = src[0];
        dest[1] = src[1];
        dest[2] = src[8] & 0xf0;
        dest[3] = src[2];
        dest[4] = src[3];
        dest[5] = (src[8] & 0x0f) << 4;
        dest[6] = src[4];
        dest[7] = src[5];
        dest[8] = src[9] & 0x0f;
        dest[9] = src[6];
        dest[10] = src[7];
        dest[11] = (src[9] & 0x0f) << 4;

        src += 10;
        dest += 12;
      }
      gst_buffer_unmap (outbuf, &destmap);
      gst_buffer_unmap (buf, &srcmap);
      gst_buffer_unref (buf);
      buf = outbuf;
      break;
    }
    case 24:
    {
      /* Rearrange 24-bit LPCM format in-place. Note that the first 2
       * and last byte are already correct */
      guint count = size / 12;
      gint i;
      GstMapInfo map;
      guint8 *ptr;

      samples = size / channels / 3;

      if (samples < 1)
        goto drop;

      /* Ensure our output buffer is writable */
      buf = gst_buffer_make_writable (buf);

      gst_buffer_map (buf, &map, GST_MAP_READWRITE);
      ptr = map.data;

      for (i = 0; i < count; i++) {
        guint8 tmp;

        tmp = ptr[10];
        ptr[10] = ptr[7];
        ptr[7] = ptr[5];
        ptr[5] = ptr[9];
        ptr[9] = ptr[6];
        ptr[6] = ptr[4];
        ptr[4] = ptr[3];
        ptr[3] = ptr[2];
        ptr[2] = ptr[8];
        ptr[8] = tmp;

        ptr += 12;
      }
      gst_buffer_unmap (buf, &map);
      break;
    }
    default:
      goto invalid_width;
  }

  update_timestamps (dvdlpcmdec, buf, samples);

  if (dvdlpcmdec->lpcm_layout)
    gst_audio_buffer_reorder_channels (buf, dvdlpcmdec->info.finfo->format,
        dvdlpcmdec->info.channels, dvdlpcmdec->lpcm_layout,
        dvdlpcmdec->info.position);

  ret = gst_pad_push (dvdlpcmdec->srcpad, buf);

done:
  return ret;

  /* ERRORS */
drop:
  {
    GST_DEBUG_OBJECT (dvdlpcmdec,
        "Buffer of size %" G_GSIZE_FORMAT " is too small. Dropping", size);
    gst_buffer_unref (buf);
    ret = GST_FLOW_OK;
    goto done;
  }
not_negotiated:
  {
    GST_ELEMENT_ERROR (dvdlpcmdec, STREAM, FORMAT, (NULL),
        ("Buffer pushed before negotiation"));
    gst_buffer_unref (buf);
    ret = GST_FLOW_NOT_NEGOTIATED;
    goto done;
  }
invalid_width:
  {
    GST_ELEMENT_ERROR (dvdlpcmdec, STREAM, WRONG_TYPE, (NULL),
        ("Invalid sample width configured"));
    gst_buffer_unref (buf);
    ret = GST_FLOW_NOT_NEGOTIATED;
    goto done;
  }
}

static gboolean
dvdlpcmdec_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstDvdLpcmDec *dvdlpcmdec;
  gboolean res;

  dvdlpcmdec = GST_DVDLPCMDEC (parent);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      res = gst_dvdlpcmdec_setcaps (pad, caps);
      gst_event_unref (event);
      break;
    }
    case GST_EVENT_SEGMENT:
    {
      GstSegment seg;

      gst_event_copy_segment (event, &seg);

      GST_DEBUG_OBJECT (dvdlpcmdec, "segment %" GST_SEGMENT_FORMAT, &seg);

      dvdlpcmdec->segment = seg;

      if (seg.format == GST_FORMAT_TIME) {
        dvdlpcmdec->timestamp = GST_CLOCK_TIME_NONE;
      } else {
        dvdlpcmdec->timestamp = 0;
      }
      res = gst_pad_push_event (dvdlpcmdec->srcpad, event);
      break;
    }
    case GST_EVENT_FLUSH_STOP:
      gst_segment_init (&dvdlpcmdec->segment, GST_FORMAT_UNDEFINED);
      res = gst_pad_push_event (dvdlpcmdec->srcpad, event);
      break;
    default:
      res = gst_pad_push_event (dvdlpcmdec->srcpad, event);
      break;
  }

  return res;
}

static GstStateChangeReturn
gst_dvdlpcmdec_change_state (GstElement * element, GstStateChange transition)
{
  GstDvdLpcmDec *dvdlpcmdec = GST_DVDLPCMDEC (element);
  GstStateChangeReturn res;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_dvdlpcm_reset (dvdlpcmdec);
      break;
    default:
      break;
  }

  res = parent_class->change_state (element, transition);

  switch (transition) {
    default:
      break;
  }

  return res;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (dvdlpcm_debug, "dvdlpcmdec", 0, "DVD LPCM Decoder");

  if (!gst_element_register (plugin, "dvdlpcmdec", GST_RANK_PRIMARY,
          GST_TYPE_DVDLPCMDEC)) {
    return FALSE;
  }

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    dvdlpcmdec,
    "Decode DVD LPCM frames into standard PCM",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
