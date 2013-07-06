/*
 * gstmpegtssection.c -
 * Copyright (C) 2013 Edward Hervey
 * Copyright (C) 2011, Hewlett-Packard Development Company, L.P.
 * Copyright (C) 2007 Alessandro Decina
 *               2010 Edward Hervey
 *  Author: Youness Alaoui <youness.alaoui@collabora.co.uk>, Collabora Ltd.
 *  Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>, Collabora Ltd.
 *  Author: Edward Hervey <bilboed@bilboed.com>, Collabora Ltd.
 *
 * Authors:
 *   Alessandro Decina <alessandro@nnva.org>
 *   Zaheer Abbas Merali <zaheerabbas at merali dot org>
 *   Edward Hervey <edward@collabora.com>
 *
 * This library is free softwagre; you can redistribute it and/or
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
 * SECTION:gst-dvb-section
 * @title: DVB variants of MPEG-TS sections
 * @short_description: Sections for the various DVB specifications
 * @include: gst/mpegts/mpegts.h
 *
 */


/*
 * TODO
 *
 * * Check minimum size for section parsing in the various 
 *   gst_mpegts_section_get_<tabld>() methods
 *
 * * Implement parsing code for
 *   * BAT
 *   * CAT
 *   * TSDT
 */

static inline GstDateTime *
_parse_utc_time (guint8 * data)
{
  guint year, month, day, hour, minute, second;
  guint16 mjd;
  guint8 *utc_ptr;

  mjd = GST_READ_UINT16_BE (data);

  if (mjd == G_MAXUINT16)
    return NULL;

  /* See EN 300 468 Annex C */
  year = (guint32) (((mjd - 15078.2) / 365.25));
  month = (guint8) ((mjd - 14956.1 - (guint) (year * 365.25)) / 30.6001);
  day = mjd - 14956 - (guint) (year * 365.25) - (guint) (month * 30.6001);
  if (month == 14 || month == 15) {
    year++;
    month = month - 1 - 12;
  } else {
    month--;
  }
  year += 1900;

  utc_ptr = data + 2;

  /* First digit of hours cannot exceeed 2 (max: 23 hours) */
  hour = ((utc_ptr[0] & 0x30) >> 4) * 10 + (utc_ptr[0] & 0x0F);
  /* First digit of minutes cannot exced 5 (max: 59 mins) */
  minute = ((utc_ptr[1] & 0x70) >> 4) * 10 + (utc_ptr[1] & 0x0F);
  /* first digit of seconds cannot exceed 5 (max: 59 seconds) */
  second = ((utc_ptr[2] & 0x70) >> 4) * 10 + (utc_ptr[2] & 0x0F);

  /* Time is UTC */
  return gst_date_time_new (0.0, year, month, day, hour, minute,
      (gdouble) second);
}

/* Event Information Table */
static GstMpegTsEITEvent *
_gst_mpegts_eit_event_copy (GstMpegTsEITEvent * eit)
{
  /* FIXME : IMPLEMENT */
  return NULL;
}

static void
_gst_mpegts_eit_event_free (GstMpegTsEITEvent * eit)
{
  if (eit->start_time)
    gst_date_time_unref (eit->start_time);
  g_array_unref (eit->descriptors);
  g_slice_free (GstMpegTsEITEvent, eit);
}

G_DEFINE_BOXED_TYPE (GstMpegTsEITEvent, gst_mpegts_eit_event,
    (GBoxedCopyFunc) _gst_mpegts_eit_event_copy,
    (GFreeFunc) _gst_mpegts_eit_event_free);

static GstMpegTsEIT *
_gst_mpegts_eit_copy (GstMpegTsEIT * eit)
{
  /* FIXME : IMPLEMENT */
  return NULL;
}

static void
_gst_mpegts_eit_free (GstMpegTsEIT * eit)
{
  g_ptr_array_unref (eit->events);
  g_slice_free (GstMpegTsEIT, eit);
}

G_DEFINE_BOXED_TYPE (GstMpegTsEIT, gst_mpegts_eit,
    (GBoxedCopyFunc) _gst_mpegts_eit_copy, (GFreeFunc) _gst_mpegts_eit_free);


static GstMpegTsEIT *
_parse_eit (GstMpegTsSection * section)
{
  GstMpegTsEIT *eit = NULL;
  guint i = 0, allocated_events = 12;
  guint8 *data, *end, *duration_ptr;
  guint16 descriptors_loop_length;

  /* fixed header + CRC == 16 */
  if (section->section_length < 18) {
    GST_WARNING ("PID %d invalid EIT size %d",
        section->pid, section->section_length);
    goto error;
  }

  eit = g_slice_new0 (GstMpegTsEIT);

  data = section->data;
  end = data + section->section_length;

  /* Skip already parsed data */
  data += 8;

  eit->transport_stream_id = GST_READ_UINT16_BE (data);
  data += 2;
  eit->original_network_id = GST_READ_UINT16_BE (data);
  data += 2;
  eit->segment_last_section_number = *data++;
  eit->last_table_id = *data++;

  eit->actual_stream = (section->table_id == 0x4E ||
      (section->table_id >= 0x50 && section->table_id <= 0x5F));
  eit->present_following = (section->table_id == 0x4E
      || section->table_id == 0x4F);

  eit->events =
      g_ptr_array_new_full (allocated_events,
      (GDestroyNotify) _gst_mpegts_eit_event_free);

  while (data < end - 4) {
    GstMpegTsEITEvent *event;

    /* 12 is the minimum entry size + CRC */
    if (end - data < 12 + 4) {
      GST_WARNING ("PID %d invalid EIT entry length %d",
          section->pid, (gint) (end - 4 - data));
      goto error;
    }

    event = g_slice_new0 (GstMpegTsEITEvent);
    g_ptr_array_add (eit->events, event);

    event->event_id = GST_READ_UINT16_BE (data);
    data += 2;

    event->start_time = _parse_utc_time (data);
    duration_ptr = data + 5;
    event->duration = (((duration_ptr[0] & 0xF0) >> 4) * 10 +
        (duration_ptr[0] & 0x0F)) * 60 * 60 +
        (((duration_ptr[1] & 0xF0) >> 4) * 10 +
        (duration_ptr[1] & 0x0F)) * 60 +
        ((duration_ptr[2] & 0xF0) >> 4) * 10 + (duration_ptr[2] & 0x0F);

    data += 8;
    event->running_status = *data >> 5;
    event->free_CA_mode = (*data >> 4) & 0x01;

    descriptors_loop_length = GST_READ_UINT16_BE (data) & 0x0FFF;
    data += 2;

    event->descriptors =
        gst_mpegts_parse_descriptors (data, descriptors_loop_length);
    if (event->descriptors == NULL)
      goto error;
    data += descriptors_loop_length;

    i += 1;
  }

  if (data != end - 4) {
    GST_WARNING ("PID %d invalid EIT parsed %d length %d",
        section->pid, (gint) (data - section->data), section->section_length);
    goto error;
  }

  return eit;

error:
  if (eit)
    gst_mpegts_section_unref (eit);

  return NULL;

}

/**
 * gst_mpegts_section_get_eit:
 * @section: a #GstMpegTsSection of type %GST_MPEGTS_SECTION_EIT
 *
 * Returns the #GstMpegTsEIT contained in the @section.
 *
 * Returns: The #GstMpegTsEIT contained in the section, or %NULL if an error
 * happened.
 */
const GstMpegTsEIT *
gst_mpegts_section_get_eit (GstMpegTsSection * section)
{
  g_return_val_if_fail (section->section_type == GST_MPEGTS_SECTION_EIT, NULL);
  g_return_val_if_fail (section->cached_parsed || section->data, NULL);

  if (!section->cached_parsed) {
    if (G_UNLIKELY (_calc_crc32 (section->data, section->section_length) != 0))
      goto bad_crc;

    section->cached_parsed = (gpointer) _parse_eit (section);
    section->destroy_parsed = (GDestroyNotify) _gst_mpegts_eit_free;
    if (section->cached_parsed == NULL)
      goto parse_failure;
  }

  return (const GstMpegTsEIT *) section->cached_parsed;

bad_crc:
  {
    GST_WARNING ("Bad CRC on section");
    return NULL;
  }

parse_failure:
  {
    GST_WARNING ("Failure to parse section");
    return NULL;
  }
}

/* Bouquet Association Table */
static GstMpegTsBAT *
_gst_mpegts_bat_copy (GstMpegTsBAT * bat)
{
  /* FIXME : IMPLEMENT */
  return NULL;
}

static void
_gst_mpegts_bat_free (GstMpegTsBAT * bat)
{
  /* FIXME: IMPLEMENT */
}

G_DEFINE_BOXED_TYPE (GstMpegTsBAT, gst_mpegts_bat,
    (GBoxedCopyFunc) _gst_mpegts_bat_copy, (GFreeFunc) _gst_mpegts_bat_free);

/* Network Information Table */

static GstMpegTsNITStream *
_gst_mpegts_nit_stream_copy (GstMpegTsNITStream * nit)
{
  /* FIXME : IMPLEMENT */
  return NULL;
}

static void
_gst_mpegts_nit_stream_free (GstMpegTsNITStream * nit)
{
  g_array_unref (nit->descriptors);
  g_slice_free (GstMpegTsNITStream, nit);
}

G_DEFINE_BOXED_TYPE (GstMpegTsNITStream, gst_mpegts_nit_stream,
    (GBoxedCopyFunc) _gst_mpegts_nit_stream_copy,
    (GFreeFunc) _gst_mpegts_nit_stream_free);

static GstMpegTsNIT *
_gst_mpegts_nit_copy (GstMpegTsNIT * nit)
{
  /* FIXME : IMPLEMENT */
  return NULL;
}

static void
_gst_mpegts_nit_free (GstMpegTsNIT * nit)
{
  g_array_unref (nit->descriptors);
  g_ptr_array_unref (nit->streams);
  g_slice_free (GstMpegTsNIT, nit);
}

G_DEFINE_BOXED_TYPE (GstMpegTsNIT, gst_mpegts_nit,
    (GBoxedCopyFunc) _gst_mpegts_nit_copy, (GFreeFunc) _gst_mpegts_nit_free);


static GstMpegTsNIT *
_parse_nit (GstMpegTsSection * section)
{
  GstMpegTsNIT *nit = NULL;
  guint i = 0, allocated_streams = 12;
  guint8 *data, *end, *entry_begin;
  guint16 descriptors_loop_length, transport_stream_loop_length;

  GST_DEBUG ("NIT");

  /* fixed header (no streams) + CRC == 16 */
  if (section->section_length < 16) {
    GST_WARNING ("PID %d invalid NIT size %d",
        section->pid, section->section_length);
    goto error;
  }

  nit = g_slice_new0 (GstMpegTsNIT);

  data = section->data;
  end = data + section->section_length;

  /* Skip already parsed data */
  data += 8;

  nit->actual_network = section->table_id == 0x40;

  descriptors_loop_length = GST_READ_UINT16_BE (data) & 0x0FFF;
  data += 2;

  /* see if the buffer is large enough */
  if (descriptors_loop_length && (data + descriptors_loop_length > end - 4)) {
    GST_WARNING ("PID %d invalid NIT descriptors loop length %d",
        section->pid, descriptors_loop_length);
    goto error;
  }
  nit->descriptors =
      gst_mpegts_parse_descriptors (data, descriptors_loop_length);
  if (nit->descriptors == NULL)
    goto error;
  data += descriptors_loop_length;

  transport_stream_loop_length = GST_READ_UINT16_BE (data) & 0x0FFF;
  data += 2;
  if (G_UNLIKELY (transport_stream_loop_length > (end - 4 - data))) {
    GST_WARNING
        ("PID 0x%04x invalid NIT (transport_stream_loop_length too big)",
        section->pid);
    goto error;
  }

  nit->streams =
      g_ptr_array_new_full (allocated_streams,
      (GDestroyNotify) _gst_mpegts_nit_stream_free);

  /* read up to the CRC */
  while (transport_stream_loop_length - 4 > 0) {
    GstMpegTsNITStream *stream = g_slice_new0 (GstMpegTsNITStream);

    g_ptr_array_add (nit->streams, stream);

    if (transport_stream_loop_length < 6) {
      /* each entry must be at least 6 bytes (+ 4bytes CRC) */
      GST_WARNING ("PID %d invalid NIT entry size %d",
          section->pid, transport_stream_loop_length);
      goto error;
    }

    entry_begin = data;

    stream->transport_stream_id = GST_READ_UINT16_BE (data);
    data += 2;

    stream->original_network_id = GST_READ_UINT16_BE (data);
    data += 2;

    descriptors_loop_length = GST_READ_UINT16_BE (data) & 0x0FFF;
    data += 2;

    GST_DEBUG ("descriptors_loop_length %d", descriptors_loop_length);

    if (descriptors_loop_length && (data + descriptors_loop_length > end - 4)) {
      GST_WARNING
          ("PID %d invalid NIT entry %d descriptors loop length %d (only have %"
          G_GSIZE_FORMAT ")", section->pid, section->subtable_extension,
          descriptors_loop_length, end - 4 - data);
      goto error;
    }
    stream->descriptors =
        gst_mpegts_parse_descriptors (data, descriptors_loop_length);
    if (stream->descriptors == NULL)
      goto error;

    data += descriptors_loop_length;

    i += 1;
    transport_stream_loop_length -= data - entry_begin;
  }

  if (data != end - 4) {
    GST_WARNING ("PID %d invalid NIT parsed %d length %d",
        section->pid, (gint) (data - section->data), section->section_length);
    goto error;
  }

  return nit;

error:
  if (nit)
    gst_mpegts_section_unref (nit);

  return NULL;
}

/**
 * gst_mpegts_section_get_nit:
 * @section: a #GstMpegTsSection of type %GST_MPEGTS_SECTION_NIT
 *
 * Returns the #GstMpegTsNIT contained in the @section.
 *
 * Returns: The #GstMpegTsNIT contained in the section, or %NULL if an error
 * happened.
 */
const GstMpegTsNIT *
gst_mpegts_section_get_nit (GstMpegTsSection * section)
{
  g_return_val_if_fail (section->section_type == GST_MPEGTS_SECTION_NIT, NULL);
  g_return_val_if_fail (section->cached_parsed || section->data, NULL);

  if (!section->cached_parsed) {
    if (G_UNLIKELY (_calc_crc32 (section->data, section->section_length) != 0))
      goto bad_crc;

    section->cached_parsed = (gpointer) _parse_nit (section);
    section->destroy_parsed = (GDestroyNotify) _gst_mpegts_nit_free;
    if (section->cached_parsed == NULL)
      goto parse_failure;
  }

  return (const GstMpegTsNIT *) section->cached_parsed;

bad_crc:
  {
    GST_WARNING ("Bad CRC on section");
    return NULL;
  }

parse_failure:
  {
    GST_WARNING ("Failure to parse section");
    return NULL;
  }
}


/* Service Description Table (SDT) */

static GstMpegTsSDTService *
_gst_mpegts_sdt_service_copy (GstMpegTsSDTService * sdt)
{
  /* FIXME : IMPLEMENT */
  return NULL;
}

static void
_gst_mpegts_sdt_service_free (GstMpegTsSDTService * sdt)
{
  g_array_unref (sdt->descriptors);
  g_slice_free (GstMpegTsSDTService, sdt);
}

G_DEFINE_BOXED_TYPE (GstMpegTsSDTService, gst_mpegts_sdt_service,
    (GBoxedCopyFunc) _gst_mpegts_sdt_service_copy,
    (GFreeFunc) _gst_mpegts_sdt_service_free);

static GstMpegTsSDT *
_gst_mpegts_sdt_copy (GstMpegTsSDT * sdt)
{
  /* FIXME : IMPLEMENT */
  return NULL;
}

static void
_gst_mpegts_sdt_free (GstMpegTsSDT * sdt)
{
  g_ptr_array_unref (sdt->services);
  g_slice_free (GstMpegTsSDT, sdt);
}

G_DEFINE_BOXED_TYPE (GstMpegTsSDT, gst_mpegts_sdt,
    (GBoxedCopyFunc) _gst_mpegts_sdt_copy, (GFreeFunc) _gst_mpegts_sdt_free);


static GstMpegTsSDT *
_parse_sdt (GstMpegTsSection * section)
{
  GstMpegTsSDT *sdt = NULL;
  guint i = 0, allocated_services = 8;
  guint8 *data, *end, *entry_begin;
  guint tmp;
  guint sdt_info_length;
  guint descriptors_loop_length;

  GST_DEBUG ("SDT");

  /* fixed header + CRC == 16 */
  if (section->section_length < 14) {
    GST_WARNING ("PID %d invalid SDT size %d",
        section->pid, section->section_length);
    goto error;
  }

  sdt = g_slice_new0 (GstMpegTsSDT);

  data = section->data;
  end = data + section->section_length;

  /* Skip common fields */
  data += 8;

  sdt->original_network_id = GST_READ_UINT16_BE (data);
  data += 2;

  /* skip reserved byte */
  data += 1;

  sdt->actual_ts = section->table_id == 0x42;

  sdt_info_length = section->section_length - 11;

  sdt->services = g_ptr_array_new_full (allocated_services,
      (GDestroyNotify) _gst_mpegts_sdt_service_free);

  /* read up to the CRC */
  while (sdt_info_length - 4 > 0) {
    GstMpegTsSDTService *service = g_slice_new0 (GstMpegTsSDTService);
    g_ptr_array_add (sdt->services, service);

    entry_begin = data;

    if (sdt_info_length < 9) {
      /* each entry must be at least 5 bytes (+4 bytes for the CRC) */
      GST_WARNING ("PID %d invalid SDT entry size %d",
          section->pid, sdt_info_length);
      goto error;
    }

    service->service_id = GST_READ_UINT16_BE (data);
    data += 2;

    service->EIT_schedule_flag = ((*data & 0x02) == 2);
    service->EIT_present_following_flag = (*data & 0x01) == 1;

    data += 1;
    tmp = GST_READ_UINT16_BE (data);

    service->running_status = (*data >> 5) & 0x07;
    service->free_CA_mode = (*data >> 4) & 0x01;

    descriptors_loop_length = tmp & 0x0FFF;
    data += 2;

    if (descriptors_loop_length && (data + descriptors_loop_length > end - 4)) {
      GST_WARNING ("PID %d invalid SDT entry %d descriptors loop length %d",
          section->pid, service->service_id, descriptors_loop_length);
      goto error;
    }
    service->descriptors =
        gst_mpegts_parse_descriptors (data, descriptors_loop_length);
    if (!service->descriptors)
      goto error;
    data += descriptors_loop_length;

    sdt_info_length -= data - entry_begin;
    i += 1;
  }

  if (data != end - 4) {
    GST_WARNING ("PID %d invalid SDT parsed %d length %d",
        section->pid, (gint) (data - section->data), section->section_length);
    goto error;
  }

  return sdt;

error:
  if (sdt)
    _gst_mpegts_sdt_free (sdt);

  return NULL;
}

/**
 * gst_mpegts_section_get_sdt:
 * @section: a #GstMpegTsSection of type %GST_MPEGTS_SECTION_SDT
 *
 * Returns the #GstMpegTsSDT contained in the @section.
 *
 * Returns: The #GstMpegTsSDT contained in the section, or %NULL if an error
 * happened.
 */
const GstMpegTsSDT *
gst_mpegts_section_get_sdt (GstMpegTsSection * section)
{
  g_return_val_if_fail (section->section_type == GST_MPEGTS_SECTION_SDT, NULL);
  g_return_val_if_fail (section->cached_parsed || section->data, NULL);

  if (!section->cached_parsed) {
    if (G_UNLIKELY (_calc_crc32 (section->data, section->section_length) != 0))
      goto bad_crc;

    section->cached_parsed = (gpointer) _parse_sdt (section);
    section->destroy_parsed = (GDestroyNotify) _gst_mpegts_sdt_free;
    if (section->cached_parsed == NULL)
      goto parse_failure;
  }

  return (const GstMpegTsSDT *) section->cached_parsed;

bad_crc:
  {
    GST_WARNING ("Bad CRC on section");
    return NULL;
  }

parse_failure:
  {
    GST_WARNING ("Failure to parse section");
    return NULL;
  }
}

/* Time and Date Table (TDT) */
static GstDateTime *
_parse_tdt (GstMpegTsSection * section)
{
  /* FIXME : Add length check */
  return _parse_utc_time (section->data + 3);
}

/**
 * gst_mpegts_section_get_tdt:
 * @section: a #GstMpegTsSection of type %GST_MPEGTS_SECTION_TDT
 *
 * Returns the #GstDateTime of the TDT
 *
 * Returns: The #GstDateTime contained in the section, or %NULL
 * if an error happened. Release with #gst_date_time_unref when done.
 */
GstDateTime *
gst_mpegts_section_get_tdt (GstMpegTsSection * section)
{
  g_return_val_if_fail (section->section_type == GST_MPEGTS_SECTION_TDT, NULL);
  g_return_val_if_fail (section->cached_parsed || section->data, NULL);

  if (!section->cached_parsed) {
    section->cached_parsed = (gpointer) _parse_tdt (section);
    section->destroy_parsed = (GDestroyNotify) gst_date_time_unref;
    if (section->cached_parsed == NULL)
      goto parse_failure;
  }

  return gst_date_time_ref ((GstDateTime *) section->cached_parsed);

parse_failure:
  {
    GST_WARNING ("Failure to parse section");
    return NULL;
  }
}


/* Time Offset Table (TOT) */
static GstMpegTsTOT *
_gst_mpegts_tot_copy (GstMpegTsTOT * tot)
{
  /* FIXME : IMPLEMENT */
  return NULL;
}

static void
_gst_mpegts_tot_free (GstMpegTsTOT * tot)
{
  if (tot->utc_time)
    gst_date_time_unref (tot->utc_time);
  g_array_unref (tot->descriptors);
  g_slice_free (GstMpegTsTOT, tot);
}

G_DEFINE_BOXED_TYPE (GstMpegTsTOT, gst_mpegts_tot,
    (GBoxedCopyFunc) _gst_mpegts_tot_copy, (GFreeFunc) _gst_mpegts_tot_free);

static GstMpegTsTOT *
_parse_tot (GstMpegTsSection * section)
{
  guint8 *data;
  GstMpegTsTOT *tot;
  guint16 desc_len;

  /* FIXME : Check minimum length */

  GST_DEBUG ("TOT");

  tot = g_slice_new0 (GstMpegTsTOT);

  tot->utc_time = _parse_utc_time (section->data + 3);

  /* Skip 5 bytes from utc_time (+3 of initial offset) */
  data = section->data + 8;

  desc_len = GST_READ_UINT16_BE (data) & 0xFFF;
  data += 2;
  tot->descriptors = gst_mpegts_parse_descriptors (data, desc_len);

  return tot;
}

/**
 * gst_mpegts_section_get_tot:
 * @section: a #GstMpegTsSection of type %GST_MPEGTS_SECTION_TOT
 *
 * Returns the #GstMpegTsTOT contained in the @section.
 *
 * Returns: The #GstMpegTsTOT contained in the section, or %NULL if an error
 * happened.
 */
const GstMpegTsTOT *
gst_mpegts_section_get_tot (GstMpegTsSection * section)
{
  g_return_val_if_fail (section->section_type == GST_MPEGTS_SECTION_TOT, NULL);
  g_return_val_if_fail (section->cached_parsed || section->data, NULL);

  if (!section->cached_parsed) {
    if (G_UNLIKELY (_calc_crc32 (section->data, section->section_length) != 0))
      goto bad_crc;

    section->cached_parsed = (gpointer) _parse_tot (section);
    section->destroy_parsed = (GDestroyNotify) _gst_mpegts_tot_free;
    if (section->cached_parsed == NULL)
      goto parse_failure;
  }

  return (const GstMpegTsTOT *) section->cached_parsed;

bad_crc:
  {
    GST_WARNING ("Bad CRC on section");
    return NULL;
  }

parse_failure:
  {
    GST_WARNING ("Failure to parse section");
    return NULL;
  }
}
