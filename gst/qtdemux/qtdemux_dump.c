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

#include "qtdemux_types.h"
#include "qtdemux_dump.h"

#if 0
#define qtdemux_dump_mem(a,b)  gst_util_dump_mem(a,b)
#else
#define qtdemux_dump_mem(a,b)   /* */
#endif

void
qtdemux_dump_mvhd (GstQTDemux * qtdemux, guint8 * buffer, int depth)
{
  GST_LOG ("%*s  version/flags: %08x", depth, "", QT_UINT32 (buffer + 8));
  GST_LOG ("%*s  creation time: %u", depth, "", QT_UINT32 (buffer + 12));
  GST_LOG ("%*s  modify time:   %u", depth, "", QT_UINT32 (buffer + 16));
  GST_LOG ("%*s  time scale:    1/%u sec", depth, "", QT_UINT32 (buffer + 20));
  GST_LOG ("%*s  duration:      %u", depth, "", QT_UINT32 (buffer + 24));
  GST_LOG ("%*s  pref. rate:    %g", depth, "", QT_FP32 (buffer + 28));
  GST_LOG ("%*s  pref. volume:  %g", depth, "", QT_FP16 (buffer + 32));
  GST_LOG ("%*s  preview time:  %u", depth, "", QT_UINT32 (buffer + 80));
  GST_LOG ("%*s  preview dur.:  %u", depth, "", QT_UINT32 (buffer + 84));
  GST_LOG ("%*s  poster time:   %u", depth, "", QT_UINT32 (buffer + 88));
  GST_LOG ("%*s  select time:   %u", depth, "", QT_UINT32 (buffer + 92));
  GST_LOG ("%*s  select dur.:   %u", depth, "", QT_UINT32 (buffer + 96));
  GST_LOG ("%*s  current time:  %u", depth, "", QT_UINT32 (buffer + 100));
  GST_LOG ("%*s  next track ID: %d", depth, "", QT_UINT32 (buffer + 104));
}

void
qtdemux_dump_tkhd (GstQTDemux * qtdemux, guint8 * buffer, int depth)
{
  GST_LOG ("%*s  version/flags: %08x", depth, "", QT_UINT32 (buffer + 8));
  GST_LOG ("%*s  creation time: %u", depth, "", QT_UINT32 (buffer + 12));
  GST_LOG ("%*s  modify time:   %u", depth, "", QT_UINT32 (buffer + 16));
  GST_LOG ("%*s  track ID:      %u", depth, "", QT_UINT32 (buffer + 20));
  GST_LOG ("%*s  duration:      %u", depth, "", QT_UINT32 (buffer + 28));
  GST_LOG ("%*s  layer:         %u", depth, "", QT_UINT16 (buffer + 36));
  GST_LOG ("%*s  alt group:     %u", depth, "", QT_UINT16 (buffer + 38));
  GST_LOG ("%*s  volume:        %g", depth, "", QT_FP16 (buffer + 44));
  GST_LOG ("%*s  track width:   %g", depth, "", QT_FP32 (buffer + 84));
  GST_LOG ("%*s  track height:  %g", depth, "", QT_FP32 (buffer + 88));

}

void
qtdemux_dump_elst (GstQTDemux * qtdemux, guint8 * buffer, int depth)
{
  int i;
  int n;

  GST_LOG ("%*s  version/flags: %08x", depth, "", QT_UINT32 (buffer + 8));
  GST_LOG ("%*s  n entries:     %u", depth, "", QT_UINT32 (buffer + 12));
  n = QT_UINT32 (buffer + 12);
  for (i = 0; i < n; i++) {
    GST_LOG ("%*s    track dur:     %u", depth, "",
        QT_UINT32 (buffer + 16 + i * 12));
    GST_LOG ("%*s    media time:    %u", depth, "",
        QT_UINT32 (buffer + 20 + i * 12));
    GST_LOG ("%*s    media rate:    %g", depth, "",
        QT_FP32 (buffer + 24 + i * 12));
  }
}

void
qtdemux_dump_mdhd (GstQTDemux * qtdemux, guint8 * buffer, int depth)
{
  guint32 version;
  guint64 duration, ctime, mtime;
  guint32 time_scale;
  guint16 language, quality;

  version = QT_UINT32 (buffer + 8);
  GST_LOG ("%*s  version/flags: %08x", depth, "", version);

  if (version == 0x01000000) {
    ctime = QT_UINT64 (buffer + 12);
    mtime = QT_UINT64 (buffer + 20);
    time_scale = QT_UINT32 (buffer + 28);
    duration = QT_UINT64 (buffer + 32);
    language = QT_UINT16 (buffer + 40);
    quality = QT_UINT16 (buffer + 42);
  } else {
    ctime = QT_UINT32 (buffer + 12);
    mtime = QT_UINT32 (buffer + 16);
    time_scale = QT_UINT32 (buffer + 20);
    duration = QT_UINT32 (buffer + 24);
    language = QT_UINT16 (buffer + 28);
    quality = QT_UINT16 (buffer + 30);
  }

  GST_LOG ("%*s  creation time: %" G_GUINT64_FORMAT, depth, "", ctime);
  GST_LOG ("%*s  modify time:   %" G_GUINT64_FORMAT, depth, "", mtime);
  GST_LOG ("%*s  time scale:    1/%u sec", depth, "", time_scale);
  GST_LOG ("%*s  duration:      %" G_GUINT64_FORMAT, depth, "", duration);
  GST_LOG ("%*s  language:      %u", depth, "", language);
  GST_LOG ("%*s  quality:       %u", depth, "", quality);
}

void
qtdemux_dump_hdlr (GstQTDemux * qtdemux, guint8 * buffer, int depth)
{
  GST_LOG ("%*s  version/flags: %08x", depth, "", QT_UINT32 (buffer + 8));
  GST_LOG ("%*s  type:          %" GST_FOURCC_FORMAT, depth, "",
      GST_FOURCC_ARGS (QT_FOURCC (buffer + 12)));
  GST_LOG ("%*s  subtype:       %" GST_FOURCC_FORMAT, depth, "",
      GST_FOURCC_ARGS (QT_FOURCC (buffer + 16)));
  GST_LOG ("%*s  manufacturer:  %" GST_FOURCC_FORMAT, depth, "",
      GST_FOURCC_ARGS (QT_FOURCC (buffer + 20)));
  GST_LOG ("%*s  flags:         %08x", depth, "", QT_UINT32 (buffer + 24));
  GST_LOG ("%*s  flags mask:    %08x", depth, "", QT_UINT32 (buffer + 28));
  GST_LOG ("%*s  name:          %*s", depth, "",
      QT_UINT8 (buffer + 32), (char *) (buffer + 33));

}

void
qtdemux_dump_vmhd (GstQTDemux * qtdemux, guint8 * buffer, int depth)
{
  GST_LOG ("%*s  version/flags: %08x", depth, "", QT_UINT32 (buffer + 8));
  GST_LOG ("%*s  mode/color:    %08x", depth, "", QT_UINT32 (buffer + 16));
}

void
qtdemux_dump_dref (GstQTDemux * qtdemux, guint8 * buffer, int depth)
{
  int n;
  int i;
  int offset;

  GST_LOG ("%*s  version/flags: %08x", depth, "", QT_UINT32 (buffer + 8));
  GST_LOG ("%*s  n entries:     %u", depth, "", QT_UINT32 (buffer + 12));
  n = QT_UINT32 (buffer + 12);
  offset = 16;
  for (i = 0; i < n; i++) {
    GST_LOG ("%*s    size:          %u", depth, "",
        QT_UINT32 (buffer + offset));
    GST_LOG ("%*s    type:          %" GST_FOURCC_FORMAT, depth, "",
        GST_FOURCC_ARGS (QT_FOURCC (buffer + offset + 4)));
    offset += QT_UINT32 (buffer + offset);
  }
}

void
qtdemux_dump_stsd (GstQTDemux * qtdemux, guint8 * buffer, int depth)
{
  int i;
  int n;
  int offset;

  GST_LOG ("%*s  version/flags: %08x", depth, "", QT_UINT32 (buffer + 8));
  GST_LOG ("%*s  n entries:     %d", depth, "", QT_UINT32 (buffer + 12));
  n = QT_UINT32 (buffer + 12);
  offset = 16;
  for (i = 0; i < n; i++) {
    GST_LOG ("%*s    size:          %u", depth, "",
        QT_UINT32 (buffer + offset));
    GST_LOG ("%*s    type:          %" GST_FOURCC_FORMAT, depth, "",
        GST_FOURCC_ARGS (QT_FOURCC (buffer + offset + 4)));
    GST_LOG ("%*s    data reference:%d", depth, "",
        QT_UINT16 (buffer + offset + 14));

    GST_LOG ("%*s    version/rev.:  %08x", depth, "",
        QT_UINT32 (buffer + offset + 16));
    GST_LOG ("%*s    vendor:        %" GST_FOURCC_FORMAT, depth, "",
        GST_FOURCC_ARGS (QT_FOURCC (buffer + offset + 20)));
    GST_LOG ("%*s    temporal qual: %u", depth, "",
        QT_UINT32 (buffer + offset + 24));
    GST_LOG ("%*s    spatial qual:  %u", depth, "",
        QT_UINT32 (buffer + offset + 28));
    GST_LOG ("%*s    width:         %u", depth, "",
        QT_UINT16 (buffer + offset + 32));
    GST_LOG ("%*s    height:        %u", depth, "",
        QT_UINT16 (buffer + offset + 34));
    GST_LOG ("%*s    horiz. resol:  %g", depth, "",
        QT_FP32 (buffer + offset + 36));
    GST_LOG ("%*s    vert. resol.:  %g", depth, "",
        QT_FP32 (buffer + offset + 40));
    GST_LOG ("%*s    data size:     %u", depth, "",
        QT_UINT32 (buffer + offset + 44));
    GST_LOG ("%*s    frame count:   %u", depth, "",
        QT_UINT16 (buffer + offset + 48));
    GST_LOG ("%*s    compressor:    %d %d %d", depth, "",
        QT_UINT8 (buffer + offset + 49),
        QT_UINT8 (buffer + offset + 50), QT_UINT8 (buffer + offset + 51));
    //(char *) (buffer + offset + 51));
    GST_LOG ("%*s    depth:         %u", depth, "",
        QT_UINT16 (buffer + offset + 82));
    GST_LOG ("%*s    color table ID:%u", depth, "",
        QT_UINT16 (buffer + offset + 84));

    offset += QT_UINT32 (buffer + offset);
  }
}

void
qtdemux_dump_stts (GstQTDemux * qtdemux, guint8 * buffer, int depth)
{
  int i;
  int n;
  int offset;

  GST_LOG ("%*s  version/flags: %08x", depth, "", QT_UINT32 (buffer + 8));
  GST_LOG ("%*s  n entries:     %d", depth, "", QT_UINT32 (buffer + 12));
  n = QT_UINT32 (buffer + 12);
  offset = 16;
  for (i = 0; i < n; i++) {
    GST_LOG ("%*s    count:         %u", depth, "",
        QT_UINT32 (buffer + offset));
    GST_LOG ("%*s    duration:      %u", depth, "",
        QT_UINT32 (buffer + offset + 4));

    offset += 8;
  }
}

void
qtdemux_dump_stps (GstQTDemux * qtdemux, guint8 * buffer, int depth)
{
  int i;
  int n;
  int offset;

  GST_LOG ("%*s  version/flags: %08x", depth, "", QT_UINT32 (buffer + 8));
  GST_LOG ("%*s  n entries:     %d", depth, "", QT_UINT32 (buffer + 12));
  n = QT_UINT32 (buffer + 12);
  offset = 16;
  for (i = 0; i < n; i++) {
    GST_LOG ("%*s    sample:        %u", depth, "",
        QT_UINT32 (buffer + offset));

    offset += 4;
  }
}

void
qtdemux_dump_stss (GstQTDemux * qtdemux, guint8 * buffer, int depth)
{
  int i;
  int n;
  int offset;

  GST_LOG ("%*s  version/flags: %08x", depth, "", QT_UINT32 (buffer + 8));
  GST_LOG ("%*s  n entries:     %d", depth, "", QT_UINT32 (buffer + 12));
  n = QT_UINT32 (buffer + 12);
  offset = 16;
  for (i = 0; i < n; i++) {
    GST_LOG ("%*s    sample:        %u", depth, "",
        QT_UINT32 (buffer + offset));

    offset += 4;
  }
}

void
qtdemux_dump_stsc (GstQTDemux * qtdemux, guint8 * buffer, int depth)
{
  int i;
  int n;
  int offset;

  GST_LOG ("%*s  version/flags: %08x", depth, "", QT_UINT32 (buffer + 8));
  GST_LOG ("%*s  n entries:     %d", depth, "", QT_UINT32 (buffer + 12));
  n = QT_UINT32 (buffer + 12);
  offset = 16;
  for (i = 0; i < n; i++) {
    GST_LOG ("%*s    first chunk:   %u", depth, "",
        QT_UINT32 (buffer + offset));
    GST_LOG ("%*s    sample per ch: %u", depth, "",
        QT_UINT32 (buffer + offset + 4));
    GST_LOG ("%*s    sample desc id:%08x", depth, "",
        QT_UINT32 (buffer + offset + 8));

    offset += 12;
  }
}

void
qtdemux_dump_stsz (GstQTDemux * qtdemux, guint8 * buffer, int depth)
{
  //int i;
  int n;
  int offset;
  int sample_size;

  GST_LOG ("%*s  version/flags: %08x", depth, "", QT_UINT32 (buffer + 8));
  sample_size = QT_UINT32 (buffer + 12);
  GST_LOG ("%*s  sample size:   %d", depth, "", sample_size);
  if (sample_size == 0) {
    GST_LOG ("%*s  n entries:     %d", depth, "", QT_UINT32 (buffer + 16));
    n = QT_UINT32 (buffer + 16);
    offset = 20;
#if 0
    for (i = 0; i < n; i++) {
      GST_LOG ("%*s    sample size:   %u", depth, "",
          QT_UINT32 (buffer + offset));

      offset += 4;
    }
#endif
  }
}

void
qtdemux_dump_stco (GstQTDemux * qtdemux, guint8 * buffer, int depth)
{
  int i;
  int n;
  int offset;

  GST_LOG ("%*s  version/flags: %08x", depth, "", QT_UINT32 (buffer + 8));
  n = QT_UINT32 (buffer + 12);
  GST_LOG ("%*s  n entries:     %d", depth, "", n);
  offset = 16;
  for (i = 0; i < n; i++) {
    GST_LOG ("%*s    chunk offset:  %d", depth, "",
        QT_UINT32 (buffer + offset));

    offset += 4;
  }
}

void
qtdemux_dump_ctts (GstQTDemux * qtdemux, guint8 * buffer, int depth)
{
  int i;
  int n;
  int offset;

  GST_LOG ("%*s  version/flags: %08x", depth, "", QT_UINT32 (buffer + 8));
  n = QT_UINT32 (buffer + 12);
  GST_LOG ("%*s  n entries:     %d", depth, "", n);
  offset = 16;
  for (i = 0; i < n; i++) {
    GST_LOG ("%*s    sample count :%8d offset: %8d",
        depth, "", QT_UINT32 (buffer + offset),
        QT_UINT32 (buffer + offset + 4));

    offset += 8;
  }
}

void
qtdemux_dump_co64 (GstQTDemux * qtdemux, guint8 * buffer, int depth)
{
  //int i;
  int n;
  int offset;

  GST_LOG ("%*s  version/flags: %08x", depth, "", QT_UINT32 (buffer + 8));
  GST_LOG ("%*s  n entries:     %d", depth, "", QT_UINT32 (buffer + 12));
  n = QT_UINT32 (buffer + 12);
  offset = 16;
#if 0
  for (i = 0; i < n; i++) {
    GST_LOG ("%*s    chunk offset:  %" G_GUINT64_FORMAT, depth, "",
        QTDEMUX_GUINT64_GET (buffer + offset));

    offset += 8;
  }
#endif
}

void
qtdemux_dump_dcom (GstQTDemux * qtdemux, guint8 * buffer, int depth)
{
  GST_LOG ("%*s  compression type: %" GST_FOURCC_FORMAT, depth, "",
      GST_FOURCC_ARGS (QT_FOURCC (buffer + 8)));
}

void
qtdemux_dump_cmvd (GstQTDemux * qtdemux, guint8 * buffer, int depth)
{
  GST_LOG ("%*s  length: %d", depth, "", QT_UINT32 (buffer + 8));
}

void
qtdemux_dump_unknown (GstQTDemux * qtdemux, guint8 * buffer, int depth)
{
  int len;

  GST_LOG ("%*s  length: %d", depth, "", QT_UINT32 (buffer + 0));

  len = QT_UINT32 (buffer + 0);
  qtdemux_dump_mem (buffer, len);

}

static gboolean
qtdemux_node_dump_foreach (GNode * node, gpointer qtdemux)
{
  guint8 *buffer = (guint8 *) node->data;
  guint32 node_length;
  guint32 fourcc;
  const QtNodeType *type;
  int depth;

  node_length = GST_READ_UINT32_BE (buffer);
  fourcc = GST_READ_UINT32_LE (buffer + 4);

  type = qtdemux_type_get (fourcc);

  depth = (g_node_depth (node) - 1) * 2;
  GST_LOG ("%*s'%" GST_FOURCC_FORMAT "', [%d], %s",
      depth, "", GST_FOURCC_ARGS (fourcc), node_length, type->name);

  if (type->dump)
    type->dump (GST_QTDEMUX_CAST (qtdemux), buffer, depth);

  return FALSE;
}

void
qtdemux_node_dump (GstQTDemux * qtdemux, GNode * node)
{
  g_node_traverse (qtdemux->moov_node, G_PRE_ORDER, G_TRAVERSE_ALL, -1,
      qtdemux_node_dump_foreach, qtdemux);
}
