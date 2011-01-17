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

/**
 * GstEventTypeFlags:
 * @GST_EVENT_TYPE_UPSTREAM:   Set if the event can travel upstream.
 * @GST_EVENT_TYPE_DOWNSTREAM: Set if the event can travel downstream.
 * @GST_EVENT_TYPE_SERIALIZED: Set if the event should be serialized with data
 *                             flow.
 * @GST_EVENT_TYPE_STICKY:     Set if the event is sticky on the pads.
 *
 * #GstEventTypeFlags indicate the aspects of the different #GstEventType
 * values. You can get the type flags of a #GstEventType with the
 * gst_event_type_get_flags() function.
 */
typedef enum {
  GST_EVENT_TYPE_UPSTREAM       = 1 << 0,
  GST_EVENT_TYPE_DOWNSTREAM     = 1 << 1,
  GST_EVENT_TYPE_SERIALIZED     = 1 << 2,
  GST_EVENT_TYPE_STICKY         = 1 << 3
} GstEventTypeFlags;

/**
 * GST_EVENT_TYPE_BOTH:
 *
 * The same thing as #GST_EVENT_TYPE_UPSTREAM | #GST_EVENT_TYPE_DOWNSTREAM.
 */
#define GST_EVENT_TYPE_BOTH \
    (GST_EVENT_TYPE_UPSTREAM | GST_EVENT_TYPE_DOWNSTREAM)

#define GST_EVENT_MAX_STICKY    16
#define GST_EVENT_STICKY_SHIFT  8
#define GST_EVENT_NUM_SHIFT     (GST_EVENT_STICKY_SHIFT + 4)

/**
 * GST_EVENT_MAKE_TYPE:
 * @num: the event number to create
 * @idx: the index in the sticky array
 * @flags: the event flags
 *
 * when making custom event types, use this macro with the num and
 * the given flags
 */
#define GST_EVENT_MAKE_TYPE(num,idx,flags) \
    (((num) << GST_EVENT_NUM_SHIFT) | ((idx) << GST_EVENT_STICKY_SHIFT) | (flags))

#define FLAG(name) GST_EVENT_TYPE_##name


#define GST_EVENT_STICKY_IDX_TYPE(type)  (((type) >> GST_EVENT_STICKY_SHIFT) & 0xf)
#define GST_EVENT_STICKY_IDX(ev)         GST_EVENT_STICKY_IDX_TYPE(GST_EVENT_TYPE(ev))

/**
 * GstEventType:
 * @GST_EVENT_UNKNOWN: unknown event.
 * @GST_EVENT_FLUSH_START: Start a flush operation. This event clears all data
 *                 from the pipeline and unblock all streaming threads.
 * @GST_EVENT_FLUSH_STOP: Stop a flush operation. This event resets the
 *                 running-time of the pipeline.
 * @GST_EVENT_EOS: End-Of-Stream. No more data is to be expected to follow
 *                 without a NEWSEGMENT event.
 * @GST_EVENT_NEWSEGMENT: A new media segment follows in the dataflow. The
 *                 segment events contains information for clipping buffers and
 *                 converting buffer timestamps to running-time and
 *                 stream-time.
 * @GST_EVENT_TAG: A new set of metadata tags has been found in the stream.
 * @GST_EVENT_BUFFERSIZE: Notification of buffering requirements. Currently not
 *                 used yet.
 * @GST_EVENT_SINK_MESSAGE: An event that sinks turn into a message. Used to
 *                          send messages that should be emitted in sync with
 *                          rendering.
 *                          Since: 0.10.26
 * @GST_EVENT_QOS: A quality message. Used to indicate to upstream elements
 *                 that the downstream elements should adjust their processing
 *                 rate.
 * @GST_EVENT_SEEK: A request for a new playback position and rate.
 * @GST_EVENT_NAVIGATION: Navigation events are usually used for communicating
 *                        user requests, such as mouse or keyboard movements,
 *                        to upstream elements.
 * @GST_EVENT_LATENCY: Notification of new latency adjustment. Sinks will use
 *                     the latency information to adjust their synchronisation.
 *                     Since: 0.10.12
 * @GST_EVENT_STEP: A request for stepping through the media. Sinks will usually
 *                  execute the step operation. Since: 0.10.24
 * @GST_EVENT_RENEGOTIATE: A request for upstream renegotiating caps.
 *                           Since: 0.10.33
 * @GST_EVENT_CUSTOM_UPSTREAM: Upstream custom event
 * @GST_EVENT_CUSTOM_DOWNSTREAM: Downstream custom event that travels in the
 *                        data flow.
 * @GST_EVENT_CUSTOM_DOWNSTREAM_OOB: Custom out-of-band downstream event.
 * @GST_EVENT_CUSTOM_BOTH: Custom upstream or downstream event.
 *                         In-band when travelling downstream.
 * @GST_EVENT_CUSTOM_BOTH_OOB: Custom upstream or downstream out-of-band event.
 *
 * #GstEventType lists the standard event types that can be sent in a pipeline.
 *
 * The custom event types can be used for private messages between elements
 * that can't be expressed using normal
 * GStreamer buffer passing semantics. Custom events carry an arbitrary
 * #GstStructure.
 * Specific custom events are distinguished by the name of the structure.
 */
/* NOTE: keep in sync with quark registration in gstevent.c */
typedef enum {
  GST_EVENT_UNKNOWN               = GST_EVENT_MAKE_TYPE (0, 0, 0),
  /* bidirectional events */
  GST_EVENT_FLUSH_START           = GST_EVENT_MAKE_TYPE (1, 0, FLAG(BOTH)),
  GST_EVENT_FLUSH_STOP            = GST_EVENT_MAKE_TYPE (2, 0, FLAG(BOTH) | FLAG(SERIALIZED)),
  /* downstream serialized events */
  GST_EVENT_EOS                   = GST_EVENT_MAKE_TYPE (5, 0, FLAG(DOWNSTREAM) | FLAG(SERIALIZED) | FLAG(STICKY)),
  GST_EVENT_CAPS                  = GST_EVENT_MAKE_TYPE (6, 5, FLAG(DOWNSTREAM) | FLAG(SERIALIZED) | FLAG(STICKY)),
  GST_EVENT_NEWSEGMENT            = GST_EVENT_MAKE_TYPE (7, 1, FLAG(DOWNSTREAM) | FLAG(SERIALIZED) | FLAG(STICKY)),
  GST_EVENT_TAG                   = GST_EVENT_MAKE_TYPE (8, 2, FLAG(DOWNSTREAM) | FLAG(SERIALIZED) | FLAG(STICKY)),
  GST_EVENT_BUFFERSIZE            = GST_EVENT_MAKE_TYPE (9, 3, FLAG(DOWNSTREAM) | FLAG(SERIALIZED) | FLAG(STICKY)),
  GST_EVENT_SINK_MESSAGE          = GST_EVENT_MAKE_TYPE (10, 4, FLAG(DOWNSTREAM) | FLAG(SERIALIZED) | FLAG(STICKY)),
  /* upstream events */
  GST_EVENT_QOS                   = GST_EVENT_MAKE_TYPE (15, 0, FLAG(UPSTREAM)),
  GST_EVENT_SEEK                  = GST_EVENT_MAKE_TYPE (16, 0, FLAG(UPSTREAM)),
  GST_EVENT_NAVIGATION            = GST_EVENT_MAKE_TYPE (17, 0, FLAG(UPSTREAM)),
  GST_EVENT_LATENCY               = GST_EVENT_MAKE_TYPE (18, 0, FLAG(UPSTREAM)),
  GST_EVENT_STEP                  = GST_EVENT_MAKE_TYPE (19, 0, FLAG(UPSTREAM)),
  GST_EVENT_RENEGOTIATE           = GST_EVENT_MAKE_TYPE (20, 0, FLAG(UPSTREAM)),

  /* custom events start here */
  GST_EVENT_CUSTOM_UPSTREAM       = GST_EVENT_MAKE_TYPE (32, 0, FLAG(UPSTREAM)),
  GST_EVENT_CUSTOM_DOWNSTREAM     = GST_EVENT_MAKE_TYPE (32, 0, FLAG(DOWNSTREAM) | FLAG(SERIALIZED)),
  GST_EVENT_CUSTOM_DOWNSTREAM_OOB = GST_EVENT_MAKE_TYPE (32, 0, FLAG(DOWNSTREAM)),
  GST_EVENT_CUSTOM_BOTH           = GST_EVENT_MAKE_TYPE (32, 0, FLAG(BOTH) | FLAG(SERIALIZED)),
  GST_EVENT_CUSTOM_BOTH_OOB       = GST_EVENT_MAKE_TYPE (32, 0, FLAG(BOTH))
} GstEventType;
#undef FLAG

/**
 * GST_EVENT_TRACE_NAME:
 *
 * The name used for memory allocation tracing
 */
#define GST_EVENT_TRACE_NAME    "GstEvent"

typedef struct _GstEvent GstEvent;

#define GST_TYPE_EVENT                  (gst_event_get_type())
#define GST_IS_EVENT(obj)               (GST_IS_MINI_OBJECT_TYPE (obj, GST_TYPE_EVENT))
#define GST_EVENT(obj)                  ((GstEvent *)(obj))
#define GST_EVENT_CAST(obj)             ((GstEvent *)(obj))

/**
 * GST_EVENT_TYPE:
 * @event: the event to query
 *
 * Get the #GstEventType of the event.
 */
#define GST_EVENT_TYPE(event)           (GST_EVENT_CAST(event)->type)

/**
 * GST_EVENT_TYPE_NAME:
 * @event: the event to query
 *
 * Get a constant string representation of the #GstEventType of the event.
 */
#define GST_EVENT_TYPE_NAME(event)      (gst_event_type_get_name(GST_EVENT_TYPE(event)))

/**
 * GST_EVENT_TIMESTAMP:
 * @event: the event to query
 *
 * Get the #GstClockTime timestamp of the event. This is the time when the event
 * was created.
 */
#define GST_EVENT_TIMESTAMP(event)      (GST_EVENT_CAST(event)->timestamp)

/**
 * GST_EVENT_SEQNUM:
 * @event: the event to query
 *
 * The sequence number of @event.
 */
#define GST_EVENT_SEQNUM(event)         (GST_EVENT_CAST(event)->seqnum)

/**
 * GST_EVENT_IS_UPSTREAM:
 * @ev: the event to query
 *
 * Check if an event can travel upstream.
 */
#define GST_EVENT_IS_UPSTREAM(ev)       !!(GST_EVENT_TYPE (ev) & GST_EVENT_TYPE_UPSTREAM)
/**
 * GST_EVENT_IS_DOWNSTREAM:
 * @ev: the event to query
 *
 * Check if an event can travel downstream.
 */
#define GST_EVENT_IS_DOWNSTREAM(ev)     !!(GST_EVENT_TYPE (ev) & GST_EVENT_TYPE_DOWNSTREAM)
/**
 * GST_EVENT_IS_SERIALIZED:
 * @ev: the event to query
 *
 * Check if an event is serialized with the data stream.
 */
#define GST_EVENT_IS_SERIALIZED(ev)     !!(GST_EVENT_TYPE (ev) & GST_EVENT_TYPE_SERIALIZED)
/**
 * GST_EVENT_IS_STICKY:
 * @ev: the event to query
 *
 * Check if an event is sticky on the pads.
 */
#define GST_EVENT_IS_STICKY(ev)     !!(GST_EVENT_TYPE (ev) & GST_EVENT_TYPE_STICKY)

/**
 * gst_event_replace:
 * @old_event: (inout) (transfer full): pointer to a pointer to a #GstEvent
 *     to be replaced.
 * @new_event: (allow-none) (transfer none): pointer to a #GstEvent that will
 *     replace the event pointed to by @old_event.
 *
 * Modifies a pointer to a #GstEvent to point to a different #GstEvent. The
 * modification is done atomically (so this is useful for ensuring thread safety
 * in some cases), and the reference counts are updated appropriately (the old
 * event is unreffed, the new one is reffed).
 *
 * Either @new_event or the #GstEvent pointed to by @old_event may be NULL.
 *
 * Since: 0.10.3
 */
#define         gst_event_replace(old_event,new_event) \
    gst_mini_object_replace ((GstMiniObject **)(old_event), GST_MINI_OBJECT_CAST (new_event))

/**
 * GstSeekType:
 * @GST_SEEK_TYPE_NONE: no change in position is required
 * @GST_SEEK_TYPE_CUR: change relative to currently configured segment. This
 *    can't be used to seek relative to the current playback position - do a
 *    position query, calculate the desired position and then do an absolute
 *    position seek instead if that's what you want to do.
 * @GST_SEEK_TYPE_SET: absolute position is requested
 * @GST_SEEK_TYPE_END: relative position to duration is requested
 *
 * The different types of seek events. When constructing a seek event with
 * gst_event_new_seek(), a format, a seek method and optional flags are to
 * be provided. The seek event is then inserted into the graph with
 * gst_pad_send_event() or gst_element_send_event().
 */
typedef enum {
  /* one of these */
  GST_SEEK_TYPE_NONE            = 0,
  GST_SEEK_TYPE_CUR             = 1,
  GST_SEEK_TYPE_SET             = 2,
  GST_SEEK_TYPE_END             = 3
} GstSeekType;

/**
 * GstSeekFlags:
 * @GST_SEEK_FLAG_NONE: no flag
 * @GST_SEEK_FLAG_FLUSH: flush pipeline
 * @GST_SEEK_FLAG_ACCURATE: accurate position is requested, this might
 *                     be considerably slower for some formats.
 * @GST_SEEK_FLAG_KEY_UNIT: seek to the nearest keyframe. This might be
 *                     faster but less accurate.
 * @GST_SEEK_FLAG_SEGMENT: perform a segment seek.
 * @GST_SEEK_FLAG_SKIP: when doing fast foward or fast reverse playback, allow
 *                     elements to skip frames instead of generating all
 *                     frames. Since 0.10.22.
 *
 * Flags to be used with gst_element_seek() or gst_event_new_seek(). All flags
 * can be used together.
 *
 * A non flushing seek might take some time to perform as the currently
 * playing data in the pipeline will not be cleared.
 *
 * An accurate seek might be slower for formats that don't have any indexes
 * or timestamp markers in the stream. Specifying this flag might require a
 * complete scan of the file in those cases.
 *
 * When performing a segment seek: after the playback of the segment completes,
 * no EOS will be emmited by the element that performed the seek, but a
 * #GST_MESSAGE_SEGMENT_DONE message will be posted on the bus by the element.
 * When this message is posted, it is possible to send a new seek event to
 * continue playback. With this seek method it is possible to perform seemless
 * looping or simple linear editing.
 *
 * When doing fast forward (rate > 1.0) or fast reverse (rate < -1.0) trickmode
 * playback, the @GST_SEEK_FLAG_SKIP flag can be used to instruct decoders
 * and demuxers to adjust the playback rate by skipping frames. This can improve
 * performance and decrease CPU usage because not all frames need to be decoded.
 *
 * Also see part-seeking.txt in the GStreamer design documentation for more
 * details on the meaning of these flags and the behaviour expected of
 * elements that handle them.
 */
typedef enum {
  GST_SEEK_FLAG_NONE            = 0,
  GST_SEEK_FLAG_FLUSH           = (1 << 0),
  GST_SEEK_FLAG_ACCURATE        = (1 << 1),
  GST_SEEK_FLAG_KEY_UNIT        = (1 << 2),
  GST_SEEK_FLAG_SEGMENT         = (1 << 3),
  GST_SEEK_FLAG_SKIP            = (1 << 4)
} GstSeekFlags;

/**
 * GstQOSType:
 * @GST_QOS_TYPE_OVERFLOW: The QoS event type that is produced when downstream
 *    elements are producing data too quickly and the element can't keep up
 *    processing the data. Upstream should reduce their processing rate. This
 *    type is also used when buffers arrive early or in time.
 * @GST_QOS_TYPE_UNDERFLOW: The QoS event type that is produced when downstream
 *    elements are producing data too slowly and need to speed up their processing
 *    rate. 
 * @GST_QOS_TYPE_THROTTLE: The QoS event type that is produced when the
 *    application enabled throttling to limit the datarate.
 *
 * The different types of QoS events that can be given to the 
 * gst_event_new_qos_full() method. 
 *
 * Since: 0.10.33
 */
typedef enum {
  GST_QOS_TYPE_OVERFLOW        = 0,
  GST_QOS_TYPE_UNDERFLOW       = 1,
  GST_QOS_TYPE_THROTTLE        = 2
} GstQOSType;

/**
 * GstEvent:
 * @mini_object: the parent structure
 * @type: the #GstEventType of the event
 * @timestamp: the timestamp of the event
 * @structure: the #GstStructure containing the event info.
 *
 * A #GstEvent.
 */
struct _GstEvent {
  GstMiniObject mini_object;

  /*< public >*/ /* with COW */
  GstEventType  type;
  guint64       timestamp;
  guint32       seqnum;

  GstStructure  *structure;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

const gchar*    gst_event_type_get_name         (GstEventType type);
GQuark          gst_event_type_to_quark         (GstEventType type);
GstEventTypeFlags
                gst_event_type_get_flags        (GstEventType type);


GType           gst_event_get_type              (void);

/* refcounting */
/**
 * gst_event_ref:
 * @event: The event to refcount
 *
 * Increase the refcount of this event.
 *
 * Returns: (transfer full): @event (for convenience when doing assignments)
 */
#ifdef _FOOL_GTK_DOC_
G_INLINE_FUNC GstEvent * gst_event_ref (GstEvent * event);
#endif

static inline GstEvent *
gst_event_ref (GstEvent * event)
{
  return (GstEvent *) gst_mini_object_ref (GST_MINI_OBJECT_CAST (event));
}

/**
 * gst_event_unref:
 * @event: (transfer full): the event to refcount
 *
 * Decrease the refcount of an event, freeing it if the refcount reaches 0.
 */
#ifdef _FOOL_GTK_DOC_
G_INLINE_FUNC void gst_event_unref (GstEvent * event);
#endif

static inline void
gst_event_unref (GstEvent * event)
{
  gst_mini_object_unref (GST_MINI_OBJECT_CAST (event));
}

/* copy event */
/**
 * gst_event_copy:
 * @event: The event to copy
 *
 * Copy the event using the event specific copy function.
 *
 * Returns: (transfer full): the new event
 */
#ifdef _FOOL_GTK_DOC_
G_INLINE_FUNC GstEvent * gst_event_copy (const GstEvent * event);
#endif

static inline GstEvent *
gst_event_copy (const GstEvent * event)
{
  return GST_EVENT_CAST (gst_mini_object_copy (GST_MINI_OBJECT_CONST_CAST (event)));
}


/* custom event */
GstEvent*       gst_event_new_custom            (GstEventType type, GstStructure *structure);

const GstStructure *
                gst_event_get_structure         (GstEvent *event);

gboolean        gst_event_has_name              (GstEvent *event, const gchar *name);

/* identifiers for events and messages */
guint32         gst_event_get_seqnum            (GstEvent *event);
void            gst_event_set_seqnum            (GstEvent *event, guint32 seqnum);

/* flush events */
GstEvent *      gst_event_new_flush_start       (void);
GstEvent *      gst_event_new_flush_stop        (void);

/* EOS event */
GstEvent *      gst_event_new_eos               (void);

/* Caps events */
GstEvent *      gst_event_new_caps              (GstCaps *caps);
void            gst_event_parse_caps            (GstEvent *event, GstCaps **caps);

/* newsegment events */
GstEvent*       gst_event_new_new_segment       (gboolean update, gdouble rate,
                                                 GstFormat format,
                                                 gint64 start, gint64 stop,
                                                 gint64 time);
GstEvent*       gst_event_new_new_segment_full  (gboolean update, gdouble rate,
                                                 gdouble applied_rate,
                                                 GstFormat format,
                                                 gint64 start, gint64 stop,
                                                 gint64 time);
void            gst_event_parse_new_segment     (GstEvent *event,
                                                 gboolean *update,
                                                 gdouble *rate,
                                                 GstFormat *format,
                                                 gint64 *start, gint64 *stop,
                                                 gint64 *time);
void            gst_event_parse_new_segment_full (GstEvent *event,
                                                 gboolean *update,
                                                 gdouble *rate,
                                                 gdouble *applied_rate,
                                                 GstFormat *format,
                                                 gint64 *start, gint64 *stop,
                                                 gint64 *time);

/* tag event */
GstEvent*       gst_event_new_tag               (GstTagList *taglist);
void            gst_event_parse_tag             (GstEvent *event, GstTagList **taglist);

/* buffer */
GstEvent *      gst_event_new_buffer_size       (GstFormat format, gint64 minsize, gint64 maxsize,
                                                 gboolean async);
void            gst_event_parse_buffer_size     (GstEvent *event, GstFormat *format, gint64 *minsize,
                                                 gint64 *maxsize, gboolean *async);

/* QOS events */
GstEvent*       gst_event_new_qos               (gdouble proportion, GstClockTimeDiff diff,
                                                 GstClockTime timestamp);
GstEvent*       gst_event_new_qos_full          (GstQOSType type, gdouble proportion,
                                                 GstClockTimeDiff diff, GstClockTime timestamp);
void            gst_event_parse_qos             (GstEvent *event, gdouble *proportion, GstClockTimeDiff *diff,
                                                 GstClockTime *timestamp);
void            gst_event_parse_qos_full        (GstEvent *event, GstQOSType *type,
                                                 gdouble *proportion, GstClockTimeDiff *diff,
                                                 GstClockTime *timestamp);
/* seek event */
GstEvent*       gst_event_new_seek              (gdouble rate, GstFormat format, GstSeekFlags flags,
                                                 GstSeekType start_type, gint64 start,
                                                 GstSeekType stop_type, gint64 stop);
void            gst_event_parse_seek            (GstEvent *event, gdouble *rate, GstFormat *format,
                                                 GstSeekFlags *flags,
                                                 GstSeekType *start_type, gint64 *start,
                                                 GstSeekType *stop_type, gint64 *stop);
/* navigation event */
GstEvent*       gst_event_new_navigation        (GstStructure *structure);

/* latency event */
GstEvent*       gst_event_new_latency           (GstClockTime latency);
void            gst_event_parse_latency         (GstEvent *event, GstClockTime *latency);

/* step event */
GstEvent*       gst_event_new_step              (GstFormat format, guint64 amount, gdouble rate,
                                                 gboolean flush, gboolean intermediate);
void            gst_event_parse_step            (GstEvent *event, GstFormat *format, guint64 *amount,
                                                 gdouble *rate, gboolean *flush, gboolean *intermediate);

/* renegotiate event */
GstEvent*       gst_event_new_renegotiate       (void);

G_END_DECLS

#endif /* __GST_EVENT_H__ */
