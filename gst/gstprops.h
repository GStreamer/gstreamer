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


#ifndef __GST_PROPS_H__
#define __GST_PROPS_H__

#include <glib.h>

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
} GstPropsId;

#define GST_PROPS_LIST_ID GINT_TO_POINTER(GST_PROPS_LIST_ID_NUM)
#define GST_PROPS_INT_ID GINT_TO_POINTER(GST_PROPS_INT_ID_NUM)
#define GST_PROPS_INT_RANGE_ID GINT_TO_POINTER(GST_PROPS_INT_RANGE_ID_NUM)
#define GST_PROPS_FOURCC_ID GINT_TO_POINTER(GST_PROPS_FOURCC_ID_NUM)
#define GST_PROPS_BOOL_ID GINT_TO_POINTER(GST_PROPS_BOOL_ID_NUM)

#define GST_PROPS_LIST(a...) GST_PROPS_LIST_ID,##a,NULL
#define GST_PROPS_INT(a) GST_PROPS_INT_ID,(GINT_TO_POINTER(a))
#define GST_PROPS_INT_RANGE(a,b) GST_PROPS_INT_RANGE_ID,(GINT_TO_POINTER(a)),(GINT_TO_POINTER(b))
#define GST_PROPS_FOURCC(a,b,c,d) GST_PROPS_FOURCC_ID,(GINT_TO_POINTER((a)|(b)<<8|(c)<<16|(d)<<24))
#define GST_PROPS_FOURCC_INT(a) GST_PROPS_FOURCC_ID,(GINT_TO_POINTER(a))
#define GST_PROPS_BOOLEAN(a) GST_PROPS_BOOL_ID,(GINT_TO_POINTER(a))


struct _GstProps {
  GSList *properties;		/* properties for this capability */
};

/* initialize the subsystem */
void 		_gst_props_initialize		(void);

GstProps*	gst_props_register		(GstPropsFactory factory);

void		gst_props_dump			(GstProps *props);

gboolean 	gst_props_check_compatibility 	(GstProps *props1, GstProps *props2);

#endif /* __GST_PROPS_H__ */
