/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wim.taymans@chello.be>
 *                    2005 Wim Taymans <wim@fluendo.com>
 *
 * gstquery.c: GstQueryType registration
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

#include <string.h>

#include "gst_private.h"
#include "gstquery.h"
#include "gstmemchunk.h"
#include "gstenumtypes.h"

GST_DEBUG_CATEGORY_STATIC (gst_query_debug);
#define GST_CAT_DEFAULT gst_query_debug

static void gst_query_init (GTypeInstance * instance, gpointer g_class);
static void gst_query_class_init (gpointer g_class, gpointer class_data);
static void gst_query_finalize (GstQuery * query);
static GstQuery *_gst_query_copy (GstQuery * query);


static GStaticMutex mutex = G_STATIC_MUTEX_INIT;
static GList *_gst_queries = NULL;
static GHashTable *_nick_to_query = NULL;
static GHashTable *_query_type_to_nick = NULL;
static guint32 _n_values = 1;   /* we start from 1 because 0 reserved for NONE */

static GstMemChunk *chunk;

static GstQueryTypeDefinition standard_definitions[] = {
  {GST_QUERY_POSITION, "position", "Current Position"},
  {GST_QUERY_LATENCY, "latency", "Latency"},
  {GST_QUERY_JITTER, "jitter", "Jitter"},
  {GST_QUERY_RATE, "rate", "Configured rate 1000000 = 1"},
  {GST_QUERY_SEEKING, "seeking", "Seeking capabilities and parameters"},
  {GST_QUERY_CONVERT, "convert", "Converting between formats"},
  {GST_QUERY_FORMATS, "formats", "Supported formats for conversion"},
  {0, NULL, NULL}
};

void
_gst_query_initialize (void)
{
  GstQueryTypeDefinition *standards = standard_definitions;

  GST_CAT_INFO (GST_CAT_GST_INIT, "init queries");

  GST_DEBUG_CATEGORY_INIT (gst_query_debug, "query", 0, "query system");

  g_static_mutex_lock (&mutex);
  if (_nick_to_query == NULL) {
    _nick_to_query = g_hash_table_new (g_str_hash, g_str_equal);
    _query_type_to_nick = g_hash_table_new (NULL, NULL);
  }

  while (standards->nick) {
    g_hash_table_insert (_nick_to_query, standards->nick, standards);
    g_hash_table_insert (_query_type_to_nick,
        GINT_TO_POINTER (standards->value), standards);

    _gst_queries = g_list_append (_gst_queries, standards);
    standards++;
    _n_values++;
  }
  g_static_mutex_unlock (&mutex);

  gst_query_get_type ();

  chunk = gst_mem_chunk_new ("GstQueryChunk", sizeof (GstQuery),
      sizeof (GstQuery) * 20, 0);
}

GType
gst_query_get_type (void)
{
  static GType _gst_query_type;

  if (G_UNLIKELY (_gst_query_type == 0)) {
    static const GTypeInfo query_info = {
      sizeof (GstQueryClass),
      NULL,
      NULL,
      gst_query_class_init,
      NULL,
      NULL,
      sizeof (GstQuery),
      0,
      gst_query_init,
      NULL
    };

    _gst_query_type = g_type_register_static (GST_TYPE_MINI_OBJECT,
        "GstQuery", &query_info, 0);
  }
  return _gst_query_type;
}

static void
gst_query_class_init (gpointer g_class, gpointer class_data)
{
  GstQueryClass *query_class = GST_QUERY_CLASS (g_class);

  query_class->mini_object_class.copy =
      (GstMiniObjectCopyFunction) _gst_query_copy;
  query_class->mini_object_class.finalize =
      (GstMiniObjectFinalizeFunction) gst_query_finalize;

}

static void
gst_query_finalize (GstQuery * query)
{
  g_return_if_fail (query != NULL);

  if (query->structure) {
    gst_structure_set_parent_refcount (query->structure, NULL);
    gst_structure_free (query->structure);
  }
}

static void
gst_query_init (GTypeInstance * instance, gpointer g_class)
{

}

static GstQuery *
_gst_query_copy (GstQuery * query)
{
  GstQuery *copy;

  copy = (GstQuery *) gst_mini_object_new (GST_TYPE_QUERY);

  copy->type = query->type;

  if (query->structure) {
    copy->structure = gst_structure_copy (query->structure);
    gst_structure_set_parent_refcount (copy->structure,
        &query->mini_object.refcount);
  }

  return copy;
}



/**
 * gst_query_type_register:
 * @nick: The nick of the new query
 * @description: The description of the new query
 *
 * Create a new GstQueryType based on the nick or return an
 * allrady registered query with that nick
 *
 * Returns: A new GstQueryType or an already registered query
 * with the same nick.
 */
GstQueryType
gst_query_type_register (const gchar * nick, const gchar * description)
{
  GstQueryTypeDefinition *query;
  GstQueryType lookup;

  g_return_val_if_fail (nick != NULL, 0);
  g_return_val_if_fail (description != NULL, 0);

  lookup = gst_query_type_get_by_nick (nick);
  if (lookup != GST_QUERY_NONE)
    return lookup;

  query = g_new0 (GstQueryTypeDefinition, 1);
  query->value = _n_values;
  query->nick = g_strdup (nick);
  query->description = g_strdup (description);

  g_static_mutex_lock (&mutex);
  g_hash_table_insert (_nick_to_query, query->nick, query);
  g_hash_table_insert (_query_type_to_nick, GINT_TO_POINTER (query->value),
      query);
  _gst_queries = g_list_append (_gst_queries, query);
  _n_values++;
  g_static_mutex_unlock (&mutex);

  return query->value;
}

/**
 * gst_query_type_get_by_nick:
 * @nick: The nick of the query
 *
 * Return the query registered with the given nick. 
 *
 * Returns: The query with @nick or GST_QUERY_NONE
 * if the query was not registered.
 */
GstQueryType
gst_query_type_get_by_nick (const gchar * nick)
{
  GstQueryTypeDefinition *query;

  g_return_val_if_fail (nick != NULL, 0);

  g_static_mutex_lock (&mutex);
  query = g_hash_table_lookup (_nick_to_query, nick);
  g_static_mutex_unlock (&mutex);

  if (query != NULL)
    return query->value;
  else
    return GST_QUERY_NONE;
}

/**
 * gst_query_types_contains:
 * @types: The query array to search
 * @type: the querytype to find
 *
 * See if the given query is inside the query array.
 *
 * Returns: TRUE if the query is found inside the array
 */
gboolean
gst_query_types_contains (const GstQueryType * types, GstQueryType type)
{
  if (!types)
    return FALSE;

  while (*types) {
    if (*types == type)
      return TRUE;

    types++;
  }
  return FALSE;
}


/**
 * gst_query_type_get_details:
 * @type: The query to get details of
 *
 * Get details about the given query.
 *
 * Returns: The #GstQueryTypeDefinition for @query or NULL on failure.
 */
const GstQueryTypeDefinition *
gst_query_type_get_details (GstQueryType type)
{
  const GstQueryTypeDefinition *result;

  g_static_mutex_lock (&mutex);
  result = g_hash_table_lookup (_query_type_to_nick, GINT_TO_POINTER (type));
  g_static_mutex_unlock (&mutex);

  return result;
}

/**
 * gst_query_type_iterate_definitions:
 *
 * Get an Iterator of all the registered query types. The querytype
 * definition is read only.
 *
 * Returns: A #GstIterator of #GstQueryTypeDefinition.
 */
GstIterator *
gst_query_type_iterate_definitions (void)
{
  GstIterator *result;

  g_static_mutex_lock (&mutex);
  result = gst_iterator_new_list (g_static_mutex_get_mutex (&mutex),
      &_n_values, &_gst_queries, NULL, NULL, NULL);
  g_static_mutex_unlock (&mutex);

  return result;
}

static GstQuery *
gst_query_new (GstQueryType type, GstStructure * structure)
{
  GstQuery *query;

  query = (GstQuery *) gst_mini_object_new (GST_TYPE_QUERY);

  GST_DEBUG ("creating new query %p %d", query, type);

  query->type = type;

  if (structure) {
    query->structure = structure;
    gst_structure_set_parent_refcount (query->structure,
        &query->mini_object.refcount);
  } else {
    query->structure = NULL;
  }

  return query;
}

GstQuery *
gst_query_new_position (GstFormat format)
{
  GstQuery *query;
  GstStructure *structure;

  structure = gst_structure_new ("GstQueryPosition",
      "format", GST_TYPE_FORMAT, format,
      "cur", G_TYPE_INT64, (gint64) - 1,
      "end", G_TYPE_INT64, (gint64) - 1, NULL);
  query = gst_query_new (GST_QUERY_POSITION, structure);

  return query;
}

void
gst_query_set_position (GstQuery * query, GstFormat format,
    gint64 cur, gint64 end)
{
  GstStructure *structure;

  g_return_if_fail (GST_QUERY_TYPE (query) == GST_QUERY_POSITION);

  structure = gst_query_get_structure (query);
  gst_structure_set (structure,
      "format", GST_TYPE_FORMAT, format,
      "cur", G_TYPE_INT64, cur, "end", G_TYPE_INT64, end, NULL);
}

void
gst_query_parse_position (GstQuery * query, GstFormat * format,
    gint64 * cur, gint64 * end)
{
  GstStructure *structure;

  g_return_if_fail (GST_QUERY_TYPE (query) == GST_QUERY_POSITION);

  structure = gst_query_get_structure (query);
  if (format)
    *format = g_value_get_enum (gst_structure_get_value (structure, "format"));
  if (cur)
    *cur = g_value_get_int64 (gst_structure_get_value (structure, "cur"));
  if (end)
    *end = g_value_get_int64 (gst_structure_get_value (structure, "end"));
}

GstQuery *
gst_query_new_convert (GstFormat src_fmt, gint64 value, GstFormat dest_fmt)
{
  GstQuery *query;
  GstStructure *structure;

  g_return_val_if_fail (value >= 0, NULL);

  structure = gst_structure_new ("GstQueryConvert",
      "src_format", GST_TYPE_FORMAT, src_fmt,
      "src_value", G_TYPE_INT64, value,
      "dest_format", GST_TYPE_FORMAT, dest_fmt,
      "dest_value", G_TYPE_INT64, (gint64) - 1, NULL);
  query = gst_query_new (GST_QUERY_CONVERT, structure);

  return query;
}

void
gst_query_set_convert (GstQuery * query, GstFormat src_format, gint64 src_value,
    GstFormat dest_format, gint64 dest_value)
{
  GstStructure *structure;

  g_return_if_fail (GST_QUERY_TYPE (query) == GST_QUERY_CONVERT);

  structure = gst_query_get_structure (query);
  gst_structure_set (structure,
      "src_format", GST_TYPE_FORMAT, src_format,
      "src_value", G_TYPE_INT64, src_value,
      "dest_format", GST_TYPE_FORMAT, dest_format,
      "dest_value", G_TYPE_INT64, dest_value, NULL);
}

void
gst_query_parse_convert (GstQuery * query, GstFormat * src_format,
    gint64 * src_value, GstFormat * dest_format, gint64 * dest_value)
{
  GstStructure *structure;

  g_return_if_fail (GST_QUERY_TYPE (query) == GST_QUERY_CONVERT);

  structure = gst_query_get_structure (query);
  if (src_format)
    *src_format =
        g_value_get_enum (gst_structure_get_value (structure, "src_format"));
  if (src_value)
    *src_value =
        g_value_get_int64 (gst_structure_get_value (structure, "src_value"));
  if (dest_format)
    *dest_format =
        g_value_get_enum (gst_structure_get_value (structure, "dest_format"));
  if (dest_value)
    *dest_value =
        g_value_get_int64 (gst_structure_get_value (structure, "dest_value"));
}


GstQuery *
gst_query_new_application (GstQueryType type, GstStructure * structure)
{
  g_return_val_if_fail (gst_query_type_get_details (type) != NULL, NULL);
  g_return_val_if_fail (structure != NULL, NULL);

  return gst_query_new (type, structure);
}

GstStructure *
gst_query_get_structure (GstQuery * query)
{
  g_return_val_if_fail (GST_IS_QUERY (query), NULL);

  return query->structure;
}
