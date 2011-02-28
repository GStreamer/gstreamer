/* 
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
 *
 * The Original Code is Fluendo MPEG Demuxer plugin.
 *
 * The Initial Developer of the Original Code is Fluendo, S.L.
 * Portions created by Fluendo, S.L. are Copyright (C) 2005
 * Fluendo, S.L. All Rights Reserved.
 *
 * Contributor(s): Wim Taymans <wim@fluendo.com>
 *
 */

#include <string.h>

#include <gst/gst.h>

#include "gstmpegdesc.h"

GST_DEBUG_CATEGORY (gstmpegtsdesc_debug);
#define GST_CAT_DEFAULT (gstmpegtsdesc_debug)

void
gst_mpeg_descriptor_free (GstMPEGDescriptor * desc)
{
  g_return_if_fail (desc != NULL);

  g_free (desc);
}

static guint
gst_mpeg_descriptor_parse_1 (guint8 * data, guint size)
{
  guint8 tag;
  guint8 length;

  /* need at least 2 bytes for tag and length */
  if (size < 2)
    return 0;

  tag = *data++;
  length = *data++;
  size -= 2;

  GST_DEBUG ("tag: 0x%02x, length: %d", tag, length);

  if (length > size)
    return 0;

  GST_MEMDUMP ("tag contents:", data, length);

  return length + 2;
}

GstMPEGDescriptor *
gst_mpeg_descriptor_parse (guint8 * data, guint size)
{
  guint8 *current;
  guint consumed, total, n_desc;
  GstMPEGDescriptor *result;

  g_return_val_if_fail (data != NULL, NULL);

  current = data;
  total = 0;
  n_desc = 0;

  do {
    consumed = gst_mpeg_descriptor_parse_1 (current, size);

    if (consumed > 0) {
      current += consumed;
      total += consumed;
      size -= consumed;
      n_desc++;
    }
  }
  while (consumed > 0);

  GST_DEBUG ("parsed %d descriptors", n_desc);

  if (total == 0)
    return NULL;

  result = g_malloc (sizeof (GstMPEGDescriptor) + total);
  result->n_desc = n_desc;
  result->data_length = total;
  result->data = ((guint8 *) result) + sizeof (GstMPEGDescriptor);

  memcpy (result->data, data, total);

  return result;
}

guint
gst_mpeg_descriptor_n_desc (GstMPEGDescriptor * desc)
{
  g_return_val_if_fail (desc != NULL, 0);

  return desc->n_desc;
}

guint8 *
gst_mpeg_descriptor_find (GstMPEGDescriptor * desc, gint tag)
{
  guint8 length;
  guint8 *current;
  guint size;

  g_return_val_if_fail (desc != NULL, NULL);

  current = desc->data;
  length = desc->data_length;

  while (length > 0) {
    if (DESC_TAG (current) == tag)
      return current;

    size = DESC_LENGTH (current) + 2;

    current += size;
    length -= size;
  }
  return NULL;
}

/* array needs freeing afterwards */
GArray *
gst_mpeg_descriptor_find_all (GstMPEGDescriptor * desc, gint tag)
{
  GArray *all;

  guint8 length;
  guint8 *current;
  guint size;

  g_return_val_if_fail (desc != NULL, NULL);
  all = g_array_new (TRUE, TRUE, sizeof (guint8 *));

  current = desc->data;
  length = desc->data_length;

  while (length > 0) {
    if (DESC_TAG (current) == tag)
      g_array_append_val (all, current);
    size = DESC_LENGTH (current) + 2;

    current += size;
    length -= size;
  }

  GST_DEBUG ("found tag 0x%02x %d times", tag, all->len);

  return all;
}

guint8 *
gst_mpeg_descriptor_nth (GstMPEGDescriptor * desc, guint i)
{
  guint8 length;
  guint8 *current;
  guint size;

  g_return_val_if_fail (desc != NULL, NULL);

  if (i > desc->n_desc)
    return NULL;

  current = desc->data;
  length = desc->data_length;

  while (length > 0) {
    if (i == 0)
      return current;

    size = DESC_LENGTH (current) + 2;

    current += size;
    length -= size;
    i--;

  }
  return NULL;
}

void
gst_mpegtsdesc_init_debug (void)
{
  GST_DEBUG_CATEGORY_INIT (gstmpegtsdesc_debug, "mpegtsdesc", 0,
      "MPEG transport stream parser (descriptor)");
}
