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

/*
 *
 *   This is a temporary copy of GstBaseSrc/GstPushSrc for the resin
 *   DVD components, to work around a deadlock with source elements that
 *   send seeks to themselves. 
 *
 */
#ifndef __RSN_BASE_SRC_H__
#define __RSN_BASE_SRC_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define RSN_TYPE_BASE_SRC               (gst_base_src_get_type())
#define RSN_BASE_SRC(obj)               (G_TYPE_CHECK_INSTANCE_CAST((obj),RSN_TYPE_BASE_SRC,RsnBaseSrc))
#define RSN_BASE_SRC_CLASS(klass)       (G_TYPE_CHECK_CLASS_CAST((klass),RSN_TYPE_BASE_SRC,RsnBaseSrcClass))
#define RSN_BASE_SRC_GET_CLASS(obj)     (G_TYPE_INSTANCE_GET_CLASS ((obj), RSN_TYPE_BASE_SRC, RsnBaseSrcClass))
#define GST_IS_BASE_SRC(obj)            (G_TYPE_CHECK_INSTANCE_TYPE((obj),RSN_TYPE_BASE_SRC))
#define GST_IS_BASE_SRC_CLASS(klass)    (G_TYPE_CHECK_CLASS_TYPE((klass),RSN_TYPE_BASE_SRC))
#define RSN_BASE_SRC_CAST(obj)          ((RsnBaseSrc *)(obj))

/**
 * RsnBaseSrcFlags:
 * @RSN_BASE_SRC_STARTED: has source been started
 * @RSN_BASE_SRC_FLAG_LAST: offset to define more flags
 *
 * The #GstElement flags that a basesrc element may have.
 */
typedef enum {
  RSN_BASE_SRC_STARTED           = (GST_ELEMENT_FLAG_LAST << 0),
  /* padding */
  RSN_BASE_SRC_FLAG_LAST         = (GST_ELEMENT_FLAG_LAST << 2)
} RsnBaseSrcFlags;

typedef struct _RsnBaseSrc RsnBaseSrc;
typedef struct _RsnBaseSrcClass RsnBaseSrcClass;
typedef struct _RsnBaseSrcPrivate RsnBaseSrcPrivate;

/**
 * RSN_BASE_SRC_PAD:
 * @obj: base source instance
 *
 * Gives the pointer to the #GstPad object of the element.
 */
#define RSN_BASE_SRC_PAD(obj)                 (RSN_BASE_SRC_CAST (obj)->srcpad)


/**
 * RsnBaseSrc:
 * @element: the parent element.
 *
 * The opaque #RsnBaseSrc data structure.
 */
struct _RsnBaseSrc {
  GstElement     element;

  /*< protected >*/
  GstPad        *srcpad;

  /* available to subclass implementations */
  /* MT-protected (with LIVE_LOCK) */
  GMutex        *live_lock;
  GCond         *live_cond;
  gboolean       is_live;
  gboolean       live_running;

  /* MT-protected (with LOCK) */
  gint           blocksize;     /* size of buffers when operating push based */
  gboolean       can_activate_push;     /* some scheduling properties */
  GstActivateMode pad_mode;
  gboolean       seekable; /* not used anymore */
  gboolean       random_access;

  GstClockID     clock_id;      /* for syncing */
  GstClockTime   end_time;

  /* MT-protected (with STREAM_LOCK) */
  GstSegment     segment;
  gboolean       need_newsegment;

  guint64        offset;        /* current offset in the resource, unused */
  guint64        size;          /* total size of the resource, unused */

  gint           num_buffers;
  gint           num_buffers_left;

  /*< private >*/
  union {
    struct {
      /* FIXME: those fields should be moved into the private struct */
      gboolean  typefind;
      gboolean  running;
      GstEvent *pending_seek;
    } ABI;
    gpointer       _gst_reserved[GST_PADDING_LARGE-1];
  } data;

  RsnBaseSrcPrivate *priv;
};

/**
 * RsnBaseSrcClass:
 * @parent_class: Element parent class
 * @get_caps: Called to get the caps to report
 * @set_caps: Notify subclass of changed output caps
 * @negotiate: Negotiated the caps with the peer.
 * @newsegment: Generate and send a new_segment event (UNUSED)
 * @start: Start processing. Subclasses should open resources and prepare
 *    to produce data.
 * @stop: Stop processing. Subclasses should use this to close resources.
 * @get_times: Given a buffer, return the start and stop time when it
 *    should be pushed out. The base class will sync on the clock using
 *    these times.
 * @get_size: Return the total size of the resource, in the configured format.
 * @is_seekable: Check if the source can seek
 * @unlock: Unlock any pending access to the resource. Subclasses should
 *    unblock any blocked function ASAP
 * @unlock_stop: Clear the previous unlock request. Subclasses should clear
 *    any state they set during unlock(), such as clearing command queues.
 * @event: Override this to implement custom event handling.
 * @create: Ask the subclass to create a buffer with offset and size.
 * @do_seek: Perform seeking on the resource to the indicated segment.
 * @prepare_seek_segment: Prepare the GstSegment that will be passed to the
 *   do_seek vmethod for executing a seek request. Sub-classes should override
 *   this if they support seeking in formats other than the configured native
 *   format. By default, it tries to convert the seek arguments to the
 *   configured native format and prepare a segment in that format.
 *   Since: 0.10.13
 * @query: Handle a requested query.
 * @check_get_range: Check whether the source would support pull-based
 *   operation if it were to be opened now. This vfunc is optional, but
 *   should be implemented if possible to avoid unnecessary start/stop
 *   cycles. The default implementation will open and close the resource
 *   to find out whether get_range is supported, and that is usually
 *   undesirable.
 * @fixate: Called during negotiation if caps need fixating. Implement instead of
 *   setting a fixate function on the source pad.
 *
 * Subclasses can override any of the available virtual methods or not, as
 * needed. At the minimum, the @create method should be overridden to produce
 * buffers.
 */
struct _RsnBaseSrcClass {
  GstElementClass parent_class;

  /*< public >*/
  /* virtual methods for subclasses */

  /* get caps from subclass */
  GstCaps*      (*get_caps)     (RsnBaseSrc *src);
  /* notify the subclass of new caps */
  gboolean      (*set_caps)     (RsnBaseSrc *src, GstCaps *caps);

  /* decide on caps */
  gboolean      (*negotiate)    (RsnBaseSrc *src);

  /* generate and send a newsegment (UNUSED) */
  gboolean      (*newsegment)   (RsnBaseSrc *src);

  /* start and stop processing, ideal for opening/closing the resource */
  gboolean      (*start)        (RsnBaseSrc *src);
  gboolean      (*stop)         (RsnBaseSrc *src);

  /* given a buffer, return start and stop time when it should be pushed
   * out. The base class will sync on the clock using these times. */
  void          (*get_times)    (RsnBaseSrc *src, GstBuffer *buffer,
                                 GstClockTime *start, GstClockTime *end);

  /* get the total size of the resource in bytes */
  gboolean      (*get_size)     (RsnBaseSrc *src, guint64 *size);

  /* check if the resource is seekable */
  gboolean      (*is_seekable)  (RsnBaseSrc *src);
  /* unlock any pending access to the resource. subclasses should unlock
   * any function ASAP. */
  gboolean      (*unlock)       (RsnBaseSrc *src);

  /* notify subclasses of an event */
  gboolean      (*event)        (RsnBaseSrc *src, GstEvent *event);

  /* ask the subclass to create a buffer with offset and size */
  GstFlowReturn (*create)       (RsnBaseSrc *src, guint64 offset, guint size,
                                 GstBuffer **buf);

  /* additions that change padding... */
  /* notify subclasses of a seek */
  gboolean      (*do_seek)      (RsnBaseSrc *src, GstSegment *segment);
  /* notify subclasses of a query */
  gboolean      (*query)        (RsnBaseSrc *src, GstQuery *query);

  /* check whether the source would support pull-based operation if
   * it were to be opened now. This vfunc is optional, but should be
   * implemented if possible to avoid unnecessary start/stop cycles.
   * The default implementation will open and close the resource to
   * find out whether get_range is supported and that is usually
   * undesirable. */
  gboolean      (*check_get_range) (RsnBaseSrc *src);

  /* called if, in negotiation, caps need fixating */
  void          (*fixate)       (RsnBaseSrc *src, GstCaps *caps);

  /* Clear any pending unlock request, as we succeeded in unlocking */
  gboolean      (*unlock_stop)  (RsnBaseSrc *src);

  /* Prepare the segment on which to perform do_seek(), converting to the
   * current basesrc format. */
  gboolean      (*prepare_seek_segment) (RsnBaseSrc *src, GstEvent *seek,
                                         GstSegment *segment);

  /*< private >*/
  gpointer       _gst_reserved[GST_PADDING_LARGE - 6];
};

GType gst_base_src_get_type (void);

GstFlowReturn   gst_base_src_wait_playing     (RsnBaseSrc *src);

void            gst_base_src_set_live         (RsnBaseSrc *src, gboolean live);
gboolean        gst_base_src_is_live          (RsnBaseSrc *src);

void            gst_base_src_set_format       (RsnBaseSrc *src, GstFormat format);

gboolean        gst_base_src_query_latency    (RsnBaseSrc *src, gboolean * live,
                                               GstClockTime * min_latency,
                                               GstClockTime * max_latency);

void            gst_base_src_set_blocksize    (RsnBaseSrc *src, gulong blocksize);
gulong          gst_base_src_get_blocksize    (RsnBaseSrc *src);

void            gst_base_src_set_do_timestamp (RsnBaseSrc *src, gboolean timestamp);
gboolean        gst_base_src_get_do_timestamp (RsnBaseSrc *src);

gboolean        gst_base_src_new_seamless_segment (RsnBaseSrc *src, gint64 start, gint64 stop, gint64 position);
G_END_DECLS

#endif /* __RSN_BASE_SRC_H__ */
