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

#include "rtpstats.h"

/**
 * rtp_stats_init_defaults:
 * @stats: an #RTPSessionStats struct
 *
 * Initialize @stats with its default values.
 */
void
rtp_stats_init_defaults (RTPSessionStats * stats)
{
  rtp_stats_set_bandwidths (stats, -1, -1, -1, -1);
  stats->min_interval = RTP_STATS_MIN_INTERVAL;
  stats->bye_timeout = RTP_STATS_BYE_TIMEOUT;
  stats->nacks_dropped = 0;
  stats->nacks_sent = 0;
  stats->nacks_received = 0;
}

/**
 * rtp_stats_set_bandwidths:
 * @stats: an #RTPSessionStats struct
 * @rtp_bw: RTP bandwidth
 * @rtcp_bw: RTCP bandwidth
 * @rs: sender RTCP bandwidth
 * @rr: receiver RTCP bandwidth
 *
 * Configure the bandwidth parameters in the stats. When an input variable is
 * set to -1, it will be calculated from the other input variables and from the
 * defaults.
 */
void
rtp_stats_set_bandwidths (RTPSessionStats * stats, guint rtp_bw,
    gdouble rtcp_bw, guint rs, guint rr)
{
  GST_DEBUG ("recalc bandwidths: RTP %u, RTCP %f, RS %u, RR %u", rtp_bw,
      rtcp_bw, rs, rr);

  /* when given, sender and receive bandwidth add up to the total
   * rtcp bandwidth */
  if (rs != -1 && rr != -1)
    rtcp_bw = rs + rr;

  /* If rtcp_bw is between 0 and 1, it is a fraction of rtp_bw */
  if (rtcp_bw > 0.0 && rtcp_bw < 1.0) {
    if (rtp_bw > 0.0)
      rtcp_bw = rtp_bw * rtcp_bw;
    else
      rtcp_bw = -1.0;
  }

  /* RTCP is 5% of the RTP bandwidth */
  if (rtp_bw == -1 && rtcp_bw > 1.0)
    rtp_bw = rtcp_bw * 20;
  else if (rtp_bw != -1 && rtcp_bw < 0.0)
    rtcp_bw = rtp_bw / 20;
  else if (rtp_bw == -1 && rtcp_bw < 0.0) {
    /* nothing given, take defaults */
    rtp_bw = RTP_STATS_BANDWIDTH;
    rtcp_bw = rtp_bw * RTP_STATS_RTCP_FRACTION;
  }

  stats->bandwidth = rtp_bw;
  stats->rtcp_bandwidth = rtcp_bw;

  /* now figure out the fractions */
  if (rs == -1) {
    /* rs unknown */
    if (rr == -1) {
      /* both not given, use defaults */
      rs = stats->rtcp_bandwidth * RTP_STATS_SENDER_FRACTION;
      rr = stats->rtcp_bandwidth * RTP_STATS_RECEIVER_FRACTION;
    } else {
      /* rr known, calculate rs */
      if (stats->rtcp_bandwidth > rr)
        rs = stats->rtcp_bandwidth - rr;
      else
        rs = 0;
    }
  } else if (rr == -1) {
    /* rs known, calculate rr */
    if (stats->rtcp_bandwidth > rs)
      rr = stats->rtcp_bandwidth - rs;
    else
      rr = 0;
  }

  if (stats->rtcp_bandwidth > 0) {
    stats->sender_fraction = ((gdouble) rs) / ((gdouble) stats->rtcp_bandwidth);
    stats->receiver_fraction = 1.0 - stats->sender_fraction;
  } else {
    /* no RTCP bandwidth, set dummy values */
    stats->sender_fraction = 0.0;
    stats->receiver_fraction = 0.0;
  }
  GST_DEBUG ("bandwidths: RTP %u, RTCP %u, RS %f, RR %f", stats->bandwidth,
      stats->rtcp_bandwidth, stats->sender_fraction, stats->receiver_fraction);
}

/**
 * rtp_stats_calculate_rtcp_interval:
 * @stats: an #RTPSessionStats struct
 * @sender: if we are a sender
 * @first: if this is the first time
 *
 * Calculate the RTCP interval. The result of this function is the amount of
 * time to wait (in nanoseconds) before sending a new RTCP message.
 *
 * Returns: the RTCP interval.
 */
GstClockTime
rtp_stats_calculate_rtcp_interval (RTPSessionStats * stats, gboolean we_send,
    gboolean first)
{
  gdouble members, senders, n;
  gdouble avg_rtcp_size, rtcp_bw;
  gdouble interval;
  gdouble rtcp_min_time;

  /* Very first call at application start-up uses half the min
   * delay for quicker notification while still allowing some time
   * before reporting for randomization and to learn about other
   * sources so the report interval will converge to the correct
   * interval more quickly.
   */
  rtcp_min_time = stats->min_interval;
  if (first)
    rtcp_min_time /= 2.0;

  /* Dedicate a fraction of the RTCP bandwidth to senders unless
   * the number of senders is large enough that their share is
   * more than that fraction.
   */
  n = members = stats->active_sources;
  senders = (gdouble) stats->sender_sources;
  rtcp_bw = stats->rtcp_bandwidth;

  if (senders <= members * stats->sender_fraction) {
    if (we_send) {
      rtcp_bw *= stats->sender_fraction;
      n = senders;
    } else {
      rtcp_bw *= stats->receiver_fraction;
      n -= senders;
    }
  }

  /* no bandwidth for RTCP, return NONE to signal that we don't want to send
   * RTCP packets */
  if (rtcp_bw <= 0.00001)
    return GST_CLOCK_TIME_NONE;

  avg_rtcp_size = stats->avg_rtcp_packet_size;
  /*
   * The effective number of sites times the average packet size is
   * the total number of octets sent when each site sends a report.
   * Dividing this by the effective bandwidth gives the time
   * interval over which those packets must be sent in order to
   * meet the bandwidth target, with a minimum enforced.  In that
   * time interval we send one report so this time is also our
   * average time between reports.
   */
  GST_DEBUG ("avg size %f, n %f, rtcp_bw %f", avg_rtcp_size, n, rtcp_bw);
  interval = avg_rtcp_size * n / rtcp_bw;
  if (interval < rtcp_min_time)
    interval = rtcp_min_time;

  return interval * GST_SECOND;
}

/**
 * rtp_stats_add_rtcp_jitter:
 * @stats: an #RTPSessionStats struct
 * @interval: an RTCP interval
 *
 * Apply a random jitter to the @interval. @interval is typically obtained with
 * rtp_stats_calculate_rtcp_interval().
 *
 * Returns: the new RTCP interval.
 */
GstClockTime
rtp_stats_add_rtcp_jitter (RTPSessionStats * stats, GstClockTime interval)
{
  gdouble temp;

  /* see RFC 3550 p 30
   * To compensate for "unconditional reconsideration" converging to a
   * value below the intended average.
   */
#define COMPENSATION  (2.71828 - 1.5);

  temp = (interval * g_random_double_range (0.5, 1.5)) / COMPENSATION;

  return (GstClockTime) temp;
}


/**
 * rtp_stats_calculate_bye_interval:
 * @stats: an #RTPSessionStats struct
 *
 * Calculate the BYE interval. The result of this function is the amount of
 * time to wait (in nanoseconds) before sending a BYE message.
 *
 * Returns: the BYE interval.
 */
GstClockTime
rtp_stats_calculate_bye_interval (RTPSessionStats * stats)
{
  gdouble members;
  gdouble avg_rtcp_size, rtcp_bw;
  gdouble interval;
  gdouble rtcp_min_time;

  /* no interval when we have less than 50 members */
  if (stats->active_sources < 50)
    return 0;

  rtcp_min_time = (stats->min_interval) / 2.0;

  /* Dedicate a fraction of the RTCP bandwidth to senders unless
   * the number of senders is large enough that their share is
   * more than that fraction.
   */
  members = stats->bye_members;
  rtcp_bw = stats->rtcp_bandwidth * stats->receiver_fraction;

  /* no bandwidth for RTCP, return NONE to signal that we don't want to send
   * RTCP packets */
  if (rtcp_bw <= 0.0001)
    return GST_CLOCK_TIME_NONE;

  avg_rtcp_size = stats->avg_rtcp_packet_size;
  /*
   * The effective number of sites times the average packet size is
   * the total number of octets sent when each site sends a report.
   * Dividing this by the effective bandwidth gives the time
   * interval over which those packets must be sent in order to
   * meet the bandwidth target, with a minimum enforced.  In that
   * time interval we send one report so this time is also our
   * average time between reports.
   */
  interval = avg_rtcp_size * members / rtcp_bw;
  if (interval < rtcp_min_time)
    interval = rtcp_min_time;

  return interval * GST_SECOND;
}

/**
 * rtp_stats_get_packets_lost:
 * @stats: an #RTPSourceStats struct
 *
 * Calculate the total number of RTP packets lost since beginning of
 * reception. Packets that arrive late are not considered lost, and
 * duplicates are not taken into account. Hence, the loss may be negative
 * if there are duplicates.
 *
 * Returns: total RTP packets lost.
 */
gint64
rtp_stats_get_packets_lost (const RTPSourceStats * stats)
{
  gint64 lost;
  guint64 extended_max, expected;

  extended_max = stats->cycles + stats->max_seq;
  expected = extended_max - stats->base_seq + 1;
  lost = expected - stats->packets_received;

  return lost;
}

void
rtp_stats_set_min_interval (RTPSessionStats * stats, gdouble min_interval)
{
  stats->min_interval = min_interval;
}

gboolean
__g_socket_address_equal (GSocketAddress * a, GSocketAddress * b)
{
  GInetSocketAddress *ia, *ib;
  GInetAddress *iaa, *iab;

  ia = G_INET_SOCKET_ADDRESS (a);
  ib = G_INET_SOCKET_ADDRESS (b);

  if (g_inet_socket_address_get_port (ia) !=
      g_inet_socket_address_get_port (ib))
    return FALSE;

  iaa = g_inet_socket_address_get_address (ia);
  iab = g_inet_socket_address_get_address (ib);

  return g_inet_address_equal (iaa, iab);
}

gchar *
__g_socket_address_to_string (GSocketAddress * addr)
{
  GInetSocketAddress *ia;
  gchar *ret, *tmp;

  ia = G_INET_SOCKET_ADDRESS (addr);

  tmp = g_inet_address_to_string (g_inet_socket_address_get_address (ia));
  ret = g_strdup_printf ("%s:%u", tmp, g_inet_socket_address_get_port (ia));
  g_free (tmp);

  return ret;
}
