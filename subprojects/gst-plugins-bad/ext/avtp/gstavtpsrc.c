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
 * SECTION:element-avtpsrc
 * @see_also: avtpsink
 *
 * avtpsrc is a network source that receives AVTPDUs from the network. It
 * should be combined with AVTP depayloaders to implement an AVTP listener.
 * For more information see https://standards.ieee.org/standard/1722-2016.html.
 *
 * <note>
 * Applications must have CAP_NET_RAW capability in order to successfully use
 * this element. See avtpsink documentation for further information.
 * </note>
 *
 * <refsect2>
 * <title>Example pipeline</title>
 * |[
 * gst-launch-1.0 avtpsrc ! avtpaafdepay ! autoaudiosink
 * ]| This example pipeline implements an AVTP listener that plays an AAF
 * stream back.
 * </refsect2>
 */

#include <arpa/inet.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "gstavtpsrc.h"

GST_DEBUG_CATEGORY_STATIC (avtpsrc_debug);
#define GST_CAT_DEFAULT (avtpsrc_debug)

#define DEFAULT_IFNAME "eth0"
#define DEFAULT_ADDRESS "01:AA:AA:AA:AA:AA"

#define MAX_AVTPDU_SIZE 1500

enum
{
  PROP_0,
  PROP_IFNAME,
  PROP_ADDRESS,
};

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-avtp")
    );

#define gst_avtp_src_parent_class parent_class
G_DEFINE_TYPE (GstAvtpSrc, gst_avtp_src, GST_TYPE_PUSH_SRC);
GST_ELEMENT_REGISTER_DEFINE (avtpsrc, "avtpsrc", GST_RANK_NONE,
    GST_TYPE_AVTP_SRC);

static void gst_avtp_src_finalize (GObject * gobject);
static void gst_avtp_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_avtp_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_avtp_src_start (GstBaseSrc * basesrc);
static gboolean gst_avtp_src_stop (GstBaseSrc * basesrc);
static gboolean gst_avtp_src_unlock (GstBaseSrc * basesrc);
static gboolean gst_avtp_src_unlock_stop (GstBaseSrc * basesrc);
static GstFlowReturn gst_avtp_src_fill (GstPushSrc * pushsrc, GstBuffer *
    buffer);

static void
gst_avtp_src_class_init (GstAvtpSrcClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseSrcClass *basesrc_class = GST_BASE_SRC_CLASS (klass);
  GstPushSrcClass *pushsrc_class = GST_PUSH_SRC_CLASS (klass);

  object_class->finalize = gst_avtp_src_finalize;
  object_class->get_property = gst_avtp_src_get_property;
  object_class->set_property = gst_avtp_src_set_property;

  g_object_class_install_property (object_class, PROP_IFNAME,
      g_param_spec_string ("ifname", "Interface Name",
          "Network interface utilized to receive AVTPDUs",
          DEFAULT_IFNAME, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));
  g_object_class_install_property (object_class, PROP_ADDRESS,
      g_param_spec_string ("address", "Destination MAC address",
          "Destination MAC address to listen to",
          DEFAULT_ADDRESS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  gst_element_class_add_static_pad_template (element_class, &src_template);

  gst_element_class_set_static_metadata (element_class,
      "Audio/Video Transport Protocol (AVTP) Source",
      "Src/Network", "Receive AVTPDUs from the network",
      "Andre Guedes <andre.guedes@intel.com>");

  basesrc_class->start = GST_DEBUG_FUNCPTR (gst_avtp_src_start);
  basesrc_class->stop = GST_DEBUG_FUNCPTR (gst_avtp_src_stop);
  basesrc_class->unlock = GST_DEBUG_FUNCPTR (gst_avtp_src_unlock);
  basesrc_class->unlock_stop = GST_DEBUG_FUNCPTR (gst_avtp_src_unlock_stop);
  pushsrc_class->fill = GST_DEBUG_FUNCPTR (gst_avtp_src_fill);

  GST_DEBUG_CATEGORY_INIT (avtpsrc_debug, "avtpsrc", 0, "AVTP Source");
}

static void
gst_avtp_src_init (GstAvtpSrc * avtpsrc)
{
  gst_base_src_set_live (GST_BASE_SRC (avtpsrc), TRUE);
  gst_base_src_set_format (GST_BASE_SRC (avtpsrc), GST_FORMAT_TIME);
  gst_base_src_set_blocksize (GST_BASE_SRC (avtpsrc), MAX_AVTPDU_SIZE);

  avtpsrc->ifname = g_strdup (DEFAULT_IFNAME);
  avtpsrc->address = g_strdup (DEFAULT_ADDRESS);
}

static void
gst_avtp_src_finalize (GObject * object)
{
  GstAvtpSrc *avtpsrc = GST_AVTP_SRC (object);

  g_free (avtpsrc->ifname);
  g_free (avtpsrc->address);

  g_clear_object (&avtpsrc->cancellable);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_avtp_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAvtpSrc *avtpsrc = GST_AVTP_SRC (object);

  GST_DEBUG_OBJECT (avtpsrc, "prop_id %u", prop_id);

  switch (prop_id) {
    case PROP_IFNAME:
      g_free (avtpsrc->ifname);
      avtpsrc->ifname = g_value_dup_string (value);
      break;
    case PROP_ADDRESS:
      g_free (avtpsrc->address);
      avtpsrc->address = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_avtp_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAvtpSrc *avtpsrc = GST_AVTP_SRC (object);

  GST_DEBUG_OBJECT (avtpsrc, "prop_id %u", prop_id);

  switch (prop_id) {
    case PROP_IFNAME:
      g_value_set_string (value, avtpsrc->ifname);
      break;
    case PROP_ADDRESS:
      g_value_set_string (value, avtpsrc->address);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_avtp_src_start (GstBaseSrc * basesrc)
{
  int fd, res;
  unsigned int index;
  guint8 addr[ETH_ALEN];
  struct sockaddr_ll sk_addr = { 0 };
  struct packet_mreq mreq = { 0 };
  GstAvtpSrc *avtpsrc = GST_AVTP_SRC (basesrc);
  GError *gerr = NULL;

  index = if_nametoindex (avtpsrc->ifname);
  if (!index) {
    GST_ELEMENT_ERROR (avtpsrc, RESOURCE, OPEN_READ, (NULL),
        ("Failed to get if_index: %s", g_strerror (errno)));
    return FALSE;
  }

  fd = socket (AF_PACKET, SOCK_DGRAM, htons (ETH_P_TSN));
  if (fd < 0) {
    GST_ELEMENT_ERROR (avtpsrc, RESOURCE, OPEN_READ, (NULL),
        ("Failed to open socket: %s", g_strerror (errno)));
    return FALSE;
  }

  sk_addr.sll_family = AF_PACKET;
  sk_addr.sll_protocol = htons (ETH_P_TSN);
  sk_addr.sll_ifindex = index;

  res = bind (fd, (struct sockaddr *) &sk_addr, sizeof (sk_addr));
  if (res < 0) {
    GST_ELEMENT_ERROR (avtpsrc, RESOURCE, SETTINGS, (NULL),
        ("Failed to bind socket: %s", g_strerror (errno)));
    goto err;
  }

  res = sscanf (avtpsrc->address, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
      &addr[0], &addr[1], &addr[2], &addr[3], &addr[4], &addr[5]);
  if (res != 6) {
    GST_ELEMENT_ERROR (avtpsrc, RESOURCE, SETTINGS, (NULL),
        ("Destination MAC address format not valid"));
    goto err;
  }

  mreq.mr_ifindex = index;
  mreq.mr_type = PACKET_MR_MULTICAST;
  mreq.mr_alen = ETH_ALEN;
  memcpy (&mreq.mr_address, addr, ETH_ALEN);
  res = setsockopt (fd, SOL_PACKET, PACKET_ADD_MEMBERSHIP, &mreq,
      sizeof (struct packet_mreq));
  if (res < 0) {
    GST_ELEMENT_ERROR (avtpsrc, RESOURCE, SETTINGS, (NULL),
        ("Failed to set multicast address: %s", g_strerror (errno)));
    goto err;
  }

  avtpsrc->socket = g_socket_new_from_fd (fd, &gerr);
  if (gerr) {
    GST_ELEMENT_ERROR (avtpsrc, RESOURCE, SETTINGS, (NULL),
        ("Could not create socket object: %s", gerr->message));
    g_clear_error (&gerr);
    goto err;
  }

  avtpsrc->cancellable = g_cancellable_new ();

  GST_DEBUG_OBJECT (avtpsrc, "AVTP source started");
  return TRUE;

err:
  close (fd);
  return FALSE;
}

static gboolean
gst_avtp_src_stop (GstBaseSrc * basesrc)
{
  GstAvtpSrc *avtpsrc = GST_AVTP_SRC (basesrc);

  GST_OBJECT_LOCK (avtpsrc);
  g_cancellable_cancel (avtpsrc->cancellable);
  g_clear_object (&avtpsrc->cancellable);
  GST_OBJECT_UNLOCK (avtpsrc);

  if (avtpsrc->socket)
    g_socket_close (avtpsrc->socket, NULL);
  g_clear_object (&avtpsrc->socket);

  GST_DEBUG_OBJECT (avtpsrc, "AVTP source stopped");
  return TRUE;
}

static gboolean
gst_avtp_src_unlock (GstBaseSrc * basesrc)
{
  GstAvtpSrc *avtpsrc = GST_AVTP_SRC (basesrc);

  GST_OBJECT_LOCK (avtpsrc);
  g_cancellable_cancel (avtpsrc->cancellable);
  GST_OBJECT_UNLOCK (avtpsrc);

  return TRUE;
}

static gboolean
gst_avtp_src_unlock_stop (GstBaseSrc * basesrc)
{
  GstAvtpSrc *avtpsrc = GST_AVTP_SRC (basesrc);

  GST_OBJECT_LOCK (avtpsrc);
  g_clear_object (&avtpsrc->cancellable);
  avtpsrc->cancellable = g_cancellable_new ();
  GST_OBJECT_UNLOCK (avtpsrc);

  return TRUE;
}


static GstFlowReturn
gst_avtp_src_fill (GstPushSrc * pushsrc, GstBuffer * buffer)
{
  GstMapInfo map;
  gsize buffer_size;
  gssize n = MAX_AVTPDU_SIZE;
  gssize received = -1;
  GstAvtpSrc *avtpsrc = GST_AVTP_SRC (pushsrc);
  GError *gerr = NULL;
  GCancellable *cancellable;

  buffer_size = gst_buffer_get_size (buffer);
  if (G_UNLIKELY (buffer_size < MAX_AVTPDU_SIZE)) {
    GST_WARNING_OBJECT (avtpsrc,
        "Buffer size (%" G_GSIZE_FORMAT
        ") may not be enough to hold AVTPDU (max AVTPDU size %d)", buffer_size,
        MAX_AVTPDU_SIZE);
    n = buffer_size;
  }

  if (!gst_buffer_map (buffer, &map, GST_MAP_WRITE)) {
    GST_WARNING_OBJECT (avtpsrc, "Failed to map buffer");
    return GST_FLOW_OK;
  }

  GST_OBJECT_LOCK (avtpsrc);
  cancellable = g_object_ref (avtpsrc->cancellable);
  GST_OBJECT_UNLOCK (avtpsrc);

  received = g_socket_receive (avtpsrc->socket,
      (gchar *) map.data, n, cancellable, &gerr);

  g_object_unref (cancellable);

  gst_buffer_unmap (buffer, &map);

  if (g_error_matches (gerr, G_IO_ERROR, G_IO_ERROR_BUSY) ||
      g_error_matches (gerr, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
    g_clear_error (&gerr);
    return GST_FLOW_FLUSHING;
  }

  if (gerr) {
    GST_ELEMENT_ERROR (avtpsrc, RESOURCE, READ, (NULL),
        ("Failed to receive AVTPDU: %s", gerr->message));
    gst_buffer_unmap (buffer, &map);
    g_clear_error (&gerr);

    return GST_FLOW_ERROR;
  }

  gst_buffer_set_size (buffer, received);

  return GST_FLOW_OK;
}
