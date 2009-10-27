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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

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

  tags = gst_tag_list_new ();

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
      tmp = g_convert_with_fallback (str, -1, "UTF-8", encoding, "*",
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

  if (gst_structure_n_fields ((GstStructure *) tags) > 0)
    return tags;

  gst_tag_list_free (tags);
  return NULL;
}

GstBuffer *
gst_rm_utils_descramble_dnet_buffer (GstBuffer * buf)
{
  guint8 *data, *end, tmp;

  buf = gst_buffer_make_writable (buf);

  /* dnet = byte-order swapped AC3 */
  data = GST_BUFFER_DATA (buf);
  end = GST_BUFFER_DATA (buf) + GST_BUFFER_SIZE (buf);
  while ((data + 1) < end) {
    /* byte-swap */
    tmp = data[0];
    data[0] = data[1];
    data[1] = tmp;
    data += sizeof (guint16);
  }
  return buf;
}
