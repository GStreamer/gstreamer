/* GStreamer
 * Copyright (C) 2016 Jan Schmidt <jan@centricular.com>
 * Copyright (C) 2016 Tim-Philipp Müller <tim@centricular.com>
 *
 * gsthlsdemux-util.c:
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
#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdio.h>

#include <gmodule.h>

#include <gst/gst.h>
#include <gst/tag/tag.h>
#include <string.h>

#include "gsthlsdemux.h"
#include "gsthlsdemux-stream.h"

GST_DEBUG_CATEGORY_EXTERN (gst_hls_demux2_debug);
#define GST_CAT_DEFAULT gst_hls_demux2_debug


/* Mpeg-TS Packet */
#define TS_PACKET_SYNC_BYTE 0x47

#define TS_PACKET_TRANSPORT_ERROR_INDICATOR(packet) \
  ((packet)[1] & 0x80)
#define TS_PACKET_PAYLOAD_UNIT_START(packet) \
  ((packet)[1] & 0x40)

#define TS_PACKET_PID(packet)				\
  ((guint16) ((packet)[1] & 0x1f) << 8 | (packet)[2])

#define TS_PACKET_TRANSPORT_SCRAMBLING_CONTROL(packet)	\
  ((packet)[3] & 0xc0)
#define TS_PACKET_HAS_ADAPTATION_FIELD(packet) \
  ((packet)[3] & 0x20)
#define TS_PACKET_HAS_PAYLOAD(packet)		\
  ((packet)[3] & 0x10)
#define TS_PACKET_CONTINUITY_COUNTER(pacet)	\
  ((packet)[3] & 0x0f)

#define TS_PACKET_ADAPTATION_FIELD(packet)     \
  (TS_PACKET_HAS_ADAPTATION_FIELD(packet) ?    \
   (packet) + 4 : NULL)

/* Adaptation field size. Note: can be 0 */
#define TS_PACKET_ADAPTATION_FIELD_SIZE(packet)	\
  (packet)[4]


#define TS_PACKET_PAYLOAD_OFFSET(packet)		\
  (TS_PACKET_HAS_ADAPTATION_FIELD (packet) ?		\
   4 + TS_PACKET_ADAPTATION_FIELD_SIZE (packet) + 1 :	\
   4)

#define TS_PACKET_PAYLOAD(packet)			\
  (TS_PACKET_HAS_PAYLOAD (packet) ?			\
   (packet) + TS_PACKET_PAYLOAD_OFFSET(packet) :	\
   NULL)

/* PES Packet */

#define PES_IS_VALID(pes) ((pes)[0] == 0x00 &&	\
			   (pes)[1] == 0x00 &&	\
			   (pes)[2] == 0x01)

#define PES_STREAM_ID(pes) ((pes)[3])

#define PES_PACKET_LENGTH(pes)			\
  ((guint16) (((pes)[4] << 8) | (pes)[5]))

#define PES_STREAM_TYPE_HAS_HEADER(stream_type) \
  (stream_type != 0xac)

#define PES_HEADER_DATA_LENGTH(pes) ((pes)[8])
#define PES_PAYLOAD_DATA_OFFSET(pes) \
  (9 + PES_HEADER_DATA_LENGTH (pes))

#define PES_HAS_PTS(pes) ((pes)[7] & 0x80)
#define PES_HAS_DTS(pes) ((pes)[7] & 0x40)

/* SI/PSI Packet */

#define TS_SECTION_POINTER(payload) ((payload)[0])
#define TS_PACKET_GET_SECTION(payload) ((payload) + TS_SECTION_POINTER(payload))

/* PAT section */
#define PAT_PROGRAM_OFFSET(pat, idx) \
  (7 + (idx) * 4)
#define PAT_PROGRAM_PID_OFFSET(pat, idx) \
  (PAT_PROGRAM_PID_OFFSET(pat,idx) + 2)
#define PAT_GET_PROGRAM_PID(pat, idx) \
  (pat[ + PAT_PROGRAM_OFFSET(pat, idx) + 2)

static inline gboolean
read_ts (const guint8 * data, guint64 * target)
{
/* sync:4 == 00xx ! pts:3 ! 1 ! pts:15 ! 1 | pts:15 ! 1 */
  if ((*data & 0x01) != 0x01)
    return FALSE;
  *target = ((guint64) (*data++ & 0x0E)) << 29;
  *target |= ((guint64) (*data++)) << 22;
  if ((*data & 0x01) != 0x01)
    return FALSE;
  *target |= ((guint64) (*data++ & 0xFE)) << 14;
  *target |= ((guint64) (*data++)) << 7;
  if ((*data & 0x01) != 0x01)
    return FALSE;
  *target |= ((guint64) (*data++ & 0xFE)) >> 1;

  return TRUE;
}

#define PES_PTS_OFFSET(pes) (9)
#define PES_PTS(pes, dest) (read_ts ((pes) + PES_PTS_OFFSET(pes), dest))

#define PES_DTS_OFFSET(pes) (PES_HAS_PTS(pes) ? 9 + 5 : 9)
#define PES_DTS(pes, dest) (read_ts ((pes) + PES_DTS_OFFSET(pes), dest))


/* Check for sync byte, error_indicator == 0 and packet has payload.
 * Adaptation control field (data[3] & 0x30) may be zero for TS packets with
 * null PIDs. Still, these streams are valid TS streams (for null packets,
 * AFC is supposed to be 0x1, but the spec also says decoders should just
 * discard any packets with AFC = 0x00) */
#define IS_MPEGTS_HEADER(data) (data[0] == 0x47 && \
                                (data[1] & 0x80) == 0x00 && \
                                ((data[3] & 0x30) != 0x00 || \
                                ((data[3] & 0x30) == 0x00 && (data[1] & 0x1f) == 0x1f && (data[2] & 0xff) == 0xff)))

#define PCRTIME_TO_GSTTIME(t) (((t) * (guint64)1000) / 27)
#define MPEGTIME_TO_GSTTIME(t) (((t) * (guint64)100000) / 9)

static gboolean
have_ts_sync (const guint8 * data, guint size, guint packet_size, guint num)
{
  while (num-- > 0) {
    if (size < packet_size)
      return FALSE;
    if (!IS_MPEGTS_HEADER (data))
      return FALSE;
    data += packet_size;
    size -= packet_size;
  }
  return TRUE;
}

#define GST_MPEGTS_TYPEFIND_MIN_HEADERS 4

static gint
find_offset (const guint8 * data, guint size, guint * out_packet_size)
{
  guint sync_points = CLAMP (size / 188, GST_MPEGTS_TYPEFIND_MIN_HEADERS, 100);
  guint off;
  const gint packet_size = 188;

  /* FIXME: check 192 as well, and maybe also 204, 208 */
  for (off = 0; off < MIN (size, 1024); ++off) {
    if (have_ts_sync (data + off, size - off, packet_size, sync_points)) {
      *out_packet_size = packet_size;
      return off;
    }
  }
  return -1;
}

static gboolean
handle_pmt (const guint8 * data, guint size, guint packet_size)
{
  const guint8 *p = data;
  guint32 hdr = GST_READ_UINT32_BE (p);
  guint slen, pcr_pid, pilen;

  GST_MEMDUMP ("PMT", data, size);
  data = p + 4;
  if ((hdr & 0x00000020) != 0)  /* has_adaptation_field */
    data += 1 + p[4];           /* adaptation_field_len */
  data += 1 + data[0];          /* pointer_field */
  if (data[0] != 0x02)          /* table_id */
    return FALSE;
  //gst_util_dump_mem (data, 8);
  /* we assume the entire PMT fits into a single packet and this is it */
  if (data[6] != 0 || data[6] != data[7])
    return FALSE;
  slen = GST_READ_UINT16_BE (data + 1) & 0x0FFF;
  if (slen > (gsize) (p + packet_size - (data + 1 + 2)) || slen < 5 + 2 + 4)
    return FALSE;
  data += 3 + 5;
  slen -= 5;                    /* bytes after section_length field itself */
  slen -= 4;                    /* crc at end */
  pcr_pid = GST_READ_UINT16_BE (data) & 0x1fff;
  if (pcr_pid != 0x1fff) {
    GST_DEBUG ("pcr_pid: %04x", pcr_pid);
  }
  data += 2;
  /* Skip global descriptors */
  pilen = GST_READ_UINT16_BE (data + 1) & 0x0FFF;
  data += 2 + pilen;


  return FALSE;
}

static gboolean
pat_get_pmt_pid (const guint8 * data, guint size, guint packet_size,
    gint * pmt_pid)
{
  const guint8 *p = data;
  guint32 hdr = GST_READ_UINT32_BE (p);
  guint slen;

  data = p + 4;
  if ((hdr & 0x00000020) != 0)  /* has_adaptation_field */
    data += 1 + p[4];           /* adaptation_field_len */
  data += 1 + data[0];          /* pointer_field */
  if (data[0] != 0)             /* table_id */
    return FALSE;
  /* we assume the entire PAT fits into a single packet and this is it */
  if (data[6] != 0 || data[6] != data[7])
    return FALSE;
  slen = GST_READ_UINT16_BE (data + 1) & 0x0FFF;
  if (slen > (gsize) (p + packet_size - (data + 1 + 2)) || slen < 5 + 4 + 4)
    return FALSE;
  data += 3 + 5;
  slen -= 5;                    /* bytes after section_length field itself */
  slen -= 4;                    /* crc at end */
  while (slen >= 4) {
    guint program_num = GST_READ_UINT16_BE (data);
    guint val = GST_READ_UINT16_BE (data + 2) & 0x1fff;
    if (program_num != 0) {
      GST_DEBUG ("  program %04x: pmt_pid : %04x", program_num, val);
      *pmt_pid = val;
      return TRUE;
    }
    data += 4;
    slen -= 4;
  }

  return FALSE;
}

static GstClockTime
get_first_mpegts_time (const guint8 * data, gsize size, guint packet_size)
{
  GstClockTime internal_time = GST_CLOCK_TIME_NONE;
  const guint8 *p;
  gint pmt_pid = -1;

  for (p = data; size >= packet_size; p += packet_size, size -= packet_size) {
    if (p[0] != TS_PACKET_SYNC_BYTE) {
      GST_WARNING ("Lost sync");
      break;
    }

    /* We only care about start packets which have some form of payload (pes or
       section) */
    if (TS_PACKET_PAYLOAD_UNIT_START (p) && TS_PACKET_HAS_PAYLOAD (p)) {
      guint16 pid;
      const guint8 *payload;
      const guint8 *afc;

      /* Skip packets which have error indicator set or are scrambled */
      if (G_UNLIKELY (TS_PACKET_TRANSPORT_ERROR_INDICATOR (p) ||
              TS_PACKET_TRANSPORT_SCRAMBLING_CONTROL (p)))
        continue;

      pid = TS_PACKET_PID (p);
      payload = TS_PACKET_PAYLOAD (p);
      afc = TS_PACKET_ADAPTATION_FIELD (p);

      GST_LOG ("PID 0x%04x", pid);
      if (afc && afc[0])
        GST_MEMDUMP ("afc", afc, afc[0]);
      GST_MEMDUMP ("payload", payload, 32);
      if (pmt_pid != -1 && PES_IS_VALID (payload)) {
        guint64 ts;
        GstClockTime pts, dts;

        pts = dts = GST_CLOCK_TIME_NONE;

        GST_DEBUG ("PID 0x%04x stream_id 0x%02x PES start", pid,
            PES_STREAM_ID (payload));
        GST_MEMDUMP ("PES data", payload + PES_PAYLOAD_DATA_OFFSET (payload),
            32);

        /* Grab PTS/DTS */
        if (PES_HAS_PTS (payload) && PES_PTS (payload, &ts)) {
          pts = MPEGTIME_TO_GSTTIME (ts);
          GST_LOG ("PID 0x%04x PTS %" G_GUINT64_FORMAT " %" GST_TIME_FORMAT,
              pid, ts, GST_TIME_ARGS (pts));
        }
        if (PES_HAS_DTS (payload) && PES_DTS (payload, &ts)) {
          dts = MPEGTIME_TO_GSTTIME (ts);
          GST_LOG ("PID 0x%04x DTS %" G_GUINT64_FORMAT " %" GST_TIME_FORMAT,
              pid, ts, GST_TIME_ARGS (dts));
        }

        /* Pick the lowest value */
        if (GST_CLOCK_TIME_IS_VALID (dts)) {
          if (GST_CLOCK_TIME_IS_VALID (pts)) {
            /* Only take the PTS if it's lower than the dts and does not differ
             * by more than a second (which would indicate bogus values) */
            if (pts < dts && ABS (pts - dts) < GST_SECOND)
              internal_time = pts;
            else
              internal_time = dts;
          } else {
            internal_time = dts;
          }
          goto out;
        } else if (GST_CLOCK_TIME_IS_VALID (pts)) {
          internal_time = pts;
          goto out;
        }
      } else if (pid == 0x00) {
        GST_DEBUG ("PAT !");
        if (!pat_get_pmt_pid (p, packet_size, packet_size, &pmt_pid)) {
          GST_WARNING ("Invalid PAT");
          goto out;
        }
      } else if (pmt_pid != -1 && pid == pmt_pid) {
        GST_DEBUG ("PMT !");
        /* FIXME : Grab the list of *actual* elementary stream PID to make sure
         * we have checked the first PTS of each stream (and not just the first
         * one we saw, which might not be the smallest */
        handle_pmt (p, packet_size, packet_size);
      }
    }
  }

out:
  return internal_time;
}

GstHLSParserResult
gst_hlsdemux_handle_content_mpegts (GstHLSDemux * demux,
    GstHLSDemuxStream * hls_stream, gboolean draining, GstBuffer ** buffer)
{
  GstMapInfo info;
  gint offset;
  const guint8 *data;
  GstClockTime internal_time = GST_CLOCK_TIME_NONE;
  guint packet_size;
  gsize size;

  if (!gst_buffer_map (*buffer, &info, GST_MAP_READ))
    return GST_HLS_PARSER_RESULT_ERROR;

  data = info.data;
  size = info.size;

  offset = find_offset (data, size, &packet_size);
  if (offset < 0) {
    gst_buffer_unmap (*buffer, &info);
    return GST_HLS_PARSER_RESULT_ERROR;
  }

  GST_LOG ("TS packet start offset: %d", offset);

  data += offset;
  size -= offset;

  internal_time = get_first_mpegts_time (data, size, packet_size);

  GST_DEBUG_OBJECT (hls_stream, "Using internal time %" GST_TIME_FORMAT,
      GST_TIME_ARGS (internal_time));

  gst_buffer_unmap (*buffer, &info);

  if (!GST_CLOCK_TIME_IS_VALID (internal_time))
    return GST_HLS_PARSER_RESULT_NEED_MORE_DATA;

  /* We have the first internal time, figure out if we are in sync or not */
  return gst_hlsdemux_stream_handle_internal_time (hls_stream, internal_time);
}

GstHLSParserResult
gst_hlsdemux_handle_content_isobmff (GstHLSDemux * demux,
    GstHLSDemuxStream * hls_stream, gboolean draining, GstBuffer ** buffer)
{
  GstMapInfo info;
  GstByteReader br, sub;
  guint32 box_type;
  guint header_size;
  guint64 box_size;
  GstHLSParserResult ret = GST_HLS_PARSER_RESULT_NEED_MORE_DATA;
  GstClockTime smallest_ts = GST_CLOCK_TIME_NONE;

  if (!gst_buffer_map (*buffer, &info, GST_MAP_READ))
    return GST_HLS_PARSER_RESULT_ERROR;

  gst_byte_reader_init (&br, info.data, info.size);

  while (gst_byte_reader_get_remaining (&br) &&
      gst_isoff_parse_box_header (&br, &box_type, NULL, &header_size,
          &box_size)) {
    GST_DEBUG ("box %" GST_FOURCC_FORMAT " size:%" G_GUINT64_FORMAT,
        GST_FOURCC_ARGS (box_type), box_size);

    GST_MEMDUMP ("box content", br.data + br.byte, MIN (256,
            box_size - header_size));

    switch (box_type) {
      case GST_ISOFF_FOURCC_MOOV:
      {
        GstMoovBox *moov;
        gst_byte_reader_get_sub_reader (&br, &sub, box_size - header_size);
        moov = gst_isoff_moov_box_parse (&sub);

        if (moov) {
          GST_DEBUG ("Got moov box");
          if (hls_stream->moov)
            gst_isoff_moov_box_free (hls_stream->moov);
          hls_stream->moov = moov;
        }
        break;
      }
      case GST_ISOFF_FOURCC_MOOF:
      {
        GstMoofBox *moof;

        if (hls_stream->moov == NULL) {
          GST_WARNING ("Received moof with moov in iso-ff stream");
          break;
        }

        gst_byte_reader_get_sub_reader (&br, &sub, box_size - header_size);

        moof = gst_isoff_moof_box_parse (&sub);

        if (moof) {
          guint i, j;
          GST_DEBUG ("Got moof box");
          /* Use the track information from stream->moov */
          for (i = 0; i < hls_stream->moov->trak->len; i++) {
            GstTrakBox *trak =
                &g_array_index (hls_stream->moov->trak, GstTrakBox, i);
            GST_LOG ("trak #%d %p", i, trak);
            for (j = 0; j < moof->traf->len; j++) {
              GstTrafBox *traf = &g_array_index (moof->traf, GstTrafBox, j);
              if (traf->tfhd.track_id == trak->tkhd.track_id) {
                GstClockTime ts = 0;
                guint64 decode_time = traf->tfdt.decode_time;

                if (decode_time != GST_CLOCK_TIME_NONE)
                  ts = gst_util_uint64_scale (decode_time, GST_SECOND,
                      trak->mdia.mdhd.timescale);

                GST_LOG ("Found decode_time %" GST_TIME_FORMAT " for trak %d",
                    GST_TIME_ARGS (ts), traf->tfhd.track_id);
                if (smallest_ts == GST_CLOCK_TIME_NONE || ts < smallest_ts)
                  smallest_ts = ts;
              }
            }
          }
          gst_isoff_moof_box_free (moof);
        } else {
          GST_WARNING ("Failed to parse moof");
        }
        if (smallest_ts != GST_CLOCK_TIME_NONE)
          goto out;
        break;
      }
      case GST_ISOFF_FOURCC_MDAT:
        GST_DEBUG ("Reached `mdat`, returning");
        goto out;
        break;
      default:
        GST_LOG ("Skipping unhandled box %" GST_FOURCC_FORMAT,
            GST_FOURCC_ARGS (box_type));
        gst_byte_reader_skip (&br, box_size - header_size);
        break;
    }

  }

out:
  gst_buffer_unmap (*buffer, &info);

  if (smallest_ts != GST_CLOCK_TIME_NONE) {
    ret = gst_hlsdemux_stream_handle_internal_time (hls_stream, smallest_ts);
  }

  return ret;
}

GstHLSParserResult
gst_hlsdemux_handle_content_id3 (GstHLSDemux * demux,
    GstHLSDemuxStream * hls_stream, gboolean draining, GstBuffer ** buffer)
{
  GstMapInfo info;
  guint32 tag_size;
  gsize size;
  GstTagList *taglist;
  GstSample *priv_data = NULL;
  GstBuffer *tag_buf;
  guint64 pts;
  GstHLSParserResult ret = GST_HLS_PARSER_RESULT_DONE;
  GstClockTime internal;

  /* We need at least 10 bytes, starting with "ID3" for the header */
  size = gst_buffer_get_size (*buffer);
  if (size < 10)
    return GST_HLS_PARSER_RESULT_NEED_MORE_DATA;

  /* Read the tag size */
  tag_size = gst_tag_get_id3v2_tag_size (*buffer);

  /* Check we've collected that much */
  if (size < tag_size)
    return GST_HLS_PARSER_RESULT_NEED_MORE_DATA;

  /* Parse the tag */
  taglist = gst_tag_list_from_id3v2_tag (*buffer);
  if (taglist == NULL) {
    return GST_HLS_PARSER_RESULT_ERROR; /* Invalid tag, stop trying */
  }

  /* Extract the timestamps */
  if (!gst_tag_list_get_sample (taglist, GST_TAG_PRIVATE_DATA, &priv_data))
    goto out;

  if (!g_str_equal ("com.apple.streaming.transportStreamTimestamp",
          gst_structure_get_string (gst_sample_get_info (priv_data), "owner")))
    goto out;

  /* OK, now as per section 3, the tag contains a 33-bit PCR inside a 64-bit
   * BE-word */
  tag_buf = gst_sample_get_buffer (priv_data);
  if (tag_buf == NULL)
    goto out;

  if (!gst_buffer_map (tag_buf, &info, GST_MAP_READ))
    goto out;
  GST_MEMDUMP ("id3 tag", info.data, info.size);

  pts = GST_READ_UINT64_BE (info.data);
  internal = MPEGTIME_TO_GSTTIME (pts);

  GST_LOG ("Got internal PTS from ID3: %" G_GUINT64_FORMAT " (%" GST_TIME_FORMAT
      ")", pts, GST_TIME_ARGS (internal));

  gst_buffer_unmap (tag_buf, &info);

  ret = gst_hlsdemux_stream_handle_internal_time (hls_stream, internal);

out:
  if (priv_data)
    gst_sample_unref (priv_data);
  if (taglist)
    gst_tag_list_unref (taglist);

  return ret;
}

/* Grabs the next numerical value from the bytereader, skipping any spaces.
 *
 * It will stop/return at the next non-digit/non-space position */
static gboolean
byte_reader_get_next_uint_string (GstByteReader * br, guint * out)
{
  guint value = 0;
  gboolean res = FALSE;

  while (gst_byte_reader_get_remaining (br)) {
    guint8 d = gst_byte_reader_peek_uint8_unchecked (br);

    if (g_ascii_isdigit (d)) {
      value = value * 10 + (d - '0');
      res = TRUE;
    } else if (d != ' ' && d != '\t') {
      /* we're done and not advancing */
      break;
    }
    gst_byte_reader_skip_unchecked (br, 1);
  }

  if (res)
    *out = value;

  return res;
}

/* Grabs the next numerical value from the bytereader, skipping any spaces.
 *
 * It will stop/return at the next non-digit/non-space position */
static gboolean
byte_reader_get_next_uint64_string (GstByteReader * br, guint64 * out)
{
  guint64 value = 0;
  gboolean res = FALSE;

  while (gst_byte_reader_get_remaining (br)) {
    guint8 d = gst_byte_reader_peek_uint8_unchecked (br);

    if (g_ascii_isdigit (d)) {
      value = value * 10 + (d - '0');
      res = TRUE;
    } else if (d != ' ' && d != '\t') {
      /* we're done and not advancing */
      break;
    }
    gst_byte_reader_skip_unchecked (br, 1);
  }

  if (res)
    *out = value;

  return res;
}

static gboolean
parse_webvtt_time (GstByteReader * br, GstClockTime * t,
    const gchar ** remainder)
{
  GstClockTime val = 0;
  gboolean res = FALSE;

  while (!res && gst_byte_reader_get_remaining (br)) {
    guint numval;
    if (byte_reader_get_next_uint_string (br, &numval)) {
      guint8 next = gst_byte_reader_peek_uint8_unchecked (br);

      if (next == ':' || next == '.') {
        /* value was hours, minutes or seconds */
        val = val * 60 + numval;
        gst_byte_reader_skip (br, 1);
      } else {
        /* Reached the milliseconds, convert to GstClockTime */
        val = val * GST_SECOND + numval * GST_MSECOND;
        res = TRUE;
      }
    }
  }

  if (res) {
    *t = val;
    if (remainder) {
      if (gst_byte_reader_get_remaining (br))
        *remainder = (const gchar *) gst_byte_reader_peek_data_unchecked (br);
      else
        *remainder = NULL;
    }
  }

  return res;
}

static inline void
br_skipwhitespace (GstByteReader * br)
{
  while (gst_byte_reader_get_remaining (br)) {
    guint8 d = gst_byte_reader_peek_uint8_unchecked (br);
    if (d != ' ' && d != '\t')
      return;
    gst_byte_reader_skip_unchecked (br, 1);
  }
}

/* Returns TRUE if br starts with str
 *
 * Skips any spaces/tabs before and after str */
static gboolean
br_startswith (GstByteReader * br, const gchar * str, gboolean skip_ws)
{
  guint len = strlen (str);
  const guint8 *data;

  if (skip_ws)
    br_skipwhitespace (br);
  if (!gst_byte_reader_peek_data (br, len, &data))
    return FALSE;
  if (strncmp ((gchar *) data, str, len))
    return FALSE;
  gst_byte_reader_skip_unchecked (br, len);
  if (skip_ws)
    br_skipwhitespace (br);

  return TRUE;
}

static gboolean
gst_hls_demux_webvtt_read_x_timestamp_map (gchar * data, GstClockTime * local,
    GstClockTime * mpegts)
{
  GstByteReader br;

  gst_byte_reader_init (&br, (guint8 *) data, strlen (data));

  if (!br_startswith (&br, "X-TIMESTAMP-MAP=", FALSE))
    return FALSE;

  if (br_startswith (&br, "MPEGTS:", TRUE)) {
    if (!byte_reader_get_next_uint64_string (&br, mpegts))
      return FALSE;
    /* Convert to GstClockTime */
    *mpegts = MPEGTIME_TO_GSTTIME (*mpegts);
    if (!br_startswith (&br, ",", TRUE))
      return FALSE;
    if (!br_startswith (&br, "LOCAL:", TRUE))
      return FALSE;
    if (!parse_webvtt_time (&br, local, NULL))
      return FALSE;
  } else if (br_startswith (&br, "LOCAL:", TRUE)) {
    if (!parse_webvtt_time (&br, local, NULL))
      return FALSE;
    if (!br_startswith (&br, ",", TRUE))
      return FALSE;
    if (!br_startswith (&br, "MPEGTS:", TRUE))
      return FALSE;
    if (!byte_reader_get_next_uint64_string (&br, mpegts))
      return FALSE;
    /* Convert to GstClockTime */
    *mpegts = MPEGTIME_TO_GSTTIME (*mpegts);
  } else {
    return FALSE;
  }

  GST_DEBUG ("local time:%" GST_TIME_FORMAT ", mpegts time:%" GST_TIME_FORMAT,
      GST_TIME_ARGS (*local), GST_TIME_ARGS (*mpegts));

  return TRUE;
}

static gboolean
utf8_string_contains_alnum (gchar * string)
{
  gunichar c;

  while ((c = g_utf8_get_char (string))) {
    if (g_unichar_isalnum (c))
      return TRUE;
    string = g_utf8_next_char (string);
  }

  return FALSE;
}

#define T_H(t) ((t) / (GST_SECOND * 60 * 60))
#define T_M(t) ((t) / (GST_SECOND * 60) % 60)
#define T_S(t) ((t) / GST_SECOND % 60)
#define WEBVTT_TIME_FORMAT "02u:%02u:%02u.%03u"
#define WEBVTT_TIME_ARGS(t)			  \
  (guint) ((t) / (GST_SECOND * 60 * 60)) ,	  \
 (guint) ((t) / (GST_SECOND * 60) % 60), \
(guint) ((t) / GST_SECOND % 60),		  \
        (guint) ((t) / GST_MSECOND % 1000)
static gboolean
process_webvtt_cue_timing_setting_line (const gchar * input,
    GstClockTime * start, GstClockTime * stop, const gchar ** cue_settings)
{
  GstByteReader br;

  gst_byte_reader_init (&br, (guint8 *) input, strlen (input));

  /* Handle cue timing start */
  if (!parse_webvtt_time (&br, start, NULL))
    return FALSE;

  /* --> */
  if (gst_byte_reader_get_remaining (&br) < 12 ||
      g_ascii_strncasecmp ((const gchar *)
          gst_byte_reader_peek_data_unchecked (&br), "-->", 3))
    return FALSE;

  gst_byte_reader_skip (&br, 4);

  /* Handle cue timing stop */
  if (!parse_webvtt_time (&br, stop, cue_settings))
    return FALSE;

  return TRUE;
}

static GstClockTimeDiff
convert_webvtt_to_stream_time (GstHLSTimeMap * map, GstClockTime localtime,
    GstClockTime mpegtime, GstClockTime vtt_value)
{
  GstClockTimeDiff res;

  if (localtime == GST_CLOCK_TIME_NONE || mpegtime == GST_CLOCK_TIME_NONE) {
    GST_DEBUG ("No X-TIMESTAMP-MAP, assuming values are MPEG-TS values");
    res = gst_hls_internal_to_stream_time (map, vtt_value);

    /* VTT only uses positive values */
    if (res < 0)
      res = 0;
  } else {
    GST_DEBUG ("Converting %" GST_TIME_FORMAT,
        GST_TIME_ARGS (vtt_value + mpegtime - localtime));
    res =
        gst_hls_internal_to_stream_time (map, vtt_value + mpegtime - localtime);
    if (res == GST_CLOCK_STIME_NONE) {
      GST_WARNING ("Couldn't convert value, using original value %"
          GST_TIME_FORMAT, GST_TIME_ARGS (vtt_value));
      res = vtt_value;
    } else if (res < 0) {
      res = 0;
    }
  }

  return res;
}

GstHLSParserResult
gst_hlsdemux_handle_content_webvtt (GstHLSDemux * demux,
    GstHLSDemuxStream * hls_stream, gboolean draining, GstBuffer ** buffer)
{
  GstHLSParserResult ret = GST_HLS_PARSER_RESULT_DONE;
  gchar *original_content;
  guint i, nb;
  gchar **original_lines;
  GstClockTime localtime = GST_CLOCK_TIME_NONE;
  GstClockTime mpegtime = GST_CLOCK_TIME_NONE;
  GstClockTime low_stream_time = GST_CLOCK_STIME_NONE;
  GstClockTime high_stream_time = GST_CLOCK_STIME_NONE;
  gboolean found_timing = FALSE;
  gboolean found_text = FALSE;
  GPtrArray *builder;
  GstM3U8MediaSegment *current_segment = hls_stream->current_segment;
  GstClockTimeDiff segment_start, segment_end;
  GstClockTimeDiff tolerance;
  gboolean out_of_bounds = FALSE;
  GstHLSTimeMap *map;

  /* We only process full webvtt fragments */
  if (!draining)
    return GST_HLS_PARSER_RESULT_NEED_MORE_DATA;

  original_content = gst_hls_buf_to_utf8_text (*buffer);

  if (!original_content)
    return GST_HLS_PARSER_RESULT_ERROR;

  segment_start = current_segment->stream_time;
  segment_end = segment_start + current_segment->duration;
  tolerance = MAX (current_segment->duration / 2, 500 * GST_MSECOND);

  map = gst_hls_demux_find_time_map (demux, current_segment->discont_sequence);

  builder = g_ptr_array_new_with_free_func (g_free);

  original_lines = g_strsplit_set (original_content, "\n\r", 0);
  nb = g_strv_length (original_lines);

  for (i = 0; i < nb; i++) {
    gchar *line = original_lines[i];

    GST_LOG ("Line: %s", line);

    if (g_str_has_prefix (line, "X-TIMESTAMP-MAP=")) {
      if (!gst_hls_demux_webvtt_read_x_timestamp_map (line, &localtime,
              &mpegtime)) {
        GST_WARNING ("webvtt timestamp map isn't valid");
        ret = GST_HLS_PARSER_RESULT_ERROR;
        goto out;
      }
      g_ptr_array_add (builder, g_strdup (line));
    } else if (strstr (line, " --> ")) {
      GstClockTime start, stop;
      const gchar *leftover;
      if (process_webvtt_cue_timing_setting_line (line, &start, &stop,
              &leftover)) {
        GstClockTimeDiff start_stream, stop_stream;
        gchar *newline;

        GST_LOG ("Found time line %" GST_TIME_FORMAT " -> %" GST_TIME_FORMAT,
            GST_TIME_ARGS (start), GST_TIME_ARGS (stop));

        start_stream =
            convert_webvtt_to_stream_time (map, localtime, mpegtime, start);
        stop_stream =
            convert_webvtt_to_stream_time (map, localtime, mpegtime, stop);

        GST_LOG ("Stream time %" GST_STIME_FORMAT " --> %" GST_STIME_FORMAT,
            GST_STIME_ARGS (start_stream), GST_STIME_ARGS (stop_stream));

        if (stop_stream < (segment_start - tolerance) ||
            start_stream > (segment_end + tolerance)) {
          GST_WARNING ("Out of bounds");
          out_of_bounds = TRUE;
        }
        if (low_stream_time == GST_CLOCK_STIME_NONE
            || stop_stream < low_stream_time)
          low_stream_time = stop_stream;
        if (high_stream_time == GST_CLOCK_STIME_NONE
            || start_stream > high_stream_time)
          high_stream_time = start_stream;

        /* Apply the stream presentation offset */
        start_stream += hls_stream->presentation_offset;
        stop_stream += hls_stream->presentation_offset;

        /* Create the time-shifted WebVTT cue line */
        if (leftover) {
          newline =
              g_strdup_printf ("%" WEBVTT_TIME_FORMAT " --> %"
              WEBVTT_TIME_FORMAT " %s", WEBVTT_TIME_ARGS (start_stream),
              WEBVTT_TIME_ARGS (stop_stream), leftover);
        } else {
          newline =
              g_strdup_printf ("%" WEBVTT_TIME_FORMAT " --> %"
              WEBVTT_TIME_FORMAT, WEBVTT_TIME_ARGS (start_stream),
              WEBVTT_TIME_ARGS (stop_stream));
        }
        GST_LOG ("Generated line '%s'", newline);
        g_ptr_array_add (builder, newline);
        found_timing = TRUE;
      } else {
        GST_WARNING ("Failed to parse time line '%s'", line);
        /* Abort ? */
      }
    } else if (found_timing && !found_text) {
      gchar *linecopy = g_strdup (line);
      g_ptr_array_add (builder, linecopy);
      if (utf8_string_contains_alnum (linecopy)) {
        GST_DEBUG ("Non-empty line '%s'", line);
        found_text = TRUE;
      }
    } else {
      g_ptr_array_add (builder, g_strdup (line));
    }
  }

out:
  if (ret) {
    gchar *newfile;

    /* Ensure file always ends with an empty newline by adding an empty
     * line. This helps downstream parsers properly detect entries */
    g_ptr_array_add (builder, g_strdup ("\n"));
    /* Add NULL-terminator to string list */
    g_ptr_array_add (builder, NULL);
    newfile = g_strjoinv ("\n", (gchar **) builder->pdata);
    GST_DEBUG ("newfile:\n%s", newfile);
    gst_buffer_unref (*buffer);
    *buffer = gst_buffer_new_wrapped (newfile, strlen (newfile));
  }

  GST_DEBUG_OBJECT (hls_stream,
      "Stream time %" GST_STIME_FORMAT " -> %" GST_STIME_FORMAT,
      GST_STIME_ARGS (low_stream_time), GST_STIME_ARGS (high_stream_time));

  g_ptr_array_unref (builder);

  g_strfreev (original_lines);
  g_free (original_content);

  if (out_of_bounds) {

    /* The computed stream time falls outside of the guesstimated stream time,
     * reassess which segment we really are in */
    GST_WARNING ("Cue %" GST_STIME_FORMAT " -> %" GST_STIME_FORMAT
        " is outside of segment %" GST_STIME_FORMAT " -> %"
        GST_STIME_FORMAT, GST_STIME_ARGS (low_stream_time),
        GST_STIME_ARGS (high_stream_time),
        GST_STIME_ARGS (current_segment->stream_time),
        GST_STIME_ARGS (current_segment->stream_time +
            current_segment->duration));

    GstM3U8SeekResult seek_result;

    if (gst_hls_media_playlist_find_position (hls_stream->playlist,
            low_stream_time, hls_stream->in_partial_segments, &seek_result)) {
      g_assert (seek_result.segment != current_segment);
      GST_DEBUG_OBJECT (hls_stream,
          "Stream time corresponds to segment %" GST_STIME_FORMAT
          " duration %" GST_TIME_FORMAT,
          GST_STIME_ARGS (seek_result.segment->stream_time),
          GST_TIME_ARGS (seek_result.segment->duration));

      /* When we land in the middle of a partial segment, actually
       * use the full segment position to resync the playlist */
      if (seek_result.found_partial_segment) {
        hls_stream->current_segment->stream_time =
            seek_result.segment->stream_time;
      } else {
        hls_stream->current_segment->stream_time = seek_result.stream_time;
      }

      /* Recalculate everything and ask parent class to restart */
      gst_hls_media_playlist_recalculate_stream_time (hls_stream->playlist,
          hls_stream->current_segment);
      gst_m3u8_media_segment_unref (seek_result.segment);
      ret = GST_HLS_PARSER_RESULT_RESYNC;
    }
  }

  if (!found_text) {
    GST_DEBUG_OBJECT (hls_stream, "Replacing buffer with droppable buffer");

    GST_BUFFER_PTS (*buffer) =
        current_segment->stream_time + hls_stream->presentation_offset;
    GST_BUFFER_DURATION (*buffer) = current_segment->duration;

    gst_buffer_set_flags (*buffer, GST_BUFFER_FLAG_DROPPABLE);
  }

  return ret;
}

/* Get a utf8-validated string of the contents of the buffer */
gchar *
gst_hls_buf_to_utf8_text (GstBuffer * buf)
{
  GstMapInfo info;
  gchar *playlist;

  if (!gst_buffer_map (buf, &info, GST_MAP_READ))
    goto map_error;

  if (!g_utf8_validate ((gchar *) info.data, info.size, NULL))
    goto validate_error;

  /* alloc size + 1 to end with a null character */
  playlist = g_malloc0 (info.size + 1);
  memcpy (playlist, info.data, info.size);

  gst_buffer_unmap (buf, &info);
  return playlist;

validate_error:
  gst_buffer_unmap (buf, &info);
map_error:
  return NULL;
}
