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
  stats->bandwidth = RTP_STATS_BANDWIDTH;
  stats->sender_fraction = RTP_STATS_SENDER_FRACTION;
  stats->receiver_fraction = RTP_STATS_RECEIVER_FRACTION;
  stats->rtcp_bandwidth = RTP_STATS_RTCP_BANDWIDTH;
  stats->min_interval = RTP_STATS_MIN_INTERVAL;
}

/**
 * rtp_stats_calculate_rtcp_interval:
 * @stats: an #RTPSessionStats struct
 * 
 * Calculate the RTCP interval. The result of this function is the amount of
 * time to wait (in seconds) before sender a new RTCP message.
 *
 * Returns: the RTCP interval.
 */
gdouble
rtp_stats_calculate_rtcp_interval (RTPSessionStats * stats, gboolean sender)
{
  gdouble active, senders, receivers, sfraction;
  gboolean avg_rtcp;
  gdouble interval;

  active = stats->active_sources;
  /* Try to avoid division by zero */
  if (stats->active_sources == 0)
    active += 1.0;

  senders = (gdouble) stats->sender_sources;
  receivers = (gdouble) (active - senders);
  avg_rtcp = (gdouble) stats->avg_rtcp_packet_size;

  sfraction = senders / active;

  GST_DEBUG ("senders: %f, receivers %f, avg_rtcp %f, sfraction %f",
      senders, receivers, avg_rtcp, sfraction);

  if (senders > 0 && sfraction <= stats->sender_fraction) {
    if (sender) {
      interval =
          (avg_rtcp * senders) / (stats->sender_fraction *
          stats->rtcp_bandwidth);
    } else {
      interval =
          (avg_rtcp * receivers) / ((1.0 -
              stats->sender_fraction) * stats->rtcp_bandwidth);
    }
  } else {
    interval = (avg_rtcp * active) / stats->rtcp_bandwidth;
  }

  if (interval < stats->min_interval)
    interval = stats->min_interval;

  if (!stats->sent_rtcp)
    interval /= 2.0;

  return interval;
}

/**
 * rtp_stats_calculate_rtcp_interval:
 * @stats: an #RTPSessionStats struct
 * @interval: an RTCP interval
 * 
 * Apply a random jitter to the @interval. @interval is typically obtained with
 * rtp_stats_calculate_rtcp_interval().
 *
 * Returns: the new RTCP interval.
 */
gdouble
rtp_stats_add_rtcp_jitter (RTPSessionStats * stats, gdouble interval)
{
  /* see RFC 3550 p 30 
   * To compensate for "unconditional reconsideration" converging to a
   * value below the intended average.
   */
#define COMPENSATION  (2.71828 - 1.5);

  return (interval * g_random_double_range (0.5, 1.5)) / COMPENSATION;
}
