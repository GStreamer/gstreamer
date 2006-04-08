/* GStreamer
 * Copyright (C) <2005> Philippe Khalaf <burger@speedy.org> 
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

#ifndef __GST_BASE_RTP_DEPAYLOAD_H__
#define __GST_BASE_RTP_DEPAYLOAD_H__

#include <gst/gst.h>
#include <gst/rtp/gstrtpbuffer.h>

G_BEGIN_DECLS

/* #define's don't like whitespacey bits */
#define GST_TYPE_BASE_RTP_DEPAYLOAD (gst_base_rtp_depayload_get_type())
#define GST_BASE_RTP_DEPAYLOAD(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_BASE_RTP_DEPAYLOAD,GstBaseRTPDepayload))
#define GST_BASE_RTP_DEPAYLOAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_BASE_RTP_DEPAYLOAD,GstBaseRTPDepayloadClass))
#define GST_BASE_RTP_DEPAYLOAD_GET_CLASS(obj) \
        (G_TYPE_INSTANCE_GET_CLASS ((obj),GST_TYPE_BASE_RTP_DEPAYLOAD,GstBaseRTPDepayloadClass))
#define GST_IS_BASE_RTP_DEPAYLOAD(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_BASE_RTP_DEPAYLOAD))
#define GST_IS_BASE_RTP_DEPAYLOAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_BASE_RTP_DEPAYLOAD))

#define GST_BASE_RTP_DEPAYLOAD_SINKPAD(depayload) (GST_BASE_RTP_DEPAYLOAD (depayload)->sinkpad)
#define GST_BASE_RTP_DEPAYLOAD_SRCPAD(depayload)  (GST_BASE_RTP_DEPAYLOAD (depayload)->srcpad)

/* in milliseconds */
#define RTP_QUEUE_DELAY 100;

#define QUEUE_LOCK_INIT(base)   (g_static_rec_mutex_init(&base->queuelock))
#define QUEUE_LOCK_FREE(base)   (g_static_rec_mutex_free(&base->queuelock))
#define QUEUE_LOCK(base)                (g_static_rec_mutex_lock(&base->queuelock))
#define QUEUE_UNLOCK(base)              (g_static_rec_mutex_unlock(&base->queuelock))

typedef struct _GstBaseRTPDepayload      GstBaseRTPDepayload;
typedef struct _GstBaseRTPDepayloadClass GstBaseRTPDepayloadClass;

struct _GstBaseRTPDepayload
{
  GstElement parent;

  GstPad *sinkpad, *srcpad;

  /* lock to protect the queue */
  GStaticRecMutex queuelock;

  gboolean thread_running;
  /* the releaser thread */
  GThread *thread;

  /* this attribute must be set by the child */
  guint clock_rate;

  /* this value can be modified by the child if needed */
  guint queue_delay;

  /* we will queue up to RTP_QUEUEDELAY ms of packets,
   * reordering them if necessary
   * dropping any packets that are more than
   * RTP_QUEUEDELAY ms late 
   */
  GQueue *queue;

  GstSegment segment;
  gboolean need_newsegment;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

struct _GstBaseRTPDepayloadClass
{
  GstElementClass parent_class;

  /* virtuals */
  gboolean (*set_caps) (GstBaseRTPDepayload *filter, GstCaps *caps);

  /* non-pure function, default implementation in base class
   * this does buffering, reordering and dropping 
   */
  GstFlowReturn (*add_to_queue) (GstBaseRTPDepayload *filter, GstBuffer *in);

  /* pure virtual function, child must use this to process incoming
   * rtp packets
   */
  GstBuffer * (*process) (GstBaseRTPDepayload *base, GstBuffer *in);

  /* non-pure function used to convert from RTP timestamp to GST timestamp
   * this function is used by the child class before gst_pad_pushing
   */
  void (*set_gst_timestamp) (GstBaseRTPDepayload *filter, guint32 timestamp, GstBuffer *buf);

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

GType gst_base_rtp_depayload_get_type (void);

G_END_DECLS

#endif /* __GST_BASE_RTP_DEPAYLOAD_H__ */
