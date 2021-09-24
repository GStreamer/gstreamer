/* GStreamer RealMedia utility functions
 * Copyright (C) 2006 Tim-Philipp Müller <tim centricular net>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include "rmutils.h"

gchar *
gst_rm_utils_read_string8 (const guint8 * data, guint datalen,
    guint * p_total_len)
{
  gint length;

  if (p_total_len)
    *p_total_len = 0;

  if (datalen < 1)
    return NULL;

  length = GST_READ_UINT8 (data);
  if (datalen < (1 + length))
    return NULL;

  if (p_total_len)
    *p_total_len = 1 + length;

  return g_strndup ((gchar *) data + 1, length);
}

gchar *
gst_rm_utils_read_string16 (const guint8 * data, guint datalen,
    guint * p_total_len)
{
  gint length;

  if (p_total_len)
    *p_total_len = 0;

  if (datalen < 2)
    return NULL;

  length = GST_READ_UINT16_BE (data);
  if (datalen < (2 + length))
    return NULL;

  if (p_total_len)
    *p_total_len = 2 + length;

  return g_strndup ((gchar *) data + 2, length);
}

GstTagList *
gst_rm_utils_read_tags (const guint8 * data, guint datalen,
    GstRmUtilsStringReadFunc read_string_func)
{
  const gchar *gst_tags[] = { GST_TAG_TITLE, GST_TAG_ARTIST,
    GST_TAG_COPYRIGHT, GST_TAG_COMMENT
  };
  GstTagList *tags;
  guint i;

  g_assert (read_string_func != NULL);

  GST_DEBUG ("File Content : (CONT) len = %d", datalen);

  tags = gst_tag_list_new_empty ();

  for (i = 0; i < G_N_ELEMENTS (gst_tags); ++i) {
    gchar *str = NULL;
    guint total_length = 0;

    str = read_string_func (data, datalen, &total_length);
    data += total_length;
    datalen -= total_length;

    if (str != NULL && !g_utf8_validate (str, -1, NULL)) {
      const gchar *encoding;
      gchar *tmp;

      encoding = g_getenv ("GST_TAG_ENCODING");
      if (encoding == NULL || *encoding == '\0') {
        if (g_get_charset (&encoding))
          encoding = "ISO-8859-15";
      }
      GST_DEBUG ("converting tag from %s to UTF-8", encoding);
      tmp = g_convert_with_fallback (str, -1, "UTF-8", encoding, (gchar *) "*",
          NULL, NULL, NULL);
      g_free (str);
      str = tmp;
    }

    GST_DEBUG ("%s = %s", gst_tags[i], GST_STR_NULL (str));
    if (str != NULL && *str != '\0') {
      gst_tag_list_add (tags, GST_TAG_MERGE_APPEND, gst_tags[i], str, NULL);
    }
    g_free (str);
  }

  if (gst_tag_list_n_tags (tags) > 0)
    return tags;

  gst_tag_list_unref (tags);
  return NULL;
}

GstBuffer *
gst_rm_utils_descramble_dnet_buffer (GstBuffer * buf)
{
  GstMapInfo map;
  guint8 *data, *end, tmp;

  buf = gst_buffer_make_writable (buf);

  /* dnet = byte-order swapped AC3 */
  gst_buffer_map (buf, &map, GST_MAP_READWRITE);
  data = map.data;
  end = data + map.size;
  while ((data + 1) < end) {
    /* byte-swap */
    tmp = data[0];
    data[0] = data[1];
    data[1] = tmp;
    data += sizeof (guint16);
  }
  gst_buffer_unmap (buf, &map);
  return buf;
}

static void
gst_rm_utils_swap_nibbles (guint8 * data, gint idx1, gint idx2, gint len)
{
  guint8 *d1, *d2, tmp1 = 0, tmp2, tmp1n, tmp2n;

  if ((idx2 & 1) && !(idx1 & 1)) {
    /* align destination to a byte by swapping the indexes */
    tmp1 = idx1;
    idx1 = idx2;
    idx2 = tmp1;
  }
  d1 = data + (idx1 >> 1);
  d2 = data + (idx2 >> 1);

  /* check if we have aligned offsets and we can copy bytes */
  if ((idx1 & 1) == (idx2 & 1)) {
    if (idx1 & 1) {
      /* swap first nibble */
      tmp1 = *d1;
      tmp2 = *d2;
      *d1++ = (tmp2 & 0xf0) | (tmp1 & 0x0f);
      *d2++ = (tmp1 & 0xf0) | (tmp2 & 0x0f);
      len--;
    }
    for (; len > 1; len -= 2) {
      /* swap 2 nibbles */
      tmp1 = *d1;
      *d1++ = *d2;
      *d2++ = tmp1;
    }
    if (len) {
      /* swap leftover nibble */
      tmp1 = *d1;
      tmp2 = *d2;
      *d1 = (tmp2 & 0x0f) | (tmp1 & 0xf0);
      *d2 = (tmp1 & 0x0f) | (tmp2 & 0xf0);
    }
  } else {
    /* preload nibbles from source */
    tmp2n = *d1;
    tmp2 = *d2;

    for (; len > 1; len -= 2) {
      /* assemble nibbles */
      *d1++ = (tmp2n & 0x0f) | (tmp2 << 4);
      tmp1n = *d1;
      *d2++ = (tmp1n << 4) | (tmp1 >> 4);

      tmp1 = tmp1n;
      tmp2n = (tmp2 >> 4);
      tmp2 = *d2;
    }
    if (len) {
      /* last leftover */
      *d1 = (tmp2 << 4) | (tmp2n & 0x0f);
      *d2 = (tmp1 >> 4) | (tmp2 & 0xf0);
    } else {
      *d1 = (tmp1 & 0xf0) | (tmp2n);
    }
  }
}

static const gint sipr_swap_index[38][2] = {
  {0, 63}, {1, 22}, {2, 44}, {3, 90},
  {5, 81}, {7, 31}, {8, 86}, {9, 58},
  {10, 36}, {12, 68}, {13, 39}, {14, 73},
  {15, 53}, {16, 69}, {17, 57}, {19, 88},
  {20, 34}, {21, 71}, {24, 46}, {25, 94},
  {26, 54}, {28, 75}, {29, 50}, {32, 70},
  {33, 92}, {35, 74}, {38, 85}, {40, 56},
  {42, 87}, {43, 65}, {45, 59}, {48, 79},
  {49, 93}, {51, 89}, {55, 95}, {61, 76},
  {67, 83}, {77, 80}
};

GstBuffer *
gst_rm_utils_descramble_sipr_buffer (GstBuffer * buf)
{
  GstMapInfo map;
  gint n, bs;
  gsize size;

  size = gst_buffer_get_size (buf);

  /* split the packet in 96 blocks of nibbles */
  bs = size * 2 / 96;
  if (bs == 0)
    return buf;

  buf = gst_buffer_make_writable (buf);

  gst_buffer_map (buf, &map, GST_MAP_WRITE);

  /* we need to perform 38 swaps on the blocks */
  for (n = 0; n < 38; n++) {
    gint idx1, idx2;

    /* get the indexes of the blocks of nibbles that need swapping */
    idx1 = bs * sipr_swap_index[n][0];
    idx2 = bs * sipr_swap_index[n][1];

    /* swap the blocks */
    gst_rm_utils_swap_nibbles (map.data, idx1, idx2, bs);
  }
  gst_buffer_unmap (buf, &map);

  return buf;
}

void
gst_rm_utils_run_tests (void)
{
#if 0
  guint8 tab1[] = { 0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe };
  guint8 tab2[8];

  memcpy (tab2, tab1, 8);
  gst_util_dump_mem (tab2, 8);

  gst_rm_utils_swap_nibbles (tab2, 0, 8, 4);
  gst_util_dump_mem (tab2, 8);
  memcpy (tab2, tab1, 8);
  gst_rm_utils_swap_nibbles (tab2, 0, 8, 5);
  gst_util_dump_mem (tab2, 8);

  memcpy (tab2, tab1, 8);
  gst_rm_utils_swap_nibbles (tab2, 1, 8, 4);
  gst_util_dump_mem (tab2, 8);
  memcpy (tab2, tab1, 8);
  gst_rm_utils_swap_nibbles (tab2, 1, 8, 5);
  gst_util_dump_mem (tab2, 8);

  memcpy (tab2, tab1, 8);
  gst_rm_utils_swap_nibbles (tab2, 0, 9, 4);
  gst_util_dump_mem (tab2, 8);
  memcpy (tab2, tab1, 8);
  gst_rm_utils_swap_nibbles (tab2, 0, 9, 5);
  gst_util_dump_mem (tab2, 8);

  memcpy (tab2, tab1, 8);
  gst_rm_utils_swap_nibbles (tab2, 1, 9, 4);
  gst_util_dump_mem (tab2, 8);
  memcpy (tab2, tab1, 8);
  gst_rm_utils_swap_nibbles (tab2, 1, 9, 5);
  gst_util_dump_mem (tab2, 8);
#endif
}
