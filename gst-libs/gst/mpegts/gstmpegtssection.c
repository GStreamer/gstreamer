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
 * SECTION:gstmpegts
 * @title: Mpeg-ts helper library
 * @short_description: Mpeg-ts helper library for plugins and applications
 * @include: gst/mpegts/mpegts.h
 */

/**
 * SECTION:gstmpegtssection
 * @title: Base MPEG-TS sections
 * @short_description: Sections for ITU H.222.0 | ISO/IEC 13818-1 
 * @include: gst/mpegts/mpegts.h
 *
 * For more details, refer to the ITU H.222.0 or ISO/IEC 13818-1 specifications
 * and other specifications mentionned in the documentation.
 */

/* FIXME : Move this to proper file once we have a C file for it */
/**
 * SECTION:gst-atsc-section
 * @title: ATSC variants of MPEG-TS sections
 * @short_description: Sections for the various ATSC specifications
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

GST_DEBUG_CATEGORY (gst_mpegts_debug);

static GQuark QUARK_PAT;
static GQuark QUARK_CAT;
static GQuark QUARK_BAT;
static GQuark QUARK_PMT;
static GQuark QUARK_NIT;
static GQuark QUARK_SDT;
static GQuark QUARK_EIT;
static GQuark QUARK_TDT;
static GQuark QUARK_TOT;
static GQuark QUARK_SECTION;

static GType _gst_mpegts_section_type = 0;
#define MPEG_TYPE_TS_SECTION (_gst_mpegts_section_type)
GST_DEFINE_MINI_OBJECT_TYPE (GstMpegTsSection, gst_mpegts_section);

static const guint32 crc_tab[256] = {
  0x00000000, 0x04c11db7, 0x09823b6e, 0x0d4326d9, 0x130476dc, 0x17c56b6b,
  0x1a864db2, 0x1e475005, 0x2608edb8, 0x22c9f00f, 0x2f8ad6d6, 0x2b4bcb61,
  0x350c9b64, 0x31cd86d3, 0x3c8ea00a, 0x384fbdbd, 0x4c11db70, 0x48d0c6c7,
  0x4593e01e, 0x4152fda9, 0x5f15adac, 0x5bd4b01b, 0x569796c2, 0x52568b75,
  0x6a1936c8, 0x6ed82b7f, 0x639b0da6, 0x675a1011, 0x791d4014, 0x7ddc5da3,
  0x709f7b7a, 0x745e66cd, 0x9823b6e0, 0x9ce2ab57, 0x91a18d8e, 0x95609039,
  0x8b27c03c, 0x8fe6dd8b, 0x82a5fb52, 0x8664e6e5, 0xbe2b5b58, 0xbaea46ef,
  0xb7a96036, 0xb3687d81, 0xad2f2d84, 0xa9ee3033, 0xa4ad16ea, 0xa06c0b5d,
  0xd4326d90, 0xd0f37027, 0xddb056fe, 0xd9714b49, 0xc7361b4c, 0xc3f706fb,
  0xceb42022, 0xca753d95, 0xf23a8028, 0xf6fb9d9f, 0xfbb8bb46, 0xff79a6f1,
  0xe13ef6f4, 0xe5ffeb43, 0xe8bccd9a, 0xec7dd02d, 0x34867077, 0x30476dc0,
  0x3d044b19, 0x39c556ae, 0x278206ab, 0x23431b1c, 0x2e003dc5, 0x2ac12072,
  0x128e9dcf, 0x164f8078, 0x1b0ca6a1, 0x1fcdbb16, 0x018aeb13, 0x054bf6a4,
  0x0808d07d, 0x0cc9cdca, 0x7897ab07, 0x7c56b6b0, 0x71159069, 0x75d48dde,
  0x6b93dddb, 0x6f52c06c, 0x6211e6b5, 0x66d0fb02, 0x5e9f46bf, 0x5a5e5b08,
  0x571d7dd1, 0x53dc6066, 0x4d9b3063, 0x495a2dd4, 0x44190b0d, 0x40d816ba,
  0xaca5c697, 0xa864db20, 0xa527fdf9, 0xa1e6e04e, 0xbfa1b04b, 0xbb60adfc,
  0xb6238b25, 0xb2e29692, 0x8aad2b2f, 0x8e6c3698, 0x832f1041, 0x87ee0df6,
  0x99a95df3, 0x9d684044, 0x902b669d, 0x94ea7b2a, 0xe0b41de7, 0xe4750050,
  0xe9362689, 0xedf73b3e, 0xf3b06b3b, 0xf771768c, 0xfa325055, 0xfef34de2,
  0xc6bcf05f, 0xc27dede8, 0xcf3ecb31, 0xcbffd686, 0xd5b88683, 0xd1799b34,
  0xdc3abded, 0xd8fba05a, 0x690ce0ee, 0x6dcdfd59, 0x608edb80, 0x644fc637,
  0x7a089632, 0x7ec98b85, 0x738aad5c, 0x774bb0eb, 0x4f040d56, 0x4bc510e1,
  0x46863638, 0x42472b8f, 0x5c007b8a, 0x58c1663d, 0x558240e4, 0x51435d53,
  0x251d3b9e, 0x21dc2629, 0x2c9f00f0, 0x285e1d47, 0x36194d42, 0x32d850f5,
  0x3f9b762c, 0x3b5a6b9b, 0x0315d626, 0x07d4cb91, 0x0a97ed48, 0x0e56f0ff,
  0x1011a0fa, 0x14d0bd4d, 0x19939b94, 0x1d528623, 0xf12f560e, 0xf5ee4bb9,
  0xf8ad6d60, 0xfc6c70d7, 0xe22b20d2, 0xe6ea3d65, 0xeba91bbc, 0xef68060b,
  0xd727bbb6, 0xd3e6a601, 0xdea580d8, 0xda649d6f, 0xc423cd6a, 0xc0e2d0dd,
  0xcda1f604, 0xc960ebb3, 0xbd3e8d7e, 0xb9ff90c9, 0xb4bcb610, 0xb07daba7,
  0xae3afba2, 0xaafbe615, 0xa7b8c0cc, 0xa379dd7b, 0x9b3660c6, 0x9ff77d71,
  0x92b45ba8, 0x9675461f, 0x8832161a, 0x8cf30bad, 0x81b02d74, 0x857130c3,
  0x5d8a9099, 0x594b8d2e, 0x5408abf7, 0x50c9b640, 0x4e8ee645, 0x4a4ffbf2,
  0x470cdd2b, 0x43cdc09c, 0x7b827d21, 0x7f436096, 0x7200464f, 0x76c15bf8,
  0x68860bfd, 0x6c47164a, 0x61043093, 0x65c52d24, 0x119b4be9, 0x155a565e,
  0x18197087, 0x1cd86d30, 0x029f3d35, 0x065e2082, 0x0b1d065b, 0x0fdc1bec,
  0x3793a651, 0x3352bbe6, 0x3e119d3f, 0x3ad08088, 0x2497d08d, 0x2056cd3a,
  0x2d15ebe3, 0x29d4f654, 0xc5a92679, 0xc1683bce, 0xcc2b1d17, 0xc8ea00a0,
  0xd6ad50a5, 0xd26c4d12, 0xdf2f6bcb, 0xdbee767c, 0xe3a1cbc1, 0xe760d676,
  0xea23f0af, 0xeee2ed18, 0xf0a5bd1d, 0xf464a0aa, 0xf9278673, 0xfde69bc4,
  0x89b8fd09, 0x8d79e0be, 0x803ac667, 0x84fbdbd0, 0x9abc8bd5, 0x9e7d9662,
  0x933eb0bb, 0x97ffad0c, 0xafb010b1, 0xab710d06, 0xa6322bdf, 0xa2f33668,
  0xbcb4666d, 0xb8757bda, 0xb5365d03, 0xb1f740b4
};

/* _calc_crc32 relicenced to LGPL from fluendo ts demuxer */
guint32
_calc_crc32 (const guint8 * data, guint datalen)
{
  gint i;
  guint32 crc = 0xffffffff;

  for (i = 0; i < datalen; i++) {
    crc = (crc << 8) ^ crc_tab[((crc >> 24) ^ *data++) & 0xff];
  }
  return crc;
}

gpointer
__common_desc_checks (GstMpegTsSection * section, guint min_size,
    GstMpegTsParseFunc parsefunc, GDestroyNotify destroynotify)
{
  gpointer res;

  /* Check section is big enough */
  if (section->section_length < min_size) {
    GST_WARNING
        ("PID:0x%04x table_id:0x%02x, section too small (Got %d, need at least %d)",
        section->pid, section->table_id, section->section_length, min_size);
    return NULL;
  }

  /* If section has a CRC, check it */
  if (!section->short_section
      && (_calc_crc32 (section->data, section->section_length) != 0)) {
    GST_WARNING ("PID:0x%04x table_id:0x%02x, Bad CRC on section", section->pid,
        section->table_id);
    return NULL;
  }

  /* Finally parse and set the destroy notify */
  res = parsefunc (section);
  if (res == NULL)
    GST_WARNING ("PID:0x%04x table_id:0x%02x, Failed to parse section",
        section->pid, section->table_id);
  else
    section->destroy_parsed = destroynotify;
  return res;
}


/*
 * GENERIC MPEG-TS SECTION
 */
static void
_gst_mpegts_section_free (GstMpegTsSection * section)
{
  GST_DEBUG ("Freeing section type %d", section->section_type);

  if (section->cached_parsed && section->destroy_parsed)
    section->destroy_parsed (section->cached_parsed);

  if (section->data)
    g_free (section->data);

  g_slice_free (GstMpegTsSection, section);
}

static GstMpegTsSection *
_gst_mpegts_section_copy (GstMpegTsSection * section)
{
  GstMpegTsSection *copy;

  copy = g_slice_new0 (GstMpegTsSection);
  gst_mini_object_init (GST_MINI_OBJECT_CAST (copy), 0, MPEG_TYPE_TS_SECTION,
      (GstMiniObjectCopyFunction) _gst_mpegts_section_copy, NULL,
      (GstMiniObjectFreeFunction) _gst_mpegts_section_free);

  copy->section_type = section->section_type;
  copy->pid = section->pid;
  copy->table_id = section->table_id;
  copy->subtable_extension = section->subtable_extension;
  copy->version_number = section->version_number;
  copy->current_next_indicator = section->current_next_indicator;
  copy->section_number = section->section_number;
  copy->last_section_number = section->last_section_number;
  copy->crc = section->crc;

  copy->data = g_memdup (section->data, section->section_length);
  copy->section_length = section->section_length;
  /* Note: We do not copy the cached parsed item, it will be
   * reconstructed on that copy */
  copy->cached_parsed = NULL;
  copy->offset = section->offset;
  copy->short_section = section->short_section;

  return copy;
}


/**
 * gst_message_parse_mpegts_section:
 * @message: a #GstMessage
 *
 * Returns the #GstMpegTsSection contained in a message.
 *
 * Returns: (transfer full): the contained #GstMpegTsSection, or %NULL.
 */
GstMpegTsSection *
gst_message_parse_mpegts_section (GstMessage * message)
{
  const GstStructure *st;
  GstMpegTsSection *section;

  if (message->type != GST_MESSAGE_ELEMENT)
    return NULL;

  st = gst_message_get_structure (message);
  /* FIXME : Add checks against know section names */
  if (!gst_structure_id_get (st, QUARK_SECTION, GST_TYPE_MPEGTS_SECTION,
          &section, NULL))
    return NULL;

  return section;
}

/**
 * gst_message_new_mpegts_section:
 * @parent: (transfer none): The creator of the message
 * @section: (transfer none): The #GstMpegTsSection to put in a message
 *
 * Creates a new #GstMessage for a @GstMpegTsSection.
 *
 * Returns: (transfer full): The new #GstMessage to be posted, or %NULL if the
 * section is not valid.
 */
GstMessage *
gst_message_new_mpegts_section (GstObject * parent, GstMpegTsSection * section)
{
  GstMessage *msg;
  GstStructure *st;
  GQuark quark;

  switch (section->section_type) {
    case GST_MPEGTS_SECTION_PAT:
      quark = QUARK_PAT;
      break;
    case GST_MPEGTS_SECTION_PMT:
      quark = QUARK_PMT;
      break;
    case GST_MPEGTS_SECTION_CAT:
      quark = QUARK_CAT;
      break;
    case GST_MPEGTS_SECTION_EIT:
      quark = QUARK_EIT;
      break;
    case GST_MPEGTS_SECTION_BAT:
      quark = QUARK_BAT;
      break;
    case GST_MPEGTS_SECTION_NIT:
      quark = QUARK_NIT;
      break;
    case GST_MPEGTS_SECTION_SDT:
      quark = QUARK_SDT;
      break;
    case GST_MPEGTS_SECTION_TDT:
      quark = QUARK_TDT;
      break;
    case GST_MPEGTS_SECTION_TOT:
      quark = QUARK_TOT;
      break;
    default:
      GST_DEBUG ("Creating message for unknown GstMpegTsSection");
      quark = QUARK_SECTION;
      break;
  }

  st = gst_structure_new_id (quark, QUARK_SECTION, MPEG_TYPE_TS_SECTION,
      section, NULL);

  msg = gst_message_new_element (parent, st);

  return msg;
}

static GstMpegTsPatProgram *
_mpegts_pat_program_copy (GstMpegTsPatProgram * orig)
{
  return g_slice_dup (GstMpegTsPatProgram, orig);
}

static void
_mpegts_pat_program_free (GstMpegTsPatProgram * orig)
{
  g_slice_free (GstMpegTsPatProgram, orig);
}

G_DEFINE_BOXED_TYPE (GstMpegTsPatProgram, gst_mpegts_pat_program,
    (GBoxedCopyFunc) _mpegts_pat_program_copy,
    (GFreeFunc) _mpegts_pat_program_free);

/* Program Association Table */
static gpointer
_parse_pat (GstMpegTsSection * section)
{
  GPtrArray *pat;
  guint16 i = 0, nb_programs;
  GstMpegTsPatProgram *program;
  guint8 *data, *end;

  /* Skip already parsed data */
  data = section->data + 8;

  /* stop at the CRC */
  end = section->data + section->section_length;

  /* Initialize program list */
  nb_programs = (end - 4 - data) / 4;
  pat =
      g_ptr_array_new_full (nb_programs,
      (GDestroyNotify) _mpegts_pat_program_free);

  while (data < end - 4) {
    program = g_slice_new0 (GstMpegTsPatProgram);
    program->program_number = GST_READ_UINT16_BE (data);
    data += 2;

    program->network_or_program_map_PID = GST_READ_UINT16_BE (data) & 0x1FFF;
    data += 2;

    g_ptr_array_index (pat, i) = program;

    i++;
  }
  pat->len = nb_programs;

  if (data != end - 4) {
    GST_ERROR ("at the end of PAT data != end - 4");
    g_ptr_array_unref (pat);

    return NULL;
  }

  return (gpointer) pat;
}

/**
 * gst_mpegts_section_get_pat:
 * @section: a #GstMpegTsSection of type %GST_MPEGTS_SECTION_PAT
 *
 * Parses a Program Association Table (ITU H.222.0, ISO/IEC 13818-1).
 *
 * Returns the array of #GstMpegTsPatProgram contained in the section.
 *
 * Note: The PAT "transport_id" field corresponds to the "subtable_extension"
 * field of the provided @section.
 *
 * Returns: (transfer container) (element-type GstMpegTsPatProgram): The
 * #GstMpegTsPatProgram contained in the section, or %NULL if an error
 * happened. Release with #g_ptr_array_unref when done.
 */
GPtrArray *
gst_mpegts_section_get_pat (GstMpegTsSection * section)
{
  g_return_val_if_fail (section->section_type == GST_MPEGTS_SECTION_PAT, NULL);
  g_return_val_if_fail (section->cached_parsed || section->data, NULL);

  if (!section->cached_parsed)
    section->cached_parsed =
        __common_desc_checks (section, 12, _parse_pat,
        (GDestroyNotify) g_ptr_array_unref);

  if (section->cached_parsed)
    return g_ptr_array_ref ((GPtrArray *) section->cached_parsed);
  return NULL;
}


/* Program Map Table */

static GstMpegTsPMTStream *
_gst_mpegts_pmt_stream_copy (GstMpegTsPMTStream * pmt)
{
  GstMpegTsPMTStream *copy;

  copy = g_slice_dup (GstMpegTsPMTStream, pmt);
  copy->descriptors = g_ptr_array_ref (pmt->descriptors);

  return copy;
}

static void
_gst_mpegts_pmt_stream_free (GstMpegTsPMTStream * pmt)
{
  g_ptr_array_unref (pmt->descriptors);
  g_slice_free (GstMpegTsPMTStream, pmt);
}

G_DEFINE_BOXED_TYPE (GstMpegTsPMTStream, gst_mpegts_pmt_stream,
    (GBoxedCopyFunc) _gst_mpegts_pmt_stream_copy,
    (GFreeFunc) _gst_mpegts_pmt_stream_free);

static GstMpegTsPMT *
_gst_mpegts_pmt_copy (GstMpegTsPMT * pmt)
{
  GstMpegTsPMT *copy;

  copy = g_slice_dup (GstMpegTsPMT, pmt);
  copy->descriptors = g_ptr_array_ref (pmt->descriptors);
  copy->streams = g_ptr_array_ref (pmt->streams);

  return copy;
}

static void
_gst_mpegts_pmt_free (GstMpegTsPMT * pmt)
{
  g_ptr_array_unref (pmt->descriptors);
  g_ptr_array_unref (pmt->streams);
  g_slice_free (GstMpegTsPMT, pmt);
}

G_DEFINE_BOXED_TYPE (GstMpegTsPMT, gst_mpegts_pmt,
    (GBoxedCopyFunc) _gst_mpegts_pmt_copy, (GFreeFunc) _gst_mpegts_pmt_free);


static gpointer
_parse_pmt (GstMpegTsSection * section)
{
  GstMpegTsPMT *pmt = NULL;
  guint i = 0, allocated_streams = 8;
  guint8 *data, *end;
  guint program_info_length;
  guint stream_info_length;

  pmt = g_slice_new0 (GstMpegTsPMT);

  data = section->data;
  end = data + section->section_length;

  GST_DEBUG ("Parsing %d Program Map Table", section->subtable_extension);

  /* Skip already parsed data */
  data += 8;

  pmt->pcr_pid = GST_READ_UINT16_BE (data) & 0x1FFF;
  data += 2;

  program_info_length = GST_READ_UINT16_BE (data) & 0x0FFF;
  data += 2;

  /* check that the buffer is large enough to contain at least
   * program_info_length bytes + CRC */
  if (program_info_length && (data + program_info_length + 4 > end)) {
    GST_WARNING ("PID %d invalid program info length %d left %d",
        section->pid, program_info_length, (gint) (end - data));
    goto error;
  }
  pmt->descriptors = gst_mpegts_parse_descriptors (data, program_info_length);
  if (pmt->descriptors == NULL)
    goto error;
  data += program_info_length;

  pmt->streams =
      g_ptr_array_new_full (allocated_streams,
      (GDestroyNotify) _gst_mpegts_pmt_stream_free);

  /* parse entries, cycle until there's space for another entry (at least 5
   * bytes) plus the CRC */
  while (data <= end - 4 - 5) {
    GstMpegTsPMTStream *stream = g_slice_new0 (GstMpegTsPMTStream);

    g_ptr_array_add (pmt->streams, stream);

    stream->stream_type = *data++;
    GST_DEBUG ("[%d] Stream type 0x%02x found", i, stream->stream_type);

    stream->pid = GST_READ_UINT16_BE (data) & 0x1FFF;
    data += 2;

    stream_info_length = GST_READ_UINT16_BE (data) & 0x0FFF;
    data += 2;

    if (data + stream_info_length + 4 > end) {
      GST_WARNING ("PID %d invalid stream info length %d left %d", section->pid,
          stream_info_length, (gint) (end - data));
      goto error;
    }

    stream->descriptors =
        gst_mpegts_parse_descriptors (data, stream_info_length);
    if (stream->descriptors == NULL)
      goto error;
    data += stream_info_length;

    i += 1;
  }

  g_assert (data == end - 4);

  return (gpointer) pmt;

error:
  if (pmt)
    _gst_mpegts_pmt_free (pmt);

  return NULL;
}

/**
 * gst_mpegts_section_get_pmt:
 * @section: a #GstMpegTsSection of type %GST_MPEGTS_SECTION_PMT
 *
 * Returns the #GstMpegTsPMT contained in the @section.
 *
 * Returns: The #GstMpegTsPMT contained in the section, or %NULL if an error
 * happened.
 */
const GstMpegTsPMT *
gst_mpegts_section_get_pmt (GstMpegTsSection * section)
{
  g_return_val_if_fail (section->section_type == GST_MPEGTS_SECTION_PMT, NULL);
  g_return_val_if_fail (section->cached_parsed || section->data, NULL);

  if (!section->cached_parsed)
    section->cached_parsed =
        __common_desc_checks (section, 16, _parse_pmt,
        (GDestroyNotify) _gst_mpegts_pmt_free);

  return (const GstMpegTsPMT *) section->cached_parsed;
}


/* Conditional Access Table */
static gpointer
_parse_cat (GstMpegTsSection * section)
{
  guint8 *data;
  guint desc_len;

  /* Skip parts already parsed */
  data = section->data + 8;

  /* descriptors */
  desc_len = section->section_length - 4 - 8;
  return (gpointer) gst_mpegts_parse_descriptors (data, desc_len);
}

/**
 * gst_mpegts_section_get_cat:
 * @section: a #GstMpegTsSection of type %GST_MPEGTS_SECTION_CAT
 *
 * Returns the array of #GstMpegTsDescriptor contained in the Condtional
 * Access Table.
 *
 * Returns: (transfer container) (element-type GstMpegTsDescriptor): The
 * #GstMpegTsDescriptor contained in the section, or %NULL if an error
 * happened. Release with #g_array_unref when done.
 */
GPtrArray *
gst_mpegts_section_get_cat (GstMpegTsSection * section)
{
  g_return_val_if_fail (section->section_type == GST_MPEGTS_SECTION_CAT, NULL);
  g_return_val_if_fail (section->cached_parsed || section->data, NULL);

  if (!section->cached_parsed)
    section->cached_parsed =
        __common_desc_checks (section, 12, _parse_cat,
        (GDestroyNotify) g_ptr_array_unref);

  if (section->cached_parsed)
    return g_ptr_array_ref ((GPtrArray *) section->cached_parsed);
  return NULL;
}

/* Transport Stream Description Table (TSDT) */
/**
 * gst_mpegts_section_get_tsdt:
 * @section: a #GstMpegTsSection of type %GST_MPEGTS_SECTION_TSDT
 *
 * Returns the array of #GstMpegTsDescriptor contained in the section
 *
 * Returns: (transfer container) (element-type GstMpegTsDescriptor): The
 * #GstMpegTsDescriptor contained in the section, or %NULL if an error
 * happened. Release with #g_array_unref when done.
 */
GPtrArray *
gst_mpegts_section_get_tsdt (GstMpegTsSection * section)
{
  g_return_val_if_fail (section->section_type == GST_MPEGTS_SECTION_TSDT, NULL);
  g_return_val_if_fail (section->cached_parsed || section->data, NULL);

  if (section->cached_parsed)
    return g_ptr_array_ref ((GPtrArray *) section->cached_parsed);

  /* FIXME : parse TSDT */
  return NULL;
}


/**
 * gst_mpegts_initialize:
 *
 * Initializes the MPEG-TS helper library. Must be called before any
 * usage.
 */
void
gst_mpegts_initialize (void)
{
  if (_gst_mpegts_section_type)
    return;

  GST_DEBUG_CATEGORY_INIT (gst_mpegts_debug, "mpegts", 0,
      "MPEG-TS helper library");

  /* FIXME : Temporary hack to initialize section gtype */
  _gst_mpegts_section_type = gst_mpegts_section_get_type ();

  QUARK_PAT = g_quark_from_string ("pat");
  QUARK_CAT = g_quark_from_string ("cat");
  QUARK_PMT = g_quark_from_string ("pmt");
  QUARK_NIT = g_quark_from_string ("nit");
  QUARK_BAT = g_quark_from_string ("bat");
  QUARK_SDT = g_quark_from_string ("sdt");
  QUARK_EIT = g_quark_from_string ("eit");
  QUARK_TDT = g_quark_from_string ("tdt");
  QUARK_TOT = g_quark_from_string ("tot");
  QUARK_SECTION = g_quark_from_string ("section");

  __initialize_descriptors ();
}

/* FIXME : Later on we might need to use more than just the table_id
 * to figure out which type of section this is. */
static GstMpegTsSectionType
_identify_section (guint16 pid, guint8 table_id)
{
  switch (table_id) {
    case GST_MTS_TABLE_ID_PROGRAM_ASSOCIATION:
      if (pid == 0x00)
        return GST_MPEGTS_SECTION_PAT;
      break;
    case GST_MTS_TABLE_ID_CONDITIONAL_ACCESS:
      if (pid == 0x01)
        return GST_MPEGTS_SECTION_CAT;
      break;
    case GST_MTS_TABLE_ID_TS_PROGRAM_MAP:
      return GST_MPEGTS_SECTION_PMT;
    case GST_MTS_TABLE_ID_BOUQUET_ASSOCIATION:
      if (pid == 0x0011)
        return GST_MPEGTS_SECTION_BAT;
      break;
    case GST_MTS_TABLE_ID_NETWORK_INFORMATION_ACTUAL_NETWORK:
    case GST_MTS_TABLE_ID_NETWORK_INFORMATION_OTHER_NETWORK:
      if (pid == 0x0010)
        return GST_MPEGTS_SECTION_NIT;
      break;
    case GST_MTS_TABLE_ID_SERVICE_DESCRIPTION_ACTUAL_TS:
    case GST_MTS_TABLE_ID_SERVICE_DESCRIPTION_OTHER_TS:
      if (pid == 0x0011)
        return GST_MPEGTS_SECTION_SDT;
      break;
    case GST_MTS_TABLE_ID_TIME_DATE:
      if (pid == 0x0014)
        return GST_MPEGTS_SECTION_TDT;
      break;
    case GST_MTS_TABLE_ID_TIME_OFFSET:
      if (pid == 0x0014)
        return GST_MPEGTS_SECTION_TOT;
      break;
      /* FIXME : FILL */
    default:
      /* Handle ranges */
      if (table_id >= GST_MTS_TABLE_ID_EVENT_INFORMATION_ACTUAL_TS_PRESENT &&
          table_id <= GST_MTS_TABLE_ID_EVENT_INFORMATION_OTHER_TS_SCHEDULE_N) {
        if (pid == 0x0012)
          return GST_MPEGTS_SECTION_EIT;
      }
      return GST_MPEGTS_SECTION_UNKNOWN;
  }
  return GST_MPEGTS_SECTION_UNKNOWN;

}

/**
 * gst_mpegts_section_new:
 * @pid: the PID to which this section belongs
 * @data: (transfer full): a pointer to the beginning of the section (i.e. the first byte
 * should contain the table_id field).
 * @data_size: size of the @data argument.
 *
 * Creates a new #GstMpegTsSection from the provided @data.
 *
 * Note: Ensuring @data is big enough to contain the full section is the
 * responsibility of the caller. If it is not big enough, %NULL will be
 * returned.
 *
 * Note: it is the responsibility of the caller to ensure @data does point
 * to the beginning of the section.
 *
 * Returns: (transfer full): A new #GstMpegTsSection if the data was valid,
 * else %NULL
 */
GstMpegTsSection *
gst_mpegts_section_new (guint16 pid, guint8 * data, gsize data_size)
{
  GstMpegTsSection *res = NULL;
  guint8 tmp;
  guint16 section_length;

  /* Check for length */
  section_length = GST_READ_UINT16_BE (data + 1) & 0x0FFF;
  if (G_UNLIKELY (data_size < section_length + 3))
    goto short_packet;

  res = g_slice_new0 (GstMpegTsSection);
  gst_mini_object_init (GST_MINI_OBJECT_CAST (res), 0, MPEG_TYPE_TS_SECTION,
      (GstMiniObjectCopyFunction) _gst_mpegts_section_copy, NULL,
      (GstMiniObjectFreeFunction) _gst_mpegts_section_free);

  res->pid = pid;
  res->data = data;
  /* table_id                        : 8  bit */
  res->table_id = *data++;
  /* section_syntax_indicator        : 1  bit
   * other_fields (reserved)         : 3  bit*/
  res->short_section = (*data & 0x80) == 0x00;
  /* section_length (already parsed) : 12 bit */
  res->section_length = section_length + 3;
  if (!res->short_section) {
    /* CRC is after section_length (-4 for the size of the CRC) */
    res->crc = GST_READ_UINT32_BE (res->data + res->section_length - 4);
    /* Skip to after section_length */
    data += 2;
    /* subtable extension            : 16 bit */
    res->subtable_extension = GST_READ_UINT16_BE (data);
    data += 2;
    /* reserved                      : 2  bit
     * version_number                : 5  bit
     * current_next_indicator        : 1  bit */
    tmp = *data++;
    res->version_number = tmp >> 1 & 0x1f;
    res->current_next_indicator = tmp & 0x01;
    /* section_number                : 8  bit */
    res->section_number = *data++;
    /* last_section_number                : 8  bit */
    res->last_section_number = *data;
  }

  res->section_type = _identify_section (res->pid, res->table_id);

  return res;

short_packet:
  {
    GST_WARNING
        ("PID 0x%04x section extends past provided data (got:%" G_GSIZE_FORMAT
        ", need:%d)", pid, data_size, section_length + 3);
    return NULL;
  }
}
