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

static RTPSource *obtain_source (RTPSession * sess, guint32 ssrc,
    gboolean * created, RTPArrivalStats * arrival, gboolean rtp);

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
  gint i;

  sess->lock = g_mutex_new ();
  sess->key = g_random_int ();
  sess->mask_idx = 0;
  sess->mask = 0;

  for (i = 0; i < 32; i++) {
    sess->ssrcs[i] =
        g_hash_table_new_full (NULL, NULL, NULL,
        (GDestroyNotify) g_object_unref);
  }
  sess->cnames = g_hash_table_new_full (NULL, NULL, g_free, NULL);

  rtp_stats_init_defaults (&sess->stats);

  /* create an active SSRC for this session manager */
  sess->source = rtp_session_create_source (sess);
  sess->stats.active_sources++;

  /* default UDP header length */
  sess->header_len = 28;
  sess->mtu = 1400;

  /* some default SDES entries */
  //sess->cname = g_strdup_printf ("%s@%s", g_get_user_name (), g_get_host_name ());
  sess->cname = g_strdup_printf ("foo@%s", g_get_host_name ());
  sess->name = g_strdup (g_get_real_name ());
  sess->tool = g_strdup ("GStreamer");

  GST_DEBUG ("%p: session using SSRC: %08x", sess, sess->source->ssrc);
}

static void
rtp_session_finalize (GObject * object)
{
  RTPSession *sess;
  gint i;

  sess = RTP_SESSION_CAST (object);

  g_mutex_free (sess->lock);
  for (i = 0; i < 32; i++)
    g_hash_table_destroy (sess->ssrcs[i]);

  g_hash_table_destroy (sess->cnames);
  g_object_unref (sess->source);

  g_free (sess->cname);
  g_free (sess->tool);

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

/**
 * rtp_session_set_cname:
 * @sess: an #RTPSession
 * @cname: a CNAME for the session
 *
 * Set the CNAME for the session. 
 */
void
rtp_session_set_cname (RTPSession * sess, const gchar * cname)
{
  g_return_if_fail (RTP_IS_SESSION (sess));

  g_free (sess->cname);
  sess->cname = g_strdup (cname);
}

/**
 * rtp_session_get_cname:
 * @sess: an #RTPSession
 *
 * Get the currently configured CNAME for the session.
 *
 * Returns: The CNAME. g_free after usage.
 */
gchar *
rtp_session_get_cname (RTPSession * sess)
{
  g_return_val_if_fail (RTP_IS_SESSION (sess), NULL);

  return g_strdup (sess->cname);
}

/**
 * rtp_session_set_name:
 * @sess: an #RTPSession
 * @name: a NAME for the session
 *
 * Set the NAME for the session. 
 */
void
rtp_session_set_name (RTPSession * sess, const gchar * name)
{
  g_return_if_fail (RTP_IS_SESSION (sess));

  g_free (sess->name);
  sess->name = g_strdup (name);
}

/**
 * rtp_session_get_name:
 * @sess: an #RTPSession
 *
 * Get the currently configured NAME for the session.
 *
 * Returns: The NAME. g_free after usage.
 */
gchar *
rtp_session_get_name (RTPSession * sess)
{
  g_return_val_if_fail (RTP_IS_SESSION (sess), NULL);

  return g_strdup (sess->name);
}

/**
 * rtp_session_set_email:
 * @sess: an #RTPSession
 * @email: an EMAIL for the session
 *
 * Set the EMAIL the session. 
 */
void
rtp_session_set_email (RTPSession * sess, const gchar * email)
{
  g_return_if_fail (RTP_IS_SESSION (sess));

  g_free (sess->email);
  sess->email = g_strdup (email);
}

/**
 * rtp_session_get_email:
 * @sess: an #RTPSession
 *
 * Get the currently configured EMAIL of the session.
 *
 * Returns: The EMAIL. g_free after usage.
 */
gchar *
rtp_session_get_email (RTPSession * sess)
{
  g_return_val_if_fail (RTP_IS_SESSION (sess), NULL);

  return g_strdup (sess->email);
}

/**
 * rtp_session_set_phone:
 * @sess: an #RTPSession
 * @phone: a PHONE for the session
 *
 * Set the PHONE the session. 
 */
void
rtp_session_set_phone (RTPSession * sess, const gchar * phone)
{
  g_return_if_fail (RTP_IS_SESSION (sess));

  g_free (sess->phone);
  sess->phone = g_strdup (phone);
}

/**
 * rtp_session_get_location:
 * @sess: an #RTPSession
 *
 * Get the currently configured PHONE of the session.
 *
 * Returns: The PHONE. g_free after usage.
 */
gchar *
rtp_session_get_phone (RTPSession * sess)
{
  g_return_val_if_fail (RTP_IS_SESSION (sess), NULL);

  return g_strdup (sess->phone);
}

/**
 * rtp_session_set_location:
 * @sess: an #RTPSession
 * @location: a LOCATION for the session
 *
 * Set the LOCATION the session. 
 */
void
rtp_session_set_location (RTPSession * sess, const gchar * location)
{
  g_return_if_fail (RTP_IS_SESSION (sess));

  g_free (sess->location);
  sess->location = g_strdup (location);
}

/**
 * rtp_session_get_location:
 * @sess: an #RTPSession
 *
 * Get the currently configured LOCATION of the session.
 *
 * Returns: The LOCATION. g_free after usage.
 */
gchar *
rtp_session_get_location (RTPSession * sess)
{
  g_return_val_if_fail (RTP_IS_SESSION (sess), NULL);

  return g_strdup (sess->location);
}

/**
 * rtp_session_set_tool:
 * @sess: an #RTPSession
 * @tool: a TOOL for the session
 *
 * Set the TOOL the session. 
 */
void
rtp_session_set_tool (RTPSession * sess, const gchar * tool)
{
  g_return_if_fail (RTP_IS_SESSION (sess));

  g_free (sess->tool);
  sess->tool = g_strdup (tool);
}

/**
 * rtp_session_get_tool:
 * @sess: an #RTPSession
 *
 * Get the currently configured TOOL of the session.
 *
 * Returns: The TOOL. g_free after usage.
 */
gchar *
rtp_session_get_tool (RTPSession * sess)
{
  g_return_val_if_fail (RTP_IS_SESSION (sess), NULL);

  return g_strdup (sess->tool);
}

/**
 * rtp_session_set_note:
 * @sess: an #RTPSession
 * @note: a NOTE for the session
 *
 * Set the NOTE the session. 
 */
void
rtp_session_set_note (RTPSession * sess, const gchar * note)
{
  g_return_if_fail (RTP_IS_SESSION (sess));

  g_free (sess->note);
  sess->note = g_strdup (note);
}

/**
 * rtp_session_get_note:
 * @sess: an #RTPSession
 *
 * Get the currently configured NOTE of the session.
 *
 * Returns: The NOTE. g_free after usage.
 */
gchar *
rtp_session_get_note (RTPSession * sess)
{
  g_return_val_if_fail (RTP_IS_SESSION (sess), NULL);

  return g_strdup (sess->note);
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

  source =
      g_hash_table_lookup (sess->ssrcs[sess->mask_idx], GINT_TO_POINTER (ssrc));
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

    g_hash_table_insert (sess->ssrcs[sess->mask_idx], GINT_TO_POINTER (ssrc),
        source);

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
  find =
      g_hash_table_lookup (sess->ssrcs[sess->mask_idx],
      GINT_TO_POINTER (src->ssrc));
  if (find == NULL) {
    g_hash_table_insert (sess->ssrcs[sess->mask_idx],
        GINT_TO_POINTER (src->ssrc), src);
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
  result =
      g_hash_table_lookup (sess->ssrcs[sess->mask_idx], GINT_TO_POINTER (ssrc));
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
    if (g_hash_table_lookup (sess->ssrcs[sess->mask_idx],
            GINT_TO_POINTER (ssrc)) == NULL)
      break;
  }
  source = rtp_source_new (ssrc);
  g_hash_table_insert (sess->ssrcs[sess->mask_idx], GINT_TO_POINTER (ssrc),
      source);
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

  gst_buffer_ref (buffer);

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

  if (source->validated) {
    guint8 i, count;
    gboolean created;

    /* for validated sources, we add the CSRCs as well */
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
  gst_buffer_unref (buffer);

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
  gboolean created, prevsender;

  gst_rtcp_packet_sr_get_sender_info (packet, &senderssrc, &ntptime, &rtptime,
      &packet_count, &octet_count);

  GST_DEBUG ("got SR packet: SSRC %08x", senderssrc);

  RTP_SESSION_LOCK (sess);
  source = obtain_source (sess, senderssrc, &created, arrival, FALSE);

  prevsender = RTP_SOURCE_IS_SENDER (source);

  /* first update the source */
  rtp_source_process_sr (source, ntptime, rtptime, packet_count, octet_count);

  if (prevsender != RTP_SOURCE_IS_SENDER (source)) {
    sess->stats.sender_sources++;
    GST_DEBUG ("source: %08x became sender, %d sender sources", senderssrc,
        sess->stats.sender_sources);
  }

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
  guint items, i, j;
  gboolean more_items, more_entries;

  items = gst_rtcp_packet_sdes_get_item_count (packet);
  GST_DEBUG ("got SDES packet with %d items", items);

  more_items = gst_rtcp_packet_sdes_first_item (packet);
  i = 0;
  while (more_items) {
    guint32 ssrc;

    ssrc = gst_rtcp_packet_sdes_get_ssrc (packet);

    GST_DEBUG ("item %d, SSRC %08x", i, ssrc);

    more_entries = gst_rtcp_packet_sdes_first_entry (packet);
    j = 0;
    while (more_entries) {
      GstRTCPSDESType type;
      guint8 len;
      guint8 *data;

      gst_rtcp_packet_sdes_get_entry (packet, &type, &len, &data);

      GST_DEBUG ("entry %d, type %d, len %d, data %s", j, type, len, data);

      more_entries = gst_rtcp_packet_sdes_next_entry (packet);
      j++;
    }
    more_items = gst_rtcp_packet_sdes_next_item (packet);
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
  RTP_SESSION_LOCK (sess);
  size = GST_BUFFER_SIZE (buffer) + sess->header_len;

  /* update average RTCP packet size */
  if (sess->stats.avg_rtcp_packet_size == 0)
    sess->stats.avg_rtcp_packet_size = size;
  else
    sess->stats.avg_rtcp_packet_size =
        (size + (15 * sess->stats.avg_rtcp_packet_size)) >> 4;
  RTP_SESSION_UNLOCK (sess);

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

  RTP_SESSION_LOCK (sess);
  source = sess->source;

  prevsender = RTP_SOURCE_IS_SENDER (source);

  /* we use our own source to send */
  result = rtp_source_send_rtp (sess->source, buffer);

  if (RTP_SOURCE_IS_SENDER (source) && !prevsender)
    sess->stats.sender_sources++;
  RTP_SESSION_UNLOCK (sess);

  return result;
}

/**
 * rtp_session_get_reporting_interval:
 * @sess: an #RTPSession
 *
 * Get the interval for sending out the next RTCP packet and doing general
 * maintenance tasks.
 *
 * Returns: an interval in seconds.
 */
gdouble
rtp_session_get_reporting_interval (RTPSession * sess)
{
  gdouble result;

  g_return_val_if_fail (RTP_IS_SESSION (sess), GST_FLOW_ERROR);

  RTP_SESSION_LOCK (sess);
  result = rtp_stats_calculate_rtcp_interval (&sess->stats, FALSE);
  result = rtp_stats_add_rtcp_jitter (&sess->stats, result);
  RTP_SESSION_UNLOCK (sess);

  return result;
}

typedef struct
{
  RTPSession *sess;
  GstBuffer *rtcp;
  GstRTCPPacket packet;
} ReportData;

static void
session_report_blocks (const gchar * key, RTPSource * source, ReportData * data)
{
  RTPSession *sess = data->sess;
  RTPSource *own = sess->source;
  GstRTCPPacket *packet = &data->packet;

  /* create a new buffer if needed */
  if (data->rtcp == NULL) {
    data->rtcp = gst_rtcp_buffer_new (sess->mtu);

    if (RTP_SOURCE_IS_SENDER (own)) {
      /* we are a sender, create SR */
      GST_DEBUG ("create SR for SSRC %08x", own->ssrc);
      gst_rtcp_buffer_add_packet (data->rtcp, GST_RTCP_TYPE_SR, packet);

      /* fill in sender report info */
      gst_rtcp_packet_sr_set_sender_info (packet, own->ssrc,
          0, 0, own->stats.packets_sent, own->stats.octets_sent);
    } else {
      /* we are only receiver, create RR */
      GST_DEBUG ("create RR for SSRC %08x", own->ssrc);
      gst_rtcp_buffer_add_packet (data->rtcp, GST_RTCP_TYPE_RR, packet);
      gst_rtcp_packet_rr_set_ssrc (packet, own->ssrc);
    }
  }
  if (gst_rtcp_packet_get_rb_count (packet) < GST_RTCP_MAX_RB_COUNT) {
    /* only report about other sources */
    if (source != sess->source) {
      RTPSourceStats *stats;
      guint32 extended_max, expected;
      guint32 expected_interval, received_interval;
      guint32 lost, lost_interval, fraction;

      stats = &source->stats;

      extended_max = (stats->cycles << 16) + stats->max_seq;
      expected = extended_max - stats->base_seq + 1;

      if (expected > stats->packets_received) {
        lost = expected - stats->packets_received;
        if (lost > 0x7fffff)
          lost = 0x7fffff;
      } else {
        lost = stats->packets_received - expected;
        if (lost > 0x800000)
          lost = 0x800000;
        else
          lost = -lost;
      }

      expected_interval = expected - stats->prev_expected;
      stats->prev_expected = expected;
      received_interval = stats->packets_received - stats->prev_received;
      stats->prev_received = stats->packets_received;

      lost_interval = expected_interval - received_interval;

      if (expected_interval == 0 || lost_interval <= 0)
        fraction = 0;
      else
        fraction = (lost_interval << 8) / expected_interval;

      GST_DEBUG ("add RR for SSRC %08x", source->ssrc);
      /* we scaled the jitter up for additional precision */
      GST_DEBUG ("fraction %d, lost %d, extseq %u, jitter %d", fraction, lost,
          extended_max, stats->jitter >> 4);

      /* packet is not yet filled, add report block for this source. */
      gst_rtcp_packet_add_rb (packet, source->ssrc, fraction, lost,
          extended_max, stats->jitter >> 4, 0, 0);
    }
  }
}

static void
session_sdes (RTPSession * sess, GstBuffer * buffer)
{
  GstRTCPPacket packet;

  /* add SDES packet */
  gst_rtcp_buffer_add_packet (buffer, GST_RTCP_TYPE_SDES, &packet);

  gst_rtcp_packet_sdes_add_item (&packet, sess->source->ssrc);
  gst_rtcp_packet_sdes_add_entry (&packet, GST_RTCP_SDES_CNAME,
      strlen (sess->cname), (guint8 *) sess->cname);

  /* other SDES items must only be added at regular intervals and only when the
   * user requests to since it might be a privacy problem */
#if 0
  gst_rtcp_packet_sdes_add_entry (&packet, GST_RTCP_SDES_NAME,
      strlen (sess->name), (guint8 *) sess->name);
  gst_rtcp_packet_sdes_add_entry (&packet, GST_RTCP_SDES_TOOL,
      strlen (sess->tool), (guint8 *) sess->tool);
#endif
}

/**
 * rtp_session_perform_reporting:
 * @sess: an #RTPSession
 *
 * Instruct the session manager to generate RTCP packets with current stats.
 * This function will call the #RTPSessionSendRTCP callback, possibly multiple
 * times, for each packet that should be processed.
 *
 * Returns: a #GstFlowReturn.
 */
GstFlowReturn
rtp_session_perform_reporting (RTPSession * sess)
{
  GstFlowReturn result = GST_FLOW_OK;
  ReportData data;

  g_return_val_if_fail (RTP_IS_SESSION (sess), GST_FLOW_ERROR);

  data.sess = sess;
  data.rtcp = NULL;

  RTP_SESSION_LOCK (sess);
  /* loop over all known sources and do something */
  g_hash_table_foreach (sess->ssrcs[sess->mask_idx],
      (GHFunc) session_report_blocks, &data);

  /* add SDES for this source */
  if (data.rtcp) {
    session_sdes (sess, data.rtcp);
    sess->stats.sent_rtcp = TRUE;
  }

  RTP_SESSION_UNLOCK (sess);

  /* push out the RTCP packet */
  if (data.rtcp) {
    /* close the RTCP packet */
    gst_rtcp_buffer_end (data.rtcp);

    if (sess->callbacks.send_rtcp)
      result = sess->callbacks.send_rtcp (sess, sess->source, data.rtcp,
          sess->user_data);
    else
      gst_buffer_unref (data.rtcp);
  }
  return result;
}
