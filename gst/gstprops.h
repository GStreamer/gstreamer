/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstprops.h: Header for properties subsystem
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


#ifndef __GST_PROPS_H__
#define __GST_PROPS_H__

#include <glib.h>
#include <parser.h> // NOTE: this is xml-config's fault

// Include compatability defines: if libxml hasn't already defined these,
// we have an old version 1.x
#ifndef xmlChildrenNode
#define xmlChildrenNode childs
#define xmlRootNode root
#endif


typedef struct _GstProps GstProps;
typedef gpointer GstPropsFactoryEntry;
typedef GstPropsFactoryEntry GstPropsFactory[];
typedef GstPropsFactory *GstPropsListFactory[];

typedef enum {
   GST_PROPS_END_ID_NUM = 0,
   GST_PROPS_LIST_ID_NUM,
   GST_PROPS_INT_ID_NUM,
   GST_PROPS_INT_RANGE_ID_NUM,
   GST_PROPS_FOURCC_ID_NUM,
   GST_PROPS_BOOL_ID_NUM,
   GST_PROPS_LAST_ID_NUM = GST_PROPS_END_ID_NUM + 16,
} GstPropsId;

#define GST_PROPS_END_ID 	GINT_TO_POINTER(GST_PROPS_END_ID_NUM)
#define GST_PROPS_LIST_ID 	GINT_TO_POINTER(GST_PROPS_LIST_ID_NUM)
#define GST_PROPS_INT_ID 	GINT_TO_POINTER(GST_PROPS_INT_ID_NUM)
#define GST_PROPS_INT_RANGE_ID 	GINT_TO_POINTER(GST_PROPS_INT_RANGE_ID_NUM)
#define GST_PROPS_FOURCC_ID 	GINT_TO_POINTER(GST_PROPS_FOURCC_ID_NUM)
#define GST_PROPS_BOOL_ID 	GINT_TO_POINTER(GST_PROPS_BOOL_ID_NUM)
#define GST_PROPS_LAST_ID 	GINT_TO_POINTER(GST_PROPS_LAST_ID_NUM)

#define GST_PROPS_LIST(a...) 		GST_PROPS_LIST_ID,##a,NULL
#define GST_PROPS_INT(a) 		GST_PROPS_INT_ID,(GINT_TO_POINTER(a))
#define GST_PROPS_INT_RANGE(a,b) 	GST_PROPS_INT_RANGE_ID,(GINT_TO_POINTER(a)),(GINT_TO_POINTER(b))
#define GST_PROPS_FOURCC(a,b,c,d) 	GST_PROPS_FOURCC_ID,(GINT_TO_POINTER((a)|(b)<<8|(c)<<16|(d)<<24))
#define GST_PROPS_FOURCC_INT(a) 	GST_PROPS_FOURCC_ID,(GINT_TO_POINTER(a))
#define GST_PROPS_BOOLEAN(a) 		GST_PROPS_BOOL_ID,(GINT_TO_POINTER(a))


struct _GstProps {
  gint refcount;

  GList *properties;		/* real properties for this property */
};

/* initialize the subsystem */
void 		_gst_props_initialize		(void);

GstProps*	gst_props_register		(GstPropsFactory factory);
GstProps*	gst_props_register_count	(GstPropsFactory factory, guint *counter);

GstProps*	gst_props_new			(GstPropsFactoryEntry entry, ...);

void            gst_props_unref                 (GstProps *props);
void            gst_props_ref                   (GstProps *props);
void            gst_props_destroy               (GstProps *props);

GstProps*       gst_props_copy                  (GstProps *props);
GstProps*       gst_props_copy_on_write         (GstProps *props);

GstProps*	gst_props_merge			(GstProps *props, GstProps *tomerge);

gboolean 	gst_props_check_compatibility 	(GstProps *fromprops, GstProps *toprops);

GstProps*	gst_props_set			(GstProps *props, const gchar *name, GstPropsFactoryEntry entry, ...);

gint 		gst_props_get_int		(GstProps *props, const gchar *name);
gulong		gst_props_get_fourcc_int	(GstProps *props, const gchar *name);
gboolean	gst_props_get_boolean		(GstProps *props, const gchar *name);

xmlNodePtr 	gst_props_save_thyself 		(GstProps *props, xmlNodePtr parent);
GstProps* 	gst_props_load_thyself 		(xmlNodePtr parent);

#endif /* __GST_PROPS_H__ */
