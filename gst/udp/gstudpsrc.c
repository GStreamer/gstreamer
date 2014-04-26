/* GStreamer
 * Copyright (C) <2005> Wim Taymans <wim@fluendo.com>
 * Copyright (C) <2005> Nokia Corporation <kai.vehmanen@nokia.com>
 * Copyright (C) <2012> Collabora Ltd.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
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
 * SECTION:element-udpsrc
 * @see_also: udpsink, multifdsink
 *
 * udpsrc is a network source that reads UDP packets from the network.
 * It can be combined with RTP depayloaders to implement RTP streaming.
 *
 * The udpsrc element supports automatic port allocation by setting the
 * #GstUDPSrc:port property to 0. After setting the udpsrc to PAUSED, the
 * allocated port can be obtained by reading the port property.
 *
 * udpsrc can read from multicast groups by setting the #GstUDPSrc:multicast-group
 * property to the IP address of the multicast group.
 *
 * Alternatively one can provide a custom socket to udpsrc with the #GstUDPSrc:sockfd
 * property, udpsrc will then not allocate a socket itself but use the provided
 * one.
 *
 * The #GstUDPSrc:caps property is mainly used to give a type to the UDP packet
 * so that they can be autoplugged in GStreamer pipelines. This is very usefull
 * for RTP implementations where the contents of the UDP packets is transfered
 * out-of-bounds using SDP or other means.
 *
 * The #GstUDPSrc:buffer-size property is used to change the default kernel
 * buffersizes used for receiving packets. The buffer size may be increased for
 * high-volume connections, or may be decreased to limit the possible backlog of
 * incoming data. The system places an absolute limit on these values, on Linux,
 * for example, the default buffer size is typically 50K and can be increased to
 * maximally 100K.
 *
 * The #GstUDPSrc:skip-first-bytes property is used to strip off an arbitrary
 * number of bytes from the start of the raw udp packet and can be used to strip
 * off proprietary header, for example.
 *
 * The udpsrc is always a live source. It does however not provide a #GstClock,
 * this is left for upstream elements such as an RTP session manager or demuxer
 * (such as an MPEG demuxer). As with all live sources, the captured buffers
 * will have their timestamp set to the current running time of the pipeline.
 *
 * udpsrc implements a #GstURIHandler interface that handles udp://host:port
 * type URIs.
 *
 * If the #GstUDPSrc:timeout property is set to a value bigger than 0, udpsrc
 * will generate an element message named
 * <classname>&quot;GstUDPSrcTimeout&quot;</classname>
 * if no data was recieved in the given timeout.
 * The message's structure contains one field:
 * <itemizedlist>
 * <listitem>
 *   <para>
 *   #guint64
 *   <classname>&quot;timeout&quot;</classname>: the timeout in microseconds that
 *   expired when waiting for data.
 *   </para>
 * </listitem>
 * </itemizedlist>
 * The message is typically used to detect that no UDP arrives in the receiver
 * because it is blocked by a firewall.
 *
 * A custom file descriptor can be configured with the
 * #GstUDPSrc:sockfd property. The socket will be closed when setting the
 * element to READY by default. This behaviour can be
 * overriden with the #GstUDPSrc:closefd property, in which case the application
 * is responsible for closing the file descriptor.
 *
 * <refsect2>
 * <title>Examples</title>
 * |[
 * gst-launch-1.0 -v udpsrc ! fakesink dump=1
 * ]| A pipeline to read from the default port and dump the udp packets.
 * To actually generate udp packets on the default port one can use the
 * udpsink element. When running the following pipeline in another terminal, the
 * above mentioned pipeline should dump data packets to the console.
 * |[
 * gst-launch-1.0 -v audiotestsrc ! udpsink
 * ]|
 * |[
 * gst-launch-1.0 -v udpsrc port=0 ! fakesink
 * ]| read udp packets from a free port.
 * </refsect2>
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstudpsrc.h"

#include <gst/net/gstnetaddressmeta.h>

#if GLIB_CHECK_VERSION (2, 35, 7)
#include <gio/gnetworking.h>
#else

/* nicked from gnetworking.h */
#ifdef G_OS_WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif
#include <winsock2.h>
#undef interface
#include <ws2tcpip.h>           /* for socklen_t */
#endif /* G_OS_WIN32 */

#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#endif

/* not 100% correct, but a good upper bound for memory allocation purposes */
#define MAX_IPV4_UDP_PACKET_SIZE (65536 - 8)

GST_DEBUG_CATEGORY_STATIC (udpsrc_debug);
#define GST_CAT_DEFAULT (udpsrc_debug)

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

#define UDP_DEFAULT_PORT                5004
#define UDP_DEFAULT_MULTICAST_GROUP     "0.0.0.0"
#define UDP_DEFAULT_MULTICAST_IFACE     NULL
#define UDP_DEFAULT_URI                 "udp://"UDP_DEFAULT_MULTICAST_GROUP":"G_STRINGIFY(UDP_DEFAULT_PORT)
#define UDP_DEFAULT_CAPS                NULL
#define UDP_DEFAULT_SOCKET              NULL
#define UDP_DEFAULT_BUFFER_SIZE		0
#define UDP_DEFAULT_TIMEOUT             0
#define UDP_DEFAULT_SKIP_FIRST_BYTES	0
#define UDP_DEFAULT_CLOSE_SOCKET       TRUE
#define UDP_DEFAULT_USED_SOCKET        NULL
#define UDP_DEFAULT_AUTO_MULTICAST     TRUE
#define UDP_DEFAULT_REUSE              TRUE

enum
{
  PROP_0,

  PROP_PORT,
  PROP_MULTICAST_GROUP,
  PROP_MULTICAST_IFACE,
  PROP_URI,
  PROP_CAPS,
  PROP_SOCKET,
  PROP_BUFFER_SIZE,
  PROP_TIMEOUT,
  PROP_SKIP_FIRST_BYTES,
  PROP_CLOSE_SOCKET,
  PROP_USED_SOCKET,
  PROP_AUTO_MULTICAST,
  PROP_REUSE,
  PROP_ADDRESS,

  PROP_LAST
};

static void gst_udpsrc_uri_handler_init (gpointer g_iface, gpointer iface_data);

static GstCaps *gst_udpsrc_getcaps (GstBaseSrc * src, GstCaps * filter);
static GstFlowReturn gst_udpsrc_create (GstPushSrc * psrc, GstBuffer ** buf);
static gboolean gst_udpsrc_close (GstUDPSrc * src);
static gboolean gst_udpsrc_unlock (GstBaseSrc * bsrc);
static gboolean gst_udpsrc_unlock_stop (GstBaseSrc * bsrc);

static void gst_udpsrc_finalize (GObject * object);

static void gst_udpsrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_udpsrc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstStateChangeReturn gst_udpsrc_change_state (GstElement * element,
    GstStateChange transition);

#define gst_udpsrc_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstUDPSrc, gst_udpsrc, GST_TYPE_PUSH_SRC,
    G_IMPLEMENT_INTERFACE (GST_TYPE_URI_HANDLER, gst_udpsrc_uri_handler_init));

static void
gst_udpsrc_class_init (GstUDPSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSrcClass *gstbasesrc_class;
  GstPushSrcClass *gstpushsrc_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesrc_class = (GstBaseSrcClass *) klass;
  gstpushsrc_class = (GstPushSrcClass *) klass;

  GST_DEBUG_CATEGORY_INIT (udpsrc_debug, "udpsrc", 0, "UDP src");

  gobject_class->set_property = gst_udpsrc_set_property;
  gobject_class->get_property = gst_udpsrc_get_property;
  gobject_class->finalize = gst_udpsrc_finalize;

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_PORT,
      g_param_spec_int ("port", "Port",
          "The port to receive the packets from, 0=allocate", 0, G_MAXUINT16,
          UDP_DEFAULT_PORT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /* FIXME 2.0: Remove multicast-group property */
  g_object_class_install_property (gobject_class, PROP_MULTICAST_GROUP,
      g_param_spec_string ("multicast-group", "Multicast Group",
          "The Address of multicast group to join. DEPRECATED: "
          "Use address property instead", UDP_DEFAULT_MULTICAST_GROUP,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_MULTICAST_IFACE,
      g_param_spec_string ("multicast-iface", "Multicast Interface",
          "The network interface on which to join the multicast group",
          UDP_DEFAULT_MULTICAST_IFACE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_URI,
      g_param_spec_string ("uri", "URI",
          "URI in the form of udp://multicast_group:port", UDP_DEFAULT_URI,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_CAPS,
      g_param_spec_boxed ("caps", "Caps",
          "The caps of the source pad", GST_TYPE_CAPS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_SOCKET,
      g_param_spec_object ("socket", "Socket",
          "Socket to use for UDP reception. (NULL == allocate)",
          G_TYPE_SOCKET, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_BUFFER_SIZE,
      g_param_spec_int ("buffer-size", "Buffer Size",
          "Size of the kernel receive buffer in bytes, 0=default", 0, G_MAXINT,
          UDP_DEFAULT_BUFFER_SIZE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_TIMEOUT,
      g_param_spec_uint64 ("timeout", "Timeout",
          "Post a message after timeout nanoseconds (0 = disabled)", 0,
          G_MAXUINT64, UDP_DEFAULT_TIMEOUT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_SKIP_FIRST_BYTES, g_param_spec_int ("skip-first-bytes",
          "Skip first bytes", "number of bytes to skip for each udp packet", 0,
          G_MAXINT, UDP_DEFAULT_SKIP_FIRST_BYTES,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_CLOSE_SOCKET,
      g_param_spec_boolean ("close-socket", "Close socket",
          "Close socket if passed as property on state change",
          UDP_DEFAULT_CLOSE_SOCKET,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_USED_SOCKET,
      g_param_spec_object ("used-socket", "Socket Handle",
          "Socket currently in use for UDP reception. (NULL = no socket)",
          G_TYPE_SOCKET, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_AUTO_MULTICAST,
      g_param_spec_boolean ("auto-multicast", "Auto Multicast",
          "Automatically join/leave multicast groups",
          UDP_DEFAULT_AUTO_MULTICAST,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_REUSE,
      g_param_spec_boolean ("reuse", "Reuse", "Enable reuse of the port",
          UDP_DEFAULT_REUSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_ADDRESS,
      g_param_spec_string ("address", "Address",
          "Address to receive packets for. This is equivalent to the "
          "multicast-group property for now", UDP_DEFAULT_MULTICAST_GROUP,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_template));

  gst_element_class_set_static_metadata (gstelement_class,
      "UDP packet receiver", "Source/Network",
      "Receive data over the network via UDP",
      "Wim Taymans <wim@fluendo.com>, "
      "Thijs Vermeir <thijs.vermeir@barco.com>");

  gstelement_class->change_state = gst_udpsrc_change_state;

  gstbasesrc_class->unlock = gst_udpsrc_unlock;
  gstbasesrc_class->unlock_stop = gst_udpsrc_unlock_stop;
  gstbasesrc_class->get_caps = gst_udpsrc_getcaps;

  gstpushsrc_class->create = gst_udpsrc_create;
}

static void
gst_udpsrc_init (GstUDPSrc * udpsrc)
{
  udpsrc->uri =
      g_strdup_printf ("udp://%s:%u", UDP_DEFAULT_MULTICAST_GROUP,
      UDP_DEFAULT_PORT);

  udpsrc->address = g_strdup (UDP_DEFAULT_MULTICAST_GROUP);
  udpsrc->port = UDP_DEFAULT_PORT;
  udpsrc->socket = UDP_DEFAULT_SOCKET;
  udpsrc->multi_iface = g_strdup (UDP_DEFAULT_MULTICAST_IFACE);
  udpsrc->buffer_size = UDP_DEFAULT_BUFFER_SIZE;
  udpsrc->timeout = UDP_DEFAULT_TIMEOUT;
  udpsrc->skip_first_bytes = UDP_DEFAULT_SKIP_FIRST_BYTES;
  udpsrc->close_socket = UDP_DEFAULT_CLOSE_SOCKET;
  udpsrc->external_socket = (udpsrc->socket != NULL);
  udpsrc->auto_multicast = UDP_DEFAULT_AUTO_MULTICAST;
  udpsrc->used_socket = UDP_DEFAULT_USED_SOCKET;
  udpsrc->reuse = UDP_DEFAULT_REUSE;

  udpsrc->cancellable = g_cancellable_new ();

  /* configure basesrc to be a live source */
  gst_base_src_set_live (GST_BASE_SRC (udpsrc), TRUE);
  /* make basesrc output a segment in time */
  gst_base_src_set_format (GST_BASE_SRC (udpsrc), GST_FORMAT_TIME);
  /* make basesrc set timestamps on outgoing buffers based on the running_time
   * when they were captured */
  gst_base_src_set_do_timestamp (GST_BASE_SRC (udpsrc), TRUE);
}

static void
gst_udpsrc_finalize (GObject * object)
{
  GstUDPSrc *udpsrc;

  udpsrc = GST_UDPSRC (object);

  if (udpsrc->caps)
    gst_caps_unref (udpsrc->caps);
  udpsrc->caps = NULL;

  g_free (udpsrc->multi_iface);
  udpsrc->multi_iface = NULL;

  g_free (udpsrc->uri);
  udpsrc->uri = NULL;

  g_free (udpsrc->address);
  udpsrc->address = NULL;

  if (udpsrc->socket)
    g_object_unref (udpsrc->socket);
  udpsrc->socket = NULL;

  if (udpsrc->used_socket)
    g_object_unref (udpsrc->used_socket);
  udpsrc->used_socket = NULL;

  if (udpsrc->cancellable)
    g_object_unref (udpsrc->cancellable);
  udpsrc->cancellable = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstCaps *
gst_udpsrc_getcaps (GstBaseSrc * src, GstCaps * filter)
{
  GstUDPSrc *udpsrc;
  GstCaps *caps, *result;

  udpsrc = GST_UDPSRC (src);

  GST_OBJECT_LOCK (src);
  if ((caps = udpsrc->caps))
    gst_caps_ref (caps);
  GST_OBJECT_UNLOCK (src);

  if (caps) {
    if (filter) {
      result = gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);
      gst_caps_unref (caps);
    } else {
      result = caps;
    }
  } else {
    result = (filter) ? gst_caps_ref (filter) : gst_caps_new_any ();
  }
  return result;
}

static GstFlowReturn
gst_udpsrc_create (GstPushSrc * psrc, GstBuffer ** buf)
{
  GstFlowReturn ret;
  GstUDPSrc *udpsrc;
  GstBuffer *outbuf = NULL;
  GstMapInfo info;
  GSocketAddress *saddr = NULL;
  gsize offset;
  gssize readsize;
  gssize res;
  gboolean try_again;
  GError *err = NULL;

  udpsrc = GST_UDPSRC_CAST (psrc);

retry:
  /* quick check, avoid going in select when we already have data */
  readsize = g_socket_get_available_bytes (udpsrc->used_socket);
  if (readsize > 0)
    goto no_select;

  do {
    gint64 timeout;

    try_again = FALSE;

    if (udpsrc->timeout)
      timeout = udpsrc->timeout / 1000;
    else
      timeout = -1;

    GST_LOG_OBJECT (udpsrc, "doing select, timeout %" G_GINT64_FORMAT, timeout);

    if (!g_socket_condition_timed_wait (udpsrc->used_socket, G_IO_IN | G_IO_PRI,
            timeout, udpsrc->cancellable, &err)) {
      if (g_error_matches (err, G_IO_ERROR, G_IO_ERROR_BUSY)
          || g_error_matches (err, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
        goto stopped;
      } else if (g_error_matches (err, G_IO_ERROR, G_IO_ERROR_TIMED_OUT)) {
        g_clear_error (&err);
        /* timeout, post element message */
        gst_element_post_message (GST_ELEMENT_CAST (udpsrc),
            gst_message_new_element (GST_OBJECT_CAST (udpsrc),
                gst_structure_new ("GstUDPSrcTimeout",
                    "timeout", G_TYPE_UINT64, udpsrc->timeout, NULL)));
      } else {
        goto select_error;
      }

      try_again = TRUE;
    }
  } while (G_UNLIKELY (try_again));

  /* ask how much is available for reading on the socket, this should be exactly
   * one UDP packet. We will check the return value, though, because in some
   * case it can return 0 and we don't want a 0 sized buffer. */
  readsize = g_socket_get_available_bytes (udpsrc->used_socket);
  if (G_UNLIKELY (readsize < 0))
    goto get_available_error;

  /* If we get here and the readsize is zero, then either select was woken up
   * by activity that is not a read, or a poll error occurred, or a UDP packet
   * was received that has no data. Since we cannot identify which case it is,
   * we handle all of them. This could possibly lead to a UDP packet getting
   * lost, but since UDP is not reliable, we can accept this. */
  if (G_UNLIKELY (!readsize)) {
    /* try to read a packet (and it will be ignored),
     * in case a packet with no data arrived */
    res =
        g_socket_receive_from (udpsrc->used_socket, NULL, NULL,
        0, udpsrc->cancellable, &err);
    if (G_UNLIKELY (res < 0))
      goto receive_error;

    /* poll again */
    goto retry;
  }

no_select:
  GST_LOG_OBJECT (udpsrc, "ioctl says %d bytes available", (int) readsize);

  /* sanity check value from _get_available_bytes(), which might be as
   * large as the kernel-side buffer on some operating systems */
  if (g_socket_get_family (udpsrc->used_socket) == G_SOCKET_FAMILY_IPV4)
    readsize = MIN (MAX_IPV4_UDP_PACKET_SIZE, readsize);

  ret = GST_BASE_SRC_CLASS (parent_class)->alloc (GST_BASE_SRC_CAST (udpsrc),
      -1, readsize, &outbuf);
  if (ret != GST_FLOW_OK)
    goto alloc_failed;

  gst_buffer_map (outbuf, &info, GST_MAP_WRITE);
  offset = 0;

  if (saddr)
    g_object_unref (saddr);
  saddr = NULL;

  res =
      g_socket_receive_from (udpsrc->used_socket, &saddr, (gchar *) info.data,
      info.size, udpsrc->cancellable, &err);

  if (G_UNLIKELY (res < 0)) {
    /* EHOSTUNREACH for a UDP socket means that a packet sent with udpsink
     * generated a "port unreachable" ICMP response. We ignore that and try
     * again. */
    if (g_error_matches (err, G_IO_ERROR, G_IO_ERROR_HOST_UNREACHABLE)) {
      gst_buffer_unmap (outbuf, &info);
      gst_buffer_unref (outbuf);
      outbuf = NULL;
      g_clear_error (&err);
      goto retry;
    }
    goto receive_error;
  }

  /* patch offset and size when stripping off the headers */
  if (G_UNLIKELY (udpsrc->skip_first_bytes != 0)) {
    if (G_UNLIKELY (readsize < udpsrc->skip_first_bytes))
      goto skip_error;

    offset += udpsrc->skip_first_bytes;
    res -= udpsrc->skip_first_bytes;
  }

  gst_buffer_unmap (outbuf, &info);
  gst_buffer_resize (outbuf, offset, res);

  /* use buffer metadata so receivers can also track the address */
  if (saddr) {
    gst_buffer_add_net_address_meta (outbuf, saddr);
    g_object_unref (saddr);
  }
  saddr = NULL;

  GST_LOG_OBJECT (udpsrc, "read %d bytes", (int) readsize);

  *buf = GST_BUFFER_CAST (outbuf);

  return ret;

  /* ERRORS */
select_error:
  {
    GST_ELEMENT_ERROR (udpsrc, RESOURCE, READ, (NULL),
        ("select error: %s", err->message));
    g_clear_error (&err);
    return GST_FLOW_ERROR;
  }
stopped:
  {
    GST_DEBUG ("stop called");
    g_clear_error (&err);
    return GST_FLOW_FLUSHING;
  }
get_available_error:
  {
    GST_ELEMENT_ERROR (udpsrc, RESOURCE, READ, (NULL),
        ("get available bytes failed"));
    return GST_FLOW_ERROR;
  }
alloc_failed:
  {
    GST_DEBUG ("Allocation failed");
    return ret;
  }
receive_error:
  {
    if (outbuf != NULL) {
      gst_buffer_unmap (outbuf, &info);
      gst_buffer_unref (outbuf);
    }

    if (g_error_matches (err, G_IO_ERROR, G_IO_ERROR_BUSY) ||
        g_error_matches (err, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
      g_clear_error (&err);
      return GST_FLOW_FLUSHING;
    } else {
      GST_ELEMENT_ERROR (udpsrc, RESOURCE, READ, (NULL),
          ("receive error %" G_GSSIZE_FORMAT ": %s", res, err->message));
      g_clear_error (&err);
      return GST_FLOW_ERROR;
    }
  }
skip_error:
  {
    gst_buffer_unmap (outbuf, &info);
    gst_buffer_unref (outbuf);

    GST_ELEMENT_ERROR (udpsrc, STREAM, DECODE, (NULL),
        ("UDP buffer to small to skip header"));
    return GST_FLOW_ERROR;
  }
}

static gboolean
gst_udpsrc_set_uri (GstUDPSrc * src, const gchar * uri, GError ** error)
{
  gchar *address;
  guint16 port;

  if (!gst_udp_parse_uri (uri, &address, &port))
    goto wrong_uri;

  if (port == (guint16) - 1)
    port = UDP_DEFAULT_PORT;

  g_free (src->address);
  src->address = address;
  src->port = port;

  g_free (src->uri);
  src->uri = g_strdup (uri);

  return TRUE;

  /* ERRORS */
wrong_uri:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL),
        ("error parsing uri %s", uri));
    g_set_error_literal (error, GST_URI_ERROR, GST_URI_ERROR_BAD_URI,
        "Could not parse UDP URI");
    return FALSE;
  }
}

static void
gst_udpsrc_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstUDPSrc *udpsrc = GST_UDPSRC (object);

  switch (prop_id) {
    case PROP_BUFFER_SIZE:
      udpsrc->buffer_size = g_value_get_int (value);
      break;
    case PROP_PORT:
      udpsrc->port = g_value_get_int (value);
      g_free (udpsrc->uri);
      udpsrc->uri =
          g_strdup_printf ("udp://%s:%u", udpsrc->address, udpsrc->port);
      break;
    case PROP_MULTICAST_GROUP:
    case PROP_ADDRESS:
    {
      const gchar *group;

      g_free (udpsrc->address);
      if ((group = g_value_get_string (value)))
        udpsrc->address = g_strdup (group);
      else
        udpsrc->address = g_strdup (UDP_DEFAULT_MULTICAST_GROUP);

      g_free (udpsrc->uri);
      udpsrc->uri =
          g_strdup_printf ("udp://%s:%u", udpsrc->address, udpsrc->port);
      break;
    }
    case PROP_MULTICAST_IFACE:
      g_free (udpsrc->multi_iface);

      if (g_value_get_string (value) == NULL)
        udpsrc->multi_iface = g_strdup (UDP_DEFAULT_MULTICAST_IFACE);
      else
        udpsrc->multi_iface = g_value_dup_string (value);
      break;
    case PROP_URI:
      gst_udpsrc_set_uri (udpsrc, g_value_get_string (value), NULL);
      break;
    case PROP_CAPS:
    {
      const GstCaps *new_caps_val = gst_value_get_caps (value);
      GstCaps *new_caps;
      GstCaps *old_caps;

      if (new_caps_val == NULL) {
        new_caps = gst_caps_new_any ();
      } else {
        new_caps = gst_caps_copy (new_caps_val);
      }

      GST_OBJECT_LOCK (udpsrc);
      old_caps = udpsrc->caps;
      udpsrc->caps = new_caps;
      GST_OBJECT_UNLOCK (udpsrc);
      if (old_caps)
        gst_caps_unref (old_caps);

      gst_pad_mark_reconfigure (GST_BASE_SRC_PAD (udpsrc));
      break;
    }
    case PROP_SOCKET:
      if (udpsrc->socket != NULL && udpsrc->socket != udpsrc->used_socket &&
          udpsrc->close_socket) {
        GError *err = NULL;

        if (!g_socket_close (udpsrc->socket, &err)) {
          GST_ERROR ("failed to close socket %p: %s", udpsrc->socket,
              err->message);
          g_clear_error (&err);
        }
      }
      if (udpsrc->socket)
        g_object_unref (udpsrc->socket);
      udpsrc->socket = g_value_dup_object (value);
      GST_DEBUG ("setting socket to %p", udpsrc->socket);
      break;
    case PROP_TIMEOUT:
      udpsrc->timeout = g_value_get_uint64 (value);
      break;
    case PROP_SKIP_FIRST_BYTES:
      udpsrc->skip_first_bytes = g_value_get_int (value);
      break;
    case PROP_CLOSE_SOCKET:
      udpsrc->close_socket = g_value_get_boolean (value);
      break;
    case PROP_AUTO_MULTICAST:
      udpsrc->auto_multicast = g_value_get_boolean (value);
      break;
    case PROP_REUSE:
      udpsrc->reuse = g_value_get_boolean (value);
      break;
    default:
      break;
  }
}

static void
gst_udpsrc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstUDPSrc *udpsrc = GST_UDPSRC (object);

  switch (prop_id) {
    case PROP_BUFFER_SIZE:
      g_value_set_int (value, udpsrc->buffer_size);
      break;
    case PROP_PORT:
      g_value_set_int (value, udpsrc->port);
      break;
    case PROP_MULTICAST_GROUP:
    case PROP_ADDRESS:
      g_value_set_string (value, udpsrc->address);
      break;
    case PROP_MULTICAST_IFACE:
      g_value_set_string (value, udpsrc->multi_iface);
      break;
    case PROP_URI:
      g_value_set_string (value, udpsrc->uri);
      break;
    case PROP_CAPS:
      GST_OBJECT_LOCK (udpsrc);
      gst_value_set_caps (value, udpsrc->caps);
      GST_OBJECT_UNLOCK (udpsrc);
      break;
    case PROP_SOCKET:
      g_value_set_object (value, udpsrc->socket);
      break;
    case PROP_TIMEOUT:
      g_value_set_uint64 (value, udpsrc->timeout);
      break;
    case PROP_SKIP_FIRST_BYTES:
      g_value_set_int (value, udpsrc->skip_first_bytes);
      break;
    case PROP_CLOSE_SOCKET:
      g_value_set_boolean (value, udpsrc->close_socket);
      break;
    case PROP_USED_SOCKET:
      g_value_set_object (value, udpsrc->used_socket);
      break;
    case PROP_AUTO_MULTICAST:
      g_value_set_boolean (value, udpsrc->auto_multicast);
      break;
    case PROP_REUSE:
      g_value_set_boolean (value, udpsrc->reuse);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GInetAddress *
gst_udpsrc_resolve (GstUDPSrc * src, const gchar * address)
{
  GInetAddress *addr;
  GError *err = NULL;
  GResolver *resolver;

  addr = g_inet_address_new_from_string (address);
  if (!addr) {
    GList *results;

    GST_DEBUG_OBJECT (src, "resolving IP address for host %s", address);
    resolver = g_resolver_get_default ();
    results =
        g_resolver_lookup_by_name (resolver, address, src->cancellable, &err);
    if (!results)
      goto name_resolve;
    addr = G_INET_ADDRESS (g_object_ref (results->data));

    g_resolver_free_addresses (results);
    g_object_unref (resolver);
  }
#ifndef GST_DISABLE_GST_DEBUG
  {
    gchar *ip = g_inet_address_to_string (addr);

    GST_DEBUG_OBJECT (src, "IP address for host %s is %s", address, ip);
    g_free (ip);
  }
#endif

  return addr;

name_resolve:
  {
    GST_WARNING_OBJECT (src, "Failed to resolve %s: %s", address, err->message);
    g_clear_error (&err);
    g_object_unref (resolver);
    return NULL;
  }
}

/* create a socket for sending to remote machine */
static gboolean
gst_udpsrc_open (GstUDPSrc * src)
{
  GInetAddress *addr, *bind_addr;
  GSocketAddress *bind_saddr;
  GError *err = NULL;

  if (src->socket == NULL) {
    /* need to allocate a socket */
    GST_DEBUG_OBJECT (src, "allocating socket for %s:%d", src->address,
        src->port);

    addr = gst_udpsrc_resolve (src, src->address);
    if (!addr)
      goto name_resolve;

    if ((src->used_socket =
            g_socket_new (g_inet_address_get_family (addr),
                G_SOCKET_TYPE_DATAGRAM, G_SOCKET_PROTOCOL_UDP, &err)) == NULL)
      goto no_socket;

    src->external_socket = FALSE;

    GST_DEBUG_OBJECT (src, "got socket %p", src->used_socket);

    if (src->addr)
      g_object_unref (src->addr);
    src->addr =
        G_INET_SOCKET_ADDRESS (g_inet_socket_address_new (addr, src->port));

    GST_DEBUG_OBJECT (src, "binding on port %d", src->port);

    /* On Windows it's not possible to bind to a multicast address
     * but the OS will make sure to filter out all packets that
     * arrive not for the multicast address the socket joined.
     *
     * On Linux and others it is necessary to bind to a multicast
     * address to let the OS filter out all packets that are received
     * on the same port but for different addresses than the multicast
     * address
     */
#ifdef G_OS_WIN32
    if (g_inet_address_get_is_multicast (addr))
      bind_addr = g_inet_address_new_any (g_inet_address_get_family (addr));
    else
#endif
      bind_addr = G_INET_ADDRESS (g_object_ref (addr));

    g_object_unref (addr);

    bind_saddr = g_inet_socket_address_new (bind_addr, src->port);
    g_object_unref (bind_addr);
    if (!g_socket_bind (src->used_socket, bind_saddr, src->reuse, &err))
      goto bind_error;

    g_object_unref (bind_saddr);
  } else {
    GST_DEBUG_OBJECT (src, "using provided socket %p", src->socket);
    /* we use the configured socket, try to get some info about it */
    src->used_socket = G_SOCKET (g_object_ref (src->socket));
    src->external_socket = TRUE;

    if (src->addr)
      g_object_unref (src->addr);
    src->addr =
        G_INET_SOCKET_ADDRESS (g_socket_get_local_address (src->used_socket,
            &err));
    if (!src->addr)
      goto getsockname_error;
  }

#if GLIB_CHECK_VERSION (2, 35, 7)
  {
    gint val = 0;

    if (src->buffer_size != 0) {
      GError *opt_err = NULL;

      GST_INFO_OBJECT (src, "setting udp buffer of %d bytes", src->buffer_size);
      /* set buffer size, Note that on Linux this is typically limited to a
       * maximum of around 100K. Also a minimum of 128 bytes is required on
       * Linux. */
      if (!g_socket_set_option (src->used_socket, SOL_SOCKET, SO_RCVBUF,
              src->buffer_size, &opt_err)) {
        GST_ELEMENT_WARNING (src, RESOURCE, SETTINGS, (NULL),
            ("Could not create a buffer of requested %d bytes: %s",
                src->buffer_size, opt_err->message));
        g_error_free (opt_err);
        opt_err = NULL;
      }
    }

    /* read the value of the receive buffer. Note that on linux this returns
     * 2x the value we set because the kernel allocates extra memory for
     * metadata. The default on Linux is about 100K (which is about 50K
     * without metadata) */
    if (g_socket_get_option (src->used_socket, SOL_SOCKET, SO_RCVBUF, &val,
            NULL)) {
      GST_INFO_OBJECT (src, "have udp buffer of %d bytes", val);
    } else {
      GST_DEBUG_OBJECT (src, "could not get udp buffer size");
    }
  }
#elif defined (SO_RCVBUF)
  {
    gint rcvsize, ret;
    socklen_t len;

    len = sizeof (rcvsize);
    if (src->buffer_size != 0) {
      rcvsize = src->buffer_size;

      GST_DEBUG_OBJECT (src, "setting udp buffer of %d bytes", rcvsize);
      /* set buffer size, Note that on Linux this is typically limited to a
       * maximum of around 100K. Also a minimum of 128 bytes is required on
       * Linux. */
      ret =
          setsockopt (g_socket_get_fd (src->used_socket), SOL_SOCKET, SO_RCVBUF,
          (void *) &rcvsize, len);
      if (ret != 0) {
        GST_ELEMENT_WARNING (src, RESOURCE, SETTINGS, (NULL),
            ("Could not create a buffer of requested %d bytes, %d: %s (%d)",
                rcvsize, ret, g_strerror (errno), errno));
      }
    }

    /* read the value of the receive buffer. Note that on linux this returns 2x the
     * value we set because the kernel allocates extra memory for metadata.
     * The default on Linux is about 100K (which is about 50K without metadata) */
    ret =
        getsockopt (g_socket_get_fd (src->used_socket), SOL_SOCKET, SO_RCVBUF,
        (void *) &rcvsize, &len);
    if (ret == 0)
      GST_DEBUG_OBJECT (src, "have udp buffer of %d bytes", rcvsize);
    else
      GST_DEBUG_OBJECT (src, "could not get udp buffer size");
  }
#else
  if (src->buffer_size != 0) {
    GST_WARNING_OBJECT (src, "don't know how to set udp buffer size on this "
        "OS. Consider upgrading your GLib to >= 2.35.7 and re-compiling the "
        "GStreamer udp plugin");
  }
#endif

  g_socket_set_broadcast (src->used_socket, TRUE);

  if (src->auto_multicast
      &&
      g_inet_address_get_is_multicast (g_inet_socket_address_get_address
          (src->addr))) {
    GST_DEBUG_OBJECT (src, "joining multicast group %s", src->address);
    if (!g_socket_join_multicast_group (src->used_socket,
            g_inet_socket_address_get_address (src->addr),
            FALSE, src->multi_iface, &err))
      goto membership;
  }

  /* NOTE: sockaddr_in.sin_port works for ipv4 and ipv6 because sin_port
   * follows ss_family on both */
  {
    GInetSocketAddress *addr;
    guint16 port;

    addr =
        G_INET_SOCKET_ADDRESS (g_socket_get_local_address (src->used_socket,
            &err));
    if (!addr)
      goto getsockname_error;

    port = g_inet_socket_address_get_port (addr);
    GST_DEBUG_OBJECT (src, "bound, on port %d", port);
    if (port != src->port) {
      src->port = port;
      GST_DEBUG_OBJECT (src, "notifying port %d", port);
      g_object_notify (G_OBJECT (src), "port");
    }
    g_object_unref (addr);
  }

  return TRUE;

  /* ERRORS */
name_resolve:
  {
    return FALSE;
  }
no_socket:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL),
        ("no socket error: %s", err->message));
    g_clear_error (&err);
    g_object_unref (addr);
    return FALSE;
  }
bind_error:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, SETTINGS, (NULL),
        ("bind failed: %s", err->message));
    g_clear_error (&err);
    g_object_unref (bind_saddr);
    gst_udpsrc_close (src);
    return FALSE;
  }
membership:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, SETTINGS, (NULL),
        ("could add membership: %s", err->message));
    g_clear_error (&err);
    gst_udpsrc_close (src);
    return FALSE;
  }
getsockname_error:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, SETTINGS, (NULL),
        ("getsockname failed: %s", err->message));
    g_clear_error (&err);
    gst_udpsrc_close (src);
    return FALSE;
  }
}

static gboolean
gst_udpsrc_unlock (GstBaseSrc * bsrc)
{
  GstUDPSrc *src;

  src = GST_UDPSRC (bsrc);

  GST_LOG_OBJECT (src, "Flushing");
  g_cancellable_cancel (src->cancellable);

  return TRUE;
}

static gboolean
gst_udpsrc_unlock_stop (GstBaseSrc * bsrc)
{
  GstUDPSrc *src;

  src = GST_UDPSRC (bsrc);

  GST_LOG_OBJECT (src, "No longer flushing");
  g_cancellable_reset (src->cancellable);

  return TRUE;
}

static gboolean
gst_udpsrc_close (GstUDPSrc * src)
{
  GST_DEBUG ("closing sockets");

  if (src->used_socket) {
    if (src->auto_multicast
        &&
        g_inet_address_get_is_multicast (g_inet_socket_address_get_address
            (src->addr))) {
      GError *err = NULL;

      GST_DEBUG_OBJECT (src, "leaving multicast group %s", src->address);

      if (!g_socket_leave_multicast_group (src->used_socket,
              g_inet_socket_address_get_address (src->addr), FALSE,
              src->multi_iface, &err)) {
        GST_ERROR_OBJECT (src, "Failed to leave multicast group: %s",
            err->message);
        g_clear_error (&err);
      }
    }

    if (src->close_socket || !src->external_socket) {
      GError *err = NULL;
      if (!g_socket_close (src->used_socket, &err)) {
        GST_ERROR_OBJECT (src, "Failed to close socket: %s", err->message);
        g_clear_error (&err);
      }
    }

    g_object_unref (src->used_socket);
    src->used_socket = NULL;
    g_object_unref (src->addr);
    src->addr = NULL;
  }

  return TRUE;
}


static GstStateChangeReturn
gst_udpsrc_change_state (GstElement * element, GstStateChange transition)
{
  GstUDPSrc *src;
  GstStateChangeReturn result;

  src = GST_UDPSRC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_udpsrc_open (src))
        goto open_failed;
      break;
    default:
      break;
  }
  if ((result =
          GST_ELEMENT_CLASS (parent_class)->change_state (element,
              transition)) == GST_STATE_CHANGE_FAILURE)
    goto failure;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_udpsrc_close (src);
      break;
    default:
      break;
  }
  return result;
  /* ERRORS */
open_failed:
  {
    GST_DEBUG_OBJECT (src, "failed to open socket");
    return GST_STATE_CHANGE_FAILURE;
  }
failure:
  {
    GST_DEBUG_OBJECT (src, "parent failed state change");
    return result;
  }
}




/*** GSTURIHANDLER INTERFACE *************************************************/

static GstURIType
gst_udpsrc_uri_get_type (GType type)
{
  return GST_URI_SRC;
}

static const gchar *const *
gst_udpsrc_uri_get_protocols (GType type)
{
  static const gchar *protocols[] = { "udp", NULL };

  return protocols;
}

static gchar *
gst_udpsrc_uri_get_uri (GstURIHandler * handler)
{
  GstUDPSrc *src = GST_UDPSRC (handler);

  return g_strdup (src->uri);
}

static gboolean
gst_udpsrc_uri_set_uri (GstURIHandler * handler, const gchar * uri,
    GError ** error)
{
  return gst_udpsrc_set_uri (GST_UDPSRC (handler), uri, error);
}

static void
gst_udpsrc_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = gst_udpsrc_uri_get_type;
  iface->get_protocols = gst_udpsrc_uri_get_protocols;
  iface->get_uri = gst_udpsrc_uri_get_uri;
  iface->set_uri = gst_udpsrc_uri_set_uri;
}
