/*
 * ISO File Format parsing library
 *
 * gstisoff.h
 *
 * Copyright (C) 2015 Samsung Electronics. All rights reserved.
 *   Author: Thiago Santos <thiagoss@osg.samsung.com>
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

#include "gstisoff.h"
#include <gst/base/gstbytereader.h>

#include <string.h>
#include "gstdash_debug.h"

#define GST_CAT_DEFAULT gst_dash_demux_debug

/* gst_isoff_parse_box:
 * @reader:
 * @type: type that was found at the current position
 * @extended_type: (allow-none): extended type if type=='uuid'
 * @header_size: (allow-none): size of the box header (type, extended type and size)
 * @size: size of the complete box including type, extended type and size
 *
 * Advances the byte reader to the start of the box content. To skip
 * over the complete box, skip size - header_size bytes.
 *
 * Returns: TRUE if a box header could be parsed, FALSE if more data is needed
 */
gboolean
gst_isoff_parse_box_header (GstByteReader * reader, guint32 * type,
    guint8 extended_type[16], guint * header_size, guint64 * size)
{
  guint header_start_offset;
  guint32 size_field;

  header_start_offset = gst_byte_reader_get_pos (reader);

  if (gst_byte_reader_get_remaining (reader) < 8)
    goto not_enough_data;

  size_field = gst_byte_reader_get_uint32_be_unchecked (reader);
  *type = gst_byte_reader_get_uint32_le_unchecked (reader);

  if (size_field == 1) {
    if (gst_byte_reader_get_remaining (reader) < 8)
      goto not_enough_data;
    *size = gst_byte_reader_get_uint64_be_unchecked (reader);
  } else {
    *size = size_field;
  }

  if (*type == GST_ISOFF_FOURCC_UUID) {
    if (gst_byte_reader_get_remaining (reader) < 16)
      goto not_enough_data;

    if (extended_type)
      memcpy (extended_type, gst_byte_reader_get_data_unchecked (reader, 16),
          16);
  }

  if (header_size)
    *header_size = gst_byte_reader_get_pos (reader) - header_start_offset;

  return TRUE;

not_enough_data:
  gst_byte_reader_set_pos (reader, header_start_offset);
  return FALSE;
}

static void
gst_isoff_trun_box_clear (GstTrunBox * trun)
{
  if (trun->samples)
    g_array_free (trun->samples, TRUE);
}

static void
gst_isoff_traf_box_clear (GstTrafBox * traf)
{
  if (traf->trun)
    g_array_free (traf->trun, TRUE);
}

static gboolean
gst_isoff_mfhd_box_parse (GstMfhdBox * mfhd, GstByteReader * reader)
{
  guint8 version;
  guint32 flags;

  if (gst_byte_reader_get_remaining (reader) != 8)
    return FALSE;

  version = gst_byte_reader_get_uint8_unchecked (reader);
  if (version != 0)
    return FALSE;

  flags = gst_byte_reader_get_uint24_be_unchecked (reader);
  if (flags != 0)
    return FALSE;

  mfhd->sequence_number = gst_byte_reader_get_uint32_be_unchecked (reader);

  return TRUE;
}

static gboolean
gst_isoff_tfhd_box_parse (GstTfhdBox * tfhd, GstByteReader * reader)
{
  memset (tfhd, 0, sizeof (*tfhd));

  if (gst_byte_reader_get_remaining (reader) < 4)
    return FALSE;

  tfhd->version = gst_byte_reader_get_uint8_unchecked (reader);
  if (tfhd->version != 0)
    return FALSE;

  tfhd->flags = gst_byte_reader_get_uint24_be_unchecked (reader);

  if (!gst_byte_reader_get_uint32_be (reader, &tfhd->track_id))
    return FALSE;

  if ((tfhd->flags & GST_TFHD_FLAGS_BASE_DATA_OFFSET_PRESENT) &&
      !gst_byte_reader_get_uint64_be (reader, &tfhd->base_data_offset))
    return FALSE;

  if ((tfhd->flags & GST_TFHD_FLAGS_SAMPLE_DESCRIPTION_INDEX_PRESENT) &&
      !gst_byte_reader_get_uint32_be (reader, &tfhd->sample_description_index))
    return FALSE;

  if ((tfhd->flags & GST_TFHD_FLAGS_DEFAULT_SAMPLE_DURATION_PRESENT) &&
      !gst_byte_reader_get_uint32_be (reader, &tfhd->default_sample_duration))
    return FALSE;

  if ((tfhd->flags & GST_TFHD_FLAGS_DEFAULT_SAMPLE_SIZE_PRESENT) &&
      !gst_byte_reader_get_uint32_be (reader, &tfhd->default_sample_size))
    return FALSE;

  if ((tfhd->flags & GST_TFHD_FLAGS_DEFAULT_SAMPLE_FLAGS_PRESENT) &&
      !gst_byte_reader_get_uint32_be (reader, &tfhd->default_sample_flags))
    return FALSE;

  return TRUE;
}

static gboolean
gst_isoff_trun_box_parse (GstTrunBox * trun, GstByteReader * reader)
{
  gint i;

  memset (trun, 0, sizeof (*trun));

  if (gst_byte_reader_get_remaining (reader) < 4)
    return FALSE;

  trun->version = gst_byte_reader_get_uint8_unchecked (reader);
  if (trun->version != 0 && trun->version != 1)
    return FALSE;

  trun->flags = gst_byte_reader_get_uint24_be_unchecked (reader);

  if (!gst_byte_reader_get_uint32_be (reader, &trun->sample_count))
    return FALSE;

  trun->samples =
      g_array_sized_new (FALSE, FALSE, sizeof (GstTrunSample),
      trun->sample_count);

  if ((trun->flags & GST_TRUN_FLAGS_DATA_OFFSET_PRESENT) &&
      !gst_byte_reader_get_uint32_be (reader, (guint32 *) & trun->data_offset))
    return FALSE;

  if ((trun->flags & GST_TRUN_FLAGS_FIRST_SAMPLE_FLAGS_PRESENT) &&
      !gst_byte_reader_get_uint32_be (reader, &trun->first_sample_flags))
    return FALSE;

  for (i = 0; i < trun->sample_count; i++) {
    GstTrunSample sample = { 0, };

    if ((trun->flags & GST_TRUN_FLAGS_SAMPLE_DURATION_PRESENT) &&
        !gst_byte_reader_get_uint32_be (reader, &sample.sample_duration))
      goto error;

    if ((trun->flags & GST_TRUN_FLAGS_SAMPLE_SIZE_PRESENT) &&
        !gst_byte_reader_get_uint32_be (reader, &sample.sample_size))
      goto error;

    if ((trun->flags & GST_TRUN_FLAGS_SAMPLE_FLAGS_PRESENT) &&
        !gst_byte_reader_get_uint32_be (reader, &sample.sample_flags))
      goto error;

    if ((trun->flags & GST_TRUN_FLAGS_SAMPLE_COMPOSITION_TIME_OFFSETS_PRESENT)
        && !gst_byte_reader_get_uint32_be (reader,
            &sample.sample_composition_time_offset.u))
      goto error;

    g_array_append_val (trun->samples, sample);
  }

  return TRUE;

error:
  gst_isoff_trun_box_clear (trun);
  return FALSE;
}

static gboolean
gst_isoff_traf_box_parse (GstTrafBox * traf, GstByteReader * reader)
{
  gboolean had_tfhd = FALSE;

  memset (traf, 0, sizeof (*traf));
  traf->trun = g_array_new (FALSE, FALSE, sizeof (GstTrunBox));
  g_array_set_clear_func (traf->trun,
      (GDestroyNotify) gst_isoff_trun_box_clear);

  while (gst_byte_reader_get_remaining (reader) > 0) {
    guint32 fourcc;
    guint header_size;
    guint64 size;

    if (!gst_isoff_parse_box_header (reader, &fourcc, NULL, &header_size,
            &size))
      goto error;
    if (gst_byte_reader_get_remaining (reader) < size - header_size)
      goto error;

    switch (fourcc) {
      case GST_ISOFF_FOURCC_TFHD:{
        GstByteReader sub_reader;

        gst_byte_reader_get_sub_reader (reader, &sub_reader,
            size - header_size);
        if (!gst_isoff_tfhd_box_parse (&traf->tfhd, &sub_reader))
          goto error;
        had_tfhd = TRUE;
        break;
      }
      case GST_ISOFF_FOURCC_TRUN:{
        GstByteReader sub_reader;
        GstTrunBox trun;

        gst_byte_reader_get_sub_reader (reader, &sub_reader,
            size - header_size);
        if (!gst_isoff_trun_box_parse (&trun, &sub_reader))
          goto error;

        g_array_append_val (traf->trun, trun);
        break;
      }
      default:
        gst_byte_reader_skip (reader, size - header_size);
        break;
    }
  }

  if (!had_tfhd)
    goto error;

  return TRUE;

error:
  gst_isoff_traf_box_clear (traf);

  return FALSE;
}

GstMoofBox *
gst_isoff_moof_box_parse (GstByteReader * reader)
{
  GstMoofBox *moof;
  gboolean had_mfhd = FALSE;

  moof = g_new0 (GstMoofBox, 1);
  moof->traf = g_array_new (FALSE, FALSE, sizeof (GstTrafBox));
  g_array_set_clear_func (moof->traf,
      (GDestroyNotify) gst_isoff_traf_box_clear);

  while (gst_byte_reader_get_remaining (reader) > 0) {
    guint32 fourcc;
    guint header_size;
    guint64 size;

    if (!gst_isoff_parse_box_header (reader, &fourcc, NULL, &header_size,
            &size))
      goto error;
    if (gst_byte_reader_get_remaining (reader) < size - header_size)
      goto error;

    switch (fourcc) {
      case GST_ISOFF_FOURCC_MFHD:{
        GstByteReader sub_reader;

        gst_byte_reader_get_sub_reader (reader, &sub_reader,
            size - header_size);
        if (!gst_isoff_mfhd_box_parse (&moof->mfhd, &sub_reader))
          goto error;
        had_mfhd = TRUE;
        break;
      }
      case GST_ISOFF_FOURCC_TRAF:{
        GstByteReader sub_reader;
        GstTrafBox traf;

        gst_byte_reader_get_sub_reader (reader, &sub_reader,
            size - header_size);
        if (!gst_isoff_traf_box_parse (&traf, &sub_reader))
          goto error;

        g_array_append_val (moof->traf, traf);
        break;
      }
      default:
        gst_byte_reader_skip (reader, size - header_size);
        break;
    }
  }

  if (!had_mfhd)
    goto error;

  return moof;

error:
  gst_isoff_moof_box_free (moof);
  return NULL;
}

void
gst_isoff_moof_box_free (GstMoofBox * moof)
{
  g_array_free (moof->traf, TRUE);
  g_free (moof);
}

void
gst_isoff_sidx_parser_init (GstSidxParser * parser)
{
  parser->status = GST_ISOFF_SIDX_PARSER_INIT;
  parser->cumulative_entry_size = 0;
  parser->sidx.entries = NULL;
  parser->sidx.entries_count = 0;
}

void
gst_isoff_sidx_parser_clear (GstSidxParser * parser)
{
  g_free (parser->sidx.entries);
  memset (parser, 0, sizeof (*parser));

  gst_isoff_sidx_parser_init (parser);
}

static void
gst_isoff_parse_sidx_entry (GstSidxBoxEntry * entry, GstByteReader * reader)
{
  guint32 aux;

  aux = gst_byte_reader_get_uint32_be_unchecked (reader);
  entry->ref_type = aux >> 31;
  entry->size = aux & 0x7FFFFFFF;
  entry->duration = gst_byte_reader_get_uint32_be_unchecked (reader);
  aux = gst_byte_reader_get_uint32_be_unchecked (reader);
  entry->starts_with_sap = aux >> 31;
  entry->sap_type = ((aux >> 28) & 0x7);
  entry->sap_delta_time = aux & 0xFFFFFFF;
}

GstIsoffParserResult
gst_isoff_sidx_parser_parse (GstSidxParser * parser,
    GstByteReader * reader, guint * consumed)
{
  GstIsoffParserResult res = GST_ISOFF_PARSER_OK;
  gsize remaining;

  switch (parser->status) {
    case GST_ISOFF_SIDX_PARSER_INIT:
      /* Try again once we have enough data for the FullBox header */
      if (gst_byte_reader_get_remaining (reader) < 4) {
        gst_byte_reader_set_pos (reader, 0);
        break;
      }
      parser->sidx.version = gst_byte_reader_get_uint8_unchecked (reader);
      parser->sidx.flags = gst_byte_reader_get_uint24_le_unchecked (reader);

      parser->status = GST_ISOFF_SIDX_PARSER_HEADER;

    case GST_ISOFF_SIDX_PARSER_HEADER:
      remaining = gst_byte_reader_get_remaining (reader);
      if (remaining < 12 + (parser->sidx.version == 0 ? 8 : 16)) {
        break;
      }

      parser->sidx.ref_id = gst_byte_reader_get_uint32_be_unchecked (reader);
      parser->sidx.timescale = gst_byte_reader_get_uint32_be_unchecked (reader);
      if (parser->sidx.version == 0) {
        parser->sidx.earliest_pts =
            gst_byte_reader_get_uint32_be_unchecked (reader);
        parser->sidx.first_offset =
            gst_byte_reader_get_uint32_be_unchecked (reader);
      } else {
        parser->sidx.earliest_pts =
            gst_byte_reader_get_uint64_be_unchecked (reader);
        parser->sidx.first_offset =
            gst_byte_reader_get_uint64_be_unchecked (reader);
      }
      /* skip 2 reserved bytes */
      gst_byte_reader_skip_unchecked (reader, 2);
      parser->sidx.entries_count =
          gst_byte_reader_get_uint16_be_unchecked (reader);

      GST_LOG ("Timescale: %" G_GUINT32_FORMAT, parser->sidx.timescale);
      GST_LOG ("Earliest pts: %" G_GUINT64_FORMAT, parser->sidx.earliest_pts);
      GST_LOG ("First offset: %" G_GUINT64_FORMAT, parser->sidx.first_offset);

      parser->cumulative_pts =
          gst_util_uint64_scale_int_round (parser->sidx.earliest_pts,
          GST_SECOND, parser->sidx.timescale);

      if (parser->sidx.entries_count) {
        parser->sidx.entries =
            g_malloc (sizeof (GstSidxBoxEntry) * parser->sidx.entries_count);
      }
      parser->sidx.entry_index = 0;

      parser->status = GST_ISOFF_SIDX_PARSER_DATA;

    case GST_ISOFF_SIDX_PARSER_DATA:
      while (parser->sidx.entry_index < parser->sidx.entries_count) {
        GstSidxBoxEntry *entry =
            &parser->sidx.entries[parser->sidx.entry_index];

        remaining = gst_byte_reader_get_remaining (reader);
        if (remaining < 12)
          break;

        entry->offset = parser->cumulative_entry_size;
        entry->pts = parser->cumulative_pts;
        gst_isoff_parse_sidx_entry (entry, reader);
        entry->duration = gst_util_uint64_scale_int_round (entry->duration,
            GST_SECOND, parser->sidx.timescale);
        parser->cumulative_entry_size += entry->size;
        parser->cumulative_pts += entry->duration;

        GST_LOG ("Sidx entry %d) offset: %" G_GUINT64_FORMAT ", pts: %"
            GST_TIME_FORMAT ", duration %" GST_TIME_FORMAT " - size %"
            G_GUINT32_FORMAT, parser->sidx.entry_index, entry->offset,
            GST_TIME_ARGS (entry->pts), GST_TIME_ARGS (entry->duration),
            entry->size);

        parser->sidx.entry_index++;
      }

      if (parser->sidx.entry_index == parser->sidx.entries_count)
        parser->status = GST_ISOFF_SIDX_PARSER_FINISHED;
      else
        break;
    case GST_ISOFF_SIDX_PARSER_FINISHED:
      parser->sidx.entry_index = 0;
      res = GST_ISOFF_PARSER_DONE;
      break;
  }

  *consumed = gst_byte_reader_get_pos (reader);

  return res;
}

GstIsoffParserResult
gst_isoff_sidx_parser_add_buffer (GstSidxParser * parser, GstBuffer * buffer,
    guint * consumed)
{
  GstIsoffParserResult res = GST_ISOFF_PARSER_OK;
  GstByteReader reader;
  GstMapInfo info;
  guint32 fourcc;

  if (!gst_buffer_map (buffer, &info, GST_MAP_READ)) {
    *consumed = 0;
    return GST_ISOFF_PARSER_ERROR;
  }

  gst_byte_reader_init (&reader, info.data, info.size);

  if (parser->status == GST_ISOFF_SIDX_PARSER_INIT) {
    if (!gst_isoff_parse_box_header (&reader, &fourcc, NULL, NULL,
            &parser->size))
      goto done;

    if (fourcc != GST_ISOFF_FOURCC_SIDX) {
      res = GST_ISOFF_PARSER_UNEXPECTED;
      gst_byte_reader_set_pos (&reader, 0);
      goto done;
    }

    if (parser->size == 0) {
      res = GST_ISOFF_PARSER_ERROR;
      gst_byte_reader_set_pos (&reader, 0);
      goto done;
    }

    /* Try again once we have enough data for the FullBox header */
    if (gst_byte_reader_get_remaining (&reader) < 4) {
      gst_byte_reader_set_pos (&reader, 0);
      goto done;
    }
  }

  res = gst_isoff_sidx_parser_parse (parser, &reader, consumed);

done:
  gst_buffer_unmap (buffer, &info);
  return res;
}
