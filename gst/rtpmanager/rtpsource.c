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

#include <gst/rtp/gstrtpbuffer.h>
#include <gst/rtp/gstrtcpbuffer.h>

#include "rtpsource.h"

GST_DEBUG_CATEGORY_STATIC (rtp_source_debug);
#define GST_CAT_DEFAULT rtp_source_debug

#define RTP_MAX_PROBATION_LEN	32

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
static void rtp_source_finalize (GObject * object);

/* static guint rtp_source_signals[LAST_SIGNAL] = { 0 }; */

G_DEFINE_TYPE (RTPSource, rtp_source, G_TYPE_OBJECT);

static void
rtp_source_class_init (RTPSourceClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = rtp_source_finalize;

  GST_DEBUG_CATEGORY_INIT (rtp_source_debug, "rtpsource", 0, "RTP Source");
}

static void
rtp_source_init (RTPSource * src)
{
  /* sources are initialy on probation until we receive enough valid RTP
   * packets or a valid RTCP packet */
  src->validated = FALSE;
  src->probation = RTP_DEFAULT_PROBATION;

  src->payload = 0;
  src->clock_rate = -1;
  src->clock_base = -1;
  src->skew_base_ntpnstime = -1;
  src->ext_rtptime = -1;
  src->prev_ext_rtptime = -1;
  src->packets = g_queue_new ();
  src->seqnum_base = -1;

  src->stats.cycles = -1;
  src->stats.jitter = 0;
  src->stats.transit = -1;
  src->stats.curr_sr = 0;
  src->stats.curr_rr = 0;
}

static void
rtp_source_finalize (GObject * object)
{
  RTPSource *src;
  GstBuffer *buffer;

  src = RTP_SOURCE_CAST (object);

  while ((buffer = g_queue_pop_head (src->packets)))
    gst_buffer_unref (buffer);
  g_queue_free (src->packets);

  G_OBJECT_CLASS (rtp_source_parent_class)->finalize (object);
}

/**
 * rtp_source_new:
 * @ssrc: an SSRC
 *
 * Create a #RTPSource with @ssrc.
 *
 * Returns: a new #RTPSource. Use g_object_unref() after usage.
 */
RTPSource *
rtp_source_new (guint32 ssrc)
{
  RTPSource *src;

  src = g_object_new (RTP_TYPE_SOURCE, NULL);
  src->ssrc = ssrc;

  return src;
}

/**
 * rtp_source_update_caps:
 * @src: an #RTPSource
 * @caps: a #GstCaps
 *
 * Parse @caps and store all relevant information in @source.
 */
void
rtp_source_update_caps (RTPSource * src, GstCaps * caps)
{
  GstStructure *s;
  guint val;
  gint ival;

  /* nothing changed, return */
  if (src->caps == caps)
    return;

  s = gst_caps_get_structure (caps, 0);

  if (gst_structure_get_int (s, "payload", &ival))
    src->payload = ival;
  GST_DEBUG ("got payload %d", src->payload);

  gst_structure_get_int (s, "clock-rate", &src->clock_rate);
  GST_DEBUG ("got clock-rate %d", src->clock_rate);

  if (gst_structure_get_uint (s, "clock-base", &val))
    src->clock_base = val;
  GST_DEBUG ("got clock-base %" G_GINT64_FORMAT, src->clock_base);

  if (gst_structure_get_uint (s, "seqnum-base", &val))
    src->seqnum_base = val;
  GST_DEBUG ("got seqnum-base %" G_GINT32_FORMAT, src->seqnum_base);

  gst_caps_replace (&src->caps, caps);
}

/**
 * rtp_source_set_callbacks:
 * @src: an #RTPSource
 * @cb: callback functions
 * @user_data: user data
 *
 * Set the callbacks for the source.
 */
void
rtp_source_set_callbacks (RTPSource * src, RTPSourceCallbacks * cb,
    gpointer user_data)
{
  g_return_if_fail (RTP_IS_SOURCE (src));

  src->callbacks.push_rtp = cb->push_rtp;
  src->callbacks.clock_rate = cb->clock_rate;
  src->user_data = user_data;
}

/**
 * rtp_source_set_as_csrc:
 * @src: an #RTPSource
 *
 * Configure @src as a CSRC, this will validate the RTpSource.
 */
void
rtp_source_set_as_csrc (RTPSource * src)
{
  g_return_if_fail (RTP_IS_SOURCE (src));

  src->validated = TRUE;
  src->is_csrc = TRUE;
}

/**
 * rtp_source_set_rtp_from:
 * @src: an #RTPSource
 * @address: the RTP address to set
 *
 * Set that @src is receiving RTP packets from @address. This is used for
 * collistion checking.
 */
void
rtp_source_set_rtp_from (RTPSource * src, GstNetAddress * address)
{
  g_return_if_fail (RTP_IS_SOURCE (src));

  src->have_rtp_from = TRUE;
  memcpy (&src->rtp_from, address, sizeof (GstNetAddress));
}

/**
 * rtp_source_set_rtcp_from:
 * @src: an #RTPSource
 * @address: the RTCP address to set
 *
 * Set that @src is receiving RTCP packets from @address. This is used for
 * collistion checking.
 */
void
rtp_source_set_rtcp_from (RTPSource * src, GstNetAddress * address)
{
  g_return_if_fail (RTP_IS_SOURCE (src));

  src->have_rtcp_from = TRUE;
  memcpy (&src->rtcp_from, address, sizeof (GstNetAddress));
}

static GstFlowReturn
push_packet (RTPSource * src, GstBuffer * buffer)
{
  GstFlowReturn ret = GST_FLOW_OK;

  /* push queued packets first if any */
  while (!g_queue_is_empty (src->packets)) {
    GstBuffer *buffer = GST_BUFFER_CAST (g_queue_pop_head (src->packets));

    GST_DEBUG ("pushing queued packet");
    if (src->callbacks.push_rtp)
      src->callbacks.push_rtp (src, buffer, src->user_data);
    else
      gst_buffer_unref (buffer);
  }
  GST_DEBUG ("pushing new packet");
  /* push packet */
  if (src->callbacks.push_rtp)
    ret = src->callbacks.push_rtp (src, buffer, src->user_data);
  else
    gst_buffer_unref (buffer);

  return ret;
}

static gint
get_clock_rate (RTPSource * src, guint8 payload)
{
  if (src->clock_rate == -1) {
    gint clock_rate = -1;

    if (src->callbacks.clock_rate)
      clock_rate = src->callbacks.clock_rate (src, payload, src->user_data);

    GST_DEBUG ("new payload %d, got clock-rate %d", payload, clock_rate);

    src->clock_rate = clock_rate;
  }
  src->payload = payload;

  return src->clock_rate;
}

static void
calculate_jitter (RTPSource * src, GstBuffer * buffer,
    RTPArrivalStats * arrival)
{
  guint64 ntpnstime;
  guint32 rtparrival, transit, rtptime;
  guint64 ext_rtptime;
  gint32 diff;
  gint clock_rate;
  guint8 pt;
  guint64 rtpdiff, ntpdiff;
  gint64 skew;

  /* get arrival time */
  if ((ntpnstime = arrival->ntpnstime) == GST_CLOCK_TIME_NONE)
    goto no_time;

  pt = gst_rtp_buffer_get_payload_type (buffer);

  /* get clockrate */
  if ((clock_rate = get_clock_rate (src, pt)) == -1)
    goto no_clock_rate;

  rtptime = gst_rtp_buffer_get_timestamp (buffer);

  /* convert to extended timestamp right away */
  ext_rtptime = gst_rtp_buffer_ext_timestamp (&src->ext_rtptime, rtptime);

  /* no clock-base, take first rtptime as base */
  if (src->clock_base == -1) {
    GST_DEBUG ("using clock-base of %" G_GUINT32_FORMAT, rtptime);
    src->clock_base = rtptime;
  }

  if (src->skew_base_ntpnstime == -1) {
    /* lock on first observed NTP and RTP time, they should increment in-sync or
     * we have a clock skew. */
    GST_DEBUG ("using base_ntpnstime of %" GST_TIME_FORMAT,
        GST_TIME_ARGS (ntpnstime));
    src->skew_base_ntpnstime = ntpnstime;
    src->skew_base_rtptime = rtptime;
    src->prev_ext_rtptime = ext_rtptime;
    src->avg_skew = 0;
  } else if (src->prev_ext_rtptime < ext_rtptime) {
    /* get elapsed rtptime but only when the previous rtptime was stricly smaller
     * than the new one. */
    rtpdiff = ext_rtptime - src->skew_base_rtptime;
    /* get NTP diff and convert to RTP time, this is always positive */
    ntpdiff = ntpnstime - src->skew_base_ntpnstime;
    ntpdiff = gst_util_uint64_scale_int (ntpdiff, clock_rate, GST_SECOND);

    /* see how the NTP and RTP relate any deviation from 0 means that they drift
     * out of sync and we must compensate. */
    skew = ntpdiff - rtpdiff;
    /* average out the skew to get a smooth value. */
    src->avg_skew = (31 * src->avg_skew + skew) / 32;

    GST_DEBUG ("skew %" G_GINT64_FORMAT ", avg %" G_GINT64_FORMAT, skew,
        src->avg_skew);
    if (src->avg_skew != 0) {
      guint32 timestamp;

      /* patch the buffer RTP timestamp with the skew */
      GST_DEBUG ("adjusting timestamp %" G_GINT64_FORMAT, src->avg_skew);
      timestamp = gst_rtp_buffer_get_timestamp (buffer);
      timestamp += src->avg_skew;
      gst_rtp_buffer_set_timestamp (buffer, timestamp);
    }
    /* store previous extended timestamp */
    src->prev_ext_rtptime = ext_rtptime;
  }

  /* convert arrival time to RTP timestamp units, truncate to 32 bits, we don't
   * care about the absolute value, just the difference. */
  rtparrival = gst_util_uint64_scale_int (ntpnstime, clock_rate, GST_SECOND);

  /* transit time is difference with RTP timestamp */
  transit = rtparrival - rtptime;

  /* get ABS diff with previous transit time */
  if (src->stats.transit != -1) {
    if (transit > src->stats.transit)
      diff = transit - src->stats.transit;
    else
      diff = src->stats.transit - transit;
  } else
    diff = 0;

  src->stats.transit = transit;

  /* update jitter, the value we store is scaled up so we can keep precision. */
  src->stats.jitter += diff - ((src->stats.jitter + 8) >> 4);

  src->stats.prev_rtptime = src->stats.last_rtptime;
  src->stats.last_rtptime = rtparrival;

  GST_DEBUG ("rtparrival %u, rtptime %u, clock-rate %d, diff %d, jitter: %f",
      rtparrival, rtptime, clock_rate, diff, (src->stats.jitter) / 16.0);

  return;

  /* ERRORS */
no_time:
  {
    GST_WARNING ("cannot get current time");
    return;
  }
no_clock_rate:
  {
    GST_WARNING ("cannot get clock-rate for pt %d", pt);
    return;
  }
}

static void
init_seq (RTPSource * src, guint16 seq)
{
  src->stats.base_seq = seq;
  src->stats.max_seq = seq;
  src->stats.bad_seq = RTP_SEQ_MOD + 1; /* so seq == bad_seq is false */
  src->stats.cycles = 0;
  src->stats.packets_received = 0;
  src->stats.octets_received = 0;
  src->stats.bytes_received = 0;
  src->stats.prev_received = 0;
  src->stats.prev_expected = 0;

  GST_DEBUG ("base_seq %d", seq);
}

/**
 * rtp_source_process_rtp:
 * @src: an #RTPSource
 * @buffer: an RTP buffer
 *
 * Let @src handle the incomming RTP @buffer.
 *
 * Returns: a #GstFlowReturn.
 */
GstFlowReturn
rtp_source_process_rtp (RTPSource * src, GstBuffer * buffer,
    RTPArrivalStats * arrival)
{
  GstFlowReturn result = GST_FLOW_OK;
  guint16 seqnr, udelta;
  RTPSourceStats *stats;

  g_return_val_if_fail (RTP_IS_SOURCE (src), GST_FLOW_ERROR);
  g_return_val_if_fail (GST_IS_BUFFER (buffer), GST_FLOW_ERROR);

  stats = &src->stats;

  seqnr = gst_rtp_buffer_get_seq (buffer);

  rtp_source_update_caps (src, GST_BUFFER_CAPS (buffer));

  if (stats->cycles == -1) {
    GST_DEBUG ("received first buffer");
    /* first time we heard of this source */
    init_seq (src, seqnr);
    src->stats.max_seq = seqnr - 1;
    src->probation = RTP_DEFAULT_PROBATION;
  }

  udelta = seqnr - stats->max_seq;

  /* if we are still on probation, check seqnum */
  if (src->probation) {
    guint16 expected;

    expected = src->stats.max_seq + 1;

    /* when in probation, we require consecutive seqnums */
    if (seqnr == expected) {
      /* expected packet */
      GST_DEBUG ("probation: seqnr %d == expected %d", seqnr, expected);
      src->probation--;
      src->stats.max_seq = seqnr;
      if (src->probation == 0) {
        GST_DEBUG ("probation done!");
        init_seq (src, seqnr);
      } else {
        GstBuffer *q;

        GST_DEBUG ("probation %d: queue buffer", src->probation);
        /* when still in probation, keep packets in a list. */
        g_queue_push_tail (src->packets, buffer);
        /* remove packets from queue if there are too many */
        while (g_queue_get_length (src->packets) > RTP_MAX_PROBATION_LEN) {
          q = g_queue_pop_head (src->packets);
          gst_object_unref (q);
        }
        goto done;
      }
    } else {
      GST_DEBUG ("probation: seqnr %d != expected %d", seqnr, expected);
      src->probation = RTP_DEFAULT_PROBATION;
      src->stats.max_seq = seqnr;
      goto done;
    }
  } else if (udelta < RTP_MAX_DROPOUT) {
    /* in order, with permissible gap */
    if (seqnr < stats->max_seq) {
      /* sequence number wrapped - count another 64K cycle. */
      stats->cycles += RTP_SEQ_MOD;
    }
    stats->max_seq = seqnr;
  } else if (udelta <= RTP_SEQ_MOD - RTP_MAX_MISORDER) {
    /* the sequence number made a very large jump */
    if (seqnr == stats->bad_seq) {
      /* two sequential packets -- assume that the other side
       * restarted without telling us so just re-sync
       * (i.e., pretend this was the first packet).  */
      init_seq (src, seqnr);
    } else {
      /* unacceptable jump */
      stats->bad_seq = (seqnr + 1) & (RTP_SEQ_MOD - 1);
      goto bad_sequence;
    }
  } else {
    /* duplicate or reordered packet, will be filtered by jitterbuffer. */
    GST_WARNING ("duplicate or reordered packet");
  }

  src->stats.octets_received += arrival->payload_len;
  src->stats.bytes_received += arrival->bytes;
  src->stats.packets_received++;
  /* the source that sent the packet must be a sender */
  src->is_sender = TRUE;
  src->validated = TRUE;

  GST_DEBUG ("seq %d, PC: %" G_GUINT64_FORMAT ", OC: %" G_GUINT64_FORMAT,
      seqnr, src->stats.packets_received, src->stats.octets_received);

  /* calculate jitter and perform skew correction */
  calculate_jitter (src, buffer, arrival);

  /* we're ready to push the RTP packet now */
  result = push_packet (src, buffer);

done:
  return result;

  /* ERRORS */
bad_sequence:
  {
    GST_WARNING ("unacceptable seqnum received");
    return GST_FLOW_OK;
  }
}

/**
 * rtp_source_process_bye:
 * @src: an #RTPSource
 * @reason: the reason for leaving
 *
 * Notify @src that a BYE packet has been received. This will make the source
 * inactive.
 */
void
rtp_source_process_bye (RTPSource * src, const gchar * reason)
{
  g_return_if_fail (RTP_IS_SOURCE (src));

  GST_DEBUG ("marking SSRC %08x as BYE, reason: %s", src->ssrc,
      GST_STR_NULL (reason));

  /* copy the reason and mark as received_bye */
  g_free (src->bye_reason);
  src->bye_reason = g_strdup (reason);
  src->received_bye = TRUE;
}

/**
 * rtp_source_send_rtp:
 * @src: an #RTPSource
 * @buffer: an RTP buffer
 * @ntpnstime: the NTP time when this buffer was captured in nanoseconds
 *
 * Send an RTP @buffer originating from @src. This will make @src a sender.
 * This function takes ownership of @buffer and modifies the SSRC in the RTP
 * packet to that of @src when needed.
 *
 * Returns: a #GstFlowReturn.
 */
GstFlowReturn
rtp_source_send_rtp (RTPSource * src, GstBuffer * buffer, guint64 ntpnstime)
{
  GstFlowReturn result = GST_FLOW_OK;
  guint len;

  g_return_val_if_fail (RTP_IS_SOURCE (src), GST_FLOW_ERROR);
  g_return_val_if_fail (GST_IS_BUFFER (buffer), GST_FLOW_ERROR);

  len = gst_rtp_buffer_get_payload_len (buffer);

  rtp_source_update_caps (src, GST_BUFFER_CAPS (buffer));

  /* we are a sender now */
  src->is_sender = TRUE;

  /* update stats for the SR */
  src->stats.packets_sent++;
  src->stats.octets_sent += len;

  /* we keep track of the last received RTP timestamp and the corresponding
   * NTP timestamp so that we can use this info when constructing SR reports */
  src->last_rtptime = gst_rtp_buffer_get_timestamp (buffer);
  src->last_ntpnstime = ntpnstime;

  /* push packet */
  if (src->callbacks.push_rtp) {
    guint32 ssrc;

    ssrc = gst_rtp_buffer_get_ssrc (buffer);
    if (ssrc != src->ssrc) {
      /* the SSRC of the packet is not correct, make a writable buffer and
       * update the SSRC. This could involve a complete copy of the packet when
       * it is not writable. Usually the payloader will use caps negotiation to
       * get the correct SSRC. */
      buffer = gst_buffer_make_writable (buffer);

      GST_DEBUG ("updating SSRC from %08x to %08x", ssrc, src->ssrc);
      gst_rtp_buffer_set_ssrc (buffer, src->ssrc);
    }
    GST_DEBUG ("pushing RTP packet %" G_GUINT64_FORMAT,
        src->stats.packets_sent);
    result = src->callbacks.push_rtp (src, buffer, src->user_data);
  } else {
    GST_DEBUG ("no callback installed");
    gst_buffer_unref (buffer);
  }

  return result;
}

/**
 * rtp_source_process_sr:
 * @src: an #RTPSource
 * @time: time of packet arrival
 * @ntptime: the NTP time
 * @rtptime: the RTP time
 * @packet_count: the packet count
 * @octet_count: the octect count
 *
 * Update the sender report in @src.
 */
void
rtp_source_process_sr (RTPSource * src, GstClockTime time, guint64 ntptime,
    guint32 rtptime, guint32 packet_count, guint32 octet_count)
{
  RTPSenderReport *curr;
  gint curridx;

  g_return_if_fail (RTP_IS_SOURCE (src));

  GST_DEBUG ("got SR packet: SSRC %08x, NTP %08x:%08x, RTP %" G_GUINT32_FORMAT
      ", PC %" G_GUINT32_FORMAT ", OC %" G_GUINT32_FORMAT, src->ssrc,
      (guint32) (ntptime >> 32), (guint32) (ntptime & 0xffffffff), rtptime,
      packet_count, octet_count);

  curridx = src->stats.curr_sr ^ 1;
  curr = &src->stats.sr[curridx];

  /* this is a sender now */
  src->is_sender = TRUE;

  /* update current */
  curr->is_valid = TRUE;
  curr->ntptime = ntptime;
  curr->rtptime = rtptime;
  curr->packet_count = packet_count;
  curr->octet_count = octet_count;
  curr->time = time;

  /* make current */
  src->stats.curr_sr = curridx;
}

/**
 * rtp_source_process_rb:
 * @src: an #RTPSource
 * @time: the current time in nanoseconds since 1970
 * @fractionlost: fraction lost since last SR/RR
 * @packetslost: the cumululative number of packets lost
 * @exthighestseq: the extended last sequence number received
 * @jitter: the interarrival jitter
 * @lsr: the last SR packet from this source
 * @dlsr: the delay since last SR packet
 *
 * Update the report block in @src.
 */
void
rtp_source_process_rb (RTPSource * src, GstClockTime time, guint8 fractionlost,
    gint32 packetslost, guint32 exthighestseq, guint32 jitter, guint32 lsr,
    guint32 dlsr)
{
  RTPReceiverReport *curr;
  gint curridx;
  guint32 ntp, A;

  g_return_if_fail (RTP_IS_SOURCE (src));

  GST_DEBUG ("got RB packet: SSRC %08x, FL %2x, PL %d, HS %" G_GUINT32_FORMAT
      ", jitter %" G_GUINT32_FORMAT ", LSR %04x:%04x, DLSR %04x:%04x",
      src->ssrc, fractionlost, packetslost, exthighestseq, jitter, lsr >> 16,
      lsr & 0xffff, dlsr >> 16, dlsr & 0xffff);

  curridx = src->stats.curr_rr ^ 1;
  curr = &src->stats.rr[curridx];

  /* update current */
  curr->is_valid = TRUE;
  curr->fractionlost = fractionlost;
  curr->packetslost = packetslost;
  curr->exthighestseq = exthighestseq;
  curr->jitter = jitter;
  curr->lsr = lsr;
  curr->dlsr = dlsr;

  /* calculate round trip */
  ntp = (gst_rtcp_unix_to_ntp (time) >> 16) & 0xffffffff;
  A = ntp - dlsr;
  A -= lsr;
  curr->round_trip = A;

  GST_DEBUG ("NTP %04x:%04x, round trip %04x:%04x", ntp >> 16, ntp & 0xffff,
      A >> 16, A & 0xffff);

  /* make current */
  src->stats.curr_rr = curridx;
}

/**
 * rtp_source_get_new_sr:
 * @src: an #RTPSource
 * @time: the current time in nanoseconds since 1970
 * @ntptime: the NTP time
 * @rtptime: the RTP time
 * @packet_count: the packet count
 * @octet_count: the octect count
 *
 * Get new values to put into a new SR report from this source.
 *
 * Returns: %TRUE on success.
 */
gboolean
rtp_source_get_new_sr (RTPSource * src, GstClockTime ntpnstime,
    guint64 * ntptime, guint32 * rtptime, guint32 * packet_count,
    guint32 * octet_count)
{
  guint32 t_rtp;
  guint64 t_current_ntp;
  GstClockTimeDiff diff;

  g_return_val_if_fail (RTP_IS_SOURCE (src), FALSE);

  /* use the sync params to interpollate the date->time member to rtptime. We
   * use the last sent timestamp and rtptime as reference points. We assume
   * that the slope of the rtptime vs timestamp curve is 1, which is certainly
   * sufficient for the frequency at which we report SR and the rate we send
   * out RTP packets. */
  t_rtp = src->last_rtptime;

  GST_DEBUG ("last_ntpnstime %" GST_TIME_FORMAT ", last_rtptime %"
      G_GUINT32_FORMAT, GST_TIME_ARGS (src->last_ntpnstime), t_rtp);

  if (src->clock_rate != -1) {
    /* get the diff with the SR time */
    diff = GST_CLOCK_DIFF (src->last_ntpnstime, ntpnstime);

    /* now translate the diff to RTP time, handle positive and negative cases.
     * If there is no diff, we already set rtptime correctly above. */
    if (diff > 0) {
      GST_DEBUG ("ntpnstime %" GST_TIME_FORMAT ", diff %" GST_TIME_FORMAT,
          GST_TIME_ARGS (ntpnstime), GST_TIME_ARGS (diff));
      t_rtp += gst_util_uint64_scale_int (diff, src->clock_rate, GST_SECOND);
    } else {
      diff = -diff;
      GST_DEBUG ("ntpnstime %" GST_TIME_FORMAT ", diff -%" GST_TIME_FORMAT,
          GST_TIME_ARGS (ntpnstime), GST_TIME_ARGS (diff));
      t_rtp -= gst_util_uint64_scale_int (diff, src->clock_rate, GST_SECOND);
    }
  } else {
    GST_WARNING ("no clock-rate, cannot interpollate rtp time");
  }

  t_current_ntp = gst_util_uint64_scale (ntpnstime, (1LL << 32), GST_SECOND);

  GST_DEBUG ("NTP %08x:%08x, RTP %" G_GUINT32_FORMAT,
      (guint32) (t_current_ntp >> 32), (guint32) (t_current_ntp & 0xffffffff),
      t_rtp);

  if (ntptime)
    *ntptime = t_current_ntp;
  if (rtptime)
    *rtptime = t_rtp;
  if (packet_count)
    *packet_count = src->stats.packets_sent;
  if (octet_count)
    *octet_count = src->stats.octets_sent;

  return TRUE;
}

/**
 * rtp_source_get_new_rb:
 * @src: an #RTPSource
 * @time: the current time in nanoseconds since 1970
 * @fractionlost: fraction lost since last SR/RR
 * @packetslost: the cumululative number of packets lost
 * @exthighestseq: the extended last sequence number received
 * @jitter: the interarrival jitter
 * @lsr: the last SR packet from this source
 * @dlsr: the delay since last SR packet
 *
 * Get the values of the last RB report set with rtp_source_process_rb().
 *
 * Returns: %TRUE on success.
 */
gboolean
rtp_source_get_new_rb (RTPSource * src, GstClockTime time,
    guint8 * fractionlost, gint32 * packetslost, guint32 * exthighestseq,
    guint32 * jitter, guint32 * lsr, guint32 * dlsr)
{
  RTPSourceStats *stats;
  guint64 extended_max, expected;
  guint64 expected_interval, received_interval, ntptime;
  gint64 lost, lost_interval;
  guint32 fraction, LSR, DLSR;
  GstClockTime sr_time;

  stats = &src->stats;

  extended_max = stats->cycles + stats->max_seq;
  expected = extended_max - stats->base_seq + 1;

  GST_DEBUG ("ext_max %" G_GUINT64_FORMAT ", expected %" G_GUINT64_FORMAT
      ", received %" G_GUINT64_FORMAT ", base_seq %" G_GUINT32_FORMAT,
      extended_max, expected, stats->packets_received, stats->base_seq);

  lost = expected - stats->packets_received;
  lost = CLAMP (lost, -0x800000, 0x7fffff);

  expected_interval = expected - stats->prev_expected;
  stats->prev_expected = expected;
  received_interval = stats->packets_received - stats->prev_received;
  stats->prev_received = stats->packets_received;

  lost_interval = expected_interval - received_interval;

  if (expected_interval == 0 || lost_interval <= 0)
    fraction = 0;
  else
    fraction = (lost_interval << 8) / expected_interval;

  GST_DEBUG ("add RR for SSRC %08x", src->ssrc);
  /* we scaled the jitter up for additional precision */
  GST_DEBUG ("fraction %" G_GUINT32_FORMAT ", lost %" G_GINT64_FORMAT
      ", extseq %" G_GUINT64_FORMAT ", jitter %d", fraction, lost,
      extended_max, stats->jitter >> 4);

  if (rtp_source_get_last_sr (src, &sr_time, &ntptime, NULL, NULL, NULL)) {
    GstClockTime diff;

    /* LSR is middle 32 bits of the last ntptime */
    LSR = (ntptime >> 16) & 0xffffffff;
    diff = time - sr_time;
    GST_DEBUG ("last SR time diff %" GST_TIME_FORMAT, GST_TIME_ARGS (diff));
    /* DLSR, delay since last SR is expressed in 1/65536 second units */
    DLSR = gst_util_uint64_scale_int (diff, 65536, GST_SECOND);
  } else {
    /* No valid SR received, LSR/DLSR are set to 0 then */
    GST_DEBUG ("no valid SR received");
    LSR = 0;
    DLSR = 0;
  }
  GST_DEBUG ("LSR %04x:%04x, DLSR %04x:%04x", LSR >> 16, LSR & 0xffff,
      DLSR >> 16, DLSR & 0xffff);

  if (fractionlost)
    *fractionlost = fraction;
  if (packetslost)
    *packetslost = lost;
  if (exthighestseq)
    *exthighestseq = extended_max;
  if (jitter)
    *jitter = stats->jitter >> 4;
  if (lsr)
    *lsr = LSR;
  if (dlsr)
    *dlsr = DLSR;

  return TRUE;
}

/**
 * rtp_source_get_last_sr:
 * @src: an #RTPSource
 * @time: time of packet arrival
 * @ntptime: the NTP time
 * @rtptime: the RTP time
 * @packet_count: the packet count
 * @octet_count: the octect count
 *
 * Get the values of the last sender report as set with rtp_source_process_sr().
 *
 * Returns: %TRUE if there was a valid SR report.
 */
gboolean
rtp_source_get_last_sr (RTPSource * src, GstClockTime * time, guint64 * ntptime,
    guint32 * rtptime, guint32 * packet_count, guint32 * octet_count)
{
  RTPSenderReport *curr;

  g_return_val_if_fail (RTP_IS_SOURCE (src), FALSE);

  curr = &src->stats.sr[src->stats.curr_sr];
  if (!curr->is_valid)
    return FALSE;

  if (ntptime)
    *ntptime = curr->ntptime;
  if (rtptime)
    *rtptime = curr->rtptime;
  if (packet_count)
    *packet_count = curr->packet_count;
  if (octet_count)
    *octet_count = curr->octet_count;
  if (time)
    *time = curr->time;

  return TRUE;
}

/**
 * rtp_source_get_last_rb:
 * @src: an #RTPSource
 * @fractionlost: fraction lost since last SR/RR
 * @packetslost: the cumululative number of packets lost
 * @exthighestseq: the extended last sequence number received
 * @jitter: the interarrival jitter
 * @lsr: the last SR packet from this source
 * @dlsr: the delay since last SR packet
 *
 * Get the values of the last RB report set with rtp_source_process_rb().
 *
 * Returns: %TRUE if there was a valid SB report.
 */
gboolean
rtp_source_get_last_rb (RTPSource * src, guint8 * fractionlost,
    gint32 * packetslost, guint32 * exthighestseq, guint32 * jitter,
    guint32 * lsr, guint32 * dlsr)
{
  RTPReceiverReport *curr;

  g_return_val_if_fail (RTP_IS_SOURCE (src), FALSE);

  curr = &src->stats.rr[src->stats.curr_rr];
  if (!curr->is_valid)
    return FALSE;

  if (fractionlost)
    *fractionlost = curr->fractionlost;
  if (packetslost)
    *packetslost = curr->packetslost;
  if (exthighestseq)
    *exthighestseq = curr->exthighestseq;
  if (jitter)
    *jitter = curr->jitter;
  if (lsr)
    *lsr = curr->lsr;
  if (dlsr)
    *dlsr = curr->dlsr;

  return TRUE;
}
