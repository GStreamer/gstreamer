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

#ifndef __GST_BASE_SRC_H__
#define __GST_BASE_SRC_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_BASE_SRC		(gst_base_src_get_type())
#define GST_BASE_SRC(obj)		(G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_BASE_SRC,GstBaseSrc))
#define GST_BASE_SRC_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_BASE_SRC,GstBaseSrcClass))
#define GST_BASE_SRC_GET_CLASS(obj)     (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_BASE_SRC, GstBaseSrcClass))
#define GST_IS_BASE_SRC(obj)		(G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_BASE_SRC))
#define GST_IS_BASE_SRC_CLASS(obj)	(G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_BASE_SRC))
#define GST_BASE_SRC_CAST(obj)		((GstBaseSrc *)(obj))

/**
 * GstBaseSrcFlags:
 * @GST_BASE_SRC_STARTED: has source been started
 * @GST_BASE_SRC_FLAG_LAST: offset to define more flags
 *
 * The #GstElement flags that a basesrc element may have.
 */
typedef enum {
  GST_BASE_SRC_STARTED           = (GST_ELEMENT_FLAG_LAST << 0),
  /* padding */
  GST_BASE_SRC_FLAG_LAST         = (GST_ELEMENT_FLAG_LAST << 2)
} GstBaseSrcFlags;

typedef struct _GstBaseSrc GstBaseSrc;
typedef struct _GstBaseSrcClass GstBaseSrcClass;

/**
 * GST_BASE_SRC_PAD:
 * @obj: base source instance
 *
 * Gives the pointer to the #GstPad object of the element.
 */
#define GST_BASE_SRC_PAD(obj)                 (GST_BASE_SRC_CAST (obj)->srcpad)


/**
 * GstBaseSrc:
 *
 * The opaque #GstBaseSrc data structure.
 */
struct _GstBaseSrc {
  GstElement     element;

  /*< protected >*/
  GstPad	*srcpad;

  /* available to subclass implementations */
  /* MT-protected (with LIVE_LOCK) */
  GMutex	*live_lock;
  GCond		*live_cond;
  gboolean	 is_live;
  gboolean	 live_running;

  /* MT-protected (with LOCK) */
  gint		 blocksize;	/* size of buffers when operating push based */
  gboolean	 can_activate_push;	/* some scheduling properties */
  GstActivateMode pad_mode;
  gboolean       seekable;
  gboolean       random_access;

  GstClockID     clock_id;	/* for syncing */
  GstClockTime   end_time;

  /* MT-protected (with STREAM_LOCK) */
  GstSegment     segment;
  gboolean	 need_newsegment;

  guint64	 offset;	/* current offset in the resource */
  guint64        size;		/* total size of the resource */

  gint           num_buffers;
  gint           num_buffers_left;

  /*< private >*/
  gpointer       _gst_reserved[GST_PADDING_LARGE];
};

/**
 * _GstBaseSrcClass:
 * @create: ask the subclass to create a buffer with offset and size
 * @start: start processing
 */
struct _GstBaseSrcClass {
  GstElementClass parent_class;

  /*< public >*/
  /* virtual methods for subclasses */

  /* get caps from subclass */
  GstCaps*      (*get_caps)     (GstBaseSrc *src);
  /* notify the subclass of new caps */
  gboolean      (*set_caps)     (GstBaseSrc *src, GstCaps *caps);

  /* decide on caps */
  gboolean      (*negotiate)    (GstBaseSrc *src);

  /* generate and send a newsegment */
  gboolean      (*newsegment)   (GstBaseSrc *src);

  /* start and stop processing, ideal for opening/closing the resource */
  gboolean      (*start)        (GstBaseSrc *src);
  gboolean      (*stop)         (GstBaseSrc *src);

  /* given a buffer, return start and stop time when it should be pushed
   * out. The base class will sync on the clock using these times. */
  void          (*get_times)    (GstBaseSrc *src, GstBuffer *buffer,
                                 GstClockTime *start, GstClockTime *end);

  /* get the total size of the resource in bytes */
  gboolean      (*get_size)     (GstBaseSrc *src, guint64 *size);

  /* check if the resource is seekable */
  gboolean      (*is_seekable)  (GstBaseSrc *src);
  /* unlock any pending access to the resource. subclasses should unlock
   * any function ASAP. */
  gboolean      (*unlock)       (GstBaseSrc *src);

  /* notify subclasses of an event */
  gboolean      (*event)        (GstBaseSrc *src, GstEvent *event);

  /* ask the subclass to create a buffer with offset and size */
  GstFlowReturn (*create)       (GstBaseSrc *src, guint64 offset, guint size,
		                 GstBuffer **buf);

  /*< private >*/
  gpointer       _gst_reserved[GST_PADDING_LARGE];
};

GType gst_base_src_get_type (void);

void		gst_base_src_set_live	(GstBaseSrc *src, gboolean live);
gboolean	gst_base_src_is_live	(GstBaseSrc *src);

G_END_DECLS

#endif /* __GST_BASE_SRC_H__ */
