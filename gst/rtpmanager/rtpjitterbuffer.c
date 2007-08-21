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
#include <string.h>
#include <stdlib.h>

#include <gst/rtp/gstrtpbuffer.h>
#include <gst/rtp/gstrtcpbuffer.h>

#include "rtpjitterbuffer.h"

GST_DEBUG_CATEGORY_STATIC (rtp_jitter_buffer_debug);
#define GST_CAT_DEFAULT rtp_jitter_buffer_debug

/* signals and args */
enum
{
  LAST_SIGNAL
};

enum
{
  PROP_0
};

/* GObject vmethods */
static void rtp_jitter_buffer_finalize (GObject * object);

/* static guint rtp_jitter_buffer_signals[LAST_SIGNAL] = { 0 }; */

G_DEFINE_TYPE (RTPJitterBuffer, rtp_jitter_buffer, G_TYPE_OBJECT);

static void
rtp_jitter_buffer_class_init (RTPJitterBufferClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = rtp_jitter_buffer_finalize;

  GST_DEBUG_CATEGORY_INIT (rtp_jitter_buffer_debug, "rtpjitterbuffer", 0,
      "RTP Jitter Buffer");
}

static void
rtp_jitter_buffer_init (RTPJitterBuffer * jbuf)
{
  jbuf->packets = g_queue_new ();
}

static void
rtp_jitter_buffer_finalize (GObject * object)
{
  RTPJitterBuffer *jbuf;

  jbuf = RTP_JITTER_BUFFER_CAST (object);

  rtp_jitter_buffer_flush (jbuf);
  g_queue_free (jbuf->packets);

  G_OBJECT_CLASS (rtp_jitter_buffer_parent_class)->finalize (object);
}

/**
 * rtp_jitter_buffer_new:
 *
 * Create an #RTPJitterBuffer.
 *
 * Returns: a new #RTPJitterBuffer. Use g_object_unref() after usage.
 */
RTPJitterBuffer *
rtp_jitter_buffer_new (void)
{
  RTPJitterBuffer *jbuf;

  jbuf = g_object_new (RTP_TYPE_JITTER_BUFFER, NULL);

  return jbuf;
}

static gint
compare_seqnum (GstBuffer * a, GstBuffer * b, RTPJitterBuffer * jbuf)
{
  guint16 seq1, seq2;

  seq1 = gst_rtp_buffer_get_seq (a);
  seq2 = gst_rtp_buffer_get_seq (b);

  /* check if diff more than half of the 16bit range */
  if (abs (seq2 - seq1) > (1 << 15)) {
    /* one of a/b has wrapped */
    return seq1 - seq2;
  } else {
    return seq2 - seq1;
  }
}

/**
 * rtp_jitter_buffer_insert:
 * @jbuf: an #RTPJitterBuffer
 * @buf: a buffer
 *
 * Inserts @buf into the packet queue of @jbuf. The sequence number of the
 * packet will be used to sort the packets. This function takes ownerhip of
 * @buf when the function returns %TRUE.
 *
 * Returns: %FALSE if a packet with the same number already existed.
 */
gboolean
rtp_jitter_buffer_insert (RTPJitterBuffer * jbuf, GstBuffer * buf)
{
  GList *list;
  gint func_ret = 1;

  g_return_val_if_fail (jbuf != NULL, FALSE);
  g_return_val_if_fail (buf != NULL, FALSE);

  /* loop the list to skip strictly smaller seqnum buffers */
  list = jbuf->packets->head;
  while (list
      && (func_ret =
          compare_seqnum (GST_BUFFER_CAST (list->data), buf, jbuf)) < 0)
    list = list->next;

  /* we hit a packet with the same seqnum, return FALSE to notify a duplicate */
  if (func_ret == 0)
    return FALSE;

  if (list)
    g_queue_insert_before (jbuf->packets, list, buf);
  else
    g_queue_push_tail (jbuf->packets, buf);

  return TRUE;
}

/**
 * rtp_jitter_buffer_pop:
 * @jbuf: an #RTPJitterBuffer
 *
 * Pops the oldest buffer from the packet queue of @jbuf.
 *
 * Returns: a #GstBuffer or %NULL when there was no packet in the queue.
 */
GstBuffer *
rtp_jitter_buffer_pop (RTPJitterBuffer * jbuf)
{
  GstBuffer *buf;

  g_return_val_if_fail (jbuf != NULL, FALSE);

  buf = g_queue_pop_tail (jbuf->packets);

  return buf;
}

/**
 * rtp_jitter_buffer_flush:
 * @jbuf: an #RTPJitterBuffer
 *
 * Flush all packets from the jitterbuffer.
 */
void
rtp_jitter_buffer_flush (RTPJitterBuffer * jbuf)
{
  GstBuffer *buffer;

  g_return_if_fail (jbuf != NULL);

  while ((buffer = g_queue_pop_head (jbuf->packets)))
    gst_buffer_unref (buffer);
}

/**
 * rtp_jitter_buffer_num_packets:
 * @jbuf: an #RTPJitterBuffer
 *
 * Get the number of packets currently in "jbuf.
 *
 * Returns: The number of packets in @jbuf.
 */
guint
rtp_jitter_buffer_num_packets (RTPJitterBuffer * jbuf)
{
  g_return_val_if_fail (jbuf != NULL, 0);

  return jbuf->packets->length;
}

/**
 * rtp_jitter_buffer_get_ts_diff:
 * @jbuf: an #RTPJitterBuffer
 *
 * Get the difference between the timestamps of first and last packet in the
 * jitterbuffer.
 *
 * Returns: The difference expressed in the timestamp units of the packets.
 */
guint32
rtp_jitter_buffer_get_ts_diff (RTPJitterBuffer * jbuf)
{
  guint64 high_ts, low_ts;
  GstBuffer *high_buf, *low_buf;
  guint32 result;

  g_return_val_if_fail (jbuf != NULL, 0);

  high_buf = g_queue_peek_head (jbuf->packets);
  low_buf = g_queue_peek_tail (jbuf->packets);

  if (!high_buf || !low_buf || high_buf == low_buf)
    return 0;

  high_ts = gst_rtp_buffer_get_timestamp (high_buf);
  low_ts = gst_rtp_buffer_get_timestamp (low_buf);

  /* it needs to work if ts wraps */
  if (high_ts >= low_ts) {
    result = (guint32) (high_ts - low_ts);
  } else {
    result = (guint32) (high_ts + G_MAXUINT32 + 1 - low_ts);
  }
  return result;
}
