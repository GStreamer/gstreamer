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
lookup (GstCache *cache, GstCacheLookupMethod method,
        GstFormat src_format, gint64 src_value,
	GstFormat dest_format)
{
  GstCacheEntry *entry;
  gint64 result;

  entry = gst_cache_get_assoc_entry (cache, 0, method, 
		                     src_format, src_value); 
  if (entry) {
    gst_cache_entry_assoc_map (entry, dest_format, &result); 

    g_print ("%lld\n", result);
  }
  else {
    const GstFormatDefinition *def = gst_format_get_details (src_format);
    
    g_print ("no cache entry found for %lld %s\n", src_value, def->nick);
  }
}

typedef struct _GstCacheTestCase
{
  GstCacheLookupMethod 	method;
  GstFormat		src_format;
  gint64		src_value;
  GstFormat		dest_format;
} GstCacheTestCase;

const static GstCacheTestCase cases[] =
{
  { GST_CACHE_LOOKUP_EXACT, 	GST_FORMAT_BYTES, 	3, 		GST_FORMAT_TIME },
  { GST_CACHE_LOOKUP_EXACT, 	GST_FORMAT_TIME, 	5000, 		GST_FORMAT_BYTES },
  { GST_CACHE_LOOKUP_EXACT, 	GST_FORMAT_TIME, 	5010, 		GST_FORMAT_BYTES },
  { GST_CACHE_LOOKUP_BEFORE, 	GST_FORMAT_TIME, 	5010, 		GST_FORMAT_BYTES },
  { GST_CACHE_LOOKUP_BEFORE, 	GST_FORMAT_TIME, 	0, 		GST_FORMAT_BYTES },
  { GST_CACHE_LOOKUP_AFTER, 	GST_FORMAT_TIME, 	G_MAXINT64, 	GST_FORMAT_BYTES },
  { GST_CACHE_LOOKUP_AFTER, 	GST_FORMAT_TIME, 	0, 		GST_FORMAT_BYTES },
  { GST_CACHE_LOOKUP_BEFORE, 	GST_FORMAT_TIME, 	-1, 		GST_FORMAT_BYTES },
  { GST_CACHE_LOOKUP_BEFORE, 	GST_FORMAT_TIME, 	G_MAXINT64, 	GST_FORMAT_BYTES },
  { GST_CACHE_LOOKUP_AFTER, 	GST_FORMAT_TIME, 	G_MAXINT64, 	GST_FORMAT_BYTES },
};

gint 
main (gint argc, gchar *argv[]) 
{
  GstCache *cache;
  GstElement *element;
  gint i, id;
  
  gst_init (&argc, &argv);

  cache = gst_cache_factory_make ("memcache");
  g_assert (cache != NULL);

  element = gst_element_factory_make ("identity", "element");
  g_assert (element != NULL);

  gst_cache_get_writer_id (cache, GST_OBJECT (element), &id);

  for (i = 0; i < 100000; i++) {
    gst_cache_add_association (cache, 0, 0, GST_FORMAT_BYTES, (gint64)i, GST_FORMAT_TIME, 
		                            (gint64) (i * 1000), 0);
  }

  for (i = 0; i < (sizeof (cases) / sizeof (GstCacheTestCase)); i++) {
    lookup (cache, cases[i].method, cases[i].src_format, cases[i].src_value, cases[i].dest_format);
  }
  
  return 0;
}
