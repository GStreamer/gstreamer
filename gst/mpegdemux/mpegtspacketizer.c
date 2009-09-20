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

#include "mpegtspacketizer.h"
#include "gstmpegdesc.h"

GST_DEBUG_CATEGORY_STATIC (mpegts_packetizer_debug);
#define GST_CAT_DEFAULT mpegts_packetizer_debug

static GQuark QUARK_PAT;
static GQuark QUARK_TRANSPORT_STREAM_ID;
static GQuark QUARK_PROGRAM_NUMBER;
static GQuark QUARK_PID;
static GQuark QUARK_PROGRAMS;

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
static GQuark QUARK_TRANSPORT_STREAM_ID;
static GQuark QUARK_ORIGINAL_NETWORK_ID;
static GQuark QUARK_TRANSPORTS;

static GQuark QUARK_SDT;
static GQuark QUARK_ACTUAL_TRANSPORT_STREAM;
static GQuark QUARK_SERVICES;

static GQuark QUARK_EIT;
static GQuark QUARK_SERVICE_ID;
static GQuark QUARK_PRESENT_FOLLOWING;
static GQuark QUARK_SEGMENT_LAST_SECTION_NUMBER;
static GQuark QUARK_LAST_TABLE_ID;
static GQuark QUARK_EVENTS;

static void _init_local (void);
G_DEFINE_TYPE_EXTENDED (MpegTSPacketizer, mpegts_packetizer, G_TYPE_OBJECT, 0,
    _init_local ());

static void mpegts_packetizer_dispose (GObject * object);
static void mpegts_packetizer_finalize (GObject * object);
static gchar *convert_to_utf8 (const gchar * text, gint length, guint start,
    const gchar * encoding, gboolean is_multibyte, GError ** error);
static gchar *get_encoding (const gchar * text, guint * start_text,
    gboolean * is_multibyte);
static gchar *get_encoding_and_convert (const gchar * text, guint length);

#define CONTINUITY_UNSET 255
#define MAX_CONTINUITY 15
#define VERSION_NUMBER_UNSET 255
#define TABLE_ID_UNSET 0xFF

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
  return subtable;
}

static MpegTSPacketizerStream *
mpegts_packetizer_stream_new ()
{
  MpegTSPacketizerStream *stream;

  stream = (MpegTSPacketizerStream *) g_new0 (MpegTSPacketizerStream, 1);
  stream->section_adapter = gst_adapter_new ();
  stream->continuity_counter = CONTINUITY_UNSET;
  stream->subtables = NULL;
  stream->section_table_id = TABLE_ID_UNSET;
  return stream;
}

static void
mpegts_packetizer_stream_free (MpegTSPacketizerStream * stream)
{
  gst_adapter_clear (stream->section_adapter);
  g_object_unref (stream->section_adapter);
  g_slist_foreach (stream->subtables, (GFunc) g_free, NULL);
  g_slist_free (stream->subtables);
  g_free (stream);
}

static void
mpegts_packetizer_clear_section (MpegTSPacketizer * packetizer,
    MpegTSPacketizerStream * stream)
{
  gst_adapter_clear (stream->section_adapter);
  stream->continuity_counter = CONTINUITY_UNSET;
  stream->section_length = 0;
  stream->section_table_id = TABLE_ID_UNSET;
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
  packetizer->streams = g_new0 (MpegTSPacketizerStream *, 8192);
  packetizer->know_packet_size = FALSE;
}

static void
mpegts_packetizer_dispose (GObject * object)
{
  MpegTSPacketizer *packetizer = GST_MPEGTS_PACKETIZER (object);

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

static gboolean
mpegts_packetizer_parse_adaptation_field_control (MpegTSPacketizer * packetizer,
    MpegTSPacketizerPacket * packet)
{
  guint8 length;

  length = *packet->data;
  packet->data += 1;

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
    GST_DEBUG ("PID %d afc length %d overflows the buffer current %d max %d",
        packet->pid, length, packet->data - packet->data_start,
        packet->data_end - packet->data_start);
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

  packet->adaptation_field_control = (*data >> 4) & 0x03;
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
  MpegTSPacketizerStreamSubtable *subtable;
  GSList *subtable_list = NULL;

  section->complete = TRUE;
  /* get the section buffer, pass the ownership to the caller */
  section->buffer = gst_adapter_take_buffer (stream->section_adapter,
      3 + stream->section_length);
  data = GST_BUFFER_DATA (section->buffer);

  section->table_id = *data++;
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

  section->section_length = GST_READ_UINT16_BE (data) & 0x0FFF;
  data += 2;

  /* skip to the version byte */
  data += 2;

  tmp = *data++;
  section->version_number = (tmp >> 1) & 0x1F;
  section->current_next_indicator = tmp & 0x01;
  if (!section->current_next_indicator)
    goto not_applicable;

  if (section->version_number == subtable->version_number)
    goto not_applicable;
  subtable->version_number = section->version_number;
  stream->section_table_id = section->table_id;

  return TRUE;

not_applicable:
  GST_LOG
      ("not applicable pid %d table_id %d subtable_extension %d, current_next %d version %d",
      section->pid, section->table_id, section->subtable_extension,
      section->current_next_indicator, section->version_number);
  section->complete = FALSE;
  gst_buffer_unref (section->buffer);
  return TRUE;
}

static gboolean
mpegts_packetizer_parse_descriptors (MpegTSPacketizer * packetizer,
    guint8 ** buffer, guint8 * buffer_end, GValueArray * descriptors)
{
  guint8 tag, length;
  guint8 *data;
  GValue value = { 0 };
  GString *desc;

  data = *buffer;

  while (data < buffer_end) {
    tag = *data++;
    length = *data++;

    if (data + length > buffer_end) {
      GST_WARNING ("invalid descriptor length %d now at %d max %d",
          length, data - *buffer, buffer_end - *buffer);
      goto error;
    }

    /* include tag and length */
    desc = g_string_new_len ((gchar *) data - 2, length + 2);
    data += length;
    /* G_TYPE_GSTING is a GBoxed type and is used so properly marshalled from python */
    g_value_init (&value, G_TYPE_GSTRING);
    g_value_take_boxed (&value, desc);
    g_value_array_append (descriptors, &value);
    g_value_unset (&value);
  }

  if (data != buffer_end) {
    GST_WARNING ("descriptors size %d expected %d",
        data - *buffer, buffer_end - *buffer);
    goto error;
  }

  *buffer = data;

  return TRUE;
error:
  return FALSE;
}

GstStructure *
mpegts_packetizer_parse_pat (MpegTSPacketizer * packetizer,
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

  data = GST_BUFFER_DATA (section->buffer);

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

  pat_info = gst_structure_id_new (QUARK_PAT,
      QUARK_TRANSPORT_STREAM_ID, G_TYPE_UINT, transport_stream_id, NULL);
  g_value_init (&entries, GST_TYPE_LIST);
  /* stop at the CRC */
  end = GST_BUFFER_DATA (section->buffer) + GST_BUFFER_SIZE (section->buffer);
  while (data < end - 4) {
    program_number = GST_READ_UINT16_BE (data);
    data += 2;

    pmt_pid = GST_READ_UINT16_BE (data) & 0x1FFF;
    data += 2;

    struct_name = g_strdup_printf ("program-%d", program_number);
    entry = gst_structure_new (struct_name, NULL);
    g_free (struct_name);
    gst_structure_id_set (entry, QUARK_PROGRAM_NUMBER, G_TYPE_UINT,
        program_number, QUARK_PID, G_TYPE_UINT, pmt_pid, NULL);

    g_value_init (&value, GST_TYPE_STRUCTURE);
    g_value_take_boxed (&value, entry);
    gst_value_list_append_value (&entries, &value);
    g_value_unset (&value);
  }

  gst_structure_id_set_value (pat_info, QUARK_PROGRAMS, &entries);
  g_value_unset (&entries);

  if (data != end - 4) {
    /* FIXME: check the CRC before parsing the packet */
    GST_ERROR ("at the end of PAT data != end - 4");
    gst_structure_free (pat_info);

    return NULL;
  }

  return pat_info;
}

GstStructure *
mpegts_packetizer_parse_pmt (MpegTSPacketizer * packetizer,
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

  pmt = gst_structure_id_new (QUARK_PMT,
      QUARK_PROGRAM_NUMBER, G_TYPE_UINT, program_number,
      QUARK_PCR_PID, G_TYPE_UINT, pcr_pid,
      QUARK_VERSION_NUMBER, G_TYPE_UINT, section->version_number, NULL);

  if (program_info_length) {
    /* check that the buffer is large enough to contain at least
     * program_info_length bytes + CRC */
    if (data + program_info_length + 4 > end) {
      GST_WARNING ("PID %d invalid program info length %d "
          "left %d", section->pid, program_info_length, end - data);
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

    pid = GST_READ_UINT16_BE (data) & 0x1FFF;
    data += 2;

    stream_info_length = GST_READ_UINT16_BE (data) & 0x0FFF;
    data += 2;

    if (data + stream_info_length + 4 > end) {
      GST_WARNING ("PID %d invalid stream info length %d "
          "left %d", section->pid, stream_info_length, end - data);
      g_value_unset (&programs);
      goto error;
    }

    struct_name = g_strdup_printf ("pid-%d", pid);
    stream_info = gst_structure_new (struct_name, NULL);
    g_free (struct_name);
    gst_structure_id_set (stream_info,
        QUARK_PID, G_TYPE_UINT, pid, QUARK_STREAM_TYPE, G_TYPE_UINT,
        stream_type, NULL);

    if (stream_info_length) {
      /* check for AC3 descriptor */
      GstMPEGDescriptor *desc =
          gst_mpeg_descriptor_parse (data, stream_info_length);
      if (desc != NULL) {
        guint8 *desc_data;
        if (gst_mpeg_descriptor_find (desc, DESC_DVB_AC3)) {
          gst_structure_set (stream_info, "has-ac3", G_TYPE_BOOLEAN, TRUE,
              NULL);
        }
        desc_data = gst_mpeg_descriptor_find (desc, DESC_DVB_DATA_BROADCAST_ID);
        if (desc_data) {
          guint16 data_broadcast_id;
          data_broadcast_id =
              DESC_DVB_DATA_BROADCAST_ID_data_broadcast_id (desc_data);
          gst_structure_set (stream_info, "data-broadcast-id", G_TYPE_UINT,
              data_broadcast_id, NULL);
        }
        desc_data = gst_mpeg_descriptor_find (desc, DESC_DVB_DATA_BROADCAST);
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
        desc_data =
            gst_mpeg_descriptor_find (desc, DESC_DVB_CAROUSEL_IDENTIFIER);
        if (desc_data) {
          guint32 carousel_id;
          carousel_id = DESC_DVB_CAROUSEL_IDENTIFIER_carousel_id (desc_data);
          gst_structure_set (stream_info, "carousel-id", G_TYPE_UINT,
              carousel_id, NULL);
        }
        desc_data = gst_mpeg_descriptor_find (desc, DESC_DVB_STREAM_IDENTIFIER);
        if (desc_data) {
          guint8 component_tag;
          component_tag = DESC_DVB_STREAM_IDENTIFIER_component_tag (desc_data);
          gst_structure_set (stream_info, "component-tag", G_TYPE_UINT,
              component_tag, NULL);
        }
        gst_mpeg_descriptor_free (desc);
      }

      descriptors = g_value_array_new (0);
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

    g_value_init (&stream_value, GST_TYPE_STRUCTURE);
    g_value_take_boxed (&stream_value, stream_info);
    gst_value_list_append_value (&programs, &stream_value);
    g_value_unset (&stream_value);
  }

  gst_structure_id_set_value (pmt, QUARK_STREAMS, &programs);
  g_value_unset (&programs);

  g_assert (data == end - 4);

  return pmt;

error:
  if (pmt)
    gst_structure_free (pmt);

  return NULL;
}

GstStructure *
mpegts_packetizer_parse_nit (MpegTSPacketizer * packetizer,
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
  if (GST_BUFFER_SIZE (section->buffer) < 23) {
    GST_WARNING ("PID %d invalid NIT size %d",
        section->pid, section->section_length);
    goto error;
  }

  data = GST_BUFFER_DATA (section->buffer);
  end = data + GST_BUFFER_SIZE (section->buffer);

  section->table_id = *data++;
  section->section_length = GST_READ_UINT16_BE (data) & 0x0FFF;
  data += 2;

  if (data + section->section_length != end) {
    GST_WARNING ("PID %d invalid NIT section length %d expected %d",
        section->pid, section->section_length, end - data);
    goto error;
  }

  network_id = GST_READ_UINT16_BE (data);
  data += 2;

  tmp = *data++;
  section->version_number = (tmp >> 1) & 0x1F;
  section->current_next_indicator = tmp & 0x01;

  /* skip section_number and last_section_number */
  data += 2;

  descriptors_loop_length = GST_READ_UINT16_BE (data) & 0x0FFF;
  data += 2;

  nit = gst_structure_id_new (QUARK_NIT,
      QUARK_NETWORK_ID, G_TYPE_UINT, network_id,
      QUARK_VERSION_NUMBER, G_TYPE_UINT, section->version_number,
      QUARK_CURRENT_NEXT_INDICATOR, G_TYPE_UINT,
      section->current_next_indicator, QUARK_ACTUAL_NETWORK, G_TYPE_BOOLEAN,
      section->table_id == 0x40, NULL);

  /* see if the buffer is large enough */
  if (descriptors_loop_length) {
    guint8 *networkname_descriptor;
    GstMPEGDescriptor *mpegdescriptor;

    if (data + descriptors_loop_length > end - 4) {
      GST_WARNING ("PID %d invalid NIT descriptors loop length %d",
          section->pid, descriptors_loop_length);
      gst_structure_free (nit);
      goto error;
    }
    mpegdescriptor = gst_mpeg_descriptor_parse (data, descriptors_loop_length);
    networkname_descriptor =
        gst_mpeg_descriptor_find (mpegdescriptor, DESC_DVB_NETWORK_NAME);
    if (networkname_descriptor != NULL) {
      gchar *networkname_tmp;

      /* No need to bounds check this value as it comes from the descriptor length itself */
      guint8 networkname_length =
          DESC_DVB_NETWORK_NAME_length (networkname_descriptor);
      gchar *networkname =
          (gchar *) DESC_DVB_NETWORK_NAME_text (networkname_descriptor);

      networkname_tmp =
          get_encoding_and_convert (networkname, networkname_length);
      gst_structure_id_set (nit, QUARK_NETWORK_NAME, G_TYPE_STRING,
          networkname_tmp, NULL);
      g_free (networkname_tmp);
    }
    gst_mpeg_descriptor_free (mpegdescriptor);

    descriptors = g_value_array_new (0);
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
    transport = gst_structure_new (transport_name, NULL);
    g_free (transport_name);
    gst_structure_id_set (transport,
        QUARK_TRANSPORT_STREAM_ID, G_TYPE_UINT, transport_stream_id,
        QUARK_ORIGINAL_NETWORK_ID, G_TYPE_UINT, original_network_id, NULL);

    if (descriptors_loop_length) {
      GstMPEGDescriptor *mpegdescriptor;
      guint8 *delivery;

      if (data + descriptors_loop_length > end - 4) {
        GST_WARNING ("PID %d invalid NIT entry %d descriptors loop length %d",
            section->pid, transport_stream_id, descriptors_loop_length);
        gst_structure_free (transport);
        goto error;
      }
      mpegdescriptor =
          gst_mpeg_descriptor_parse (data, descriptors_loop_length);

      if ((delivery =
              gst_mpeg_descriptor_find (mpegdescriptor,
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
        gchar *polarization_str;
        guint8 modulation =
            DESC_DVB_SATELLITE_DELIVERY_SYSTEM_modulation (delivery);
        gchar *modulation_str;
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
        gchar *fec_inner_str;

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
        delivery_structure = gst_structure_new ("satellite",
            "orbital", G_TYPE_FLOAT, orbital,
            "east-or-west", G_TYPE_STRING, east ? "east" : "west",
            "modulation", G_TYPE_STRING, modulation_str,
            "frequency", G_TYPE_UINT, frequency,
            "polarization", G_TYPE_STRING, polarization_str,
            "symbol-rate", G_TYPE_UINT, symbol_rate,
            "inner-fec", G_TYPE_STRING, fec_inner_str, NULL);
        gst_structure_set (transport, "delivery", GST_TYPE_STRUCTURE,
            delivery_structure, NULL);
      } else if ((delivery =
              gst_mpeg_descriptor_find (mpegdescriptor,
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
        gchar *constellation_str, *code_rate_hp_str, *code_rate_lp_str,
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
        delivery_structure = gst_structure_new ("terrestrial",
            "frequency", G_TYPE_UINT, frequency,
            "bandwidth", G_TYPE_UINT, bandwidth,
            "constellation", G_TYPE_STRING, constellation_str,
            "hierarchy", G_TYPE_UINT, hierarchy,
            "code-rate-hp", G_TYPE_STRING, code_rate_hp_str,
            "code-rate-lp", G_TYPE_STRING, code_rate_lp_str,
            "guard-interval", G_TYPE_UINT, guard_interval,
            "transmission-mode", G_TYPE_STRING, transmission_mode_str,
            "other-frequency", G_TYPE_BOOLEAN, other_frequency, NULL);
        gst_structure_set (transport, "delivery", GST_TYPE_STRUCTURE,
            delivery_structure, NULL);
      } else if ((delivery =
              gst_mpeg_descriptor_find (mpegdescriptor,
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
        gchar *modulation_str;
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
        gchar *fec_inner_str;

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
        delivery_structure = gst_structure_new ("cable",
            "modulation", G_TYPE_STRING, modulation_str,
            "frequency", G_TYPE_UINT, frequency,
            "symbol-rate", G_TYPE_UINT, symbol_rate,
            "inner-fec", G_TYPE_STRING, fec_inner_str, NULL);
        gst_structure_set (transport, "delivery", GST_TYPE_STRUCTURE,
            delivery_structure, NULL);
      }
      /* free the temporary delivery structure */
      if (delivery_structure != NULL) {
        gst_structure_free (delivery_structure);
        delivery_structure = NULL;
      }
      if ((delivery =
              gst_mpeg_descriptor_find (mpegdescriptor,
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
          channel =
              gst_structure_new ("channels", "service-id", G_TYPE_UINT,
              service_id, "logical-channel-number", G_TYPE_UINT,
              logical_channel_number, NULL);
          g_value_init (&channel_value, GST_TYPE_STRUCTURE);
          g_value_take_boxed (&channel_value, channel);
          gst_value_list_append_value (&channel_numbers, &channel_value);
          g_value_unset (&channel_value);
          current_pos += 2;
        }
        gst_structure_set_value (transport, "channels", &channel_numbers);
        g_value_unset (&channel_numbers);
      }
      if ((delivery =
              gst_mpeg_descriptor_find (mpegdescriptor,
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

          gst_structure_set_value (transport, fieldname, &frequencies);
          g_value_unset (&frequencies);
        }
      }
      gst_mpeg_descriptor_free (mpegdescriptor);

      descriptors = g_value_array_new (0);
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
        section->pid, data - GST_BUFFER_DATA (section->buffer),
        GST_BUFFER_SIZE (section->buffer));
    goto error;
  }

  gst_structure_id_set_value (nit, QUARK_TRANSPORTS, &transports);
  g_value_unset (&transports);

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
mpegts_packetizer_parse_sdt (MpegTSPacketizer * packetizer,
    MpegTSPacketizerSection * section)
{
  GstStructure *sdt = NULL, *service = NULL;
  guint8 *data, *end, *entry_begin;
  guint16 transport_stream_id, original_network_id, service_id;
  guint tmp;
  guint sdt_info_length;
  gboolean EIT_schedule, EIT_present_following;
  guint8 running_status;
  gboolean scrambled;
  guint descriptors_loop_length;
  GValue services = { 0 };
  GValueArray *descriptors = NULL;
  GValue service_value = { 0 };

  GST_DEBUG ("SDT");
  /* fixed header + CRC == 16 */
  if (GST_BUFFER_SIZE (section->buffer) < 14) {
    GST_WARNING ("PID %d invalid SDT size %d",
        section->pid, section->section_length);
    goto error;
  }

  data = GST_BUFFER_DATA (section->buffer);
  end = data + GST_BUFFER_SIZE (section->buffer);

  section->table_id = *data++;
  section->section_length = GST_READ_UINT16_BE (data) & 0x0FFF;
  data += 2;

  if (data + section->section_length != end) {
    GST_WARNING ("PID %d invalid SDT section length %d expected %d",
        section->pid, section->section_length, end - data);
    goto error;
  }

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

  sdt = gst_structure_id_new (QUARK_SDT,
      QUARK_TRANSPORT_STREAM_ID, G_TYPE_UINT, transport_stream_id,
      QUARK_VERSION_NUMBER, G_TYPE_UINT, section->version_number,
      QUARK_CURRENT_NEXT_INDICATOR, G_TYPE_UINT,
      section->current_next_indicator, QUARK_ORIGINAL_NETWORK_ID, G_TYPE_UINT,
      original_network_id, QUARK_ACTUAL_TRANSPORT_STREAM, G_TYPE_BOOLEAN,
      section->table_id == 0x42, NULL);

  sdt_info_length = section->section_length - 8;
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

    EIT_schedule = ((*data & 0x02) == 2);
    EIT_present_following = (*data & 0x01) == 1;

    data += 1;
    tmp = GST_READ_UINT16_BE (data);

    running_status = (*data >> 5) & 0x07;
    scrambled = (*data >> 4) & 0x01;
    descriptors_loop_length = tmp & 0x0FFF;
    data += 2;

    /* TODO send tag event down relevant pad for channel name and provider */
    service_name = g_strdup_printf ("service-%d", service_id);
    service = gst_structure_new (service_name, NULL);
    g_free (service_name);

    if (descriptors_loop_length) {
      guint8 *service_descriptor;
      GstMPEGDescriptor *mpegdescriptor;

      if (data + descriptors_loop_length > end - 4) {
        GST_WARNING ("PID %d invalid SDT entry %d descriptors loop length %d",
            section->pid, service_id, descriptors_loop_length);
        gst_structure_free (service);
        goto error;
      }
      mpegdescriptor =
          gst_mpeg_descriptor_parse (data, descriptors_loop_length);
      service_descriptor =
          gst_mpeg_descriptor_find (mpegdescriptor, DESC_DVB_SERVICE);
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
          gchar *running_status_tmp;
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
              get_encoding_and_convert (servicename, servicename_length);
          serviceprovider_name_tmp =
              get_encoding_and_convert (serviceprovider_name,
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
      gst_mpeg_descriptor_free (mpegdescriptor);

      descriptors = g_value_array_new (0);
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
        section->pid, data - GST_BUFFER_DATA (section->buffer),
        GST_BUFFER_SIZE (section->buffer));
    goto error;
  }

  gst_structure_id_set_value (sdt, QUARK_SERVICES, &services);
  g_value_unset (&services);

  return sdt;

error:
  if (sdt)
    gst_structure_free (sdt);

  if (GST_VALUE_HOLDS_LIST (&services))
    g_value_unset (&services);

  return NULL;
}

GstStructure *
mpegts_packetizer_parse_eit (MpegTSPacketizer * packetizer,
    MpegTSPacketizerSection * section)
{
  GstStructure *eit = NULL, *event = NULL;
  guint service_id, last_table_id, segment_last_section_number;
  guint transport_stream_id, original_network_id;
  gboolean free_ca_mode;
  guint event_id, running_status;
  guint64 start_and_duration;
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
  if (GST_BUFFER_SIZE (section->buffer) < 18) {
    GST_WARNING ("PID %d invalid EIT size %d",
        section->pid, section->section_length);
    goto error;
  }

  data = GST_BUFFER_DATA (section->buffer);
  end = data + GST_BUFFER_SIZE (section->buffer);

  section->table_id = *data++;
  section->section_length = GST_READ_UINT16_BE (data) & 0x0FFF;
  data += 2;

  if (data + section->section_length != end) {
    GST_WARNING ("PID %d invalid EIT section length %d expected %d",
        section->pid, section->section_length, end - data);
    goto error;
  }

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

  eit = gst_structure_id_new (QUARK_EIT,
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
          section->pid, end - 4 - data);
      gst_structure_free (eit);
      goto error;
    }

    event_id = GST_READ_UINT16_BE (data);
    data += 2;
    start_and_duration = GST_READ_UINT64_BE (data);
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
    event = gst_structure_new (event_name,
        "event-id", G_TYPE_UINT, event_id,
        "year", G_TYPE_UINT, year,
        "month", G_TYPE_UINT, month,
        "day", G_TYPE_UINT, day,
        "hour", G_TYPE_UINT, hour,
        "minute", G_TYPE_UINT, minute,
        "second", G_TYPE_UINT, second,
        "duration", G_TYPE_UINT, duration,
        "running-status", G_TYPE_UINT, running_status,
        "free-ca-mode", G_TYPE_BOOLEAN, free_ca_mode, NULL);
    g_free (event_name);

    if (descriptors_loop_length) {
      guint8 *event_descriptor;
      GArray *component_descriptors;
      GArray *extended_event_descriptors;
      GstMPEGDescriptor *mpegdescriptor;

      if (data + descriptors_loop_length > end - 4) {
        GST_WARNING ("PID %d invalid EIT descriptors loop length %d",
            section->pid, descriptors_loop_length);
        gst_structure_free (event);
        goto error;
      }
      mpegdescriptor =
          gst_mpeg_descriptor_parse (data, descriptors_loop_length);
      event_descriptor =
          gst_mpeg_descriptor_find (mpegdescriptor, DESC_DVB_SHORT_EVENT);
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
              get_encoding_and_convert (eventname, eventname_length),
              eventdescription_tmp =
              get_encoding_and_convert (eventdescription,
              eventdescription_length);

          gst_structure_set (event, "name", G_TYPE_STRING, eventname_tmp, NULL);
          gst_structure_set (event, "description", G_TYPE_STRING,
              eventdescription_tmp, NULL);
          g_free (eventname_tmp);
          g_free (eventdescription_tmp);
        }
      }
      extended_event_descriptors = gst_mpeg_descriptor_find_all (mpegdescriptor,
          DESC_DVB_EXTENDED_EVENT);
      if (extended_event_descriptors) {
        int i;
        guint8 *extended_descriptor;
        /*GValue extended_items = { 0 }; */
        gchar *extended_text = NULL;
        /*g_value_init (&extended_items, GST_TYPE_LIST); */
        for (i = 0; i < extended_event_descriptors->len; i++) {
          extended_descriptor = g_array_index (extended_event_descriptors,
              guint8 *, i);
          if (DESC_DVB_EXTENDED_EVENT_descriptor_number (extended_descriptor) ==
              i) {
            if (extended_text) {
              gchar *tmp;
              gchar *old_extended_text = extended_text;
              tmp = get_encoding_and_convert ((gchar *)
                  DESC_DVB_EXTENDED_EVENT_text (extended_descriptor),
                  DESC_DVB_EXTENDED_EVENT_text_length (extended_descriptor));
              extended_text = g_strdup_printf ("%s%s", extended_text, tmp);
              g_free (old_extended_text);
              g_free (tmp);
            } else {
              extended_text = get_encoding_and_convert ((gchar *)
                  DESC_DVB_EXTENDED_EVENT_text (extended_descriptor),
                  DESC_DVB_EXTENDED_EVENT_text_length (extended_descriptor));
            }
          }
        }
        if (extended_text) {
          gst_structure_set (event, "extended-text", G_TYPE_STRING,
              extended_text, NULL);
          g_free (extended_text);
        }
        g_array_free (extended_event_descriptors, TRUE);
      }

      component_descriptors = gst_mpeg_descriptor_find_all (mpegdescriptor,
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
          gboolean highdef = FALSE;
          gboolean panvectors = FALSE;
          gchar *comptype = "";

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
                  highdef = TRUE;
                  freq = 25;
                  break;
                case 0x0A:
                  widescreen = 1;
                  highdef = TRUE;
                  panvectors = TRUE;
                  freq = 25;
                  break;
                case 0x0B:
                  widescreen = 1;
                  highdef = TRUE;
                  panvectors = FALSE;
                  freq = 25;
                  break;
                case 0x0C:
                  widescreen = 2;
                  highdef = TRUE;
                  freq = 25;
                  break;
                case 0x0D:
                  widescreen = 0;
                  highdef = TRUE;
                  freq = 30;
                  break;
                case 0x0E:
                  widescreen = 1;
                  highdef = TRUE;
                  panvectors = TRUE;
                  freq = 30;
                  break;
                case 0x0F:
                  widescreen = 1;
                  highdef = TRUE;
                  panvectors = FALSE;
                  freq = 30;
                  break;
                case 0x10:
                  widescreen = 2;
                  highdef = TRUE;
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
        gst_structure_set_value (event, "components", &components);
        g_value_unset (&components);
        g_array_free (component_descriptors, TRUE);
      }
      gst_mpeg_descriptor_free (mpegdescriptor);

      descriptors = g_value_array_new (0);
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
        section->pid, data - GST_BUFFER_DATA (section->buffer),
        GST_BUFFER_SIZE (section->buffer));
    goto error;
  }

  gst_structure_id_set_value (eit, QUARK_EVENTS, &events);
  g_value_unset (&events);

  GST_DEBUG ("EIT %" GST_PTR_FORMAT, eit);

  return eit;

error:
  if (eit)
    gst_structure_free (eit);

  if (GST_VALUE_HOLDS_LIST (&events))
    g_value_unset (&events);

  return NULL;
}

void
mpegts_packetizer_clear (MpegTSPacketizer * packetizer)
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
        packetizer->streams[i] = NULL;
      }
    }
  }

  gst_adapter_clear (packetizer->adapter);
}

void
mpegts_packetizer_remove_stream (MpegTSPacketizer * packetizer, gint16 pid)
{
  MpegTSPacketizerStream *stream = packetizer->streams[pid];
  if (stream) {
    GST_INFO ("Removing stream for PID %d", pid);
    mpegts_packetizer_stream_free (stream);
    packetizer->streams[pid] = NULL;
  }
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
  gst_adapter_push (packetizer->adapter, buffer);
}

void
mpegts_try_discover_packet_size (MpegTSPacketizer * packetizer)
{
  guint8 *dest;
  int i, pos, j;
  const guint psizes[] = { MPEGTS_NORMAL_PACKETSIZE,
    MPEGTS_M2TS_PACKETSIZE,
    MPEGTS_DVB_ASI_PACKETSIZE,
    MPEGTS_ATSC_PACKETSIZE
  };
  /* wait for 3 sync bytes */
  /* so first return if there is not enough data for 4 * max packetsize */
  if (gst_adapter_available_fast (packetizer->adapter) <
      MPEGTS_MAX_PACKETSIZE * 4)
    return;
  /* check for sync bytes */
  dest = g_malloc (MPEGTS_MAX_PACKETSIZE * 4);
  gst_adapter_copy (packetizer->adapter, dest, 0, MPEGTS_MAX_PACKETSIZE * 4);
  /* find first sync byte */
  pos = -1;
  for (i = 0; i < MPEGTS_MAX_PACKETSIZE; i++) {
    if (dest[i] == 0x47) {
      for (j = 0; j < 4; j++) {
        guint packetsize = psizes[j];
        /* check each of the packet size possibilities in turn */
        if (dest[i] == 0x47 && dest[i + packetsize] == 0x47 &&
            dest[i + packetsize * 2] == 0x47 &&
            dest[i + packetsize * 3] == 0x47) {
          gchar *str;
          packetizer->know_packet_size = TRUE;
          packetizer->packet_size = packetsize;
          str =
              g_strdup_printf
              ("video/mpegts, systemstream=(boolean)true, packetsize=%d",
              packetsize);
          packetizer->caps = gst_caps_from_string ((const gchar *) str);
          g_free (str);
          pos = i;
          break;
        }
      }
      break;
    }
  }
  GST_DEBUG ("have packetsize detected: %d of %u bytes",
      packetizer->know_packet_size, packetizer->packet_size);
  /* flush to sync byte */
  if (pos > 0)
    gst_adapter_flush (packetizer->adapter, pos);
  g_free (dest);
}


gboolean
mpegts_packetizer_has_packets (MpegTSPacketizer * packetizer)
{
  if (G_UNLIKELY (packetizer->know_packet_size == FALSE)) {
    mpegts_try_discover_packet_size (packetizer);
    if (!packetizer->know_packet_size)
      return FALSE;
  }
  return gst_adapter_available (packetizer->adapter) >= packetizer->packet_size;
}

MpegTSPacketizerPacketReturn
mpegts_packetizer_next_packet (MpegTSPacketizer * packetizer,
    MpegTSPacketizerPacket * packet)
{
  guint8 sync_byte;
  guint avail;

  packet->buffer = NULL;
  if (G_UNLIKELY (!packetizer->know_packet_size)) {
    mpegts_try_discover_packet_size (packetizer);
    if (!packetizer->know_packet_size)
      return PACKET_NEED_MORE;
  }
  while ((avail = gst_adapter_available (packetizer->adapter)) >=
      packetizer->packet_size) {
    sync_byte = *gst_adapter_peek (packetizer->adapter, 1);
    if (G_UNLIKELY (sync_byte != 0x47)) {
      GST_DEBUG ("lost sync %02x", sync_byte);
      gst_adapter_flush (packetizer->adapter, 1);
      continue;
    }

    packet->buffer = gst_adapter_take_buffer (packetizer->adapter,
        packetizer->packet_size);
    packet->data_start = GST_BUFFER_DATA (packet->buffer);
    packet->data_end =
        GST_BUFFER_DATA (packet->buffer) + GST_BUFFER_SIZE (packet->buffer);
    return mpegts_packetizer_parse_packet (packetizer, packet);
  }

  return PACKET_NEED_MORE;
}

void
mpegts_packetizer_clear_packet (MpegTSPacketizer * packetizer,
    MpegTSPacketizerPacket * packet)
{
  if (packet->buffer)
    gst_buffer_unref (packet->buffer);
  memset (packet, 0, sizeof (MpegTSPacketizerPacket));
}

gboolean
mpegts_packetizer_push_section (MpegTSPacketizer * packetizer,
    MpegTSPacketizerPacket * packet, MpegTSPacketizerSection * section)
{
  gboolean res = FALSE;
  MpegTSPacketizerStream *stream;
  guint8 pointer, table_id;
  guint16 subtable_extension;
  guint section_length;
  GstBuffer *sub_buf;
  guint8 *data;

  data = packet->data;
  section->pid = packet->pid;

  if (packet->payload_unit_start_indicator == 1) {
    pointer = *data++;
    if (data + pointer > packet->data_end) {
      GST_WARNING ("PID %d PSI section pointer points past the end "
          "of the buffer", packet->pid);
      goto out;
    }

    data += pointer;
  }
  /* create a sub buffer from the start of the section (table_id and
   * section_length included) to the end */
  sub_buf = gst_buffer_create_sub (packet->buffer,
      data - GST_BUFFER_DATA (packet->buffer), packet->data_end - data);

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
    GST_DEBUG ("pid: %d table_id %d sub_table_extension %d",
        packet->pid, table_id, subtable_extension);

    section_length = GST_READ_UINT16_BE (data) & 0x0FFF;

    if (stream->continuity_counter != CONTINUITY_UNSET) {
      GST_DEBUG
          ("PID %d table_id %d sub_table_extension %d payload_unit_start_indicator set but section "
          "not complete (last_continuity: %d continuity: %d sec len %d buffer %d avail %d",
          packet->pid, table_id, subtable_extension, stream->continuity_counter,
          packet->continuity_counter, section_length, GST_BUFFER_SIZE (sub_buf),
          gst_adapter_available (stream->section_adapter));
      mpegts_packetizer_clear_section (packetizer, stream);
    } else {
      GST_DEBUG
          ("pusi set and new stream section is %d long and data we have is: %d",
          section_length, packet->data_end - packet->data);
    }
    stream->continuity_counter = packet->continuity_counter;
    stream->section_length = section_length;
    stream->section_table_id = table_id;
    gst_adapter_push (stream->section_adapter, sub_buf);

    res = TRUE;
  } else if (stream->continuity_counter != CONTINUITY_UNSET &&
      (packet->continuity_counter == stream->continuity_counter + 1 ||
          (stream->continuity_counter == MAX_CONTINUITY &&
              packet->continuity_counter == 0))) {
    stream->continuity_counter = packet->continuity_counter;
    gst_adapter_push (stream->section_adapter, sub_buf);

    res = TRUE;
  } else {
    if (stream->continuity_counter == CONTINUITY_UNSET)
      GST_DEBUG ("PID %d waiting for pusi", packet->pid);
    else
      GST_DEBUG ("PID %d section discontinuity "
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
    GST_WARNING ("section not complete");
    section->complete = FALSE;
  }

out:
  packet->data = data;
  return res;
}

static void
_init_local ()
{
  GST_DEBUG_CATEGORY_INIT (mpegts_packetizer_debug, "mpegtspacketizer", 0,
      "MPEG transport stream parser");

  QUARK_PAT = g_quark_from_string ("pat");
  QUARK_TRANSPORT_STREAM_ID = g_quark_from_string ("transport-stream-id");
  QUARK_PROGRAM_NUMBER = g_quark_from_string ("program-number");
  QUARK_PID = g_quark_from_string ("pid");
  QUARK_PROGRAMS = g_quark_from_string ("programs");

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
  QUARK_TRANSPORT_STREAM_ID = g_quark_from_string ("transport-stream-id");
  QUARK_ORIGINAL_NETWORK_ID = g_quark_from_string ("original-network-id");
  QUARK_TRANSPORTS = g_quark_from_string ("transports");

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
}

/**
 * @text: The text you want to get the encoding from
 * @start_text: Location where the beginning of the actual text is stored
 * @is_multibyte: Location where information whether it's a multibyte encoding
 * or not is stored
 * @returns: Name of encoding or NULL of encoding could not be detected.
 * 
 * The returned string should be freed with g_free () when no longer needed.
 */
static gchar *
get_encoding (const gchar * text, guint * start_text, gboolean * is_multibyte)
{
  gchar *encoding;
  guint8 firstbyte;

  g_return_val_if_fail (text != NULL, NULL);

  firstbyte = (guint8) text[0];

  if (firstbyte == 0x01) {
    encoding = g_strdup ("iso8859-5");
    *start_text = 1;
    *is_multibyte = FALSE;
  } else if (firstbyte == 0x02) {
    encoding = g_strdup ("iso8859-6");
    *start_text = 1;
    *is_multibyte = FALSE;
  } else if (firstbyte == 0x03) {
    encoding = g_strdup ("iso8859-7");
    *start_text = 1;
    *is_multibyte = FALSE;
  } else if (firstbyte == 0x04) {
    encoding = g_strdup ("iso8859-8");
    *start_text = 1;
    *is_multibyte = FALSE;
  } else if (firstbyte == 0x05) {
    encoding = g_strdup ("iso8859-9");
    *start_text = 1;
    *is_multibyte = FALSE;
  } else if (firstbyte >= 0x20) {
    encoding = g_strdup ("iso6937");
    *start_text = 0;
    *is_multibyte = FALSE;
  } else if (firstbyte == 0x10) {
    guint16 table;
    gchar table_str[6];

    text++;
    table = GST_READ_UINT16_BE (text);
    g_snprintf (table_str, 6, "%d", table);

    encoding = g_strconcat ("iso8859-", table_str, NULL);
    *start_text = 3;
    *is_multibyte = FALSE;
  } else if (firstbyte == 0x11) {
    encoding = g_strdup ("ISO-10646/UCS2");
    *start_text = 1;
    *is_multibyte = TRUE;
  } else if (firstbyte == 0x12) {
    // That's korean encoding.
    // The spec says it's encoded in KSC 5601, but iconv only knows KSC 5636.
    // Couldn't find any information about either of them.
    encoding = NULL;
    *start_text = 1;
    *is_multibyte = TRUE;
  } else {
    // reserved
    encoding = NULL;
  }

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
    const gchar * encoding, gboolean is_multibyte, GError ** error)
{
  gchar *new_text;
  GByteArray *sb;
  gint i;

  g_return_val_if_fail (text != NULL, NULL);
  g_return_val_if_fail (encoding != NULL, NULL);

  text += start;

  sb = g_byte_array_sized_new (length * 1.1);

  if (is_multibyte) {
    if (length == -1) {
      while (*text != '\0') {
        guint16 code = GST_READ_UINT16_BE (text);

        switch (code) {
          case 0xE086:{
            guint8 emph_on[] = { 0x3C, 0x00,    // <
              0x62, 0x00,       // b
              0x3E, 0x00        // >
            };
            g_byte_array_append (sb, emph_on, 6);
            break;
          }
          case 0xE087:{
            guint8 emph_on[] = { 0x3C, 0x00,    // <
              0x2F, 0x00,       // /
              0x62, 0x00,       // b
              0x3E, 0x00        // >
            };
            g_byte_array_append (sb, emph_on, 8);
            break;
          }
          case 0xE08A:{
            guint8 nl[] = { 0x0A, 0x00 };       // new line
            g_byte_array_append (sb, nl, 2);
            break;
          }
          default:
            g_byte_array_append (sb, (guint8 *) text, 2);
            break;
        }

        text += 2;
      }
    } else {
      for (i = 0; i < length; i += 2) {
        guint16 code = GST_READ_UINT16_BE (text);

        switch (code) {
          case 0xE086:{
            guint8 emph_on[] = { 0x3C, 0x00,    // <
              0x62, 0x00,       // b
              0x3E, 0x00        // >
            };
            g_byte_array_append (sb, emph_on, 6);
            break;
          }
          case 0xE087:{
            guint8 emph_on[] = { 0x3C, 0x00,    // <
              0x2F, 0x00,       // /
              0x62, 0x00,       // b
              0x3E, 0x00        // >
            };
            g_byte_array_append (sb, emph_on, 8);
            break;
          }
          case 0xE08A:{
            guint8 nl[] = { 0x0A, 0x00 };       // new line
            g_byte_array_append (sb, nl, 2);
            break;
          }
          default:
            g_byte_array_append (sb, (guint8 *) text, 2);
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
          case 0x86:
            g_byte_array_append (sb, (guint8 *) "<b>", 3);
            break;
          case 0x87:
            g_byte_array_append (sb, (guint8 *) "</b>", 4);
            break;
          case 0x8A:
            g_byte_array_append (sb, (guint8 *) "\n", 1);
            break;
          default:
            g_byte_array_append (sb, &code, 1);
            break;
        }

        text++;
      }
    } else {
      for (i = 0; i < length; i++) {
        guint8 code = (guint8) (*text);

        switch (code) {
          case 0x86:
            g_byte_array_append (sb, (guint8 *) "<b>", 3);
            break;
          case 0x87:
            g_byte_array_append (sb, (guint8 *) "</b>", 4);
            break;
          case 0x8A:
            g_byte_array_append (sb, (guint8 *) "\n", 1);
            break;
          default:
            g_byte_array_append (sb, &code, 1);
            break;
        }

        text++;
      }
    }
  }

  if (sb->len > 0) {
    new_text =
        g_convert ((gchar *) sb->data, sb->len, "utf-8", encoding, NULL, NULL,
        error);
  } else {
    new_text = g_strdup ("");
  }

  g_byte_array_free (sb, TRUE);

  return new_text;
}

static gchar *
get_encoding_and_convert (const gchar * text, guint length)
{
  GError *error = NULL;
  gchar *converted_str;
  gchar *encoding;
  guint start_text = 0;
  gboolean is_multibyte;

  g_return_val_if_fail (text != NULL, NULL);

  if (length == 0)
    return g_strdup ("");

  encoding = get_encoding (text, &start_text, &is_multibyte);

  if (encoding == NULL) {
    converted_str = g_strndup (text, length);
  } else {
    converted_str = convert_to_utf8 (text, length - start_text, start_text,
        encoding, is_multibyte, &error);
    if (error != NULL) {
      g_critical ("Could not convert string: %s", error->message);
      g_error_free (error);
      text += start_text;
      converted_str = g_strndup (text, length - start_text);
    }

    g_free (encoding);
  }

  return converted_str;
}
