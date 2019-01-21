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
 * SECTION:element-ristsrc
 * @title: ristsrc
 * @see_also: ristsink
 *
 * This element implements RIST TR-06-1 Simple Profile receiver. The stream
 * produced by this element will be RTP payloaded. This element also implement
 * the URI scheme `rist://` allowing to render RIST streams in GStreamer based
 * media players. The RIST uri handler also allow setting propertied through
 * the URI query.
 *
 * ## Example launch line
 * |[
 * gst-launch-1.0 ristsrc address=0.0.0.0 port=5004 ! rtpmp2depay ! udpsink
 * gst-play-1.0 "rist://0.0.0.0:5004?receiver-buffer=700"
 * ]|
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gio/gio.h>
#include <gst/net/net.h>
#include <gst/rtp/rtp.h>

/* for setsockopt() */
#ifndef G_OS_WIN32
#include <sys/types.h>
#include <sys/socket.h>
#endif

#include "gstrist.h"

GST_DEBUG_CATEGORY_STATIC (gst_rist_src_debug);
#define GST_CAT_DEFAULT gst_rist_src_debug

enum
{
  PROP_ADDRESS = 1,
  PROP_PORT,
  PROP_RECEIVER_BUFFER,
  PROP_REORDER_SECTION,
  PROP_MAX_RTX_RETRIES,
  PROP_MIN_RTCP_INTERVAL,
  PROP_MAX_RTCP_BANDWIDTH,
  PROP_STATS_UPDATE_INTERVAL,
  PROP_STATS,
  PROP_CNAME,
  PROP_MULTICAST_LOOPBACK,
  PROP_MULTICAST_IFACE,
  PROP_MULTICAST_TTL
};

static GstStaticPadTemplate src_templ = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp"));

struct _GstRistSrc
{
  GstBin parent;

  GstUri *uri;

  /* Elements contained in the pipeline, the rtp/rtcp_src are 'udpsrc' */
  GstElement *rtpbin;
  GstElement *rtp_src;
  GstElement *rtcp_src;
  GstElement *rtcp_sink;
  gulong rtcp_recv_probe;
  gulong rtcp_send_probe;
  GSocketAddress *rtcp_send_addr;
  GstPad *srcpad;
  gint multicast_ttl;

  /* RTX Elements */
  GstElement *rtxbin;
  GstElement *rtx_receive;

  /* For property handling */
  guint reorder_section;
  guint max_rtx_retries;

  /* For stats */
  guint stats_interval;
  guint32 rtp_ssrc;
  GstClockID stats_cid;
  GstElement *jitterbuffer;

  /* This is set whenever there is a pipeline construction failure, and used
   * to fail state changes later */
  gboolean construct_failed;
  const gchar *missing_plugin;
};

static void gst_rist_src_uri_init (gpointer g_iface, gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (GstRistSrc, gst_rist_src, GST_TYPE_BIN,
    G_IMPLEMENT_INTERFACE (GST_TYPE_URI_HANDLER, gst_rist_src_uri_init);
    GST_DEBUG_CATEGORY_INIT (gst_rist_src_debug, "ristsrc", 0, "RIST Source"));

static void
gst_rist_src_pad_added (GstRistSrc * src, GstPad * new_pad, GstElement * rtpbin)
{
  GST_TRACE_OBJECT (src, "New pad '%s'.", GST_PAD_NAME (new_pad));

  if (g_str_has_prefix (GST_PAD_NAME (new_pad), "recv_rtp_src_0_")) {
    GST_DEBUG_OBJECT (src, "Using new pad '%s' as ghost pad target.",
        GST_PAD_NAME (new_pad));
    gst_ghost_pad_set_target (GST_GHOST_PAD (src->srcpad), new_pad);
  }
}

static GstCaps *
gst_rist_src_request_pt_map (GstRistSrc * src, GstElement * session, guint pt)
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
gst_rist_src_request_aux_receiver (GstRistSrc * src, guint session_id,
    GstElement * rtpbin)
{
  if (session_id != 0)
    return NULL;

  return gst_object_ref (src->rtxbin);
}

/* Overrides the nack creation. Right now we don't send mixed NACKS type, we
 * simply send a set of range NACK if it takes less space, or allow adding
 * more seqnum. */
static guint
gst_rist_src_on_sending_nacks (GObject * session, guint sender_ssrc,
    guint media_ssrc, GArray * nacks, GstBuffer * buffer, gpointer user_data)
{
  GstRTCPBuffer rtcp = GST_RTCP_BUFFER_INIT;
  GstRTCPPacket packet;
  guint8 *app_data;
  guint nacked_seqnums = 0;
  guint range_size = 0;
  guint n_rg_nacks = 0;
  guint n_fb_nacks = 0;
  guint16 seqnum;
  guint i;
  gint diff;

  /* We'll assume that range will be best, and find how many generic NACK
   * would have been created. If this number ends up being smaller, we will
   * just remove the APP packet and return 0, leaving it to RTPSession to
   * create the generic NACK.*/

  gst_rtcp_buffer_map (buffer, GST_MAP_READWRITE, &rtcp);
  if (!gst_rtcp_buffer_add_packet (&rtcp, GST_RTCP_TYPE_APP, &packet))
    /* exit because the packet is full, will put next request in a
     * further packet */
    goto done;

  gst_rtcp_packet_app_set_ssrc (&packet, media_ssrc);
  gst_rtcp_packet_app_set_name (&packet, "RIST");

  if (!gst_rtcp_packet_app_set_data_length (&packet, 1)) {
    gst_rtcp_packet_remove (&packet);
    GST_WARNING ("no range nacks fit in the packet");
    goto done;
  }

  app_data = gst_rtcp_packet_app_get_data (&packet);
  for (i = 0; i < nacks->len; i = nacked_seqnums) {
    guint j;
    seqnum = g_array_index (nacks, guint16, i);

    if (!gst_rtcp_packet_app_set_data_length (&packet, n_rg_nacks + 1))
      break;

    n_rg_nacks++;
    nacked_seqnums++;

    for (j = i + 1; j < nacks->len; j++) {
      guint16 next_seqnum = g_array_index (nacks, guint16, j);
      diff = gst_rtp_buffer_compare_seqnum (seqnum, next_seqnum);
      GST_TRACE ("[%u][%u] %u %u diff %i", i, j, seqnum, next_seqnum, diff);
      if (diff > (j - i))
        break;

      nacked_seqnums++;
    }

    range_size = j - i - 1;
    GST_WRITE_UINT32_BE (app_data, seqnum << 16 | range_size);
    app_data += 4;
  }

  /* count how many FB NACK it would take to wrap nacked_seqnums */
  seqnum = g_array_index (nacks, guint16, 0);
  n_fb_nacks = 1;
  for (i = 1; i < nacked_seqnums; i++) {
    guint16 next_seqnum = g_array_index (nacks, guint16, i);
    diff = gst_rtp_buffer_compare_seqnum (seqnum, next_seqnum);
    if (diff > 16) {
      n_fb_nacks++;
      seqnum = next_seqnum;
    }
  }

  if (n_fb_nacks <= n_rg_nacks) {
    GST_DEBUG ("Not sending %u range nacks, as %u FB nacks will be smaller",
        n_rg_nacks, n_fb_nacks);
    gst_rtcp_packet_remove (&packet);
    nacked_seqnums = 0;
    goto done;
  }

  GST_DEBUG ("Sent %u seqnums into %u Range NACKs", nacked_seqnums, n_rg_nacks);

done:
  gst_rtcp_buffer_unmap (&rtcp);
  return nacked_seqnums;
}

static void
gst_rist_src_on_new_ssrc (GstRistSrc * src, guint session_id, guint ssrc,
    GstElement * rtpbin)
{
  GObject *session = NULL;
  GObject *source = NULL;

  if (session_id != 0)
    return;

  g_signal_emit_by_name (rtpbin, "get-internal-session", session_id, &session);
  g_signal_emit_by_name (session, "get-source-by-ssrc", ssrc, &source);

  if (ssrc & 1)
    g_object_set (source, "disable-rtcp", TRUE, "probation", 0, NULL);
  else
    g_signal_connect (session, "on-sending-nacks",
        (GCallback) gst_rist_src_on_sending_nacks, NULL);

  g_object_unref (source);
  g_object_unref (session);
}

static void
gst_rist_src_new_jitterbuffer (GstRistSrc * src, GstElement * jitterbuffer,
    guint session, guint ssrc, GstElement * rtpbin)
{
  GST_OBJECT_LOCK (src);
  g_object_set (jitterbuffer, "rtx-delay", src->reorder_section,
      "rtx-max-retries", src->max_rtx_retries, NULL);

  if ((ssrc & 1) == 0) {
    GST_INFO_OBJECT (src, "Saving jitterbuffer for session %u ssrc %u",
        session, ssrc);
    g_clear_object (&src->jitterbuffer);
    src->jitterbuffer = gst_object_ref (jitterbuffer);
    src->rtp_ssrc = ssrc;
  }

  GST_OBJECT_UNLOCK (src);
}

static void
gst_rist_src_init (GstRistSrc * src)
{
  GstPad *pad, *gpad;
  GstStructure *sdes = NULL;

  /* Construct the RIST RTP receiver pipeline.
   *
   * udpsrc -> [recv_rtp_sink_%u]  --------  [recv_rtp_src_%u_%u_%u]
   *                              | rtpbin |
   * udpsrc -> [recv_rtcp_sink_%u] --------  [send_rtcp_src_%u] -> udpsink
   *
   * This pipeline is fixed for now, note that optionally an FEC stream could
   * be added later.
   */
  src->srcpad = gst_ghost_pad_new_no_target_from_template ("src",
      gst_static_pad_template_get (&src_templ));
  gst_element_add_pad (GST_ELEMENT (src), src->srcpad);

  src->rtpbin = gst_element_factory_make ("rtpbin", "rist_recv_rtbpin");
  if (!src->rtpbin) {
    src->missing_plugin = "rtpmanager";
    goto missing_plugin;
  }

  /* RIST specification says the SDES should only contain the CNAME */
  g_object_get (src->rtpbin, "sdes", &sdes, NULL);
  gst_structure_remove_field (sdes, "tool");

  gst_bin_add (GST_BIN (src), src->rtpbin);
  g_object_set (src->rtpbin, "do-retransmission", TRUE,
      "rtp-profile", 3 /* AVPF */ ,
      "sdes", sdes, NULL);

  gst_structure_free (sdes);

  g_signal_connect_swapped (src->rtpbin, "request-pt-map",
      G_CALLBACK (gst_rist_src_request_pt_map), src);
  g_signal_connect_swapped (src->rtpbin, "request-aux-receiver",
      G_CALLBACK (gst_rist_src_request_aux_receiver), src);

  src->rtxbin = gst_bin_new ("rist_recv_rtxbin");
  g_object_ref_sink (src->rtxbin);
  src->rtx_receive = gst_element_factory_make ("ristrtxreceive",
      "rist_rtx_receive");
  gst_bin_add (GST_BIN (src->rtxbin), src->rtx_receive);

  pad = gst_element_get_static_pad (src->rtx_receive, "sink");
  gpad = gst_ghost_pad_new ("sink_0", pad);
  gst_object_unref (pad);
  gst_element_add_pad (src->rtxbin, gpad);

  pad = gst_element_get_static_pad (src->rtx_receive, "src");
  gpad = gst_ghost_pad_new ("src_0", pad);
  gst_object_unref (pad);
  gst_element_add_pad (src->rtxbin, gpad);

  src->rtp_src = gst_element_factory_make ("udpsrc", "rist_rtp_udpsrc");
  src->rtcp_src = gst_element_factory_make ("udpsrc", "rist_rtcp_udpsrc");
  src->rtcp_sink =
      gst_element_factory_make ("dynudpsink", "rist_rtcp_dynudpsink");
  if (!src->rtp_src || !src->rtcp_src || !src->rtcp_sink) {
    g_clear_object (&src->rtp_src);
    g_clear_object (&src->rtcp_src);
    g_clear_object (&src->rtcp_sink);
    src->missing_plugin = "udp";
    goto missing_plugin;
  }
  gst_bin_add_many (GST_BIN (src), src->rtp_src, src->rtcp_src,
      src->rtcp_sink, NULL);
  g_object_set (src->rtcp_sink, "sync", FALSE, "async", FALSE, NULL);
  /* delay udpsink startup, we will give it the socket from the RTCP udpsrc,
   * but socket can only be set in NULL state */
  gst_element_set_locked_state (src->rtcp_sink, TRUE);

  gst_element_link_pads (src->rtp_src, "src", src->rtpbin, "recv_rtp_sink_0");
  gst_element_link_pads (src->rtcp_src, "src", src->rtpbin, "recv_rtcp_sink_0");
  gst_element_link_pads (src->rtpbin, "send_rtcp_src_0",
      src->rtcp_sink, "sink");

  g_signal_connect_swapped (src->rtpbin, "pad-added",
      G_CALLBACK (gst_rist_src_pad_added), src);
  g_signal_connect_swapped (src->rtpbin, "on-new-ssrc",
      G_CALLBACK (gst_rist_src_on_new_ssrc), src);
  g_signal_connect_swapped (src->rtpbin, "new-jitterbuffer",
      G_CALLBACK (gst_rist_src_new_jitterbuffer), src);

  return;

missing_plugin:
  {
    GST_ERROR_OBJECT (src, "'%s' plugin is missing.", src->missing_plugin);
    src->construct_failed = TRUE;
  }
}

static GstPadProbeReturn
gst_rist_src_on_recv_rtcp (GstPad * pad, GstPadProbeInfo * info,
    gpointer user_data)
{
  GstRistSrc *src = GST_RIST_SRC (user_data);
  GstBuffer *buffer;
  GstNetAddressMeta *meta;

  if (info->type == GST_PAD_PROBE_TYPE_BUFFER_LIST) {
    GstBufferList *buffer_list = info->data;
    buffer = gst_buffer_list_get (buffer_list, 0);
  } else {
    buffer = info->data;
  }

  meta = gst_buffer_get_net_address_meta (buffer);

  GST_OBJECT_LOCK (src);
  g_clear_object (&src->rtcp_send_addr);
  src->rtcp_send_addr = g_object_ref (meta->addr);
  GST_OBJECT_UNLOCK (src);

  return GST_PAD_PROBE_OK;
}

static inline void
gst_rist_src_attach_net_address_meta (GstRistSrc * src, GstBuffer * buffer)
{
  GST_OBJECT_LOCK (src);
  if (src->rtcp_send_addr)
    gst_buffer_add_net_address_meta (buffer, src->rtcp_send_addr);
  GST_OBJECT_UNLOCK (src);
}

static GstPadProbeReturn
gst_rist_src_on_send_rtcp (GstPad * pad, GstPadProbeInfo * info,
    gpointer user_data)
{
  GstRistSrc *src = GST_RIST_SRC (user_data);

  if (info->type == GST_PAD_PROBE_TYPE_BUFFER_LIST) {
    GstBufferList *buffer_list = info->data;
    GstBuffer *buffer;
    gint i;

    info->data = buffer_list = gst_buffer_list_make_writable (buffer_list);
    for (i = 0; i < gst_buffer_list_length (buffer_list); i++) {
      buffer = gst_buffer_list_get (buffer_list, i);
      gst_rist_src_attach_net_address_meta (src, buffer);
    }
  } else {
    GstBuffer *buffer = info->data;
    info->data = buffer = gst_buffer_make_writable (buffer);
    gst_rist_src_attach_net_address_meta (src, buffer);
  }

  return GST_PAD_PROBE_OK;
}

static GstStateChangeReturn
gst_rist_src_start (GstRistSrc * src)
{
  GstPad *pad;
  GSocket *socket = NULL;
  gchar *address;
  guint rtcp_port;
  GInetAddress *iaddr;

  if (src->construct_failed) {
    GST_ELEMENT_ERROR (src, CORE, MISSING_PLUGIN,
        ("Your GStreamer installation is missing plugin '%s'",
            src->missing_plugin), (NULL));
    return GST_STATE_CHANGE_FAILURE;
  }

  g_object_get (src->rtcp_src, "used-socket", &socket,
      "address", &address, "port", &rtcp_port, NULL);

  iaddr = g_inet_address_new_from_string (address);
  g_free (address);

  if (g_inet_address_get_is_multicast (iaddr)) {
    /* mc-ttl is not supported by dynudpsink */
    g_socket_set_multicast_ttl (socket, src->multicast_ttl);
    /* In multicast, send RTCP to the multicast group */
    src->rtcp_send_addr = g_inet_socket_address_new (iaddr, rtcp_port);
  } else {
    /* In unicast, send RTCP to the detected sender address */
    pad = gst_element_get_static_pad (src->rtcp_src, "src");
    src->rtcp_recv_probe = gst_pad_add_probe (pad,
        GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_BUFFER_LIST,
        gst_rist_src_on_recv_rtcp, src, NULL);
    gst_object_unref (pad);
  }
  g_object_unref (iaddr);

  pad = gst_element_get_static_pad (src->rtcp_sink, "sink");
  src->rtcp_send_probe = gst_pad_add_probe (pad,
      GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_BUFFER_LIST,
      gst_rist_src_on_send_rtcp, src, NULL);
  gst_object_unref (pad);

  /* share the socket created by the source */
  g_object_set (src->rtcp_sink, "socket", socket, "close-socket", FALSE, NULL);
  g_object_unref (socket);

  gst_element_set_locked_state (src->rtcp_sink, FALSE);
  gst_element_sync_state_with_parent (src->rtcp_sink);

  return GST_STATE_CHANGE_SUCCESS;
}

static GstStructure *
gst_rist_src_create_stats (GstRistSrc * src)
{
  GObject *session = NULL, *source = NULL;
  GstStructure *stats = NULL, *ret;
  guint64 dropped = 0, received = 0, recovered = 0, lost = 0;
  guint64 duplicates = 0, rtx_sent = 0, rtt = 0;

  ret = gst_structure_new_empty ("rist/x-receiver-stats");

  g_signal_emit_by_name (src->rtpbin, "get-internal-session", 0, &session);
  if (!session)
    return ret;

  g_signal_emit_by_name (session, "get-source-by-ssrc", src->rtp_ssrc, &source);
  if (source) {
    gint packets_lost;
    g_object_get (source, "stats", &stats, NULL);
    gst_structure_get_int (stats, "packets-lost", &packets_lost);
    gst_structure_free (stats);
    g_clear_object (&source);
    dropped = MAX (packets_lost, 0);
  }
  g_object_unref (session);

  if (src->jitterbuffer) {
    g_object_get (src->jitterbuffer, "stats", &stats, NULL);
    gst_structure_get (stats, "num-pushed", G_TYPE_UINT64, &received,
        "num-lost", G_TYPE_UINT64, &lost,
        "rtx-count", G_TYPE_UINT64, &rtx_sent,
        "num-duplicates", G_TYPE_UINT64, &duplicates,
        "rtx-success-count", G_TYPE_UINT64, &recovered,
        "rtx-rtt", G_TYPE_UINT64, &rtt, NULL);
    gst_structure_free (stats);
  }

  gst_structure_set (ret, "dropped", G_TYPE_UINT64, dropped,
      "received", G_TYPE_UINT64, received,
      "recovered", G_TYPE_UINT64, recovered,
      "permanently-lost", G_TYPE_UINT64, lost,
      "duplicates", G_TYPE_UINT64, duplicates,
      "retransmission-requests-sent", G_TYPE_UINT64, rtx_sent,
      "rtx-roundtrip-time", G_TYPE_UINT64, rtt, NULL);

  return ret;
}

static gboolean
gst_rist_src_dump_stats (GstClock * clock, GstClockTime time, GstClockID id,
    gpointer user_data)
{
  GstRistSrc *src = GST_RIST_SRC (user_data);
  GstStructure *stats = gst_rist_src_create_stats (src);

  gst_println ("%s: %" GST_PTR_FORMAT, GST_OBJECT_NAME (src), stats);

  gst_structure_free (stats);
  return TRUE;
}

static void
gst_rist_src_enable_stats_interval (GstRistSrc * src)
{
  GstClock *clock;
  GstClockTime start, interval;

  if (src->stats_interval == 0)
    return;

  interval = src->stats_interval * GST_MSECOND;
  clock = gst_system_clock_obtain ();
  start = gst_clock_get_time (clock) + interval;

  src->stats_cid = gst_clock_new_periodic_id (clock, start, interval);
  gst_clock_id_wait_async (src->stats_cid, gst_rist_src_dump_stats,
      gst_object_ref (src), (GDestroyNotify) gst_object_unref);

  gst_object_unref (clock);
}

static void
gst_rist_src_disable_stats_interval (GstRistSrc * src)
{
  if (src->stats_cid) {
    gst_clock_id_unschedule (src->stats_cid);
    gst_clock_id_unref (src->stats_cid);
    src->stats_cid = NULL;
  }
}

static void
gst_rist_src_stop (GstRistSrc * src)
{
  GstPad *pad;

  if (src->rtcp_recv_probe) {
    pad = gst_element_get_static_pad (src->rtcp_src, "src");
    gst_pad_remove_probe (pad, src->rtcp_recv_probe);
    src->rtcp_recv_probe = 0;
    gst_object_unref (pad);
  }

  pad = gst_element_get_static_pad (src->rtcp_sink, "sink");
  gst_pad_remove_probe (pad, src->rtcp_send_probe);
  src->rtcp_send_probe = 0;
  gst_object_unref (pad);
}

static GstStateChangeReturn
gst_rist_src_change_state (GstElement * element, GstStateChange transition)
{
  GstRistSrc *src = GST_RIST_SRC (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_rist_src_disable_stats_interval (src);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (gst_rist_src_parent_class)->change_state (element,
      transition);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      gst_rist_src_start (src);
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_rist_src_enable_stats_interval (src);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_rist_src_stop (src);
      break;
    default:
      break;
  }

  return ret;
}

static void
gst_rist_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstRistSrc *src = GST_RIST_SRC (object);
  GstElement *session = NULL;
  GstClockTime interval;
  GstStructure *sdes;

  if (src->construct_failed)
    return;

  switch (prop_id) {
    case PROP_ADDRESS:
      g_object_get_property (G_OBJECT (src->rtp_src), "address", value);
      break;

    case PROP_PORT:
      g_object_get_property (G_OBJECT (src->rtp_src), "port", value);
      break;

    case PROP_RECEIVER_BUFFER:
      g_object_get_property (G_OBJECT (src->rtpbin), "latency", value);
      break;

    case PROP_REORDER_SECTION:
      GST_OBJECT_LOCK (src);
      g_value_set_uint (value, src->reorder_section);
      GST_OBJECT_UNLOCK (src);
      break;

    case PROP_MAX_RTX_RETRIES:
      GST_OBJECT_LOCK (src);
      g_value_set_uint (value, src->max_rtx_retries);
      GST_OBJECT_UNLOCK (src);
      break;

    case PROP_MIN_RTCP_INTERVAL:
      g_signal_emit_by_name (src->rtpbin, "get-session", 0, &session);
      g_object_get (session, "rtcp-min-interval", &interval, NULL);
      g_value_set_uint (value, (guint) (interval / GST_MSECOND));
      g_object_unref (session);
      break;

    case PROP_MAX_RTCP_BANDWIDTH:
      g_signal_emit_by_name (src->rtpbin, "get-session", 0, &session);
      g_object_get_property (G_OBJECT (session), "rtcp-fraction", value);
      g_object_unref (session);
      break;

    case PROP_STATS_UPDATE_INTERVAL:
      g_value_set_uint (value, src->stats_interval);
      break;

    case PROP_STATS:
      g_value_take_boxed (value, gst_rist_src_create_stats (src));
      break;

    case PROP_CNAME:
      g_object_get (src->rtpbin, "sdes", &sdes, NULL);
      g_value_set_string (value, gst_structure_get_string (sdes, "cname"));
      gst_structure_free (sdes);
      break;

    case PROP_MULTICAST_LOOPBACK:
      g_object_get_property (G_OBJECT (src->rtp_src), "loop", value);
      break;

    case PROP_MULTICAST_IFACE:
      g_object_get_property (G_OBJECT (src->rtp_src), "multicast-iface", value);
      break;

    case PROP_MULTICAST_TTL:
      g_value_set_int (value, src->multicast_ttl);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rist_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRistSrc *src = GST_RIST_SRC (object);
  GstElement *session = NULL;
  GstStructure *sdes;

  if (src->construct_failed)
    return;

  switch (prop_id) {
    case PROP_ADDRESS:
      g_object_set_property (G_OBJECT (src->rtp_src), "address", value);
      g_object_set_property (G_OBJECT (src->rtcp_src), "address", value);
      break;

    case PROP_PORT:{
      guint port = g_value_get_uint (value);

      /* According to 5.1.1, RTCP receiver port most be event number and RTCP
       * port should be the RTP port + 1 */

      if (port & 0x1) {
        g_warning ("Invalid RIST port %u, should be an even number.", port);
        return;
      }

      g_object_set (src->rtp_src, "port", port, NULL);
      g_object_set (src->rtcp_src, "port", port + 1, NULL);
      break;
    }

    case PROP_RECEIVER_BUFFER:
      g_object_set (src->rtpbin, "latency", g_value_get_uint (value), NULL);
      break;

    case PROP_REORDER_SECTION:
      GST_OBJECT_LOCK (src);
      src->reorder_section = g_value_get_uint (value);
      GST_OBJECT_UNLOCK (src);
      break;

    case PROP_MAX_RTX_RETRIES:
      GST_OBJECT_LOCK (src);
      src->max_rtx_retries = g_value_get_uint (value);
      GST_OBJECT_UNLOCK (src);
      break;

    case PROP_MIN_RTCP_INTERVAL:
      g_signal_emit_by_name (src->rtpbin, "get-session", 0, &session);
      g_object_set (session, "rtcp-min-interval",
          g_value_get_uint (value) * GST_MSECOND, NULL);
      g_object_unref (session);
      break;

    case PROP_MAX_RTCP_BANDWIDTH:
      g_signal_emit_by_name (src->rtpbin, "get-session", 0, &session);
      g_object_set (session, "rtcp-fraction", g_value_get_double (value), NULL);
      g_object_unref (session);
      break;

    case PROP_STATS_UPDATE_INTERVAL:
      src->stats_interval = g_value_get_uint (value);
      break;

    case PROP_CNAME:
      g_object_get (src->rtpbin, "sdes", &sdes, NULL);
      gst_structure_set_value (sdes, "cname", value);
      g_object_set (src->rtpbin, "sdes", sdes, NULL);
      gst_structure_free (sdes);
      break;

    case PROP_MULTICAST_LOOPBACK:
      g_object_set_property (G_OBJECT (src->rtp_src), "loop", value);
      g_object_set_property (G_OBJECT (src->rtcp_src), "loop", value);
      break;

    case PROP_MULTICAST_IFACE:
      g_object_set_property (G_OBJECT (src->rtp_src), "multicast-iface", value);
      g_object_set_property (G_OBJECT (src->rtcp_src),
          "multicast-iface", value);
      break;

    case PROP_MULTICAST_TTL:
      src->multicast_ttl = g_value_get_int (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rist_src_finalize (GObject * object)
{
  GstRistSrc *src = GST_RIST_SRC (object);

  if (src->jitterbuffer)
    gst_object_unref (src->jitterbuffer);

  gst_object_unref (src->rtxbin);

  G_OBJECT_CLASS (gst_rist_src_parent_class)->finalize (object);
}

static void
gst_rist_src_class_init (GstRistSrcClass * klass)
{
  GstElementClass *element_class = (GstElementClass *) klass;
  GObjectClass *object_class = (GObjectClass *) klass;

  gst_element_class_set_metadata (element_class,
      "RIST Source", "Source/Network",
      "Source that implements RIST TR-06-1 streaming specification",
      "Nicolas Dufresne <nicolas.dufresne@collabora.com");
  gst_element_class_add_static_pad_template (element_class, &src_templ);

  element_class->change_state = gst_rist_src_change_state;

  object_class->get_property = gst_rist_src_get_property;
  object_class->set_property = gst_rist_src_set_property;
  object_class->finalize = gst_rist_src_finalize;

  g_object_class_install_property (object_class, PROP_ADDRESS,
      g_param_spec_string ("address", "Address",
          "Address to receive packets from (can be IPv4 or IPv6).", "0.0.0.0",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_PORT,
      g_param_spec_uint ("port", "Port", "The port to listen for RTP packets, "
          "RTCP port is derived from it, this port must be an even number.",
          2, 65534, 5004,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT));

  g_object_class_install_property (object_class, PROP_RECEIVER_BUFFER,
      g_param_spec_uint ("receiver-buffer", "Receiver Buffer",
          "Buffering duration in ms", 0, G_MAXUINT, 1000,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT));

  g_object_class_install_property (object_class, PROP_REORDER_SECTION,
      g_param_spec_uint ("reorder-section", "Recorder Section",
          "Time to wait before sending retransmission request in ms.",
          0, G_MAXUINT, 70,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT));

  g_object_class_install_property (object_class, PROP_MAX_RTX_RETRIES,
      g_param_spec_uint ("max-rtx-retries", "Maximum Retransmission Retries",
          "The maximum number of retransmission requests for a lost packet.",
          0, G_MAXUINT, 7,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT));

  g_object_class_install_property (object_class, PROP_MIN_RTCP_INTERVAL,
      g_param_spec_uint ("min-rtcp-interval", "Minimum RTCP Intercal",
          "The minimum interval in ms between two successive RTCP packets",
          0, 100, 100,
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
          "Statistic in a GstStructure named 'rist/x-receiver-stats'",
          GST_TYPE_STRUCTURE, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_CNAME,
      g_param_spec_string ("cname", "CName",
          "Set the CNAME in the SDES block of the receiver report.", NULL,
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

static GstURIType
gst_rist_src_uri_get_type (GType type)
{
  return GST_URI_SRC;
}

static const gchar *const *
gst_rist_src_uri_get_protocols (GType type)
{
  static const char *protocols[] = { "rist", NULL };
  return protocols;
}

static gchar *
gst_rist_src_uri_get_uri (GstURIHandler * handler)
{
  GstRistSrc *src = GST_RIST_SRC (handler);
  gchar *uri = NULL;

  GST_OBJECT_LOCK (src);
  if (src->uri)
    uri = gst_uri_to_string (src->uri);
  GST_OBJECT_UNLOCK (src);

  return uri;
}

static void
gst_rist_src_uri_query_foreach (const gchar * key, const gchar * value,
    GObject * src)
{
  if (g_str_equal (key, "async-handling")) {
    GST_WARNING_OBJECT (src, "Setting '%s' property from URI is not allowed.",
        key);
    return;
  }

  GST_DEBUG_OBJECT (src, "Setting property '%s' to '%s'", key, value);
  gst_util_set_object_arg (src, key, value);
}

static gboolean
gst_rist_src_uri_set_uri (GstURIHandler * handler, const gchar * uri,
    GError ** error)
{
  GstRistSrc *src = GST_RIST_SRC (handler);
  GstUri *gsturi;
  GHashTable *query_table;

  if (GST_STATE (src) >= GST_STATE_PAUSED) {
    g_set_error (error, GST_URI_ERROR, GST_URI_ERROR_BAD_STATE,
        "Changing the URI on ristsrc when it is running is not supported");
    GST_ERROR_OBJECT (src, "%s", (*error)->message);
    return FALSE;
  }

  if (!(gsturi = gst_uri_from_string (uri))) {
    g_set_error (error, GST_URI_ERROR, GST_URI_ERROR_BAD_URI,
        "Could not parse URI");
    GST_ERROR_OBJECT (src, "%s", (*error)->message);
    gst_uri_unref (gsturi);
    return FALSE;
  }

  GST_OBJECT_LOCK (src);
  if (src->uri)
    gst_uri_unref (src->uri);
  src->uri = gst_uri_ref (gsturi);
  GST_OBJECT_UNLOCK (src);

  g_object_set (src, "address", gst_uri_get_host (gsturi), NULL);
  if (gst_uri_get_port (gsturi))
    g_object_set (src, "port", gst_uri_get_port (gsturi), NULL);

  query_table = gst_uri_get_query_table (gsturi);
  if (query_table)
    g_hash_table_foreach (query_table,
        (GHFunc) gst_rist_src_uri_query_foreach, src);

  gst_uri_unref (gsturi);
  return TRUE;
}

static void
gst_rist_src_uri_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = gst_rist_src_uri_get_type;
  iface->get_protocols = gst_rist_src_uri_get_protocols;
  iface->get_uri = gst_rist_src_uri_get_uri;
  iface->set_uri = gst_rist_src_uri_set_uri;
}
