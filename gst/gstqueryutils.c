/* GStreamer
 * Copyright (C) 2005 Andy Wingo <wingo@pobox.com>
 *
 * gstqueryutils.c: Utility functions for creating and parsing GstQueries.
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


#include <stdarg.h>

#include "gstqueryutils.h"

/* some macros are just waiting to be defined here */


void
gst_query_set_position (GstQuery * query, GstFormat format, gint64 cur,
    gint64 end)
{
  GstStructure *structure;

  g_return_if_fail (GST_QUERY_TYPE (query) == GST_QUERY_POSITION);

  structure = gst_query_get_structure (query);
  gst_structure_set (structure,
      "format", GST_TYPE_FORMAT, format,
      "cur", G_TYPE_INT64, cur, "end", G_TYPE_INT64, end, NULL);
}

void
gst_query_parse_position_query (GstQuery * query, GstFormat * format)
{
  GstStructure *structure;

  g_return_if_fail (GST_QUERY_TYPE (query) == GST_QUERY_POSITION);

  structure = gst_query_get_structure (query);
  *format = g_value_get_enum (gst_structure_get_value (structure, "format"));
}

void
gst_query_parse_position_response (GstQuery * query, GstFormat * format,
    gint64 * cur, gint64 * end)
{
  GstStructure *structure;

  g_return_if_fail (GST_QUERY_TYPE (query) == GST_QUERY_POSITION);

  structure = gst_query_get_structure (query);
  *format = g_value_get_enum (gst_structure_get_value (structure, "format"));
  *cur = g_value_get_int64 (gst_structure_get_value (structure, "cur"));
  *end = g_value_get_int64 (gst_structure_get_value (structure, "end"));
}

void
gst_query_parse_seeking_query (GstQuery * query, GstFormat * format)
{
  GstStructure *structure;

  g_return_if_fail (GST_QUERY_TYPE (query) == GST_QUERY_SEEKING);

  structure = gst_query_get_structure (query);
  *format = g_value_get_enum (gst_structure_get_value (structure, "format"));
}

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

void
gst_query_parse_seeking_response (GstQuery * query, GstFormat * format,
    gboolean * seekable, gint64 * segment_start, gint64 * segment_end)
{
  GstStructure *structure;

  g_return_if_fail (GST_QUERY_TYPE (query) == GST_QUERY_SEEKING);

  structure = gst_query_get_structure (query);
  *format = g_value_get_enum (gst_structure_get_value (structure, "format"));
  *seekable = g_value_get_boolean (gst_structure_get_value
      (structure, "seekable"));
  *segment_start = g_value_get_int64 (gst_structure_get_value
      (structure, "segment-start"));
  *segment_end = g_value_get_int64 (gst_structure_get_value
      (structure, "segment-end"));
}

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
