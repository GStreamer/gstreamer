/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wim.taymans@chello.be>
 *                    2005 Wim Taymans <wim@fluendo.com>
 *
 * gstquery.c: GstQueryType registration and Query parsing/creation
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

/**
 * SECTION:gstquery
 * @short_description: Dynamically register new query types. Provide functions
 *                     to create queries, and to set and parse values in them.
 * @see_also: #GstPad, #GstElement
 *
 * GstQuery functions are used to register a new query types to the gstreamer
 * core.
 * Query types can be used to perform queries on pads and elements.
 *
 * Queries can be created using the gst_query_new_xxx() functions.  
 * Query values can be set using gst_query_set_xxx(), and parsed using 
 * gst_query_parse_xxx() helpers.
 */
#include <string.h>

#include "gst_private.h"
#include "gstquery.h"
#include "gstvalue.h"
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

static GstQueryTypeDefinition standard_definitions[] = {
  {GST_QUERY_POSITION, "position", "Current position", 0},
  {GST_QUERY_DURATION, "duration", "Total duration", 0},
  {GST_QUERY_LATENCY, "latency", "Latency", 0},
  {GST_QUERY_JITTER, "jitter", "Jitter", 0},
  {GST_QUERY_RATE, "rate", "Configured rate 1000000 = 1", 0},
  {GST_QUERY_SEEKING, "seeking", "Seeking capabilities and parameters", 0},
  {GST_QUERY_SEGMENT, "segment", "currently configured segment", 0},
  {GST_QUERY_CONVERT, "convert", "Converting between formats", 0},
  {GST_QUERY_FORMATS, "formats", "Supported formats for conversion", 0},
  {0, NULL, NULL, 0}
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
    standards->quark = g_quark_from_static_string (standards->nick);
    g_hash_table_insert (_nick_to_query, standards->nick, standards);
    g_hash_table_insert (_query_type_to_nick,
        GINT_TO_POINTER (standards->value), standards);

    _gst_queries = g_list_append (_gst_queries, standards);
    standards++;
    _n_values++;
  }
  g_static_mutex_unlock (&mutex);

  gst_query_get_type ();
}

/**
 * gst_query_type_get_name:
 * @query: the query type
 *
 * Get a printable name for the given query type. Do not modify or free.
 *
 * Returns: a reference to the static name of the query.
 */
const gchar *
gst_query_type_get_name (GstQueryType query)
{
  const GstQueryTypeDefinition *def;

  def = gst_query_type_get_details (query);

  return def->nick;
}

/**
 * gst_query_type_to_quark:
 * @query: the query type
 *
 * Get the unique quark for the given query type.
 *
 * Returns: the quark associated with the query type
 */
GQuark
gst_query_type_to_quark (GstQueryType query)
{
  const GstQueryTypeDefinition *def;

  def = gst_query_type_get_details (query);

  return def->quark;
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
 * already registered query with that nick
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
  query->quark = g_quark_from_static_string (query->nick);

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
 * Get the query type registered with @nick.
 *
 * Returns: The query registered with @nick or #GST_QUERY_NONE
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
 * @type: the #GstQueryType to find
 *
 * See if the given #GstQueryType is inside the @types query types array.
 *
 * Returns: TRUE if the type is found inside the array
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
 * @type: a #GstQueryType
 *
 * Get details about the given #GstQueryType.
 *
 * Returns: The #GstQueryTypeDefinition for @type or NULL on failure.
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
 * Get a #GstIterator of all the registered query types. The definitions 
 * iterated over are read only.
 *
 * Returns: A #GstIterator of #GstQueryTypeDefinition.
 */
GstIterator *
gst_query_type_iterate_definitions (void)
{
  GstIterator *result;

  g_static_mutex_lock (&mutex);
  /* FIXME: register a boxed type for GstQueryTypeDefinition */
  result = gst_iterator_new_list (G_TYPE_POINTER,
      g_static_mutex_get_mutex (&mutex), &_n_values, &_gst_queries,
      NULL, NULL, NULL);
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

/**
 * gst_query_new_position:
 * @format: the default #GstFormat for the new query
 *
 * Constructs a new query stream position query object. Use gst_query_unref()
 * when done with it. A position query is used to query the current position
 * of playback in the streams, in some format.
 *
 * Returns: A #GstQuery
 */
GstQuery *
gst_query_new_position (GstFormat format)
{
  GstQuery *query;
  GstStructure *structure;

  structure = gst_structure_new ("GstQueryPosition",
      "format", GST_TYPE_FORMAT, format,
      "cur", G_TYPE_INT64, (gint64) - 1, NULL);
  query = gst_query_new (GST_QUERY_POSITION, structure);

  return query;
}

/**
 * gst_query_set_position:
 * @query: a #GstQuery with query type GST_QUERY_POSITION
 * @format: the requested #GstFormat
 * @cur: the position to set
 *
 * Answer a position query by setting the requested value in the given format.
 */
void
gst_query_set_position (GstQuery * query, GstFormat format, gint64 cur)
{
  GstStructure *structure;

  g_return_if_fail (GST_QUERY_TYPE (query) == GST_QUERY_POSITION);

  structure = gst_query_get_structure (query);
  gst_structure_set (structure,
      "format", GST_TYPE_FORMAT, format, "cur", G_TYPE_INT64, cur, NULL);
}

/**
 * gst_query_parse_position:
 * @query: a #GstQuery
 * @format: the storage for the #GstFormat of the position values (may be NULL)
 * @cur: the storage for the current position (may be NULL)
 *
 * Parse a position query, writing the format into @format, and the position
 * into @cur, if the respective parameters are non-NULL.
 */
void
gst_query_parse_position (GstQuery * query, GstFormat * format, gint64 * cur)
{
  GstStructure *structure;

  g_return_if_fail (GST_QUERY_TYPE (query) == GST_QUERY_POSITION);

  structure = gst_query_get_structure (query);
  if (format)
    *format = g_value_get_enum (gst_structure_get_value (structure, "format"));
  if (cur)
    *cur = g_value_get_int64 (gst_structure_get_value (structure, "cur"));
}


/**
 * gst_query_new_duration:
 * @format: the #GstFormat for this duration query
 *
 * Constructs a new stream duration query object to query in the given format. 
 * Use gst_query_unref() when done with it. A duration query will give the
 * total length of the stream.
 *
 * Returns: A #GstQuery
 */
GstQuery *
gst_query_new_duration (GstFormat format)
{
  GstQuery *query;
  GstStructure *structure;

  structure = gst_structure_new ("GstQueryDuration",
      "format", GST_TYPE_FORMAT, format,
      "duration", G_TYPE_INT64, (gint64) - 1, NULL);
  query = gst_query_new (GST_QUERY_DURATION, structure);

  return query;
}

/**
 * gst_query_set_duration:
 * @query: a #GstQuery
 * @format: the #GstFormat for the duration
 * @duration: the duration of the stream
 *
 * Answer a duration query by setting the requested value in the given format.
 */
void
gst_query_set_duration (GstQuery * query, GstFormat format, gint64 duration)
{
  GstStructure *structure;

  g_return_if_fail (GST_QUERY_TYPE (query) == GST_QUERY_DURATION);

  structure = gst_query_get_structure (query);
  gst_structure_set (structure,
      "format", GST_TYPE_FORMAT, format,
      "duration", G_TYPE_INT64, duration, NULL);
}

/**
 * gst_query_parse_duration:
 * @query: a #GstQuery
 * @format: the storage for the #GstFormat of the duration value, or NULL.
 * @duration: the storage for the total duration, or NULL.
 *
 * Parse a duration query answer. Write the format of the duration into @format,
 * and the value into @duration, if the respective variables are non-NULL.
 */
void
gst_query_parse_duration (GstQuery * query, GstFormat * format,
    gint64 * duration)
{
  GstStructure *structure;

  g_return_if_fail (GST_QUERY_TYPE (query) == GST_QUERY_DURATION);

  structure = gst_query_get_structure (query);
  if (format)
    *format = g_value_get_enum (gst_structure_get_value (structure, "format"));
  if (duration)
    *duration =
        g_value_get_int64 (gst_structure_get_value (structure, "duration"));
}

/**
 * gst_query_new_convert:
 * @src_format: the source #GstFormat for the new query
 * @value: the value to convert
 * @dest_format: the target #GstFormat
 *
 * Constructs a new convert query object. Use gst_query_unref()
 * when done with it. A convert query is used to ask for a conversion between
 * one format and another.
 *
 * Returns: A #GstQuery
 */
GstQuery *
gst_query_new_convert (GstFormat src_format, gint64 value,
    GstFormat dest_format)
{
  GstQuery *query;
  GstStructure *structure;

  g_return_val_if_fail (value >= 0, NULL);

  structure = gst_structure_new ("GstQueryConvert",
      "src_format", GST_TYPE_FORMAT, src_format,
      "src_value", G_TYPE_INT64, value,
      "dest_format", GST_TYPE_FORMAT, dest_format,
      "dest_value", G_TYPE_INT64, (gint64) - 1, NULL);
  query = gst_query_new (GST_QUERY_CONVERT, structure);

  return query;
}

/**
 * gst_query_set_convert:
 * @query: a #GstQuery
 * @src_format: the source #GstFormat
 * @src_value: the source value
 * @dest_format: the destination #GstFormat
 * @dest_value: the destination value
 *
 * Answer a convert query by setting the requested values.
 */
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

/**
 * gst_query_parse_convert:
 * @query: a #GstQuery
 * @src_format: the storage for the #GstFormat of the source value, or NULL
 * @src_value: the storage for the source value, or NULL
 * @dest_format: the storage for the #GstFormat of the destination value, or NULL
 * @dest_value: the storage for the destination value, or NULL
 *
 * Parse a convert query answer. Any of @src_format, @src_value, @dest_format,
 * and @dest_value may be NULL, in which case that value is omitted.
 */
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

/**
 * gst_query_new_segment:
 * @format: the #GstFormat for the new query
 *
 * Constructs a new segment query object. Use gst_query_unref()
 * when done with it. A segment query is used to discover information about the
 * currently configured segment for playback.
 *
 * Returns: a #GstQuery
 */
GstQuery *
gst_query_new_segment (GstFormat format)
{
  GstQuery *query;
  GstStructure *structure;

  structure = gst_structure_new ("GstQuerySegment",
      "format", GST_TYPE_FORMAT, format, NULL);
  query = gst_query_new (GST_QUERY_SEGMENT, structure);

  return query;
}

/**
 * gst_query_set_segment:
 * @query: a #GstQuery
 * @rate: the rate of the segment
 * @format: the #GstFormat of the segment values (@start_value and @stop_value)
 * @start_value: the start value
 * @stop_value: the stop value
 *
 * Answer a segment query by setting the requested values.
 */
void
gst_query_set_segment (GstQuery * query, gdouble rate, GstFormat format,
    gint64 start_value, gint64 stop_value)
{
  GstStructure *structure;

  g_return_if_fail (GST_QUERY_TYPE (query) == GST_QUERY_SEGMENT);

  structure = gst_query_get_structure (query);
  gst_structure_set (structure,
      "rate", G_TYPE_DOUBLE, rate,
      "format", GST_TYPE_FORMAT, format,
      "start_value", G_TYPE_INT64, start_value,
      "stop_value", G_TYPE_INT64, stop_value, NULL);
}

/**
 * gst_query_parse_segment:
 * @query: a #GstQuery
 * @rate: the storage for the rate of the segment, or NULL
 * @format: the storage for the #GstFormat of the values, or NULL
 * @start_value: the storage for the start value, or NULL
 * @stop_value: the storage for the stop value, or NULL
 *
 * Parse a segment query answer. Any of @rate, @format, @start_value, and 
 * @stop_value may be NULL, which will cause this value to be omitted.
 */
void
gst_query_parse_segment (GstQuery * query, gdouble * rate, GstFormat * format,
    gint64 * start_value, gint64 * stop_value)
{
  GstStructure *structure;

  g_return_if_fail (GST_QUERY_TYPE (query) == GST_QUERY_SEGMENT);

  structure = gst_query_get_structure (query);
  if (rate)
    *rate = g_value_get_double (gst_structure_get_value (structure, "rate"));
  if (format)
    *format = g_value_get_enum (gst_structure_get_value (structure, "format"));
  if (start_value)
    *start_value =
        g_value_get_int64 (gst_structure_get_value (structure, "start_value"));
  if (stop_value)
    *stop_value =
        g_value_get_int64 (gst_structure_get_value (structure, "stop_value"));
}

/**
 * gst_query_new_application:
 * @type: the query type
 * @structure: a structure for the query
 *
 * Constructs a new custom application query object. Use gst_query_unref()
 * when done with it.
 *
 * Returns: a #GstQuery
 */
GstQuery *
gst_query_new_application (GstQueryType type, GstStructure * structure)
{
  g_return_val_if_fail (gst_query_type_get_details (type) != NULL, NULL);
  g_return_val_if_fail (structure != NULL, NULL);

  return gst_query_new (type, structure);
}

/**
 * gst_query_get_structure:
 * @query: a #GstQuery
 *
 * Get the structure of a query.
 *
 * Returns: The #GstStructure of the query. The structure is still owned
 * by the query and will therefore be freed when the query is unreffed.
 */
GstStructure *
gst_query_get_structure (GstQuery * query)
{
  g_return_val_if_fail (GST_IS_QUERY (query), NULL);

  return query->structure;
}

/**
 * gst_query_new_seeking (GstFormat *format)
 * @format: the default #GstFormat for the new query
 *
 * Constructs a new query object for querying seeking properties of
 * the stream. 
 *
 * Returns: A #GstQuery
 */
GstQuery *
gst_query_new_seeking (GstFormat format)
{
  GstQuery *query;
  GstStructure *structure;

  structure = gst_structure_new ("GstQuerySeeking",
      "format", GST_TYPE_FORMAT, format,
      "seekable", G_TYPE_BOOLEAN, FALSE,
      "segment-start", G_TYPE_INT64, (gint64) - 1,
      "segment-end", G_TYPE_INT64, (gint64) - 1, NULL);
  query = gst_query_new (GST_QUERY_SEEKING, structure);

  return query;
}

/**
 * gst_query_set_seeking:
 * @query: a #GstQuery
 * @format: the format to set for the @segment_start and @segment_end values
 * @seekable: the seekable flag to set
 * @segment_start: the segment_start to set
 * @segment_end: the segment_end to set
 *
 * Set the seeking query result fields in @query.
 */
void
gst_query_set_seeking (GstQuery * query, GstFormat format,
    gboolean seekable, gint64 segment_start, gint64 segment_end)
{
  GstStructure *structure;

  g_return_if_fail (GST_QUERY_TYPE (query) == GST_QUERY_SEEKING);

  structure = gst_query_get_structure (query);
  gst_structure_set (structure,
      "format", GST_TYPE_FORMAT, format,
      "seekable", G_TYPE_BOOLEAN, seekable,
      "segment-start", G_TYPE_INT64, segment_start,
      "segment-end", G_TYPE_INT64, segment_end, NULL);
}

/**
 * gst_query_parse_seeking:
 * @query: a GST_QUERY_SEEKING type query #GstQuery
 * @format: the format to set for the @segment_start and @segment_end values
 * @seekable: the seekable flag to set
 * @segment_start: the segment_start to set
 * @segment_end: the segment_end to set
 *
 * Parse a seeking query, writing the format into @format, and 
 * other results into the passed parameters, if the respective parameters
 * are non-NULL
 */
void
gst_query_parse_seeking (GstQuery * query, GstFormat * format,
    gboolean * seekable, gint64 * segment_start, gint64 * segment_end)
{
  GstStructure *structure;

  g_return_if_fail (GST_QUERY_TYPE (query) == GST_QUERY_SEEKING);

  structure = gst_query_get_structure (query);
  if (format)
    *format = g_value_get_enum (gst_structure_get_value (structure, "format"));
  if (seekable)
    *seekable =
        g_value_get_boolean (gst_structure_get_value (structure, "seekable"));
  if (segment_start)
    *segment_start =
        g_value_get_int64 (gst_structure_get_value (structure,
            "segment-start"));
  if (segment_end)
    *segment_end =
        g_value_get_int64 (gst_structure_get_value (structure, "segment-end"));
}

/**
 * gst_query_set_formats:
 * @query: a #GstQuery
 * @n_formats: the number of formats to set.
 * @...: A number of @GstFormats equal to @n_formats.
 *
 * Set the formats query result fields in @query. The number of formats passed
 * must be equal to @n_formats.
 */
void
gst_query_set_formats (GstQuery * query, gint n_formats, ...)
{
  va_list ap;
  GValue list = { 0, };
  GValue item = { 0, };
  GstStructure *structure;
  gint i;

  g_value_init (&list, GST_TYPE_LIST);

  va_start (ap, n_formats);

  for (i = 0; i < n_formats; i++) {
    g_value_init (&item, GST_TYPE_FORMAT);
    g_value_set_enum (&item, va_arg (ap, GstFormat));
    gst_value_list_append_value (&list, &item);
    g_value_unset (&item);
  }

  va_end (ap);

  structure = gst_query_get_structure (query);
  gst_structure_set_value (structure, "formats", &list);
}
