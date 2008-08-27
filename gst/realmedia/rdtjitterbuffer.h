/* GStreamer
 * Copyright (C) <2007> Wim Taymans <wim.taymans@gmail.com>
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

#ifndef __RDT_JITTER_BUFFER_H__
#define __RDT_JITTER_BUFFER_H__

#include <gst/gst.h>

typedef struct _RDTJitterBuffer RDTJitterBuffer;
typedef struct _RDTJitterBufferClass RDTJitterBufferClass;

#define RDT_TYPE_JITTER_BUFFER             (rdt_jitter_buffer_get_type())
#define RDT_JITTER_BUFFER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),RDT_TYPE_JITTER_BUFFER,RDTJitterBuffer))
#define RDT_JITTER_BUFFER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),RDT_TYPE_JITTER_BUFFER,RDTJitterBufferClass))
#define RDT_IS_JITTER_BUFFER(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),RDT_TYPE_JITTER_BUFFER))
#define RDT_IS_JITTER_BUFFER_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),RDT_TYPE_JITTER_BUFFER))
#define RDT_JITTER_BUFFER_CAST(src)        ((RDTJitterBuffer *)(src))

/**
 * RTPTailChanged:
 * @jbuf: an #RDTJitterBuffer
 * @user_data: user data specified when registering
 *
 * This callback will be called when the tail buffer of @jbuf changed.
 */
typedef void (*RTPTailChanged) (RDTJitterBuffer *jbuf, gpointer user_data);

#define RDT_JITTER_BUFFER_MAX_WINDOW 512
/**
 * RDTJitterBuffer:
 *
 * A JitterBuffer in the #RTPSession
 */
struct _RDTJitterBuffer {
  GObject        object;

  GQueue        *packets;

  /* for calculating skew */
  GstClockTime   base_time;
  GstClockTime   base_rtptime;
  guint64        ext_rtptime;
  gint64         window[RDT_JITTER_BUFFER_MAX_WINDOW];
  guint          window_pos;
  guint          window_size;
  gboolean       window_filling;
  gint64         window_min;
  gint64         skew;
  gint64         prev_send_diff;
};

struct _RDTJitterBufferClass {
  GObjectClass   parent_class;
};

GType rdt_jitter_buffer_get_type (void);

/* managing lifetime */
RDTJitterBuffer*      rdt_jitter_buffer_new              (void);

void                  rdt_jitter_buffer_reset_skew       (RDTJitterBuffer *jbuf);

gboolean              rdt_jitter_buffer_insert           (RDTJitterBuffer *jbuf, GstBuffer *buf,
		                                          GstClockTime time,
		                                          guint32 clock_rate,
		                                          gboolean *tail);
GstBuffer *           rdt_jitter_buffer_peek             (RDTJitterBuffer *jbuf);
GstBuffer *           rdt_jitter_buffer_pop              (RDTJitterBuffer *jbuf);

void                  rdt_jitter_buffer_flush            (RDTJitterBuffer *jbuf);

guint                 rdt_jitter_buffer_num_packets      (RDTJitterBuffer *jbuf);
guint32               rdt_jitter_buffer_get_ts_diff      (RDTJitterBuffer *jbuf);

#endif /* __RDT_JITTER_BUFFER_H__ */
