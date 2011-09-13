/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2004> Thomas Vander Stichele <thomas at apestaart dot org>
 *
 * gsttcp.c: TCP functions
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/ioctl.h>

#ifdef HAVE_FIONREAD_IN_SYS_FILIO
#include <sys/filio.h>
#endif

#include "gsttcp.h"
#include <gst/gst-i18n-plugin.h>

GST_DEBUG_CATEGORY_EXTERN (tcp_debug);
#define GST_CAT_DEFAULT tcp_debug

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

/* resolve host to IP address, throwing errors if it fails */
/* host can already be an IP address */
/* returns a newly allocated gchar * with the dotted ip address,
   or NULL, in which case it already fired an error. */
gchar *
gst_tcp_host_to_ip (GstElement * element, const gchar * host)
{
  struct hostent *hostinfo;
  char **addrs;
  gchar *ip;
  struct in_addr addr;

  GST_DEBUG_OBJECT (element, "resolving host %s", host);

  /* first check if it already is an IP address */
  if (inet_aton (host, &addr)) {
    ip = g_strdup (host);
    goto beach;
  }
  /* FIXME: could do a localhost check here */

  /* perform a name lookup */
  if (!(hostinfo = gethostbyname (host)))
    goto resolve_error;

  if (hostinfo->h_addrtype != AF_INET)
    goto not_ip;

  addrs = hostinfo->h_addr_list;

  /* There could be more than one IP address, but we just return the first */
  ip = g_strdup (inet_ntoa (*(struct in_addr *) *addrs));

beach:
  GST_DEBUG_OBJECT (element, "resolved to IP %s", ip);
  return ip;

resolve_error:
  {
    GST_ELEMENT_ERROR (element, RESOURCE, NOT_FOUND, (NULL),
        ("Could not find IP address for host \"%s\".", host));
    return NULL;
  }
not_ip:
  {
    GST_ELEMENT_ERROR (element, RESOURCE, NOT_FOUND, (NULL),
        ("host \"%s\" is not an IP host", host));
    return NULL;
  }
}

/* write buffer to given socket incrementally.
 * Returns number of bytes written.
 */
gint
gst_tcp_socket_write (int socket, const void *buf, size_t count)
{
  size_t bytes_written = 0;

  while (bytes_written < count) {
    ssize_t wrote = send (socket, (const char *) buf + bytes_written,
        count - bytes_written, MSG_NOSIGNAL);

    if (wrote <= 0) {
      GST_WARNING ("error while writing");
      return bytes_written;
    }
    bytes_written += wrote;
  }

  GST_LOG ("wrote %" G_GSIZE_FORMAT " bytes successfully", bytes_written);
  return bytes_written;
}

/* atomically read count bytes into buf, cancellable. return val of GST_FLOW_OK
 * indicates success, anything else is failure.
 */
static GstFlowReturn
gst_tcp_socket_read (GstElement * this, int socket, void *buf, size_t count,
    GstPoll * fdset)
{
  ssize_t n;
  size_t bytes_read;
  int num_to_read;
  int ret;

  bytes_read = 0;

  while (bytes_read < count) {
    /* do a blocking select on the socket */
    /* no action (0) is an error too in our case */
    if ((ret = gst_poll_wait (fdset, GST_CLOCK_TIME_NONE)) <= 0) {
      if (ret == -1 && errno == EBUSY)
        goto cancelled;
      else
        goto select_error;
    }

    /* ask how much is available for reading on the socket */
    if (ioctl (socket, FIONREAD, &num_to_read) < 0)
      goto ioctl_error;

    if (num_to_read == 0)
      goto got_eos;

    /* sizeof(ssize_t) >= sizeof(int), so I know num_to_read <= SSIZE_MAX */

    num_to_read = MIN (num_to_read, count - bytes_read);

    n = read (socket, ((guint8 *) buf) + bytes_read, num_to_read);

    if (n < 0)
      goto read_error;

    if (n < num_to_read)
      goto short_read;

    bytes_read += num_to_read;
  }

  return GST_FLOW_OK;

  /* ERRORS */
select_error:
  {
    GST_ELEMENT_ERROR (this, RESOURCE, READ, (NULL),
        ("select failed: %s", g_strerror (errno)));
    return GST_FLOW_ERROR;
  }
cancelled:
  {
    GST_DEBUG_OBJECT (this, "Select was cancelled");
    return GST_FLOW_WRONG_STATE;
  }
ioctl_error:
  {
    GST_ELEMENT_ERROR (this, RESOURCE, READ, (NULL),
        ("ioctl failed: %s", g_strerror (errno)));
    return GST_FLOW_ERROR;
  }
got_eos:
  {
    GST_DEBUG_OBJECT (this, "Got EOS on socket stream");
    return GST_FLOW_UNEXPECTED;
  }
read_error:
  {
    GST_ELEMENT_ERROR (this, RESOURCE, READ, (NULL),
        ("read failed: %s", g_strerror (errno)));
    return GST_FLOW_ERROR;
  }
short_read:
  {
    GST_ELEMENT_ERROR (this, RESOURCE, READ, (NULL),
        ("short read: wanted %d bytes, got %" G_GSSIZE_FORMAT, num_to_read, n));
    return GST_FLOW_ERROR;
  }
}

/* close the socket and reset the fd.  Used to clean up after errors. */
void
gst_tcp_socket_close (GstPollFD * socket)
{
  if (socket->fd >= 0) {
    close (socket->fd);
    socket->fd = -1;
  }
}

/* read a buffer from the given socket
 * returns:
 * - a GstBuffer in which data should be read
 * - NULL, indicating a connection close or an error, to be handled with
 *         EOS
 */
GstFlowReturn
gst_tcp_read_buffer (GstElement * this, int socket, GstPoll * fdset,
    GstBuffer ** buf)
{
  int ret;
  ssize_t bytes_read;
  int readsize;

  *buf = NULL;

  /* do a blocking select on the socket */
  /* no action (0) is an error too in our case */
  if ((ret = gst_poll_wait (fdset, GST_CLOCK_TIME_NONE)) <= 0) {
    if (ret == -1 && errno == EBUSY)
      goto cancelled;
    else
      goto select_error;
  }

  /* ask how much is available for reading on the socket */
  if (ioctl (socket, FIONREAD, &readsize) < 0)
    goto ioctl_error;

  if (readsize == 0)
    goto got_eos;

  /* sizeof(ssize_t) >= sizeof(int), so I know readsize <= SSIZE_MAX */

  *buf = gst_buffer_new_and_alloc (readsize);

  bytes_read = read (socket, GST_BUFFER_DATA (*buf), readsize);

  if (bytes_read < 0)
    goto read_error;

  if (bytes_read < readsize)
    /* but mom, you promised to give me readsize bytes! */
    goto short_read;

  GST_LOG_OBJECT (this, "returning buffer of size %d", GST_BUFFER_SIZE (*buf));
  return GST_FLOW_OK;

  /* ERRORS */
select_error:
  {
    GST_ELEMENT_ERROR (this, RESOURCE, READ, (NULL),
        ("select failed: %s", g_strerror (errno)));
    return GST_FLOW_ERROR;
  }
cancelled:
  {
    GST_DEBUG_OBJECT (this, "Select was cancelled");
    return GST_FLOW_WRONG_STATE;
  }
ioctl_error:
  {
    GST_ELEMENT_ERROR (this, RESOURCE, READ, (NULL),
        ("ioctl failed: %s", g_strerror (errno)));
    return GST_FLOW_ERROR;
  }
got_eos:
  {
    GST_DEBUG_OBJECT (this, "Got EOS on socket stream");
    return GST_FLOW_UNEXPECTED;
  }
read_error:
  {
    GST_ELEMENT_ERROR (this, RESOURCE, READ, (NULL),
        ("read failed: %s", g_strerror (errno)));
    gst_buffer_unref (*buf);
    *buf = NULL;
    return GST_FLOW_ERROR;
  }
short_read:
  {
    GST_ELEMENT_ERROR (this, RESOURCE, READ, (NULL),
        ("short read: wanted %d bytes, got %" G_GSSIZE_FORMAT, readsize,
            bytes_read));
    gst_buffer_unref (*buf);
    *buf = NULL;
    return GST_FLOW_ERROR;
  }
}

/* read a buffer from the given socket
 * returns:
 * - a GstBuffer in which data should be read
 * - NULL, indicating a connection close or an error, to be handled with
 *         EOS
 */
GstFlowReturn
gst_tcp_gdp_read_buffer (GstElement * this, int socket, GstPoll * fdset,
    GstBuffer ** buf)
{
  GstFlowReturn ret;
  guint8 *header = NULL;

  GST_LOG_OBJECT (this, "Reading %d bytes for buffer packet header",
      GST_DP_HEADER_LENGTH);

  *buf = NULL;
  header = g_malloc (GST_DP_HEADER_LENGTH);

  ret = gst_tcp_socket_read (this, socket, header, GST_DP_HEADER_LENGTH, fdset);

  if (ret != GST_FLOW_OK)
    goto header_read_error;

  if (!gst_dp_validate_header (GST_DP_HEADER_LENGTH, header))
    goto validate_error;

  if (gst_dp_header_payload_type (header) != GST_DP_PAYLOAD_BUFFER)
    goto is_not_buffer;

  GST_LOG_OBJECT (this, "validated buffer packet header");

  *buf = gst_dp_buffer_from_header (GST_DP_HEADER_LENGTH, header);

  g_free (header);

  ret = gst_tcp_socket_read (this, socket, GST_BUFFER_DATA (*buf),
      GST_BUFFER_SIZE (*buf), fdset);

  if (ret != GST_FLOW_OK)
    goto data_read_error;

  return GST_FLOW_OK;

  /* ERRORS */
header_read_error:
  {
    g_free (header);
    return ret;
  }
validate_error:
  {
    GST_ELEMENT_ERROR (this, RESOURCE, READ, (NULL),
        ("GDP buffer packet header does not validate"));
    g_free (header);
    return GST_FLOW_ERROR;
  }
is_not_buffer:
  {
    GST_ELEMENT_ERROR (this, RESOURCE, READ, (NULL),
        ("GDP packet contains something that is not a buffer (type %d)",
            gst_dp_header_payload_type (header)));
    g_free (header);
    return GST_FLOW_ERROR;
  }
data_read_error:
  {
    gst_buffer_unref (*buf);
    *buf = NULL;
    return ret;
  }
}

GstFlowReturn
gst_tcp_gdp_read_caps (GstElement * this, int socket, GstPoll * fdset,
    GstCaps ** caps)
{
  GstFlowReturn ret;
  guint8 *header = NULL;
  guint8 *payload = NULL;
  size_t payload_length;

  GST_LOG_OBJECT (this, "Reading %d bytes for caps packet header",
      GST_DP_HEADER_LENGTH);

  *caps = NULL;
  header = g_malloc (GST_DP_HEADER_LENGTH);

  ret = gst_tcp_socket_read (this, socket, header, GST_DP_HEADER_LENGTH, fdset);

  if (ret != GST_FLOW_OK)
    goto header_read_error;

  if (!gst_dp_validate_header (GST_DP_HEADER_LENGTH, header))
    goto header_validate_error;

  if (gst_dp_header_payload_type (header) != GST_DP_PAYLOAD_CAPS)
    goto is_not_caps;

  GST_LOG_OBJECT (this, "validated caps packet header");

  payload_length = gst_dp_header_payload_length (header);
  payload = g_malloc (payload_length);

  GST_LOG_OBJECT (this,
      "Reading %" G_GSIZE_FORMAT " bytes for caps packet payload",
      payload_length);

  ret = gst_tcp_socket_read (this, socket, payload, payload_length, fdset);

  if (ret != GST_FLOW_OK)
    goto payload_read_error;

  if (!gst_dp_validate_payload (GST_DP_HEADER_LENGTH, header, payload))
    goto payload_validate_error;

  *caps = gst_dp_caps_from_packet (GST_DP_HEADER_LENGTH, header, payload);

  GST_DEBUG_OBJECT (this, "Got caps over GDP: %" GST_PTR_FORMAT, *caps);

  g_free (header);
  g_free (payload);

  return GST_FLOW_OK;

  /* ERRORS */
header_read_error:
  {
    g_free (header);
    return ret;
  }
header_validate_error:
  {
    GST_ELEMENT_ERROR (this, RESOURCE, READ, (NULL),
        ("GDP caps packet header does not validate"));
    g_free (header);
    return GST_FLOW_ERROR;
  }
is_not_caps:
  {
    GST_ELEMENT_ERROR (this, RESOURCE, READ, (NULL),
        ("GDP packet contains something that is not a caps (type %d)",
            gst_dp_header_payload_type (header)));
    g_free (header);
    return GST_FLOW_ERROR;
  }
payload_read_error:
  {
    g_free (header);
    g_free (payload);
    return ret;
  }
payload_validate_error:
  {
    GST_ELEMENT_ERROR (this, RESOURCE, READ, (NULL),
        ("GDP caps packet payload does not validate"));
    g_free (header);
    g_free (payload);
    return GST_FLOW_ERROR;
  }
}

/* write a GDP header to the socket.  Return false if fails. */
gboolean
gst_tcp_gdp_write_buffer (GstElement * this, int socket, GstBuffer * buffer,
    gboolean fatal, const gchar * host, int port)
{
  guint length;
  guint8 *header;
  size_t wrote;

  if (!gst_dp_header_from_buffer (buffer, 0, &length, &header))
    goto create_error;

  GST_LOG_OBJECT (this, "writing %d bytes for GDP buffer header", length);
  wrote = gst_tcp_socket_write (socket, header, length);
  g_free (header);

  if (wrote != length)
    goto write_error;

  return TRUE;

  /* ERRORS */
create_error:
  {
    if (fatal)
      GST_ELEMENT_ERROR (this, CORE, TOO_LAZY, (NULL),
          ("Could not create GDP header from buffer"));
    return FALSE;
  }
write_error:
  {
    if (fatal)
      GST_ELEMENT_ERROR (this, RESOURCE, WRITE,
          (_("Error while sending data to \"%s:%d\"."), host, port),
          ("Only %" G_GSIZE_FORMAT " of %u bytes written: %s",
              wrote, GST_BUFFER_SIZE (buffer), g_strerror (errno)));
    return FALSE;
  }
}

/* write GDP header and payload to the given socket for the given caps.
 * Return false if fails. */
gboolean
gst_tcp_gdp_write_caps (GstElement * this, int socket, const GstCaps * caps,
    gboolean fatal, const char *host, int port)
{
  guint length;
  guint8 *header;
  guint8 *payload;
  size_t wrote;

  if (!gst_dp_packet_from_caps (caps, 0, &length, &header, &payload))
    goto create_error;

  GST_LOG_OBJECT (this, "writing %d bytes for GDP caps header", length);
  wrote = gst_tcp_socket_write (socket, header, length);
  if (wrote != length)
    goto write_header_error;

  length = gst_dp_header_payload_length (header);
  g_free (header);

  GST_LOG_OBJECT (this, "writing %d bytes for GDP caps payload", length);
  wrote = gst_tcp_socket_write (socket, payload, length);
  g_free (payload);

  if (wrote != length)
    goto write_payload_error;

  return TRUE;

  /* ERRORS */
create_error:
  {
    if (fatal)
      GST_ELEMENT_ERROR (this, CORE, TOO_LAZY, (NULL),
          ("Could not create GDP packet from caps"));
    return FALSE;
  }
write_header_error:
  {
    g_free (header);
    g_free (payload);
    if (fatal)
      GST_ELEMENT_ERROR (this, RESOURCE, WRITE,
          (_("Error while sending gdp header data to \"%s:%d\"."), host, port),
          ("Only %" G_GSIZE_FORMAT " of %u bytes written: %s",
              wrote, length, g_strerror (errno)));
    return FALSE;
  }
write_payload_error:
  {
    if (fatal)
      GST_ELEMENT_ERROR (this, RESOURCE, WRITE,
          (_("Error while sending gdp payload data to \"%s:%d\"."), host, port),
          ("Only %" G_GSIZE_FORMAT " of %u bytes written: %s",
              wrote, length, g_strerror (errno)));
    return FALSE;
  }
}
