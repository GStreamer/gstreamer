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
static void		gst_cache_init		(GstCache *tc);

#define CLASS(cache)  GST_CACHE_CLASS (G_OBJECT_GET_CLASS (cache))

static GstObject *parent_class = NULL;
static guint gst_cache_signals[LAST_SIGNAL] = { 0 };

GType
gst_cache_get_type(void) {
  static GType tc_type = 0;

  if (!tc_type) {
    static const GTypeInfo tc_info = {
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
    tc_type = g_type_register_static(GST_TYPE_OBJECT, "GstCache", &tc_info, 0);
  }
  return tc_type;
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
  GstCacheGroup *tcgroup = g_new(GstCacheGroup,1);

  tcgroup->groupnum = groupnum;
  tcgroup->entries = NULL;
  tcgroup->certainty = GST_CACHE_UNKNOWN;
  tcgroup->peergroup = -1;

  GST_DEBUG(0, "created new cache group %d",groupnum);

  return tcgroup;
}

static void
gst_cache_init (GstCache *tc)
{
  tc->curgroup = gst_cache_group_new(0);
  tc->maxgroup = 0;
  tc->groups = g_list_prepend(NULL, tc->curgroup);

  tc->writers = g_hash_table_new (NULL, NULL);
  tc->last_id = 0;
  
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
  GstCache *tc;

  tc = g_object_new (gst_cache_get_type (), NULL);

  return tc;
}

/**
 * gst_cache_get_group:
 * @tc: the cache to get the current group from
 *
 * Get the id of the current group.
 *
 * Returns: the id of the current group.
 */
gint
gst_cache_get_group(GstCache *tc)
{
  return tc->curgroup->groupnum;
}

/**
 * gst_cache_new_group:
 * @tc: the cache to create the new group in
 *
 * Create a new group for the given cache. It will be
 * set as the current group.
 *
 * Returns: the id of the newly created group.
 */
gint
gst_cache_new_group(GstCache *tc)
{
  tc->curgroup = gst_cache_group_new(++tc->maxgroup);
  tc->groups = g_list_append(tc->groups,tc->curgroup);
  GST_DEBUG(0, "created new group %d in cache",tc->maxgroup);
  return tc->maxgroup;
}

/**
 * gst_cache_set_group:
 * @tc: the cache to set the new group in
 * @groupnum: the groupnumber to set
 *
 * Set the current groupnumber to the given argument.
 *
 * Returns: TRUE if the operation succeeded, FALSE if the group
 * did not exist.
 */
gboolean
gst_cache_set_group(GstCache *tc, gint groupnum)
{
  GList *list;
  GstCacheGroup *tcgroup;

  /* first check for null change */
  if (groupnum == tc->curgroup->groupnum)
    return TRUE;

  /* else search for the proper group */
  list = tc->groups;
  while (list) {
    tcgroup = (GstCacheGroup *)(list->data);
    list = g_list_next(list);
    if (tcgroup->groupnum == groupnum) {
      tc->curgroup = tcgroup;
      GST_DEBUG(0, "switched to cache group %d", tcgroup->groupnum);
      return TRUE;
    }
  }

  /* couldn't find the group in question */
  GST_DEBUG(0, "couldn't find cache group %d",groupnum);
  return FALSE;
}

/**
 * gst_cache_set_certainty:
 * @tc: the cache to set the certainty on
 * @certainty: the certainty to set
 *
 * Set the certainty of the given cache.
 */
void
gst_cache_set_certainty(GstCache *tc, GstCacheCertainty certainty)
{
  tc->curgroup->certainty = certainty;
}

/**
 * gst_cache_get_certainty:
 * @tc: the cache to get the certainty of
 *
 * Get the certainty of the given cache.
 *
 * Returns: the certainty of the cache.
 */
GstCacheCertainty
gst_cache_get_certainty(GstCache *tc)
{
  return tc->curgroup->certainty;
}


/**
 * gst_cache_add_format:
 * @tc: the cache to add the entry to
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
gst_cache_add_format (GstCache *tc, gint id, GstFormat format)
{
  GstCacheEntry *entry;
  const GstFormatDefinition* def;

  g_return_val_if_fail (GST_IS_CACHE (tc), NULL);
  g_return_val_if_fail (format != 0, NULL);
  
  entry = g_new0 (GstCacheEntry, 1);
  entry->type = GST_CACHE_ENTRY_FORMAT;
  entry->id = id;
  entry->data.format.format = format;
  def = gst_format_get_details (format);
  entry->data.format.key = def->nick;
  
  g_signal_emit (G_OBJECT (tc), gst_cache_signals[ENTRY_ADDED], 0, entry);

  return entry;
}

/**
 * gst_cache_add_id:
 * @tc: the cache to add the entry to
 * @id: the id of the cache writer
 * @description: the description of the cache writer
 *
 * Returns: a pointer to the newly added entry in the cache.
 */
GstCacheEntry*
gst_cache_add_id (GstCache *tc, gint id, gchar *description)
{
  GstCacheEntry *entry;

  g_return_val_if_fail (GST_IS_CACHE (tc), NULL);
  g_return_val_if_fail (description != NULL, NULL);
  
  entry = g_new0 (GstCacheEntry, 1);
  entry->type = GST_CACHE_ENTRY_ID;
  entry->id = id;
  entry->data.id.description = description;
  
  g_signal_emit (G_OBJECT (tc), gst_cache_signals[ENTRY_ADDED], 0, entry);

  return entry;
}

/**
 * gst_cache_get_writer_id:
 * @tc: the cache to get a unique write id for
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
gst_cache_get_writer_id (GstCache *tc, GstObject *writer, gint *id)
{
  gchar *writer_string = NULL;
  gboolean success = FALSE;
  GstCacheEntry *entry;

  g_return_val_if_fail (GST_IS_CACHE (tc), FALSE);
  g_return_val_if_fail (GST_IS_OBJECT (writer), FALSE);
  g_return_val_if_fail (id, FALSE);

  *id = -1;

  entry = g_hash_table_lookup (tc->writers, writer);
  if (entry == NULL) { 
    *id = tc->last_id;

    writer_string = gst_object_get_path_string (writer);
    
    gst_cache_add_id (tc, *id, writer_string);
    tc->last_id++;
    g_hash_table_insert (tc->writers, writer, entry);
  }

  if (CLASS (tc)->resolve_writer) {
    success = CLASS (tc)->resolve_writer (tc, writer, id, &writer_string);
  }

  if (tc->resolver) {
    success = tc->resolver (tc, writer, id, &writer_string, tc->user_data);
  }

  return success;
}

/**
 * gst_cache_add_association:
 * @tc: the cache to add the entry to
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
gst_cache_add_association (GstCache *tc, gint id, GstAssocFlags flags, 
		                GstFormat format, gint64 value, ...)
{
  va_list args;
  GstCacheAssociation *assoc;
  GstCacheEntry *entry;
  gulong size;
  gint nassocs = 0;
  GstFormat cur_format;
  gint64 dummy;

  g_return_val_if_fail (GST_IS_CACHE (tc), NULL);
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

  if (CLASS (tc)->add_entry)
    CLASS (tc)->add_entry (tc, entry);

  g_signal_emit (G_OBJECT (tc), gst_cache_signals[ENTRY_ADDED], 0, entry);

  return entry;
}
