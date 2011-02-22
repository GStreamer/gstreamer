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

#define MPEGTS_AFC_PCR_FLAG	0x10
#define MPEGTS_AFC_OPCR_FLAG	0x08

G_BEGIN_DECLS

#define GST_TYPE_MPEGTS_PACKETIZER \
  (mpegts_packetizer_get_type())
#define GST_MPEGTS_PACKETIZER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MPEGTS_PACKETIZER,MpegTSPacketizer2))
#define GST_MPEGTS_PACKETIZER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MPEGTS_PACKETIZER,MpegTSPacketizer2Class))
#define GST_IS_MPEGTS_PACKETIZER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MPEGTS_PACKETIZER))
#define GST_IS_MPEGTS_PACKETIZER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MPEGTS_PACKETIZER))

typedef struct _MpegTSPacketizer2 MpegTSPacketizer2;
typedef struct _MpegTSPacketizer2Class MpegTSPacketizer2Class;

typedef struct
{
  guint continuity_counter;
  GstAdapter *section_adapter;
  guint8 section_table_id;
  guint section_length;
  GSList *subtables;
  guint64 offset;
} MpegTSPacketizerStream;

struct _MpegTSPacketizer2 {
  GObject object;

  GstAdapter *adapter;
  /* streams hashed by pid */
  MpegTSPacketizerStream **streams;
  gboolean disposed;
  gboolean know_packet_size;
  guint16 packet_size;
  GstCaps *caps;

  /* current offset of the tip of the adapter */
  guint64 offset;
  gboolean empty;
};

struct _MpegTSPacketizer2Class {
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

  guint8 afc_flags;
  guint64 pcr;
  guint64 opcr;
  guint64 offset;
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
  guint32 crc;
} MpegTSPacketizerSection; 

typedef struct
{
  guint8 table_id;
  /* the spec says sub_table_extension is the fourth and fifth byte of a 
   * section when the section_syntax_indicator is set to a value of "1". If 
   * section_syntax_indicator is 0, sub_table_extension will be set to 0 */
  guint16 subtable_extension;
  guint8 version_number;
  guint32 crc;
} MpegTSPacketizerStreamSubtable;

typedef enum {
  PACKET_BAD       = FALSE,
  PACKET_OK        = TRUE,
  PACKET_NEED_MORE
} MpegTSPacketizerPacketReturn;

GType mpegts_packetizer_get_type(void);

MpegTSPacketizer2 *mpegts_packetizer_new (void);
void mpegts_packetizer_clear (MpegTSPacketizer2 *packetizer);
void mpegts_packetizer_flush (MpegTSPacketizer2 *packetizer);
void mpegts_packetizer_push (MpegTSPacketizer2 *packetizer, GstBuffer *buffer);
gboolean mpegts_packetizer_has_packets (MpegTSPacketizer2 *packetizer);
MpegTSPacketizerPacketReturn mpegts_packetizer_next_packet (MpegTSPacketizer2 *packetizer,
  MpegTSPacketizerPacket *packet);
void mpegts_packetizer_clear_packet (MpegTSPacketizer2 *packetizer,
  MpegTSPacketizerPacket *packet);
void mpegts_packetizer_remove_stream(MpegTSPacketizer2 *packetizer,
  gint16 pid);

gboolean mpegts_packetizer_push_section (MpegTSPacketizer2 *packetzer,
  MpegTSPacketizerPacket *packet, MpegTSPacketizerSection *section);
GstStructure *mpegts_packetizer_parse_pat (MpegTSPacketizer2 *packetizer,
  MpegTSPacketizerSection *section);
GstStructure *mpegts_packetizer_parse_pmt (MpegTSPacketizer2 *packetizer,
  MpegTSPacketizerSection *section);
GstStructure *mpegts_packetizer_parse_nit (MpegTSPacketizer2 *packetizer,
  MpegTSPacketizerSection *section);
GstStructure *mpegts_packetizer_parse_sdt (MpegTSPacketizer2 *packetizer,
  MpegTSPacketizerSection *section);
GstStructure *mpegts_packetizer_parse_eit (MpegTSPacketizer2 *packetizer,
  MpegTSPacketizerSection *section);
GstStructure *mpegts_packetizer_parse_tdt (MpegTSPacketizer2 *packetizer,
  MpegTSPacketizerSection *section);
guint64 mpegts_packetizer_compute_pcr(const guint8 * data);

G_END_DECLS

#endif /* GST_MPEGTS_PACKETIZER_H */
