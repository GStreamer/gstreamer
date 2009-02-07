/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

#ifndef __GST_QTDEMUX_DUMP_H__
#define __GST_QTDEMUX_DUMP_H__

#include <gst/gst.h>
#include <qtdemux.h>

G_BEGIN_DECLS

void qtdemux_dump_mvhd (GstQTDemux * qtdemux, guint8 * buffer, int depth);
void qtdemux_dump_tkhd (GstQTDemux * qtdemux, guint8 * buffer, int depth);
void qtdemux_dump_elst (GstQTDemux * qtdemux, guint8 * buffer, int depth);
void qtdemux_dump_mdhd (GstQTDemux * qtdemux, guint8 * buffer, int depth);
void qtdemux_dump_hdlr (GstQTDemux * qtdemux, guint8 * buffer, int depth);
void qtdemux_dump_vmhd (GstQTDemux * qtdemux, guint8 * buffer, int depth);
void qtdemux_dump_dref (GstQTDemux * qtdemux, guint8 * buffer, int depth);
void qtdemux_dump_stsd (GstQTDemux * qtdemux, guint8 * buffer, int depth);
void qtdemux_dump_stts (GstQTDemux * qtdemux, guint8 * buffer, int depth);
void qtdemux_dump_stss (GstQTDemux * qtdemux, guint8 * buffer, int depth);
void qtdemux_dump_stps (GstQTDemux * qtdemux, guint8 * buffer, int depth);
void qtdemux_dump_stsc (GstQTDemux * qtdemux, guint8 * buffer, int depth);
void qtdemux_dump_stsz (GstQTDemux * qtdemux, guint8 * buffer, int depth);
void qtdemux_dump_stco (GstQTDemux * qtdemux, guint8 * buffer, int depth);
void qtdemux_dump_co64 (GstQTDemux * qtdemux, guint8 * buffer, int depth);
void qtdemux_dump_dcom (GstQTDemux * qtdemux, guint8 * buffer, int depth);
void qtdemux_dump_cmvd (GstQTDemux * qtdemux, guint8 * buffer, int depth);
void qtdemux_dump_ctts (GstQTDemux * qtdemux, guint8 * buffer, int depth);
void qtdemux_dump_unknown (GstQTDemux * qtdemux, guint8 * buffer, int depth);

void qtdemux_node_dump (GstQTDemux * qtdemux, GNode * node);

G_END_DECLS

#endif /* __GST_QTDEMUX_DUMP_H__ */
