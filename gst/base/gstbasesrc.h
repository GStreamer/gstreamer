/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2005 Wim Taymans <wim@fluendo.com>
 *
 * gstbasesrc.h: 
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

#ifndef __GST_BASESRC_H__
#define __GST_BASESRC_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_BASESRC  		(gst_basesrc_get_type())
#define GST_BASESRC(obj)  		(G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_BASESRC,GstBaseSrc))
#define GST_BASESRC_CLASS(klass) 	(G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_BASESRC,GstBaseSrcClass))
#define GST_BASESRC_GET_CLASS(obj)      (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_BASESRC, GstBaseSrcClass))
#define GST_IS_BASESRC(obj)  		(G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_BASESRC))
#define GST_IS_BASESRC_CLASS(obj)  	(G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_BASESRC))

typedef enum {
  GST_BASESRC_STARTED           = GST_ELEMENT_FLAG_LAST,

  GST_BASESRC_FLAG_LAST         = GST_ELEMENT_FLAG_LAST + 2
} GstFileSrcFlags;


typedef struct _GstBaseSrc GstBaseSrc;
typedef struct _GstBaseSrcClass GstBaseSrcClass;

struct _GstBaseSrc {
  GstElement     element;

  GstPad 	*srcpad;

  gint 		 blocksize;

  gint64	 segment_start;
  gint64	 segment_end;
  gboolean	 segment_loop;

  gboolean	 has_loop;
  gboolean	 has_getrange;

  gboolean       seekable;
  guint64 	 offset;
  guint64        size;
};

struct _GstBaseSrcClass {
  GstElementClass parent_class;

  GstCaps*      (*get_caps)     (GstBaseSrc *src);
  gboolean      (*set_caps)     (GstBaseSrc *src, GstCaps *caps);

  gboolean      (*start)        (GstBaseSrc *src);
  gboolean      (*stop)         (GstBaseSrc *src);

  void          (*get_times)    (GstBaseSrc *src, GstBuffer *buffer,
                                 GstClockTime *start, GstClockTime *end);

  gboolean      (*get_size)     (GstBaseSrc *src, guint64 *size);

  gboolean      (*is_seekable)  (GstBaseSrc *src);
  gboolean      (*unlock)       (GstBaseSrc *src);

  gboolean      (*event)        (GstBaseSrc *src, GstEvent *event);
  GstFlowReturn (*create)       (GstBaseSrc *src, guint64 offset, guint size, 
		                 GstBuffer **buf);
};

GType gst_basesrc_get_type(void);

G_END_DECLS

#endif /* __GST_BASESRC_H__ */
