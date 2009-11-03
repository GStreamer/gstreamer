/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) 2009 Tim-Philipp Müller <tim centricular net>
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

#include <gst/base/gstbytereader.h>

#include <string.h>

#define GET_UINT8(data)   qt_atom_parser_get_uint8_unchecked(data)
#define GET_UINT16(data)  qt_atom_parser_get_uint16_unchecked(data)
#define GET_UINT32(data)  qt_atom_parser_get_uint32_unchecked(data)
#define GET_UINT64(data)  qt_atom_parser_get_uint64_unchecked(data)
#define GET_FP32(data)   (qt_atom_parser_get_uint32_unchecked(data)/65536.0)
#define GET_FP16(data)   (qt_atom_parser_get_uint16_unchecked(data)/256.0)
#define GET_FOURCC(data)  qt_atom_parser_get_fourcc_unchecked(data)

gboolean
qtdemux_dump_mvhd (GstQTDemux * qtdemux, QtAtomParser * data, int depth)
{
  if (!qt_atom_parser_has_remaining (data, 100))
    return FALSE;

  GST_LOG ("%*s  version/flags: %08x", depth, "", GET_UINT32 (data));
  GST_LOG ("%*s  creation time: %u", depth, "", GET_UINT32 (data));
  GST_LOG ("%*s  modify time:   %u", depth, "", GET_UINT32 (data));
  GST_LOG ("%*s  time scale:    1/%u sec", depth, "", GET_UINT32 (data));
  GST_LOG ("%*s  duration:      %u", depth, "", GET_UINT32 (data));
  GST_LOG ("%*s  pref. rate:    %g", depth, "", GET_FP32 (data));
  GST_LOG ("%*s  pref. volume:  %g", depth, "", GET_FP16 (data));
  qt_atom_parser_skip (data, 46);
  GST_LOG ("%*s  preview time:  %u", depth, "", GET_UINT32 (data));
  GST_LOG ("%*s  preview dur.:  %u", depth, "", GET_UINT32 (data));
  GST_LOG ("%*s  poster time:   %u", depth, "", GET_UINT32 (data));
  GST_LOG ("%*s  select time:   %u", depth, "", GET_UINT32 (data));
  GST_LOG ("%*s  select dur.:   %u", depth, "", GET_UINT32 (data));
  GST_LOG ("%*s  current time:  %u", depth, "", GET_UINT32 (data));
  GST_LOG ("%*s  next track ID: %d", depth, "", GET_UINT32 (data));
  return TRUE;
}

gboolean
qtdemux_dump_tkhd (GstQTDemux * qtdemux, QtAtomParser * data, int depth)
{
  guint64 duration, ctime, mtime;
  guint32 version, track_id, iwidth, iheight;
  guint16 layer, alt_group, ivol;
  guint value_size;

  if (!qt_atom_parser_get_uint32 (data, &version))
    return FALSE;

  GST_LOG ("%*s  version/flags: %08x", depth, "", version);

  value_size = ((version >> 24) == 1) ? sizeof (guint64) : sizeof (guint32);

  if (qt_atom_parser_get_offset (data, value_size, &ctime) &&
      qt_atom_parser_get_offset (data, value_size, &mtime) &&
      qt_atom_parser_get_uint32 (data, &track_id) &&
      qt_atom_parser_skip (data, 4) &&
      qt_atom_parser_get_offset (data, value_size, &duration) &&
      qt_atom_parser_skip (data, 4) &&
      qt_atom_parser_get_uint16 (data, &layer) &&
      qt_atom_parser_get_uint16 (data, &alt_group) &&
      qt_atom_parser_skip (data, 4) &&
      qt_atom_parser_get_uint16 (data, &ivol) &&
      qt_atom_parser_skip (data, 2 + (9 * 4)) &&
      qt_atom_parser_get_uint32 (data, &iwidth) &&
      qt_atom_parser_get_uint32 (data, &iheight)) {
    GST_LOG ("%*s  creation time: %" G_GUINT64_FORMAT, depth, "", ctime);
    GST_LOG ("%*s  modify time:   %" G_GUINT64_FORMAT, depth, "", mtime);
    GST_LOG ("%*s  track ID:      %u", depth, "", track_id);
    GST_LOG ("%*s  duration:      %" G_GUINT64_FORMAT, depth, "", duration);
    GST_LOG ("%*s  layer:         %u", depth, "", layer);
    GST_LOG ("%*s  alt group:     %u", depth, "", alt_group);
    GST_LOG ("%*s  volume:        %g", depth, "", ivol / 256.0);
    GST_LOG ("%*s  track width:   %g", depth, "", iwidth / 65536.0);
    GST_LOG ("%*s  track height:  %g", depth, "", iheight / 65536.0);
    return TRUE;
  }

  return FALSE;
}

gboolean
qtdemux_dump_elst (GstQTDemux * qtdemux, QtAtomParser * data, int depth)
{
  guint32 ver_flags = 0, num_entries = 0, i;

  if (!qt_atom_parser_get_uint32 (data, &ver_flags) ||
      !qt_atom_parser_get_uint32 (data, &num_entries))
    return FALSE;

  GST_LOG ("%*s  version/flags: %08x", depth, "", ver_flags);
  GST_LOG ("%*s  n entries:     %d", depth, "", num_entries);

  if (!qt_atom_parser_has_chunks (data, num_entries, 4 + 4 + 4))
    return FALSE;

  for (i = 0; i < num_entries; i++) {
    GST_LOG ("%*s    track dur:     %u", depth, "", GET_UINT32 (data));
    GST_LOG ("%*s    media time:    %u", depth, "", GET_UINT32 (data));
    GST_LOG ("%*s    media rate:    %g", depth, "", GET_FP32 (data));
  }
  return TRUE;
}

gboolean
qtdemux_dump_mdhd (GstQTDemux * qtdemux, QtAtomParser * data, int depth)
{
  guint32 version;
  guint64 duration, ctime, mtime;
  guint32 time_scale;
  guint16 language, quality;
  guint value_size;

  if (!qt_atom_parser_get_uint32 (data, &version))
    return FALSE;

  GST_LOG ("%*s  version/flags: %08x", depth, "", version);

  value_size = ((version >> 24) == 1) ? sizeof (guint64) : sizeof (guint32);

  if (qt_atom_parser_get_offset (data, value_size, &ctime) &&
      qt_atom_parser_get_offset (data, value_size, &mtime) &&
      qt_atom_parser_get_uint32 (data, &time_scale) &&
      qt_atom_parser_get_offset (data, value_size, &duration) &&
      qt_atom_parser_get_uint16 (data, &language) &&
      qt_atom_parser_get_uint16 (data, &quality)) {
    GST_LOG ("%*s  creation time: %" G_GUINT64_FORMAT, depth, "", ctime);
    GST_LOG ("%*s  modify time:   %" G_GUINT64_FORMAT, depth, "", mtime);
    GST_LOG ("%*s  time scale:    1/%u sec", depth, "", time_scale);
    GST_LOG ("%*s  duration:      %" G_GUINT64_FORMAT, depth, "", duration);
    GST_LOG ("%*s  language:      %u", depth, "", language);
    GST_LOG ("%*s  quality:       %u", depth, "", quality);
    return TRUE;
  }

  return FALSE;
}

gboolean
qtdemux_dump_hdlr (GstQTDemux * qtdemux, QtAtomParser * data, int depth)
{
  guint32 version, type, subtype, manufacturer;
  const gchar *name;

  if (!qt_atom_parser_has_remaining (data, 4 + 4 + 4 + 4 + 4 + 4 + 1))
    return FALSE;

  version = GET_UINT32 (data);
  type = GET_FOURCC (data);
  subtype = GET_FOURCC (data);
  manufacturer = GET_FOURCC (data);

  GST_LOG ("%*s  version/flags: %08x", depth, "", version);
  GST_LOG ("%*s  type:          %" GST_FOURCC_FORMAT, depth, "",
      GST_FOURCC_ARGS (type));
  GST_LOG ("%*s  subtype:       %" GST_FOURCC_FORMAT, depth, "",
      GST_FOURCC_ARGS (subtype));
  GST_LOG ("%*s  manufacturer:  %" GST_FOURCC_FORMAT, depth, "",
      GST_FOURCC_ARGS (manufacturer));
  GST_LOG ("%*s  flags:         %08x", depth, "", GET_UINT32 (data));
  GST_LOG ("%*s  flags mask:    %08x", depth, "", GET_UINT32 (data));

  /* quicktime uses pascal string, mp4 zero-terminated string */
  if (gst_byte_reader_peek_string (data, &name)) {
    GST_LOG ("%*s  name:          %s", depth, "", name);
  } else {
    gchar buf[256];
    guint len;

    len = qt_atom_parser_get_uint8_unchecked (data);
    if (qt_atom_parser_has_remaining (data, len)) {
      memcpy (buf, qt_atom_parser_peek_bytes_unchecked (data), len);
      buf[len] = '\0';
      GST_LOG ("%*s  name:          %s", depth, "", buf);
    }
  }
  return TRUE;
}

gboolean
qtdemux_dump_vmhd (GstQTDemux * qtdemux, QtAtomParser * data, int depth)
{
  if (!qt_atom_parser_has_remaining (data, 4 + 4))
    return FALSE;

  GST_LOG ("%*s  version/flags: %08x", depth, "", GET_UINT32 (data));
  GST_LOG ("%*s  mode/color:    %08x", depth, "", GET_UINT32 (data));
  return TRUE;
}

gboolean
qtdemux_dump_dref (GstQTDemux * qtdemux, QtAtomParser * data, int depth)
{
  guint32 ver_flags, num_entries, i;

  if (!qt_atom_parser_get_uint32 (data, &ver_flags) ||
      !qt_atom_parser_get_uint32 (data, &num_entries))
    return FALSE;

  GST_LOG ("%*s  version/flags: %08x", depth, "", ver_flags);
  GST_LOG ("%*s  n entries:     %u", depth, "", num_entries);
  for (i = 0; i < num_entries; i++) {
    guint32 size, fourcc;

    if (!qt_atom_parser_get_uint32 (data, &size) ||
        !qt_atom_parser_get_fourcc (data, &fourcc) || size < 8 ||
        !qt_atom_parser_skip (data, size - 8))
      return FALSE;

    GST_LOG ("%*s    size:          %u", depth, "", size);
    GST_LOG ("%*s    type:          %" GST_FOURCC_FORMAT, depth, "",
        GST_FOURCC_ARGS (fourcc));
  }
  return TRUE;
}

gboolean
qtdemux_dump_stsd (GstQTDemux * qtdemux, QtAtomParser * data, int depth)
{
  guint32 ver_flags, num_entries, i;

  if (!qt_atom_parser_get_uint32 (data, &ver_flags) ||
      !qt_atom_parser_get_uint32 (data, &num_entries))
    return FALSE;

  GST_LOG ("%*s  version/flags: %08x", depth, "", ver_flags);
  GST_LOG ("%*s  n entries:     %d", depth, "", num_entries);

  for (i = 0; i < num_entries; i++) {
    QtAtomParser sub;
    guint32 size, fourcc;

    if (!qt_atom_parser_get_uint32 (data, &size) ||
        !qt_atom_parser_get_fourcc (data, &fourcc))
      return FALSE;

    GST_LOG ("%*s    size:          %u", depth, "", size);
    GST_LOG ("%*s    type:          %" GST_FOURCC_FORMAT, depth, "",
        GST_FOURCC_ARGS (fourcc));

    if (size < (6 + 2 + 4 + 4 + 4 + 4 + 2 + 2 + 4 + 4 + 4 + 2 + 1 + 31 + 2 + 2))
      return FALSE;

    qt_atom_parser_peek_sub (data, 0, 78, &sub);
    qt_atom_parser_skip (&sub, 6);
    GST_LOG ("%*s    data reference:%d", depth, "", GET_UINT16 (&sub));
    GST_LOG ("%*s    version/rev.:  %08x", depth, "", GET_UINT32 (&sub));
    fourcc = GET_FOURCC (&sub);
    GST_LOG ("%*s    vendor:        %" GST_FOURCC_FORMAT, depth, "",
        GST_FOURCC_ARGS (fourcc));
    GST_LOG ("%*s    temporal qual: %u", depth, "", GET_UINT32 (&sub));
    GST_LOG ("%*s    spatial qual:  %u", depth, "", GET_UINT32 (&sub));
    GST_LOG ("%*s    width:         %u", depth, "", GET_UINT16 (&sub));
    GST_LOG ("%*s    height:        %u", depth, "", GET_UINT16 (&sub));
    GST_LOG ("%*s    horiz. resol:  %g", depth, "", GET_FP32 (&sub));
    GST_LOG ("%*s    vert. resol.:  %g", depth, "", GET_FP32 (&sub));
    GST_LOG ("%*s    data size:     %u", depth, "", GET_UINT32 (&sub));
    GST_LOG ("%*s    frame count:   %u", depth, "", GET_UINT16 (&sub));
    /* something is not right with this, it's supposed to be a string but it's
     * not apparently, so just skip this for now */
    qt_atom_parser_skip (&sub, 1 + 31);
    GST_LOG ("%*s    compressor:    (skipped)", depth, "");
    GST_LOG ("%*s    depth:         %u", depth, "", GET_UINT16 (&sub));
    GST_LOG ("%*s    color table ID:%u", depth, "", GET_UINT16 (&sub));
    if (!qt_atom_parser_skip (data, size - (4 + 4)))
      return FALSE;
  }
  return TRUE;
}

gboolean
qtdemux_dump_stts (GstQTDemux * qtdemux, QtAtomParser * data, int depth)
{
  guint32 ver_flags, num_entries, i;

  if (!qt_atom_parser_get_uint32 (data, &ver_flags) ||
      !qt_atom_parser_get_uint32 (data, &num_entries))
    return FALSE;

  GST_LOG ("%*s  version/flags: %08x", depth, "", ver_flags);
  GST_LOG ("%*s  n entries:     %d", depth, "", num_entries);

  if (!qt_atom_parser_has_chunks (data, num_entries, 4 + 4))
    return FALSE;

  for (i = 0; i < num_entries; i++) {
    GST_LOG ("%*s    count:         %u", depth, "", GET_UINT32 (data));
    GST_LOG ("%*s    duration:      %u", depth, "", GET_UINT32 (data));
  }
  return TRUE;
}

gboolean
qtdemux_dump_stps (GstQTDemux * qtdemux, QtAtomParser * data, int depth)
{
  guint32 ver_flags, num_entries, i;

  if (!qt_atom_parser_get_uint32 (data, &ver_flags) ||
      !qt_atom_parser_get_uint32 (data, &num_entries))
    return FALSE;

  GST_LOG ("%*s  version/flags: %08x", depth, "", ver_flags);
  GST_LOG ("%*s  n entries:     %d", depth, "", num_entries);

  if (!qt_atom_parser_has_chunks (data, num_entries, 4))
    return FALSE;

  for (i = 0; i < num_entries; i++) {
    GST_LOG ("%*s    sample:        %u", depth, "", GET_UINT32 (data));
  }
  return TRUE;
}

gboolean
qtdemux_dump_stss (GstQTDemux * qtdemux, QtAtomParser * data, int depth)
{
  guint32 ver_flags, num_entries, i;

  if (!qt_atom_parser_get_uint32 (data, &ver_flags) ||
      !qt_atom_parser_get_uint32 (data, &num_entries))
    return FALSE;

  GST_LOG ("%*s  version/flags: %08x", depth, "", ver_flags);
  GST_LOG ("%*s  n entries:     %d", depth, "", num_entries);

  if (!qt_atom_parser_has_chunks (data, num_entries, 4))
    return FALSE;

  for (i = 0; i < num_entries; i++) {
    GST_LOG ("%*s    sample:        %u", depth, "", GET_UINT32 (data));
  }
  return TRUE;
}

gboolean
qtdemux_dump_stsc (GstQTDemux * qtdemux, QtAtomParser * data, int depth)
{
  guint32 ver_flags, num_entries, i;

  if (!qt_atom_parser_get_uint32 (data, &ver_flags) ||
      !qt_atom_parser_get_uint32 (data, &num_entries))
    return FALSE;

  GST_LOG ("%*s  version/flags: %08x", depth, "", ver_flags);
  GST_LOG ("%*s  n entries:     %d", depth, "", num_entries);

  if (!qt_atom_parser_has_chunks (data, num_entries, 4 + 4 + 4))
    return FALSE;

  for (i = 0; i < num_entries; i++) {
    GST_LOG ("%*s    first chunk:   %u", depth, "", GET_UINT32 (data));
    GST_LOG ("%*s    sample per ch: %u", depth, "", GET_UINT32 (data));
    GST_LOG ("%*s    sample desc id:%08x", depth, "", GET_UINT32 (data));
  }
  return TRUE;
}

gboolean
qtdemux_dump_stsz (GstQTDemux * qtdemux, QtAtomParser * data, int depth)
{
  guint32 ver_flags, sample_size, num_entries;

  if (!qt_atom_parser_get_uint32 (data, &ver_flags) ||
      !qt_atom_parser_get_uint32 (data, &sample_size))
    return FALSE;

  GST_LOG ("%*s  version/flags: %08x", depth, "", ver_flags);
  GST_LOG ("%*s  sample size:   %d", depth, "", sample_size);

  if (sample_size == 0) {
    if (!qt_atom_parser_get_uint32 (data, &num_entries))
      return FALSE;

    GST_LOG ("%*s  n entries:     %d", depth, "", num_entries);
#if 0
    if (!qt_atom_parser_has_chunks (data, num_entries, 4))
      return FALSE;
    for (i = 0; i < num_entries; i++) {
      GST_LOG ("%*s    sample size:   %u", depth, "", GET_UINT32 (data));
    }
#endif
  }
  return TRUE;
}

gboolean
qtdemux_dump_stco (GstQTDemux * qtdemux, QtAtomParser * data, int depth)
{
  guint32 ver_flags, num_entries, i;

  if (!qt_atom_parser_get_uint32 (data, &ver_flags) ||
      !qt_atom_parser_get_uint32 (data, &num_entries))
    return FALSE;

  GST_LOG ("%*s  version/flags: %08x", depth, "", ver_flags);
  GST_LOG ("%*s  n entries:     %d", depth, "", num_entries);

  if (!qt_atom_parser_has_chunks (data, num_entries, 4))
    return FALSE;

  for (i = 0; i < num_entries; i++) {
    GST_LOG ("%*s    chunk offset:  %u", depth, "", GET_UINT32 (data));
  }
  return TRUE;
}

gboolean
qtdemux_dump_ctts (GstQTDemux * qtdemux, QtAtomParser * data, int depth)
{
  guint32 ver_flags, num_entries, i, count, offset;

  if (!qt_atom_parser_get_uint32 (data, &ver_flags) ||
      !qt_atom_parser_get_uint32 (data, &num_entries))
    return FALSE;

  GST_LOG ("%*s  version/flags: %08x", depth, "", ver_flags);
  GST_LOG ("%*s  n entries:     %d", depth, "", num_entries);

  if (!qt_atom_parser_has_chunks (data, num_entries, 4 + 4))
    return FALSE;

  for (i = 0; i < num_entries; i++) {
    count = GET_UINT32 (data);
    offset = GET_UINT32 (data);
    GST_LOG ("%*s    sample count :%8d offset: %8d", depth, "", count, offset);
  }
  return TRUE;
}

gboolean
qtdemux_dump_co64 (GstQTDemux * qtdemux, QtAtomParser * data, int depth)
{
  guint32 ver_flags, num_entries, i;

  if (!qt_atom_parser_get_uint32 (data, &ver_flags) ||
      !qt_atom_parser_get_uint32 (data, &num_entries))
    return FALSE;

  GST_LOG ("%*s  version/flags: %08x", depth, "", ver_flags);
  GST_LOG ("%*s  n entries:     %d", depth, "", num_entries);

  if (!qt_atom_parser_has_chunks (data, num_entries, 8))
    return FALSE;

  for (i = 0; i < num_entries; i++) {
    GST_LOG ("%*s    chunk offset:  %" G_GUINT64_FORMAT, depth, "",
        GET_UINT64 (data));
  }
  return TRUE;
}

gboolean
qtdemux_dump_dcom (GstQTDemux * qtdemux, QtAtomParser * data, int depth)
{
  if (!qt_atom_parser_has_remaining (data, 4))
    return FALSE;

  GST_LOG ("%*s  compression type: %" GST_FOURCC_FORMAT, depth, "",
      GST_FOURCC_ARGS (GET_FOURCC (data)));
  return TRUE;
}

gboolean
qtdemux_dump_cmvd (GstQTDemux * qtdemux, QtAtomParser * data, int depth)
{
  if (!qt_atom_parser_has_remaining (data, 4))
    return FALSE;

  GST_LOG ("%*s  length: %d", depth, "", GET_UINT32 (data));
  return TRUE;
}

gboolean
qtdemux_dump_unknown (GstQTDemux * qtdemux, QtAtomParser * data, int depth)
{
  int len;

  len = qt_atom_parser_get_remaining (data);
  GST_LOG ("%*s  length: %d", depth, "", len);

  GST_MEMDUMP_OBJECT (qtdemux, "unknown atom data",
      qt_atom_parser_peek_bytes_unchecked (data), len);
  return TRUE;
}

static gboolean
qtdemux_node_dump_foreach (GNode * node, gpointer qtdemux)
{
  QtAtomParser parser;
  guint8 *buffer = (guint8 *) node->data;       /* FIXME: move to byte reader */
  guint32 node_length;
  guint32 fourcc;
  const QtNodeType *type;
  int depth;

  node_length = GST_READ_UINT32_BE (buffer);
  fourcc = GST_READ_UINT32_LE (buffer + 4);

  g_warn_if_fail (node_length >= 8);

  qt_atom_parser_init (&parser, buffer + 8, node_length - 8);

  type = qtdemux_type_get (fourcc);

  depth = (g_node_depth (node) - 1) * 2;
  GST_LOG ("%*s'%" GST_FOURCC_FORMAT "', [%d], %s",
      depth, "", GST_FOURCC_ARGS (fourcc), node_length, type->name);

  if (type->dump) {
    gboolean ret;

    ret = type->dump (GST_QTDEMUX_CAST (qtdemux), &parser, depth);

    if (!ret) {
      GST_WARNING ("%*s  not enough data parsing atom %" GST_FOURCC_FORMAT,
          depth, "", GST_FOURCC_ARGS (fourcc));
    }
  }

  return FALSE;
}

gboolean
qtdemux_node_dump (GstQTDemux * qtdemux, GNode * node)
{
  if (__gst_debug_min < GST_LEVEL_LOG)
    return TRUE;

  g_node_traverse (qtdemux->moov_node, G_PRE_ORDER, G_TRAVERSE_ALL, -1,
      qtdemux_node_dump_foreach, qtdemux);
  return TRUE;
}
