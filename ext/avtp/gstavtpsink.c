/*
 * GStreamer AVTP Plugin
 * Copyright (C) 2019 Intel Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later
 * version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA
 */

/**
 * SECTION:element-avtpsink
 * @see_also: avtpsrc
 *
 * avtpsink is a network sink that sends AVTPDUs to the network. It should be
 * combined with AVTP payloaders to implement an AVTP talker. For more
 * information see https://standards.ieee.org/standard/1722-2016.html.
 *
 * <note>
 * This element opens an AF_PACKET socket which requires CAP_NET_RAW
 * capability. Therefore, applications must have that capability in order to
 * successfully use this element. The capability can be dropped by the
 * application after the element transitions to PAUSED state if wanted.
 * </note>
 *
 * <refsect2>
 * <title>Example pipeline</title>
 * |[
 * gst-launch-1.0 audiotestsrc ! audioconvert ! avtpaafpay ! avtpsink
 * ]| This example pipeline implements an AVTP talker that transmit an AAF
 * stream.
 * </refsect2>
 */

#include <arpa/inet.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "gstavtpsink.h"

GST_DEBUG_CATEGORY_STATIC (avtpsink_debug);
#define GST_CAT_DEFAULT (avtpsink_debug)

#define DEFAULT_IFNAME "eth0"
#define DEFAULT_ADDRESS "01:AA:AA:AA:AA:AA"
#define DEFAULT_PRIORITY 0

enum
{
  PROP_0,
  PROP_IFNAME,
  PROP_ADDRESS,
  PROP_PRIORITY,
};

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-avtp")
    );

#define gst_avtp_sink_parent_class parent_class
G_DEFINE_TYPE (GstAvtpSink, gst_avtp_sink, GST_TYPE_BASE_SINK);

static void gst_avtp_sink_finalize (GObject * gobject);
static void gst_avtp_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_avtp_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_avtp_sink_start (GstBaseSink * basesink);
static gboolean gst_avtp_sink_stop (GstBaseSink * basesink);
static GstFlowReturn gst_avtp_sink_render (GstBaseSink * basesink, GstBuffer *
    buffer);

static void
gst_avtp_sink_class_init (GstAvtpSinkClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseSinkClass *basesink_class = GST_BASE_SINK_CLASS (klass);

  object_class->finalize = gst_avtp_sink_finalize;
  object_class->get_property = gst_avtp_sink_get_property;
  object_class->set_property = gst_avtp_sink_set_property;

  g_object_class_install_property (object_class, PROP_IFNAME,
      g_param_spec_string ("ifname", "Interface Name",
          "Network interface utilized to transmit AVTPDUs",
          DEFAULT_IFNAME, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));
  g_object_class_install_property (object_class, PROP_ADDRESS,
      g_param_spec_string ("address", "Destination MAC address",
          "Destination MAC address from Ethernet frames",
          DEFAULT_ADDRESS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));
  g_object_class_install_property (object_class, PROP_PRIORITY,
      g_param_spec_int ("priority", "Socket priority",
          "Priority configured into socket (SO_PRIORITY)", 0, G_MAXINT,
          DEFAULT_PRIORITY, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  gst_element_class_add_static_pad_template (element_class, &sink_template);

  gst_element_class_set_static_metadata (element_class,
      "Audio/Video Transport Protocol (AVTP) Sink",
      "Sink/Network", "Send AVTPDUs over the network",
      "Andre Guedes <andre.guedes@intel.com>");

  basesink_class->start = GST_DEBUG_FUNCPTR (gst_avtp_sink_start);
  basesink_class->stop = GST_DEBUG_FUNCPTR (gst_avtp_sink_stop);
  basesink_class->render = GST_DEBUG_FUNCPTR (gst_avtp_sink_render);

  GST_DEBUG_CATEGORY_INIT (avtpsink_debug, "avtpsink", 0, "AVTP Sink");
}

static void
gst_avtp_sink_init (GstAvtpSink * avtpsink)
{
  gst_base_sink_set_sync (GST_BASE_SINK (avtpsink), TRUE);

  avtpsink->ifname = g_strdup (DEFAULT_IFNAME);
  avtpsink->address = g_strdup (DEFAULT_ADDRESS);
  avtpsink->priority = DEFAULT_PRIORITY;
  avtpsink->sk_fd = -1;
  memset (&avtpsink->sk_addr, 0, sizeof (avtpsink->sk_addr));
}

static void
gst_avtp_sink_finalize (GObject * object)
{
  GstAvtpSink *avtpsink = GST_AVTP_SINK (object);

  g_free (avtpsink->ifname);
  g_free (avtpsink->address);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_avtp_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAvtpSink *avtpsink = GST_AVTP_SINK (object);

  GST_DEBUG_OBJECT (avtpsink, "prop_id %u", prop_id);

  switch (prop_id) {
    case PROP_IFNAME:
      g_free (avtpsink->ifname);
      avtpsink->ifname = g_value_dup_string (value);
      break;
    case PROP_ADDRESS:
      g_free (avtpsink->address);
      avtpsink->address = g_value_dup_string (value);
      break;
    case PROP_PRIORITY:
      avtpsink->priority = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_avtp_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAvtpSink *avtpsink = GST_AVTP_SINK (object);

  GST_DEBUG_OBJECT (avtpsink, "prop_id %u", prop_id);

  switch (prop_id) {
    case PROP_IFNAME:
      g_value_set_string (value, avtpsink->ifname);
      break;
    case PROP_ADDRESS:
      g_value_set_string (value, avtpsink->address);
      break;
    case PROP_PRIORITY:
      g_value_set_int (value, avtpsink->priority);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_avtp_sink_start (GstBaseSink * basesink)
{
  int fd, res;
  struct ifreq req;
  guint8 addr[ETH_ALEN];
  struct sockaddr_ll sk_addr;
  GstAvtpSink *avtpsink = GST_AVTP_SINK (basesink);

  fd = socket (AF_PACKET, SOCK_DGRAM | SOCK_NONBLOCK, htons (ETH_P_TSN));
  if (fd < 0) {
    GST_ERROR_OBJECT (avtpsink, "Failed to open socket: %s", strerror (errno));
    return FALSE;
  }

  res = setsockopt (fd, SOL_SOCKET, SO_PRIORITY, &avtpsink->priority,
      sizeof (avtpsink->priority));
  if (res < 0) {
    GST_ERROR_OBJECT (avtpsink, "Failed to socket priority: %s", strerror
        (errno));
    goto err;
  }

  res = sscanf (avtpsink->address, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
      &addr[0], &addr[1], &addr[2], &addr[3], &addr[4], &addr[5]);
  if (res != 6) {
    GST_ERROR_OBJECT (avtpsink, "Destination MAC address format not valid");
    goto err;
  }

  snprintf (req.ifr_name, sizeof (req.ifr_name), "%s", avtpsink->ifname);
  res = ioctl (fd, SIOCGIFINDEX, &req);
  if (res < 0) {
    GST_ERROR_OBJECT (avtpsink, "Failed to ioctl(): %s", strerror (errno));
    goto err;
  }

  sk_addr.sll_family = AF_PACKET;
  sk_addr.sll_protocol = htons (ETH_P_TSN);
  sk_addr.sll_halen = ETH_ALEN;
  sk_addr.sll_ifindex = req.ifr_ifindex;
  sk_addr.sll_hatype = 0;
  sk_addr.sll_pkttype = 0;
  memcpy (sk_addr.sll_addr, addr, ETH_ALEN);

  avtpsink->sk_fd = fd;
  avtpsink->sk_addr = sk_addr;

  GST_DEBUG_OBJECT (avtpsink, "AVTP sink started");
  return TRUE;

err:
  close (fd);
  return FALSE;
}

static gboolean
gst_avtp_sink_stop (GstBaseSink * basesink)
{
  GstAvtpSink *avtpsink = GST_AVTP_SINK (basesink);

  close (avtpsink->sk_fd);

  GST_DEBUG_OBJECT (avtpsink, "AVTP sink stopped");
  return TRUE;
}

static GstFlowReturn
gst_avtp_sink_render (GstBaseSink * basesink, GstBuffer * buffer)
{
  ssize_t n;
  GstMapInfo info;
  GstAvtpSink *avtpsink = GST_AVTP_SINK (basesink);

  if (!gst_buffer_map (buffer, &info, GST_MAP_READ)) {
    GST_ERROR_OBJECT (avtpsink, "Failed to map buffer");
    return GST_FLOW_ERROR;
  }

  n = sendto (avtpsink->sk_fd, info.data, info.size, 0,
      (struct sockaddr *) &avtpsink->sk_addr, sizeof (avtpsink->sk_addr));
  if (n < 0) {
    GST_INFO_OBJECT (avtpsink, "Failed to send AVTPDU: %s", strerror (errno));
    goto out;
  }
  if (n != info.size) {
    GST_INFO_OBJECT (avtpsink, "Incomplete AVTPDU transmission");
    goto out;
  }

out:
  gst_buffer_unmap (buffer, &info);
  return GST_FLOW_OK;
}

gboolean
gst_avtp_sink_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "avtpsink", GST_RANK_NONE,
      GST_TYPE_AVTP_SINK);
}
