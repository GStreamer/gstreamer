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
#include <gst/netbuffer/gstnetbuffer.h>

#include "rtpsession.h"

GST_DEBUG_CATEGORY_STATIC (rtp_session_debug);
#define GST_CAT_DEFAULT rtp_session_debug

/* signals and args */
enum
{
  SIGNAL_ON_NEW_SSRC,
  SIGNAL_ON_SSRC_COLLISION,
  SIGNAL_ON_SSRC_VALIDATED,
  SIGNAL_ON_BYE_SSRC,
  LAST_SIGNAL
};

#define RTP_DEFAULT_BANDWIDTH        64000.0
#define RTP_DEFAULT_RTCP_BANDWIDTH   1000

enum
{
  PROP_0
};

/* GObject vmethods */
static void rtp_session_finalize (GObject * object);
static void rtp_session_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void rtp_session_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static guint rtp_session_signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (RTPSession, rtp_session, G_TYPE_OBJECT);

static void
rtp_session_class_init (RTPSessionClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = rtp_session_finalize;
  gobject_class->set_property = rtp_session_set_property;
  gobject_class->get_property = rtp_session_get_property;

  /**
   * RTPSession::on-new-ssrc:
   * @session: the object which received the signal
   * @src: the new RTPSource
   *
   * Notify of a new SSRC that entered @session.
   */
  rtp_session_signals[SIGNAL_ON_NEW_SSRC] =
      g_signal_new ("on-new-ssrc", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (RTPSessionClass, on_new_ssrc),
      NULL, NULL, g_cclosure_marshal_VOID__OBJECT, G_TYPE_NONE, 1,
      G_TYPE_OBJECT);
  /**
   * RTPSession::on-ssrc_collision:
   * @session: the object which received the signal
   * @src: the #RTPSource that caused a collision
   *
   * Notify when we have an SSRC collision
   */
  rtp_session_signals[SIGNAL_ON_SSRC_COLLISION] =
      g_signal_new ("on-ssrc-collision", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (RTPSessionClass, on_ssrc_collision),
      NULL, NULL, g_cclosure_marshal_VOID__OBJECT, G_TYPE_NONE, 1,
      G_TYPE_OBJECT);
  /**
   * RTPSession::on-ssrc_validated:
   * @session: the object which received the signal
   * @src: the new validated RTPSource
   *
   * Notify of a new SSRC that became validated.
   */
  rtp_session_signals[SIGNAL_ON_SSRC_VALIDATED] =
      g_signal_new ("on-ssrc-validated", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (RTPSessionClass, on_ssrc_validated),
      NULL, NULL, g_cclosure_marshal_VOID__OBJECT, G_TYPE_NONE, 1,
      G_TYPE_OBJECT);
  /**
   * RTPSession::on-bye-ssrc:
   * @session: the object which received the signal
   * @src: the RTPSource that went away
   *
   * Notify of an SSRC that became inactive because of a BYE packet.
   */
  rtp_session_signals[SIGNAL_ON_BYE_SSRC] =
      g_signal_new ("on-bye-ssrc", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (RTPSessionClass, on_bye_ssrc),
      NULL, NULL, g_cclosure_marshal_VOID__OBJECT, G_TYPE_NONE, 1,
      G_TYPE_OBJECT);

  GST_DEBUG_CATEGORY_INIT (rtp_session_debug, "rtpsession", 0, "RTP Session");
}

static void
rtp_session_init (RTPSession * sess)
{
  sess->lock = g_mutex_new ();
  sess->ssrcs =
      g_hash_table_new_full (NULL, NULL, NULL, (GDestroyNotify) g_object_unref);
  sess->cnames = g_hash_table_new_full (NULL, NULL, g_free, NULL);

  /* create an SSRC for this session manager */
  sess->source = rtp_session_create_source (sess);

  rtp_stats_init_defaults (&sess->stats);

  /* default UDP header length */
  sess->header_len = 28;

  GST_DEBUG ("%p: session using SSRC: %08x", sess, sess->source->ssrc);
}

static void
rtp_session_finalize (GObject * object)
{
  RTPSession *sess;

  sess = RTP_SESSION_CAST (object);

  g_mutex_free (sess->lock);
  g_hash_table_destroy (sess->ssrcs);
  g_hash_table_destroy (sess->cnames);
  g_object_unref (sess->source);

  G_OBJECT_CLASS (rtp_session_parent_class)->finalize (object);
}

static void
rtp_session_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  RTPSession *sess;

  sess = RTP_SESSION (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
rtp_session_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  RTPSession *sess;

  sess = RTP_SESSION (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
on_new_ssrc (RTPSession * sess, RTPSource * source)
{
  g_signal_emit (sess, rtp_session_signals[SIGNAL_ON_NEW_SSRC], 0, source);
}

static void
on_ssrc_collision (RTPSession * sess, RTPSource * source)
{
  g_signal_emit (sess, rtp_session_signals[SIGNAL_ON_SSRC_COLLISION], 0,
      source);
}

static void
on_ssrc_validated (RTPSession * sess, RTPSource * source)
{
  g_signal_emit (sess, rtp_session_signals[SIGNAL_ON_SSRC_VALIDATED], 0,
      source);
}

static void
on_bye_ssrc (RTPSession * sess, RTPSource * source)
{
  g_signal_emit (sess, rtp_session_signals[SIGNAL_ON_BYE_SSRC], 0, source);
}

/**
 * rtp_session_new:
 *
 * Create a new session object.
 *
 * Returns: a new #RTPSession. g_object_unref() after usage.
 */
RTPSession *
rtp_session_new (void)
{
  RTPSession *sess;

  sess = g_object_new (RTP_TYPE_SESSION, NULL);

  return sess;
}

/**
 * rtp_session_set_callbacks:
 * @sess: an #RTPSession
 * @callbacks: callbacks to configure
 * @user_data: user data passed in the callbacks
 *
 * Configure a set of callbacks to be notified of actions.
 */
void
rtp_session_set_callbacks (RTPSession * sess, RTPSessionCallbacks * callbacks,
    gpointer user_data)
{
  g_return_if_fail (RTP_IS_SESSION (sess));

  sess->callbacks.process_rtp = callbacks->process_rtp;
  sess->callbacks.send_rtp = callbacks->send_rtp;
  sess->callbacks.send_rtcp = callbacks->send_rtcp;
  sess->callbacks.clock_rate = callbacks->clock_rate;
  sess->callbacks.get_time = callbacks->get_time;
  sess->user_data = user_data;
}

/**
 * rtp_session_set_bandwidth:
 * @sess: an #RTPSession
 * @bandwidth: the bandwidth allocated
 *
 * Set the session bandwidth in bytes per second.
 */
void
rtp_session_set_bandwidth (RTPSession * sess, gdouble bandwidth)
{
  g_return_if_fail (RTP_IS_SESSION (sess));

  sess->stats.bandwidth = bandwidth;
}

/**
 * rtp_session_get_bandwidth:
 * @sess: an #RTPSession
 *
 * Get the session bandwidth.
 *
 * Returns: the session bandwidth.
 */
gdouble
rtp_session_get_bandwidth (RTPSession * sess)
{
  g_return_val_if_fail (RTP_IS_SESSION (sess), 0);

  return sess->stats.bandwidth;
}

/**
 * rtp_session_set_rtcp_bandwidth:
 * @sess: an #RTPSession
 * @bandwidth: the RTCP bandwidth
 *
 * Set the bandwidth that should be used for RTCP
 * messages. 
 */
void
rtp_session_set_rtcp_bandwidth (RTPSession * sess, gdouble bandwidth)
{
  g_return_if_fail (RTP_IS_SESSION (sess));

  sess->stats.rtcp_bandwidth = bandwidth;
}

/**
 * rtp_session_get_rtcp_bandwidth:
 * @sess: an #RTPSession
 *
 * Get the session bandwidth used for RTCP.
 *
 * Returns: The bandwidth used for RTCP messages.
 */
gdouble
rtp_session_get_rtcp_bandwidth (RTPSession * sess)
{
  g_return_val_if_fail (RTP_IS_SESSION (sess), 0.0);

  return sess->stats.rtcp_bandwidth;
}

static GstFlowReturn
source_push_rtp (RTPSource * source, GstBuffer * buffer, RTPSession * session)
{
  GstFlowReturn result = GST_FLOW_OK;

  if (source == session->source) {
    GST_DEBUG ("source %08x pushed sender RTP packet", source->ssrc);
    if (session->callbacks.send_rtp)
      result =
          session->callbacks.send_rtp (session, source, buffer,
          session->user_data);
    else
      gst_buffer_unref (buffer);
  } else {
    GST_DEBUG ("source %08x pushed receiver RTP packet", source->ssrc);
    if (session->callbacks.process_rtp)
      result =
          session->callbacks.process_rtp (session, source, buffer,
          session->user_data);
    else
      gst_buffer_unref (buffer);
  }
  return result;
}

static gint
source_clock_rate (RTPSource * source, guint8 pt, RTPSession * session)
{
  gint result;

  if (session->callbacks.clock_rate)
    result = session->callbacks.clock_rate (session, pt, session->user_data);
  else
    result = -1;

  GST_DEBUG ("got clock-rate %d for pt %d", result, pt);

  return result;
}

static RTPSourceCallbacks callbacks = {
  (RTPSourcePushRTP) source_push_rtp,
  (RTPSourceClockRate) source_clock_rate,
};

static gboolean
check_collision (RTPSession * sess, RTPSource * source,
    RTPArrivalStats * arrival)
{
  /* FIXME, do collision check */
  return FALSE;
}

static RTPSource *
obtain_source (RTPSession * sess, guint32 ssrc, gboolean * created,
    RTPArrivalStats * arrival, gboolean rtp)
{
  RTPSource *source;

  source = g_hash_table_lookup (sess->ssrcs, GINT_TO_POINTER (ssrc));
  if (source == NULL) {
    /* make new Source in probation and insert */
    source = rtp_source_new (ssrc);

    if (rtp)
      source->probation = RTP_DEFAULT_PROBATION;
    else
      source->probation = 0;

    /* store from address, if any */
    if (arrival->have_address) {
      if (rtp)
        rtp_source_set_rtp_from (source, &arrival->address);
      else
        rtp_source_set_rtcp_from (source, &arrival->address);
    }

    /* configure a callback on the source */
    rtp_source_set_callbacks (source, &callbacks, sess);

    g_hash_table_insert (sess->ssrcs, GINT_TO_POINTER (ssrc), source);

    /* we have one more source now */
    sess->total_sources++;
    *created = TRUE;
  } else {
    *created = FALSE;
    /* check for collision, this updates the address when not previously set */
    if (check_collision (sess, source, arrival))
      on_ssrc_collision (sess, source);
  }
  return source;
}

/**
 * rtp_session_add_source:
 * @sess: a #RTPSession
 * @src: #RTPSource to add
 *
 * Add @src to @session.
 *
 * Returns: %TRUE on success, %FALSE if a source with the same SSRC already
 * existed in the session.
 */
gboolean
rtp_session_add_source (RTPSession * sess, RTPSource * src)
{
  gboolean result = FALSE;
  RTPSource *find;

  g_return_val_if_fail (RTP_IS_SESSION (sess), FALSE);
  g_return_val_if_fail (src != NULL, FALSE);

  RTP_SESSION_LOCK (sess);
  find = g_hash_table_lookup (sess->ssrcs, GINT_TO_POINTER (src->ssrc));
  if (find == NULL) {
    g_hash_table_insert (sess->ssrcs, GINT_TO_POINTER (src->ssrc), src);
    /* we have one more source now */
    sess->total_sources++;
    result = TRUE;
  }
  RTP_SESSION_UNLOCK (sess);

  return result;
}

/**
 * rtp_session_get_num_sources:
 * @sess: an #RTPSession
 *
 * Get the number of sources in @sess.
 *
 * Returns: The number of sources in @sess.
 */
gint
rtp_session_get_num_sources (RTPSession * sess)
{
  gint result;

  g_return_val_if_fail (RTP_IS_SESSION (sess), FALSE);

  RTP_SESSION_LOCK (sess);
  result = sess->total_sources;
  RTP_SESSION_UNLOCK (sess);

  return result;
}

/**
 * rtp_session_get_num_active_sources:
 * @sess: an #RTPSession
 *
 * Get the number of active sources in @sess. A source is considered active when
 * it has been validated and has not yet received a BYE RTCP message.
 *
 * Returns: The number of active sources in @sess.
 */
gint
rtp_session_get_num_active_sources (RTPSession * sess)
{
  gint result;

  g_return_val_if_fail (RTP_IS_SESSION (sess), FALSE);

  RTP_SESSION_LOCK (sess);
  result = sess->stats.active_sources;
  RTP_SESSION_UNLOCK (sess);

  return result;
}

/**
 * rtp_session_get_source_by_ssrc:
 * @sess: an #RTPSession
 * @ssrc: an SSRC
 *
 * Find the source with @ssrc in @sess.
 *
 * Returns: a #RTPSource with SSRC @ssrc or NULL if the source was not found.
 * g_object_unref() after usage.
 */
RTPSource *
rtp_session_get_source_by_ssrc (RTPSession * sess, guint32 ssrc)
{
  RTPSource *result;

  g_return_val_if_fail (RTP_IS_SESSION (sess), NULL);

  RTP_SESSION_LOCK (sess);
  result = g_hash_table_lookup (sess->ssrcs, GINT_TO_POINTER (ssrc));
  if (result)
    g_object_ref (result);
  RTP_SESSION_UNLOCK (sess);

  return result;
}

/**
 * rtp_session_get_source_by_cname:
 * @sess: a #RTPSession
 * @cname: an CNAME
 *
 * Find the source with @cname in @sess.
 *
 * Returns: a #RTPSource with CNAME @cname or NULL if the source was not found.
 * g_object_unref() after usage.
 */
RTPSource *
rtp_session_get_source_by_cname (RTPSession * sess, const gchar * cname)
{
  RTPSource *result;

  g_return_val_if_fail (RTP_IS_SESSION (sess), NULL);
  g_return_val_if_fail (cname != NULL, NULL);

  RTP_SESSION_LOCK (sess);
  result = g_hash_table_lookup (sess->cnames, cname);
  if (result)
    g_object_ref (result);
  RTP_SESSION_UNLOCK (sess);

  return result;
}

/**
 * rtp_session_create_source:
 * @sess: an #RTPSession
 *
 * Create an #RTPSource for use in @sess. This function will create a source
 * with an ssrc that is currently not used by any participants in the session.
 *
 * Returns: an #RTPSource.
 */
RTPSource *
rtp_session_create_source (RTPSession * sess)
{
  guint32 ssrc;
  RTPSource *source;

  RTP_SESSION_LOCK (sess);
  while (TRUE) {
    ssrc = g_random_int ();

    /* see if it exists in the session, we're done if it doesn't */
    if (g_hash_table_lookup (sess->ssrcs, GINT_TO_POINTER (ssrc)) == NULL)
      break;
  }
  source = rtp_source_new (ssrc);
  g_hash_table_insert (sess->ssrcs, GINT_TO_POINTER (ssrc), source);
  /* we have one more source now */
  sess->total_sources++;
  RTP_SESSION_UNLOCK (sess);

  return source;
}

/* update the RTPArrivalStats structure with the current time and other bits
 * about the current buffer we are handling.
 * This function is typically called when a validated packet is received.
 */
static void
update_arrival_stats (RTPSession * sess, RTPArrivalStats * arrival,
    gboolean rtp, GstBuffer * buffer)
{
  /* get time or arrival */
  if (sess->callbacks.get_time)
    arrival->time = sess->callbacks.get_time (sess, sess->user_data);
  else
    arrival->time = GST_CLOCK_TIME_NONE;

  /* update sizes */
  arrival->bytes = GST_BUFFER_SIZE (buffer) + 28;
  arrival->payload_len = (rtp ? gst_rtp_buffer_get_payload_len (buffer) : 0);

  /* for netbuffer we can store the IP address to check for collisions */
  arrival->have_address = GST_IS_NETBUFFER (buffer);
  if (arrival->have_address) {
    GstNetBuffer *netbuf = (GstNetBuffer *) buffer;

    memcpy (&arrival->address, &netbuf->from, sizeof (GstNetAddress));
  }
}

/**
 * rtp_session_process_rtp:
 * @sess: and #RTPSession
 * @buffer: an RTP buffer
 *
 * Process an RTP buffer in the session manager. This function takes ownership
 * of @buffer.
 *
 * Returns: a #GstFlowReturn.
 */
GstFlowReturn
rtp_session_process_rtp (RTPSession * sess, GstBuffer * buffer)
{
  GstFlowReturn result;
  guint32 ssrc;
  RTPSource *source;
  gboolean created;
  gboolean prevsender, prevactive;
  RTPArrivalStats arrival;

  g_return_val_if_fail (RTP_IS_SESSION (sess), GST_FLOW_ERROR);
  g_return_val_if_fail (GST_IS_BUFFER (buffer), GST_FLOW_ERROR);

  if (!gst_rtp_buffer_validate (buffer))
    goto invalid_packet;

  /* update arrival stats */
  update_arrival_stats (sess, &arrival, TRUE, buffer);

  /* get SSRC and look up in session database */
  ssrc = gst_rtp_buffer_get_ssrc (buffer);

  RTP_SESSION_LOCK (sess);
  source = obtain_source (sess, ssrc, &created, &arrival, TRUE);

  prevsender = RTP_SOURCE_IS_SENDER (source);
  prevactive = RTP_SOURCE_IS_ACTIVE (source);

  /* let source process the packet */
  result = rtp_source_process_rtp (source, buffer, &arrival);

  /* source became active */
  if (prevactive != RTP_SOURCE_IS_ACTIVE (source)) {
    sess->stats.active_sources++;
    GST_DEBUG ("source: %08x became active, %d active sources", ssrc,
        sess->stats.active_sources);
    on_ssrc_validated (sess, source);
  }
  if (prevsender != RTP_SOURCE_IS_SENDER (source)) {
    sess->stats.sender_sources++;
    GST_DEBUG ("source: %08x became sender, %d sender sources", ssrc,
        sess->stats.sender_sources);
  }

  if (created)
    on_new_ssrc (sess, source);

  /* for validated sources, we add the CSRCs as well */
  if (source->validated) {
    guint8 i, count;

    count = gst_rtp_buffer_get_csrc_count (buffer);

    for (i = 0; i < count; i++) {
      guint32 csrc;
      RTPSource *csrc_src;

      csrc = gst_rtp_buffer_get_csrc (buffer, i);

      /* get source */
      csrc_src = obtain_source (sess, csrc, &created, &arrival, TRUE);
      if (created) {
        GST_DEBUG ("created new CSRC: %08x", csrc);
        rtp_source_set_as_csrc (csrc_src);
        if (RTP_SOURCE_IS_ACTIVE (csrc_src))
          sess->stats.active_sources++;
        on_new_ssrc (sess, source);
      }
    }
  }
  RTP_SESSION_UNLOCK (sess);

  return result;

  /* ERRORS */
invalid_packet:
  {
    GST_DEBUG ("invalid RTP packet received");
    return GST_FLOW_OK;
  }
}

/* A Sender report contains statistics about how the sender is doing. This
 * includes timing informataion about the relation between RTP and NTP
 * timestamps is it using and the number of packets/bytes it sent to us.
 *
 * In this report is also included a set of report blocks related to how this
 * sender is receiving data (in case we (or somebody else) is also sending stuff
 * to it). This info includes the packet loss, jitter and seqnum. It also
 * contains information to calculate the round trip time (LSR/DLSR).
 */
static void
rtp_session_process_sr (RTPSession * sess, GstRTCPPacket * packet,
    RTPArrivalStats * arrival)
{
  guint32 senderssrc, rtptime, packet_count, octet_count;
  guint64 ntptime;
  guint count, i;
  RTPSource *source;
  gboolean created;

  gst_rtcp_packet_sr_get_sender_info (packet, &senderssrc, &ntptime, &rtptime,
      &packet_count, &octet_count);

  RTP_SESSION_LOCK (sess);
  source = obtain_source (sess, senderssrc, &created, arrival, FALSE);

  /* first update the source */
  rtp_source_process_sr (source, ntptime, rtptime, packet_count, octet_count);

  if (created)
    on_new_ssrc (sess, source);

  count = gst_rtcp_packet_get_rb_count (packet);
  for (i = 0; i < count; i++) {
    guint32 ssrc, exthighestseq, jitter, lsr, dlsr;
    guint8 fractionlost;
    gint32 packetslost;

    gst_rtcp_packet_get_rb (packet, i, &ssrc, &fractionlost,
        &packetslost, &exthighestseq, &jitter, &lsr, &dlsr);

    if (ssrc == sess->source->ssrc) {
      /* only deal with report blocks for our session, we update the stats of
       * the sender of the TCP message. We could also compare our stats against
       * the other sender to see if we are better or worse. */
      rtp_source_process_rb (source, fractionlost, packetslost,
          exthighestseq, jitter, lsr, dlsr);
    }
  }
  RTP_SESSION_UNLOCK (sess);
}

/* A receiver report contains statistics about how a receiver is doing. It
 * includes stuff like packet loss, jitter and the seqnum it received last. It
 * also contains info to calculate the round trip time.
 *
 * We are only interested in how the sender of this report is doing wrt to us.
 */
static void
rtp_session_process_rr (RTPSession * sess, GstRTCPPacket * packet,
    RTPArrivalStats * arrival)
{
  guint32 senderssrc;
  guint count, i;
  RTPSource *source;
  gboolean created;

  senderssrc = gst_rtcp_packet_rr_get_ssrc (packet);

  GST_DEBUG ("got RR packet: SSRC %08x", senderssrc);

  RTP_SESSION_LOCK (sess);
  source = obtain_source (sess, senderssrc, &created, arrival, FALSE);

  if (created)
    on_new_ssrc (sess, source);

  count = gst_rtcp_packet_get_rb_count (packet);
  for (i = 0; i < count; i++) {
    guint32 ssrc, exthighestseq, jitter, lsr, dlsr;
    guint8 fractionlost;
    gint32 packetslost;

    gst_rtcp_packet_get_rb (packet, i, &ssrc, &fractionlost,
        &packetslost, &exthighestseq, &jitter, &lsr, &dlsr);

    if (ssrc == sess->source->ssrc) {
      rtp_source_process_rb (source, fractionlost, packetslost,
          exthighestseq, jitter, lsr, dlsr);
    }
  }
  RTP_SESSION_UNLOCK (sess);
}

/* FIXME, we're just printing this for now... */
static void
rtp_session_process_sdes (RTPSession * sess, GstRTCPPacket * packet,
    RTPArrivalStats * arrival)
{
  guint chunks, i, j;
  gboolean more_chunks, more_items;

  chunks = gst_rtcp_packet_sdes_get_chunk_count (packet);
  GST_DEBUG ("got SDES packet with %d chunks", chunks);

  more_chunks = gst_rtcp_packet_sdes_first_chunk (packet);
  i = 0;
  while (more_chunks) {
    guint32 ssrc;

    ssrc = gst_rtcp_packet_sdes_get_ssrc (packet);

    GST_DEBUG ("chunk %d, SSRC %08x", i, ssrc);

    more_items = gst_rtcp_packet_sdes_first_item (packet);
    j = 0;
    while (more_items) {
      GstRTCPSDESType type;
      guint8 len;
      gchar *data;

      gst_rtcp_packet_sdes_get_item (packet, &type, &len, &data);

      GST_DEBUG ("item %d, type %d, len %d, data %s", j, type, len, data);

      more_items = gst_rtcp_packet_sdes_next_item (packet);
      j++;
    }
    more_chunks = gst_rtcp_packet_sdes_next_chunk (packet);
    i++;
  }
}

/* BYE is sent when a client leaves the session
 */
static void
rtp_session_process_bye (RTPSession * sess, GstRTCPPacket * packet,
    RTPArrivalStats * arrival)
{
  guint count, i;
  gchar *reason;

  reason = gst_rtcp_packet_bye_get_reason (packet);
  GST_DEBUG ("got BYE packet (reason: %s)", GST_STR_NULL (reason));

  count = gst_rtcp_packet_bye_get_ssrc_count (packet);
  for (i = 0; i < count; i++) {
    guint32 ssrc;
    RTPSource *source;
    gboolean created, prevactive, prevsender;

    ssrc = gst_rtcp_packet_bye_get_nth_ssrc (packet, i);
    GST_DEBUG ("SSRC: %08x", ssrc);

    /* find src and mark bye, no probation when dealing with RTCP */
    RTP_SESSION_LOCK (sess);
    source = obtain_source (sess, ssrc, &created, arrival, FALSE);

    prevactive = RTP_SOURCE_IS_ACTIVE (source);
    prevsender = RTP_SOURCE_IS_SENDER (source);

    /* let the source handle the rest */
    rtp_source_process_bye (source, reason);

    if (prevactive && !RTP_SOURCE_IS_ACTIVE (source)) {
      sess->stats.active_sources--;
      GST_DEBUG ("source: %08x became inactive, %d active sources", ssrc,
          sess->stats.active_sources);
    }
    if (prevsender && !RTP_SOURCE_IS_SENDER (source)) {
      sess->stats.sender_sources--;
      GST_DEBUG ("source: %08x became non sender, %d sender sources", ssrc,
          sess->stats.sender_sources);
    }

    if (created)
      on_new_ssrc (sess, source);

    on_bye_ssrc (sess, source);
    RTP_SESSION_UNLOCK (sess);
  }
  g_free (reason);
}

static void
rtp_session_process_app (RTPSession * sess, GstRTCPPacket * packet,
    RTPArrivalStats * arrival)
{
  GST_DEBUG ("received APP");
}

/**
 * rtp_session_process_rtcp:
 * @sess: and #RTPSession
 * @buffer: an RTCP buffer
 *
 * Process an RTCP buffer in the session manager.
 *
 * Returns: a #GstFlowReturn.
 */
GstFlowReturn
rtp_session_process_rtcp (RTPSession * sess, GstBuffer * buffer)
{
  GstRTCPPacket packet;
  gboolean more;
  RTPArrivalStats arrival;
  guint size;

  g_return_val_if_fail (RTP_IS_SESSION (sess), GST_FLOW_ERROR);
  g_return_val_if_fail (GST_IS_BUFFER (buffer), GST_FLOW_ERROR);

  if (!gst_rtcp_buffer_validate (buffer))
    goto invalid_packet;

  /* update arrival stats */
  update_arrival_stats (sess, &arrival, FALSE, buffer);

  GST_DEBUG ("received RTCP packet");

  /* get packet size including header overhead */
  size = GST_BUFFER_SIZE (buffer) + sess->header_len;

  /* update average RTCP packet size */
  if (sess->stats.avg_rtcp_packet_size == 0)
    sess->stats.avg_rtcp_packet_size = size;
  else
    sess->stats.avg_rtcp_packet_size =
        (size + (15 * sess->stats.avg_rtcp_packet_size)) >> 4;

  /* start processing the compound packet */
  more = gst_rtcp_buffer_get_first_packet (buffer, &packet);
  while (more) {
    switch (gst_rtcp_packet_get_type (&packet)) {
      case GST_RTCP_TYPE_SR:
        rtp_session_process_sr (sess, &packet, &arrival);
        break;
      case GST_RTCP_TYPE_RR:
        rtp_session_process_rr (sess, &packet, &arrival);
        break;
      case GST_RTCP_TYPE_SDES:
        rtp_session_process_sdes (sess, &packet, &arrival);
        break;
      case GST_RTCP_TYPE_BYE:
        rtp_session_process_bye (sess, &packet, &arrival);
        break;
      case GST_RTCP_TYPE_APP:
        rtp_session_process_app (sess, &packet, &arrival);
        break;
      default:
        GST_WARNING ("got unknown RTCP packet");
        break;
    }
    more = gst_rtcp_packet_move_to_next (&packet);
  }

  gst_buffer_unref (buffer);

  return GST_FLOW_OK;

  /* ERRORS */
invalid_packet:
  {
    GST_DEBUG ("invalid RTCP packet received");
    return GST_FLOW_OK;
  }
}

/**
 * rtp_session_send_rtp:
 * @sess: and #RTPSession
 * @buffer: an RTP buffer
 *
 * Send the RTP buffer in the session manager.
 *
 * Returns: a #GstFlowReturn.
 */
GstFlowReturn
rtp_session_send_rtp (RTPSession * sess, GstBuffer * buffer)
{
  GstFlowReturn result;
  RTPSource *source;
  gboolean prevsender;

  g_return_val_if_fail (RTP_IS_SESSION (sess), GST_FLOW_ERROR);
  g_return_val_if_fail (GST_IS_BUFFER (buffer), GST_FLOW_ERROR);

  source = sess->source;

  prevsender = RTP_SOURCE_IS_SENDER (source);

  /* we use our own source to send */
  result = rtp_source_send_rtp (sess->source, buffer);

  if (RTP_SOURCE_IS_SENDER (source) && !prevsender)
    sess->stats.sender_sources++;

  return result;
}

/**
 * rtp_session_get_rtcp_interval:
 * @sess: an #RTPSession
 *
 * Get the interval for sending out the next RTCP packet
 *
 * Returns: an interval in seconds.
 */
gdouble
rtp_session_get_rtcp_interval (RTPSession * sess)
{
  gdouble result;

  g_return_val_if_fail (RTP_IS_SESSION (sess), GST_FLOW_ERROR);

  RTP_SESSION_LOCK (sess);
  result = rtp_stats_calculate_rtcp_interval (&sess->stats, FALSE);
  result = rtp_stats_add_rtcp_jitter (&sess->stats, result);
  RTP_SESSION_UNLOCK (sess);

  return result;
}

/**
 * rtp_session_produce_rtcp:
 * @sess: an #RTPSession
 *
 * Instruct the session manager to generate RTCP packets with current stats.
 * This function will call the #RTPSessionSendRTCP callback, possibly multiple
 * times, for each packet that should be processed.
 *
 * Returns: a #GstFlowReturn.
 */
GstFlowReturn
rtp_session_produce_rtcp (RTPSession * sess)
{
  /* FIXME: implement me */
  return GST_FLOW_NOT_SUPPORTED;
}
