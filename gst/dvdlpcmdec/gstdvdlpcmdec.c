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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
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
        "audio/x-private2-lpcm; "
        "audio/x-private-ts-lpcm; "
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

#define gst_dvdlpcmdec_parent_class parent_class
G_DEFINE_TYPE (GstDvdLpcmDec, gst_dvdlpcmdec, GST_TYPE_AUDIO_DECODER);

static gboolean gst_dvdlpcmdec_set_format (GstAudioDecoder * bdec,
    GstCaps * caps);
static GstFlowReturn gst_dvdlpcmdec_parse (GstAudioDecoder * bdec,
    GstAdapter * adapter, gint * offset, gint * len);
static GstFlowReturn gst_dvdlpcmdec_handle_frame (GstAudioDecoder * bdec,
    GstBuffer * buffer);
static GstFlowReturn gst_dvdlpcmdec_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer);


static void
gst_dvdlpcmdec_class_init (GstDvdLpcmDecClass * klass)
{
  GstElementClass *element_class;
  GstAudioDecoderClass *gstbase_class;

  element_class = (GstElementClass *) klass;
  gstbase_class = (GstAudioDecoderClass *) klass;

  gstbase_class->set_format = GST_DEBUG_FUNCPTR (gst_dvdlpcmdec_set_format);
  gstbase_class->parse = GST_DEBUG_FUNCPTR (gst_dvdlpcmdec_parse);
  gstbase_class->handle_frame = GST_DEBUG_FUNCPTR (gst_dvdlpcmdec_handle_frame);

  gst_element_class_add_static_pad_template (element_class,
      &gst_dvdlpcmdec_sink_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_dvdlpcmdec_src_template);
  gst_element_class_set_static_metadata (element_class,
      "DVD LPCM Audio decoder", "Codec/Decoder/Audio",
      "Decode DVD LPCM frames into standard PCM audio",
      "Jan Schmidt <jan@noraisin.net>, Michael Smith <msmith@fluendo.com>");

  GST_DEBUG_CATEGORY_INIT (dvdlpcm_debug, "dvdlpcmdec", 0, "DVD LPCM Decoder");
}

static void
gst_dvdlpcm_reset (GstDvdLpcmDec * dvdlpcmdec)
{
  gst_audio_info_init (&dvdlpcmdec->info);
  dvdlpcmdec->dynamic_range = 0;
  dvdlpcmdec->emphasis = FALSE;
  dvdlpcmdec->mute = FALSE;

  dvdlpcmdec->header = 0;

  dvdlpcmdec->mode = GST_LPCM_UNKNOWN;
}

static void
gst_dvdlpcmdec_init (GstDvdLpcmDec * dvdlpcmdec)
{
  gst_dvdlpcm_reset (dvdlpcmdec);

  gst_audio_decoder_set_use_default_pad_acceptcaps (GST_AUDIO_DECODER_CAST
      (dvdlpcmdec), TRUE);
  GST_PAD_SET_ACCEPT_TEMPLATE (GST_AUDIO_DECODER_SINK_PAD (dvdlpcmdec));

  /* retrieve and intercept base class chain.
   * Quite HACKish, but that's dvd specs/caps for you,
   * since one buffer needs to be split into 2 frames */
  dvdlpcmdec->base_chain =
      GST_PAD_CHAINFUNC (GST_AUDIO_DECODER_SINK_PAD (dvdlpcmdec));
  gst_pad_set_chain_function (GST_AUDIO_DECODER_SINK_PAD (dvdlpcmdec),
      GST_DEBUG_FUNCPTR (gst_dvdlpcmdec_chain));
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

static const GstAudioChannelPosition bluray_channel_positions[][8] = {
  /* invalid */
  {GST_AUDIO_CHANNEL_POSITION_INVALID},
  /* mono */
  {GST_AUDIO_CHANNEL_POSITION_MONO},
  /* invalid */
  {GST_AUDIO_CHANNEL_POSITION_INVALID},
  /* stereo */
  {GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
      GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT},
  /* surround */
  {GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
      GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER},
  /* 2.1 */
  {GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
      GST_AUDIO_CHANNEL_POSITION_REAR_CENTER},
  /* 4.0 */
  {GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
      GST_AUDIO_CHANNEL_POSITION_REAR_CENTER},
  /* 2.2 */
  {GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT,
      GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT},
  /* 5.0 */
  {GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
        GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT,
      GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT},
  /* 5.1 */
  {GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
        GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT,
        GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT,
      GST_AUDIO_CHANNEL_POSITION_LFE1},
  /* 7.0 */
  {GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
        GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT,
        GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
      GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT},
  /* 7.1 */
  {GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
        GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT,
        GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
        GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT,
      GST_AUDIO_CHANNEL_POSITION_LFE1},
  /* invalid */
  {GST_AUDIO_CHANNEL_POSITION_INVALID},
  /* invalid */
  {GST_AUDIO_CHANNEL_POSITION_INVALID},
  /* invalid */
  {GST_AUDIO_CHANNEL_POSITION_INVALID},
  /* invalid */
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

  gst_audio_decoder_merge_tags (GST_AUDIO_DECODER (dvdlpcmdec), taglist,
      GST_TAG_MERGE_REPLACE);
  gst_tag_list_unref (taglist);
}

static gboolean
gst_dvdlpcmdec_set_output_format (GstDvdLpcmDec * dvdlpcmdec)
{
  gboolean res = TRUE;

  res = gst_audio_decoder_set_output_format (GST_AUDIO_DECODER (dvdlpcmdec),
      &dvdlpcmdec->info);
  if (res) {
    GST_DEBUG_OBJECT (dvdlpcmdec, "Successfully set output format");

    gst_dvdlpcmdec_send_tags (dvdlpcmdec);
  } else {
    GST_DEBUG_OBJECT (dvdlpcmdec, "Failed to set output format");
  }

  return res;
}

static void
gst_dvdlpcmdec_update_audio_formats (GstDvdLpcmDec * dec, gint channels,
    gint rate, GstAudioFormat format, guint8 channel_indicator,
    const GstAudioChannelPosition positions[][8])
{
  GST_DEBUG_OBJECT (dec, "got channels = %d, rate = %d, format = %d", channels,
      rate, format);

  /* Reorder the channel positions and set the default into for the audio */
  if (channels < 9
      && positions[channel_indicator][0] !=
      GST_AUDIO_CHANNEL_POSITION_INVALID) {
    const GstAudioChannelPosition *position;
    GstAudioChannelPosition sorted_position[8];
    guint c;

    position = positions[channel_indicator];
    for (c = 0; c < channels; ++c)
      sorted_position[c] = position[c];
    gst_audio_channel_positions_to_valid_order (sorted_position, channels);
    gst_audio_info_set_format (&dec->info, format, rate, channels,
        sorted_position);
    if (memcmp (position, sorted_position,
            channels * sizeof (position[0])) != 0)
      dec->lpcm_layout = position;
    else
      dec->lpcm_layout = NULL;
  } else {
    gst_audio_info_set_format (&dec->info, format, rate, channels, NULL);
  }
}

static gboolean
gst_dvdlpcmdec_set_format (GstAudioDecoder * bdec, GstCaps * caps)
{
  GstDvdLpcmDec *dvdlpcmdec = GST_DVDLPCMDEC (bdec);
  GstStructure *structure;
  gboolean res = TRUE;
  GstAudioFormat format;
  gint rate, channels, width;

  gst_dvdlpcm_reset (dvdlpcmdec);

  structure = gst_caps_get_structure (caps, 0);

  /* If we have the DVD structured LPCM (including header) then we wait
   * for incoming data before creating the output pad caps */
  if (gst_structure_has_name (structure, "audio/x-private1-lpcm")) {
    dvdlpcmdec->mode = GST_LPCM_DVD;
    goto done;
  }
  if (gst_structure_has_name (structure, "audio/x-private2-lpcm")) {
    dvdlpcmdec->mode = GST_LPCM_1394;
    goto done;
  }
  if (gst_structure_has_name (structure, "audio/x-private-ts-lpcm")) {
    dvdlpcmdec->mode = GST_LPCM_BLURAY;
    goto done;
  }

  dvdlpcmdec->mode = GST_LPCM_RAW;

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

  gst_dvdlpcmdec_update_audio_formats (dvdlpcmdec, channels, rate, format,
      channels - 1, channel_positions);

  dvdlpcmdec->width = width;

  res = gst_dvdlpcmdec_set_output_format (dvdlpcmdec);

done:
  return res;

  /* ERRORS */
caps_parse_error:
  {
    GST_DEBUG_OBJECT (dvdlpcmdec, "Couldn't get parameters; missing caps?");
    return FALSE;
  }
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

  gst_dvdlpcmdec_update_audio_formats (dec, channels, rate, format,
      channels - 1, channel_positions);
}

static GstFlowReturn
gst_dvdlpcmdec_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstDvdLpcmDec *dvdlpcmdec = GST_DVDLPCMDEC (parent);
  guint8 data[2];
  gsize size;
  guint first_access;
  GstBuffer *subbuf;
  GstFlowReturn ret = GST_FLOW_OK;
  gint off, len;

  if (dvdlpcmdec->mode != GST_LPCM_DVD)
    return dvdlpcmdec->base_chain (pad, parent, buf);

  size = gst_buffer_get_size (buf);
  if (size < 5)
    goto too_small;

  gst_buffer_extract (buf, 0, data, 2);
  first_access = (data[0] << 8) | data[1];
  if (first_access > size)
    goto invalid_data;

  /* After first_access, we have an additional 3 bytes of header data; this
   * is included within the value of first_access.
   * So a first_access value of between 1 and 3 is just broken, we treat that
   * the same as zero. first_access == 4 means we only need to create a single
   * sub-buffer, greater than that we need to create two. */

  /* skip access unit bytes */
  off = 2;

  if (first_access > 4) {
    /* length of first buffer */
    len = first_access - 1;

    GST_LOG_OBJECT (dvdlpcmdec, "Creating first sub-buffer off %d, len %d",
        off, len);

    /* see if we need a subbuffer without timestamp */
    if (off + len > size)
      goto bad_first_access;

    subbuf = gst_buffer_copy_region (buf, GST_BUFFER_COPY_ALL, off, len);
    GST_BUFFER_TIMESTAMP (subbuf) = GST_CLOCK_TIME_NONE;
    ret = dvdlpcmdec->base_chain (pad, parent, subbuf);
    if (ret != GST_FLOW_OK)
      goto done;

    /* then the buffer with new timestamp */
    off += len;
    len = size - off;

    GST_LOG_OBJECT (dvdlpcmdec, "Creating next sub-buffer off %d, len %d", off,
        len);

    if (len > 0) {
      GstMemory *header, *tmp;
      subbuf = gst_buffer_copy_region (buf, GST_BUFFER_COPY_ALL, off, len);
      tmp = gst_buffer_peek_memory (buf, 0);
      header = gst_memory_copy (tmp, 2, 3);
      gst_buffer_prepend_memory (subbuf, header);
      GST_BUFFER_TIMESTAMP (subbuf) = GST_BUFFER_TIMESTAMP (buf);

      ret = dvdlpcmdec->base_chain (pad, parent, subbuf);
    }
  } else {
    GST_LOG_OBJECT (dvdlpcmdec,
        "Creating single sub-buffer off %d, len %" G_GSIZE_FORMAT, off,
        size - off);
    subbuf = gst_buffer_copy_region (buf, GST_BUFFER_COPY_ALL, off, size - off);
    GST_BUFFER_TIMESTAMP (subbuf) = GST_BUFFER_TIMESTAMP (buf);
    ret = dvdlpcmdec->base_chain (pad, parent, subbuf);
  }

done:
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
gst_dvdlpcmdec_parse_dvd (GstDvdLpcmDec * dvdlpcmdec, GstAdapter * adapter,
    gint * offset, gint * len)
{
  guint32 header;
  const guint8 *data;

  data = (const guint8 *) gst_adapter_map (adapter, 3);
  if (!data)
    goto too_small;

  /* Don't keep the 'frame number' low 5 bits of the first byte */
  header = ((data[0] & 0xC0) << 16) | (data[1] << 8) | data[2];

  gst_adapter_unmap (adapter);

  /* see if we have a new header */
  if (header != dvdlpcmdec->header) {
    parse_header (dvdlpcmdec, header);

    if (!gst_dvdlpcmdec_set_output_format (dvdlpcmdec))
      goto negotiation_failed;

    dvdlpcmdec->header = header;
  }

  *offset = 3;
  *len = gst_adapter_available (adapter) - 3;

  return GST_FLOW_OK;

  /* ERRORS */
too_small:
  {
    /* Buffer is too small */
    GST_ELEMENT_WARNING (dvdlpcmdec, STREAM, DECODE,
        ("Invalid data found parsing LPCM packet"),
        ("LPCM packet was too small. Dropping"));
    *offset = gst_adapter_available (adapter);
    return GST_FLOW_EOS;
  }
negotiation_failed:
  {
    GST_ELEMENT_ERROR (dvdlpcmdec, STREAM, FORMAT, (NULL),
        ("Failed to configure output format"));
    return GST_FLOW_NOT_NEGOTIATED;
  }
}

static GstFlowReturn
gst_dvdlpcmdec_parse_bluray (GstDvdLpcmDec * dvdlpcmdec, GstAdapter * adapter,
    gint * offset, gint * len)
{
  guint32 header;
  const guint8 *data;
  guint8 channel_indicator;

  data = (const guint8 *) gst_adapter_map (adapter, 4);
  if (!data)
    goto too_small;

  header = GST_READ_UINT32_BE (data);

  gst_adapter_unmap (adapter);

  /* see if we have a new header */
  if (header != dvdlpcmdec->header) {
    GstAudioFormat format;
    gint rate, channels;

    switch ((header >> 6) & 0x3) {
      case 0x1:
        format = GST_AUDIO_FORMAT_S16BE;
        dvdlpcmdec->width = 16;
        break;
      case 0x2:
        format = GST_AUDIO_FORMAT_S24BE;
        dvdlpcmdec->width = 20;
        break;
      case 0x3:
        format = GST_AUDIO_FORMAT_S24BE;
        dvdlpcmdec->width = 24;
        break;
      default:
        format = GST_AUDIO_FORMAT_UNKNOWN;
        dvdlpcmdec->width = 0;
        GST_WARNING ("Invalid sample depth!");
        break;
    }

    switch ((header >> 8) & 0xf) {
      case 0x1:
        rate = 48000;
        break;
      case 0x4:
        rate = 96000;
        break;
      case 0x5:
        rate = 192000;
        break;
      default:
        rate = 0;
        GST_WARNING ("Invalid audio sampling frequency!");
        break;
    }
    channel_indicator = (header >> 12) & 0xf;
    switch (channel_indicator) {
      case 0x1:
        channels = 1;
        break;
      case 0x3:
        channels = 2;
        break;
      case 0x4:
      case 0x5:
        channels = 3;
        break;
      case 0x6:
      case 0x7:
        channels = 4;
        break;
      case 0x8:
        channels = 5;
        break;
      case 0x9:
        channels = 6;
        break;
      case 0xa:
        channels = 7;
        break;
      case 0xb:
        channels = 8;
        break;
      default:
        channels = 0;
        GST_WARNING ("Invalid number of audio channels!");
        goto negotiation_failed;
    }
    GST_DEBUG_OBJECT (dvdlpcmdec, "got channels %d rate %d format %s",
        channels, rate, gst_audio_format_to_string (format));

    gst_dvdlpcmdec_update_audio_formats (dvdlpcmdec, channels, rate, format,
        channel_indicator, bluray_channel_positions);

    if (!gst_dvdlpcmdec_set_output_format (dvdlpcmdec))
      goto negotiation_failed;

    dvdlpcmdec->header = header;
  }

  *offset = 4;
  *len = gst_adapter_available (adapter) - 4;

  return GST_FLOW_OK;

  /* ERRORS */
too_small:
  {
    /* Buffer is too small */
    GST_ELEMENT_WARNING (dvdlpcmdec, STREAM, DECODE,
        ("Invalid data found parsing LPCM packet"),
        ("LPCM packet was too small. Dropping"));
    *offset = gst_adapter_available (adapter);
    return GST_FLOW_EOS;
  }
negotiation_failed:
  {
    GST_ELEMENT_ERROR (dvdlpcmdec, STREAM, FORMAT, (NULL),
        ("Failed to configure output format"));
    return GST_FLOW_NOT_NEGOTIATED;
  }

}

static GstFlowReturn
gst_dvdlpcmdec_parse_1394 (GstDvdLpcmDec * dvdlpcmdec, GstAdapter * adapter,
    gint * offset, gint * len)
{
  guint32 header;
  const guint8 *data;

  data = (const guint8 *) gst_adapter_map (adapter, 4);
  if (!data)
    goto too_small;

  header = GST_READ_UINT32_BE (data);

  gst_adapter_unmap (adapter);

  /* see if we have a new header */
  if (header != dvdlpcmdec->header) {
    GstAudioFormat format;
    gint rate, channels;

    if (header >> 24 != 0xa0)
      goto invalid_data;

    switch ((header >> 6) & 0x3) {
      case 0x0:
        format = GST_AUDIO_FORMAT_S16BE;
        dvdlpcmdec->width = 16;
        break;
      default:
        format = GST_AUDIO_FORMAT_UNKNOWN;
        dvdlpcmdec->width = 0;
        GST_WARNING ("Invalid quantization word length!");
        break;
    }

    switch ((header >> 3) & 0x7) {
      case 0x1:
        rate = 44100;
        break;
      case 0x2:
        rate = 48000;
        break;
      default:
        rate = 0;
        GST_WARNING ("Invalid audio sampling frequency!");
        break;
    }
    switch (header & 0x7) {
      case 0x0:                /* 2 channels dual-mono */
      case 0x1:                /* 2 channels stereo */
        channels = 2;
        break;
      default:
        channels = 0;
        GST_WARNING ("Invalid number of audio channels!");
        goto negotiation_failed;
    }

    gst_dvdlpcmdec_update_audio_formats (dvdlpcmdec, channels, rate, format,
        channels - 1, channel_positions);

    if (!gst_dvdlpcmdec_set_output_format (dvdlpcmdec))
      goto negotiation_failed;

    dvdlpcmdec->header = header;
  }

  *offset = 4;
  *len = gst_adapter_available (adapter) - 4;

  return GST_FLOW_OK;

  /* ERRORS */
too_small:
  {
    /* Buffer is too small */
    GST_ELEMENT_WARNING (dvdlpcmdec, STREAM, DECODE,
        ("Invalid data found parsing LPCM packet"),
        ("LPCM packet was too small. Dropping"));
    *offset = gst_adapter_available (adapter);
    return GST_FLOW_EOS;
  }
invalid_data:
  {
    GST_ELEMENT_WARNING (dvdlpcmdec, STREAM, DECODE,
        ("Invalid data found parsing LPCM packet"),
        ("LPCM packet contains invalid sub_stream_id."));
    return GST_FLOW_ERROR;
  }
negotiation_failed:
  {
    GST_ELEMENT_ERROR (dvdlpcmdec, STREAM, FORMAT, (NULL),
        ("Failed to configure output format"));
    return GST_FLOW_NOT_NEGOTIATED;
  }
}

static GstFlowReturn
gst_dvdlpcmdec_parse (GstAudioDecoder * bdec, GstAdapter * adapter,
    gint * offset, gint * len)
{
  GstDvdLpcmDec *dvdlpcmdec = GST_DVDLPCMDEC (bdec);

  switch (dvdlpcmdec->mode) {
    case GST_LPCM_UNKNOWN:
      return GST_FLOW_NOT_NEGOTIATED;

    case GST_LPCM_RAW:
      *offset = 0;
      *len = gst_adapter_available (adapter);
      return GST_FLOW_OK;

    case GST_LPCM_DVD:
      return gst_dvdlpcmdec_parse_dvd (dvdlpcmdec, adapter, offset, len);

    case GST_LPCM_1394:
      return gst_dvdlpcmdec_parse_1394 (dvdlpcmdec, adapter, offset, len);

    case GST_LPCM_BLURAY:
      return gst_dvdlpcmdec_parse_bluray (dvdlpcmdec, adapter, offset, len);
  }
  return GST_FLOW_ERROR;
}

static GstFlowReturn
gst_dvdlpcmdec_handle_frame (GstAudioDecoder * bdec, GstBuffer * buf)
{
  GstDvdLpcmDec *dvdlpcmdec = GST_DVDLPCMDEC (bdec);
  gsize size;
  GstFlowReturn ret;
  guint samples = 0;
  gint rate, channels;

  /* no fancy draining */
  if (G_UNLIKELY (!buf))
    return GST_FLOW_OK;

  size = gst_buffer_get_size (buf);

  GST_LOG_OBJECT (dvdlpcmdec,
      "got buffer %p of size %" G_GSIZE_FORMAT " with ts %" GST_TIME_FORMAT,
      buf, size, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)));

  rate = GST_AUDIO_INFO_RATE (&dvdlpcmdec->info);
  channels = GST_AUDIO_INFO_CHANNELS (&dvdlpcmdec->info);

  if (rate == 0)
    goto not_negotiated;

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

      gst_buffer_ref (buf);
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
      buf = outbuf;
      break;
    }
    case 24:
    {
      /* Rearrange 24-bit LPCM format in-place. Note that the first 2
       * and last byte are already correct */
      guint count = size / 12;
      gint i;
      GstMapInfo srcmap, destmap;
      guint8 *src, *dest;
      GstBuffer *outbuf;

      samples = size / channels / 3;

      if (samples < 1)
        goto drop;

      outbuf = gst_buffer_new_allocate (NULL, size, NULL);
      gst_buffer_copy_into (outbuf, buf, GST_BUFFER_COPY_TIMESTAMPS, 0, -1);

      gst_buffer_map (buf, &srcmap, GST_MAP_READ);
      gst_buffer_map (outbuf, &destmap, GST_MAP_READWRITE);
      src = srcmap.data;
      dest = destmap.data;

      for (i = 0; i < count; i++) {
        dest[0] = src[0];
        dest[1] = src[1];
        dest[11] = src[11];
        dest[10] = src[7];
        dest[7] = src[5];
        dest[5] = src[9];
        dest[9] = src[6];
        dest[6] = src[4];
        dest[4] = src[3];
        dest[3] = src[2];
        dest[2] = src[8];
        dest[8] = src[10];

        src += 12;
        dest += 12;
      }
      gst_buffer_unmap (outbuf, &destmap);
      gst_buffer_unmap (buf, &srcmap);
      buf = outbuf;
      break;
    }
    default:
      goto invalid_width;
  }

  if (dvdlpcmdec->lpcm_layout) {
    buf = gst_buffer_make_writable (buf);
    gst_audio_buffer_reorder_channels (buf, dvdlpcmdec->info.finfo->format,
        dvdlpcmdec->info.channels, dvdlpcmdec->lpcm_layout,
        dvdlpcmdec->info.position);
  }

  ret = gst_audio_decoder_finish_frame (bdec, buf, 1);

done:
  return ret;

  /* ERRORS */
drop:
  {
    GST_DEBUG_OBJECT (dvdlpcmdec,
        "Buffer of size %" G_GSIZE_FORMAT " is too small. Dropping", size);
    ret = GST_FLOW_OK;
    goto done;
  }
not_negotiated:
  {
    GST_ELEMENT_ERROR (dvdlpcmdec, STREAM, FORMAT, (NULL),
        ("Buffer pushed before negotiation"));
    ret = GST_FLOW_NOT_NEGOTIATED;
    goto done;
  }
invalid_width:
  {
    GST_ELEMENT_ERROR (dvdlpcmdec, STREAM, WRONG_TYPE, (NULL),
        ("Invalid sample width configured"));
    ret = GST_FLOW_NOT_NEGOTIATED;
    goto done;
  }
}

static gboolean
plugin_init (GstPlugin * plugin)
{

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
