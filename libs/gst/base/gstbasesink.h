/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstbasesink.h:
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

#ifndef __GST_BASE_SINK_H__
#define __GST_BASE_SINK_H__

#include <gst/gst.h>

G_BEGIN_DECLS


#define GST_TYPE_BASE_SINK		(gst_base_sink_get_type())
#define GST_BASE_SINK(obj)		(G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_BASE_SINK,GstBaseSink))
#define GST_BASE_SINK_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_BASE_SINK,GstBaseSinkClass))
#define GST_BASE_SINK_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_BASE_SINK, GstBaseSinkClass))
#define GST_IS_BASE_SINK(obj)		(G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_BASE_SINK))
#define GST_IS_BASE_SINK_CLASS(obj)	(G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_BASE_SINK))
#define GST_BASE_SINK_CAST(obj)		((GstBaseSink *) (obj))

/**
 * GST_BASE_SINK_PAD:
 * @obj: base sink instance
 *
 * Gives the pointer to the #GstPad object of the element.
 */
#define GST_BASE_SINK_PAD(obj)		(GST_BASE_SINK_CAST (obj)->sinkpad)

typedef struct _GstBaseSink GstBaseSink;
typedef struct _GstBaseSinkClass GstBaseSinkClass;

/**
 * GstBaseSink:
 *
 * The opaque #GstBaseSink data structure.
 */
struct _GstBaseSink {
  GstElement	 element;

  /*< protected >*/
  GstPad	*sinkpad;
  GstActivateMode	pad_mode;

  /*< protected >*/ /* with LOCK */
  guint64	 offset;
  gboolean	 can_activate_pull;
  gboolean	 can_activate_push;

  /*< protected >*/ /* with PREROLL_LOCK */
  GQueue	*preroll_queue;
  gint		 preroll_queue_max_len;
  gint		 preroll_queued;
  gint		 buffers_queued;
  gint		 events_queued;
  gboolean       eos;
  gboolean       eos_queued;
  gboolean       need_preroll;
  gboolean       have_preroll;
  gboolean       playing_async;

  /*< protected >*/ /* with STREAM_LOCK */
  gboolean	 have_newsegment;
  GstSegment     segment;

  /*< private >*/ /* with LOCK */
  GstClockID     clock_id;
  GstClockTime   end_time;
  gboolean       sync;
  gboolean       flushing;

  /*< private >*/
  gpointer       _gst_reserved[GST_PADDING_LARGE];
};

struct _GstBaseSinkClass {
  GstElementClass parent_class;

  /* get caps from subclass */
  GstCaps*      (*get_caps)     (GstBaseSink *sink);
  /* notify subclass of new caps */
  gboolean      (*set_caps)     (GstBaseSink *sink, GstCaps *caps);

  /* allocate a new buffer with given caps */
  GstFlowReturn (*buffer_alloc) (GstBaseSink *sink, guint64 offset, guint size,
		                 GstCaps *caps, GstBuffer **buf);

  /* get the start and end times for syncing on this buffer */
  void		(*get_times)    (GstBaseSink *sink, GstBuffer *buffer,
		                 GstClockTime *start, GstClockTime *end);

  /* start and stop processing, ideal for opening/closing the resource */
  gboolean      (*start)        (GstBaseSink *sink);
  gboolean      (*stop)         (GstBaseSink *sink);

  /* unlock any pending access to the resource. subclasses should unlock
   * any function ASAP. */
  gboolean      (*unlock)       (GstBaseSink *sink);

  /* notify subclass of event, preroll buffer or real buffer */
  gboolean      (*event)        (GstBaseSink *sink, GstEvent *event);
  GstFlowReturn (*preroll)      (GstBaseSink *sink, GstBuffer *buffer);
  GstFlowReturn (*render)       (GstBaseSink *sink, GstBuffer *buffer);

  /*< private >*/
  gpointer       _gst_reserved[GST_PADDING_LARGE];
};

GType gst_base_sink_get_type(void);

G_END_DECLS

#endif /* __GST_BASE_SINK_H__ */
