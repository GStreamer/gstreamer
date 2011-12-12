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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 */

/**
 * SECTION:element-gstrtpjitterbuffer
 *
 * This element reorders and removes duplicate RTP packets as they are received
 * from a network source. It will also wait for missing packets up to a
 * configurable time limit using the #GstRtpJitterBuffer:latency property.
 * Packets arriving too late are considered to be lost packets.
 *
 * This element acts as a live element and so adds #GstRtpJitterBuffer:latency
 * to the pipeline.
 *
 * The element needs the clock-rate of the RTP payload in order to estimate the
 * delay. This information is obtained either from the caps on the sink pad or,
 * when no caps are present, from the #GstRtpJitterBuffer::request-pt-map signal.
 * To clear the previous pt-map use the #GstRtpJitterBuffer::clear-pt-map signal.
 *
 * This element will automatically be used inside gstrtpbin.
 *
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * gst-launch rtspsrc location=rtsp://192.168.1.133:8554/mpeg1or2AudioVideoTest ! gstrtpjitterbuffer ! rtpmpvdepay ! mpeg2dec ! xvimagesink
 * ]| Connect to a streaming server and decode the MPEG video. The jitterbuffer is
 * inserted into the pipeline to smooth out network jitter and to reorder the
 * out-of-order RTP packets.
 * </refsect2>
 *
 * Last reviewed on 2007-05-28 (0.10.5)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <gst/rtp/gstrtpbuffer.h>

#include "gstrtpbin-marshal.h"

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

#define DEFAULT_LATENCY_MS      200
#define DEFAULT_DROP_ON_LATENCY FALSE
#define DEFAULT_TS_OFFSET       0
#define DEFAULT_DO_LOST         FALSE
#define DEFAULT_MODE            RTP_JITTER_BUFFER_MODE_SLAVE
#define DEFAULT_PERCENT         0

enum
{
  PROP_0,
  PROP_LATENCY,
  PROP_DROP_ON_LATENCY,
  PROP_TS_OFFSET,
  PROP_DO_LOST,
  PROP_MODE,
  PROP_PERCENT,
  PROP_LAST
};

#define JBUF_LOCK(priv)   (g_mutex_lock ((priv)->jbuf_lock))

#define JBUF_LOCK_CHECK(priv,label) G_STMT_START {    \
  JBUF_LOCK (priv);                                   \
  if (G_UNLIKELY (priv->srcresult != GST_FLOW_OK))    \
    goto label;                                       \
} G_STMT_END

#define JBUF_UNLOCK(priv) (g_mutex_unlock ((priv)->jbuf_lock))
#define JBUF_WAIT(priv)   (g_cond_wait ((priv)->jbuf_cond, (priv)->jbuf_lock))

#define JBUF_WAIT_CHECK(priv,label) G_STMT_START {    \
  JBUF_WAIT(priv);                                    \
  if (G_UNLIKELY (priv->srcresult != GST_FLOW_OK))    \
    goto label;                                       \
} G_STMT_END

#define JBUF_SIGNAL(priv) (g_cond_signal ((priv)->jbuf_cond))

struct _GstRtpJitterBufferPrivate
{
  GstPad *sinkpad, *srcpad;
  GstPad *rtcpsinkpad;

  RTPJitterBuffer *jbuf;
  GMutex *jbuf_lock;
  GCond *jbuf_cond;
  gboolean waiting;
  gboolean discont;
  gboolean active;
  guint64 out_offset;

  /* properties */
  guint latency_ms;
  guint64 latency_ns;
  gboolean drop_on_latency;
  gint64 ts_offset;
  gboolean do_lost;

  /* the last seqnum we pushed out */
  guint32 last_popped_seqnum;
  /* the next expected seqnum we push */
  guint32 next_seqnum;
  /* last output time */
  GstClockTime last_out_time;
  /* the next expected seqnum we receive */
  guint32 next_in_seqnum;

  /* start and stop ranges */
  GstClockTime npt_start;
  GstClockTime npt_stop;
  guint64 ext_timestamp;
  guint64 last_elapsed;
  guint64 estimated_eos;
  GstClockID eos_id;
  gboolean reached_npt_stop;

  /* state */
  gboolean eos;

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
  gboolean unscheduled;
  /* the latency of the upstream peer, we have to take this into account when
   * synchronizing the buffers. */
  GstClockTime peer_latency;

  /* some accounting */
  guint64 num_late;
  guint64 num_duplicates;
};

#define GST_RTP_JITTER_BUFFER_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), GST_TYPE_RTP_JITTER_BUFFER, \
                                GstRtpJitterBufferPrivate))

static GstStaticPadTemplate gst_rtp_jitter_buffer_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "clock-rate = (int) [ 1, 2147483647 ]"
        /* "payload = (int) , "
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

GST_BOILERPLATE (GstRtpJitterBuffer, gst_rtp_jitter_buffer, GstElement,
    GST_TYPE_ELEMENT);

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
    GstPadTemplate * templ, const gchar * name);
static void gst_rtp_jitter_buffer_release_pad (GstElement * element,
    GstPad * pad);
static GstClock *gst_rtp_jitter_buffer_provide_clock (GstElement * element);

/* pad overrides */
static GstCaps *gst_rtp_jitter_buffer_getcaps (GstPad * pad);
static GstIterator *gst_rtp_jitter_buffer_iterate_internal_links (GstPad * pad);

/* sinkpad overrides */
static gboolean gst_jitter_buffer_sink_setcaps (GstPad * pad, GstCaps * caps);
static gboolean gst_rtp_jitter_buffer_sink_event (GstPad * pad,
    GstEvent * event);
static GstFlowReturn gst_rtp_jitter_buffer_chain (GstPad * pad,
    GstBuffer * buffer);

static gboolean gst_rtp_jitter_buffer_sink_rtcp_event (GstPad * pad,
    GstEvent * event);
static GstFlowReturn gst_rtp_jitter_buffer_chain_rtcp (GstPad * pad,
    GstBuffer * buffer);

/* srcpad overrides */
static gboolean gst_rtp_jitter_buffer_src_event (GstPad * pad,
    GstEvent * event);
static gboolean
gst_rtp_jitter_buffer_src_activate_push (GstPad * pad, gboolean active);
static void gst_rtp_jitter_buffer_loop (GstRtpJitterBuffer * jitterbuffer);
static gboolean gst_rtp_jitter_buffer_query (GstPad * pad, GstQuery * query);

static void
gst_rtp_jitter_buffer_clear_pt_map (GstRtpJitterBuffer * jitterbuffer);
static GstClockTime
gst_rtp_jitter_buffer_set_active (GstRtpJitterBuffer * jitterbuffer,
    gboolean active, guint64 base_time);

static void
gst_rtp_jitter_buffer_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class,
      &gst_rtp_jitter_buffer_src_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_rtp_jitter_buffer_sink_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_rtp_jitter_buffer_sink_rtcp_template);

  gst_element_class_set_details_simple (element_class,
      "RTP packet jitter-buffer", "Filter/Network/RTP",
      "A buffer that deals with network jitter and other transmission faults",
      "Philippe Kalaf <philippe.kalaf@collabora.co.uk>, "
      "Wim Taymans <wim.taymans@gmail.com>");
}

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
   * GstRtpJitterBuffer::latency:
   *
   * The maximum latency of the jitterbuffer. Packets will be kept in the buffer
   * for at most this time.
   */
  g_object_class_install_property (gobject_class, PROP_LATENCY,
      g_param_spec_uint ("latency", "Buffer latency in ms",
          "Amount of ms to buffer", 0, G_MAXUINT, DEFAULT_LATENCY_MS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstRtpJitterBuffer::drop-on-latency:
   *
   * Drop oldest buffers when the queue is completely filled.
   */
  g_object_class_install_property (gobject_class, PROP_DROP_ON_LATENCY,
      g_param_spec_boolean ("drop-on-latency",
          "Drop buffers when maximum latency is reached",
          "Tells the jitterbuffer to never exceed the given latency in size",
          DEFAULT_DROP_ON_LATENCY, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstRtpJitterBuffer::ts-offset:
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
   * GstRtpJitterBuffer::do-lost:
   *
   * Send out a GstRTPPacketLost event downstream when a packet is considered
   * lost.
   */
  g_object_class_install_property (gobject_class, PROP_DO_LOST,
      g_param_spec_boolean ("do-lost", "Do Lost",
          "Send an event downstream when a packet is lost", DEFAULT_DO_LOST,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstRtpJitterBuffer::mode:
   *
   * Control the buffering and timestamping mode used by the jitterbuffer.
   */
  g_object_class_install_property (gobject_class, PROP_MODE,
      g_param_spec_enum ("mode", "Mode",
          "Control the buffering algorithm in use", RTP_TYPE_JITTER_BUFFER_MODE,
          DEFAULT_MODE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstRtpJitterBuffer::percent:
   *
   * The percent of the jitterbuffer that is filled.
   *
   * Since: 0.10.19
   */
  g_object_class_install_property (gobject_class, PROP_PERCENT,
      g_param_spec_int ("percent", "percent",
          "The buffer filled percent", 0, 100,
          0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
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
          request_pt_map), NULL, NULL, gst_rtp_bin_marshal_BOXED__UINT,
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
   * GstRtpJitterBuffer::on-npt-stop
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
   *
   * Since: 0.10.19
   */
  gst_rtp_jitter_buffer_signals[SIGNAL_SET_ACTIVE] =
      g_signal_new ("set-active", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (GstRtpJitterBufferClass, set_active), NULL, NULL,
      gst_rtp_bin_marshal_UINT64__BOOL_UINT64, G_TYPE_UINT64, 2, G_TYPE_BOOLEAN,
      G_TYPE_UINT64);

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_rtp_jitter_buffer_change_state);
  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_rtp_jitter_buffer_request_new_pad);
  gstelement_class->release_pad =
      GST_DEBUG_FUNCPTR (gst_rtp_jitter_buffer_release_pad);
  gstelement_class->provide_clock =
      GST_DEBUG_FUNCPTR (gst_rtp_jitter_buffer_provide_clock);

  klass->clear_pt_map = GST_DEBUG_FUNCPTR (gst_rtp_jitter_buffer_clear_pt_map);
  klass->set_active = GST_DEBUG_FUNCPTR (gst_rtp_jitter_buffer_set_active);

  GST_DEBUG_CATEGORY_INIT
      (rtpjitterbuffer_debug, "gstrtpjitterbuffer", 0, "RTP Jitter Buffer");
}

static void
gst_rtp_jitter_buffer_init (GstRtpJitterBuffer * jitterbuffer,
    GstRtpJitterBufferClass * klass)
{
  GstRtpJitterBufferPrivate *priv;

  priv = GST_RTP_JITTER_BUFFER_GET_PRIVATE (jitterbuffer);
  jitterbuffer->priv = priv;

  priv->latency_ms = DEFAULT_LATENCY_MS;
  priv->latency_ns = priv->latency_ms * GST_MSECOND;
  priv->drop_on_latency = DEFAULT_DROP_ON_LATENCY;
  priv->do_lost = DEFAULT_DO_LOST;

  priv->jbuf = rtp_jitter_buffer_new ();
  priv->jbuf_lock = g_mutex_new ();
  priv->jbuf_cond = g_cond_new ();

  /* reset skew detection initialy */
  rtp_jitter_buffer_reset_skew (priv->jbuf);
  rtp_jitter_buffer_set_delay (priv->jbuf, priv->latency_ns);
  rtp_jitter_buffer_set_buffering (priv->jbuf, FALSE);
  priv->active = TRUE;

  priv->srcpad =
      gst_pad_new_from_static_template (&gst_rtp_jitter_buffer_src_template,
      "src");

  gst_pad_set_activatepush_function (priv->srcpad,
      GST_DEBUG_FUNCPTR (gst_rtp_jitter_buffer_src_activate_push));
  gst_pad_set_query_function (priv->srcpad,
      GST_DEBUG_FUNCPTR (gst_rtp_jitter_buffer_query));
  gst_pad_set_getcaps_function (priv->srcpad,
      GST_DEBUG_FUNCPTR (gst_rtp_jitter_buffer_getcaps));
  gst_pad_set_event_function (priv->srcpad,
      GST_DEBUG_FUNCPTR (gst_rtp_jitter_buffer_src_event));

  priv->sinkpad =
      gst_pad_new_from_static_template (&gst_rtp_jitter_buffer_sink_template,
      "sink");

  gst_pad_set_chain_function (priv->sinkpad,
      GST_DEBUG_FUNCPTR (gst_rtp_jitter_buffer_chain));
  gst_pad_set_event_function (priv->sinkpad,
      GST_DEBUG_FUNCPTR (gst_rtp_jitter_buffer_sink_event));
  gst_pad_set_setcaps_function (priv->sinkpad,
      GST_DEBUG_FUNCPTR (gst_jitter_buffer_sink_setcaps));
  gst_pad_set_getcaps_function (priv->sinkpad,
      GST_DEBUG_FUNCPTR (gst_rtp_jitter_buffer_getcaps));

  gst_element_add_pad (GST_ELEMENT (jitterbuffer), priv->srcpad);
  gst_element_add_pad (GST_ELEMENT (jitterbuffer), priv->sinkpad);
}

static void
gst_rtp_jitter_buffer_finalize (GObject * object)
{
  GstRtpJitterBuffer *jitterbuffer;

  jitterbuffer = GST_RTP_JITTER_BUFFER (object);

  g_mutex_free (jitterbuffer->priv->jbuf_lock);
  g_cond_free (jitterbuffer->priv->jbuf_cond);

  g_object_unref (jitterbuffer->priv->jbuf);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstIterator *
gst_rtp_jitter_buffer_iterate_internal_links (GstPad * pad)
{
  GstRtpJitterBuffer *jitterbuffer;
  GstPad *otherpad = NULL;
  GstIterator *it;

  jitterbuffer = GST_RTP_JITTER_BUFFER (gst_pad_get_parent (pad));

  if (pad == jitterbuffer->priv->sinkpad) {
    otherpad = jitterbuffer->priv->srcpad;
  } else if (pad == jitterbuffer->priv->srcpad) {
    otherpad = jitterbuffer->priv->sinkpad;
  } else if (pad == jitterbuffer->priv->rtcpsinkpad) {
    otherpad = NULL;
  }

  it = gst_iterator_new_single (GST_TYPE_PAD, otherpad,
      (GstCopyFunction) gst_object_ref, (GFreeFunc) gst_object_unref);

  gst_object_unref (jitterbuffer);

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
    GstPadTemplate * templ, const gchar * name)
{
  GstRtpJitterBuffer *jitterbuffer;
  GstElementClass *klass;
  GstPad *result;
  GstRtpJitterBufferPrivate *priv;

  g_return_val_if_fail (templ != NULL, NULL);
  g_return_val_if_fail (GST_IS_RTP_JITTER_BUFFER (element), NULL);

  jitterbuffer = GST_RTP_JITTER_BUFFER (element);
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
    g_warning ("gstrtpjitterbuffer: this is not our template");
    return NULL;
  }
exists:
  {
    g_warning ("gstrtpjitterbuffer: pad already requested");
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

  jitterbuffer = GST_RTP_JITTER_BUFFER (element);
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
  priv->last_popped_seqnum = -1;
  priv->next_seqnum = -1;
  JBUF_UNLOCK (priv);
}

static GstClockTime
gst_rtp_jitter_buffer_set_active (GstRtpJitterBuffer * jbuf, gboolean active,
    guint64 offset)
{
  GstRtpJitterBufferPrivate *priv;
  GstClockTime last_out;
  GstBuffer *head;

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
    JBUF_SIGNAL (priv);
  }
  if (!active) {
    rtp_jitter_buffer_set_buffering (priv->jbuf, TRUE);
  }
  if ((head = rtp_jitter_buffer_peek (priv->jbuf))) {
    /* head buffer timestamp and offset gives our output time */
    last_out = GST_BUFFER_TIMESTAMP (head) + priv->ts_offset;
  } else {
    /* use last known time when the buffer is empty */
    last_out = priv->last_out_time;
  }
  JBUF_UNLOCK (priv);

  return last_out;
}

static GstCaps *
gst_rtp_jitter_buffer_getcaps (GstPad * pad)
{
  GstRtpJitterBuffer *jitterbuffer;
  GstRtpJitterBufferPrivate *priv;
  GstPad *other;
  GstCaps *caps;
  const GstCaps *templ;

  jitterbuffer = GST_RTP_JITTER_BUFFER (gst_pad_get_parent (pad));
  priv = jitterbuffer->priv;

  other = (pad == priv->srcpad ? priv->sinkpad : priv->srcpad);

  caps = gst_pad_peer_get_caps (other);

  templ = gst_pad_get_pad_template_caps (pad);
  if (caps == NULL) {
    GST_DEBUG_OBJECT (jitterbuffer, "copy template");
    caps = gst_caps_copy (templ);
  } else {
    GstCaps *intersect;

    GST_DEBUG_OBJECT (jitterbuffer, "intersect with template");

    intersect = gst_caps_intersect (caps, templ);
    gst_caps_unref (caps);

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
    if (priv->next_seqnum == -1)
      priv->next_seqnum = val;
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

static gboolean
gst_jitter_buffer_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstRtpJitterBuffer *jitterbuffer;
  GstRtpJitterBufferPrivate *priv;
  gboolean res;

  jitterbuffer = GST_RTP_JITTER_BUFFER (gst_pad_get_parent (pad));
  priv = jitterbuffer->priv;

  JBUF_LOCK (priv);
  res = gst_jitter_buffer_sink_parse_caps (jitterbuffer, caps);
  JBUF_UNLOCK (priv);

  /* set same caps on srcpad on success */
  if (res)
    gst_pad_set_caps (priv->srcpad, caps);

  gst_object_unref (jitterbuffer);

  return res;
}

static void
gst_rtp_jitter_buffer_flush_start (GstRtpJitterBuffer * jitterbuffer)
{
  GstRtpJitterBufferPrivate *priv;

  priv = jitterbuffer->priv;

  JBUF_LOCK (priv);
  /* mark ourselves as flushing */
  priv->srcresult = GST_FLOW_WRONG_STATE;
  GST_DEBUG_OBJECT (jitterbuffer, "Disabling pop on queue");
  /* this unblocks any waiting pops on the src pad task */
  JBUF_SIGNAL (priv);
  /* unlock clock, we just unschedule, the entry will be released by the
   * locking streaming thread. */
  if (priv->clock_id) {
    gst_clock_id_unschedule (priv->clock_id);
    priv->unscheduled = TRUE;
  }
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
  priv->next_in_seqnum = -1;
  priv->clock_rate = -1;
  priv->eos = FALSE;
  priv->estimated_eos = -1;
  priv->last_elapsed = 0;
  priv->reached_npt_stop = FALSE;
  priv->ext_timestamp = -1;
  GST_DEBUG_OBJECT (jitterbuffer, "flush and reset jitterbuffer");
  rtp_jitter_buffer_flush (priv->jbuf);
  rtp_jitter_buffer_reset_skew (priv->jbuf);
  JBUF_UNLOCK (priv);
}

static gboolean
gst_rtp_jitter_buffer_src_activate_push (GstPad * pad, gboolean active)
{
  gboolean result = TRUE;
  GstRtpJitterBuffer *jitterbuffer = NULL;

  jitterbuffer = GST_RTP_JITTER_BUFFER (gst_pad_get_parent (pad));

  if (active) {
    /* allow data processing */
    gst_rtp_jitter_buffer_flush_stop (jitterbuffer);

    /* start pushing out buffers */
    GST_DEBUG_OBJECT (jitterbuffer, "Starting task on srcpad");
    gst_pad_start_task (jitterbuffer->priv->srcpad,
        (GstTaskFunction) gst_rtp_jitter_buffer_loop, jitterbuffer);
  } else {
    /* make sure all data processing stops ASAP */
    gst_rtp_jitter_buffer_flush_start (jitterbuffer);

    /* NOTE this will hardlock if the state change is called from the src pad
     * task thread because we will _join() the thread. */
    GST_DEBUG_OBJECT (jitterbuffer, "Stopping task on srcpad");
    result = gst_pad_stop_task (pad);
  }

  gst_object_unref (jitterbuffer);

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
      JBUF_UNLOCK (priv);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      JBUF_LOCK (priv);
      /* unblock to allow streaming in PLAYING */
      priv->blocked = FALSE;
      JBUF_SIGNAL (priv);
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
      JBUF_UNLOCK (priv);
      if (ret != GST_STATE_CHANGE_FAILURE)
        ret = GST_STATE_CHANGE_NO_PREROLL;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}

static gboolean
gst_rtp_jitter_buffer_src_event (GstPad * pad, GstEvent * event)
{
  gboolean ret = TRUE;
  GstRtpJitterBuffer *jitterbuffer;
  GstRtpJitterBufferPrivate *priv;

  jitterbuffer = GST_RTP_JITTER_BUFFER (gst_pad_get_parent (pad));
  if (G_UNLIKELY (jitterbuffer == NULL)) {
    gst_event_unref (event);
    return FALSE;
  }
  priv = jitterbuffer->priv;

  GST_DEBUG_OBJECT (jitterbuffer, "received %s", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_LATENCY:
    {
      GstClockTime latency;

      gst_event_parse_latency (event, &latency);

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
  gst_object_unref (jitterbuffer);

  return ret;
}

static gboolean
gst_rtp_jitter_buffer_sink_event (GstPad * pad, GstEvent * event)
{
  gboolean ret = TRUE;
  GstRtpJitterBuffer *jitterbuffer;
  GstRtpJitterBufferPrivate *priv;

  jitterbuffer = GST_RTP_JITTER_BUFFER (gst_pad_get_parent (pad));
  if (G_UNLIKELY (jitterbuffer == NULL)) {
    gst_event_unref (event);
    return FALSE;
  }
  priv = jitterbuffer->priv;

  GST_DEBUG_OBJECT (jitterbuffer, "received %s", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:
    {
      GstFormat format;
      gdouble rate, arate;
      gint64 start, stop, time;
      gboolean update;

      gst_event_parse_new_segment_full (event, &update, &rate, &arate, &format,
          &start, &stop, &time);

      /* we need time for now */
      if (format != GST_FORMAT_TIME)
        goto newseg_wrong_format;

      GST_DEBUG_OBJECT (jitterbuffer,
          "newsegment: update %d, rate %g, arate %g, start %" GST_TIME_FORMAT
          ", stop %" GST_TIME_FORMAT ", time %" GST_TIME_FORMAT,
          update, rate, arate, GST_TIME_ARGS (start), GST_TIME_ARGS (stop),
          GST_TIME_ARGS (time));

      /* now configure the values, we need these to time the release of the
       * buffers on the srcpad. */
      gst_segment_set_newsegment_full (&priv->segment, update,
          rate, arate, format, start, stop, time);

      /* FIXME, push SEGMENT in the queue. Sorting order might be difficult. */
      ret = gst_pad_push_event (priv->srcpad, event);
      break;
    }
    case GST_EVENT_FLUSH_START:
      gst_rtp_jitter_buffer_flush_start (jitterbuffer);
      ret = gst_pad_push_event (priv->srcpad, event);
      break;
    case GST_EVENT_FLUSH_STOP:
      ret = gst_pad_push_event (priv->srcpad, event);
      ret = gst_rtp_jitter_buffer_src_activate_push (priv->srcpad, TRUE);
      break;
    case GST_EVENT_EOS:
    {
      /* push EOS in queue. We always push it at the head */
      JBUF_LOCK (priv);
      /* check for flushing, we need to discard the event and return FALSE when
       * we are flushing */
      ret = priv->srcresult == GST_FLOW_OK;
      if (ret && !priv->eos) {
        GST_INFO_OBJECT (jitterbuffer, "queuing EOS");
        priv->eos = TRUE;
        JBUF_SIGNAL (priv);
      } else if (priv->eos) {
        GST_DEBUG_OBJECT (jitterbuffer, "dropping EOS, we are already EOS");
      } else {
        GST_DEBUG_OBJECT (jitterbuffer, "dropping EOS, reason %s",
            gst_flow_get_name (priv->srcresult));
      }
      JBUF_UNLOCK (priv);
      gst_event_unref (event);
      break;
    }
    default:
      ret = gst_pad_push_event (priv->srcpad, event);
      break;
  }

done:
  gst_object_unref (jitterbuffer);

  return ret;

  /* ERRORS */
newseg_wrong_format:
  {
    GST_DEBUG_OBJECT (jitterbuffer, "received non TIME newsegment");
    ret = FALSE;
    gst_event_unref (event);
    goto done;
  }
}

static gboolean
gst_rtp_jitter_buffer_sink_rtcp_event (GstPad * pad, GstEvent * event)
{
  GstRtpJitterBuffer *jitterbuffer;

  jitterbuffer = GST_RTP_JITTER_BUFFER (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (jitterbuffer, "received %s", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      break;
    case GST_EVENT_FLUSH_STOP:
      break;
    default:
      break;
  }
  gst_event_unref (event);
  gst_object_unref (jitterbuffer);

  return TRUE;
}

/*
 * Must be called with JBUF_LOCK held, will release the LOCK when emiting the
 * signal. The function returns GST_FLOW_ERROR when a parsing error happened and
 * GST_FLOW_WRONG_STATE when the element is shutting down. On success
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
    return GST_FLOW_WRONG_STATE;
  }
parse_failed:
  {
    GST_DEBUG_OBJECT (jitterbuffer, "parse failed");
    return GST_FLOW_ERROR;
  }
}

/* call with jbuf lock held */
static void
check_buffering_percent (GstRtpJitterBuffer * jitterbuffer, gint * percent)
{
  GstRtpJitterBufferPrivate *priv = jitterbuffer->priv;

  /* too short a stream, or too close to EOS will never really fill buffer */
  if (*percent != -1 && priv->npt_stop != -1 &&
      priv->npt_stop - priv->npt_start <=
      rtp_jitter_buffer_get_delay (priv->jbuf)) {
    GST_DEBUG_OBJECT (jitterbuffer, "short stream; faking full buffer");
    rtp_jitter_buffer_set_buffering (priv->jbuf, FALSE);
    *percent = 100;
  }
}

static void
post_buffering_percent (GstRtpJitterBuffer * jitterbuffer, gint percent)
{
  GstMessage *message;

  /* Post a buffering message */
  message = gst_message_new_buffering (GST_OBJECT_CAST (jitterbuffer), percent);
  gst_message_set_buffering_stats (message, GST_BUFFERING_LIVE, -1, -1, -1);

  gst_element_post_message (GST_ELEMENT_CAST (jitterbuffer), message);
}

static GstFlowReturn
gst_rtp_jitter_buffer_chain (GstPad * pad, GstBuffer * buffer)
{
  GstRtpJitterBuffer *jitterbuffer;
  GstRtpJitterBufferPrivate *priv;
  guint16 seqnum;
  GstFlowReturn ret = GST_FLOW_OK;
  GstClockTime timestamp;
  guint64 latency_ts;
  gboolean tail;
  gint percent = -1;
  guint8 pt;

  jitterbuffer = GST_RTP_JITTER_BUFFER (gst_pad_get_parent (pad));

  if (G_UNLIKELY (!gst_rtp_buffer_validate (buffer)))
    goto invalid_buffer;

  priv = jitterbuffer->priv;

  pt = gst_rtp_buffer_get_payload_type (buffer);

  /* take the timestamp of the buffer. This is the time when the packet was
   * received and is used to calculate jitter and clock skew. We will adjust
   * this timestamp with the smoothed value after processing it in the
   * jitterbuffer. */
  timestamp = GST_BUFFER_TIMESTAMP (buffer);
  /* bring to running time */
  timestamp = gst_segment_to_running_time (&priv->segment, GST_FORMAT_TIME,
      timestamp);

  seqnum = gst_rtp_buffer_get_seq (buffer);

  GST_DEBUG_OBJECT (jitterbuffer,
      "Received packet #%d at time %" GST_TIME_FORMAT, seqnum,
      GST_TIME_ARGS (timestamp));

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
    if ((caps = GST_BUFFER_CAPS (buffer))) {
      gst_jitter_buffer_sink_parse_caps (jitterbuffer, caps);
    }
  }

  if (G_UNLIKELY (priv->clock_rate == -1)) {
    /* no clock rate given on the caps, try to get one with the signal */
    if (gst_rtp_jitter_buffer_get_clock_rate (jitterbuffer,
            pt) == GST_FLOW_WRONG_STATE)
      goto out_flushing;

    if (G_UNLIKELY (priv->clock_rate == -1))
      goto no_clock_rate;
  }

  /* don't accept more data on EOS */
  if (G_UNLIKELY (priv->eos))
    goto have_eos;

  /* now check against our expected seqnum */
  if (G_LIKELY (priv->next_in_seqnum != -1)) {
    gint gap;
    gboolean reset = FALSE;

    gap = gst_rtp_buffer_compare_seqnum (priv->next_in_seqnum, seqnum);
    if (G_UNLIKELY (gap != 0)) {
      GST_DEBUG_OBJECT (jitterbuffer, "expected #%d, got #%d, gap of %d",
          priv->next_in_seqnum, seqnum, gap);
      /* priv->next_in_seqnum >= seqnum, this packet is too late or the
       * sender might have been restarted with different seqnum. */
      if (gap < -RTP_MAX_MISORDER) {
        GST_DEBUG_OBJECT (jitterbuffer, "reset: buffer too old %d", gap);
        reset = TRUE;
      }
      /* priv->next_in_seqnum < seqnum, this is a new packet */
      else if (G_UNLIKELY (gap > RTP_MAX_DROPOUT)) {
        GST_DEBUG_OBJECT (jitterbuffer, "reset: too many dropped packets %d",
            gap);
        reset = TRUE;
      } else {
        GST_DEBUG_OBJECT (jitterbuffer, "tolerable gap");
      }
    }
    if (G_UNLIKELY (reset)) {
      GST_DEBUG_OBJECT (jitterbuffer, "flush and reset jitterbuffer");
      rtp_jitter_buffer_flush (priv->jbuf);
      rtp_jitter_buffer_reset_skew (priv->jbuf);
      priv->last_popped_seqnum = -1;
      priv->next_seqnum = seqnum;
    }
  }
  priv->next_in_seqnum = (seqnum + 1) & 0xffff;

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
      GstBuffer *old_buf;

      old_buf = rtp_jitter_buffer_pop (priv->jbuf, &percent);

      GST_DEBUG_OBJECT (jitterbuffer, "Queue full, dropping old packet #%d",
          gst_rtp_buffer_get_seq (old_buf));

      gst_buffer_unref (old_buf);
    }
  }

  /* we need to make the metadata writable before pushing it in the jitterbuffer
   * because the jitterbuffer will update the timestamp */
  buffer = gst_buffer_make_metadata_writable (buffer);

  /* now insert the packet into the queue in sorted order. This function returns
   * FALSE if a packet with the same seqnum was already in the queue, meaning we
   * have a duplicate. */
  if (G_UNLIKELY (!rtp_jitter_buffer_insert (priv->jbuf, buffer, timestamp,
              priv->clock_rate, &tail, &percent)))
    goto duplicate;

  /* signal addition of new buffer when the _loop is waiting. */
  if (priv->waiting)
    JBUF_SIGNAL (priv);

  /* let's unschedule and unblock any waiting buffers. We only want to do this
   * when the tail buffer changed */
  if (G_UNLIKELY (priv->clock_id && tail)) {
    GST_DEBUG_OBJECT (jitterbuffer,
        "Unscheduling waiting buffer, new tail buffer");
    gst_clock_id_unschedule (priv->clock_id);
    priv->unscheduled = TRUE;
  }

  GST_DEBUG_OBJECT (jitterbuffer, "Pushed packet #%d, now %d packets, tail: %d",
      seqnum, rtp_jitter_buffer_num_packets (priv->jbuf), tail);

  check_buffering_percent (jitterbuffer, &percent);

finished:
  JBUF_UNLOCK (priv);

  if (percent != -1)
    post_buffering_percent (jitterbuffer, percent);

  gst_object_unref (jitterbuffer);

  return ret;

  /* ERRORS */
invalid_buffer:
  {
    /* this is not fatal but should be filtered earlier */
    GST_ELEMENT_WARNING (jitterbuffer, STREAM, DECODE, (NULL),
        ("Received invalid RTP payload, dropping"));
    gst_buffer_unref (buffer);
    gst_object_unref (jitterbuffer);
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
    ret = GST_FLOW_UNEXPECTED;
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
    gst_buffer_unref (buffer);
    goto finished;
  }
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

static GstClockTime
get_sync_time (GstRtpJitterBuffer * jitterbuffer, GstClockTime timestamp)
{
  GstClockTime result;
  GstRtpJitterBufferPrivate *priv;

  priv = jitterbuffer->priv;

  result = timestamp + GST_ELEMENT_CAST (jitterbuffer)->base_time;
  /* add latency, this includes our own latency and the peer latency. */
  result += priv->latency_ns;
  result += priv->peer_latency;

  return result;
}

static gboolean
eos_reached (GstClock * clock, GstClockTime time, GstClockID id,
    GstRtpJitterBuffer * jitterbuffer)
{
  GstRtpJitterBufferPrivate *priv;

  priv = jitterbuffer->priv;

  JBUF_LOCK_CHECK (priv, flushing);
  if (priv->waiting) {
    GST_INFO_OBJECT (jitterbuffer, "got the NPT timeout");
    priv->reached_npt_stop = TRUE;
    JBUF_SIGNAL (priv);
  }
  JBUF_UNLOCK (priv);

  return TRUE;

  /* ERRORS */
flushing:
  {
    JBUF_UNLOCK (priv);
    return FALSE;
  }
}

static GstClockTime
compute_elapsed (GstRtpJitterBuffer * jitterbuffer, GstBuffer * outbuf)
{
  guint64 ext_time, elapsed;
  guint32 rtp_time;
  GstRtpJitterBufferPrivate *priv;

  priv = jitterbuffer->priv;
  rtp_time = gst_rtp_buffer_get_timestamp (outbuf);

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

/*
 * This funcion will push out buffers on the source pad.
 *
 * For each pushed buffer, the seqnum is recorded, if the next buffer B has a
 * different seqnum (missing packets before B), this function will wait for the
 * missing packet to arrive up to the timestamp of buffer B.
 */
static void
gst_rtp_jitter_buffer_loop (GstRtpJitterBuffer * jitterbuffer)
{
  GstRtpJitterBufferPrivate *priv;
  GstBuffer *outbuf;
  GstFlowReturn result;
  guint16 seqnum;
  guint32 next_seqnum;
  GstClockTime timestamp, out_time;
  gboolean discont = FALSE;
  gint gap;
  GstClock *clock;
  GstClockID id;
  GstClockTime sync_time;
  gint percent = -1;

  priv = jitterbuffer->priv;

  JBUF_LOCK_CHECK (priv, flushing);
again:
  GST_DEBUG_OBJECT (jitterbuffer, "Peeking item");
  while (TRUE) {
    id = NULL;
    /* always wait if we are blocked */
    if (G_LIKELY (!priv->blocked)) {
      /* we're buffering but not EOS, wait. */
      if (!priv->eos && (!priv->active
              || rtp_jitter_buffer_is_buffering (priv->jbuf))) {
        GstClockTime elapsed, delay, left;

        if (priv->estimated_eos == -1)
          goto do_wait;

        outbuf = rtp_jitter_buffer_peek (priv->jbuf);
        if (outbuf != NULL) {
          elapsed = compute_elapsed (jitterbuffer, outbuf);
          if (GST_BUFFER_DURATION_IS_VALID (outbuf))
            elapsed += GST_BUFFER_DURATION (outbuf);
        } else {
          GST_INFO_OBJECT (jitterbuffer, "no buffer, using last_elapsed");
          elapsed = priv->last_elapsed;
        }

        delay = rtp_jitter_buffer_get_delay (priv->jbuf);

        if (priv->estimated_eos > elapsed)
          left = priv->estimated_eos - elapsed;
        else
          left = 0;

        GST_INFO_OBJECT (jitterbuffer, "buffering, elapsed %" GST_TIME_FORMAT
            " estimated_eos %" GST_TIME_FORMAT " left %" GST_TIME_FORMAT
            " delay %" GST_TIME_FORMAT,
            GST_TIME_ARGS (elapsed), GST_TIME_ARGS (priv->estimated_eos),
            GST_TIME_ARGS (left), GST_TIME_ARGS (delay));
        if (left > delay)
          goto do_wait;
      }
      /* if we have a packet, we can exit the loop and grab it */
      if (rtp_jitter_buffer_num_packets (priv->jbuf) > 0)
        break;
      /* no packets but we are EOS, do eos logic */
      if (G_UNLIKELY (priv->eos))
        goto do_eos;
      /* underrun, wait for packets or flushing now if we are expecting an EOS
       * timeout, set the async timer for it too */
      if (priv->estimated_eos != -1 && !priv->reached_npt_stop) {
        sync_time = get_sync_time (jitterbuffer, priv->estimated_eos);

        GST_OBJECT_LOCK (jitterbuffer);
        clock = GST_ELEMENT_CLOCK (jitterbuffer);
        if (clock) {
          GST_INFO_OBJECT (jitterbuffer, "scheduling timeout");
          id = gst_clock_new_single_shot_id (clock, sync_time);
          gst_clock_id_wait_async (id, (GstClockCallback) eos_reached,
              jitterbuffer);
        }
        GST_OBJECT_UNLOCK (jitterbuffer);
      }
    }
  do_wait:
    /* now we wait */
    GST_DEBUG_OBJECT (jitterbuffer, "waiting");
    priv->waiting = TRUE;
    JBUF_WAIT (priv);
    priv->waiting = FALSE;
    GST_DEBUG_OBJECT (jitterbuffer, "waiting done");

    if (id) {
      /* unschedule any pending async notifications we might have */
      gst_clock_id_unschedule (id);
      gst_clock_id_unref (id);
    }
    if (G_UNLIKELY (priv->srcresult != GST_FLOW_OK))
      goto flushing;

    if (id && priv->reached_npt_stop) {
      goto do_npt_stop;
    }
  }

  /* peek a buffer, we're just looking at the timestamp and the sequence number.
   * If all is fine, we'll pop and push it. If the sequence number is wrong we
   * wait on the timestamp. In the chain function we will unlock the wait when a
   * new buffer is available. The peeked buffer is valid for as long as we hold
   * the jitterbuffer lock. */
  outbuf = rtp_jitter_buffer_peek (priv->jbuf);

  /* get the seqnum and the next expected seqnum */
  seqnum = gst_rtp_buffer_get_seq (outbuf);
  next_seqnum = priv->next_seqnum;

  /* get the timestamp, this is already corrected for clock skew by the
   * jitterbuffer */
  timestamp = GST_BUFFER_TIMESTAMP (outbuf);

  GST_DEBUG_OBJECT (jitterbuffer,
      "Peeked buffer #%d, expect #%d, timestamp %" GST_TIME_FORMAT
      ", now %d left", seqnum, next_seqnum, GST_TIME_ARGS (timestamp),
      rtp_jitter_buffer_num_packets (priv->jbuf));

  /* apply our timestamp offset to the incomming buffer, this will be our output
   * timestamp. */
  out_time = apply_offset (jitterbuffer, timestamp);

  /* get the gap between this and the previous packet. If we don't know the
   * previous packet seqnum assume no gap. */
  if (G_LIKELY (next_seqnum != -1)) {
    gap = gst_rtp_buffer_compare_seqnum (next_seqnum, seqnum);

    /* if we have a packet that we already pushed or considered dropped, pop it
     * off and get the next packet */
    if (G_UNLIKELY (gap < 0)) {
      GST_DEBUG_OBJECT (jitterbuffer, "Old packet #%d, next #%d dropping",
          seqnum, next_seqnum);
      outbuf = rtp_jitter_buffer_pop (priv->jbuf, &percent);
      gst_buffer_unref (outbuf);
      goto again;
    }
  } else {
    GST_DEBUG_OBJECT (jitterbuffer, "no next seqnum known, first packet");
    gap = -1;
  }

  /* If we don't know what the next seqnum should be (== -1) we have to wait
   * because it might be possible that we are not receiving this buffer in-order,
   * a buffer with a lower seqnum could arrive later and we want to push that
   * earlier buffer before this buffer then.
   * If we know the expected seqnum, we can compare it to the current seqnum to
   * determine if we have missing a packet. If we have a missing packet (which
   * must be before this packet) we can wait for it until the deadline for this
   * packet expires. */
  if (G_UNLIKELY (gap != 0 && out_time != -1)) {
    GstClockReturn ret;
    GstClockTime duration = GST_CLOCK_TIME_NONE;

    if (gap > 0) {
      /* we have a gap */
      GST_DEBUG_OBJECT (jitterbuffer,
          "Sequence number GAP detected: expected %d instead of %d (%d missing)",
          next_seqnum, seqnum, gap);

      if (priv->last_out_time != -1) {
        GST_DEBUG_OBJECT (jitterbuffer,
            "out_time %" GST_TIME_FORMAT ", last %" GST_TIME_FORMAT,
            GST_TIME_ARGS (out_time), GST_TIME_ARGS (priv->last_out_time));
        /* interpolate between the current time and the last time based on
         * number of packets we are missing, this is the estimated duration
         * for the missing packet based on equidistant packet spacing. Also make
         * sure we never go negative. */
        if (out_time >= priv->last_out_time)
          duration = (out_time - priv->last_out_time) / (gap + 1);
        else
          goto lost;

        GST_DEBUG_OBJECT (jitterbuffer, "duration %" GST_TIME_FORMAT,
            GST_TIME_ARGS (duration));
        /* add this duration to the timestamp of the last packet we pushed */
        out_time = (priv->last_out_time + duration);
      }
    } else {
      /* we don't know what the next_seqnum should be, wait for the last
       * possible moment to push this buffer, maybe we get an earlier seqnum
       * while we wait */
      GST_DEBUG_OBJECT (jitterbuffer, "First buffer %d, do sync", seqnum);
    }

    GST_OBJECT_LOCK (jitterbuffer);
    clock = GST_ELEMENT_CLOCK (jitterbuffer);
    if (!clock) {
      GST_OBJECT_UNLOCK (jitterbuffer);
      /* let's just push if there is no clock */
      GST_DEBUG_OBJECT (jitterbuffer, "No clock, push right away");
      goto push_buffer;
    }

    GST_DEBUG_OBJECT (jitterbuffer, "sync to timestamp %" GST_TIME_FORMAT,
        GST_TIME_ARGS (out_time));

    /* prepare for sync against clock */
    sync_time = get_sync_time (jitterbuffer, out_time);

    /* create an entry for the clock */
    id = priv->clock_id = gst_clock_new_single_shot_id (clock, sync_time);
    priv->unscheduled = FALSE;
    GST_OBJECT_UNLOCK (jitterbuffer);

    /* release the lock so that the other end can push stuff or unlock */
    JBUF_UNLOCK (priv);

    ret = gst_clock_id_wait (id, NULL);

    JBUF_LOCK (priv);
    /* and free the entry */
    gst_clock_id_unref (id);
    priv->clock_id = NULL;

    /* at this point, the clock could have been unlocked by a timeout, a new
     * tail element was added to the queue or because we are shutting down. Check
     * for shutdown first. */
    if G_UNLIKELY
      ((priv->srcresult != GST_FLOW_OK))
          goto flushing;

    /* if we got unscheduled and we are not flushing, it's because a new tail
     * element became available in the queue or we flushed the queue.
     * Grab it and try to push or sync. */
    if (ret == GST_CLOCK_UNSCHEDULED || priv->unscheduled) {
      GST_DEBUG_OBJECT (jitterbuffer,
          "Wait got unscheduled, will retry to push with new buffer");
      goto again;
    }

  lost:
    /* we now timed out, this means we lost a packet or finished synchronizing
     * on the first buffer. */
    if (gap > 0) {
      GstEvent *event;

      /* we had a gap and thus we lost a packet. Create an event for this.  */
      GST_DEBUG_OBJECT (jitterbuffer, "Packet #%d lost", next_seqnum);
      priv->num_late++;
      discont = TRUE;

      /* update our expected next packet */
      priv->last_popped_seqnum = next_seqnum;
      priv->last_out_time = out_time;
      priv->next_seqnum = (next_seqnum + 1) & 0xffff;

      if (priv->do_lost) {
        /* create paket lost event */
        event = gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM,
            gst_structure_new ("GstRTPPacketLost",
                "seqnum", G_TYPE_UINT, (guint) next_seqnum,
                "timestamp", G_TYPE_UINT64, out_time,
                "duration", G_TYPE_UINT64, duration, NULL));

        JBUF_UNLOCK (priv);
        gst_pad_push_event (priv->srcpad, event);
        JBUF_LOCK_CHECK (priv, flushing);
      }
      /* look for next packet */
      goto again;
    }

    /* there was no known gap,just the first packet, exit the loop and push */
    GST_DEBUG_OBJECT (jitterbuffer, "First packet #%d synced", seqnum);

    /* get new timestamp, latency might have changed */
    out_time = apply_offset (jitterbuffer, timestamp);
  }
push_buffer:

  /* when we get here we are ready to pop and push the buffer */
  outbuf = rtp_jitter_buffer_pop (priv->jbuf, &percent);

  check_buffering_percent (jitterbuffer, &percent);

  if (G_UNLIKELY (discont || priv->discont)) {
    /* set DISCONT flag when we missed a packet. We pushed the buffer writable
     * into the jitterbuffer so we can modify now. */
    GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DISCONT);
    priv->discont = FALSE;
  }

  /* apply timestamp with offset to buffer now */
  GST_BUFFER_TIMESTAMP (outbuf) = out_time;

  /* update the elapsed time when we need to check against the npt stop time. */
  if (priv->npt_stop != -1 && priv->ext_timestamp != -1
      && priv->clock_base != -1 && priv->clock_rate > 0) {
    guint64 elapsed, estimated;

    elapsed = compute_elapsed (jitterbuffer, outbuf);

    if (elapsed > priv->last_elapsed || !priv->last_elapsed) {
      guint64 left;

      priv->last_elapsed = elapsed;

      left = priv->npt_stop - priv->npt_start;
      GST_LOG_OBJECT (jitterbuffer, "left %" GST_TIME_FORMAT,
          GST_TIME_ARGS (left));

      if (elapsed > 0)
        estimated = gst_util_uint64_scale (out_time, left, elapsed);
      else {
        /* if there is almost nothing left,
         * we may never advance enough to end up in the above case */
        if (left < GST_SECOND)
          estimated = GST_SECOND;
        else
          estimated = -1;
      }

      GST_LOG_OBJECT (jitterbuffer, "elapsed %" GST_TIME_FORMAT ", estimated %"
          GST_TIME_FORMAT, GST_TIME_ARGS (elapsed), GST_TIME_ARGS (estimated));

      priv->estimated_eos = estimated;
    }
  }

  /* now we are ready to push the buffer. Save the seqnum and release the lock
   * so the other end can push stuff in the queue again. */
  priv->last_popped_seqnum = seqnum;
  priv->last_out_time = out_time;
  priv->next_seqnum = (seqnum + 1) & 0xffff;
  JBUF_UNLOCK (priv);

  if (percent != -1)
    post_buffering_percent (jitterbuffer, percent);

  /* push buffer */
  GST_DEBUG_OBJECT (jitterbuffer,
      "Pushing buffer %d, timestamp %" GST_TIME_FORMAT, seqnum,
      GST_TIME_ARGS (out_time));
  result = gst_pad_push (priv->srcpad, outbuf);
  if (G_UNLIKELY (result != GST_FLOW_OK))
    goto pause;

  return;

  /* ERRORS */
do_eos:
  {
    /* store result, we are flushing now */
    GST_DEBUG_OBJECT (jitterbuffer, "We are EOS, pushing EOS downstream");
    priv->srcresult = GST_FLOW_UNEXPECTED;
    gst_pad_pause_task (priv->srcpad);
    JBUF_UNLOCK (priv);
    gst_pad_push_event (priv->srcpad, gst_event_new_eos ());
    return;
  }
do_npt_stop:
  {
    /* store result, we are flushing now */
    GST_DEBUG_OBJECT (jitterbuffer, "We reached the NPT stop");
    JBUF_UNLOCK (priv);

    g_signal_emit (jitterbuffer,
        gst_rtp_jitter_buffer_signals[SIGNAL_ON_NPT_STOP], 0, NULL);
    return;
  }
flushing:
  {
    GST_DEBUG_OBJECT (jitterbuffer, "we are flushing");
    gst_pad_pause_task (priv->srcpad);
    JBUF_UNLOCK (priv);
    return;
  }
pause:
  {
    GST_DEBUG_OBJECT (jitterbuffer, "pausing task, reason %s",
        gst_flow_get_name (result));

    JBUF_LOCK (priv);
    /* store result */
    priv->srcresult = result;
    /* we don't post errors or anything because upstream will do that for us
     * when we pass the return value upstream. */
    gst_pad_pause_task (priv->srcpad);
    JBUF_UNLOCK (priv);
    return;
  }
}

static GstFlowReturn
gst_rtp_jitter_buffer_chain_rtcp (GstPad * pad, GstBuffer * buffer)
{
  GstRtpJitterBuffer *jitterbuffer;
  GstRtpJitterBufferPrivate *priv;
  GstFlowReturn ret = GST_FLOW_OK;
  guint64 base_rtptime, base_time;
  guint32 clock_rate;
  guint64 last_rtptime;
  guint32 ssrc;
  GstRTCPPacket packet;
  guint64 ext_rtptime, diff;
  guint32 rtptime;
  gboolean drop = FALSE;
  guint64 clock_base;

  jitterbuffer = GST_RTP_JITTER_BUFFER (gst_pad_get_parent (pad));

  if (G_UNLIKELY (!gst_rtcp_buffer_validate (buffer)))
    goto invalid_buffer;

  priv = jitterbuffer->priv;

  if (!gst_rtcp_buffer_get_first_packet (buffer, &packet))
    goto invalid_buffer;

  /* first packet must be SR or RR or else the validate would have failed */
  switch (gst_rtcp_packet_get_type (&packet)) {
    case GST_RTCP_TYPE_SR:
      gst_rtcp_packet_sr_get_sender_info (&packet, &ssrc, NULL, &rtptime,
          NULL, NULL);
      break;
    default:
      goto ignore_buffer;
  }

  GST_DEBUG_OBJECT (jitterbuffer, "received RTCP of SSRC %08x", ssrc);

  JBUF_LOCK (priv);
  /* convert the RTP timestamp to our extended timestamp, using the same offset
   * we used in the jitterbuffer */
  ext_rtptime = priv->jbuf->ext_rtptime;
  ext_rtptime = gst_rtp_buffer_ext_timestamp (&ext_rtptime, rtptime);

  /* get the last values from the jitterbuffer */
  rtp_jitter_buffer_get_sync (priv->jbuf, &base_rtptime, &base_time,
      &clock_rate, &last_rtptime);

  clock_base = priv->clock_base;

  GST_DEBUG_OBJECT (jitterbuffer, "ext SR %" G_GUINT64_FORMAT ", base %"
      G_GUINT64_FORMAT ", clock-rate %" G_GUINT32_FORMAT
      ", clock-base %" G_GUINT64_FORMAT,
      ext_rtptime, base_rtptime, clock_rate, clock_base);

  if (base_rtptime == -1 || clock_rate == -1 || base_time == -1) {
    GST_DEBUG_OBJECT (jitterbuffer, "dropping, no RTP values");
    drop = TRUE;
  } else {
    /* we can't accept anything that happened before we did the last resync */
    if (base_rtptime > ext_rtptime) {
      GST_DEBUG_OBJECT (jitterbuffer, "dropping, older than base time");
      drop = TRUE;
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
  JBUF_UNLOCK (priv);

  if (!drop) {
    GstStructure *s;

    s = gst_structure_new ("application/x-rtp-sync",
        "base-rtptime", G_TYPE_UINT64, base_rtptime,
        "base-time", G_TYPE_UINT64, base_time,
        "clock-rate", G_TYPE_UINT, clock_rate,
        "clock-base", G_TYPE_UINT64, clock_base,
        "sr-ext-rtptime", G_TYPE_UINT64, ext_rtptime,
        "sr-buffer", GST_TYPE_BUFFER, buffer, NULL);

    GST_DEBUG_OBJECT (jitterbuffer, "signaling sync");
    g_signal_emit (jitterbuffer,
        gst_rtp_jitter_buffer_signals[SIGNAL_HANDLE_SYNC], 0, s);
    gst_structure_free (s);
  } else {
    GST_DEBUG_OBJECT (jitterbuffer, "dropping RTCP packet");
    ret = GST_FLOW_OK;
  }

done:
  gst_buffer_unref (buffer);
  gst_object_unref (jitterbuffer);

  return ret;

invalid_buffer:
  {
    /* this is not fatal but should be filtered earlier */
    GST_ELEMENT_WARNING (jitterbuffer, STREAM, DECODE, (NULL),
        ("Received invalid RTCP payload, dropping"));
    ret = GST_FLOW_OK;
    goto done;
  }
ignore_buffer:
  {
    GST_DEBUG_OBJECT (jitterbuffer, "ignoring RTCP packet");
    ret = GST_FLOW_OK;
    goto done;
  }
}

static gboolean
gst_rtp_jitter_buffer_query (GstPad * pad, GstQuery * query)
{
  GstRtpJitterBuffer *jitterbuffer;
  GstRtpJitterBufferPrivate *priv;
  gboolean res = FALSE;

  jitterbuffer = GST_RTP_JITTER_BUFFER (gst_pad_get_parent (pad));
  if (G_UNLIKELY (jitterbuffer == NULL))
    return FALSE;
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
        res = gst_pad_query_default (pad, query);
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
        res = gst_pad_query_default (pad, query);
      }
      break;
    }
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }

  gst_object_unref (jitterbuffer);

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
      /* FIXME, we don't really have a method for signaling a timestamp
       * DISCONT without also making this a data discont. */
      /* priv->discont = TRUE; */
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
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
