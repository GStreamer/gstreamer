/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2001 Dominic Ludlam <dom@recoil.org>
 *
 * gstmultifilesrc.h: 
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

#ifndef __GST_MULTIFILESRC_H__
#define __GST_MULTIFILESRC_H__

#include <gst/gst.h>

G_BEGIN_DECLS


#define GST_TYPE_MULTIFILESRC \
  (gst_multifilesrc_get_type())
#define GST_MULTIFILESRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MULTIFILESRC,GstMultiFileSrc))
#define GST_MULTIFILESRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MULTIFILESRC,GstMultiFileSrcClass))
#define GST_IS_MULTIFILESRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MULTIFILESRC))
#define GST_IS_MULTIFILESRC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MULTIFILESRC))

typedef enum {
  GST_MULTIFILESRC_OPEN		= GST_ELEMENT_FLAG_LAST,

  GST_MULTIFILESRC_FLAG_LAST	= GST_ELEMENT_FLAG_LAST + 2
} GstMultiFileSrcFlags;

typedef struct _GstMultiFileSrc GstMultiFileSrc;
typedef struct _GstMultiFileSrcClass GstMultiFileSrcClass;

struct _GstMultiFileSrc {
  GstElement element;
  /* pads */
  GstPad *srcpad;

  /* current file details */
  gchar  *currentfilename;
  GSList *listptr;

  /* mapping parameters */
  gint fd;
  gulong size;    /* how long is the file? */
  guchar *map;    /* where the file is mapped to */

  gboolean new_seek;
};

struct _GstMultiFileSrcClass {
  GstElementClass parent_class;

  void (*new_file)  (GstMultiFileSrc *multifilesrc, gchar *newfilename);
};

GType gst_multifilesrc_get_type(void);

G_END_DECLS

#endif /* __GST_MULTIFILESRC_H__ */
