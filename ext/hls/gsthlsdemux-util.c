#include <gst/gst.h>
#include <string.h>

#include "gsthlsdemux.h"

GST_DEBUG_CATEGORY_EXTERN (gst_hls_demux_debug);
#define GST_CAT_DEFAULT gst_hls_demux_debug

/* Check for sync byte, error_indicator == 0 and packet has payload.
 * Adaptation control field (data[3] & 0x30) may be zero for TS packets with
 * null PIDs. Still, these streams are valid TS streams (for null packets,
 * AFC is supposed to be 0x1, but the spec also says decoders should just
 * discard any packets with AFC = 0x00) */
#define IS_MPEGTS_HEADER(data) (data[0] == 0x47 && \
                                (data[1] & 0x80) == 0x00 && \
                                ((data[3] & 0x30) != 0x00 || \
                                ((data[3] & 0x30) == 0x00 && (data[1] & 0x1f) == 0x1f && (data[2] & 0xff) == 0xff)))

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

static gint
find_offset (GstHLSTSReader * r, const guint8 * data, guint size)
{
  guint sync_points = CLAMP (size / 188, 25, 100);
  guint off;
  const gint packet_size = 188;

  /* FIXME: check 192 as well, and maybe also 204, 208 */
  for (off = 0; off < MIN (size, packet_size); ++off) {
    if (have_ts_sync (data + off, size - off, packet_size, sync_points)) {
      r->packet_size = packet_size;
      return off;
    }
  }
  return -1;
}

static gboolean
handle_pcr (GstHLSTSReader * r, const guint8 * data, guint size)
{
  const guint8 *p = data;
  guint32 hdr = GST_READ_UINT32_BE (p);
  guint af_len, flags;

  guint64 pcr_base, pcr_ext, pcr, ts;

  data = p + 4;
  if ((hdr & 0x00000020) == 0)  /* has_adaptation_field */
    return FALSE;
  af_len = p[4];                /* adaptation_field_len */
  ++data;
  if (af_len < (1 + 6) || af_len > r->packet_size - (4 + 1))
    return FALSE;
  flags = data[0];
  /* Does the packet have a PCR? */
  if ((flags & 0x10) == 0)
    return FALSE;
  ++data;
  --af_len;
  pcr_base = (GST_READ_UINT64_BE (data) >> 16) >> (6 + 9);
  pcr_ext = (GST_READ_UINT64_BE (data) >> 16) & 0x1ff;
  pcr = pcr_base * 300 + pcr_ext;
#define PCRTIME_TO_GSTTIME(t) (((t) * (guint64)1000) / 27)
  ts = PCRTIME_TO_GSTTIME (pcr);
  GST_LOG ("have PCR! %" G_GUINT64_FORMAT "\t%" GST_TIME_FORMAT,
      pcr, GST_TIME_ARGS (ts));
  if (r->first_pcr == GST_CLOCK_TIME_NONE)
    r->first_pcr = ts;
  r->last_pcr = ts;

  return TRUE;
}

static gboolean
handle_pmt (GstHLSTSReader * r, const guint8 * data, guint size)
{
  const guint8 *p = data;
  guint32 hdr = GST_READ_UINT32_BE (p);
  guint slen, pcr_pid;

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
  if (slen > (gsize) (p + r->packet_size - (data + 1 + 2)) || slen < 5 + 2 + 4)
    return FALSE;
  data += 3 + 5;
  slen -= 5;                    /* bytes after section_length field itself */
  slen -= 4;                    /* crc at end */
  pcr_pid = GST_READ_UINT16_BE (data) & 0x1fff;
  if (pcr_pid != 0x1fff) {
    GST_DEBUG ("pcr_pid now: %04x", pcr_pid);
    r->pcr_pid = pcr_pid;
    return TRUE;
  }

  return FALSE;
}

static gboolean
handle_pat (GstHLSTSReader * r, const guint8 * data, guint size)
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
  if (slen > (gsize) (p + r->packet_size - (data + 1 + 2)) || slen < 5 + 4 + 4)
    return FALSE;
  data += 3 + 5;
  slen -= 5;                    /* bytes after section_length field itself */
  slen -= 4;                    /* crc at end */
  while (slen >= 4) {
    guint program_num = GST_READ_UINT16_BE (data);
    guint val = GST_READ_UINT16_BE (data + 2) & 0x1fff;
    if (program_num != 0) {
      GST_DEBUG ("  program %04x: pmt_pid : %04x\n", program_num, val);
      r->pmt_pid = val;
      return TRUE;
    }
    data += 4;
    slen -= 4;
  }

  return FALSE;
}

void
gst_hlsdemux_tsreader_init (GstHLSTSReader * r)
{
  r->packet_size = 188;
  r->pmt_pid = r->pcr_pid = -1;
  r->first_pcr = GST_CLOCK_TIME_NONE;
  r->last_pcr = GST_CLOCK_TIME_NONE;
}

gboolean
gst_hlsdemux_tsreader_find_pcrs (GstHLSTSReader * r,
    const guint8 * data, guint size, GstClockTime * first_pcr,
    GstClockTime * last_pcr)
{
  gint offset;
  const guint8 *p;

  *first_pcr = *last_pcr = GST_CLOCK_TIME_NONE;

  offset = find_offset (r, data, size);
  if (offset < 0)
    return FALSE;

  GST_LOG ("TS packet start offset: %d", offset);

  data += offset;
  size -= offset;

  for (p = data; size >= r->packet_size;
      p += r->packet_size, size -= r->packet_size) {
    guint32 hdr = GST_READ_UINT32_BE (p);

    /* sync byte (0x47), error indicator (TEI) not set, PID 0, has_payload */
    if ((hdr & 0xFF9FFF10) == 0x47000010) {
      GST_LOG ("Found packet for PID 0000 (PAT)");
      handle_pat (r, p, size);
    }
    /* sync byte (0x47), error indicator (TEI) not set, has_payload, PID = PMT_pid */
    else if ((hdr & 0xFF800010) == 0x47000010
        && ((hdr >> 8) & 0x1fff) == r->pmt_pid) {
      GST_LOG ("Found packet for PID %04x (PMT)", r->pmt_pid);
      handle_pmt (r, p, size);
    }
    /* sync byte (0x47), error indicator (TEI) not set, has_payload */
    else if ((hdr & 0xFF800010) == 0x47000010
        && ((hdr >> 8) & 0x1fff) == r->pcr_pid) {
      GST_LOG ("Found packet for PID %04x (PCR)", r->pcr_pid);
      handle_pcr (r, p, size);
    }
  }

  *first_pcr = r->first_pcr;
  *last_pcr = r->last_pcr;

  /* Return TRUE if this piece was big enough to get a PCR from */
  return (r->first_pcr != GST_CLOCK_TIME_NONE);
}
