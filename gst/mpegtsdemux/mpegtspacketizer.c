/*
 * mpegtspacketizer.c -
 * Copyright (C) 2007, 2008 Alessandro Decina, Zaheer Merali
 *
 * Authors:
 *   Zaheer Merali <zaheerabbas at merali dot org>
 *   Alessandro Decina <alessandro@nnva.org>
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

#include <string.h>
#include <stdlib.h>

/* Skew calculation pameters */
#define MAX_TIME	(2 * GST_SECOND)

/* maximal PCR time */
#define PCR_MAX_VALUE (((((guint64)1)<<33) * 300) + 298)
#define PCR_GST_MAX_VALUE (PCR_MAX_VALUE * GST_MSECOND / (27000))
#define PTS_DTS_MAX_VALUE (((guint64)1) << 33)

#include "mpegtspacketizer.h"
#include "gstmpegdesc.h"

GST_DEBUG_CATEGORY_STATIC (mpegts_packetizer_debug);
#define GST_CAT_DEFAULT mpegts_packetizer_debug

#define MPEGTS_PACKETIZER_GET_PRIVATE(obj)  \
   (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_MPEGTS_PACKETIZER, MpegTSPacketizerPrivate))

static void _init_local (void);
G_DEFINE_TYPE_EXTENDED (MpegTSPacketizer2, mpegts_packetizer, G_TYPE_OBJECT, 0,
    _init_local ());

/* Maximum number of MpegTSPcr
 * 256 should be sufficient for most multiplexes */
#define MAX_PCR_OBS_CHANNELS 256

typedef struct _MpegTSPCR
{
  guint16 pid;

  /* Following variables are only active/used when
   * calculate_skew is TRUE */
  GstClockTime base_time;
  GstClockTime base_pcrtime;
  GstClockTime prev_out_time;
  GstClockTime prev_in_time;
  GstClockTime last_pcrtime;
  gint64 window[MAX_WINDOW];
  guint window_pos;
  guint window_size;
  gboolean window_filling;
  gint64 window_min;
  gint64 skew;
  gint64 prev_send_diff;

  /* Offset to apply to PCR to handle wraparounds */
  guint64 pcroffset;

  /* Used for bitrate calculation */
  /* FIXME : Replace this later on with a balanced tree or sequence */
  guint64 first_offset;
  guint64 first_pcr;
  GstClockTime first_pcr_ts;
  guint64 last_offset;
  guint64 last_pcr;
  GstClockTime last_pcr_ts;

} MpegTSPCR;

struct _MpegTSPacketizerPrivate
{
  /* Shortcuts for adapter usage */
  guint available;
  guint8 *mapped;
  guint offset;
  guint mapped_size;

  /* Reference offset */
  guint64 refoffset;

  guint nb_seen_offsets;

  /* Last inputted timestamp */
  GstClockTime last_in_time;

  /* offset to observations table */
  guint8 pcrtablelut[0x2000];
  MpegTSPCR *observations[MAX_PCR_OBS_CHANNELS];
  guint8 lastobsid;
};

static void mpegts_packetizer_dispose (GObject * object);
static void mpegts_packetizer_finalize (GObject * object);
static GstClockTime calculate_skew (MpegTSPCR * pcr, guint64 pcrtime,
    GstClockTime time);
static void record_pcr (MpegTSPacketizer2 * packetizer, MpegTSPCR * pcrtable,
    guint64 pcr, guint64 offset);

#define CONTINUITY_UNSET 255
#define VERSION_NUMBER_UNSET 255
#define TABLE_ID_UNSET 0xFF
#define PACKET_SYNC_BYTE 0x47

static inline MpegTSPCR *
get_pcr_table (MpegTSPacketizer2 * packetizer, guint16 pid)
{
  MpegTSPacketizerPrivate *priv = packetizer->priv;
  MpegTSPCR *res;

  res = priv->observations[priv->pcrtablelut[pid]];

  if (G_UNLIKELY (res == NULL)) {
    /* If we don't have a PCR table for the requested PID, create one .. */
    res = g_new0 (MpegTSPCR, 1);
    /* Add it to the last table position */
    priv->observations[priv->lastobsid] = res;
    /* Update the pcrtablelut */
    priv->pcrtablelut[pid] = priv->lastobsid;
    /* And increment the last know slot */
    priv->lastobsid++;

    /* Finally set the default values */
    res->pid = pid;
    res->first_offset = -1;
    res->first_pcr = -1;
    res->first_pcr_ts = GST_CLOCK_TIME_NONE;
    res->last_offset = -1;
    res->last_pcr = -1;
    res->last_pcr_ts = GST_CLOCK_TIME_NONE;
    res->base_time = GST_CLOCK_TIME_NONE;
    res->base_pcrtime = GST_CLOCK_TIME_NONE;
    res->last_pcrtime = GST_CLOCK_TIME_NONE;
    res->window_pos = 0;
    res->window_filling = TRUE;
    res->window_min = 0;
    res->skew = 0;
    res->prev_send_diff = GST_CLOCK_TIME_NONE;
    res->prev_out_time = GST_CLOCK_TIME_NONE;
    res->pcroffset = 0;
  }

  return res;
}

static void
flush_observations (MpegTSPacketizer2 * packetizer)
{
  MpegTSPacketizerPrivate *priv = packetizer->priv;
  gint i;

  for (i = 0; i < priv->lastobsid; i++) {
    g_free (priv->observations[i]);
    priv->observations[i] = NULL;
  }
  memset (priv->pcrtablelut, 0xff, 0x2000);
  priv->lastobsid = 0;
}

static inline MpegTSPacketizerStreamSubtable *
find_subtable (GSList * subtables, guint8 table_id, guint16 subtable_extension)
{
  GSList *tmp;

  /* FIXME: Make this an array ! */
  for (tmp = subtables; tmp; tmp = tmp->next) {
    MpegTSPacketizerStreamSubtable *sub =
        (MpegTSPacketizerStreamSubtable *) tmp->data;
    if (sub->table_id == table_id
        && sub->subtable_extension == subtable_extension)
      return sub;
  }

  return FALSE;
}

static gboolean
seen_section_before (MpegTSPacketizerStream * stream, guint8 table_id,
    guint16 subtable_extension, guint8 version_number, guint8 section_number,
    guint8 last_section_number)
{
  MpegTSPacketizerStreamSubtable *subtable;

  /* Check if we've seen this table_id/subtable_extension first */
  subtable = find_subtable (stream->subtables, table_id, subtable_extension);
  if (!subtable) {
    GST_DEBUG ("Haven't seen subtale");
    return FALSE;
  }
  /* If we have, check it has the same version_number */
  if (subtable->version_number != version_number) {
    GST_DEBUG ("Different version number");
    return FALSE;
  }
  /* Did the number of sections change ? */
  if (subtable->last_section_number != last_section_number) {
    GST_DEBUG ("Different last_section_number");
    return FALSE;
  }
  /* Finally return whether we saw that section or not */
  return MPEGTS_BIT_IS_SET (subtable->seen_section, section_number);
}

static MpegTSPacketizerStreamSubtable *
mpegts_packetizer_stream_subtable_new (guint8 table_id,
    guint16 subtable_extension, guint8 last_section_number)
{
  MpegTSPacketizerStreamSubtable *subtable;

  subtable = g_new0 (MpegTSPacketizerStreamSubtable, 1);
  subtable->version_number = VERSION_NUMBER_UNSET;
  subtable->table_id = table_id;
  subtable->subtable_extension = subtable_extension;
  subtable->last_section_number = last_section_number;
  return subtable;
}

static MpegTSPacketizerStream *
mpegts_packetizer_stream_new (guint16 pid)
{
  MpegTSPacketizerStream *stream;

  stream = (MpegTSPacketizerStream *) g_new0 (MpegTSPacketizerStream, 1);
  stream->continuity_counter = CONTINUITY_UNSET;
  stream->subtables = NULL;
  stream->table_id = TABLE_ID_UNSET;
  stream->pid = pid;
  return stream;
}

static void
mpegts_packetizer_clear_section (MpegTSPacketizerStream * stream)
{
  stream->continuity_counter = CONTINUITY_UNSET;
  stream->section_length = 0;
  stream->section_offset = 0;
  stream->table_id = TABLE_ID_UNSET;
  if (stream->section_data)
    g_free (stream->section_data);
  stream->section_data = NULL;
}

static void
mpegts_packetizer_stream_subtable_free (MpegTSPacketizerStreamSubtable *
    subtable)
{
  g_free (subtable);
}

static void
mpegts_packetizer_stream_free (MpegTSPacketizerStream * stream)
{
  mpegts_packetizer_clear_section (stream);
  if (stream->section_data)
    g_free (stream->section_data);
  g_slist_foreach (stream->subtables,
      (GFunc) mpegts_packetizer_stream_subtable_free, NULL);
  g_slist_free (stream->subtables);
  g_free (stream);
}

static void
mpegts_packetizer_class_init (MpegTSPacketizer2Class * klass)
{
  GObjectClass *gobject_class;

  g_type_class_add_private (klass, sizeof (MpegTSPacketizerPrivate));

  gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->dispose = mpegts_packetizer_dispose;
  gobject_class->finalize = mpegts_packetizer_finalize;
}

static void
mpegts_packetizer_init (MpegTSPacketizer2 * packetizer)
{
  MpegTSPacketizerPrivate *priv;

  priv = packetizer->priv = MPEGTS_PACKETIZER_GET_PRIVATE (packetizer);
  packetizer->adapter = gst_adapter_new ();
  packetizer->offset = 0;
  packetizer->empty = TRUE;
  packetizer->streams = g_new0 (MpegTSPacketizerStream *, 8192);
  packetizer->packet_size = 0;
  packetizer->calculate_skew = FALSE;
  packetizer->calculate_offset = FALSE;

  priv->available = 0;
  priv->mapped = NULL;
  priv->mapped_size = 0;
  priv->offset = 0;

  memset (priv->pcrtablelut, 0xff, 0x2000);
  memset (priv->observations, 0x0, sizeof (priv->observations));
  priv->lastobsid = 0;

  priv->nb_seen_offsets = 0;
  priv->refoffset = -1;
  priv->last_in_time = GST_CLOCK_TIME_NONE;
}

static void
mpegts_packetizer_dispose (GObject * object)
{
  MpegTSPacketizer2 *packetizer = GST_MPEGTS_PACKETIZER (object);

  if (!packetizer->disposed) {
    if (packetizer->packet_size)
      packetizer->packet_size = 0;
    if (packetizer->streams) {
      int i;
      for (i = 0; i < 8192; i++) {
        if (packetizer->streams[i])
          mpegts_packetizer_stream_free (packetizer->streams[i]);
      }
      g_free (packetizer->streams);
    }

    gst_adapter_clear (packetizer->adapter);
    g_object_unref (packetizer->adapter);
    packetizer->disposed = TRUE;
    packetizer->offset = 0;
    packetizer->empty = TRUE;

    flush_observations (packetizer);
  }

  if (G_OBJECT_CLASS (mpegts_packetizer_parent_class)->dispose)
    G_OBJECT_CLASS (mpegts_packetizer_parent_class)->dispose (object);
}

static void
mpegts_packetizer_finalize (GObject * object)
{
  if (G_OBJECT_CLASS (mpegts_packetizer_parent_class)->finalize)
    G_OBJECT_CLASS (mpegts_packetizer_parent_class)->finalize (object);
}

static inline guint64
mpegts_packetizer_compute_pcr (const guint8 * data)
{
  guint32 pcr1;
  guint16 pcr2;
  guint64 pcr, pcr_ext;

  pcr1 = GST_READ_UINT32_BE (data);
  pcr2 = GST_READ_UINT16_BE (data + 4);
  pcr = ((guint64) pcr1) << 1;
  pcr |= (pcr2 & 0x8000) >> 15;
  pcr_ext = (pcr2 & 0x01ff);
  return pcr * 300 + pcr_ext % 300;
}

static gboolean
mpegts_packetizer_parse_adaptation_field_control (MpegTSPacketizer2 *
    packetizer, MpegTSPacketizerPacket * packet)
{
  guint8 length, afcflags;
  guint8 *data;

  length = *packet->data++;

  /* an adaptation field with length 0 is valid and
   * can be used to insert a single stuffing byte */
  if (!length) {
    packet->afc_flags = 0;
    return TRUE;
  }

  if (FLAGS_HAS_AFC (packet->scram_afc_cc)) {
    /* no payload, adaptation field of 183 bytes */
    if (length != 183) {
      GST_DEBUG ("PID %d afc == 0x%02x and length %d != 183",
          packet->pid, packet->scram_afc_cc & 0x30, length);
    }
  } else if (length > 182) {
    GST_DEBUG ("PID %d afc == 0x%02x and length %d > 182",
        packet->pid, packet->scram_afc_cc & 0x30, length);
  }

  if (packet->data + length > packet->data_end) {
    GST_DEBUG ("PID %d afc length %d overflows the buffer current %d max %d",
        packet->pid, length, (gint) (packet->data - packet->data_start),
        (gint) (packet->data_end - packet->data_start));
    return FALSE;
  }

  data = packet->data;
  packet->data += length;

  afcflags = packet->afc_flags = *data++;

  GST_DEBUG ("flags: %s%s%s%s%s%s%s%s%s",
      afcflags & 0x80 ? "discontinuity " : "",
      afcflags & 0x40 ? "random_access " : "",
      afcflags & 0x20 ? "elementary_stream_priority " : "",
      afcflags & 0x10 ? "PCR " : "",
      afcflags & 0x08 ? "OPCR " : "",
      afcflags & 0x04 ? "splicing_point " : "",
      afcflags & 0x02 ? "transport_private_data " : "",
      afcflags & 0x01 ? "extension " : "", afcflags == 0x00 ? "<none>" : "");

  /* PCR */
  if (afcflags & MPEGTS_AFC_PCR_FLAG) {
    MpegTSPCR *pcrtable = NULL;
    packet->pcr = mpegts_packetizer_compute_pcr (data);
    data += 6;
    GST_DEBUG ("pcr 0x%04x %" G_GUINT64_FORMAT " (%" GST_TIME_FORMAT
        ") offset:%" G_GUINT64_FORMAT, packet->pid, packet->pcr,
        GST_TIME_ARGS (PCRTIME_TO_GSTTIME (packet->pcr)), packet->offset);

    if (packetizer->calculate_skew
        && GST_CLOCK_TIME_IS_VALID (packetizer->priv->last_in_time)) {
      pcrtable = get_pcr_table (packetizer, packet->pid);
      calculate_skew (pcrtable, packet->pcr, packetizer->priv->last_in_time);
    }
    if (packetizer->calculate_offset) {
      if (!pcrtable)
        pcrtable = get_pcr_table (packetizer, packet->pid);
      record_pcr (packetizer, pcrtable, packet->pcr, packet->offset);
    }
  }
#ifndef GST_DISABLE_GST_DEBUG
  /* OPCR */
  if (afcflags & MPEGTS_AFC_OPCR_FLAG) {
    /* Note: We don't use/need opcr for the time being */
    guint64 opcr = mpegts_packetizer_compute_pcr (data);
    data += 6;
    GST_DEBUG ("opcr %" G_GUINT64_FORMAT " (%" GST_TIME_FORMAT ")",
        opcr, GST_TIME_ARGS (PCRTIME_TO_GSTTIME (opcr)));
  }

  if (afcflags & MPEGTS_AFC_SPLICING_POINT_FLAG) {
    GST_DEBUG ("splice_countdown: %u", *data++);
  }

  if (afcflags & MPEGTS_AFC_TRANSPORT_PRIVATE_DATA_FLAG) {
    guint8 len = *data++;
    GST_MEMDUMP ("private data", data, len);
    data += len;
  }

  if (afcflags & MPEGTS_AFC_EXTENSION_FLAG) {
    guint8 extlen = *data++;
    guint8 flags = *data++;
    GST_DEBUG ("extension size:%d flags : %s%s%s", extlen,
        flags & 0x80 ? "ltw " : "",
        flags & 0x40 ? "piecewise_rate " : "",
        flags & 0x20 ? "seamless_splice " : "");
    if (flags & 0x80) {
      GST_DEBUG ("legal time window: valid_flag:%d offset:%d", *data >> 7,
          GST_READ_UINT16_BE (data) & 0x7fff);
      data += 2;
    }
  }
#endif

  return TRUE;
}

static MpegTSPacketizerPacketReturn
mpegts_packetizer_parse_packet (MpegTSPacketizer2 * packetizer,
    MpegTSPacketizerPacket * packet)
{
  guint8 *data;
  guint8 tmp;

  data = packet->data_start;
  data += 1;
  tmp = *data;

  /* transport_error_indicator 1 */
  if (G_UNLIKELY (tmp & 0x80))
    return PACKET_BAD;

  /* payload_unit_start_indicator 1 */
  packet->payload_unit_start_indicator = tmp & 0x40;

  /* transport_priority 1 */
  /* PID 13 */
  packet->pid = GST_READ_UINT16_BE (data) & 0x1FFF;
  data += 2;

  packet->scram_afc_cc = tmp = *data++;
  /* transport_scrambling_control 2 */
  if (G_UNLIKELY (tmp & 0xc0))
    return PACKET_BAD;

  packet->data = data;

  if (FLAGS_HAS_AFC (tmp))
    if (!mpegts_packetizer_parse_adaptation_field_control (packetizer, packet))
      return FALSE;

  if (FLAGS_HAS_PAYLOAD (tmp))
    packet->payload = packet->data;
  else
    packet->payload = NULL;

  return PACKET_OK;
}

static GstMpegTsSection *
mpegts_packetizer_parse_section_header (MpegTSPacketizer2 * packetizer,
    MpegTSPacketizerStream * stream)
{
  MpegTSPacketizerStreamSubtable *subtable;
  GstMpegTsSection *res;

  subtable =
      find_subtable (stream->subtables, stream->table_id,
      stream->subtable_extension);
  if (subtable) {
    GST_DEBUG ("Found previous subtable_extension:0x%04x",
        stream->subtable_extension);
    if (G_UNLIKELY (stream->version_number != subtable->version_number)) {
      /* If the version number changed, reset the subtable */
      subtable->version_number = stream->version_number;
      subtable->last_section_number = stream->last_section_number;
      memset (subtable->seen_section, 0, 32);
    }
  } else {
    GST_DEBUG ("Appending new subtable_extension: 0x%04x",
        stream->subtable_extension);
    subtable = mpegts_packetizer_stream_subtable_new (stream->table_id,
        stream->subtable_extension, stream->last_section_number);
    subtable->version_number = stream->version_number;

    stream->subtables = g_slist_prepend (stream->subtables, subtable);
  }

  GST_MEMDUMP ("Full section data", stream->section_data,
      stream->section_length);
  /* TODO ? : Replace this by an efficient version (where we provide all
   * pre-parsed header data) */
  res =
      gst_mpegts_section_new (stream->pid, stream->section_data,
      stream->section_length);
  stream->section_data = NULL;
  mpegts_packetizer_clear_section (stream);

  if (res) {
    /* NOTE : Due to the new mpegts-si system, There is a insanely low probability
     * that we might have gotten a section that was corrupted (i.e. wrong crc)
     * and that we consider it as seen.
     *
     * The reason why we consider this as acceptable is because all the previous
     * checks were already done:
     * * transport layer checks (DVB)
     * * 0x47 validation
     * * continuity counter validation
     * * subtable validation
     * * section_number validation
     * * section_length validation
     *
     * The probability of this happening vs the overhead of doing CRC checks
     * on all sections (including those we would not use) is just not worth it.
     * */
    MPEGTS_BIT_SET (subtable->seen_section, stream->section_number);
    res->offset = stream->offset;
  }

  return res;
}

void
mpegts_packetizer_clear (MpegTSPacketizer2 * packetizer)
{
  if (packetizer->packet_size)
    packetizer->packet_size = 0;

  if (packetizer->streams) {
    int i;
    for (i = 0; i < 8192; i++) {
      if (packetizer->streams[i]) {
        mpegts_packetizer_stream_free (packetizer->streams[i]);
      }
    }
    memset (packetizer->streams, 0, 8192 * sizeof (MpegTSPacketizerStream *));
  }

  gst_adapter_clear (packetizer->adapter);
  packetizer->offset = 0;
  packetizer->empty = TRUE;
  packetizer->priv->available = 0;
  packetizer->priv->mapped = NULL;
  packetizer->priv->mapped_size = 0;
  packetizer->priv->offset = 0;
  packetizer->priv->last_in_time = GST_CLOCK_TIME_NONE;
}

void
mpegts_packetizer_flush (MpegTSPacketizer2 * packetizer, gboolean hard)
{
  GST_DEBUG ("Flushing");

  if (packetizer->streams) {
    int i;
    for (i = 0; i < 8192; i++) {
      if (packetizer->streams[i]) {
        mpegts_packetizer_clear_section (packetizer->streams[i]);
      }
    }
  }
  gst_adapter_clear (packetizer->adapter);

  packetizer->offset = 0;
  packetizer->empty = TRUE;
  packetizer->priv->available = 0;
  packetizer->priv->mapped = NULL;
  packetizer->priv->offset = 0;
  packetizer->priv->mapped_size = 0;
  packetizer->priv->last_in_time = GST_CLOCK_TIME_NONE;
  if (hard) {
    /* For pull mode seeks in tsdemux the observation must be preserved */
    flush_observations (packetizer);
  }
}

void
mpegts_packetizer_remove_stream (MpegTSPacketizer2 * packetizer, gint16 pid)
{
  MpegTSPacketizerStream *stream = packetizer->streams[pid];
  if (stream) {
    GST_INFO ("Removing stream for PID %d", pid);
    mpegts_packetizer_stream_free (stream);
    packetizer->streams[pid] = NULL;
  }
}

MpegTSPacketizer2 *
mpegts_packetizer_new (void)
{
  MpegTSPacketizer2 *packetizer;

  packetizer =
      GST_MPEGTS_PACKETIZER (g_object_new (GST_TYPE_MPEGTS_PACKETIZER, NULL));

  return packetizer;
}

void
mpegts_packetizer_push (MpegTSPacketizer2 * packetizer, GstBuffer * buffer)
{
  if (G_UNLIKELY (packetizer->empty)) {
    packetizer->empty = FALSE;
    packetizer->offset = GST_BUFFER_OFFSET (buffer);
  }

  GST_DEBUG ("Pushing %" G_GSIZE_FORMAT " byte from offset %"
      G_GUINT64_FORMAT, gst_buffer_get_size (buffer),
      GST_BUFFER_OFFSET (buffer));
  gst_adapter_push (packetizer->adapter, buffer);
  packetizer->priv->available += gst_buffer_get_size (buffer);
  /* If buffer timestamp is valid, store it */
  if (GST_CLOCK_TIME_IS_VALID (GST_BUFFER_TIMESTAMP (buffer)))
    packetizer->priv->last_in_time = GST_BUFFER_TIMESTAMP (buffer);
}

static gboolean
mpegts_try_discover_packet_size (MpegTSPacketizer2 * packetizer)
{
  guint8 *dest;
  int i, pos = -1, j;
  static const guint psizes[] = {
    MPEGTS_NORMAL_PACKETSIZE,
    MPEGTS_M2TS_PACKETSIZE,
    MPEGTS_DVB_ASI_PACKETSIZE,
    MPEGTS_ATSC_PACKETSIZE
  };


  dest = g_malloc (MPEGTS_MAX_PACKETSIZE * 4);
  /* wait for 3 sync bytes */
  while (packetizer->priv->available >= MPEGTS_MAX_PACKETSIZE * 4) {

    /* check for sync bytes */
    gst_adapter_copy (packetizer->adapter, dest, 0, MPEGTS_MAX_PACKETSIZE * 4);
    /* find first sync byte */
    pos = -1;
    for (i = 0; i < MPEGTS_MAX_PACKETSIZE; i++) {
      if (dest[i] == PACKET_SYNC_BYTE) {
        for (j = 0; j < 4; j++) {
          guint packetsize = psizes[j];
          /* check each of the packet size possibilities in turn */
          if (dest[i] == PACKET_SYNC_BYTE
              && dest[i + packetsize] == PACKET_SYNC_BYTE
              && dest[i + packetsize * 2] == PACKET_SYNC_BYTE
              && dest[i + packetsize * 3] == PACKET_SYNC_BYTE) {
            packetizer->packet_size = packetsize;
            if (packetsize == MPEGTS_M2TS_PACKETSIZE)
              pos = i - 4;
            else
              pos = i;
            break;
          }
        }
        break;
      }
    }

    if (packetizer->packet_size)
      break;

    /* Skip MPEGTS_MAX_PACKETSIZE */
    gst_adapter_flush (packetizer->adapter, MPEGTS_MAX_PACKETSIZE);
    packetizer->priv->available -= MPEGTS_MAX_PACKETSIZE;
    packetizer->offset += MPEGTS_MAX_PACKETSIZE;
  }

  g_free (dest);

  if (packetizer->packet_size) {
    GST_DEBUG ("have packetsize detected: %d of %u bytes",
        packetizer->packet_size, packetizer->packet_size);
    /* flush to sync byte */
    if (pos > 0) {
      GST_DEBUG ("Flushing out %d bytes", pos);
      gst_adapter_flush (packetizer->adapter, pos);
      packetizer->offset += pos;
      packetizer->priv->available -= MPEGTS_MAX_PACKETSIZE;
    }
  } else {
    /* drop invalid data and move to the next possible packets */
    GST_DEBUG ("Could not determine packet size");
  }

  return packetizer->packet_size;
}

gboolean
mpegts_packetizer_has_packets (MpegTSPacketizer2 * packetizer)
{
  if (G_UNLIKELY (!packetizer->packet_size)) {
    if (!mpegts_try_discover_packet_size (packetizer))
      return FALSE;
  }
  return packetizer->priv->available >= packetizer->packet_size;
}

MpegTSPacketizerPacketReturn
mpegts_packetizer_next_packet (MpegTSPacketizer2 * packetizer,
    MpegTSPacketizerPacket * packet)
{
  MpegTSPacketizerPrivate *priv = packetizer->priv;
  guint skip;
  guint sync_offset;
  guint packet_size;

  packet_size = packetizer->packet_size;
  if (G_UNLIKELY (!packet_size)) {
    if (!mpegts_try_discover_packet_size (packetizer))
      return PACKET_NEED_MORE;
    packet_size = packetizer->packet_size;
  }

  while (priv->available >= packet_size) {
    if (priv->mapped == NULL) {
      priv->mapped_size = priv->available;
      priv->mapped =
          (guint8 *) gst_adapter_map (packetizer->adapter, priv->mapped_size);
      priv->offset = 0;
    }

    /* M2TS packets don't start with the sync byte, all other variants do */
    sync_offset = priv->offset;
    if (packet_size == MPEGTS_M2TS_PACKETSIZE)
      sync_offset += 4;

    /* Check sync byte */
    if (G_LIKELY (priv->mapped[sync_offset] == 0x47)) {
      /* ALL mpeg-ts variants contain 188 bytes of data. Those with bigger
       * packet sizes contain either extra data (timesync, FEC, ..) either
       * before or after the data */
      packet->data_start = priv->mapped + sync_offset;
      packet->data_end = packet->data_start + 188;
      packet->offset = packetizer->offset;
      GST_LOG ("offset %" G_GUINT64_FORMAT, packet->offset);
      packetizer->offset += packet_size;
      GST_MEMDUMP ("data_start", packet->data_start, 16);
      goto got_valid_packet;
    }

    GST_LOG ("Lost sync %d", packet_size);

    /* Find the 0x47 in the buffer (and require at least 2 checks) */
    for (; sync_offset + 2 * packet_size < priv->mapped_size; sync_offset++)
      if (priv->mapped[sync_offset] == 0x47 &&
          priv->mapped[sync_offset + packet_size] == 0x47 &&
          priv->mapped[sync_offset + 2 * packet_size] == 0x47)
        break;

    /* Pop out the remaining data... */
    skip = sync_offset - priv->offset;
    if (packet_size == MPEGTS_M2TS_PACKETSIZE)
      skip -= 4;

    priv->available -= skip;
    priv->offset += skip;
    packetizer->offset += skip;

    if (G_UNLIKELY (priv->available < packet_size)) {
      GST_DEBUG ("Flushing %d bytes out", priv->offset);
      gst_adapter_flush (packetizer->adapter, priv->offset);
      priv->mapped = NULL;
    }
  }

  return PACKET_NEED_MORE;

got_valid_packet:
  return mpegts_packetizer_parse_packet (packetizer, packet);
}

MpegTSPacketizerPacketReturn
mpegts_packetizer_process_next_packet (MpegTSPacketizer2 * packetizer)
{
  MpegTSPacketizerPacket packet;
  MpegTSPacketizerPacketReturn ret;

  ret = mpegts_packetizer_next_packet (packetizer, &packet);
  if (ret != PACKET_NEED_MORE) {
    packetizer->priv->offset += packetizer->packet_size;
    packetizer->priv->available -= packetizer->packet_size;
    if (G_UNLIKELY (packetizer->priv->available < packetizer->packet_size)) {
      gst_adapter_flush (packetizer->adapter, packetizer->priv->offset);
      packetizer->priv->mapped = NULL;
    }
  }
  return ret;
}

void
mpegts_packetizer_clear_packet (MpegTSPacketizer2 * packetizer,
    MpegTSPacketizerPacket * packet)
{
  guint8 packet_size = packetizer->packet_size;
  MpegTSPacketizerPrivate *priv = packetizer->priv;

  priv->offset += packet_size;
  priv->available -= packet_size;

  if (G_UNLIKELY (priv->mapped && priv->available < packet_size)) {
    gst_adapter_flush (packetizer->adapter, priv->offset);
    priv->mapped = NULL;
  }
}

/*
 * Ideally it should just return a section if:
 * * The section is complete
 * * The section is valid (sanity checks for length for example)
 * * The section applies now (current_next_indicator)
 * * The section is an update or was never seen
 *
 * The section should be a new GstMpegTsSection:
 * * properly initialized
 * * With pid, table_id AND section_type set (move logic from mpegtsbase)
 * * With data copied into it (yes, minor overhead)
 *
 * In all other cases it should just return NULL
 *
 * If more than one section is available, the 'remaining' field will
 * be set to the beginning of a valid GList containing other sections.
 * */
GstMpegTsSection *
mpegts_packetizer_push_section (MpegTSPacketizer2 * packetizer,
    MpegTSPacketizerPacket * packet, GList ** remaining)
{
  GstMpegTsSection *section;
  GstMpegTsSection *res = NULL;
  MpegTSPacketizerStream *stream;
  gboolean long_packet;
  guint8 pointer = 0, table_id;
  guint16 subtable_extension = 0;
  gsize to_read;
  guint section_length;
  /* data points to the current read location
   * data_start points to the beginning of the data to accumulate */
  guint8 *data, *data_start;
  guint8 packet_cc;
  GList *others = NULL;
  guint8 version_number, section_number, last_section_number;

  data = packet->data;
  packet_cc = FLAGS_CONTINUITY_COUNTER (packet->scram_afc_cc);

  /* Get our filter */
  stream = packetizer->streams[packet->pid];
  if (G_UNLIKELY (stream == NULL)) {
    if (!packet->payload_unit_start_indicator) {
      /* Early exit (we need to start with a section start) */
      GST_DEBUG ("PID 0x%04x  waiting for section start", packet->pid);
      goto out;
    }
    stream = mpegts_packetizer_stream_new (packet->pid);
    packetizer->streams[packet->pid] = stream;
  }

  GST_MEMDUMP ("Full packet data", packet->data,
      packet->data_end - packet->data);

  /* This function is split into several parts:
   *
   * Pre checks (packet-wide). Determines where we go next
   * accumulate_data: store data and check if section is complete
   * section_start: handle beginning of a section, if needed loop back to
   *                accumulate_data
   *
   * The trigger that makes the loop stop and return is if:
   * 1) We do not have enough data for the current packet
   * 2) There is remaining data after a packet which is only made
   *    of stuffing bytes (0xff).
   *
   * Pre-loop checks, related to the whole incoming packet:
   *
   * If there is a CC-discont:
   *  If it is a PUSI, skip the pointer and handle section_start
   *  If not a PUSI, reset and return nothing
   * If there is not a CC-discont:
   *  If it is a PUSI
   *    If pointer, accumulate that data and check for complete section
   *    (loop)
   *  If it is not a PUSI
   *    Accumulate the expected data and check for complete section
   *    (loop)
   *    
   **/

  if (packet->payload_unit_start_indicator) {
    pointer = *data++;
    /* If the pointer is zero, we're guaranteed to be able to handle it */
    if (pointer == 0) {
      GST_LOG
          ("PID 0x%04x PUSI and pointer == 0, skipping straight to section_start parsing",
          packet->pid);
      goto section_start;
    }
  }

  if (stream->continuity_counter == CONTINUITY_UNSET ||
      (stream->continuity_counter + 1) % 16 != packet_cc) {
    if (stream->continuity_counter != CONTINUITY_UNSET)
      GST_WARNING ("PID 0x%04x section discontinuity (%d vs %d)", packet->pid,
          stream->continuity_counter, packet_cc);
    mpegts_packetizer_clear_section (stream);
    /* If not a PUSI, not much we can do */
    if (!packet->payload_unit_start_indicator) {
      GST_LOG ("PID 0x%04x continuity discont/unset and not PUSI, bailing out",
          packet->pid);
      goto out;
    }
    /* If PUSI, skip pointer data and carry on to section start */
    data += pointer;
    pointer = 0;
    GST_LOG ("discont, but PUSI, skipped %d bytes and doing section start",
        pointer);
    goto section_start;
  }

  GST_LOG ("Accumulating data from beginning of packet");

  data_start = data;

accumulate_data:
  /* If not the beginning of a new section, accumulate what we have */
  stream->continuity_counter = packet_cc;
  to_read = MIN (stream->section_length - stream->section_offset,
      packet->data_end - data_start);
  memcpy (stream->section_data + stream->section_offset, data_start, to_read);
  stream->section_offset += to_read;
  /* Point data to after the data we accumulated */
  data = data_start + to_read;
  GST_DEBUG ("Appending data (need %d, have %d)", stream->section_length,
      stream->section_offset);

  /* Check if we have enough */
  if (stream->section_offset < stream->section_length) {
    GST_DEBUG ("PID 0x%04x, section not complete (Got %d, need %d)",
        stream->pid, stream->section_offset, stream->section_length);
    goto out;
  }

  /* Small sanity check. We should have collected *exactly* the right amount */
  if (G_UNLIKELY (stream->section_offset != stream->section_length))
    GST_WARNING ("PID 0x%04x Accumulated too much data (%d vs %d) !",
        stream->pid, stream->section_offset, stream->section_length);
  GST_DEBUG ("PID 0x%04x Section complete", stream->pid);

  if ((section = mpegts_packetizer_parse_section_header (packetizer, stream))) {
    if (res)
      others = g_list_append (others, section);
    else
      res = section;
  }

  /* FIXME : We need at least 8 bytes with current algorithm :(
   * We might end up losing sections that start across two packets (srsl...) */
  if (data > packet->data_end - 8 || *data == 0xff) {
    /* flush stuffing bytes and leave */
    mpegts_packetizer_clear_section (stream);
    goto out;
  }

  /* We have more data to process ... */
  GST_DEBUG ("PID 0x%04x, More section present in packet (remaining bytes:%"
      G_GSIZE_FORMAT ")", stream->pid, packet->data_end - data);

section_start:
  GST_MEMDUMP ("section_start", data, packet->data_end - data);
  data_start = data;
  /* Beginning of a new section */
  /*
   * section_syntax_indicator means that the header is of the following format:
   * * table_id (8bit)
   * * section_syntax_indicator (1bit) == 0
   * * reserved/private fields (3bit)
   * * section_length (12bit)
   * * data (of size section_length)
   * * NO CRC !
   */
  long_packet = data[1] & 0x80;

  /* Fast path for short packets */
  if (!long_packet) {
    /* We can create the section now (function will check for size) */
    GST_DEBUG ("Short packet");
    section_length = (GST_READ_UINT16_BE (data + 1) & 0xfff) + 3;
    /* Only do fast-path if we have enough byte */
    if (section_length < packet->data_end - data) {
      if ((section =
              gst_mpegts_section_new (packet->pid, g_memdup (data,
                      section_length), section_length))) {
        GST_DEBUG ("PID 0x%04x Short section complete !", packet->pid);
        section->offset = packet->offset;
        if (res)
          others = g_list_append (others, section);
        else
          res = section;
      }
      /* Advance reader and potentially read another section */
      data += section_length;
      if (data < packet->data_end && *data != 0xff)
        goto section_start;
      /* If not, exit */
      goto out;
    }
    /* We don't have enough bytes to do short section shortcut */
  }

  /* Beginning of a new section, do as much pre-parsing as possible */
  /* table_id                        : 8  bit */
  table_id = *data++;

  /* section_syntax_indicator        : 1  bit
   * other_fields (reserved)         : 3  bit
   * section_length                  : 12 bit */
  section_length = (GST_READ_UINT16_BE (data) & 0x0FFF) + 3;
  data += 2;

  if (long_packet) {
    /* subtable_extension (always present, we are in a long section) */
    /* subtable extension              : 16 bit */
    subtable_extension = GST_READ_UINT16_BE (data);
    data += 2;

    /* reserved                      : 2  bit
     * version_number                : 5  bit
     * current_next_indicator        : 1  bit */
    /* Bail out now if current_next_indicator == 0 */
    if (G_UNLIKELY (!(*data & 0x01))) {
      GST_DEBUG
          ("PID 0x%04x table_id 0x%02x section does not apply (current_next_indicator == 0)",
          packet->pid, table_id);
      goto out;
    }

    version_number = *data++ >> 1 & 0x1f;
    /* section_number                : 8  bit */
    section_number = *data++;
    /* last_section_number                : 8  bit */
    last_section_number = *data++;
  } else {
    subtable_extension = 0;
    version_number = 0;
    section_number = 0;
    last_section_number = 0;
  }
  GST_DEBUG
      ("PID 0x%04x length:%d table_id:0x%02x subtable_extension:0x%04x version_number:%d section_number:%d(last:%d)",
      packet->pid, section_length, table_id, subtable_extension, version_number,
      section_number, last_section_number);

  to_read = MIN (section_length, packet->data_end - data_start);

  /* Check as early as possible whether we already saw this section
   * i.e. that we saw a subtable with:
   * * same subtable_extension (might be zero)
   * * same version_number
   * * same last_section_number
   * * same section_number was seen
   */
  if (seen_section_before (stream, table_id, subtable_extension,
          version_number, section_number, last_section_number)) {
    GST_DEBUG
        ("PID 0x%04x Already processed table_id:0x%02x subtable_extension:0x%04x, version_number:%d, section_number:%d",
        packet->pid, table_id, subtable_extension, version_number,
        section_number);
    /* skip data and see if we have more sections after */
    data = data_start + to_read;
    if (data == packet->data_end || *data == 0xff)
      goto out;
    goto section_start;
  }
  if (G_UNLIKELY (section_number > last_section_number)) {
    GST_WARNING
        ("PID 0x%04x corrupted packet (section_number:%d > last_section_number:%d)",
        packet->pid, section_number, last_section_number);
    goto out;
  }


  /* Copy over already parsed values */
  stream->table_id = table_id;
  stream->section_length = section_length;
  stream->version_number = version_number;
  stream->subtable_extension = subtable_extension;
  stream->section_number = section_number;
  stream->last_section_number = last_section_number;
  stream->offset = packet->offset;

  /* Create enough room to store chunks of sections */
  stream->section_data = g_malloc (stream->section_length);
  stream->section_offset = 0;

  /* Finally, accumulate and check if we parsed enough */
  goto accumulate_data;

out:
  packet->data = data;
  *remaining = others;

  GST_DEBUG ("result: %p", res);

  return res;
}

static void
_init_local (void)
{
  GST_DEBUG_CATEGORY_INIT (mpegts_packetizer_debug, "mpegtspacketizer", 0,
      "MPEG transport stream parser");
}


static void
mpegts_packetizer_resync (MpegTSPCR * pcr, GstClockTime time,
    GstClockTime gstpcrtime, gboolean reset_skew)
{
  pcr->base_time = time;
  pcr->base_pcrtime = gstpcrtime;
  pcr->prev_out_time = GST_CLOCK_TIME_NONE;
  pcr->prev_send_diff = GST_CLOCK_TIME_NONE;
  if (reset_skew) {
    pcr->window_filling = TRUE;
    pcr->window_pos = 0;
    pcr->window_min = 0;
    pcr->window_size = 0;
    pcr->skew = 0;
  }
}


/* Code mostly copied from -good/gst/rtpmanager/rtpjitterbuffer.c */

/* For the clock skew we use a windowed low point averaging algorithm as can be
 * found in Fober, Orlarey and Letz, 2005, "Real Time Clock Skew Estimation
 * over Network Delays":
 * http://www.grame.fr/Ressources/pub/TR-050601.pdf
 * http://citeseerx.ist.psu.edu/viewdoc/summary?doi=10.1.1.102.1546
 *
 * The idea is that the jitter is composed of:
 *
 *  J = N + n
 *
 *   N   : a constant network delay.
 *   n   : random added noise. The noise is concentrated around 0
 *
 * In the receiver we can track the elapsed time at the sender with:
 *
 *  send_diff(i) = (Tsi - Ts0);
 *
 *   Tsi : The time at the sender at packet i
 *   Ts0 : The time at the sender at the first packet
 *
 * This is the difference between the RTP timestamp in the first received packet
 * and the current packet.
 *
 * At the receiver we have to deal with the jitter introduced by the network.
 *
 *  recv_diff(i) = (Tri - Tr0)
 *
 *   Tri : The time at the receiver at packet i
 *   Tr0 : The time at the receiver at the first packet
 *
 * Both of these values contain a jitter Ji, a jitter for packet i, so we can
 * write:
 *
 *  recv_diff(i) = (Cri + D + ni) - (Cr0 + D + n0))
 *
 *    Cri    : The time of the clock at the receiver for packet i
 *    D + ni : The jitter when receiving packet i
 *
 * We see that the network delay is irrelevant here as we can elliminate D:
 *
 *  recv_diff(i) = (Cri + ni) - (Cr0 + n0))
 *
 * The drift is now expressed as:
 *
 *  Drift(i) = recv_diff(i) - send_diff(i);
 *
 * We now keep the W latest values of Drift and find the minimum (this is the
 * one with the lowest network jitter and thus the one which is least affected
 * by it). We average this lowest value to smooth out the resulting network skew.
 *
 * Both the window and the weighting used for averaging influence the accuracy
 * of the drift estimation. Finding the correct parameters turns out to be a
 * compromise between accuracy and inertia.
 *
 * We use a 2 second window or up to 512 data points, which is statistically big
 * enough to catch spikes (FIXME, detect spikes).
 * We also use a rather large weighting factor (125) to smoothly adapt. During
 * startup, when filling the window, we use a parabolic weighting factor, the
 * more the window is filled, the faster we move to the detected possible skew.
 *
 * Returns: @time adjusted with the clock skew.
 */
static GstClockTime
calculate_skew (MpegTSPCR * pcr, guint64 pcrtime, GstClockTime time)
{
  guint64 send_diff, recv_diff;
  gint64 delta;
  gint64 old;
  gint pos, i;
  GstClockTime gstpcrtime, out_time;
#ifndef GST_DISABLE_GST_DEBUG
  guint64 slope;
#endif

  gstpcrtime = PCRTIME_TO_GSTTIME (pcrtime) + pcr->pcroffset;

  /* first time, lock on to time and gstpcrtime */
  if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (pcr->base_time))) {
    pcr->base_time = time;
    pcr->prev_out_time = GST_CLOCK_TIME_NONE;
    GST_DEBUG ("Taking new base time %" GST_TIME_FORMAT, GST_TIME_ARGS (time));
  }

  if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (pcr->base_pcrtime))) {
    pcr->base_pcrtime = gstpcrtime;
    pcr->prev_send_diff = -1;
    GST_DEBUG ("Taking new base pcrtime %" GST_TIME_FORMAT,
        GST_TIME_ARGS (gstpcrtime));
  }

  /* Handle PCR wraparound and resets */
  if (GST_CLOCK_TIME_IS_VALID (pcr->last_pcrtime) &&
      gstpcrtime < pcr->last_pcrtime) {
    if (pcr->last_pcrtime - gstpcrtime > PCR_GST_MAX_VALUE / 2) {
      /* PCR wraparound */
      GST_DEBUG ("PCR wrap");
      pcr->pcroffset += PCR_GST_MAX_VALUE;
      gstpcrtime = PCRTIME_TO_GSTTIME (pcrtime) + pcr->pcroffset;
      send_diff = gstpcrtime - pcr->base_pcrtime;
    } else if (GST_CLOCK_TIME_IS_VALID (time)
        && pcr->last_pcrtime - gstpcrtime > 15 * GST_SECOND) {
      /* Assume a reset */
      GST_DEBUG ("PCR reset");
      /* Calculate PCR we would have expected for the given input time,
       * essentially applying the reverse correction process
       *
       * We want to find the PCR offset to apply
       *   pcroffset = (corrected) gstpcrtime - (received) gstpcrtime
       *
       * send_diff = (corrected) gstpcrtime - pcr->base_pcrtime
       * recv_diff = time - pcr->base_time
       * out_time = pcr->base_time + send_diff
       *
       * We are assuming that send_diff == recv_diff
       *   (corrected) gstpcrtime - pcr->base_pcrtime = time - pcr->base_time
       * Giving us:
       *   (corrected) gstpcrtime = time - pcr->base_time + pcr->base_pcrtime
       *
       * And therefore:
       *   pcroffset = time - pcr->base_time + pcr->base_pcrtime - (received) gstpcrtime
       **/
      pcr->pcroffset += time - pcr->base_time + pcr->base_pcrtime - gstpcrtime;
      gstpcrtime = PCRTIME_TO_GSTTIME (pcrtime) + pcr->pcroffset;
      send_diff = gstpcrtime - pcr->base_pcrtime;
      GST_DEBUG ("Introduced offset is now %" GST_TIME_FORMAT
          " corrected pcr time %" GST_TIME_FORMAT,
          GST_TIME_ARGS (pcr->pcroffset), GST_TIME_ARGS (gstpcrtime));
    } else {
      GST_WARNING ("backward timestamps at server but no timestamps");
      send_diff = 0;
      /* at least try to get a new timestamp.. */
      pcr->base_time = GST_CLOCK_TIME_NONE;
    }
  } else
    send_diff = gstpcrtime - pcr->base_pcrtime;

  GST_DEBUG ("gstpcr %" GST_TIME_FORMAT ", buftime %" GST_TIME_FORMAT
      ", base %" GST_TIME_FORMAT ", send_diff %" GST_TIME_FORMAT,
      GST_TIME_ARGS (gstpcrtime), GST_TIME_ARGS (time),
      GST_TIME_ARGS (pcr->base_pcrtime), GST_TIME_ARGS (send_diff));

  /* keep track of the last extended pcrtime */
  pcr->last_pcrtime = gstpcrtime;

  /* we don't have an arrival timestamp so we can't do skew detection. we
   * should still apply a timestamp based on RTP timestamp and base_time */
  if (!GST_CLOCK_TIME_IS_VALID (time)
      || !GST_CLOCK_TIME_IS_VALID (pcr->base_time))
    goto no_skew;

  /* elapsed time at receiver, includes the jitter */
  recv_diff = time - pcr->base_time;

  /* Ignore packets received at 100% the same time (i.e. from the same input buffer) */
  if (G_UNLIKELY (time == pcr->prev_in_time
          && GST_CLOCK_TIME_IS_VALID (pcr->prev_in_time)))
    goto no_skew;

  /* measure the diff */
  delta = ((gint64) recv_diff) - ((gint64) send_diff);

#ifndef GST_DISABLE_GST_DEBUG
  /* measure the slope, this gives a rought estimate between the sender speed
   * and the receiver speed. This should be approximately 8, higher values
   * indicate a burst (especially when the connection starts) */
  slope = recv_diff > 0 ? (send_diff * 8) / recv_diff : 8;
#endif

  GST_DEBUG ("time %" GST_TIME_FORMAT ", base %" GST_TIME_FORMAT
      ", recv_diff %" GST_TIME_FORMAT ", slope %" G_GUINT64_FORMAT,
      GST_TIME_ARGS (time), GST_TIME_ARGS (pcr->base_time),
      GST_TIME_ARGS (recv_diff), slope);

  /* if the difference between the sender timeline and the receiver timeline
   * changed too quickly we have to resync because the server likely restarted
   * its timestamps. */
  if (ABS (delta - pcr->skew) > GST_SECOND) {
    GST_WARNING ("delta - skew: %" GST_TIME_FORMAT " too big, reset skew",
        GST_TIME_ARGS (delta - pcr->skew));
    mpegts_packetizer_resync (pcr, time, gstpcrtime, TRUE);
    send_diff = 0;
    delta = 0;
  }

  pos = pcr->window_pos;

  if (G_UNLIKELY (pcr->window_filling)) {
    /* we are filling the window */
    GST_DEBUG ("filling %d, delta %" G_GINT64_FORMAT, pos, delta);
    pcr->window[pos++] = delta;
    /* calc the min delta we observed */
    if (G_UNLIKELY (pos == 1 || delta < pcr->window_min))
      pcr->window_min = delta;

    if (G_UNLIKELY (send_diff >= MAX_TIME || pos >= MAX_WINDOW)) {
      pcr->window_size = pos;

      /* window filled */
      GST_DEBUG ("min %" G_GINT64_FORMAT, pcr->window_min);

      /* the skew is now the min */
      pcr->skew = pcr->window_min;
      pcr->window_filling = FALSE;
    } else {
      gint perc_time, perc_window, perc;

      /* figure out how much we filled the window, this depends on the amount of
       * time we have or the max number of points we keep. */
      perc_time = send_diff * 100 / MAX_TIME;
      perc_window = pos * 100 / MAX_WINDOW;
      perc = MAX (perc_time, perc_window);

      /* make a parabolic function, the closer we get to the MAX, the more value
       * we give to the scaling factor of the new value */
      perc = perc * perc;

      /* quickly go to the min value when we are filling up, slowly when we are
       * just starting because we're not sure it's a good value yet. */
      pcr->skew =
          (perc * pcr->window_min + ((10000 - perc) * pcr->skew)) / 10000;
      pcr->window_size = pos + 1;
    }
  } else {
    /* pick old value and store new value. We keep the previous value in order
     * to quickly check if the min of the window changed */
    old = pcr->window[pos];
    pcr->window[pos++] = delta;

    if (G_UNLIKELY (delta <= pcr->window_min)) {
      /* if the new value we inserted is smaller or equal to the current min,
       * it becomes the new min */
      pcr->window_min = delta;
    } else if (G_UNLIKELY (old == pcr->window_min)) {
      gint64 min = G_MAXINT64;

      /* if we removed the old min, we have to find a new min */
      for (i = 0; i < pcr->window_size; i++) {
        /* we found another value equal to the old min, we can stop searching now */
        if (pcr->window[i] == old) {
          min = old;
          break;
        }
        if (pcr->window[i] < min)
          min = pcr->window[i];
      }
      pcr->window_min = min;
    }
    /* average the min values */
    pcr->skew = (pcr->window_min + (124 * pcr->skew)) / 125;
    GST_DEBUG ("delta %" G_GINT64_FORMAT ", new min: %" G_GINT64_FORMAT,
        delta, pcr->window_min);
  }
  /* wrap around in the window */
  if (G_UNLIKELY (pos >= pcr->window_size))
    pos = 0;

  pcr->window_pos = pos;

no_skew:
  /* the output time is defined as the base timestamp plus the PCR time
   * adjusted for the clock skew .*/
  if (pcr->base_time != -1) {
    out_time = pcr->base_time + send_diff;
    /* skew can be negative and we don't want to make invalid timestamps */
    if (pcr->skew < 0 && out_time < -pcr->skew) {
      out_time = 0;
    } else {
      out_time += pcr->skew;
    }
    /* check if timestamps are not going backwards, we can only check this if we
     * have a previous out time and a previous send_diff */
    if (G_LIKELY (pcr->prev_out_time != -1 && pcr->prev_send_diff != -1)) {
      /* now check for backwards timestamps */
      if (G_UNLIKELY (
              /* if the server timestamps went up and the out_time backwards */
              (send_diff > pcr->prev_send_diff
                  && out_time < pcr->prev_out_time) ||
              /* if the server timestamps went backwards and the out_time forwards */
              (send_diff < pcr->prev_send_diff
                  && out_time > pcr->prev_out_time) ||
              /* if the server timestamps did not change */
              send_diff == pcr->prev_send_diff)) {
        GST_DEBUG ("backwards timestamps, using previous time");
        out_time = GSTTIME_TO_MPEGTIME (out_time);
      }
    }
  } else {
    /* We simply use the pcrtime without applying any skew compensation */
    out_time = time;
  }

  pcr->prev_out_time = out_time;
  pcr->prev_in_time = time;
  pcr->prev_send_diff = send_diff;

  GST_DEBUG ("skew %" G_GINT64_FORMAT ", out %" GST_TIME_FORMAT,
      pcr->skew, GST_TIME_ARGS (out_time));

  return out_time;
}

static void
record_pcr (MpegTSPacketizer2 * packetizer, MpegTSPCR * pcrtable,
    guint64 pcr, guint64 offset)
{
  MpegTSPacketizerPrivate *priv = packetizer->priv;

  /* Check against first PCR */
  if (pcrtable->first_pcr == -1 || pcrtable->first_offset > offset) {
    GST_DEBUG ("Recording first value. PCR:%" G_GUINT64_FORMAT " offset:%"
        G_GUINT64_FORMAT " pcr_pid:0x%04x", pcr, offset, pcrtable->pid);
    pcrtable->first_pcr = pcr;
    pcrtable->first_pcr_ts = PCRTIME_TO_GSTTIME (pcr);
    pcrtable->first_offset = offset;
    priv->nb_seen_offsets++;
  } else
    /* If we didn't update the first PCR, let's check against last PCR */
  if (pcrtable->last_pcr == -1 || pcrtable->last_offset < offset) {
    GST_DEBUG ("Recording last value. PCR:%" G_GUINT64_FORMAT " offset:%"
        G_GUINT64_FORMAT " pcr_pid:0x%04x", pcr, offset, pcrtable->pid);
    if (G_UNLIKELY (pcrtable->first_pcr != -1 && pcr < pcrtable->first_pcr)) {
      GST_DEBUG ("rollover detected");
      pcr += PCR_MAX_VALUE;
    }
    pcrtable->last_pcr = pcr;
    pcrtable->last_pcr_ts = PCRTIME_TO_GSTTIME (pcr);
    pcrtable->last_offset = offset;
    priv->nb_seen_offsets++;
  }
}

guint
mpegts_packetizer_get_seen_pcr (MpegTSPacketizer2 * packetizer)
{
  return packetizer->priv->nb_seen_offsets;
}

GstClockTime
mpegts_packetizer_offset_to_ts (MpegTSPacketizer2 * packetizer,
    guint64 offset, guint16 pid)
{
  MpegTSPacketizerPrivate *priv = packetizer->priv;
  MpegTSPCR *pcrtable;
  GstClockTime res;

  if (G_UNLIKELY (!packetizer->calculate_offset))
    return GST_CLOCK_TIME_NONE;

  if (G_UNLIKELY (priv->refoffset == -1))
    return GST_CLOCK_TIME_NONE;

  if (G_UNLIKELY (offset < priv->refoffset))
    return GST_CLOCK_TIME_NONE;

  pcrtable = get_pcr_table (packetizer, pid);

  if (G_UNLIKELY (pcrtable->last_offset <= pcrtable->first_offset))
    return GST_CLOCK_TIME_NONE;

  /* Convert byte difference into time difference */
  res = PCRTIME_TO_GSTTIME (gst_util_uint64_scale (offset - priv->refoffset,
          pcrtable->last_pcr - pcrtable->first_pcr,
          pcrtable->last_offset - pcrtable->first_offset));
  GST_DEBUG ("Returning timestamp %" GST_TIME_FORMAT " for offset %"
      G_GUINT64_FORMAT, GST_TIME_ARGS (res), offset);

  return res;
}

GstClockTime
mpegts_packetizer_pts_to_ts (MpegTSPacketizer2 * packetizer,
    GstClockTime pts, guint16 pcr_pid)
{
  GstClockTime res = GST_CLOCK_TIME_NONE;
  MpegTSPCR *pcrtable = get_pcr_table (packetizer, pcr_pid);

  /* Use clock skew if present */
  if (packetizer->calculate_skew
      && GST_CLOCK_TIME_IS_VALID (pcrtable->base_time)) {
    GST_DEBUG ("pts %" G_GUINT64_FORMAT " base_pcrtime:%" G_GUINT64_FORMAT
        " base_time:%" GST_TIME_FORMAT, pts, pcrtable->base_pcrtime,
        GST_TIME_ARGS (pcrtable->base_time));
    res =
        pts + pcrtable->pcroffset - pcrtable->base_pcrtime +
        pcrtable->base_time + pcrtable->skew;
  } else
    /* If not, use pcr observations */
  if (packetizer->calculate_offset && pcrtable->first_pcr != -1) {
    /* Rollover */
    if (G_UNLIKELY (pts < pcrtable->first_pcr_ts))
      pts += MPEGTIME_TO_GSTTIME (PTS_DTS_MAX_VALUE);
    res = pts - pcrtable->first_pcr_ts;
  } else
    GST_WARNING ("Not enough information to calculate proper timestamp");

  GST_DEBUG ("Returning timestamp %" GST_TIME_FORMAT " for pts %"
      GST_TIME_FORMAT " pcr_pid:0x%04x", GST_TIME_ARGS (res),
      GST_TIME_ARGS (pts), pcr_pid);
  return res;
}

guint64
mpegts_packetizer_ts_to_offset (MpegTSPacketizer2 * packetizer,
    GstClockTime ts, guint16 pcr_pid)
{
  MpegTSPacketizerPrivate *priv = packetizer->priv;
  MpegTSPCR *pcrtable;
  guint64 res;

  if (!packetizer->calculate_offset)
    return -1;

  pcrtable = get_pcr_table (packetizer, pcr_pid);
  if (pcrtable->first_pcr == -1)
    return -1;

  GST_DEBUG ("ts(pcr) %" G_GUINT64_FORMAT " first_pcr:%" G_GUINT64_FORMAT,
      GSTTIME_TO_MPEGTIME (ts), pcrtable->first_pcr);

  /* Convert ts to PCRTIME */
  res = gst_util_uint64_scale (GSTTIME_TO_PCRTIME (ts),
      pcrtable->last_offset - pcrtable->first_offset,
      pcrtable->last_pcr - pcrtable->first_pcr);
  res += pcrtable->first_offset + priv->refoffset;

  GST_DEBUG ("Returning offset %" G_GUINT64_FORMAT " for ts %"
      GST_TIME_FORMAT, res, GST_TIME_ARGS (ts));

  return res;
}

void
mpegts_packetizer_set_reference_offset (MpegTSPacketizer2 * packetizer,
    guint64 refoffset)
{
  GST_DEBUG ("Setting reference offset to %" G_GUINT64_FORMAT, refoffset);

  packetizer->priv->refoffset = refoffset;
}
