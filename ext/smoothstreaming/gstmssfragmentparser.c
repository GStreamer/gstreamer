/*
 * Microsoft Smooth-Streaming fragment parsing library
 *
 * gstmssfragmentparser.h
 *
 * Copyright (C) 2016 Igalia S.L
 * Copyright (C) 2016 Metrological
 *   Author: Philippe Normand <philn@igalia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library (COPYING); if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "gstmssfragmentparser.h"
#include <gst/base/gstbytereader.h>
#include <string.h>

GST_DEBUG_CATEGORY_EXTERN (mssdemux_debug);
#define GST_CAT_DEFAULT mssdemux_debug

void
gst_mss_fragment_parser_init (GstMssFragmentParser * parser)
{
  parser->status = GST_MSS_FRAGMENT_HEADER_PARSER_INIT;
  parser->tfrf.entries_count = 0;
}

void
gst_mss_fragment_parser_clear (GstMssFragmentParser * parser)
{
  parser->tfrf.entries_count = 0;
  if (parser->tfrf.entries) {
    g_free (parser->tfrf.entries);
    parser->tfrf.entries = 0;
  }
}

static gboolean
_parse_tfrf_box (GstMssFragmentParser * parser, GstByteReader * reader)
{
  guint8 version;
  guint32 flags = 0;
  guint8 fragment_count = 0;
  guint8 index = 0;

  if (!gst_byte_reader_get_uint8 (reader, &version)) {
    GST_ERROR ("Error getting box's version field");
    return FALSE;
  }

  if (!gst_byte_reader_get_uint24_be (reader, &flags)) {
    GST_ERROR ("Error getting box's flags field");
    return FALSE;
  }

  gst_byte_reader_get_uint8 (reader, &fragment_count);
  parser->tfrf.entries_count = fragment_count;
  parser->tfrf.entries =
      g_malloc (sizeof (GstTfrfBoxEntry) * parser->tfrf.entries_count);
  for (index = 0; index < fragment_count; index++) {
    guint64 absolute_time = 0;
    guint64 absolute_duration = 0;
    if (version & 0x01) {
      gst_byte_reader_get_uint64_be (reader, &absolute_time);
      gst_byte_reader_get_uint64_be (reader, &absolute_duration);
    } else {
      guint32 time = 0;
      guint32 duration = 0;
      gst_byte_reader_get_uint32_be (reader, &time);
      gst_byte_reader_get_uint32_be (reader, &duration);
      time = ~time;
      duration = ~duration;
      absolute_time = ~time;
      absolute_duration = ~duration;
    }
    parser->tfrf.entries[index].time = absolute_time;
    parser->tfrf.entries[index].duration = absolute_duration;
  }

  GST_LOG ("tfrf box parsed");
  return TRUE;
}

static gboolean
_parse_tfxd_box (GstMssFragmentParser * parser, GstByteReader * reader)
{
  guint8 version;
  guint32 flags = 0;
  guint64 absolute_time = 0;
  guint64 absolute_duration = 0;

  if (!gst_byte_reader_get_uint8 (reader, &version)) {
    GST_ERROR ("Error getting box's version field");
    return FALSE;
  }

  if (!gst_byte_reader_get_uint24_be (reader, &flags)) {
    GST_ERROR ("Error getting box's flags field");
    return FALSE;
  }

  if (version & 0x01) {
    gst_byte_reader_get_uint64_be (reader, &absolute_time);
    gst_byte_reader_get_uint64_be (reader, &absolute_duration);
  } else {
    guint32 time = 0;
    guint32 duration = 0;
    gst_byte_reader_get_uint32_be (reader, &time);
    gst_byte_reader_get_uint32_be (reader, &duration);
    time = ~time;
    duration = ~duration;
    absolute_time = ~time;
    absolute_duration = ~duration;
  }

  parser->tfxd.time = absolute_time;
  parser->tfxd.duration = absolute_duration;
  GST_LOG ("tfxd box parsed");
  return TRUE;
}

gboolean
gst_mss_fragment_parser_add_buffer (GstMssFragmentParser * parser,
    GstBuffer * buffer)
{
  GstByteReader reader;
  GstMapInfo info;
  guint32 size;
  guint32 fourcc;
  const guint8 *uuid;
  gboolean error = FALSE;
  gboolean mdat_box_found = FALSE;

  static const guint8 tfrf_uuid[] = {
    0xd4, 0x80, 0x7e, 0xf2, 0xca, 0x39, 0x46, 0x95,
    0x8e, 0x54, 0x26, 0xcb, 0x9e, 0x46, 0xa7, 0x9f
  };

  static const guint8 tfxd_uuid[] = {
    0x6d, 0x1d, 0x9b, 0x05, 0x42, 0xd5, 0x44, 0xe6,
    0x80, 0xe2, 0x14, 0x1d, 0xaf, 0xf7, 0x57, 0xb2
  };

  static const guint8 piff_uuid[] = {
    0xa2, 0x39, 0x4f, 0x52, 0x5a, 0x9b, 0x4f, 0x14,
    0xa2, 0x44, 0x6c, 0x42, 0x7c, 0x64, 0x8d, 0xf4
  };

  if (!gst_buffer_map (buffer, &info, GST_MAP_READ)) {
    return FALSE;
  }

  gst_byte_reader_init (&reader, info.data, info.size);
  GST_TRACE ("Total buffer size: %u", gst_byte_reader_get_size (&reader));

  size = gst_byte_reader_get_uint32_be_unchecked (&reader);
  fourcc = gst_byte_reader_get_uint32_le_unchecked (&reader);
  if (fourcc == GST_MSS_FRAGMENT_FOURCC_MOOF) {
    GST_TRACE ("moof box found");
    size = gst_byte_reader_get_uint32_be_unchecked (&reader);
    fourcc = gst_byte_reader_get_uint32_le_unchecked (&reader);
    if (fourcc == GST_MSS_FRAGMENT_FOURCC_MFHD) {
      gst_byte_reader_skip_unchecked (&reader, size - 8);

      size = gst_byte_reader_get_uint32_be_unchecked (&reader);
      fourcc = gst_byte_reader_get_uint32_le_unchecked (&reader);
      if (fourcc == GST_MSS_FRAGMENT_FOURCC_TRAF) {
        size = gst_byte_reader_get_uint32_be_unchecked (&reader);
        fourcc = gst_byte_reader_get_uint32_le_unchecked (&reader);
        if (fourcc == GST_MSS_FRAGMENT_FOURCC_TFHD) {
          gst_byte_reader_skip_unchecked (&reader, size - 8);

          size = gst_byte_reader_get_uint32_be_unchecked (&reader);
          fourcc = gst_byte_reader_get_uint32_le_unchecked (&reader);
          if (fourcc == GST_MSS_FRAGMENT_FOURCC_TRUN) {
            GST_TRACE ("trun box found, size: %" G_GUINT32_FORMAT, size);
            if (!gst_byte_reader_skip (&reader, size - 8)) {
              GST_WARNING ("Failed to skip trun box, enough data?");
              error = TRUE;
              goto beach;
            }
          }
        }
      }
    }
  }

  while (!mdat_box_found) {
    GST_TRACE ("remaining data: %u", gst_byte_reader_get_remaining (&reader));
    if (!gst_byte_reader_get_uint32_be (&reader, &size)) {
      GST_WARNING ("Failed to get box size, enough data?");
      error = TRUE;
      break;
    }

    GST_TRACE ("box size: %" G_GUINT32_FORMAT, size);
    if (!gst_byte_reader_get_uint32_le (&reader, &fourcc)) {
      GST_WARNING ("Failed to get fourcc, enough data?");
      error = TRUE;
      break;
    }

    if (fourcc == GST_MSS_FRAGMENT_FOURCC_MDAT) {
      GST_LOG ("mdat box found");
      mdat_box_found = TRUE;
      break;
    }

    if (fourcc != GST_MSS_FRAGMENT_FOURCC_UUID) {
      GST_ERROR ("invalid UUID fourcc: %" GST_FOURCC_FORMAT,
          GST_FOURCC_ARGS (fourcc));
      error = TRUE;
      break;
    }

    if (!gst_byte_reader_peek_data (&reader, 16, &uuid)) {
      GST_ERROR ("not enough data in UUID box");
      error = TRUE;
      break;
    }

    if (memcmp (uuid, piff_uuid, 16) == 0) {
      gst_byte_reader_skip_unchecked (&reader, size - 8);
      GST_LOG ("piff box detected");
    }

    if (memcmp (uuid, tfrf_uuid, 16) == 0) {
      gst_byte_reader_get_data (&reader, 16, &uuid);
      if (!_parse_tfrf_box (parser, &reader)) {
        GST_ERROR ("txrf box parsing error");
        error = TRUE;
        break;
      }
    }

    if (memcmp (uuid, tfxd_uuid, 16) == 0) {
      gst_byte_reader_get_data (&reader, 16, &uuid);
      if (!_parse_tfxd_box (parser, &reader)) {
        GST_ERROR ("tfrf box parsing error");
        error = TRUE;
        break;
      }
    }
  }

beach:

  if (!error)
    parser->status = GST_MSS_FRAGMENT_HEADER_PARSER_FINISHED;

  GST_LOG ("Fragment parsing successful: %s", error ? "no" : "yes");
  gst_buffer_unmap (buffer, &info);
  return !error;
}
