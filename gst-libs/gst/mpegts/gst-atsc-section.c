/*
 * Copyright (C) 2014 Stefan Ringel
 *
 * Authors:
 *   Stefan Ringel <linuxtv@stefanringel.de>
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

#include <string.h>
#include <stdlib.h>

#include "mpegts.h"
#include "gstmpegts-private.h"

/**
 * SECTION:gst-atsc-section
 * @title: ATSC variants of MPEG-TS sections
 * @short_description: Sections for the various ATSC specifications
 * @include: gst/mpegts/mpegts.h
 *
 */

/* Terrestrial Virtual Channel Table TVCT */
static GstMpegTsAtscTVCTSource *
_gst_mpegts_atsc_tvct_source_copy (GstMpegTsAtscTVCTSource * source)
{
  GstMpegTsAtscTVCTSource *copy;

  copy = g_slice_dup (GstMpegTsAtscTVCTSource, source);
  copy->descriptors = g_ptr_array_ref (source->descriptors);

  return copy;
}

static void
_gst_mpegts_atsc_tvct_source_free (GstMpegTsAtscTVCTSource * source)
{
  g_ptr_array_unref (source->descriptors);
  g_slice_free (GstMpegTsAtscTVCTSource, source);
}

G_DEFINE_BOXED_TYPE (GstMpegTsAtscTVCTSource, gst_mpegts_atsc_tvct_source,
    (GBoxedCopyFunc) _gst_mpegts_atsc_tvct_source_copy,
    (GFreeFunc) _gst_mpegts_atsc_tvct_source_free);

static GstMpegTsAtscTVCT *
_gst_mpegts_atsc_tvct_copy (GstMpegTsAtscTVCT * tvct)
{
  GstMpegTsAtscTVCT *copy;

  copy = g_slice_dup (GstMpegTsAtscTVCT, tvct);
  copy->sources = g_ptr_array_ref (tvct->sources);
  copy->descriptors = g_ptr_array_ref (tvct->descriptors);

  return copy;
}

static void
_gst_mpegts_atsc_tvct_free (GstMpegTsAtscTVCT * tvct)
{
  g_ptr_array_unref (tvct->sources);
  g_ptr_array_unref (tvct->descriptors);
  g_slice_free (GstMpegTsAtscTVCT, tvct);
}

G_DEFINE_BOXED_TYPE (GstMpegTsAtscTVCT, gst_mpegts_atsc_tvct,
    (GBoxedCopyFunc) _gst_mpegts_atsc_tvct_copy,
    (GFreeFunc) _gst_mpegts_atsc_tvct_free);

static gpointer
_parse_atsc_tvct (GstMpegTsSection * section)
{
  GstMpegTsAtscTVCT *tvct = NULL;
  guint8 *data, *end, source_nb;
  guint32 tmp32;
  guint16 descriptors_loop_length, tmp16;

  tvct = g_slice_new0 (GstMpegTsAtscTVCT);

  data = section->data;
  end = data + section->section_length;

  tvct->transport_stream_id = section->subtable_extension;

  /* Skip already parsed data */
  data += 8;

  /* minimum size */
  if (data - end < 2 + 2 + 4)
    goto error;

  tvct->protocol_version = *data;
  data += 1;

  source_nb = *data;
  data += 1;

  tvct->sources = g_ptr_array_new_full (source_nb,
      (GDestroyNotify) _gst_mpegts_atsc_tvct_source_free);

  for (guint i = 0; i < source_nb; i++) {
    GstMpegTsAtscTVCTSource *source;

    /* minimum 32 bytes for a entry, 2 bytes second descriptor
       loop-length, 4 bytes crc */
    if (end - data < 32 + 2 + 4)
      goto error;

    source = g_slice_new0 (GstMpegTsAtscTVCTSource);
    g_ptr_array_add (tvct->sources, source);

    /* FIXME: 7 utf16 charater
       GST_READ_UINT16_BE x 7 or extern method ? */
    source->short_name = g_memdup (data, 14);
    data += 14;

    tmp32 = GST_READ_UINT32_BE (data);
    source->major_channel_number = (tmp32 >> 18) & 0x03FF;
    source->minor_channel_number = (tmp32 >> 8) & 0x03FF;
    source->modulation_mode = tmp32 & 0xF;
    data += 4;

    source->carrier_frequency = GST_READ_UINT32_BE (data);
    data += 4;

    source->channel_TSID = GST_READ_UINT16_BE (data);
    data += 2;

    source->program_number = GST_READ_UINT16_BE (data);
    data += 2;

    tmp16 = GST_READ_UINT16_BE (data);
    source->ETM_location = (tmp16 >> 14) & 0x3;
    source->access_controlled = (tmp16 >> 13) & 0x1;
    source->hidden = (tmp16 >> 12) & 0x1;
    source->hide_guide = (tmp16 >> 10) & 0x1;
    source->service_type = tmp16 & 0x3f;
    data += 2;

    source->source_id = GST_READ_UINT16_BE (data);
    data += 2;

    descriptors_loop_length = GST_READ_UINT16_BE (data) & 0x03FF;
    data += 2;

    if (end - data < descriptors_loop_length + 6)
      goto error;

    source->descriptors =
        gst_mpegts_parse_descriptors (data, descriptors_loop_length);
    if (source->descriptors == NULL)
      goto error;
    data += descriptors_loop_length;
  }

  descriptors_loop_length = GST_READ_UINT16_BE (data) & 0x03FF;
  data += 2;

  if (end - data < descriptors_loop_length + 4)
    goto error;

  tvct->descriptors =
      gst_mpegts_parse_descriptors (data, descriptors_loop_length);
  if (tvct->descriptors == NULL)
    goto error;
  data += descriptors_loop_length;

  return (gpointer) tvct;

error:
  if (tvct)
    _gst_mpegts_atsc_tvct_free (tvct);

  return NULL;
}

/**
 * gst_mpegts_section_get_atsc_tvct:
 * @section: a #GstMpegTsSection of type %GST_MPEGTS_SECTION_ATSC_TVCT
 *
 * Returns the #GstMpegTsAtscTVCT contained in the @section
 *
 * Returns: The #GstMpegTsAtscTVCT contained in the section, or %NULL if an error
 * happened.
 */
const GstMpegTsAtscTVCT *
gst_mpegts_section_get_atsc_tvct (GstMpegTsSection * section)
{
  g_return_val_if_fail (section->section_type == GST_MPEGTS_SECTION_ATSC_TVCT,
      NULL);
  g_return_val_if_fail (section->cached_parsed || section->data, NULL);

  if (!section->cached_parsed)
    section->cached_parsed =
        __common_section_checks (section, 16, _parse_atsc_tvct,
        (GDestroyNotify) _gst_mpegts_atsc_tvct_free);

  return (const GstMpegTsAtscTVCT *) section->cached_parsed;
}
