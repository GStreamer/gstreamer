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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <string.h>

/* FIXME 0.11: suppress warnings for deprecated API such as GValueArray
 * with newer GLib versions (>= 2.31.0) */
#define GLIB_DISABLE_DEPRECATION_WARNINGS

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

static GQuark QUARK_PAT;
static GQuark QUARK_TRANSPORT_STREAM_ID;
static GQuark QUARK_PROGRAM_NUMBER;
static GQuark QUARK_PID;
static GQuark QUARK_PROGRAMS;

static GQuark QUARK_CAT;

static GQuark QUARK_PMT;
static GQuark QUARK_PCR_PID;
static GQuark QUARK_VERSION_NUMBER;
static GQuark QUARK_DESCRIPTORS;
static GQuark QUARK_STREAM_TYPE;
static GQuark QUARK_STREAMS;

static GQuark QUARK_NIT;
static GQuark QUARK_NETWORK_ID;
static GQuark QUARK_CURRENT_NEXT_INDICATOR;
static GQuark QUARK_ACTUAL_NETWORK;
static GQuark QUARK_NETWORK_NAME;
static GQuark QUARK_ORIGINAL_NETWORK_ID;
static GQuark QUARK_TRANSPORTS;
static GQuark QUARK_TERRESTRIAL;
static GQuark QUARK_CABLE;
static GQuark QUARK_FREQUENCY;
static GQuark QUARK_MODULATION;
static GQuark QUARK_BANDWIDTH;
static GQuark QUARK_CONSTELLATION;
static GQuark QUARK_HIERARCHY;
static GQuark QUARK_CODE_RATE_HP;
static GQuark QUARK_CODE_RATE_LP;
static GQuark QUARK_GUARD_INTERVAL;
static GQuark QUARK_TRANSMISSION_MODE;
static GQuark QUARK_OTHER_FREQUENCY;
static GQuark QUARK_SYMBOL_RATE;
static GQuark QUARK_INNER_FEC;
static GQuark QUARK_DELIVERY;
static GQuark QUARK_CHANNELS;
static GQuark QUARK_LOGICAL_CHANNEL_NUMBER;

static GQuark QUARK_SDT;
static GQuark QUARK_ACTUAL_TRANSPORT_STREAM;
static GQuark QUARK_SERVICES;

static GQuark QUARK_EIT;
static GQuark QUARK_SERVICE_ID;
static GQuark QUARK_PRESENT_FOLLOWING;
static GQuark QUARK_SEGMENT_LAST_SECTION_NUMBER;
static GQuark QUARK_LAST_TABLE_ID;
static GQuark QUARK_EVENTS;
static GQuark QUARK_NAME;
static GQuark QUARK_DESCRIPTION;
static GQuark QUARK_EXTENDED_ITEM;
static GQuark QUARK_EXTENDED_ITEMS;
static GQuark QUARK_TEXT;
static GQuark QUARK_EXTENDED_TEXT;
static GQuark QUARK_EVENT_ID;
static GQuark QUARK_YEAR;
static GQuark QUARK_MONTH;
static GQuark QUARK_DAY;
static GQuark QUARK_HOUR;
static GQuark QUARK_MINUTE;
static GQuark QUARK_SECOND;
static GQuark QUARK_DURATION;
static GQuark QUARK_RUNNING_STATUS;
static GQuark QUARK_FREE_CA_MODE;

#define MAX_KNOWN_ICONV 25
/* All these conversions will be to UTF8 */
typedef enum
{
  _ICONV_UNKNOWN = -1,
  _ICONV_ISO8859_1,
  _ICONV_ISO8859_2,
  _ICONV_ISO8859_3,
  _ICONV_ISO8859_4,
  _ICONV_ISO8859_5,
  _ICONV_ISO8859_6,
  _ICONV_ISO8859_7,
  _ICONV_ISO8859_8,
  _ICONV_ISO8859_9,
  _ICONV_ISO8859_10,
  _ICONV_ISO8859_11,
  _ICONV_ISO8859_12,
  _ICONV_ISO8859_13,
  _ICONV_ISO8859_14,
  _ICONV_ISO8859_15,
  _ICONV_ISO10646_UC2,
  _ICONV_EUC_KR,
  _ICONV_GB2312,
  _ICONV_UTF_16BE,
  _ICONV_ISO10646_UTF8,
  _ICONV_ISO6937,
  /* Insert more here if needed */
  _ICONV_MAX
} LocalIconvCode;

static const gchar *iconvtablename[] = {
  "iso-8859-1",
  "iso-8859-2",
  "iso-8859-3",
  "iso-8859-4",
  "iso-8859-5",
  "iso-8859-6",
  "iso-8859-7",
  "iso-8859-8",
  "iso-8859-9",
  "iso-8859-10",
  "iso-8859-11",
  "iso-8859-12",
  "iso-8859-13",
  "iso-8859-14",
  "iso-8859-15",
  "ISO-10646/UCS2",
  "EUC-KR",
  "GB2312",
  "UTF-16BE",
  "ISO-10646/UTF8",
  "iso6937"
      /* Insert more here if needed */
};

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

  /* Conversion tables */
  GIConv iconvs[_ICONV_MAX];
};

static void mpegts_packetizer_dispose (GObject * object);
static void mpegts_packetizer_finalize (GObject * object);
static gchar *get_encoding_and_convert (MpegTSPacketizer2 * packetizer,
    const gchar * text, guint length);
static GstClockTime calculate_skew (MpegTSPCR * pcr, guint64 pcrtime,
    GstClockTime time);
static void record_pcr (MpegTSPacketizer2 * packetizer, MpegTSPCR * pcrtable,
    guint64 pcr, guint64 offset);

#define CONTINUITY_UNSET 255
#define MAX_CONTINUITY 15
#define VERSION_NUMBER_UNSET 255
#define TABLE_ID_UNSET 0xFF
#define PACKET_SYNC_BYTE 0x47

static MpegTSPCR *
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
  memset (priv->pcrtablelut, 0xff, 0x200);
  priv->lastobsid = 0;
}

static gint
mpegts_packetizer_stream_subtable_compare (gconstpointer a, gconstpointer b)
{
  MpegTSPacketizerStreamSubtable *asub, *bsub;

  asub = (MpegTSPacketizerStreamSubtable *) a;
  bsub = (MpegTSPacketizerStreamSubtable *) b;

  if (asub->table_id == bsub->table_id &&
      asub->subtable_extension == bsub->subtable_extension)
    return 0;
  return -1;
}

static MpegTSPacketizerStreamSubtable *
mpegts_packetizer_stream_subtable_new (guint8 table_id,
    guint16 subtable_extension)
{
  MpegTSPacketizerStreamSubtable *subtable;

  subtable = g_new0 (MpegTSPacketizerStreamSubtable, 1);
  subtable->version_number = VERSION_NUMBER_UNSET;
  subtable->table_id = table_id;
  subtable->subtable_extension = subtable_extension;
  subtable->crc = 0;
  return subtable;
}

static MpegTSPacketizerStream *
mpegts_packetizer_stream_new (void)
{
  MpegTSPacketizerStream *stream;

  stream = (MpegTSPacketizerStream *) g_new0 (MpegTSPacketizerStream, 1);
  stream->continuity_counter = CONTINUITY_UNSET;
  stream->subtables = NULL;
  stream->section_table_id = TABLE_ID_UNSET;
  return stream;
}

static void
mpegts_packetizer_clear_section (MpegTSPacketizerStream * stream)
{
  stream->continuity_counter = CONTINUITY_UNSET;
  stream->section_length = 0;
  stream->section_offset = 0;
  stream->section_table_id = TABLE_ID_UNSET;
}

static void
mpegts_packetizer_stream_free (MpegTSPacketizerStream * stream)
{
  mpegts_packetizer_clear_section (stream);
  if (stream->section_data)
    g_free (stream->section_data);
  g_slist_foreach (stream->subtables, (GFunc) g_free, NULL);
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
  guint i;

  priv = packetizer->priv = MPEGTS_PACKETIZER_GET_PRIVATE (packetizer);
  packetizer->adapter = gst_adapter_new ();
  packetizer->offset = 0;
  packetizer->empty = TRUE;
  packetizer->streams = g_new0 (MpegTSPacketizerStream *, 8192);
  packetizer->know_packet_size = FALSE;
  packetizer->calculate_skew = FALSE;
  packetizer->calculate_offset = FALSE;

  priv->available = 0;
  priv->mapped = NULL;
  priv->mapped_size = 0;
  priv->offset = 0;

  memset (priv->pcrtablelut, 0xff, 0x200);
  memset (priv->observations, 0x0, sizeof (priv->observations));
  for (i = 0; i < _ICONV_MAX; i++)
    priv->iconvs[i] = (GIConv) - 1;

  priv->lastobsid = 0;

  priv->nb_seen_offsets = 0;
  priv->refoffset = -1;
  priv->last_in_time = GST_CLOCK_TIME_NONE;
}

static void
mpegts_packetizer_dispose (GObject * object)
{
  MpegTSPacketizer2 *packetizer = GST_MPEGTS_PACKETIZER (object);
  guint i;

  if (!packetizer->disposed) {
    if (packetizer->know_packet_size && packetizer->caps != NULL) {
      gst_caps_unref (packetizer->caps);
      packetizer->caps = NULL;
      packetizer->know_packet_size = FALSE;
    }
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

    for (i = 0; i < _ICONV_MAX; i++)
      if (packetizer->priv->iconvs[i] != (GIConv) - 1)
        g_iconv_close (packetizer->priv->iconvs[i]);

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

  if (packet->adaptation_field_control == 0x02) {
    /* no payload, adaptation field of 183 bytes */
    if (length != 183) {
      GST_DEBUG ("PID %d afc == 0x%x and length %d != 183",
          packet->pid, packet->adaptation_field_control, length);
    }
  } else if (length > 182) {
    GST_DEBUG ("PID %d afc == 0x%01x and length %d > 182",
        packet->pid, packet->adaptation_field_control, length);
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

  /* PCR */
  if (afcflags & MPEGTS_AFC_PCR_FLAG) {
    MpegTSPCR *pcrtable = NULL;
    packet->pcr = mpegts_packetizer_compute_pcr (data);
    data += 6;
    GST_DEBUG ("pcr 0x%04x %" G_GUINT64_FORMAT " (%" GST_TIME_FORMAT
        ") offset:%" G_GUINT64_FORMAT, packet->pid, packet->pcr,
        GST_TIME_ARGS (PCRTIME_TO_GSTTIME (packet->pcr)), packet->offset);

    if (GST_CLOCK_TIME_IS_VALID (packet->origts) && packetizer->calculate_skew) {
      pcrtable = get_pcr_table (packetizer, packet->pid);
      packet->origts = calculate_skew (pcrtable, packet->pcr, packet->origts);
    }
    if (packetizer->calculate_offset) {
      if (!pcrtable)
        pcrtable = get_pcr_table (packetizer, packet->pid);
      record_pcr (packetizer, pcrtable, packet->pcr, packet->offset);
    }
  }

  /* OPCR */
  if (afcflags & MPEGTS_AFC_OPCR_FLAG) {
    packet->opcr = mpegts_packetizer_compute_pcr (data);
    /* *data += 6; */
    GST_DEBUG ("opcr %" G_GUINT64_FORMAT " (%" GST_TIME_FORMAT ")",
        packet->pcr, GST_TIME_ARGS (PCRTIME_TO_GSTTIME (packet->pcr)));
  }

  return TRUE;
}

static MpegTSPacketizerPacketReturn
mpegts_packetizer_parse_packet (MpegTSPacketizer2 * packetizer,
    MpegTSPacketizerPacket * packet)
{
  guint8 *data;

  data = packet->data_start;
  data++;

  /* transport_error_indicator 1 */
  if (G_UNLIKELY (*data >> 7))
    return PACKET_BAD;

  /* payload_unit_start_indicator 1 */
  packet->payload_unit_start_indicator = (*data >> 6) & 0x01;

  /* transport_priority 1 */
  /* PID 13 */
  packet->pid = GST_READ_UINT16_BE (data) & 0x1FFF;
  data += 2;

  /* transport_scrambling_control 2 */
  if (G_UNLIKELY (*data >> 6))
    return PACKET_BAD;

  /* adaptation_field_control 2 */
  packet->adaptation_field_control = (*data >> 4) & 0x03;

  /* continuity_counter 4 */
  packet->continuity_counter = *data & 0x0F;
  data += 1;

  packet->data = data;

  if (packet->adaptation_field_control & 0x02)
    if (!mpegts_packetizer_parse_adaptation_field_control (packetizer, packet))
      return FALSE;

  if (packet->adaptation_field_control & 0x01)
    packet->payload = packet->data;
  else
    packet->payload = NULL;

  return PACKET_OK;
}

static gboolean
mpegts_packetizer_parse_section_header (MpegTSPacketizer2 * packetizer,
    MpegTSPacketizerStream * stream, MpegTSPacketizerSection * section)
{
  guint8 tmp;
  guint8 *data, *crc_data;
  MpegTSPacketizerStreamSubtable *subtable;
  GSList *subtable_list = NULL;

  section->complete = TRUE;
  /* get the section buffer, ownership stays with the stream */
  data = section->data = stream->section_data;
  section->offset = stream->offset;

  GST_MEMDUMP ("section header", data, stream->section_length);

  /* table_id : 8 bits
   * NOTE : Already parsed/stored in _push_section()
   */
  section->table_id = stream->section_table_id;
  data += 1;

  /* section_syntax_indicator :  1 bit
   * private_indicator        :  1 bit
   * RESERVED                 :  2 bit
   * private_section_length   : 12 bit
   */
  /* if table_id is 0 (pat) then ignore the subtable extension */
  if ((data[0] & 0x80) == 0 || section->table_id == 0)
    section->subtable_extension = 0;
  else
    section->subtable_extension = GST_READ_UINT16_BE (data + 2);

  subtable = mpegts_packetizer_stream_subtable_new (section->table_id,
      section->subtable_extension);

  subtable_list = g_slist_find_custom (stream->subtables, subtable,
      mpegts_packetizer_stream_subtable_compare);
  if (subtable_list) {
    g_free (subtable);
    subtable = (MpegTSPacketizerStreamSubtable *) (subtable_list->data);
  } else {
    stream->subtables = g_slist_prepend (stream->subtables, subtable);
  }

  /* private_section_length : 12 bit
   * NOTE : Already parsed/stored in _push_section()
   * NOTE : Same as private_section_length mentionned above
   */
  section->section_length = stream->section_length;
  data += 2;

  /* transport_stream_id    : 16 bit */
  /* skip to the version byte */
  data += 2;

  /* Reserved               :  2 bits
   * version_number         :  5 bits
   * current_next_indicator : 1 bit*/
  tmp = *data++;
  section->version_number = (tmp >> 1) & 0x1F;
  section->current_next_indicator = tmp & 0x01;

  if (!section->current_next_indicator)
    goto not_applicable;

  /* CRC is at the end of the section */
  crc_data = section->data + section->section_length - 4;
  section->crc = GST_READ_UINT32_BE (crc_data);

  if (section->version_number == subtable->version_number &&
      section->crc == subtable->crc)
    goto no_changes;

  subtable->version_number = section->version_number;
  subtable->crc = section->crc;
  stream->section_table_id = section->table_id;

  return TRUE;

no_changes:
  GST_LOG
      ("no changes. pid 0x%04x table_id 0x%02x subtable_extension %d, current_next %d version %d, crc 0x%x",
      section->pid, section->table_id, section->subtable_extension,
      section->current_next_indicator, section->version_number, section->crc);
  section->complete = FALSE;
  return TRUE;

not_applicable:
  GST_LOG
      ("not applicable pid 0x%04x table_id 0x%02x subtable_extension %d, current_next %d version %d, crc 0x%x",
      section->pid, section->table_id, section->subtable_extension,
      section->current_next_indicator, section->version_number, section->crc);
  section->complete = FALSE;
  return TRUE;
}

static gboolean
mpegts_packetizer_parse_descriptors (MpegTSPacketizer2 * packetizer,
    guint8 ** buffer, guint8 * buffer_end, GValueArray * descriptors)
{
  guint8 length;
  guint8 *data;
  GValue value = { 0 };
  GString *desc;

  data = *buffer;

  while (data < buffer_end) {
    data++;                     /* skip tag */
    length = *data++;

    if (data + length > buffer_end) {
      GST_WARNING ("invalid descriptor length %d now at %d max %d", length,
          (gint) (data - *buffer), (gint) (buffer_end - *buffer));
      goto error;
    }

    /* include length */
    desc = g_string_new_len ((gchar *) data - 2, length + 2);
    data += length;
    /* G_TYPE_GSTRING is a GBoxed type and is used so properly marshalled from python */
    g_value_init (&value, G_TYPE_GSTRING);
    g_value_take_boxed (&value, desc);
    g_value_array_append (descriptors, &value);
    g_value_unset (&value);
  }

  if (data != buffer_end) {
    GST_WARNING ("descriptors size %d expected %d", (gint) (data - *buffer),
        (gint) (buffer_end - *buffer));
    goto error;
  }

  *buffer = data;

  return TRUE;
error:
  return FALSE;
}

GstStructure *
mpegts_packetizer_parse_cat (MpegTSPacketizer2 * packetizer,
    MpegTSPacketizerSection * section)
{
  GstStructure *cat_info = NULL;
  guint8 *data;
  guint8 tmp;
  GValueArray *descriptors;
  GstMPEGDescriptor desc;
  guint desc_len;

  /* Skip parts already parsed */
  data = section->data + 3;

  /* reserved  : 18bits */
  data += 2;

  /* version_number         : 5 bits
   * current_next_indicator : 1 bit */
  tmp = *data++;
  section->version_number = (tmp >> 1) & 0x1F;
  section->current_next_indicator = tmp & 0x01;

  /* skip already handled section_number and last_section_number */
  data += 2;

  cat_info = gst_structure_new_id_empty (QUARK_CAT);

  /* descriptors */
  desc_len = section->section_length - 4 - 8;
  gst_mpeg_descriptor_parse (&desc, data, desc_len);
  descriptors = g_value_array_new (desc.n_desc);
  if (!mpegts_packetizer_parse_descriptors (packetizer, &data, data + desc_len,
          descriptors)) {
    g_value_array_free (descriptors);
    goto error;
  }
  gst_structure_id_set (cat_info, QUARK_DESCRIPTORS, G_TYPE_VALUE_ARRAY,
      descriptors, NULL);
  g_value_array_free (descriptors);

  return cat_info;
error:
  if (cat_info)
    gst_structure_free (cat_info);
  return NULL;
}

GstStructure *
mpegts_packetizer_parse_pat (MpegTSPacketizer2 * packetizer,
    MpegTSPacketizerSection * section)
{
  GstStructure *pat_info = NULL;
  guint8 *data, *end;
  guint transport_stream_id;
  guint8 tmp;
  guint program_number;
  guint pmt_pid;
  GValue entries = { 0 };
  GValue value = { 0 };
  GstStructure *entry = NULL;
  gchar *struct_name;

  data = section->data;

  data += 3;

  transport_stream_id = GST_READ_UINT16_BE (data);
  data += 2;

  tmp = *data++;
  section->version_number = (tmp >> 1) & 0x1F;
  section->current_next_indicator = tmp & 0x01;

  /* skip section_number and last_section_number */
  data += 2;

  pat_info = gst_structure_new_id (QUARK_PAT,
      QUARK_TRANSPORT_STREAM_ID, G_TYPE_UINT, transport_stream_id, NULL);
  g_value_init (&entries, GST_TYPE_LIST);
  /* stop at the CRC */
  end = section->data + section->section_length;
  while (data < end - 4) {
    program_number = GST_READ_UINT16_BE (data);
    data += 2;

    pmt_pid = GST_READ_UINT16_BE (data) & 0x1FFF;
    data += 2;

    struct_name = g_strdup_printf ("program-%d", program_number);
    entry = gst_structure_new_empty (struct_name);
    g_free (struct_name);
    gst_structure_id_set (entry, QUARK_PROGRAM_NUMBER, G_TYPE_UINT,
        program_number, QUARK_PID, G_TYPE_UINT, pmt_pid, NULL);

    g_value_init (&value, GST_TYPE_STRUCTURE);
    g_value_take_boxed (&value, entry);
    gst_value_list_append_value (&entries, &value);
    g_value_unset (&value);
  }

  gst_structure_id_take_value (pat_info, QUARK_PROGRAMS, &entries);

  if (data != end - 4) {
    /* FIXME: check the CRC before parsing the packet */
    GST_ERROR ("at the end of PAT data != end - 4");
    gst_structure_free (pat_info);

    return NULL;
  }

  return pat_info;
}

GstStructure *
mpegts_packetizer_parse_pmt (MpegTSPacketizer2 * packetizer,
    MpegTSPacketizerSection * section)
{
  GstStructure *pmt = NULL;
  guint8 *data, *end;
  guint16 program_number;
  guint8 tmp;
  guint pcr_pid;
  guint program_info_length;
  guint8 stream_type;
  guint16 pid;
  guint stream_info_length;
  GValueArray *descriptors;
  GValue stream_value = { 0 };
  GValue programs = { 0 };
  GstStructure *stream_info = NULL;
  gchar *struct_name;

  /* fixed header + CRC == 16 */
  if (section->section_length < 16) {
    GST_WARNING ("PID %d invalid PMT size %d",
        section->pid, section->section_length);
    goto error;
  }

  data = section->data;
  end = data + section->section_length;

  data += 3;

  program_number = GST_READ_UINT16_BE (data);
  data += 2;

  GST_DEBUG ("Parsing %d Program Map Table", program_number);

  tmp = *data++;
  section->version_number = (tmp >> 1) & 0x1F;
  section->current_next_indicator = tmp & 0x01;

  /* skip section_number and last_section_number */
  data += 2;

  pcr_pid = GST_READ_UINT16_BE (data) & 0x1FFF;
  data += 2;

  program_info_length = GST_READ_UINT16_BE (data) & 0x0FFF;
  data += 2;

  pmt = gst_structure_new_id (QUARK_PMT,
      QUARK_PROGRAM_NUMBER, G_TYPE_UINT, program_number,
      QUARK_PCR_PID, G_TYPE_UINT, pcr_pid,
      QUARK_VERSION_NUMBER, G_TYPE_UINT, section->version_number, NULL);

  if (program_info_length) {
    /* check that the buffer is large enough to contain at least
     * program_info_length bytes + CRC */
    if (data + program_info_length + 4 > end) {
      GST_WARNING ("PID %d invalid program info length %d left %d",
          section->pid, program_info_length, (gint) (end - data));
      goto error;
    }

    descriptors = g_value_array_new (0);
    if (!mpegts_packetizer_parse_descriptors (packetizer,
            &data, data + program_info_length, descriptors)) {
      g_value_array_free (descriptors);
      goto error;
    }

    gst_structure_id_set (pmt, QUARK_DESCRIPTORS, G_TYPE_VALUE_ARRAY,
        descriptors, NULL);
    g_value_array_free (descriptors);
  }

  g_value_init (&programs, GST_TYPE_LIST);
  /* parse entries, cycle until there's space for another entry (at least 5
   * bytes) plus the CRC */
  while (data <= end - 4 - 5) {
    stream_type = *data++;
    GST_DEBUG ("Stream type 0x%02x found", stream_type);

    pid = GST_READ_UINT16_BE (data) & 0x1FFF;
    data += 2;

    stream_info_length = GST_READ_UINT16_BE (data) & 0x0FFF;
    data += 2;

    if (data + stream_info_length + 4 > end) {
      GST_WARNING ("PID %d invalid stream info length %d left %d", section->pid,
          stream_info_length, (gint) (end - data));
      g_value_unset (&programs);
      goto error;
    }

    struct_name = g_strdup_printf ("pid-%d", pid);
    stream_info = gst_structure_new_empty (struct_name);
    g_free (struct_name);
    gst_structure_id_set (stream_info,
        QUARK_PID, G_TYPE_UINT, pid, QUARK_STREAM_TYPE, G_TYPE_UINT,
        stream_type, NULL);

    if (stream_info_length) {
      /* check for AC3 descriptor */
      GstMPEGDescriptor desc;

      if (gst_mpeg_descriptor_parse (&desc, data, stream_info_length)) {
        /* DVB AC3 */
        guint8 *desc_data;
        if (gst_mpeg_descriptor_find (&desc, DESC_DVB_AC3)) {
          gst_structure_set (stream_info, "has-ac3", G_TYPE_BOOLEAN, TRUE,
              NULL);
        }

        /* DATA BROADCAST ID */
        desc_data =
            gst_mpeg_descriptor_find (&desc, DESC_DVB_DATA_BROADCAST_ID);
        if (desc_data) {
          guint16 data_broadcast_id;
          data_broadcast_id =
              DESC_DVB_DATA_BROADCAST_ID_data_broadcast_id (desc_data);
          gst_structure_set (stream_info, "data-broadcast-id", G_TYPE_UINT,
              data_broadcast_id, NULL);
        }

        /* DATA BROADCAST */
        desc_data = gst_mpeg_descriptor_find (&desc, DESC_DVB_DATA_BROADCAST);
        if (desc_data) {
          GstStructure *databroadcast_info;
          guint16 data_broadcast_id;
          guint8 component_tag;
          data_broadcast_id =
              DESC_DVB_DATA_BROADCAST_data_broadcast_id (desc_data);
          component_tag = DESC_DVB_DATA_BROADCAST_component_tag (desc_data);
          databroadcast_info = gst_structure_new ("data-broadcast", "id",
              G_TYPE_UINT, data_broadcast_id, "component-tag", component_tag,
              NULL);
          gst_structure_set (stream_info, "data-broadcast", GST_TYPE_STRUCTURE,
              databroadcast_info, NULL);
        }

        /* DVB CAROUSEL IDENTIFIER */
        desc_data =
            gst_mpeg_descriptor_find (&desc, DESC_DVB_CAROUSEL_IDENTIFIER);
        if (desc_data) {
          guint32 carousel_id;
          carousel_id = DESC_DVB_CAROUSEL_IDENTIFIER_carousel_id (desc_data);
          gst_structure_set (stream_info, "carousel-id", G_TYPE_UINT,
              carousel_id, NULL);
        }

        /* DVB STREAM IDENTIFIER */
        desc_data =
            gst_mpeg_descriptor_find (&desc, DESC_DVB_STREAM_IDENTIFIER);
        if (desc_data) {
          guint8 component_tag;
          component_tag = DESC_DVB_STREAM_IDENTIFIER_component_tag (desc_data);
          gst_structure_set (stream_info, "component-tag", G_TYPE_UINT,
              component_tag, NULL);
        }

        /* ISO 639 LANGUAGE */
        desc_data = gst_mpeg_descriptor_find (&desc, DESC_ISO_639_LANGUAGE);
        if (desc_data && DESC_ISO_639_LANGUAGE_codes_n (desc_data)) {
          gchar *lang_code;
          gchar *language_n = (gchar *)
              DESC_ISO_639_LANGUAGE_language_code_nth (desc_data, 0);
          lang_code = g_strndup (language_n, 3);
          gst_structure_set (stream_info, "lang-code", G_TYPE_STRING,
              lang_code, NULL);
          g_free (lang_code);
        }

        descriptors = g_value_array_new (desc.n_desc);
        if (!mpegts_packetizer_parse_descriptors (packetizer,
                &data, data + stream_info_length, descriptors)) {
          g_value_unset (&programs);
          gst_structure_free (stream_info);
          g_value_array_free (descriptors);
          goto error;
        }

        gst_structure_id_set (stream_info,
            QUARK_DESCRIPTORS, G_TYPE_VALUE_ARRAY, descriptors, NULL);
        g_value_array_free (descriptors);
      }
    }

    g_value_init (&stream_value, GST_TYPE_STRUCTURE);
    g_value_take_boxed (&stream_value, stream_info);
    gst_value_list_append_value (&programs, &stream_value);
    g_value_unset (&stream_value);
  }

  gst_structure_id_take_value (pmt, QUARK_STREAMS, &programs);

  g_assert (data == end - 4);

  return pmt;

error:
  if (pmt)
    gst_structure_free (pmt);

  return NULL;
}

GstStructure *
mpegts_packetizer_parse_nit (MpegTSPacketizer2 * packetizer,
    MpegTSPacketizerSection * section)
{
  GstStructure *nit = NULL, *transport = NULL, *delivery_structure = NULL;
  guint8 *data, *end, *entry_begin;
  guint16 network_id, transport_stream_id, original_network_id;
  guint tmp;
  guint16 descriptors_loop_length, transport_stream_loop_length;
  GValue transports = { 0 };
  GValue transport_value = { 0 };
  GValueArray *descriptors = NULL;

  GST_DEBUG ("NIT");

  /* fixed header + CRC == 16 */
  if (section->section_length < 23) {
    GST_WARNING ("PID %d invalid NIT size %d",
        section->pid, section->section_length);
    goto error;
  }

  data = section->data;
  end = data + section->section_length;

  data += 3;

  network_id = GST_READ_UINT16_BE (data);
  data += 2;

  tmp = *data++;
  section->version_number = (tmp >> 1) & 0x1F;
  section->current_next_indicator = tmp & 0x01;

  /* skip section_number and last_section_number */
  data += 2;

  descriptors_loop_length = GST_READ_UINT16_BE (data) & 0x0FFF;
  data += 2;

  nit = gst_structure_new_id (QUARK_NIT,
      QUARK_NETWORK_ID, G_TYPE_UINT, network_id,
      QUARK_VERSION_NUMBER, G_TYPE_UINT, section->version_number,
      QUARK_CURRENT_NEXT_INDICATOR, G_TYPE_UINT,
      section->current_next_indicator, QUARK_ACTUAL_NETWORK, G_TYPE_BOOLEAN,
      section->table_id == 0x40, NULL);

  /* see if the buffer is large enough */
  if (descriptors_loop_length) {
    guint8 *networkname_descriptor;
    GstMPEGDescriptor mpegdescriptor;

    if (data + descriptors_loop_length > end - 4) {
      GST_WARNING ("PID %d invalid NIT descriptors loop length %d",
          section->pid, descriptors_loop_length);
      gst_structure_free (nit);
      goto error;
    }
    if (gst_mpeg_descriptor_parse (&mpegdescriptor, data,
            descriptors_loop_length)) {
      networkname_descriptor =
          gst_mpeg_descriptor_find (&mpegdescriptor, DESC_DVB_NETWORK_NAME);
      if (networkname_descriptor != NULL) {
        gchar *networkname_tmp;

        /* No need to bounds check this value as it comes from the descriptor length itself */
        guint8 networkname_length =
            DESC_DVB_NETWORK_NAME_length (networkname_descriptor);
        gchar *networkname =
            (gchar *) DESC_DVB_NETWORK_NAME_text (networkname_descriptor);

        networkname_tmp =
            get_encoding_and_convert (packetizer, networkname,
            networkname_length);
        gst_structure_id_set (nit, QUARK_NETWORK_NAME, G_TYPE_STRING,
            networkname_tmp, NULL);
        g_free (networkname_tmp);
      }

      descriptors = g_value_array_new (mpegdescriptor.n_desc);
      if (!mpegts_packetizer_parse_descriptors (packetizer,
              &data, data + descriptors_loop_length, descriptors)) {
        gst_structure_free (nit);
        g_value_array_free (descriptors);
        goto error;
      }
      gst_structure_id_set (nit, QUARK_DESCRIPTORS, G_TYPE_VALUE_ARRAY,
          descriptors, NULL);
      g_value_array_free (descriptors);
    }
  }

  transport_stream_loop_length = GST_READ_UINT16_BE (data) & 0x0FFF;
  data += 2;

  g_value_init (&transports, GST_TYPE_LIST);
  /* read up to the CRC */
  while (transport_stream_loop_length - 4 > 0) {
    gchar *transport_name;

    entry_begin = data;

    if (transport_stream_loop_length < 10) {
      /* each entry must be at least 6 bytes (+ 4bytes CRC) */
      GST_WARNING ("PID %d invalid NIT entry size %d",
          section->pid, transport_stream_loop_length);
      goto error;
    }

    transport_stream_id = GST_READ_UINT16_BE (data);
    data += 2;

    original_network_id = GST_READ_UINT16_BE (data);
    data += 2;

    descriptors_loop_length = GST_READ_UINT16_BE (data) & 0x0FFF;
    data += 2;

    transport_name = g_strdup_printf ("transport-%d", transport_stream_id);
    transport = gst_structure_new_empty (transport_name);
    g_free (transport_name);
    gst_structure_id_set (transport,
        QUARK_TRANSPORT_STREAM_ID, G_TYPE_UINT, transport_stream_id,
        QUARK_ORIGINAL_NETWORK_ID, G_TYPE_UINT, original_network_id, NULL);

    if (descriptors_loop_length) {
      GstMPEGDescriptor mpegdescriptor;
      guint8 *delivery;

      if (data + descriptors_loop_length > end - 4) {
        GST_WARNING ("PID %d invalid NIT entry %d descriptors loop length %d",
            section->pid, transport_stream_id, descriptors_loop_length);
        gst_structure_free (transport);
        goto error;
      }
      gst_mpeg_descriptor_parse (&mpegdescriptor, data,
          descriptors_loop_length);

      if ((delivery = gst_mpeg_descriptor_find (&mpegdescriptor,
                  DESC_DVB_SATELLITE_DELIVERY_SYSTEM))) {

        guint8 *frequency_bcd =
            DESC_DVB_SATELLITE_DELIVERY_SYSTEM_frequency (delivery);
        guint32 frequency =
            10 * ((frequency_bcd[3] & 0x0F) +
            10 * ((frequency_bcd[3] & 0xF0) >> 4) +
            100 * (frequency_bcd[2] & 0x0F) +
            1000 * ((frequency_bcd[2] & 0xF0) >> 4) +
            10000 * (frequency_bcd[1] & 0x0F) +
            100000 * ((frequency_bcd[1] & 0xF0) >> 4) +
            1000000 * (frequency_bcd[0] & 0x0F) +
            10000000 * ((frequency_bcd[0] & 0xF0) >> 4));
        guint8 *orbital_bcd =
            DESC_DVB_SATELLITE_DELIVERY_SYSTEM_orbital_position (delivery);
        gfloat orbital =
            (orbital_bcd[1] & 0x0F) / 10. + ((orbital_bcd[1] & 0xF0) >> 4) +
            10 * (orbital_bcd[0] & 0x0F) + 100 * ((orbital_bcd[0] & 0xF0) >> 4);
        gboolean east =
            DESC_DVB_SATELLITE_DELIVERY_SYSTEM_west_east_flag (delivery);
        guint8 polarization =
            DESC_DVB_SATELLITE_DELIVERY_SYSTEM_polarization (delivery);
        const gchar *polarization_str;
        guint8 modulation =
            DESC_DVB_SATELLITE_DELIVERY_SYSTEM_modulation (delivery);
        const gchar *modulation_str;
        guint8 *symbol_rate_bcd =
            DESC_DVB_SATELLITE_DELIVERY_SYSTEM_symbol_rate (delivery);
        guint32 symbol_rate =
            (symbol_rate_bcd[2] & 0x0F) +
            10 * ((symbol_rate_bcd[2] & 0xF0) >> 4) +
            100 * (symbol_rate_bcd[1] & 0x0F) +
            1000 * ((symbol_rate_bcd[1] & 0xF0) >> 4) +
            10000 * (symbol_rate_bcd[0] & 0x0F) +
            100000 * ((symbol_rate_bcd[0] & 0xF0) >> 4);
        guint8 fec_inner =
            DESC_DVB_SATELLITE_DELIVERY_SYSTEM_fec_inner (delivery);
        const gchar *fec_inner_str;

        switch (polarization) {
          case 0:
            polarization_str = "horizontal";
            break;
          case 1:
            polarization_str = "vertical";
            break;
          case 2:
            polarization_str = "left";
            break;
          case 3:
            polarization_str = "right";
            break;
          default:
            polarization_str = "";
        }
        switch (fec_inner) {
          case 0:
            fec_inner_str = "undefined";
            break;
          case 1:
            fec_inner_str = "1/2";
            break;
          case 2:
            fec_inner_str = "2/3";
            break;
          case 3:
            fec_inner_str = "3/4";
            break;
          case 4:
            fec_inner_str = "5/6";
            break;
          case 5:
            fec_inner_str = "7/8";
            break;
          case 6:
            fec_inner_str = "8/9";
            break;
          case 0xF:
            fec_inner_str = "none";
            break;
          default:
            fec_inner_str = "reserved";
        }
        switch (modulation) {
          case 0x00:
            modulation_str = "auto";
            break;
          case 0x01:
            modulation_str = "QPSK";
            break;
          case 0x02:
            modulation_str = "8PSK";
            break;
          case 0x03:
            modulation_str = "QAM16";
            break;
          default:
            modulation_str = "";
            break;
        }
        delivery_structure = gst_structure_new ("satellite",
            "orbital", G_TYPE_FLOAT, orbital,
            "east-or-west", G_TYPE_STRING, east ? "east" : "west",
            "modulation", G_TYPE_STRING, modulation_str,
            "frequency", G_TYPE_UINT, frequency,
            "polarization", G_TYPE_STRING, polarization_str,
            "symbol-rate", G_TYPE_UINT, symbol_rate,
            "inner-fec", G_TYPE_STRING, fec_inner_str, NULL);
        gst_structure_id_set (transport, QUARK_DELIVERY, GST_TYPE_STRUCTURE,
            delivery_structure, NULL);
      } else if ((delivery = gst_mpeg_descriptor_find (&mpegdescriptor,
                  DESC_DVB_TERRESTRIAL_DELIVERY_SYSTEM))) {

        guint32 frequency =
            DESC_DVB_TERRESTRIAL_DELIVERY_SYSTEM_frequency (delivery) * 10;
        guint8 bandwidth =
            DESC_DVB_TERRESTRIAL_DELIVERY_SYSTEM_bandwidth (delivery);
        guint8 constellation =
            DESC_DVB_TERRESTRIAL_DELIVERY_SYSTEM_constellation (delivery);
        guint8 hierarchy =
            DESC_DVB_TERRESTRIAL_DELIVERY_SYSTEM_hierarchy (delivery);
        guint8 code_rate_hp =
            DESC_DVB_TERRESTRIAL_DELIVERY_SYSTEM_code_rate_hp (delivery);
        guint8 code_rate_lp =
            DESC_DVB_TERRESTRIAL_DELIVERY_SYSTEM_code_rate_lp (delivery);
        guint8 guard_interval =
            DESC_DVB_TERRESTRIAL_DELIVERY_SYSTEM_guard_interval (delivery);
        guint8 transmission_mode =
            DESC_DVB_TERRESTRIAL_DELIVERY_SYSTEM_transmission_mode (delivery);
        gboolean other_frequency =
            DESC_DVB_TERRESTRIAL_DELIVERY_SYSTEM_other_frequency (delivery);
        const gchar *constellation_str, *code_rate_hp_str, *code_rate_lp_str,
            *transmission_mode_str;
        /* do the stuff */
        /* bandwidth is 8 if 0, 7 if 1, 6 if 2, reserved otherwise */
        if (bandwidth <= 2)
          bandwidth = 8 - bandwidth;
        else
          bandwidth = 0;
        switch (constellation) {
          case 0:
            constellation_str = "QPSK";
            break;
          case 1:
            constellation_str = "QAM16";
            break;
          case 2:
            constellation_str = "QAM64";
            break;
          default:
            constellation_str = "reserved";
        }
        /* hierarchy is 4 if 3, 2 if 2, 1 if 1, 0 if 0, reserved if > 3 */
        if (hierarchy <= 3) {
          if (hierarchy == 3)
            hierarchy = 4;
        } else {
          hierarchy = 0;
        }

        switch (code_rate_hp) {
          case 0:
            code_rate_hp_str = "1/2";
            break;
          case 1:
            code_rate_hp_str = "2/3";
            break;
          case 2:
            code_rate_hp_str = "3/4";
            break;
          case 3:
            code_rate_hp_str = "5/6";
            break;
          case 4:
            code_rate_hp_str = "7/8";
            break;
          default:
            code_rate_hp_str = "reserved";
        }

        switch (code_rate_lp) {
          case 0:
            code_rate_lp_str = "1/2";
            break;
          case 1:
            code_rate_lp_str = "2/3";
            break;
          case 2:
            code_rate_lp_str = "3/4";
            break;
          case 3:
            code_rate_lp_str = "5/6";
            break;
          case 4:
            code_rate_lp_str = "7/8";
            break;
          default:
            code_rate_lp_str = "reserved";
        }
        /* guard is 32 if 0, 16 if 1, 8 if 2, 4 if 3 */
        switch (guard_interval) {
          case 0:
            guard_interval = 32;
            break;
          case 1:
            guard_interval = 16;
            break;
          case 2:
            guard_interval = 8;
            break;
          case 3:
            guard_interval = 4;
            break;
          default:             /* make it default to 32 */
            guard_interval = 32;
        }
        switch (transmission_mode) {
          case 0:
            transmission_mode_str = "2k";
            break;
          case 1:
            transmission_mode_str = "8k";
            break;
          default:
            transmission_mode_str = "reserved";
        }
        delivery_structure = gst_structure_new_id (QUARK_TERRESTRIAL,
            QUARK_FREQUENCY, G_TYPE_UINT, frequency,
            QUARK_BANDWIDTH, G_TYPE_UINT, bandwidth,
            QUARK_CONSTELLATION, G_TYPE_STRING, constellation_str,
            QUARK_HIERARCHY, G_TYPE_UINT, hierarchy,
            QUARK_CODE_RATE_HP, G_TYPE_STRING, code_rate_hp_str,
            QUARK_CODE_RATE_LP, G_TYPE_STRING, code_rate_lp_str,
            QUARK_GUARD_INTERVAL, G_TYPE_UINT, guard_interval,
            QUARK_TRANSMISSION_MODE, G_TYPE_STRING, transmission_mode_str,
            QUARK_OTHER_FREQUENCY, G_TYPE_BOOLEAN, other_frequency, NULL);
        gst_structure_id_set (transport, QUARK_DELIVERY, GST_TYPE_STRUCTURE,
            delivery_structure, NULL);
      } else if ((delivery = gst_mpeg_descriptor_find (&mpegdescriptor,
                  DESC_DVB_CABLE_DELIVERY_SYSTEM))) {

        guint8 *frequency_bcd =
            DESC_DVB_CABLE_DELIVERY_SYSTEM_frequency (delivery);
        /* see en 300 468 section 6.2.13.1 least significant bcd digit
         * is measured in 100Hz units so multiplier needs to be 100 to get
         * into Hz */
        guint32 frequency = 100 *
            ((frequency_bcd[3] & 0x0F) +
            10 * ((frequency_bcd[3] & 0xF0) >> 4) +
            100 * (frequency_bcd[2] & 0x0F) +
            1000 * ((frequency_bcd[2] & 0xF0) >> 4) +
            10000 * (frequency_bcd[1] & 0x0F) +
            100000 * ((frequency_bcd[1] & 0xF0) >> 4) +
            1000000 * (frequency_bcd[0] & 0x0F) +
            10000000 * ((frequency_bcd[0] & 0xF0) >> 4));
        guint8 modulation =
            DESC_DVB_CABLE_DELIVERY_SYSTEM_modulation (delivery);
        const gchar *modulation_str;
        guint8 *symbol_rate_bcd =
            DESC_DVB_CABLE_DELIVERY_SYSTEM_symbol_rate (delivery);
        guint32 symbol_rate =
            (symbol_rate_bcd[2] & 0x0F) +
            10 * ((symbol_rate_bcd[2] & 0xF0) >> 4) +
            100 * (symbol_rate_bcd[1] & 0x0F) +
            1000 * ((symbol_rate_bcd[1] & 0xF0) >> 4) +
            10000 * (symbol_rate_bcd[0] & 0x0F) +
            100000 * ((symbol_rate_bcd[0] & 0xF0) >> 4);
        guint8 fec_inner = DESC_DVB_CABLE_DELIVERY_SYSTEM_fec_inner (delivery);
        const gchar *fec_inner_str;

        switch (fec_inner) {
          case 0:
            fec_inner_str = "undefined";
            break;
          case 1:
            fec_inner_str = "1/2";
            break;
          case 2:
            fec_inner_str = "2/3";
            break;
          case 3:
            fec_inner_str = "3/4";
            break;
          case 4:
            fec_inner_str = "5/6";
            break;
          case 5:
            fec_inner_str = "7/8";
            break;
          case 6:
            fec_inner_str = "8/9";
            break;
          case 0xF:
            fec_inner_str = "none";
            break;
          default:
            fec_inner_str = "reserved";
        }
        switch (modulation) {
          case 0x00:
            modulation_str = "undefined";
            break;
          case 0x01:
            modulation_str = "QAM16";
            break;
          case 0x02:
            modulation_str = "QAM32";
            break;
          case 0x03:
            modulation_str = "QAM64";
            break;
          case 0x04:
            modulation_str = "QAM128";
            break;
          case 0x05:
            modulation_str = "QAM256";
            break;
          default:
            modulation_str = "reserved";
        }
        delivery_structure = gst_structure_new_id (QUARK_CABLE,
            QUARK_MODULATION, G_TYPE_STRING, modulation_str,
            QUARK_FREQUENCY, G_TYPE_UINT, frequency,
            QUARK_SYMBOL_RATE, G_TYPE_UINT, symbol_rate,
            QUARK_INNER_FEC, G_TYPE_STRING, fec_inner_str, NULL);
        gst_structure_id_set (transport, QUARK_DELIVERY, GST_TYPE_STRUCTURE,
            delivery_structure, NULL);
      }
      /* free the temporary delivery structure */
      if (delivery_structure != NULL) {
        gst_structure_free (delivery_structure);
        delivery_structure = NULL;
      }
      if ((delivery = gst_mpeg_descriptor_find (&mpegdescriptor,
                  DESC_DTG_LOGICAL_CHANNEL))) {
        guint8 *current_pos = delivery + 2;
        GValue channel_numbers = { 0 };

        g_value_init (&channel_numbers, GST_TYPE_LIST);
        while (current_pos < delivery + DESC_LENGTH (delivery)) {
          GstStructure *channel;
          GValue channel_value = { 0 };
          guint16 service_id = GST_READ_UINT16_BE (current_pos);
          guint16 logical_channel_number;

          current_pos += 2;
          logical_channel_number = GST_READ_UINT16_BE (current_pos) & 0x03ff;
          channel = gst_structure_new_id (QUARK_CHANNELS,
              QUARK_SERVICE_ID, G_TYPE_UINT,
              service_id, QUARK_LOGICAL_CHANNEL_NUMBER, G_TYPE_UINT,
              logical_channel_number, NULL);
          g_value_init (&channel_value, GST_TYPE_STRUCTURE);
          g_value_take_boxed (&channel_value, channel);
          gst_value_list_append_value (&channel_numbers, &channel_value);
          g_value_unset (&channel_value);
          current_pos += 2;
        }
        gst_structure_id_take_value (transport, QUARK_CHANNELS,
            &channel_numbers);
      }
      if ((delivery = gst_mpeg_descriptor_find (&mpegdescriptor,
                  DESC_DVB_FREQUENCY_LIST))) {
        guint8 *current_pos = delivery + 2;
        GValue frequencies = { 0 };
        guint8 type;

        type = *current_pos & 0x03;
        current_pos++;

        if (type) {
          const gchar *fieldname = NULL;
          g_value_init (&frequencies, GST_TYPE_LIST);

          while (current_pos < delivery + DESC_LENGTH (delivery) - 3) {
            guint32 freq = 0;
            guint8 *frequency_bcd = current_pos;
            GValue frequency = { 0 };

            switch (type) {
              case 0x01:
                /* satellite */
                freq =
                    10 * ((frequency_bcd[3] & 0x0F) +
                    10 * ((frequency_bcd[3] & 0xF0) >> 4) +
                    100 * (frequency_bcd[2] & 0x0F) +
                    1000 * ((frequency_bcd[2] & 0xF0) >> 4) +
                    10000 * (frequency_bcd[1] & 0x0F) +
                    100000 * ((frequency_bcd[1] & 0xF0) >> 4) +
                    1000000 * (frequency_bcd[0] & 0x0F) +
                    10000000 * ((frequency_bcd[0] & 0xF0) >> 4));
                break;
              case 0x02:
                /* cable */
                freq = 100 *
                    ((frequency_bcd[3] & 0x0F) +
                    10 * ((frequency_bcd[3] & 0xF0) >> 4) +
                    100 * (frequency_bcd[2] & 0x0F) +
                    1000 * ((frequency_bcd[2] & 0xF0) >> 4) +
                    10000 * (frequency_bcd[1] & 0x0F) +
                    100000 * ((frequency_bcd[1] & 0xF0) >> 4) +
                    1000000 * (frequency_bcd[0] & 0x0F) +
                    10000000 * ((frequency_bcd[0] & 0xF0) >> 4));
                break;
              case 0x03:
                /* terrestrial */
                freq = GST_READ_UINT32_BE (current_pos) * 10;
                break;
            }
            g_value_init (&frequency, G_TYPE_UINT);
            g_value_set_uint (&frequency, freq);
            gst_value_list_append_value (&frequencies, &frequency);
            g_value_unset (&frequency);
            current_pos += 4;
          }

          switch (type) {
            case 0x01:
              fieldname = "frequency-list-satellite";
              break;
            case 0x02:
              fieldname = "frequency-list-cable";
              break;
            case 0x03:
              fieldname = "frequency-list-terrestrial";
              break;
          }

          gst_structure_take_value (transport, fieldname, &frequencies);
        }
      }

      descriptors = g_value_array_new (mpegdescriptor.n_desc);
      if (!mpegts_packetizer_parse_descriptors (packetizer,
              &data, data + descriptors_loop_length, descriptors)) {
        gst_structure_free (transport);
        g_value_array_free (descriptors);
        goto error;
      }

      gst_structure_id_set (transport, QUARK_DESCRIPTORS, G_TYPE_VALUE_ARRAY,
          descriptors, NULL);
      g_value_array_free (descriptors);
    }

    g_value_init (&transport_value, GST_TYPE_STRUCTURE);
    g_value_take_boxed (&transport_value, transport);
    gst_value_list_append_value (&transports, &transport_value);
    g_value_unset (&transport_value);

    transport_stream_loop_length -= data - entry_begin;
  }

  if (data != end - 4) {
    GST_WARNING ("PID %d invalid NIT parsed %d length %d",
        section->pid, (gint) (data - section->data), section->section_length);
    goto error;
  }

  gst_structure_id_take_value (nit, QUARK_TRANSPORTS, &transports);

  GST_DEBUG ("NIT %" GST_PTR_FORMAT, nit);

  return nit;

error:
  if (nit)
    gst_structure_free (nit);

  if (GST_VALUE_HOLDS_LIST (&transports))
    g_value_unset (&transports);

  return NULL;
}

GstStructure *
mpegts_packetizer_parse_sdt (MpegTSPacketizer2 * packetizer,
    MpegTSPacketizerSection * section)
{
  GstStructure *sdt = NULL, *service = NULL;
  guint8 *data, *end, *entry_begin;
  guint16 transport_stream_id, original_network_id, service_id;
  guint tmp;
  guint sdt_info_length;
  guint8 running_status;
  gboolean scrambled;
  guint descriptors_loop_length;
  GValue services = { 0 };
  GValueArray *descriptors = NULL;
  GValue service_value = { 0 };

  GST_DEBUG ("SDT");

  /* fixed header + CRC == 16 */
  if (section->section_length < 14) {
    GST_WARNING ("PID %d invalid SDT size %d",
        section->pid, section->section_length);
    goto error;
  }

  data = section->data;
  end = data + section->section_length;

  data += 3;

  transport_stream_id = GST_READ_UINT16_BE (data);
  data += 2;

  tmp = *data++;
  section->version_number = (tmp >> 1) & 0x1F;
  section->current_next_indicator = tmp & 0x01;

  /* skip section_number and last_section_number */
  data += 2;

  original_network_id = GST_READ_UINT16_BE (data);
  data += 2;

  /* skip reserved byte */
  data += 1;

  sdt = gst_structure_new_id (QUARK_SDT,
      QUARK_TRANSPORT_STREAM_ID, G_TYPE_UINT, transport_stream_id,
      QUARK_VERSION_NUMBER, G_TYPE_UINT, section->version_number,
      QUARK_CURRENT_NEXT_INDICATOR, G_TYPE_UINT,
      section->current_next_indicator, QUARK_ORIGINAL_NETWORK_ID, G_TYPE_UINT,
      original_network_id, QUARK_ACTUAL_TRANSPORT_STREAM, G_TYPE_BOOLEAN,
      section->table_id == 0x42, NULL);

  sdt_info_length = section->section_length - 11;
  g_value_init (&services, GST_TYPE_LIST);
  /* read up to the CRC */
  while (sdt_info_length - 4 > 0) {
    gchar *service_name;

    entry_begin = data;

    if (sdt_info_length < 9) {
      /* each entry must be at least 5 bytes (+4 bytes for the CRC) */
      GST_WARNING ("PID %d invalid SDT entry size %d",
          section->pid, sdt_info_length);
      goto error;
    }

    service_id = GST_READ_UINT16_BE (data);
    data += 2;

    /* EIT_schedule = ((*data & 0x02) == 2); */
    /* EIT_present_following = (*data & 0x01) == 1; */

    data += 1;
    tmp = GST_READ_UINT16_BE (data);

    running_status = (*data >> 5) & 0x07;
    scrambled = (*data >> 4) & 0x01;
    descriptors_loop_length = tmp & 0x0FFF;
    data += 2;

    /* TODO send tag event down relevant pad for channel name and provider */
    service_name = g_strdup_printf ("service-%d", service_id);
    service = gst_structure_new_empty (service_name);
    g_free (service_name);

    if (descriptors_loop_length) {
      guint8 *service_descriptor;
      GstMPEGDescriptor mpegdescriptor;

      if (data + descriptors_loop_length > end - 4) {
        GST_WARNING ("PID %d invalid SDT entry %d descriptors loop length %d",
            section->pid, service_id, descriptors_loop_length);
        gst_structure_free (service);
        goto error;
      }
      gst_mpeg_descriptor_parse (&mpegdescriptor, data,
          descriptors_loop_length);
      service_descriptor =
          gst_mpeg_descriptor_find (&mpegdescriptor, DESC_DVB_SERVICE);
      if (service_descriptor != NULL) {
        gchar *servicename_tmp, *serviceprovider_name_tmp;
        guint8 serviceprovider_name_length =
            DESC_DVB_SERVICE_provider_name_length (service_descriptor);
        gchar *serviceprovider_name =
            (gchar *) DESC_DVB_SERVICE_provider_name_text (service_descriptor);
        guint8 servicename_length =
            DESC_DVB_SERVICE_name_length (service_descriptor);
        gchar *servicename =
            (gchar *) DESC_DVB_SERVICE_name_text (service_descriptor);
        if (servicename_length + serviceprovider_name_length + 2 <=
            DESC_LENGTH (service_descriptor)) {
          const gchar *running_status_tmp;
          switch (running_status) {
            case 0:
              running_status_tmp = "undefined";
              break;
            case 1:
              running_status_tmp = "not running";
              break;
            case 2:
              running_status_tmp = "starts in a few seconds";
              break;
            case 3:
              running_status_tmp = "pausing";
              break;
            case 4:
              running_status_tmp = "running";
              break;
            default:
              running_status_tmp = "reserved";
          }
          servicename_tmp =
              get_encoding_and_convert (packetizer, servicename,
              servicename_length);
          serviceprovider_name_tmp =
              get_encoding_and_convert (packetizer, serviceprovider_name,
              serviceprovider_name_length);

          gst_structure_set (service,
              "name", G_TYPE_STRING, servicename_tmp,
              "provider-name", G_TYPE_STRING, serviceprovider_name_tmp,
              "scrambled", G_TYPE_BOOLEAN, scrambled,
              "running-status", G_TYPE_STRING, running_status_tmp, NULL);

          g_free (servicename_tmp);
          g_free (serviceprovider_name_tmp);
        }
      }

      descriptors = g_value_array_new (mpegdescriptor.n_desc);
      if (!mpegts_packetizer_parse_descriptors (packetizer,
              &data, data + descriptors_loop_length, descriptors)) {
        gst_structure_free (service);
        g_value_array_free (descriptors);
        goto error;
      }

      gst_structure_id_set (service, QUARK_DESCRIPTORS, G_TYPE_VALUE_ARRAY,
          descriptors, NULL);

      g_value_array_free (descriptors);
    }

    g_value_init (&service_value, GST_TYPE_STRUCTURE);
    g_value_take_boxed (&service_value, service);
    gst_value_list_append_value (&services, &service_value);
    g_value_unset (&service_value);

    sdt_info_length -= data - entry_begin;
  }

  if (data != end - 4) {
    GST_WARNING ("PID %d invalid SDT parsed %d length %d",
        section->pid, (gint) (data - section->data), section->section_length);
    goto error;
  }

  gst_structure_id_take_value (sdt, QUARK_SERVICES, &services);

  return sdt;

error:
  if (sdt)
    gst_structure_free (sdt);

  if (GST_VALUE_HOLDS_LIST (&services))
    g_value_unset (&services);

  return NULL;
}

/* FIXME : Can take up to 50% of total mpeg-ts demuxing cpu usage */
GstStructure *
mpegts_packetizer_parse_eit (MpegTSPacketizer2 * packetizer,
    MpegTSPacketizerSection * section)
{
  GstStructure *eit = NULL, *event = NULL;
  guint service_id, last_table_id, segment_last_section_number;
  guint transport_stream_id, original_network_id;
  gboolean free_ca_mode;
  guint event_id, running_status;
  guint16 mjd;
  guint year, month, day, hour, minute, second;
  guint duration;
  guint8 *data, *end, *duration_ptr, *utc_ptr;
  guint16 descriptors_loop_length;
  GValue events = { 0 };
  GValue event_value = { 0 };
  GValueArray *descriptors = NULL;
  gchar *event_name;
  guint tmp;

  /* fixed header + CRC == 16 */
  if (section->section_length < 18) {
    GST_WARNING ("PID %d invalid EIT size %d",
        section->pid, section->section_length);
    goto error;
  }

  data = section->data;
  end = data + section->section_length;

  data += 3;

  service_id = GST_READ_UINT16_BE (data);
  data += 2;

  tmp = *data++;
  section->version_number = (tmp >> 1) & 0x1F;
  section->current_next_indicator = tmp & 0x01;

  /* skip section_number and last_section_number */
  data += 2;

  transport_stream_id = GST_READ_UINT16_BE (data);
  data += 2;
  original_network_id = GST_READ_UINT16_BE (data);
  data += 2;
  segment_last_section_number = *data;
  data += 1;
  last_table_id = *data;
  data += 1;

  eit = gst_structure_new_id (QUARK_EIT,
      QUARK_VERSION_NUMBER, G_TYPE_UINT, section->version_number,
      QUARK_CURRENT_NEXT_INDICATOR, G_TYPE_UINT,
      section->current_next_indicator, QUARK_SERVICE_ID, G_TYPE_UINT,
      service_id, QUARK_ACTUAL_TRANSPORT_STREAM, G_TYPE_BOOLEAN,
      (section->table_id == 0x4E || (section->table_id >= 0x50
              && section->table_id <= 0x5F)), QUARK_PRESENT_FOLLOWING,
      G_TYPE_BOOLEAN, (section->table_id == 0x4E
          || section->table_id == 0x4F), QUARK_TRANSPORT_STREAM_ID, G_TYPE_UINT,
      transport_stream_id, QUARK_ORIGINAL_NETWORK_ID, G_TYPE_UINT,
      original_network_id, QUARK_SEGMENT_LAST_SECTION_NUMBER, G_TYPE_UINT,
      segment_last_section_number, QUARK_LAST_TABLE_ID, G_TYPE_UINT,
      last_table_id, NULL);

  g_value_init (&events, GST_TYPE_LIST);
  while (data < end - 4) {
    /* 12 is the minimum entry size + CRC */
    if (end - data < 12 + 4) {
      GST_WARNING ("PID %d invalid EIT entry length %d",
          section->pid, (gint) (end - 4 - data));
      gst_structure_free (eit);
      goto error;
    }

    event_id = GST_READ_UINT16_BE (data);
    data += 2;
    /* start_and_duration = GST_READ_UINT64_BE (data); */
    duration_ptr = data + 5;
    utc_ptr = data + 2;
    mjd = GST_READ_UINT16_BE (data);
    if (mjd == G_MAXUINT16) {
      year = 1900;
      month = day = hour = minute = second = 0;
    } else {
      /* See EN 300 468 Annex C */
      year = (guint32) (((mjd - 15078.2) / 365.25));
      month = (guint8) ((mjd - 14956.1 - (guint) (year * 365.25)) / 30.6001);
      day = mjd - 14956 - (guint) (year * 365.25) - (guint) (month * 30.6001);
      if (month == 14 || month == 15) {
        year++;
        month = month - 1 - 12;
      } else {
        month--;
      }
      year += 1900;
      hour = ((utc_ptr[0] & 0xF0) >> 4) * 10 + (utc_ptr[0] & 0x0F);
      minute = ((utc_ptr[1] & 0xF0) >> 4) * 10 + (utc_ptr[1] & 0x0F);
      second = ((utc_ptr[2] & 0xF0) >> 4) * 10 + (utc_ptr[2] & 0x0F);
    }

    duration = (((duration_ptr[0] & 0xF0) >> 4) * 10 +
        (duration_ptr[0] & 0x0F)) * 60 * 60 +
        (((duration_ptr[1] & 0xF0) >> 4) * 10 +
        (duration_ptr[1] & 0x0F)) * 60 +
        ((duration_ptr[2] & 0xF0) >> 4) * 10 + (duration_ptr[2] & 0x0F);

    data += 8;
    running_status = *data >> 5;
    free_ca_mode = (*data >> 4) & 0x01;
    descriptors_loop_length = GST_READ_UINT16_BE (data) & 0x0FFF;
    data += 2;

    /* TODO: send tag event down relevant pad saying what is currently playing */
    event_name = g_strdup_printf ("event-%d", event_id);
    event = gst_structure_new_empty (event_name);
    g_free (event_name);
    gst_structure_id_set (event,
        QUARK_EVENT_ID, G_TYPE_UINT, event_id,
        QUARK_YEAR, G_TYPE_UINT, year,
        QUARK_MONTH, G_TYPE_UINT, month,
        QUARK_DAY, G_TYPE_UINT, day,
        QUARK_HOUR, G_TYPE_UINT, hour,
        QUARK_MINUTE, G_TYPE_UINT, minute,
        QUARK_SECOND, G_TYPE_UINT, second,
        QUARK_DURATION, G_TYPE_UINT, duration,
        QUARK_RUNNING_STATUS, G_TYPE_UINT, running_status,
        QUARK_FREE_CA_MODE, G_TYPE_BOOLEAN, free_ca_mode, NULL);

    if (descriptors_loop_length) {
      guint8 *event_descriptor;
      GArray *component_descriptors;
      GArray *extended_event_descriptors;
      GstMPEGDescriptor mpegdescriptor;

      if (data + descriptors_loop_length > end - 4) {
        GST_WARNING ("PID %d invalid EIT descriptors loop length %d",
            section->pid, descriptors_loop_length);
        gst_structure_free (event);
        goto error;
      }
      gst_mpeg_descriptor_parse (&mpegdescriptor, data,
          descriptors_loop_length);
      event_descriptor =
          gst_mpeg_descriptor_find (&mpegdescriptor, DESC_DVB_SHORT_EVENT);
      if (event_descriptor != NULL) {
        gchar *eventname_tmp, *eventdescription_tmp;
        guint8 eventname_length =
            DESC_DVB_SHORT_EVENT_name_length (event_descriptor);
        gchar *eventname =
            (gchar *) DESC_DVB_SHORT_EVENT_name_text (event_descriptor);
        guint8 eventdescription_length =
            DESC_DVB_SHORT_EVENT_description_length (event_descriptor);
        gchar *eventdescription =
            (gchar *) DESC_DVB_SHORT_EVENT_description_text (event_descriptor);
        if (eventname_length + eventdescription_length + 2 <=
            DESC_LENGTH (event_descriptor)) {

          eventname_tmp =
              get_encoding_and_convert (packetizer, eventname,
              eventname_length);
          eventdescription_tmp =
              get_encoding_and_convert (packetizer, eventdescription,
              eventdescription_length);

          gst_structure_id_set (event, QUARK_NAME, G_TYPE_STRING, eventname_tmp,
              QUARK_DESCRIPTION, G_TYPE_STRING, eventdescription_tmp, NULL);
          g_free (eventname_tmp);
          g_free (eventdescription_tmp);
        }
      }

      extended_event_descriptors =
          gst_mpeg_descriptor_find_all (&mpegdescriptor,
          DESC_DVB_EXTENDED_EVENT);
      if (extended_event_descriptors) {
        int i;
        guint8 *extended_descriptor;
        GValue extended_items = { 0 };
        GValue extended_item_value = { 0 };
        GstStructure *extended_item;
        gchar *extended_text = NULL;
        g_value_init (&extended_items, GST_TYPE_LIST);
        for (i = 0; i < extended_event_descriptors->len; i++) {
          extended_descriptor = g_array_index (extended_event_descriptors,
              guint8 *, i);
          if (DESC_DVB_EXTENDED_EVENT_descriptor_number (extended_descriptor) ==
              i) {
            guint8 *items_aux =
                DESC_DVB_EXTENDED_EVENT_items (extended_descriptor);
            guint8 *items_limit =
                items_aux +
                DESC_DVB_EXTENDED_EVENT_items_length (extended_descriptor);
            while (items_aux < items_limit) {
              guint8 length_aux;
              gchar *description, *text;

              /* Item Description text */
              length_aux = *items_aux;
              ++items_aux;
              description =
                  get_encoding_and_convert (packetizer, (gchar *) items_aux,
                  length_aux);
              items_aux += length_aux;

              /* Item text */
              length_aux = *items_aux;
              ++items_aux;
              text =
                  get_encoding_and_convert (packetizer, (gchar *) items_aux,
                  length_aux);
              items_aux += length_aux;

              extended_item = gst_structure_new_id (QUARK_EXTENDED_ITEM,
                  QUARK_DESCRIPTION, G_TYPE_STRING, description,
                  QUARK_TEXT, G_TYPE_STRING, text, NULL);

              g_value_init (&extended_item_value, GST_TYPE_STRUCTURE);
              g_value_take_boxed (&extended_item_value, extended_item);
              gst_value_list_append_value (&extended_items,
                  &extended_item_value);
              g_value_unset (&extended_item_value);
            }

            if (extended_text) {
              gchar *tmp;
              gchar *old_extended_text = extended_text;
              tmp = get_encoding_and_convert (packetizer, (gchar *)
                  DESC_DVB_EXTENDED_EVENT_text (extended_descriptor),
                  DESC_DVB_EXTENDED_EVENT_text_length (extended_descriptor));
              extended_text = g_strdup_printf ("%s%s", extended_text, tmp);
              g_free (old_extended_text);
              g_free (tmp);
            } else {
              extended_text = get_encoding_and_convert (packetizer, (gchar *)
                  DESC_DVB_EXTENDED_EVENT_text (extended_descriptor),
                  DESC_DVB_EXTENDED_EVENT_text_length (extended_descriptor));
            }
          }
        }
        if (extended_text) {
          gst_structure_id_set (event, QUARK_EXTENDED_TEXT, G_TYPE_STRING,
              extended_text, NULL);
          g_free (extended_text);
        }
        gst_structure_id_take_value (event, QUARK_EXTENDED_ITEMS,
            &extended_items);
        g_array_free (extended_event_descriptors, TRUE);
      }

      component_descriptors = gst_mpeg_descriptor_find_all (&mpegdescriptor,
          DESC_DVB_COMPONENT);
      if (component_descriptors) {
        int i;
        guint8 *comp_descriptor;
        GValue components = { 0 };
        g_value_init (&components, GST_TYPE_LIST);
        /* FIXME: do the component descriptor parsing less verbosely
         * and better...a task for 0.10.6 */
        for (i = 0; i < component_descriptors->len; i++) {
          GstStructure *component = NULL;
          GValue component_value = { 0 };
          gint widescreen = 0;  /* 0 for 4:3, 1 for 16:9, 2 for > 16:9 */
          gint freq = 25;       /* 25 or 30 measured in Hertz */
          /* gboolean highdef = FALSE; */
          gboolean panvectors = FALSE;
          const gchar *comptype = "";

          comp_descriptor = g_array_index (component_descriptors, guint8 *, i);
          switch (DESC_DVB_COMPONENT_stream_content (comp_descriptor)) {
            case 0x01:
              /* video */
              switch (DESC_DVB_COMPONENT_type (comp_descriptor)) {
                case 0x01:
                  widescreen = 0;
                  freq = 25;
                  break;
                case 0x02:
                  widescreen = 1;
                  panvectors = TRUE;
                  freq = 25;
                  break;
                case 0x03:
                  widescreen = 1;
                  panvectors = FALSE;
                  freq = 25;
                  break;
                case 0x04:
                  widescreen = 2;
                  freq = 25;
                  break;
                case 0x05:
                  widescreen = 0;
                  freq = 30;
                  break;
                case 0x06:
                  widescreen = 1;
                  panvectors = TRUE;
                  freq = 30;
                  break;
                case 0x07:
                  widescreen = 1;
                  panvectors = FALSE;
                  freq = 30;
                  break;
                case 0x08:
                  widescreen = 2;
                  freq = 30;
                  break;
                case 0x09:
                  widescreen = 0;
                  /* highdef = TRUE; */
                  freq = 25;
                  break;
                case 0x0A:
                  widescreen = 1;
                  /* highdef = TRUE; */
                  panvectors = TRUE;
                  freq = 25;
                  break;
                case 0x0B:
                  widescreen = 1;
                  /* highdef = TRUE; */
                  panvectors = FALSE;
                  freq = 25;
                  break;
                case 0x0C:
                  widescreen = 2;
                  /* highdef = TRUE; */
                  freq = 25;
                  break;
                case 0x0D:
                  widescreen = 0;
                  /* highdef = TRUE; */
                  freq = 30;
                  break;
                case 0x0E:
                  widescreen = 1;
                  /* highdef = TRUE; */
                  panvectors = TRUE;
                  freq = 30;
                  break;
                case 0x0F:
                  widescreen = 1;
                  /* highdef = TRUE; */
                  panvectors = FALSE;
                  freq = 30;
                  break;
                case 0x10:
                  widescreen = 2;
                  /* highdef = TRUE; */
                  freq = 30;
                  break;
              }
              component = gst_structure_new ("video", "high-definition",
                  G_TYPE_BOOLEAN, TRUE, "frequency", G_TYPE_INT, freq,
                  "tag", G_TYPE_INT, DESC_DVB_COMPONENT_tag (comp_descriptor),
                  NULL);
              if (widescreen == 0) {
                gst_structure_set (component, "aspect-ratio",
                    G_TYPE_STRING, "4:3", NULL);
              } else if (widescreen == 2) {
                gst_structure_set (component, "aspect-ratio", G_TYPE_STRING,
                    "> 16:9", NULL);
              } else {
                gst_structure_set (component, "aspect-ratio", G_TYPE_STRING,
                    "16:9", "pan-vectors", G_TYPE_BOOLEAN, panvectors, NULL);
              }
              break;
            case 0x02:         /* audio */
              comptype = "undefined";
              switch (DESC_DVB_COMPONENT_type (comp_descriptor)) {
                case 0x01:
                  comptype = "single channel mono";
                  break;
                case 0x02:
                  comptype = "dual channel mono";
                  break;
                case 0x03:
                  comptype = "stereo";
                  break;
                case 0x04:
                  comptype = "multi-channel multi-lingual";
                  break;
                case 0x05:
                  comptype = "surround";
                  break;
                case 0x40:
                  comptype = "audio description for the visually impaired";
                  break;
                case 0x41:
                  comptype = "audio for the hard of hearing";
                  break;
              }
              component = gst_structure_new ("audio", "type", G_TYPE_STRING,
                  comptype, "tag", G_TYPE_INT,
                  DESC_DVB_COMPONENT_tag (comp_descriptor), NULL);
              break;
            case 0x03:         /* subtitles/teletext/vbi */
              comptype = "reserved";
              switch (DESC_DVB_COMPONENT_type (comp_descriptor)) {
                case 0x01:
                  comptype = "EBU Teletext subtitles";
                  break;
                case 0x02:
                  comptype = "associated EBU Teletext";
                  break;
                case 0x03:
                  comptype = "VBI data";
                  break;
                case 0x10:
                  comptype = "Normal DVB subtitles";
                  break;
                case 0x11:
                  comptype = "Normal DVB subtitles for 4:3";
                  break;
                case 0x12:
                  comptype = "Normal DVB subtitles for 16:9";
                  break;
                case 0x13:
                  comptype = "Normal DVB subtitles for 2.21:1";
                  break;
                case 0x20:
                  comptype = "Hard of hearing DVB subtitles";
                  break;
                case 0x21:
                  comptype = "Hard of hearing DVB subtitles for 4:3";
                  break;
                case 0x22:
                  comptype = "Hard of hearing DVB subtitles for 16:9";
                  break;
                case 0x23:
                  comptype = "Hard of hearing DVB subtitles for 2.21:1";
                  break;
              }
              component = gst_structure_new ("teletext", "type", G_TYPE_STRING,
                  comptype, "tag", G_TYPE_INT,
                  DESC_DVB_COMPONENT_tag (comp_descriptor), NULL);
              break;
          }
          if (component) {
            g_value_init (&component_value, GST_TYPE_STRUCTURE);
            g_value_take_boxed (&component_value, component);
            gst_value_list_append_value (&components, &component_value);
            g_value_unset (&component_value);
            component = NULL;
          }
        }
        gst_structure_take_value (event, "components", &components);
        g_array_free (component_descriptors, TRUE);
      }

      descriptors = g_value_array_new (mpegdescriptor.n_desc);
      if (!mpegts_packetizer_parse_descriptors (packetizer,
              &data, data + descriptors_loop_length, descriptors)) {
        gst_structure_free (event);
        g_value_array_free (descriptors);
        goto error;
      }
      gst_structure_id_set (event, QUARK_DESCRIPTORS, G_TYPE_VALUE_ARRAY,
          descriptors, NULL);
      g_value_array_free (descriptors);
    }

    g_value_init (&event_value, GST_TYPE_STRUCTURE);
    g_value_take_boxed (&event_value, event);
    gst_value_list_append_value (&events, &event_value);
    g_value_unset (&event_value);
  }

  if (data != end - 4) {
    GST_WARNING ("PID %d invalid EIT parsed %d length %d",
        section->pid, (gint) (data - section->data), section->section_length);
    goto error;
  }

  gst_structure_id_take_value (eit, QUARK_EVENTS, &events);

  GST_DEBUG ("EIT %" GST_PTR_FORMAT, eit);

  return eit;

error:
  if (eit)
    gst_structure_free (eit);

  if (GST_VALUE_HOLDS_LIST (&events))
    g_value_unset (&events);

  return NULL;
}

static GstStructure *
parse_tdt_tot_common (MpegTSPacketizer2 * packetizer,
    MpegTSPacketizerSection * section, const gchar * name)
{
  GstStructure *res;
  guint16 mjd;
  guint year, month, day, hour, minute, second;
  guint8 *data, *utc_ptr;

  /* length at least 8 */
  if (section->section_length < 8) {
    GST_WARNING ("PID %d invalid TDT/TOT size %d",
        section->pid, section->section_length);
    return NULL;
  }

  data = section->data;
  data += 3;

  mjd = GST_READ_UINT16_BE (data);
  data += 2;
  utc_ptr = data;
  if (mjd == G_MAXUINT16) {
    year = 1900;
    month = day = hour = minute = second = 0;
  } else {
    /* See EN 300 468 Annex C */
    year = (guint32) (((mjd - 15078.2) / 365.25));
    month = (guint8) ((mjd - 14956.1 - (guint) (year * 365.25)) / 30.6001);
    day = mjd - 14956 - (guint) (year * 365.25) - (guint) (month * 30.6001);
    if (month == 14 || month == 15) {
      year++;
      month = month - 1 - 12;
    } else {
      month--;
    }
    year += 1900;
    hour = ((utc_ptr[0] & 0xF0) >> 4) * 10 + (utc_ptr[0] & 0x0F);
    minute = ((utc_ptr[1] & 0xF0) >> 4) * 10 + (utc_ptr[1] & 0x0F);
    second = ((utc_ptr[2] & 0xF0) >> 4) * 10 + (utc_ptr[2] & 0x0F);
  }
  res = gst_structure_new (name,
      "year", G_TYPE_UINT, year,
      "month", G_TYPE_UINT, month,
      "day", G_TYPE_UINT, day,
      "hour", G_TYPE_UINT, hour,
      "minute", G_TYPE_UINT, minute, "second", G_TYPE_UINT, second, NULL);

  return res;
}

GstStructure *
mpegts_packetizer_parse_tdt (MpegTSPacketizer2 * packetizer,
    MpegTSPacketizerSection * section)
{
  GstStructure *tdt = NULL;
  GST_DEBUG ("TDT");

  tdt = parse_tdt_tot_common (packetizer, section, "tdt");

  return tdt;
}

GstStructure *
mpegts_packetizer_parse_tot (MpegTSPacketizer2 * packetizer,
    MpegTSPacketizerSection * section)
{
  guint8 *data;
  GstStructure *tot = NULL;
  GValueArray *descriptors;
  guint16 desc_len;

  GST_DEBUG ("TOT");

  tot = parse_tdt_tot_common (packetizer, section, "tot");
  data = section->data + 8;

  desc_len = ((*data++) & 0xf) << 8;
  desc_len |= *data++;
  descriptors = g_value_array_new (0);

  if (!mpegts_packetizer_parse_descriptors (packetizer, &data, data + desc_len,
          descriptors)) {
    g_value_array_free (descriptors);
    gst_structure_free (tot);
    return NULL;
  }
  gst_structure_id_set (tot, QUARK_DESCRIPTORS, G_TYPE_VALUE_ARRAY, descriptors,
      NULL);
  g_value_array_free (descriptors);

  return tot;
}

void
mpegts_packetizer_clear (MpegTSPacketizer2 * packetizer)
{
  if (packetizer->know_packet_size) {
    packetizer->know_packet_size = FALSE;
    packetizer->packet_size = 0;
    if (packetizer->caps != NULL) {
      gst_caps_unref (packetizer->caps);
      packetizer->caps = NULL;
    }
  }
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
mpegts_packetizer_flush (MpegTSPacketizer2 * packetizer)
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
  flush_observations (packetizer);
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

  GST_DEBUG ("Pushing %" G_GSIZE_FORMAT " byte from offset %" G_GUINT64_FORMAT,
      gst_buffer_get_size (buffer), GST_BUFFER_OFFSET (buffer));
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
            packetizer->know_packet_size = TRUE;
            packetizer->packet_size = packetsize;
            packetizer->caps = gst_caps_new_simple ("video/mpegts",
                "systemstream", G_TYPE_BOOLEAN, TRUE,
                "packetsize", G_TYPE_INT, packetsize, NULL);
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

    if (packetizer->know_packet_size)
      break;

    /* Skip MPEGTS_MAX_PACKETSIZE */
    gst_adapter_flush (packetizer->adapter, MPEGTS_MAX_PACKETSIZE);
    packetizer->priv->available -= MPEGTS_MAX_PACKETSIZE;
    packetizer->offset += MPEGTS_MAX_PACKETSIZE;
  }

  g_free (dest);

  if (packetizer->know_packet_size) {
    GST_DEBUG ("have packetsize detected: %d of %u bytes",
        packetizer->know_packet_size, packetizer->packet_size);
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

  return packetizer->know_packet_size;
}

gboolean
mpegts_packetizer_has_packets (MpegTSPacketizer2 * packetizer)
{
  if (G_UNLIKELY (packetizer->know_packet_size == FALSE)) {
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
  guint avail;
  int i;

  if (G_UNLIKELY (!packetizer->know_packet_size)) {
    if (!mpegts_try_discover_packet_size (packetizer))
      return PACKET_NEED_MORE;
  }

  while ((avail = priv->available) >= packetizer->packet_size) {
    if (priv->mapped == NULL) {
      priv->mapped_size =
          priv->available - (priv->available % packetizer->packet_size);
      priv->mapped =
          (guint8 *) gst_adapter_map (packetizer->adapter, priv->mapped_size);
      priv->offset = 0;
    }
    packet->data_start = priv->mapped + priv->offset;

    /* M2TS packets don't start with the sync byte, all other variants do */
    if (packetizer->packet_size == MPEGTS_M2TS_PACKETSIZE)
      packet->data_start += 4;

    /* ALL mpeg-ts variants contain 188 bytes of data. Those with bigger packet
     * sizes contain either extra data (timesync, FEC, ..) either before or after
     * the data */
    packet->data_end = packet->data_start + 188;
    packet->offset = packetizer->offset;
    GST_LOG ("offset %" G_GUINT64_FORMAT, packet->offset);
    packetizer->offset += packetizer->packet_size;
    GST_MEMDUMP ("data_start", packet->data_start, 16);
    packet->origts = priv->last_in_time;

    /* Check sync byte */
    if (G_LIKELY (packet->data_start[0] == 0x47))
      goto got_valid_packet;

    GST_LOG ("Lost sync %d", packetizer->packet_size);

    /* Find the 0x47 in the buffer */
    for (i = 0; i < packetizer->packet_size; i++)
      if (packet->data_start[i] == 0x47)
        break;

    if (packetizer->packet_size == MPEGTS_M2TS_PACKETSIZE) {
      if (i >= 4)
        i -= 4;
      else
        i += 188;
    }

    GST_DEBUG ("Flushing %d bytes out", i);
    /* gst_adapter_flush (packetizer->adapter, i); */
    /* Pop out the remaining data... */
    priv->offset += i;
    priv->available -= i;
    if (G_UNLIKELY (priv->available < packetizer->packet_size)) {
      GST_DEBUG ("Flushing %d bytes out", priv->offset);
      gst_adapter_flush (packetizer->adapter, priv->offset);
      priv->mapped = NULL;
    }
    continue;
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
  MpegTSPacketizerPrivate *priv = packetizer->priv;

  priv->offset += packetizer->packet_size;
  priv->available -= packetizer->packet_size;

  if (G_UNLIKELY (priv->mapped && priv->available < packetizer->packet_size)) {
    gst_adapter_flush (packetizer->adapter, priv->offset);
    priv->mapped = NULL;
  }
}

gboolean
mpegts_packetizer_push_section (MpegTSPacketizer2 * packetizer,
    MpegTSPacketizerPacket * packet, MpegTSPacketizerSection * section)
{
  gboolean res = FALSE;
  MpegTSPacketizerStream *stream;
  guint8 pointer, table_id;
  guint16 subtable_extension;
  guint section_length;
  guint8 *data, *data_start;

  data = packet->data;
  section->pid = packet->pid;

  if (packet->payload_unit_start_indicator == 1) {
    pointer = *data++;
    if (data + pointer > packet->data_end) {
      GST_WARNING ("PID 0x%04x PSI section pointer points past the end "
          "of the buffer", packet->pid);
      goto out;
    }

    data += pointer;
  }

  GST_MEMDUMP ("section data", packet->data, packet->data_end - packet->data);

  /* TDT and TOT sections (see ETSI EN 300 468 5.2.5)
   *  these sections do not extend to several packets so we don't need to use the
   *  sections filter. */
  if (packet->pid == 0x14) {
    section->offset = packet->offset;
    table_id = data[0];
    section->section_length = (GST_READ_UINT24_BE (data) & 0x000FFF) + 3;

    if (data + section->section_length > packet->data_end) {
      GST_WARNING ("PID 0x%04x PSI section length extends past the end "
          "of the buffer", packet->pid);
      goto out;
    }
    section->data = data;
    section->table_id = table_id;
    section->complete = TRUE;
    res = TRUE;
    GST_DEBUG ("TDT section pid:0x%04x table_id:0x%02x section_length: %d",
        packet->pid, table_id, section->section_length);
    goto out;
  }

  data_start = data;

  stream = packetizer->streams[packet->pid];
  if (stream == NULL) {
    stream = mpegts_packetizer_stream_new ();
    packetizer->streams[packet->pid] = stream;
  }

  if (packet->payload_unit_start_indicator) {
    table_id = *data++;
    /* subtable_extension should be read from 4th and 5th bytes only if
     * section_syntax_indicator is 1 */
    if ((data[0] & 0x80) == 0)
      subtable_extension = 0;
    else
      subtable_extension = GST_READ_UINT16_BE (data + 2);
    GST_DEBUG ("pid: 0x%04x table_id 0x%02x sub_table_extension %d",
        packet->pid, table_id, subtable_extension);

    section_length = (GST_READ_UINT16_BE (data) & 0x0FFF) + 3;

    if (stream->continuity_counter != CONTINUITY_UNSET) {
      GST_DEBUG
          ("PID 0x%04x table_id 0x%02x sub_table_extension %d payload_unit_start_indicator set but section "
          "not complete (last_continuity: %d continuity: %d sec len %d",
          packet->pid, table_id, subtable_extension, stream->continuity_counter,
          packet->continuity_counter, section_length);
      mpegts_packetizer_clear_section (stream);
    } else {
      GST_DEBUG
          ("pusi set and new stream section is %d long and data we have is: %d",
          section_length, (gint) (packet->data_end - packet->data));
    }
    stream->continuity_counter = packet->continuity_counter;
    stream->section_length = section_length;

    /* Create enough room to store chunks of sections, including FF padding */
    if (stream->section_allocated == 0) {
      stream->section_data = g_malloc (section_length + 188);
      stream->section_allocated = section_length + 188;
    } else if (G_UNLIKELY (stream->section_allocated < section_length + 188)) {
      stream->section_data =
          g_realloc (stream->section_data, section_length + 188);
      stream->section_allocated = section_length + 188;
    }
    memcpy (stream->section_data, data_start, packet->data_end - data_start);
    stream->section_offset = packet->data_end - data_start;

    stream->section_table_id = table_id;
    stream->offset = packet->offset;

    res = TRUE;
  } else if (stream->continuity_counter != CONTINUITY_UNSET &&
      (packet->continuity_counter == stream->continuity_counter + 1 ||
          (stream->continuity_counter == MAX_CONTINUITY &&
              packet->continuity_counter == 0))) {
    stream->continuity_counter = packet->continuity_counter;

    memcpy (stream->section_data + stream->section_offset, data_start,
        packet->data_end - data_start);
    stream->section_offset += packet->data_end - data_start;
    GST_DEBUG ("Appending data (need %d, have %d)", stream->section_length,
        stream->section_offset);

    res = TRUE;
  } else {
    if (stream->continuity_counter == CONTINUITY_UNSET)
      GST_DEBUG ("PID 0x%04x waiting for pusi", packet->pid);
    else
      GST_DEBUG ("PID 0x%04x section discontinuity "
          "(last_continuity: %d continuity: %d", packet->pid,
          stream->continuity_counter, packet->continuity_counter);
    mpegts_packetizer_clear_section (stream);
  }

  if (res) {
    /* we pushed some data in the section adapter, see if the section is
     * complete now */

    /* >= as sections can be padded and padding is not included in
     * section_length */
    if (stream->section_offset >= stream->section_length) {
      res = mpegts_packetizer_parse_section_header (packetizer,
          stream, section);

      /* flush stuffing bytes */
      mpegts_packetizer_clear_section (stream);
    } else {
      GST_DEBUG ("section not complete");
      /* section not complete yet */
      section->complete = FALSE;
    }
  } else {
    GST_WARNING ("section not complete");
    section->complete = FALSE;
  }

out:
  packet->data = data;

  GST_DEBUG ("result: %d complete: %d", res, section->complete);

  return res;
}

static void
_init_local (void)
{
  GST_DEBUG_CATEGORY_INIT (mpegts_packetizer_debug, "mpegtspacketizer", 0,
      "MPEG transport stream parser");

  QUARK_PAT = g_quark_from_string ("pat");
  QUARK_TRANSPORT_STREAM_ID = g_quark_from_string ("transport-stream-id");
  QUARK_PROGRAM_NUMBER = g_quark_from_string ("program-number");
  QUARK_PID = g_quark_from_string ("pid");
  QUARK_PROGRAMS = g_quark_from_string ("programs");

  QUARK_CAT = g_quark_from_string ("cat");

  QUARK_PMT = g_quark_from_string ("pmt");
  QUARK_PCR_PID = g_quark_from_string ("pcr-pid");
  QUARK_VERSION_NUMBER = g_quark_from_string ("version-number");
  QUARK_DESCRIPTORS = g_quark_from_string ("descriptors");
  QUARK_STREAM_TYPE = g_quark_from_string ("stream-type");
  QUARK_STREAMS = g_quark_from_string ("streams");

  QUARK_NIT = g_quark_from_string ("nit");
  QUARK_NETWORK_ID = g_quark_from_string ("network-id");
  QUARK_CURRENT_NEXT_INDICATOR = g_quark_from_string ("current-next-indicator");
  QUARK_ACTUAL_NETWORK = g_quark_from_string ("actual-network");
  QUARK_NETWORK_NAME = g_quark_from_string ("network-name");
  QUARK_ORIGINAL_NETWORK_ID = g_quark_from_string ("original-network-id");
  QUARK_TRANSPORTS = g_quark_from_string ("transports");
  QUARK_TERRESTRIAL = g_quark_from_string ("terrestrial");
  QUARK_CABLE = g_quark_from_string ("cable");
  QUARK_FREQUENCY = g_quark_from_string ("frequency");
  QUARK_MODULATION = g_quark_from_string ("modulation");
  QUARK_BANDWIDTH = g_quark_from_string ("bandwidth");
  QUARK_CONSTELLATION = g_quark_from_string ("constellation");
  QUARK_HIERARCHY = g_quark_from_string ("hierarchy");
  QUARK_CODE_RATE_HP = g_quark_from_string ("code-rate-hp");
  QUARK_CODE_RATE_LP = g_quark_from_string ("code-rate-lp");
  QUARK_GUARD_INTERVAL = g_quark_from_string ("guard-interval");
  QUARK_TRANSMISSION_MODE = g_quark_from_string ("transmission-mode");
  QUARK_OTHER_FREQUENCY = g_quark_from_string ("other-frequency");
  QUARK_SYMBOL_RATE = g_quark_from_string ("symbol-rate");
  QUARK_INNER_FEC = g_quark_from_string ("inner-fec");
  QUARK_DELIVERY = g_quark_from_string ("delivery");
  QUARK_CHANNELS = g_quark_from_string ("channels");
  QUARK_LOGICAL_CHANNEL_NUMBER = g_quark_from_string ("logical-channel-number");

  QUARK_SDT = g_quark_from_string ("sdt");
  QUARK_ACTUAL_TRANSPORT_STREAM =
      g_quark_from_string ("actual-transport-stream");
  QUARK_SERVICES = g_quark_from_string ("services");

  QUARK_EIT = g_quark_from_string ("eit");
  QUARK_SERVICE_ID = g_quark_from_string ("service-id");
  QUARK_PRESENT_FOLLOWING = g_quark_from_string ("present-following");
  QUARK_SEGMENT_LAST_SECTION_NUMBER =
      g_quark_from_string ("segment-last-section-number");
  QUARK_LAST_TABLE_ID = g_quark_from_string ("last-table-id");
  QUARK_EVENTS = g_quark_from_string ("events");
  QUARK_NAME = g_quark_from_string ("name");
  QUARK_DESCRIPTION = g_quark_from_string ("description");
  QUARK_EXTENDED_ITEM = g_quark_from_string ("extended_item");
  QUARK_EXTENDED_ITEMS = g_quark_from_string ("extended-items");
  QUARK_TEXT = g_quark_from_string ("text");
  QUARK_EXTENDED_TEXT = g_quark_from_string ("extended-text");
  QUARK_EVENT_ID = g_quark_from_string ("event-id");
  QUARK_YEAR = g_quark_from_string ("year");
  QUARK_MONTH = g_quark_from_string ("month");
  QUARK_DAY = g_quark_from_string ("day");
  QUARK_HOUR = g_quark_from_string ("hour");
  QUARK_MINUTE = g_quark_from_string ("minute");
  QUARK_SECOND = g_quark_from_string ("second");
  QUARK_DURATION = g_quark_from_string ("duration");
  QUARK_RUNNING_STATUS = g_quark_from_string ("running-status");
  QUARK_FREE_CA_MODE = g_quark_from_string ("free-ca-mode");
}

/**
 * @text: The text you want to get the encoding from
 * @start_text: Location where the beginning of the actual text is stored
 * @is_multibyte: Location where information whether it's a multibyte encoding
 * or not is stored
 * @returns: GIconv for conversion or NULL
 */
static LocalIconvCode
get_encoding (MpegTSPacketizer2 * packetizer, const gchar * text,
    guint * start_text, gboolean * is_multibyte)
{
  LocalIconvCode encoding;
  guint8 firstbyte;

  *is_multibyte = FALSE;
  *start_text = 0;

  firstbyte = (guint8) text[0];

  /* A wrong value */
  g_return_val_if_fail (firstbyte != 0x00, _ICONV_UNKNOWN);

  if (firstbyte <= 0x0B) {
    /* 0x01 => iso 8859-5 */
    encoding = firstbyte + _ICONV_ISO8859_4;
    *start_text = 1;
    goto beach;
  }

  /* ETSI EN 300 468, "Selection of character table" */
  switch (firstbyte) {
    case 0x0C:
    case 0x0D:
    case 0x0E:
    case 0x0F:
      /* RESERVED */
      encoding = _ICONV_UNKNOWN;
      break;
    case 0x10:
    {
      guint16 table;

      table = GST_READ_UINT16_BE (text + 1);

      if (table < 17)
        encoding = _ICONV_UNKNOWN + table;
      else
        encoding = _ICONV_UNKNOWN;;
      *start_text = 3;
      break;
    }
    case 0x11:
      encoding = _ICONV_ISO10646_UC2;
      *start_text = 1;
      *is_multibyte = TRUE;
      break;
    case 0x12:
      /*  EUC-KR implements KSX1001 */
      encoding = _ICONV_EUC_KR;
      *start_text = 1;
      *is_multibyte = TRUE;
      break;
    case 0x13:
      encoding = _ICONV_GB2312;
      *start_text = 1;
      break;
    case 0x14:
      encoding = _ICONV_UTF_16BE;
      *start_text = 1;
      *is_multibyte = TRUE;
      break;
    case 0x15:
      /* TODO : Where does this come from ?? */
      encoding = _ICONV_ISO10646_UTF8;
      *start_text = 1;
      break;
    case 0x16:
    case 0x17:
    case 0x18:
    case 0x19:
    case 0x1A:
    case 0x1B:
    case 0x1C:
    case 0x1D:
    case 0x1E:
    case 0x1F:
      /* RESERVED */
      encoding = _ICONV_UNKNOWN;
      break;
    default:
      encoding = _ICONV_ISO6937;
      break;
  }

beach:
  GST_DEBUG
      ("Found encoding %d, first byte is 0x%02x, start_text: %u, is_multibyte: %d",
      encoding, firstbyte, *start_text, *is_multibyte);

  return encoding;
}

/**
 * @text: The text to convert. It may include pango markup (<b> and </b>)
 * @length: The length of the string -1 if it's nul-terminated
 * @start: Where to start converting in the text
 * @encoding: The encoding of text
 * @is_multibyte: Whether the encoding is a multibyte encoding
 * @error: The location to store the error, or NULL to ignore errors
 * @returns: UTF-8 encoded string
 *
 * Convert text to UTF-8.
 */
static gchar *
convert_to_utf8 (const gchar * text, gint length, guint start,
    GIConv iconv, gboolean is_multibyte, GError ** error)
{
  gchar *new_text;
  gchar *tmp, *pos;
  gint i;

  text += start;

  pos = tmp = g_malloc (length * 2);

  if (is_multibyte) {
    if (length == -1) {
      while (*text != '\0') {
        guint16 code = GST_READ_UINT16_BE (text);

        switch (code) {
          case 0xE086:         /* emphasis on */
          case 0xE087:         /* emphasis off */
            /* skip it */
            break;
          case 0xE08A:{
            pos[0] = 0x00;      /* 0x00 0x0A is a new line */
            pos[1] = 0x0A;
            pos += 2;
            break;
          }
          default:
            pos[0] = text[0];
            pos[1] = text[1];
            pos += 2;
            break;
        }

        text += 2;
      }
    } else {
      for (i = 0; i < length; i += 2) {
        guint16 code = GST_READ_UINT16_BE (text);

        switch (code) {
          case 0xE086:         /* emphasis on */
          case 0xE087:         /* emphasis off */
            /* skip it */
            break;
          case 0xE08A:{
            pos[0] = 0x00;      /* 0x00 0x0A is a new line */
            pos[1] = 0x0A;
            pos += 2;
            break;
          }
          default:
            pos[0] = text[0];
            pos[1] = text[1];
            pos += 2;
            break;
        }

        text += 2;
      }
    }
  } else {
    if (length == -1) {
      while (*text != '\0') {
        guint8 code = (guint8) (*text);

        switch (code) {
          case 0x86:           /* emphasis on */
          case 0x87:           /* emphasis off */
            /* skip it */
            break;
          case 0x8A:
            *pos = '\n';
            pos += 1;
            break;
          default:
            *pos = *text;
            pos += 1;
            break;
        }

        text++;
      }
    } else {
      for (i = 0; i < length; i++) {
        guint8 code = (guint8) (*text);

        switch (code) {
          case 0x86:           /* emphasis on */
          case 0x87:           /* emphasis off */
            /* skip it */
            break;
          case 0x8A:
            *pos = '\n';
            pos += 1;
            break;
          default:
            *pos = *text;
            pos += 1;
            break;
        }

        text++;
      }
    }
  }

  if (pos > tmp) {
    gsize bread = 0;

    new_text =
        g_convert_with_iconv (tmp, pos - tmp, iconv, &bread, NULL, error);
    GST_DEBUG ("Converted to : %s", new_text);
  } else {
    new_text = g_strdup ("");
  }

  g_free (tmp);

  return new_text;
}

static gchar *
get_encoding_and_convert (MpegTSPacketizer2 * packetizer, const gchar * text,
    guint length)
{
  GError *error = NULL;
  gchar *converted_str;
  guint start_text = 0;
  gboolean is_multibyte;
  LocalIconvCode encoding;
  GIConv iconv = (GIConv) - 1;

  g_return_val_if_fail (text != NULL, NULL);

  if (text == NULL || length == 0)
    return g_strdup ("");

  encoding = get_encoding (packetizer, text, &start_text, &is_multibyte);

  if (encoding > _ICONV_UNKNOWN && encoding < _ICONV_MAX) {
    GST_DEBUG ("Encoding %s", iconvtablename[encoding]);
    if (packetizer->priv->iconvs[encoding] == (GIConv) - 1)
      packetizer->priv->iconvs[encoding] =
          g_iconv_open ("utf-8", iconvtablename[encoding]);
    iconv = packetizer->priv->iconvs[encoding];
  }

  if (iconv == (GIConv) - 1) {
    GST_WARNING ("Could not detect encoding");
    converted_str = g_strndup (text, length);
    goto beach;
  }

  converted_str = convert_to_utf8 (text, length - start_text, start_text,
      iconv, is_multibyte, &error);
  if (error != NULL) {
    GST_WARNING ("Could not convert string: %s", error->message);
    g_error_free (error);
    error = NULL;

    if (encoding >= _ICONV_ISO8859_2 && encoding <= _ICONV_ISO8859_15) {
      /* Sometimes using the standard 8859-1 set fixes issues */
      GST_DEBUG ("Encoding %s", iconvtablename[_ICONV_ISO8859_1]);
      if (packetizer->priv->iconvs[_ICONV_ISO8859_1] == (GIConv) - 1)
        packetizer->priv->iconvs[_ICONV_ISO8859_1] =
            g_iconv_open ("utf-8", iconvtablename[_ICONV_ISO8859_1]);
      iconv = packetizer->priv->iconvs[_ICONV_ISO8859_1];

      GST_INFO ("Trying encoding ISO 8859-1");
      converted_str = convert_to_utf8 (text, length, 1, iconv, FALSE, &error);
      if (error != NULL) {
        GST_WARNING
            ("Could not convert string while assuming encoding ISO 8859-1: %s",
            error->message);
        g_error_free (error);
        goto failed;
      }
    } else if (encoding == _ICONV_ISO6937) {

      /* The first part of ISO 6937 is identical to ISO 8859-9, but
       * they differ in the second part. Some channels don't
       * provide the first byte that indicates ISO 8859-9 encoding.
       * If decoding from ISO 6937 failed, we try ISO 8859-9 here.
       */
      if (packetizer->priv->iconvs[_ICONV_ISO8859_9] == (GIConv) - 1)
        packetizer->priv->iconvs[_ICONV_ISO8859_9] =
            g_iconv_open ("utf-8", iconvtablename[_ICONV_ISO8859_9]);
      iconv = packetizer->priv->iconvs[_ICONV_ISO8859_9];

      GST_INFO ("Trying encoding ISO 8859-9");
      converted_str = convert_to_utf8 (text, length, 0, iconv, FALSE, &error);
      if (error != NULL) {
        GST_WARNING
            ("Could not convert string while assuming encoding ISO 8859-9: %s",
            error->message);
        g_error_free (error);
        goto failed;
      }
    } else
      goto failed;
  }

beach:
  return converted_str;

failed:
  {
    text += start_text;
    return g_strndup (text, length - start_text);
  }
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
  guint64 slope;

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

  GST_DEBUG ("gstpcr %" GST_TIME_FORMAT ", buftime %" GST_TIME_FORMAT ", base %"
      GST_TIME_FORMAT ", send_diff %" GST_TIME_FORMAT,
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

  /* measure the slope, this gives a rought estimate between the sender speed
   * and the receiver speed. This should be approximately 8, higher values
   * indicate a burst (especially when the connection starts) */
  slope = recv_diff > 0 ? (send_diff * 8) / recv_diff : 8;

  GST_DEBUG ("time %" GST_TIME_FORMAT ", base %" GST_TIME_FORMAT ", recv_diff %"
      GST_TIME_FORMAT ", slope %" G_GUINT64_FORMAT, GST_TIME_ARGS (time),
      GST_TIME_ARGS (pcr->base_time), GST_TIME_ARGS (recv_diff), slope);

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
    GST_DEBUG ("delta %" G_GINT64_FORMAT ", new min: %" G_GINT64_FORMAT, delta,
        pcr->window_min);
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
record_pcr (MpegTSPacketizer2 * packetizer, MpegTSPCR * pcrtable, guint64 pcr,
    guint64 offset)
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
mpegts_packetizer_offset_to_ts (MpegTSPacketizer2 * packetizer, guint64 offset,
    guint16 pid)
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

  /* Convert byte difference into time difference */
  res = PCRTIME_TO_GSTTIME (gst_util_uint64_scale (offset - priv->refoffset,
          pcrtable->last_pcr - pcrtable->first_pcr,
          pcrtable->last_offset - pcrtable->first_offset));
  GST_DEBUG ("Returning timestamp %" GST_TIME_FORMAT " for offset %"
      G_GUINT64_FORMAT, GST_TIME_ARGS (res), offset);

  return res;
}

GstClockTime
mpegts_packetizer_pts_to_ts (MpegTSPacketizer2 * packetizer, GstClockTime pts,
    guint16 pcr_pid)
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
mpegts_packetizer_ts_to_offset (MpegTSPacketizer2 * packetizer, GstClockTime ts,
    guint16 pcr_pid)
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

  GST_DEBUG ("Returning offset %" G_GUINT64_FORMAT " for ts %" GST_TIME_FORMAT,
      res, GST_TIME_ARGS (ts));

  return res;
}

void
mpegts_packetizer_set_reference_offset (MpegTSPacketizer2 * packetizer,
    guint64 refoffset)
{
  GST_DEBUG ("Setting reference offset to %" G_GUINT64_FORMAT, refoffset);

  packetizer->priv->refoffset = refoffset;
}
