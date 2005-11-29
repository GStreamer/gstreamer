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
 * @GST_QUERY_POSITION: current position in stream
 * @GST_QUERY_DURATION: total duration of the stream
 * @GST_QUERY_LATENCY: latency of stream
 * @GST_QUERY_JITTER: current jitter of stream
 * @GST_QUERY_RATE: current rate of the stream
 * @GST_QUERY_SEEKING: seeking capabilities
 * @GST_QUERY_SEGMENT: segment start/stop positions
 * @GST_QUERY_CONVERT: convert values between formats
 * @GST_QUERY_FORMATS: query supported formats for convert
 *
 * Standard predefined Query types
 */
/* NOTE: don't forget to update the table in gstquery.c when changing
 * this enum */
typedef enum {
  GST_QUERY_NONE = 0,
  GST_QUERY_POSITION,
  GST_QUERY_DURATION,
  GST_QUERY_LATENCY,
  GST_QUERY_JITTER, 	/* not in draft-query, necessary? */
  GST_QUERY_RATE,
  GST_QUERY_SEEKING,
  GST_QUERY_SEGMENT,
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
 * @quark: the quark for the nick
 *
 * A Query Type definition
 */
struct _GstQueryTypeDefinition
{
  GstQueryType   value;
  gchar     	*nick;
  gchar     	*description;
  GQuark         quark;
};

#define GST_TYPE_QUERY				(gst_query_get_type())
#define GST_IS_QUERY(obj)                      (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_QUERY))
#define GST_IS_QUERY_CLASS(klass)              (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_QUERY))
#define GST_QUERY_GET_CLASS(obj)               (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_QUERY, GstQueryClass))
#define GST_QUERY(obj)                         (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_QUERY, GstQuery))
#define GST_QUERY_CLASS(klass)                 (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_QUERY, GstQueryClass))

#define GST_QUERY_TYPE(query)  (((GstQuery*)(query))->type)

/**
 * GstQuery:
 * @mini_object: The parent #GstMiniObject type
 * @type: the #GstQueryType
 * @structure: the #GstStructure containing the query details.
 *
 * The #GstQuery structure.
 */
struct _GstQuery
{
  GstMiniObject mini_object;

  /*< public > *//* with COW */
  GstQueryType type;

  GstStructure *structure;

  /*< private > */
  gpointer _gst_reserved;
};

struct _GstQueryClass {
  GstMiniObjectClass mini_object_class;

  /*< private > */
  gpointer _gst_reserved[GST_PADDING];
};

void            _gst_query_initialize          (void);

const gchar*    gst_query_type_get_name        (GstQueryType query);
GQuark          gst_query_type_to_quark        (GstQueryType query);

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
/**
 * gst_query_ref:
 * @q: a #GstQuery to increase the refcount of.
 *
 * Increases the refcount of the given query by one.
 */
#define         gst_query_ref(q)		GST_QUERY (gst_mini_object_ref (GST_MINI_OBJECT (q)))
/**
 * gst_query_unref:
 * @q: a #GstQuery to decrease the refcount of.
 *
 * Decreases the refcount of the query. If the refcount reaches 0, the query
 * will be freed.
 */
#define         gst_query_unref(q)		gst_mini_object_unref (GST_MINI_OBJECT (q))

/* copy query */
/**
 * gst_query_copy:
 * @q: a #GstQuery to copy.
 *
 * Copies the given query using the copy function of the parent #GstData
 * structure.
 */
#define         gst_query_copy(q)		GST_QUERY (gst_mini_object_copy (GST_MINI_OBJECT (q)))
/**
 * gst_query_make_writable:
 * @q: a #GstQuery to make writable
 *
 * Makes a writable query from the given query.
 */
#define         gst_query_make_writable(q)	GST_QUERY (gst_mini_object_make_writable (GST_MINI_OBJECT (q)))

/* position query */
GstQuery*	gst_query_new_position		(GstFormat format);
void		gst_query_set_position		(GstQuery *query, GstFormat format, gint64 cur);
void		gst_query_parse_position	(GstQuery *query, GstFormat *format, gint64 *cur);

/* duration query */
GstQuery*	gst_query_new_duration		(GstFormat format);
void		gst_query_set_duration		(GstQuery *query, GstFormat format, gint64 duration);
void		gst_query_parse_duration	(GstQuery *query, GstFormat *format, gint64 *duration);

/* convert query */
GstQuery*	gst_query_new_convert		(GstFormat src_format, gint64 value, GstFormat dest_format);
void		gst_query_set_convert		(GstQuery *query, GstFormat src_format, gint64 src_value,
						 GstFormat dest_format, gint64 dest_value);
void		gst_query_parse_convert		(GstQuery *query, GstFormat *src_format, gint64 *src_value,
						 GstFormat *dest_format, gint64 *dest_value);
/* segment query */
GstQuery*       gst_query_new_segment           (GstFormat format);
void            gst_query_set_segment           (GstQuery *query, gdouble rate, GstFormat format,
                                                 gint64 start_value, gint64 stop_value);
void            gst_query_parse_segment         (GstQuery *query, gdouble *rate, GstFormat *format,
                                                 gint64 *start_value, gint64 *stop_value);


/* application specific query */
GstQuery *	gst_query_new_application 	(GstQueryType type,
                                                 GstStructure *structure);


GstStructure *  gst_query_get_structure		(GstQuery *query);

/* moved from old gstqueryutils.h */
GstQuery*       gst_query_new_seeking           (GstFormat format);
void            gst_query_set_seeking           (GstQuery *query, GstFormat format,
                                                 gboolean seekable,
                                                 gint64 segment_start,
                                                 gint64 segment_end);
void            gst_query_parse_seeking         (GstQuery *query, GstFormat *format,
                                                 gboolean *seekable,
                                                 gint64 *segment_start,
                                                 gint64 *segment_end);

void            gst_query_set_formats           (GstQuery *query, gint n_formats, ...);

G_END_DECLS

#endif /* __GST_QUERY_H__ */

