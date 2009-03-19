/* GStreamer
 * Copyright (C) <2009> Sebastian Dröge <sebastian.droege@collabora.co.uk>
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
#include <config.h>
#endif

#include <string.h>

#include "mxful.h"

const MXFUL _mxf_ul_table[] = {
  /* SMPTE */
  {{0x06, 0x0e, 0x2b, 0x34, 0x00,}},
  /* FILL, SMPTE 336M */
  {{0x06, 0x0e, 0x2b, 0x34, 0x01, 0x01, 0x01, 0x01,
          0x03, 0x01, 0x02, 0x10, 0x01, 0x00,}},
  /* PARTITION_PACK, SMPTE 377M 6.1 */
  {{0x06, 0x0e, 0x2b, 0x34, 0x02, 0x05, 0x01, 0x01,
          0x0d, 0x01, 0x02, 0x01, 0x01, 0x00,}},
  /* PRIMER_PACK, SMPTE 377M 8.1 */
  {{0x06, 0x0e, 0x2b, 0x34, 0x02, 0x05, 0x01, 0x01,
          0x0d, 0x01, 0x02, 0x01, 0x01, 0x05, 0x01, 0x00}},
  /* METADATA, SMPTE 377M 8.6 */
  {{0x06, 0x0e, 0x2b, 0x34, 0x02, 0x53, 0x01, 0x01,
          0x0d, 0x01, 0x01, 0x01, 0x01, 0x00,}},
  /* DESCRIPTIVE_METADATA, SMPTE 377M 8.7.3 */
  {{0x06, 0x0e, 0x2b, 0x34, 0x02, 0x00, 0x01, 0x01,
          0x0d, 0x01, 0x04, 0x01, 0x00,}},
  /* RANDOM_INDEX_PACK, SMPTE 377M 11.1 */
  {{0x06, 0x0e, 0x2b, 0x34, 0x02, 0x05, 0x01, 0x01,
          0x0d, 0x01, 0x02, 0x01, 0x01, 0x11, 0x01, 0x00}},
  /* INDEX_TABLE_SEGMENT, SMPTE 377M 10.2.2 */
  {{0x06, 0x0e, 0x2b, 0x34, 0x02, 0x53, 0x01, 0x01,
          0x0d, 0x01, 0x02, 0x01, 0x01, 0x10, 0x01, 0x00}},
  /* GENERIC_CONTAINER_SYSTEM_ITEM, SMPTE 379M 6.2.1 */
  {{0x06, 0x0e, 0x2b, 0x34, 0x02, 0x00, 0x01, 0x00,
          0x0d, 0x01, 0x03, 0x01, 0x00}},
  /* GENERIC_CONTAINER_ESSENCE_ELEMENT, SMPTE 379M 7.1 */
  {{0x06, 0x0e, 0x2b, 0x34, 0x01, 0x02, 0x01, 0x00,
          0x0d, 0x01, 0x03, 0x01, 0x00,}},
  /* GENERIC_CONTAINER_ESSENCE_CONTAINER_LABEL, SMPTE 379M 8 */
  {{0x06, 0x0e, 0x2b, 0x34, 0x04, 0x01, 0x01, 0x00,
          0x0d, 0x01, 0x03, 0x01, 0x00,}},
  /* AVID_ESSENCE_CONTAINER_ESSENCE_ELEMENT, undocumented */
  {{0x06, 0x0e, 0x2b, 0x34, 0x01, 0x02, 0x01, 0x01,
          0x0e, 0x04, 0x03, 0x01, 0x00,}},
  /* AVID_ESSENCE_CONTAINER_ESSENCE_LABEL, undocumented */
  {{0x06, 0x0e, 0x2b, 0x34, 0x01, 0x01, 0x01, 0xff,
          0x4b, 0x46, 0x41, 0x41, 0x00, 0x0d, 0x4d, 0x4f}},
};

gboolean
mxf_ul_is_equal (const MXFUL * a, const MXFUL * b)
{
  guint i;

  g_return_val_if_fail (a != NULL, FALSE);
  g_return_val_if_fail (b != NULL, FALSE);

  for (i = 0; i < 16; i++) {
    /* registry version */
    if (i == 7)
      continue;

    if (a->u[i] != b->u[i])
      return FALSE;
  }

  return TRUE;
}

gboolean
mxf_ul_is_subclass (const MXFUL * class, const MXFUL * subclass)
{
  guint i, j;

  g_return_val_if_fail (class != NULL, FALSE);
  g_return_val_if_fail (subclass != NULL, FALSE);

  for (i = 0; i < 16; i++) {
    if (i == 7)
      /* registry version */
      continue;

    if (class->u[i] == 0x00) {
      gboolean terminated = TRUE;

      for (j = i; j < 16; j++) {
        if (class->u[j] != 0x00) {
          terminated = FALSE;
          break;
        }
      }

      if (terminated)
        return TRUE;

      continue;
    }

    if (class->u[i] != subclass->u[i])
      return FALSE;
  }

  return TRUE;
}

gboolean
mxf_ul_is_zero (const MXFUL * ul)
{
  static const guint8 zero[16] = { 0, };

  g_return_val_if_fail (ul != NULL, FALSE);

  return (memcmp (ul, &zero, 16) == 0);
}

gboolean
mxf_ul_is_valid (const MXFUL * ul)
{
  guint i, j;

  g_return_val_if_fail (ul != NULL, FALSE);

  for (i = 0; i < 16; i++) {
    if (ul->u[i] == 0x00) {
      for (j = i; j < 16; j++) {
        if (ul->u[j] != 0x00)
          return FALSE;
      }

      return TRUE;
    }

    if (ul->u[i] > 0x7f)
      return FALSE;
  }

  return TRUE;
}

guint
mxf_ul_hash (const MXFUL * ul)
{
  guint32 ret = 0;
  guint i;

  g_return_val_if_fail (ul != NULL, 0);

  for (i = 0; i < 4; i++)
    ret ^= (ul->u[i * 4 + 0] << 24) |
        (ul->u[i * 4 + 1] << 16) |
        (ul->u[i * 4 + 2] << 8) | (ul->u[i * 4 + 3] << 0);

  return ret;
}

gchar *
mxf_ul_to_string (const MXFUL * ul, gchar str[48])
{
  gchar *ret = str;

  g_return_val_if_fail (ul != NULL, NULL);

  if (ret == NULL)
    ret = g_malloc (48);

  g_snprintf (ret, 48,
      "%02x.%02x.%02x.%02x."
      "%02x.%02x.%02x.%02x."
      "%02x.%02x.%02x.%02x."
      "%02x.%02x.%02x.%02x",
      ul->u[0], ul->u[1], ul->u[2], ul->u[3],
      ul->u[4], ul->u[5], ul->u[6], ul->u[7],
      ul->u[8], ul->u[9], ul->u[10], ul->u[11],
      ul->u[12], ul->u[13], ul->u[14], ul->u[15]);

  return ret;
}

MXFUL *
mxf_ul_from_string (const gchar * str, MXFUL * ul)
{
  MXFUL *ret = ul;
  gint len;
  guint i, j;

  g_return_val_if_fail (str != NULL, NULL);

  len = strlen (str);
  if (len != 47) {
    GST_ERROR ("Invalid UL string length %d, should be 47", len);
    return NULL;
  }

  if (ret == NULL)
    ret = g_new0 (MXFUL, 1);

  memset (ret, 0, 16);

  for (i = 0, j = 0; i < 16; i++) {
    if (!g_ascii_isxdigit (str[j]) ||
        !g_ascii_isxdigit (str[j + 1]) ||
        (str[j + 2] != '.' && str[j + 2] != '\0')) {
      GST_ERROR ("Invalid UL string '%s'", str);
      if (ul == NULL)
        g_free (ret);
      return NULL;
    }

    ret->u[i] = (g_ascii_xdigit_value (str[j]) << 4) |
        (g_ascii_xdigit_value (str[j + 1]));
    j += 3;
  }
  return ret;
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
