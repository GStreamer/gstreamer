/* GStreamer
 * Copyright (C) 2003 Erik Walthinsen <omega@cse.ogi.edu>
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
my_resolver (GstIndex * index, GstObject * _ign, gchar ** writer_string,
    gpointer user_data)
{
  *writer_string = user_data;
}

gint
main (gint argc, gchar * argv[])
{
  GstIndex *index;
  GstObject *identity;
  gint id;
  gint64 cur;

  gst_init (&argc, &argv);

  if (argc != 3) {
    g_print ("usage: dumpfileindex /path/to/fileindex writer_id\n");
    exit (0);
  }

  index = gst_index_factory_make ("fileindex");
  g_assert (index != NULL);

  g_object_set (index, "location", argv[1], NULL);
  gst_index_set_resolver (index, (GstIndexResolver) my_resolver, argv[2]);

  identity = (GstObject *) gst_element_factory_make ("identity", "element");
  g_assert (identity);
  gst_index_get_writer_id (index, identity, &id);

  cur = 0;
  while (1) {
    gint fx;
    GstIndexEntry *entry =
	gst_index_get_assoc_entry (index, id, GST_INDEX_LOOKUP_AFTER, 0,
	GST_FORMAT_TIME, cur);

    if (!entry)
      break;

    g_print ("%x", GST_INDEX_ASSOC_FLAGS (entry));
    for (fx = 0; fx < GST_INDEX_NASSOCS (entry); fx++) {
      GstFormat fmt = GST_INDEX_ASSOC_FORMAT (entry, fx);
      const GstFormatDefinition *def = gst_format_get_details (fmt);

      if (fmt == GST_FORMAT_TIME) {
	cur = GST_INDEX_ASSOC_VALUE (entry, fx) + 1;
	g_print (" time %.4f",
	    GST_INDEX_ASSOC_VALUE (entry, fx) / (double) GST_SECOND);
      } else
	g_print (" %s %lld", def->nick, GST_INDEX_ASSOC_VALUE (entry, fx));
    }
    g_print ("\n");
  }

  return 0;
}
