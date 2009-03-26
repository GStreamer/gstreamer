/* GStreamer
 * Copyright (C) 2009 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

#include "mxfwrite.h"
#include "mxful.h"

GST_DEBUG_CATEGORY_EXTERN (mxf_debug);
#define GST_CAT_DEFAULT mxf_debug

static GList *_essence_element_writer_registry = NULL;
static GPtrArray *_essence_element_writer_pad_templates = NULL;

void
mxf_essence_element_writer_register (const MXFEssenceElementWriter * writer)
{
  _essence_element_writer_registry =
      g_list_prepend (_essence_element_writer_registry, (gpointer) writer);

  if (!_essence_element_writer_pad_templates)
    _essence_element_writer_pad_templates = g_ptr_array_new ();

  if (_essence_element_writer_pad_templates->len > 0 &&
      g_ptr_array_index (_essence_element_writer_pad_templates,
          _essence_element_writer_pad_templates->len - 1) == NULL)
    g_ptr_array_remove_index (_essence_element_writer_pad_templates,
        _essence_element_writer_pad_templates->len - 1);

  g_ptr_array_add (_essence_element_writer_pad_templates,
      (gpointer) writer->pad_template);
}

const GstPadTemplate **
mxf_essence_element_writer_get_pad_templates (void)
{
  if (!_essence_element_writer_pad_templates
      || _essence_element_writer_pad_templates->len == 0)
    return NULL;

  if (g_ptr_array_index (_essence_element_writer_pad_templates,
          _essence_element_writer_pad_templates->len - 1))
    g_ptr_array_add (_essence_element_writer_pad_templates, NULL);

  return (const GstPadTemplate **) _essence_element_writer_pad_templates->pdata;
}

const MXFEssenceElementWriter *
mxf_essence_element_writer_find (const GstPadTemplate * templ)
{
  GList *l = _essence_element_writer_registry;

  for (; l; l = l->next) {
    MXFEssenceElementWriter *writer = l->data;

    if (writer->pad_template == templ)
      return writer;
  }

  return NULL;
}

void
mxf_ul_set (MXFUL * ul, GHashTable * hashtable)
{
  guint i;

next_try:
  for (i = 0; i < 4; i++)
    GST_WRITE_UINT32_BE (&ul->u[i * 4], g_random_int ());

  if (hashtable && g_hash_table_lookup_extended (hashtable, ul, NULL, NULL))
    goto next_try;
}

void
mxf_umid_set (MXFUMID * umid)
{
  guint i;
  guint32 tmp;

  /* SMPTE S330M 5.1.1:
   *    UMID Identifier
   */
  umid->u[0] = 0x06;
  umid->u[1] = 0x0a;
  umid->u[2] = 0x2b;
  umid->u[3] = 0x34;
  umid->u[4] = 0x01;
  umid->u[5] = 0x01;
  umid->u[6] = 0x01;
  umid->u[7] = 0x05;            /* version, see RP210 */
  umid->u[8] = 0x01;
  umid->u[9] = 0x01;
  umid->u[10] = 0x0d;           /* mixed group of components in a single container */

  /* - UUID/UL method for material number
   * - 24 bit PRG for instance number
   */
  umid->u[11] = 0x20 | 0x02;

  /* Length of remaining data */
  umid->u[12] = 0x13;

  /* Instance number */
  tmp = g_random_int ();
  umid->u[13] = (tmp >> 24) & 0xff;
  umid->u[14] = (tmp >> 16) & 0xff;
  umid->u[15] = (tmp >> 8) & 0xff;

  /* Material number: ISO UUID Version 4 */
  for (i = 16; i < 32; i += 4)
    GST_WRITE_UINT32_BE (&umid->u[i], g_random_int ());

  umid->u[16 + 6] &= 0x0f;
  umid->u[16 + 6] |= 0x40;

  umid->u[16 + 8] &= 0x3f;
  umid->u[16 + 8] |= 0x80;
}

void
mxf_timestamp_set_now (MXFTimestamp * timestamp)
{
  GTimeVal tv;
  time_t t;
  struct tm *tm;

#ifdef HAVE_GMTIME_R
  struct tm tm_;
#endif

  g_get_current_time (&tv);
  t = (time_t) tv.tv_sec;

#ifdef HAVE_GMTIME_R
  tm = gmtime_r (&t, &tm_);
#else
  tm = gmtime (&t);
#endif

  timestamp->year = tm->tm_year + 1900;
  timestamp->month = tm->tm_mon;
  timestamp->day = tm->tm_mday;
  timestamp->hour = tm->tm_hour;
  timestamp->minute = tm->tm_min;
  timestamp->second = tm->tm_sec;
  timestamp->msecond = tv.tv_usec / 1000;
}

static guint8 mxf_op_identification_ul[] = {
  0x06, 0x0e, 0x2b, 0x34, 0x04, 0x01, 0x01, 0x01, 0x0d, 0x01, 0x02, 0x01
};

void
mxf_op_set_atom (MXFUL * ul, gboolean single_sourceclip,
    gboolean single_essence_track)
{
  memcpy (&ul->u, &mxf_op_identification_ul, 12);
  ul->u[12] = 0x10;
  ul->u[13] = 0;

  if (!single_sourceclip)
    ul->u[13] |= 0x80;

  if (!single_essence_track)
    ul->u[13] |= 0x40;

  ul->u[14] = 0;
  ul->u[15] = 0;
}

void
mxf_op_set_generalized (MXFUL * ul, MXFOperationalPattern pattern,
    gboolean internal_essence, gboolean streamable, gboolean single_track)
{
  g_return_if_fail (pattern >= MXF_OP_1a);

  memcpy (&ul->u, &mxf_op_identification_ul, 12);

  if (pattern == MXF_OP_1a || pattern == MXF_OP_1b || pattern == MXF_OP_1c)
    ul->u[12] = 0x01;
  else if (pattern == MXF_OP_2a || pattern == MXF_OP_2b || pattern == MXF_OP_2c)
    ul->u[12] = 0x02;
  else if (pattern == MXF_OP_3a || pattern == MXF_OP_3b || pattern == MXF_OP_3c)
    ul->u[12] = 0x03;

  if (pattern == MXF_OP_1a || pattern == MXF_OP_2a || pattern == MXF_OP_3a)
    ul->u[13] = 0x01;
  else if (pattern == MXF_OP_1b || pattern == MXF_OP_2b || pattern == MXF_OP_3b)
    ul->u[13] = 0x02;
  else if (pattern == MXF_OP_1c || pattern == MXF_OP_2c || pattern == MXF_OP_3c)
    ul->u[13] = 0x02;

  ul->u[14] = 0x80;
  if (!internal_essence)
    ul->u[14] |= 0x40;
  if (!streamable)
    ul->u[14] |= 0x20;
  if (!single_track)
    ul->u[14] |= 0x10;

  ul->u[15] = 0;
}

static void
_mxf_mapping_ul_free (MXFUL * ul)
{
  g_slice_free (MXFUL, ul);
}

guint16
mxf_primer_pack_add_mapping (MXFPrimerPack * primer, guint16 local_tag,
    const MXFUL * ul)
{
  MXFUL *uid;
#ifndef GST_DISABLE_GST_DEBUG
  gchar str[48];
#endif

  if (primer->mappings == NULL) {
    primer->mappings = g_hash_table_new_full (g_direct_hash, g_direct_equal,
        (GDestroyNotify) NULL, (GDestroyNotify) _mxf_mapping_ul_free);
  }

  if (primer->reverse_mappings == NULL) {
    primer->reverse_mappings = g_hash_table_new_full ((GHashFunc) mxf_ul_hash,
        (GEqualFunc) mxf_ul_is_equal, (GDestroyNotify) _mxf_mapping_ul_free,
        (GDestroyNotify) NULL);
  }

  if (primer->next_free_tag == 0xffff && local_tag == 0) {
    GST_ERROR ("Used too many dynamic tags");
    return 0;
  }

  if (local_tag == 0) {
    guint16 tmp;

    tmp = GPOINTER_TO_UINT (g_hash_table_lookup (primer->reverse_mappings, ul));
    if (tmp == 0) {
      local_tag = primer->next_free_tag;
      primer->next_free_tag++;
    }
  } else {
    if (g_hash_table_lookup (primer->mappings, GUINT_TO_POINTER (local_tag)))
      return local_tag;
  }

  g_assert (local_tag != 0);

  uid = g_slice_new (MXFUL);
  memcpy (uid, ul, 16);

  GST_DEBUG ("Adding mapping = 0x%04x -> %s", local_tag,
      mxf_ul_to_string (uid, str));
  g_hash_table_insert (primer->mappings, GUINT_TO_POINTER (local_tag), uid);
  uid = g_slice_dup (MXFUL, uid);
  g_hash_table_insert (primer->reverse_mappings, uid,
      GUINT_TO_POINTER (local_tag));

  return local_tag;
}

guint
mxf_ber_encode_size (guint size, guint8 ber[9])
{
  guint8 slen, i;
  guint8 tmp[8];

  memset (ber, 0, 9);

  if (size <= 127) {
    ber[0] = size;
    return 1;
  } else if (size > G_MAXUINT) {
    return 0;
  }

  slen = 0;
  while (size > 0) {
    tmp[slen] = size & 0xff;
    size >>= 8;
    slen++;
  }

  ber[0] = 0x80 | slen;
  for (i = 0; i < slen; i++) {
    ber[i + 1] = tmp[slen - i - 1];
  }

  return slen + 1;
}

void
mxf_timestamp_write (const MXFTimestamp * timestamp, guint8 * data)
{
  GST_WRITE_UINT16_BE (data, timestamp->year);
  GST_WRITE_UINT8 (data + 2, timestamp->month);
  GST_WRITE_UINT8 (data + 3, timestamp->day);
  GST_WRITE_UINT8 (data + 4, timestamp->hour);
  GST_WRITE_UINT8 (data + 5, timestamp->minute);
  GST_WRITE_UINT8 (data + 6, timestamp->second);
  GST_WRITE_UINT8 (data + 7, (timestamp->msecond * 256) / 1000);
}

guint8 *
mxf_utf8_to_utf16 (const gchar * str, guint16 * size)
{
  guint8 *ret;
  GError *error = NULL;
  gsize s;

  g_return_val_if_fail (size != NULL, NULL);

  if (str == NULL) {
    *size = 0;
    return NULL;
  }

  ret = (guint8 *)
      g_convert_with_fallback (str, -1, "UTF-16BE", "UTF-8", "*", NULL, &s,
      &error);

  if (ret == NULL) {
    GST_WARNING ("UTF-16-BE to UTF-8 conversion failed: %s", error->message);
    g_error_free (error);
    *size = 0;
    return NULL;
  }

  *size = s;
  return (guint8 *) ret;
}

void
mxf_product_version_write (const MXFProductVersion * version, guint8 * data)
{
  GST_WRITE_UINT16_BE (data, version->major);
  GST_WRITE_UINT16_BE (data + 2, version->minor);
  GST_WRITE_UINT16_BE (data + 4, version->patch);
  GST_WRITE_UINT16_BE (data + 6, version->build);
  GST_WRITE_UINT16_BE (data + 8, version->release);
}

GstBuffer *
mxf_partition_pack_to_buffer (const MXFPartitionPack * pack)
{
  guint slen;
  guint8 ber[9];
  GstBuffer *ret;
  guint8 *data;
  guint i;
  guint size =
      8 + 16 * pack->n_essence_containers + 16 + 4 + 8 + 4 + 8 + 8 + 8 + 8 + 8 +
      4 + 2 + 2;

  slen = mxf_ber_encode_size (size, ber);

  ret = gst_buffer_new_and_alloc (16 + slen + size);
  memcpy (GST_BUFFER_DATA (ret), MXF_UL (PARTITION_PACK), 13);
  if (pack->type == MXF_PARTITION_PACK_HEADER)
    GST_BUFFER_DATA (ret)[13] = 0x02;
  else if (pack->type == MXF_PARTITION_PACK_BODY)
    GST_BUFFER_DATA (ret)[13] = 0x03;
  else if (pack->type == MXF_PARTITION_PACK_FOOTER)
    GST_BUFFER_DATA (ret)[13] = 0x04;
  GST_BUFFER_DATA (ret)[14] = 0;
  if (pack->complete)
    GST_BUFFER_DATA (ret)[14] |= 0x02;
  if (pack->closed)
    GST_BUFFER_DATA (ret)[14] |= 0x01;
  GST_BUFFER_DATA (ret)[14] += 1;
  GST_BUFFER_DATA (ret)[15] = 0;
  memcpy (GST_BUFFER_DATA (ret) + 16, &ber, slen);

  data = GST_BUFFER_DATA (ret) + 16 + slen;

  GST_WRITE_UINT16_BE (data, pack->major_version);
  GST_WRITE_UINT16_BE (data + 2, pack->minor_version);
  data += 4;

  GST_WRITE_UINT32_BE (data, pack->kag_size);
  data += 4;

  GST_WRITE_UINT64_BE (data, pack->this_partition);
  data += 8;

  GST_WRITE_UINT64_BE (data, pack->prev_partition);
  data += 8;

  GST_WRITE_UINT64_BE (data, pack->footer_partition);
  data += 8;

  GST_WRITE_UINT64_BE (data, pack->header_byte_count);
  data += 8;

  GST_WRITE_UINT64_BE (data, pack->index_byte_count);
  data += 8;

  GST_WRITE_UINT32_BE (data, pack->index_sid);
  data += 4;

  GST_WRITE_UINT64_BE (data, pack->body_offset);
  data += 8;

  GST_WRITE_UINT32_BE (data, pack->body_sid);
  data += 4;

  memcpy (data, &pack->operational_pattern, 16);
  data += 16;

  GST_WRITE_UINT32_BE (data, pack->n_essence_containers);
  GST_WRITE_UINT32_BE (data + 4, 16);
  data += 8;

  for (i = 0; i < pack->n_essence_containers; i++)
    memcpy (data + 16 * i, &pack->essence_containers[i], 16);

  return ret;
}

GstBuffer *
mxf_primer_pack_to_buffer (const MXFPrimerPack * pack)
{
  guint slen;
  guint8 ber[9];
  GstBuffer *ret;
  guint n;
  guint8 *data;

  if (pack->mappings)
    n = g_hash_table_size (pack->mappings);
  else
    n = 0;

  slen = mxf_ber_encode_size (8 + 18 * n, ber);

  ret = gst_buffer_new_and_alloc (16 + slen + 8 + 18 * n);
  memcpy (GST_BUFFER_DATA (ret), MXF_UL (PRIMER_PACK), 16);
  memcpy (GST_BUFFER_DATA (ret) + 16, &ber, slen);

  data = GST_BUFFER_DATA (ret) + 16 + slen;

  GST_WRITE_UINT32_BE (data, n);
  GST_WRITE_UINT32_BE (data + 4, 18);
  data += 8;

  if (pack->mappings) {
    guint16 local_tag;
    MXFUL *ul;
#if GLIB_CHECK_VERSION (2, 16, 0)
    GHashTableIter iter;

    g_hash_table_iter_init (&iter, pack->mappings);
#else
    GList *l, *values;

    keys = g_hash_table_get_keys (pack->mappings);
#endif

#if GLIB_CHECK_VERSION (2, 16, 0)
    while (g_hash_table_iter_next (&iter, (gpointer) & local_tag,
            (gpointer) & ul)) {
#else
    for (l = keys l; l = l->next) {
      local_tag = GPOINTER_TO_GUINT (l->data);
      ul = g_hash_table_lookup (pack->mappings, GUINT_TO_POINTER (local_tag));
#endif
      GST_WRITE_UINT16_BE (data, local_tag);
      memcpy (data + 2, ul, 16);
      data += 18;
    }

#if !GLIB_CHECK_VERSION (2, 16, 0)
    g_list_free (keys);
#endif
  }

  return ret;
}

GstBuffer *
mxf_fill_new (guint size)
{
  GstBuffer *ret;
  guint slen;
  guint8 ber[9];

  slen = mxf_ber_encode_size (size, ber);

  ret = gst_buffer_new_and_alloc (16 + slen + size);
  memcpy (GST_BUFFER_DATA (ret), MXF_UL (FILL), 16);
  memcpy (GST_BUFFER_DATA (ret) + 16, &ber, slen);
  memset (GST_BUFFER_DATA (ret) + slen, 0, size);

  return ret;
}

GstBuffer *
mxf_random_index_pack_to_buffer (const GArray * array)
{
  MXFRandomIndexPackEntry *entry;
  guint i;
  GstBuffer *ret;
  guint8 slen, ber[9];
  guint size;
  guint8 *data;

  if (array->len == 0)
    return NULL;

  size = array->len * 12 + 4;
  slen = mxf_ber_encode_size (size, ber);
  ret = gst_buffer_new_and_alloc (16 + slen + size);
  memcpy (GST_BUFFER_DATA (ret), MXF_UL (RANDOM_INDEX_PACK), 16);
  memcpy (GST_BUFFER_DATA (ret) + 16, ber, slen);

  data = GST_BUFFER_DATA (ret) + 16 + slen;

  for (i = 0; i < array->len; i++) {
    entry = &g_array_index (array, MXFRandomIndexPackEntry, i);
    GST_WRITE_UINT32_BE (data, entry->body_sid);
    GST_WRITE_UINT64_BE (data + 4, entry->offset);
    data += 12;
  }
  GST_WRITE_UINT32_BE (data, GST_BUFFER_SIZE (ret));

  return ret;
}
