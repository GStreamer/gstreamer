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
  gint i;

  jbuf->packets = g_queue_new ();
  jbuf->base_time = -1;
  jbuf->base_rtptime = -1;
  jbuf->ext_rtptime = -1;

  for (i = 0; i < 100; i++) {
    jbuf->window[i] = 0;
  }
  jbuf->window_pos = 0;
  jbuf->window_size = 100;
  jbuf->window_filling = TRUE;
  jbuf->window_min = 0;
  jbuf->skew = 0;
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

void
rtp_jitter_buffer_set_tail_changed (RTPJitterBuffer * jbuf, RTPTailChanged func,
    gpointer user_data)
{
  g_return_if_fail (jbuf != NULL);

  jbuf->tail_changed = func;
  jbuf->user_data = user_data;
}

void
rtp_jitter_buffer_set_clock_rate (RTPJitterBuffer * jbuf, gint clock_rate)
{
  g_return_if_fail (jbuf != NULL);

  jbuf->clock_rate = clock_rate;
}

gint
rtp_jitter_buffer_get_clock_rate (RTPJitterBuffer * jbuf)
{
  g_return_val_if_fail (jbuf != NULL, 0);

  return jbuf->clock_rate;
}


/* For the clock skew we use a windowed low point averaging algorithm as can be
 * found in http://www.grame.fr/pub/TR-050601.pdf. The idea is that the jitter is
 * composed of:
 *
 *  J = N + n
 *
 *   N   : a constant network delay.
 *   n   : random added noise. The noise is concentrated around 0
 *
 * In the receiver we can track the elapsed time at the sender with:
 *
 *  send_diff(i) = (Tsi - Ts0);
 *
 *   Tsi : The time at the sender at packet i
 *   Ts0 : The time at the sender at the first packet
 *
 * This is the difference between the RTP timestamp in the first received packet
 * and the current packet.
 *
 * At the receiver we have to deal with the jitter introduced by the network.
 *
 *  recv_diff(i) = (Tri - Tr0)
 *
 *   Tri : The time at the receiver at packet i
 *   Tr0 : The time at the receiver at the first packet
 *
 * Both of these values contain a jitter Ji, a jitter for packet i, so we can
 * write:
 *
 *  recv_diff(i) = (Cri + D + ni) - (Cr0 + D + n0))
 *
 *    Cri    : The time of the clock at the receiver for packet i
 *    D + ni : The jitter when receiving packet i
 *
 * We see that the network delay is irrelevant here as we can elliminate D:
 *
 *  recv_diff(i) = (Cri + ni) - (Cr0 + n0))
 *
 * The drift is now expressed as:
 *
 *  Drift(i) = recv_diff(i) - send_diff(i);
 *
 * We now keep the W latest values of Drift and find the minimum (this is the
 * one with the lowest network jitter and thus the one which is least affected
 * by it). We average this lowest value to smooth out the resulting network skew.
 *
 * Both the window and the weighting used for averaging influence the accuracy
 * of the drift estimation. Finding the correct parameters turns out to be a
 * compromise between accuracy and inertia.
 */
static void
calculate_skew (RTPJitterBuffer * jbuf, guint32 rtptime, GstClockTime time)
{
  guint64 ext_rtptime;
  guint64 send_diff, recv_diff;
  gint64 delta;
  gint64 old;
  gint pos, i;
  GstClockTime gstrtptime;

  ext_rtptime = gst_rtp_buffer_ext_timestamp (&jbuf->ext_rtptime, rtptime);

  gstrtptime =
      gst_util_uint64_scale_int (ext_rtptime, GST_SECOND, jbuf->clock_rate);

  /* first time, lock on to time and gstrtptime */
  if (jbuf->base_time == -1)
    jbuf->base_time = time;
  if (jbuf->base_rtptime == -1)
    jbuf->base_rtptime = gstrtptime;

  /* elapsed time at sender */
  send_diff = gstrtptime - jbuf->base_rtptime;
  /* elapsed time at receiver, includes the jitter */
  recv_diff = time - jbuf->base_time;

  /* measure the diff */
  delta = ((gint64) recv_diff) - ((gint64) send_diff);

  pos = jbuf->window_pos;

  if (jbuf->window_filling) {
    /* we are filling the window */
    GST_DEBUG ("filling %d %" G_GINT64_FORMAT ", diff %" G_GUINT64_FORMAT, pos,
        delta, send_diff);
    jbuf->window[pos++] = delta;
    /* calc the min delta we observed */
    if (pos == 1 || delta < jbuf->window_min)
      jbuf->window_min = delta;

    if (send_diff >= 2 * GST_SECOND || pos >= 100) {
      jbuf->window_size = pos;

      /* window filled, fill window with min */
      GST_DEBUG ("min %" G_GINT64_FORMAT, jbuf->window_min);
      for (i = 0; i < jbuf->window_size; i++)
        jbuf->window[i] = jbuf->window_min;

      /* the skew is initially the min */
      jbuf->skew = jbuf->window_min;
      jbuf->window_filling = FALSE;
    } else {
      jbuf->window_size = pos + 1;
    }
  } else {
    /* pick old value and store new value. We keep the previous value in order
     * to quickly check if the min of the window changed */
    old = jbuf->window[pos];
    jbuf->window[pos++] = delta;

    if (delta <= jbuf->window_min) {
      /* if the new value we inserted is smaller or equal to the current min,
       * it becomes the new min */
      jbuf->window_min = delta;
    } else if (old == jbuf->window_min) {
      gint64 min = G_MAXINT64;

      /* if we removed the old min, we have to find a new min */
      for (i = 0; i < jbuf->window_size; i++) {
        /* we found another value equal to the old min, we can stop searching now */
        if (jbuf->window[i] == old) {
          min = old;
          break;
        }
        if (jbuf->window[i] < min)
          min = jbuf->window[i];
      }
      jbuf->window_min = min;
    }
    /* average the min values */
    jbuf->skew = (jbuf->window_min + (15 * jbuf->skew)) / 16;
    GST_DEBUG ("new min: %" G_GINT64_FORMAT ", skew %" G_GINT64_FORMAT,
        jbuf->window_min, jbuf->skew);
  }
  /* wrap around in the window */
  if (pos >= jbuf->window_size)
    pos = 0;
  jbuf->window_pos = pos;
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
 * @time: a timestamp when this buffer was received in nanoseconds
 *
 * Inserts @buf into the packet queue of @jbuf. The sequence number of the
 * packet will be used to sort the packets. This function takes ownerhip of
 * @buf when the function returns %TRUE.
 *
 * Returns: %FALSE if a packet with the same number already existed.
 */
gboolean
rtp_jitter_buffer_insert (RTPJitterBuffer * jbuf, GstBuffer * buf,
    GstClockTime time)
{
  GList *list;
  gint func_ret = 1;
  guint32 rtptime;

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

  /* do skew calculation by measuring the difference between rtptime and the
   * receive time */
  if (time != -1) {
    rtptime = gst_rtp_buffer_get_timestamp (buf);
    calculate_skew (jbuf, rtptime, time);
  }

  if (list)
    g_queue_insert_before (jbuf->packets, list, buf);
  else {
    g_queue_push_tail (jbuf->packets, buf);

    /* tail buffer changed, signal callback */
    if (jbuf->tail_changed)
      jbuf->tail_changed (jbuf, jbuf->user_data);
  }

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
 * rtp_jitter_buffer_peek:
 * @jbuf: an #RTPJitterBuffer
 *
 * Peek the oldest buffer from the packet queue of @jbuf. Register a callback
 * with rtp_jitter_buffer_set_tail_changed() to be notified when an older packet
 * was inserted in the queue.
 *
 * Returns: a #GstBuffer or %NULL when there was no packet in the queue.
 */
GstBuffer *
rtp_jitter_buffer_peek (RTPJitterBuffer * jbuf)
{
  GstBuffer *buf;

  g_return_val_if_fail (jbuf != NULL, FALSE);

  buf = g_queue_peek_tail (jbuf->packets);

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
