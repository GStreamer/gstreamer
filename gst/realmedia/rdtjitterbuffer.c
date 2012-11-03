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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */
#include <string.h>
#include <stdlib.h>

#include "rdtjitterbuffer.h"
#include "gstrdtbuffer.h"

GST_DEBUG_CATEGORY_STATIC (rdt_jitter_buffer_debug);
#define GST_CAT_DEFAULT rdt_jitter_buffer_debug

#define MAX_WINDOW	RDT_JITTER_BUFFER_MAX_WINDOW
#define MAX_TIME	(2 * GST_SECOND)

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
static void rdt_jitter_buffer_finalize (GObject * object);

/* static guint rdt_jitter_buffer_signals[LAST_SIGNAL] = { 0 }; */

G_DEFINE_TYPE (RDTJitterBuffer, rdt_jitter_buffer, G_TYPE_OBJECT);

static void
rdt_jitter_buffer_class_init (RDTJitterBufferClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = rdt_jitter_buffer_finalize;

  GST_DEBUG_CATEGORY_INIT (rdt_jitter_buffer_debug, "rdtjitterbuffer", 0,
      "RDT Jitter Buffer");
}

static void
rdt_jitter_buffer_init (RDTJitterBuffer * jbuf)
{
  jbuf->packets = g_queue_new ();

  rdt_jitter_buffer_reset_skew (jbuf);
}

static void
rdt_jitter_buffer_finalize (GObject * object)
{
  RDTJitterBuffer *jbuf;

  jbuf = RDT_JITTER_BUFFER_CAST (object);

  rdt_jitter_buffer_flush (jbuf);
  g_queue_free (jbuf->packets);

  G_OBJECT_CLASS (rdt_jitter_buffer_parent_class)->finalize (object);
}

/**
 * rdt_jitter_buffer_new:
 *
 * Create an #RDTJitterBuffer.
 *
 * Returns: a new #RDTJitterBuffer. Use g_object_unref() after usage.
 */
RDTJitterBuffer *
rdt_jitter_buffer_new (void)
{
  RDTJitterBuffer *jbuf;

  jbuf = g_object_new (RDT_TYPE_JITTER_BUFFER, NULL);

  return jbuf;
}

void
rdt_jitter_buffer_reset_skew (RDTJitterBuffer * jbuf)
{
  jbuf->base_time = -1;
  jbuf->base_rtptime = -1;
  jbuf->ext_rtptime = -1;
  jbuf->window_pos = 0;
  jbuf->window_filling = TRUE;
  jbuf->window_min = 0;
  jbuf->skew = 0;
  jbuf->prev_send_diff = -1;
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
 * This is the difference between the RDT timestamp in the first received packet
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
 *
 * We use a 2 second window or up to 512 data points, which is statistically big
 * enough to catch spikes (FIXME, detect spikes).
 * We also use a rather large weighting factor (125) to smoothly adapt. During
 * startup, when filling the window, we use a parabolic weighting factor, the
 * more the window is filled, the faster we move to the detected possible skew.
 *
 * Returns: @time adjusted with the clock skew.
 */
static GstClockTime
calculate_skew (RDTJitterBuffer * jbuf, guint32 rtptime, GstClockTime time,
    guint32 clock_rate)
{
  guint64 ext_rtptime;
  guint64 send_diff, recv_diff;
  gint64 delta;
  gint64 old;
  gint pos, i;
  GstClockTime gstrtptime, out_time;

  //ext_rtptime = gst_rtp_buffer_ext_timestamp (&jbuf->ext_rtptime, rtptime);
  ext_rtptime = rtptime;

  gstrtptime = gst_util_uint64_scale_int (ext_rtptime, GST_SECOND, clock_rate);

again:
  /* first time, lock on to time and gstrtptime */
  if (jbuf->base_time == -1)
    jbuf->base_time = time;
  if (jbuf->base_rtptime == -1)
    jbuf->base_rtptime = gstrtptime;

  if (gstrtptime >= jbuf->base_rtptime)
    send_diff = gstrtptime - jbuf->base_rtptime;
  else {
    /* elapsed time at sender, timestamps can go backwards and thus be smaller
     * than our base time, take a new base time in that case. */
    GST_DEBUG ("backward timestamps at server, taking new base time");
    jbuf->base_rtptime = gstrtptime;
    jbuf->base_time = time;
    send_diff = 0;
  }

  GST_DEBUG ("extrtp %" G_GUINT64_FORMAT ", gstrtp %" GST_TIME_FORMAT ", base %"
      GST_TIME_FORMAT ", send_diff %" GST_TIME_FORMAT, ext_rtptime,
      GST_TIME_ARGS (gstrtptime), GST_TIME_ARGS (jbuf->base_rtptime),
      GST_TIME_ARGS (send_diff));

  if (jbuf->prev_send_diff != -1 && time != -1) {
    gint64 delta_diff;

    if (send_diff > jbuf->prev_send_diff)
      delta_diff = send_diff - jbuf->prev_send_diff;
    else
      delta_diff = jbuf->prev_send_diff - send_diff;

    /* server changed rtp timestamps too quickly, reset skew detection and start
     * again. This value is sortof arbitrary and can be a bad measurement up if
     * there are many packets missing because then we get a big gap that is
     * unrelated to a timestamp switch. */
    if (delta_diff > GST_SECOND) {
      GST_DEBUG ("delta changed too quickly %" GST_TIME_FORMAT " reset skew",
          GST_TIME_ARGS (delta_diff));
      rdt_jitter_buffer_reset_skew (jbuf);
      goto again;
    }
  }
  jbuf->prev_send_diff = send_diff;

  /* we don't have an arrival timestamp so we can't do skew detection. we
   * should still apply a timestamp based on RDT timestamp and base_time */
  if (time == -1)
    goto no_skew;

  /* elapsed time at receiver, includes the jitter */
  recv_diff = time - jbuf->base_time;

  GST_DEBUG ("time %" GST_TIME_FORMAT ", base %" GST_TIME_FORMAT ", recv_diff %"
      GST_TIME_FORMAT, GST_TIME_ARGS (time), GST_TIME_ARGS (jbuf->base_time),
      GST_TIME_ARGS (recv_diff));

  /* measure the diff */
  delta = ((gint64) recv_diff) - ((gint64) send_diff);

  pos = jbuf->window_pos;

  if (jbuf->window_filling) {
    /* we are filling the window */
    GST_DEBUG ("filling %d, delta %" G_GINT64_FORMAT, pos, delta);
    jbuf->window[pos++] = delta;
    /* calc the min delta we observed */
    if (pos == 1 || delta < jbuf->window_min)
      jbuf->window_min = delta;

    if (send_diff >= MAX_TIME || pos >= MAX_WINDOW) {
      jbuf->window_size = pos;

      /* window filled */
      GST_DEBUG ("min %" G_GINT64_FORMAT, jbuf->window_min);

      /* the skew is now the min */
      jbuf->skew = jbuf->window_min;
      jbuf->window_filling = FALSE;
    } else {
      gint perc_time, perc_window, perc;

      /* figure out how much we filled the window, this depends on the amount of
       * time we have or the max number of points we keep. */
      perc_time = send_diff * 100 / MAX_TIME;
      perc_window = pos * 100 / MAX_WINDOW;
      perc = MAX (perc_time, perc_window);

      /* make a parabolic function, the closer we get to the MAX, the more value
       * we give to the scaling factor of the new value */
      perc = perc * perc;

      /* quickly go to the min value when we are filling up, slowly when we are
       * just starting because we're not sure it's a good value yet. */
      jbuf->skew =
          (perc * jbuf->window_min + ((10000 - perc) * jbuf->skew)) / 10000;
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
    jbuf->skew = (jbuf->window_min + (124 * jbuf->skew)) / 125;
    GST_DEBUG ("delta %" G_GINT64_FORMAT ", new min: %" G_GINT64_FORMAT,
        delta, jbuf->window_min);
  }
  /* wrap around in the window */
  if (pos >= jbuf->window_size)
    pos = 0;
  jbuf->window_pos = pos;

no_skew:
  /* the output time is defined as the base timestamp plus the RDT time
   * adjusted for the clock skew .*/
  out_time = jbuf->base_time + send_diff + jbuf->skew;

  GST_DEBUG ("skew %" G_GINT64_FORMAT ", out %" GST_TIME_FORMAT,
      jbuf->skew, GST_TIME_ARGS (out_time));

  return out_time;
}

/**
 * rdt_jitter_buffer_insert:
 * @jbuf: an #RDTJitterBuffer
 * @buf: a buffer
 * @time: a running_time when this buffer was received in nanoseconds
 * @clock_rate: the clock-rate of the payload of @buf
 * @tail: TRUE when the tail element changed.
 *
 * Inserts @buf into the packet queue of @jbuf. The sequence number of the
 * packet will be used to sort the packets. This function takes ownerhip of
 * @buf when the function returns %TRUE.
 * @buf should have writable metadata when calling this function.
 *
 * Returns: %FALSE if a packet with the same number already existed.
 */
gboolean
rdt_jitter_buffer_insert (RDTJitterBuffer * jbuf, GstBuffer * buf,
    GstClockTime time, guint32 clock_rate, gboolean * tail)
{
  GList *list;
  guint32 rtptime;
  guint16 seqnum;
  GstRDTPacket packet;
  gboolean more;

  g_return_val_if_fail (jbuf != NULL, FALSE);
  g_return_val_if_fail (buf != NULL, FALSE);

  more = gst_rdt_buffer_get_first_packet (buf, &packet);
  /* programmer error */
  g_return_val_if_fail (more == TRUE, FALSE);

  seqnum = gst_rdt_packet_data_get_seq (&packet);
  /* do skew calculation by measuring the difference between rtptime and the
   * receive time, this function will retimestamp @buf with the skew corrected
   * running time. */
  rtptime = gst_rdt_packet_data_get_timestamp (&packet);

  /* loop the list to skip strictly smaller seqnum buffers */
  for (list = jbuf->packets->head; list; list = g_list_next (list)) {
    guint16 qseq;
    gint gap;

    more =
        gst_rdt_buffer_get_first_packet (GST_BUFFER_CAST (list->data), &packet);
    /* programmer error */
    g_return_val_if_fail (more == TRUE, FALSE);

    qseq = gst_rdt_packet_data_get_seq (&packet);

    /* compare the new seqnum to the one in the buffer */
    gap = gst_rdt_buffer_compare_seqnum (seqnum, qseq);

    /* we hit a packet with the same seqnum, notify a duplicate */
    if (G_UNLIKELY (gap == 0))
      goto duplicate;

    /* seqnum > qseq, we can stop looking */
    if (G_LIKELY (gap < 0))
      break;
  }


  if (clock_rate) {
    time = calculate_skew (jbuf, rtptime, time, clock_rate);
    GST_BUFFER_TIMESTAMP (buf) = time;
  }

  if (list)
    g_queue_insert_before (jbuf->packets, list, buf);
  else
    g_queue_push_tail (jbuf->packets, buf);

  /* tail was changed when we did not find a previous packet, we set the return
   * flag when requested. */
  if (tail)
    *tail = (list == NULL);

  return TRUE;

  /* ERRORS */
duplicate:
  {
    GST_WARNING ("duplicate packet %d found", (gint) seqnum);
    return FALSE;
  }
}

/**
 * rdt_jitter_buffer_pop:
 * @jbuf: an #RDTJitterBuffer
 *
 * Pops the oldest buffer from the packet queue of @jbuf. The popped buffer will
 * have its timestamp adjusted with the incomming running_time and the detected
 * clock skew.
 *
 * Returns: a #GstBuffer or %NULL when there was no packet in the queue.
 */
GstBuffer *
rdt_jitter_buffer_pop (RDTJitterBuffer * jbuf)
{
  GstBuffer *buf;

  g_return_val_if_fail (jbuf != NULL, FALSE);

  buf = g_queue_pop_tail (jbuf->packets);

  return buf;
}

/**
 * rdt_jitter_buffer_peek:
 * @jbuf: an #RDTJitterBuffer
 *
 * Peek the oldest buffer from the packet queue of @jbuf. Register a callback
 * with rdt_jitter_buffer_set_tail_changed() to be notified when an older packet
 * was inserted in the queue.
 *
 * Returns: a #GstBuffer or %NULL when there was no packet in the queue.
 */
GstBuffer *
rdt_jitter_buffer_peek (RDTJitterBuffer * jbuf)
{
  GstBuffer *buf;

  g_return_val_if_fail (jbuf != NULL, FALSE);

  buf = g_queue_peek_tail (jbuf->packets);

  return buf;
}

/**
 * rdt_jitter_buffer_flush:
 * @jbuf: an #RDTJitterBuffer
 *
 * Flush all packets from the jitterbuffer.
 */
void
rdt_jitter_buffer_flush (RDTJitterBuffer * jbuf)
{
  GstBuffer *buffer;

  g_return_if_fail (jbuf != NULL);

  while ((buffer = g_queue_pop_head (jbuf->packets)))
    gst_buffer_unref (buffer);
}

/**
 * rdt_jitter_buffer_num_packets:
 * @jbuf: an #RDTJitterBuffer
 *
 * Get the number of packets currently in "jbuf.
 *
 * Returns: The number of packets in @jbuf.
 */
guint
rdt_jitter_buffer_num_packets (RDTJitterBuffer * jbuf)
{
  g_return_val_if_fail (jbuf != NULL, 0);

  return jbuf->packets->length;
}

/**
 * rdt_jitter_buffer_get_ts_diff:
 * @jbuf: an #RDTJitterBuffer
 *
 * Get the difference between the timestamps of first and last packet in the
 * jitterbuffer.
 *
 * Returns: The difference expressed in the timestamp units of the packets.
 */
guint32
rdt_jitter_buffer_get_ts_diff (RDTJitterBuffer * jbuf)
{
  guint64 high_ts, low_ts;
  GstBuffer *high_buf, *low_buf;
  guint32 result;

  g_return_val_if_fail (jbuf != NULL, 0);

  high_buf = g_queue_peek_head (jbuf->packets);
  low_buf = g_queue_peek_tail (jbuf->packets);

  if (!high_buf || !low_buf || high_buf == low_buf)
    return 0;

  //high_ts = gst_rtp_buffer_get_timestamp (high_buf);
  //low_ts = gst_rtp_buffer_get_timestamp (low_buf);
  high_ts = 0;
  low_ts = 0;

  /* it needs to work if ts wraps */
  if (high_ts >= low_ts) {
    result = (guint32) (high_ts - low_ts);
  } else {
    result = (guint32) (high_ts + G_MAXUINT32 + 1 - low_ts);
  }
  return result;
}
