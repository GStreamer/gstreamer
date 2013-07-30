/* GStreamer ASF/WMV/WMA demuxer
 * Copyright (C) 2007 Tim-Philipp Müller <tim centricular net>
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

/* FIXME:
 *  file:///home/tpm/samples/video/asf//336370-regis-velo862.wmv
 *  file:///home/tpm/samples/video/asf//336370-eichhoer.wmv
 * throw errors (not always necessarily) in this code path
 * (looks like they carry broken payloads/packets though) */

#include "asfpacket.h"

#include <gst/gstutils.h>
#include <gst/gstinfo.h>
#include <string.h>

/* we are unlikely to deal with lengths > 2GB here any time soon, so just
 * return a signed int and use that for error reporting */
static inline gint
asf_packet_read_varlen_int (guint lentype_flags, guint lentype_bit_offset,
    const guint8 ** p_data, guint * p_size)
{
  static const guint lens[4] = { 0, 1, 2, 4 };
  guint len, val;

  len = lens[(lentype_flags >> lentype_bit_offset) & 0x03];

  /* will make caller bail out with a short read if there's not enough data */
  if (G_UNLIKELY (*p_size < len)) {
    GST_WARNING ("need %u bytes, but only %u bytes available", len, *p_size);
    return -1;
  }

  switch (len) {
    case 0:
      val = 0;
      break;
    case 1:
      val = GST_READ_UINT8 (*p_data);
      break;
    case 2:
      val = GST_READ_UINT16_LE (*p_data);
      break;
    case 4:
      val = GST_READ_UINT32_LE (*p_data);
      break;
    default:
      val = 0;
      g_assert_not_reached ();
  }

  *p_data += len;
  *p_size -= len;

  return (gint) val;
}

static GstBuffer *
asf_packet_create_payload_buffer (AsfPacket * packet, const guint8 ** p_data,
    guint * p_size, guint payload_len)
{
  guint off;

  g_assert (payload_len <= *p_size);

  off = (guint) (*p_data - packet->bdata);
  g_assert (off < gst_buffer_get_size (packet->buf));

  *p_data += payload_len;
  *p_size -= payload_len;

  return gst_buffer_copy_region (packet->buf, GST_BUFFER_COPY_ALL, off,
      payload_len);
}

static AsfPayload *
asf_payload_find_previous_fragment (AsfPayload * payload, AsfStream * stream)
{
  AsfPayload *ret;

  if (G_UNLIKELY (stream->payloads->len == 0)) {
    GST_DEBUG ("No previous fragments to merge with for stream %u", stream->id);
    return NULL;
  }

  ret =
      &g_array_index (stream->payloads, AsfPayload, stream->payloads->len - 1);

  if (G_UNLIKELY (ret->mo_size != payload->mo_size ||
          ret->mo_number != payload->mo_number || ret->mo_offset != 0)) {
    if (payload->mo_size != 0) {
      GST_WARNING ("Previous fragment does not match continued fragment");
      return NULL;
    } else {
      /* Warn about this case, but accept it anyway: files in the wild sometimes
       * have continued packets where the subsequent fragments say that they're
       * zero-sized. */
      GST_WARNING ("Previous fragment found, but current fragment has "
          "zero size, accepting anyway");
    }
  }
#if 0
  if (this_fragment->mo_offset + this_payload_len > first_fragment->mo_size) {
    GST_WARNING ("Merged fragments would be bigger than the media object");
    return FALSE;
  }
#endif

  return ret;
}

/* TODO: if we have another payload already queued for this stream and that
 * payload doesn't have a duration, maybe we can calculate a duration for it
 * (if the previous timestamp is smaller etc. etc.) */
static void
gst_asf_payload_queue_for_stream (GstASFDemux * demux, AsfPayload * payload,
    AsfStream * stream)
{
  GST_DEBUG_OBJECT (demux, "Got payload for stream %d ts:%" GST_TIME_FORMAT,
      stream->id, GST_TIME_ARGS (payload->ts));

  /* make timestamps start from 0; first_ts will be determined during activation (once we have enough data),
     which will also update ts of all packets queued before we knew first_ts;  */
  if (G_LIKELY (GST_CLOCK_TIME_IS_VALID (demux->first_ts)
          && GST_CLOCK_TIME_IS_VALID (payload->ts))) {
    if (payload->ts > demux->first_ts)
      payload->ts -= demux->first_ts;
    else
      payload->ts = 0;
  }

  /* remove any incomplete payloads that will never be completed */
  while (stream->payloads->len > 0) {
    AsfPayload *prev;
    guint idx_last;

    idx_last = stream->payloads->len - 1;
    prev = &g_array_index (stream->payloads, AsfPayload, idx_last);

    if (G_UNLIKELY (gst_asf_payload_is_complete (prev)))
      break;

    GST_DEBUG_OBJECT (demux, "Dropping incomplete fragmented media object "
        "queued for stream %u", stream->id);

    gst_buffer_replace (&prev->buf, NULL);
    g_array_remove_index (stream->payloads, idx_last);

    /* there's data missing, so there's a discontinuity now */
    GST_BUFFER_FLAG_SET (payload->buf, GST_BUFFER_FLAG_DISCONT);
  }

  /* If we're about to queue a key frame that is before the segment start, we
   * can ditch any previously queued payloads (which would also be before the
   * segment start). This makes sure the decoder doesn't decode more than
   * absolutely necessary after a seek (we don't push out payloads that are
   * before the segment start until we have at least one that falls within the
   * segment) */
  if (G_UNLIKELY (GST_CLOCK_TIME_IS_VALID (payload->ts) &&
          payload->ts < demux->segment.start && payload->keyframe)) {
    GST_DEBUG_OBJECT (demux, "Queueing keyframe before segment start, removing"
        " %u previously-queued payloads, which would be out of segment too and"
        " hence don't have to be decoded", stream->payloads->len);
    while (stream->payloads->len > 0) {
      AsfPayload *last;
      guint idx_last;

      idx_last = stream->payloads->len - 1;
      last = &g_array_index (stream->payloads, AsfPayload, idx_last);
      gst_buffer_replace (&last->buf, NULL);
      g_array_remove_index (stream->payloads, idx_last);
    }

    /* Mark discontinuity (should be done via stream->discont anyway though) */
    GST_BUFFER_FLAG_SET (payload->buf, GST_BUFFER_FLAG_DISCONT);
  }

  /* remember the first queued timestamp for the segment */
  if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (demux->segment_ts) &&
          GST_CLOCK_TIME_IS_VALID (demux->first_ts))) {
    GST_DEBUG_OBJECT (demux, "segment ts: %" GST_TIME_FORMAT,
        GST_TIME_ARGS (demux->first_ts));
    demux->segment_ts = demux->first_ts;
    /* always note, but only determines segment when streaming */
    if (demux->streaming)
      gst_segment_do_seek (&demux->segment, demux->in_segment.rate,
          GST_FORMAT_TIME, (GstSeekFlags) demux->segment.flags,
          GST_SEEK_TYPE_SET, demux->segment_ts, GST_SEEK_TYPE_NONE, 0, NULL);
  }

  g_array_append_vals (stream->payloads, payload, 1);
}

static void
asf_payload_parse_replicated_data_extensions (AsfStream * stream,
    AsfPayload * payload)
{
  AsfPayloadExtension *ext;
  guint off;
  guint16 ext_len;

  if (!stream->ext_props.valid || stream->ext_props.payload_extensions == NULL)
    return;

  off = 8;
  for (ext = stream->ext_props.payload_extensions; ext->len > 0; ++ext) {
    ext_len = ext->len;
    if (ext_len == 0xFFFF) {    /* extension length is determined by first two bytes in replicated data */
      ext_len = GST_READ_UINT16_LE (payload->rep_data + off);
      off += 2;
    }
    if (G_UNLIKELY (off + ext_len > payload->rep_data_len)) {
      GST_WARNING ("not enough replicated data for defined extensions");
      return;
    }
    switch (ext->id) {
      case ASF_PAYLOAD_EXTENSION_DURATION:
        if (G_LIKELY (ext_len == 2)) {
          guint16 tdur = GST_READ_UINT16_LE (payload->rep_data + off);
          /* packet durations of 1ms are mostly invalid */
          if (tdur != 1)
            payload->duration = tdur * GST_MSECOND;
        } else {
          GST_WARNING ("unexpected DURATION extensions len %u", ext_len);
        }
        break;
      case ASF_PAYLOAD_EXTENSION_SYSTEM_CONTENT:
        if (G_LIKELY (ext_len == 1)) {
          guint8 data = payload->rep_data[off];

          payload->interlaced = data & 0x1;
          payload->rff = data & 0x8;
          payload->tff = (data & 0x2) || !(data & 0x4);
          GST_DEBUG ("SYSTEM_CONTENT: interlaced:%d, rff:%d, tff:%d",
              payload->interlaced, payload->rff, payload->tff);
        } else {
          GST_WARNING ("unexpected SYSTEM_CONTE extensions len %u", ext_len);
        }
        break;
      case ASF_PAYLOAD_EXTENSION_SYSTEM_PIXEL_ASPECT_RATIO:
        if (G_LIKELY (ext_len == 2)) {
          payload->par_x = payload->rep_data[off];
          payload->par_y = payload->rep_data[off + 1];
          GST_DEBUG ("PAR %d / %d", payload->par_x, payload->par_y);
        } else {
          GST_WARNING ("unexpected SYSTEM_PIXEL_ASPECT_RATIO extensions len %u",
              ext_len);
        }
        break;
      case ASF_PAYLOAD_EXTENSION_TIMING:
      {
        /* dvr-ms timing - this will override packet timestamp */
        guint64 time = GST_READ_UINT64_LE (payload->rep_data + off + 8);
        if (time != 0xFFFFFFFFFFFFFFFF)
          payload->ts = time * 100;
        else
          payload->ts = GST_CLOCK_TIME_NONE;
      }
        break;
      default:
        GST_LOG ("UNKNOWN PAYLOAD EXTENSION!");
        break;
    }
    off += ext_len;
  }
}

static gboolean
gst_asf_demux_parse_payload (GstASFDemux * demux, AsfPacket * packet,
    gint lentype, const guint8 ** p_data, guint * p_size)
{
  AsfPayload payload = { 0, };
  AsfStream *stream;
  gboolean is_compressed;
  guint payload_len;
  guint stream_num;

  if (G_UNLIKELY (*p_size < 1)) {
    GST_WARNING_OBJECT (demux, "Short packet!");
    return FALSE;
  }

  stream_num = GST_READ_UINT8 (*p_data) & 0x7f;
  payload.keyframe = ((GST_READ_UINT8 (*p_data) & 0x80) != 0);

  *p_data += 1;
  *p_size -= 1;

  payload.ts = GST_CLOCK_TIME_NONE;
  payload.duration = GST_CLOCK_TIME_NONE;
  payload.par_x = 0;
  payload.par_y = 0;
  payload.interlaced = FALSE;
  payload.tff = FALSE;
  payload.rff = FALSE;

  payload.mo_number =
      asf_packet_read_varlen_int (packet->prop_flags, 4, p_data, p_size);
  payload.mo_offset =
      asf_packet_read_varlen_int (packet->prop_flags, 2, p_data, p_size);
  payload.rep_data_len =
      asf_packet_read_varlen_int (packet->prop_flags, 0, p_data, p_size);

  is_compressed = (payload.rep_data_len == 1);

  GST_LOG_OBJECT (demux, "payload for stream %u", stream_num);
  GST_LOG_OBJECT (demux, "keyframe   : %s", (payload.keyframe) ? "yes" : "no");
  GST_LOG_OBJECT (demux, "compressed : %s", (is_compressed) ? "yes" : "no");

  if (G_UNLIKELY (*p_size < payload.rep_data_len)) {
    GST_WARNING_OBJECT (demux, "Short packet! rep_data_len=%u, size=%u",
        payload.rep_data_len, *p_size);
    return FALSE;
  }

  memcpy (payload.rep_data, *p_data,
      MIN (sizeof (payload.rep_data), payload.rep_data_len));

  *p_data += payload.rep_data_len;
  *p_size -= payload.rep_data_len;

  if (G_UNLIKELY (*p_size == 0)) {
    GST_WARNING_OBJECT (demux, "payload without data!?");
    return FALSE;
  }

  /* we use -1 as lentype for a single payload that's the size of the packet */
  if (G_UNLIKELY ((lentype >= 0 && lentype <= 3))) {
    payload_len = asf_packet_read_varlen_int (lentype, 0, p_data, p_size);
    if (*p_size < payload_len) {
      GST_WARNING_OBJECT (demux, "Short packet! payload_len=%u, size=%u",
          payload_len, *p_size);
      return FALSE;
    }
  } else {
    payload_len = *p_size;
  }

  GST_LOG_OBJECT (demux, "payload length: %u", payload_len);

  stream = gst_asf_demux_get_stream (demux, stream_num);

  if (G_UNLIKELY (stream == NULL)) {
    if (gst_asf_demux_is_unknown_stream (demux, stream_num)) {
      GST_WARNING_OBJECT (demux, "Payload for unknown stream %u, skipping",
          stream_num);
    }
    if (*p_size < payload_len) {
      *p_data += *p_size;
      *p_size = 0;
    } else {
      *p_data += payload_len;
      *p_size -= payload_len;
    }
    return TRUE;
  }

  if (G_UNLIKELY (!is_compressed)) {
    GST_LOG_OBJECT (demux, "replicated data length: %u", payload.rep_data_len);

    if (payload.rep_data_len >= 8) {
      payload.mo_size = GST_READ_UINT32_LE (payload.rep_data);
      payload.ts = GST_READ_UINT32_LE (payload.rep_data + 4) * GST_MSECOND;
      if (G_UNLIKELY (payload.ts < demux->preroll))
        payload.ts = 0;
      else
        payload.ts -= demux->preroll;
      asf_payload_parse_replicated_data_extensions (stream, &payload);

      GST_LOG_OBJECT (demux, "media object size   : %u", payload.mo_size);
      GST_LOG_OBJECT (demux, "media object ts     : %" GST_TIME_FORMAT,
          GST_TIME_ARGS (payload.ts));
      GST_LOG_OBJECT (demux, "media object dur    : %" GST_TIME_FORMAT,
          GST_TIME_ARGS (payload.duration));
    } else if (payload.rep_data_len != 0) {
      GST_WARNING_OBJECT (demux, "invalid replicated data length, very bad");
      *p_data += payload_len;
      *p_size -= payload_len;
      return FALSE;
    }

    GST_LOG_OBJECT (demux, "media object offset : %u", payload.mo_offset);

    GST_LOG_OBJECT (demux, "payload length: %u", payload_len);

    if (payload_len == 0) {
      GST_DEBUG_OBJECT (demux, "skipping empty payload");
    } else if (payload.mo_offset == 0 && payload.mo_size == payload_len) {
      /* if the media object is not fragmented, just create a sub-buffer */
      GST_LOG_OBJECT (demux, "unfragmented media object size %u", payload_len);
      payload.buf = asf_packet_create_payload_buffer (packet, p_data, p_size,
          payload_len);
      payload.buf_filled = payload_len;
      gst_asf_payload_queue_for_stream (demux, &payload, stream);
    } else {
      const guint8 *payload_data = *p_data;

      g_assert (payload_len <= *p_size);

      *p_data += payload_len;
      *p_size -= payload_len;

      /* n-th fragment of a fragmented media object? */
      if (payload.mo_offset != 0) {
        AsfPayload *prev;

        if ((prev = asf_payload_find_previous_fragment (&payload, stream))) {
          if (prev->buf == NULL || payload.mo_size != prev->mo_size ||
              payload.mo_offset >= gst_buffer_get_size (prev->buf) ||
              payload.mo_offset + payload_len >
              gst_buffer_get_size (prev->buf)) {
            GST_WARNING_OBJECT (demux, "Offset doesn't match previous data?!");
          } else {
            /* we assume fragments are payloaded with increasing mo_offset */
            if (payload.mo_offset != prev->buf_filled) {
              GST_WARNING_OBJECT (demux, "media object payload discontinuity: "
                  "offset=%u vs buf_filled=%u", payload.mo_offset,
                  prev->buf_filled);
            }
            gst_buffer_fill (prev->buf, payload.mo_offset,
                payload_data, payload_len);
            prev->buf_filled =
                MAX (prev->buf_filled, payload.mo_offset + payload_len);
            GST_LOG_OBJECT (demux, "Merged media object fragments, size now %u",
                prev->buf_filled);
          }
        } else {
          GST_DEBUG_OBJECT (demux, "n-th payload fragment, but don't have "
              "any previous fragment, ignoring payload");
        }
      } else {
        GST_LOG_OBJECT (demux, "allocating buffer of size %u for fragmented "
            "media object", payload.mo_size);
        payload.buf = gst_buffer_new_allocate (NULL, payload.mo_size, NULL);
        gst_buffer_fill (payload.buf, 0, payload_data, payload_len);
        payload.buf_filled = payload_len;

        gst_asf_payload_queue_for_stream (demux, &payload, stream);
      }
    }
  } else {
    const guint8 *payload_data;
    GstClockTime ts, ts_delta;
    guint num;

    GST_LOG_OBJECT (demux, "Compressed payload, length=%u", payload_len);

    payload_data = *p_data;

    *p_data += payload_len;
    *p_size -= payload_len;

    ts = payload.mo_offset * GST_MSECOND;
    if (G_UNLIKELY (ts < demux->preroll))
      ts = 0;
    else
      ts -= demux->preroll;
    ts_delta = payload.rep_data[0] * GST_MSECOND;

    for (num = 0; payload_len > 0; ++num) {
      guint sub_payload_len;

      sub_payload_len = GST_READ_UINT8 (payload_data);

      GST_LOG_OBJECT (demux, "subpayload #%u: len=%u, ts=%" GST_TIME_FORMAT,
          num, sub_payload_len, GST_TIME_ARGS (ts));

      ++payload_data;
      --payload_len;

      if (G_UNLIKELY (payload_len < sub_payload_len)) {
        GST_WARNING_OBJECT (demux, "Short payload! %u bytes left", payload_len);
        return FALSE;
      }

      if (G_LIKELY (sub_payload_len > 0)) {
        payload.buf = asf_packet_create_payload_buffer (packet,
            &payload_data, &payload_len, sub_payload_len);
        payload.buf_filled = sub_payload_len;

        payload.ts = ts;
        if (G_LIKELY (ts_delta))
          payload.duration = ts_delta;
        else
          payload.duration = GST_CLOCK_TIME_NONE;

        gst_asf_payload_queue_for_stream (demux, &payload, stream);
      }

      ts += ts_delta;
    }
  }

  return TRUE;
}

GstAsfDemuxParsePacketError
gst_asf_demux_parse_packet (GstASFDemux * demux, GstBuffer * buf)
{
  AsfPacket packet = { 0, };
  GstMapInfo map;
  const guint8 *data;
  gboolean has_multiple_payloads;
  GstAsfDemuxParsePacketError ret = GST_ASF_DEMUX_PARSE_PACKET_ERROR_NONE;
  guint8 ec_flags, flags1;
  guint size;

  gst_buffer_map (buf, &map, GST_MAP_READ);
  data = map.data;
  size = map.size;
  GST_LOG_OBJECT (demux, "Buffer size: %u", size);

  /* need at least two payload flag bytes, send time, and duration */
  if (G_UNLIKELY (size < 2 + 4 + 2)) {
    GST_WARNING_OBJECT (demux, "Packet size is < 8");
    ret = GST_ASF_DEMUX_PARSE_PACKET_ERROR_RECOVERABLE;
    goto done;
  }

  packet.buf = buf;
  /* evidently transient */
  packet.bdata = data;

  ec_flags = GST_READ_UINT8 (data);

  /* skip optional error correction stuff */
  if ((ec_flags & 0x80) != 0) {
    guint ec_len_type, ec_len;

    ec_len_type = (ec_flags & 0x60) >> 5;
    if (ec_len_type == 0) {
      ec_len = ec_flags & 0x0f;
    } else {
      GST_WARNING_OBJECT (demux, "unexpected error correction length type %u",
          ec_len_type);
      ec_len = 2;
    }
    GST_LOG_OBJECT (demux, "packet has error correction (%u bytes)", ec_len);

    /* still need at least two payload flag bytes, send time, and duration */
    if (size <= (1 + ec_len) + 2 + 4 + 2) {
      GST_WARNING_OBJECT (demux, "Packet size is < 8 with Error Correction");
      ret = GST_ASF_DEMUX_PARSE_PACKET_ERROR_FATAL;
      goto done;
    }

    data += 1 + ec_len;
    size -= 1 + ec_len;
  }

  /* parse payload info */
  flags1 = GST_READ_UINT8 (data);
  packet.prop_flags = GST_READ_UINT8 (data + 1);

  data += 2;
  size -= 2;

  has_multiple_payloads = (flags1 & 0x01) != 0;

  packet.length = asf_packet_read_varlen_int (flags1, 5, &data, &size);

  packet.sequence = asf_packet_read_varlen_int (flags1, 1, &data, &size);

  packet.padding = asf_packet_read_varlen_int (flags1, 3, &data, &size);

  if (G_UNLIKELY (size < 6)) {
    GST_WARNING_OBJECT (demux, "Packet size is < 6");
    ret = GST_ASF_DEMUX_PARSE_PACKET_ERROR_FATAL;
    goto done;
  }

  packet.send_time = GST_READ_UINT32_LE (data) * GST_MSECOND;
  packet.duration = GST_READ_UINT16_LE (data + 4) * GST_MSECOND;

  data += 4 + 2;
  size -= 4 + 2;

  GST_LOG_OBJECT (demux, "flags            : 0x%x", flags1);
  GST_LOG_OBJECT (demux, "multiple payloads: %u", has_multiple_payloads);
  GST_LOG_OBJECT (demux, "packet length    : %u", packet.length);
  GST_LOG_OBJECT (demux, "sequence         : %u", packet.sequence);
  GST_LOG_OBJECT (demux, "padding          : %u", packet.padding);
  GST_LOG_OBJECT (demux, "send time        : %" GST_TIME_FORMAT,
      GST_TIME_ARGS (packet.send_time));
  GST_LOG_OBJECT (demux, "duration         : %" GST_TIME_FORMAT,
      GST_TIME_ARGS (packet.duration));

  if (G_UNLIKELY (packet.padding == (guint) - 1 || size < packet.padding)) {
    GST_WARNING_OBJECT (demux, "No padding, or padding bigger than buffer");
    ret = GST_ASF_DEMUX_PARSE_PACKET_ERROR_RECOVERABLE;
    goto done;
  }

  size -= packet.padding;

  /* adjust available size for parsing if there's less actual packet data for
   * parsing than there is data in bytes (for sample see bug 431318) */
  if (G_UNLIKELY (packet.length != 0 && packet.padding == 0
          && packet.length < demux->packet_size)) {
    GST_LOG_OBJECT (demux, "shortened packet with implicit padding, "
        "adjusting available data size");
    if (size < demux->packet_size - packet.length) {
      /* the buffer is smaller than the implicit padding */
      GST_WARNING_OBJECT (demux, "Buffer is smaller than the implicit padding");
      ret = GST_ASF_DEMUX_PARSE_PACKET_ERROR_RECOVERABLE;
      goto done;
    } else {
      /* subtract the implicit padding */
      size -= (demux->packet_size - packet.length);
    }
  }

  if (has_multiple_payloads) {
    guint i, num, lentype;

    if (G_UNLIKELY (size < 1)) {
      GST_WARNING_OBJECT (demux, "No room more in buffer");
      ret = GST_ASF_DEMUX_PARSE_PACKET_ERROR_RECOVERABLE;
      goto done;
    }

    num = (GST_READ_UINT8 (data) & 0x3F) >> 0;
    lentype = (GST_READ_UINT8 (data) & 0xC0) >> 6;

    ++data;
    --size;

    GST_LOG_OBJECT (demux, "num payloads     : %u", num);

    for (i = 0; i < num; ++i) {
      GST_LOG_OBJECT (demux, "Parsing payload %u/%u, size left: %u", i + 1, num,
          size);

      if (G_UNLIKELY (!gst_asf_demux_parse_payload (demux, &packet, lentype,
                  &data, &size))) {
        GST_WARNING_OBJECT (demux, "Failed to parse payload %u/%u", i + 1, num);
        ret = GST_ASF_DEMUX_PARSE_PACKET_ERROR_FATAL;
        break;
      }
    }
  } else {
    GST_LOG_OBJECT (demux, "Parsing single payload");
    if (G_UNLIKELY (!gst_asf_demux_parse_payload (demux, &packet, -1, &data,
                &size))) {
      GST_WARNING_OBJECT (demux, "Failed to parse payload");
      ret = GST_ASF_DEMUX_PARSE_PACKET_ERROR_RECOVERABLE;
    }
  }

done:
  gst_buffer_unmap (buf, &map);
  return ret;
}
