/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstbin.h: Header for GstBin container object
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

typedef enum {
  /* this bin is a manager of child elements, i.e. a pipeline or thread */
  GST_BIN_FLAG_MANAGER		= GST_ELEMENT_FLAG_LAST,

  /* we prefer to have cothreads when its an option, over chain-based */
  GST_BIN_FLAG_PREFER_COTHREADS,

  /* padding */
  GST_BIN_FLAG_LAST		= GST_ELEMENT_FLAG_LAST + 4,
} GstBinFlags;

typedef struct _GstBin GstBin;
typedef struct _GstBinClass GstBinClass;
typedef struct __GstBinChain _GstBinChain;

struct _GstBin {
  GstElement element;

  /* our children */
  gint numchildren;
  GList *children;
  gint num_eos_providers;
  GList *eos_providers;
  GCond *eoscond;

  /* iteration state */
  gboolean need_cothreads;
  GList *managed_elements;
  gint num_managed_elements;

  GList *chains;
  gint num_chains;
  GList *entries;
  gint num_entries;

  cothread_context *threadcontext;
  gboolean use_cothreads;
};

struct _GstBinClass {
  GstElementClass parent_class;

  /* signals */
  void		(*object_added)		(GstObject *object, GstObject *child);
  void		(*object_removed)	(GstObject *object, GstObject *child);

  /* change the state of elements of the given type */
  gboolean	(*change_state_type)	(GstBin *bin,
					 GstElementState state,
					 GtkType type);
  /* create a plan for the execution of the bin */
  void		(*create_plan)		(GstBin *bin);
  void		(*schedule)		(GstBin *bin);
  /* run a full iteration of operation */
  gboolean	(*iterate)		(GstBin *bin);
};

struct __GstBinChain {
  GList *elements;
  gint num_elements;

  GList *entries;

  gboolean need_cothreads;
  gboolean need_scheduling;
};


GtkType		gst_bin_get_type		(void);
GstElement*	gst_bin_new			(const gchar *name);
#define		gst_bin_destroy(bin)		gst_object_destroy(GST_OBJECT(bin))

void		gst_bin_set_element_manager	(GstElement *element, GstElement *manager);
void		gst_bin_add_managed_element	(GstBin *bin, GstElement *element);
void		gst_bin_remove_managed_element	(GstBin *bin, GstElement *element);

/* add and remove elements from the bin */
void		gst_bin_add			(GstBin *bin,
						 GstElement *element);
void		gst_bin_remove			(GstBin *bin,
						 GstElement *element);

/* retrieve a single element or the list of children */
GstElement*	gst_bin_get_by_name		(GstBin *bin,
						 const gchar *name);
GstElement*	gst_bin_get_by_name_recurse_up	(GstBin *bin,
						 const gchar *name);
GList*		gst_bin_get_list		(GstBin *bin);

void		gst_bin_create_plan		(GstBin *bin);
void		gst_bin_schedule		(GstBin *bin);
gboolean	gst_bin_set_state_type		(GstBin *bin,
						 GstElementState state,
						 GtkType type);

gboolean	gst_bin_iterate			(GstBin *bin);

/* hack FIXME */
void		gst_bin_use_cothreads		(GstBin *bin,
						 gboolean enabled);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_BIN_H__ */

