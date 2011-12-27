/* GStreamer
 * Copyright (C) <2005> Wim Taymans <wim@fluendo.com>
 * Copyright (C) <2005> Nokia Corporation <kai.vehmanen@nokia.com>
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
 * </para>
 * <para>
 * A custom file descriptor can be configured with the
 * #GstUDPSrc:sockfd property. The socket will be closed when setting the
 * element to READY by default. This behaviour can be
 * overriden with the #GstUDPSrc:closefd property, in which case the application
 * is responsible for closing the file descriptor.
 *
 * <refsect2>
 * <title>Examples</title>
 * |[
 * gst-launch -v udpsrc ! fakesink dump=1
 * ]| A pipeline to read from the default port and dump the udp packets.
 * To actually generate udp packets on the default port one can use the
 * udpsink element. When running the following pipeline in another terminal, the
 * above mentioned pipeline should dump data packets to the console.
 * |[
 * gst-launch -v audiotestsrc ! udpsink
 * ]|
 * |[
 * gst-launch -v udpsrc port=0 ! fakesink
 * ]| read udp packets from a free port.
 * </refsect2>
 *
 * Last reviewed on 2007-09-20 (0.10.7)
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstudpsrc.h"
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <stdlib.h>

#if defined _MSC_VER && (_MSC_VER >= 1400)
#include <io.h>
#endif

#include <gst/netbuffer/gstnetbuffer.h>

#ifdef HAVE_FIONREAD_IN_SYS_FILIO
#include <sys/filio.h>
#endif

GST_DEBUG_CATEGORY_STATIC (udpsrc_debug);
#define GST_CAT_DEFAULT (udpsrc_debug)

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

#define UDP_DEFAULT_PORT                4951
#define UDP_DEFAULT_MULTICAST_GROUP     "0.0.0.0"
#define UDP_DEFAULT_MULTICAST_IFACE     NULL
#define UDP_DEFAULT_URI                 "udp://"UDP_DEFAULT_MULTICAST_GROUP":"G_STRINGIFY(UDP_DEFAULT_PORT)
#define UDP_DEFAULT_CAPS                NULL
#define UDP_DEFAULT_SOCKFD              -1
#define UDP_DEFAULT_BUFFER_SIZE		0
#define UDP_DEFAULT_TIMEOUT             0
#define UDP_DEFAULT_SKIP_FIRST_BYTES	0
#define UDP_DEFAULT_CLOSEFD            TRUE
#define UDP_DEFAULT_SOCK                -1
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
  PROP_SOCKFD,
  PROP_BUFFER_SIZE,
  PROP_TIMEOUT,
  PROP_SKIP_FIRST_BYTES,
  PROP_CLOSEFD,
  PROP_SOCK,
  PROP_AUTO_MULTICAST,
  PROP_REUSE,

  PROP_LAST
};

#define CLOSE_IF_REQUESTED(udpctx)                                        \
G_STMT_START {                                                            \
  if ((!udpctx->externalfd) || (udpctx->externalfd && udpctx->closefd)) { \
    CLOSE_SOCKET(udpctx->sock.fd);                                        \
    if (udpctx->sock.fd == udpctx->sockfd)                                \
      udpctx->sockfd = UDP_DEFAULT_SOCKFD;                                \
  }                                                                       \
  udpctx->sock.fd = UDP_DEFAULT_SOCK;                                     \
} G_STMT_END

static void gst_udpsrc_uri_handler_init (gpointer g_iface, gpointer iface_data);

static GstCaps *gst_udpsrc_getcaps (GstBaseSrc * src);

static GstFlowReturn gst_udpsrc_create (GstPushSrc * psrc, GstBuffer ** buf);

static gboolean gst_udpsrc_start (GstBaseSrc * bsrc);

static gboolean gst_udpsrc_stop (GstBaseSrc * bsrc);

static gboolean gst_udpsrc_unlock (GstBaseSrc * bsrc);

static gboolean gst_udpsrc_unlock_stop (GstBaseSrc * bsrc);

static void gst_udpsrc_finalize (GObject * object);

static void gst_udpsrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_udpsrc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void
_do_init (GType type)
{
  static const GInterfaceInfo urihandler_info = {
    gst_udpsrc_uri_handler_init,
    NULL,
    NULL
  };

  g_type_add_interface_static (type, GST_TYPE_URI_HANDLER, &urihandler_info);

  GST_DEBUG_CATEGORY_INIT (udpsrc_debug, "udpsrc", 0, "UDP src");
}

GST_BOILERPLATE_FULL (GstUDPSrc, gst_udpsrc, GstPushSrc, GST_TYPE_PUSH_SRC,
    _do_init);

static void
gst_udpsrc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class, &src_template);

  gst_element_class_set_details_simple (element_class, "UDP packet receiver",
      "Source/Network",
      "Receive data over the network via UDP",
      "Wim Taymans <wim@fluendo.com>, "
      "Thijs Vermeir <thijs.vermeir@barco.com>");
}

static void
gst_udpsrc_class_init (GstUDPSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseSrcClass *gstbasesrc_class;
  GstPushSrcClass *gstpushsrc_class;

  gobject_class = (GObjectClass *) klass;
  gstbasesrc_class = (GstBaseSrcClass *) klass;
  gstpushsrc_class = (GstPushSrcClass *) klass;

  gobject_class->set_property = gst_udpsrc_set_property;
  gobject_class->get_property = gst_udpsrc_get_property;
  gobject_class->finalize = gst_udpsrc_finalize;

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_PORT,
      g_param_spec_int ("port", "Port",
          "The port to receive the packets from, 0=allocate", 0, G_MAXUINT16,
          UDP_DEFAULT_PORT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_MULTICAST_GROUP,
      g_param_spec_string ("multicast-group", "Multicast Group",
          "The Address of multicast group to join", UDP_DEFAULT_MULTICAST_GROUP,
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
  g_object_class_install_property (gobject_class, PROP_SOCKFD,
      g_param_spec_int ("sockfd", "Socket Handle",
          "Socket to use for UDP reception. (-1 == allocate)",
          -1, G_MAXINT, UDP_DEFAULT_SOCKFD,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_BUFFER_SIZE,
      g_param_spec_int ("buffer-size", "Buffer Size",
          "Size of the kernel receive buffer in bytes, 0=default", 0, G_MAXINT,
          UDP_DEFAULT_BUFFER_SIZE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_TIMEOUT,
      g_param_spec_uint64 ("timeout", "Timeout",
          "Post a message after timeout microseconds (0 = disabled)", 0,
          G_MAXUINT64, UDP_DEFAULT_TIMEOUT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_SKIP_FIRST_BYTES, g_param_spec_int ("skip-first-bytes",
          "Skip first bytes", "number of bytes to skip for each udp packet", 0,
          G_MAXINT, UDP_DEFAULT_SKIP_FIRST_BYTES,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_CLOSEFD,
      g_param_spec_boolean ("closefd", "Close sockfd",
          "Close sockfd if passed as property on state change",
          UDP_DEFAULT_CLOSEFD, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_SOCK,
      g_param_spec_int ("sock", "Socket Handle",
          "Socket currently in use for UDP reception. (-1 = no socket)",
          -1, G_MAXINT, UDP_DEFAULT_SOCK,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_AUTO_MULTICAST,
      g_param_spec_boolean ("auto-multicast", "Auto Multicast",
          "Automatically join/leave multicast groups",
          UDP_DEFAULT_AUTO_MULTICAST,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_REUSE,
      g_param_spec_boolean ("reuse", "Reuse", "Enable reuse of the port",
          UDP_DEFAULT_REUSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstbasesrc_class->start = gst_udpsrc_start;
  gstbasesrc_class->stop = gst_udpsrc_stop;
  gstbasesrc_class->unlock = gst_udpsrc_unlock;
  gstbasesrc_class->unlock_stop = gst_udpsrc_unlock_stop;
  gstbasesrc_class->get_caps = gst_udpsrc_getcaps;

  gstpushsrc_class->create = gst_udpsrc_create;
}

static void
gst_udpsrc_init (GstUDPSrc * udpsrc, GstUDPSrcClass * g_class)
{
  WSA_STARTUP (udpsrc);

  gst_udp_uri_init (&udpsrc->uri, UDP_DEFAULT_MULTICAST_GROUP,
      UDP_DEFAULT_PORT);

  udpsrc->sockfd = UDP_DEFAULT_SOCKFD;
  udpsrc->multi_iface = g_strdup (UDP_DEFAULT_MULTICAST_IFACE);
  udpsrc->buffer_size = UDP_DEFAULT_BUFFER_SIZE;
  udpsrc->timeout = UDP_DEFAULT_TIMEOUT;
  udpsrc->skip_first_bytes = UDP_DEFAULT_SKIP_FIRST_BYTES;
  udpsrc->closefd = UDP_DEFAULT_CLOSEFD;
  udpsrc->externalfd = (udpsrc->sockfd != -1);
  udpsrc->auto_multicast = UDP_DEFAULT_AUTO_MULTICAST;
  udpsrc->sock.fd = UDP_DEFAULT_SOCK;
  udpsrc->reuse = UDP_DEFAULT_REUSE;

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

  g_free (udpsrc->multi_iface);

  gst_udp_uri_free (&udpsrc->uri);
  g_free (udpsrc->uristr);

  if (udpsrc->sockfd >= 0 && udpsrc->closefd)
    CLOSE_SOCKET (udpsrc->sockfd);

  WSA_CLEANUP (object);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstCaps *
gst_udpsrc_getcaps (GstBaseSrc * src)
{
  GstUDPSrc *udpsrc;

  udpsrc = GST_UDPSRC (src);

  if (udpsrc->caps)
    return gst_caps_ref (udpsrc->caps);
  else
    return gst_caps_new_any ();
}

/* read a message from the error queue */
static void
clear_error (GstUDPSrc * udpsrc)
{
#if defined (MSG_ERRQUEUE)
  struct msghdr cmsg;
  char cbuf[128];
  char msgbuf[CMSG_SPACE (128)];
  struct iovec iov;

  /* Flush ERRORS from fd so next poll will not return at once */
  /* No need for address : We look for local error */
  cmsg.msg_name = NULL;
  cmsg.msg_namelen = 0;

  /* IOV */
  memset (&cbuf, 0, sizeof (cbuf));
  iov.iov_base = cbuf;
  iov.iov_len = sizeof (cbuf);
  cmsg.msg_iov = &iov;
  cmsg.msg_iovlen = 1;

  /* msg_control */
  memset (&msgbuf, 0, sizeof (msgbuf));
  cmsg.msg_control = &msgbuf;
  cmsg.msg_controllen = sizeof (msgbuf);

  recvmsg (udpsrc->sock.fd, &cmsg, MSG_ERRQUEUE);
#endif
}

static GstFlowReturn
gst_udpsrc_create (GstPushSrc * psrc, GstBuffer ** buf)
{
  GstUDPSrc *udpsrc;
  GstNetBuffer *outbuf;
  union gst_sockaddr
  {
    struct sockaddr sa;
    struct sockaddr_in sa_in;
    struct sockaddr_in6 sa_in6;
    struct sockaddr_storage sa_stor;
  } sa;
  socklen_t slen;
  guint8 *pktdata;
  gint pktsize;
#ifdef G_OS_UNIX
  gint readsize;
#elif defined G_OS_WIN32
  gulong readsize;
#endif
  GstClockTime timeout;
  gint ret;
  gboolean try_again;

  udpsrc = GST_UDPSRC_CAST (psrc);

retry:
  /* quick check, avoid going in select when we already have data */
  readsize = 0;
  if (G_UNLIKELY ((ret =
              IOCTL_SOCKET (udpsrc->sock.fd, FIONREAD, &readsize)) < 0))
    goto ioctl_failed;

  if (readsize > 0)
    goto no_select;

  if (udpsrc->timeout > 0) {
    timeout = udpsrc->timeout * GST_USECOND;
  } else {
    timeout = GST_CLOCK_TIME_NONE;
  }

  do {
    try_again = FALSE;

    GST_LOG_OBJECT (udpsrc, "doing select, timeout %" G_GUINT64_FORMAT,
        udpsrc->timeout);

    ret = gst_poll_wait (udpsrc->fdset, timeout);
    GST_LOG_OBJECT (udpsrc, "select returned %d", ret);
    if (G_UNLIKELY (ret < 0)) {
      if (errno == EBUSY)
        goto stopped;
#ifdef G_OS_WIN32
      if (WSAGetLastError () != WSAEINTR)
        goto select_error;
#else
      if (errno != EAGAIN && errno != EINTR)
        goto select_error;
#endif
      try_again = TRUE;
    } else if (G_UNLIKELY (ret == 0)) {
      /* timeout, post element message */
      gst_element_post_message (GST_ELEMENT_CAST (udpsrc),
          gst_message_new_element (GST_OBJECT_CAST (udpsrc),
              gst_structure_new ("GstUDPSrcTimeout",
                  "timeout", G_TYPE_UINT64, udpsrc->timeout, NULL)));
      try_again = TRUE;
    }
  } while (G_UNLIKELY (try_again));

  /* ask how much is available for reading on the socket, this should be exactly
   * one UDP packet. We will check the return value, though, because in some
   * case it can return 0 and we don't want a 0 sized buffer. */
  readsize = 0;
  if (G_UNLIKELY ((ret =
              IOCTL_SOCKET (udpsrc->sock.fd, FIONREAD, &readsize)) < 0))
    goto ioctl_failed;

  /* If we get here and the readsize is zero, then either select was woken up
   * by activity that is not a read, or a poll error occurred, or a UDP packet
   * was received that has no data. Since we cannot identify which case it is,
   * we handle all of them. This could possibly lead to a UDP packet getting
   * lost, but since UDP is not reliable, we can accept this. */
  if (G_UNLIKELY (!readsize)) {
    /* try to read a packet (and it will be ignored),
     * in case a packet with no data arrived */
    slen = sizeof (sa);
    recvfrom (udpsrc->sock.fd, (char *) &slen, 0, 0, &sa.sa, &slen);

    /* clear any error, in case a poll error occurred */
    clear_error (udpsrc);

    /* poll again */
    goto retry;
  }

no_select:
  GST_LOG_OBJECT (udpsrc, "ioctl says %d bytes available", (int) readsize);

  pktdata = g_malloc (readsize);
  pktsize = readsize;

  while (TRUE) {
    slen = sizeof (sa);
#ifdef G_OS_WIN32
    ret = recvfrom (udpsrc->sock.fd, (char *) pktdata, pktsize, 0, &sa.sa,
        &slen);
#else
    ret = recvfrom (udpsrc->sock.fd, pktdata, pktsize, 0, &sa.sa, &slen);
#endif
    if (G_UNLIKELY (ret < 0)) {
#ifdef G_OS_WIN32
      /* WSAECONNRESET for a UDP socket means that a packet sent with udpsink
       * generated a "port unreachable" ICMP response. We ignore that and try
       * again. */
      if (WSAGetLastError () == WSAECONNRESET) {
        g_free (pktdata);
        pktdata = NULL;
        goto retry;
      }
      if (WSAGetLastError () != WSAEINTR)
        goto receive_error;
#else
      if (errno != EAGAIN && errno != EINTR)
        goto receive_error;
#endif
    } else
      break;
  }

  /* special case buffer so receivers can also track the address */
  outbuf = gst_netbuffer_new ();
  GST_BUFFER_MALLOCDATA (outbuf) = pktdata;

  /* patch pktdata and len when stripping off the headers */
  if (G_UNLIKELY (udpsrc->skip_first_bytes != 0)) {
    if (G_UNLIKELY (readsize < udpsrc->skip_first_bytes))
      goto skip_error;

    pktdata += udpsrc->skip_first_bytes;
    ret -= udpsrc->skip_first_bytes;
  }
  GST_BUFFER_DATA (outbuf) = pktdata;
  GST_BUFFER_SIZE (outbuf) = ret;

  switch (sa.sa.sa_family) {
    case AF_INET:
    {
      gst_netaddress_set_ip4_address (&outbuf->from, sa.sa_in.sin_addr.s_addr,
          sa.sa_in.sin_port);
    }
      break;
    case AF_INET6:
    {
      guint8 ip6[16];

      memcpy (ip6, &sa.sa_in6.sin6_addr, sizeof (ip6));
      gst_netaddress_set_ip6_address (&outbuf->from, ip6, sa.sa_in6.sin6_port);
    }
      break;
    default:
#ifdef G_OS_WIN32
      WSASetLastError (WSAEAFNOSUPPORT);
#else
      errno = EAFNOSUPPORT;
#endif
      goto receive_error;
  }
  GST_LOG_OBJECT (udpsrc, "read %d bytes", (int) readsize);

  *buf = GST_BUFFER_CAST (outbuf);

  return GST_FLOW_OK;

  /* ERRORS */
select_error:
  {
    GST_ELEMENT_ERROR (udpsrc, RESOURCE, READ, (NULL),
        ("select error %d: %s (%d)", ret, g_strerror (errno), errno));
    return GST_FLOW_ERROR;
  }
stopped:
  {
    GST_DEBUG ("stop called");
    return GST_FLOW_WRONG_STATE;
  }
ioctl_failed:
  {
    GST_ELEMENT_ERROR (udpsrc, RESOURCE, READ, (NULL),
        ("ioctl failed %d: %s (%d)", ret, g_strerror (errno), errno));
    return GST_FLOW_ERROR;
  }
receive_error:
  {
    g_free (pktdata);
#ifdef G_OS_WIN32
    GST_ELEMENT_ERROR (udpsrc, RESOURCE, READ, (NULL),
        ("receive error %d (WSA error: %d)", ret, WSAGetLastError ()));
#else
    GST_ELEMENT_ERROR (udpsrc, RESOURCE, READ, (NULL),
        ("receive error %d: %s (%d)", ret, g_strerror (errno), errno));
#endif
    return GST_FLOW_ERROR;
  }
skip_error:
  {
    GST_ELEMENT_ERROR (udpsrc, STREAM, DECODE, (NULL),
        ("UDP buffer to small to skip header"));
    return GST_FLOW_ERROR;
  }
}

static gboolean
gst_udpsrc_set_uri (GstUDPSrc * src, const gchar * uri)
{
  if (gst_udp_parse_uri (uri, &src->uri) < 0)
    goto wrong_uri;

  if (src->uri.port == -1)
    src->uri.port = UDP_DEFAULT_PORT;

  return TRUE;

  /* ERRORS */
wrong_uri:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL),
        ("error parsing uri %s", uri));
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
      gst_udp_uri_update (&udpsrc->uri, NULL, g_value_get_int (value));
      break;
    case PROP_MULTICAST_GROUP:
    {
      const gchar *group;

      if ((group = g_value_get_string (value)))
        gst_udp_uri_update (&udpsrc->uri, group, -1);
      else
        gst_udp_uri_update (&udpsrc->uri, UDP_DEFAULT_MULTICAST_GROUP, -1);
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
      gst_udpsrc_set_uri (udpsrc, g_value_get_string (value));
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

      old_caps = udpsrc->caps;
      udpsrc->caps = new_caps;
      if (old_caps)
        gst_caps_unref (old_caps);
      gst_pad_set_caps (GST_BASE_SRC (udpsrc)->srcpad, new_caps);
      break;
    }
    case PROP_SOCKFD:
      if (udpsrc->sockfd >= 0 && udpsrc->sockfd != udpsrc->sock.fd &&
          udpsrc->closefd)
        CLOSE_SOCKET (udpsrc->sockfd);
      udpsrc->sockfd = g_value_get_int (value);
      GST_DEBUG ("setting SOCKFD to %d", udpsrc->sockfd);
      break;
    case PROP_TIMEOUT:
      udpsrc->timeout = g_value_get_uint64 (value);
      break;
    case PROP_SKIP_FIRST_BYTES:
      udpsrc->skip_first_bytes = g_value_get_int (value);
      break;
    case PROP_CLOSEFD:
      udpsrc->closefd = g_value_get_boolean (value);
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
      g_value_set_int (value, udpsrc->uri.port);
      break;
    case PROP_MULTICAST_GROUP:
      g_value_set_string (value, udpsrc->uri.host);
      break;
    case PROP_MULTICAST_IFACE:
      g_value_set_string (value, udpsrc->multi_iface);
      break;
    case PROP_URI:
      g_value_take_string (value, gst_udp_uri_string (&udpsrc->uri));
      break;
    case PROP_CAPS:
      gst_value_set_caps (value, udpsrc->caps);
      break;
    case PROP_SOCKFD:
      g_value_set_int (value, udpsrc->sockfd);
      break;
    case PROP_TIMEOUT:
      g_value_set_uint64 (value, udpsrc->timeout);
      break;
    case PROP_SKIP_FIRST_BYTES:
      g_value_set_int (value, udpsrc->skip_first_bytes);
      break;
    case PROP_CLOSEFD:
      g_value_set_boolean (value, udpsrc->closefd);
      break;
    case PROP_SOCK:
      g_value_set_int (value, udpsrc->sock.fd);
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

/* create a socket for sending to remote machine */
static gboolean
gst_udpsrc_start (GstBaseSrc * bsrc)
{
  guint bc_val;
  guint err_val;
  gint reuse;
  int port;
  GstUDPSrc *src;
  gint ret;
  int rcvsize;
  struct sockaddr_storage bind_address;
  socklen_t len;
  src = GST_UDPSRC (bsrc);

  if (src->sockfd == -1) {
    /* need to allocate a socket */
    GST_DEBUG_OBJECT (src, "allocating socket for %s:%d", src->uri.host,
        src->uri.port);
    if ((ret =
            gst_udp_get_addr (src->uri.host, src->uri.port, &src->myaddr)) < 0)
      goto getaddrinfo_error;

    if ((ret = socket (src->myaddr.ss_family, SOCK_DGRAM, IPPROTO_UDP)) < 0)
      goto no_socket;

    src->sock.fd = ret;
    src->externalfd = FALSE;

    GST_DEBUG_OBJECT (src, "got socket %d", src->sock.fd);

    GST_DEBUG_OBJECT (src, "setting reuse %d", src->reuse);
    reuse = src->reuse ? 1 : 0;
    if ((ret =
            setsockopt (src->sock.fd, SOL_SOCKET, SO_REUSEADDR, &reuse,
                sizeof (reuse))) < 0)
      goto setsockopt_error;

    GST_DEBUG_OBJECT (src, "binding on port %d", src->uri.port);

    /* Take a temporary copy of the address in case we need to fix it for bind */
    memcpy (&bind_address, &src->myaddr, sizeof (struct sockaddr_storage));

#ifdef G_OS_WIN32
    /* Windows does not allow binding to a multicast group so fix source address */
    if (gst_udp_is_multicast (&src->myaddr)) {
      switch (((struct sockaddr *) &bind_address)->sa_family) {
        case AF_INET:
          ((struct sockaddr_in *) &bind_address)->sin_addr.s_addr =
              htonl (INADDR_ANY);
          break;
        case AF_INET6:
          ((struct sockaddr_in6 *) &bind_address)->sin6_addr = in6addr_any;
          break;
        default:
          break;
      }
    }
#endif

    len = gst_udp_get_sockaddr_length (&bind_address);
    if ((ret = bind (src->sock.fd, (struct sockaddr *) &bind_address, len)) < 0)
      goto bind_error;

    if (!gst_udp_is_multicast (&src->myaddr)) {
      len = sizeof (src->myaddr);
      if ((ret = getsockname (src->sock.fd, (struct sockaddr *) &src->myaddr,
                  &len)) < 0)
        goto getsockname_error;
    }
  } else {
    GST_DEBUG_OBJECT (src, "using provided socket %d", src->sockfd);
    /* we use the configured socket, try to get some info about it */
    len = sizeof (src->myaddr);
    if ((ret =
            getsockname (src->sockfd, (struct sockaddr *) &src->myaddr,
                &len)) < 0)
      goto getsockname_error;

    src->sock.fd = src->sockfd;
    src->externalfd = TRUE;
  }

  len = sizeof (rcvsize);
  if (src->buffer_size != 0) {
    rcvsize = src->buffer_size;

    GST_DEBUG_OBJECT (src, "setting udp buffer of %d bytes", rcvsize);
    /* set buffer size, Note that on Linux this is typically limited to a
     * maximum of around 100K. Also a minimum of 128 bytes is required on
     * Linux. */
    ret =
        setsockopt (src->sock.fd, SOL_SOCKET, SO_RCVBUF, (void *) &rcvsize,
        len);
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
      getsockopt (src->sock.fd, SOL_SOCKET, SO_RCVBUF, (void *) &rcvsize, &len);
  if (ret == 0)
    GST_DEBUG_OBJECT (src, "have udp buffer of %d bytes", rcvsize);
  else
    GST_DEBUG_OBJECT (src, "could not get udp buffer size");

  bc_val = 1;
  if ((ret = setsockopt (src->sock.fd, SOL_SOCKET, SO_BROADCAST, &bc_val,
              sizeof (bc_val))) < 0) {
    GST_ELEMENT_WARNING (src, RESOURCE, SETTINGS, (NULL),
        ("could not configure socket for broadcast %d: %s (%d)", ret,
            g_strerror (errno), errno));
  }

  /* Accept ERRQUEUE to get and flush icmp errors */
  err_val = 1;
#if defined (IP_RECVERR)
  if ((ret = setsockopt (src->sock.fd, IPPROTO_IP, IP_RECVERR, &err_val,
              sizeof (err_val))) < 0) {
    GST_ELEMENT_WARNING (src, RESOURCE, SETTINGS, (NULL),
        ("could not configure socket for IP_RECVERR %d: %s (%d)", ret,
            g_strerror (errno), errno));
  }
#endif

  if (src->auto_multicast && gst_udp_is_multicast (&src->myaddr)) {
    GST_DEBUG_OBJECT (src, "joining multicast group %s", src->uri.host);
    ret = gst_udp_join_group (src->sock.fd, &src->myaddr, src->multi_iface);
    if (ret < 0)
      goto membership;
  }

  /* NOTE: sockaddr_in.sin_port works for ipv4 and ipv6 because sin_port
   * follows ss_family on both */
  port = g_ntohs (((struct sockaddr_in *) &src->myaddr)->sin_port);
  GST_DEBUG_OBJECT (src, "bound, on port %d", port);
  if (port != src->uri.port) {
    src->uri.port = port;
    GST_DEBUG_OBJECT (src, "notifying port %d", port);
    g_object_notify (G_OBJECT (src), "port");
  }

  if ((src->fdset = gst_poll_new (TRUE)) == NULL)
    goto no_fdset;

  gst_poll_add_fd (src->fdset, &src->sock);
  gst_poll_fd_ctl_read (src->fdset, &src->sock, TRUE);

  return TRUE;

  /* ERRORS */
getaddrinfo_error:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, SETTINGS, (NULL),
        ("getaddrinfo failed: %s (%d)", gai_strerror (ret), ret));
    return FALSE;
  }
no_socket:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL),
        ("no socket error %d: %s (%d)", ret, g_strerror (errno), errno));
    return FALSE;
  }
setsockopt_error:
  {
    CLOSE_IF_REQUESTED (src);
    GST_ELEMENT_ERROR (src, RESOURCE, SETTINGS, (NULL),
        ("setsockopt failed %d: %s (%d)", ret, g_strerror (errno), errno));
    return FALSE;
  }
bind_error:
  {
    CLOSE_IF_REQUESTED (src);
    GST_ELEMENT_ERROR (src, RESOURCE, SETTINGS, (NULL),
        ("bind failed %d: %s (%d)", ret, g_strerror (errno), errno));
    return FALSE;
  }
membership:
  {
    CLOSE_IF_REQUESTED (src);
    GST_ELEMENT_ERROR (src, RESOURCE, SETTINGS, (NULL),
        ("could add membership %d: %s (%d)", ret, g_strerror (errno), errno));
    return FALSE;
  }
getsockname_error:
  {
    CLOSE_IF_REQUESTED (src);
    GST_ELEMENT_ERROR (src, RESOURCE, SETTINGS, (NULL),
        ("getsockname failed %d: %s (%d)", ret, g_strerror (errno), errno));
    return FALSE;
  }
no_fdset:
  {
    CLOSE_IF_REQUESTED (src);
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ_WRITE, (NULL),
        ("could not create an fdset %d: %s (%d)", ret, g_strerror (errno),
            errno));
    return FALSE;
  }
}

static gboolean
gst_udpsrc_unlock (GstBaseSrc * bsrc)
{
  GstUDPSrc *src;

  src = GST_UDPSRC (bsrc);

  GST_LOG_OBJECT (src, "Flushing");
  gst_poll_set_flushing (src->fdset, TRUE);

  return TRUE;
}

static gboolean
gst_udpsrc_unlock_stop (GstBaseSrc * bsrc)
{
  GstUDPSrc *src;

  src = GST_UDPSRC (bsrc);

  GST_LOG_OBJECT (src, "No longer flushing");
  gst_poll_set_flushing (src->fdset, FALSE);

  return TRUE;
}

static gboolean
gst_udpsrc_stop (GstBaseSrc * bsrc)
{
  GstUDPSrc *src;

  src = GST_UDPSRC (bsrc);

  GST_DEBUG ("stopping, closing sockets");

  if (src->sock.fd >= 0) {
    if (src->auto_multicast && gst_udp_is_multicast (&src->myaddr)) {
      GST_DEBUG_OBJECT (src, "leaving multicast group %s", src->uri.host);
      gst_udp_leave_group (src->sock.fd, &src->myaddr);
    }
    CLOSE_IF_REQUESTED (src);
  }

  if (src->fdset) {
    gst_poll_free (src->fdset);
    src->fdset = NULL;
  }

  return TRUE;
}

/*** GSTURIHANDLER INTERFACE *************************************************/

static GstURIType
gst_udpsrc_uri_get_type (void)
{
  return GST_URI_SRC;
}

static gchar **
gst_udpsrc_uri_get_protocols (void)
{
  static gchar *protocols[] = { (char *) "udp", NULL };

  return protocols;
}

static const gchar *
gst_udpsrc_uri_get_uri (GstURIHandler * handler)
{
  GstUDPSrc *src = GST_UDPSRC (handler);

  g_free (src->uristr);
  src->uristr = gst_udp_uri_string (&src->uri);

  return src->uristr;
}

static gboolean
gst_udpsrc_uri_set_uri (GstURIHandler * handler, const gchar * uri)
{
  gboolean ret;

  GstUDPSrc *src = GST_UDPSRC (handler);

  ret = gst_udpsrc_set_uri (src, uri);

  return ret;
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
