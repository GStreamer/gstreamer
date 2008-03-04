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
#include "gstmpegdesc.h"

GST_DEBUG_CATEGORY_STATIC (mpegts_packetizer_debug);
#define GST_CAT_DEFAULT mpegts_packetizer_debug

G_DEFINE_TYPE (MpegTSPacketizer, mpegts_packetizer, G_TYPE_OBJECT);

static void mpegts_packetizer_dispose (GObject * object);
static void mpegts_packetizer_finalize (GObject * object);

#define CONTINUITY_UNSET 255
#define MAX_CONTINUITY 15
#define VERSION_NUMBER_NOTSET 255

typedef struct
{
  guint8 table_id;
  /* the spec says sub_table_extension is the fourth and fifth byte of a 
   * section when the section_syntax_indicator is set to a value of "1". If 
   * section_syntax_indicator is 0, sub_table_extension will be set to 0 */
  guint16 subtable_extension;
  guint8 version_number;
} MpegTSPacketizerStreamSubtable;

typedef struct
{
  guint continuity_counter;
  GstAdapter *section_adapter;
  guint section_length;
  GSList *subtables;
} MpegTSPacketizerStream;

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
  subtable->version_number = VERSION_NUMBER_NOTSET;
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
  if ((data[0] & 0x80) == 0)
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

  pat_info = gst_structure_new ("pat",
      "transport-stream-id", G_TYPE_UINT, transport_stream_id, NULL);
  g_value_init (&entries, GST_TYPE_LIST);
  /* stop at the CRC */
  end = GST_BUFFER_DATA (section->buffer) + GST_BUFFER_SIZE (section->buffer);
  while (data < end - 4) {
    program_number = GST_READ_UINT16_BE (data);
    data += 2;

    pmt_pid = GST_READ_UINT16_BE (data) & 0x1FFF;
    data += 2;

    struct_name = g_strdup_printf ("program-%d", program_number);
    entry = gst_structure_new (struct_name,
        "program-number", G_TYPE_UINT, program_number,
        "pid", G_TYPE_UINT, pmt_pid, NULL);
    g_free (struct_name);

    g_value_init (&value, GST_TYPE_STRUCTURE);
    g_value_take_boxed (&value, entry);
    gst_value_list_append_value (&entries, &value);
    g_value_unset (&value);
  }

  gst_structure_set_value (pat_info, "programs", &entries);
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

  struct_name = g_strdup ("pmt");
  pmt = gst_structure_new (struct_name,
      "program-number", G_TYPE_UINT, program_number,
      "pcr-pid", G_TYPE_UINT, pcr_pid,
      "version-number", G_TYPE_UINT, section->version_number, NULL);
  g_free (struct_name);

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

    gst_structure_set (pmt, "descriptors", G_TYPE_VALUE_ARRAY, descriptors,
        NULL);
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
    stream_info = gst_structure_new (struct_name,
        "pid", G_TYPE_UINT, pid, "stream-type", G_TYPE_UINT, stream_type, NULL);
    g_free (struct_name);

    if (stream_info_length) {
      descriptors = g_value_array_new (0);
      if (!mpegts_packetizer_parse_descriptors (packetizer,
              &data, data + stream_info_length, descriptors)) {
        g_value_unset (&programs);
        gst_structure_free (stream_info);
        g_value_array_free (descriptors);
        goto error;
      }

      gst_structure_set (stream_info,
          "descriptors", G_TYPE_VALUE_ARRAY, descriptors, NULL);
      g_value_array_free (descriptors);
    }

    g_value_init (&stream_value, GST_TYPE_STRUCTURE);
    g_value_take_boxed (&stream_value, stream_info);
    gst_value_list_append_value (&programs, &stream_value);
    g_value_unset (&stream_value);
  }

  gst_structure_set_value (pmt, "streams", &programs);
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
  gchar *dbg_str;

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

  nit = gst_structure_new ("nit",
      "network-id", G_TYPE_UINT, network_id,
      "version-number", G_TYPE_UINT, section->version_number,
      "current-next-indicator", G_TYPE_UINT, section->current_next_indicator,
      "actual-network", G_TYPE_BOOLEAN, section->table_id == 0x40, NULL);

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
      if (networkname[0] < 0x20) {
        networkname_length -= 1;
        networkname += 1;
      }
      networkname_tmp = g_strndup (networkname, networkname_length);
      gst_structure_set (nit, "network-name", G_TYPE_STRING, networkname_tmp,
          NULL);
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

    gst_structure_set (nit, "descriptors", G_TYPE_VALUE_ARRAY, descriptors,
        NULL);
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
    transport = gst_structure_new (transport_name,
        "transport-stream-id", G_TYPE_UINT, transport_stream_id,
        "original-network-id", G_TYPE_UINT, original_network_id, NULL);
    g_free (transport_name);

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
        /* guard is 32 if 0, 16 if 1, 8 if 2, 4 if 4 */
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
          case 4:
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
        guint32 frequency =
            10 * ((frequency_bcd[3] & 0x0F) +
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
      gst_mpeg_descriptor_free (mpegdescriptor);

      descriptors = g_value_array_new (0);
      if (!mpegts_packetizer_parse_descriptors (packetizer,
              &data, data + descriptors_loop_length, descriptors)) {
        gst_structure_free (transport);
        g_value_array_free (descriptors);
        goto error;
      }

      gst_structure_set (transport, "descriptors", G_TYPE_VALUE_ARRAY,
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

  gst_structure_set_value (nit, "transports", &transports);
  g_value_unset (&transports);

  dbg_str = gst_structure_to_string (nit);
  GST_DEBUG ("NIT %s", dbg_str);
  g_free (dbg_str);

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
  gchar *dbg_str;

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

  sdt = gst_structure_new ("sdt",
      "transport-stream-id", G_TYPE_UINT, transport_stream_id,
      "version-number", G_TYPE_UINT, section->version_number,
      "current-next-indicator", G_TYPE_UINT, section->current_next_indicator,
      "original-network-id", G_TYPE_UINT, original_network_id,
      "actual-transport-stream", G_TYPE_BOOLEAN, section->table_id == 0x42,
      NULL);

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

    /* reserved */
    data += 1;

    tmp = GST_READ_UINT16_BE (data);
    data += 2;

    EIT_schedule = (tmp >> 15);
    EIT_present_following = (tmp >> 14) & 0x01;
    running_status = (tmp >> 5) & 0x03;
    scrambled = (tmp >> 4) & 0x01;
    descriptors_loop_length = tmp & 0x0FFF;

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
          if (servicename[0] < 0x20) {
            servicename_length -= 1;
            servicename += 1;
          }
          if (serviceprovider_name[0] < 0x20) {
            serviceprovider_name_length -= 1;
            serviceprovider_name += 1;
          }
          servicename_tmp = g_strndup (servicename, servicename_length);
          serviceprovider_name_tmp =
              g_strndup (serviceprovider_name, serviceprovider_name_length);
          gst_structure_set (service, "name", G_TYPE_STRING, servicename_tmp,
              NULL);
          gst_structure_set (service, "provider-name", G_TYPE_STRING,
              serviceprovider_name_tmp, NULL);
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

      gst_structure_set (service, "descriptors", G_TYPE_VALUE_ARRAY,
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

  gst_structure_set_value (sdt, "services", &services);
  g_value_unset (&services);

  dbg_str = gst_structure_to_string (sdt);
  g_free (dbg_str);

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
  gchar *dbg_str, *event_name;
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

  eit = gst_structure_new ("eit",
      "version-number", G_TYPE_UINT, section->version_number,
      "current-next-indicator", G_TYPE_UINT, section->current_next_indicator,
      "service-id", G_TYPE_UINT, service_id,
      "transport-stream-id", G_TYPE_UINT, transport_stream_id,
      "original-network-id", G_TYPE_UINT, original_network_id,
      "segment-last-section-number", G_TYPE_UINT, segment_last_section_number,
      "last-table-id", G_TYPE_UINT, last_table_id, NULL);

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
          if (eventname[0] < 0x20) {
            eventname_length -= 1;
            eventname += 1;
          }
          if (eventdescription[0] < 0x20) {
            eventdescription_length -= 1;
            eventdescription += 1;
          }
          eventname_tmp = g_strndup (eventname, eventname_length),
              eventdescription_tmp =
              g_strndup (eventdescription, eventdescription_length);

          gst_structure_set (event, "name", G_TYPE_STRING, eventname_tmp, NULL);
          gst_structure_set (event, "description", G_TYPE_STRING,
              eventdescription_tmp, NULL);
          g_free (eventname_tmp);
          g_free (eventdescription_tmp);
        }
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
                  G_TYPE_BOOLEAN, TRUE, "frequency", G_TYPE_INT, freq, NULL);
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
                  comptype, NULL);
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
                  comptype, NULL);
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
      gst_structure_set (event, "descriptors", G_TYPE_VALUE_ARRAY, descriptors,
          NULL);
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

  gst_structure_set_value (eit, "events", &events);
  g_value_unset (&events);

  dbg_str = gst_structure_to_string (eit);
  GST_DEBUG ("EIT %s", dbg_str);
  g_free (dbg_str);

  return eit;

error:
  if (eit)
    gst_structure_free (eit);

  if (GST_VALUE_HOLDS_LIST (&events))
    g_value_unset (&events);

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
  guint16 subtable_extension;
  guint section_length;
  GstBuffer *sub_buf;
  guint8 *data;

  g_return_val_if_fail (GST_IS_MPEGTS_PACKETIZER (packetizer), FALSE);
  g_return_val_if_fail (packet != NULL, FALSE);
  g_return_val_if_fail (section != NULL, FALSE);

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

  stream = (MpegTSPacketizerStream *) g_hash_table_lookup (packetizer->streams,
      GINT_TO_POINTER ((gint) packet->pid));
  if (stream == NULL) {
    stream = mpegts_packetizer_stream_new ();
    g_hash_table_insert (packetizer->streams,
        GINT_TO_POINTER ((gint) packet->pid), stream);
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

void
mpegts_packetizer_init_debug ()
{
  GST_DEBUG_CATEGORY_INIT (mpegts_packetizer_debug, "mpegtspacketizer", 0,
      "MPEG transport stream parser");
}
