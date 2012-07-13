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

#include "gstmpegdefs.h"

#define MPEGTS_NORMAL_PACKETSIZE  188
#define MPEGTS_M2TS_PACKETSIZE    192
#define MPEGTS_DVB_ASI_PACKETSIZE 204
#define MPEGTS_ATSC_PACKETSIZE    208

#define MPEGTS_MIN_PACKETSIZE MPEGTS_NORMAL_PACKETSIZE
#define MPEGTS_MAX_PACKETSIZE MPEGTS_ATSC_PACKETSIZE

#define MPEGTS_AFC_PCR_FLAG	0x10
#define MPEGTS_AFC_OPCR_FLAG	0x08

#define MAX_WINDOW 512

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
typedef struct _MpegTSPacketizerPrivate MpegTSPacketizerPrivate;

typedef struct
{
  guint   continuity_counter;

  /* Section data (reused) */
  guint8 *section_data;
  /* Expected length of the section */
  guint   section_length;
  /* Allocated length of section_data */
  guint   section_allocated;
  /* Current offset in section_data */
  guint16 section_offset;
  /* table_id of the pending section_data */
  guint8  section_table_id;

  GSList *subtables;

  /* Upstream offset of the data contained in the section */
  guint64 offset;
} MpegTSPacketizerStream;

struct _MpegTSPacketizer2 {
  GObject     parent;

  GstAdapter *adapter;
  /* streams hashed by pid */
  /* FIXME : be more memory efficient (see how it's done in mpegtsbase) */
  MpegTSPacketizerStream **streams;
  gboolean    disposed;
  gboolean    know_packet_size;
  guint16     packet_size;
  GstCaps    *caps;

  /* current offset of the tip of the adapter */
  guint64  offset;
  gboolean empty;

  /* clock skew calculation */
  gboolean       calculate_skew;

  /* offset/bitrate calculator */
  gboolean       calculate_offset;

  MpegTSPacketizerPrivate *priv;
};

struct _MpegTSPacketizer2Class {
  GObjectClass object_class;
};

typedef struct
{
  gint16  pid;
  guint8  payload_unit_start_indicator;
  guint8  adaptation_field_control;
  guint8  continuity_counter;
  guint8 *payload;

  guint8 *data_start;
  guint8 *data_end;
  guint8 *data;

  guint8  afc_flags;
  guint64 pcr;
  guint64 opcr;
  guint64 offset;
  GstClockTime origts;
} MpegTSPacketizerPacket;

typedef struct
{
  gboolean complete;
  /* GstBuffer *buffer; */
  guint8  *data;
  guint    section_length;
  guint64  offset;

  gint16   pid;
  guint8   table_id;
  guint16  subtable_extension;
  guint8   version_number;
  guint8   current_next_indicator;

  guint32  crc;
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

G_GNUC_INTERNAL GType mpegts_packetizer_get_type(void);

G_GNUC_INTERNAL MpegTSPacketizer2 *mpegts_packetizer_new (void);
G_GNUC_INTERNAL void mpegts_packetizer_clear (MpegTSPacketizer2 *packetizer);
G_GNUC_INTERNAL void mpegts_packetizer_flush (MpegTSPacketizer2 *packetizer);
G_GNUC_INTERNAL void mpegts_packetizer_push (MpegTSPacketizer2 *packetizer, GstBuffer *buffer);
G_GNUC_INTERNAL gboolean mpegts_packetizer_has_packets (MpegTSPacketizer2 *packetizer);
G_GNUC_INTERNAL MpegTSPacketizerPacketReturn mpegts_packetizer_next_packet (MpegTSPacketizer2 *packetizer,
  MpegTSPacketizerPacket *packet);
G_GNUC_INTERNAL MpegTSPacketizerPacketReturn
mpegts_packetizer_process_next_packet(MpegTSPacketizer2 * packetizer);
G_GNUC_INTERNAL void mpegts_packetizer_clear_packet (MpegTSPacketizer2 *packetizer,
				     MpegTSPacketizerPacket *packet);
G_GNUC_INTERNAL void mpegts_packetizer_remove_stream(MpegTSPacketizer2 *packetizer,
  gint16 pid);

G_GNUC_INTERNAL gboolean mpegts_packetizer_push_section (MpegTSPacketizer2 *packetzer,
  MpegTSPacketizerPacket *packet, MpegTSPacketizerSection *section);
G_GNUC_INTERNAL GstStructure *mpegts_packetizer_parse_cat (MpegTSPacketizer2 *packetizer,
  MpegTSPacketizerSection *section);
G_GNUC_INTERNAL GstStructure *mpegts_packetizer_parse_pat (MpegTSPacketizer2 *packetizer,
  MpegTSPacketizerSection *section);
G_GNUC_INTERNAL GstStructure *mpegts_packetizer_parse_pmt (MpegTSPacketizer2 *packetizer,
  MpegTSPacketizerSection *section);
G_GNUC_INTERNAL GstStructure *mpegts_packetizer_parse_nit (MpegTSPacketizer2 *packetizer,
  MpegTSPacketizerSection *section);
G_GNUC_INTERNAL GstStructure *mpegts_packetizer_parse_sdt (MpegTSPacketizer2 *packetizer,
  MpegTSPacketizerSection *section);
G_GNUC_INTERNAL GstStructure *mpegts_packetizer_parse_eit (MpegTSPacketizer2 *packetizer,
  MpegTSPacketizerSection *section);
G_GNUC_INTERNAL GstStructure *mpegts_packetizer_parse_tdt (MpegTSPacketizer2 *packetizer,
  MpegTSPacketizerSection *section);
G_GNUC_INTERNAL GstStructure *mpegts_packetizer_parse_tot (MpegTSPacketizer2 *packetizer,
  MpegTSPacketizerSection *section);

/* Only valid if calculate_offset is TRUE */
G_GNUC_INTERNAL guint mpegts_packetizer_get_seen_pcr (MpegTSPacketizer2 *packetizer);

G_GNUC_INTERNAL GstClockTime
mpegts_packetizer_offset_to_ts (MpegTSPacketizer2 * packetizer,
				guint64 offset, guint16 pcr_pid);
G_GNUC_INTERNAL guint64
mpegts_packetizer_ts_to_offset (MpegTSPacketizer2 * packetizer,
				GstClockTime ts, guint16 pcr_pid);
G_GNUC_INTERNAL GstClockTime
mpegts_packetizer_pts_to_ts (MpegTSPacketizer2 * packetizer,
			     GstClockTime pts, guint16 pcr_pid);
G_GNUC_INTERNAL void
mpegts_packetizer_set_reference_offset (MpegTSPacketizer2 * packetizer,
					guint64 refoffset);
G_END_DECLS

#endif /* GST_MPEGTS_PACKETIZER_H */
