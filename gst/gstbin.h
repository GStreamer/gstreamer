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


#ifndef __GST_BIN_H__
#define __GST_BIN_H__

#include <gst/gstelement.h>
#include <gst/cothreads.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

extern GstElementDetails gst_bin_details;

#define GST_TYPE_BIN \
  (gst_bin_get_type())
#define GST_BIN(obj) \
  (GTK_CHECK_CAST((obj),GST_TYPE_BIN,GstBin))
#define GST_BIN_CLASS(klass) \
  (GTK_CHECK_CLASS_CAST((klass),GST_TYPE_BIN,GstBinClass))
#define GST_IS_BIN(obj) \
  (GTK_CHECK_TYPE((obj),GST_TYPE_BIN))
#define GST_IS_BIN_CLASS(obj) \
  (GTK_CHECK_CLASS_TYPE((klass),GST_TYPE_BIN))

#define GST_BIN_FLAG_LAST (GST_ELEMENT_FLAG_LAST + 2)

typedef struct _GstBin GstBin;
typedef struct _GstBinClass GstBinClass;

struct _GstBin {
  GstElement element;

  // our children
  gint numchildren;
  GList *children;

  // iteration state
  gboolean need_cothreads;
  GList *entries;
  gint numentries;

  cothread_context *threadcontext;
  gboolean use_cothreads;
  GList *outside_schedules;
};

struct _GstBinClass {
  GstElementClass parent_class;

  void 		(*object_added) 	(GstObject *object, GstObject *child);

  /* change the state of elements of the given type */
  gboolean 	(*change_state_type) 	(GstBin *bin,
                                 	 GstElementState state,
                                 	 GtkType type);

  /* create a plan for the execution of the bin */
  void 		(*create_plan) 		(GstBin *bin);

  /* run a full iteration of operation */
  void		(*iterate) 		(GstBin *bin);
};

/* this struct is used for odd scheduling cases */
typedef struct __GstBinOutsideSchedule {
  guint32 flags;
  GstElement *element;
  GstBin *bin;
  cothread_state *threadstate;
  GSList *padlist;
} _GstBinOutsideSchedule;



GtkType 	gst_bin_get_type		(void);
GstElement*	gst_bin_new			(gchar *name);
#define 	gst_bin_destroy(bin) 		gst_object_destroy(GST_OBJECT(bin))

/* add and remove elements from the bin */
void 		gst_bin_add			(GstBin *bin, GstElement *element);
void 		gst_bin_remove			(GstBin *bin, GstElement *element);

/* retrieve a single element or the list of children */
GstElement*	gst_bin_get_by_name		(GstBin *bin, gchar *name);
GList*		gst_bin_get_list		(GstBin *bin);

void 		gst_bin_create_plan		(GstBin *bin);
gboolean 	gst_bin_set_state_type		(GstBin *bin,
                                		 GstElementState state,
                                		 GtkType type);

void 		gst_bin_iterate			(GstBin *bin);

// hack FIXME
void 		gst_bin_use_cothreads		(GstBin *bin, gboolean enabled);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_BIN_H__ */     

