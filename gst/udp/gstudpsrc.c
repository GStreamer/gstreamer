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
 * <refsect2>
 * <para>
 * udpsrc is a network source that reads UDP packets from the network.
 * It can be combined with RTP depayloaders to implement RTP streaming.
 * </para>
 * <title>Examples</title>
 * <para>
 * Here is a simple pipeline to read from the default port and dump the udp packets.
 * <programlisting>
 * gst-launch -v udpsrc ! fakesink dump=1
 * </programlisting>
 * To actually generate udp packets on the default port one can use the
 * udpsink element. When running the following pipeline in another terminal, the
 * above mentioned pipeline should dump data packets to the console.
 * <programlisting>
 * gst-launch -v audiotestsrc ! udpsink
 * </programlisting>
 * </para>
 * <para>
 * The udpsrc element supports automatic port allocation by setting the
 * "port" property to 0. the following pipeline reads UDP from a free port.
 * <programlisting>
 * gst-launch -v udpsrc port=0 ! fakesink
 * </programlisting>
 * After setting the udpsrc to PAUSED, the allocated port can be obtained by
 * reading the port property.
 * </para>
 * <para>
 * udpsrc can read from multicast groups by setting the multicast_group property
 * to the IP address of the multicast group.
 * </para>
 * <para>
 * Alternatively one can provide a custom socket to udpsrc with the "sockfd" property,
 * udpsrc will then not allocate a socket itself but use the provided one.
 * </para>
 * <para>
 * The "caps" property is mainly used to give a type to the UDP packet so that they
 * can be autoplugged in GStreamer pipelines. This is very usefull for RTP 
 * implementations where the contents of the UDP packets is transfered out-of-bounds
 * using SDP or other means. 
 * </para>
 * <para>
 * The "buffer" property is used to change the default kernel buffer sizes used for
 * receiving packets. The buffer size may be increased for high-volume connections,
 * or may be decreased to limit the possible backlog of incoming data.
 * The system places an absolute limit on these values, on Linux, for example, the
 * default buffer size is typically 50K and can be increased to maximally 100K.
 * </para>
 * <para>
 * The "skip-first-bytes" property is used to strip off an arbitrary number of
 * bytes from the start of the raw udp packet and can be used to strip off
 * proprietary header, for example. 
 * </para>
 * <para>
 * The udpsrc is always a live source. It does however not provide a GstClock, this
 * is left for upstream elements such as an RTP session manager or demuxer (such
 * as an MPEG demuxer). As with all live sources, the captured buffers will have
 * their timestamp set to the current running time of the pipeline.
 * </para>
 * <para>
 * udpsrc implements a GstURIHandler interface that handles udp://host:port type
 * URIs.
 * </para>
 * <para>
 * If the <link linkend="GstUDPSrc--timeout">timeout property</link> is set to a
 * value bigger than 0, udpsrc will generate an element message named
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
 * <link linkend="GstUDPSrc--sockfd">sockfd property</link>. The socket will be
 * closed when setting the element to READY by default. This behaviour can be
 * overriden with the <link linkend="GstUDPSrc--closefd">closefd property</link>,
 * in which case the application is responsible for closing the file descriptor.
 * </para>
 * <para>
 * Last reviewed on 2007-09-20 (0.10.7)
 * </para>
 * </refsect2>
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
#ifdef G_OS_WIN32
typedef int socklen_t;
#endif

#ifdef HAVE_FIONREAD_IN_SYS_FILIO
#include <sys/filio.h>
#endif

GST_DEBUG_CATEGORY_STATIC (udpsrc_debug);
#define GST_CAT_DEFAULT (udpsrc_debug)

#define CLOSE_IF_REQUESTED(udpctx)                                        \
G_STMT_START {                                                            \
  if ((!udpctx->externalfd) || (udpctx->externalfd && udpctx->closefd))   \
    CLOSE_SOCKET(udpctx->sock.fd);                                        \
  udpctx->sock.fd = -1;                                                   \
} G_STMT_END

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static const GstElementDetails gst_udpsrc_details =
GST_ELEMENT_DETAILS ("UDP packet receiver",
    "Source/Network",
    "Receive data over the network via UDP",
    "Wim Taymans <wim@fluendo.com>\n"
    "Thijs Vermeir <thijs.vermeir@barco.com>");

#define UDP_DEFAULT_PORT                4951
#define UDP_DEFAULT_MULTICAST_GROUP     "0.0.0.0"
#define UDP_DEFAULT_URI                 "udp://"UDP_DEFAULT_MULTICAST_GROUP":"G_STRINGIFY(UDP_DEFAULT_PORT)
#define UDP_DEFAULT_CAPS                NULL
#define UDP_DEFAULT_SOCKFD              -1
#define UDP_DEFAULT_BUFFER_SIZE		0
#define UDP_DEFAULT_TIMEOUT             0
#define UDP_DEFAULT_SKIP_FIRST_BYTES	0
#define UDP_DEFAULT_CLOSEFD            TRUE
#define UDP_DEFAULT_SOCK                -1

enum
{
  PROP_0,
  PROP_PORT,
  PROP_MULTICAST_GROUP,
  PROP_URI,
  PROP_CAPS,
  PROP_SOCKFD,
  PROP_BUFFER_SIZE,
  PROP_TIMEOUT,
  PROP_SKIP_FIRST_BYTES,
  PROP_CLOSEFD,
  PROP_SOCK
};

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

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));

  gst_element_class_set_details (element_class, &gst_udpsrc_details);
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
          UDP_DEFAULT_PORT, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_MULTICAST_GROUP,
      g_param_spec_string ("multicast_group", "Multicast Group",
          "The Address of multicast group to join", UDP_DEFAULT_MULTICAST_GROUP,
          G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_URI,
      g_param_spec_string ("uri", "URI",
          "URI in the form of udp://multicast_group:port", UDP_DEFAULT_URI,
          G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_CAPS,
      g_param_spec_boxed ("caps", "Caps",
          "The caps of the source pad", GST_TYPE_CAPS, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_SOCKFD,
      g_param_spec_int ("sockfd", "Socket Handle",
          "Socket to use for UDP reception. (-1 == allocate)",
          -1, G_MAXINT, UDP_DEFAULT_SOCKFD, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_BUFFER_SIZE,
      g_param_spec_int ("buffer-size", "Buffer Size",
          "Size of the kernel receive buffer in bytes, 0=default", 0, G_MAXINT,
          UDP_DEFAULT_BUFFER_SIZE, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_TIMEOUT,
      g_param_spec_uint64 ("timeout", "Timeout",
          "Post a message after timeout microseconds (0 = disabled)", 0,
          G_MAXUINT64, UDP_DEFAULT_TIMEOUT, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_SKIP_FIRST_BYTES, g_param_spec_int ("skip-first-bytes",
          "Skip first bytes", "number of bytes to skip for each udp packet", 0,
          G_MAXINT, UDP_DEFAULT_SKIP_FIRST_BYTES, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_CLOSEFD,
      g_param_spec_boolean ("closefd", "Close sockfd",
          "Close sockfd if passed as property on state change",
          UDP_DEFAULT_CLOSEFD, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_SOCK,
      g_param_spec_int ("sock", "Socket Handle",
          "Socket currently in use for UDP reception. (-1 = no socket)",
          -1, G_MAXINT, UDP_DEFAULT_SOCK, G_PARAM_READABLE));

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

  gst_base_src_set_live (GST_BASE_SRC (udpsrc), TRUE);
  udpsrc->port = UDP_DEFAULT_PORT;
  udpsrc->sockfd = UDP_DEFAULT_SOCKFD;
  udpsrc->multi_group = g_strdup (UDP_DEFAULT_MULTICAST_GROUP);
  udpsrc->uri = g_strdup (UDP_DEFAULT_URI);
  udpsrc->buffer_size = UDP_DEFAULT_BUFFER_SIZE;
  udpsrc->timeout = UDP_DEFAULT_TIMEOUT;
  udpsrc->skip_first_bytes = UDP_DEFAULT_SKIP_FIRST_BYTES;
  udpsrc->closefd = UDP_DEFAULT_CLOSEFD;
  udpsrc->externalfd = (udpsrc->sockfd != -1);

  udpsrc->sock.fd = UDP_DEFAULT_SOCK;
  gst_base_src_set_format (GST_BASE_SRC (udpsrc), GST_FORMAT_TIME);
  gst_base_src_set_do_timestamp (GST_BASE_SRC (udpsrc), TRUE);
}

static void
gst_udpsrc_finalize (GObject * object)
{
  GstUDPSrc *udpsrc;

  udpsrc = GST_UDPSRC (object);

  if (udpsrc->caps)
    gst_caps_unref (udpsrc->caps);
  g_free (udpsrc->multi_group);
  g_free (udpsrc->uri);

  WSA_CLEANUP (src);

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

static GstFlowReturn
gst_udpsrc_create (GstPushSrc * psrc, GstBuffer ** buf)
{
  GstUDPSrc *udpsrc;
  GstNetBuffer *outbuf;
  struct sockaddr_in tmpaddr;
  socklen_t len;
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

  udpsrc = GST_UDPSRC (psrc);

retry:
  /* quick check, avoid going in select when we already have data */
  readsize = 0;
  if ((ret = IOCTL_SOCKET (udpsrc->sock.fd, FIONREAD, &readsize)) < 0)
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
    if (ret < 0) {
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
    } else if (ret == 0) {
      /* timeout, post element message */
      gst_element_post_message (GST_ELEMENT_CAST (udpsrc),
          gst_message_new_element (GST_OBJECT_CAST (udpsrc),
              gst_structure_new ("GstUDPSrcTimeout",
                  "timeout", G_TYPE_UINT64, udpsrc->timeout, NULL)));
      try_again = TRUE;
    }
  } while (try_again);

  /* ask how much is available for reading on the socket, this should be exactly
   * one UDP packet. We will check the return value, though, because in some
   * case it can return 0 and we don't want a 0 sized buffer. */
  readsize = 0;
  if ((ret = IOCTL_SOCKET (udpsrc->sock.fd, FIONREAD, &readsize)) < 0)
    goto ioctl_failed;

  /* if we get here and there is nothing to read from the socket, the select got
   * woken up by activity on the socket but it was not a read. We how someone
   * will also do something with the socket so that we don't go into an infinite
   * loop in the select(). */
  if (!readsize)
    goto retry;

no_select:
  GST_LOG_OBJECT (udpsrc, "ioctl says %d bytes available", (int) readsize);

  pktdata = g_malloc (readsize);
  pktsize = readsize;

  while (TRUE) {
    len = sizeof (struct sockaddr);
    ret = recvfrom (udpsrc->sock.fd, pktdata, pktsize,
        0, (struct sockaddr *) &tmpaddr, &len);
    if (ret < 0) {
      if (errno != EAGAIN && errno != EINTR)
        goto receive_error;
    } else
      break;
  }

  /* special case buffer so receivers can also track the address */
  outbuf = gst_netbuffer_new ();
  GST_BUFFER_MALLOCDATA (outbuf) = pktdata;

  /* patch pktdata and len when stripping off the headers */
  if (udpsrc->skip_first_bytes != 0) {
    if (G_UNLIKELY (readsize <= udpsrc->skip_first_bytes))
      goto skip_error;

    pktdata += udpsrc->skip_first_bytes;
    ret -= udpsrc->skip_first_bytes;
  }
  GST_BUFFER_DATA (outbuf) = pktdata;
  GST_BUFFER_SIZE (outbuf) = ret;

  gst_netaddress_set_ip4_address (&outbuf->from, tmpaddr.sin_addr.s_addr,
      tmpaddr.sin_port);

  gst_buffer_set_caps (GST_BUFFER_CAST (outbuf), udpsrc->caps);

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
    GST_ELEMENT_ERROR (udpsrc, RESOURCE, READ, (NULL),
        ("receive error %d: %s (%d)", ret, g_strerror (errno), errno));
    return GST_FLOW_ERROR;
  }
skip_error:
  {
    GST_ELEMENT_ERROR (udpsrc, STREAM, DECODE, (NULL),
        ("UDP buffer to small to skip header"));
    return GST_FLOW_ERROR;
  }
}

/* Call this function when multicastgroup and/or port are updated */

static void
gst_udpsrc_update_uri (GstUDPSrc * src)
{
  g_free (src->uri);
  src->uri = g_strdup_printf ("udp://%s:%d", src->multi_group, src->port);

  GST_DEBUG_OBJECT (src, "updated uri to %s", src->uri);
}

static gboolean
gst_udpsrc_set_uri (GstUDPSrc * src, const gchar * uri)
{
  gchar *protocol;
  gchar *location;
  gchar *colptr;

  protocol = gst_uri_get_protocol (uri);
  if (strcmp (protocol, "udp") != 0)
    goto wrong_protocol;
  g_free (protocol);

  location = gst_uri_get_location (uri);
  if (!location)
    return FALSE;
  colptr = strstr (location, ":");
  if (colptr != NULL) {
    g_free (src->multi_group);
    src->multi_group = g_strndup (location, colptr - location);
    src->port = atoi (colptr + 1);
  } else {
    g_free (src->multi_group);
    src->multi_group = g_strdup (location);
    src->port = UDP_DEFAULT_PORT;
  }
  g_free (location);

  gst_udpsrc_update_uri (src);

  return TRUE;

  /* ERRORS */
wrong_protocol:
  {
    g_free (protocol);
    GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL),
        ("error parsing uri %s: wrong protocol (%s != udp)", uri, protocol));
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
      gst_udpsrc_update_uri (udpsrc);
      break;
    case PROP_MULTICAST_GROUP:
      g_free (udpsrc->multi_group);

      if (g_value_get_string (value) == NULL)
        udpsrc->multi_group = g_strdup (UDP_DEFAULT_MULTICAST_GROUP);
      else
        udpsrc->multi_group = g_value_dup_string (value);
      gst_udpsrc_update_uri (udpsrc);
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
      g_value_set_string (value, udpsrc->multi_group);
      break;
    case PROP_URI:
      g_value_set_string (value, udpsrc->uri);
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
  gint reuse;
  struct sockaddr_in my_addr;
  guint len;
  int port;
  GstUDPSrc *src;
  gint ret;
  int rcvsize;

  src = GST_UDPSRC (bsrc);

  if (!inet_aton (src->multi_group, &(src->multi_addr.imr_multiaddr)))
    src->multi_addr.imr_multiaddr.s_addr = 0;

  if (src->sockfd == -1) {
    /* need to allocate a socket */
    if ((ret = socket (PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
      goto no_socket;

    src->sock.fd = ret;
    src->externalfd = FALSE;

    reuse = 1;
    if ((ret =
            setsockopt (src->sock.fd, SOL_SOCKET, SO_REUSEADDR, &reuse,
                sizeof (reuse))) < 0)
      goto setsockopt_error;

    memset (&src->myaddr, 0, sizeof (src->myaddr));
    src->myaddr.sin_family = AF_INET;   /* host byte order */
    src->myaddr.sin_port = htons (src->port);   /* short, network byte order */

    if (src->multi_addr.imr_multiaddr.s_addr)
      src->myaddr.sin_addr.s_addr = src->multi_addr.imr_multiaddr.s_addr;
    else
      src->myaddr.sin_addr.s_addr = INADDR_ANY;

    GST_DEBUG_OBJECT (src, "binding on port %d", src->port);
    if ((ret = bind (src->sock.fd, (struct sockaddr *) &src->myaddr,
                sizeof (src->myaddr))) < 0)
      goto bind_error;
  } else {
    /* we use the configured socket */
    src->sock.fd = src->sockfd;
    src->externalfd = TRUE;
  }

  if (src->multi_addr.imr_multiaddr.s_addr) {
    src->multi_addr.imr_interface.s_addr = INADDR_ANY;
    if ((ret =
            setsockopt (src->sock.fd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                &src->multi_addr, sizeof (src->multi_addr))) < 0)
      goto membership;
  }

  len = sizeof (my_addr);
  if ((ret =
          getsockname (src->sock.fd, (struct sockaddr *) &my_addr, &len)) < 0)
    goto getsockname_error;

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
    if (ret != 0)
      goto udpbuffer_error;
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
              sizeof (bc_val))) < 0)
    goto no_broadcast;

  port = ntohs (my_addr.sin_port);
  GST_DEBUG_OBJECT (src, "bound, on port %d", port);
  if (port != src->port) {
    src->port = port;
    GST_DEBUG_OBJECT (src, "notifying %d", port);
    g_object_notify (G_OBJECT (src), "port");
  }

  src->myaddr.sin_port = htons (src->port + 1);

  if ((src->fdset = gst_poll_new (TRUE)) == NULL)
    goto no_fdset;

  gst_poll_add_fd (src->fdset, &src->sock);
  gst_poll_fd_ctl_read (src->fdset, &src->sock, TRUE);

  return TRUE;

  /* ERRORS */
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
udpbuffer_error:
  {
    CLOSE_IF_REQUESTED (src);
    GST_ELEMENT_ERROR (src, RESOURCE, SETTINGS, (NULL),
        ("Could not create a buffer of the size requested, %d: %s (%d)", ret,
            g_strerror (errno), errno));
    return FALSE;
  }
no_broadcast:
  {
    CLOSE_IF_REQUESTED (src);
    GST_ELEMENT_ERROR (src, RESOURCE, SETTINGS, (NULL),
        ("could not configure socket for broadcast %d: %s (%d)", ret,
            g_strerror (errno), errno));
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
  static gchar *protocols[] = { "udp", NULL };

  return protocols;
}

static const gchar *
gst_udpsrc_uri_get_uri (GstURIHandler * handler)
{
  GstUDPSrc *src = GST_UDPSRC (handler);

  return src->uri;
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
