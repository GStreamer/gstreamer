/* GStreamer
 *
 * ts-parser.c: sample application to display mpeg-ts info from any pipeline
 * Copyright (C) 2013
 *           Edward Hervey <bilboed@gmail.com>
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
#include "config.h"
#endif

#include <glib.h>
#include <glib-object.h>
#include <glib/gprintf.h>
#include <gst/gst.h>
#include <gst/mpegts/mpegts.h>

static const gchar *
descriptor_name (gint val)
{
  GEnumValue *en;

  en = g_enum_get_value (G_ENUM_CLASS (g_type_class_peek
          (GST_TYPE_MPEG_TS_DESCRIPTOR_TYPE)), val);
  if (en == NULL)
    /* Else try with DVB enum types */
    en = g_enum_get_value (G_ENUM_CLASS (g_type_class_peek
            (GST_TYPE_MPEG_TS_DVB_DESCRIPTOR_TYPE)), val);
  if (en == NULL)
    /* Else try with ATSC enum types */
    en = g_enum_get_value (G_ENUM_CLASS (g_type_class_peek
            (GST_TYPE_MPEG_TS_ATSC_DESCRIPTOR_TYPE)), val);
  if (en == NULL)
    /* Else try with ISB enum types */
    en = g_enum_get_value (G_ENUM_CLASS (g_type_class_peek
            (GST_TYPE_MPEG_TS_ISDB_DESCRIPTOR_TYPE)), val);
  if (en == NULL)
    /* Else try with misc enum types */
    en = g_enum_get_value (G_ENUM_CLASS (g_type_class_peek
            (GST_TYPE_MPEG_TS_MISC_DESCRIPTOR_TYPE)), val);
  if (en == NULL)
    return "UNKNOWN/PRIVATE";
  return en->value_nick;
}

static const gchar *
enum_name (GType instance_type, gint val)
{
  GEnumValue *en;

  en = g_enum_get_value (G_ENUM_CLASS (g_type_class_peek (instance_type)), val);

  if (!en)
    return "UNKNOWN/PRIVATE";
  return en->value_nick;
}

static void
dump_cable_delivery_descriptor (GstMpegTsDescriptor * desc, guint spacing)
{
  GstMpegTsCableDeliverySystemDescriptor res;

  if (gst_mpegts_descriptor_parse_cable_delivery_system (desc, &res)) {
    g_printf ("%*s Cable Delivery Descriptor\n", spacing, "");
    g_printf ("%*s   Frequency   : %d Hz\n", spacing, "", res.frequency);
    g_printf ("%*s   Outer FEC   : %d\n", spacing, "", res.outer_fec);
    g_printf ("%*s   modulation  : %d\n", spacing, "", res.modulation);
    g_printf ("%*s   Symbol rate : %d sym/s\n", spacing, "", res.symbol_rate);
    g_printf ("%*s   Inner FEC   : %d\n", spacing, "", res.fec_inner);
  }
}

static void
dump_logical_channel_descriptor (GstMpegTsDescriptor * desc, guint spacing)
{
  GstMpegTsLogicalChannelDescriptor res;
  guint i;

  if (gst_mpegts_descriptor_parse_logical_channel (desc, &res)) {
    g_printf ("%*s Logical Channel Descriptor (%d channels)\n", spacing, "",
        res.nb_channels);
    for (i = 0; i < res.nb_channels; i++) {
      GstMpegTsLogicalChannel *chann = &res.channels[i];
      g_printf ("%*s   service_id: 0x%04x, logical channel number:%4d\n",
          spacing, "", chann->service_id, chann->logical_channel_number);
    }
  }
}

static void
dump_iso_639_language (GstMpegTsDescriptor * desc, guint spacing)
{
  guint i;
  GstMpegTsISO639LanguageDescriptor res;

  if (gst_mpegts_descriptor_parse_iso_639_language (desc, &res)) {
    for (i = 0; i < res.nb_language; i++)
      g_print
          ("%*s ISO 639 Language Descriptor %c%c%c , audio_type:0x%x (%s)\n",
          spacing, "", res.language[i][0], res.language[i][1],
          res.language[i][2], res.audio_type[i],
          enum_name (GST_TYPE_MPEG_TS_ISO639_AUDIO_TYPE, res.audio_type[i]));
  }
}


static void
dump_descriptors (GPtrArray * descriptors, guint spacing)
{
  guint i;

  for (i = 0; i < descriptors->len; i++) {
    GstMpegTsDescriptor *desc = g_ptr_array_index (descriptors, i);
    g_printf ("%*s [descriptor 0x%02x (%s) length:%d]\n", spacing, "",
        desc->tag, descriptor_name (desc->tag), desc->length);
    switch (desc->tag) {
      case GST_MTS_DESC_REGISTRATION:
      {
        const guint8 *data = desc->data + 2;
#define SAFE_CHAR(a) (g_ascii_isalnum(a) ? a : '.')
        g_printf ("%*s   Registration : %c%c%c%c\n", spacing, "",
            SAFE_CHAR (data[0]), SAFE_CHAR (data[1]),
            SAFE_CHAR (data[2]), SAFE_CHAR (data[3]));

        break;
      }
      case GST_MTS_DESC_DVB_NETWORK_NAME:
      {
        gchar *network_name;
        if (gst_mpegts_descriptor_parse_dvb_network_name (desc, &network_name)) {
          g_printf ("%*s   Network Name : %s\n", spacing, "", network_name);
          g_free (network_name);
        }
        break;
      }
      case GST_MTS_DESC_DVB_CABLE_DELIVERY_SYSTEM:
        dump_cable_delivery_descriptor (desc, spacing + 2);
        break;
      case GST_MTS_DESC_DTG_LOGICAL_CHANNEL:
        dump_logical_channel_descriptor (desc, spacing + 2);
        break;
      case GST_MTS_DESC_DVB_SERVICE:
      {
        gchar *service_name, *provider_name;
        GstMpegTsDVBServiceType service_type;
        if (gst_mpegts_descriptor_parse_dvb_service (desc, &service_type,
                &service_name, &provider_name)) {
          g_printf ("%*s   Service Descriptor, type:0x%02x (%s)\n", spacing, "",
              service_type, enum_name (GST_TYPE_MPEG_TS_DVB_SERVICE_TYPE,
                  service_type));
          g_printf ("%*s      service_name  : %s\n", spacing, "", service_name);
          g_printf ("%*s      provider_name : %s\n", spacing, "",
              provider_name);
          g_free (service_name);
          g_free (provider_name);

        }
        break;
      }
      case GST_MTS_DESC_ISO_639_LANGUAGE:
        dump_iso_639_language (desc, spacing + 2);
        break;
      case GST_MTS_DESC_DVB_SHORT_EVENT:
      {
        gchar *language_code, *event_name, *text;
        if (gst_mpegts_descriptor_parse_dvb_short_event (desc, &language_code,
                &event_name, &text)) {
          g_printf ("%*s   Short Event, language_code:%s\n", spacing, "",
              language_code);
          g_printf ("%*s     event_name : %s\n", spacing, "", event_name);
          g_printf ("%*s     text       : %s\n", spacing, "", text);
          g_free (language_code);
          g_free (event_name);
          g_free (text);
        }
      }
        break;
      default:
        break;
    }
  }
}

static void
dump_pat (GstMpegTsSection * section)
{
  GPtrArray *pat = gst_mpegts_section_get_pat (section);
  guint i, len;

  len = pat->len;
  g_printf ("   %d program(s):\n", len);

  for (i = 0; i < len; i++) {
    GstMpegTsPatProgram *patp = g_ptr_array_index (pat, i);

    g_print
        ("     program_number:%6d (0x%04x), network_or_program_map_PID:0x%04x\n",
        patp->program_number, patp->program_number,
        patp->network_or_program_map_PID);
  }

  g_ptr_array_unref (pat);
}

static void
dump_pmt (GstMpegTsSection * section)
{
  const GstMpegTsPMT *pmt = gst_mpegts_section_get_pmt (section);
  guint i, len;

  g_printf ("     program_number : 0x%04x\n", section->subtable_extension);
  g_printf ("     pcr_pid        : 0x%04x\n", pmt->pcr_pid);
  dump_descriptors (pmt->descriptors, 7);
  len = pmt->streams->len;
  g_printf ("     %d Streams:\n", len);
  for (i = 0; i < len; i++) {
    GstMpegTsPMTStream *stream = g_ptr_array_index (pmt->streams, i);
    g_printf ("       pid:0x%04x , stream_type:0x%02x (%s)\n", stream->pid,
        stream->stream_type,
        enum_name (GST_TYPE_MPEG_TS_STREAM_TYPE, stream->stream_type));
    dump_descriptors (stream->descriptors, 9);
  }
}

static void
dump_eit (GstMpegTsSection * section)
{
  const GstMpegTsEIT *eit = gst_mpegts_section_get_eit (section);
  guint i, len;

  g_assert (eit);

  g_printf ("     service_id          : 0x%04x\n", section->subtable_extension);
  g_printf ("     transport_stream_id : 0x%04x\n", eit->transport_stream_id);
  g_printf ("     original_network_id : 0x%04x\n", eit->original_network_id);
  g_printf ("     segment_last_section_number:0x%02x, last_table_id:0x%02x\n",
      eit->segment_last_section_number, eit->last_table_id);
  g_printf ("     actual_stream : %s, present_following : %s\n",
      eit->actual_stream ? "TRUE" : "FALSE",
      eit->present_following ? "TRUE" : "FALSE");

  len = eit->events->len;
  g_printf ("     %d Event(s):\n", len);
  for (i = 0; i < len; i++) {
    gchar *tmp = (gchar *) "<NO TIME>";
    GstMpegTsEITEvent *event = g_ptr_array_index (eit->events, i);

    if (event->start_time)
      tmp = gst_date_time_to_iso8601_string (event->start_time);
    g_printf ("       event_id:0x%04x, start_time:%s, duration:%"
        GST_TIME_FORMAT "\n", event->event_id, tmp,
        GST_TIME_ARGS (event->duration * GST_SECOND));
    g_printf ("       running_status:0x%02x (%s), free_CA_mode:%d (%s)\n",
        event->running_status, enum_name (GST_TYPE_MPEG_TS_RUNNING_STATUS,
            event->running_status), event->free_CA_mode,
        event->free_CA_mode ? "MAYBE SCRAMBLED" : "NOT SCRAMBLED");
    if (event->start_time)
      g_free (tmp);
    dump_descriptors (event->descriptors, 9);
  }
}

static void
dump_nit (GstMpegTsSection * section)
{
  const GstMpegTsNIT *nit = gst_mpegts_section_get_nit (section);
  guint i, len;

  g_assert (nit);

  g_printf ("     network_id     : 0x%04x\n", section->subtable_extension);
  g_printf ("     actual_network : %s\n",
      nit->actual_network ? "TRUE" : "FALSE");
  dump_descriptors (nit->descriptors, 7);
  len = nit->streams->len;
  g_printf ("     %d Streams:\n", len);
  for (i = 0; i < len; i++) {
    GstMpegTsNITStream *stream = g_ptr_array_index (nit->streams, i);
    g_printf
        ("       transport_stream_id:0x%04x , original_network_id:0x%02x\n",
        stream->transport_stream_id, stream->original_network_id);
    dump_descriptors (stream->descriptors, 9);
  }
}

static void
dump_bat (GstMpegTsSection * section)
{
  const GstMpegTsBAT *bat = gst_mpegts_section_get_bat (section);
  guint i, len;

  g_assert (bat);

  g_printf ("     bouquet_id     : 0x%04x\n", section->subtable_extension);
  dump_descriptors (bat->descriptors, 7);
  len = bat->streams->len;
  g_printf ("     %d Streams:\n", len);
  for (i = 0; i < len; i++) {
    GstMpegTsBATStream *stream = g_ptr_array_index (bat->streams, i);
    g_printf
        ("       transport_stream_id:0x%04x , original_network_id:0x%02x\n",
        stream->transport_stream_id, stream->original_network_id);
    dump_descriptors (stream->descriptors, 9);
  }
}

static void
dump_sdt (GstMpegTsSection * section)
{
  const GstMpegTsSDT *sdt = gst_mpegts_section_get_sdt (section);
  guint i, len;

  g_assert (sdt);

  g_printf ("     original_network_id : 0x%04x\n", sdt->original_network_id);
  g_printf ("     actual_ts           : %s\n",
      sdt->actual_ts ? "TRUE" : "FALSE");
  len = sdt->services->len;
  g_printf ("     %d Services:\n", len);
  for (i = 0; i < len; i++) {
    GstMpegTsSDTService *service = g_ptr_array_index (sdt->services, i);
    g_print
        ("       service_id:0x%04x, EIT_schedule_flag:%d, EIT_present_following_flag:%d\n",
        service->service_id, service->EIT_schedule_flag,
        service->EIT_present_following_flag);
    g_print
        ("       running_status:0x%02x (%s), free_CA_mode:%d (%s)\n",
        service->running_status,
        enum_name (GST_TYPE_MPEG_TS_RUNNING_STATUS, service->running_status),
        service->free_CA_mode,
        service->free_CA_mode ? "MAYBE SCRAMBLED" : "NOT SCRAMBLED");
    dump_descriptors (service->descriptors, 9);
  }
}

static void
dump_tdt (GstMpegTsSection * section)
{
  GstDateTime *date = gst_mpegts_section_get_tdt (section);

  if (date) {
    gchar *str = gst_date_time_to_iso8601_string (date);
    g_printf ("     utc_time : %s\n", str);
    g_free (str);
    gst_date_time_unref (date);
  } else {
    g_printf ("     No utc_time present\n");
  }
}

static void
dump_tot (GstMpegTsSection * section)
{
  const GstMpegTsTOT *tot = gst_mpegts_section_get_tot (section);
  gchar *str = gst_date_time_to_iso8601_string (tot->utc_time);

  g_printf ("     utc_time : %s\n", str);
  dump_descriptors (tot->descriptors, 7);
  g_free (str);
}

static void
dump_section (GstMpegTsSection * section)
{
  switch (GST_MPEGTS_SECTION_TYPE (section)) {
    case GST_MPEGTS_SECTION_PAT:
      dump_pat (section);
      break;
    case GST_MPEGTS_SECTION_PMT:
      dump_pmt (section);
      break;
    case GST_MPEGTS_SECTION_TDT:
      dump_tdt (section);
      break;
    case GST_MPEGTS_SECTION_TOT:
      dump_tot (section);
      break;
    case GST_MPEGTS_SECTION_SDT:
      dump_sdt (section);
      break;
    case GST_MPEGTS_SECTION_NIT:
      dump_nit (section);
      break;
    case GST_MPEGTS_SECTION_BAT:
      dump_bat (section);
      break;
    case GST_MPEGTS_SECTION_EIT:
      dump_eit (section);
      break;
    default:
      g_printf ("     Unknown section type\n");
      break;
  }
}

static void
_on_bus_message (GstBus * bus, GstMessage * message, GMainLoop * mainloop)
{
  /* g_printf ("Got message %s\n", GST_MESSAGE_TYPE_NAME (message)); */

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:
    case GST_MESSAGE_EOS:
      g_main_loop_quit (mainloop);
      break;
    case GST_MESSAGE_ELEMENT:
    {
      GstMpegTsSection *section;
      if ((section = gst_message_parse_mpegts_section (message))) {
        g_print
            ("Got section: PID:0x%04x type:%s (table_id 0x%02x (%s)) at offset %"
            G_GUINT64_FORMAT "\n", section->pid,
            enum_name (GST_TYPE_MPEG_TS_SECTION_TYPE, section->section_type),
            section->table_id, enum_name (GST_TYPE_MPEG_TS_SECTION_TABLE_ID,
                section->table_id), section->offset);
        if (!section->short_section) {
          g_print
              ("   subtable_extension:0x%04x, version_number:0x%02x\n",
              section->subtable_extension, section->version_number);
          g_print
              ("   section_number:0x%02x last_section_number:0x%02x crc:0x%08x\n",
              section->section_number, section->last_section_number,
              section->crc);
        }
        dump_section (section);
        g_printf ("\n\n");
        gst_mpegts_section_unref (section);
      }
      break;
    }
    default:
      break;
  }
}

int
main (int argc, gchar ** argv)
{
  GstElement *pipeline = NULL;
  GError *error = NULL;
  GstBus *bus;
  GMainLoop *mainloop;

  gst_init (&argc, &argv);

  gst_mpegts_initialize ();

  pipeline = gst_parse_launchv ((const gchar **) &argv[1], &error);
  if (error) {
    g_printf ("pipeline could not be constructed: %s\n", error->message);
    g_error_free (error);
    return 1;
  }

  /* Hack: ensure all enum type classes are loaded */
  g_type_class_ref (GST_TYPE_MPEG_TS_SECTION_TYPE);
  g_type_class_ref (GST_TYPE_MPEG_TS_SECTION_TABLE_ID);
  g_type_class_ref (GST_TYPE_MPEG_TS_RUNNING_STATUS);
  g_type_class_ref (GST_TYPE_MPEG_TS_DESCRIPTOR_TYPE);
  g_type_class_ref (GST_TYPE_MPEG_TS_DVB_DESCRIPTOR_TYPE);
  g_type_class_ref (GST_TYPE_MPEG_TS_ATSC_DESCRIPTOR_TYPE);
  g_type_class_ref (GST_TYPE_MPEG_TS_ISDB_DESCRIPTOR_TYPE);
  g_type_class_ref (GST_TYPE_MPEG_TS_MISC_DESCRIPTOR_TYPE);
  g_type_class_ref (GST_TYPE_MPEG_TS_ISO639_AUDIO_TYPE);
  g_type_class_ref (GST_TYPE_MPEG_TS_DVB_SERVICE_TYPE);
  g_type_class_ref (GST_TYPE_MPEG_TS_STREAM_TYPE);

  mainloop = g_main_loop_new (NULL, FALSE);

  /* Put a bus handler */
  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message", (GCallback) _on_bus_message, mainloop);

  /* Start pipeline */
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  g_main_loop_run (mainloop);

  gst_element_set_state (pipeline, GST_STATE_NULL);

  gst_object_unref (pipeline);
  gst_object_unref (bus);

  return 0;
}
