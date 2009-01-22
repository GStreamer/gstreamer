/* GStreamer
 * Copyright (C) 2008 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <string.h>

#include "mxfparse.h"

GST_DEBUG_CATEGORY_EXTERN (mxf_debug);
#define GST_CAT_DEFAULT mxf_debug

/* SMPTE 377M 3.3: A value of 0 for every field means unknown timestamp */
static const MXFTimestamp mxf_timestamp_unknown = { 0, 0, 0, 0, 0, 0, 0 };

static const MXFUMID umid_zero = { {0,} };
static const MXFUL key_zero = { {0,} };

/* UL common to all MXF UL */
static const guint8 mxf_key[] = { 0x06, 0x0e, 0x2b, 0x34 };

/* SMPTE 377M 6.1 */
static const guint8 partition_pack_key[] =
    { 0x06, 0x0e, 0x2b, 0x34, 0x02, 0x05, 0x01, 0x01, 0x0d, 0x01, 0x02, 0x01,
  0x01
};

/* SMPTE 336M */
static const guint8 fill_key[] =
    { 0x06, 0x0e, 0x2b, 0x34, 0x01, 0x01, 0x01, 0x01, 0x03, 0x01, 0x02, 0x10,
  0x01, 0x00, 0x00, 0x00
};

/* SMPTE 377M 8.1 */
static const guint8 primer_pack_key[] =
    { 0x06, 0x0e, 0x2b, 0x34, 0x02, 0x05, 0x01, 0x01, 0x0d, 0x01, 0x02, 0x01,
  0x01, 0x05, 0x01, 0x00
};

/* SMPTE 377M 8.6 */
static const guint8 metadata_key[] =
    { 0x06, 0x0e, 0x2b, 0x34, 0x02, 0x53, 0x01, 0x01, 0x0d, 0x01, 0x01, 0x01,
  0x01
};

static const guint8 random_index_pack_key[] =
    { 0x06, 0x0e, 0x2b, 0x34, 0x02, 0x05, 0x01, 0x01, 0x0d, 0x01, 0x02, 0x01,
  0x01, 0x11, 0x01, 0x00
};

static const guint8 index_table_segment_key[] =
    { 0x06, 0x0e, 0x2b, 0x34, 0x02, 0x53, 0x01, 0x01, 0x0d, 0x01, 0x02, 0x01,
  0x01, 0x10, 0x01, 0x00
};

gboolean
mxf_is_mxf_packet (const MXFUL * key)
{
  return (memcmp (key, mxf_key, 4) == 0);
}

/* SMPTE 377M 6.1: Check if this is a valid partition pack */
gboolean
mxf_is_partition_pack (const MXFUL * key)
{
  if (memcmp (key, partition_pack_key, 13) == 0 && key->u[13] >= 0x02
      && key->u[13] <= 0x04 && key->u[14] < 0x05 && key->u[15] == 0x00)
    return TRUE;

  return FALSE;
}

/* SMPTE 377M 6.2: header partition pack has byte 14 == 0x02 */
gboolean
mxf_is_header_partition_pack (const MXFUL * key)
{
  if (memcmp (key, partition_pack_key, 13) == 0 && key->u[13] == 0x02 &&
      key->u[14] < 0x05 && key->u[15] == 0x00)
    return TRUE;

  return FALSE;
}

/* SMPTE 377M 6.3: body partition pack has byte 14 == 0x03 */
gboolean
mxf_is_body_partition_pack (const MXFUL * key)
{
  if (memcmp (key, partition_pack_key, 13) == 0 && key->u[13] == 0x03 &&
      key->u[14] < 0x05 && key->u[15] == 0x00)
    return TRUE;

  return FALSE;
}

/* SMPTE 377M 6.4: footer partition pack has byte 14 == 0x04 */
gboolean
mxf_is_footer_partition_pack (const MXFUL * key)
{
  if (memcmp (key, partition_pack_key, 13) == 0 && key->u[13] == 0x04 &&
      key->u[14] < 0x05 && key->u[15] == 0x00)
    return TRUE;

  return FALSE;
}

gboolean
mxf_is_fill (const MXFUL * key)
{
  return (memcmp (key, fill_key, 16) == 0);
}

gboolean
mxf_is_primer_pack (const MXFUL * key)
{
  return (memcmp (key, primer_pack_key, 16) == 0);
}

gboolean
mxf_is_metadata (const MXFUL * key)
{
  return (memcmp (key, metadata_key, 13) == 0 && key->u[15] == 0x00);
}

/* SMPTE 377M 8.7.3 */
gboolean
mxf_is_descriptive_metadata (const MXFUL * key)
{
  return (memcmp (key, mxf_key, 4) == 0 &&
      key->u[4] == 0x02 &&
      key->u[6] == 0x01 &&
      key->u[7] == 0x01 &&
      key->u[8] == 0x0d &&
      key->u[9] == 0x01 && key->u[10] == 0x04 && key->u[11] == 0x01);
}

gboolean
mxf_is_random_index_pack (const MXFUL * key)
{
  return (memcmp (key, random_index_pack_key, 16) == 0);
}

gboolean
mxf_is_index_table_segment (const MXFUL * key)
{
  return (memcmp (key, index_table_segment_key, 16) == 0);
}

/* SMPTE 379M 6.2.1 */
gboolean
mxf_is_generic_container_system_item (const MXFUL * key)
{
  return (memcmp (key, mxf_key, 4) == 0 && key->u[4] == 0x02
      && key->u[6] == 0x01 && key->u[8] == 0x0d && key->u[9] == 0x01
      && key->u[10] == 0x03 && key->u[11] == 0x01 && (key->u[12] == 0x04
          || key->u[12] == 0x14));
}

/* SMPTE 379M 7.1 */
gboolean
mxf_is_generic_container_essence_element (const MXFUL * key)
{
  return (memcmp (key, mxf_key, 4) == 0 && key->u[4] == 0x01
      && key->u[5] == 0x02 && key->u[6] == 0x01 && key->u[8] == 0x0d
      && key->u[9] == 0x01 && key->u[10] == 0x03 && key->u[11] == 0x01
      && (key->u[12] == 0x05 || key->u[12] == 0x06 || key->u[12] == 0x07
          || key->u[12] == 0x15 || key->u[12] == 0x16 || key->u[12] == 0x17
          || key->u[12] == 0x18));
}

/* SMPTE 379M 8 */
gboolean
mxf_is_generic_container_essence_container_label (const MXFUL * key)
{
  return (key->u[0] == 0x06 &&
      key->u[1] == 0x0e &&
      key->u[2] == 0x2b &&
      key->u[3] == 0x34 &&
      key->u[4] == 0x04 &&
      key->u[5] == 0x01 &&
      key->u[6] == 0x01 &&
      key->u[8] == 0x0d &&
      key->u[9] == 0x01 &&
      key->u[10] == 0x03 &&
      key->u[11] == 0x01 && (key->u[12] == 0x01 || key->u[12] == 0x02));
}

gboolean
mxf_ul_is_equal (const MXFUL * a, const MXFUL * b)
{
  return (memcmp (a, b, 16) == 0);
}

gboolean
mxf_ul_is_zero (const MXFUL * key)
{
  return (memcmp (key, &key_zero, 16) == 0);
}

gchar *
mxf_ul_to_string (const MXFUL * key, gchar str[48])
{
  g_return_val_if_fail (key != NULL, NULL);
  g_return_val_if_fail (str != NULL, NULL);

  g_snprintf (str, 48,
      "%02x.%02x.%02x.%02x.%02x.%02x.%02x.%02x.%02x.%02x.%02x.%02x.%02x.%02x.%02x.%02x",
      key->u[0], key->u[1], key->u[2], key->u[3], key->u[4], key->u[5],
      key->u[6], key->u[7], key->u[8], key->u[9], key->u[10], key->u[11],
      key->u[12], key->u[13], key->u[14], key->u[15]);

  return str;
}

gboolean
mxf_umid_is_equal (const MXFUMID * a, const MXFUMID * b)
{
  return (memcmp (a, b, 32) == 0);
}

gboolean
mxf_umid_is_zero (const MXFUMID * umid)
{
  return (memcmp (umid, &umid_zero, 32) == 0);
}

gchar *
mxf_umid_to_string (const MXFUMID * key, gchar str[96])
{
  g_return_val_if_fail (key != NULL, NULL);
  g_return_val_if_fail (str != NULL, NULL);

  g_snprintf (str, 96,
      "%02x.%02x.%02x.%02x.%02x.%02x.%02x.%02x.%02x.%02x.%02x.%02x.%02x.%02x.%02x.%02x."
      "%02x.%02x.%02x.%02x.%02x.%02x.%02x.%02x.%02x.%02x.%02x.%02x.%02x.%02x.%02x.%02x",
      key->u[0], key->u[1], key->u[2], key->u[3], key->u[4], key->u[5],
      key->u[6], key->u[7], key->u[8], key->u[9], key->u[10], key->u[11],
      key->u[12], key->u[13], key->u[14], key->u[15],
      key->u[16],
      key->u[17],
      key->u[18],
      key->u[19],
      key->u[20],
      key->u[21],
      key->u[22],
      key->u[23],
      key->u[24],
      key->u[25],
      key->u[26], key->u[27], key->u[28], key->u[29], key->u[30], key->u[31]
      );

  return str;
}

MXFUMID *
mxf_umid_from_string (const gchar * str, MXFUMID * umid)
{
  gint len;
  guint i, j;

  g_return_val_if_fail (str != NULL, NULL);
  len = strlen (str);

  memset (umid, 0, 32);

  if (len != 95) {
    GST_ERROR ("Invalid UMID string length %d", len);
    return NULL;
  }

  for (i = 0, j = 0; i < 32; i++) {
    if (!g_ascii_isxdigit (str[j]) ||
        !g_ascii_isxdigit (str[j + 1]) ||
        (str[j + 2] != '.' && str[j + 2] != '\0')) {
      GST_ERROR ("Invalid UMID string '%s'", str);
      return NULL;
    }

    umid->u[i] =
        (g_ascii_xdigit_value (str[j]) << 4) | (g_ascii_xdigit_value (str[j +
                1]));
    j += 3;
  }
  return umid;
}

static guint
gst_mxf_ul_hash (const MXFUL * key)
{
  guint32 ret = 0;
  guint i;

  for (i = 0; i < 4; i++)
    ret ^=
        (key->u[i * 4 + 0] << 24) | (key->u[i * 4 + 1] << 16) | (key->u[i * 4 +
            2] << 8) | (key->u[i * 4 + 3] << 0);

  return ret;
}

static gboolean
gst_mxf_ul_equal (const MXFUL * a, const MXFUL * b)
{
  return (memcmp (a, b, 16) == 0);
}

gboolean
mxf_timestamp_parse (MXFTimestamp * timestamp, const guint8 * data, guint size)
{
  g_return_val_if_fail (data != NULL, FALSE);
  g_return_val_if_fail (timestamp != NULL, FALSE);

  memset (timestamp, 0, sizeof (MXFTimestamp));

  if (size < 8)
    return FALSE;

  timestamp->year = GST_READ_UINT16_BE (data);
  timestamp->month = GST_READ_UINT8 (data + 2);
  timestamp->day = GST_READ_UINT8 (data + 3);
  timestamp->hour = GST_READ_UINT8 (data + 4);
  timestamp->minute = GST_READ_UINT8 (data + 5);
  timestamp->second = GST_READ_UINT8 (data + 6);
  timestamp->quarter_msecond = GST_READ_UINT8 (data + 7);

  return TRUE;
}

gboolean
mxf_timestamp_is_unknown (const MXFTimestamp * a)
{
  return (memcmp (a, &mxf_timestamp_unknown, sizeof (MXFTimestamp)) == 0);
}

gint
mxf_timestamp_compare (const MXFTimestamp * a, const MXFTimestamp * b)
{
  gint diff;

  if ((diff = a->year - b->year) != 0)
    return diff;
  else if ((diff = a->month - b->month) != 0)
    return diff;
  else if ((diff = a->day - b->day) != 0)
    return diff;
  else if ((diff = a->hour - b->hour) != 0)
    return diff;
  else if ((diff = a->minute - b->minute) != 0)
    return diff;
  else if ((diff = a->second - b->second) != 0)
    return diff;
  else if ((diff = a->quarter_msecond - b->quarter_msecond) != 0)
    return diff;
  else
    return 0;
}

gboolean
mxf_fraction_parse (MXFFraction * fraction, const guint8 * data, guint size)
{
  g_return_val_if_fail (fraction != NULL, FALSE);
  g_return_val_if_fail (data != NULL, FALSE);

  memset (fraction, 0, sizeof (MXFFraction));

  if (size < 8)
    return FALSE;

  fraction->n = GST_READ_UINT32_BE (data);
  fraction->d = GST_READ_UINT32_BE (data + 4);

  return TRUE;
}

gchar *
mxf_utf16_to_utf8 (const guint8 * data, guint size)
{
  gchar *ret;
  GError *error = NULL;

  ret =
      g_convert ((const gchar *) data, size, "UTF-8", "UTF-16BE", NULL, NULL,
      &error);

  if (ret == NULL) {
    GST_WARNING ("UTF-16-BE to UTF-8 conversion failed: %s", error->message);
    g_error_free (error);
    return NULL;
  }

  return ret;
}

gboolean
mxf_product_version_parse (MXFProductVersion * product_version,
    const guint8 * data, guint size)
{
  g_return_val_if_fail (product_version != NULL, FALSE);
  g_return_val_if_fail (data != NULL, FALSE);

  memset (product_version, 0, sizeof (MXFProductVersion));

  if (size < 9)
    return FALSE;

  product_version->major = GST_READ_UINT16_BE (data);
  product_version->minor = GST_READ_UINT16_BE (data + 2);
  product_version->patch = GST_READ_UINT16_BE (data + 4);
  product_version->build = GST_READ_UINT16_BE (data + 6);

  /* Avid writes a 9 byte product version */
  if (size == 9)
    product_version->release = GST_READ_UINT8 (data + 8);
  else
    product_version->release = GST_READ_UINT16_BE (data + 8);

  return TRUE;
}

gboolean
mxf_ul_array_parse (MXFUL ** array, guint32 * count, const guint8 * data,
    guint size)
{
  guint32 element_count, element_size;
  guint i;

  g_return_val_if_fail (array != NULL, FALSE);
  g_return_val_if_fail (count != NULL, FALSE);
  g_return_val_if_fail (data != NULL, FALSE);

  if (size < 8)
    return FALSE;

  element_count = GST_READ_UINT32_BE (data);
  data += 4;
  size -= 4;

  if (element_count == 0) {
    *array = NULL;
    *count = 0;
    return TRUE;
  }

  element_size = GST_READ_UINT32_BE (data);
  data += 4;
  size -= 4;

  if (element_size != 16) {
    *array = NULL;
    *count = 0;
    return FALSE;
  }

  if (16 * element_count < size) {
    *array = NULL;
    *count = 0;
    return FALSE;
  }

  *array = g_new (MXFUL, element_count);
  *count = element_count;

  for (i = 0; i < element_count; i++) {
    memcpy (&((*array)[i]), data, 16);
    data += 16;
  }

  return TRUE;
}

/* SMPTE 377M 6.1, Table 2 */
gboolean
mxf_partition_pack_parse (const MXFUL * key, MXFPartitionPack * pack,
    const guint8 * data, guint size)
{
#ifndef GST_DISABLE_GST_DEBUG
  guint i;
  gchar str[48];
#endif

  g_return_val_if_fail (data != NULL, FALSE);
  g_return_val_if_fail (size >= 84, FALSE);

  memset (pack, 0, sizeof (MXFPartitionPack));

  GST_DEBUG ("Parsing partition pack:");

  if (key->u[13] == 0x02)
    pack->type = MXF_PARTITION_PACK_HEADER;
  else if (key->u[13] == 0x03)
    pack->type = MXF_PARTITION_PACK_BODY;
  else if (key->u[13] == 0x04)
    pack->type = MXF_PARTITION_PACK_FOOTER;

  GST_DEBUG ("  type = %s",
      (pack->type == MXF_PARTITION_PACK_HEADER) ? "header" : (pack->type ==
          MXF_PARTITION_PACK_BODY) ? "body" : "footer");

  pack->closed = (key->u[14] == 0x02 || key->u[14] == 0x04);
  pack->complete = (key->u[14] == 0x03 || key->u[14] == 0x04);

  GST_DEBUG ("  closed = %s, complete = %s", (pack->closed) ? "yes" : "no",
      (pack->complete) ? "yes" : "no");

  pack->major_version = GST_READ_UINT16_BE (data);
  if (pack->major_version != 1)
    goto error;
  data += 2;
  size -= 2;

  pack->minor_version = GST_READ_UINT16_BE (data);
  data += 2;
  size -= 2;

  GST_DEBUG ("  MXF version = %u.%u", pack->major_version, pack->minor_version);

  pack->kag_size = GST_READ_UINT32_BE (data);
  data += 4;
  size -= 4;

  GST_DEBUG ("  KAG size = %u", pack->kag_size);

  pack->this_partition = GST_READ_UINT64_BE (data);
  data += 8;
  size -= 8;

  GST_DEBUG ("  this partition offset = %" G_GUINT64_FORMAT,
      pack->this_partition);

  pack->prev_partition = GST_READ_UINT64_BE (data);
  data += 8;
  size -= 8;

  GST_DEBUG ("  previous partition offset = %" G_GUINT64_FORMAT,
      pack->prev_partition);

  pack->footer_partition = GST_READ_UINT64_BE (data);
  data += 8;
  size -= 8;

  GST_DEBUG ("  footer partition offset = %" G_GUINT64_FORMAT,
      pack->footer_partition);

  pack->header_byte_count = GST_READ_UINT64_BE (data);
  data += 8;
  size -= 8;

  GST_DEBUG ("  header byte count = %" G_GUINT64_FORMAT,
      pack->header_byte_count);

  pack->index_byte_count = GST_READ_UINT64_BE (data);
  data += 8;
  size -= 8;

  pack->index_sid = GST_READ_UINT32_BE (data);
  data += 4;
  size -= 4;

  GST_DEBUG ("  index sid = %u, size = %" G_GUINT64_FORMAT, pack->index_sid,
      pack->index_byte_count);

  pack->body_offset = GST_READ_UINT64_BE (data);
  data += 8;
  size -= 8;

  pack->body_sid = GST_READ_UINT32_BE (data);
  data += 4;
  size -= 4;

  GST_DEBUG ("  body sid = %u, offset = %" G_GUINT64_FORMAT, pack->body_sid,
      pack->body_offset);

  memcpy (&pack->operational_pattern, data, 16);
  data += 16;
  size -= 16;

  GST_DEBUG ("  operational pattern = %s",
      mxf_ul_to_string (&pack->operational_pattern, str));

  if (!mxf_ul_array_parse (&pack->essence_containers,
          &pack->n_essence_containers, data, size))
    goto error;

#ifndef GST_DISABLE_GST_DEBUG
  GST_DEBUG ("  number of essence containers = %u", pack->n_essence_containers);
  if (pack->n_essence_containers) {
    for (i = 0; i < pack->n_essence_containers; i++) {
      GST_DEBUG ("  essence container %u = %s", i,
          mxf_ul_to_string (&pack->essence_containers[i], str));
    }
  }
#endif

  pack->valid = TRUE;

  return TRUE;

error:
  GST_ERROR ("Invalid partition pack");

  mxf_partition_pack_reset (pack);
  return FALSE;
}

void
mxf_partition_pack_reset (MXFPartitionPack * pack)
{
  g_return_if_fail (pack != NULL);

  g_free (pack->essence_containers);

  memset (pack, 0, sizeof (MXFPartitionPack));
}

/* SMPTE 377M 11.1 */
gboolean
mxf_random_index_pack_parse (const MXFUL * key, const guint8 * data, guint size,
    GArray ** array)
{
  guint len, i;
  MXFRandomIndexPackEntry entry;

  g_return_val_if_fail (data != NULL, FALSE);
  g_return_val_if_fail (array != NULL, FALSE);

  if (size < 4)
    return FALSE;

  if ((size - 4) % 12 != 0)
    return FALSE;

  GST_DEBUG ("Parsing random index pack:");

  len = (size - 4) / 12;

  GST_DEBUG ("  number of entries = %u", len);

  *array =
      g_array_sized_new (FALSE, FALSE, sizeof (MXFRandomIndexPackEntry), len);

  for (i = 0; i < len; i++) {
    entry.body_sid = GST_READ_UINT32_BE (data);
    entry.offset = GST_READ_UINT64_BE (data + 4);
    data += 12;

    GST_DEBUG ("  entry %u = body sid %u at offset %" G_GUINT64_FORMAT, i,
        entry.body_sid, entry.offset);

    g_array_append_val (*array, entry);
  }

  return TRUE;
}

/* SMPTE 377M 10.2.3 */
gboolean
mxf_index_table_segment_parse (const MXFUL * key,
    MXFIndexTableSegment * segment, const MXFPrimerPack * primer,
    const guint8 * data, guint size)
{
#ifndef GST_DISABLE_GST_DEBUG
  gchar str[48];
#endif
  guint16 tag, tag_size;
  const guint8 *tag_data;

  g_return_val_if_fail (key != NULL, FALSE);
  g_return_val_if_fail (data != NULL, FALSE);
  g_return_val_if_fail (primer != NULL, FALSE);

  memset (segment, 0, sizeof (MXFIndexTableSegment));

  if (size < 70)
    return FALSE;

  GST_DEBUG ("Parsing index table segment:");

  while (mxf_local_tag_parse (data, size, &tag, &tag_size, &tag_data)) {
    if (tag_size == 0 || tag == 0x0000)
      goto next;

    switch (tag) {
      case 0x3c0a:
        if (tag_size != 16)
          goto error;
        memcpy (&segment->instance_id, tag_data, 16);
        GST_DEBUG ("  instance id = %s",
            mxf_ul_to_string (&segment->instance_id, str));
        break;
      case 0x3f0b:
        if (!mxf_fraction_parse (&segment->index_edit_rate, tag_data, tag_size))
          goto error;
        GST_DEBUG ("  index edit rate = %d/%d", segment->index_edit_rate.n,
            segment->index_edit_rate.d);
        break;
      case 0x3f0c:
        if (tag_size != 8)
          goto error;
        segment->index_start_position = GST_READ_UINT64_BE (tag_data);
        GST_DEBUG ("  index start position = %" G_GINT64_FORMAT,
            segment->index_start_position);
        break;
      case 0x3f0d:
        if (tag_size != 8)
          goto error;
        segment->index_duration = GST_READ_UINT64_BE (tag_data);
        GST_DEBUG ("  index duration = %" G_GINT64_FORMAT,
            segment->index_duration);
        break;
      case 0x3f05:
        if (tag_size != 4)
          goto error;
        segment->edit_unit_byte_count = GST_READ_UINT32_BE (tag_data);
        GST_DEBUG ("  edit unit byte count = %u",
            segment->edit_unit_byte_count);
        break;
      case 0x3f06:
        if (tag_size != 4)
          goto error;
        segment->index_sid = GST_READ_UINT32_BE (tag_data);
        GST_DEBUG ("  index sid = %u", segment->index_sid);
        break;
      case 0x3f07:
        if (tag_size != 4)
          goto error;
        segment->body_sid = GST_READ_UINT32_BE (tag_data);
        GST_DEBUG ("  body sid = %u", segment->body_sid);
        break;
      case 0x3f08:
        if (tag_size != 1)
          goto error;
        segment->slice_count = GST_READ_UINT8 (tag_data);
        GST_DEBUG ("  slice count = %u", segment->slice_count);
        break;
      case 0x3f0e:
        if (tag_size != 1)
          goto error;
        segment->pos_table_count = GST_READ_UINT8 (tag_data);
        GST_DEBUG ("  pos table count = %u", segment->pos_table_count);
        break;
      case 0x3f09:{
        guint len, i;

        if (tag_size < 8)
          goto error;

        len = GST_READ_UINT32_BE (tag_data);
        segment->n_delta_entries = len;
        GST_DEBUG ("  number of delta entries = %u", segment->n_delta_entries);
        if (len == 0)
          goto next;
        tag_data += 4;
        tag_size -= 4;

        if (GST_READ_UINT32_BE (tag_data) != 6)
          goto error;

        tag_data += 4;
        tag_size -= 4;

        if (tag_size < len * 6)
          goto error;

        segment->delta_entries = g_new (MXFDeltaEntry, len);

        for (i = 0; i < len; i++) {
          GST_DEBUG ("    delta entry %u:", i);

          segment->delta_entries[i].pos_table_index = GST_READ_UINT8 (tag_data);
          tag_data += 1;
          tag_size -= 1;
          GST_DEBUG ("    pos table index = %d",
              segment->delta_entries[i].pos_table_index);

          segment->delta_entries[i].slice = GST_READ_UINT8 (tag_data);
          tag_data += 1;
          tag_size -= 1;
          GST_DEBUG ("    slice = %u", segment->delta_entries[i].slice);

          segment->delta_entries[i].element_delta =
              GST_READ_UINT32_BE (tag_data);
          tag_data += 4;
          tag_size -= 4;
          GST_DEBUG ("    element delta = %u",
              segment->delta_entries[i].element_delta);
        }
        break;
      }
      case 0x3f0a:{
        guint len, i, j;

        if (tag_size < 8)
          goto error;

        len = GST_READ_UINT32_BE (tag_data);
        segment->n_index_entries = len;
        GST_DEBUG ("  number of index entries = %u", segment->n_index_entries);
        if (len == 0)
          goto next;
        tag_data += 4;
        tag_size -= 4;

        if (GST_READ_UINT32_BE (tag_data) !=
            (11 + 4 * segment->slice_count + 8 * segment->pos_table_count))
          goto error;

        tag_data += 4;
        tag_size -= 4;

        if (tag_size <
            len * (11 + 4 * segment->slice_count +
                8 * segment->pos_table_count))
          goto error;

        segment->index_entries = g_new (MXFIndexEntry, len);

        for (i = 0; i < len; i++) {
          MXFIndexEntry *entry = &segment->index_entries[i];

          GST_DEBUG ("    index entry %u:", i);

          entry->temporal_offset = GST_READ_UINT8 (tag_data);
          tag_data += 1;
          tag_size -= 1;
          GST_DEBUG ("    temporal offset = %d", entry->temporal_offset);

          entry->key_frame_offset = GST_READ_UINT8 (tag_data);
          tag_data += 1;
          tag_size -= 1;
          GST_DEBUG ("    keyframe offset = %d", entry->key_frame_offset);

          entry->flags = GST_READ_UINT8 (tag_data);
          tag_data += 1;
          tag_size -= 1;
          GST_DEBUG ("    flags = 0x%02x", entry->flags);

          entry->stream_offset = GST_READ_UINT64_BE (tag_data);
          tag_data += 8;
          tag_size -= 8;
          GST_DEBUG ("    stream offset = %" G_GUINT64_FORMAT,
              entry->stream_offset);

          for (j = 0; j < segment->slice_count; j++) {
            entry->slice_offset[j] = GST_READ_UINT32_BE (tag_data);
            tag_data += 4;
            tag_size -= 4;
            GST_DEBUG ("    slice %u offset = %u", j, entry->slice_offset[j]);
          }

          for (j = 0; j < segment->pos_table_count; j++) {
            mxf_fraction_parse (&entry->pos_table[j], tag_data, tag_size);
            tag_data += 8;
            tag_size -= 8;
            GST_DEBUG ("    pos table %u = %d/%d", j, entry->pos_table[j].n,
                entry->pos_table[j].d);
          }
        }
        break;
      }
      default:
        if (!mxf_local_tag_add_to_hash_table (primer, tag, tag_data, tag_size,
                &segment->other_tags))
          goto error;
        break;
    }

  next:
    data += 4 + tag_size;
    size -= 4 + tag_size;
  }
  return TRUE;

error:
  GST_ERROR ("Invalid index table segment");
  return FALSE;
}

void
mxf_index_table_segment_reset (MXFIndexTableSegment * segment)
{
  guint i;
  g_return_if_fail (segment != NULL);

  for (i = 0; i < segment->n_index_entries; i++) {
    g_free (segment->index_entries[i].slice_offset);
    g_free (segment->index_entries[i].pos_table);
  }
  g_free (segment->index_entries);
  g_free (segment->delta_entries);

  if (segment->other_tags)
    g_hash_table_destroy (segment->other_tags);

  memset (segment, 0, sizeof (MXFIndexTableSegment));
}

/* SMPTE 377M 8.2 Table 1 and 2 */

static void
_mxf_mapping_ul_free (MXFUL * ul)
{
  g_slice_free (MXFUL, ul);
}

gboolean
mxf_primer_pack_parse (const MXFUL * key, MXFPrimerPack * pack,
    const guint8 * data, guint size)
{
  guint i;
  guint32 n;

  g_return_val_if_fail (data != NULL, FALSE);
  g_return_val_if_fail (size >= 8, FALSE);

  memset (pack, 0, sizeof (MXFPrimerPack));

  GST_DEBUG ("Parsing primer pack:");

  pack->mappings =
      g_hash_table_new_full (g_direct_hash, g_direct_equal,
      (GDestroyNotify) NULL, (GDestroyNotify) _mxf_mapping_ul_free);

  n = GST_READ_UINT32_BE (data);
  data += 4;

  GST_DEBUG ("  number of mappings = %u", n);

  if (GST_READ_UINT32_BE (data) != 18)
    goto error;
  data += 4;

  if (size < 8 + n * 18)
    goto error;

  for (i = 0; i < n; i++) {
    guint local_tag;
#ifndef GST_DISABLE_GST_DEBUG
    gchar str[48];
#endif
    MXFUL *uid;

    local_tag = GST_READ_UINT16_BE (data);
    data += 2;

    if (g_hash_table_lookup (pack->mappings, GUINT_TO_POINTER (local_tag)))
      continue;

    uid = g_slice_new (MXFUL);
    memcpy (uid, data, 16);
    data += 16;

    g_hash_table_insert (pack->mappings, GUINT_TO_POINTER (local_tag), uid);
    GST_DEBUG ("  Adding mapping = 0x%04x -> %s", local_tag,
        mxf_ul_to_string (uid, str));
  }

  pack->valid = TRUE;

  return TRUE;

error:
  GST_DEBUG ("Invalid primer pack");
  mxf_primer_pack_reset (pack);
  return FALSE;
}

void
mxf_primer_pack_reset (MXFPrimerPack * pack)
{
  g_return_if_fail (pack != NULL);

  if (pack->mappings)
    g_hash_table_destroy (pack->mappings);
  memset (pack, 0, sizeof (MXFPrimerPack));
}

/* structural metadata parsing */

gboolean
mxf_local_tag_parse (const guint8 * data, guint size, guint16 * tag,
    guint16 * tag_size, const guint8 ** tag_data)
{
  g_return_val_if_fail (data != NULL, FALSE);

  if (size < 4)
    return FALSE;

  *tag = GST_READ_UINT16_BE (data);
  *tag_size = GST_READ_UINT16_BE (data + 2);

  if (size < 4 + *tag_size)
    return FALSE;

  *tag_data = data + 4;

  return TRUE;
}

void
gst_mxf_local_tag_free (MXFLocalTag * tag)
{
  g_free (tag->data);
  g_slice_free (MXFLocalTag, tag);
}

gboolean
mxf_local_tag_add_to_hash_table (const MXFPrimerPack * primer,
    guint16 tag, const guint8 * tag_data, guint16 tag_size,
    GHashTable ** hash_table)
{
  MXFLocalTag *local_tag;
  MXFUL *key;

  g_return_val_if_fail (primer != NULL, FALSE);
  g_return_val_if_fail (tag_data != NULL, FALSE);
  g_return_val_if_fail (hash_table != NULL, FALSE);
  g_return_val_if_fail (primer->mappings != NULL, FALSE);

  if (*hash_table == NULL)
    *hash_table =
        g_hash_table_new_full ((GHashFunc) gst_mxf_ul_hash,
        (GEqualFunc) gst_mxf_ul_equal, (GDestroyNotify) NULL,
        (GDestroyNotify) gst_mxf_local_tag_free);

  g_return_val_if_fail (*hash_table != NULL, FALSE);

  key = (MXFUL *) g_hash_table_lookup (primer->mappings,
      GUINT_TO_POINTER (((guint) tag)));

  if (key) {
#ifndef GST_DISABLE_GST_DEBUG
    gchar str[48];
#endif

    GST_DEBUG ("Adding local tag 0x%04x with UL %s and size %u", tag,
        mxf_ul_to_string (key, str), tag_size);

    local_tag = g_slice_new (MXFLocalTag);
    memcpy (&local_tag->key, key, sizeof (MXFUL));
    local_tag->size = tag_size;
    local_tag->data = g_memdup (tag_data, tag_size);

    g_hash_table_insert (*hash_table, &local_tag->key, local_tag);
  } else {
    GST_WARNING ("Local tag with no entry in primer pack: 0x%04x", tag);
  }

  return TRUE;
}

static GSList *_mxf_essence_element_handler_registry = NULL;

void
mxf_essence_element_handler_register (const MXFEssenceElementHandler * handler)
{
  _mxf_essence_element_handler_registry =
      g_slist_prepend (_mxf_essence_element_handler_registry,
      (gpointer) handler);
}

const MXFEssenceElementHandler *
mxf_essence_element_handler_find (const MXFMetadataTimelineTrack * track)
{
  GSList *l;
  const MXFEssenceElementHandler *ret = NULL;

  for (l = _mxf_essence_element_handler_registry; l; l = l->next) {
    MXFEssenceElementHandler *current = l->data;

    if (current->handles_track (track)) {
      ret = current;
    }
  }

  return ret;
}
