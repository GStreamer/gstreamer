/* GStreamer
 * Copyright (C) <2007> Wim Taymans <wim@fluendo.com>
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

#ifndef __RTP_JITTER_BUFFER_H__
#define __RTP_JITTER_BUFFER_H__

#include <gst/gst.h>
#include <gst/rtp/gstrtcpbuffer.h>
#include <gst/netbuffer/gstnetbuffer.h>

typedef struct _RTPJitterBuffer RTPJitterBuffer;
typedef struct _RTPJitterBufferClass RTPJitterBufferClass;

#define RTP_TYPE_JITTER_BUFFER             (rtp_jitter_buffer_get_type())
#define RTP_JITTER_BUFFER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),RTP_TYPE_JITTER_BUFFER,RTPJitterBuffer))
#define RTP_JITTER_BUFFER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),RTP_TYPE_JITTER_BUFFER,RTPJitterBufferClass))
#define RTP_IS_JITTER_BUFFER(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),RTP_TYPE_JITTER_BUFFER))
#define RTP_IS_JITTER_BUFFER_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),RTP_TYPE_JITTER_BUFFER))
#define RTP_JITTER_BUFFER_CAST(src)        ((RTPJitterBuffer *)(src))

/**
 * RTPJitterBuffer:
 *
 * A JitterBuffer in the #RTPSession
 */
struct _RTPJitterBuffer {
  GObject       object;

  GQueue       *packets;
};

struct _RTPJitterBufferClass {
  GObjectClass   parent_class;
};

GType rtp_jitter_buffer_get_type (void);

/* managing lifetime */
RTPJitterBuffer*      rtp_jitter_buffer_new            (void);

gboolean              rtp_jitter_buffer_insert         (RTPJitterBuffer *jbuf, GstBuffer *buf);
GstBuffer *           rtp_jitter_buffer_pop            (RTPJitterBuffer *jbuf);

void                  rtp_jitter_buffer_flush          (RTPJitterBuffer *jbuf);

guint                 rtp_jitter_buffer_num_packets    (RTPJitterBuffer *jbuf);
guint32               rtp_jitter_buffer_get_ts_diff    (RTPJitterBuffer *jbuf);

#endif /* __RTP_JITTER_BUFFER_H__ */
