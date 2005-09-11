/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wim.taymans@chello.be>
 *                    2005 Wim Taymans <wim@fluendo.com>
 *
 * gstevent.h: Header for GstEvent subsystem
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


#ifndef __GST_EVENT_H__
#define __GST_EVENT_H__

#include <gst/gstminiobject.h>
#include <gst/gstformat.h>
#include <gst/gstobject.h>
#include <gst/gstclock.h>
#include <gst/gststructure.h>
#include <gst/gsttaglist.h>

G_BEGIN_DECLS

/* bitmaks defining the direction */
#define GST_EVDIR_US	(1 << 0)	
#define GST_EVDIR_DS	(1 << 1)	
#define GST_EVDIR_BOTH	GST_EVDIR_US | GST_EVDIR_DS
/* mask defining event is serialized with data */
#define GST_EVSER	(1 << 2)	
#define GST_EVSHIFT	4	

/* when making custom event types, use this macro with the num and
 * the given flags */
#define GST_EVENT_MAKE_TYPE(num,flags) (((num) << GST_EVSHIFT) | (flags))

/**
 * GstEventType:
 * @GST_EVENT_UNKNOWN: unknown event.
 * @GST_EVENT_FLUSH_START: Start a flush operation
 * @GST_EVENT_FLUSH_STOP: Stop a flush operation
 * @GST_EVENT_EOS: End-Of-Stream. No more data is to be expected to follow without
 * a NEWSEGMENT event.
 * @GST_EVENT_NEWSEGMENT: A new media segment follows in the dataflow.
 * @GST_EVENT_TAG: A new set of metadata tags has been found in the stream.
 * @GST_EVENT_FILLER: Filler for sparse data streams.
 * @GST_EVENT_QOS: A quality message. Used to indicate to upstream elements that the downstream elements
 * are being starved of or flooded with data.
 * @GST_EVENT_SEEK: A request for a new playback position and rate.
 * @GST_EVENT_NAVIGATION: Navigation events are usually used for communicating user
 * requests, such as mouse or keyboard movements, to upstream elements.
 * @GST_EVENT_CUSTOM_UP: Upstream custom event
 * @GST_EVENT_CUSTOM_DS: Downstream custom event that travels in the data flow.
 * @GST_EVENT_CUSTOM_DS_OOB: Custom out-of-band downstream event.
 * @GST_EVENT_CUSTOM_BOTH: Custom upstream or downstream event. In-band when travelling downstream.
 * @GST_EVENT_CUSTOM_BOTH_OOB: Custom upstream or downstream out-of-band event.
 *
 * GstEventType lists the standard event types that can be sent in a pipeline. 
 *
 * The custom event types can be used for private messages between elements that can't be expressed using normal
 * GStreamer buffer passing semantics. Custom events carry an arbitrary GstStructure. 
 * Specific custom events are distinguished by the name of the structure.
 */
typedef enum {
  GST_EVENT_UNKNOWN		= GST_EVENT_MAKE_TYPE (0, 0),
  /* bidirectional events */
  GST_EVENT_FLUSH_START		= GST_EVENT_MAKE_TYPE (1, GST_EVDIR_BOTH),
  GST_EVENT_FLUSH_STOP		= GST_EVENT_MAKE_TYPE (2, GST_EVDIR_BOTH),
  /* downstream serialized events */
  GST_EVENT_EOS			= GST_EVENT_MAKE_TYPE (3, GST_EVDIR_DS | GST_EVSER),
  GST_EVENT_NEWSEGMENT		= GST_EVENT_MAKE_TYPE (4, GST_EVDIR_DS | GST_EVSER),
  GST_EVENT_TAG			= GST_EVENT_MAKE_TYPE (5, GST_EVDIR_DS | GST_EVSER),
  GST_EVENT_FILLER		= GST_EVENT_MAKE_TYPE (6, GST_EVDIR_DS | GST_EVSER),
  /* upstream events */
  GST_EVENT_QOS			= GST_EVENT_MAKE_TYPE (7, GST_EVDIR_US),
  GST_EVENT_SEEK		= GST_EVENT_MAKE_TYPE (8, GST_EVDIR_US),
  GST_EVENT_NAVIGATION		= GST_EVENT_MAKE_TYPE (9, GST_EVDIR_US),

  /* custom events start here */
  GST_EVENT_CUSTOM_UP		= GST_EVENT_MAKE_TYPE (32, GST_EVDIR_US),
  GST_EVENT_CUSTOM_DS		= GST_EVENT_MAKE_TYPE (32, GST_EVDIR_DS | GST_EVSER),
  GST_EVENT_CUSTOM_DS_OOB	= GST_EVENT_MAKE_TYPE (32, GST_EVDIR_DS),
  GST_EVENT_CUSTOM_BOTH		= GST_EVENT_MAKE_TYPE (32, GST_EVDIR_BOTH | GST_EVSER),
  GST_EVENT_CUSTOM_BOTH_OOB	= GST_EVENT_MAKE_TYPE (32, GST_EVDIR_BOTH)
} GstEventType;

/**
 * GST_EVENT_TRACE_NAME:
 *
 * The name used for memory allocation tracing 
 */
#define GST_EVENT_TRACE_NAME	"GstEvent"

typedef struct _GstEvent GstEvent;
typedef struct _GstEventClass GstEventClass;

#define GST_TYPE_EVENT		        (gst_event_get_type())
#define GST_IS_EVENT(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_EVENT))
#define GST_IS_EVENT_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_EVENT))
#define GST_EVENT_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_EVENT, GstEventClass))
#define GST_EVENT(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_EVENT, GstEvent))
#define GST_EVENT_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_EVENT, GstEventClass))

/**
 * GST_EVENT_TYPE:
 * @event: the event to query
 *
 * Get the #GstEventType of the event.
 */
#define GST_EVENT_TYPE(event)		(GST_EVENT(event)->type)

/**
 * GST_EVENT_TIMESTAMP:
 * @event: the event to query
 *
 * Get the #GstClockTime timestamp of the event.
 */
#define GST_EVENT_TIMESTAMP(event)	(GST_EVENT(event)->timestamp)

/**
 * GST_EVENT_SRC:
 * @event: the event to query
 *
 * The source #GstObject that generated this event.
 */
#define GST_EVENT_SRC(event)		(GST_EVENT(event)->src)

#define GST_EVENT_IS_UPSTREAM(ev)	!!(GST_EVENT_TYPE (ev) & GST_EVDIR_US)
#define GST_EVENT_IS_DOWNSTREAM(ev)	!!(GST_EVENT_TYPE (ev) & GST_EVDIR_DS)
#define GST_EVENT_IS_SERIALIZED(ev)	!!(GST_EVENT_TYPE (ev) & GST_EVSER)

/**
 * GstSeekType:
 * @GST_SEEK_TYPE_NONE: no change in position is required
 * @GST_SEEK_TYPE_CUR: change relative to current position
 * @GST_SEEK_TYPE_SET: absolute position is requested
 * @GST_SEEK_TYPE_END: relative position to duration is requested
 *
 * The different types of seek events. When constructing a seek event a format,
 * a seek method and optional flags are OR-ed together. The seek event is then
 * inserted into the graph with #gst_pad_send_event() or
 * #gst_element_send_event().
 */
typedef enum {
  /* one of these */
  GST_SEEK_TYPE_NONE		= 0,
  GST_SEEK_TYPE_CUR		= 1,
  GST_SEEK_TYPE_SET		= 2,
  GST_SEEK_TYPE_END		= 3
} GstSeekType;

/**
 * GstSeekFlags:
 * @GST_SEEK_FLAG_NONE: no flag
 * @GST_SEEK_FLAG_FLUSH: flush pipeline
 * @GST_SEEK_FLAG_ACCURATE: accurate position is requested, this might
 *                     be slower for some formats.
 * @GST_SEEK_FLAG_KEY_UNIT: seek to the nearest keyframe. This might be
 * 		       faster but less accurate.
 * @GST_SEEK_FLAG_SEGMENT: perform a segment seek. After the playback
 *            of the segment completes, no EOS will be emmited but a
 *            SEGMENT_DONE message will be posted on the bus.
 */
typedef enum {
  GST_SEEK_FLAG_NONE		= 0,
  GST_SEEK_FLAG_FLUSH		= (1 << 0),
  GST_SEEK_FLAG_ACCURATE	= (1 << 1),
  GST_SEEK_FLAG_KEY_UNIT	= (1 << 2),
  GST_SEEK_FLAG_SEGMENT		= (1 << 3)
} GstSeekFlags;

struct _GstEvent {
  GstMiniObject mini_object;

  /*< public >*/ /* with COW */
  GstEventType  type;
  guint64	timestamp;
  GstObject	*src;

  GstStructure	*structure;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

struct _GstEventClass {
  GstMiniObjectClass mini_object_class;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

void		_gst_event_initialize		(void);
	
GType		gst_event_get_type		(void);

/* refcounting */
/**
 * gst_event_ref:
 * @ev: The event to refcount
 *
 * Increase the refcount of this event.
 */
#define         gst_event_ref(ev)		GST_EVENT (gst_mini_object_ref (GST_MINI_OBJECT (ev)))
/**
 * gst_event_ref:
 * @ev: The event to refcount
 *
 * Decrease the refcount of an event, freeing it if the refcount reaches 0.
 */
#define         gst_event_unref(ev)		gst_mini_object_unref (GST_MINI_OBJECT (ev))

/* copy event */
/**
 * gst_event_copy:
 * @ev: The event to copy
 *
 * Copy the event using the event specific copy function.
 */
#define         gst_event_copy(ev)		GST_EVENT (gst_mini_object_copy (GST_MINI_OBJECT (ev)))

/* custom event */
GstEvent*	gst_event_new_custom		(GstEventType type, GstStructure *structure);

const GstStructure *  
		gst_event_get_structure 	(GstEvent *event);

/* flush events */
GstEvent *	gst_event_new_flush_start	(void);
GstEvent *	gst_event_new_flush_stop	(void);

/* EOS event */
GstEvent *	gst_event_new_eos		(void);

/* newsegment events */
GstEvent*	gst_event_new_newsegment	(gdouble rate, GstFormat format,
                                                 gint64 start_value, gint64 stop_value,
						 gint64 base);
void		gst_event_parse_newsegment	(GstEvent *event, gdouble *rate, GstFormat *format, 
						 gint64 *start_value, gint64 *stop_value, gint64 *base);
/* tag event */
GstEvent*	gst_event_new_tag		(GstTagList *taglist);
void		gst_event_parse_tag		(GstEvent *event, GstTagList **taglist);

/* filler event */
/* FIXME: FILLER events need to be fully specified and implemented */
GstEvent *	gst_event_new_filler		(void);


/* QOS events */
/* FIXME: QOS events need to be fully specified and implemented */
GstEvent*	gst_event_new_qos		(gdouble proportion, GstClockTimeDiff diff,
						 GstClockTime timestamp);
void		gst_event_parse_qos		(GstEvent *event, gdouble *proportion, GstClockTimeDiff *diff,
						 GstClockTime *timestamp);
/* seek event */
GstEvent*	gst_event_new_seek		(gdouble rate, GstFormat format, GstSeekFlags flags,
						 GstSeekType cur_type, gint64 cur, 
						 GstSeekType stop_type, gint64 stop);
void		gst_event_parse_seek		(GstEvent *event, gdouble *rate, GstFormat *format, 
		                                 GstSeekFlags *flags,
						 GstSeekType *cur_type, gint64 *cur, 
						 GstSeekType *stop_type, gint64 *stop);
/* navigation event */
GstEvent*	gst_event_new_navigation	(GstStructure *structure);

G_END_DECLS

#endif /* __GST_EVENT_H__ */
