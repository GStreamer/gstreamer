/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wim.taymans@chello.be>
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

G_BEGIN_DECLS typedef enum
{
  GST_QUERY_NONE = 0,
  GST_QUERY_TOTAL,
  GST_QUERY_POSITION,
  GST_QUERY_LATENCY,
  GST_QUERY_JITTER,
  GST_QUERY_START,
  GST_QUERY_SEGMENT_END,
  GST_QUERY_RATE
}
GstQueryType;

/* rate is relative to 1000000  */
#define GST_QUERY_TYPE_RATE_DEN          G_GINT64_CONSTANT (1000000)

typedef struct _GstQueryTypeDefinition GstQueryTypeDefinition;

struct _GstQueryTypeDefinition
{
  GstQueryType value;
  gchar *nick;
  gchar *description;
};

#ifdef G_HAVE_ISO_VARARGS
#define GST_QUERY_TYPE_FUNCTION(type, functionname, ...)  	\
static const GstQueryType*                           	\
functionname (type object)                         	\
{                                                       \
  static const GstQueryType types[] = {              	\
    __VA_ARGS__,                                        \
    0                                              	\
  };                                                    \
  return types;                                         \
}
#elif defined(G_HAVE_GNUC_VARARGS)
#define GST_QUERY_TYPE_FUNCTION(type, functionname, a...) 	\
static const GstQueryType*                           	\
functionname (type object)                          	\
{                                                       \
  static const GstQueryType types[] = {              	\
    a,                                                  \
    0                                              	\
  };                                                    \
  return types;                                         \
}
#endif

void _gst_query_type_initialize (void);

/* register a new query */
GstQueryType gst_query_type_register (const gchar * nick,
    const gchar * description);
GstQueryType gst_query_type_get_by_nick (const gchar * nick);

/* check if a query is in an array of querys */
gboolean gst_query_types_contains (const GstQueryType * types,
    GstQueryType type);

/* query for query details */
G_CONST_RETURN GstQueryTypeDefinition *gst_query_type_get_details (GstQueryType
    type);
G_CONST_RETURN GList *gst_query_type_get_definitions (void);

G_END_DECLS
#endif /* __GST_QUERY_H__ */
