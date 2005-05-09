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


#include <glib.h>

#include <gst/gstformat.h>
#include <gst/gstquery.h>


GstQuery* gst_query_new_position        (GstFormat format);
void gst_query_set_position		(GstQuery *query, GstFormat format,
                                         gint64 cur, gint64 end);
void gst_query_parse_position_query	(GstQuery *query, GstFormat *format);
void gst_query_parse_position_response	(GstQuery *query, GstFormat *format,
                                         gint64 *cur, gint64 *end);
void gst_query_parse_seeking_query	(GstQuery *query, GstFormat *format);
void gst_query_set_seeking		(GstQuery *query, GstFormat format,
                                         gboolean seekable, gint64 segment_start,
                                         gint64 segment_end);
void gst_query_parse_seeking_response	(GstQuery *query, GstFormat *format,
                                         gboolean *seekable, 
                                         gint64 *segment_start,
                                         gint64 *segment_end);
void gst_query_set_formats		(GstQuery *query, gint n_formats, ...);
