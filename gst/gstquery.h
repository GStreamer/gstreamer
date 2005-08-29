/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wim.taymans@chello.be>
 *                    2005 Wim Taymans <wim@fluendo.com>
 *
 * gstquery.h: GstQuery API declaration
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


#ifndef __GST_QUERY_H__
#define __GST_QUERY_H__

#include <glib.h>

#include <gst/gstiterator.h>
#include <gst/gstminiobject.h>
#include <gst/gststructure.h>
#include <gst/gstformat.h>

G_BEGIN_DECLS

/**
 * GstQueryType:
 * @GST_QUERY_NONE: invalid query type
 * @GST_QUERY_POSITION: current/end position in stream
 * @GST_QUERY_LATENCY: latency of stream
 * @GST_QUERY_JITTER: current jitter of stream
 * @GST_QUERY_RATE: current rate of the stream
 * @GST_QUERY_SEEKING: seeking start/stop positions
 * @GST_QUERY_CONVERT: convert values
 * @GST_QUERY_FORMATS: query supported formats for convert
 *
 * Standard predefined Query types
 */
typedef enum {
  GST_QUERY_NONE = 0,
  GST_QUERY_POSITION,
  GST_QUERY_LATENCY,
  GST_QUERY_JITTER, 	/* not in draft-query, necessary? */
  GST_QUERY_RATE,
  GST_QUERY_SEEKING,
  GST_QUERY_CONVERT,
  GST_QUERY_FORMATS
} GstQueryType;

/**
 * GST_QUERY_TYPE_RATE_DEN:
 *
 * Rates are relative to this value
 */
#define GST_QUERY_TYPE_RATE_DEN          G_GINT64_CONSTANT (1000000)

typedef struct _GstQueryTypeDefinition GstQueryTypeDefinition;
typedef struct _GstQuery GstQuery;
typedef struct _GstQueryClass GstQueryClass;

/**
 * GstQueryTypeDefinition:
 * @value: the unique id of the Query type
 * @nick: a short nick
 * @description: a longer description of the query type
 *
 * A Query Type definition
 */
struct _GstQueryTypeDefinition
{
  GstQueryType   value;
  gchar     	*nick;
  gchar     	*description;
};

#define GST_TYPE_QUERY				(gst_query_get_type())
#define GST_IS_QUERY(obj)                      (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_QUERY))
#define GST_IS_QUERY_CLASS(klass)              (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_QUERY))
#define GST_QUERY_GET_CLASS(obj)               (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_QUERY, GstQueryClass))
#define GST_QUERY(obj)                         (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_QUERY, GstQuery))
#define GST_QUERY_CLASS(klass)                 (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_QUERY, GstQueryClass))

#define GST_QUERY_TYPE(query)  (((GstQuery*)(query))->type)

struct _GstQuery
{
  GstMiniObject mini_object;

  /*< public > */
  GstQueryType type;

  GstStructure *structure;
  
  /*< private > */
  gpointer _gst_reserved[GST_PADDING];
};

struct _GstQueryClass {
  GstMiniObjectClass mini_object_class;
  
  /*< private > */
  gpointer _gst_reserved[GST_PADDING];
};

void            _gst_query_initialize          (void);
GType		gst_query_get_type             (void);

/* register a new query */
GstQueryType    gst_query_type_register        (const gchar *nick, 
                                                const gchar *description);
GstQueryType    gst_query_type_get_by_nick     (const gchar *nick);

/* check if a query is in an array of querys */
gboolean        gst_query_types_contains       (const GstQueryType *types,
                                                GstQueryType type);

/* query for query details */

G_CONST_RETURN GstQueryTypeDefinition*      
                gst_query_type_get_details         (GstQueryType type);
GstIterator*    gst_query_type_iterate_definitions (void);

/* refcounting */
#define         gst_query_ref(msg)		GST_QUERY (gst_mini_object_ref (GST_MINI_OBJECT (msg)))
#define         gst_query_unref(msg)		gst_mini_object_unref (GST_MINI_OBJECT (msg))
/* copy query */
#define         gst_query_copy(msg)		GST_QUERY (gst_mini_object_copy (GST_MINI_OBJECT (msg)))
#define         gst_query_make_writable(msg)	GST_QUERY (gst_mini_object_make_writable (GST_MINI_OBJECT (msg)))

/* position query */
GstQuery*	gst_query_new_position		(GstFormat format);
void		gst_query_set_position		(GstQuery *query, GstFormat format,
						 gint64 cur, gint64 end);
void		gst_query_parse_position	(GstQuery *query, GstFormat *format,
						 gint64 *cur, gint64 *end);
/* convert query */
GstQuery*	gst_query_new_convert		(GstFormat src_fmt, gint64 value, GstFormat dest_fmt);
void		gst_query_set_convert		(GstQuery *query, GstFormat src_format, gint64 src_value,
						 GstFormat dest_format, gint64 dest_value);
void		gst_query_parse_convert		(GstQuery *query, GstFormat *src_format, gint64 *src_value,
						 GstFormat *dest_format, gint64 *dest_value);

/* application specific query */
GstQuery *	gst_query_new_application 	(GstQueryType type,
                                                 GstStructure *structure);


GstStructure *  gst_query_get_structure		(GstQuery *query);

G_END_DECLS

#endif /* __GST_QUERY_H__ */

