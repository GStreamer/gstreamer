/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstsrc.h: Header for GstSrc element (depracated)
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


#ifndef __GST_SRC_H__
#define __GST_SRC_H__

#include <gst/gstelement.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define GST_TYPE_SRC \
  (gst_src_get_type())
#define GST_SRC(obj) \
  (GTK_CHECK_CAST((obj),GST_TYPE_SRC,GstSrc))
#define GST_SRC_CLASS(klass) \
  (GTK_CHECK_CLASS_CAST((klass),GST_TYPE_SRC,GstSrcClass))
#define GST_IS_SRC(obj) \
  (GTK_CHECK_TYPE((obj),GST_TYPE_SRC))
#define GST_IS_SRC_CLASS(obj) \
  (GTK_CHECK_CLASS_TYPE((klass),GST_TYPE_SRC))

typedef enum {
  GST_SRC_ASYNC		= GST_ELEMENT_FLAG_LAST,

  GST_SRC_FLAG_LAST	= GST_ELEMENT_FLAG_LAST +2,
} GstSrcFlags;

typedef struct _GstSrc 		GstSrc;
typedef struct _GstSrcClass 	GstSrcClass;

#define GST_SRC_IS_ASYNC(obj) (GST_FLAG_IS_SET(obj,GST_SRC_ASYNC))

struct _GstSrc {
  GstElement			element;
};

struct _GstSrcClass {
  GstElementClass parent_class;

  /* signals */
  void (*eos) 		(GstSrc *src);
};

GtkType 	gst_src_get_type		(void);

void 		gst_src_signal_eos		(GstSrc *src);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_SRC_H__ */
