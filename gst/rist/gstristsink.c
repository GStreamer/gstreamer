/* GStreamer RIST plugin
 * Copyright (C) 2019 Net Insight AB
 *     Author: Nicolas Dufresne <nicolas.dufresne@collabora.com>
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
 * SECTION:element-ristsink
 * @title: ristsink
 * @see_also: ristsrc
 *
 * This element implements RIST TR-06-1 Simple Profile transmitter. It
 * currently supports any registered RTP payload types such as MPEG TS. The
 * stream passed to this element must be RTP payloaded already. Even though
 * RTP SSRC collision is rare in unidirectional streaming, this element expect
 * the upstream elements to obey to collision events and change the SSRC in
 * use. Collision will ocure when tranmitting and receiving over multicast on
 * the same host.
 *
 * ## Example launch line
 * |[
 * gst-launch-1.0 udpsrc ! tsparse set-timestamp=1 ! rtpmp2pay ! ristsink address=10.0.0.1 port=5004
 * ]|
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gio/gio.h>
#include <gst/rtp/rtp.h>

#include "gstrist.h"

GST_DEBUG_CATEGORY_STATIC (gst_rist_sink_debug);
#define GST_CAT_DEFAULT gst_rist_sink_debug

enum
{
  PROP_ADDRESS = 1,
  PROP_PORT,
  PROP_SENDER_BUFFER,
  PROP_MIN_RTCP_INTERVAL,
  PROP_MAX_RTCP_BANDWIDTH,
  PROP_STATS_UPDATE_INTERVAL,
  PROP_STATS,
  PROP_CNAME,
  PROP_MULTICAST_LOOPBACK,
  PROP_MULTICAST_IFACE,
  PROP_MULTICAST_TTL
};

static GstStaticPadTemplate sink_templ = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp"));

struct _GstRistSink
{
  GstBin parent;

  /* Elements contained in the pipeline */
  GstElement *rtpbin;
  GstElement *rtp_sink;
  GstElement *rtcp_src;
  GstElement *rtcp_sink;
  GstElement *ssrc_filter;
  GstPad *sinkpad;

  /* RTX Elements */
  GstElement *rtxbin;
  GstElement *rtx_send;

  /* For stats */
  guint stats_interval;
  guint32 rtp_ssrc;
  guint32 rtcp_ssrc;
  GstClockID stats_cid;

  /* This is set whenever there is a pipeline construction failure, and used
   * to fail state changes later */
  gboolean construct_failed;
  const gchar *missing_plugin;
};

G_DEFINE_TYPE_WITH_CODE (GstRistSink, gst_rist_sink, GST_TYPE_BIN,
    GST_DEBUG_CATEGORY_INIT (gst_rist_sink_debug, "ristsink", 0, "RIST Sink"));

static GstCaps *
gst_rist_sink_request_pt_map (GstRistSrc * sink, GstElement * session, guint pt)
{
  const GstRTPPayloadInfo *pt_info;
  GstCaps *ret;

  pt_info = gst_rtp_payload_info_for_pt (pt);
  if (!pt_info || !pt_info->clock_rate)
    return NULL;

  ret = gst_caps_new_simple ("application/x-rtp",
      "media", G_TYPE_STRING, pt_info->media,
      "encoding_name", G_TYPE_STRING, pt_info->encoding_name,
      "clock-rate", G_TYPE_INT, (gint) pt_info->clock_rate, NULL);

  /* FIXME add sprop-parameter-set if any */
  g_warn_if_fail (pt_info->encoding_parameters == NULL);

  return ret;
}

static GstElement *
gst_rist_sink_request_aux_sender (GstRistSink * sink, guint session_id,
    GstElement * rtpbin)
{
  if (session_id != 0)
    return NULL;

  return gst_object_ref (sink->rtxbin);
}

static void
on_app_rtcp (GObject * session, guint32 subtype, guint32 ssrc,
    const gchar * name, GstBuffer * data, GstElement * rtpsession)
{
  if (g_str_equal (name, "RIST")) {
    GstEvent *event;
    GstPad *send_rtp_sink;
    GstMapInfo map;
    gint i;

    send_rtp_sink = gst_element_get_static_pad (rtpsession, "send_rtp_sink");
    if (send_rtp_sink) {
      gst_buffer_map (data, &map, GST_MAP_READ);

      for (i = 0; i < map.size; i += sizeof (guint32)) {
        guint32 dword = GST_READ_UINT32_BE (map.data + i);
        guint16 seqnum = dword >> 16;
        guint16 num = dword & 0x0000FFFF;
        guint16 j;

        GST_DEBUG ("got RIST nack packet, #%u %u", seqnum, num);

        /* num is inclusive, i.e. it can be 0, which means exactly 1 seqnum */
        for (j = 0; j <= num; j++) {
          event = gst_event_new_custom (GST_EVENT_CUSTOM_UPSTREAM,
              gst_structure_new ("GstRTPRetransmissionRequest",
                  "seqnum", G_TYPE_UINT, (guint) seqnum + j,
                  "ssrc", G_TYPE_UINT, (guint) ssrc, NULL));
          gst_pad_push_event (send_rtp_sink, event);
        }
      }

      gst_buffer_unmap (data, &map);
      gst_object_unref (send_rtp_sink);
    }
  }
}

static void
gst_rist_sink_on_new_sender_ssrc (GstRistSink * sink, guint session_id,
    guint ssrc, GstElement * rtpbin)
{
  GObject *gstsession = NULL;
  GObject *session = NULL;
  GObject *source = NULL;

  if (session_id != 0)
    return;

  g_signal_emit_by_name (rtpbin, "get-session", session_id, &gstsession);
  g_signal_emit_by_name (rtpbin, "get-internal-session", session_id, &session);
  g_signal_emit_by_name (session, "get-source-by-ssrc", ssrc, &source);

  if (ssrc & 1)
    g_object_set (source, "disable-rtcp", TRUE, NULL);
  else
    g_signal_connect (session, "on-app-rtcp", (GCallback) on_app_rtcp,
        gstsession);

  g_object_unref (source);
  g_object_unref (session);
}

static void
gst_rist_sink_on_new_receiver_ssrc (GstRistSink * sink, guint session_id,
    guint ssrc, GstElement * rtpbin)
{
  if (session_id != 0)
    return;

  GST_INFO_OBJECT (sink, "Got RTCP remote SSRC %u", ssrc);
  sink->rtcp_ssrc = ssrc;
}

static GstPadProbeReturn
gst_rist_sink_fix_collision (GstPad * pad, GstPadProbeInfo * info,
    gpointer user_data)
{
  GstEvent *event = info->data;
  const GstStructure *cs;
  GstStructure *s;
  guint ssrc;

  /* We simply ignore collisions */
  if (GST_EVENT_TYPE (event) != GST_EVENT_CUSTOM_UPSTREAM)
    return GST_PAD_PROBE_OK;

  cs = gst_event_get_structure (event);
  if (!gst_structure_has_name (cs, "GstRTPCollision"))
    return GST_PAD_PROBE_OK;

  gst_structure_get_uint (cs, "suggested-ssrc", &ssrc);
  if ((ssrc & 1) == 0)
    return GST_PAD_PROBE_OK;

  event = info->data = gst_event_make_writable (event);
  /* we can drop the const qualifier as we ensured writability */
  s = (GstStructure *) gst_event_get_structure (event);
  gst_structure_set (s, "suggested-ssrc", G_TYPE_UINT, ssrc - 1, NULL);

  return GST_PAD_PROBE_OK;
}

static gboolean
gst_rist_sink_set_caps (GstRistSink * sink, GstCaps * caps)
{
  const GstStructure *s = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_uint (s, "ssrc", &sink->rtp_ssrc)) {
    GST_ELEMENT_ERROR (sink, CORE, NEGOTIATION, ("No 'ssrc' field in caps."),
        (NULL));
    return FALSE;
  }

  if (sink->rtp_ssrc & 1) {
    GST_ELEMENT_ERROR (sink, CORE, NEGOTIATION,
        ("Invalid RIST SSRC, LSB must be zero."), (NULL));
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_rist_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstRistSink *sink = GST_RIST_SINK (parent);
  GstCaps *caps;
  gboolean ret = TRUE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
      gst_event_parse_caps (event, &caps);
      ret = gst_rist_sink_set_caps (sink, caps);
      break;
    default:
      break;
  }

  if (ret)
    ret = gst_pad_event_default (pad, parent, event);
  else
    gst_event_unref (event);

  return ret;
}

static void
gst_rist_sink_init (GstRistSink * sink)
{
  GstPad *ssrc_filter_sinkpad;
  GstCaps *ssrc_caps;
  GstPad *pad, *gpad;
  GstStructure *sdes = NULL;

  /* Construct the RIST RTP sender pipeline.
   *
   * capsfilter*-> [send_rtp_sink_%u]   --------  [send_rtp_src_%u]  -> udpsink
   *                                   | rtpbin |
   * udpsrc     -> [recv_rtcp_sink_%u]  --------  [send_rtcp_src_%u] -> * udpsink
   *
   * * To select RIST compatible SSRC
   */
  sink->rtpbin = gst_element_factory_make ("rtpbin", "rist_send_rtbpin");
  if (!sink->rtpbin) {
    sink->missing_plugin = "rtpmanager";
    goto missing_plugin;
  }

  /* RIST specification says the SDES should only contain the CNAME */
  g_object_get (sink->rtpbin, "sdes", &sdes, NULL);
  gst_structure_remove_field (sdes, "tool");

  gst_bin_add (GST_BIN (sink), sink->rtpbin);
  g_object_set (sink->rtpbin, "do-retransmission", TRUE,
      "rtp-profile", 3 /* AVFP */ ,
      "sdes", sdes, NULL);
  gst_structure_free (sdes);

  g_signal_connect_swapped (sink->rtpbin, "request-pt-map",
      G_CALLBACK (gst_rist_sink_request_pt_map), sink);
  g_signal_connect_swapped (sink->rtpbin, "request-aux-sender",
      G_CALLBACK (gst_rist_sink_request_aux_sender), sink);
  g_signal_connect_swapped (sink->rtpbin, "on-new-sender-ssrc",
      G_CALLBACK (gst_rist_sink_on_new_sender_ssrc), sink);
  g_signal_connect_swapped (sink->rtpbin, "on-new-ssrc",
      G_CALLBACK (gst_rist_sink_on_new_receiver_ssrc), sink);

  sink->rtxbin = gst_bin_new ("rist_send_rtxbin");
  g_object_ref_sink (sink->rtxbin);
  sink->rtx_send = gst_element_factory_make ("ristrtxsend", "rist_rtx_send");
  gst_bin_add (GST_BIN (sink->rtxbin), sink->rtx_send);
  g_object_set (sink->rtx_send, "max-size-packets", 0, NULL);

  pad = gst_element_get_static_pad (sink->rtx_send, "sink");
  gpad = gst_ghost_pad_new ("sink_0", pad);
  gst_object_unref (pad);
  gst_element_add_pad (sink->rtxbin, gpad);

  pad = gst_element_get_static_pad (sink->rtx_send, "src");
  gpad = gst_ghost_pad_new ("src_0", pad);
  gst_object_unref (pad);
  gst_element_add_pad (sink->rtxbin, gpad);

  sink->rtp_sink = gst_element_factory_make ("udpsink", "rist_rtp_udpsink");
  sink->rtcp_src = gst_element_factory_make ("udpsrc", "rist_rtcp_udpsrc");
  sink->rtcp_sink = gst_element_factory_make ("udpsink", "rist_rtcp_udpsink");
  if (!sink->rtp_sink || !sink->rtcp_src || !sink->rtcp_sink) {
    g_clear_object (&sink->rtp_sink);
    g_clear_object (&sink->rtcp_src);
    g_clear_object (&sink->rtcp_sink);
    sink->missing_plugin = "udp";
    goto missing_plugin;
  }
  gst_bin_add_many (GST_BIN (sink), sink->rtp_sink, sink->rtcp_src,
      sink->rtcp_sink, NULL);
  gst_element_set_locked_state (sink->rtcp_src, TRUE);
  gst_element_set_locked_state (sink->rtcp_sink, TRUE);

  sink->ssrc_filter = gst_element_factory_make ("capsfilter",
      "rist_ssrc_filter");
  if (!sink->ssrc_filter) {
    sink->missing_plugin = "coreelements";
    goto missing_plugin;
  }
  gst_bin_add (GST_BIN (sink), sink->ssrc_filter);

  sink->rtp_ssrc = g_random_int () & ~1;
  ssrc_caps = gst_caps_new_simple ("application/x-rtp",
      "ssrc", G_TYPE_UINT, sink->rtp_ssrc, NULL);
  gst_caps_append_structure (ssrc_caps,
      gst_structure_new_empty ("application/x-rtp"));
  g_object_set (sink->ssrc_filter, "caps", ssrc_caps, NULL);
  gst_caps_unref (ssrc_caps);
  gst_element_link_pads (sink->ssrc_filter, "src", sink->rtpbin,
      "send_rtp_sink_0");
  gst_element_link_pads (sink->rtpbin, "send_rtp_src_0", sink->rtp_sink,
      "sink");
  gst_element_link_pads (sink->rtcp_src, "src", sink->rtpbin,
      "recv_rtcp_sink_0");
  gst_element_link_pads (sink->rtpbin, "send_rtcp_src_0", sink->rtcp_sink,
      "sink");

  ssrc_filter_sinkpad = gst_element_get_static_pad (sink->ssrc_filter, "sink");
  sink->sinkpad = gst_ghost_pad_new_from_template ("sink", ssrc_filter_sinkpad,
      gst_static_pad_template_get (&sink_templ));
  gst_pad_set_event_function (sink->sinkpad, gst_rist_sink_event);
  gst_element_add_pad (GST_ELEMENT (sink), sink->sinkpad);
  gst_object_unref (ssrc_filter_sinkpad);

  gst_pad_add_probe (sink->sinkpad, GST_PAD_PROBE_TYPE_EVENT_UPSTREAM,
      gst_rist_sink_fix_collision, sink, NULL);

  return;

missing_plugin:
  {
    GST_ERROR_OBJECT (sink, "'%s' plugin is missing.", sink->missing_plugin);
    sink->construct_failed = TRUE;
    /* Just make our element valid, so we fail cleanly */
    gst_element_add_pad (GST_ELEMENT (sink),
        gst_pad_new_from_static_template (&sink_templ, "sink"));
  }
}

static GstStateChangeReturn
gst_rist_sink_start (GstRistSink * sink)
{
  GSocket *socket = NULL;
  GInetAddress *iaddr = NULL;
  gchar *remote_addr = NULL;
  guint remote_port;
  GError *error = NULL;

  if (sink->construct_failed) {
    GST_ELEMENT_ERROR (sink, CORE, MISSING_PLUGIN,
        ("Your GStreamer installation is missing plugin '%s'",
            sink->missing_plugin), (NULL));
    return GST_STATE_CHANGE_FAILURE;
  }

  g_object_get (sink->rtcp_sink, "host", &remote_addr, "port", &remote_port,
      NULL);

  iaddr = g_inet_address_new_from_string (remote_addr);
  if (!iaddr) {
    GList *results;
    GResolver *resolver = NULL;

    resolver = g_resolver_get_default ();
    results = g_resolver_lookup_by_name (resolver, remote_addr, NULL, &error);

    if (!results) {
      g_object_unref (resolver);
      goto dns_resolve_failed;
    }

    iaddr = G_INET_ADDRESS (g_object_ref (results->data));

    g_free (remote_addr);
    remote_addr = g_inet_address_to_string (iaddr);

    g_resolver_free_addresses (results);
    g_object_unref (resolver);
  }

  if (g_inet_address_get_is_multicast (iaddr)) {
    g_object_set (sink->rtcp_src, "address", remote_addr, "port", remote_port,
        NULL);
  } else {
    const gchar *any_addr;

    if (g_inet_address_get_family (iaddr) == G_SOCKET_FAMILY_IPV6)
      any_addr = "::";
    else
      any_addr = "0.0.0.0";

    g_object_set (sink->rtcp_src, "address", any_addr, "port", 0, NULL);
  }
  g_object_unref (iaddr);

  gst_element_set_locked_state (sink->rtcp_src, FALSE);
  gst_element_sync_state_with_parent (sink->rtcp_src);

  /* share the socket created by the sink */
  g_object_get (sink->rtcp_src, "used-socket", &socket, NULL);
  g_object_set (sink->rtcp_sink, "socket", socket, "auto-multicast", FALSE,
      "close-socket", FALSE, NULL);
  g_object_unref (socket);

  gst_element_set_locked_state (sink->rtcp_sink, FALSE);
  gst_element_sync_state_with_parent (sink->rtcp_sink);

  return GST_STATE_CHANGE_SUCCESS;

dns_resolve_failed:
  GST_ELEMENT_ERROR (sink, RESOURCE, NOT_FOUND,
      ("Could not resolve hostname '%s'", remote_addr),
      ("DNS resolver reported: %s", error->message));
  g_free (remote_addr);
  g_error_free (error);
  return GST_STATE_CHANGE_FAILURE;
}

static GstStructure *
gst_rist_sink_create_stats (GstRistSink * sink)
{
  GObject *session = NULL, *source = NULL;
  GstStructure *sstats = NULL, *ret;
  guint64 pkt_sent = 0, rtx_sent = 0, rtt;
  guint rb_rtt = 0;

  ret = gst_structure_new_empty ("rist/x-sender-stats");

  g_signal_emit_by_name (sink->rtpbin, "get-internal-session", 0, &session);
  if (!session)
    return ret;

  g_signal_emit_by_name (session, "get-source-by-ssrc", sink->rtp_ssrc,
      &source);
  if (source) {
    g_object_get (source, "stats", &sstats, NULL);
    gst_structure_get_uint64 (sstats, "packets-sent", &pkt_sent);
    gst_structure_free (sstats);
    g_clear_object (&source);
  }

  g_signal_emit_by_name (session, "get-source-by-ssrc", sink->rtcp_ssrc,
      &source);
  if (source) {
    g_object_get (source, "stats", &sstats, NULL);
    gst_structure_get_uint (sstats, "rb-round-trip", &rb_rtt);
    gst_structure_free (sstats);
    g_clear_object (&source);
  }
  g_object_unref (session);

  g_object_get (sink->rtx_send, "num-rtx-packets", &rtx_sent, NULL);

  /* rb_rtt is in Q16 in NTP time */
  rtt = gst_util_uint64_scale (rb_rtt, GST_SECOND, 65536);

  gst_structure_set (ret, "sent-original-packets", G_TYPE_UINT64, pkt_sent,
      "sent-retransmitted-packets", G_TYPE_UINT64, rtx_sent,
      "round-trip-time", G_TYPE_UINT64, rtt, NULL);

  return ret;
}

static gboolean
gst_rist_sink_dump_stats (GstClock * clock, GstClockTime time, GstClockID id,
    gpointer user_data)
{
  GstRistSink *sink = GST_RIST_SINK (user_data);
  GstStructure *stats = gst_rist_sink_create_stats (sink);

  gst_println ("%s: %" GST_PTR_FORMAT, GST_OBJECT_NAME (sink), stats);

  gst_structure_free (stats);
  return TRUE;
}

static void
gst_rist_sink_enable_stats_interval (GstRistSink * sink)
{
  GstClock *clock;
  GstClockTime start, interval;

  if (sink->stats_interval == 0)
    return;

  interval = sink->stats_interval * GST_MSECOND;
  clock = gst_system_clock_obtain ();
  start = gst_clock_get_time (clock) + interval;

  sink->stats_cid = gst_clock_new_periodic_id (clock, start, interval);
  gst_clock_id_wait_async (sink->stats_cid, gst_rist_sink_dump_stats,
      gst_object_ref (sink), (GDestroyNotify) gst_object_unref);

  gst_object_unref (clock);
}

static void
gst_rist_sink_disable_stats_interval (GstRistSink * sink)
{
  if (sink->stats_cid) {
    gst_clock_id_unschedule (sink->stats_cid);
    gst_clock_id_unref (sink->stats_cid);
    sink->stats_cid = NULL;
  }
}

static GstStateChangeReturn
gst_rist_sink_change_state (GstElement * element, GstStateChange transition)
{
  GstRistSink *sink = GST_RIST_SINK (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_rist_sink_disable_stats_interval (sink);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (gst_rist_sink_parent_class)->change_state (element,
      transition);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      ret = gst_rist_sink_start (sink);
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_rist_sink_enable_stats_interval (sink);
      break;
    default:
      break;
  }

  return ret;
}

static void
gst_rist_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstRistSink *sink = GST_RIST_SINK (object);
  GstElement *session = NULL;
  GstClockTime interval;
  GstStructure *sdes;

  if (sink->construct_failed)
    return;

  switch (prop_id) {
    case PROP_ADDRESS:
      g_object_get_property (G_OBJECT (sink->rtp_sink), "host", value);
      break;

    case PROP_PORT:
      g_object_get_property (G_OBJECT (sink->rtp_sink), "port", value);
      break;

    case PROP_SENDER_BUFFER:
      g_object_get_property (G_OBJECT (sink->rtx_send), "max-size-time", value);
      break;

    case PROP_MIN_RTCP_INTERVAL:
      g_signal_emit_by_name (sink->rtpbin, "get-session", 0, &session);
      g_object_get (session, "rtcp-min-interval", &interval, NULL);
      g_value_set_uint (value, (guint) (interval / GST_MSECOND));
      g_object_unref (session);
      break;

    case PROP_MAX_RTCP_BANDWIDTH:
      g_signal_emit_by_name (sink->rtpbin, "get-session", 0, &session);
      g_object_get_property (G_OBJECT (session), "rtcp-fraction", value);
      g_object_unref (session);
      break;

    case PROP_STATS_UPDATE_INTERVAL:
      g_value_set_uint (value, sink->stats_interval);
      break;

    case PROP_STATS:
      g_value_take_boxed (value, gst_rist_sink_create_stats (sink));
      break;

    case PROP_CNAME:
      g_object_get (sink->rtpbin, "sdes", &sdes, NULL);
      g_value_set_string (value, gst_structure_get_string (sdes, "cname"));
      gst_structure_free (sdes);
      break;

    case PROP_MULTICAST_LOOPBACK:
      g_object_get_property (G_OBJECT (sink->rtp_sink), "loop", value);
      break;

    case PROP_MULTICAST_IFACE:
      g_object_get_property (G_OBJECT (sink->rtp_sink),
          "multicast-iface", value);
      break;

    case PROP_MULTICAST_TTL:
      g_object_get_property (G_OBJECT (sink->rtp_sink), "ttl-mc", value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rist_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRistSink *sink = GST_RIST_SINK (object);
  GstElement *session = NULL;
  GstStructure *sdes;

  if (sink->construct_failed)
    return;

  switch (prop_id) {
    case PROP_ADDRESS:
      g_object_set_property (G_OBJECT (sink->rtp_sink), "host", value);
      g_object_set_property (G_OBJECT (sink->rtcp_sink), "host", value);
      break;

    case PROP_PORT:{
      guint port = g_value_get_uint (value);

      /* According to 5.1.1, RTCP receiver port most be event number and RTCP
       * port should be the RTP port + 1 */

      if (port & 0x1) {
        g_warning ("Invalid RIST port %u, should be an even number.", port);
        return;
      }

      g_object_set (sink->rtp_sink, "port", port, NULL);
      g_object_set (sink->rtcp_sink, "port", port + 1, NULL);
      break;
    }

    case PROP_SENDER_BUFFER:
      g_object_set (sink->rtx_send,
          "max-size-time", g_value_get_uint (value), NULL);
      break;

    case PROP_MIN_RTCP_INTERVAL:
      g_signal_emit_by_name (sink->rtpbin, "get-session", 0, &session);
      g_object_set (session, "rtcp-min-interval",
          g_value_get_uint (value) * GST_MSECOND, NULL);
      g_object_unref (session);
      break;

    case PROP_MAX_RTCP_BANDWIDTH:
      g_signal_emit_by_name (sink->rtpbin, "get-session", 0, &session);
      g_object_set (session, "rtcp-fraction", g_value_get_double (value), NULL);
      g_object_unref (session);
      break;

    case PROP_STATS_UPDATE_INTERVAL:
      sink->stats_interval = g_value_get_uint (value);
      break;

    case PROP_CNAME:
      g_object_get (sink->rtpbin, "sdes", &sdes, NULL);
      gst_structure_set_value (sdes, "cname", value);
      g_object_set (sink->rtpbin, "sdes", sdes, NULL);
      gst_structure_free (sdes);
      break;

    case PROP_MULTICAST_LOOPBACK:
      g_object_set_property (G_OBJECT (sink->rtp_sink), "loop", value);
      g_object_set_property (G_OBJECT (sink->rtcp_sink), "loop", value);
      break;

    case PROP_MULTICAST_IFACE:
      g_object_set_property (G_OBJECT (sink->rtp_sink),
          "multicast-iface", value);
      g_object_set_property (G_OBJECT (sink->rtcp_sink),
          "multicast-iface", value);
      break;

    case PROP_MULTICAST_TTL:
      g_object_set_property (G_OBJECT (sink->rtp_sink), "ttl-mc", value);
      g_object_set_property (G_OBJECT (sink->rtcp_sink), "ttl-mc", value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rist_sink_finalize (GObject * object)
{
  GstRistSink *sink = GST_RIST_SINK (object);
  g_clear_object (&sink->rtxbin);

  G_OBJECT_CLASS (gst_rist_sink_parent_class)->finalize (object);
}

static void
gst_rist_sink_class_init (GstRistSinkClass * klass)
{
  GstElementClass *element_class = (GstElementClass *) klass;
  GObjectClass *object_class = (GObjectClass *) klass;

  gst_element_class_set_metadata (element_class,
      "RIST Sink", "Source/Network",
      "Sink that implements RIST TR-06-1 streaming specification",
      "Nicolas Dufresne <nicolas.dufresne@collabora.com");
  gst_element_class_add_static_pad_template (element_class, &sink_templ);

  element_class->change_state = gst_rist_sink_change_state;

  object_class->get_property = gst_rist_sink_get_property;
  object_class->set_property = gst_rist_sink_set_property;
  object_class->finalize = gst_rist_sink_finalize;

  g_object_class_install_property (object_class, PROP_ADDRESS,
      g_param_spec_string ("address", "Address",
          "Address to send packets to (can be IPv4 or IPv6).", "0.0.0.0",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_PORT,
      g_param_spec_uint ("port", "Port", "The port RTP packets will be sent, "
          "RTCP port is derived from it, this port must be an even number.",
          2, 65534, 5004,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT));

  g_object_class_install_property (object_class, PROP_SENDER_BUFFER,
      g_param_spec_uint ("sender-buffer", "Sender Buffer",
          "Size of the retransmission queue in ms", 0, G_MAXUINT, 1200,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT));

  g_object_class_install_property (object_class, PROP_MIN_RTCP_INTERVAL,
      g_param_spec_uint ("min-rtcp-interval", "Minimum RTCP Intercal",
          "The minimum interval in ms between two regular successive RTCP "
          "packets.", 0, 100, 100,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT));

  g_object_class_install_property (object_class, PROP_MAX_RTCP_BANDWIDTH,
      g_param_spec_double ("max-rtcp-bandwidth", "Maximum RTCP Bandwidth",
          "The maximum bandwidth used for RTCP in fraction of RTP bandwdith",
          0.0, 0.05, 0.05,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT));

  g_object_class_install_property (object_class, PROP_STATS_UPDATE_INTERVAL,
      g_param_spec_uint ("stats-update-interval", "Statistics Update Interval",
          "The interval between 'stats' update notification (0 disabled)",
          0, G_MAXUINT, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT));

  g_object_class_install_property (object_class, PROP_STATS,
      g_param_spec_boxed ("stats", "Statistics",
          "Statistic in a GstStructure named 'rist/x-sender-stats'",
          GST_TYPE_STRUCTURE, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_CNAME,
      g_param_spec_string ("cname", "CName",
          "Set the CNAME in the SDES block of the sender report.", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_MULTICAST_LOOPBACK,
      g_param_spec_boolean ("multicast-loopback", "Multicast Loopback",
          "When enabled, the packet will be received locally.", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT));

  g_object_class_install_property (object_class, PROP_MULTICAST_IFACE,
      g_param_spec_string ("multicast-iface", "multicast-iface",
          "The multicast interface to use to send packets.", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_MULTICAST_TTL,
      g_param_spec_int ("multicast-ttl", "Multicast TTL",
          "The multicast time-to-live parameter.", 0, 255, 1,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT));
}
