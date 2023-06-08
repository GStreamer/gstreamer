/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2005 Wim Taymans <wim@fluendo.com>
 *                    2005 Andy Wingo <wingo@pobox.com>
 * Copyright (C) 2012 Collabora Ltd. <tim.muller@collabora.co.uk>
 * Copyright (C) 2015 Sebastian Dröge <sebastian@centricular.com>
 *
 * gstnetclientclock.h: clock that synchronizes itself to a time provider over
 * the network
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
/**
 * SECTION:gstnetclientclock
 * @title: GstNetClientClock
 * @short_description: Special clock that synchronizes to a remote time
 *                     provider.
 * @see_also: #GstClock, #GstNetTimeProvider, #GstPipeline
 *
 * #GstNetClientClock implements a custom #GstClock that synchronizes its time
 * to a remote time provider such as #GstNetTimeProvider. #GstNtpClock
 * implements a #GstClock that synchronizes its time to a remote NTPv4 server.
 *
 * A new clock is created with gst_net_client_clock_new() or
 * gst_ntp_clock_new(), which takes the address and port of the remote time
 * provider along with a name and an initial time.
 *
 * This clock will poll the time provider and will update its calibration
 * parameters based on the local and remote observations.
 *
 * The "round-trip" property limits the maximum round trip packets can take.
 *
 * Various parameters of the clock can be configured with the parent #GstClock
 * "timeout", "window-size" and "window-threshold" object properties.
 *
 * A #GstNetClientClock and #GstNtpClock is typically set on a #GstPipeline with
 * gst_pipeline_use_clock().
 *
 * If you set a #GstBus on the clock via the "bus" object property, it will
 * send @GST_MESSAGE_ELEMENT messages with an attached #GstStructure containing
 * statistics about clock accuracy and network traffic.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstnettimepacket.h"
#include "gstntppacket.h"
#include "gstnetclientclock.h"
#include "gstnetutils.h"

#include <gio/gio.h>

#include <string.h>

GST_DEBUG_CATEGORY_STATIC (ncc_debug);
#define GST_CAT_DEFAULT (ncc_debug)

typedef struct
{
  GstClock *clock;              /* GstNetClientInternalClock */

  GList *clocks;                /* GstNetClientClocks */

  GstClockID remove_id;
} ClockCache;

G_LOCK_DEFINE_STATIC (clocks_lock);
static GList *clocks = NULL;

#define GST_TYPE_NET_CLIENT_INTERNAL_CLOCK \
  (gst_net_client_internal_clock_get_type())
#define GST_NET_CLIENT_INTERNAL_CLOCK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_NET_CLIENT_INTERNAL_CLOCK,GstNetClientInternalClock))
#define GST_NET_CLIENT_INTERNAL_CLOCK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_NET_CLIENT_INTERNAL_CLOCK,GstNetClientInternalClockClass))
#define GST_IS_NET_CLIENT_INTERNAL_CLOCK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_NET_CLIENT_INTERNAL_CLOCK))
#define GST_IS_NET_CLIENT_INTERNAL_CLOCK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_NET_CLIENT_INTERNAL_CLOCK))

typedef struct _GstNetClientInternalClock GstNetClientInternalClock;
typedef struct _GstNetClientInternalClockClass GstNetClientInternalClockClass;

G_GNUC_INTERNAL GType gst_net_client_internal_clock_get_type (void);

#define DEFAULT_ADDRESS         "127.0.0.1"
#define DEFAULT_PORT            5637
#define DEFAULT_TIMEOUT         GST_SECOND
#define DEFAULT_ROUNDTRIP_LIMIT GST_SECOND
/* Minimum timeout will be immediately (ie, as fast as one RTT), but no
 * more often than 1/20th second (arbitrarily, to spread observations a little) */
#define DEFAULT_MINIMUM_UPDATE_INTERVAL (GST_SECOND / 20)
#define DEFAULT_BASE_TIME       0
#define DEFAULT_QOS_DSCP        -1

/* Maximum number of clock updates we can skip before updating */
#define MAX_SKIPPED_UPDATES 5

#define MEDIAN_PRE_FILTERING_WINDOW 9

enum
{
  PROP_0,
  PROP_ADDRESS,
  PROP_PORT,
  PROP_ROUNDTRIP_LIMIT,
  PROP_MINIMUM_UPDATE_INTERVAL,
  PROP_BUS,
  PROP_BASE_TIME,
  PROP_INTERNAL_CLOCK,
  PROP_IS_NTP,
  PROP_QOS_DSCP
};

struct _GstNetClientInternalClock
{
  GstSystemClock clock;

  GThread *thread;

  GSocket *socket;
  GSocketAddress *servaddr;
  GCancellable *cancel;
  gboolean made_cancel_fd;
  gboolean marked_corrupted;

  GstClockTime timeout_expiration;
  GstClockTime roundtrip_limit;
  GstClockTime rtt_avg;
  GstClockTime minimum_update_interval;
  GstClockTime last_remote_poll_interval;
  GstClockTime last_remote_time;
  GstClockTime remote_avg_old;
  guint skipped_updates;
  GstClockTime last_rtts[MEDIAN_PRE_FILTERING_WINDOW];
  gint last_rtts_missing;

  gchar *address;
  gint port;
  gboolean is_ntp;
  gint qos_dscp;

  /* Protected by OBJECT_LOCK */
  GList *busses;
};

struct _GstNetClientInternalClockClass
{
  GstSystemClockClass parent_class;
};

#define _do_init \
  GST_DEBUG_CATEGORY_INIT (ncc_debug, "netclock", 0, "Network client clock");

G_DEFINE_TYPE_WITH_CODE (GstNetClientInternalClock,
    gst_net_client_internal_clock, GST_TYPE_SYSTEM_CLOCK, _do_init);

static void gst_net_client_internal_clock_finalize (GObject * object);
static void gst_net_client_internal_clock_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_net_client_internal_clock_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_net_client_internal_clock_constructed (GObject * object);

static gboolean gst_net_client_internal_clock_start (GstNetClientInternalClock *
    self);
static void gst_net_client_internal_clock_stop (GstNetClientInternalClock *
    self);

static void
gst_net_client_internal_clock_class_init (GstNetClientInternalClockClass *
    klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = gst_net_client_internal_clock_finalize;
  gobject_class->get_property = gst_net_client_internal_clock_get_property;
  gobject_class->set_property = gst_net_client_internal_clock_set_property;
  gobject_class->constructed = gst_net_client_internal_clock_constructed;

  g_object_class_install_property (gobject_class, PROP_ADDRESS,
      g_param_spec_string ("address", "address",
          "The IP address of the machine providing a time server",
          DEFAULT_ADDRESS,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PORT,
      g_param_spec_int ("port", "port",
          "The port on which the remote server is listening", 0, G_MAXUINT16,
          DEFAULT_PORT,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_IS_NTP,
      g_param_spec_boolean ("is-ntp", "Is NTP",
          "The clock is using the NTPv4 protocol", FALSE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
}

static void
gst_net_client_internal_clock_init (GstNetClientInternalClock * self)
{
  GST_OBJECT_FLAG_SET (self, GST_CLOCK_FLAG_NEEDS_STARTUP_SYNC);

  self->port = DEFAULT_PORT;
  self->address = g_strdup (DEFAULT_ADDRESS);
  self->is_ntp = FALSE;
  self->qos_dscp = DEFAULT_QOS_DSCP;

  gst_clock_set_timeout (GST_CLOCK (self), DEFAULT_TIMEOUT);

  self->thread = NULL;

  self->servaddr = NULL;
  self->rtt_avg = GST_CLOCK_TIME_NONE;
  self->roundtrip_limit = DEFAULT_ROUNDTRIP_LIMIT;
  self->minimum_update_interval = DEFAULT_MINIMUM_UPDATE_INTERVAL;
  self->last_remote_poll_interval = GST_CLOCK_TIME_NONE;
  self->skipped_updates = 0;
  self->last_rtts_missing = MEDIAN_PRE_FILTERING_WINDOW;
  self->marked_corrupted = FALSE;
  self->remote_avg_old = 0;
}

static void
gst_net_client_internal_clock_finalize (GObject * object)
{
  GstNetClientInternalClock *self = GST_NET_CLIENT_INTERNAL_CLOCK (object);

  if (self->thread) {
    gst_net_client_internal_clock_stop (self);
  }

  g_free (self->address);
  self->address = NULL;

  if (self->servaddr != NULL) {
    g_object_unref (self->servaddr);
    self->servaddr = NULL;
  }

  if (self->socket != NULL) {
    if (!g_socket_close (self->socket, NULL))
      GST_ERROR_OBJECT (self, "Failed to close socket");
    g_object_unref (self->socket);
    self->socket = NULL;
  }

  g_warn_if_fail (self->busses == NULL);

  G_OBJECT_CLASS (gst_net_client_internal_clock_parent_class)->finalize
      (object);
}

static void
gst_net_client_internal_clock_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstNetClientInternalClock *self = GST_NET_CLIENT_INTERNAL_CLOCK (object);

  switch (prop_id) {
    case PROP_ADDRESS:
      GST_OBJECT_LOCK (self);
      g_free (self->address);
      self->address = g_value_dup_string (value);
      if (self->address == NULL)
        self->address = g_strdup (DEFAULT_ADDRESS);
      GST_OBJECT_UNLOCK (self);
      break;
    case PROP_PORT:
      GST_OBJECT_LOCK (self);
      self->port = g_value_get_int (value);
      GST_OBJECT_UNLOCK (self);
      break;
    case PROP_IS_NTP:
      GST_OBJECT_LOCK (self);
      self->is_ntp = g_value_get_boolean (value);
      GST_OBJECT_UNLOCK (self);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_net_client_internal_clock_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstNetClientInternalClock *self = GST_NET_CLIENT_INTERNAL_CLOCK (object);

  switch (prop_id) {
    case PROP_ADDRESS:
      GST_OBJECT_LOCK (self);
      g_value_set_string (value, self->address);
      GST_OBJECT_UNLOCK (self);
      break;
    case PROP_PORT:
      g_value_set_int (value, self->port);
      break;
    case PROP_IS_NTP:
      g_value_set_boolean (value, self->is_ntp);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_net_client_internal_clock_constructed (GObject * object)
{
  GstNetClientInternalClock *self = GST_NET_CLIENT_INTERNAL_CLOCK (object);

  G_OBJECT_CLASS (gst_net_client_internal_clock_parent_class)->constructed
      (object);

  if (!gst_net_client_internal_clock_start (self)) {
    g_warning ("failed to start clock '%s'", GST_OBJECT_NAME (self));
  }

  /* all systems go, cap'n */
}

static gint
compare_clock_time (const GstClockTime * a, const GstClockTime * b)
{
  if (*a < *b)
    return -1;
  else if (*a > *b)
    return 1;
  return 0;
}

static void
gst_net_client_internal_clock_observe_times (GstNetClientInternalClock * self,
    GstClockTime local_1, GstClockTime remote_1, GstClockTime remote_2,
    GstClockTime local_2)
{
  GstClockTime current_timeout = 0;
  GstClockTime local_avg, remote_avg;
  gdouble r_squared;
  GstClock *clock;
  GstClockTime rtt, rtt_limit, min_update_interval;
  /* Use for discont tracking */
  GstClockTime time_before = 0;
  GstClockTime min_guess = 0;
  GstClockTimeDiff time_discont = 0;
  gboolean synched, now_synched;
  GstClockTime internal_time, external_time, rate_num, rate_den;
  GstClockTime orig_internal_time, orig_external_time, orig_rate_num,
      orig_rate_den;
  GstClockTime max_discont;
  GstClockTime last_rtts[MEDIAN_PRE_FILTERING_WINDOW];
  GstClockTime median;
  gint i;

  GST_OBJECT_LOCK (self);
  rtt_limit = self->roundtrip_limit;

  GST_LOG_OBJECT (self,
      "local1 %" G_GUINT64_FORMAT " remote1 %" G_GUINT64_FORMAT " remote2 %"
      G_GUINT64_FORMAT " local2 %" G_GUINT64_FORMAT, local_1, remote_1,
      remote_2, local_2);

  /* if we are ahead of time the time server may have been reset */
  if (self->last_remote_time > remote_1 || self->marked_corrupted)
    goto corrupted;
  self->last_remote_time = remote_1;

  /* If the server told us a poll interval and it's bigger than the
   * one configured via the property, use the server's */
  if (self->last_remote_poll_interval != GST_CLOCK_TIME_NONE &&
      self->last_remote_poll_interval > self->minimum_update_interval)
    min_update_interval = self->last_remote_poll_interval;
  else
    min_update_interval = self->minimum_update_interval;
  GST_OBJECT_UNLOCK (self);

  if (local_2 < local_1) {
    GST_LOG_OBJECT (self, "Dropping observation: receive time %" GST_TIME_FORMAT
        " < send time %" GST_TIME_FORMAT, GST_TIME_ARGS (local_1),
        GST_TIME_ARGS (local_2));
    goto bogus_observation;
  }

  if (remote_2 < remote_1) {
    GST_LOG_OBJECT (self,
        "Dropping observation: remote receive time %" GST_TIME_FORMAT
        " < send time %" GST_TIME_FORMAT, GST_TIME_ARGS (remote_1),
        GST_TIME_ARGS (remote_2));
    goto bogus_observation;
  }

  /* The round trip time is (assuming symmetric path delays)
   * delta = (local_2 - local_1) - (remote_2 - remote_1)
   */

  rtt = GST_CLOCK_DIFF (local_1, local_2) - GST_CLOCK_DIFF (remote_1, remote_2);

  if ((rtt_limit > 0) && (rtt > rtt_limit)) {
    GST_LOG_OBJECT (self,
        "Dropping observation: RTT %" GST_TIME_FORMAT " > limit %"
        GST_TIME_FORMAT, GST_TIME_ARGS (rtt), GST_TIME_ARGS (rtt_limit));
    goto bogus_observation;
  }

  for (i = 1; i < MEDIAN_PRE_FILTERING_WINDOW; i++)
    self->last_rtts[i - 1] = self->last_rtts[i];
  self->last_rtts[i - 1] = rtt;

  if (self->last_rtts_missing) {
    self->last_rtts_missing--;
  } else {
    memcpy (&last_rtts, &self->last_rtts, sizeof (last_rtts));
    g_qsort_with_data (&last_rtts,
        MEDIAN_PRE_FILTERING_WINDOW, sizeof (GstClockTime),
        (GCompareDataFunc) compare_clock_time, NULL);

    median = last_rtts[MEDIAN_PRE_FILTERING_WINDOW / 2];

    /* FIXME: We might want to use something else here, like only allowing
     * things in the interquartile range, or also filtering away delays that
     * are too small compared to the median. This here worked well enough
     * in tests so far.
     */
    if (rtt > 2 * median) {
      GST_LOG_OBJECT (self,
          "Dropping observation, long RTT %" GST_TIME_FORMAT " > 2 * median %"
          GST_TIME_FORMAT, GST_TIME_ARGS (rtt), GST_TIME_ARGS (median));
      goto bogus_observation;
    }
  }

  /* Track an average round trip time, for a bit of smoothing */
  /* Always update before discarding a sample, so genuine changes in
   * the network get picked up, eventually */
  if (self->rtt_avg == GST_CLOCK_TIME_NONE)
    self->rtt_avg = rtt;
  else if (rtt < self->rtt_avg) /* Shorter RTTs carry more weight than longer */
    self->rtt_avg = (3 * self->rtt_avg + rtt) / 4;
  else
    self->rtt_avg = (15 * self->rtt_avg + rtt) / 16;

  if (rtt > 2 * self->rtt_avg) {
    GST_LOG_OBJECT (self,
        "Dropping observation, long RTT %" GST_TIME_FORMAT " > 2 * avg %"
        GST_TIME_FORMAT, GST_TIME_ARGS (rtt), GST_TIME_ARGS (self->rtt_avg));
    goto bogus_observation;
  }

  /* The difference between the local and remote clock (again assuming
   * symmetric path delays):
   *
   * local_1 + delta / 2 - remote_1 = theta
   * or
   * local_2 - delta / 2 - remote_2 = theta
   *
   * which gives after some simple algebraic transformations:
   *
   *         (remote_1 - local_1) + (remote_2 - local_2)
   * theta = -------------------------------------------
   *                              2
   *
   *
   * Thus remote time at local_avg is equal to:
   *
   * local_avg + theta =
   *
   * local_1 + local_2   (remote_1 - local_1) + (remote_2 - local_2)
   * ----------------- + -------------------------------------------
   *         2                                2
   *
   * =
   *
   * remote_1 + remote_2
   * ------------------- = remote_avg
   *          2
   *
   * We use this for our clock estimation, i.e. local_avg at remote clock
   * being the same as remote_avg.
   */

  local_avg = (local_2 + local_1) / 2;
  remote_avg = (remote_2 + remote_1) / 2;

  GST_LOG_OBJECT (self,
      "remoteavg %" G_GUINT64_FORMAT " localavg %" G_GUINT64_FORMAT,
      remote_avg, local_avg);

  clock = GST_CLOCK_CAST (self);

  /* Store what the clock produced as 'now' before this update */
  gst_clock_get_calibration (GST_CLOCK_CAST (self), &orig_internal_time,
      &orig_external_time, &orig_rate_num, &orig_rate_den);
  internal_time = orig_internal_time;
  external_time = orig_external_time;
  rate_num = orig_rate_num;
  rate_den = orig_rate_den;

  min_guess =
      gst_clock_adjust_with_calibration (GST_CLOCK_CAST (self), local_1,
      internal_time, external_time, rate_num, rate_den);
  time_before =
      gst_clock_adjust_with_calibration (GST_CLOCK_CAST (self), local_2,
      internal_time, external_time, rate_num, rate_den);

  /* Maximum discontinuity, when we're synched with the master. Could make this a property,
   * but this value seems to work fine */
  max_discont = self->rtt_avg / 4;

  /* If the remote observation was within a max_discont window around our min/max estimates, we're synched */
  synched =
      (GST_CLOCK_DIFF (remote_avg, min_guess) < (GstClockTimeDiff) (max_discont)
      && GST_CLOCK_DIFF (time_before,
          remote_avg) < (GstClockTimeDiff) (max_discont));

  /* Check if new remote_avg is less than before to detect if signal lost
   * sync due to the remote clock has restarted. Then the new remote time will
   * be less than the previous time which should not happen if increased in a
   * monotonic way. Also, only perform this check on a synchronized clock to
   * avoid startup issues.
   */
  if (synched) {
    if (remote_avg < self->remote_avg_old) {
      gst_clock_set_synced (GST_CLOCK (self), FALSE);
    } else {
      self->remote_avg_old = remote_avg;
    }
  }

  if (gst_clock_add_observation_unapplied (GST_CLOCK_CAST (self),
          local_avg, remote_avg, &r_squared, &internal_time, &external_time,
          &rate_num, &rate_den)) {

    /* Now compare the difference (discont) in the clock
     * after this observation */
    time_discont = GST_CLOCK_DIFF (time_before,
        gst_clock_adjust_with_calibration (GST_CLOCK_CAST (self), local_2,
            internal_time, external_time, rate_num, rate_den));

    /* If we were in sync with the remote clock, clamp the allowed
     * discontinuity to within quarter of one RTT. In sync means our send/receive estimates
     * of remote time correctly windowed the actual remote time observation */
    if (synched && ABS (time_discont) > max_discont) {
      GstClockTimeDiff offset;
      GST_DEBUG_OBJECT (clock,
          "Too large a discont, clamping to 1/4 average RTT = %"
          GST_TIME_FORMAT, GST_TIME_ARGS (max_discont));
      if (time_discont > 0) {   /* Too large a forward step - add a -ve offset */
        offset = max_discont - time_discont;
        if (-offset > external_time)
          external_time = 0;
        else
          external_time += offset;
      } else {                  /* Too large a backward step - add a +ve offset */
        offset = -(max_discont + time_discont);
        external_time += offset;
      }

      time_discont += offset;
    }

    /* Check if the new clock params would have made our observation within range */
    now_synched =
        (GST_CLOCK_DIFF (remote_avg,
            gst_clock_adjust_with_calibration (GST_CLOCK_CAST (self),
                local_1, internal_time, external_time, rate_num,
                rate_den)) < (GstClockTimeDiff) (max_discont))
        &&
        (GST_CLOCK_DIFF (gst_clock_adjust_with_calibration
            (GST_CLOCK_CAST (self), local_2, internal_time, external_time,
                rate_num, rate_den),
            remote_avg) < (GstClockTimeDiff) (max_discont));

    /* Only update the clock if we had synch or just gained it */
    if (synched || now_synched || self->skipped_updates > MAX_SKIPPED_UPDATES) {
      gst_clock_set_calibration (GST_CLOCK_CAST (self), internal_time,
          external_time, rate_num, rate_den);
      /* ghetto formula - shorter timeout for bad correlations */
      current_timeout = (1e-3 / (1 - MIN (r_squared, 0.99999))) * GST_SECOND;
      current_timeout =
          MIN (current_timeout, gst_clock_get_timeout (GST_CLOCK_CAST (self)));
      self->skipped_updates = 0;

      /* FIXME: When do we consider the clock absolutely not synced anymore? */
      gst_clock_set_synced (GST_CLOCK (self), TRUE);
    } else {
      /* Restore original calibration vars for the report, we're not changing the clock */
      internal_time = orig_internal_time;
      external_time = orig_external_time;
      rate_num = orig_rate_num;
      rate_den = orig_rate_den;
      time_discont = 0;
      self->skipped_updates++;
    }
  }

  /* Limit the polling to at most one per minimum_update_interval */
  if (rtt < min_update_interval)
    current_timeout = MAX (min_update_interval - rtt, current_timeout);

  GST_OBJECT_LOCK (self);
  if (self->busses) {
    GstStructure *s;
    GstMessage *msg;
    GList *l;

    /* Output a stats message, whether we updated the clock or not */
    s = gst_structure_new ("gst-netclock-statistics",
        "synchronised", G_TYPE_BOOLEAN, synched,
        "rtt", G_TYPE_UINT64, rtt,
        "rtt-average", G_TYPE_UINT64, self->rtt_avg,
        "local", G_TYPE_UINT64, local_avg,
        "remote", G_TYPE_UINT64, remote_avg,
        "discontinuity", G_TYPE_INT64, time_discont,
        "remote-min-estimate", G_TYPE_UINT64, min_guess,
        "remote-max-estimate", G_TYPE_UINT64, time_before,
        "remote-min-error", G_TYPE_INT64, GST_CLOCK_DIFF (remote_avg,
            min_guess), "remote-max-error", G_TYPE_INT64,
        GST_CLOCK_DIFF (remote_avg, time_before), "request-send", G_TYPE_UINT64,
        local_1, "request-receive", G_TYPE_UINT64, local_2, "r-squared",
        G_TYPE_DOUBLE, r_squared, "timeout", G_TYPE_UINT64, current_timeout,
        "internal-time", G_TYPE_UINT64, internal_time, "external-time",
        G_TYPE_UINT64, external_time, "rate-num", G_TYPE_UINT64, rate_num,
        "rate-den", G_TYPE_UINT64, rate_den, "rate", G_TYPE_DOUBLE,
        (gdouble) (rate_num) / rate_den, "local-clock-offset", G_TYPE_INT64,
        GST_CLOCK_DIFF (internal_time, external_time), NULL);
    msg = gst_message_new_element (GST_OBJECT (self), s);

    for (l = self->busses; l; l = l->next)
      gst_bus_post (l->data, gst_message_ref (msg));
    gst_message_unref (msg);
  }
  GST_OBJECT_UNLOCK (self);

  GST_INFO ("next timeout: %" GST_TIME_FORMAT, GST_TIME_ARGS (current_timeout));
  self->timeout_expiration = gst_util_get_timestamp () + current_timeout;

  return;

bogus_observation:
  /* Schedule a new packet again soon */
  self->timeout_expiration = gst_util_get_timestamp () + (GST_SECOND / 4);
  return;

corrupted:
  if (!self->marked_corrupted) {
    GST_ERROR_OBJECT (self, "Remote clock time reverted, mark clock invalid");
    self->marked_corrupted = TRUE;
  }
  GST_OBJECT_UNLOCK (self);
  return;
}

static gpointer
gst_net_client_internal_clock_thread (gpointer data)
{
  GstNetClientInternalClock *self = data;
  GSocket *socket = self->socket;
  GError *err = NULL;
  gint cur_qos_dscp = DEFAULT_QOS_DSCP;

  GST_INFO_OBJECT (self, "net client clock thread running, socket=%p", socket);

  g_socket_set_blocking (socket, TRUE);
  g_socket_set_timeout (socket, 0);

  while (!g_cancellable_is_cancelled (self->cancel)) {
    GstClockTime expiration_time = self->timeout_expiration;
    GstClockTime now = gst_util_get_timestamp ();
    gint64 socket_timeout;

    if (now >= expiration_time || (expiration_time - now) <= GST_MSECOND) {
      socket_timeout = 0;
    } else {
      socket_timeout = (expiration_time - now) / GST_USECOND;
    }

    GST_TRACE_OBJECT (self, "timeout: %" G_GINT64_FORMAT "us", socket_timeout);

    if (!g_socket_condition_timed_wait (socket, G_IO_IN, socket_timeout,
            self->cancel, &err)) {
      /* cancelled, timeout or error */
      if (err->code == G_IO_ERROR_CANCELLED) {
        GST_INFO_OBJECT (self, "cancelled");
        g_clear_error (&err);
        break;
      } else if (err->code == G_IO_ERROR_TIMED_OUT) {
        gint new_qos_dscp;

        /* timed out, let's send another packet */
        GST_DEBUG_OBJECT (self, "timed out");

        /* before next sending check if need to change QoS */
        new_qos_dscp = self->qos_dscp;
        if (cur_qos_dscp != new_qos_dscp &&
            gst_net_utils_set_socket_tos (socket, new_qos_dscp)) {
          GST_DEBUG_OBJECT (self, "changed QoS DSCP to: %d", new_qos_dscp);
          cur_qos_dscp = new_qos_dscp;
        }

        if (self->is_ntp) {
          GstNtpPacket *packet;

          packet = gst_ntp_packet_new (NULL, NULL);

          packet->transmit_time =
              gst_clock_get_internal_time (GST_CLOCK_CAST (self));

          GST_DEBUG_OBJECT (self,
              "sending packet, local time = %" GST_TIME_FORMAT,
              GST_TIME_ARGS (packet->transmit_time));

          gst_ntp_packet_send (packet, self->socket, self->servaddr, NULL);

          g_free (packet);
        } else {
          GstNetTimePacket *packet;

          packet = gst_net_time_packet_new (NULL);

          packet->local_time =
              gst_clock_get_internal_time (GST_CLOCK_CAST (self));

          GST_DEBUG_OBJECT (self,
              "sending packet, local time = %" GST_TIME_FORMAT,
              GST_TIME_ARGS (packet->local_time));

          gst_net_time_packet_send (packet, self->socket, self->servaddr, NULL);

          g_free (packet);
        }

        /* reset timeout (but are expecting a response sooner anyway) */
        self->timeout_expiration =
            gst_util_get_timestamp () +
            gst_clock_get_timeout (GST_CLOCK_CAST (self));
      } else {
        GST_DEBUG_OBJECT (self, "socket error: %s", err->message);
        g_usleep (G_USEC_PER_SEC / 10); /* throttle */
      }
      g_clear_error (&err);
    } else {
      GstClockTime new_local;

      /* got packet */

      new_local = gst_clock_get_internal_time (GST_CLOCK_CAST (self));

      if (self->is_ntp) {
        GstNtpPacket *packet;

        packet = gst_ntp_packet_receive (socket, NULL, &err);

        if (packet != NULL) {
          GST_LOG_OBJECT (self, "got packet back");
          GST_LOG_OBJECT (self, "local_1 = %" GST_TIME_FORMAT,
              GST_TIME_ARGS (packet->origin_time));
          GST_LOG_OBJECT (self, "remote_1 = %" GST_TIME_FORMAT,
              GST_TIME_ARGS (packet->receive_time));
          GST_LOG_OBJECT (self, "remote_2 = %" GST_TIME_FORMAT,
              GST_TIME_ARGS (packet->transmit_time));
          GST_LOG_OBJECT (self, "local_2 = %" GST_TIME_FORMAT,
              GST_TIME_ARGS (new_local));
          GST_LOG_OBJECT (self, "poll_interval = %" GST_TIME_FORMAT,
              GST_TIME_ARGS (packet->poll_interval));

          /* Remember the last poll interval we ever got from the server */
          if (packet->poll_interval != GST_CLOCK_TIME_NONE)
            self->last_remote_poll_interval = packet->poll_interval;

          /* observe_times will reset the timeout */
          gst_net_client_internal_clock_observe_times (self,
              packet->origin_time, packet->receive_time, packet->transmit_time,
              new_local);

          g_free (packet);
        } else if (err != NULL) {
          if (g_error_matches (err, GST_NTP_ERROR, GST_NTP_ERROR_WRONG_VERSION)
              || g_error_matches (err, GST_NTP_ERROR, GST_NTP_ERROR_KOD_DENY)) {
            GST_ERROR_OBJECT (self, "fatal receive error: %s", err->message);
            g_clear_error (&err);
            break;
          } else if (g_error_matches (err, GST_NTP_ERROR,
                  GST_NTP_ERROR_KOD_RATE)) {
            GST_WARNING_OBJECT (self, "need to limit rate");

            /* If the server did not tell us a poll interval before, double
             * our minimum poll interval. Otherwise we assume that the server
             * already told us something sensible and that this error here
             * was just a spurious error */
            if (self->last_remote_poll_interval == GST_CLOCK_TIME_NONE)
              self->minimum_update_interval *= 2;

            /* And wait a bit before we send the next packet instead of
             * sending it immediately */
            self->timeout_expiration =
                gst_util_get_timestamp () +
                gst_clock_get_timeout (GST_CLOCK_CAST (self));
          } else {
            GST_WARNING_OBJECT (self, "receive error: %s", err->message);
          }
          g_clear_error (&err);
        }
      } else {
        GstNetTimePacket *packet;

        packet = gst_net_time_packet_receive (socket, NULL, &err);

        if (packet != NULL) {
          GST_LOG_OBJECT (self, "got packet back");
          GST_LOG_OBJECT (self, "local_1 = %" GST_TIME_FORMAT,
              GST_TIME_ARGS (packet->local_time));
          GST_LOG_OBJECT (self, "remote = %" GST_TIME_FORMAT,
              GST_TIME_ARGS (packet->remote_time));
          GST_LOG_OBJECT (self, "local_2 = %" GST_TIME_FORMAT,
              GST_TIME_ARGS (new_local));

          /* observe_times will reset the timeout */
          gst_net_client_internal_clock_observe_times (self, packet->local_time,
              packet->remote_time, packet->remote_time, new_local);

          g_free (packet);
        } else if (err != NULL) {
          GST_WARNING_OBJECT (self, "receive error: %s", err->message);
          g_clear_error (&err);
        }
      }
    }
  }
  GST_INFO_OBJECT (self, "shutting down net client clock thread");
  return NULL;
}

static gboolean
gst_net_client_internal_clock_start (GstNetClientInternalClock * self)
{
  GSocketAddress *servaddr;
  GSocketAddress *myaddr;
  GSocketAddress *anyaddr;
  GInetAddress *inetaddr;
  GSocket *socket;
  GError *error = NULL;
  GSocketFamily family;
  GPollFD dummy_pollfd;
  GResolver *resolver = NULL;
  GError *err = NULL;

  g_return_val_if_fail (self->address != NULL, FALSE);
  g_return_val_if_fail (self->servaddr == NULL, FALSE);

  /* create target address */
  inetaddr = g_inet_address_new_from_string (self->address);
  if (inetaddr == NULL) {
    GList *results;

    resolver = g_resolver_get_default ();

    results = g_resolver_lookup_by_name (resolver, self->address, NULL, &err);
    if (!results)
      goto failed_to_resolve;

    inetaddr = G_INET_ADDRESS (g_object_ref (results->data));
    g_resolver_free_addresses (results);
    g_object_unref (resolver);
  }

  family = g_inet_address_get_family (inetaddr);

  servaddr = g_inet_socket_address_new (inetaddr, self->port);
  g_object_unref (inetaddr);

  g_assert (servaddr != NULL);

  GST_DEBUG_OBJECT (self, "will communicate with %s:%d", self->address,
      self->port);

  socket = g_socket_new (family, G_SOCKET_TYPE_DATAGRAM,
      G_SOCKET_PROTOCOL_UDP, &error);

  if (socket == NULL)
    goto no_socket;

  GST_DEBUG_OBJECT (self, "binding socket");
  inetaddr = g_inet_address_new_any (family);
  anyaddr = g_inet_socket_address_new (inetaddr, 0);
  g_socket_bind (socket, anyaddr, TRUE, &error);
  g_object_unref (anyaddr);
  g_object_unref (inetaddr);

  if (error != NULL)
    goto bind_error;

  /* check address we're bound to, mostly for debugging purposes */
  myaddr = g_socket_get_local_address (socket, &error);

  if (myaddr == NULL)
    goto getsockname_error;

  GST_DEBUG_OBJECT (self, "socket opened on UDP port %d",
      g_inet_socket_address_get_port (G_INET_SOCKET_ADDRESS (myaddr)));

  g_object_unref (myaddr);

  self->cancel = g_cancellable_new ();
  self->made_cancel_fd =
      g_cancellable_make_pollfd (self->cancel, &dummy_pollfd);

  self->socket = socket;
  self->servaddr = G_SOCKET_ADDRESS (servaddr);

  self->thread = g_thread_try_new ("GstNetClientInternalClock",
      gst_net_client_internal_clock_thread, self, &error);

  if (error != NULL)
    goto no_thread;

  return TRUE;

  /* ERRORS */
no_socket:
  {
    GST_ERROR_OBJECT (self, "socket_new() failed: %s", error->message);
    g_error_free (error);
    return FALSE;
  }
bind_error:
  {
    GST_ERROR_OBJECT (self, "bind failed: %s", error->message);
    g_error_free (error);
    g_object_unref (socket);
    return FALSE;
  }
getsockname_error:
  {
    GST_ERROR_OBJECT (self, "get_local_address() failed: %s", error->message);
    g_error_free (error);
    g_object_unref (socket);
    return FALSE;
  }
failed_to_resolve:
  {
    GST_ERROR_OBJECT (self, "resolving '%s' failed: %s",
        self->address, err->message);
    g_clear_error (&err);
    g_object_unref (resolver);
    return FALSE;
  }
no_thread:
  {
    GST_ERROR_OBJECT (self, "could not create thread: %s", error->message);
    g_object_unref (self->servaddr);
    self->servaddr = NULL;
    g_object_unref (self->socket);
    self->socket = NULL;
    g_error_free (error);
    return FALSE;
  }
}

static void
gst_net_client_internal_clock_stop (GstNetClientInternalClock * self)
{
  if (self->thread == NULL)
    return;

  GST_INFO_OBJECT (self, "stopping...");
  g_cancellable_cancel (self->cancel);

  g_thread_join (self->thread);
  self->thread = NULL;

  if (self->made_cancel_fd)
    g_cancellable_release_fd (self->cancel);

  g_object_unref (self->cancel);
  self->cancel = NULL;

  g_object_unref (self->servaddr);
  self->servaddr = NULL;

  g_object_unref (self->socket);
  self->socket = NULL;

  GST_INFO_OBJECT (self, "stopped");
}

struct _GstNetClientClockPrivate
{
  GstClock *internal_clock;

  GstClockTime roundtrip_limit;
  GstClockTime minimum_update_interval;

  GstClockTime base_time, internal_base_time;

  gchar *address;
  gint port;
  gint qos_dscp;

  GstBus *bus;

  gboolean is_ntp;

  gulong synced_id;
};

G_DEFINE_TYPE_WITH_PRIVATE (GstNetClientClock, gst_net_client_clock,
    GST_TYPE_SYSTEM_CLOCK);

static void gst_net_client_clock_finalize (GObject * object);
static void gst_net_client_clock_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_net_client_clock_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_net_client_clock_constructed (GObject * object);

static GstClockTime gst_net_client_clock_get_internal_time (GstClock * clock);

static void
gst_net_client_clock_class_init (GstNetClientClockClass * klass)
{
  GObjectClass *gobject_class;
  GstClockClass *clock_class;

  gobject_class = G_OBJECT_CLASS (klass);
  clock_class = GST_CLOCK_CLASS (klass);

  gobject_class->finalize = gst_net_client_clock_finalize;
  gobject_class->get_property = gst_net_client_clock_get_property;
  gobject_class->set_property = gst_net_client_clock_set_property;
  gobject_class->constructed = gst_net_client_clock_constructed;

  g_object_class_install_property (gobject_class, PROP_ADDRESS,
      g_param_spec_string ("address", "address",
          "The IP address of the machine providing a time server",
          DEFAULT_ADDRESS,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PORT,
      g_param_spec_int ("port", "port",
          "The port on which the remote server is listening", 0, G_MAXUINT16,
          DEFAULT_PORT,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_BUS,
      g_param_spec_object ("bus", "bus",
          "A GstBus on which to send clock status information", GST_TYPE_BUS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstNetClientInternalClock::round-trip-limit:
   *
   * Maximum allowed round-trip for packets. If this property is set to a nonzero
   * value, all packets with a round-trip interval larger than this limit will be
   * ignored. This is useful for networks with severe and fluctuating transport
   * delays. Filtering out these packets increases stability of the synchronization.
   * On the other hand, the lower the limit, the higher the amount of filtered
   * packets. Empirical tests are typically necessary to estimate a good value
   * for the limit.
   * If the property is set to zero, the limit is disabled.
   *
   * Since: 1.4
   */
  g_object_class_install_property (gobject_class, PROP_ROUNDTRIP_LIMIT,
      g_param_spec_uint64 ("round-trip-limit", "round-trip limit",
          "Maximum tolerable round-trip interval for packets, in nanoseconds "
          "(0 = no limit)", 0, G_MAXUINT64, DEFAULT_ROUNDTRIP_LIMIT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MINIMUM_UPDATE_INTERVAL,
      g_param_spec_uint64 ("minimum-update-interval", "minimum update interval",
          "Minimum polling interval for packets, in nanoseconds"
          "(0 = no limit)", 0, G_MAXUINT64, DEFAULT_MINIMUM_UPDATE_INTERVAL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_BASE_TIME,
      g_param_spec_uint64 ("base-time", "Base Time",
          "Initial time that is reported before synchronization", 0,
          G_MAXUINT64, DEFAULT_BASE_TIME,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_INTERNAL_CLOCK,
      g_param_spec_object ("internal-clock", "Internal Clock",
          "Internal clock that directly slaved to the remote clock",
          GST_TYPE_CLOCK, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_QOS_DSCP,
      g_param_spec_int ("qos-dscp", "QoS diff srv code point",
          "Quality of Service, differentiated services code point (-1 default)",
          -1, 63, DEFAULT_QOS_DSCP,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  clock_class->get_internal_time = gst_net_client_clock_get_internal_time;
}

static void
gst_net_client_clock_init (GstNetClientClock * self)
{
  GstNetClientClockPrivate *priv;
  GstClock *clock;

  self->priv = priv = gst_net_client_clock_get_instance_private (self);

  GST_OBJECT_FLAG_SET (self, GST_CLOCK_FLAG_CAN_SET_MASTER);
  GST_OBJECT_FLAG_SET (self, GST_CLOCK_FLAG_NEEDS_STARTUP_SYNC);

  priv->port = DEFAULT_PORT;
  priv->address = g_strdup (DEFAULT_ADDRESS);
  priv->qos_dscp = DEFAULT_QOS_DSCP;

  priv->roundtrip_limit = DEFAULT_ROUNDTRIP_LIMIT;
  priv->minimum_update_interval = DEFAULT_MINIMUM_UPDATE_INTERVAL;

  clock = gst_system_clock_obtain ();
  priv->base_time = DEFAULT_BASE_TIME;
  priv->internal_base_time = gst_clock_get_time (clock);
  gst_object_unref (clock);
}

/* Must be called with clocks_lock */
static void
update_clock_cache (ClockCache * cache)
{
  GstClockTime roundtrip_limit = 0, minimum_update_interval = 0;
  GList *l, *busses = NULL;
  gint qos_dscp = DEFAULT_QOS_DSCP;

  GST_OBJECT_LOCK (cache->clock);
  g_list_free_full (GST_NET_CLIENT_INTERNAL_CLOCK (cache->clock)->busses,
      (GDestroyNotify) gst_object_unref);

  for (l = cache->clocks; l; l = l->next) {
    GstNetClientClock *clock = l->data;

    if (clock->priv->bus)
      busses = g_list_prepend (busses, gst_object_ref (clock->priv->bus));

    if (roundtrip_limit == 0)
      roundtrip_limit = clock->priv->roundtrip_limit;
    else
      roundtrip_limit = MAX (roundtrip_limit, clock->priv->roundtrip_limit);

    if (minimum_update_interval == 0)
      minimum_update_interval = clock->priv->minimum_update_interval;
    else
      minimum_update_interval =
          MIN (minimum_update_interval, clock->priv->minimum_update_interval);

    qos_dscp = MAX (qos_dscp, clock->priv->qos_dscp);
  }
  GST_NET_CLIENT_INTERNAL_CLOCK (cache->clock)->busses = busses;
  GST_NET_CLIENT_INTERNAL_CLOCK (cache->clock)->roundtrip_limit =
      roundtrip_limit;
  GST_NET_CLIENT_INTERNAL_CLOCK (cache->clock)->minimum_update_interval =
      minimum_update_interval;
  GST_NET_CLIENT_INTERNAL_CLOCK (cache->clock)->qos_dscp = qos_dscp;

  GST_OBJECT_UNLOCK (cache->clock);
}

static gboolean
remove_clock_cache (GstClock * clock, GstClockTime time, GstClockID id,
    gpointer user_data)
{
  ClockCache *cache = user_data;

  G_LOCK (clocks_lock);
  if (!cache->clocks) {
    gst_clock_id_unref (cache->remove_id);
    gst_object_unref (cache->clock);
    clocks = g_list_remove (clocks, cache);
    g_free (cache);
  }
  G_UNLOCK (clocks_lock);

  return TRUE;
}

static void
gst_net_client_clock_finalize (GObject * object)
{
  GstNetClientClock *self = GST_NET_CLIENT_CLOCK (object);
  GList *l;

  if (self->priv->synced_id)
    g_signal_handler_disconnect (self->priv->internal_clock,
        self->priv->synced_id);
  self->priv->synced_id = 0;

  G_LOCK (clocks_lock);
  for (l = clocks; l; l = l->next) {
    ClockCache *cache = l->data;

    if (cache->clock == self->priv->internal_clock) {
      cache->clocks = g_list_remove (cache->clocks, self);

      if (cache->clocks) {
        update_clock_cache (cache);
      } else {
        GstClock *sysclock = gst_system_clock_obtain ();
        GstClockTime time = gst_clock_get_time (sysclock);
        GstNetClientInternalClock *internal_clock =
            GST_NET_CLIENT_INTERNAL_CLOCK (cache->clock);

        /* only defer deletion if the clock is not marked corrupted */
        if (!internal_clock->marked_corrupted)
          time += 60 * GST_SECOND;

        cache->remove_id = gst_clock_new_single_shot_id (sysclock, time);
        gst_clock_id_wait_async (cache->remove_id, remove_clock_cache, cache,
            NULL);
        gst_object_unref (sysclock);
      }
      break;
    }
  }
  G_UNLOCK (clocks_lock);

  g_free (self->priv->address);
  self->priv->address = NULL;

  if (self->priv->bus != NULL) {
    gst_object_unref (self->priv->bus);
    self->priv->bus = NULL;
  }

  G_OBJECT_CLASS (gst_net_client_clock_parent_class)->finalize (object);
}

static void
gst_net_client_clock_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstNetClientClock *self = GST_NET_CLIENT_CLOCK (object);
  gboolean update = FALSE;

  switch (prop_id) {
    case PROP_ADDRESS:
      GST_OBJECT_LOCK (self);
      g_free (self->priv->address);
      self->priv->address = g_value_dup_string (value);
      if (self->priv->address == NULL)
        self->priv->address = g_strdup (DEFAULT_ADDRESS);
      GST_OBJECT_UNLOCK (self);
      break;
    case PROP_PORT:
      GST_OBJECT_LOCK (self);
      self->priv->port = g_value_get_int (value);
      GST_OBJECT_UNLOCK (self);
      break;
    case PROP_ROUNDTRIP_LIMIT:
      GST_OBJECT_LOCK (self);
      self->priv->roundtrip_limit = g_value_get_uint64 (value);
      GST_OBJECT_UNLOCK (self);
      update = TRUE;
      break;
    case PROP_MINIMUM_UPDATE_INTERVAL:
      GST_OBJECT_LOCK (self);
      self->priv->minimum_update_interval = g_value_get_uint64 (value);
      GST_OBJECT_UNLOCK (self);
      update = TRUE;
      break;
    case PROP_BUS:
      GST_OBJECT_LOCK (self);
      if (self->priv->bus)
        gst_object_unref (self->priv->bus);
      self->priv->bus = g_value_dup_object (value);
      GST_OBJECT_UNLOCK (self);
      update = TRUE;
      break;
    case PROP_BASE_TIME:{
      GstClock *clock;

      self->priv->base_time = g_value_get_uint64 (value);
      clock = gst_system_clock_obtain ();
      self->priv->internal_base_time = gst_clock_get_time (clock);
      gst_object_unref (clock);
      break;
    }
    case PROP_QOS_DSCP:
      GST_OBJECT_LOCK (self);
      self->priv->qos_dscp = g_value_get_int (value);
      GST_OBJECT_UNLOCK (self);
      update = TRUE;
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  if (update && self->priv->internal_clock) {
    GList *l;

    G_LOCK (clocks_lock);
    for (l = clocks; l; l = l->next) {
      ClockCache *cache = l->data;

      if (cache->clock == self->priv->internal_clock) {
        update_clock_cache (cache);
      }
    }
    G_UNLOCK (clocks_lock);
  }
}

static void
gst_net_client_clock_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstNetClientClock *self = GST_NET_CLIENT_CLOCK (object);

  switch (prop_id) {
    case PROP_ADDRESS:
      GST_OBJECT_LOCK (self);
      g_value_set_string (value, self->priv->address);
      GST_OBJECT_UNLOCK (self);
      break;
    case PROP_PORT:
      g_value_set_int (value, self->priv->port);
      break;
    case PROP_ROUNDTRIP_LIMIT:
      GST_OBJECT_LOCK (self);
      g_value_set_uint64 (value, self->priv->roundtrip_limit);
      GST_OBJECT_UNLOCK (self);
      break;
    case PROP_MINIMUM_UPDATE_INTERVAL:
      GST_OBJECT_LOCK (self);
      g_value_set_uint64 (value, self->priv->minimum_update_interval);
      GST_OBJECT_UNLOCK (self);
      break;
    case PROP_BUS:
      GST_OBJECT_LOCK (self);
      g_value_set_object (value, self->priv->bus);
      GST_OBJECT_UNLOCK (self);
      break;
    case PROP_BASE_TIME:
      g_value_set_uint64 (value, self->priv->base_time);
      break;
    case PROP_INTERNAL_CLOCK:
      g_value_set_object (value, self->priv->internal_clock);
      break;
    case PROP_QOS_DSCP:
      GST_OBJECT_LOCK (self);
      g_value_set_int (value, self->priv->qos_dscp);
      GST_OBJECT_UNLOCK (self);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_net_client_clock_synced_cb (GstClock * internal_clock, gboolean synced,
    GstClock * self)
{
  gst_clock_set_synced (self, synced);
}

static void
gst_net_client_clock_constructed (GObject * object)
{
  GstNetClientClock *self = GST_NET_CLIENT_CLOCK (object);
  GstClock *internal_clock;
  GList *l;
  ClockCache *cache = NULL;

  G_OBJECT_CLASS (gst_net_client_clock_parent_class)->constructed (object);

  G_LOCK (clocks_lock);
  for (l = clocks; l; l = l->next) {
    ClockCache *tmp = l->data;
    GstNetClientInternalClock *internal_clock =
        GST_NET_CLIENT_INTERNAL_CLOCK (tmp->clock);

    if (internal_clock->marked_corrupted)
      break;

    if (strcmp (internal_clock->address, self->priv->address) == 0 &&
        internal_clock->port == self->priv->port) {
      cache = tmp;

      if (cache->remove_id) {
        gst_clock_id_unschedule (cache->remove_id);
        cache->remove_id = NULL;
      }
      break;
    }
  }

  if (!cache) {
    cache = g_new0 (ClockCache, 1);

    cache->clock =
        g_object_new (GST_TYPE_NET_CLIENT_INTERNAL_CLOCK, "address",
        self->priv->address, "port", self->priv->port, "is-ntp",
        self->priv->is_ntp, NULL);
    gst_object_ref_sink (cache->clock);
    clocks = g_list_prepend (clocks, cache);

    /* Not actually leaked but is cached for a while before being disposed,
     * see gst_net_client_clock_finalize, so pretend it is to not confuse
     * tests. */
    GST_OBJECT_FLAG_SET (cache->clock, GST_OBJECT_FLAG_MAY_BE_LEAKED);
  }

  cache->clocks = g_list_prepend (cache->clocks, self);

  GST_OBJECT_LOCK (cache->clock);
  if (gst_clock_is_synced (cache->clock))
    gst_clock_set_synced (GST_CLOCK (self), TRUE);
  self->priv->synced_id =
      g_signal_connect (cache->clock, "synced",
      G_CALLBACK (gst_net_client_clock_synced_cb), self);
  GST_OBJECT_UNLOCK (cache->clock);

  G_UNLOCK (clocks_lock);

  self->priv->internal_clock = internal_clock = cache->clock;

  /* all systems go, cap'n */
}

static GstClockTime
gst_net_client_clock_get_internal_time (GstClock * clock)
{
  GstNetClientClock *self = GST_NET_CLIENT_CLOCK (clock);

  if (!gst_clock_is_synced (self->priv->internal_clock)) {
    GstClockTime now = gst_clock_get_internal_time (self->priv->internal_clock);
    return gst_clock_adjust_with_calibration (self->priv->internal_clock, now,
        self->priv->internal_base_time, self->priv->base_time, 1, 1);
  }

  return gst_clock_get_time (self->priv->internal_clock);
}

/**
 * gst_net_client_clock_new:
 * @name: (nullable): a name for the clock
 * @remote_address: the address or hostname of the remote clock provider
 * @remote_port: the port of the remote clock provider
 * @base_time: initial time of the clock
 *
 * Create a new #GstNetClientClock that will report the time
 * provided by the #GstNetTimeProvider on @remote_address and
 * @remote_port.
 *
 * Returns: (transfer full): a new #GstClock that receives a time from the remote
 * clock.
 */
GstClock *
gst_net_client_clock_new (const gchar * name, const gchar * remote_address,
    gint remote_port, GstClockTime base_time)
{
  GstClock *ret;

  g_return_val_if_fail (remote_address != NULL, NULL);
  g_return_val_if_fail (remote_port > 0, NULL);
  g_return_val_if_fail (remote_port <= G_MAXUINT16, NULL);
  g_return_val_if_fail (base_time != GST_CLOCK_TIME_NONE, NULL);

  ret =
      g_object_new (GST_TYPE_NET_CLIENT_CLOCK, "name", name, "address",
      remote_address, "port", remote_port, "base-time", base_time, NULL);

  /* Clear floating flag */
  gst_object_ref_sink (ret);

  return ret;
}

G_DEFINE_TYPE (GstNtpClock, gst_ntp_clock, GST_TYPE_NET_CLIENT_CLOCK);

static void
gst_ntp_clock_class_init (GstNtpClockClass * klass)
{
}

static void
gst_ntp_clock_init (GstNtpClock * self)
{
  GST_NET_CLIENT_CLOCK (self)->priv->is_ntp = TRUE;
}

/**
 * gst_ntp_clock_new:
 * @name: (nullable): a name for the clock
 * @remote_address: the address or hostname of the remote clock provider
 * @remote_port: the port of the remote clock provider
 * @base_time: initial time of the clock
 *
 * Create a new #GstNtpClock that will report the time provided by
 * the NTPv4 server on @remote_address and @remote_port.
 *
 * Returns: (transfer full): a new #GstClock that receives a time from the remote
 * clock.
 *
 * Since: 1.6
 */
GstClock *
gst_ntp_clock_new (const gchar * name, const gchar * remote_address,
    gint remote_port, GstClockTime base_time)
{
  GstClock *ret;

  g_return_val_if_fail (remote_address != NULL, NULL);
  g_return_val_if_fail (remote_port > 0, NULL);
  g_return_val_if_fail (remote_port <= G_MAXUINT16, NULL);
  g_return_val_if_fail (base_time != GST_CLOCK_TIME_NONE, NULL);

  ret =
      g_object_new (GST_TYPE_NTP_CLOCK, "name", name, "address", remote_address,
      "port", remote_port, "base-time", base_time, NULL);

  gst_object_ref_sink (ret);

  return ret;
}
