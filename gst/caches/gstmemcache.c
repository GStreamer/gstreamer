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

#include <gst/gst_private.h>
#include <gst/gstversion.h>
#include <gst/gstplugin.h>
#include <gst/gstcache.h>

#define GST_TYPE_MEM_CACHE		\
  (gst_cache_get_type ())
#define GST_MEM_CACHE(obj)		\
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_MEM_CACHE, GstMemCache))
#define GST_MEM_CACHE_CLASS(klass)	\
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_MEM_CACHE, GstMemCacheClass))
#define GST_IS_MEM_CACHE(obj)		\
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_MEM_CACHE))
#define GST_IS_MEM_CACHE_CLASS(obj)	\
  (GST_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_MEM_CACHE))
	
/*
 * Object model:
 *
 * All entries are simply added to a GList first. Then we build
 * an index to each entry for each id/format
 * 
 *
 *  memcache
 *    -----------------------------...
 *    !                  !         
 *   id1                 id2        
 *    ------------
 *    !          !
 *   format1  format2
 *    !          !
 *   GTree      GTree
 *
 *
 * The memcache creates a MemCacheId object for each writer id, a
 * Hashtable is kept to map the id to the MemCacheId
 *
 * The MemCacheId keeps a MemCacheFormatIndex for each format the
 * specific writer wants indexed.
 *
 * The MemCacheFormatIndex keeps all the values of the particular 
 * format in a GTree, The values of the GTree point back to the entry. 
 *
 * Finding a value for an id/format requires locating the correct GTree,
 * then do a lookup in the Tree to get the required value.
 */

typedef struct {
  GstFormat 	 format;
  gint		 offset;
  GTree		*tree;
} GstMemCacheFormatIndex;

typedef struct {
  gint 		 id;
  GHashTable	*format_index;
} GstMemCacheId;

typedef struct _GstMemCache GstMemCache;
typedef struct _GstMemCacheClass GstMemCacheClass;

struct _GstMemCache {
  GstCache		 parent;

  GList			*associations;

  GHashTable		*id_index;
};

struct _GstMemCacheClass {
  GstCacheClass parent_class;
};

/* Cache signals and args */
enum {
  LAST_SIGNAL
};

enum {
  ARG_0,
  /* FILL ME */
};

static void		gst_mem_cache_class_init	(GstMemCacheClass *klass);
static void		gst_mem_cache_init		(GstMemCache *cache);
static void 		gst_mem_cache_dispose 		(GObject *object);

static void 		gst_mem_cache_add_entry 	(GstCache *cache, GstCacheEntry *entry);
static GstCacheEntry* 	gst_mem_cache_get_assoc_entry 	(GstCache *cache, gint id,
                              				 GstCacheLookupMethod method,
                              				 GstFormat format, gint64 value,
                              				 GCompareDataFunc func,
                              				 gpointer user_data);

#define CLASS(mem_cache)  GST_MEM_CACHE_CLASS (G_OBJECT_GET_CLASS (mem_cache))

static GstCache *parent_class = NULL;
/*static guint gst_mem_cache_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_mem_cache_get_type(void) {
  static GType mem_cache_type = 0;

  if (!mem_cache_type) {
    static const GTypeInfo mem_cache_info = {
      sizeof(GstMemCacheClass),
      NULL,
      NULL,
      (GClassInitFunc)gst_mem_cache_class_init,
      NULL,
      NULL,
      sizeof(GstMemCache),
      1,
      (GInstanceInitFunc)gst_mem_cache_init,
      NULL
    };
    mem_cache_type = g_type_register_static(GST_TYPE_CACHE, "GstMemCache", &mem_cache_info, 0);
  }
  return mem_cache_type;
}

static void
gst_mem_cache_class_init (GstMemCacheClass *klass)
{
  GObjectClass *gobject_class;
  GstCacheClass *gstcache_class;

  gobject_class = (GObjectClass*)klass;
  gstcache_class = (GstCacheClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_CACHE);

  gobject_class->dispose = gst_mem_cache_dispose;

  gstcache_class->add_entry 	  = gst_mem_cache_add_entry;
  gstcache_class->get_assoc_entry = gst_mem_cache_get_assoc_entry;
}

static void
gst_mem_cache_init (GstMemCache *cache)
{
  GST_DEBUG(0, "created new mem cache");

  cache->associations = NULL;
  cache->id_index = g_hash_table_new (g_int_hash, g_int_equal);
}

static void
gst_mem_cache_dispose (GObject *object)
{
  //GstMemCache *memcache = GST_MEM_CACHE (object);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_mem_cache_add_id (GstCache *cache, GstCacheEntry *entry)
{
  GstMemCache *memcache = GST_MEM_CACHE (cache);
  GstMemCacheId *id_index;

  id_index = g_hash_table_lookup (memcache->id_index, &entry->id);

  if (!id_index) {
    id_index = g_new0 (GstMemCacheId, 1);

    id_index->id = entry->id;
    id_index->format_index = g_hash_table_new (g_int_hash, g_int_equal);
    g_hash_table_insert (memcache->id_index, &entry->id, id_index);
  }
}

static gint
mem_cache_compare (gconstpointer a,
		   gconstpointer b,
		   gpointer user_data)
{
  GstMemCacheFormatIndex *index = user_data;
  gint64 val1, val2;
  gint64 diff;

  val1 = GST_CACHE_ASSOC_VALUE (((GstCacheEntry *)a), index->offset);
  val2 = GST_CACHE_ASSOC_VALUE (((GstCacheEntry *)b), index->offset);
	  
  diff = (val2 - val1);

  return (diff == 0 ? 0 : (diff > 0 ? 1 : -1));
}

static void
gst_mem_cache_index_format (GstMemCacheId *id_index, GstCacheEntry *entry, gint assoc)
{
  GstMemCacheFormatIndex *index;
  GstFormat *format;

  format = &GST_CACHE_ASSOC_FORMAT (entry, assoc);

  index = g_hash_table_lookup (id_index->format_index, format);

  if (!index) {
    index = g_new0 (GstMemCacheFormatIndex, 1);

    index->format = *format;
    index->offset = assoc;
    index->tree = g_tree_new_with_data (mem_cache_compare, index);

    g_hash_table_insert (id_index->format_index, format, index);
  }

  g_tree_insert (index->tree, entry, entry);
}

static void
gst_mem_cache_add_association (GstCache *cache, GstCacheEntry *entry)
{
  GstMemCache *memcache = GST_MEM_CACHE (cache);
  GstMemCacheId *id_index;

  memcache->associations = g_list_prepend (memcache->associations, entry);

  id_index = g_hash_table_lookup (memcache->id_index, &entry->id);
  if (id_index) {
    gint i;

    for (i = 0; i < GST_CACHE_NASSOCS (entry); i++) {
      gst_mem_cache_index_format (id_index, entry, i);
    }
  }
}

static void
gst_mem_cache_add_object (GstCache *cache, GstCacheEntry *entry)
{
}

static void
gst_mem_cache_add_format (GstCache *cache, GstCacheEntry *entry)
{
}

static void
gst_mem_cache_add_entry (GstCache *cache, GstCacheEntry *entry)
{
  GstMemCache *memcache = GST_MEM_CACHE (cache);

  GST_DEBUG (0, "adding entry %p\n", memcache);

  switch (entry->type){
     case GST_CACHE_ENTRY_ID:
       gst_mem_cache_add_id (cache, entry);
       break;
     case GST_CACHE_ENTRY_ASSOCIATION:
       gst_mem_cache_add_association (cache, entry);
       break;
     case GST_CACHE_ENTRY_OBJECT:
       gst_mem_cache_add_object (cache, entry);
       break;
     case GST_CACHE_ENTRY_FORMAT:
       gst_mem_cache_add_format (cache, entry);
       break;
     default:
       break;
  }
}

typedef struct {
  gint64	 	  value;
  GstMemCacheFormatIndex *index;
  gboolean		  exact;
  GstCacheEntry 	 *lower;
  gint64		  low_diff;
  GstCacheEntry 	 *higher;
  gint64		  high_diff;
} GstMemCacheSearchData;

static gint
mem_cache_search (gconstpointer a,
		  gconstpointer b)
{
  GstMemCacheSearchData *data = (GstMemCacheSearchData *) b;
  GstMemCacheFormatIndex *index = data->index;
  gint64 val1, val2;
  gint64 diff;

  val1 = GST_CACHE_ASSOC_VALUE (((GstCacheEntry *)a), index->offset);
  val2 = data->value;
	  
  diff = (val1 - val2);
  if (diff == 0)
    return 0;

  /* exact matching, don't update low/high */
  if (data->exact)
    return (diff > 0 ? 1 : -1);

  if (diff < 0) {
    if (diff > data->low_diff) {
      data->low_diff = diff;
      data->lower = (GstCacheEntry *) a;
    }
    diff = -1;
  }
  else {
    if (diff < data->high_diff) {
      data->high_diff = diff;
      data->higher = (GstCacheEntry *) a;
    }
    diff = 1;
  }

  return diff;
}

static GstCacheEntry*
gst_mem_cache_get_assoc_entry (GstCache *cache, gint id,
                               GstCacheLookupMethod method,
                               GstFormat format, gint64 value,
                               GCompareDataFunc func,
                               gpointer user_data)
{
  GstMemCache *memcache = GST_MEM_CACHE (cache);
  GstMemCacheId *id_index;
  GstMemCacheFormatIndex *index;
  GstCacheEntry *entry;
  GstMemCacheSearchData data;

  id_index = g_hash_table_lookup (memcache->id_index, &id);
  if (!id_index)
    return NULL;

  index = g_hash_table_lookup (id_index->format_index, &format);
  if (!index)
    return NULL;

  data.value = value;
  data.index = index;
  data.exact = (method == GST_CACHE_LOOKUP_EXACT);

  /* setup data for low/high checks if we are not looking 
   * for an exact match */
  if (!data.exact) {
    data.low_diff = G_MININT64;
    data.lower = NULL;
    data.high_diff = G_MAXINT64;
    data.higher = NULL;
  }

  entry = g_tree_search (index->tree, mem_cache_search, &data);

  /* get the low/high values if we're not exact */
  if (entry == NULL && !data.exact) { 
    if (method == GST_CACHE_LOOKUP_BEFORE)
      entry = data.lower;
    else if (method == GST_CACHE_LOOKUP_AFTER) {
      entry = data.higher;
    }
  }

  return entry;
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstCacheFactory *factory;

  gst_plugin_set_longname (plugin, "A memory cache");

  factory = gst_cache_factory_new ("memcache",
	                           "A cache that stores entries in memory",
                                   gst_mem_cache_get_type());

  if (factory != NULL) {
    gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));
  }
  else {
    g_warning ("could not register memcache");
  }
  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "gstcaches",
  plugin_init
};

