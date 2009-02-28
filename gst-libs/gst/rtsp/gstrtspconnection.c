/* GStreamer
 * Copyright (C) <2005,2006> Wim Taymans <wim@fluendo.com>
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
/*
 * Unless otherwise indicated, Source Code is licensed under MIT license.
 * See further explanation attached in License Statement (distributed in the file
 * LICENSE).
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * SECTION:gstrtspconnection
 * @short_description: manage RTSP connections
 * @see_also: gstrtspurl
 *  
 * <refsect2>
 * <para>
 * This object manages the RTSP connection to the server. It provides function
 * to receive and send bytes and messages.
 * </para>
 * </refsect2>
 *  
 * Last reviewed on 2007-07-24 (0.10.14)
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif


/* we include this here to get the G_OS_* defines */
#include <glib.h>
#include <gst/gst.h>

#ifdef G_OS_WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#define EINPROGRESS WSAEINPROGRESS
#else
#include <sys/ioctl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#endif

#ifdef HAVE_FIONREAD_IN_SYS_FILIO
#include <sys/filio.h>
#endif

#include "gstrtspconnection.h"
#include "gstrtspbase64.h"
#include "md5.h"

#ifdef G_OS_WIN32
#define READ_SOCKET(fd, buf, len) recv (fd, (char *)buf, len, 0)
#define WRITE_SOCKET(fd, buf, len) send (fd, (const char *)buf, len, 0)
#define SETSOCKOPT(sock, level, name, val, len) setsockopt (sock, level, name, (const char *)val, len)
#define CLOSE_SOCKET(sock) closesocket (sock)
#define ERRNO_IS_EAGAIN (WSAGetLastError () == WSAEWOULDBLOCK)
#define ERRNO_IS_EINTR (WSAGetLastError () == WSAEINTR)
/* According to Microsoft's connect() documentation this one returns
 * WSAEWOULDBLOCK and not WSAEINPROGRESS. */
#define ERRNO_IS_EINPROGRESS (WSAGetLastError () == WSAEWOULDBLOCK)
#else
#define READ_SOCKET(fd, buf, len) read (fd, buf, len)
#define WRITE_SOCKET(fd, buf, len) write (fd, buf, len)
#define SETSOCKOPT(sock, level, name, val, len) setsockopt (sock, level, name, val, len)
#define CLOSE_SOCKET(sock) close (sock)
#define ERRNO_IS_EAGAIN (errno == EAGAIN)
#define ERRNO_IS_EINTR (errno == EINTR)
#define ERRNO_IS_EINPROGRESS (errno == EINPROGRESS)
#endif

struct _GstRTSPConnection
{
  /*< private > */
  /* URL for the connection */
  GstRTSPUrl *url;

  /* connection state */
  GstPollFD fd;
  GstPoll *fdset;
  gchar *ip;

  /* Session state */
  gint cseq;                    /* sequence number */
  gchar session_id[512];        /* session id */
  gint timeout;                 /* session timeout in seconds */
  GTimer *timer;                /* timeout timer */

  /* Authentication */
  GstRTSPAuthMethod auth_method;
  gchar *username;
  gchar *passwd;
  GHashTable *auth_params;
};

#ifdef G_OS_WIN32
static int
inet_aton (const char *c, struct in_addr *paddr)
{
  /* note that inet_addr is deprecated on unix because
   * inet_addr returns -1 (INADDR_NONE) for the valid 255.255.255.255
   * address. */
  paddr->s_addr = inet_addr (c);

  if (paddr->s_addr == INADDR_NONE)
    return 0;

  return 1;
}
#endif

enum
{
  STATE_START = 0,
  STATE_DATA_HEADER,
  STATE_DATA_BODY,
  STATE_READ_LINES,
  STATE_END,
  STATE_LAST
};

/* a structure for constructing RTSPMessages */
typedef struct
{
  gint state;
  guint8 buffer[4096];
  guint offset;

  guint line;
  guint8 *body_data;
  glong body_len;
} GstRTSPBuilder;

static void
build_reset (GstRTSPBuilder * builder)
{
  g_free (builder->body_data);
  memset (builder, 0, sizeof (builder));
}

/**
 * gst_rtsp_connection_create:
 * @url: a #GstRTSPUrl 
 * @conn: storage for a #GstRTSPConnection
 *
 * Create a newly allocated #GstRTSPConnection from @url and store it in @conn.
 * The connection will not yet attempt to connect to @url, use
 * gst_rtsp_connection_connect().
 *
 * Returns: #GST_RTSP_OK when @conn contains a valid connection.
 */
GstRTSPResult
gst_rtsp_connection_create (GstRTSPUrl * url, GstRTSPConnection ** conn)
{
  GstRTSPConnection *newconn;
#ifdef G_OS_WIN32
  WSADATA w;
  int error;
#endif

  g_return_val_if_fail (conn != NULL, GST_RTSP_EINVAL);

#ifdef G_OS_WIN32
  error = WSAStartup (0x0202, &w);

  if (error)
    goto startup_error;

  if (w.wVersion != 0x0202)
    goto version_error;
#endif

  newconn = g_new0 (GstRTSPConnection, 1);

  if ((newconn->fdset = gst_poll_new (TRUE)) == NULL)
    goto no_fdset;

  newconn->url = url;
  newconn->fd.fd = -1;
  newconn->timer = g_timer_new ();
  newconn->timeout = 60;

  newconn->auth_method = GST_RTSP_AUTH_NONE;
  newconn->username = NULL;
  newconn->passwd = NULL;
  newconn->auth_params = NULL;

  *conn = newconn;

  return GST_RTSP_OK;

  /* ERRORS */
#ifdef G_OS_WIN32
startup_error:
  {
    g_warning ("Error %d on WSAStartup", error);
    return GST_RTSP_EWSASTART;
  }
version_error:
  {
    g_warning ("Windows sockets are not version 0x202 (current 0x%x)",
        w.wVersion);
    WSACleanup ();
    return GST_RTSP_EWSAVERSION;
  }
#endif
no_fdset:
  {
    g_free (newconn);
#ifdef G_OS_WIN32
    WSACleanup ();
#endif
    return GST_RTSP_ESYS;
  }
}

/**
 * gst_rtsp_connection_accept:
 * @sock: a socket
 * @conn: storage for a #GstRTSPConnection
 *
 * Accept a new connection on @sock and create a new #GstRTSPConnection for
 * handling communication on new socket.
 *
 * Returns: #GST_RTSP_OK when @conn contains a valid connection.
 *
 * Since: 0.10.23
 */
GstRTSPResult
gst_rtsp_connection_accept (gint sock, GstRTSPConnection ** conn)
{
  int fd;
  unsigned int address_len;
  GstRTSPConnection *newconn = NULL;
  struct sockaddr_in address;
  GstRTSPUrl *url;
#ifdef G_OS_WIN32
  gulong flags = 1;
#endif

  address_len = sizeof (address);
  memset (&address, 0, address_len);

#ifndef G_OS_WIN32
  fd = accept (sock, (struct sockaddr *) &address, &address_len);
#else
  fd = accept (sock, (struct sockaddr *) &address, (gint *) & address_len);
#endif /* G_OS_WIN32 */
  if (fd == -1)
    goto accept_failed;

  /* set to non-blocking mode so that we can cancel the communication */
#ifndef G_OS_WIN32
  fcntl (fd, F_SETFL, O_NONBLOCK);
#else
  ioctlsocket (fd, FIONBIO, &flags);
#endif /* G_OS_WIN32 */

  /* create a url for the client address */
  url = g_new0 (GstRTSPUrl, 1);
  url->host = g_strdup_printf ("%s", inet_ntoa (address.sin_addr));
  url->port = address.sin_port;

  /* now create the connection object */
  gst_rtsp_connection_create (url, &newconn);
  newconn->fd.fd = fd;
  gst_poll_add_fd (newconn->fdset, &newconn->fd);

  *conn = newconn;

  return GST_RTSP_OK;

  /* ERRORS */
accept_failed:
  {
    return GST_RTSP_ESYS;
  }
}

/**
 * gst_rtsp_connection_connect:
 * @conn: a #GstRTSPConnection 
 * @timeout: a #GTimeVal timeout
 *
 * Attempt to connect to the url of @conn made with
 * gst_rtsp_connection_create(). If @timeout is #NULL this function can block
 * forever. If @timeout contains a valid timeout, this function will return
 * #GST_RTSP_ETIMEOUT after the timeout expired.
 *
 * This function can be cancelled with gst_rtsp_connection_flush().
 *
 * Returns: #GST_RTSP_OK when a connection could be made.
 */
GstRTSPResult
gst_rtsp_connection_connect (GstRTSPConnection * conn, GTimeVal * timeout)
{
  gint fd;
  struct sockaddr_in sa_in;
  struct hostent *hostinfo;
  const gchar *ip;
  struct in_addr addr;
  gint ret;
  guint16 port;
  GstRTSPUrl *url;
  GstClockTime to;
  gint retval;

#ifdef G_OS_WIN32
  unsigned long flags = 1;
  struct in_addr *addrp;
#else
  char **addrs;
  gchar ipbuf[INET_ADDRSTRLEN];
#endif /* G_OS_WIN32 */

  g_return_val_if_fail (conn != NULL, GST_RTSP_EINVAL);
  g_return_val_if_fail (conn->url != NULL, GST_RTSP_EINVAL);
  g_return_val_if_fail (conn->fd.fd < 0, GST_RTSP_EINVAL);

  url = conn->url;

  /* first check if it already is an IP address */
  if (inet_aton (url->host, &addr)) {
    ip = url->host;
  } else {
    hostinfo = gethostbyname (url->host);
    if (!hostinfo)
      goto not_resolved;        /* h_errno set */

    if (hostinfo->h_addrtype != AF_INET)
      goto not_ip;              /* host not an IP host */
#ifdef G_OS_WIN32
    addrp = (struct in_addr *) hostinfo->h_addr_list[0];
    /* this is not threadsafe */
    ip = inet_ntoa (*addrp);
#else
    addrs = hostinfo->h_addr_list;
    ip = inet_ntop (AF_INET, (struct in_addr *) addrs[0], ipbuf,
        sizeof (ipbuf));
#endif /* G_OS_WIN32 */
  }

  /* get the port from the url */
  gst_rtsp_url_get_port (url, &port);

  memset (&sa_in, 0, sizeof (sa_in));
  sa_in.sin_family = AF_INET;   /* network socket */
  sa_in.sin_port = htons (port);        /* on port */
  sa_in.sin_addr.s_addr = inet_addr (ip);       /* on host ip */

  fd = socket (AF_INET, SOCK_STREAM, 0);
  if (fd == -1)
    goto sys_error;

  /* set to non-blocking mode so that we can cancel the connect */
#ifndef G_OS_WIN32
  fcntl (fd, F_SETFL, O_NONBLOCK);
#else
  ioctlsocket (fd, FIONBIO, &flags);
#endif /* G_OS_WIN32 */

  /* add the socket to our fdset */
  conn->fd.fd = fd;
  gst_poll_add_fd (conn->fdset, &conn->fd);

  /* we are going to connect ASYNC now */
  ret = connect (fd, (struct sockaddr *) &sa_in, sizeof (sa_in));
  if (ret == 0)
    goto done;
  if (!ERRNO_IS_EINPROGRESS)
    goto sys_error;

  /* wait for connect to complete up to the specified timeout or until we got
   * interrupted. */
  gst_poll_fd_ctl_write (conn->fdset, &conn->fd, TRUE);

  to = timeout ? GST_TIMEVAL_TO_TIME (*timeout) : GST_CLOCK_TIME_NONE;

  do {
    retval = gst_poll_wait (conn->fdset, to);
  } while (retval == -1 && (errno == EINTR || errno == EAGAIN));

  if (retval == 0)
    goto timeout;
  else if (retval == -1)
    goto sys_error;

  /* we can still have an error connecting on windows */
  if (gst_poll_fd_has_error (conn->fdset, &conn->fd)) {
    socklen_t len = sizeof (errno);
#ifndef G_OS_WIN32
    getsockopt (conn->fd.fd, SOL_SOCKET, SO_ERROR, &errno, &len);
#else
    getsockopt (conn->fd.fd, SOL_SOCKET, SO_ERROR, (char *) &errno, &len);
#endif
    goto sys_error;
  }

  gst_poll_fd_ignored (conn->fdset, &conn->fd);

done:
  conn->ip = g_strdup (ip);

  return GST_RTSP_OK;

sys_error:
  {
    GST_ERROR ("system error %d (%s)", errno, g_strerror (errno));
    if (conn->fd.fd >= 0) {
      GST_DEBUG ("remove fd %d", conn->fd.fd);
      gst_poll_remove_fd (conn->fdset, &conn->fd);
      conn->fd.fd = -1;
    }
    if (fd >= 0)
      CLOSE_SOCKET (fd);
    return GST_RTSP_ESYS;
  }
not_resolved:
  {
    GST_ERROR ("could not resolve %s", url->host);
    return GST_RTSP_ENET;
  }
not_ip:
  {
    GST_ERROR ("not an IP address");
    return GST_RTSP_ENOTIP;
  }
timeout:
  {
    GST_ERROR ("timeout");
    if (conn->fd.fd >= 0) {
      GST_DEBUG ("remove fd %d", conn->fd.fd);
      gst_poll_remove_fd (conn->fdset, &conn->fd);
      conn->fd.fd = -1;
    }
    if (fd >= 0)
      CLOSE_SOCKET (fd);
    return GST_RTSP_ETIMEOUT;
  }
}

static void
md5_digest_to_hex_string (unsigned char digest[16], char string[33])
{
  static const char hexdigits[] = "0123456789abcdef";
  int i;

  for (i = 0; i < 16; i++) {
    string[i * 2] = hexdigits[(digest[i] >> 4) & 0x0f];
    string[i * 2 + 1] = hexdigits[digest[i] & 0x0f];
  }
  string[32] = 0;
}

static void
auth_digest_compute_hex_urp (const gchar * username,
    const gchar * realm, const gchar * password, gchar hex_urp[33])
{
  struct MD5Context md5_context;
  unsigned char digest[16];

  MD5Init (&md5_context);
  MD5Update (&md5_context, username, strlen (username));
  MD5Update (&md5_context, ":", 1);
  MD5Update (&md5_context, realm, strlen (realm));
  MD5Update (&md5_context, ":", 1);
  MD5Update (&md5_context, password, strlen (password));
  MD5Final (digest, &md5_context);
  md5_digest_to_hex_string (digest, hex_urp);
}

static void
auth_digest_compute_response (const gchar * method,
    const gchar * uri, const gchar * hex_a1, const gchar * nonce,
    gchar response[33])
{
  char hex_a2[33];
  struct MD5Context md5_context;
  unsigned char digest[16];

  /* compute A2 */
  MD5Init (&md5_context);
  MD5Update (&md5_context, method, strlen (method));
  MD5Update (&md5_context, ":", 1);
  MD5Update (&md5_context, uri, strlen (uri));
  MD5Final (digest, &md5_context);
  md5_digest_to_hex_string (digest, hex_a2);

  /* compute KD */
  MD5Init (&md5_context);
  MD5Update (&md5_context, hex_a1, strlen (hex_a1));
  MD5Update (&md5_context, ":", 1);
  MD5Update (&md5_context, nonce, strlen (nonce));
  MD5Update (&md5_context, ":", 1);

  MD5Update (&md5_context, hex_a2, 32);
  MD5Final (digest, &md5_context);
  md5_digest_to_hex_string (digest, response);
}

static void
add_auth_header (GstRTSPConnection * conn, GstRTSPMessage * message)
{
  switch (conn->auth_method) {
    case GST_RTSP_AUTH_BASIC:{
      gchar *user_pass =
          g_strdup_printf ("%s:%s", conn->username, conn->passwd);
      gchar *user_pass64 =
          gst_rtsp_base64_encode (user_pass, strlen (user_pass));
      gchar *auth_string = g_strdup_printf ("Basic %s", user_pass64);

      gst_rtsp_message_take_header (message, GST_RTSP_HDR_AUTHORIZATION,
          auth_string);

      g_free (user_pass);
      g_free (user_pass64);
      break;
    }
    case GST_RTSP_AUTH_DIGEST:{
      gchar response[33], hex_urp[33];
      gchar *auth_string, *auth_string2;
      gchar *realm;
      gchar *nonce;
      gchar *opaque;
      const gchar *uri;
      const gchar *method;

      /* we need to have some params set */
      if (conn->auth_params == NULL)
        break;

      /* we need the realm and nonce */
      realm = (gchar *) g_hash_table_lookup (conn->auth_params, "realm");
      nonce = (gchar *) g_hash_table_lookup (conn->auth_params, "nonce");
      if (realm == NULL || nonce == NULL)
        break;

      auth_digest_compute_hex_urp (conn->username, realm, conn->passwd,
          hex_urp);

      method = gst_rtsp_method_as_text (message->type_data.request.method);
      uri = message->type_data.request.uri;

      /* Assume no qop, algorithm=md5, stale=false */
      /* For algorithm MD5, a1 = urp. */
      auth_digest_compute_response (method, uri, hex_urp, nonce, response);
      auth_string = g_strdup_printf ("Digest username=\"%s\", "
          "realm=\"%s\", nonce=\"%s\", uri=\"%s\", response=\"%s\"",
          conn->username, realm, nonce, uri, response);

      opaque = (gchar *) g_hash_table_lookup (conn->auth_params, "opaque");
      if (opaque) {
        auth_string2 = g_strdup_printf ("%s, opaque=\"%s\"", auth_string,
            opaque);
        g_free (auth_string);
        auth_string = auth_string2;
      }
      gst_rtsp_message_take_header (message, GST_RTSP_HDR_AUTHORIZATION,
          auth_string);
      break;
    }
    default:
      /* Nothing to do */
      break;
  }
}

static void
add_date_header (GstRTSPMessage * message)
{
  GTimeVal tv;
  gchar date_string[100];
  time_t t;

#ifdef HAVE_GMTIME_R
  struct tm tm_;
#endif

  g_get_current_time (&tv);
  t = (time_t) tv.tv_sec;

#ifdef HAVE_GMTIME_R
  strftime (date_string, sizeof (date_string), "%a, %d %b %Y %H:%M:%S GMT",
      gmtime_r (&t, &tm_));
#else
  strftime (date_string, sizeof (date_string), "%a, %d %b %Y %H:%M:%S GMT",
      gmtime (&t));
#endif

  gst_rtsp_message_add_header (message, GST_RTSP_HDR_DATE, date_string);
}

static GstRTSPResult
write_bytes (gint fd, const guint8 * buffer, guint * idx, guint size)
{
  guint left;

  if (*idx > size)
    return GST_RTSP_ERROR;

  left = size - *idx;

  while (left) {
    gint r;

    r = WRITE_SOCKET (fd, &buffer[*idx], left);
    if (r == 0) {
      return GST_RTSP_EINTR;
    } else if (r < 0) {
      if (ERRNO_IS_EAGAIN)
        return GST_RTSP_EINTR;
      if (!ERRNO_IS_EINTR)
        return GST_RTSP_ESYS;
    } else {
      left -= r;
      *idx += r;
    }
  }
  return GST_RTSP_OK;
}

static GstRTSPResult
read_bytes (gint fd, guint8 * buffer, guint * idx, guint size)
{
  guint left;

  if (*idx > size)
    return GST_RTSP_ERROR;

  left = size - *idx;

  while (left) {
    gint r;

    r = READ_SOCKET (fd, &buffer[*idx], left);
    if (r == 0) {
      return GST_RTSP_EEOF;
    } else if (r < 0) {
      if (ERRNO_IS_EAGAIN)
        return GST_RTSP_EINTR;
      if (!ERRNO_IS_EINTR)
        return GST_RTSP_ESYS;
    } else {
      left -= r;
      *idx += r;
    }
  }
  return GST_RTSP_OK;
}

static GstRTSPResult
read_line (gint fd, guint8 * buffer, guint * idx, guint size)
{
  while (TRUE) {
    guint8 c;
    gint r;

    r = READ_SOCKET (fd, &c, 1);
    if (r == 0) {
      return GST_RTSP_EEOF;
    } else if (r < 0) {
      if (ERRNO_IS_EAGAIN)
        return GST_RTSP_EINTR;
      if (!ERRNO_IS_EINTR)
        return GST_RTSP_ESYS;
    } else {
      if (c == '\n')            /* end on \n */
        break;
      if (c == '\r')            /* ignore \r */
        continue;

      if (*idx < size - 1)
        buffer[(*idx)++] = c;
    }
  }
  buffer[*idx] = '\0';

  return GST_RTSP_OK;
}

/**
 * gst_rtsp_connection_write:
 * @conn: a #GstRTSPConnection
 * @data: the data to write
 * @size: the size of @data
 * @timeout: a timeout value or #NULL
 *
 * Attempt to write @size bytes of @data to the connected @conn, blocking up to
 * the specified @timeout. @timeout can be #NULL, in which case this function
 * might block forever.
 * 
 * This function can be cancelled with gst_rtsp_connection_flush().
 *
 * Returns: #GST_RTSP_OK on success.
 */
GstRTSPResult
gst_rtsp_connection_write (GstRTSPConnection * conn, const guint8 * data,
    guint size, GTimeVal * timeout)
{
  guint offset;
  gint retval;
  GstClockTime to;
  GstRTSPResult res;

  g_return_val_if_fail (conn != NULL, GST_RTSP_EINVAL);
  g_return_val_if_fail (data != NULL || size == 0, GST_RTSP_EINVAL);
  g_return_val_if_fail (conn->fd.fd >= 0, GST_RTSP_EINVAL);

  gst_poll_set_controllable (conn->fdset, TRUE);
  gst_poll_fd_ctl_write (conn->fdset, &conn->fd, TRUE);
  gst_poll_fd_ctl_read (conn->fdset, &conn->fd, FALSE);
  /* clear all previous poll results */
  gst_poll_fd_ignored (conn->fdset, &conn->fd);

  to = timeout ? GST_TIMEVAL_TO_TIME (*timeout) : GST_CLOCK_TIME_NONE;

  offset = 0;

  while (TRUE) {
    /* try to write */
    res = write_bytes (conn->fd.fd, data, &offset, size);
    if (res == GST_RTSP_OK)
      break;
    if (res != GST_RTSP_EINTR)
      goto write_error;

    /* not all is written, wait until we can write more */
    do {
      retval = gst_poll_wait (conn->fdset, to);
    } while (retval == -1 && (errno == EINTR || errno == EAGAIN));

    if (retval == 0)
      goto timeout;

    if (retval == -1) {
      if (errno == EBUSY)
        goto stopped;
      else
        goto select_error;
    }
  }
  return GST_RTSP_OK;

  /* ERRORS */
timeout:
  {
    return GST_RTSP_ETIMEOUT;
  }
select_error:
  {
    return GST_RTSP_ESYS;
  }
stopped:
  {
    return GST_RTSP_EINTR;
  }
write_error:
  {
    return res;
  }
}

static GString *
message_to_string (GstRTSPConnection * conn, GstRTSPMessage * message)
{
  GString *str = NULL;

  str = g_string_new ("");

  switch (message->type) {
    case GST_RTSP_MESSAGE_REQUEST:
      /* create request string, add CSeq */
      g_string_append_printf (str, "%s %s RTSP/1.0\r\n"
          "CSeq: %d\r\n",
          gst_rtsp_method_as_text (message->type_data.request.method),
          message->type_data.request.uri, conn->cseq++);
      /* add session id if we have one */
      if (conn->session_id[0] != '\0') {
        gst_rtsp_message_add_header (message, GST_RTSP_HDR_SESSION,
            conn->session_id);
      }
      /* add any authentication headers */
      add_auth_header (conn, message);
      break;
    case GST_RTSP_MESSAGE_RESPONSE:
      /* create response string */
      g_string_append_printf (str, "RTSP/1.0 %d %s\r\n",
          message->type_data.response.code, message->type_data.response.reason);
      break;
    case GST_RTSP_MESSAGE_DATA:
    {
      guint8 data_header[4];

      /* prepare data header */
      data_header[0] = '$';
      data_header[1] = message->type_data.data.channel;
      data_header[2] = (message->body_size >> 8) & 0xff;
      data_header[3] = message->body_size & 0xff;

      /* create string with header and data */
      str = g_string_append_len (str, (gchar *) data_header, 4);
      str =
          g_string_append_len (str, (gchar *) message->body,
          message->body_size);
      break;
    }
    default:
      g_string_free (str, TRUE);
      g_return_val_if_reached (NULL);
      break;
  }

  /* append headers and body */
  if (message->type != GST_RTSP_MESSAGE_DATA) {
    /* add date header */
    add_date_header (message);

    /* append headers */
    gst_rtsp_message_append_headers (message, str);

    /* append Content-Length and body if needed */
    if (message->body != NULL && message->body_size > 0) {
      gchar *len;

      len = g_strdup_printf ("%d", message->body_size);
      g_string_append_printf (str, "%s: %s\r\n",
          gst_rtsp_header_as_text (GST_RTSP_HDR_CONTENT_LENGTH), len);
      g_free (len);
      /* header ends here */
      g_string_append (str, "\r\n");
      str =
          g_string_append_len (str, (gchar *) message->body,
          message->body_size);
    } else {
      /* just end headers */
      g_string_append (str, "\r\n");
    }
  }

  return str;
}

/**
 * gst_rtsp_connection_send:
 * @conn: a #GstRTSPConnection
 * @message: the message to send
 * @timeout: a timeout value or #NULL
 *
 * Attempt to send @message to the connected @conn, blocking up to
 * the specified @timeout. @timeout can be #NULL, in which case this function
 * might block forever.
 * 
 * This function can be cancelled with gst_rtsp_connection_flush().
 *
 * Returns: #GST_RTSP_OK on success.
 */
GstRTSPResult
gst_rtsp_connection_send (GstRTSPConnection * conn, GstRTSPMessage * message,
    GTimeVal * timeout)
{
  GString *str = NULL;
  GstRTSPResult res;

  g_return_val_if_fail (conn != NULL, GST_RTSP_EINVAL);
  g_return_val_if_fail (message != NULL, GST_RTSP_EINVAL);

  if (!(str = message_to_string (conn, message)))
    goto no_message;

  /* write request */
  res =
      gst_rtsp_connection_write (conn, (guint8 *) str->str, str->len, timeout);

  g_string_free (str, TRUE);

  return res;

no_message:
  {
    g_warning ("Wrong message");
    return GST_RTSP_EINVAL;
  }
}

static void
parse_string (gchar * dest, gint size, gchar ** src)
{
  gint idx;

  idx = 0;
  /* skip spaces */
  while (g_ascii_isspace (**src))
    (*src)++;

  while (!g_ascii_isspace (**src) && **src != '\0') {
    if (idx < size - 1)
      dest[idx++] = **src;
    (*src)++;
  }
  if (size > 0)
    dest[idx] = '\0';
}

static void
parse_key (gchar * dest, gint size, gchar ** src)
{
  gint idx;

  idx = 0;
  while (**src != ':' && **src != '\0') {
    if (idx < size - 1)
      dest[idx++] = **src;
    (*src)++;
  }
  if (size > 0)
    dest[idx] = '\0';
}

static GstRTSPResult
parse_response_status (guint8 * buffer, GstRTSPMessage * msg)
{
  GstRTSPResult res;
  gchar versionstr[20];
  gchar codestr[4];
  gint code;
  gchar *bptr;

  bptr = (gchar *) buffer;

  parse_string (versionstr, sizeof (versionstr), &bptr);
  parse_string (codestr, sizeof (codestr), &bptr);
  code = atoi (codestr);

  while (g_ascii_isspace (*bptr))
    bptr++;

  if (strcmp (versionstr, "RTSP/1.0") == 0)
    GST_RTSP_CHECK (gst_rtsp_message_init_response (msg, code, bptr, NULL),
        parse_error);
  else if (strncmp (versionstr, "RTSP/", 5) == 0) {
    GST_RTSP_CHECK (gst_rtsp_message_init_response (msg, code, bptr, NULL),
        parse_error);
    msg->type_data.response.version = GST_RTSP_VERSION_INVALID;
  } else
    goto parse_error;

  return GST_RTSP_OK;

parse_error:
  {
    return GST_RTSP_EPARSE;
  }
}

static GstRTSPResult
parse_request_line (guint8 * buffer, GstRTSPMessage * msg)
{
  GstRTSPResult res = GST_RTSP_OK;
  gchar versionstr[20];
  gchar methodstr[20];
  gchar urlstr[4096];
  gchar *bptr;
  GstRTSPMethod method;

  bptr = (gchar *) buffer;

  parse_string (methodstr, sizeof (methodstr), &bptr);
  method = gst_rtsp_find_method (methodstr);

  parse_string (urlstr, sizeof (urlstr), &bptr);
  if (*urlstr == '\0')
    res = GST_RTSP_EPARSE;

  parse_string (versionstr, sizeof (versionstr), &bptr);

  if (*bptr != '\0')
    res = GST_RTSP_EPARSE;

  if (strcmp (versionstr, "RTSP/1.0") == 0) {
    if (gst_rtsp_message_init_request (msg, method, urlstr) != GST_RTSP_OK)
      res = GST_RTSP_EPARSE;
  } else if (strncmp (versionstr, "RTSP/", 5) == 0) {
    if (gst_rtsp_message_init_request (msg, method, urlstr) != GST_RTSP_OK)
      res = GST_RTSP_EPARSE;
    msg->type_data.request.version = GST_RTSP_VERSION_INVALID;
  } else {
    gst_rtsp_message_init_request (msg, method, urlstr);
    msg->type_data.request.version = GST_RTSP_VERSION_INVALID;
    res = GST_RTSP_EPARSE;
  }

  return res;
}

/* parsing lines means reading a Key: Value pair */
static GstRTSPResult
parse_line (guint8 * buffer, GstRTSPMessage * msg)
{
  gchar key[32];
  gchar *bptr;
  GstRTSPHeaderField field;

  bptr = (gchar *) buffer;

  /* read key */
  parse_key (key, sizeof (key), &bptr);
  if (*bptr != ':')
    goto no_column;

  bptr++;

  field = gst_rtsp_find_header_field (key);
  if (field != GST_RTSP_HDR_INVALID) {
    while (g_ascii_isspace (*bptr))
      bptr++;
    gst_rtsp_message_add_header (msg, field, bptr);
  }

  return GST_RTSP_OK;

no_column:
  {
    return GST_RTSP_EPARSE;
  }
}

/* returns:
 *  GST_RTSP_OK when a complete message was read.
 *  GST_RTSP_EEOF: when the socket is closed
 *  GST_RTSP_EINTR: when more data is needed.
 *  GST_RTSP_..: some other error occured.
 */
static GstRTSPResult
build_next (GstRTSPBuilder * builder, GstRTSPMessage * message,
    GstRTSPConnection * conn)
{
  GstRTSPResult res;

  while (TRUE) {
    switch (builder->state) {
      case STATE_START:
        builder->offset = 0;
        res =
            read_bytes (conn->fd.fd, (guint8 *) builder->buffer,
            &builder->offset, 1);
        if (res != GST_RTSP_OK)
          goto done;

        /* we have 1 bytes now and we can see if this is a data message or
         * not */
        if (builder->buffer[0] == '$') {
          /* data message, prepare for the header */
          builder->state = STATE_DATA_HEADER;
        } else {
          builder->line = 0;
          builder->state = STATE_READ_LINES;
        }
        break;
      case STATE_DATA_HEADER:
      {
        res =
            read_bytes (conn->fd.fd, (guint8 *) builder->buffer,
            &builder->offset, 4);
        if (res != GST_RTSP_OK)
          goto done;

        gst_rtsp_message_init_data (message, builder->buffer[1]);

        builder->body_len = (builder->buffer[2] << 8) | builder->buffer[3];
        builder->body_data = g_malloc (builder->body_len + 1);
        builder->body_data[builder->body_len] = '\0';
        builder->offset = 0;
        builder->state = STATE_DATA_BODY;
        break;
      }
      case STATE_DATA_BODY:
      {
        res = read_bytes (conn->fd.fd, builder->body_data, &builder->offset,
            builder->body_len);
        if (res != GST_RTSP_OK)
          goto done;

        /* we have the complete body now, store in the message adjusting the
         * length to include the traling '\0' */
        gst_rtsp_message_take_body (message,
            (guint8 *) builder->body_data, builder->body_len + 1);
        builder->body_data = NULL;
        builder->body_len = 0;

        builder->state = STATE_END;
        break;
      }
      case STATE_READ_LINES:
      {
        res = read_line (conn->fd.fd, builder->buffer, &builder->offset,
            sizeof (builder->buffer));
        if (res != GST_RTSP_OK)
          goto done;

        /* we have a regular response */
        if (builder->buffer[0] == '\r') {
          builder->buffer[0] = '\0';
        }

        if (builder->buffer[0] == '\0') {
          gchar *hdrval;

          /* empty line, end of message header */
          /* see if there is a Content-Length header */
          if (gst_rtsp_message_get_header (message,
                  GST_RTSP_HDR_CONTENT_LENGTH, &hdrval, 0) == GST_RTSP_OK) {
            /* there is, prepare to read the body */
            builder->body_len = atol (hdrval);
            builder->body_data = g_malloc (builder->body_len + 1);
            builder->body_data[builder->body_len] = '\0';
            builder->offset = 0;
            builder->state = STATE_DATA_BODY;
          } else {
            builder->state = STATE_END;
          }
          break;
        }

        /* we have a line */
        if (builder->line == 0) {
          /* first line, check for response status */
          if (memcmp (builder->buffer, "RTSP", 4) == 0) {
            res = parse_response_status (builder->buffer, message);
          } else {
            res = parse_request_line (builder->buffer, message);
          }
        } else {
          /* else just parse the line */
          parse_line (builder->buffer, message);
        }
        builder->line++;
        builder->offset = 0;
        break;
      }
      case STATE_END:
      {
        gchar *session_id;

        if (message->type == GST_RTSP_MESSAGE_DATA) {
          /* data messages don't have headers */
          res = GST_RTSP_OK;
          goto done;
        }

        /* save session id in the connection for further use */
        if (gst_rtsp_message_get_header (message, GST_RTSP_HDR_SESSION,
                &session_id, 0) == GST_RTSP_OK) {
          gint maxlen, i;

          maxlen = sizeof (conn->session_id) - 1;
          /* the sessionid can have attributes marked with ;
           * Make sure we strip them */
          for (i = 0; session_id[i] != '\0'; i++) {
            if (session_id[i] == ';') {
              maxlen = i;
              /* parse timeout */
              do {
                i++;
              } while (g_ascii_isspace (session_id[i]));
              if (g_str_has_prefix (&session_id[i], "timeout=")) {
                gint to;

                /* if we parsed something valid, configure */
                if ((to = atoi (&session_id[i + 9])) > 0)
                  conn->timeout = to;
              }
              break;
            }
          }

          /* make sure to not overflow */
          strncpy (conn->session_id, session_id, maxlen);
          conn->session_id[maxlen] = '\0';
        }
        res = GST_RTSP_OK;
        goto done;
      }
      default:
        res = GST_RTSP_ERROR;
        break;
    }
  }
done:
  return res;
}

/**
 * gst_rtsp_connection_read:
 * @conn: a #GstRTSPConnection
 * @data: the data to read
 * @size: the size of @data
 * @timeout: a timeout value or #NULL
 *
 * Attempt to read @size bytes into @data from the connected @conn, blocking up to
 * the specified @timeout. @timeout can be #NULL, in which case this function
 * might block forever.
 *
 * This function can be cancelled with gst_rtsp_connection_flush().
 *
 * Returns: #GST_RTSP_OK on success.
 */
GstRTSPResult
gst_rtsp_connection_read (GstRTSPConnection * conn, guint8 * data, guint size,
    GTimeVal * timeout)
{
  guint offset;
  gint retval;
  GstClockTime to;
  GstRTSPResult res;

  g_return_val_if_fail (conn != NULL, GST_RTSP_EINVAL);
  g_return_val_if_fail (data != NULL, GST_RTSP_EINVAL);
  g_return_val_if_fail (conn->fd.fd >= 0, GST_RTSP_EINVAL);

  if (size == 0)
    return GST_RTSP_OK;

  offset = 0;

  /* configure timeout if any */
  to = timeout ? GST_TIMEVAL_TO_TIME (*timeout) : GST_CLOCK_TIME_NONE;

  gst_poll_set_controllable (conn->fdset, TRUE);
  gst_poll_fd_ctl_write (conn->fdset, &conn->fd, FALSE);
  gst_poll_fd_ctl_read (conn->fdset, &conn->fd, TRUE);

  while (TRUE) {
    res = read_bytes (conn->fd.fd, data, &offset, size);
    if (res == GST_RTSP_EEOF)
      goto eof;
    if (res == GST_RTSP_OK)
      break;
    if (res != GST_RTSP_EINTR)
      goto read_error;

    do {
      retval = gst_poll_wait (conn->fdset, to);
    } while (retval == -1 && (errno == EINTR || errno == EAGAIN));

    /* check for timeout */
    if (retval == 0)
      goto select_timeout;

    if (retval == -1) {
      if (errno == EBUSY)
        goto stopped;
      else
        goto select_error;
    }
    gst_poll_set_controllable (conn->fdset, FALSE);
  }
  return GST_RTSP_OK;

  /* ERRORS */
select_error:
  {
    return GST_RTSP_ESYS;
  }
select_timeout:
  {
    return GST_RTSP_ETIMEOUT;
  }
stopped:
  {
    return GST_RTSP_EINTR;
  }
eof:
  {
    return GST_RTSP_EEOF;
  }
read_error:
  {
    return res;
  }
}


/**
 * gst_rtsp_connection_receive:
 * @conn: a #GstRTSPConnection
 * @message: the message to read
 * @timeout: a timeout value or #NULL
 *
 * Attempt to read into @message from the connected @conn, blocking up to
 * the specified @timeout. @timeout can be #NULL, in which case this function
 * might block forever.
 * 
 * This function can be cancelled with gst_rtsp_connection_flush().
 *
 * Returns: #GST_RTSP_OK on success.
 */
GstRTSPResult
gst_rtsp_connection_receive (GstRTSPConnection * conn, GstRTSPMessage * message,
    GTimeVal * timeout)
{
  GstRTSPResult res;
  GstRTSPBuilder builder = { 0 };
  gint retval;
  GstClockTime to;

  g_return_val_if_fail (conn != NULL, GST_RTSP_EINVAL);
  g_return_val_if_fail (message != NULL, GST_RTSP_EINVAL);

  /* configure timeout if any */
  to = timeout ? GST_TIMEVAL_TO_TIME (*timeout) : GST_CLOCK_TIME_NONE;

  gst_poll_set_controllable (conn->fdset, TRUE);
  gst_poll_fd_ctl_write (conn->fdset, &conn->fd, FALSE);
  gst_poll_fd_ctl_read (conn->fdset, &conn->fd, TRUE);

  while (TRUE) {
    res = build_next (&builder, message, conn);
    if (res == GST_RTSP_EEOF)
      goto eof;
    if (res == GST_RTSP_OK)
      break;
    if (res != GST_RTSP_EINTR)
      goto read_error;

    do {
      retval = gst_poll_wait (conn->fdset, to);
    } while (retval == -1 && (errno == EINTR || errno == EAGAIN));

    /* check for timeout */
    if (retval == 0)
      goto select_timeout;

    if (retval == -1) {
      if (errno == EBUSY)
        goto stopped;
      else
        goto select_error;
    }
    gst_poll_set_controllable (conn->fdset, FALSE);
  }

  /* we have a message here */
  build_reset (&builder);

  return GST_RTSP_OK;

  /* ERRORS */
select_error:
  {
    res = GST_RTSP_ESYS;
    goto cleanup;
  }
select_timeout:
  {
    res = GST_RTSP_ETIMEOUT;
    goto cleanup;
  }
stopped:
  {
    res = GST_RTSP_EINTR;
    goto cleanup;
  }
eof:
  {
    res = GST_RTSP_EEOF;
    goto cleanup;
  }
read_error:
cleanup:
  {
    build_reset (&builder);
    gst_rtsp_message_unset (message);
    return res;
  }
}

/**
 * gst_rtsp_connection_close:
 * @conn: a #GstRTSPConnection
 *
 * Close the connected @conn.
 * 
 * Returns: #GST_RTSP_OK on success.
 */
GstRTSPResult
gst_rtsp_connection_close (GstRTSPConnection * conn)
{
  gint res;

  g_return_val_if_fail (conn != NULL, GST_RTSP_EINVAL);

  g_free (conn->ip);
  conn->ip = NULL;

  if (conn->fd.fd != -1) {
    gst_poll_remove_fd (conn->fdset, &conn->fd);
    res = CLOSE_SOCKET (conn->fd.fd);
    conn->fd.fd = -1;
    if (res != 0)
      goto sys_error;
  }

  return GST_RTSP_OK;

sys_error:
  {
    return GST_RTSP_ESYS;
  }
}

/**
 * gst_rtsp_connection_free:
 * @conn: a #GstRTSPConnection
 *
 * Close and free @conn.
 * 
 * Returns: #GST_RTSP_OK on success.
 */
GstRTSPResult
gst_rtsp_connection_free (GstRTSPConnection * conn)
{
  GstRTSPResult res;

  g_return_val_if_fail (conn != NULL, GST_RTSP_EINVAL);

  res = gst_rtsp_connection_close (conn);
  gst_poll_free (conn->fdset);
  g_timer_destroy (conn->timer);
  g_free (conn->username);
  g_free (conn->passwd);
  gst_rtsp_connection_clear_auth_params (conn);
  g_free (conn);
#ifdef G_OS_WIN32
  WSACleanup ();
#endif

  return res;
}

/**
 * gst_rtsp_connection_poll:
 * @conn: a #GstRTSPConnection
 * @events: a bitmask of #GstRTSPEvent flags to check
 * @revents: location for result flags 
 * @timeout: a timeout
 *
 * Wait up to the specified @timeout for the connection to become available for
 * at least one of the operations specified in @events. When the function returns
 * with #GST_RTSP_OK, @revents will contain a bitmask of available operations on
 * @conn.
 *
 * @timeout can be #NULL, in which case this function might block forever.
 *
 * This function can be cancelled with gst_rtsp_connection_flush().
 * 
 * Returns: #GST_RTSP_OK on success.
 *
 * Since: 0.10.15
 */
GstRTSPResult
gst_rtsp_connection_poll (GstRTSPConnection * conn, GstRTSPEvent events,
    GstRTSPEvent * revents, GTimeVal * timeout)
{
  GstClockTime to;
  gint retval;

  g_return_val_if_fail (conn != NULL, GST_RTSP_EINVAL);
  g_return_val_if_fail (events != 0, GST_RTSP_EINVAL);
  g_return_val_if_fail (revents != NULL, GST_RTSP_EINVAL);
  g_return_val_if_fail (conn->fd.fd >= 0, GST_RTSP_EINVAL);

  gst_poll_set_controllable (conn->fdset, TRUE);

  /* add fd to writer set when asked to */
  gst_poll_fd_ctl_write (conn->fdset, &conn->fd, events & GST_RTSP_EV_WRITE);

  /* add fd to reader set when asked to */
  gst_poll_fd_ctl_read (conn->fdset, &conn->fd, events & GST_RTSP_EV_READ);

  /* configure timeout if any */
  to = timeout ? GST_TIMEVAL_TO_TIME (*timeout) : GST_CLOCK_TIME_NONE;

  do {
    retval = gst_poll_wait (conn->fdset, to);
  } while (retval == -1 && (errno == EINTR || errno == EAGAIN));

  if (retval == 0)
    goto select_timeout;

  if (retval == -1) {
    if (errno == EBUSY)
      goto stopped;
    else
      goto select_error;
  }

  *revents = 0;
  if (events & GST_RTSP_EV_READ) {
    if (gst_poll_fd_can_read (conn->fdset, &conn->fd))
      *revents |= GST_RTSP_EV_READ;
  }
  if (events & GST_RTSP_EV_WRITE) {
    if (gst_poll_fd_can_write (conn->fdset, &conn->fd))
      *revents |= GST_RTSP_EV_WRITE;
  }
  return GST_RTSP_OK;

  /* ERRORS */
select_timeout:
  {
    return GST_RTSP_ETIMEOUT;
  }
select_error:
  {
    return GST_RTSP_ESYS;
  }
stopped:
  {
    return GST_RTSP_EINTR;
  }
}

/**
 * gst_rtsp_connection_next_timeout:
 * @conn: a #GstRTSPConnection
 * @timeout: a timeout
 *
 * Calculate the next timeout for @conn, storing the result in @timeout.
 * 
 * Returns: #GST_RTSP_OK.
 */
GstRTSPResult
gst_rtsp_connection_next_timeout (GstRTSPConnection * conn, GTimeVal * timeout)
{
  gdouble elapsed;
  glong sec;
  gulong usec;

  g_return_val_if_fail (conn != NULL, GST_RTSP_EINVAL);
  g_return_val_if_fail (timeout != NULL, GST_RTSP_EINVAL);

  elapsed = g_timer_elapsed (conn->timer, &usec);
  if (elapsed >= conn->timeout) {
    sec = 0;
    usec = 0;
  } else {
    sec = conn->timeout - elapsed;
  }

  timeout->tv_sec = sec;
  timeout->tv_usec = usec;

  return GST_RTSP_OK;
}

/**
 * gst_rtsp_connection_reset_timeout:
 * @conn: a #GstRTSPConnection
 *
 * Reset the timeout of @conn.
 * 
 * Returns: #GST_RTSP_OK.
 */
GstRTSPResult
gst_rtsp_connection_reset_timeout (GstRTSPConnection * conn)
{
  g_return_val_if_fail (conn != NULL, GST_RTSP_EINVAL);

  g_timer_start (conn->timer);

  return GST_RTSP_OK;
}

/**
 * gst_rtsp_connection_flush:
 * @conn: a #GstRTSPConnection
 * @flush: start or stop the flush
 *
 * Start or stop the flushing action on @conn. When flushing, all current
 * and future actions on @conn will return #GST_RTSP_EINTR until the connection
 * is set to non-flushing mode again.
 * 
 * Returns: #GST_RTSP_OK.
 */
GstRTSPResult
gst_rtsp_connection_flush (GstRTSPConnection * conn, gboolean flush)
{
  g_return_val_if_fail (conn != NULL, GST_RTSP_EINVAL);

  gst_poll_set_flushing (conn->fdset, flush);

  return GST_RTSP_OK;
}

/**
 * gst_rtsp_connection_set_auth:
 * @conn: a #GstRTSPConnection
 * @method: authentication method
 * @user: the user
 * @pass: the password
 *
 * Configure @conn for authentication mode @method with @user and @pass as the
 * user and password respectively.
 * 
 * Returns: #GST_RTSP_OK.
 */
GstRTSPResult
gst_rtsp_connection_set_auth (GstRTSPConnection * conn,
    GstRTSPAuthMethod method, const gchar * user, const gchar * pass)
{
  g_return_val_if_fail (conn != NULL, GST_RTSP_EINVAL);

  if (method == GST_RTSP_AUTH_DIGEST && ((user == NULL || pass == NULL)
          || g_strrstr (user, ":") != NULL))
    return GST_RTSP_EINVAL;

  /* Make sure the username and passwd are being set for authentication */
  if (method == GST_RTSP_AUTH_NONE && (user == NULL || pass == NULL))
    return GST_RTSP_EINVAL;

  /* ":" chars are not allowed in usernames for basic auth */
  if (method == GST_RTSP_AUTH_BASIC && g_strrstr (user, ":") != NULL)
    return GST_RTSP_EINVAL;

  g_free (conn->username);
  g_free (conn->passwd);

  conn->auth_method = method;
  conn->username = g_strdup (user);
  conn->passwd = g_strdup (pass);

  return GST_RTSP_OK;
}

/**
 * str_case_hash:
 * @key: ASCII string to hash
 *
 * Hashes @key in a case-insensitive manner.
 *
 * Returns: the hash code.
 **/
static guint
str_case_hash (gconstpointer key)
{
  const char *p = key;
  guint h = g_ascii_toupper (*p);

  if (h)
    for (p += 1; *p != '\0'; p++)
      h = (h << 5) - h + g_ascii_toupper (*p);

  return h;
}

/**
 * str_case_equal:
 * @v1: an ASCII string
 * @v2: another ASCII string
 *
 * Compares @v1 and @v2 in a case-insensitive manner
 *
 * Returns: %TRUE if they are equal (modulo case)
 **/
static gboolean
str_case_equal (gconstpointer v1, gconstpointer v2)
{
  const char *string1 = v1;
  const char *string2 = v2;

  return g_ascii_strcasecmp (string1, string2) == 0;
}

/**
 * gst_rtsp_connection_set_auth_param:
 * @conn: a #GstRTSPConnection
 * @param: authentication directive
 * @value: value
 *
 * Setup @conn with authentication directives. This is not necesary for
 * methods #GST_RTSP_AUTH_NONE and #GST_RTSP_AUTH_BASIC. For
 * #GST_RTSP_AUTH_DIGEST, directives should be taken from the digest challenge
 * in the WWW-Authenticate response header and can include realm, domain,
 * nonce, opaque, stale, algorithm, qop as per RFC2617.
 * 
 * Since: 0.10.20
 */
void
gst_rtsp_connection_set_auth_param (GstRTSPConnection * conn,
    const gchar * param, const gchar * value)
{
  g_return_if_fail (conn != NULL);
  g_return_if_fail (param != NULL);

  if (conn->auth_params == NULL) {
    conn->auth_params =
        g_hash_table_new_full (str_case_hash, str_case_equal, g_free, g_free);
  }
  g_hash_table_insert (conn->auth_params, g_strdup (param), g_strdup (value));
}

/**
 * gst_rtsp_connection_clear_auth_params:
 * @conn: a #GstRTSPConnection
 *
 * Clear the list of authentication directives stored in @conn.
 *
 * Since: 0.10.20
 */
void
gst_rtsp_connection_clear_auth_params (GstRTSPConnection * conn)
{
  g_return_if_fail (conn != NULL);

  if (conn->auth_params != NULL) {
    g_hash_table_destroy (conn->auth_params);
    conn->auth_params = NULL;
  }
}

/**
 * gst_rtsp_connection_set_qos_dscp:
 * @conn: a #GstRTSPConnection
 * @qos_dscp: DSCP value
 *
 * Configure @conn to use the specified DSCP value.
 *
 * Returns: #GST_RTSP_OK on success.
 *
 * Since: 0.10.20
 */
GstRTSPResult
gst_rtsp_connection_set_qos_dscp (GstRTSPConnection * conn, guint qos_dscp)
{
  union gst_sockaddr
  {
    struct sockaddr sa;
    struct sockaddr_in6 sa_in6;
    struct sockaddr_storage sa_stor;
  } sa;
  socklen_t slen = sizeof (sa);
  gint af;
  gint tos;

  g_return_val_if_fail (conn != NULL, GST_RTSP_EINVAL);
  g_return_val_if_fail (conn->fd.fd >= 0, GST_RTSP_EINVAL);

  if (getsockname (conn->fd.fd, &sa.sa, &slen) < 0)
    goto no_getsockname;

  af = sa.sa.sa_family;

  /* if this is an IPv4-mapped address then do IPv4 QoS */
  if (af == AF_INET6) {

    if (IN6_IS_ADDR_V4MAPPED (&sa.sa_in6.sin6_addr))
      af = AF_INET;
  }

  /* extract and shift 6 bits of the DSCP */
  tos = (qos_dscp & 0x3f) << 2;

  switch (af) {
    case AF_INET:
      if (SETSOCKOPT (conn->fd.fd, IPPROTO_IP, IP_TOS, &tos, sizeof (tos)) < 0)
        goto no_setsockopt;
      break;
    case AF_INET6:
#ifdef IPV6_TCLASS
      if (SETSOCKOPT (conn->fd.fd, IPPROTO_IPV6, IPV6_TCLASS, &tos,
              sizeof (tos)) < 0)
        goto no_setsockopt;
      break;
#endif
    default:
      goto wrong_family;
  }

  return GST_RTSP_OK;

  /* ERRORS */
no_getsockname:
no_setsockopt:
  {
    return GST_RTSP_ESYS;
  }

wrong_family:
  {
    return GST_RTSP_ERROR;
  }
}

/**
 * gst_rtsp_connection_get_ip:
 * @conn: a #GstRTSPConnection
 *
 * Retrieve the IP address of the other end of @conn.
 *
 * Returns: The IP address as a string. this value remains valid until the
 * connection is closed.
 *
 * Since: 0.10.20
 */
const gchar *
gst_rtsp_connection_get_ip (const GstRTSPConnection * conn)
{
  g_return_val_if_fail (conn != NULL, NULL);

  return conn->ip;
}

#define READ_COND   (G_IO_IN | G_IO_HUP | G_IO_ERR)
#define WRITE_COND  (G_IO_OUT | G_IO_ERR)

typedef struct
{
  GString *str;
  guint cseq;
} GstRTSPRec;

/* async functions */
struct _GstRTSPWatch
{
  GSource source;

  GstRTSPConnection *conn;

  GstRTSPBuilder builder;
  GstRTSPMessage message;

  GPollFD readfd;
  GPollFD writefd;
  gboolean write_added;

  /* queued message for transmission */
  GList *messages;
  guint8 *write_data;
  guint write_off;
  guint write_len;
  guint write_cseq;

  GstRTSPWatchFuncs funcs;

  gpointer user_data;
  GDestroyNotify notify;
};

static gboolean
gst_rtsp_source_prepare (GSource * source, gint * timeout)
{
  GstRTSPWatch *watch = (GstRTSPWatch *) source;

  *timeout = (watch->conn->timeout * 1000);

  return FALSE;
}

static gboolean
gst_rtsp_source_check (GSource * source)
{
  GstRTSPWatch *watch = (GstRTSPWatch *) source;

  if (watch->readfd.revents & READ_COND)
    return TRUE;

  if (watch->writefd.revents & WRITE_COND)
    return TRUE;

  return FALSE;
}

static gboolean
gst_rtsp_source_dispatch (GSource * source, GSourceFunc callback,
    gpointer user_data)
{
  GstRTSPWatch *watch = (GstRTSPWatch *) source;
  GstRTSPResult res;

  /* first read as much as we can */
  if (watch->readfd.revents & READ_COND) {
    do {
      res = build_next (&watch->builder, &watch->message, watch->conn);
      if (res == GST_RTSP_EINTR)
        break;
      if (res == GST_RTSP_EEOF)
        goto eof;
      if (res != GST_RTSP_OK)
        goto error;

      if (watch->funcs.message_received)
        watch->funcs.message_received (watch, &watch->message,
            watch->user_data);

      gst_rtsp_message_unset (&watch->message);
      build_reset (&watch->builder);
    } while (FALSE);
  }

  if (watch->writefd.revents & WRITE_COND) {
    do {
      if (watch->write_data == NULL) {
        GstRTSPRec *data;

        if (!watch->messages)
          goto done;

        /* no data, get a new message from the queue */
        data = watch->messages->data;
        watch->messages = g_list_delete_link (watch->messages, watch->messages);

        watch->write_off = 0;
        watch->write_len = data->str->len;
        watch->write_data = (guint8 *) g_string_free (data->str, FALSE);
        watch->write_cseq = data->cseq;

        g_slice_free (GstRTSPRec, data);
      }

      res = write_bytes (watch->writefd.fd, watch->write_data,
          &watch->write_off, watch->write_len);
      if (res == GST_RTSP_EINTR)
        break;
      if (res != GST_RTSP_OK)
        goto error;

      if (watch->funcs.message_sent)
        watch->funcs.message_sent (watch, watch->write_cseq, watch->user_data);

    done:
      if (watch->messages == NULL && watch->write_added) {
        g_source_remove_poll ((GSource *) watch, &watch->writefd);
        watch->write_added = FALSE;
        watch->writefd.revents = 0;
      }
      g_free (watch->write_data);
      watch->write_data = NULL;
    } while (FALSE);
  }

  return TRUE;

  /* ERRORS */
eof:
  {
    if (watch->funcs.closed)
      watch->funcs.closed (watch, watch->user_data);
    return FALSE;
  }
error:
  {
    if (watch->funcs.error)
      watch->funcs.error (watch, res, watch->user_data);
    return FALSE;
  }
}

static void
gst_rtsp_source_finalize (GSource * source)
{
  GstRTSPWatch *watch = (GstRTSPWatch *) source;
  GList *walk;

  build_reset (&watch->builder);

  for (walk = watch->messages; walk; walk = g_list_next (walk)) {
    GstRTSPRec *data = walk->data;

    g_string_free (data->str, TRUE);
    g_slice_free (GstRTSPRec, data);
  }
  g_list_free (watch->messages);
  g_free (watch->write_data);

  if (watch->notify)
    watch->notify (watch->user_data);
}

static GSourceFuncs gst_rtsp_source_funcs = {
  gst_rtsp_source_prepare,
  gst_rtsp_source_check,
  gst_rtsp_source_dispatch,
  gst_rtsp_source_finalize
};

/**
 * gst_rtsp_watch_new:
 * @conn: a #GstRTSPConnection
 * @funcs: watch functions
 * @user_data: user data to pass to @funcs
 *
 * Create a watch object for @conn. The functions provided in @funcs will be
 * called with @user_data when activity happened on the watch.
 *
 * The new watch is usually created so that it can be attached to a
 * maincontext with gst_rtsp_watch_attach(). 
 *
 * @conn must exist for the entire lifetime of the watch.
 *
 * Returns: a #GstRTSPWatch that can be used for asynchronous RTSP
 * communication. Free with gst_rtsp_watch_unref () after usage.
 *
 * Since: 0.10.23
 */
GstRTSPWatch *
gst_rtsp_watch_new (GstRTSPConnection * conn,
    GstRTSPWatchFuncs * funcs, gpointer user_data, GDestroyNotify notify)
{
  GstRTSPWatch *result;

  g_return_val_if_fail (conn != NULL, NULL);
  g_return_val_if_fail (funcs != NULL, NULL);

  result = (GstRTSPWatch *) g_source_new (&gst_rtsp_source_funcs,
      sizeof (GstRTSPWatch));

  result->conn = conn;
  result->builder.state = STATE_START;

  result->readfd.fd = conn->fd.fd;
  result->readfd.events = READ_COND;
  result->readfd.revents = 0;

  result->writefd.fd = conn->fd.fd;
  result->writefd.events = WRITE_COND;
  result->writefd.revents = 0;
  result->write_added = FALSE;

  result->funcs = *funcs;
  result->user_data = user_data;
  result->notify = notify;

  /* only add the read fd, the write fd is only added when we have data
   * to send. */
  g_source_add_poll ((GSource *) result, &result->readfd);

  return result;
}

/**
 * gst_rtsp_watch_attach:
 * @watch: a #GstRTSPWatch
 * @context: a GMainContext (if NULL, the default context will be used)
 *
 * Adds a #GstRTSPWatch to a context so that it will be executed within that context.
 *
 * Returns: the ID (greater than 0) for the watch within the GMainContext. 
 *
 * Since: 0.10.23
 */
guint
gst_rtsp_watch_attach (GstRTSPWatch * watch, GMainContext * context)
{
  g_return_val_if_fail (watch != NULL, 0);

  return g_source_attach ((GSource *) watch, context);
}

/**
 * gst_rtsp_watch_free:
 * @watch: a #GstRTSPWatch
 *
 * Decreases the reference count of @watch by one. If the resulting reference
 * count is zero the watch and associated memory will be destroyed.
 *
 * Since: 0.10.23
 */
void
gst_rtsp_watch_unref (GstRTSPWatch * watch)
{
  g_return_if_fail (watch != NULL);

  g_source_unref ((GSource *) watch);
}

/**
 * gst_rtsp_watch_queue_message:
 * @watch: a #GstRTSPWatch
 * @message: a #GstRTSPMessage
 *
 * Queue a @message for transmission in @watch. The contents of this 
 * message will be serialized and transmitted when the connection of the
 * watch becomes writable.
 *
 * The return value of this function will be returned as the cseq argument in
 * the message_sent callback.
 *
 * Returns: the sequence number of the message or -1 if the cseq could not be
 * determined.
 *
 * Since: 0.10.23
 */
guint
gst_rtsp_watch_queue_message (GstRTSPWatch * watch, GstRTSPMessage * message)
{
  GstRTSPRec *data;
  gchar *header;
  guint cseq;

  g_return_val_if_fail (watch != NULL, GST_RTSP_EINVAL);
  g_return_val_if_fail (message != NULL, GST_RTSP_EINVAL);

  /* get the cseq from the message, when we finish writing this message on the
   * socket we will have to pass the cseq to the callback. */
  if (gst_rtsp_message_get_header (message, GST_RTSP_HDR_CSEQ, &header,
          0) == GST_RTSP_OK) {
    cseq = atoi (header);
  } else {
    cseq = -1;
  }

  /* make a record with the message as a string ans cseq */
  data = g_slice_new (GstRTSPRec);
  data->str = message_to_string (watch->conn, message);
  data->cseq = cseq;

  /* add the record to a queue */
  watch->messages = g_list_append (watch->messages, data);

  /* make sure the main context will now also check for writability on the
   * socket */
  if (!watch->write_added) {
    g_source_add_poll ((GSource *) watch, &watch->writefd);
    watch->write_added = TRUE;
  }
  return cseq;
}
