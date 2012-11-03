/* GStreamer
 * Copyright (C) <2007> Leandro Melo de Sales <leandroal@gmail.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstdccp.h"

#ifdef HAVE_FIONREAD_IN_SYS_FILIO
#include <sys/filio.h>
#endif

/*
 * Resolves host to IP address
 * @param element - the element
 * @return a gchar pointer containing the ip address or NULL if it
 * couldn't resolve the host to a IP adress
 */
gchar *
gst_dccp_host_to_ip (GstElement * element, const gchar * host)
{
  struct hostent *hostinfo;
  char **addrs;
  gchar *ip;
  struct in_addr addr;

  GST_DEBUG_OBJECT (element, "resolving host %s", host);

  /* first check if it already is an IP address */
#ifndef G_OS_WIN32
  if (inet_aton (host, &addr)) {
#else
  if ((addr.S_un.S_addr = inet_addr (host)) != INADDR_NONE) {
#endif
    ip = g_strdup (host);
    GST_DEBUG_OBJECT (element, "resolved to IP %s", ip);
    return ip;
  }

  /* perform a name lookup */
  if (!(hostinfo = gethostbyname (host))) {
    GST_ELEMENT_ERROR (element, RESOURCE, NOT_FOUND, (NULL),
        ("Could not find IP address for host \"%s\".", host));
    return NULL;
  }

  if (hostinfo->h_addrtype != AF_INET) {
    GST_ELEMENT_ERROR (element, RESOURCE, NOT_FOUND, (NULL),
        ("host \"%s\" is not an IP host", host));
    return NULL;
  }

  addrs = hostinfo->h_addr_list;

  /* There could be more than one IP address, but we just return the first */
  ip = g_strdup (inet_ntoa (*(struct in_addr *) *addrs));

  return ip;
}

/*
 * Read a buffer from the given socket
 *
 * @param this - the element that has the socket that will be read
 * @param socket - the socket fd that will be read
 * @param buf - the buffer with the data read from the socket
 * @return GST_FLOW_OK if the read operation was successful
 * or GST_FLOW_ERROR indicating a connection close or an error.
 * Handle it with EOS.
 */
GstFlowReturn
gst_dccp_read_buffer (GstElement * this, int socket, GstBuffer ** buf)
{
  fd_set testfds;
  int maxfdp1;
  gssize bytes_read;
#ifndef G_OS_WIN32
  int readsize;
  struct msghdr mh;
  struct iovec iov;
#else
  unsigned long readsize;
#endif

  *buf = NULL;

  /* do a blocking select on the socket */
  FD_ZERO (&testfds);
  FD_SET (socket, &testfds);
  maxfdp1 = socket + 1;

  /* no action (0) is also an error in our case */
  if (select (maxfdp1, &testfds, NULL, NULL, 0) <= 0) {
    GST_ELEMENT_ERROR (this, RESOURCE, READ, (NULL),
        ("select failed: %s", g_strerror (errno)));
    return GST_FLOW_ERROR;
  }

  /* ask how much is available for reading on the socket */
#ifndef G_OS_WIN32
  if (ioctl (socket, FIONREAD, &readsize) < 0) {
    GST_ELEMENT_ERROR (this, RESOURCE, READ, (NULL),
        ("read FIONREAD value failed: %s", g_strerror (errno)));
#else
  if (ioctlsocket (socket, FIONREAD, &readsize) == SOCKET_ERROR) {
    GST_ELEMENT_ERROR (this, RESOURCE, READ, (NULL),
        ("read FIONREAD value failed: %s", g_strerror (WSAGetLastError ())));
#endif
    return GST_FLOW_ERROR;
  }

  if (readsize == 0) {
    GST_DEBUG_OBJECT (this, "Got EOS on socket stream");
    return GST_FLOW_UNEXPECTED;
  }

  *buf = gst_buffer_new_and_alloc ((int) readsize);
#ifndef G_OS_WIN32
  memset (&mh, 0, sizeof (mh));
  mh.msg_name = NULL;
  mh.msg_namelen = 0;
  iov.iov_base = (char *) GST_BUFFER_DATA (*buf);
  iov.iov_len = readsize;
  mh.msg_iov = &iov;
  mh.msg_iovlen = 1;

  bytes_read = recvmsg (socket, &mh, 0);
#else
  bytes_read =
      recvfrom (socket, (char *) GST_BUFFER_DATA (*buf), (int) readsize, 0,
      NULL, 0);
#endif

  if (bytes_read != readsize) {
    GST_DEBUG_OBJECT (this, "Error while reading data");
    return GST_FLOW_ERROR;
  }

  GST_LOG_OBJECT (this, "bytes read %" G_GSSIZE_FORMAT, bytes_read);
  GST_LOG_OBJECT (this, "returning buffer of size %d", GST_BUFFER_SIZE (*buf));

  return GST_FLOW_OK;
}

/* Create a new DCCP socket
 *
 * @param element - the element
 * @return the socket file descriptor
 */
gint
gst_dccp_create_new_socket (GstElement * element)
{
  int sock_fd;
  if ((sock_fd = socket (AF_INET, SOCK_DCCP, IPPROTO_DCCP)) < 0) {
    GST_ELEMENT_ERROR (element, RESOURCE, OPEN_READ, (NULL), GST_ERROR_SYSTEM);
  }

  return sock_fd;
}

/* Connect to a server
 * @param element - the element
 * @param server_sin - a struct with a server address and port
 * @param sock_fd - the socket to connect
 * @return TRUE in case of successful connection, FALSE otherwise
 */
gboolean
gst_dccp_connect_to_server (GstElement * element, struct sockaddr_in server_sin,
    int sock_fd)
{
#ifdef G_OS_WIN32
  int errorCode;
#endif
  GST_DEBUG_OBJECT (element, "connecting to server");

  if (connect (sock_fd, (struct sockaddr *) &server_sin, sizeof (server_sin))) {
#ifdef G_OS_WIN32
    errorCode = WSAGetLastError ();
    switch (errorCode) {
      case WSAECONNREFUSED:
        GST_ELEMENT_ERROR (element, RESOURCE, OPEN_WRITE,
            ("Connection to %s:%d refused.", inet_ntoa (server_sin.sin_addr),
                ntohs (server_sin.sin_port)), (NULL));
        return FALSE;
        break;
      default:
        GST_ELEMENT_ERROR (element, RESOURCE, OPEN_READ, (NULL),
            ("Connect to %s:%d failed: %s", inet_ntoa (server_sin.sin_addr),
                ntohs (server_sin.sin_port), g_strerror (errorCode)));
        return FALSE;
        break;
    }
#else
    switch (errno) {
      case ECONNREFUSED:
        GST_ELEMENT_ERROR (element, RESOURCE, OPEN_WRITE,
            ("Connection to %s:%d refused.", inet_ntoa (server_sin.sin_addr),
                ntohs (server_sin.sin_port)), (NULL));
        return FALSE;
        break;
      default:
        GST_ELEMENT_ERROR (element, RESOURCE, OPEN_READ, (NULL),
            ("Connect to %s:%d failed: %s", inet_ntoa (server_sin.sin_addr),
                ntohs (server_sin.sin_port), g_strerror (errno)));
        return FALSE;
        break;
    }
#endif
  }
  return TRUE;
}

/* FIXME support only one client */
/*
 * Accept connection on the server socket.
 *
 * @param element - the element
 * @param server_sock_fd - the server socket file descriptor
 * @return the socket of the client connected to the server.
 */
gint
gst_dccp_server_wait_connections (GstElement * element, int server_sock_fd)
{
  /* new client */
  int client_sock_fd;
  struct sockaddr_in client_address;
  socklen_t client_address_len;

  memset (&client_address, 0, sizeof (client_address));
  client_address_len = 0;

  if ((client_sock_fd =
          accept (server_sock_fd, (struct sockaddr *) &client_address,
              &client_address_len)) == -1) {
    GST_ELEMENT_ERROR (element, RESOURCE, OPEN_WRITE, (NULL),
        ("Could not accept client on server socket %d: %s (%d)",
            server_sock_fd, g_strerror (errno), errno));
    return -1;
  }

  GST_DEBUG_OBJECT (element, "Added new client ip %s with fd %d.",
      inet_ntoa (client_address.sin_addr), client_sock_fd);

  return client_sock_fd;
}

/*
 * Bind a server address.
 *
 * @param element - the element
 * @param server_sock_fd - the server socket fd
 * @param server_sin - the address and the port to bind the server on
 * @return true in success, false otherwise.
 */
gboolean
gst_dccp_bind_server_socket (GstElement * element, int server_sock_fd,
    struct sockaddr_in server_sin)
{
  int ret;

  GST_DEBUG_OBJECT (element, "Binding server socket to address.");

  ret = bind (server_sock_fd, (struct sockaddr *) &server_sin,
      sizeof (server_sin));
  if (ret) {
    switch (errno) {
      default:
        GST_ELEMENT_ERROR (element, RESOURCE, OPEN_READ, (NULL),
            ("Bind on port %d failed: %s", ntohs (server_sin.sin_port),
                g_strerror (errno)));
        return FALSE;
        break;
    }
  }
  return TRUE;
}

/*
 * Listen on server socket.
 *
 * @param element - the element
 * @param server_sock_fd - the server socket fd
 * @return true in success, false otherwise.
 */
gboolean
gst_dccp_listen_server_socket (GstElement * element, int server_sock_fd)
{

  GST_DEBUG_OBJECT (element, "Listening on server socket %d with queue of %d",
      server_sock_fd, DCCP_BACKLOG);

  if (listen (server_sock_fd, DCCP_BACKLOG) == -1) {
    GST_ELEMENT_ERROR (element, RESOURCE, OPEN_READ, (NULL),
        ("Could not listen on server socket: %s", g_strerror (errno)));
    return FALSE;
  }

  GST_DEBUG_OBJECT (element,
      "Listened on server socket %d, returning from connection setup",
      server_sock_fd);

  return TRUE;
}

/* Write buffer to given socket incrementally.
 *
 * @param element - the element
 * @param socket - the socket
 * @param buf - the buffer that will be written
 * @param size - the number of bytes of the buffer
 * @param packet_size - the MTU
 * @return the number of bytes written.
 */
static GstFlowReturn
gst_dccp_socket_write (GstElement * element, int socket, const void *buf,
    size_t size, int packet_size)
{
  size_t bytes_written = 0;
  ssize_t wrote = 0;

#ifndef G_OS_WIN32
  struct iovec iov;
  struct msghdr mh;

  memset (&mh, 0, sizeof (mh));

  while (bytes_written < size) {
    do {
      mh.msg_name = NULL;
      mh.msg_namelen = 0;
      iov.iov_base = (char *) buf + bytes_written;
      iov.iov_len = MIN (packet_size, size - bytes_written);
      mh.msg_iov = &iov;
      mh.msg_iovlen = 1;

      wrote = sendmsg (socket, &mh, 0);
    } while (wrote == -1 && errno == EAGAIN);
#else
  int errorCode = 0;
  while (bytes_written < size) {
    do {
      wrote = sendto (socket, (char *) buf + bytes_written,
          MIN (packet_size, size - bytes_written), 0, NULL, 0);
      errorCode = WSAGetLastError ();
    } while (wrote == SOCKET_ERROR && errorCode == EAGAIN);
#endif

    /* give up on error */
    if (wrote >= 0)
      bytes_written += wrote;
    else
      break;
  }

  if (wrote < 0)
    GST_WARNING ("Error while writing.");
  else
    GST_LOG_OBJECT (element, "Wrote %" G_GSIZE_FORMAT " bytes succesfully.",
        bytes_written);

  if (bytes_written != size) {
    GST_ELEMENT_ERROR (element, RESOURCE, WRITE,
        ("Error while sending data to socket %d.", socket),
        ("Only %" G_GSIZE_FORMAT " of %" G_GSIZE_FORMAT " bytes written: %s",
            bytes_written, size, g_strerror (errno)));
    return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;
}

/* Write buffer to given socket.
 *
 * @param this - the element
 * @param buf - the buffer that will be written
 * @param client_sock_fd - the client socket
 * @param packet_size - the MTU
 * @return GST_FLOW_OK if the send operation was successful, GST_FLOW_ERROR otherwise.
 */
GstFlowReturn
gst_dccp_send_buffer (GstElement * this, GstBuffer * buffer, int client_sock_fd,
    int packet_size)
{
//  size_t wrote;
  gint size = 0;
  guint8 *data;

  size = GST_BUFFER_SIZE (buffer);
  data = GST_BUFFER_DATA (buffer);

  GST_LOG_OBJECT (this, "writing %d bytes", size);

  if (packet_size < 0) {
    return GST_FLOW_ERROR;
  }

  return gst_dccp_socket_write (this, client_sock_fd, data, size, packet_size);
}

/*
 * Make address reusable.
 * @param element - the element
 * @param sock_fd - the socket
 * @return TRUE if the operation was successful, FALSE otherwise.
 */
gboolean
gst_dccp_make_address_reusable (GstElement * element, int sock_fd)
{
  int ret = 1;
  /* make address reusable */
  if (setsockopt (sock_fd, SOL_SOCKET, SO_REUSEADDR,
          (void *) &ret, sizeof (ret)) < 0) {
    GST_ELEMENT_ERROR (element, RESOURCE, SETTINGS, (NULL),
        ("Could not setsockopt: %s", g_strerror (errno)));
    return FALSE;
  }
  return TRUE;
}

/*
 * Set DCCP congestion control.
 * @param element - the element
 * @param sock_fd - the socket
 * @param ccid - the ccid number
 * @return TRUE if the operation was successful, FALSE otherwise.
 */
gboolean
gst_dccp_set_ccid (GstElement * element, int sock_fd, uint8_t ccid)
{
  uint8_t ccids[4];             /* for getting the available CCIDs, should be large enough */
  socklen_t len = sizeof (ccids);
  int i, ret;
  gboolean ccid_supported = FALSE;

  /*
   * Determine which CCIDs are available on the host
   */
#ifndef G_OS_WIN32
  ret = getsockopt (sock_fd, SOL_DCCP, DCCP_SOCKOPT_AVAILABLE_CCIDS, &ccids,
      &len);
#else
  ret =
      getsockopt (sock_fd, SOL_DCCP, DCCP_SOCKOPT_AVAILABLE_CCIDS,
      (char *) &ccids, &len);
#endif
  if (ret < 0) {
    GST_ERROR_OBJECT (element, "Can not determine available CCIDs");
    return FALSE;
  }

  for (i = 0; i < sizeof (ccids); i++) {
    if (ccid == ccids[i]) {
      ccid_supported = TRUE;
    }
  }

  if (!ccid_supported) {
    GST_ERROR_OBJECT (element, "CCID specified is not supported");
    return FALSE;
  }
#ifndef G_OS_WIN32
  if (setsockopt (sock_fd, SOL_DCCP, DCCP_SOCKOPT_CCID, &ccid,
#else
  if (setsockopt (sock_fd, SOL_DCCP, DCCP_SOCKOPT_CCID, (char *) &ccid,
#endif
          sizeof (ccid)) < 0) {
    GST_ERROR_OBJECT (element, "Can not set CCID");
    return FALSE;
  }

  return TRUE;
}

#if 0
/*
 * Get the current ccid of TX or RX half-connection. tx_or_rx parameter must be
 * DCCP_SOCKOPT_TX_CCID or DCCP_SOCKOPT_RX_CCID.
 * @return ccid or -1 on error or tx_or_rx not the correct option
 */
static uint8_t
gst_dccp_get_ccid (GstElement * element, int sock_fd, int tx_or_rx)
{
  uint8_t ccid;
  socklen_t ccidlen;
  int ret;

  switch (tx_or_rx) {
    case DCCP_SOCKOPT_TX_CCID:
    case DCCP_SOCKOPT_RX_CCID:
      break;
    default:
      return -1;
  }

  ccidlen = sizeof (ccid);
#ifndef G_OS_WIN32
  ret = getsockopt (sock_fd, SOL_DCCP, tx_or_rx, &ccid, &ccidlen);
#else
  ret = getsockopt (sock_fd, SOL_DCCP, tx_or_rx, (char *) &ccid, &ccidlen);
#endif
  if (ret < 0) {
    GST_ERROR_OBJECT (element, "Can not determine available CCIDs");
    return -1;
  }
  return ccid;
}
#endif

/*
 * Get the socket MTU.
 * @param element - the element
 * @param sock - the socket
 * @return the MTU if the operation was successful, -1 otherwise.
 */
gint
gst_dccp_get_max_packet_size (GstElement * element, int sock)
{
  int size;
  socklen_t sizelen = sizeof (size);
#ifndef G_OS_WIN32
  if (getsockopt (sock, SOL_DCCP, DCCP_SOCKOPT_GET_CUR_MPS,
          &size, &sizelen) < 0) {
#else
  if (getsockopt (sock, SOL_DCCP, DCCP_SOCKOPT_GET_CUR_MPS,
          (char *) &size, &sizelen) < 0) {
#endif
    GST_ELEMENT_ERROR (element, RESOURCE, SETTINGS, (NULL),
        ("Could not get current MTU %d: %s", errno, g_strerror (errno)));
    return -1;
  }
  GST_DEBUG_OBJECT (element, "MTU: %d", size);
  return size;
}

void
gst_dccp_socket_close (GstElement * element, int *socket)
{
  if (*socket >= 0) {
    GST_DEBUG_OBJECT (element, "closing socket");
    close (*socket);
    *socket = -1;
  }
}
