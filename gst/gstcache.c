/* GStreamer
 * Copyright (C) 2001 RidgeRun (http://www.ridgerun.com/)
 * Written by Erik Walthinsen <omega@ridgerun.com>
 *
 * gstcache.c: Cache for mappings and other data
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

#include "gstlog.h"
#include "gst_private.h"
#include "gstregistry.h"

#include "gstcache.h"

/* Cache signals and args */
enum {
  ENTRY_ADDED,
  LAST_SIGNAL
};

enum {
  ARG_0,
  /* FILL ME */
};

static void		gst_cache_class_init	(GstCacheClass *klass);
static void		gst_cache_init		(GstCache *cache);

#define CLASS(cache)  GST_CACHE_CLASS (G_OBJECT_GET_CLASS (cache))

static GstObject *parent_class = NULL;
static guint gst_cache_signals[LAST_SIGNAL] = { 0 };

GType
gst_cache_get_type(void) {
  static GType cache_type = 0;

  if (!cache_type) {
    static const GTypeInfo cache_info = {
      sizeof(GstCacheClass),
      NULL,
      NULL,
      (GClassInitFunc)gst_cache_class_init,
      NULL,
      NULL,
      sizeof(GstCache),
      1,
      (GInstanceInitFunc)gst_cache_init,
      NULL
    };
    cache_type = g_type_register_static(GST_TYPE_OBJECT, "GstCache", &cache_info, 0);
  }
  return cache_type;
}

static void
gst_cache_class_init (GstCacheClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_OBJECT);

  gst_cache_signals[ENTRY_ADDED] =
    g_signal_new ("entry_added", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (GstCacheClass, entry_added), NULL, NULL,
                  gst_marshal_VOID__POINTER, G_TYPE_NONE, 1,
                  G_TYPE_POINTER);
}

static GstCacheGroup *
gst_cache_group_new(guint groupnum)
{
  GstCacheGroup *cachegroup = g_new(GstCacheGroup,1);

  cachegroup->groupnum = groupnum;
  cachegroup->entries = NULL;
  cachegroup->certainty = GST_CACHE_UNKNOWN;
  cachegroup->peergroup = -1;

  GST_DEBUG(0, "created new cache group %d",groupnum);

  return cachegroup;
}

static void
gst_cache_init (GstCache *cache)
{
  cache->curgroup = gst_cache_group_new(0);
  cache->maxgroup = 0;
  cache->groups = g_list_prepend(NULL, cache->curgroup);

  cache->writers = g_hash_table_new (NULL, NULL);
  cache->last_id = 0;
  
  GST_DEBUG(0, "created new cache");
}

/**
 * gst_cache_new:
 *
 * Create a new tilecache object
 *
 * Returns: a new cache object
 */
GstCache *
gst_cache_new()
{
  GstCache *cache;

  cache = g_object_new (gst_cache_get_type (), NULL);

  return cache;
}

/**
 * gst_cache_get_group:
 * @cache: the cache to get the current group from
 *
 * Get the id of the current group.
 *
 * Returns: the id of the current group.
 */
gint
gst_cache_get_group(GstCache *cache)
{
  return cache->curgroup->groupnum;
}

/**
 * gst_cache_new_group:
 * @cache: the cache to create the new group in
 *
 * Create a new group for the given cache. It will be
 * set as the current group.
 *
 * Returns: the id of the newly created group.
 */
gint
gst_cache_new_group(GstCache *cache)
{
  cache->curgroup = gst_cache_group_new(++cache->maxgroup);
  cache->groups = g_list_append(cache->groups,cache->curgroup);
  GST_DEBUG(0, "created new group %d in cache",cache->maxgroup);
  return cache->maxgroup;
}

/**
 * gst_cache_set_group:
 * @cache: the cache to set the new group in
 * @groupnum: the groupnumber to set
 *
 * Set the current groupnumber to the given argument.
 *
 * Returns: TRUE if the operation succeeded, FALSE if the group
 * did not exist.
 */
gboolean
gst_cache_set_group(GstCache *cache, gint groupnum)
{
  GList *list;
  GstCacheGroup *cachegroup;

  /* first check for null change */
  if (groupnum == cache->curgroup->groupnum)
    return TRUE;

  /* else search for the proper group */
  list = cache->groups;
  while (list) {
    cachegroup = (GstCacheGroup *)(list->data);
    list = g_list_next(list);
    if (cachegroup->groupnum == groupnum) {
      cache->curgroup = cachegroup;
      GST_DEBUG(0, "swicachehed to cache group %d", cachegroup->groupnum);
      return TRUE;
    }
  }

  /* couldn't find the group in question */
  GST_DEBUG(0, "couldn't find cache group %d",groupnum);
  return FALSE;
}

/**
 * gst_cache_set_certainty:
 * @cache: the cache to set the certainty on
 * @certainty: the certainty to set
 *
 * Set the certainty of the given cache.
 */
void
gst_cache_set_certainty(GstCache *cache, GstCacheCertainty certainty)
{
  cache->curgroup->certainty = certainty;
}

/**
 * gst_cache_get_certainty:
 * @cache: the cache to get the certainty of
 *
 * Get the certainty of the given cache.
 *
 * Returns: the certainty of the cache.
 */
GstCacheCertainty
gst_cache_get_certainty(GstCache *cache)
{
  return cache->curgroup->certainty;
}

void
gst_cache_set_filter (GstCache *cache, 
		      GstCacheFilter filter, gpointer user_data)
{
  g_return_if_fail (GST_IS_CACHE (cache));

  cache->filter = filter;
  cache->filter_user_data = user_data;
}

void
gst_cache_set_resolver (GstCache *cache, 
		        GstCacheResolver resolver, gpointer user_data)
{
  g_return_if_fail (GST_IS_CACHE (cache));

  cache->resolver = resolver;
  cache->resolver_user_data = user_data;
}

/**
 * gst_cache_add_format:
 * @cache: the cache to add the entry to
 * @id: the id of the cache writer
 * @format: the format to add to the cache
 *
 * Adds a format entry into the cache. This function is
 * used to map dynamic GstFormat ids to their original
 * format key.
 *
 * Returns: a pointer to the newly added entry in the cache.
 */
GstCacheEntry*
gst_cache_add_format (GstCache *cache, gint id, GstFormat format)
{
  GstCacheEntry *entry;
  const GstFormatDefinition* def;

  g_return_val_if_fail (GST_IS_CACHE (cache), NULL);
  g_return_val_if_fail (format != 0, NULL);
  
  entry = g_new0 (GstCacheEntry, 1);
  entry->type = GST_CACHE_ENTRY_FORMAT;
  entry->id = id;
  entry->data.format.format = format;
  def = gst_format_get_details (format);
  entry->data.format.key = def->nick;
  
  if (CLASS (cache)->add_entry)
    CLASS (cache)->add_entry (cache, entry);

  g_signal_emit (G_OBJECT (cache), gst_cache_signals[ENTRY_ADDED], 0, entry);

  return entry;
}

/**
 * gst_cache_add_id:
 * @cache: the cache to add the entry to
 * @id: the id of the cache writer
 * @description: the description of the cache writer
 *
 * Returns: a pointer to the newly added entry in the cache.
 */
GstCacheEntry*
gst_cache_add_id (GstCache *cache, gint id, gchar *description)
{
  GstCacheEntry *entry;

  g_return_val_if_fail (GST_IS_CACHE (cache), NULL);
  g_return_val_if_fail (description != NULL, NULL);
  
  entry = g_new0 (GstCacheEntry, 1);
  entry->type = GST_CACHE_ENTRY_ID;
  entry->id = id;
  entry->data.id.description = description;

  if (CLASS (cache)->add_entry)
    CLASS (cache)->add_entry (cache, entry);
  
  g_signal_emit (G_OBJECT (cache), gst_cache_signals[ENTRY_ADDED], 0, entry);

  return entry;
}

/**
 * gst_cache_get_writer_id:
 * @cache: the cache to get a unique write id for
 * @writer: the GstObject to allocate an id for
 * @id: a pointer to a gint to hold the id
 *
 * Before entries can be added to the cache, a writer
 * should obtain a unique id. The methods to add new entries
 * to the cache require this id as an argument. 
 *
 * The application or a GstCache subclass can implement
 * custom functions to map the writer object to an id.
 *
 * Returns: TRUE if the writer would be mapped to an id.
 */
gboolean 
gst_cache_get_writer_id (GstCache *cache, GstObject *writer, gint *id)
{
  gchar *writer_string = NULL;
  gboolean success = FALSE;
  GstCacheEntry *entry;

  g_return_val_if_fail (GST_IS_CACHE (cache), FALSE);
  g_return_val_if_fail (GST_IS_OBJECT (writer), FALSE);
  g_return_val_if_fail (id, FALSE);

  *id = -1;

  entry = g_hash_table_lookup (cache->writers, writer);
  if (entry == NULL) { 
    *id = cache->last_id;

    writer_string = gst_object_get_path_string (writer);
    
    gst_cache_add_id (cache, *id, writer_string);
    cache->last_id++;
    g_hash_table_insert (cache->writers, writer, entry);
  }

  if (CLASS (cache)->resolve_writer) {
    success = CLASS (cache)->resolve_writer (cache, writer, id, &writer_string);
  }

  if (cache->resolver) {
    success = cache->resolver (cache, writer, id, &writer_string, cache->resolver_user_data);
  }

  return success;
}

/**
 * gst_cache_add_association:
 * @cache: the cache to add the entry to
 * @id: the id of the cache writer
 * @format: the format of the value
 * @value: the value 
 * @...: other format/value pairs or 0 to end the list
 *
 * Associate given format/value pairs with eachother.
 *
 * Returns: a pointer to the newly added entry in the cache.
 */
GstCacheEntry*
gst_cache_add_association (GstCache *cache, gint id, GstAssocFlags flags, 
		                GstFormat format, gint64 value, ...)
{
  va_list args;
  GstCacheAssociation *assoc;
  GstCacheEntry *entry;
  gulong size;
  gint nassocs = 0;
  GstFormat cur_format;
  gint64 dummy;

  g_return_val_if_fail (GST_IS_CACHE (cache), NULL);
  g_return_val_if_fail (format != 0, NULL);
  
  va_start (args, value);

  cur_format = format;

  while (cur_format) {
    nassocs++;
    cur_format = va_arg (args, GstFormat);
    if (cur_format)
      dummy = va_arg (args, gint64);
  }
  va_end (args);

  /* make room for two assoc */
  size = sizeof (GstCacheEntry) + (sizeof (GstCacheAssociation) * nassocs);

  entry = g_malloc (size);

  entry->type = GST_CACHE_ENTRY_ASSOCIATION;
  entry->id = id;
  entry->data.assoc.flags = flags;
  assoc = (GstCacheAssociation *) (((guint8 *) entry) + sizeof (GstCacheEntry));
  entry->data.assoc.assocs = assoc;
  entry->data.assoc.nassocs = nassocs;

  va_start (args, value);
  while (format) {
    assoc->format = format;
    assoc->value = value;

    assoc++;

    format = va_arg (args, GstFormat);
    if (format)
      value = va_arg (args, gint64);
  }
  va_end (args);

  if (CLASS (cache)->add_entry)
    CLASS (cache)->add_entry (cache, entry);

  g_signal_emit (G_OBJECT (cache), gst_cache_signals[ENTRY_ADDED], 0, entry);

  return entry;
}

static void 		gst_cache_factory_class_init 		(GstCacheFactoryClass *klass);
static void 		gst_cache_factory_init 		(GstCacheFactory *factory);

static GstPluginFeatureClass *factory_parent_class = NULL;
/* static guint gst_cache_factory_signals[LAST_SIGNAL] = { 0 }; */

GType 
gst_cache_factory_get_type (void) 
{
  static GType cachefactory_type = 0;

  if (!cachefactory_type) {
    static const GTypeInfo cachefactory_info = {
      sizeof (GstCacheFactoryClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_cache_factory_class_init,
      NULL,
      NULL,
      sizeof(GstCacheFactory),
      0,
      (GInstanceInitFunc) gst_cache_factory_init,
      NULL
    };
    cachefactory_type = g_type_register_static (GST_TYPE_PLUGIN_FEATURE, 
	    				  "GstCacheFactory", &cachefactory_info, 0);
  }
  return cachefactory_type;
}

static void
gst_cache_factory_class_init (GstCacheFactoryClass *klass)
{
  GObjectClass *gobject_class;
  GstObjectClass *gstobject_class;
  GstPluginFeatureClass *gstpluginfeature_class;

  gobject_class = (GObjectClass*)klass;
  gstobject_class = (GstObjectClass*)klass;
  gstpluginfeature_class = (GstPluginFeatureClass*) klass;

  factory_parent_class = g_type_class_ref (GST_TYPE_PLUGIN_FEATURE);
}

static void
gst_cache_factory_init (GstCacheFactory *factory)
{
}

/**
 * gst_cache_factory_new:
 * @name: name of cachefactory to create
 * @longdesc: long description of cachefactory to create
 * @type: the GType of the GstCache element of this factory
 *
 * Create a new cachefactory with the given parameters
 *
 * Returns: a new #GstCacheFactory.
 */
GstCacheFactory*
gst_cache_factory_new (const gchar *name, const gchar *longdesc, GType type)
{
  GstCacheFactory *factory;

  g_return_val_if_fail(name != NULL, NULL);
  factory = gst_cache_factory_find (name);
  if (!factory) {
    factory = GST_CACHE_FACTORY (g_object_new (GST_TYPE_CACHE_FACTORY, NULL));
  }

  GST_PLUGIN_FEATURE_NAME (factory) = g_strdup (name);
  if (factory->longdesc)
    g_free (factory->longdesc);
  factory->longdesc = g_strdup (longdesc);
  factory->type = type;

  return factory;
}

/**
 * gst_cache_factory_destroy:
 * @factory: factory to destroy
 *
 * Removes the cache from the global list.
 */
void
gst_cache_factory_destroy (GstCacheFactory *factory)
{
  g_return_if_fail (factory != NULL);

  /* we don't free the struct bacause someone might  have a handle to it.. */
}

/**
 * gst_cache_factory_find:
 * @name: name of cachefactory to find
 *
 * Search for an cachefactory of the given name.
 *
 * Returns: #GstCacheFactory if found, NULL otherwise
 */
GstCacheFactory*
gst_cache_factory_find (const gchar *name)
{
  GstPluginFeature *feature;

  g_return_val_if_fail (name != NULL, NULL);

  GST_DEBUG (0,"gstcache: find \"%s\"", name);

  feature = gst_registry_pool_find_feature (name, GST_TYPE_CACHE_FACTORY);
  if (feature)
    return GST_CACHE_FACTORY (feature);

  return NULL;
}

/**
 * gst_cache_factory_create:
 * @factory: the factory used to create the instance
 *
 * Create a new #GstCache instance from the 
 * given cachefactory.
 *
 * Returns: A new #GstCache instance.
 */
GstCache*
gst_cache_factory_create (GstCacheFactory *factory)
{
  GstCache *new = NULL;

  g_return_val_if_fail (factory != NULL, NULL);

  if (gst_plugin_feature_ensure_loaded (GST_PLUGIN_FEATURE (factory))) {
    g_return_val_if_fail (factory->type != 0, NULL);

    new = GST_CACHE (g_object_new(factory->type,NULL));
  }

  return new;
}

/**
 * gst_cache_factory_make:
 * @name: the name of the factory used to create the instance
 *
 * Create a new #GstCache instance from the 
 * cachefactory with the given name.
 *
 * Returns: A new #GstCache instance.
 */
GstCache*
gst_cache_factory_make (const gchar *name)
{
  GstCacheFactory *factory;

  g_return_val_if_fail (name != NULL, NULL);

  factory = gst_cache_factory_find (name);

  if (factory == NULL)
    return NULL;

  return gst_cache_factory_create (factory);
}

