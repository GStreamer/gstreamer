/* Gnome-Streamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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


#ifndef __GST_TYPE_H__
#define __GST_TYPE_H__


#include <gst/gstbuffer.h>
#include <gst/gstelement.h>


/* type of function used to check a stream for equality with type */
typedef gboolean (*GstTypeFindFunc) (GstBuffer *buf,gpointer priv);

typedef struct _GstType GstType;
typedef struct _GstTypeFactory GstTypeFactory;

struct _GstType {
  guint16 id;			/* type id (assigned) */

  gchar *mime;			/* MIME type */
  gchar *exts;			/* space-delimited list of extensions */

  GstTypeFindFunc typefindfunc;	/* typefind function */

  GList *srcs;			/* list of src objects for this type */
  GList *sinks;			/* list of sink objects for type */

  GHashTable *converters;       /* a hashtable of factories that can convert
				   from this type to destination type. The
				   factories are indexed by destination type */
};

struct _GstTypeFactory {
  gchar *mime;
  gchar *exts;
  GstTypeFindFunc typefindfunc;
};


/* initialize the subsystem */
void _gst_type_initialize();

/* create a new type, or find/merge an existing one */
guint16 gst_type_register(GstTypeFactory *factory);

/* look up a type by mime or extension */
guint16 gst_type_find_by_mime(gchar *mime);
guint16 gst_type_find_by_ext(gchar *ext);

/* add src or sink object */
void gst_type_add_src(guint16 id,GstElementFactory *src);
void gst_type_add_sink(guint16 id,GstElementFactory *sink);
/* get list of src or sink objects */
GList *gst_type_get_srcs(guint16 id);
GList *gst_type_get_sinks(guint16 id);

/* get GstType by id */
GstType *gst_type_find_by_id(guint16 id);

GList *gst_type_get_sink_to_src(guint16 sinkid, guint16 srcid);

/* get the list of registered types (returns list of GstType!) */
GList *gst_type_get_list();

void gst_type_dump();

xmlNodePtr gst_type_save_thyself(GstType *type, xmlNodePtr parent);
guint16 gst_type_load_thyself(xmlNodePtr parent);

xmlNodePtr gst_typefactory_save_thyself(GstTypeFactory *factory, xmlNodePtr parent);
GstTypeFactory *gst_typefactory_load_thyself(xmlNodePtr parent);

#endif /* __GST_TYPE_H__ */
