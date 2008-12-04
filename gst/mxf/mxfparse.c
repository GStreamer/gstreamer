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

/* SMPTE 377M 6.1, Table 2 */
gboolean
mxf_partition_pack_parse (const MXFUL * key, MXFPartitionPack * pack,
    const guint8 * data, guint size)
{
  guint i;
  gchar str[48];

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

  pack->n_essence_containers = GST_READ_UINT32_BE (data);
  data += 4;
  size -= 4;

  GST_DEBUG ("  number of essence containers = %u", pack->n_essence_containers);

  if (GST_READ_UINT32_BE (data) != 16)
    goto error;
  data += 4;
  size -= 4;

  if (size < 16 * pack->n_essence_containers)
    goto error;

  if (pack->n_essence_containers) {
    pack->essence_containers = g_new (MXFUL, pack->n_essence_containers);
    for (i = 0; i < pack->n_essence_containers; i++) {
      memcpy (&pack->essence_containers[i], data + i * 16, 16);
      GST_DEBUG ("  essence container %u = %s", i,
          mxf_ul_to_string (&pack->essence_containers[i], str));
    }
  }

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
  gchar str[48];
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
        if (!gst_metadata_add_custom_tag (primer, tag, tag_data, tag_size,
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
    gchar str[48];
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
gst_metadata_add_custom_tag (const MXFPrimerPack * primer,
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
    gchar str[48];

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

/* All following defined in SMPTE 377M Annex A, B, C, D */
gboolean
mxf_metadata_preface_parse (const MXFUL * key,
    MXFMetadataPreface * preface, const MXFPrimerPack * primer,
    const guint8 * data, guint size)
{
  guint16 tag, tag_size;
  const guint8 *tag_data;
  gchar str[48];

  g_return_val_if_fail (data != NULL, FALSE);

  memset (preface, 0, sizeof (MXFMetadataPreface));

  GST_DEBUG ("Parsing preface:");

  while (mxf_local_tag_parse (data, size, &tag, &tag_size, &tag_data)) {
    if (tag_size == 0 || tag == 0x0000)
      goto next;

    switch (tag) {
      case 0x3c0a:
        if (tag_size != 16)
          goto error;
        memcpy (&preface->instance_uid, tag_data, 16);
        GST_DEBUG ("  instance uid = %s",
            mxf_ul_to_string (&preface->instance_uid, str));
        break;
      case 0x0102:
        if (tag_size != 16)
          goto error;
        memcpy (&preface->generation_uid, tag_data, 16);
        GST_DEBUG ("  generation uid = %s",
            mxf_ul_to_string (&preface->generation_uid, str));
        break;
      case 0x3b02:
        if (!mxf_timestamp_parse (&preface->last_modified_date, tag_data,
                tag_size))
          goto error;
        GST_DEBUG ("  last modified date = %d/%u/%u %u:%u:%u.%u",
            preface->last_modified_date.year, preface->last_modified_date.month,
            preface->last_modified_date.day, preface->last_modified_date.hour,
            preface->last_modified_date.minute,
            preface->last_modified_date.second,
            (preface->last_modified_date.quarter_msecond * 1000) / 256);
        break;
      case 0x3b05:
        if (tag_size != 2)
          goto error;
        preface->version = GST_READ_UINT16_BE (tag_data);
        GST_DEBUG ("  version = %u.%u", (preface->version >> 8),
            (preface->version & 0x0f));
        break;
      case 0x3b07:
        if (tag_size != 4)
          goto error;
        preface->object_model_version = GST_READ_UINT32_BE (tag_data);
        GST_DEBUG ("  object model version = %u",
            preface->object_model_version);
        break;
      case 0x3b08:
        if (tag_size != 16)
          goto error;
        memcpy (&preface->primary_package_uid, tag_data, 16);
        GST_DEBUG ("  primary package = %s",
            mxf_ul_to_string (&preface->primary_package_uid, str));
        break;
      case 0x3b06:{
        guint32 len;
        guint i;


        if (tag_size < 8)
          goto error;
        len = GST_READ_UINT32_BE (tag_data);
        GST_DEBUG ("  number of identifications = %u", len);
        if (len == 0)
          break;
        if (GST_READ_UINT32_BE (tag_data + 4) != 16)
          goto error;
        if (tag_size < 8 + len * 16)
          goto error;

        preface->n_identifications = len;
        preface->identifications_uids = g_new (MXFUL, len);
        for (i = 0; i < len; i++) {
          memcpy (&preface->identifications_uids[i], tag_data + 8 + i * 16, 16);
          GST_DEBUG ("  identification %u = %s", i,
              mxf_ul_to_string (&preface->identifications_uids[i], str));
        }
        break;
      }
      case 0x3b03:
        if (tag_size != 16)
          goto error;
        memcpy (&preface->content_storage_uid, tag_data, 16);
        GST_DEBUG ("  content storage = %s",
            mxf_ul_to_string (&preface->content_storage_uid, str));
        break;
      case 0x3b09:
        if (tag_size != 16)
          goto error;
        memcpy (&preface->operational_pattern, tag_data, 16);
        GST_DEBUG ("  operational pattern = %s",
            mxf_ul_to_string (&preface->operational_pattern, str));
        break;
      case 0x3b0a:{
        guint32 len;
        guint i;


        if (tag_size < 8)
          goto error;
        len = GST_READ_UINT32_BE (tag_data);
        GST_DEBUG ("  number of essence containers = %u", len);
        if (len == 0)
          break;
        if (GST_READ_UINT32_BE (tag_data + 4) != 16)
          goto error;
        if (tag_size < 8 + len * 16)
          goto error;

        preface->n_essence_containers = len;
        preface->essence_containers = g_new (MXFUL, len);
        for (i = 0; i < len; i++) {
          memcpy (&preface->essence_containers[i], tag_data + 8 + i * 16, 16);
          GST_DEBUG ("  essence container %u = %s", i,
              mxf_ul_to_string (&preface->essence_containers[i], str));
        }
        break;
      }
      case 0x3b0b:{
        guint32 len;
        guint i;


        if (tag_size < 8)
          goto error;
        len = GST_READ_UINT32_BE (tag_data);
        GST_DEBUG ("  number of DM schemes = %u", len);
        if (len == 0)
          break;
        if (GST_READ_UINT32_BE (tag_data + 4) != 16)
          goto error;
        if (tag_size < 8 + len * 16)
          goto error;

        preface->n_dm_schemes = len;
        preface->dm_schemes = g_new (MXFUL, len);
        for (i = 0; i < len; i++) {
          memcpy (&preface->dm_schemes[i], tag_data + 8 + i * 16, 16);
          GST_DEBUG ("  DM schemes %u = %s", i,
              mxf_ul_to_string (&preface->dm_schemes[i], str));
        }
        break;
      }
      default:
        if (!gst_metadata_add_custom_tag (primer, tag, tag_data, tag_size,
                &preface->other_tags))
          goto error;
        break;
    }

  next:
    data += 4 + tag_size;
    size -= 4 + tag_size;
  }

  return TRUE;

error:
  GST_ERROR ("Invalid preface");
  mxf_metadata_preface_reset (preface);

  return FALSE;
}

void
mxf_metadata_preface_reset (MXFMetadataPreface * preface)
{
  g_return_if_fail (preface != NULL);

  g_free (preface->identifications_uids);
  g_free (preface->identifications);
  g_free (preface->essence_containers);
  g_free (preface->dm_schemes);

  if (preface->other_tags)
    g_hash_table_destroy (preface->other_tags);

  memset (preface, 0, sizeof (MXFMetadataPreface));
}

gboolean
mxf_metadata_identification_parse (const MXFUL * key,
    MXFMetadataIdentification * identification,
    const MXFPrimerPack * primer, const guint8 * data, guint size)
{
  guint16 tag, tag_size;
  const guint8 *tag_data;
  gchar str[48];

  g_return_val_if_fail (data != NULL, FALSE);

  memset (identification, 0, sizeof (MXFMetadataIdentification));

  GST_DEBUG ("Parsing identification:");

  while (mxf_local_tag_parse (data, size, &tag, &tag_size, &tag_data)) {
    if (tag_size == 0 || tag == 0x0000)
      goto next;

    switch (tag) {
      case 0x3c0a:
        if (tag_size != 16)
          goto error;
        memcpy (&identification->instance_uid, tag_data, 16);
        GST_DEBUG ("  instance uid = %s",
            mxf_ul_to_string (&identification->instance_uid, str));
        break;
      case 0x3c09:
        if (tag_size != 16)
          goto error;
        memcpy (&identification->generation_uid, tag_data, 16);
        GST_DEBUG ("  generation uid = %s",
            mxf_ul_to_string (&identification->generation_uid, str));
        break;
      case 0x3c01:
        identification->company_name = mxf_utf16_to_utf8 (tag_data, tag_size);
        GST_DEBUG ("  company name = %s",
            GST_STR_NULL (identification->company_name));
        break;
      case 0x3c02:
        identification->product_name = mxf_utf16_to_utf8 (tag_data, tag_size);
        GST_DEBUG ("  product name = %s",
            GST_STR_NULL (identification->product_name));
        break;
      case 0x3c03:
        if (!mxf_product_version_parse (&identification->product_version,
                tag_data, tag_size))
          goto error;
        GST_DEBUG ("  product version = %u.%u.%u.%u.%u",
            identification->product_version.major,
            identification->product_version.minor,
            identification->product_version.patch,
            identification->product_version.build,
            identification->product_version.release);
        break;
      case 0x3c04:
        identification->version_string = mxf_utf16_to_utf8 (tag_data, tag_size);
        GST_DEBUG ("  version string = %s",
            GST_STR_NULL (identification->version_string));
        break;
      case 0x3c05:
        if (tag_size != 16)
          goto error;
        memcpy (&identification->product_uid, tag_data, 16);
        GST_DEBUG ("  product uid = %s",
            mxf_ul_to_string (&identification->product_uid, str));
        break;
      case 0x3c06:
        if (!mxf_timestamp_parse (&identification->modification_date, tag_data,
                tag_size))
          goto error;
        GST_DEBUG ("  modification date = %d/%u/%u %u:%u:%u.%u",
            identification->modification_date.year,
            identification->modification_date.month,
            identification->modification_date.day,
            identification->modification_date.hour,
            identification->modification_date.minute,
            identification->modification_date.second,
            (identification->modification_date.quarter_msecond * 1000) / 256);
        break;
      case 0x3c07:
        if (!mxf_product_version_parse (&identification->toolkit_version,
                tag_data, tag_size))
          goto error;
        GST_DEBUG ("  toolkit version = %u.%u.%u.%u.%u",
            identification->toolkit_version.major,
            identification->toolkit_version.minor,
            identification->toolkit_version.patch,
            identification->toolkit_version.build,
            identification->toolkit_version.release);
        break;
      case 0x3c08:
        identification->platform = mxf_utf16_to_utf8 (tag_data, tag_size);
        GST_DEBUG ("  platform = %s", GST_STR_NULL (identification->platform));
        break;
      default:
        if (!gst_metadata_add_custom_tag (primer, tag, tag_data, tag_size,
                &identification->other_tags))
          goto error;
        break;
    }

  next:
    data += 4 + tag_size;
    size -= 4 + tag_size;
  }

  return TRUE;

error:
  GST_ERROR ("Invalid identification");
  mxf_metadata_identification_reset (identification);

  return FALSE;
}

void mxf_metadata_identification_reset
    (MXFMetadataIdentification * identification)
{
  g_return_if_fail (identification != NULL);

  g_free (identification->company_name);
  g_free (identification->product_name);
  g_free (identification->version_string);
  g_free (identification->platform);

  if (identification->other_tags)
    g_hash_table_destroy (identification->other_tags);

  memset (identification, 0, sizeof (MXFMetadataIdentification));
}

gboolean
mxf_metadata_content_storage_parse (const MXFUL * key,
    MXFMetadataContentStorage * content_storage,
    const MXFPrimerPack * primer, const guint8 * data, guint size)
{
  guint16 tag, tag_size;
  const guint8 *tag_data;
  gchar str[48];

  g_return_val_if_fail (data != NULL, FALSE);

  memset (content_storage, 0, sizeof (MXFMetadataContentStorage));

  GST_DEBUG ("Parsing content storage:");

  while (mxf_local_tag_parse (data, size, &tag, &tag_size, &tag_data)) {
    if (tag_size == 0 || tag == 0x0000)
      goto next;

    switch (tag) {
      case 0x3c0a:
        if (tag_size != 16)
          goto error;
        memcpy (&content_storage->instance_uid, tag_data, 16);
        GST_DEBUG ("  instance uid = %s",
            mxf_ul_to_string (&content_storage->instance_uid, str));
        break;
      case 0x0102:
        if (tag_size != 16)
          goto error;
        memcpy (&content_storage->generation_uid, tag_data, 16);
        GST_DEBUG ("  generation uid = %s",
            mxf_ul_to_string (&content_storage->generation_uid, str));
        break;
      case 0x1901:{
        guint32 len;
        guint i;


        len = GST_READ_UINT32_BE (tag_data);
        GST_DEBUG ("  number of packages = %u", len);
        if (len == 0)
          break;
        if (GST_READ_UINT32_BE (tag_data + 4) != 16)
          goto error;
        if (tag_size < 8 + len * 16)
          goto error;

        content_storage->packages_uids = g_new (MXFUL, len);
        content_storage->n_packages = len;
        for (i = 0; i < len; i++) {
          memcpy (&content_storage->packages_uids[i], tag_data + 8 + i * 16,
              16);
          GST_DEBUG ("  package %u = %s", i,
              mxf_ul_to_string (&content_storage->packages_uids[i], str));
        }
        break;
      }
      case 0x1902:{
        guint32 len;
        guint i;


        len = GST_READ_UINT32_BE (tag_data);
        GST_DEBUG ("  number of essence container data = %u", len);
        if (len == 0)
          break;
        if (GST_READ_UINT32_BE (tag_data + 4) != 16)
          goto error;
        if (tag_size < 8 + len * 16)
          goto error;

        content_storage->essence_container_data_uids = g_new (MXFUL, len);
        content_storage->n_essence_container_data = len;
        for (i = 0; i < len; i++) {
          memcpy (&content_storage->essence_container_data_uids[i],
              tag_data + 8 + i * 16, 16);
          GST_DEBUG ("  essence container data %u = %s", i,
              mxf_ul_to_string (&content_storage->essence_container_data_uids
                  [i], str));
        }
        break;
      }
      default:
        if (!gst_metadata_add_custom_tag (primer, tag, tag_data, tag_size,
                &content_storage->other_tags))
          goto error;
        break;
    }

  next:
    data += 4 + tag_size;
    size -= 4 + tag_size;
  }

  return TRUE;

error:
  GST_ERROR ("Invalid content storage");
  mxf_metadata_content_storage_reset (content_storage);

  return FALSE;
}

void mxf_metadata_content_storage_reset
    (MXFMetadataContentStorage * content_storage)
{
  g_return_if_fail (content_storage != NULL);

  g_free (content_storage->packages);
  g_free (content_storage->packages_uids);
  g_free (content_storage->essence_container_data);
  g_free (content_storage->essence_container_data_uids);

  if (content_storage->other_tags)
    g_hash_table_destroy (content_storage->other_tags);

  memset (content_storage, 0, sizeof (MXFMetadataContentStorage));
}

gboolean
mxf_metadata_essence_container_data_parse (const MXFUL * key,
    MXFMetadataEssenceContainerData * essence_container_data,
    const MXFPrimerPack * primer, const guint8 * data, guint size)
{
  guint16 tag, tag_size;
  const guint8 *tag_data;
  gchar str[96];

  g_return_val_if_fail (data != NULL, FALSE);

  memset (essence_container_data, 0, sizeof (MXFMetadataEssenceContainerData));

  GST_DEBUG ("Parsing essence container data:");

  while (mxf_local_tag_parse (data, size, &tag, &tag_size, &tag_data)) {
    if (tag_size == 0 || tag == 0x0000)
      goto next;

    switch (tag) {
      case 0x3c0a:
        if (tag_size != 16)
          goto error;
        memcpy (&essence_container_data->instance_uid, tag_data, 16);
        GST_DEBUG ("  instance uid = %s",
            mxf_ul_to_string (&essence_container_data->instance_uid, str));
        break;
      case 0x2701:
        if (tag_size != 32)
          goto error;
        memcpy (&essence_container_data->linked_package_uid, tag_data, 32);
        GST_DEBUG ("  linked package = %s",
            mxf_umid_to_string (&essence_container_data->linked_package_uid,
                str));
        break;
      case 0x0102:
        if (tag_size != 16)
          goto error;
        memcpy (&essence_container_data->generation_uid, tag_data, 16);
        GST_DEBUG ("  generation uid = %s",
            mxf_ul_to_string (&essence_container_data->generation_uid, str));
        break;
      case 0x3f06:
        if (tag_size != 4)
          goto error;
        essence_container_data->index_sid = GST_READ_UINT32_BE (tag_data);
        GST_DEBUG ("  index sid = %u", essence_container_data->index_sid);
        break;
      case 0x3f07:
        if (tag_size != 4)
          goto error;
        essence_container_data->body_sid = GST_READ_UINT32_BE (tag_data);
        GST_DEBUG ("  body sid = %u", essence_container_data->body_sid);
        break;
      default:
        if (!gst_metadata_add_custom_tag (primer, tag, tag_data, tag_size,
                &essence_container_data->other_tags))
          goto error;
        break;
    }
  next:

    data += 4 + tag_size;
    size -= 4 + tag_size;
  }

  return TRUE;

error:
  GST_ERROR ("Invalid essence container data");
  mxf_metadata_essence_container_data_reset (essence_container_data);

  return FALSE;
}

void mxf_metadata_essence_container_data_reset
    (MXFMetadataEssenceContainerData * essence_container_data)
{
  g_return_if_fail (essence_container_data != NULL);

  if (essence_container_data->other_tags)
    g_hash_table_destroy (essence_container_data->other_tags);

  memset (essence_container_data, 0, sizeof (MXFMetadataEssenceContainerData));
}

gboolean
mxf_metadata_generic_package_parse (const MXFUL * key,
    MXFMetadataGenericPackage * generic_package,
    const MXFPrimerPack * primer, const guint8 * data, guint size)
{
  guint16 tag, tag_size;
  const guint8 *tag_data;
  gchar str[96];

  g_return_val_if_fail (data != NULL, FALSE);

  memset (generic_package, 0, sizeof (MXFMetadataGenericPackage));

  GST_DEBUG ("Parsing generic package:");

  while (mxf_local_tag_parse (data, size, &tag, &tag_size, &tag_data)) {
    if (tag_size == 0 || tag == 0x0000)
      goto next;

    switch (tag) {
      case 0x3c0a:
        if (tag_size != 16)
          goto error;
        memcpy (&generic_package->instance_uid, tag_data, 16);
        GST_DEBUG ("  instance uid = %s",
            mxf_ul_to_string (&generic_package->instance_uid, str));
        break;
      case 0x4401:
        if (tag_size != 32)
          goto error;
        memcpy (&generic_package->package_uid, tag_data, 32);
        GST_DEBUG ("  UMID = %s",
            mxf_umid_to_string (&generic_package->package_uid, str));
        break;
      case 0x0102:
        if (tag_size != 16)
          goto error;
        memcpy (&generic_package->generation_uid, tag_data, 16);
        GST_DEBUG ("  generation uid = %s",
            mxf_ul_to_string (&generic_package->generation_uid, str));
        break;
      case 0x4402:
        generic_package->name = mxf_utf16_to_utf8 (tag_data, tag_size);
        GST_DEBUG ("  name = %s", GST_STR_NULL (generic_package->name));
        break;
      case 0x4405:
        if (!mxf_timestamp_parse (&generic_package->package_creation_date,
                tag_data, tag_size))
          goto error;
        GST_DEBUG ("  creation date = %d/%u/%u %u:%u:%u.%u",
            generic_package->package_creation_date.year,
            generic_package->package_creation_date.month,
            generic_package->package_creation_date.day,
            generic_package->package_creation_date.hour,
            generic_package->package_creation_date.minute,
            generic_package->package_creation_date.second,
            (generic_package->package_creation_date.quarter_msecond * 1000) /
            256);
        break;
      case 0x4404:
        if (!mxf_timestamp_parse (&generic_package->package_modified_date,
                tag_data, tag_size))
          goto error;
        GST_DEBUG ("  modification date = %d/%u/%u %u:%u:%u.%u",
            generic_package->package_modified_date.year,
            generic_package->package_modified_date.month,
            generic_package->package_modified_date.day,
            generic_package->package_modified_date.hour,
            generic_package->package_modified_date.minute,
            generic_package->package_modified_date.second,
            (generic_package->package_modified_date.quarter_msecond * 1000) /
            256);
        break;
      case 0x4403:{
        guint32 len;
        guint i;


        len = GST_READ_UINT32_BE (tag_data);
        GST_DEBUG ("  number of tracks = %u", len);
        if (len == 0)
          break;
        if (GST_READ_UINT32_BE (tag_data + 4) != 16)
          goto error;
        if (tag_size < 8 + len * 16)
          goto error;

        generic_package->tracks_uids = g_new (MXFUL, len);
        generic_package->n_tracks = len;
        for (i = 0; i < len; i++) {
          memcpy (&generic_package->tracks_uids[i], tag_data + 8 + i * 16, 16);
          GST_DEBUG ("  track %u = %s", i,
              mxf_ul_to_string (&generic_package->tracks_uids[i], str));
        }
        break;
      }
      case 0x4701:
        if (tag_size != 16)
          goto error;

        generic_package->n_descriptors = 1;
        memcpy (&generic_package->descriptors_uid, tag_data, 16);
        GST_DEBUG ("  descriptor = %s",
            mxf_ul_to_string (&generic_package->descriptors_uid, str));
        break;
      default:
        if (!gst_metadata_add_custom_tag (primer, tag, tag_data, tag_size,
                &generic_package->other_tags))
          goto error;
        break;
    }

  next:
    data += 4 + tag_size;
    size -= 4 + tag_size;
  }

  return TRUE;

error:
  GST_ERROR ("Invalid package");
  mxf_metadata_generic_package_reset (generic_package);

  return FALSE;
}

void mxf_metadata_generic_package_reset
    (MXFMetadataGenericPackage * generic_package)
{
  g_return_if_fail (generic_package != NULL);

  g_free (generic_package->name);
  g_free (generic_package->tracks_uids);

  g_free (generic_package->tracks);

  if (generic_package->other_tags)
    g_hash_table_destroy (generic_package->other_tags);

  g_free (generic_package->descriptors);

  memset (generic_package, 0, sizeof (MXFMetadataGenericPackage));
}

gboolean
mxf_metadata_track_parse (const MXFUL * key,
    MXFMetadataTrack * track, const MXFPrimerPack * primer,
    const guint8 * data, guint size)
{
  guint16 tag, tag_size;
  const guint8 *tag_data;
  gchar str[48];

  g_return_val_if_fail (data != NULL, FALSE);

  memset (track, 0, sizeof (MXFMetadataTrack));

  GST_DEBUG ("Parsing track:");

  while (mxf_local_tag_parse (data, size, &tag, &tag_size, &tag_data)) {
    if (tag_size == 0 || tag == 0x0000)
      goto next;

    switch (tag) {
      case 0x3c0a:
        if (tag_size != 16)
          goto error;
        memcpy (&track->instance_uid, tag_data, 16);
        GST_DEBUG ("  instance uid = %s",
            mxf_ul_to_string (&track->instance_uid, str));
        break;
      case 0x0102:
        if (tag_size != 16)
          goto error;
        memcpy (&track->generation_uid, tag_data, 16);
        GST_DEBUG ("  generation uid = %s",
            mxf_ul_to_string (&track->generation_uid, str));
        break;
      case 0x4801:
        if (tag_size != 4)
          goto error;
        track->track_id = GST_READ_UINT32_BE (tag_data);
        GST_DEBUG ("  track id = %u", track->track_id);
        break;
      case 0x4804:
        if (tag_size != 4)
          goto error;
        track->track_number = GST_READ_UINT32_BE (tag_data);
        GST_DEBUG ("  track number = %u", track->track_number);
        break;
      case 0x4802:
        track->track_name = mxf_utf16_to_utf8 (tag_data, tag_size);
        GST_DEBUG ("  track name = %s", GST_STR_NULL (track->track_name));
        break;
      case 0x4b01:
        if (!mxf_fraction_parse (&track->edit_rate, tag_data, tag_size))
          goto error;
        GST_DEBUG ("  edit rate = %d/%d", track->edit_rate.n,
            track->edit_rate.d);
        break;
      case 0x4b02:
        if (tag_size != 8)
          goto error;
        track->origin = GST_READ_UINT64_BE (tag_data);
        GST_DEBUG ("  origin = %" G_GINT64_FORMAT, track->origin);
        break;
      case 0x4803:
        if (tag_size != 16)
          goto error;
        memcpy (&track->sequence_uid, tag_data, 16);
        GST_DEBUG ("  sequence uid = %s",
            mxf_ul_to_string (&track->sequence_uid, str));
        break;
      default:
        if (!gst_metadata_add_custom_tag (primer, tag, tag_data, tag_size,
                &track->other_tags))
          goto error;
        break;
    }

  next:
    data += 4 + tag_size;
    size -= 4 + tag_size;
  }

  return TRUE;

error:
  GST_ERROR ("Invalid track");
  mxf_metadata_track_reset (track);

  return FALSE;
}

void
mxf_metadata_track_reset (MXFMetadataTrack * track)
{
  g_return_if_fail (track != NULL);

  g_free (track->track_name);

  if (track->descriptor)
    g_free (track->descriptor);

  if (track->other_tags)
    g_hash_table_destroy (track->other_tags);

  memset (track, 0, sizeof (MXFMetadataTrack));
}

/* SMPTE RP224 */
static const struct
{
  guint8 ul[16];
  MXFMetadataTrackType type;
} mxf_metadata_track_identifier[] = {
  { {
  0x06, 0x0e, 0x2b, 0x34, 0x04, 0x01, 0x01, 0x01,
          0x01, 0x03, 0x02, 0x01, 0x01, 0x00, 0x00, 0x00},
      MXF_METADATA_TRACK_TIMECODE_12M_INACTIVE}, { {
  0x06, 0x0e, 0x2b, 0x34, 0x04, 0x01, 0x01, 0x01, 0x01, 0x03, 0x02, 0x01,
          0x02, 0x00, 0x00, 0x00}, MXF_METADATA_TRACK_TIMECODE_12M_ACTIVE}, { {
  0x06, 0x0e, 0x2b, 0x34, 0x04, 0x01, 0x01, 0x01, 0x01, 0x03, 0x02, 0x01,
          0x03, 0x00, 0x00, 0x00}, MXF_METADATA_TRACK_TIMECODE_309M}, { {
  0x06, 0x0e, 0x2b, 0x34, 0x04, 0x01, 0x01, 0x01, 0x01, 0x03, 0x02, 0x01,
          0x10, 0x00, 0x00, 0x00}, MXF_METADATA_TRACK_METADATA}, { {
  0x06, 0x0e, 0x2b, 0x34, 0x04, 0x01, 0x01, 0x01, 0x01, 0x03, 0x02, 0x02,
          0x01, 0x00, 0x00, 0x00}, MXF_METADATA_TRACK_PICTURE_ESSENCE}, { {
  0x06, 0x0e, 0x2b, 0x34, 0x04, 0x01, 0x01, 0x01, 0x01, 0x03, 0x02, 0x02,
          0x02, 0x00, 0x00, 0x00}, MXF_METADATA_TRACK_SOUND_ESSENCE}, { {
  0x06, 0x0e, 0x2b, 0x34, 0x04, 0x01, 0x01, 0x01, 0x01, 0x03, 0x02, 0x02,
          0x03, 0x00, 0x00, 0x00}, MXF_METADATA_TRACK_DATA_ESSENCE}, { {
  0x06, 0x0e, 0x2b, 0x34, 0x04, 0x01, 0x01, 0x01, 0x01, 0x03, 0x02, 0x03,
          0x01, 0x00, 0x00, 0x00}, MXF_METADATA_TRACK_AUXILIARY_DATA}, { {
  0x06, 0x0e, 0x2b, 0x34, 0x04, 0x01, 0x01, 0x01, 0x01, 0x03, 0x02, 0x03,
          0x02, 0x00, 0x00, 0x00}, MXF_METADATA_TRACK_PARSED_TEXT}
};

MXFMetadataTrackType
mxf_metadata_track_identifier_parse (const MXFUL * track_identifier)
{
  guint i;

  for (i = 0; i < G_N_ELEMENTS (mxf_metadata_track_identifier); i++)
    if (memcmp (&mxf_metadata_track_identifier[i].ul, &track_identifier->u,
            16) == 0)
      return mxf_metadata_track_identifier[i].type;

  return MXF_METADATA_TRACK_UNKNOWN;
}

gboolean
mxf_metadata_sequence_parse (const MXFUL * key,
    MXFMetadataSequence * sequence, const MXFPrimerPack * primer,
    const guint8 * data, guint size)
{
  guint16 tag, tag_size;
  const guint8 *tag_data;
  gchar str[48];

  g_return_val_if_fail (data != NULL, FALSE);

  memset (sequence, 0, sizeof (MXFMetadataSequence));

  GST_DEBUG ("Parsing sequence:");

  while (mxf_local_tag_parse (data, size, &tag, &tag_size, &tag_data)) {
    if (tag_size == 0 || tag == 0x0000)
      goto next;

    switch (tag) {
      case 0x3c0a:
        if (tag_size != 16)
          goto error;
        memcpy (&sequence->instance_uid, tag_data, 16);
        GST_DEBUG ("  instance uid = %s",
            mxf_ul_to_string (&sequence->instance_uid, str));
        break;
      case 0x0102:
        if (tag_size != 16)
          goto error;
        memcpy (&sequence->generation_uid, tag_data, 16);
        GST_DEBUG ("  generation uid = %s",
            mxf_ul_to_string (&sequence->generation_uid, str));
        break;
      case 0x0201:
        if (tag_size != 16)
          goto error;
        memcpy (&sequence->data_definition, tag_data, 16);
        GST_DEBUG ("  data definition = %s",
            mxf_ul_to_string (&sequence->data_definition, str));
        break;
      case 0x0202:
        if (tag_size != 8)
          goto error;
        sequence->duration = GST_READ_UINT64_BE (tag_data);
        GST_DEBUG ("  duration = %" G_GINT64_FORMAT, sequence->duration);
        break;
      case 0x1001:{
        guint32 len;
        guint i;


        len = GST_READ_UINT32_BE (tag_data);
        GST_DEBUG ("  number of structural components = %u", len);
        if (len == 0)
          break;
        if (GST_READ_UINT32_BE (tag_data + 4) != 16)
          goto error;
        if (tag_size < 8 + len * 16)
          goto error;

        sequence->structural_components_uids = g_new (MXFUL, len);
        sequence->n_structural_components = len;
        for (i = 0; i < len; i++) {
          memcpy (&sequence->structural_components_uids[i],
              tag_data + 8 + i * 16, 16);
          GST_DEBUG ("  structural component %u = %s", i,
              mxf_ul_to_string (&sequence->structural_components_uids[i], str));
        }
        break;
      }
      default:
        if (!gst_metadata_add_custom_tag (primer, tag, tag_data, tag_size,
                &sequence->other_tags))
          goto error;
        break;
    }

  next:
    data += 4 + tag_size;
    size -= 4 + tag_size;
  }

  return TRUE;

error:
  GST_ERROR ("Invalid sequence");
  mxf_metadata_sequence_reset (sequence);

  return FALSE;
}

void
mxf_metadata_sequence_reset (MXFMetadataSequence * sequence)
{
  g_return_if_fail (sequence != NULL);

  g_free (sequence->structural_components_uids);
  g_free (sequence->structural_components);

  if (sequence->other_tags)
    g_hash_table_destroy (sequence->other_tags);

  memset (sequence, 0, sizeof (MXFMetadataSequence));
}

gboolean
mxf_metadata_structural_component_parse (const MXFUL * key,
    MXFMetadataStructuralComponent * component,
    const MXFPrimerPack * primer, guint16 type, const guint8 * data, guint size)
{
  guint16 tag, tag_size;
  const guint8 *tag_data;
  gchar str[96];

  g_return_val_if_fail (data != NULL, FALSE);

  memset (component, 0, sizeof (MXFMetadataStructuralComponent));

  GST_DEBUG ("Parsing structural component:");

  component->type = type;
  GST_DEBUG ("  type = %s",
      (component->type ==
          MXF_METADATA_TIMECODE_COMPONENT) ? "timecode component" :
      "source clip");

  while (mxf_local_tag_parse (data, size, &tag, &tag_size, &tag_data)) {
    if (tag_size == 0 || tag == 0x0000)
      goto next;

    switch (tag) {
      case 0x3c0a:
        if (tag_size != 16)
          goto error;
        memcpy (&component->instance_uid, tag_data, 16);
        GST_DEBUG ("  instance uid = %s",
            mxf_ul_to_string (&component->instance_uid, str));
        break;
      case 0x0102:
        if (tag_size != 16)
          goto error;
        memcpy (&component->generation_uid, tag_data, 16);
        GST_DEBUG ("  generation uid = %s",
            mxf_ul_to_string (&component->generation_uid, str));
        break;
      case 0x0201:
        if (tag_size != 16)
          goto error;
        memcpy (&component->data_definition, tag_data, 16);
        GST_DEBUG ("  data definition = %s",
            mxf_ul_to_string (&component->data_definition, str));
        break;
      case 0x0202:
        if (tag_size != 8)
          goto error;
        component->duration = GST_READ_UINT64_BE (tag_data);
        GST_DEBUG ("  duration = %" G_GINT64_FORMAT, component->duration);
        break;
        /* Timecode component specifics */
      case 0x1502:
        if (type != MXF_METADATA_TIMECODE_COMPONENT)
          goto DFLT;
        if (tag_size != 2)
          goto error;
        component->timecode_component.rounded_timecode_base =
            GST_READ_UINT16_BE (tag_data);
        GST_DEBUG ("  rounded timecode base = %u",
            component->timecode_component.rounded_timecode_base);
        break;
      case 0x1501:
        if (type != MXF_METADATA_TIMECODE_COMPONENT)
          goto DFLT;
        if (tag_size != 8)
          goto error;
        component->timecode_component.start_timecode =
            GST_READ_UINT64_BE (tag_data);
        GST_DEBUG ("  start timecode = %" G_GINT64_FORMAT,
            component->timecode_component.start_timecode);
        break;
      case 0x1503:
        if (type != MXF_METADATA_TIMECODE_COMPONENT)
          goto DFLT;
        if (tag_size != 1)
          goto error;
        component->timecode_component.drop_frame =
            (GST_READ_UINT8 (tag_data) != 0);
        GST_DEBUG ("  drop frame = %s",
            (component->timecode_component.drop_frame) ? "yes" : "no");
        break;
        /* Source clip specifics */
      case 0x1201:
        if (type != MXF_METADATA_SOURCE_CLIP)
          goto DFLT;
        if (tag_size != 8)
          goto error;
        component->source_clip.start_position = GST_READ_UINT64_BE (tag_data);
        GST_DEBUG ("  start position = %" G_GINT64_FORMAT,
            component->source_clip.start_position);
        break;
      case 0x1101:
        if (type != MXF_METADATA_SOURCE_CLIP)
          goto DFLT;
        if (tag_size != 32)
          goto error;
        memcpy (&component->source_clip.source_package_id, tag_data, 32);
        GST_DEBUG ("  source package id = %s",
            mxf_umid_to_string (&component->source_clip.source_package_id,
                str));
        break;
      case 0x1102:
        if (type != MXF_METADATA_SOURCE_CLIP)
          goto DFLT;
        if (tag_size != 4)
          goto error;
        component->source_clip.source_track_id = GST_READ_UINT32_BE (tag_data);
        GST_DEBUG ("  source track id = %u",
            component->source_clip.source_track_id);
        break;
      DFLT:
      default:
        if (!gst_metadata_add_custom_tag (primer, tag, tag_data, tag_size,
                &component->other_tags))
          goto error;
        break;
    }

  next:
    data += 4 + tag_size;
    size -= 4 + tag_size;
  }

  return TRUE;

error:
  GST_ERROR ("Invalid structural component");
  mxf_metadata_structural_component_reset (component);

  return FALSE;
}

void mxf_metadata_structural_component_reset
    (MXFMetadataStructuralComponent * component)
{
  g_return_if_fail (component != NULL);

  if (component->other_tags)
    g_hash_table_destroy (component->other_tags);

  memset (component, 0, sizeof (MXFMetadataStructuralComponent));
}

gboolean
mxf_metadata_descriptor_parse (const MXFUL * key,
    MXFMetadataGenericDescriptor * descriptor,
    const MXFPrimerPack * primer, guint16 type,
    const guint8 * data, guint size,
    MXFMetadataDescriptorHandleTag handle_tag, MXFMetadataDescriptorReset reset)
{
  guint16 tag, tag_size;
  const guint8 *tag_data;

  g_return_val_if_fail (data != NULL, FALSE);
  g_return_val_if_fail (key != NULL, FALSE);
  g_return_val_if_fail (primer != NULL, FALSE);
  g_return_val_if_fail (descriptor != NULL, FALSE);
  g_return_val_if_fail (handle_tag != NULL, FALSE);
  g_return_val_if_fail (reset != NULL, FALSE);

  reset (descriptor);

  descriptor->type = type;

  GST_DEBUG ("Parsing descriptor of type 0x%04x:", type);

  while (mxf_local_tag_parse (data, size, &tag, &tag_size, &tag_data)) {
    if (tag_size == 0 || tag == 0x0000)
      goto next;

    if (!handle_tag (descriptor, primer, tag, tag_data, tag_size))
      if (!gst_metadata_add_custom_tag (primer, tag, tag_data, tag_size,
              &((MXFMetadataGenericDescriptor *) descriptor)->other_tags))
        goto next;

  next:
    data += 4 + tag_size;
    size -= 4 + tag_size;
  }
  return TRUE;
}

gboolean
mxf_metadata_generic_descriptor_handle_tag (MXFMetadataGenericDescriptor *
    descriptor, const MXFPrimerPack * primer, guint16 tag,
    const guint8 * tag_data, guint16 tag_size)
{
  gboolean ret = FALSE;
  gchar str[48];

  switch (tag) {
    case 0x3c0a:
      if (tag_size != 16)
        goto error;
      memcpy (&descriptor->instance_uid, tag_data, 16);
      GST_DEBUG ("  instance uid = %s",
          mxf_ul_to_string (&descriptor->instance_uid, str));
      ret = TRUE;
      break;
    case 0x0102:
      if (tag_size != 16)
        goto error;
      memcpy (&descriptor->generation_uid, tag_data, 16);
      GST_DEBUG ("  generation uid = %s",
          mxf_ul_to_string (&descriptor->generation_uid, str));
      ret = TRUE;
      break;
    case 0x2f01:{
      guint32 len;
      guint i;

      if (tag_size < 8)
        goto error;

      len = GST_READ_UINT32_BE (tag_data);
      GST_DEBUG ("  number of locators = %u", len);
      if (len == 0)
        return TRUE;

      if (GST_READ_UINT32_BE (tag_data + 4) != 16)
        goto error;

      descriptor->locators_uids = g_new (MXFUL, len);
      descriptor->n_locators = len;
      for (i = 0; i < len; i++) {
        memcpy (&descriptor->locators_uids[i], tag_data + 8 + i * 16, 16);
        GST_DEBUG ("  locator %u = %s", i,
            mxf_ul_to_string (&descriptor->locators_uids[i], str));
      }
      ret = TRUE;
      break;
    }
  }

  return ret;

error:
  GST_ERROR ("Invalid generic descriptor tag 0x%04x of size %u", tag, tag_size);

  return TRUE;
}

void mxf_metadata_generic_descriptor_reset
    (MXFMetadataGenericDescriptor * descriptor)
{
  g_return_if_fail (descriptor != NULL);

  if (descriptor->locators_uids)
    g_free (descriptor->locators_uids);

  if (descriptor->locators)
    g_free (descriptor->locators);

  if (descriptor->other_tags)
    g_hash_table_destroy (descriptor->other_tags);

  memset (descriptor, 0, sizeof (MXFMetadataGenericDescriptor));
}

gboolean
mxf_metadata_file_descriptor_handle_tag (MXFMetadataGenericDescriptor * d,
    const MXFPrimerPack * primer, guint16 tag, const guint8 * tag_data,
    guint16 tag_size)
{
  MXFMetadataFileDescriptor *descriptor = (MXFMetadataFileDescriptor *) d;
  gboolean ret = FALSE;
  gchar str[48];

  switch (tag) {
    case 0x3006:
      if (tag_size != 4)
        goto error;
      descriptor->linked_track_id = GST_READ_UINT32_BE (tag_data);
      GST_DEBUG ("  linked track id = %u", descriptor->linked_track_id);
      ret = TRUE;
      break;
    case 0x3001:
      if (!mxf_fraction_parse (&descriptor->sample_rate, tag_data, tag_size))
        goto error;
      GST_DEBUG ("  sample rate = %d/%d", descriptor->sample_rate.n,
          descriptor->sample_rate.d);
      ret = TRUE;
      break;
    case 0x3002:
      if (tag_size != 8)
        goto error;
      descriptor->container_duration = GST_READ_UINT64_BE (tag_data);
      GST_DEBUG ("  container duration = %" G_GINT64_FORMAT,
          descriptor->container_duration);
      ret = TRUE;
      break;
    case 0x3004:
      if (tag_size != 16)
        goto error;
      memcpy (&descriptor->essence_container, tag_data, 16);
      GST_DEBUG ("  essence container = %s",
          mxf_ul_to_string (&descriptor->essence_container, str));
      ret = TRUE;
      break;
    case 0x3005:
      if (tag_size != 16)
        goto error;
      memcpy (&descriptor->codec, tag_data, 16);
      GST_DEBUG ("  codec = %s", mxf_ul_to_string (&descriptor->codec, str));
      ret = TRUE;
      break;
    default:
      ret =
          mxf_metadata_generic_descriptor_handle_tag (d, primer, tag, tag_data,
          tag_size);
      break;
  }

  return ret;

error:
  GST_ERROR ("Invalid file descriptor tag 0x%04x of size %u", tag, tag_size);

  return TRUE;
}

void
mxf_metadata_file_descriptor_reset (MXFMetadataFileDescriptor * descriptor)
{
  g_return_if_fail (descriptor != NULL);

  mxf_metadata_generic_descriptor_reset ((MXFMetadataGenericDescriptor *)
      descriptor);

  MXF_METADATA_DESCRIPTOR_CLEAR (descriptor, MXFMetadataFileDescriptor,
      MXFMetadataGenericDescriptor);

  descriptor->parent.is_file_descriptor = TRUE;
}

gboolean
    mxf_metadata_generic_sound_essence_descriptor_handle_tag
    (MXFMetadataGenericDescriptor * d, const MXFPrimerPack * primer,
    guint16 tag, const guint8 * tag_data, guint16 tag_size)
{
  MXFMetadataGenericSoundEssenceDescriptor *descriptor =
      (MXFMetadataGenericSoundEssenceDescriptor *) d;
  gboolean ret = FALSE;
  gchar str[48];

  switch (tag) {
    case 0x3d03:
      if (!mxf_fraction_parse (&descriptor->audio_sampling_rate, tag_data,
              tag_size))
        goto error;
      GST_DEBUG ("  audio sampling rate = %d/%d",
          descriptor->audio_sampling_rate.n, descriptor->audio_sampling_rate.d);
      ret = TRUE;
      break;
    case 0x3d02:
      if (tag_size != 1)
        goto error;
      descriptor->locked = (GST_READ_UINT8 (tag_data) != 0);
      GST_DEBUG ("  locked = %s", (descriptor->locked) ? "yes" : "no");
      ret = TRUE;
      break;
    case 0x3d04:
      if (tag_size != 1)
        goto error;
      descriptor->audio_ref_level = GST_READ_UINT8 (tag_data);
      GST_DEBUG ("  audio ref level = %d", descriptor->audio_ref_level);
      ret = TRUE;
      break;
    case 0x3d05:
      if (tag_size != 1)
        goto error;
      descriptor->electro_spatial_formulation = GST_READ_UINT8 (tag_data);
      GST_DEBUG ("  electro spatial formulation = %u",
          descriptor->electro_spatial_formulation);
      ret = TRUE;
      break;
    case 0x3d07:
      if (tag_size != 4)
        goto error;
      descriptor->channel_count = GST_READ_UINT32_BE (tag_data);
      GST_DEBUG ("  channel count = %u", descriptor->channel_count);
      ret = TRUE;
      break;
    case 0x3d01:
      if (tag_size != 4)
        goto error;
      descriptor->quantization_bits = GST_READ_UINT32_BE (tag_data);
      GST_DEBUG ("  quantization bits = %u", descriptor->quantization_bits);
      ret = TRUE;
      break;
    case 0x3d0c:
      if (tag_size != 1)
        goto error;
      descriptor->dial_norm = GST_READ_UINT8 (tag_data);
      GST_DEBUG ("  dial norm = %d", descriptor->dial_norm);
      ret = TRUE;
      break;
    case 0x3d06:
      if (tag_size != 16)
        goto error;
      memcpy (&descriptor->sound_essence_compression, tag_data, 16);
      GST_DEBUG ("  sound essence compression = %s",
          mxf_ul_to_string (&descriptor->sound_essence_compression, str));
      ret = TRUE;
      break;
    default:
      ret =
          mxf_metadata_file_descriptor_handle_tag (d, primer, tag, tag_data,
          tag_size);
      break;
  }

  return ret;

error:
  GST_ERROR ("Invalid generic sound essence descriptor tag 0x%04x of size %u",
      tag, tag_size);

  return TRUE;
}

void mxf_metadata_generic_sound_essence_descriptor_reset
    (MXFMetadataGenericSoundEssenceDescriptor * descriptor)
{
  g_return_if_fail (descriptor != NULL);

  mxf_metadata_file_descriptor_reset ((MXFMetadataFileDescriptor *) descriptor);

  MXF_METADATA_DESCRIPTOR_CLEAR (descriptor,
      MXFMetadataGenericSoundEssenceDescriptor, MXFMetadataFileDescriptor);

  descriptor->audio_sampling_rate.n = 48000;
  descriptor->audio_sampling_rate.d = 1;
}

gboolean
    mxf_metadata_generic_picture_essence_descriptor_handle_tag
    (MXFMetadataGenericDescriptor * d, const MXFPrimerPack * primer,
    guint16 tag, const guint8 * tag_data, guint16 tag_size)
{
  MXFMetadataGenericPictureEssenceDescriptor *descriptor =
      (MXFMetadataGenericPictureEssenceDescriptor *) d;
  gboolean ret = FALSE;
  gchar str[48];

  switch (tag) {
    case 0x3215:
      if (tag_size != 1)
        goto error;
      descriptor->signal_standard = GST_READ_UINT8 (tag_data);
      GST_DEBUG ("  signal standard = %u", descriptor->signal_standard);
      ret = TRUE;
      break;
    case 0x320c:
      if (tag_size != 1)
        goto error;
      descriptor->frame_layout = GST_READ_UINT8 (tag_data);
      GST_DEBUG ("  frame layout = %u", descriptor->frame_layout);
      ret = TRUE;
      break;
    case 0x3203:
      if (tag_size != 4)
        goto error;
      descriptor->stored_width = GST_READ_UINT32_BE (tag_data);
      GST_DEBUG ("  stored width = %u", descriptor->stored_width);
      ret = TRUE;
      break;
    case 0x3202:
      if (tag_size != 4)
        goto error;
      descriptor->stored_height = GST_READ_UINT32_BE (tag_data);
      GST_DEBUG ("  stored height = %u", descriptor->stored_height);
      ret = TRUE;
      break;
    case 0x3216:
      if (tag_size != 4)
        goto error;
      descriptor->stored_f2_offset = GST_READ_UINT32_BE (tag_data);
      GST_DEBUG ("  stored f2 offset = %d", descriptor->stored_f2_offset);
      ret = TRUE;
      break;
    case 0x3205:
      if (tag_size != 4)
        goto error;
      descriptor->sampled_width = GST_READ_UINT32_BE (tag_data);
      GST_DEBUG ("  sampled width = %u", descriptor->sampled_width);
      ret = TRUE;
      break;
    case 0x3204:
      if (tag_size != 4)
        goto error;
      descriptor->sampled_height = GST_READ_UINT32_BE (tag_data);
      GST_DEBUG ("  sampled height = %u", descriptor->sampled_height);
      ret = TRUE;
      break;
    case 0x3206:
      if (tag_size != 4)
        goto error;
      descriptor->sampled_x_offset = GST_READ_UINT32_BE (tag_data);
      GST_DEBUG ("  sampled x offset = %d", descriptor->sampled_x_offset);
      ret = TRUE;
      break;
    case 0x3207:
      if (tag_size != 4)
        goto error;
      descriptor->sampled_y_offset = GST_READ_UINT32_BE (tag_data);
      GST_DEBUG ("  sampled y offset = %d", descriptor->sampled_y_offset);
      ret = TRUE;
      break;
    case 0x3208:
      if (tag_size != 4)
        goto error;
      descriptor->display_height = GST_READ_UINT32_BE (tag_data);
      GST_DEBUG ("  display height = %u", descriptor->display_height);
      ret = TRUE;
      break;
    case 0x3209:
      if (tag_size != 4)
        goto error;
      descriptor->display_width = GST_READ_UINT32_BE (tag_data);
      GST_DEBUG ("  display width = %u", descriptor->display_width);
      ret = TRUE;
      break;
    case 0x320a:
      if (tag_size != 4)
        goto error;
      descriptor->display_x_offset = GST_READ_UINT32_BE (tag_data);
      GST_DEBUG ("  display x offset = %d", descriptor->display_x_offset);
      ret = TRUE;
      break;
    case 0x320b:
      if (tag_size != 4)
        goto error;
      descriptor->display_y_offset = GST_READ_UINT32_BE (tag_data);
      GST_DEBUG ("  display y offset = %d", descriptor->display_y_offset);
      ret = TRUE;
      break;
    case 0x3217:
      if (tag_size != 4)
        goto error;
      descriptor->display_f2_offset = GST_READ_UINT32_BE (tag_data);
      GST_DEBUG ("  display f2 offset = %d", descriptor->display_f2_offset);
      ret = TRUE;
      break;
    case 0x320e:
      if (!mxf_fraction_parse (&descriptor->aspect_ratio, tag_data, tag_size))
        goto error;
      GST_DEBUG ("  aspect ratio = %d/%d", descriptor->aspect_ratio.n,
          descriptor->aspect_ratio.d);
      ret = TRUE;
      break;
    case 0x3218:
      if (tag_size != 1)
        goto error;
      descriptor->active_format_descriptor = GST_READ_UINT8 (tag_data);
      GST_DEBUG ("  active format descriptor = %u",
          descriptor->active_format_descriptor);
      ret = TRUE;
      break;
    case 0x320d:
      if (tag_size < 8)
        goto error;

      if (GST_READ_UINT32_BE (tag_data) == 0)
        return TRUE;

      if (GST_READ_UINT32_BE (tag_data) != 2 &&
          GST_READ_UINT32_BE (tag_data + 4) != 4)
        goto error;

      if (tag_size != 16)
        goto error;

      descriptor->video_line_map[0] = GST_READ_UINT32_BE (tag_data + 8);
      descriptor->video_line_map[1] = GST_READ_UINT32_BE (tag_data + 12);
      GST_DEBUG ("  video line map = {%i, %i}", descriptor->video_line_map[0],
          descriptor->video_line_map[1]);
      ret = TRUE;
      break;
    case 0x320f:
      if (tag_size != 1)
        goto error;
      descriptor->alpha_transparency = GST_READ_UINT8 (tag_data);
      GST_DEBUG ("  alpha transparency = %u", descriptor->alpha_transparency);
      ret = TRUE;
      break;
    case 0x3210:
      if (tag_size != 16)
        goto error;
      memcpy (&descriptor->capture_gamma, tag_data, 16);
      GST_DEBUG ("  capture gamma = %s",
          mxf_ul_to_string (&descriptor->capture_gamma, str));
      ret = TRUE;
      break;
    case 0x3211:
      if (tag_size != 4)
        goto error;
      descriptor->image_alignment_offset = GST_READ_UINT32_BE (tag_data);
      GST_DEBUG ("  image alignment offset = %u",
          descriptor->image_alignment_offset);
      ret = TRUE;
      break;
    case 0x3213:
      if (tag_size != 4)
        goto error;
      descriptor->image_start_offset = GST_READ_UINT32_BE (tag_data);
      GST_DEBUG ("  image start offset = %u", descriptor->image_start_offset);
      ret = TRUE;
      break;
    case 0x3214:
      if (tag_size != 4)
        goto error;
      descriptor->image_end_offset = GST_READ_UINT32_BE (tag_data);
      GST_DEBUG ("  image end offset = %u", descriptor->image_end_offset);
      ret = TRUE;
      break;
    case 0x3212:
      if (tag_size != 1)
        goto error;
      descriptor->field_dominance = GST_READ_UINT8 (tag_data);
      GST_DEBUG ("  field dominance = %u", descriptor->field_dominance);
      ret = TRUE;
      break;
    case 0x3201:
      if (tag_size != 16)
        goto error;
      memcpy (&descriptor->picture_essence_coding, tag_data, 16);
      GST_DEBUG ("  picture essence coding = %s",
          mxf_ul_to_string (&descriptor->picture_essence_coding, str));
      ret = TRUE;
      break;
    default:
      ret =
          mxf_metadata_file_descriptor_handle_tag (d, primer, tag, tag_data,
          tag_size);
      break;
  }

  return ret;

error:
  GST_ERROR ("Invalid generic picture essence descriptor tag 0x%04x of size %u",
      tag, tag_size);

  return TRUE;
}

void mxf_metadata_generic_picture_essence_descriptor_reset
    (MXFMetadataGenericPictureEssenceDescriptor * descriptor)
{
  g_return_if_fail (descriptor != NULL);

  mxf_metadata_file_descriptor_reset ((MXFMetadataFileDescriptor *) descriptor);

  MXF_METADATA_DESCRIPTOR_CLEAR (descriptor,
      MXFMetadataGenericPictureEssenceDescriptor, MXFMetadataFileDescriptor);

  descriptor->signal_standard = 1;
}

void mxf_metadata_generic_picture_essence_descriptor_set_caps
    (MXFMetadataGenericPictureEssenceDescriptor * descriptor, GstCaps * caps)
{
  guint par_n, par_d;
  guint width, height;
  MXFMetadataFileDescriptor *f = (MXFMetadataFileDescriptor *) descriptor;

  g_return_if_fail (descriptor != NULL);
  g_return_if_fail (GST_IS_CAPS (caps));

  gst_caps_set_simple (caps, "framerate", GST_TYPE_FRACTION, f->sample_rate.n,
      f->sample_rate.d, NULL);

  width = descriptor->stored_width;
  height = descriptor->stored_height;

  /* If the video is stored as separate fields the
   * height is only the height of one field, i.e.
   * half the height of the frame.
   *
   * See SMPTE 377M E2.2 and E1.2
   */
  if (descriptor->frame_layout == 1 || descriptor->frame_layout == 2)
    height *= 2;

  if (width == 0 || height == 0)
    return;

  gst_caps_set_simple (caps, "width", G_TYPE_INT, width, "height", G_TYPE_INT,
      height, NULL);

  if (descriptor->aspect_ratio.n == 0 || descriptor->aspect_ratio.d == 0)
    return;

  par_n = height * descriptor->aspect_ratio.n;
  par_d = width * descriptor->aspect_ratio.d;

  gst_caps_set_simple (caps, "pixel-aspect-ratio", GST_TYPE_FRACTION,
      par_n, par_d, NULL);
}

gboolean
    mxf_metadata_cdci_picture_essence_descriptor_handle_tag
    (MXFMetadataGenericDescriptor * d, const MXFPrimerPack * primer,
    guint16 tag, const guint8 * tag_data, guint16 tag_size) {
  MXFMetadataCDCIPictureEssenceDescriptor *descriptor =
      (MXFMetadataCDCIPictureEssenceDescriptor *) d;
  gboolean ret = FALSE;

  switch (tag) {
    case 0x3301:
      if (tag_size != 4)
        goto error;
      descriptor->component_depth = GST_READ_UINT32_BE (tag_data);
      GST_DEBUG ("  component depth = %u", descriptor->component_depth);
      ret = TRUE;
      break;
    case 0x3302:
      if (tag_size != 4)
        goto error;
      descriptor->horizontal_subsampling = GST_READ_UINT32_BE (tag_data);
      GST_DEBUG ("  horizontal subsampling = %u",
          descriptor->horizontal_subsampling);
      ret = TRUE;
      break;
    case 0x3308:
      if (tag_size != 4)
        goto error;
      descriptor->vertical_subsampling = GST_READ_UINT32_BE (tag_data);
      GST_DEBUG ("  vertical subsampling = %u",
          descriptor->vertical_subsampling);
      ret = TRUE;
      break;
    case 0x3303:
      if (tag_size != 1)
        goto error;
      descriptor->color_siting = GST_READ_UINT8 (tag_data);
      GST_DEBUG ("  color siting = %u", descriptor->color_siting);
      ret = TRUE;
      break;
    case 0x330b:
      if (tag_size != 1)
        goto error;
      descriptor->reversed_byte_order = GST_READ_UINT8 (tag_data);
      GST_DEBUG ("  reversed byte order = %s",
          (descriptor->reversed_byte_order) ? "yes" : "no");
      ret = TRUE;
      break;
    case 0x3307:
      if (tag_size != 2)
        goto error;
      descriptor->padding_bits = GST_READ_UINT16_BE (tag_data);
      GST_DEBUG ("  padding bits = %d", descriptor->padding_bits);
      ret = TRUE;
      break;
    case 0x3309:
      if (tag_size != 4)
        goto error;
      descriptor->alpha_sample_depth = GST_READ_UINT32_BE (tag_data);
      GST_DEBUG ("  alpha sample depth = %u", descriptor->alpha_sample_depth);
      ret = TRUE;
      break;
    case 0x3304:
      if (tag_size != 4)
        goto error;
      descriptor->black_ref_level = GST_READ_UINT32_BE (tag_data);
      GST_DEBUG ("  black ref level = %u", descriptor->black_ref_level);
      ret = TRUE;
      break;
    case 0x3305:
      if (tag_size != 4)
        goto error;
      descriptor->white_ref_level = GST_READ_UINT32_BE (tag_data);
      GST_DEBUG ("  white ref level = %u", descriptor->white_ref_level);
      ret = TRUE;
      break;
    case 0x3306:
      if (tag_size != 4)
        goto error;
      descriptor->color_range = GST_READ_UINT32_BE (tag_data);
      GST_DEBUG ("  color range = %u", descriptor->color_range);
      ret = TRUE;
      break;
    default:
      ret =
          mxf_metadata_generic_picture_essence_descriptor_handle_tag (d, primer,
          tag, tag_data, tag_size);
      break;
  }

  return ret;

error:
  GST_ERROR ("Invalid CDCI picture essence descriptor tag 0x%04x of size %u",
      tag, tag_size);

  return TRUE;
}

void mxf_metadata_cdci_picture_essence_descriptor_reset
    (MXFMetadataCDCIPictureEssenceDescriptor * descriptor)
{
  g_return_if_fail (descriptor != NULL);

  mxf_metadata_generic_picture_essence_descriptor_reset (
      (MXFMetadataGenericPictureEssenceDescriptor *) descriptor);

  MXF_METADATA_DESCRIPTOR_CLEAR (descriptor,
      MXFMetadataCDCIPictureEssenceDescriptor,
      MXFMetadataGenericPictureEssenceDescriptor);
}

gboolean
    mxf_metadata_rgba_picture_essence_descriptor_handle_tag
    (MXFMetadataGenericDescriptor * d, const MXFPrimerPack * primer,
    guint16 tag, const guint8 * tag_data, guint16 tag_size)
{
  MXFMetadataRGBAPictureEssenceDescriptor *descriptor =
      (MXFMetadataRGBAPictureEssenceDescriptor *) d;
  gboolean ret = FALSE;

  switch (tag) {
    case 0x3406:
      if (tag_size != 4)
        goto error;
      descriptor->component_max_ref = GST_READ_UINT32_BE (tag_data);
      GST_DEBUG ("  component max ref = %u", descriptor->component_max_ref);
      break;
    case 0x3407:
      if (tag_size != 4)
        goto error;
      descriptor->component_min_ref = GST_READ_UINT32_BE (tag_data);
      GST_DEBUG ("  component min ref = %u", descriptor->component_min_ref);
      break;
    case 0x3408:
      if (tag_size != 4)
        goto error;
      descriptor->alpha_max_ref = GST_READ_UINT32_BE (tag_data);
      GST_DEBUG ("  alpha max ref = %u", descriptor->alpha_max_ref);
      break;
    case 0x3409:
      if (tag_size != 4)
        goto error;
      descriptor->alpha_min_ref = GST_READ_UINT32_BE (tag_data);
      GST_DEBUG ("  alpha min ref = %u", descriptor->alpha_min_ref);
      break;
    case 0x3405:
      if (tag_size != 1)
        goto error;
      descriptor->scanning_direction = GST_READ_UINT8 (tag_data);
      GST_DEBUG ("  scanning direction = %u", descriptor->scanning_direction);
      break;
    case 0x3401:
    case 0x3403:
    case 0x3404:
      /* TODO: handle this */
      GST_WARNING ("  tag 0x%04x not implemented yet", tag);
      break;
    default:
      ret =
          mxf_metadata_generic_picture_essence_descriptor_handle_tag (d, primer,
          tag, tag_data, tag_size);
      break;
  }

  return ret;

error:
  GST_ERROR ("Invalid RGBA picture essence descriptor tag 0x%04x of size %u",
      tag, tag_size);

  return TRUE;
}

void mxf_metadata_rgba_picture_essence_descriptor_reset
    (MXFMetadataRGBAPictureEssenceDescriptor * descriptor)
{
  g_return_if_fail (descriptor != NULL);

  mxf_metadata_generic_picture_essence_descriptor_reset (
      (MXFMetadataGenericPictureEssenceDescriptor *) descriptor);

  MXF_METADATA_DESCRIPTOR_CLEAR (descriptor,
      MXFMetadataRGBAPictureEssenceDescriptor,
      MXFMetadataGenericPictureEssenceDescriptor);

  descriptor->component_max_ref = 255;
  descriptor->alpha_max_ref = 255;
}

gboolean
mxf_metadata_multiple_descriptor_handle_tag (MXFMetadataGenericDescriptor * d,
    const MXFPrimerPack * primer, guint16 tag, const guint8 * tag_data,
    guint16 tag_size)
{
  MXFMetadataMultipleDescriptor *descriptor =
      (MXFMetadataMultipleDescriptor *) d;
  gboolean ret = FALSE;
  gchar str[48];

  switch (tag) {
    case 0x3f01:{
      guint32 len;
      guint i;

      if (tag_size < 8)
        goto error;
      len = GST_READ_UINT32_BE (tag_data);
      GST_DEBUG ("  number of sub descriptors = %u", len);
      if (len == 0)
        return TRUE;

      if (GST_READ_UINT32_BE (tag_data + 4) != 16)
        goto error;

      descriptor->n_sub_descriptors = len;
      descriptor->sub_descriptors_uids = g_new0 (MXFUL, len);
      for (i = 0; i < len; i++) {
        memcpy (&descriptor->sub_descriptors_uids[i], tag_data + 8 + i * 16,
            16);
        GST_DEBUG ("  sub descriptor %u = %s", i,
            mxf_ul_to_string (&descriptor->sub_descriptors_uids[i], str));
      }
      ret = TRUE;
      break;
    }
    default:
      ret =
          mxf_metadata_file_descriptor_handle_tag (d, primer, tag, tag_data,
          tag_size);
      break;
  }

  return ret;

error:
  GST_ERROR ("Invalid multiple descriptor tag 0x%04x of size %u", tag,
      tag_size);

  return TRUE;
}

void mxf_metadata_multiple_descriptor_reset
    (MXFMetadataMultipleDescriptor * descriptor)
{
  g_return_if_fail (descriptor != NULL);

  g_free (descriptor->sub_descriptors_uids);
  g_free (descriptor->sub_descriptors);

  mxf_metadata_file_descriptor_reset ((MXFMetadataFileDescriptor *) descriptor);

  MXF_METADATA_DESCRIPTOR_CLEAR (descriptor, MXFMetadataMultipleDescriptor,
      MXFMetadataFileDescriptor);
}

gboolean
mxf_metadata_locator_parse (const MXFUL * key,
    MXFMetadataLocator * locator, const MXFPrimerPack * primer,
    guint16 type, const guint8 * data, guint size)
{
  guint16 tag, tag_size;
  const guint8 *tag_data;
  gchar str[48];

  g_return_val_if_fail (data != NULL, FALSE);

  memset (locator, 0, sizeof (MXFMetadataLocator));

  locator->type = type;

  GST_DEBUG ("Parsing locator of type 0x%04x:", type);

  while (mxf_local_tag_parse (data, size, &tag, &tag_size, &tag_data)) {
    if (tag_size == 0 || tag == 0x0000)
      goto next;

    switch (tag) {
      case 0x3c0a:
        if (tag_size != 16)
          goto error;
        memcpy (&locator->instance_uid, tag_data, 16);
        GST_DEBUG ("  instance uid = %s",
            mxf_ul_to_string (&locator->instance_uid, str));
        break;
      case 0x0102:
        if (tag_size != 16)
          goto error;
        memcpy (&locator->generation_uid, tag_data, 16);
        GST_DEBUG ("  generation uid = %s",
            mxf_ul_to_string (&locator->generation_uid, str));
        break;
      case 0x4101:
        if (type != MXF_METADATA_TEXT_LOCATOR
            && type != MXF_METADATA_NETWORK_LOCATOR)
          goto DFLT;
        locator->location = mxf_utf16_to_utf8 (tag_data, tag_size);
        GST_DEBUG ("  location = %s", GST_STR_NULL (locator->location));
        break;
      DFLT:
      default:
        if (!gst_metadata_add_custom_tag (primer, tag, tag_data, tag_size,
                &locator->other_tags))
          goto error;
        break;
    }

  next:
    data += 4 + tag_size;
    size -= 4 + tag_size;
  }

  return TRUE;

error:
  GST_ERROR ("Invalid locator");
  mxf_metadata_locator_reset (locator);

  return FALSE;
}

void
mxf_metadata_locator_reset (MXFMetadataLocator * locator)
{
  g_return_if_fail (locator != NULL);

  if (locator->location)
    g_free (locator->location);

  if (locator->other_tags)
    g_hash_table_destroy (locator->other_tags);

  memset (locator, 0, sizeof (MXFMetadataLocator));
}
