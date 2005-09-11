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
#include <gst/gstiterator.h>
#include <gst/gstbus.h>

G_BEGIN_DECLS

GST_EXPORT GType _gst_bin_type;

#define GST_TYPE_BIN             (_gst_bin_type)
#define GST_IS_BIN(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_BIN))
#define GST_IS_BIN_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_BIN))
#define GST_BIN_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_BIN, GstBinClass))
#define GST_BIN(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_BIN, GstBin))
#define GST_BIN_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_BIN, GstBinClass))
#define GST_BIN_CAST(obj)        ((GstBin*)(obj))

/**
 * GstBinFlags:
 * @GST_BIN_FLAG_LAST: the last enum in the series of flags for bins.
 * Derived classes can use this as first value in a list of flags.
 *
 * GstBinFlags are a set of flags specific to bins. Most are set/used
 * internally. They can be checked using the GST_FLAG_IS_SET () macro,
 * and (un)set using GST_FLAG_SET () and GST_FLAG_UNSET ().
 */
typedef enum {
  /* padding */
  GST_BIN_FLAG_LAST		= GST_ELEMENT_FLAG_LAST + 5
} GstBinFlags;

typedef struct _GstBin GstBin;
typedef struct _GstBinClass GstBinClass;

/**
 * GST_BIN_NUMCHILDREN:
 * @bin: the bin to get the number of children from
 *
 * Gets the number of children a bin manages.
 */
#define GST_BIN_NUMCHILDREN(bin)	(GST_BIN_CAST(bin)->numchildren)
/**
 * GST_BIN_CHILDREN:
 * @bin: the bin to get the list with children from
 *
 * Gets the list with children a bin manages.
 */
#define GST_BIN_CHILDREN(bin)		(GST_BIN_CAST(bin)->children)
/**
 * GST_BIN_CHILDREN_COOKIE:
 * @bin: the bin to get the children cookie from
 *
 * Gets the children cookie that watches the children list.
 */
#define GST_BIN_CHILDREN_COOKIE(bin)	(GST_BIN_CAST(bin)->children_cookie)

struct _GstBin {
  GstElement	 element;

  /*< public >*/ /* with LOCK */
  /* our children, subclass are supposed to update these
   * fields to reflect their state with _iterate_*() */
  gint		 numchildren;
  GList		*children;
  guint32	 children_cookie;

  GstBus        *child_bus;	/* Bus we set on our children */
  GList         *eosed;         /* list of elements that posted EOS */

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

struct _GstBinClass {
  GstElementClass parent_class;

  /*< private >*/
  /* signals */
  void		(*element_added)	(GstBin *bin, GstElement *child);
  void		(*element_removed)	(GstBin *bin, GstElement *child);

  /*< public >*/
  /* virtual methods for subclasses */
  gboolean	(*add_element)		(GstBin *bin, GstElement *element);
  gboolean	(*remove_element)	(GstBin *bin, GstElement *element);

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

GType		gst_bin_get_type		(void);
GstElement*	gst_bin_new			(const gchar *name);

/* add and remove elements from the bin */
gboolean	gst_bin_add			(GstBin *bin, GstElement *element);
gboolean	gst_bin_remove			(GstBin *bin, GstElement *element);

/* retrieve a single child */
GstElement*	gst_bin_get_by_name		 (GstBin *bin, const gchar *name);
GstElement*	gst_bin_get_by_name_recurse_up	 (GstBin *bin, const gchar *name);
GstElement*	gst_bin_get_by_interface	 (GstBin *bin, GType interface);

/* retrieve multiple children */
GstIterator*    gst_bin_iterate_elements	 (GstBin *bin);
GstIterator*    gst_bin_iterate_recurse		 (GstBin *bin);

GstIterator*	gst_bin_iterate_sinks		 (GstBin *bin);
GstIterator*	gst_bin_iterate_all_by_interface (GstBin *bin, GType interface);

G_END_DECLS


#endif /* __GST_BIN_H__ */
