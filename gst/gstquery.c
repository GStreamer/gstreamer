/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wim.taymans@chello.be>
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

#include "gstlog.h"

#include "gstquery.h"

static GList *_gst_queries = NULL;
static GHashTable *_nick_to_query = NULL;
static GHashTable *_query_type_to_nick = NULL;
static gint _n_values = 1;	/* we start from 1 because 0 reserved for NONE */

static GstQueryTypeDefinition standard_definitions[] = {
  {GST_QUERY_TOTAL, "total", "Total length"},
  {GST_QUERY_POSITION, "position", "Current Position"},
  {GST_QUERY_LATENCY, "latency", "Latency"},
  {GST_QUERY_JITTER, "jitter", "Jitter"},
  {GST_QUERY_START, "start", "Start position of stream"},
  {GST_QUERY_SEGMENT_END, "segment_end", "End position of the stream"},
  {GST_QUERY_RATE, "rate", "Configured rate 1000000 = 1"},
  {0, NULL, NULL}
};

void
_gst_query_type_initialize (void)
{
  GstQueryTypeDefinition *standards = standard_definitions;

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

  g_hash_table_insert (_nick_to_query, query->nick, query);
  g_hash_table_insert (_query_type_to_nick, GINT_TO_POINTER (query->value),
      query);
  _gst_queries = g_list_append (_gst_queries, query);
  _n_values++;

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

  query = g_hash_table_lookup (_nick_to_query, nick);

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
  return g_hash_table_lookup (_query_type_to_nick, GINT_TO_POINTER (type));
}

/**
 * gst_query_type_get_definitions:
 *
 * Get a list of all the registered query types.
 *
 * Returns: A GList of #GstQueryTypeDefinition.
 */
const GList *
gst_query_type_get_definitions (void)
{
  return _gst_queries;
}
