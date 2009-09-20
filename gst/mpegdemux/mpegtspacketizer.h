/*
 * mpegtspacketizer.h - 
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

#ifndef GST_MPEGTS_PACKETIZER_H
#define GST_MPEGTS_PACKETIZER_H

#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <glib.h>

#define MPEGTS_NORMAL_PACKETSIZE  188
#define MPEGTS_M2TS_PACKETSIZE    192
#define MPEGTS_DVB_ASI_PACKETSIZE 204
#define MPEGTS_ATSC_PACKETSIZE    208

#define MPEGTS_MIN_PACKETSIZE MPEGTS_NORMAL_PACKETSIZE
#define MPEGTS_MAX_PACKETSIZE MPEGTS_ATSC_PACKETSIZE

G_BEGIN_DECLS

#define GST_TYPE_MPEGTS_PACKETIZER \
  (mpegts_packetizer_get_type())
#define GST_MPEGTS_PACKETIZER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MPEGTS_PACKETIZER,MpegTSPacketizer))
#define GST_MPEGTS_PACKETIZER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MPEGTS_PACKETIZER,MpegTSPacketizerClass))
#define GST_IS_MPEGTS_PACKETIZER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MPEGTS_PACKETIZER))
#define GST_IS_MPEGTS_PACKETIZER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MPEGTS_PACKETIZER))


typedef struct _MpegTSPacketizer MpegTSPacketizer;
typedef struct _MpegTSPacketizerClass MpegTSPacketizerClass;

typedef struct
{
  guint continuity_counter;
  GstAdapter *section_adapter;
  guint8 section_table_id;
  guint section_length;
  GSList *subtables;
} MpegTSPacketizerStream;

struct _MpegTSPacketizer {
  GObject object;

  GstAdapter *adapter;
  /* streams hashed by pid */
  MpegTSPacketizerStream **streams;
  gboolean disposed;
  gboolean know_packet_size;
  guint16 packet_size;
  GstCaps *caps;
};

struct _MpegTSPacketizerClass {
  GObjectClass object_class;
};

typedef struct
{
  GstBuffer *buffer;
  gint16 pid;
  guint8 payload_unit_start_indicator;
  guint8 adaptation_field_control;
  guint8 continuity_counter;
  guint8 *payload;

  guint8 *data_start;
  guint8 *data_end;
  guint8 *data;

} MpegTSPacketizerPacket;

typedef struct
{
  gboolean complete;
  GstBuffer *buffer;
  gint16 pid;
  guint8 table_id;
  guint16 subtable_extension;
  guint section_length;
  guint8 version_number;
  guint8 current_next_indicator;
} MpegTSPacketizerSection; 

typedef struct
{
  guint8 table_id;
  /* the spec says sub_table_extension is the fourth and fifth byte of a 
   * section when the section_syntax_indicator is set to a value of "1". If 
   * section_syntax_indicator is 0, sub_table_extension will be set to 0 */
  guint16 subtable_extension;
  guint8 version_number;
} MpegTSPacketizerStreamSubtable;

typedef enum {
  PACKET_BAD       = FALSE,
  PACKET_OK        = TRUE,
  PACKET_NEED_MORE
} MpegTSPacketizerPacketReturn;

GType gst_mpegts_packetizer_get_type(void);

MpegTSPacketizer *mpegts_packetizer_new ();
void mpegts_packetizer_clear (MpegTSPacketizer *packetizer);
void mpegts_packetizer_push (MpegTSPacketizer *packetizer, GstBuffer *buffer);
gboolean mpegts_packetizer_has_packets (MpegTSPacketizer *packetizer);
MpegTSPacketizerPacketReturn mpegts_packetizer_next_packet (MpegTSPacketizer *packetizer,
  MpegTSPacketizerPacket *packet);
void mpegts_packetizer_clear_packet (MpegTSPacketizer *packetizer,
  MpegTSPacketizerPacket *packet);
void mpegts_packetizer_remove_stream(MpegTSPacketizer *packetizer, 
  gint16 pid);

gboolean mpegts_packetizer_push_section (MpegTSPacketizer *packetzer,
  MpegTSPacketizerPacket *packet, MpegTSPacketizerSection *section);
GstStructure *mpegts_packetizer_parse_pat (MpegTSPacketizer *packetizer,
  MpegTSPacketizerSection *section);
GstStructure *mpegts_packetizer_parse_pmt (MpegTSPacketizer *packetizer,
  MpegTSPacketizerSection *section);
GstStructure *mpegts_packetizer_parse_nit (MpegTSPacketizer *packetizer,
  MpegTSPacketizerSection *section);
GstStructure *mpegts_packetizer_parse_sdt (MpegTSPacketizer *packetizer,
  MpegTSPacketizerSection *section);
GstStructure *mpegts_packetizer_parse_eit (MpegTSPacketizer *packetizer,
  MpegTSPacketizerSection *section);

G_END_DECLS

#endif /* GST_MPEGTS_PACKETIZER_H */
