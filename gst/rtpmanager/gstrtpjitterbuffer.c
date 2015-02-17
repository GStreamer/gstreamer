/*
 * Farsight Voice+Video library
 *
 *  Copyright 2007 Collabora Ltd,
 *  Copyright 2007 Nokia Corporation
 *   @author: Philippe Kalaf <philippe.kalaf@collabora.co.uk>.
 *  Copyright 2007 Wim Taymans <wim.taymans@gmail.com>
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
 *
 */

/**
 * SECTION:element-rtpjitterbuffer
 *
 * This element reorders and removes duplicate RTP packets as they are received
 * from a network source.
 *
 * The element needs the clock-rate of the RTP payload in order to estimate the
 * delay. This information is obtained either from the caps on the sink pad or,
 * when no caps are present, from the #GstRtpJitterBuffer::request-pt-map signal.
 * To clear the previous pt-map use the #GstRtpJitterBuffer::clear-pt-map signal.
 *
 * The rtpjitterbuffer will wait for missing packets up to a configurable time
 * limit using the #GstRtpJitterBuffer:latency property. Packets arriving too
 * late are considered to be lost packets. If the #GstRtpJitterBuffer:do-lost
 * property is set, lost packets will result in a custom serialized downstream
 * event of name GstRTPPacketLost. The lost packet events are usually used by a
 * depayloader or other element to create concealment data or some other logic
 * to gracefully handle the missing packets.
 *
 * The jitterbuffer will use the DTS (or PTS if no DTS is set) of the incomming
 * buffer and the rtptime inside the RTP packet to create a PTS on the outgoing
 * buffer.
 *
 * The jitterbuffer can also be configured to send early retransmission events
 * upstream by setting the #GstRtpJitterBuffer:do-retransmission property. In
 * this mode, the jitterbuffer tries to estimate when a packet should arrive and
 * sends a custom upstream event named GstRTPRetransmissionRequest when the
 * packet is considered late. The initial expected packet arrival time is
 * calculated as follows:
 *
 * - If seqnum N arrived at time T, seqnum N+1 is expected to arrive at
 *     T + packet-spacing + #GstRtpJitterBuffer:rtx-delay. The packet spacing is
 *     calculated from the DTS (or PTS is no DTS) of two consecutive RTP
 *     packets with different rtptime.
 *
 * - If seqnum N0 arrived at time T0 and seqnum Nm arrived at time Tm,
 *     seqnum Ni is expected at time Ti = T0 + i*(Tm - T0)/(Nm - N0). Any
 *     previously scheduled timeout is overwritten.
 *
 * - If seqnum N arrived, all seqnum older than
 *     N - #GstRtpJitterBuffer:rtx-delay-reorder are considered late
 *     immediately. This is to request fast feedback for abonormally reorder
 *     packets before any of the previous timeouts is triggered.
 *
 * A late packet triggers the GstRTPRetransmissionRequest custom upstream
 * event. After the initial timeout expires and the retransmission event is
 * sent, the timeout is scheduled for
 * T + #GstRtpJitterBuffer:rtx-retry-timeout. If the missing packet did not
 * arrive after #GstRtpJitterBuffer:rtx-retry-timeout, a new
 * GstRTPRetransmissionRequest is sent upstream and the timeout is rescheduled
 * again for T + #GstRtpJitterBuffer:rtx-retry-timeout. This repeats until
 * #GstRtpJitterBuffer:rtx-retry-period elapsed, at which point no further
 * retransmission requests are sent and the regular logic is performed to
 * schedule a lost packet as discussed above.
 *
 * This element acts as a live element and so adds #GstRtpJitterBuffer:latency
 * to the pipeline.
 *
 * This element will automatically be used inside rtpbin.
 *
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * gst-launch-1.0 rtspsrc location=rtsp://192.168.1.133:8554/mpeg1or2AudioVideoTest ! rtpjitterbuffer ! rtpmpvdepay ! mpeg2dec ! xvimagesink
 * ]| Connect to a streaming server and decode the MPEG video. The jitterbuffer is
 * inserted into the pipeline to smooth out network jitter and to reorder the
 * out-of-order RTP packets.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <gst/rtp/gstrtpbuffer.h>

#include "gstrtpjitterbuffer.h"
#include "rtpjitterbuffer.h"
#include "rtpstats.h"

#include <gst/glib-compat-private.h>

GST_DEBUG_CATEGORY (rtpjitterbuffer_debug);
#define GST_CAT_DEFAULT (rtpjitterbuffer_debug)

/* RTPJitterBuffer signals and args */
enum
{
  SIGNAL_REQUEST_PT_MAP,
  SIGNAL_CLEAR_PT_MAP,
  SIGNAL_HANDLE_SYNC,
  SIGNAL_ON_NPT_STOP,
  SIGNAL_SET_ACTIVE,
  LAST_SIGNAL
};

#define DEFAULT_LATENCY_MS          200
#define DEFAULT_DROP_ON_LATENCY     FALSE
#define DEFAULT_TS_OFFSET           0
#define DEFAULT_DO_LOST             FALSE
#define DEFAULT_MODE                RTP_JITTER_BUFFER_MODE_SLAVE
#define DEFAULT_PERCENT             0
#define DEFAULT_DO_RETRANSMISSION   FALSE
#define DEFAULT_RTX_DELAY           -1
#define DEFAULT_RTX_MIN_DELAY       0
#define DEFAULT_RTX_DELAY_REORDER   3
#define DEFAULT_RTX_RETRY_TIMEOUT   -1
#define DEFAULT_RTX_MIN_RETRY_TIMEOUT   -1
#define DEFAULT_RTX_RETRY_PERIOD    -1

#define DEFAULT_AUTO_RTX_DELAY (20 * GST_MSECOND)
#define DEFAULT_AUTO_RTX_TIMEOUT (40 * GST_MSECOND)

enum
{
  PROP_0,
  PROP_LATENCY,
  PROP_DROP_ON_LATENCY,
  PROP_TS_OFFSET,
  PROP_DO_LOST,
  PROP_MODE,
  PROP_PERCENT,
  PROP_DO_RETRANSMISSION,
  PROP_RTX_DELAY,
  PROP_RTX_MIN_DELAY,
  PROP_RTX_DELAY_REORDER,
  PROP_RTX_RETRY_TIMEOUT,
  PROP_RTX_MIN_RETRY_TIMEOUT,
  PROP_RTX_RETRY_PERIOD,
  PROP_STATS,
  PROP_LAST
};

#define JBUF_LOCK(priv)   (g_mutex_lock (&(priv)->jbuf_lock))

#define JBUF_LOCK_CHECK(priv,label) G_STMT_START {    \
  JBUF_LOCK (priv);                                   \
  if (G_UNLIKELY (priv->srcresult != GST_FLOW_OK))    \
    goto label;                                       \
} G_STMT_END
#define JBUF_UNLOCK(priv) (g_mutex_unlock (&(priv)->jbuf_lock))

#define JBUF_WAIT_TIMER(priv)   G_STMT_START {            \
  GST_DEBUG ("waiting timer");                            \
  (priv)->waiting_timer = TRUE;                           \
  g_cond_wait (&(priv)->jbuf_timer, &(priv)->jbuf_lock);  \
  (priv)->waiting_timer = FALSE;                          \
  GST_DEBUG ("waiting timer done");                       \
} G_STMT_END
#define JBUF_SIGNAL_TIMER(priv) G_STMT_START {            \
  if (G_UNLIKELY ((priv)->waiting_timer)) {               \
    GST_DEBUG ("signal timer");                           \
    g_cond_signal (&(priv)->jbuf_timer);                  \
  }                                                       \
} G_STMT_END

#define JBUF_WAIT_EVENT(priv,label) G_STMT_START {       \
  GST_DEBUG ("waiting event");                           \
  (priv)->waiting_event = TRUE;                          \
  g_cond_wait (&(priv)->jbuf_event, &(priv)->jbuf_lock); \
  (priv)->waiting_event = FALSE;                         \
  GST_DEBUG ("waiting event done");                      \
  if (G_UNLIKELY (priv->srcresult != GST_FLOW_OK))       \
    goto label;                                          \
} G_STMT_END
#define JBUF_SIGNAL_EVENT(priv) G_STMT_START {           \
  if (G_UNLIKELY ((priv)->waiting_event)) {              \
    GST_DEBUG ("signal event");                          \
    g_cond_signal (&(priv)->jbuf_event);                 \
  }                                                      \
} G_STMT_END

#define JBUF_WAIT_QUERY(priv,label) G_STMT_START {       \
  GST_DEBUG ("waiting query");                           \
  (priv)->waiting_query = TRUE;                          \
  g_cond_wait (&(priv)->jbuf_query, &(priv)->jbuf_lock); \
  (priv)->waiting_query = FALSE;                         \
  GST_DEBUG ("waiting query done");                      \
  if (G_UNLIKELY (priv->srcresult != GST_FLOW_OK))       \
    goto label;                                          \
} G_STMT_END
#define JBUF_SIGNAL_QUERY(priv,res) G_STMT_START {       \
  (priv)->last_query = res;                              \
  if (G_UNLIKELY ((priv)->waiting_query)) {              \
    GST_DEBUG ("signal query");                          \
    g_cond_signal (&(priv)->jbuf_query);                 \
  }                                                      \
} G_STMT_END


struct _GstRtpJitterBufferPrivate
{
  GstPad *sinkpad, *srcpad;
  GstPad *rtcpsinkpad;

  RTPJitterBuffer *jbuf;
  GMutex jbuf_lock;
  gboolean waiting_timer;
  GCond jbuf_timer;
  gboolean waiting_event;
  GCond jbuf_event;
  gboolean waiting_query;
  GCond jbuf_query;
  gboolean last_query;
  gboolean discont;
  gboolean ts_discont;
  gboolean active;
  guint64 out_offset;

  gboolean timer_running;
  GThread *timer_thread;

  /* properties */
  guint latency_ms;
  guint64 latency_ns;
  gboolean drop_on_latency;
  gint64 ts_offset;
  gboolean do_lost;
  gboolean do_retransmission;
  gint rtx_delay;
  guint rtx_min_delay;
  gint rtx_delay_reorder;
  gint rtx_retry_timeout;
  gint rtx_min_retry_timeout;
  gint rtx_retry_period;

  /* the last seqnum we pushed out */
  guint32 last_popped_seqnum;
  /* the next expected seqnum we push */
  guint32 next_seqnum;
  /* last output time */
  GstClockTime last_out_time;
  /* last valid input timestamp and rtptime pair */
  GstClockTime ips_dts;
  guint64 ips_rtptime;
  GstClockTime packet_spacing;

  /* the next expected seqnum we receive */
  GstClockTime last_in_dts;
  guint32 last_in_seqnum;
  guint32 next_in_seqnum;

  GArray *timers;

  /* start and stop ranges */
  GstClockTime npt_start;
  GstClockTime npt_stop;
  guint64 ext_timestamp;
  guint64 last_elapsed;
  guint64 estimated_eos;
  GstClockID eos_id;

  /* state */
  gboolean eos;
  guint last_percent;

  /* clock rate and rtp timestamp offset */
  gint last_pt;
  gint32 clock_rate;
  gint64 clock_base;
  gint64 prev_ts_offset;

  /* when we are shutting down */
  GstFlowReturn srcresult;
  gboolean blocked;

  /* for sync */
  GstSegment segment;
  GstClockID clock_id;
  GstClockTime timer_timeout;
  guint16 timer_seqnum;
  /* the latency of the upstream peer, we have to take this into account when
   * synchronizing the buffers. */
  GstClockTime peer_latency;
  guint64 ext_rtptime;
  GstBuffer *last_sr;

  /* some accounting */
  guint64 num_late;
  guint64 num_duplicates;
  guint64 num_rtx_requests;
  guint64 num_rtx_success;
  guint64 num_rtx_failed;
  gdouble avg_rtx_num;
  guint64 avg_rtx_rtt;

  /* for the jitter */
  GstClockTime last_dts;
  guint64 last_rtptime;
  GstClockTime avg_jitter;
};

typedef enum
{
  TIMER_TYPE_EXPECTED,
  TIMER_TYPE_LOST,
  TIMER_TYPE_DEADLINE,
  TIMER_TYPE_EOS
} TimerType;

typedef struct
{
  guint idx;
  guint16 seqnum;
  guint num;
  TimerType type;
  GstClockTime timeout;
  GstClockTime duration;
  GstClockTime rtx_base;
  GstClockTime rtx_delay;
  GstClockTime rtx_retry;
  GstClockTime rtx_last;
  guint num_rtx_retry;
} TimerData;

#define GST_RTP_JITTER_BUFFER_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), GST_TYPE_RTP_JITTER_BUFFER, \
                                GstRtpJitterBufferPrivate))

static GstStaticPadTemplate gst_rtp_jitter_buffer_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp"
        /* "clock-rate = (int) [ 1, 2147483647 ], "
         * "payload = (int) , "
         * "encoding-name = (string) "
         */ )
    );

static GstStaticPadTemplate gst_rtp_jitter_buffer_sink_rtcp_template =
GST_STATIC_PAD_TEMPLATE ("sink_rtcp",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("application/x-rtcp")
    );

static GstStaticPadTemplate gst_rtp_jitter_buffer_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp"
        /* "payload = (int) , "
         * "clock-rate = (int) , "
         * "encoding-name = (string) "
         */ )
    );

static guint gst_rtp_jitter_buffer_signals[LAST_SIGNAL] = { 0 };

#define gst_rtp_jitter_buffer_parent_class parent_class
G_DEFINE_TYPE (GstRtpJitterBuffer, gst_rtp_jitter_buffer, GST_TYPE_ELEMENT);

/* object overrides */
static void gst_rtp_jitter_buffer_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_rtp_jitter_buffer_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_rtp_jitter_buffer_finalize (GObject * object);

/* element overrides */
static GstStateChangeReturn gst_rtp_jitter_buffer_change_state (GstElement
    * element, GstStateChange transition);
static GstPad *gst_rtp_jitter_buffer_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name, const GstCaps * filter);
static void gst_rtp_jitter_buffer_release_pad (GstElement * element,
    GstPad * pad);
static GstClock *gst_rtp_jitter_buffer_provide_clock (GstElement * element);

/* pad overrides */
static GstCaps *gst_rtp_jitter_buffer_getcaps (GstPad * pad, GstCaps * filter);
static GstIterator *gst_rtp_jitter_buffer_iterate_internal_links (GstPad * pad,
    GstObject * parent);

/* sinkpad overrides */
static gboolean gst_rtp_jitter_buffer_sink_event (GstPad * pad,
    GstObject * parent, GstEvent * event);
static GstFlowReturn gst_rtp_jitter_buffer_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buffer);

static gboolean gst_rtp_jitter_buffer_sink_rtcp_event (GstPad * pad,
    GstObject * parent, GstEvent * event);
static GstFlowReturn gst_rtp_jitter_buffer_chain_rtcp (GstPad * pad,
    GstObject * parent, GstBuffer * buffer);

static gboolean gst_rtp_jitter_buffer_sink_query (GstPad * pad,
    GstObject * parent, GstQuery * query);

/* srcpad overrides */
static gboolean gst_rtp_jitter_buffer_src_event (GstPad * pad,
    GstObject * parent, GstEvent * event);
static gboolean gst_rtp_jitter_buffer_src_activate_mode (GstPad * pad,
    GstObject * parent, GstPadMode mode, gboolean active);
static void gst_rtp_jitter_buffer_loop (GstRtpJitterBuffer * jitterbuffer);
static gboolean gst_rtp_jitter_buffer_src_query (GstPad * pad,
    GstObject * parent, GstQuery * query);

static void
gst_rtp_jitter_buffer_clear_pt_map (GstRtpJitterBuffer * jitterbuffer);
static GstClockTime
gst_rtp_jitter_buffer_set_active (GstRtpJitterBuffer * jitterbuffer,
    gboolean active, guint64 base_time);
static void do_handle_sync (GstRtpJitterBuffer * jitterbuffer);

static void unschedule_current_timer (GstRtpJitterBuffer * jitterbuffer);
static void remove_all_timers (GstRtpJitterBuffer * jitterbuffer);

static void wait_next_timeout (GstRtpJitterBuffer * jitterbuffer);

static GstStructure *gst_rtp_jitter_buffer_create_stats (GstRtpJitterBuffer *
    jitterbuffer);

static void
gst_rtp_jitter_buffer_class_init (GstRtpJitterBufferClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  g_type_class_add_private (klass, sizeof (GstRtpJitterBufferPrivate));

  gobject_class->finalize = gst_rtp_jitter_buffer_finalize;

  gobject_class->set_property = gst_rtp_jitter_buffer_set_property;
  gobject_class->get_property = gst_rtp_jitter_buffer_get_property;

  /**
   * GstRtpJitterBuffer:latency:
   *
   * The maximum latency of the jitterbuffer. Packets will be kept in the buffer
   * for at most this time.
   */
  g_object_class_install_property (gobject_class, PROP_LATENCY,
      g_param_spec_uint ("latency", "Buffer latency in ms",
          "Amount of ms to buffer", 0, G_MAXUINT, DEFAULT_LATENCY_MS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstRtpJitterBuffer:drop-on-latency:
   *
   * Drop oldest buffers when the queue is completely filled.
   */
  g_object_class_install_property (gobject_class, PROP_DROP_ON_LATENCY,
      g_param_spec_boolean ("drop-on-latency",
          "Drop buffers when maximum latency is reached",
          "Tells the jitterbuffer to never exceed the given latency in size",
          DEFAULT_DROP_ON_LATENCY, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstRtpJitterBuffer:ts-offset:
   *
   * Adjust GStreamer output buffer timestamps in the jitterbuffer with offset.
   * This is mainly used to ensure interstream synchronisation.
   */
  g_object_class_install_property (gobject_class, PROP_TS_OFFSET,
      g_param_spec_int64 ("ts-offset", "Timestamp Offset",
          "Adjust buffer timestamps with offset in nanoseconds", G_MININT64,
          G_MAXINT64, DEFAULT_TS_OFFSET,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstRtpJitterBuffer:do-lost:
   *
   * Send out a GstRTPPacketLost event downstream when a packet is considered
   * lost.
   */
  g_object_class_install_property (gobject_class, PROP_DO_LOST,
      g_param_spec_boolean ("do-lost", "Do Lost",
          "Send an event downstream when a packet is lost", DEFAULT_DO_LOST,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstRtpJitterBuffer:mode:
   *
   * Control the buffering and timestamping mode used by the jitterbuffer.
   */
  g_object_class_install_property (gobject_class, PROP_MODE,
      g_param_spec_enum ("mode", "Mode",
          "Control the buffering algorithm in use", RTP_TYPE_JITTER_BUFFER_MODE,
          DEFAULT_MODE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstRtpJitterBuffer:percent:
   *
   * The percent of the jitterbuffer that is filled.
   */
  g_object_class_install_property (gobject_class, PROP_PERCENT,
      g_param_spec_int ("percent", "percent",
          "The buffer filled percent", 0, 100,
          0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  /**
   * GstRtpJitterBuffer:do-retransmission:
   *
   * Send out a GstRTPRetransmission event upstream when a packet is considered
   * late and should be retransmitted.
   *
   * Since: 1.2
   */
  g_object_class_install_property (gobject_class, PROP_DO_RETRANSMISSION,
      g_param_spec_boolean ("do-retransmission", "Do Retransmission",
          "Send retransmission events upstream when a packet is late",
          DEFAULT_DO_RETRANSMISSION,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstRtpJitterBuffer:rtx-delay:
   *
   * When a packet did not arrive at the expected time, wait this extra amount
   * of time before sending a retransmission event.
   *
   * When -1 is used, the max jitter will be used as extra delay.
   *
   * Since: 1.2
   */
  g_object_class_install_property (gobject_class, PROP_RTX_DELAY,
      g_param_spec_int ("rtx-delay", "RTX Delay",
          "Extra time in ms to wait before sending retransmission "
          "event (-1 automatic)", -1, G_MAXINT, DEFAULT_RTX_DELAY,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstRtpJitterBuffer:rtx-min-delay:
   *
   * When a packet did not arrive at the expected time, wait at least this extra amount
   * of time before sending a retransmission event.
   *
   * Since: 1.6
   */
  g_object_class_install_property (gobject_class, PROP_RTX_MIN_DELAY,
      g_param_spec_uint ("rtx-min-delay", "Minimum RTX Delay",
          "Minimum time in ms to wait before sending retransmission "
          "event", 0, G_MAXUINT, DEFAULT_RTX_MIN_DELAY,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstRtpJitterBuffer:rtx-delay-reorder:
   *
   * Assume that a retransmission event should be sent when we see
   * this much packet reordering.
   *
   * When -1 is used, the value will be estimated based on observed packet
   * reordering.
   *
   * Since: 1.2
   */
  g_object_class_install_property (gobject_class, PROP_RTX_DELAY_REORDER,
      g_param_spec_int ("rtx-delay-reorder", "RTX Delay Reorder",
          "Sending retransmission event when this much reordering (-1 automatic)",
          -1, G_MAXINT, DEFAULT_RTX_DELAY_REORDER,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstRtpJitterBuffer::rtx-retry-timeout:
   *
   * When no packet has been received after sending a retransmission event
   * for this time, retry sending a retransmission event.
   *
   * When -1 is used, the value will be estimated based on observed round
   * trip time.
   *
   * Since: 1.2
   */
  g_object_class_install_property (gobject_class, PROP_RTX_RETRY_TIMEOUT,
      g_param_spec_int ("rtx-retry-timeout", "RTX Retry Timeout",
          "Retry sending a transmission event after this timeout in "
          "ms (-1 automatic)", -1, G_MAXINT, DEFAULT_RTX_RETRY_TIMEOUT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstRtpJitterBuffer::rtx-min-retry-timeout:
   *
   * The minimum amount of time between retry timeouts. When
   * GstRtpJitterBuffer::rtx-retry-timeout is -1, this value ensures a
   * minimum interval between retry timeouts.
   *
   * When -1 is used, the value will be estimated based on the
   * packet spacing.
   *
   * Since: 1.6
   */
  g_object_class_install_property (gobject_class, PROP_RTX_MIN_RETRY_TIMEOUT,
      g_param_spec_int ("rtx-min-retry-timeout", "RTX Min Retry Timeout",
          "Minimum timeout between sending a transmission event in "
          "ms (-1 automatic)", -1, G_MAXINT, DEFAULT_RTX_MIN_RETRY_TIMEOUT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstRtpJitterBuffer:rtx-retry-period:
   *
   * The amount of time to try to get a retransmission.
   *
   * When -1 is used, the value will be estimated based on the jitterbuffer
   * latency and the observed round trip time.
   *
   * Since: 1.2
   */
  g_object_class_install_property (gobject_class, PROP_RTX_RETRY_PERIOD,
      g_param_spec_int ("rtx-retry-period", "RTX Retry Period",
          "Try to get a retransmission for this many ms "
          "(-1 automatic)", -1, G_MAXINT, DEFAULT_RTX_RETRY_PERIOD,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstRtpJitterBuffer:stats:
   *
   * Various jitterbuffer statistics. This property returns a GstStructure
   * with name application/x-rtp-jitterbuffer-stats with the following fields:
   *
   *  "rtx-count"         G_TYPE_UINT64 The number of retransmissions requested
   *  "rtx-success-count" G_TYPE_UINT64 The number of successful retransmissions
   *  "rtx-per-packet"    G_TYPE_DOUBLE Average number of RTX per packet
   *  "rtx-rtt"           G_TYPE_UINT64 Average round trip time per RTX
   *
   * Since: 1.4
   */
  g_object_class_install_property (gobject_class, PROP_STATS,
      g_param_spec_boxed ("stats", "Statistics",
          "Various statistics", GST_TYPE_STRUCTURE,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /**
   * GstRtpJitterBuffer::request-pt-map:
   * @buffer: the object which received the signal
   * @pt: the pt
   *
   * Request the payload type as #GstCaps for @pt.
   */
  gst_rtp_jitter_buffer_signals[SIGNAL_REQUEST_PT_MAP] =
      g_signal_new ("request-pt-map", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRtpJitterBufferClass,
          request_pt_map), NULL, NULL, g_cclosure_marshal_generic,
      GST_TYPE_CAPS, 1, G_TYPE_UINT);
  /**
   * GstRtpJitterBuffer::handle-sync:
   * @buffer: the object which received the signal
   * @struct: a GstStructure containing sync values.
   *
   * Be notified of new sync values.
   */
  gst_rtp_jitter_buffer_signals[SIGNAL_HANDLE_SYNC] =
      g_signal_new ("handle-sync", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRtpJitterBufferClass,
          handle_sync), NULL, NULL, g_cclosure_marshal_VOID__BOXED,
      G_TYPE_NONE, 1, GST_TYPE_STRUCTURE | G_SIGNAL_TYPE_STATIC_SCOPE);

  /**
   * GstRtpJitterBuffer::on-npt-stop:
   * @buffer: the object which received the signal
   *
   * Signal that the jitterbufer has pushed the RTP packet that corresponds to
   * the npt-stop position.
   */
  gst_rtp_jitter_buffer_signals[SIGNAL_ON_NPT_STOP] =
      g_signal_new ("on-npt-stop", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRtpJitterBufferClass,
          on_npt_stop), NULL, NULL, g_cclosure_marshal_VOID__VOID,
      G_TYPE_NONE, 0, G_TYPE_NONE);

  /**
   * GstRtpJitterBuffer::clear-pt-map:
   * @buffer: the object which received the signal
   *
   * Invalidate the clock-rate as obtained with the
   * #GstRtpJitterBuffer::request-pt-map signal.
   */
  gst_rtp_jitter_buffer_signals[SIGNAL_CLEAR_PT_MAP] =
      g_signal_new ("clear-pt-map", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (GstRtpJitterBufferClass, clear_pt_map), NULL, NULL,
      g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0, G_TYPE_NONE);

  /**
   * GstRtpJitterBuffer::set-active:
   * @buffer: the object which received the signal
   *
   * Start pushing out packets with the given base time. This signal is only
   * useful in buffering mode.
   *
   * Returns: the time of the last pushed packet.
   */
  gst_rtp_jitter_buffer_signals[SIGNAL_SET_ACTIVE] =
      g_signal_new ("set-active", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (GstRtpJitterBufferClass, set_active), NULL, NULL,
      g_cclosure_marshal_generic, G_TYPE_UINT64, 2, G_TYPE_BOOLEAN,
      G_TYPE_UINT64);

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_rtp_jitter_buffer_change_state);
  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_rtp_jitter_buffer_request_new_pad);
  gstelement_class->release_pad =
      GST_DEBUG_FUNCPTR (gst_rtp_jitter_buffer_release_pad);
  gstelement_class->provide_clock =
      GST_DEBUG_FUNCPTR (gst_rtp_jitter_buffer_provide_clock);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_rtp_jitter_buffer_src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_rtp_jitter_buffer_sink_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_rtp_jitter_buffer_sink_rtcp_template));

  gst_element_class_set_static_metadata (gstelement_class,
      "RTP packet jitter-buffer", "Filter/Network/RTP",
      "A buffer that deals with network jitter and other transmission faults",
      "Philippe Kalaf <philippe.kalaf@collabora.co.uk>, "
      "Wim Taymans <wim.taymans@gmail.com>");

  klass->clear_pt_map = GST_DEBUG_FUNCPTR (gst_rtp_jitter_buffer_clear_pt_map);
  klass->set_active = GST_DEBUG_FUNCPTR (gst_rtp_jitter_buffer_set_active);

  GST_DEBUG_CATEGORY_INIT
      (rtpjitterbuffer_debug, "rtpjitterbuffer", 0, "RTP Jitter Buffer");
}

static void
gst_rtp_jitter_buffer_init (GstRtpJitterBuffer * jitterbuffer)
{
  GstRtpJitterBufferPrivate *priv;

  priv = GST_RTP_JITTER_BUFFER_GET_PRIVATE (jitterbuffer);
  jitterbuffer->priv = priv;

  priv->latency_ms = DEFAULT_LATENCY_MS;
  priv->latency_ns = priv->latency_ms * GST_MSECOND;
  priv->drop_on_latency = DEFAULT_DROP_ON_LATENCY;
  priv->do_lost = DEFAULT_DO_LOST;
  priv->do_retransmission = DEFAULT_DO_RETRANSMISSION;
  priv->rtx_delay = DEFAULT_RTX_DELAY;
  priv->rtx_min_delay = DEFAULT_RTX_MIN_DELAY;
  priv->rtx_delay_reorder = DEFAULT_RTX_DELAY_REORDER;
  priv->rtx_retry_timeout = DEFAULT_RTX_RETRY_TIMEOUT;
  priv->rtx_min_retry_timeout = DEFAULT_RTX_MIN_RETRY_TIMEOUT;
  priv->rtx_retry_period = DEFAULT_RTX_RETRY_PERIOD;

  priv->last_dts = -1;
  priv->last_rtptime = -1;
  priv->avg_jitter = 0;
  priv->timers = g_array_new (FALSE, TRUE, sizeof (TimerData));
  priv->jbuf = rtp_jitter_buffer_new ();
  g_mutex_init (&priv->jbuf_lock);
  g_cond_init (&priv->jbuf_timer);
  g_cond_init (&priv->jbuf_event);
  g_cond_init (&priv->jbuf_query);

  /* reset skew detection initialy */
  rtp_jitter_buffer_reset_skew (priv->jbuf);
  rtp_jitter_buffer_set_delay (priv->jbuf, priv->latency_ns);
  rtp_jitter_buffer_set_buffering (priv->jbuf, FALSE);
  priv->active = TRUE;

  priv->srcpad =
      gst_pad_new_from_static_template (&gst_rtp_jitter_buffer_src_template,
      "src");

  gst_pad_set_activatemode_function (priv->srcpad,
      GST_DEBUG_FUNCPTR (gst_rtp_jitter_buffer_src_activate_mode));
  gst_pad_set_query_function (priv->srcpad,
      GST_DEBUG_FUNCPTR (gst_rtp_jitter_buffer_src_query));
  gst_pad_set_event_function (priv->srcpad,
      GST_DEBUG_FUNCPTR (gst_rtp_jitter_buffer_src_event));

  priv->sinkpad =
      gst_pad_new_from_static_template (&gst_rtp_jitter_buffer_sink_template,
      "sink");

  gst_pad_set_chain_function (priv->sinkpad,
      GST_DEBUG_FUNCPTR (gst_rtp_jitter_buffer_chain));
  gst_pad_set_event_function (priv->sinkpad,
      GST_DEBUG_FUNCPTR (gst_rtp_jitter_buffer_sink_event));
  gst_pad_set_query_function (priv->sinkpad,
      GST_DEBUG_FUNCPTR (gst_rtp_jitter_buffer_sink_query));

  gst_element_add_pad (GST_ELEMENT (jitterbuffer), priv->srcpad);
  gst_element_add_pad (GST_ELEMENT (jitterbuffer), priv->sinkpad);

  GST_OBJECT_FLAG_SET (jitterbuffer, GST_ELEMENT_FLAG_PROVIDE_CLOCK);
}

#define IS_DROPABLE(it) (((it)->type == ITEM_TYPE_BUFFER) || ((it)->type == ITEM_TYPE_LOST))

#define ITEM_TYPE_BUFFER        0
#define ITEM_TYPE_LOST          1
#define ITEM_TYPE_EVENT         2
#define ITEM_TYPE_QUERY         3

static RTPJitterBufferItem *
alloc_item (gpointer data, guint type, GstClockTime dts, GstClockTime pts,
    guint seqnum, guint count, guint rtptime)
{
  RTPJitterBufferItem *item;

  item = g_slice_new (RTPJitterBufferItem);
  item->data = data;
  item->next = NULL;
  item->prev = NULL;
  item->type = type;
  item->dts = dts;
  item->pts = pts;
  item->seqnum = seqnum;
  item->count = count;
  item->rtptime = rtptime;

  return item;
}

static void
free_item (RTPJitterBufferItem * item)
{
  if (item->data && item->type != ITEM_TYPE_QUERY)
    gst_mini_object_unref (item->data);
  g_slice_free (RTPJitterBufferItem, item);
}

static void
free_item_and_retain_events (RTPJitterBufferItem * item, gpointer user_data)
{
  GList **l = user_data;

  if (item->data && item->type == ITEM_TYPE_EVENT
      && GST_EVENT_IS_STICKY (item->data)) {
    *l = g_list_prepend (*l, item->data);
  } else if (item->data && item->type != ITEM_TYPE_QUERY) {
    gst_mini_object_unref (item->data);
  }
  g_slice_free (RTPJitterBufferItem, item);
}

static void
gst_rtp_jitter_buffer_finalize (GObject * object)
{
  GstRtpJitterBuffer *jitterbuffer;
  GstRtpJitterBufferPrivate *priv;

  jitterbuffer = GST_RTP_JITTER_BUFFER (object);
  priv = jitterbuffer->priv;

  g_array_free (priv->timers, TRUE);
  g_mutex_clear (&priv->jbuf_lock);
  g_cond_clear (&priv->jbuf_timer);
  g_cond_clear (&priv->jbuf_event);
  g_cond_clear (&priv->jbuf_query);

  rtp_jitter_buffer_flush (priv->jbuf, (GFunc) free_item, NULL);
  g_object_unref (priv->jbuf);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstIterator *
gst_rtp_jitter_buffer_iterate_internal_links (GstPad * pad, GstObject * parent)
{
  GstRtpJitterBuffer *jitterbuffer;
  GstPad *otherpad = NULL;
  GstIterator *it = NULL;
  GValue val = { 0, };

  jitterbuffer = GST_RTP_JITTER_BUFFER_CAST (parent);

  if (pad == jitterbuffer->priv->sinkpad) {
    otherpad = jitterbuffer->priv->srcpad;
  } else if (pad == jitterbuffer->priv->srcpad) {
    otherpad = jitterbuffer->priv->sinkpad;
  } else if (pad == jitterbuffer->priv->rtcpsinkpad) {
    it = gst_iterator_new_single (GST_TYPE_PAD, NULL);
  }

  if (it == NULL) {
    g_value_init (&val, GST_TYPE_PAD);
    g_value_set_object (&val, otherpad);
    it = gst_iterator_new_single (GST_TYPE_PAD, &val);
    g_value_unset (&val);
  }

  return it;
}

static GstPad *
create_rtcp_sink (GstRtpJitterBuffer * jitterbuffer)
{
  GstRtpJitterBufferPrivate *priv;

  priv = jitterbuffer->priv;

  GST_DEBUG_OBJECT (jitterbuffer, "creating RTCP sink pad");

  priv->rtcpsinkpad =
      gst_pad_new_from_static_template
      (&gst_rtp_jitter_buffer_sink_rtcp_template, "sink_rtcp");
  gst_pad_set_chain_function (priv->rtcpsinkpad,
      gst_rtp_jitter_buffer_chain_rtcp);
  gst_pad_set_event_function (priv->rtcpsinkpad,
      (GstPadEventFunction) gst_rtp_jitter_buffer_sink_rtcp_event);
  gst_pad_set_iterate_internal_links_function (priv->rtcpsinkpad,
      gst_rtp_jitter_buffer_iterate_internal_links);
  gst_pad_set_active (priv->rtcpsinkpad, TRUE);
  gst_element_add_pad (GST_ELEMENT_CAST (jitterbuffer), priv->rtcpsinkpad);

  return priv->rtcpsinkpad;
}

static void
remove_rtcp_sink (GstRtpJitterBuffer * jitterbuffer)
{
  GstRtpJitterBufferPrivate *priv;

  priv = jitterbuffer->priv;

  GST_DEBUG_OBJECT (jitterbuffer, "removing RTCP sink pad");

  gst_pad_set_active (priv->rtcpsinkpad, FALSE);

  gst_element_remove_pad (GST_ELEMENT_CAST (jitterbuffer), priv->rtcpsinkpad);
  priv->rtcpsinkpad = NULL;
}

static GstPad *
gst_rtp_jitter_buffer_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name, const GstCaps * filter)
{
  GstRtpJitterBuffer *jitterbuffer;
  GstElementClass *klass;
  GstPad *result;
  GstRtpJitterBufferPrivate *priv;

  g_return_val_if_fail (templ != NULL, NULL);
  g_return_val_if_fail (GST_IS_RTP_JITTER_BUFFER (element), NULL);

  jitterbuffer = GST_RTP_JITTER_BUFFER_CAST (element);
  priv = jitterbuffer->priv;
  klass = GST_ELEMENT_GET_CLASS (element);

  GST_DEBUG_OBJECT (element, "requesting pad %s", GST_STR_NULL (name));

  /* figure out the template */
  if (templ == gst_element_class_get_pad_template (klass, "sink_rtcp")) {
    if (priv->rtcpsinkpad != NULL)
      goto exists;

    result = create_rtcp_sink (jitterbuffer);
  } else
    goto wrong_template;

  return result;

  /* ERRORS */
wrong_template:
  {
    g_warning ("rtpjitterbuffer: this is not our template");
    return NULL;
  }
exists:
  {
    g_warning ("rtpjitterbuffer: pad already requested");
    return NULL;
  }
}

static void
gst_rtp_jitter_buffer_release_pad (GstElement * element, GstPad * pad)
{
  GstRtpJitterBuffer *jitterbuffer;
  GstRtpJitterBufferPrivate *priv;

  g_return_if_fail (GST_IS_RTP_JITTER_BUFFER (element));
  g_return_if_fail (GST_IS_PAD (pad));

  jitterbuffer = GST_RTP_JITTER_BUFFER_CAST (element);
  priv = jitterbuffer->priv;

  GST_DEBUG_OBJECT (element, "releasing pad %s:%s", GST_DEBUG_PAD_NAME (pad));

  if (priv->rtcpsinkpad == pad) {
    remove_rtcp_sink (jitterbuffer);
  } else
    goto wrong_pad;

  return;

  /* ERRORS */
wrong_pad:
  {
    g_warning ("gstjitterbuffer: asked to release an unknown pad");
    return;
  }
}

static GstClock *
gst_rtp_jitter_buffer_provide_clock (GstElement * element)
{
  return gst_system_clock_obtain ();
}

static void
gst_rtp_jitter_buffer_clear_pt_map (GstRtpJitterBuffer * jitterbuffer)
{
  GstRtpJitterBufferPrivate *priv;

  priv = jitterbuffer->priv;

  /* this will trigger a new pt-map request signal, FIXME, do something better. */

  JBUF_LOCK (priv);
  priv->clock_rate = -1;
  /* do not clear current content, but refresh state for new arrival */
  GST_DEBUG_OBJECT (jitterbuffer, "reset jitterbuffer");
  rtp_jitter_buffer_reset_skew (priv->jbuf);
  JBUF_UNLOCK (priv);
}

static GstClockTime
gst_rtp_jitter_buffer_set_active (GstRtpJitterBuffer * jbuf, gboolean active,
    guint64 offset)
{
  GstRtpJitterBufferPrivate *priv;
  GstClockTime last_out;
  RTPJitterBufferItem *item;

  priv = jbuf->priv;

  JBUF_LOCK (priv);
  GST_DEBUG_OBJECT (jbuf, "setting active %d with offset %" GST_TIME_FORMAT,
      active, GST_TIME_ARGS (offset));

  if (active != priv->active) {
    /* add the amount of time spent in paused to the output offset. All
     * outgoing buffers will have this offset applied to their timestamps in
     * order to make them arrive in time in the sink. */
    priv->out_offset = offset;
    GST_DEBUG_OBJECT (jbuf, "out offset %" GST_TIME_FORMAT,
        GST_TIME_ARGS (priv->out_offset));
    priv->active = active;
    JBUF_SIGNAL_EVENT (priv);
  }
  if (!active) {
    rtp_jitter_buffer_set_buffering (priv->jbuf, TRUE);
  }
  if ((item = rtp_jitter_buffer_peek (priv->jbuf))) {
    /* head buffer timestamp and offset gives our output time */
    last_out = item->dts + priv->ts_offset;
  } else {
    /* use last known time when the buffer is empty */
    last_out = priv->last_out_time;
  }
  JBUF_UNLOCK (priv);

  return last_out;
}

static GstCaps *
gst_rtp_jitter_buffer_getcaps (GstPad * pad, GstCaps * filter)
{
  GstRtpJitterBuffer *jitterbuffer;
  GstRtpJitterBufferPrivate *priv;
  GstPad *other;
  GstCaps *caps;
  GstCaps *templ;

  jitterbuffer = GST_RTP_JITTER_BUFFER (gst_pad_get_parent (pad));
  priv = jitterbuffer->priv;

  other = (pad == priv->srcpad ? priv->sinkpad : priv->srcpad);

  caps = gst_pad_peer_query_caps (other, filter);

  templ = gst_pad_get_pad_template_caps (pad);
  if (caps == NULL) {
    GST_DEBUG_OBJECT (jitterbuffer, "use template");
    caps = templ;
  } else {
    GstCaps *intersect;

    GST_DEBUG_OBJECT (jitterbuffer, "intersect with template");

    intersect = gst_caps_intersect (caps, templ);
    gst_caps_unref (caps);
    gst_caps_unref (templ);

    caps = intersect;
  }
  gst_object_unref (jitterbuffer);

  return caps;
}

/*
 * Must be called with JBUF_LOCK held
 */

static gboolean
gst_jitter_buffer_sink_parse_caps (GstRtpJitterBuffer * jitterbuffer,
    GstCaps * caps)
{
  GstRtpJitterBufferPrivate *priv;
  GstStructure *caps_struct;
  guint val;
  GstClockTime tval;

  priv = jitterbuffer->priv;

  /* first parse the caps */
  caps_struct = gst_caps_get_structure (caps, 0);

  GST_DEBUG_OBJECT (jitterbuffer, "got caps");

  /* we need a clock-rate to convert the rtp timestamps to GStreamer time and to
   * measure the amount of data in the buffer */
  if (!gst_structure_get_int (caps_struct, "clock-rate", &priv->clock_rate))
    goto error;

  if (priv->clock_rate <= 0)
    goto wrong_rate;

  GST_DEBUG_OBJECT (jitterbuffer, "got clock-rate %d", priv->clock_rate);

  rtp_jitter_buffer_set_clock_rate (priv->jbuf, priv->clock_rate);

  /* The clock base is the RTP timestamp corrsponding to the npt-start value. We
   * can use this to track the amount of time elapsed on the sender. */
  if (gst_structure_get_uint (caps_struct, "clock-base", &val))
    priv->clock_base = val;
  else
    priv->clock_base = -1;

  priv->ext_timestamp = priv->clock_base;

  GST_DEBUG_OBJECT (jitterbuffer, "got clock-base %" G_GINT64_FORMAT,
      priv->clock_base);

  if (gst_structure_get_uint (caps_struct, "seqnum-base", &val)) {
    /* first expected seqnum, only update when we didn't have a previous base. */
    if (priv->next_in_seqnum == -1)
      priv->next_in_seqnum = val;
    if (priv->next_seqnum == -1) {
      priv->next_seqnum = val;
      JBUF_SIGNAL_EVENT (priv);
    }
  }

  GST_DEBUG_OBJECT (jitterbuffer, "got seqnum-base %d", priv->next_in_seqnum);

  /* the start and stop times. The seqnum-base corresponds to the start time. We
   * will keep track of the seqnums on the output and when we reach the one
   * corresponding to npt-stop, we emit the npt-stop-reached signal */
  if (gst_structure_get_clock_time (caps_struct, "npt-start", &tval))
    priv->npt_start = tval;
  else
    priv->npt_start = 0;

  if (gst_structure_get_clock_time (caps_struct, "npt-stop", &tval))
    priv->npt_stop = tval;
  else
    priv->npt_stop = -1;

  GST_DEBUG_OBJECT (jitterbuffer,
      "npt start/stop: %" GST_TIME_FORMAT "-%" GST_TIME_FORMAT,
      GST_TIME_ARGS (priv->npt_start), GST_TIME_ARGS (priv->npt_stop));

  return TRUE;

  /* ERRORS */
error:
  {
    GST_DEBUG_OBJECT (jitterbuffer, "No clock-rate in caps!");
    return FALSE;
  }
wrong_rate:
  {
    GST_DEBUG_OBJECT (jitterbuffer, "Invalid clock-rate %d", priv->clock_rate);
    return FALSE;
  }
}

static void
gst_rtp_jitter_buffer_flush_start (GstRtpJitterBuffer * jitterbuffer)
{
  GstRtpJitterBufferPrivate *priv;

  priv = jitterbuffer->priv;

  JBUF_LOCK (priv);
  /* mark ourselves as flushing */
  priv->srcresult = GST_FLOW_FLUSHING;
  GST_DEBUG_OBJECT (jitterbuffer, "Disabling pop on queue");
  /* this unblocks any waiting pops on the src pad task */
  JBUF_SIGNAL_EVENT (priv);
  JBUF_SIGNAL_QUERY (priv, FALSE);
  JBUF_UNLOCK (priv);
}

static void
gst_rtp_jitter_buffer_flush_stop (GstRtpJitterBuffer * jitterbuffer)
{
  GstRtpJitterBufferPrivate *priv;

  priv = jitterbuffer->priv;

  JBUF_LOCK (priv);
  GST_DEBUG_OBJECT (jitterbuffer, "Enabling pop on queue");
  /* Mark as non flushing */
  priv->srcresult = GST_FLOW_OK;
  gst_segment_init (&priv->segment, GST_FORMAT_TIME);
  priv->last_popped_seqnum = -1;
  priv->last_out_time = -1;
  priv->next_seqnum = -1;
  priv->ips_rtptime = -1;
  priv->ips_dts = GST_CLOCK_TIME_NONE;
  priv->packet_spacing = 0;
  priv->next_in_seqnum = -1;
  priv->clock_rate = -1;
  priv->last_pt = -1;
  priv->eos = FALSE;
  priv->estimated_eos = -1;
  priv->last_elapsed = 0;
  priv->ext_timestamp = -1;
  priv->avg_jitter = 0;
  priv->last_dts = -1;
  priv->last_rtptime = -1;
  GST_DEBUG_OBJECT (jitterbuffer, "flush and reset jitterbuffer");
  rtp_jitter_buffer_flush (priv->jbuf, (GFunc) free_item, NULL);
  rtp_jitter_buffer_disable_buffering (priv->jbuf, FALSE);
  rtp_jitter_buffer_reset_skew (priv->jbuf);
  remove_all_timers (jitterbuffer);
  JBUF_UNLOCK (priv);
}

static gboolean
gst_rtp_jitter_buffer_src_activate_mode (GstPad * pad, GstObject * parent,
    GstPadMode mode, gboolean active)
{
  gboolean result;
  GstRtpJitterBuffer *jitterbuffer = NULL;

  jitterbuffer = GST_RTP_JITTER_BUFFER (parent);

  switch (mode) {
    case GST_PAD_MODE_PUSH:
      if (active) {
        /* allow data processing */
        gst_rtp_jitter_buffer_flush_stop (jitterbuffer);

        /* start pushing out buffers */
        GST_DEBUG_OBJECT (jitterbuffer, "Starting task on srcpad");
        result = gst_pad_start_task (jitterbuffer->priv->srcpad,
            (GstTaskFunction) gst_rtp_jitter_buffer_loop, jitterbuffer, NULL);
      } else {
        /* make sure all data processing stops ASAP */
        gst_rtp_jitter_buffer_flush_start (jitterbuffer);

        /* NOTE this will hardlock if the state change is called from the src pad
         * task thread because we will _join() the thread. */
        GST_DEBUG_OBJECT (jitterbuffer, "Stopping task on srcpad");
        result = gst_pad_stop_task (pad);
      }
      break;
    default:
      result = FALSE;
      break;
  }
  return result;
}

static GstStateChangeReturn
gst_rtp_jitter_buffer_change_state (GstElement * element,
    GstStateChange transition)
{
  GstRtpJitterBuffer *jitterbuffer;
  GstRtpJitterBufferPrivate *priv;
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  jitterbuffer = GST_RTP_JITTER_BUFFER (element);
  priv = jitterbuffer->priv;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      JBUF_LOCK (priv);
      /* reset negotiated values */
      priv->clock_rate = -1;
      priv->clock_base = -1;
      priv->peer_latency = 0;
      priv->last_pt = -1;
      /* block until we go to PLAYING */
      priv->blocked = TRUE;
      priv->timer_running = TRUE;
      priv->timer_thread =
          g_thread_new ("timer", (GThreadFunc) wait_next_timeout, jitterbuffer);
      JBUF_UNLOCK (priv);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      JBUF_LOCK (priv);
      /* unblock to allow streaming in PLAYING */
      priv->blocked = FALSE;
      JBUF_SIGNAL_EVENT (priv);
      JBUF_SIGNAL_TIMER (priv);
      JBUF_UNLOCK (priv);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      /* we are a live element because we sync to the clock, which we can only
       * do in the PLAYING state */
      if (ret != GST_STATE_CHANGE_FAILURE)
        ret = GST_STATE_CHANGE_NO_PREROLL;
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      JBUF_LOCK (priv);
      /* block to stop streaming when PAUSED */
      priv->blocked = TRUE;
      unschedule_current_timer (jitterbuffer);
      JBUF_UNLOCK (priv);
      if (ret != GST_STATE_CHANGE_FAILURE)
        ret = GST_STATE_CHANGE_NO_PREROLL;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      JBUF_LOCK (priv);
      gst_buffer_replace (&priv->last_sr, NULL);
      priv->timer_running = FALSE;
      unschedule_current_timer (jitterbuffer);
      JBUF_SIGNAL_TIMER (priv);
      JBUF_SIGNAL_QUERY (priv, FALSE);
      JBUF_UNLOCK (priv);
      g_thread_join (priv->timer_thread);
      priv->timer_thread = NULL;
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}

static gboolean
gst_rtp_jitter_buffer_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  gboolean ret = TRUE;
  GstRtpJitterBuffer *jitterbuffer;
  GstRtpJitterBufferPrivate *priv;

  jitterbuffer = GST_RTP_JITTER_BUFFER_CAST (parent);
  priv = jitterbuffer->priv;

  GST_DEBUG_OBJECT (jitterbuffer, "received %s", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_LATENCY:
    {
      GstClockTime latency;

      gst_event_parse_latency (event, &latency);

      GST_DEBUG_OBJECT (jitterbuffer,
          "configuring latency of %" GST_TIME_FORMAT, GST_TIME_ARGS (latency));

      JBUF_LOCK (priv);
      /* adjust the overall buffer delay to the total pipeline latency in
       * buffering mode because if downstream consumes too fast (because of
       * large latency or queues, we would start rebuffering again. */
      if (rtp_jitter_buffer_get_mode (priv->jbuf) ==
          RTP_JITTER_BUFFER_MODE_BUFFER) {
        rtp_jitter_buffer_set_delay (priv->jbuf, latency);
      }
      JBUF_UNLOCK (priv);

      ret = gst_pad_push_event (priv->sinkpad, event);
      break;
    }
    default:
      ret = gst_pad_push_event (priv->sinkpad, event);
      break;
  }

  return ret;
}

/* handles and stores the event in the jitterbuffer, must be called with
 * LOCK */
static gboolean
queue_event (GstRtpJitterBuffer * jitterbuffer, GstEvent * event)
{
  GstRtpJitterBufferPrivate *priv = jitterbuffer->priv;
  RTPJitterBufferItem *item;
  gboolean head;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      gst_jitter_buffer_sink_parse_caps (jitterbuffer, caps);
      break;
    }
    case GST_EVENT_SEGMENT:
      gst_event_copy_segment (event, &priv->segment);

      /* we need time for now */
      if (priv->segment.format != GST_FORMAT_TIME)
        goto newseg_wrong_format;

      GST_DEBUG_OBJECT (jitterbuffer,
          "segment:  %" GST_SEGMENT_FORMAT, &priv->segment);
      break;
    case GST_EVENT_EOS:
      priv->eos = TRUE;
      rtp_jitter_buffer_disable_buffering (priv->jbuf, TRUE);
      break;
    default:
      break;
  }


  GST_DEBUG_OBJECT (jitterbuffer, "adding event");
  item = alloc_item (event, ITEM_TYPE_EVENT, -1, -1, -1, 0, -1);
  rtp_jitter_buffer_insert (priv->jbuf, item, &head, NULL);
  if (head)
    JBUF_SIGNAL_EVENT (priv);

  return TRUE;

  /* ERRORS */
newseg_wrong_format:
  {
    GST_DEBUG_OBJECT (jitterbuffer, "received non TIME newsegment");
    gst_event_unref (event);
    return FALSE;
  }
}

static gboolean
gst_rtp_jitter_buffer_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  gboolean ret = TRUE;
  GstRtpJitterBuffer *jitterbuffer;
  GstRtpJitterBufferPrivate *priv;

  jitterbuffer = GST_RTP_JITTER_BUFFER (parent);
  priv = jitterbuffer->priv;

  GST_DEBUG_OBJECT (jitterbuffer, "received %s", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      ret = gst_pad_push_event (priv->srcpad, event);
      gst_rtp_jitter_buffer_flush_start (jitterbuffer);
      /* wait for the loop to go into PAUSED */
      gst_pad_pause_task (priv->srcpad);
      break;
    case GST_EVENT_FLUSH_STOP:
      ret = gst_pad_push_event (priv->srcpad, event);
      ret =
          gst_rtp_jitter_buffer_src_activate_mode (priv->srcpad, parent,
          GST_PAD_MODE_PUSH, TRUE);
      break;
    default:
      if (GST_EVENT_IS_SERIALIZED (event)) {
        /* serialized events go in the queue */
        JBUF_LOCK (priv);
        if (priv->srcresult != GST_FLOW_OK) {
          /* Errors in sticky event pushing are no problem and ignored here
           * as they will cause more meaningful errors during data flow.
           * For EOS events, that are not followed by data flow, we still
           * return FALSE here though.
           */
          if (!GST_EVENT_IS_STICKY (event) ||
              GST_EVENT_TYPE (event) == GST_EVENT_EOS)
            goto out_flow_error;
        }
        /* refuse more events on EOS */
        if (priv->eos)
          goto out_eos;
        ret = queue_event (jitterbuffer, event);
        JBUF_UNLOCK (priv);
      } else {
        /* non-serialized events are forwarded downstream immediately */
        ret = gst_pad_push_event (priv->srcpad, event);
      }
      break;
  }
  return ret;

  /* ERRORS */
out_flow_error:
  {
    GST_DEBUG_OBJECT (jitterbuffer,
        "refusing event, we have a downstream flow error: %s",
        gst_flow_get_name (priv->srcresult));
    JBUF_UNLOCK (priv);
    gst_event_unref (event);
    return FALSE;
  }
out_eos:
  {
    GST_DEBUG_OBJECT (jitterbuffer, "refusing event, we are EOS");
    JBUF_UNLOCK (priv);
    gst_event_unref (event);
    return FALSE;
  }
}

static gboolean
gst_rtp_jitter_buffer_sink_rtcp_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  gboolean ret = TRUE;
  GstRtpJitterBuffer *jitterbuffer;

  jitterbuffer = GST_RTP_JITTER_BUFFER (parent);

  GST_DEBUG_OBJECT (jitterbuffer, "received %s", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      gst_event_unref (event);
      break;
    case GST_EVENT_FLUSH_STOP:
      gst_event_unref (event);
      break;
    default:
      ret = gst_pad_event_default (pad, parent, event);
      break;
  }

  return ret;
}

/*
 * Must be called with JBUF_LOCK held, will release the LOCK when emiting the
 * signal. The function returns GST_FLOW_ERROR when a parsing error happened and
 * GST_FLOW_FLUSHING when the element is shutting down. On success
 * GST_FLOW_OK is returned.
 */
static GstFlowReturn
gst_rtp_jitter_buffer_get_clock_rate (GstRtpJitterBuffer * jitterbuffer,
    guint8 pt)
{
  GValue ret = { 0 };
  GValue args[2] = { {0}, {0} };
  GstCaps *caps;
  gboolean res;

  g_value_init (&args[0], GST_TYPE_ELEMENT);
  g_value_set_object (&args[0], jitterbuffer);
  g_value_init (&args[1], G_TYPE_UINT);
  g_value_set_uint (&args[1], pt);

  g_value_init (&ret, GST_TYPE_CAPS);
  g_value_set_boxed (&ret, NULL);

  JBUF_UNLOCK (jitterbuffer->priv);
  g_signal_emitv (args, gst_rtp_jitter_buffer_signals[SIGNAL_REQUEST_PT_MAP], 0,
      &ret);
  JBUF_LOCK_CHECK (jitterbuffer->priv, out_flushing);

  g_value_unset (&args[0]);
  g_value_unset (&args[1]);
  caps = (GstCaps *) g_value_dup_boxed (&ret);
  g_value_unset (&ret);
  if (!caps)
    goto no_caps;

  res = gst_jitter_buffer_sink_parse_caps (jitterbuffer, caps);
  gst_caps_unref (caps);

  if (G_UNLIKELY (!res))
    goto parse_failed;

  return GST_FLOW_OK;

  /* ERRORS */
no_caps:
  {
    GST_DEBUG_OBJECT (jitterbuffer, "could not get caps");
    return GST_FLOW_ERROR;
  }
out_flushing:
  {
    GST_DEBUG_OBJECT (jitterbuffer, "we are flushing");
    return GST_FLOW_FLUSHING;
  }
parse_failed:
  {
    GST_DEBUG_OBJECT (jitterbuffer, "parse failed");
    return GST_FLOW_ERROR;
  }
}

/* call with jbuf lock held */
static GstMessage *
check_buffering_percent (GstRtpJitterBuffer * jitterbuffer, gint percent)
{
  GstRtpJitterBufferPrivate *priv = jitterbuffer->priv;
  GstMessage *message = NULL;

  if (percent == -1)
    return NULL;

  /* Post a buffering message */
  if (priv->last_percent != percent) {
    priv->last_percent = percent;
    message =
        gst_message_new_buffering (GST_OBJECT_CAST (jitterbuffer), percent);
    gst_message_set_buffering_stats (message, GST_BUFFERING_LIVE, -1, -1, -1);
  }

  return message;
}

static GstClockTime
apply_offset (GstRtpJitterBuffer * jitterbuffer, GstClockTime timestamp)
{
  GstRtpJitterBufferPrivate *priv;

  priv = jitterbuffer->priv;

  if (timestamp == -1)
    return -1;

  /* apply the timestamp offset, this is used for inter stream sync */
  timestamp += priv->ts_offset;
  /* add the offset, this is used when buffering */
  timestamp += priv->out_offset;

  return timestamp;
}

static TimerData *
find_timer (GstRtpJitterBuffer * jitterbuffer, TimerType type, guint16 seqnum)
{
  GstRtpJitterBufferPrivate *priv = jitterbuffer->priv;
  TimerData *timer = NULL;
  gint i, len;

  len = priv->timers->len;
  for (i = 0; i < len; i++) {
    TimerData *test = &g_array_index (priv->timers, TimerData, i);
    if (test->seqnum == seqnum && test->type == type) {
      timer = test;
      break;
    }
  }
  return timer;
}

static void
unschedule_current_timer (GstRtpJitterBuffer * jitterbuffer)
{
  GstRtpJitterBufferPrivate *priv = jitterbuffer->priv;

  if (priv->clock_id) {
    GST_DEBUG_OBJECT (jitterbuffer, "unschedule current timer");
    gst_clock_id_unschedule (priv->clock_id);
    priv->clock_id = NULL;
  }
}

static GstClockTime
get_timeout (GstRtpJitterBuffer * jitterbuffer, TimerData * timer)
{
  GstRtpJitterBufferPrivate *priv = jitterbuffer->priv;
  GstClockTime test_timeout;

  if ((test_timeout = timer->timeout) == -1)
    return -1;

  if (timer->type != TIMER_TYPE_EXPECTED) {
    /* add our latency and offset to get output times. */
    test_timeout = apply_offset (jitterbuffer, test_timeout);
    test_timeout += priv->latency_ns;
  }
  return test_timeout;
}

static void
recalculate_timer (GstRtpJitterBuffer * jitterbuffer, TimerData * timer)
{
  GstRtpJitterBufferPrivate *priv = jitterbuffer->priv;

  if (priv->clock_id) {
    GstClockTime timeout = get_timeout (jitterbuffer, timer);

    GST_DEBUG ("%" GST_TIME_FORMAT " <> %" GST_TIME_FORMAT,
        GST_TIME_ARGS (timeout), GST_TIME_ARGS (priv->timer_timeout));

    if (timeout == -1 || timeout < priv->timer_timeout)
      unschedule_current_timer (jitterbuffer);
  }
}

static TimerData *
add_timer (GstRtpJitterBuffer * jitterbuffer, TimerType type,
    guint16 seqnum, guint num, GstClockTime timeout, GstClockTime delay,
    GstClockTime duration)
{
  GstRtpJitterBufferPrivate *priv = jitterbuffer->priv;
  TimerData *timer;
  gint len;

  GST_DEBUG_OBJECT (jitterbuffer,
      "add timer %d for seqnum %d to %" GST_TIME_FORMAT ", delay %"
      GST_TIME_FORMAT, type, seqnum, GST_TIME_ARGS (timeout),
      GST_TIME_ARGS (delay));

  len = priv->timers->len;
  g_array_set_size (priv->timers, len + 1);
  timer = &g_array_index (priv->timers, TimerData, len);
  timer->idx = len;
  timer->type = type;
  timer->seqnum = seqnum;
  timer->num = num;
  timer->timeout = timeout + delay;
  timer->duration = duration;
  if (type == TIMER_TYPE_EXPECTED) {
    timer->rtx_base = timeout;
    timer->rtx_delay = delay;
    timer->rtx_retry = 0;
  }
  timer->num_rtx_retry = 0;
  recalculate_timer (jitterbuffer, timer);
  JBUF_SIGNAL_TIMER (priv);

  return timer;
}

static void
reschedule_timer (GstRtpJitterBuffer * jitterbuffer, TimerData * timer,
    guint16 seqnum, GstClockTime timeout, GstClockTime delay, gboolean reset)
{
  GstRtpJitterBufferPrivate *priv = jitterbuffer->priv;
  gboolean seqchange, timechange;
  guint16 oldseq;

  seqchange = timer->seqnum != seqnum;
  timechange = timer->timeout != timeout;

  if (!seqchange && !timechange)
    return;

  oldseq = timer->seqnum;

  GST_DEBUG_OBJECT (jitterbuffer,
      "replace timer for seqnum %d->%d to %" GST_TIME_FORMAT,
      oldseq, seqnum, GST_TIME_ARGS (timeout + delay));

  timer->timeout = timeout + delay;
  timer->seqnum = seqnum;
  if (reset) {
    timer->rtx_base = timeout;
    timer->rtx_delay = delay;
    timer->rtx_retry = 0;
  }
  if (seqchange)
    timer->num_rtx_retry = 0;

  if (priv->clock_id) {
    /* we changed the seqnum and there is a timer currently waiting with this
     * seqnum, unschedule it */
    if (seqchange && priv->timer_seqnum == oldseq)
      unschedule_current_timer (jitterbuffer);
    /* we changed the time, check if it is earlier than what we are waiting
     * for and unschedule if so */
    else if (timechange)
      recalculate_timer (jitterbuffer, timer);
  }
}

static TimerData *
set_timer (GstRtpJitterBuffer * jitterbuffer, TimerType type,
    guint16 seqnum, GstClockTime timeout)
{
  TimerData *timer;

  /* find the seqnum timer */
  timer = find_timer (jitterbuffer, type, seqnum);
  if (timer == NULL) {
    timer = add_timer (jitterbuffer, type, seqnum, 0, timeout, 0, -1);
  } else {
    reschedule_timer (jitterbuffer, timer, seqnum, timeout, 0, FALSE);
  }
  return timer;
}

static void
remove_timer (GstRtpJitterBuffer * jitterbuffer, TimerData * timer)
{
  GstRtpJitterBufferPrivate *priv = jitterbuffer->priv;
  guint idx;

  if (priv->clock_id && priv->timer_seqnum == timer->seqnum)
    unschedule_current_timer (jitterbuffer);

  idx = timer->idx;
  GST_DEBUG_OBJECT (jitterbuffer, "removed index %d", idx);
  g_array_remove_index_fast (priv->timers, idx);
  timer->idx = idx;
}

static void
remove_all_timers (GstRtpJitterBuffer * jitterbuffer)
{
  GstRtpJitterBufferPrivate *priv = jitterbuffer->priv;
  GST_DEBUG_OBJECT (jitterbuffer, "removed all timers");
  g_array_set_size (priv->timers, 0);
  unschedule_current_timer (jitterbuffer);
}

/* get the extra delay to wait before sending RTX */
static GstClockTime
get_rtx_delay (GstRtpJitterBufferPrivate * priv)
{
  GstClockTime delay;

  if (priv->rtx_delay == -1) {
    if (priv->avg_jitter == 0)
      delay = DEFAULT_AUTO_RTX_DELAY;
    else
      /* jitter is in nanoseconds, 2x jitter is a good margin */
      delay = priv->avg_jitter * 2;
  } else {
    delay = priv->rtx_delay * GST_MSECOND;
  }
  if (priv->rtx_min_delay > 0)
    delay = MAX (delay, priv->rtx_min_delay * GST_MSECOND);

  return delay;
}

/* we just received a packet with seqnum and dts.
 *
 * First check for old seqnum that we are still expecting. If the gap with the
 * current seqnum is too big, unschedule the timeouts.
 *
 * If we have a valid packet spacing estimate we can set a timer for when we
 * should receive the next packet.
 * If we don't have a valid estimate, we remove any timer we might have
 * had for this packet.
 */
static void
update_timers (GstRtpJitterBuffer * jitterbuffer, guint16 seqnum,
    GstClockTime dts, gboolean do_next_seqnum)
{
  GstRtpJitterBufferPrivate *priv = jitterbuffer->priv;
  TimerData *timer = NULL;
  gint i, len;

  /* go through all timers and unschedule the ones with a large gap, also find
   * the timer for the seqnum */
  len = priv->timers->len;
  for (i = 0; i < len; i++) {
    TimerData *test = &g_array_index (priv->timers, TimerData, i);
    gint gap;

    gap = gst_rtp_buffer_compare_seqnum (test->seqnum, seqnum);

    GST_DEBUG_OBJECT (jitterbuffer, "%d, %d, #%d<->#%d gap %d", i,
        test->type, test->seqnum, seqnum, gap);

    if (gap == 0) {
      GST_DEBUG ("found timer for current seqnum");
      /* the timer for the current seqnum */
      timer = test;
      /* when no retransmission, we can stop now, we only need to find the
       * timer for the current seqnum */
      if (!priv->do_retransmission)
        break;
    } else if (gap > priv->rtx_delay_reorder) {
      /* max gap, we exceeded the max reorder distance and we don't expect the
       * missing packet to be this reordered */
      if (test->num_rtx_retry == 0 && test->type == TIMER_TYPE_EXPECTED)
        reschedule_timer (jitterbuffer, test, test->seqnum, -1, 0, FALSE);
    }
  }

  do_next_seqnum = do_next_seqnum && priv->packet_spacing > 0
      && priv->do_retransmission;

  if (timer && timer->type != TIMER_TYPE_DEADLINE) {
    if (timer->num_rtx_retry > 0) {
      GstClockTime rtx_last, delay;

      /* we scheduled a retry for this packet and now we have it */
      priv->num_rtx_success++;
      /* all the previous retry attempts failed */
      priv->num_rtx_failed += timer->num_rtx_retry - 1;
      /* number of retries before receiving the packet */
      if (priv->avg_rtx_num == 0.0)
        priv->avg_rtx_num = timer->num_rtx_retry;
      else
        priv->avg_rtx_num = (timer->num_rtx_retry + 7 * priv->avg_rtx_num) / 8;
      /* calculate the delay between retransmission request and receiving this
       * packet, start with when we scheduled this timeout last */
      rtx_last = timer->rtx_last;
      if (dts != GST_CLOCK_TIME_NONE && dts > rtx_last) {
        /* we have a valid delay if this packet arrived after we scheduled the
         * request */
        delay = dts - rtx_last;
        if (priv->avg_rtx_rtt == 0)
          priv->avg_rtx_rtt = delay;
        else
          priv->avg_rtx_rtt = (delay + 7 * priv->avg_rtx_rtt) / 8;
      } else
        delay = 0;

      GST_LOG_OBJECT (jitterbuffer,
          "RTX success %" G_GUINT64_FORMAT ", failed %" G_GUINT64_FORMAT
          ", requests %" G_GUINT64_FORMAT ", dups %" G_GUINT64_FORMAT
          ", avg-num %g, delay %" GST_TIME_FORMAT ", avg-rtt %" GST_TIME_FORMAT,
          priv->num_rtx_success, priv->num_rtx_failed, priv->num_rtx_requests,
          priv->num_duplicates, priv->avg_rtx_num, GST_TIME_ARGS (delay),
          GST_TIME_ARGS (priv->avg_rtx_rtt));

      /* don't try to estimate the next seqnum because this is a retransmitted
       * packet and it probably did not arrive with the expected packet
       * spacing. */
      do_next_seqnum = FALSE;
    }
  }

  if (do_next_seqnum) {
    GstClockTime expected, delay;

    /* calculate expected arrival time of the next seqnum */
    expected = dts + priv->packet_spacing;

    delay = get_rtx_delay (priv);

    /* and update/install timer for next seqnum */
    if (timer)
      reschedule_timer (jitterbuffer, timer, priv->next_in_seqnum, expected,
          delay, TRUE);
    else
      add_timer (jitterbuffer, TIMER_TYPE_EXPECTED, priv->next_in_seqnum, 0,
          expected, delay, priv->packet_spacing);
  } else if (timer && timer->type != TIMER_TYPE_DEADLINE) {
    /* if we had a timer, remove it, we don't know when to expect the next
     * packet. */
    remove_timer (jitterbuffer, timer);
  }
}

static void
calculate_packet_spacing (GstRtpJitterBuffer * jitterbuffer, guint32 rtptime,
    GstClockTime dts)
{
  GstRtpJitterBufferPrivate *priv = jitterbuffer->priv;

  /* we need consecutive seqnums with a different
   * rtptime to estimate the packet spacing. */
  if (priv->ips_rtptime != rtptime) {
    /* rtptime changed, check dts diff */
    if (priv->ips_dts != -1 && dts != -1 && dts > priv->ips_dts) {
      priv->packet_spacing = dts - priv->ips_dts;
      GST_DEBUG_OBJECT (jitterbuffer,
          "new packet spacing %" GST_TIME_FORMAT,
          GST_TIME_ARGS (priv->packet_spacing));
    }
    priv->ips_rtptime = rtptime;
    priv->ips_dts = dts;
  }
}

static void
calculate_expected (GstRtpJitterBuffer * jitterbuffer, guint32 expected,
    guint16 seqnum, GstClockTime dts, gint gap)
{
  GstRtpJitterBufferPrivate *priv = jitterbuffer->priv;
  GstClockTime total_duration, duration, expected_dts;
  TimerType type;

  GST_DEBUG_OBJECT (jitterbuffer,
      "dts %" GST_TIME_FORMAT ", last %" GST_TIME_FORMAT,
      GST_TIME_ARGS (dts), GST_TIME_ARGS (priv->last_in_dts));

  /* the total duration spanned by the missing packets */
  if (dts >= priv->last_in_dts)
    total_duration = dts - priv->last_in_dts;
  else
    total_duration = 0;

  /* interpolate between the current time and the last time based on
   * number of packets we are missing, this is the estimated duration
   * for the missing packet based on equidistant packet spacing. */
  duration = total_duration / (gap + 1);

  GST_DEBUG_OBJECT (jitterbuffer, "duration %" GST_TIME_FORMAT,
      GST_TIME_ARGS (duration));

  if (total_duration > priv->latency_ns) {
    GstClockTime gap_time;
    guint lost_packets;

    gap_time = total_duration - priv->latency_ns;

    if (duration > 0) {
      lost_packets = gap_time / duration;
      gap_time = lost_packets * duration;
    } else {
      lost_packets = gap;
    }

    /* too many lost packets, some of the missing packets are already
     * too late and we can generate lost packet events for them. */
    GST_DEBUG_OBJECT (jitterbuffer, "too many lost packets %" GST_TIME_FORMAT
        " > %" GST_TIME_FORMAT ", consider %u lost",
        GST_TIME_ARGS (total_duration), GST_TIME_ARGS (priv->latency_ns),
        lost_packets);

    /* this timer will fire immediately and the lost event will be pushed from
     * the timer thread */
    add_timer (jitterbuffer, TIMER_TYPE_LOST, expected, lost_packets,
        priv->last_in_dts + duration, 0, gap_time);

    expected += lost_packets;
    priv->last_in_dts += gap_time;
  }

  expected_dts = priv->last_in_dts + duration;

  if (priv->do_retransmission) {
    TimerData *timer;

    type = TIMER_TYPE_EXPECTED;
    /* if we had a timer for the first missing packet, update it. */
    if ((timer = find_timer (jitterbuffer, type, expected))) {
      GstClockTime timeout = timer->timeout;

      timer->duration = duration;
      if (timeout > expected_dts) {
        GstClockTime delay = timeout - expected_dts - timer->rtx_retry;
        reschedule_timer (jitterbuffer, timer, timer->seqnum, expected_dts,
            delay, TRUE);
      }
      expected++;
      expected_dts += duration;
    }
  } else {
    type = TIMER_TYPE_LOST;
  }

  while (gst_rtp_buffer_compare_seqnum (expected, seqnum) > 0) {
    add_timer (jitterbuffer, type, expected, 0, expected_dts, 0, duration);
    expected_dts += duration;
    expected++;
  }
}

static void
calculate_jitter (GstRtpJitterBuffer * jitterbuffer, GstClockTime dts,
    guint rtptime)
{
  gint32 rtpdiff;
  GstClockTimeDiff dtsdiff, rtpdiffns, diff;
  GstRtpJitterBufferPrivate *priv;

  priv = jitterbuffer->priv;

  if (G_UNLIKELY (dts == GST_CLOCK_TIME_NONE) || priv->clock_rate <= 0)
    goto no_time;

  if (priv->last_dts != -1)
    dtsdiff = dts - priv->last_dts;
  else
    dtsdiff = 0;

  if (priv->last_rtptime != -1)
    rtpdiff = rtptime - (guint32) priv->last_rtptime;
  else
    rtpdiff = 0;

  priv->last_dts = dts;
  priv->last_rtptime = rtptime;

  if (rtpdiff > 0)
    rtpdiffns =
        gst_util_uint64_scale_int (rtpdiff, GST_SECOND, priv->clock_rate);
  else
    rtpdiffns =
        -gst_util_uint64_scale_int (-rtpdiff, GST_SECOND, priv->clock_rate);

  diff = ABS (dtsdiff - rtpdiffns);

  /* jitter is stored in nanoseconds */
  priv->avg_jitter = (diff + (15 * priv->avg_jitter)) >> 4;

  GST_LOG_OBJECT (jitterbuffer,
      "dtsdiff %" GST_TIME_FORMAT " rtptime %" GST_TIME_FORMAT
      ", clock-rate %d, diff %" GST_TIME_FORMAT ", jitter: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (dtsdiff), GST_TIME_ARGS (rtpdiffns), priv->clock_rate,
      GST_TIME_ARGS (diff), GST_TIME_ARGS (priv->avg_jitter));

  return;

  /* ERRORS */
no_time:
  {
    GST_DEBUG_OBJECT (jitterbuffer,
        "no dts or no clock-rate, can't calculate jitter");
    return;
  }
}

static GstFlowReturn
gst_rtp_jitter_buffer_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer)
{
  GstRtpJitterBuffer *jitterbuffer;
  GstRtpJitterBufferPrivate *priv;
  guint16 seqnum;
  guint32 expected, rtptime;
  GstFlowReturn ret = GST_FLOW_OK;
  GstClockTime dts, pts;
  guint64 latency_ts;
  gboolean head;
  gint percent = -1;
  guint8 pt;
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  gboolean do_next_seqnum = FALSE;
  RTPJitterBufferItem *item;
  GstMessage *msg = NULL;

  jitterbuffer = GST_RTP_JITTER_BUFFER_CAST (parent);

  priv = jitterbuffer->priv;

  if (G_UNLIKELY (!gst_rtp_buffer_map (buffer, GST_MAP_READ, &rtp)))
    goto invalid_buffer;

  pt = gst_rtp_buffer_get_payload_type (&rtp);
  seqnum = gst_rtp_buffer_get_seq (&rtp);
  rtptime = gst_rtp_buffer_get_timestamp (&rtp);
  gst_rtp_buffer_unmap (&rtp);

  /* make sure we have PTS and DTS set */
  pts = GST_BUFFER_PTS (buffer);
  dts = GST_BUFFER_DTS (buffer);
  if (dts == -1)
    dts = pts;
  else if (pts == -1)
    pts = dts;

  /* take the DTS of the buffer. This is the time when the packet was
   * received and is used to calculate jitter and clock skew. We will adjust
   * this DTS with the smoothed value after processing it in the
   * jitterbuffer and assign it as the PTS. */
  /* bring to running time */
  dts = gst_segment_to_running_time (&priv->segment, GST_FORMAT_TIME, dts);

  GST_DEBUG_OBJECT (jitterbuffer,
      "Received packet #%d at time %" GST_TIME_FORMAT ", discont %d", seqnum,
      GST_TIME_ARGS (dts), GST_BUFFER_IS_DISCONT (buffer));

  JBUF_LOCK_CHECK (priv, out_flushing);

  if (G_UNLIKELY (priv->last_pt != pt)) {
    GstCaps *caps;

    GST_DEBUG_OBJECT (jitterbuffer, "pt changed from %u to %u", priv->last_pt,
        pt);

    priv->last_pt = pt;
    /* reset clock-rate so that we get a new one */
    priv->clock_rate = -1;

    /* Try to get the clock-rate from the caps first if we can. If there are no
     * caps we must fire the signal to get the clock-rate. */
    if ((caps = gst_pad_get_current_caps (pad))) {
      gst_jitter_buffer_sink_parse_caps (jitterbuffer, caps);
      gst_caps_unref (caps);
    }
  }

  if (G_UNLIKELY (priv->clock_rate == -1)) {
    /* no clock rate given on the caps, try to get one with the signal */
    if (gst_rtp_jitter_buffer_get_clock_rate (jitterbuffer,
            pt) == GST_FLOW_FLUSHING)
      goto out_flushing;

    if (G_UNLIKELY (priv->clock_rate == -1))
      goto no_clock_rate;
  }

  /* don't accept more data on EOS */
  if (G_UNLIKELY (priv->eos))
    goto have_eos;

  calculate_jitter (jitterbuffer, dts, rtptime);

  expected = priv->next_in_seqnum;

  /* now check against our expected seqnum */
  if (G_LIKELY (expected != -1)) {
    gint gap;

    /* now calculate gap */
    gap = gst_rtp_buffer_compare_seqnum (expected, seqnum);

    GST_DEBUG_OBJECT (jitterbuffer, "expected #%d, got #%d, gap of %d",
        expected, seqnum, gap);

    if (G_LIKELY (gap == 0)) {
      /* packet is expected */
      calculate_packet_spacing (jitterbuffer, rtptime, dts);
      do_next_seqnum = TRUE;
    } else {
      gboolean reset = FALSE;

      if (!GST_CLOCK_TIME_IS_VALID (dts)) {
        /* We would run into calculations with GST_CLOCK_TIME_NONE below
         * and can't compensate for anything without DTS on RTP packets
         */
        goto gap_but_no_dts;
      } else if (gap < 0) {
        /* we received an old packet */
        if (G_UNLIKELY (gap < -RTP_MAX_MISORDER)) {
          /* too old packet, reset */
          GST_DEBUG_OBJECT (jitterbuffer, "reset: buffer too old %d < %d", gap,
              -RTP_MAX_MISORDER);
          reset = TRUE;
        } else {
          GST_DEBUG_OBJECT (jitterbuffer, "old packet received");
        }
      } else {
        /* new packet, we are missing some packets */
        if (G_UNLIKELY (gap > RTP_MAX_DROPOUT)) {
          /* packet too far in future, reset */
          GST_DEBUG_OBJECT (jitterbuffer, "reset: buffer too new %d > %d", gap,
              RTP_MAX_DROPOUT);
          reset = TRUE;
        } else {
          GST_DEBUG_OBJECT (jitterbuffer, "%d missing packets", gap);
          /* fill in the gap with EXPECTED timers */
          calculate_expected (jitterbuffer, expected, seqnum, dts, gap);

          do_next_seqnum = TRUE;
        }
      }
      if (G_UNLIKELY (reset)) {
        GList *events = NULL, *l;

        GST_DEBUG_OBJECT (jitterbuffer, "flush and reset jitterbuffer");
        rtp_jitter_buffer_flush (priv->jbuf,
            (GFunc) free_item_and_retain_events, &events);
        rtp_jitter_buffer_reset_skew (priv->jbuf);
        remove_all_timers (jitterbuffer);
        priv->last_popped_seqnum = -1;
        priv->next_seqnum = seqnum;
        do_next_seqnum = TRUE;

        /* Insert all sticky events again in order, otherwise we would
         * potentially loose STREAM_START, CAPS or SEGMENT events
         */
        events = g_list_reverse (events);
        for (l = events; l; l = l->next) {
          RTPJitterBufferItem *item;

          item = alloc_item (l->data, ITEM_TYPE_EVENT, -1, -1, -1, 0, -1);
          rtp_jitter_buffer_insert (priv->jbuf, item, &head, NULL);
        }
        g_list_free (events);

        JBUF_SIGNAL_EVENT (priv);
      }
      /* reset spacing estimation when gap */
      priv->ips_rtptime = -1;
      priv->ips_dts = GST_CLOCK_TIME_NONE;
    }
  } else {
    GST_DEBUG_OBJECT (jitterbuffer, "First buffer #%d", seqnum);
    /* we don't know what the next_in_seqnum should be, wait for the last
     * possible moment to push this buffer, maybe we get an earlier seqnum
     * while we wait */
    set_timer (jitterbuffer, TIMER_TYPE_DEADLINE, seqnum, dts);
    do_next_seqnum = TRUE;
    /* take rtptime and dts to calculate packet spacing */
    priv->ips_rtptime = rtptime;
    priv->ips_dts = dts;
  }
  if (do_next_seqnum) {
    priv->last_in_seqnum = seqnum;
    priv->last_in_dts = dts;
    priv->next_in_seqnum = (seqnum + 1) & 0xffff;
  }

  /* let's check if this buffer is too late, we can only accept packets with
   * bigger seqnum than the one we last pushed. */
  if (G_LIKELY (priv->last_popped_seqnum != -1)) {
    gint gap;

    gap = gst_rtp_buffer_compare_seqnum (priv->last_popped_seqnum, seqnum);

    /* priv->last_popped_seqnum >= seqnum, we're too late. */
    if (G_UNLIKELY (gap <= 0))
      goto too_late;
  }

  /* let's drop oldest packet if the queue is already full and drop-on-latency
   * is set. We can only do this when there actually is a latency. When no
   * latency is set, we just pump it in the queue and let the other end push it
   * out as fast as possible. */
  if (priv->latency_ms && priv->drop_on_latency) {
    latency_ts =
        gst_util_uint64_scale_int (priv->latency_ms, priv->clock_rate, 1000);

    if (G_UNLIKELY (rtp_jitter_buffer_get_ts_diff (priv->jbuf) >= latency_ts)) {
      RTPJitterBufferItem *old_item;

      old_item = rtp_jitter_buffer_peek (priv->jbuf);

      if (IS_DROPABLE (old_item)) {
        old_item = rtp_jitter_buffer_pop (priv->jbuf, &percent);
        GST_DEBUG_OBJECT (jitterbuffer, "Queue full, dropping old packet %p",
            old_item);
        priv->next_seqnum = (old_item->seqnum + 1) & 0xffff;
        free_item (old_item);
      }
      /* we might have removed some head buffers, signal the pushing thread to
       * see if it can push now */
      JBUF_SIGNAL_EVENT (priv);
    }
  }

  item = alloc_item (buffer, ITEM_TYPE_BUFFER, dts, pts, seqnum, 1, rtptime);

  /* now insert the packet into the queue in sorted order. This function returns
   * FALSE if a packet with the same seqnum was already in the queue, meaning we
   * have a duplicate. */
  if (G_UNLIKELY (!rtp_jitter_buffer_insert (priv->jbuf, item,
              &head, &percent)))
    goto duplicate;

  /* update timers */
  update_timers (jitterbuffer, seqnum, dts, do_next_seqnum);

  /* we had an unhandled SR, handle it now */
  if (priv->last_sr)
    do_handle_sync (jitterbuffer);

  if (G_UNLIKELY (head)) {
    /* signal addition of new buffer when the _loop is waiting. */
    if (G_LIKELY (priv->active))
      JBUF_SIGNAL_EVENT (priv);

    /* let's unschedule and unblock any waiting buffers. We only want to do this
     * when the head buffer changed */
    if (G_UNLIKELY (priv->clock_id)) {
      GST_DEBUG_OBJECT (jitterbuffer, "Unscheduling waiting new buffer");
      unschedule_current_timer (jitterbuffer);
    }
  }

  GST_DEBUG_OBJECT (jitterbuffer,
      "Pushed packet #%d, now %d packets, head: %d, " "percent %d", seqnum,
      rtp_jitter_buffer_num_packets (priv->jbuf), head, percent);

  msg = check_buffering_percent (jitterbuffer, percent);

finished:
  JBUF_UNLOCK (priv);

  if (msg)
    gst_element_post_message (GST_ELEMENT_CAST (jitterbuffer), msg);

  return ret;

  /* ERRORS */
invalid_buffer:
  {
    /* this is not fatal but should be filtered earlier */
    GST_ELEMENT_WARNING (jitterbuffer, STREAM, DECODE, (NULL),
        ("Received invalid RTP payload, dropping"));
    gst_buffer_unref (buffer);
    return GST_FLOW_OK;
  }
no_clock_rate:
  {
    GST_WARNING_OBJECT (jitterbuffer,
        "No clock-rate in caps!, dropping buffer");
    gst_buffer_unref (buffer);
    goto finished;
  }
out_flushing:
  {
    ret = priv->srcresult;
    GST_DEBUG_OBJECT (jitterbuffer, "flushing %s", gst_flow_get_name (ret));
    gst_buffer_unref (buffer);
    goto finished;
  }
have_eos:
  {
    ret = GST_FLOW_EOS;
    GST_WARNING_OBJECT (jitterbuffer, "we are EOS, refusing buffer");
    gst_buffer_unref (buffer);
    goto finished;
  }
too_late:
  {
    GST_WARNING_OBJECT (jitterbuffer, "Packet #%d too late as #%d was already"
        " popped, dropping", seqnum, priv->last_popped_seqnum);
    priv->num_late++;
    gst_buffer_unref (buffer);
    goto finished;
  }
duplicate:
  {
    GST_WARNING_OBJECT (jitterbuffer, "Duplicate packet #%d detected, dropping",
        seqnum);
    priv->num_duplicates++;
    free_item (item);
    goto finished;
  }
gap_but_no_dts:
  {
    /* this is fatal as we can't compensate for gaps without DTS */
    GST_ELEMENT_ERROR (jitterbuffer, STREAM, DECODE, (NULL),
        ("Received packet without DTS after a gap"));
    gst_buffer_unref (buffer);
    return GST_FLOW_ERROR;
  }
}

static GstClockTime
compute_elapsed (GstRtpJitterBuffer * jitterbuffer, RTPJitterBufferItem * item)
{
  guint64 ext_time, elapsed;
  guint32 rtp_time;
  GstRtpJitterBufferPrivate *priv;

  priv = jitterbuffer->priv;
  rtp_time = item->rtptime;

  GST_LOG_OBJECT (jitterbuffer, "rtp %" G_GUINT32_FORMAT ", ext %"
      G_GUINT64_FORMAT, rtp_time, priv->ext_timestamp);

  if (rtp_time < priv->ext_timestamp) {
    ext_time = priv->ext_timestamp;
  } else {
    ext_time = gst_rtp_buffer_ext_timestamp (&priv->ext_timestamp, rtp_time);
  }

  if (ext_time > priv->clock_base)
    elapsed = ext_time - priv->clock_base;
  else
    elapsed = 0;

  elapsed = gst_util_uint64_scale_int (elapsed, GST_SECOND, priv->clock_rate);
  return elapsed;
}

static void
update_estimated_eos (GstRtpJitterBuffer * jitterbuffer,
    RTPJitterBufferItem * item)
{
  guint64 total, elapsed, left, estimated;
  GstClockTime out_time;
  GstRtpJitterBufferPrivate *priv = jitterbuffer->priv;

  if (priv->npt_stop == -1 || priv->ext_timestamp == -1
      || priv->clock_base == -1 || priv->clock_rate <= 0)
    return;

  /* compute the elapsed time */
  elapsed = compute_elapsed (jitterbuffer, item);

  /* do nothing if elapsed time doesn't increment */
  if (priv->last_elapsed && elapsed <= priv->last_elapsed)
    return;

  priv->last_elapsed = elapsed;

  /* this is the total time we need to play */
  total = priv->npt_stop - priv->npt_start;
  GST_LOG_OBJECT (jitterbuffer, "total %" GST_TIME_FORMAT,
      GST_TIME_ARGS (total));

  /* this is how much time there is left */
  if (total > elapsed)
    left = total - elapsed;
  else
    left = 0;

  /* if we have less time left that the size of the buffer, we will not
   * be able to keep it filled, disabled buffering then */
  if (left < rtp_jitter_buffer_get_delay (priv->jbuf)) {
    GST_DEBUG_OBJECT (jitterbuffer, "left %" GST_TIME_FORMAT
        ", disable buffering close to EOS", GST_TIME_ARGS (left));
    rtp_jitter_buffer_disable_buffering (priv->jbuf, TRUE);
  }

  /* this is the current time as running-time */
  out_time = item->dts;

  if (elapsed > 0)
    estimated = gst_util_uint64_scale (out_time, total, elapsed);
  else {
    /* if there is almost nothing left,
     * we may never advance enough to end up in the above case */
    if (total < GST_SECOND)
      estimated = GST_SECOND;
    else
      estimated = -1;
  }
  GST_LOG_OBJECT (jitterbuffer, "elapsed %" GST_TIME_FORMAT ", estimated %"
      GST_TIME_FORMAT, GST_TIME_ARGS (elapsed), GST_TIME_ARGS (estimated));

  if (estimated != -1 && priv->estimated_eos != estimated) {
    set_timer (jitterbuffer, TIMER_TYPE_EOS, -1, estimated);
    priv->estimated_eos = estimated;
  }
}

/* take a buffer from the queue and push it */
static GstFlowReturn
pop_and_push_next (GstRtpJitterBuffer * jitterbuffer, guint seqnum)
{
  GstRtpJitterBufferPrivate *priv = jitterbuffer->priv;
  GstFlowReturn result = GST_FLOW_OK;
  RTPJitterBufferItem *item;
  GstBuffer *outbuf = NULL;
  GstEvent *outevent = NULL;
  GstQuery *outquery = NULL;
  GstClockTime dts, pts;
  gint percent = -1;
  gboolean do_push = TRUE;
  guint type;
  GstMessage *msg;

  /* when we get here we are ready to pop and push the buffer */
  item = rtp_jitter_buffer_pop (priv->jbuf, &percent);
  type = item->type;

  switch (type) {
    case ITEM_TYPE_BUFFER:

      /* we need to make writable to change the flags and timestamps */
      outbuf = gst_buffer_make_writable (item->data);

      if (G_UNLIKELY (priv->discont)) {
        /* set DISCONT flag when we missed a packet. We pushed the buffer writable
         * into the jitterbuffer so we can modify now. */
        GST_DEBUG_OBJECT (jitterbuffer, "mark output buffer discont");
        GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DISCONT);
        priv->discont = FALSE;
      }
      if (G_UNLIKELY (priv->ts_discont)) {
        GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_RESYNC);
        priv->ts_discont = FALSE;
      }

      dts =
          gst_segment_to_position (&priv->segment, GST_FORMAT_TIME, item->dts);
      pts =
          gst_segment_to_position (&priv->segment, GST_FORMAT_TIME, item->pts);

      /* apply timestamp with offset to buffer now */
      GST_BUFFER_DTS (outbuf) = apply_offset (jitterbuffer, dts);
      GST_BUFFER_PTS (outbuf) = apply_offset (jitterbuffer, pts);

      /* update the elapsed time when we need to check against the npt stop time. */
      update_estimated_eos (jitterbuffer, item);

      priv->last_out_time = GST_BUFFER_PTS (outbuf);
      break;
    case ITEM_TYPE_LOST:
      priv->discont = TRUE;
      if (!priv->do_lost)
        do_push = FALSE;
      /* FALLTHROUGH */
    case ITEM_TYPE_EVENT:
      outevent = item->data;
      break;
    case ITEM_TYPE_QUERY:
      outquery = item->data;
      break;
  }

  /* now we are ready to push the buffer. Save the seqnum and release the lock
   * so the other end can push stuff in the queue again. */
  if (seqnum != -1) {
    priv->last_popped_seqnum = seqnum;
    priv->next_seqnum = (seqnum + item->count) & 0xffff;
  }
  msg = check_buffering_percent (jitterbuffer, percent);
  JBUF_UNLOCK (priv);

  item->data = NULL;
  free_item (item);

  if (msg)
    gst_element_post_message (GST_ELEMENT_CAST (jitterbuffer), msg);

  switch (type) {
    case ITEM_TYPE_BUFFER:
      /* push buffer */
      GST_DEBUG_OBJECT (jitterbuffer,
          "Pushing buffer %d, dts %" GST_TIME_FORMAT ", pts %" GST_TIME_FORMAT,
          seqnum, GST_TIME_ARGS (GST_BUFFER_DTS (outbuf)),
          GST_TIME_ARGS (GST_BUFFER_PTS (outbuf)));
      result = gst_pad_push (priv->srcpad, outbuf);

      JBUF_LOCK_CHECK (priv, out_flushing);
      break;
    case ITEM_TYPE_LOST:
    case ITEM_TYPE_EVENT:
      GST_DEBUG_OBJECT (jitterbuffer, "%sPushing event %" GST_PTR_FORMAT
          ", seqnum %d", do_push ? "" : "NOT ", outevent, seqnum);

      if (do_push)
        gst_pad_push_event (priv->srcpad, outevent);
      else
        gst_event_unref (outevent);

      result = GST_FLOW_OK;

      JBUF_LOCK_CHECK (priv, out_flushing);
      break;
    case ITEM_TYPE_QUERY:
    {
      gboolean res;

      res = gst_pad_peer_query (priv->srcpad, outquery);

      JBUF_LOCK_CHECK (priv, out_flushing);
      result = GST_FLOW_OK;
      GST_LOG_OBJECT (jitterbuffer, "did query %p, return %d", outquery, res);
      JBUF_SIGNAL_QUERY (priv, res);
      break;
    }
  }
  return result;

  /* ERRORS */
out_flushing:
  {
    return priv->srcresult;
  }
}

#define GST_FLOW_WAIT GST_FLOW_CUSTOM_SUCCESS

/* Peek a buffer and compare the seqnum to the expected seqnum.
 * If all is fine, the buffer is pushed.
 * If something is wrong, we wait for some event
 */
static GstFlowReturn
handle_next_buffer (GstRtpJitterBuffer * jitterbuffer)
{
  GstRtpJitterBufferPrivate *priv = jitterbuffer->priv;
  GstFlowReturn result = GST_FLOW_OK;
  RTPJitterBufferItem *item;
  guint seqnum;
  guint32 next_seqnum;
  gint gap;

  /* only push buffers when PLAYING and active and not buffering */
  if (priv->blocked || !priv->active ||
      rtp_jitter_buffer_is_buffering (priv->jbuf))
    return GST_FLOW_WAIT;

again:
  /* peek a buffer, we're just looking at the sequence number.
   * If all is fine, we'll pop and push it. If the sequence number is wrong we
   * wait for a timeout or something to change.
   * The peeked buffer is valid for as long as we hold the jitterbuffer lock. */
  item = rtp_jitter_buffer_peek (priv->jbuf);
  if (item == NULL)
    goto wait;

  /* get the seqnum and the next expected seqnum */
  seqnum = item->seqnum;
  if (seqnum == -1)
    goto do_push;

  next_seqnum = priv->next_seqnum;

  /* get the gap between this and the previous packet. If we don't know the
   * previous packet seqnum assume no gap. */
  if (G_UNLIKELY (next_seqnum == -1)) {
    GST_DEBUG_OBJECT (jitterbuffer, "First buffer #%d", seqnum);
    /* we don't know what the next_seqnum should be, the chain function should
     * have scheduled a DEADLINE timer that will increment next_seqnum when it
     * fires, so wait for that */
    result = GST_FLOW_WAIT;
  } else {
    /* else calculate GAP */
    gap = gst_rtp_buffer_compare_seqnum (next_seqnum, seqnum);

    if (G_LIKELY (gap == 0)) {
    do_push:
      /* no missing packet, pop and push */
      result = pop_and_push_next (jitterbuffer, seqnum);
    } else if (G_UNLIKELY (gap < 0)) {
      RTPJitterBufferItem *item;
      /* if we have a packet that we already pushed or considered dropped, pop it
       * off and get the next packet */
      GST_DEBUG_OBJECT (jitterbuffer, "Old packet #%d, next #%d dropping",
          seqnum, next_seqnum);
      item = rtp_jitter_buffer_pop (priv->jbuf, NULL);
      free_item (item);
      goto again;
    } else {
      /* the chain function has scheduled timers to request retransmission or
       * when to consider the packet lost, wait for that */
      GST_DEBUG_OBJECT (jitterbuffer,
          "Sequence number GAP detected: expected %d instead of %d (%d missing)",
          next_seqnum, seqnum, gap);
      result = GST_FLOW_WAIT;
    }
  }
  return result;

wait:
  {
    GST_DEBUG_OBJECT (jitterbuffer, "no buffer, going to wait");
    if (priv->eos)
      result = GST_FLOW_EOS;
    else
      result = GST_FLOW_WAIT;
    return result;
  }
}

static GstClockTime
get_rtx_retry_timeout (GstRtpJitterBufferPrivate * priv)
{
  GstClockTime rtx_retry_timeout;
  GstClockTime rtx_min_retry_timeout;

  if (priv->rtx_retry_timeout == -1) {
    if (priv->avg_rtx_rtt == 0)
      rtx_retry_timeout = DEFAULT_AUTO_RTX_TIMEOUT;
    else
      /* we want to ask for a retransmission after we waited for a
       * complete RTT and the additional jitter */
      rtx_retry_timeout = priv->avg_rtx_rtt + priv->avg_jitter * 2;
  } else {
    rtx_retry_timeout = priv->rtx_retry_timeout * GST_MSECOND;
  }
  /* make sure we don't retry too often. On very low latency networks,
   * the RTT and jitter can be very low. */
  if (priv->rtx_min_retry_timeout == -1) {
    rtx_min_retry_timeout = priv->packet_spacing;
  } else {
    rtx_min_retry_timeout = priv->rtx_min_retry_timeout * GST_MSECOND;
  }
  rtx_retry_timeout = MAX (rtx_retry_timeout, rtx_min_retry_timeout);

  return rtx_retry_timeout;
}

static GstClockTime
get_rtx_retry_period (GstRtpJitterBufferPrivate * priv,
    GstClockTime rtx_retry_timeout)
{
  GstClockTime rtx_retry_period;

  if (priv->rtx_retry_period == -1) {
    /* we retry up to the configured jitterbuffer size but leaving some
     * room for the retransmission to arrive in time */
    if (rtx_retry_timeout > priv->latency_ns) {
      rtx_retry_period = 0;
    } else {
      rtx_retry_period = priv->latency_ns - rtx_retry_timeout;
    }
  } else {
    rtx_retry_period = priv->rtx_retry_period * GST_MSECOND;
  }
  return rtx_retry_period;
}

/* the timeout for when we expected a packet expired */
static gboolean
do_expected_timeout (GstRtpJitterBuffer * jitterbuffer, TimerData * timer,
    GstClockTime now)
{
  GstRtpJitterBufferPrivate *priv = jitterbuffer->priv;
  GstEvent *event;
  guint delay, delay_ms, avg_rtx_rtt_ms;
  guint rtx_retry_timeout_ms, rtx_retry_period_ms;
  GstClockTime rtx_retry_period;
  GstClockTime rtx_retry_timeout;
  GstClock *clock;

  GST_DEBUG_OBJECT (jitterbuffer, "expected %d didn't arrive, now %"
      GST_TIME_FORMAT, timer->seqnum, GST_TIME_ARGS (now));

  rtx_retry_timeout = get_rtx_retry_timeout (priv);
  rtx_retry_period = get_rtx_retry_period (priv, rtx_retry_timeout);

  GST_DEBUG_OBJECT (jitterbuffer, "timeout %" GST_TIME_FORMAT ", period %"
      GST_TIME_FORMAT, GST_TIME_ARGS (rtx_retry_timeout),
      GST_TIME_ARGS (rtx_retry_period));

  delay = timer->rtx_delay + timer->rtx_retry;

  delay_ms = GST_TIME_AS_MSECONDS (delay);
  rtx_retry_timeout_ms = GST_TIME_AS_MSECONDS (rtx_retry_timeout);
  rtx_retry_period_ms = GST_TIME_AS_MSECONDS (rtx_retry_period);
  avg_rtx_rtt_ms = GST_TIME_AS_MSECONDS (priv->avg_rtx_rtt);

  event = gst_event_new_custom (GST_EVENT_CUSTOM_UPSTREAM,
      gst_structure_new ("GstRTPRetransmissionRequest",
          "seqnum", G_TYPE_UINT, (guint) timer->seqnum,
          "running-time", G_TYPE_UINT64, timer->rtx_base,
          "delay", G_TYPE_UINT, delay_ms,
          "retry", G_TYPE_UINT, timer->num_rtx_retry,
          "frequency", G_TYPE_UINT, rtx_retry_timeout_ms,
          "period", G_TYPE_UINT, rtx_retry_period_ms,
          "deadline", G_TYPE_UINT, priv->latency_ms,
          "packet-spacing", G_TYPE_UINT64, priv->packet_spacing,
          "avg-rtt", G_TYPE_UINT, avg_rtx_rtt_ms, NULL));

  priv->num_rtx_requests++;
  timer->num_rtx_retry++;

  GST_OBJECT_LOCK (jitterbuffer);
  if ((clock = GST_ELEMENT_CLOCK (jitterbuffer))) {
    timer->rtx_last = gst_clock_get_time (clock);
    timer->rtx_last -= GST_ELEMENT_CAST (jitterbuffer)->base_time;
  } else {
    timer->rtx_last = now;
  }
  GST_OBJECT_UNLOCK (jitterbuffer);

  /* calculate the timeout for the next retransmission attempt */
  timer->rtx_retry += rtx_retry_timeout;
  GST_DEBUG_OBJECT (jitterbuffer, "base %" GST_TIME_FORMAT ", delay %"
      GST_TIME_FORMAT ", retry %" GST_TIME_FORMAT ", num_retry %u",
      GST_TIME_ARGS (timer->rtx_base), GST_TIME_ARGS (timer->rtx_delay),
      GST_TIME_ARGS (timer->rtx_retry), timer->num_rtx_retry);

  if (timer->rtx_retry + timer->rtx_delay > rtx_retry_period) {
    GST_DEBUG_OBJECT (jitterbuffer, "reschedule as LOST timer");
    /* too many retransmission request, we now convert the timer
     * to a lost timer, leave the num_rtx_retry as it is for stats */
    timer->type = TIMER_TYPE_LOST;
    timer->rtx_delay = 0;
    timer->rtx_retry = 0;
  }
  reschedule_timer (jitterbuffer, timer, timer->seqnum,
      timer->rtx_base + timer->rtx_retry, timer->rtx_delay, FALSE);

  JBUF_UNLOCK (priv);
  gst_pad_push_event (priv->sinkpad, event);
  JBUF_LOCK (priv);

  return FALSE;
}

/* a packet is lost */
static gboolean
do_lost_timeout (GstRtpJitterBuffer * jitterbuffer, TimerData * timer,
    GstClockTime now)
{
  GstRtpJitterBufferPrivate *priv = jitterbuffer->priv;
  GstClockTime duration, timestamp;
  guint seqnum, lost_packets, num_rtx_retry, next_in_seqnum;
  gboolean late, head;
  GstEvent *event;
  RTPJitterBufferItem *item;

  seqnum = timer->seqnum;
  timestamp = apply_offset (jitterbuffer, timer->timeout);
  duration = timer->duration;
  if (duration == GST_CLOCK_TIME_NONE && priv->packet_spacing > 0)
    duration = priv->packet_spacing;
  lost_packets = MAX (timer->num, 1);
  late = timer->num > 0;
  num_rtx_retry = timer->num_rtx_retry;

  /* we had a gap and thus we lost some packets. Create an event for this.  */
  if (lost_packets > 1)
    GST_DEBUG_OBJECT (jitterbuffer, "Packets #%d -> #%d lost", seqnum,
        seqnum + lost_packets - 1);
  else
    GST_DEBUG_OBJECT (jitterbuffer, "Packet #%d lost", seqnum);

  priv->num_late += lost_packets;
  priv->num_rtx_failed += num_rtx_retry;

  next_in_seqnum = (seqnum + lost_packets) & 0xffff;

  /* we now only accept seqnum bigger than this */
  if (gst_rtp_buffer_compare_seqnum (priv->next_in_seqnum, next_in_seqnum) > 0)
    priv->next_in_seqnum = next_in_seqnum;

  /* create paket lost event */
  event = gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM,
      gst_structure_new ("GstRTPPacketLost",
          "seqnum", G_TYPE_UINT, (guint) seqnum,
          "timestamp", G_TYPE_UINT64, timestamp,
          "duration", G_TYPE_UINT64, duration,
          "late", G_TYPE_BOOLEAN, late,
          "retry", G_TYPE_UINT, num_rtx_retry, NULL));

  item = alloc_item (event, ITEM_TYPE_LOST, -1, -1, seqnum, lost_packets, -1);
  rtp_jitter_buffer_insert (priv->jbuf, item, &head, NULL);

  /* remove timer now */
  remove_timer (jitterbuffer, timer);
  if (head)
    JBUF_SIGNAL_EVENT (priv);

  return TRUE;
}

static gboolean
do_eos_timeout (GstRtpJitterBuffer * jitterbuffer, TimerData * timer,
    GstClockTime now)
{
  GstRtpJitterBufferPrivate *priv = jitterbuffer->priv;

  GST_INFO_OBJECT (jitterbuffer, "got the NPT timeout");
  remove_timer (jitterbuffer, timer);
  if (!priv->eos) {
    /* there was no EOS in the buffer, put one in there now */
    queue_event (jitterbuffer, gst_event_new_eos ());
  }
  JBUF_SIGNAL_EVENT (priv);

  return TRUE;
}

static gboolean
do_deadline_timeout (GstRtpJitterBuffer * jitterbuffer, TimerData * timer,
    GstClockTime now)
{
  GstRtpJitterBufferPrivate *priv = jitterbuffer->priv;

  GST_INFO_OBJECT (jitterbuffer, "got deadline timeout");

  /* timer seqnum might have been obsoleted by caps seqnum-base,
   * only mess with current ongoing seqnum if still unknown */
  if (priv->next_seqnum == -1)
    priv->next_seqnum = timer->seqnum;
  remove_timer (jitterbuffer, timer);
  JBUF_SIGNAL_EVENT (priv);

  return TRUE;
}

static gboolean
do_timeout (GstRtpJitterBuffer * jitterbuffer, TimerData * timer,
    GstClockTime now)
{
  gboolean removed = FALSE;

  switch (timer->type) {
    case TIMER_TYPE_EXPECTED:
      removed = do_expected_timeout (jitterbuffer, timer, now);
      break;
    case TIMER_TYPE_LOST:
      removed = do_lost_timeout (jitterbuffer, timer, now);
      break;
    case TIMER_TYPE_DEADLINE:
      removed = do_deadline_timeout (jitterbuffer, timer, now);
      break;
    case TIMER_TYPE_EOS:
      removed = do_eos_timeout (jitterbuffer, timer, now);
      break;
  }
  return removed;
}

/* called when we need to wait for the next timeout.
 *
 * We loop over the array of recorded timeouts and wait for the earliest one.
 * When it timed out, do the logic associated with the timer.
 *
 * If there are no timers, we wait on a gcond until something new happens.
 */
static void
wait_next_timeout (GstRtpJitterBuffer * jitterbuffer)
{
  GstRtpJitterBufferPrivate *priv = jitterbuffer->priv;
  GstClockTime now = 0;

  JBUF_LOCK (priv);
  while (priv->timer_running) {
    TimerData *timer = NULL;
    GstClockTime timer_timeout = -1;
    gint i, len;

    GST_DEBUG_OBJECT (jitterbuffer, "now %" GST_TIME_FORMAT,
        GST_TIME_ARGS (now));

    len = priv->timers->len;
    for (i = 0; i < len; i++) {
      TimerData *test = &g_array_index (priv->timers, TimerData, i);
      GstClockTime test_timeout = get_timeout (jitterbuffer, test);
      gboolean save_best = FALSE;

      GST_DEBUG_OBJECT (jitterbuffer, "%d, %d, %d, %" GST_TIME_FORMAT,
          i, test->type, test->seqnum, GST_TIME_ARGS (test_timeout));

      /* find the smallest timeout */
      if (timer == NULL) {
        save_best = TRUE;
      } else if (timer_timeout == -1) {
        /* we already have an immediate timeout, the new timer must be an
         * immediate timer with smaller seqnum to become the best */
        if (test_timeout == -1
            && (gst_rtp_buffer_compare_seqnum (test->seqnum,
                    timer->seqnum) > 0))
          save_best = TRUE;
      } else if (test_timeout == -1) {
        /* first immediate timer */
        save_best = TRUE;
      } else if (test_timeout < timer_timeout) {
        /* earlier timer */
        save_best = TRUE;
      } else if (test_timeout == timer_timeout
          && (gst_rtp_buffer_compare_seqnum (test->seqnum,
                  timer->seqnum) > 0)) {
        /* same timer, smaller seqnum */
        save_best = TRUE;
      }
      if (save_best) {
        GST_DEBUG_OBJECT (jitterbuffer, "new best %d", i);
        timer = test;
        timer_timeout = test_timeout;
      }
    }
    if (timer && !priv->blocked) {
      GstClock *clock;
      GstClockTime sync_time;
      GstClockID id;
      GstClockReturn ret;
      GstClockTimeDiff clock_jitter;

      if (timer_timeout == -1 || timer_timeout <= now) {
        do_timeout (jitterbuffer, timer, now);
        /* check here, do_timeout could have released the lock */
        if (!priv->timer_running)
          break;
        continue;
      }

      GST_OBJECT_LOCK (jitterbuffer);
      clock = GST_ELEMENT_CLOCK (jitterbuffer);
      if (!clock) {
        GST_OBJECT_UNLOCK (jitterbuffer);
        /* let's just push if there is no clock */
        GST_DEBUG_OBJECT (jitterbuffer, "No clock, timeout right away");
        now = timer_timeout;
        continue;
      }

      /* prepare for sync against clock */
      sync_time = timer_timeout + GST_ELEMENT_CAST (jitterbuffer)->base_time;
      /* add latency of peer to get input time */
      sync_time += priv->peer_latency;

      GST_DEBUG_OBJECT (jitterbuffer, "sync to timestamp %" GST_TIME_FORMAT
          " with sync time %" GST_TIME_FORMAT,
          GST_TIME_ARGS (timer_timeout), GST_TIME_ARGS (sync_time));

      /* create an entry for the clock */
      id = priv->clock_id = gst_clock_new_single_shot_id (clock, sync_time);
      priv->timer_timeout = timer_timeout;
      priv->timer_seqnum = timer->seqnum;
      GST_OBJECT_UNLOCK (jitterbuffer);

      /* release the lock so that the other end can push stuff or unlock */
      JBUF_UNLOCK (priv);

      ret = gst_clock_id_wait (id, &clock_jitter);

      JBUF_LOCK (priv);
      if (!priv->timer_running) {
        gst_clock_id_unref (id);
        priv->clock_id = NULL;
        break;
      }

      if (ret != GST_CLOCK_UNSCHEDULED) {
        now = timer_timeout + MAX (clock_jitter, 0);
        GST_DEBUG_OBJECT (jitterbuffer, "sync done, %d, #%d, %" G_GINT64_FORMAT,
            ret, priv->timer_seqnum, clock_jitter);
      } else {
        GST_DEBUG_OBJECT (jitterbuffer, "sync unscheduled");
      }
      /* and free the entry */
      gst_clock_id_unref (id);
      priv->clock_id = NULL;
    } else {
      /* no timers, wait for activity */
      JBUF_WAIT_TIMER (priv);
    }
  }
  JBUF_UNLOCK (priv);

  GST_DEBUG_OBJECT (jitterbuffer, "we are stopping");
  return;
}

/*
 * This funcion implements the main pushing loop on the source pad.
 *
 * It first tries to push as many buffers as possible. If there is a seqnum
 * mismatch, we wait for the next timeouts.
 */
static void
gst_rtp_jitter_buffer_loop (GstRtpJitterBuffer * jitterbuffer)
{
  GstRtpJitterBufferPrivate *priv;
  GstFlowReturn result = GST_FLOW_OK;

  priv = jitterbuffer->priv;

  JBUF_LOCK_CHECK (priv, flushing);
  do {
    result = handle_next_buffer (jitterbuffer);
    if (G_LIKELY (result == GST_FLOW_WAIT)) {
      /* now wait for the next event */
      JBUF_WAIT_EVENT (priv, flushing);
      result = GST_FLOW_OK;
    }
  }
  while (result == GST_FLOW_OK);
  /* store result for upstream */
  priv->srcresult = result;
  /* if we get here we need to pause */
  goto pause;

  /* ERRORS */
flushing:
  {
    result = priv->srcresult;
    goto pause;
  }
pause:
  {
    GstEvent *event;

    JBUF_SIGNAL_QUERY (priv, FALSE);
    JBUF_UNLOCK (priv);

    GST_DEBUG_OBJECT (jitterbuffer, "pausing task, reason %s",
        gst_flow_get_name (result));
    gst_pad_pause_task (priv->srcpad);
    if (result == GST_FLOW_EOS) {
      event = gst_event_new_eos ();
      gst_pad_push_event (priv->srcpad, event);
    }
    return;
  }
}

/* collect the info from the lastest RTCP packet and the jitterbuffer sync, do
 * some sanity checks and then emit the handle-sync signal with the parameters.
 * This function must be called with the LOCK */
static void
do_handle_sync (GstRtpJitterBuffer * jitterbuffer)
{
  GstRtpJitterBufferPrivate *priv;
  guint64 base_rtptime, base_time;
  guint32 clock_rate;
  guint64 last_rtptime;
  guint64 clock_base;
  guint64 ext_rtptime, diff;
  gboolean valid = TRUE, keep = FALSE;

  priv = jitterbuffer->priv;

  /* get the last values from the jitterbuffer */
  rtp_jitter_buffer_get_sync (priv->jbuf, &base_rtptime, &base_time,
      &clock_rate, &last_rtptime);

  clock_base = priv->clock_base;
  ext_rtptime = priv->ext_rtptime;

  GST_DEBUG_OBJECT (jitterbuffer, "ext SR %" G_GUINT64_FORMAT ", base %"
      G_GUINT64_FORMAT ", clock-rate %" G_GUINT32_FORMAT
      ", clock-base %" G_GUINT64_FORMAT ", last-rtptime %" G_GUINT64_FORMAT,
      ext_rtptime, base_rtptime, clock_rate, clock_base, last_rtptime);

  if (base_rtptime == -1 || clock_rate == -1 || base_time == -1) {
    /* we keep this SR packet for later. When we get a valid RTP packet the
     * above values will be set and we can try to use the SR packet */
    GST_DEBUG_OBJECT (jitterbuffer, "keeping for later, no RTP values");
    keep = TRUE;
  } else {
    /* we can't accept anything that happened before we did the last resync */
    if (base_rtptime > ext_rtptime) {
      GST_DEBUG_OBJECT (jitterbuffer, "dropping, older than base time");
      valid = FALSE;
    } else {
      /* the SR RTP timestamp must be something close to what we last observed
       * in the jitterbuffer */
      if (ext_rtptime > last_rtptime) {
        /* check how far ahead it is to our RTP timestamps */
        diff = ext_rtptime - last_rtptime;
        /* if bigger than 1 second, we drop it */
        if (diff > clock_rate) {
          GST_DEBUG_OBJECT (jitterbuffer, "too far ahead");
          /* should drop this, but some RTSP servers end up with bogus
           * way too ahead RTCP packet when repeated PAUSE/PLAY,
           * so still trigger rptbin sync but invalidate RTCP data
           * (sync might use other methods) */
          ext_rtptime = -1;
        }
        GST_DEBUG_OBJECT (jitterbuffer, "ext last %" G_GUINT64_FORMAT ", diff %"
            G_GUINT64_FORMAT, last_rtptime, diff);
      }
    }
  }

  if (keep) {
    GST_DEBUG_OBJECT (jitterbuffer, "keeping RTCP packet for later");
  } else if (valid) {
    GstStructure *s;

    s = gst_structure_new ("application/x-rtp-sync",
        "base-rtptime", G_TYPE_UINT64, base_rtptime,
        "base-time", G_TYPE_UINT64, base_time,
        "clock-rate", G_TYPE_UINT, clock_rate,
        "clock-base", G_TYPE_UINT64, clock_base,
        "sr-ext-rtptime", G_TYPE_UINT64, ext_rtptime,
        "sr-buffer", GST_TYPE_BUFFER, priv->last_sr, NULL);

    GST_DEBUG_OBJECT (jitterbuffer, "signaling sync");
    gst_buffer_replace (&priv->last_sr, NULL);
    JBUF_UNLOCK (priv);
    g_signal_emit (jitterbuffer,
        gst_rtp_jitter_buffer_signals[SIGNAL_HANDLE_SYNC], 0, s);
    JBUF_LOCK (priv);
    gst_structure_free (s);
  } else {
    GST_DEBUG_OBJECT (jitterbuffer, "dropping RTCP packet");
    gst_buffer_replace (&priv->last_sr, NULL);
  }
}

static GstFlowReturn
gst_rtp_jitter_buffer_chain_rtcp (GstPad * pad, GstObject * parent,
    GstBuffer * buffer)
{
  GstRtpJitterBuffer *jitterbuffer;
  GstRtpJitterBufferPrivate *priv;
  GstFlowReturn ret = GST_FLOW_OK;
  guint32 ssrc;
  GstRTCPPacket packet;
  guint64 ext_rtptime;
  guint32 rtptime;
  GstRTCPBuffer rtcp = { NULL, };

  jitterbuffer = GST_RTP_JITTER_BUFFER (parent);

  if (G_UNLIKELY (!gst_rtcp_buffer_validate (buffer)))
    goto invalid_buffer;

  priv = jitterbuffer->priv;

  gst_rtcp_buffer_map (buffer, GST_MAP_READ, &rtcp);

  if (!gst_rtcp_buffer_get_first_packet (&rtcp, &packet))
    goto empty_buffer;

  /* first packet must be SR or RR or else the validate would have failed */
  switch (gst_rtcp_packet_get_type (&packet)) {
    case GST_RTCP_TYPE_SR:
      gst_rtcp_packet_sr_get_sender_info (&packet, &ssrc, NULL, &rtptime,
          NULL, NULL);
      break;
    default:
      goto ignore_buffer;
  }
  gst_rtcp_buffer_unmap (&rtcp);

  GST_DEBUG_OBJECT (jitterbuffer, "received RTCP of SSRC %08x", ssrc);

  JBUF_LOCK (priv);
  /* convert the RTP timestamp to our extended timestamp, using the same offset
   * we used in the jitterbuffer */
  ext_rtptime = priv->jbuf->ext_rtptime;
  ext_rtptime = gst_rtp_buffer_ext_timestamp (&ext_rtptime, rtptime);

  priv->ext_rtptime = ext_rtptime;
  gst_buffer_replace (&priv->last_sr, buffer);

  do_handle_sync (jitterbuffer);
  JBUF_UNLOCK (priv);

done:
  gst_buffer_unref (buffer);

  return ret;

invalid_buffer:
  {
    /* this is not fatal but should be filtered earlier */
    GST_ELEMENT_WARNING (jitterbuffer, STREAM, DECODE, (NULL),
        ("Received invalid RTCP payload, dropping"));
    ret = GST_FLOW_OK;
    goto done;
  }
empty_buffer:
  {
    /* this is not fatal but should be filtered earlier */
    GST_ELEMENT_WARNING (jitterbuffer, STREAM, DECODE, (NULL),
        ("Received empty RTCP payload, dropping"));
    gst_rtcp_buffer_unmap (&rtcp);
    ret = GST_FLOW_OK;
    goto done;
  }
ignore_buffer:
  {
    GST_DEBUG_OBJECT (jitterbuffer, "ignoring RTCP packet");
    gst_rtcp_buffer_unmap (&rtcp);
    ret = GST_FLOW_OK;
    goto done;
  }
}

static gboolean
gst_rtp_jitter_buffer_sink_query (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  gboolean res = FALSE;
  GstRtpJitterBuffer *jitterbuffer;
  GstRtpJitterBufferPrivate *priv;

  jitterbuffer = GST_RTP_JITTER_BUFFER (parent);
  priv = jitterbuffer->priv;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *filter, *caps;

      gst_query_parse_caps (query, &filter);
      caps = gst_rtp_jitter_buffer_getcaps (pad, filter);
      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);
      res = TRUE;
      break;
    }
    default:
      if (GST_QUERY_IS_SERIALIZED (query)) {
        RTPJitterBufferItem *item;
        gboolean head;

        JBUF_LOCK_CHECK (priv, out_flushing);
        if (rtp_jitter_buffer_get_mode (priv->jbuf) !=
            RTP_JITTER_BUFFER_MODE_BUFFER) {
          GST_DEBUG_OBJECT (jitterbuffer, "adding serialized query");
          item = alloc_item (query, ITEM_TYPE_QUERY, -1, -1, -1, 0, -1);
          rtp_jitter_buffer_insert (priv->jbuf, item, &head, NULL);
          if (head)
            JBUF_SIGNAL_EVENT (priv);
          JBUF_WAIT_QUERY (priv, out_flushing);
          res = priv->last_query;
        } else {
          GST_DEBUG_OBJECT (jitterbuffer, "refusing query, we are buffering");
          res = FALSE;
        }
        JBUF_UNLOCK (priv);
      } else {
        res = gst_pad_query_default (pad, parent, query);
      }
      break;
  }
  return res;
  /* ERRORS */
out_flushing:
  {
    GST_DEBUG_OBJECT (jitterbuffer, "we are flushing");
    JBUF_UNLOCK (priv);
    return FALSE;
  }

}

static gboolean
gst_rtp_jitter_buffer_src_query (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  GstRtpJitterBuffer *jitterbuffer;
  GstRtpJitterBufferPrivate *priv;
  gboolean res = FALSE;

  jitterbuffer = GST_RTP_JITTER_BUFFER (parent);
  priv = jitterbuffer->priv;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:
    {
      /* We need to send the query upstream and add the returned latency to our
       * own */
      GstClockTime min_latency, max_latency;
      gboolean us_live;
      GstClockTime our_latency;

      if ((res = gst_pad_peer_query (priv->sinkpad, query))) {
        gst_query_parse_latency (query, &us_live, &min_latency, &max_latency);

        GST_DEBUG_OBJECT (jitterbuffer, "Peer latency: min %"
            GST_TIME_FORMAT " max %" GST_TIME_FORMAT,
            GST_TIME_ARGS (min_latency), GST_TIME_ARGS (max_latency));

        /* store this so that we can safely sync on the peer buffers. */
        JBUF_LOCK (priv);
        priv->peer_latency = min_latency;
        our_latency = priv->latency_ns;
        JBUF_UNLOCK (priv);

        GST_DEBUG_OBJECT (jitterbuffer, "Our latency: %" GST_TIME_FORMAT,
            GST_TIME_ARGS (our_latency));

        /* we add some latency but can buffer an infinite amount of time */
        min_latency += our_latency;
        max_latency = -1;

        GST_DEBUG_OBJECT (jitterbuffer, "Calculated total latency : min %"
            GST_TIME_FORMAT " max %" GST_TIME_FORMAT,
            GST_TIME_ARGS (min_latency), GST_TIME_ARGS (max_latency));

        gst_query_set_latency (query, TRUE, min_latency, max_latency);
      }
      break;
    }
    case GST_QUERY_POSITION:
    {
      GstClockTime start, last_out;
      GstFormat fmt;

      gst_query_parse_position (query, &fmt, NULL);
      if (fmt != GST_FORMAT_TIME) {
        res = gst_pad_query_default (pad, parent, query);
        break;
      }

      JBUF_LOCK (priv);
      start = priv->npt_start;
      last_out = priv->last_out_time;
      JBUF_UNLOCK (priv);

      GST_DEBUG_OBJECT (jitterbuffer, "npt start %" GST_TIME_FORMAT
          ", last out %" GST_TIME_FORMAT, GST_TIME_ARGS (start),
          GST_TIME_ARGS (last_out));

      if (GST_CLOCK_TIME_IS_VALID (start) && GST_CLOCK_TIME_IS_VALID (last_out)) {
        /* bring 0-based outgoing time to stream time */
        gst_query_set_position (query, GST_FORMAT_TIME, start + last_out);
        res = TRUE;
      } else {
        res = gst_pad_query_default (pad, parent, query);
      }
      break;
    }
    case GST_QUERY_CAPS:
    {
      GstCaps *filter, *caps;

      gst_query_parse_caps (query, &filter);
      caps = gst_rtp_jitter_buffer_getcaps (pad, filter);
      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);
      res = TRUE;
      break;
    }
    default:
      res = gst_pad_query_default (pad, parent, query);
      break;
  }

  return res;
}

static void
gst_rtp_jitter_buffer_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstRtpJitterBuffer *jitterbuffer;
  GstRtpJitterBufferPrivate *priv;

  jitterbuffer = GST_RTP_JITTER_BUFFER (object);
  priv = jitterbuffer->priv;

  switch (prop_id) {
    case PROP_LATENCY:
    {
      guint new_latency, old_latency;

      new_latency = g_value_get_uint (value);

      JBUF_LOCK (priv);
      old_latency = priv->latency_ms;
      priv->latency_ms = new_latency;
      priv->latency_ns = priv->latency_ms * GST_MSECOND;
      rtp_jitter_buffer_set_delay (priv->jbuf, priv->latency_ns);
      JBUF_UNLOCK (priv);

      /* post message if latency changed, this will inform the parent pipeline
       * that a latency reconfiguration is possible/needed. */
      if (new_latency != old_latency) {
        GST_DEBUG_OBJECT (jitterbuffer, "latency changed to: %" GST_TIME_FORMAT,
            GST_TIME_ARGS (new_latency * GST_MSECOND));

        gst_element_post_message (GST_ELEMENT_CAST (jitterbuffer),
            gst_message_new_latency (GST_OBJECT_CAST (jitterbuffer)));
      }
      break;
    }
    case PROP_DROP_ON_LATENCY:
      JBUF_LOCK (priv);
      priv->drop_on_latency = g_value_get_boolean (value);
      JBUF_UNLOCK (priv);
      break;
    case PROP_TS_OFFSET:
      JBUF_LOCK (priv);
      priv->ts_offset = g_value_get_int64 (value);
      priv->ts_discont = TRUE;
      JBUF_UNLOCK (priv);
      break;
    case PROP_DO_LOST:
      JBUF_LOCK (priv);
      priv->do_lost = g_value_get_boolean (value);
      JBUF_UNLOCK (priv);
      break;
    case PROP_MODE:
      JBUF_LOCK (priv);
      rtp_jitter_buffer_set_mode (priv->jbuf, g_value_get_enum (value));
      JBUF_UNLOCK (priv);
      break;
    case PROP_DO_RETRANSMISSION:
      JBUF_LOCK (priv);
      priv->do_retransmission = g_value_get_boolean (value);
      JBUF_UNLOCK (priv);
      break;
    case PROP_RTX_DELAY:
      JBUF_LOCK (priv);
      priv->rtx_delay = g_value_get_int (value);
      JBUF_UNLOCK (priv);
      break;
    case PROP_RTX_MIN_DELAY:
      JBUF_LOCK (priv);
      priv->rtx_min_delay = g_value_get_uint (value);
      JBUF_UNLOCK (priv);
      break;
    case PROP_RTX_DELAY_REORDER:
      JBUF_LOCK (priv);
      priv->rtx_delay_reorder = g_value_get_int (value);
      JBUF_UNLOCK (priv);
      break;
    case PROP_RTX_RETRY_TIMEOUT:
      JBUF_LOCK (priv);
      priv->rtx_retry_timeout = g_value_get_int (value);
      JBUF_UNLOCK (priv);
      break;
    case PROP_RTX_MIN_RETRY_TIMEOUT:
      JBUF_LOCK (priv);
      priv->rtx_min_retry_timeout = g_value_get_int (value);
      JBUF_UNLOCK (priv);
      break;
    case PROP_RTX_RETRY_PERIOD:
      JBUF_LOCK (priv);
      priv->rtx_retry_period = g_value_get_int (value);
      JBUF_UNLOCK (priv);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtp_jitter_buffer_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstRtpJitterBuffer *jitterbuffer;
  GstRtpJitterBufferPrivate *priv;

  jitterbuffer = GST_RTP_JITTER_BUFFER (object);
  priv = jitterbuffer->priv;

  switch (prop_id) {
    case PROP_LATENCY:
      JBUF_LOCK (priv);
      g_value_set_uint (value, priv->latency_ms);
      JBUF_UNLOCK (priv);
      break;
    case PROP_DROP_ON_LATENCY:
      JBUF_LOCK (priv);
      g_value_set_boolean (value, priv->drop_on_latency);
      JBUF_UNLOCK (priv);
      break;
    case PROP_TS_OFFSET:
      JBUF_LOCK (priv);
      g_value_set_int64 (value, priv->ts_offset);
      JBUF_UNLOCK (priv);
      break;
    case PROP_DO_LOST:
      JBUF_LOCK (priv);
      g_value_set_boolean (value, priv->do_lost);
      JBUF_UNLOCK (priv);
      break;
    case PROP_MODE:
      JBUF_LOCK (priv);
      g_value_set_enum (value, rtp_jitter_buffer_get_mode (priv->jbuf));
      JBUF_UNLOCK (priv);
      break;
    case PROP_PERCENT:
    {
      gint percent;

      JBUF_LOCK (priv);
      if (priv->srcresult != GST_FLOW_OK)
        percent = 100;
      else
        percent = rtp_jitter_buffer_get_percent (priv->jbuf);

      g_value_set_int (value, percent);
      JBUF_UNLOCK (priv);
      break;
    }
    case PROP_DO_RETRANSMISSION:
      JBUF_LOCK (priv);
      g_value_set_boolean (value, priv->do_retransmission);
      JBUF_UNLOCK (priv);
      break;
    case PROP_RTX_DELAY:
      JBUF_LOCK (priv);
      g_value_set_int (value, priv->rtx_delay);
      JBUF_UNLOCK (priv);
      break;
    case PROP_RTX_MIN_DELAY:
      JBUF_LOCK (priv);
      g_value_set_uint (value, priv->rtx_min_delay);
      JBUF_UNLOCK (priv);
      break;
    case PROP_RTX_DELAY_REORDER:
      JBUF_LOCK (priv);
      g_value_set_int (value, priv->rtx_delay_reorder);
      JBUF_UNLOCK (priv);
      break;
    case PROP_RTX_RETRY_TIMEOUT:
      JBUF_LOCK (priv);
      g_value_set_int (value, priv->rtx_retry_timeout);
      JBUF_UNLOCK (priv);
      break;
    case PROP_RTX_MIN_RETRY_TIMEOUT:
      JBUF_LOCK (priv);
      g_value_set_int (value, priv->rtx_min_retry_timeout);
      JBUF_UNLOCK (priv);
      break;
    case PROP_RTX_RETRY_PERIOD:
      JBUF_LOCK (priv);
      g_value_set_int (value, priv->rtx_retry_period);
      JBUF_UNLOCK (priv);
      break;
    case PROP_STATS:
      g_value_take_boxed (value,
          gst_rtp_jitter_buffer_create_stats (jitterbuffer));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStructure *
gst_rtp_jitter_buffer_create_stats (GstRtpJitterBuffer * jbuf)
{
  GstStructure *s;

  JBUF_LOCK (jbuf->priv);
  s = gst_structure_new ("application/x-rtp-jitterbuffer-stats",
      "rtx-count", G_TYPE_UINT64, jbuf->priv->num_rtx_requests,
      "rtx-success-count", G_TYPE_UINT64, jbuf->priv->num_rtx_success,
      "rtx-per-packet", G_TYPE_DOUBLE, jbuf->priv->avg_rtx_num,
      "rtx-rtt", G_TYPE_UINT64, jbuf->priv->avg_rtx_rtt, NULL);
  JBUF_UNLOCK (jbuf->priv);

  return s;
}
