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

G_BEGIN_DECLS

GST_EXPORT GType _gst_bin_type;

#define GST_TYPE_BIN             (_gst_bin_type)
#define GST_IS_BIN(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_BIN))
#define GST_IS_BIN_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_BIN))
#define GST_BIN_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_BIN, GstBinClass))
#define GST_BIN(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_BIN, GstBin))
#define GST_BIN_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_BIN, GstBinClass))

/**
 * GstBinFlags:
 * @GST_BIN_FLAG_MANAGER: This bin has a scheduler and can be used as a toplevel bin.
 * @GST_BIN_SELF_SCHEDULABLE: This bin iterates itself, so no calls to gst_bin_iterate() should be made.
 * @GST_BIN_FLAG_PREFER_COTHREADS: This bin preferes to have its elements scheduled with cothreads
 * @GST_BIN_FLAG_FIXED_CLOCK: This bin uses a fixed clock, possibly the one set with gst_bin_use_clock().
 * @GST_BIN_FLAG_LAST: id of last for chaining flsg definitions
 * 
 * Flags for a #GstBin .
 */
typedef enum {
  GST_BIN_FLAG_MANAGER		= GST_ELEMENT_FLAG_LAST,
  GST_BIN_SELF_SCHEDULABLE,
  GST_BIN_FLAG_PREFER_COTHREADS,
  GST_BIN_FLAG_FIXED_CLOCK,
  /* padding */
  GST_BIN_FLAG_LAST		= GST_ELEMENT_FLAG_LAST + 5
} GstBinFlags;

/*typedef struct _GstBin GstBin; */
/*typedef struct _GstBinClass GstBinClass; */

struct _GstBin {
  GstElement 	 element;

  /* our children */
  gint 		 numchildren;
  GList 	*children;

  GstElementState child_states[GST_NUM_STATES];

  gpointer _gst_reserved[GST_PADDING];
};

struct _GstBinClass {
  GstElementClass parent_class;

  /* vtable */
  void		(*add_element)		(GstBin *bin, GstElement *element);
  void		(*remove_element)	(GstBin *bin, GstElement *element);
  void		(*child_state_change)	(GstBin *bin, GstElementState oldstate, 
					 GstElementState newstate, GstElement *element);

  /* run a full iteration of operation */
  gboolean	(*iterate)		(GstBin *bin);

  /* signals */
  void		(*element_added)	(GstBin *bin, GstElement *child);
  void		(*element_removed)	(GstBin *bin, GstElement *child);

  gpointer _gst_reserved[GST_PADDING];
};

GType		gst_bin_get_type		(void);
GstElement*	gst_bin_new			(const gchar *name);

/* add and remove elements from the bin */
void		gst_bin_add			(GstBin *bin, GstElement *element);
void		gst_bin_add_many 		(GstBin *bin, GstElement *element_1, ...);
void		gst_bin_remove			(GstBin *bin, GstElement *element);
void		gst_bin_remove_many		(GstBin *bin, GstElement *element_1, ...);

/* retrieve a single element or the list of children */
GstElement*	gst_bin_get_by_name		(GstBin *bin, const gchar *name);
GstElement*	gst_bin_get_by_name_recurse_up	(GstBin *bin, const gchar *name);
G_CONST_RETURN GList*
		gst_bin_get_list		(GstBin *bin);
GstElement*	gst_bin_get_by_interface	(GstBin *bin, GType interface);
GList *		gst_bin_get_all_by_interface	(GstBin *bin, GType interface);

gboolean	gst_bin_iterate			(GstBin *bin);

void		gst_bin_use_clock		(GstBin *bin, GstClock *clock);
GstClock*	gst_bin_get_clock		(GstBin *bin);
void		gst_bin_auto_clock		(GstBin *bin);

GstElementStateReturn gst_bin_sync_children_state (GstBin *bin);

/* internal */
/* one of our childs signaled a state change */
void 		gst_bin_child_state_change 		(GstBin *bin, GstElementState oldstate, 
							 GstElementState newstate, GstElement *child);

G_END_DECLS


#endif /* __GST_BIN_H__ */
