/* GStreamer Ogg Granulepos Mapping Utility Functions
 * Copyright (C) 2006 Tim-Philipp Müller <tim centricular net>
 * Copyright (C) 2009 David Schleef <ds@schleef.org>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstoggstream.h"
#include "dirac_parse.h"

#include <string.h>

GST_DEBUG_CATEGORY_EXTERN (gst_ogg_demux_debug);
GST_DEBUG_CATEGORY_EXTERN (gst_ogg_demux_setup_debug);
#define GST_CAT_DEFAULT gst_ogg_demux_debug

typedef struct _GstOggMap GstOggMap;

typedef gboolean (*GstOggMapSetupFunc) (GstOggStream * pad,
    ogg_packet * packet);
typedef GstClockTime (*GstOggMapToTimeFunc) (GstOggStream * pad,
    gint64 granulepos);
typedef gint64 (*GstOggMapToGranuleFunc) (GstOggStream * pad,
    gint64 granulepos);
typedef gint64 (*GstOggMapToGranuleposFunc) (GstOggStream * pad,
    gint64 granule, gint64 keyframe_granule);

/* returns TRUE if the granulepos denotes a key frame */
typedef gboolean (*GstOggMapIsKeyFrameFunc) (GstOggStream * pad,
    gint64 granulepos);

/* returns TRUE if the given packet is a stream header packet */
typedef gboolean (*GstOggMapIsHeaderPacketFunc) (GstOggStream * pad,
    ogg_packet * packet);
typedef gint64 (*GstOggMapPacketDurationFunc) (GstOggStream * pad,
    ogg_packet * packet);



#define SKELETON_FISBONE_MIN_SIZE  52


struct _GstOggMap
{
  const gchar *id;
  int id_length;
  int min_packet_size;
  const gchar *media_type;
  GstOggMapSetupFunc setup_func;
  GstOggMapToGranuleFunc granulepos_to_granule_func;
  GstOggMapToGranuleposFunc granule_to_granulepos_func;
  GstOggMapIsKeyFrameFunc is_key_frame_func;
  GstOggMapIsHeaderPacketFunc is_header_func;
  GstOggMapPacketDurationFunc packet_duration_func;
};

static const GstOggMap mappers[];

GstClockTime
gst_ogg_stream_get_packet_start_time (GstOggStream * pad, ogg_packet * packet)
{
  int duration;

  if (packet->granulepos == -1) {
    return GST_CLOCK_TIME_NONE;
  }

  duration = gst_ogg_stream_get_packet_duration (pad, packet);
  if (duration == -1) {
    return GST_CLOCK_TIME_NONE;
  }

  return gst_ogg_stream_granule_to_time (pad,
      gst_ogg_stream_granulepos_to_granule (pad,
          packet->granulepos) - duration);
}

GstClockTime
gst_ogg_stream_get_start_time_for_granulepos (GstOggStream * pad,
    gint64 granulepos)
{
  if (pad->frame_size == 0)
    return GST_CLOCK_TIME_NONE;

  return gst_ogg_stream_granule_to_time (pad,
      gst_ogg_stream_granulepos_to_granule (pad, granulepos));
}

GstClockTime
gst_ogg_stream_get_end_time_for_granulepos (GstOggStream * pad,
    gint64 granulepos)
{
  return gst_ogg_stream_granule_to_time (pad,
      gst_ogg_stream_granulepos_to_granule (pad, granulepos));
}

GstClockTime
gst_ogg_stream_granule_to_time (GstOggStream * pad, gint64 granule)
{
  if (granule == 0 || pad->granulerate_n == 0 || pad->granulerate_d == 0)
    return 0;

  return gst_util_uint64_scale (granule, GST_SECOND * pad->granulerate_d,
      pad->granulerate_n);
}

gint64
gst_ogg_stream_granulepos_to_granule (GstOggStream * pad, gint64 granulepos)
{
  if (granulepos == -1 || granulepos == 0) {
    return granulepos;
  }

  if (mappers[pad->map].granulepos_to_granule_func == NULL) {
    GST_WARNING ("Failed to convert granulepos to granule");
    return -1;
  }

  return mappers[pad->map].granulepos_to_granule_func (pad, granulepos);
}

gint64
gst_ogg_stream_granulepos_to_key_granule (GstOggStream * pad, gint64 granulepos)
{
  if (granulepos == -1 || granulepos == 0) {
    return granulepos;
  }

  return granulepos >> pad->granuleshift;
}

gint64
gst_ogg_stream_granule_to_granulepos (GstOggStream * pad, gint64 granule,
    gint64 keyframe_granule)
{
  if (granule == -1 || granule == 0) {
    return granule;
  }

  if (mappers[pad->map].granule_to_granulepos_func == NULL) {
    GST_WARNING ("Failed to convert granule to granulepos");
    return -1;
  }

  return mappers[pad->map].granule_to_granulepos_func (pad, granule,
      keyframe_granule);
}

gboolean
gst_ogg_stream_packet_granulepos_is_key_frame (GstOggStream * pad,
    gint64 granulepos)
{
  if (granulepos == -1) {
    return FALSE;
  }

  if (mappers[pad->map].is_key_frame_func == NULL) {
    GST_WARNING ("Failed to determine key frame");
    return FALSE;
  }

  return mappers[pad->map].is_key_frame_func (pad, granulepos);
}

gboolean
gst_ogg_stream_packet_is_header (GstOggStream * pad, ogg_packet * packet)
{
  if (mappers[pad->map].is_header_func == NULL) {
    GST_WARNING ("Failed to determine header");
    return FALSE;
  }

  return mappers[pad->map].is_header_func (pad, packet);
}

gint64
gst_ogg_stream_get_packet_duration (GstOggStream * pad, ogg_packet * packet)
{
  if (mappers[pad->map].packet_duration_func == NULL) {
    GST_WARNING ("Failed to determine packet duration");
    return -1;
  }

  return mappers[pad->map].packet_duration_func (pad, packet);
}




/* some generic functions */

static gboolean
is_keyframe_true (GstOggStream * pad, gint64 granulepos)
{
  return TRUE;
}

static gint64
granulepos_to_granule_default (GstOggStream * pad, gint64 granulepos)
{
  gint64 keyindex, keyoffset;

  if (pad->granuleshift != 0) {
    keyindex = granulepos >> pad->granuleshift;
    keyoffset = granulepos - (keyindex << pad->granuleshift);
    return keyindex + keyoffset;
  } else {
    return granulepos;
  }
}


static gint64
granule_to_granulepos_default (GstOggStream * pad, gint64 granule,
    gint64 keyframe_granule)
{
  gint64 keyoffset;

  if (pad->granuleshift != 0) {
    keyoffset = granule - keyframe_granule;
    return (keyframe_granule << pad->granuleshift) | keyoffset;
  } else {
    return granule;
  }
}

static gboolean
is_header_unknown (GstOggStream * pad, ogg_packet * packet)
{
  GST_WARNING ("don't know how to detect header");
  return FALSE;
}

static gboolean
is_header_true (GstOggStream * pad, ogg_packet * packet)
{
  return TRUE;
}

static gboolean
is_header_count (GstOggStream * pad, ogg_packet * packet)
{
  if (pad->n_header_packets_seen < pad->n_header_packets) {
    return TRUE;
  }
  return FALSE;
}

static gint64
packet_duration_constant (GstOggStream * pad, ogg_packet * packet)
{
  return pad->frame_size;
}

/* theora */

static gboolean
setup_theora_mapper (GstOggStream * pad, ogg_packet * packet)
{
  guint8 *data = packet->packet;

  pad->granulerate_n = GST_READ_UINT32_BE (data + 22);
  pad->granulerate_d = GST_READ_UINT32_BE (data + 26);
  GST_LOG ("fps = %d/%d", pad->granulerate_n, pad->granulerate_d);

  /* 2 bits + 3 bits = 5 bits KFGSHIFT */
  pad->granuleshift = ((GST_READ_UINT8 (data + 40) & 0x03) << 3) +
      (GST_READ_UINT8 (data + 41) >> 5);

  pad->n_header_packets = 3;
  pad->frame_size = 1;

  if (pad->granuleshift == 0 || pad->granulerate_n == 0
      || pad->granulerate_d == 0)
    return FALSE;

  pad->caps = gst_caps_new_simple ("video/x-theora",
      "framerate", GST_TYPE_FRACTION, pad->granulerate_n,
      pad->granulerate_d, NULL);

  return TRUE;
}

static gint64
granulepos_to_granule_theora (GstOggStream * pad, gint64 granulepos)
{
  gint64 keyindex, keyoffset;

  if (pad->granuleshift != 0) {
    keyindex = granulepos >> pad->granuleshift;
    keyoffset = granulepos - (keyindex << pad->granuleshift);
    if (keyoffset == 0) {
      pad->theora_has_zero_keyoffset = TRUE;
    }
    if (pad->theora_has_zero_keyoffset) {
      keyoffset++;
    }
    return keyindex + keyoffset;
  } else {
    return granulepos;
  }
}

static gboolean
is_keyframe_theora (GstOggStream * pad, gint64 granulepos)
{
  gint64 frame_mask;

  if (granulepos == (gint64) - 1)
    return FALSE;

  frame_mask = (1 << (pad->granuleshift + 1)) - 1;

  return ((granulepos & frame_mask) == 0);
}

static gboolean
is_header_theora (GstOggStream * pad, ogg_packet * packet)
{
  return (packet->bytes > 0 && (packet->packet[0] & 0x80) == 0x80);
}

/* dirac */

static gboolean
setup_dirac_mapper (GstOggStream * pad, ogg_packet * packet)
{
  int ret;
  DiracSequenceHeader header;

  ret = dirac_sequence_header_parse (&header, packet->packet + 13,
      packet->bytes - 13);
  if (ret == 0) {
    GST_DEBUG ("Failed to parse Dirac sequence header");
    return FALSE;
  }

  pad->granulerate_n = header.frame_rate_numerator * 2;
  pad->granulerate_d = header.frame_rate_denominator;
  pad->granuleshift = 22;
  pad->n_header_packets = 1;
  pad->frame_size = 2;

  if (header.interlaced_coding != 0) {
    GST_DEBUG ("non-progressive Dirac coding not implemented");
    return FALSE;
  }

  pad->caps = gst_caps_new_simple ("video/x-dirac",
      "width", G_TYPE_INT, header.width,
      "height", G_TYPE_INT, header.height,
      "interlaced", G_TYPE_BOOLEAN, header.interlaced,
      "pixel-aspect-ratio", GST_TYPE_FRACTION,
      header.aspect_ratio_numerator, header.aspect_ratio_denominator,
      "framerate", GST_TYPE_FRACTION, header.frame_rate_numerator,
      header.frame_rate_denominator, NULL);

  return TRUE;
}

#define OGG_DIRAC_GRANULE_LOW_MASK ((1<<22) - 1)
static gboolean
is_keyframe_dirac (GstOggStream * pad, gint64 granulepos)
{
  gint64 pt;
  int dist_h;
  int dist_l;
  int dist;
  int delay;
  gint64 dt;

  if (granulepos == -1)
    return -1;

  pt = ((granulepos >> 22) + (granulepos & OGG_DIRAC_GRANULE_LOW_MASK)) >> 9;
  dist_h = (granulepos >> 22) & 0xff;
  dist_l = granulepos & 0xff;
  dist = (dist_h << 8) | dist_l;
  delay = (granulepos >> 9) & 0x1fff;
  dt = pt - delay;

  return (dist == 0);
}

static gint64
granulepos_to_granule_dirac (GstOggStream * pad, gint64 gp)
{
  gint64 pt;
  int dist_h;
  int dist_l;
  int dist;
  int delay;
  gint64 dt;

  pt = ((gp >> 22) + (gp & OGG_DIRAC_GRANULE_LOW_MASK)) >> 9;
  dist_h = (gp >> 22) & 0xff;
  dist_l = gp & 0xff;
  dist = (dist_h << 8) | dist_l;
  delay = (gp >> 9) & 0x1fff;
  dt = pt - delay;

  GST_DEBUG ("pt %" G_GINT64_FORMAT " delay %d", pt, delay);

  return dt + 4;
}

static gint64
granule_to_granulepos_dirac (GstOggStream * pad, gint64 granule,
    gint64 keyframe_granule)
{
  /* This conversion requires knowing more details about the Dirac
   * stream. */
  return -1;
}


/* vorbis */

void parse_vorbis_header_packet (GstOggStream * pad, ogg_packet * op);
void parse_vorbis_setup_packet (GstOggStream * pad, ogg_packet * op);


static gboolean
setup_vorbis_mapper (GstOggStream * pad, ogg_packet * packet)
{
  guint8 *data = packet->packet;

  data += 1 + 6 + 4 + 1;
  pad->granulerate_n = GST_READ_UINT32_LE (data);
  pad->granulerate_d = 1;
  pad->granuleshift = 0;
  pad->last_size = 0;
  GST_LOG ("sample rate: %d", pad->granulerate_n);

  pad->n_header_packets = 3;

  if (pad->granulerate_n == 0)
    return FALSE;

  parse_vorbis_header_packet (pad, packet);

  pad->caps = gst_caps_new_simple ("audio/x-vorbis",
      "rate", G_TYPE_INT, pad->granulerate_n, NULL);

  return TRUE;
}

static gboolean
is_header_vorbis (GstOggStream * pad, ogg_packet * packet)
{
  if (packet->bytes > 0 && (packet->packet[0] & 0x01) == 0)
    return FALSE;

  if (packet->packet[0] == 5) {
    parse_vorbis_setup_packet (pad, packet);
  }

  return TRUE;
}

static gint64
packet_duration_vorbis (GstOggStream * pad, ogg_packet * packet)
{
  int mode;
  int size;
  int duration;

  if (packet->packet[0] & 1)
    return 0;

  mode = (packet->packet[0] >> 1) & ((1 << pad->vorbis_log2_num_modes) - 1);
  size = pad->vorbis_mode_sizes[mode] ? pad->long_size : pad->short_size;

  if (pad->last_size == 0) {
    duration = 0;
  } else {
    duration = pad->last_size / 4 + size / 4;
  }
  pad->last_size = size;

  GST_DEBUG ("duration %d", (int) duration);

  return duration;
}

/* speex */


static gboolean
setup_speex_mapper (GstOggStream * pad, ogg_packet * packet)
{
  guint8 *data = packet->packet;

  data += 8 + 20 + 4 + 4;
  pad->granulerate_n = GST_READ_UINT32_LE (data);
  pad->granulerate_d = 1;
  pad->granuleshift = 0;
  GST_LOG ("sample rate: %d", pad->granulerate_n);

  pad->n_header_packets = GST_READ_UINT32_LE (packet->packet + 68) + 2;
  pad->frame_size = GST_READ_UINT32_LE (packet->packet + 64) *
      GST_READ_UINT32_LE (packet->packet + 56);

  if (pad->granulerate_n == 0)
    return FALSE;

  pad->caps = gst_caps_new_simple ("audio/x-speex",
      "rate", G_TYPE_INT, pad->granulerate_n, NULL);

  return TRUE;
}


/* flac */

static gboolean
setup_fLaC_mapper (GstOggStream * pad, ogg_packet * packet)
{
  /* FIXME punt on this for now */
  return FALSE;
}

static gboolean
setup_flac_mapper (GstOggStream * pad, ogg_packet * packet)
{
  guint8 *data = packet->packet;

  /* see http://flac.sourceforge.net/ogg_mapping.html */

  pad->granulerate_n = (GST_READ_UINT32_BE (data + 27) & 0xFFFFF000) >> 12;
  pad->granulerate_d = 1;
  pad->granuleshift = 0;
  GST_DEBUG ("sample rate: %d", pad->granulerate_n);

  pad->n_header_packets = GST_READ_UINT16_BE (packet->packet + 7);

  if (pad->granulerate_n == 0)
    return FALSE;

  pad->caps = gst_caps_new_simple ("audio/x-flac",
      "rate", G_TYPE_INT, pad->granulerate_n, NULL);

  return TRUE;
}

static gint64
packet_duration_flac (GstOggStream * pad, ogg_packet * packet)
{
  int block_size_index;

  if (packet->bytes < 4)
    return -1;

  block_size_index = packet->packet[2] >> 4;
  if (block_size_index == 1)
    return 192;
  if (block_size_index >= 2 && block_size_index <= 5) {
    return 576 << (block_size_index - 2);
  }
  if (block_size_index >= 8) {
    return 256 << (block_size_index - 8);
  }
  return -1;
}

/* fishead */

static gboolean
setup_fishead_mapper (GstOggStream * pad, ogg_packet * packet)
{
  guint8 *data;
  gint64 prestime_n, prestime_d;
  gint64 basetime_n, basetime_d;
  gint64 basetime;

  data = packet->packet;

  data += 8 + 2 + 2;            /* header + major/minor version */

  prestime_n = (gint64) GST_READ_UINT64_LE (data);
  data += 8;
  prestime_d = (gint64) GST_READ_UINT64_LE (data);
  data += 8;
  basetime_n = (gint64) GST_READ_UINT64_LE (data);
  data += 8;
  basetime_d = (gint64) GST_READ_UINT64_LE (data);
  data += 8;

  /* FIXME: we don't use basetime anywhere in the demuxer! */
  basetime = gst_util_uint64_scale (GST_SECOND, basetime_n, basetime_d);
  GST_INFO ("skeleton fishead parsed (basetime: %" GST_TIME_FORMAT ")",
      GST_TIME_ARGS (basetime));

  return TRUE;
}

gboolean
gst_ogg_map_add_fisbone (GstOggStream * pad,
    const guint8 * data, guint size, GstClockTime * p_start_time,
    guint32 * p_preroll)
{
  GstClockTime start_time;
  gint64 start_granule;
  guint32 preroll;

  if (size < SKELETON_FISBONE_MIN_SIZE || memcmp (data, "fisbone\0", 8) != 0) {
    GST_WARNING ("invalid fisbone packet, ignoring");
    return FALSE;
  }

  if (pad->have_fisbone) {
    GST_DEBUG ("already have fisbone, ignoring second one");
    return FALSE;
  }

  /* skip "fisbone\0" + headers offset + serialno + num headers */
  data += 8 + 4 + 4 + 4;

  pad->have_fisbone = TRUE;

  /* we just overwrite whatever was set before by the format-specific setup */
  pad->granulerate_n = GST_READ_UINT64_LE (data);
  pad->granulerate_d = GST_READ_UINT64_LE (data + 8);

  start_granule = GST_READ_UINT64_LE (data + 16);
  preroll = GST_READ_UINT32_LE (data + 24);
  pad->granuleshift = GST_READ_UINT8 (data + 28);

  start_time = granulepos_to_granule_default (pad, start_granule);

  if (p_start_time)
    *p_start_time = start_time;

  if (p_preroll)
    *p_preroll = preroll;

  return TRUE;
}

#define STREAMHEADER_OGM_AUDIO_MIN_SIZE  53
#define STREAMHEADER_OGM_VIDEO_MIN_SIZE  53
#define STREAMHEADER_OGM_TEXT_MIN_SIZE    9

/* Do we need these for something?
 * ogm->hdr.size = GST_READ_UINT32_LE (&data[13]);
 * ogm->hdr.time_unit = GST_READ_UINT64_LE (&data[17]);
 * ogm->hdr.samples_per_unit = GST_READ_UINT64_LE (&data[25]);
 * ogm->hdr.default_len = GST_READ_UINT32_LE (&data[33]);
 * ogm->hdr.buffersize = GST_READ_UINT32_LE (&data[37]);
 * ogm->hdr.bits_per_sample = GST_READ_UINT32_LE (&data[41]);
 */

static gboolean
setup_ogmaudio_mapper (GstOggStream * pad, ogg_packet * packet)
{
  guint8 *data = packet->packet;
  guint size = packet->bytes;

  if (size < STREAMHEADER_OGM_AUDIO_MIN_SIZE ||
      memcmp (data, "\001audio\000\000\000", 9) != 0) {
    GST_WARNING ("not an ogm audio identification header");
    return FALSE;
  }

  pad->granulerate_n = GST_READ_UINT64_LE (data + 25);
  pad->granulerate_d = 1;

  GST_LOG ("sample rate: %d", pad->granulerate_n);
  if (pad->granulerate_n == 0)
    return FALSE;

  /* FIXME */
  pad->caps = gst_caps_new_simple ("audio/x-unknown",
      "rate", G_TYPE_INT, pad->granulerate_n, NULL);

  return TRUE;
}

static gboolean
setup_ogmvideo_mapper (GstOggStream * pad, ogg_packet * packet)
{
  guint8 *data = packet->packet;
  guint size = packet->bytes;

  if (size < STREAMHEADER_OGM_VIDEO_MIN_SIZE ||
      memcmp (data, "\001video\000\000\000", 9) != 0) {
    GST_WARNING ("not an ogm video identification header");
    return FALSE;
  }

  pad->granulerate_n = 10000000;
  pad->granulerate_d = GST_READ_UINT64_LE (data + 17);

  GST_LOG ("fps = %d/%d = %.3f",
      pad->granulerate_n, pad->granulerate_d,
      (double) pad->granulerate_n / pad->granulerate_d);

  if (pad->granulerate_d <= 0)
    return FALSE;

  /* FIXME */
  pad->caps = gst_caps_new_simple ("video/x-unknown",
      "framerate", GST_TYPE_FRACTION, pad->granulerate_n,
      pad->granulerate_d, NULL);

  return TRUE;
}

static gboolean
setup_ogmtext_mapper (GstOggStream * pad, ogg_packet * packet)
{
  guint8 *data = packet->packet;
  guint size = packet->bytes;

  if (size < STREAMHEADER_OGM_AUDIO_MIN_SIZE ||
      memcmp (data, "\001text\000\000\000\000", 9) != 0) {
    GST_WARNING ("not an ogm text identification header");
    return FALSE;
  }

  pad->granulerate_n = 10000000;
  pad->granulerate_d = GST_READ_UINT64_LE (data + 17);

  GST_LOG ("fps = %d/%d = %.3f",
      pad->granulerate_n, pad->granulerate_d,
      (double) pad->granulerate_n / pad->granulerate_d);

  if (pad->granulerate_d <= 0)
    return FALSE;

  /* FIXME */
  pad->caps = gst_caps_new_simple ("text/x-unknown", NULL);

  return TRUE;
}

/* PCM */

#define OGGPCM_FMT_S8 0x00000000        /* Signed integer 8 bit */
#define OGGPCM_FMT_U8 0x00000001        /* Unsigned integer 8 bit */
#define OGGPCM_FMT_S16_LE 0x00000002    /* Signed integer 16 bit little endian */
#define OGGPCM_FMT_S16_BE 0x00000003    /* Signed integer 16 bit big endian */
#define OGGPCM_FMT_S24_LE 0x00000004    /* Signed integer 24 bit little endian */
#define OGGPCM_FMT_S24_BE 0x00000005    /* Signed integer 24 bit big endian */
#define OGGPCM_FMT_S32_LE 0x00000006    /* Signed integer 32 bit little endian */
#define OGGPCM_FMT_S32_BE 0x00000007    /* Signed integer 32 bit big endian */

#define OGGPCM_FMT_ULAW 0x00000010      /* G.711 u-law encoding (8 bit) */
#define OGGPCM_FMT_ALAW 0x00000011      /* G.711 A-law encoding (8 bit) */

#define OGGPCM_FMT_FLT32_LE 0x00000020  /* IEEE Float [-1,1] 32 bit little endian */
#define OGGPCM_FMT_FLT32_BE 0x00000021  /* IEEE Float [-1,1] 32 bit big endian */
#define OGGPCM_FMT_FLT64_LE 0x00000022  /* IEEE Float [-1,1] 64 bit little endian */
#define OGGPCM_FMT_FLT64_BE 0x00000023  /* IEEE Float [-1,1] 64 bit big endian */


static gboolean
setup_pcm_mapper (GstOggStream * pad, ogg_packet * packet)
{
  guint8 *data = packet->packet;
  int format;
  int channels;
  GstCaps *caps;

  pad->granulerate_n = GST_READ_UINT32_LE (data + 16);
  pad->granulerate_d = 1;
  GST_LOG ("sample rate: %d", pad->granulerate_n);

  format = GST_READ_UINT32_LE (data + 12);
  channels = GST_READ_UINT8 (data + 21);

  pad->n_header_packets = 2 + GST_READ_UINT32_LE (data + 24);

  if (pad->granulerate_n == 0)
    return FALSE;

  switch (format) {
    case OGGPCM_FMT_S8:
      caps = gst_caps_new_simple ("audio/x-raw-int",
          "depth", G_TYPE_INT, 8,
          "width", G_TYPE_INT, 8, "signed", G_TYPE_BOOLEAN, TRUE, NULL);
      break;
    case OGGPCM_FMT_U8:
      caps = gst_caps_new_simple ("audio/x-raw-int",
          "depth", G_TYPE_INT, 8,
          "width", G_TYPE_INT, 8, "signed", G_TYPE_BOOLEAN, FALSE, NULL);
      break;
    case OGGPCM_FMT_S16_LE:
      caps = gst_caps_new_simple ("audio/x-raw-int",
          "depth", G_TYPE_INT, 16,
          "width", G_TYPE_INT, 16,
          "endianness", G_TYPE_INT, G_LITTLE_ENDIAN,
          "signed", G_TYPE_BOOLEAN, TRUE, NULL);
      break;
    case OGGPCM_FMT_S16_BE:
      caps = gst_caps_new_simple ("audio/x-raw-int",
          "depth", G_TYPE_INT, 16,
          "width", G_TYPE_INT, 16,
          "endianness", G_TYPE_INT, G_BIG_ENDIAN,
          "signed", G_TYPE_BOOLEAN, TRUE, NULL);
      break;
    case OGGPCM_FMT_S24_LE:
      caps = gst_caps_new_simple ("audio/x-raw-int",
          "depth", G_TYPE_INT, 24,
          "width", G_TYPE_INT, 24,
          "endianness", G_TYPE_INT, G_LITTLE_ENDIAN,
          "signed", G_TYPE_BOOLEAN, TRUE, NULL);
      break;
    case OGGPCM_FMT_S24_BE:
      caps = gst_caps_new_simple ("audio/x-raw-int",
          "depth", G_TYPE_INT, 24,
          "width", G_TYPE_INT, 24,
          "endianness", G_TYPE_INT, G_BIG_ENDIAN,
          "signed", G_TYPE_BOOLEAN, TRUE, NULL);
      break;
    case OGGPCM_FMT_S32_LE:
      caps = gst_caps_new_simple ("audio/x-raw-int",
          "depth", G_TYPE_INT, 32,
          "width", G_TYPE_INT, 32,
          "endianness", G_TYPE_INT, G_LITTLE_ENDIAN,
          "signed", G_TYPE_BOOLEAN, TRUE, NULL);
      break;
    case OGGPCM_FMT_S32_BE:
      caps = gst_caps_new_simple ("audio/x-raw-int",
          "depth", G_TYPE_INT, 32,
          "width", G_TYPE_INT, 32,
          "endianness", G_TYPE_INT, G_BIG_ENDIAN,
          "signed", G_TYPE_BOOLEAN, TRUE, NULL);
      break;
    case OGGPCM_FMT_ULAW:
      caps = gst_caps_new_simple ("audio/x-mulaw", NULL);
      break;
    case OGGPCM_FMT_ALAW:
      caps = gst_caps_new_simple ("audio/x-alaw", NULL);
      break;
    case OGGPCM_FMT_FLT32_LE:
      caps = gst_caps_new_simple ("audio/x-raw-float",
          "width", G_TYPE_INT, 32,
          "endianness", G_TYPE_INT, G_LITTLE_ENDIAN, NULL);
      break;
    case OGGPCM_FMT_FLT32_BE:
      caps = gst_caps_new_simple ("audio/x-raw-float",
          "width", G_TYPE_INT, 32,
          "endianness", G_TYPE_INT, G_BIG_ENDIAN, NULL);
      break;
    case OGGPCM_FMT_FLT64_LE:
      caps = gst_caps_new_simple ("audio/x-raw-float",
          "width", G_TYPE_INT, 64,
          "endianness", G_TYPE_INT, G_LITTLE_ENDIAN, NULL);
      break;
    case OGGPCM_FMT_FLT64_BE:
      caps = gst_caps_new_simple ("audio/x-raw-float",
          "width", G_TYPE_INT, 64,
          "endianness", G_TYPE_INT, G_BIG_ENDIAN, NULL);
      break;
    default:
      return FALSE;
  }

  gst_caps_set_simple (caps, "audio/x-raw-int",
      "rate", G_TYPE_INT, pad->granulerate_n,
      "channels", G_TYPE_INT, channels, NULL);
  pad->caps = caps;

  return TRUE;
}

/* cmml */

static gboolean
setup_cmml_mapper (GstOggStream * pad, ogg_packet * packet)
{
  guint8 *data = packet->packet;

  pad->granulerate_n = GST_READ_UINT64_LE (data + 12);
  pad->granulerate_d = GST_READ_UINT64_LE (data + 20);
  pad->granuleshift = data[28];
  GST_LOG ("sample rate: %d", pad->granulerate_n);

  pad->n_header_packets = 3;

  if (pad->granulerate_n == 0)
    return FALSE;

  data += 4 + (4 + 4 + 4);
  GST_DEBUG ("blocksize0: %u", 1 << (data[0] >> 4));
  GST_DEBUG ("blocksize1: %u", 1 << (data[0] & 0x0F));

  pad->caps = gst_caps_new_simple ("text/x-cmml", NULL);

  return TRUE;
}

/* celt */

static gboolean
setup_celt_mapper (GstOggStream * pad, ogg_packet * packet)
{
  guint8 *data = packet->packet;

  pad->granulerate_n = GST_READ_UINT32_LE (data + 36);
  pad->granulerate_d = 1;
  pad->granuleshift = 0;
  GST_LOG ("sample rate: %d", pad->granulerate_n);

  pad->frame_size = GST_READ_UINT32_LE (packet->packet + 44);
  pad->n_header_packets = GST_READ_UINT32_LE (packet->packet + 56) + 2;

  if (pad->granulerate_n == 0)
    return FALSE;

  pad->caps = gst_caps_new_simple ("audio/x-celt",
      "rate", G_TYPE_INT, pad->granulerate_n, NULL);

  return TRUE;
}

/* kate */

static gboolean
setup_kate_mapper (GstOggStream * pad, ogg_packet * packet)
{
  guint8 *data = packet->packet;

  pad->granulerate_n = GST_READ_UINT32_LE (data + 24);
  pad->granulerate_d = GST_READ_UINT32_LE (data + 28);
  pad->granuleshift = GST_READ_UINT8 (data + 15);
  GST_LOG ("sample rate: %d", pad->granulerate_n);

  pad->n_header_packets = GST_READ_UINT8 (data + 11);

  if (pad->granulerate_n == 0)
    return FALSE;

  pad->caps = gst_caps_new_simple ("audio/x-kate",
      "rate", G_TYPE_INT, pad->granulerate_n, NULL);

  return TRUE;
}


/* *INDENT-OFF* */
/* indent hates our freedoms */
static const GstOggMap mappers[] = {
  {
    "\200theora", 7, 42,
    "video/x-theora",
    setup_theora_mapper,
    granulepos_to_granule_theora,
    granule_to_granulepos_default,
    is_keyframe_theora,
    is_header_theora,
    packet_duration_constant
  },
  {
    "\001vorbis", 7, 22,
    "audio/x-vorbis",
    setup_vorbis_mapper,
    granulepos_to_granule_default,
    granule_to_granulepos_default,
    is_keyframe_true,
    is_header_vorbis,
    packet_duration_vorbis
  },
  {
    "Speex", 5, 80,
    "audio/x-speex",
    setup_speex_mapper,
    granulepos_to_granule_default,
    granule_to_granulepos_default,
    is_keyframe_true,
    is_header_count,
    packet_duration_constant
  },
  {
    "PCM     ", 8, 0,
    "audio/x-raw-int",
    setup_pcm_mapper,
    NULL,
    NULL,
    NULL,
    is_header_count,
    NULL
  },
  {
    "CMML\0\0\0\0", 8, 0,
    "text/x-cmml",
    setup_cmml_mapper,
    NULL,
    NULL,
    NULL,
    is_header_count,
    NULL
  },
  {
    "Annodex", 7, 0,
    "application/x-annodex",
    setup_fishead_mapper,
    granulepos_to_granule_default,
    granule_to_granulepos_default,
    NULL,
    is_header_count,
    NULL
  },
  {
    "fishead", 7, 64,
    "application/octet-stream",
    setup_fishead_mapper,
    NULL,
    NULL,
    NULL,
    is_header_true,
    NULL
  },
  {
    "fLaC", 4, 0,
    "audio/x-flac",
    setup_fLaC_mapper,
    granulepos_to_granule_default,
    granule_to_granulepos_default,
    NULL,
    is_header_count,
    NULL
  },
  {
    "\177FLAC", 4, 36,
    "audio/x-flac",
    setup_flac_mapper,
    granulepos_to_granule_default,
    granule_to_granulepos_default,
    NULL,
    is_header_count,
    packet_duration_flac
  },
  {
    "AnxData", 7, 0,
    "application/octet-stream",
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
  },
  {
    "CELT    ", 8, 0,
    "audio/x-celt",
    setup_celt_mapper,
    granulepos_to_granule_default,
    granule_to_granulepos_default,
    NULL,
    is_header_count,
    packet_duration_constant
  },
  {
    "\200kate\0\0\0", 8, 0,
    "text/x-kate",
    setup_kate_mapper,
    NULL,
    NULL,
    NULL,
    is_header_count,
    NULL
  },
  {
    "BBCD\0", 5, 13,
    "video/x-dirac",
    setup_dirac_mapper,
    granulepos_to_granule_dirac,
    granule_to_granulepos_dirac,
    is_keyframe_dirac,
    is_header_count,
    packet_duration_constant
  },
  {
    "OGM audio", 100, 0,
    "application/x-ogm-audio",
    setup_ogmaudio_mapper,
    granulepos_to_granule_default,
    granule_to_granulepos_default,
    is_keyframe_true,
    is_header_unknown,
    NULL
  },
  {
    "OGM video", 100, 0,
    "application/x-ogm-video",
    setup_ogmvideo_mapper,
    granulepos_to_granule_default,
    granule_to_granulepos_default,
    NULL,
    is_header_unknown,
    NULL
  },
  {
    "OGM text", 100, 0,
    "application/x-ogm-text",
    setup_ogmtext_mapper,
    granulepos_to_granule_default,
    granule_to_granulepos_default,
    is_keyframe_true,
    is_header_unknown,
    NULL
  }
};
/* *INDENT-ON* */

gboolean
gst_ogg_stream_setup_map (GstOggStream * pad, ogg_packet * packet)
{
  int i;
  gboolean ret;

  for (i = 0; i < G_N_ELEMENTS (mappers); i++) {
    if (packet->bytes >= mappers[i].min_packet_size &&
        packet->bytes >= mappers[i].id_length &&
        memcmp (packet->packet, mappers[i].id, mappers[i].id_length) == 0) {
      ret = mappers[i].setup_func (pad, packet);
      if (ret) {
        GST_DEBUG ("got stream type %" GST_PTR_FORMAT, pad->caps);
        pad->map = i;
        return TRUE;
      }
    }
  }

  return FALSE;
}
