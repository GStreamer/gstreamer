/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

#include <gst/gst.h>

static void
lookup (GstIndex * index, GstIndexLookupMethod method,
    GstFormat src_format, gint64 src_value,
    GstFormat dest_format, gint64 expecting)
{
  GstIndexEntry *entry;
  gint64 result;

  entry = gst_index_get_assoc_entry (index, 0, method, 0,
      src_format, src_value);
  if (entry) {
    gst_index_entry_assoc_map (entry, dest_format, &result);

    if (result == expecting) {
      g_print ("OK (%lld)\n", result);
    } else {
      g_print ("FAIL - expecting %lld, got %lld\n", expecting, result);
    }
  } else {
    const GstFormatDefinition *def = gst_format_get_details (src_format);

    if (expecting == -1)
      g_print ("OK (not found)\n");
    else
      g_print ("FAIL - no index entry found for %lld %s, expecting %lld\n",
	  src_value, def->nick, expecting);
  }
}

typedef struct _GstIndexTestCase
{
  GstIndexLookupMethod method;
  GstFormat src_format;
  gint64 src_value;
  GstFormat dest_format;
  gint64 expecting;
} GstIndexTestCase;

const static GstIndexTestCase cases[] = {
  {GST_INDEX_LOOKUP_EXACT, GST_FORMAT_BYTES, 3, GST_FORMAT_TIME, 3000},
  {GST_INDEX_LOOKUP_EXACT, GST_FORMAT_TIME, 5000, GST_FORMAT_BYTES, 5},
  {GST_INDEX_LOOKUP_EXACT, GST_FORMAT_TIME, 5010, GST_FORMAT_BYTES, -1},
  {GST_INDEX_LOOKUP_BEFORE, GST_FORMAT_TIME, 5010, GST_FORMAT_BYTES, 5},
  {GST_INDEX_LOOKUP_AFTER, GST_FORMAT_TIME, 5010, GST_FORMAT_BYTES, 6},
  {GST_INDEX_LOOKUP_BEFORE, GST_FORMAT_TIME, 0, GST_FORMAT_BYTES, 0},
  {GST_INDEX_LOOKUP_AFTER, GST_FORMAT_TIME, G_MAXINT64, GST_FORMAT_BYTES, -1},
  {GST_INDEX_LOOKUP_AFTER, GST_FORMAT_TIME, 0, GST_FORMAT_BYTES, 0},
  {GST_INDEX_LOOKUP_BEFORE, GST_FORMAT_TIME, -1, GST_FORMAT_BYTES, -1},
  {GST_INDEX_LOOKUP_BEFORE, GST_FORMAT_TIME, G_MAXINT64, GST_FORMAT_BYTES,
	99999},
  {GST_INDEX_LOOKUP_AFTER, GST_FORMAT_TIME, G_MAXINT64, GST_FORMAT_BYTES, -1},
};

gint
main (gint argc, gchar * argv[])
{
  GstIndex *index;
  GstElement *element;
  gint i, id;

  gst_init (&argc, &argv);

  if (argc != 2) {
    g_print ("usage: cache1 (memindex | fileindex)\n");
    exit (0);
  }

  index = gst_index_factory_make (argv[1]);
  g_assert (index != NULL);

  element = gst_element_factory_make ("identity", "element");
  g_assert (element != NULL);

  gst_index_get_writer_id (index, GST_OBJECT (element), &id);

  g_print ("Building index...\n");

  for (i = 0; i < 100000; i++) {
    gst_index_add_association (index, 0, 0, GST_FORMAT_BYTES, (gint64) i,
	GST_FORMAT_TIME, (gint64) (i * 1000), 0);
  }

  g_print ("Testing index...\n");

  for (i = 0; i < (sizeof (cases) / sizeof (GstIndexTestCase)); i++) {
    lookup (index, cases[i].method, cases[i].src_format, cases[i].src_value,
	cases[i].dest_format, cases[i].expecting);
  }

  return 0;
}
