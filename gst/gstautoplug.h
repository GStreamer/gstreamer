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


#ifndef __GST_AUTOPLUG_H__
#define __GST_AUTOPLUG_H__

#include "gstelement.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define GST_TYPE_AUTOPLUG \
  (gst_object_get_type())
#define GST_AUTOPLUG(obj) \
  (GTK_CHECK_CAST((obj),GST_TYPE_AUTOPLUG,GstAutoplug))
#define GST_AUTOPLUG_CLASS(klass) \
  (GTK_CHECK_CLASS_CAST((klass),GST_TYPE_AUTOPLUG,GstAutoplugClass))
#define GST_IS_AUTOPLUG(obj) \
  (GTK_CHECK_TYPE((obj),GST_TYPE_AUTOPLUG))
#define GST_IS_AUTOPLUG_CLASS(obj) \
  (GTK_CHECK_CLASS_TYPE((klass),GST_TYPE_AUTOPLUG))

typedef struct _GstAutoplug GstAutoplug;
typedef struct _GstAutoplugClass GstAutoplugClass;

#define GST_AUTOPLUG_MAX_COST 999999

typedef guint   (*GstAutoplugCostFunction) (gpointer src, gpointer dest, gpointer data);
typedef GList*  (*GstAutoplugListFunction) (gpointer data);

struct _GstAutoplug {
  GtkObject object;
};

struct _GstAutoplugClass {
  GtkObjectClass parent_class;
};

GtkType 	gst_autoplug_get_type			(void);

GList* 		gst_autoplug_caps 			(GstCaps *srccaps, GstCaps *sinkcaps);


#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_AUTOPLUG_H__ */     

