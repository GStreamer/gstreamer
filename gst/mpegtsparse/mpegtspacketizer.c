/*
 * mpegtspacketizer.c - 
 * Copyright (C) 2007 Alessandro Decina
 * 
 * Authors:
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

#include "mpegtspacketizer.h"
#include "flutspatinfo.h"
#include "flutspmtinfo.h"

GST_DEBUG_CATEGORY_STATIC (mpegts_packetizer_debug);
#define GST_CAT_DEFAULT mpegts_packetizer_debug

G_DEFINE_TYPE (MpegTSPacketizer, mpegts_packetizer, G_TYPE_OBJECT);

static void mpegts_packetizer_dispose (GObject * object);
static void mpegts_packetizer_finalize (GObject * object);

#define CONTINUITY_UNSET 255
#define MAX_CONTINUITY 15
#define SECTION_VERSION_NUMBER_NOTSET 255

typedef struct
{
  guint16 pid;
  guint continuity_counter;
  GstAdapter *section_adapter;
  guint section_length;
  guint8 section_version_number;
} MpegTSPacketizerStream;

static MpegTSPacketizerStream *
mpegts_packetizer_stream_new (guint16 pid)
{
  MpegTSPacketizerStream *stream;

  stream = (MpegTSPacketizerStream *) g_new0 (MpegTSPacketizerStream, 1);
  stream->section_adapter = gst_adapter_new ();
  stream->pid = pid;
  stream->continuity_counter = CONTINUITY_UNSET;
  stream->section_version_number = SECTION_VERSION_NUMBER_NOTSET;
  return stream;
}

static void
mpegts_packetizer_stream_free (MpegTSPacketizerStream * stream)
{
  gst_adapter_clear (stream->section_adapter);
  g_object_unref (stream->section_adapter);
  g_free (stream);
}

static void
mpegts_packetizer_clear_section (MpegTSPacketizer * packetizer,
    MpegTSPacketizerStream * stream)
{
  gst_adapter_clear (stream->section_adapter);
  stream->continuity_counter = CONTINUITY_UNSET;
  stream->section_length = 0;
}

static void
mpegts_packetizer_class_init (MpegTSPacketizerClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->dispose = mpegts_packetizer_dispose;
  gobject_class->finalize = mpegts_packetizer_finalize;
}

static void
mpegts_packetizer_init (MpegTSPacketizer * packetizer)
{
  packetizer->adapter = gst_adapter_new ();
  packetizer->streams = g_hash_table_new (g_direct_hash, g_direct_equal);
}

static void
mpegts_packetizer_dispose (GObject * object)
{
  MpegTSPacketizer *packetizer = GST_MPEGTS_PACKETIZER (object);

  if (!packetizer->disposed) {
    gst_adapter_clear (packetizer->adapter);
    g_object_unref (packetizer->adapter);
    packetizer->disposed = TRUE;
  }

  if (G_OBJECT_CLASS (mpegts_packetizer_parent_class)->dispose)
    G_OBJECT_CLASS (mpegts_packetizer_parent_class)->dispose (object);
}

static gboolean
stream_foreach_remove (gpointer key, gpointer value, gpointer data)
{
  MpegTSPacketizerStream *stream;

  stream = (MpegTSPacketizerStream *) value;
  mpegts_packetizer_stream_free (stream);

  return TRUE;
}

static void
mpegts_packetizer_finalize (GObject * object)
{
  MpegTSPacketizer *packetizer = GST_MPEGTS_PACKETIZER (object);

  g_hash_table_foreach_remove (packetizer->streams,
      stream_foreach_remove, packetizer);
  g_hash_table_destroy (packetizer->streams);

  if (G_OBJECT_CLASS (mpegts_packetizer_parent_class)->finalize)
    G_OBJECT_CLASS (mpegts_packetizer_parent_class)->finalize (object);
}

static gboolean
mpegts_packetizer_parse_adaptation_field_control (MpegTSPacketizer * packetizer,
    MpegTSPacketizerPacket * packet)
{
  guint8 length;

  length = *packet->data++;

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

  /* skip the adaptation field body for now */
  if (packet->data + length > packet->data_end) {
    GST_DEBUG ("PID %d afc length overflows the buffer %d",
        packet->pid, length);
    return FALSE;
  }

  packet->data += length;

  return TRUE;
}

static gboolean
mpegts_packetizer_parse_packet (MpegTSPacketizer * packetizer,
    MpegTSPacketizerPacket * packet)
{
  guint8 *data;

  data = GST_BUFFER_DATA (packet->buffer);
  /* skip sync_byte */
  data++;

  packet->payload_unit_start_indicator = (*data >> 6) & 0x01;
  packet->pid = GST_READ_UINT16_BE (data) & 0x1FFF;
  data += 2;

  packet->adaptation_field_control = *data >> 4 & 0x03;
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

  return TRUE;
}

static gboolean
mpegts_packetizer_parse_section_header (MpegTSPacketizer * packetizer,
    MpegTSPacketizerStream * stream, MpegTSPacketizerSection * section)
{
  guint8 tmp;
  guint8 *data;

  section->complete = TRUE;
  /* get the section buffer, pass the ownership to the caller */
  section->buffer = gst_adapter_take_buffer (stream->section_adapter,
      3 + stream->section_length);
  data = GST_BUFFER_DATA (section->buffer);

  section->table_id = *data++;
  section->section_length = GST_READ_UINT16_BE (data) & 0x0FFF;
  data += 2;

  /* skip to the version byte */
  data += 2;

  tmp = *data++;
  section->version_number = (tmp >> 1) & 0x1F;
  section->current_next_indicator = tmp & 0x01;

  if (!section->current_next_indicator)
    goto not_applicable;

  if (section->version_number == stream->section_version_number)
    goto not_applicable;

  stream->section_version_number = section->version_number;

  return TRUE;

not_applicable:
  section->complete = FALSE;
  gst_buffer_unref (section->buffer);
  return TRUE;
}

GValueArray *
mpegts_packetizer_parse_pat (MpegTSPacketizer * packetizer,
    MpegTSPacketizerSection * section)
{
  guint8 *data, *end;
  guint16 transport_stream_id;
  guint8 tmp;
  guint program_number;
  guint16 pmt_pid;
  MpegTSPatInfo *info;
  GValueArray *pat;
  GValue value = { 0 };

  data = GST_BUFFER_DATA (section->buffer);
  pat = g_value_array_new (0);

  section->table_id = *data++;
  section->section_length = GST_READ_UINT16_BE (data) & 0x0FFF;
  data += 2;

  transport_stream_id = GST_READ_UINT16_BE (data);
  data += 2;

  tmp = *data++;
  section->version_number = (tmp >> 1) & 0x1F;
  section->current_next_indicator = tmp & 0x01;

  /* skip section_number and last_section_number */
  data += 2;

  /* stop at the CRC */
  end = GST_BUFFER_DATA (section->buffer) + GST_BUFFER_SIZE (section->buffer);
  while (data < end - 4) {
    program_number = GST_READ_UINT16_BE (data);
    data += 2;

    pmt_pid = GST_READ_UINT16_BE (data) & 0x1FFF;
    data += 2;

    info = mpegts_pat_info_new (program_number, pmt_pid);

    g_value_init (&value, G_TYPE_OBJECT);
    g_value_take_object (&value, info);
    g_value_array_append (pat, &value);
    g_value_unset (&value);
  }

  if (data != end - 4) {
    /* FIXME: check the CRC before parsing the packet */
    GST_ERROR ("at the end of PAT data != end - 4");
    g_value_array_free (pat);

    return NULL;
  }

  return pat;
}

GObject *
mpegts_packetizer_parse_pmt (MpegTSPacketizer * packetizer,
    MpegTSPacketizerSection * section)
{
  MpegTSPmtInfo *pmt = NULL;
  MpegTSPmtStreamInfo *stream_info;
  guint8 *data, *end;
  guint16 program_number;
  guint8 tmp;
  guint16 pcr_pid;
  guint program_info_length;
  guint8 tag, length;
  guint8 stream_type;
  guint16 pid;
  guint stream_info_length;

  /* fixed header + CRC == 16 */
  if (GST_BUFFER_SIZE (section->buffer) < 16) {
    GST_WARNING ("PID %d invalid PMT size %d",
        section->pid, section->section_length);
    goto error;
  }

  data = GST_BUFFER_DATA (section->buffer);
  end = data + GST_BUFFER_SIZE (section->buffer);

  section->table_id = *data++;
  section->section_length = GST_READ_UINT16_BE (data) & 0x0FFF;
  data += 2;

  program_number = GST_READ_UINT16_BE (data);
  data += 2;

  tmp = *data++;
  section->version_number = (tmp >> 1) & 0x1F;
  section->current_next_indicator = tmp & 0x01;

  /* skip section_number and last_section_number */
  data += 2;

  pcr_pid = GST_READ_UINT16_BE (data) & 0x1FFF;
  data += 2;

  program_info_length = GST_READ_UINT16_BE (data) & 0x0FFF;
  data += 2;

  /* check that the buffer is large enough to contain at least
   * program_info_length bytes + CRC */
  if (data + program_info_length + 4 > end) {
    GST_WARNING ("PID %d invalid program info length %d "
        "left %d", section->pid, program_info_length, end - data);
    goto error;
  }

  pmt = mpegts_pmt_info_new (program_number, pcr_pid, section->version_number);

  /* parse program level descriptors */
  while (program_info_length > 0) {
    tag = *data++;
    length = *data++;
    program_info_length -= 2;

    if (length > program_info_length) {
      GST_WARNING ("PID %d invalid descriptor length %d left %d",
          section->pid, length, program_info_length);
      goto error;
    }

    mpegts_pmt_info_add_descriptor (pmt, (const gchar *) data - 2, 2 + length);
    data += length;
    program_info_length -= length;
  }

  g_assert (program_info_length == 0);

  /* parse entries, cycle until there's space for another entry (at least 5
   * bytes) plus the CRC */
  while (data <= end - 4 - 5) {
    stream_type = *data++;

    pid = GST_READ_UINT16_BE (data) & 0x1FFF;
    data += 2;

    stream_info_length = GST_READ_UINT16_BE (data) & 0x0FFF;
    data += 2;

    if (data + stream_info_length + 4 > end) {
      GST_WARNING ("PID %d invalid stream info length %d "
          "left %d", section->pid, stream_info_length, end - data);
      goto error;
    }

    GST_INFO ("PMT PID %d program_number %d pid %d",
        section->pid, program_number, pid);

    stream_info = mpegts_pmt_stream_info_new (pid, stream_type);

    /* parse stream level descriptors */
    while (stream_info_length > 0) {
      tag = *data++;
      length = *data++;
      stream_info_length -= 2;

      if (length > stream_info_length) {
        GST_WARNING ("PID %d invalid descriptor length %d left %d",
            section->pid, length, stream_info_length);
        g_object_unref (stream_info);
        goto error;
      }

      mpegts_pmt_stream_info_add_descriptor (stream_info,
          (const gchar *) data - 2, 2 + length);
      data += length;
      stream_info_length -= length;
    }

    /* adds a ref to stream_info */
    mpegts_pmt_info_add_stream (pmt, stream_info);
    g_object_unref (stream_info);
  }

  g_assert (data == end - 4);

  return G_OBJECT (pmt);

error:
  if (pmt)
    g_object_unref (pmt);
  return NULL;
}

static void
foreach_stream_clear (gpointer key, gpointer value, gpointer data)
{
  MpegTSPacketizerStream *stream = (MpegTSPacketizerStream *) value;

  /* remove the stream */
  g_object_unref (stream->section_adapter);
  g_free (stream);
}

static gboolean
remove_all (gpointer key, gpointer value, gpointer user_data)
{
  return TRUE;
}

void
mpegts_packetizer_clear (MpegTSPacketizer * packetizer)
{
  g_hash_table_foreach (packetizer->streams, foreach_stream_clear, packetizer);

  /* FIXME can't use remove_all because we don't depend on 2.12 yet */
  g_hash_table_foreach_remove (packetizer->streams, remove_all, NULL);
  gst_adapter_clear (packetizer->adapter);
}

MpegTSPacketizer *
mpegts_packetizer_new ()
{
  MpegTSPacketizer *packetizer;

  packetizer =
      GST_MPEGTS_PACKETIZER (g_object_new (GST_TYPE_MPEGTS_PACKETIZER, NULL));

  return packetizer;
}

void
mpegts_packetizer_push (MpegTSPacketizer * packetizer, GstBuffer * buffer)
{
  g_return_if_fail (GST_IS_MPEGTS_PACKETIZER (packetizer));
  g_return_if_fail (GST_IS_BUFFER (buffer));

  gst_adapter_push (packetizer->adapter, buffer);
}

gboolean
mpegts_packetizer_has_packets (MpegTSPacketizer * packetizer)
{
  g_return_val_if_fail (GST_IS_MPEGTS_PACKETIZER (packetizer), FALSE);

  return gst_adapter_available (packetizer->adapter) >= 188;
}

gboolean
mpegts_packetizer_next_packet (MpegTSPacketizer * packetizer,
    MpegTSPacketizerPacket * packet)
{
  guint8 sync_byte;
  gboolean ret = FALSE;

  g_return_val_if_fail (GST_IS_MPEGTS_PACKETIZER (packetizer), FALSE);
  g_return_val_if_fail (packet != NULL, FALSE);

  packet->buffer = NULL;
  while (gst_adapter_available (packetizer->adapter) >= 188) {
    sync_byte = *gst_adapter_peek (packetizer->adapter, 1);
    if (sync_byte != 0x47) {
      GST_DEBUG ("lost sync %02x", sync_byte);
      gst_adapter_flush (packetizer->adapter, 1);
      continue;
    }

    packet->buffer = gst_adapter_take_buffer (packetizer->adapter, 188);
    packet->data_start = GST_BUFFER_DATA (packet->buffer);
    packet->data_end =
        GST_BUFFER_DATA (packet->buffer) + GST_BUFFER_SIZE (packet->buffer);
    ret = mpegts_packetizer_parse_packet (packetizer, packet);
    break;
  }

  return ret;
}

void
mpegts_packetizer_clear_packet (MpegTSPacketizer * packetizer,
    MpegTSPacketizerPacket * packet)
{
  g_return_if_fail (GST_IS_MPEGTS_PACKETIZER (packetizer));
  g_return_if_fail (packet != NULL);

  if (packet->buffer)
    gst_buffer_unref (packet->buffer);
  packet->buffer = NULL;
  packet->continuity_counter = 0;
  packet->payload_unit_start_indicator = 0;
  packet->payload = NULL;
  packet->data_start = NULL;
  packet->data_end = NULL;
}

gboolean
mpegts_packetizer_push_section (MpegTSPacketizer * packetizer,
    MpegTSPacketizerPacket * packet, MpegTSPacketizerSection * section)
{
  gboolean res = FALSE;
  MpegTSPacketizerStream *stream;
  guint8 pointer, table_id;
  guint section_length;
  GstBuffer *sub_buf;
  guint8 *data;

  g_return_val_if_fail (GST_IS_MPEGTS_PACKETIZER (packetizer), FALSE);
  g_return_val_if_fail (packet != NULL, FALSE);
  g_return_val_if_fail (section != NULL, FALSE);

  data = packet->data;
  section->pid = packet->pid;

  stream = (MpegTSPacketizerStream *) g_hash_table_lookup (packetizer->streams,
      GINT_TO_POINTER ((gint) packet->pid));
  if (stream == NULL) {
    stream = mpegts_packetizer_stream_new (packet->pid);
    g_hash_table_insert (packetizer->streams,
        GINT_TO_POINTER ((gint) packet->pid), stream);
  }

  if (packet->payload_unit_start_indicator == 1) {
    pointer = *data++;
    if (data + pointer > packet->data_end) {
      GST_WARNING ("PID %d PSI section pointer points past the end "
          "of the buffer", packet->pid);
      goto out;
    }

    data += pointer;
  }

  table_id = *data++;

  section_length = GST_READ_UINT16_BE (data) & 0x0FFF;
  data += 2;

  /* create a sub buffer from the start of the section (table_id and
   * section_length included) to the end */
  sub_buf = gst_buffer_create_sub (packet->buffer,
      data - 3 - GST_BUFFER_DATA (packet->buffer), packet->data_end - data + 3);

  if (packet->payload_unit_start_indicator) {
    if (stream->continuity_counter != CONTINUITY_UNSET) {
      GST_WARNING ("PID %d payload_unit_start_indicator set but section "
          "not complete (last_continuity: %d continuity: %d sec len %d buffer %d avail %d",
          packet->pid, stream->continuity_counter, packet->continuity_counter,
          section_length, GST_BUFFER_SIZE (sub_buf),
          gst_adapter_available (stream->section_adapter));
      mpegts_packetizer_clear_section (packetizer, stream);
    }

    stream->continuity_counter = packet->continuity_counter;
    stream->section_length = section_length;
    gst_adapter_push (stream->section_adapter, sub_buf);

    res = TRUE;
  } else if (packet->continuity_counter == stream->continuity_counter + 1 ||
      (stream->continuity_counter == MAX_CONTINUITY &&
          packet->continuity_counter == 0)) {
    stream->continuity_counter = packet->continuity_counter;
    gst_adapter_push (stream->section_adapter, sub_buf);

    res = TRUE;
  } else {
    GST_WARNING ("PID %d section discontinuity "
        "(last_continuity: %d continuity: %d", packet->pid,
        stream->continuity_counter, packet->continuity_counter);
    mpegts_packetizer_clear_section (packetizer, stream);
    gst_buffer_unref (sub_buf);
  }

  if (res) {
    /* we pushed some data in the section adapter, see if the section is
     * complete now */

    /* >= as sections can be padded and padding is not included in
     * section_length */
    if (gst_adapter_available (stream->section_adapter) >=
        stream->section_length + 3) {
      res = mpegts_packetizer_parse_section_header (packetizer,
          stream, section);

      /* flush stuffing bytes */
      mpegts_packetizer_clear_section (packetizer, stream);
    } else {
      /* section not complete yet */
      section->complete = FALSE;
    }
  } else {
    section->complete = FALSE;
  }

out:
  packet->data = data;
  return res;
}

void
mpegts_packetizer_init_debug ()
{
  GST_DEBUG_CATEGORY_INIT (mpegts_packetizer_debug, "mpegtspacketizer", 0,
      "MPEG transport stream parser");
}
