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


#ifndef __GST_CAPS_H__
#define __GST_CAPS_H__

#include <gst/gst.h>

typedef struct _GstCaps GstCaps;
typedef gpointer GstCapsFactoryEntry;
typedef GstCapsFactoryEntry GstCapsFactory[];

typedef enum {
   GST_CAPS_END_ID_NUM = 0,
   GST_CAPS_LIST_ID_NUM,
   GST_CAPS_INT_ID_NUM,
   GST_CAPS_INT_RANGE_ID_NUM,
   GST_CAPS_INT32_ID_NUM,
   GST_CAPS_BOOL_ID_NUM,
} GstCapsId;

#define GST_CAPS_LIST_ID GINT_TO_POINTER(GST_CAPS_LIST_ID_NUM)
#define GST_CAPS_INT_ID GINT_TO_POINTER(GST_CAPS_INT_ID_NUM)
#define GST_CAPS_INT_RANGE_ID GINT_TO_POINTER(GST_CAPS_INT_RANGE_ID_NUM)
#define GST_CAPS_INT32_ID GINT_TO_POINTER(GST_CAPS_INT32_ID_NUM)
#define GST_CAPS_BOOL_ID GINT_TO_POINTER(GST_CAPS_BOOL_ID_NUM)

#define GST_CAPS_LIST(a...) GST_CAPS_LIST_ID,##a,NULL
#define GST_CAPS_INT(a) GST_CAPS_INT_ID,(GINT_TO_POINTER(a))
#define GST_CAPS_INT_RANGE(a,b) GST_CAPS_INT_RANGE_ID,(GINT_TO_POINTER(a)),(GINT_TO_POINTER(b))
#define GST_CAPS_INT32(a) GST_CAPS_INT_ID,(GINT_TO_POINTER(a))
#define GST_CAPS_BOOLEAN(a) GST_CAPS_BOOL_ID,(GINT_TO_POINTER(a))


struct _GstCaps {
  guint16 id;			/* type id (major type) */

  GSList *properties;		/* properties for this capability */
};

/* initialize the subsystem */
void 		_gst_caps_initialize		(void);

GstCaps*	gst_caps_register		(GstCapsFactory factory);
GList*		gst_caps_register_va		(GstCapsFactory factory,...);

void		gst_caps_dump			(GstCaps *caps);

gboolean 	gst_caps_check_compatibility 	(GstCaps *caps1, GstCaps *caps2);

#endif /* __GST_CAPS_H__ */
