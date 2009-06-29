/* GStreamer
 * Copyright (C) <2005-2009> Wim Taymans <wim.taymans@gmail.com>
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
/* ws2_32.dll has getaddrinfo and freeaddrinfo on Windows XP and later.
 * minwg32 headers check WINVER before allowing the use of these */
#ifndef WINVER
#define WINVER 0x0501
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#define EINPROGRESS WSAEINPROGRESS
#else
#include <sys/ioctl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/in.h>
#endif

#ifdef HAVE_FIONREAD_IN_SYS_FILIO
#include <sys/filio.h>
#endif

#include "gstrtspconnection.h"
#include "gstrtspbase64.h"
#include "md5.h"

union gst_sockaddr
{
  struct sockaddr sa;
  struct sockaddr_in sa_in;
  struct sockaddr_in6 sa_in6;
  struct sockaddr_storage sa_stor;
};

typedef struct
{
  gint state;
  guint save;
  guchar out[3];                /* the size must be evenly divisible by 3 */
  guint cout;
  guint coutl;
} DecodeCtx;

static GstRTSPResult read_line (gint fd, guint8 * buffer, guint * idx,
    guint size, DecodeCtx * ctxp);
static GstRTSPResult parse_key_value (guint8 * buffer, gchar * key,
    guint keysize, gchar ** value);
static void parse_string (gchar * dest, gint size, gchar ** src);

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

#define ADD_POLLFD(fdset, pfd, fd)        \
G_STMT_START {                            \
  (pfd)->fd = fd;                         \
  gst_poll_add_fd (fdset, pfd);           \
} G_STMT_END

#define REMOVE_POLLFD(fdset, pfd)          \
G_STMT_START {                             \
  if ((pfd)->fd != -1) {                   \
    GST_DEBUG ("remove fd %d", (pfd)->fd); \
    gst_poll_remove_fd (fdset, pfd);       \
    CLOSE_SOCKET ((pfd)->fd);              \
    (pfd)->fd = -1;                        \
  }                                        \
} G_STMT_END

typedef enum
{
  TUNNEL_STATE_NONE,
  TUNNEL_STATE_GET,
  TUNNEL_STATE_POST,
  TUNNEL_STATE_COMPLETE
} GstRTSPTunnelState;

#define TUNNELID_LEN   24

struct _GstRTSPConnection
{
  /*< private > */
  /* URL for the connection */
  GstRTSPUrl *url;

  /* connection state */
  GstPollFD fd0;
  GstPollFD fd1;

  GstPollFD *readfd;
  GstPollFD *writefd;

  gchar tunnelid[TUNNELID_LEN];
  gboolean tunneled;
  GstRTSPTunnelState tstate;

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

  DecodeCtx ctx;
  DecodeCtx *ctxp;

  gchar *proxy_host;
  guint proxy_port;
};

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
  memset (builder, 0, sizeof (GstRTSPBuilder));
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
 * A copy of @url will be made.
 *
 * Returns: #GST_RTSP_OK when @conn contains a valid connection.
 */
GstRTSPResult
gst_rtsp_connection_create (const GstRTSPUrl * url, GstRTSPConnection ** conn)
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

  newconn->url = gst_rtsp_url_copy (url);
  newconn->fd0.fd = -1;
  newconn->fd1.fd = -1;
  newconn->timer = g_timer_new ();
  newconn->timeout = 60;
  newconn->cseq = 1;

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
  GstRTSPConnection *newconn = NULL;
  union gst_sockaddr sa;
  socklen_t slen = sizeof (sa);
  gchar ip[INET6_ADDRSTRLEN];
  GstRTSPUrl *url;
#ifdef G_OS_WIN32
  gulong flags = 1;
#endif

  memset (&sa, 0, slen);

#ifndef G_OS_WIN32
  fd = accept (sock, &sa.sa, &slen);
#else
  fd = accept (sock, &sa.sa, (gint *) & slen);
#endif /* G_OS_WIN32 */
  if (fd == -1)
    goto accept_failed;

  if (getnameinfo (&sa.sa, slen, ip, sizeof (ip), NULL, 0, NI_NUMERICHOST) != 0)
    goto getnameinfo_failed;
  if (sa.sa.sa_family != AF_INET && sa.sa.sa_family != AF_INET6)
    goto wrong_family;

  /* set to non-blocking mode so that we can cancel the communication */
#ifndef G_OS_WIN32
  fcntl (fd, F_SETFL, O_NONBLOCK);
#else
  ioctlsocket (fd, FIONBIO, &flags);
#endif /* G_OS_WIN32 */

  /* create a url for the client address */
  url = g_new0 (GstRTSPUrl, 1);
  url->host = g_strdup (ip);
  if (sa.sa.sa_family == AF_INET)
    url->port = sa.sa_in.sin_port;
  else
    url->port = sa.sa_in6.sin6_port;

  /* now create the connection object */
  gst_rtsp_connection_create (url, &newconn);
  gst_rtsp_url_free (url);

  ADD_POLLFD (newconn->fdset, &newconn->fd0, fd);

  /* both read and write initially */
  newconn->readfd = &newconn->fd0;
  newconn->writefd = &newconn->fd0;

  *conn = newconn;

  return GST_RTSP_OK;

  /* ERRORS */
accept_failed:
  {
    return GST_RTSP_ESYS;
  }
getnameinfo_failed:
wrong_family:
  {
    close (fd);
    return GST_RTSP_ERROR;
  }
}

static gchar *
do_resolve (const gchar * host)
{
  static gchar ip[INET6_ADDRSTRLEN];
  struct addrinfo *aires;
  struct addrinfo *ai;
  gint aierr;

  aierr = getaddrinfo (host, NULL, NULL, &aires);
  if (aierr != 0)
    goto no_addrinfo;

  for (ai = aires; ai; ai = ai->ai_next) {
    if (ai->ai_family == AF_INET || ai->ai_family == AF_INET6) {
      break;
    }
  }
  if (ai == NULL)
    goto no_family;

  aierr = getnameinfo (ai->ai_addr, ai->ai_addrlen, ip, sizeof (ip), NULL, 0,
      NI_NUMERICHOST | NI_NUMERICSERV);
  if (aierr != 0)
    goto no_address;

  freeaddrinfo (aires);

  return g_strdup (ip);

  /* ERRORS */
no_addrinfo:
  {
    GST_ERROR ("no addrinfo found for %s: %s", host, gai_strerror (aierr));
    return NULL;
  }
no_family:
  {
    GST_ERROR ("no family found for %s", host);
    freeaddrinfo (aires);
    return NULL;
  }
no_address:
  {
    GST_ERROR ("no address found for %s: %s", host, gai_strerror (aierr));
    freeaddrinfo (aires);
    return NULL;
  }
}

static GstRTSPResult
do_connect (const gchar * ip, guint16 port, GstPollFD * fdout,
    GstPoll * fdset, GTimeVal * timeout)
{
  gint fd;
  struct addrinfo hints;
  struct addrinfo *aires;
  struct addrinfo *ai;
  gint aierr;
  gchar service[NI_MAXSERV];
  gint ret;
#ifdef G_OS_WIN32
  unsigned long flags = 1;
#endif /* G_OS_WIN32 */
  GstClockTime to;
  gint retval;

  memset (&hints, 0, sizeof hints);
  hints.ai_flags = AI_NUMERICHOST;
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  g_snprintf (service, sizeof (service) - 1, "%hu", port);
  service[sizeof (service) - 1] = '\0';

  aierr = getaddrinfo (ip, service, &hints, &aires);
  if (aierr != 0)
    goto no_addrinfo;

  for (ai = aires; ai; ai = ai->ai_next) {
    if (ai->ai_family == AF_INET || ai->ai_family == AF_INET6) {
      break;
    }
  }
  if (ai == NULL)
    goto no_family;

  fd = socket (ai->ai_family, SOCK_STREAM, 0);
  if (fd == -1)
    goto no_socket;

  /* set to non-blocking mode so that we can cancel the connect */
#ifndef G_OS_WIN32
  fcntl (fd, F_SETFL, O_NONBLOCK);
#else
  ioctlsocket (fd, FIONBIO, &flags);
#endif /* G_OS_WIN32 */

  /* add the socket to our fdset */
  ADD_POLLFD (fdset, fdout, fd);

  /* we are going to connect ASYNC now */
  ret = connect (fd, ai->ai_addr, ai->ai_addrlen);
  if (ret == 0)
    goto done;
  if (!ERRNO_IS_EINPROGRESS)
    goto sys_error;

  /* wait for connect to complete up to the specified timeout or until we got
   * interrupted. */
  gst_poll_fd_ctl_write (fdset, fdout, TRUE);

  to = timeout ? GST_TIMEVAL_TO_TIME (*timeout) : GST_CLOCK_TIME_NONE;

  do {
    retval = gst_poll_wait (fdset, to);
  } while (retval == -1 && (errno == EINTR || errno == EAGAIN));

  if (retval == 0)
    goto timeout;
  else if (retval == -1)
    goto sys_error;

  /* we can still have an error connecting on windows */
  if (gst_poll_fd_has_error (fdset, fdout)) {
    socklen_t len = sizeof (errno);
#ifndef G_OS_WIN32
    getsockopt (fd, SOL_SOCKET, SO_ERROR, &errno, &len);
#else
    getsockopt (fd, SOL_SOCKET, SO_ERROR, (char *) &errno, &len);
#endif
    goto sys_error;
  }

  gst_poll_fd_ignored (fdset, fdout);

done:
  freeaddrinfo (aires);

  return GST_RTSP_OK;

  /* ERRORS */
no_addrinfo:
  {
    GST_ERROR ("no addrinfo found for %s: %s", ip, gai_strerror (aierr));
    return GST_RTSP_ERROR;
  }
no_family:
  {
    GST_ERROR ("no family found for %s", ip);
    freeaddrinfo (aires);
    return GST_RTSP_ERROR;
  }
no_socket:
  {
    GST_ERROR ("no socket %d (%s)", errno, g_strerror (errno));
    freeaddrinfo (aires);
    return GST_RTSP_ESYS;
  }
sys_error:
  {
    GST_ERROR ("system error %d (%s)", errno, g_strerror (errno));
    REMOVE_POLLFD (fdset, fdout);
    freeaddrinfo (aires);
    return GST_RTSP_ESYS;
  }
timeout:
  {
    GST_ERROR ("timeout");
    REMOVE_POLLFD (fdset, fdout);
    freeaddrinfo (aires);
    return GST_RTSP_ETIMEOUT;
  }
}

static GstRTSPResult
setup_tunneling (GstRTSPConnection * conn, GTimeVal * timeout)
{
  gint i;
  GstRTSPResult res;
  gchar *str;
  guint idx, line;
  gint retval;
  GstClockTime to;
  gchar *ip, *url_port_str;
  guint16 port, url_port;
  gchar codestr[4], *resultstr;
  gint code;
  GstRTSPUrl *url;
  gchar *hostparam;

  /* create a random sessionid */
  for (i = 0; i < TUNNELID_LEN; i++)
    conn->tunnelid[i] = g_random_int_range ('a', 'z');
  conn->tunnelid[TUNNELID_LEN - 1] = '\0';

  url = conn->url;
  /* get the port from the url */
  gst_rtsp_url_get_port (url, &url_port);

  if (conn->proxy_host) {
    hostparam = g_strdup_printf ("Host: %s:%d\r\n", url->host, url_port);
    url_port_str = g_strdup_printf (":%d", url_port);
    ip = conn->proxy_host;
    port = conn->proxy_port;
  } else {
    hostparam = NULL;
    url_port_str = NULL;
    ip = conn->ip;
    port = url_port;
  }

  /* */
  str = g_strdup_printf ("GET %s%s%s%s%s%s HTTP/1.0\r\n"
      "%s"
      "x-sessioncookie: %s\r\n"
      "Accept: application/x-rtsp-tunnelled\r\n"
      "Pragma: no-cache\r\n"
      "Cache-Control: no-cache\r\n" "\r\n",
      conn->proxy_host ? "http://" : "",
      conn->proxy_host ? url->host : "",
      conn->proxy_host ? url_port_str : "",
      url->abspath, url->query ? "?" : "", url->query ? url->query : "",
      hostparam ? hostparam : "", conn->tunnelid);

  /* we start by writing to this fd */
  conn->writefd = &conn->fd0;

  res = gst_rtsp_connection_write (conn, (guint8 *) str, strlen (str), timeout);
  g_free (str);
  if (res != GST_RTSP_OK)
    goto write_failed;

  gst_poll_fd_ctl_write (conn->fdset, &conn->fd0, FALSE);
  gst_poll_fd_ctl_read (conn->fdset, &conn->fd0, TRUE);

  to = timeout ? GST_TIMEVAL_TO_TIME (*timeout) : GST_CLOCK_TIME_NONE;

  line = 0;
  while (TRUE) {
    guint8 buffer[4096];

    idx = 0;
    while (TRUE) {
      res = read_line (conn->fd0.fd, buffer, &idx, sizeof (buffer), NULL);
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
        goto timeout;

      if (retval == -1) {
        if (errno == EBUSY)
          goto stopped;
        else
          goto select_error;
      }
    }

    /* check for last line */
    if (buffer[0] == '\r')
      buffer[0] = '\0';
    if (buffer[0] == '\0')
      break;

    if (line == 0) {
      /* first line, parse response */
      gchar versionstr[20];
      gchar *bptr;

      bptr = (gchar *) buffer;

      parse_string (versionstr, sizeof (versionstr), &bptr);
      parse_string (codestr, sizeof (codestr), &bptr);
      code = atoi (codestr);

      while (g_ascii_isspace (*bptr))
        bptr++;

      resultstr = bptr;

      if (code != GST_RTSP_STS_OK)
        goto wrong_result;
    } else {
      gchar key[32];
      gchar *value;

      /* other lines, parse key/value */
      res = parse_key_value (buffer, key, sizeof (key), &value);
      if (res == GST_RTSP_OK) {
        /* we got a new ip address */
        if (g_ascii_strcasecmp (key, "x-server-ip-address") == 0) {
          if (conn->proxy_host) {
            /* if we use a proxy we need to change the destination url */
            g_free (url->host);
            url->host = g_strdup (value);
            g_free (hostparam);
            g_free (url_port_str);
            hostparam =
                g_strdup_printf ("Host: %s:%d\r\n", url->host, url_port);
            url_port_str = g_strdup_printf (":%d", url_port);
          } else {
            /* and resolve the new ip address */
            if (!(ip = do_resolve (conn->ip)))
              goto not_resolved;
            g_free (conn->ip);
            conn->ip = ip;
          }
        }
      }
    }
    line++;
  }

  /* connect to the host/port */
  res = do_connect (ip, port, &conn->fd1, conn->fdset, timeout);
  if (res != GST_RTSP_OK)
    goto connect_failed;

  /* this is now our writing socket */
  conn->writefd = &conn->fd1;

  /* */
  str = g_strdup_printf ("POST %s%s%s%s%s%s HTTP/1.0\r\n"
      "%s"
      "x-sessioncookie: %s\r\n"
      "Content-Type: application/x-rtsp-tunnelled\r\n"
      "Pragma: no-cache\r\n"
      "Cache-Control: no-cache\r\n"
      "Content-Length: 32767\r\n"
      "Expires: Sun, 9 Jan 1972 00:00:00 GMT\r\n"
      "\r\n",
      conn->proxy_host ? "http://" : "",
      conn->proxy_host ? url->host : "",
      conn->proxy_host ? url_port_str : "",
      url->abspath, url->query ? "?" : "", url->query ? url->query : "",
      hostparam ? hostparam : "", conn->tunnelid);

  res = gst_rtsp_connection_write (conn, (guint8 *) str, strlen (str), timeout);
  g_free (str);
  if (res != GST_RTSP_OK)
    goto write_failed;

exit:
  g_free (hostparam);
  g_free (url_port_str);

  return res;

  /* ERRORS */
write_failed:
  {
    GST_ERROR ("write failed (%d)", res);
    goto exit;
  }
eof:
  {
    res = GST_RTSP_EEOF;
    goto exit;
  }
read_error:
  {
    goto exit;
  }
timeout:
  {
    res = GST_RTSP_ETIMEOUT;
    goto exit;
  }
select_error:
  {
    res = GST_RTSP_ESYS;
    goto exit;
  }
stopped:
  {
    res = GST_RTSP_EINTR;
    goto exit;
  }
wrong_result:
  {
    GST_ERROR ("got failure response %d %s", code, resultstr);
    res = GST_RTSP_ERROR;
    goto exit;
  }
not_resolved:
  {
    GST_ERROR ("could not resolve %s", conn->ip);
    res = GST_RTSP_ENET;
    goto exit;
  }
connect_failed:
  {
    GST_ERROR ("failed to connect");
    goto exit;
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
  GstRTSPResult res;
  gchar *ip;
  guint16 port;
  GstRTSPUrl *url;

  g_return_val_if_fail (conn != NULL, GST_RTSP_EINVAL);
  g_return_val_if_fail (conn->url != NULL, GST_RTSP_EINVAL);
  g_return_val_if_fail (conn->fd0.fd < 0, GST_RTSP_EINVAL);

  url = conn->url;

  if (conn->proxy_host && conn->tunneled) {
    if (!(ip = do_resolve (conn->proxy_host))) {
      GST_ERROR ("could not resolve %s", conn->proxy_host);
      goto not_resolved;
    }
    port = conn->proxy_port;
    g_free (conn->proxy_host);
    conn->proxy_host = ip;
  } else {
    if (!(ip = do_resolve (url->host))) {
      GST_ERROR ("could not resolve %s", url->host);
      goto not_resolved;
    }
    /* get the port from the url */
    gst_rtsp_url_get_port (url, &port);

    g_free (conn->ip);
    conn->ip = ip;
  }

  /* connect to the host/port */
  res = do_connect (ip, port, &conn->fd0, conn->fdset, timeout);
  if (res != GST_RTSP_OK)
    goto connect_failed;

  /* this is our read URL */
  conn->readfd = &conn->fd0;

  if (conn->tunneled) {
    res = setup_tunneling (conn, timeout);
    if (res != GST_RTSP_OK)
      goto tunneling_failed;
  } else {
    conn->writefd = &conn->fd0;
  }

  return GST_RTSP_OK;

not_resolved:
  {
    return GST_RTSP_ENET;
  }
connect_failed:
  {
    GST_ERROR ("failed to connect");
    return res;
  }
tunneling_failed:
  {
    GST_ERROR ("failed to setup tunneling");
    return res;
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
      gchar *user_pass;
      gchar *user_pass64;
      gchar *auth_string;

      user_pass = g_strdup_printf ("%s:%s", conn->username, conn->passwd);
      user_pass64 = g_base64_encode ((guchar *) user_pass, strlen (user_pass));
      auth_string = g_strdup_printf ("Basic %s", user_pass64);

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
gen_date_string (gchar * date_string, guint len)
{
  GTimeVal tv;
  time_t t;
#ifdef HAVE_GMTIME_R
  struct tm tm_;
#endif

  g_get_current_time (&tv);
  t = (time_t) tv.tv_sec;

#ifdef HAVE_GMTIME_R
  strftime (date_string, len, "%a, %d %b %Y %H:%M:%S GMT", gmtime_r (&t, &tm_));
#else
  strftime (date_string, len, "%a, %d %b %Y %H:%M:%S GMT", gmtime (&t));
#endif
}

static GstRTSPResult
write_bytes (gint fd, const guint8 * buffer, guint * idx, guint size)
{
  guint left;

  if (G_UNLIKELY (*idx > size))
    return GST_RTSP_ERROR;

  left = size - *idx;

  while (left) {
    gint r;

    r = WRITE_SOCKET (fd, &buffer[*idx], left);
    if (G_UNLIKELY (r == 0)) {
      return GST_RTSP_EINTR;
    } else if (G_UNLIKELY (r < 0)) {
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

static gint
fill_bytes (gint fd, guint8 * buffer, guint size, DecodeCtx * ctx)
{
  gint out = 0;

  if (ctx) {
    while (size > 0) {
      guint8 in[sizeof (ctx->out) * 4 / 3];
      gint r;

      while (size > 0 && ctx->cout < ctx->coutl) {
        /* we have some leftover bytes */
        *buffer++ = ctx->out[ctx->cout++];
        size--;
        out++;
      }

      /* got what we needed? */
      if (size == 0)
        break;

      /* try to read more bytes */
      r = READ_SOCKET (fd, in, sizeof (in));
      if (r <= 0) {
        if (out == 0)
          out = r;
        break;
      }

      ctx->cout = 0;
      ctx->coutl =
          g_base64_decode_step ((gchar *) in, r, ctx->out, &ctx->state,
          &ctx->save);
    }
  } else {
    out = READ_SOCKET (fd, buffer, size);
  }

  return out;
}

static GstRTSPResult
read_bytes (gint fd, guint8 * buffer, guint * idx, guint size, DecodeCtx * ctx)
{
  guint left;

  if (G_UNLIKELY (*idx > size))
    return GST_RTSP_ERROR;

  left = size - *idx;

  while (left) {
    gint r;

    r = fill_bytes (fd, &buffer[*idx], left, ctx);
    if (G_UNLIKELY (r == 0)) {
      return GST_RTSP_EEOF;
    } else if (G_UNLIKELY (r < 0)) {
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
read_line (gint fd, guint8 * buffer, guint * idx, guint size, DecodeCtx * ctx)
{
  while (TRUE) {
    guint8 c;
    gint r;

    r = fill_bytes (fd, &c, 1, ctx);
    if (G_UNLIKELY (r == 0)) {
      return GST_RTSP_EEOF;
    } else if (G_UNLIKELY (r < 0)) {
      if (ERRNO_IS_EAGAIN)
        return GST_RTSP_EINTR;
      if (!ERRNO_IS_EINTR)
        return GST_RTSP_ESYS;
    } else {
      if (c == '\n')            /* end on \n */
        break;
      if (c == '\r')            /* ignore \r */
        continue;

      if (G_LIKELY (*idx < size - 1))
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
  g_return_val_if_fail (conn->writefd != NULL, GST_RTSP_EINVAL);

  gst_poll_set_controllable (conn->fdset, TRUE);
  gst_poll_fd_ctl_write (conn->fdset, conn->writefd, TRUE);
  gst_poll_fd_ctl_read (conn->fdset, conn->readfd, FALSE);
  /* clear all previous poll results */
  gst_poll_fd_ignored (conn->fdset, conn->writefd);
  gst_poll_fd_ignored (conn->fdset, conn->readfd);

  to = timeout ? GST_TIMEVAL_TO_TIME (*timeout) : GST_CLOCK_TIME_NONE;

  offset = 0;

  while (TRUE) {
    /* try to write */
    res = write_bytes (conn->writefd->fd, data, &offset, size);
    if (G_LIKELY (res == GST_RTSP_OK))
      break;
    if (G_UNLIKELY (res != GST_RTSP_EINTR))
      goto write_error;

    /* not all is written, wait until we can write more */
    do {
      retval = gst_poll_wait (conn->fdset, to);
    } while (retval == -1 && (errno == EINTR || errno == EAGAIN));

    if (G_UNLIKELY (retval == 0))
      goto timeout;

    if (G_UNLIKELY (retval == -1)) {
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
    gchar date_string[100];

    gen_date_string (date_string, sizeof (date_string));

    /* add date header */
    gst_rtsp_message_add_header (message, GST_RTSP_HDR_DATE, date_string);

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
  GString *string = NULL;
  GstRTSPResult res;
  gchar *str;
  gsize len;

  g_return_val_if_fail (conn != NULL, GST_RTSP_EINVAL);
  g_return_val_if_fail (message != NULL, GST_RTSP_EINVAL);

  if (G_UNLIKELY (!(string = message_to_string (conn, message))))
    goto no_message;

  if (conn->tunneled) {
    str = g_base64_encode ((const guchar *) string->str, string->len);
    g_string_free (string, TRUE);
    len = strlen (str);
  } else {
    str = string->str;
    len = string->len;
    g_string_free (string, FALSE);
  }

  /* write request */
  res = gst_rtsp_connection_write (conn, (guint8 *) str, len, timeout);

  g_free (str);

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
parse_request_line (GstRTSPConnection * conn, guint8 * buffer,
    GstRTSPMessage * msg)
{
  GstRTSPResult res = GST_RTSP_OK;
  gchar versionstr[20];
  gchar methodstr[20];
  gchar urlstr[4096];
  gchar *bptr;
  GstRTSPMethod method;
  GstRTSPTunnelState tstate = TUNNEL_STATE_NONE;

  bptr = (gchar *) buffer;

  parse_string (methodstr, sizeof (methodstr), &bptr);
  method = gst_rtsp_find_method (methodstr);
  if (method == GST_RTSP_INVALID) {
    /* a tunnel request is allowed when we don't have one yet */
    if (conn->tstate != TUNNEL_STATE_NONE)
      goto invalid_method;
    /* we need GET or POST for a valid tunnel request */
    if (!strcmp (methodstr, "GET"))
      tstate = TUNNEL_STATE_GET;
    else if (!strcmp (methodstr, "POST"))
      tstate = TUNNEL_STATE_POST;
    else
      goto invalid_method;
  }

  parse_string (urlstr, sizeof (urlstr), &bptr);
  if (G_UNLIKELY (*urlstr == '\0'))
    goto invalid_url;

  parse_string (versionstr, sizeof (versionstr), &bptr);

  if (G_UNLIKELY (*bptr != '\0'))
    goto invalid_version;

  if (strcmp (versionstr, "RTSP/1.0") == 0) {
    res = gst_rtsp_message_init_request (msg, method, urlstr);
  } else if (strncmp (versionstr, "RTSP/", 5) == 0) {
    res = gst_rtsp_message_init_request (msg, method, urlstr);
    msg->type_data.request.version = GST_RTSP_VERSION_INVALID;
  } else if (strcmp (versionstr, "HTTP/1.0") == 0) {
    /* tunnel request, we need a tunnel method */
    if (tstate == TUNNEL_STATE_NONE) {
      res = GST_RTSP_EPARSE;
    } else {
      conn->tstate = tstate;
    }
  } else {
    res = GST_RTSP_EPARSE;
  }

  return res;

  /* ERRORS */
invalid_method:
  {
    GST_ERROR ("invalid method %s", methodstr);
    return GST_RTSP_EPARSE;
  }
invalid_url:
  {
    GST_ERROR ("invalid url %s", urlstr);
    return GST_RTSP_EPARSE;
  }
invalid_version:
  {
    GST_ERROR ("invalid version");
    return GST_RTSP_EPARSE;
  }
}

static GstRTSPResult
parse_key_value (guint8 * buffer, gchar * key, guint keysize, gchar ** value)
{
  gchar *bptr;

  bptr = (gchar *) buffer;

  /* read key */
  parse_key (key, keysize, &bptr);
  if (G_UNLIKELY (*bptr != ':'))
    goto no_column;

  bptr++;
  while (g_ascii_isspace (*bptr))
    bptr++;

  *value = bptr;

  return GST_RTSP_OK;

  /* ERRORS */
no_column:
  {
    return GST_RTSP_EPARSE;
  }
}

/* parsing lines means reading a Key: Value pair */
static GstRTSPResult
parse_line (GstRTSPConnection * conn, guint8 * buffer, GstRTSPMessage * msg)
{
  GstRTSPResult res;
  gchar key[32];
  gchar *value;
  GstRTSPHeaderField field;

  res = parse_key_value (buffer, key, sizeof (key), &value);
  if (G_UNLIKELY (res != GST_RTSP_OK))
    goto parse_error;

  if (conn->tstate == TUNNEL_STATE_GET || conn->tstate == TUNNEL_STATE_POST) {
    /* save the tunnel session in the connection */
    if (!strcmp (key, "x-sessioncookie")) {
      strncpy (conn->tunnelid, value, TUNNELID_LEN);
      conn->tunnelid[TUNNELID_LEN - 1] = '\0';
      conn->tunneled = TRUE;
    }
  } else {
    field = gst_rtsp_find_header_field (key);
    if (field != GST_RTSP_HDR_INVALID)
      gst_rtsp_message_add_header (msg, field, value);
  }

  return GST_RTSP_OK;

  /* ERRORS */
parse_error:
  {
    return res;
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
            read_bytes (conn->readfd->fd, (guint8 *) builder->buffer,
            &builder->offset, 1, conn->ctxp);
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
            read_bytes (conn->readfd->fd, (guint8 *) builder->buffer,
            &builder->offset, 4, conn->ctxp);
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
        res =
            read_bytes (conn->readfd->fd, builder->body_data, &builder->offset,
            builder->body_len, conn->ctxp);
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
        res = read_line (conn->readfd->fd, builder->buffer, &builder->offset,
            sizeof (builder->buffer), conn->ctxp);
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
            res = parse_request_line (conn, builder->buffer, message);
          }
          /* the first line must parse without errors */
          if (res != GST_RTSP_OK)
            goto done;
        } else {
          /* else just parse the line, ignore errors */
          parse_line (conn, builder->buffer, message);
        }
        builder->line++;
        builder->offset = 0;
        break;
      }
      case STATE_END:
      {
        gchar *session_id;

        if (conn->tstate == TUNNEL_STATE_GET) {
          res = GST_RTSP_ETGET;
          goto done;
        } else if (conn->tstate == TUNNEL_STATE_POST) {
          res = GST_RTSP_ETPOST;
          goto done;
        }

        if (message->type == GST_RTSP_MESSAGE_DATA) {
          /* data messages don't have headers */
          res = GST_RTSP_OK;
          goto done;
        }

        /* save session id in the connection for further use */
        if (message->type == GST_RTSP_MESSAGE_RESPONSE &&
            gst_rtsp_message_get_header (message, GST_RTSP_HDR_SESSION,
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
                if ((to = atoi (&session_id[i + 8])) > 0)
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
  g_return_val_if_fail (conn->readfd != NULL, GST_RTSP_EINVAL);

  if (G_UNLIKELY (size == 0))
    return GST_RTSP_OK;

  offset = 0;

  /* configure timeout if any */
  to = timeout ? GST_TIMEVAL_TO_TIME (*timeout) : GST_CLOCK_TIME_NONE;

  gst_poll_set_controllable (conn->fdset, TRUE);
  gst_poll_fd_ctl_write (conn->fdset, conn->writefd, FALSE);
  gst_poll_fd_ctl_read (conn->fdset, conn->readfd, TRUE);

  while (TRUE) {
    res = read_bytes (conn->readfd->fd, data, &offset, size, conn->ctxp);
    if (G_UNLIKELY (res == GST_RTSP_EEOF))
      goto eof;
    if (G_LIKELY (res == GST_RTSP_OK))
      break;
    if (G_UNLIKELY (res != GST_RTSP_EINTR))
      goto read_error;

    do {
      retval = gst_poll_wait (conn->fdset, to);
    } while (retval == -1 && (errno == EINTR || errno == EAGAIN));

    /* check for timeout */
    if (G_UNLIKELY (retval == 0))
      goto select_timeout;

    if (G_UNLIKELY (retval == -1)) {
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

static GString *
gen_tunnel_reply (GstRTSPConnection * conn, GstRTSPStatusCode code)
{
  GString *str;
  gchar date_string[100];
  const gchar *status;

  gen_date_string (date_string, sizeof (date_string));

  status = gst_rtsp_status_as_text (code);
  if (status == NULL) {
    code = GST_RTSP_STS_INTERNAL_SERVER_ERROR;
    status = "Internal Server Error";
  }

  str = g_string_new ("");

  /* */
  g_string_append_printf (str, "HTTP/1.0 %d %s\r\n", code, status);
  g_string_append_printf (str,
      "Server: GStreamer RTSP Server\r\n"
      "Date: %s\r\n"
      "Connection: close\r\n"
      "Cache-Control: no-store\r\n" "Pragma: no-cache\r\n", date_string);
  if (code == GST_RTSP_STS_OK) {
    if (conn->ip)
      g_string_append_printf (str, "x-server-ip-address: %s\r\n", conn->ip);
    g_string_append_printf (str,
        "Content-Type: application/x-rtsp-tunnelled\r\n");
  }
  g_string_append_printf (str, "\r\n");
  return str;
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
  GstRTSPBuilder builder;
  gint retval;
  GstClockTime to;

  g_return_val_if_fail (conn != NULL, GST_RTSP_EINVAL);
  g_return_val_if_fail (message != NULL, GST_RTSP_EINVAL);
  g_return_val_if_fail (conn->readfd != NULL, GST_RTSP_EINVAL);

  /* configure timeout if any */
  to = timeout ? GST_TIMEVAL_TO_TIME (*timeout) : GST_CLOCK_TIME_NONE;

  gst_poll_set_controllable (conn->fdset, TRUE);
  gst_poll_fd_ctl_write (conn->fdset, conn->writefd, FALSE);
  gst_poll_fd_ctl_read (conn->fdset, conn->readfd, TRUE);

  memset (&builder, 0, sizeof (GstRTSPBuilder));
  while (TRUE) {
    res = build_next (&builder, message, conn);
    if (G_UNLIKELY (res == GST_RTSP_EEOF))
      goto eof;
    if (G_LIKELY (res == GST_RTSP_OK))
      break;
    if (res == GST_RTSP_ETGET) {
      GString *str;

      /* tunnel GET request, we can reply now */
      str = gen_tunnel_reply (conn, GST_RTSP_STS_OK);
      res =
          gst_rtsp_connection_write (conn, (guint8 *) str->str, str->len,
          timeout);
      g_string_free (str, TRUE);
    } else if (res == GST_RTSP_ETPOST) {
      /* tunnel POST request, return the value, the caller now has to link the
       * two connections. */
      break;
    } else if (G_UNLIKELY (res != GST_RTSP_EINTR))
      goto read_error;

    do {
      retval = gst_poll_wait (conn->fdset, to);
    } while (retval == -1 && (errno == EINTR || errno == EAGAIN));

    /* check for timeout */
    if (G_UNLIKELY (retval == 0))
      goto select_timeout;

    if (G_UNLIKELY (retval == -1)) {
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
 * Close the connected @conn. After this call, the connection is in the same
 * state as when it was first created.
 * 
 * Returns: #GST_RTSP_OK on success.
 */
GstRTSPResult
gst_rtsp_connection_close (GstRTSPConnection * conn)
{
  g_return_val_if_fail (conn != NULL, GST_RTSP_EINVAL);

  g_free (conn->ip);
  conn->ip = NULL;

  REMOVE_POLLFD (conn->fdset, &conn->fd0);
  REMOVE_POLLFD (conn->fdset, &conn->fd1);
  conn->writefd = NULL;
  conn->readfd = NULL;
  conn->tunneled = FALSE;
  conn->tstate = TUNNEL_STATE_NONE;
  conn->ctxp = NULL;
  g_free (conn->username);
  conn->username = NULL;
  g_free (conn->passwd);
  conn->passwd = NULL;
  gst_rtsp_connection_clear_auth_params (conn);
  conn->timeout = 60;
  conn->cseq = 0;
  conn->session_id[0] = '\0';

  return GST_RTSP_OK;
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
  gst_rtsp_url_free (conn->url);
  g_free (conn->proxy_host);
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
  g_return_val_if_fail (conn->readfd != NULL, GST_RTSP_EINVAL);
  g_return_val_if_fail (conn->writefd != NULL, GST_RTSP_EINVAL);

  gst_poll_set_controllable (conn->fdset, TRUE);

  /* add fd to writer set when asked to */
  gst_poll_fd_ctl_write (conn->fdset, conn->writefd,
      events & GST_RTSP_EV_WRITE);

  /* add fd to reader set when asked to */
  gst_poll_fd_ctl_read (conn->fdset, conn->readfd, events & GST_RTSP_EV_READ);

  /* configure timeout if any */
  to = timeout ? GST_TIMEVAL_TO_TIME (*timeout) : GST_CLOCK_TIME_NONE;

  do {
    retval = gst_poll_wait (conn->fdset, to);
  } while (retval == -1 && (errno == EINTR || errno == EAGAIN));

  if (G_UNLIKELY (retval == 0))
    goto select_timeout;

  if (G_UNLIKELY (retval == -1)) {
    if (errno == EBUSY)
      goto stopped;
    else
      goto select_error;
  }

  *revents = 0;
  if (events & GST_RTSP_EV_READ) {
    if (gst_poll_fd_can_read (conn->fdset, conn->readfd))
      *revents |= GST_RTSP_EV_READ;
  }
  if (events & GST_RTSP_EV_WRITE) {
    if (gst_poll_fd_can_write (conn->fdset, conn->writefd))
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
 * gst_rtsp_connection_set_proxy:
 * @conn: a #GstRTSPConnection
 * @host: the proxy host
 * @port: the proxy port
 *
 * Set the proxy host and port.
 * 
 * Returns: #GST_RTSP_OK.
 *
 * Since: 0.10.23
 */
GstRTSPResult
gst_rtsp_connection_set_proxy (GstRTSPConnection * conn,
    const gchar * host, guint port)
{
  g_return_val_if_fail (conn != NULL, GST_RTSP_EINVAL);

  g_free (conn->proxy_host);
  conn->proxy_host = g_strdup (host);
  conn->proxy_port = port;

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

static GstRTSPResult
set_qos_dscp (gint fd, guint qos_dscp)
{
  union gst_sockaddr sa;
  socklen_t slen = sizeof (sa);
  gint af;
  gint tos;

  if (fd == -1)
    return GST_RTSP_OK;

  if (getsockname (fd, &sa.sa, &slen) < 0)
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
      if (SETSOCKOPT (fd, IPPROTO_IP, IP_TOS, &tos, sizeof (tos)) < 0)
        goto no_setsockopt;
      break;
    case AF_INET6:
#ifdef IPV6_TCLASS
      if (SETSOCKOPT (fd, IPPROTO_IPV6, IPV6_TCLASS, &tos, sizeof (tos)) < 0)
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
  GstRTSPResult res;

  g_return_val_if_fail (conn != NULL, GST_RTSP_EINVAL);
  g_return_val_if_fail (conn->readfd != NULL, GST_RTSP_EINVAL);
  g_return_val_if_fail (conn->writefd != NULL, GST_RTSP_EINVAL);

  res = set_qos_dscp (conn->fd0.fd, qos_dscp);
  if (res == GST_RTSP_OK)
    res = set_qos_dscp (conn->fd1.fd, qos_dscp);

  return res;
}


/**
 * gst_rtsp_connection_get_url:
 * @conn: a #GstRTSPConnection
 *
 * Retrieve the URL of the other end of @conn.
 *
 * Returns: The URL. This value remains valid until the
 * connection is freed.
 *
 * Since: 0.10.23
 */
GstRTSPUrl *
gst_rtsp_connection_get_url (const GstRTSPConnection * conn)
{
  g_return_val_if_fail (conn != NULL, NULL);

  return conn->url;
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

/**
 * gst_rtsp_connection_set_ip:
 * @conn: a #GstRTSPConnection
 * @ip: an ip address
 *
 * Set the IP address of the server.
 *
 * Since: 0.10.23
 */
void
gst_rtsp_connection_set_ip (GstRTSPConnection * conn, const gchar * ip)
{
  g_return_if_fail (conn != NULL);

  g_free (conn->ip);
  conn->ip = g_strdup (ip);
}

/**
 * gst_rtsp_connection_get_readfd:
 * @conn: a #GstRTSPConnection
 *
 * Get the file descriptor for reading.
 *
 * Returns: the file descriptor used for reading or -1 on error. The file
 * descriptor remains valid until the connection is closed.
 *
 * Since: 0.10.23
 */
gint
gst_rtsp_connection_get_readfd (const GstRTSPConnection * conn)
{
  g_return_val_if_fail (conn != NULL, -1);
  g_return_val_if_fail (conn->readfd != NULL, -1);

  return conn->readfd->fd;
}

/**
 * gst_rtsp_connection_get_writefd:
 * @conn: a #GstRTSPConnection
 *
 * Get the file descriptor for writing.
 *
 * Returns: the file descriptor used for writing or -1 on error. The file
 * descriptor remains valid until the connection is closed.
 *
 * Since: 0.10.23
 */
gint
gst_rtsp_connection_get_writefd (const GstRTSPConnection * conn)
{
  g_return_val_if_fail (conn != NULL, -1);
  g_return_val_if_fail (conn->writefd != NULL, -1);

  return conn->writefd->fd;
}


/**
 * gst_rtsp_connection_set_tunneled:
 * @conn: a #GstRTSPConnection
 * @tunneled: the new state
 *
 * Set the HTTP tunneling state of the connection. This must be configured before
 * the @conn is connected.
 *
 * Since: 0.10.23
 */
void
gst_rtsp_connection_set_tunneled (GstRTSPConnection * conn, gboolean tunneled)
{
  g_return_if_fail (conn != NULL);
  g_return_if_fail (conn->readfd == NULL);
  g_return_if_fail (conn->writefd == NULL);

  conn->tunneled = tunneled;
}

/**
 * gst_rtsp_connection_is_tunneled:
 * @conn: a #GstRTSPConnection
 *
 * Get the tunneling state of the connection. 
 *
 * Returns: if @conn is using HTTP tunneling.
 *
 * Since: 0.10.23
 */
gboolean
gst_rtsp_connection_is_tunneled (const GstRTSPConnection * conn)
{
  g_return_val_if_fail (conn != NULL, FALSE);

  return conn->tunneled;
}

/**
 * gst_rtsp_connection_get_tunnelid:
 * @conn: a #GstRTSPConnection
 *
 * Get the tunnel session id the connection. 
 *
 * Returns: returns a non-empty string if @conn is being tunneled over HTTP.
 *
 * Since: 0.10.23
 */
const gchar *
gst_rtsp_connection_get_tunnelid (const GstRTSPConnection * conn)
{
  g_return_val_if_fail (conn != NULL, NULL);

  if (!conn->tunneled)
    return NULL;

  return conn->tunnelid;
}

/**
 * gst_rtsp_connection_do_tunnel:
 * @conn: a #GstRTSPConnection
 * @conn2: a #GstRTSPConnection
 *
 * If @conn received the first tunnel connection and @conn2 received
 * the second tunnel connection, link the two connections together so that
 * @conn manages the tunneled connection.
 *
 * After this call, @conn2 cannot be used anymore and must be freed with
 * gst_rtsp_connection_free().
 *
 * Returns: return GST_RTSP_OK on success.
 *
 * Since: 0.10.23
 */
GstRTSPResult
gst_rtsp_connection_do_tunnel (GstRTSPConnection * conn,
    GstRTSPConnection * conn2)
{
  g_return_val_if_fail (conn != NULL, GST_RTSP_EINVAL);
  g_return_val_if_fail (conn2 != NULL, GST_RTSP_EINVAL);
  g_return_val_if_fail (conn->tstate == TUNNEL_STATE_GET, GST_RTSP_EINVAL);
  g_return_val_if_fail (conn2->tstate == TUNNEL_STATE_POST, GST_RTSP_EINVAL);
  g_return_val_if_fail (!memcmp (conn2->tunnelid, conn->tunnelid, TUNNELID_LEN),
      GST_RTSP_EINVAL);

  /* both connections have fd0 as the read/write socket. start by taking the
   * socket from conn2 and set it as the socket in conn */
  conn->fd1 = conn2->fd0;

  /* clean up some of the state of conn2 */
  gst_poll_remove_fd (conn2->fdset, &conn2->fd0);
  conn2->fd0.fd = -1;
  conn2->readfd = conn2->writefd = NULL;

  /* We make fd0 the write socket and fd1 the read socket. */
  conn->writefd = &conn->fd0;
  conn->readfd = &conn->fd1;

  conn->tstate = TUNNEL_STATE_COMPLETE;

  /* we need base64 decoding for the readfd */
  conn->ctx.state = 0;
  conn->ctx.save = 0;
  conn->ctx.cout = 0;
  conn->ctx.coutl = 0;
  conn->ctxp = &conn->ctx;

  return GST_RTSP_OK;
}

#define READ_COND   (G_IO_IN | G_IO_HUP | G_IO_ERR)
#define WRITE_COND  (G_IO_OUT | G_IO_ERR)

typedef struct
{
  guint8 *data;
  guint size;
  guint id;
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
  guint id;
  GAsyncQueue *messages;
  guint8 *write_data;
  guint write_off;
  guint write_size;
  guint write_id;

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
gst_rtsp_source_dispatch (GSource * source, GSourceFunc callback G_GNUC_UNUSED,
    gpointer user_data G_GNUC_UNUSED)
{
  GstRTSPWatch *watch = (GstRTSPWatch *) source;
  GstRTSPResult res;

  /* first read as much as we can */
  if (watch->readfd.revents & READ_COND) {
    do {
      res = build_next (&watch->builder, &watch->message, watch->conn);
      if (res == GST_RTSP_EINTR)
        break;
      if (G_UNLIKELY (res == GST_RTSP_EEOF))
        goto eof;
      if (res == GST_RTSP_ETGET) {
        GString *str;
        GstRTSPStatusCode code;
        guint size;

        if (watch->funcs.tunnel_start)
          code = watch->funcs.tunnel_start (watch, watch->user_data);
        else
          code = GST_RTSP_STS_OK;

        /* queue the response string */
        str = gen_tunnel_reply (watch->conn, code);
        size = str->len;
        gst_rtsp_watch_queue_data (watch, (guint8 *) g_string_free (str, FALSE),
            size);
      } else if (res == GST_RTSP_ETPOST) {
        /* in the callback the connection should be tunneled with the
         * GET connection */
        if (watch->funcs.tunnel_complete)
          watch->funcs.tunnel_complete (watch, watch->user_data);
      } else if (G_UNLIKELY (res != GST_RTSP_OK))
        goto error;

      if (G_LIKELY (res == GST_RTSP_OK)) {
        if (watch->funcs.message_received)
          watch->funcs.message_received (watch, &watch->message,
              watch->user_data);

        gst_rtsp_message_unset (&watch->message);
      }
      build_reset (&watch->builder);
    } while (FALSE);
  }

  if (watch->writefd.revents & WRITE_COND) {
    do {
      if (watch->write_data == NULL) {
        GstRTSPRec *rec;

        /* get a new message from the queue */
        rec = g_async_queue_try_pop (watch->messages);
        if (rec == NULL)
          goto done;

        watch->write_off = 0;
        watch->write_data = rec->data;
        watch->write_size = rec->size;
        watch->write_id = rec->id;

        g_slice_free (GstRTSPRec, rec);
      }

      res = write_bytes (watch->writefd.fd, watch->write_data,
          &watch->write_off, watch->write_size);
      if (res == GST_RTSP_EINTR)
        break;
      if (G_UNLIKELY (res != GST_RTSP_OK))
        goto error;

      if (watch->funcs.message_sent)
        watch->funcs.message_sent (watch, watch->write_id, watch->user_data);

    done:
      if (g_async_queue_length (watch->messages) == 0 && watch->write_added) {
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
gst_rtsp_rec_free (gpointer data)
{
  GstRTSPRec *rec = data;

  g_free (rec->data);
  g_slice_free (GstRTSPRec, rec);
}

static void
gst_rtsp_source_finalize (GSource * source)
{
  GstRTSPWatch *watch = (GstRTSPWatch *) source;

  build_reset (&watch->builder);
  gst_rtsp_message_unset (&watch->message);

  g_async_queue_unref (watch->messages);
  watch->messages = NULL;

  g_free (watch->write_data);

  if (watch->notify)
    watch->notify (watch->user_data);
}

static GSourceFuncs gst_rtsp_source_funcs = {
  gst_rtsp_source_prepare,
  gst_rtsp_source_check,
  gst_rtsp_source_dispatch,
  gst_rtsp_source_finalize,
  NULL,
  NULL
};

/**
 * gst_rtsp_watch_new:
 * @conn: a #GstRTSPConnection
 * @funcs: watch functions
 * @user_data: user data to pass to @funcs
 * @notify: notify when @user_data is not referenced anymore
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
  g_return_val_if_fail (conn->readfd != NULL, NULL);
  g_return_val_if_fail (conn->writefd != NULL, NULL);

  result = (GstRTSPWatch *) g_source_new (&gst_rtsp_source_funcs,
      sizeof (GstRTSPWatch));

  result->conn = conn;
  result->builder.state = STATE_START;

  result->messages = g_async_queue_new_full (gst_rtsp_rec_free);

  result->readfd.fd = -1;
  result->writefd.fd = -1;

  gst_rtsp_watch_reset (result);

  result->funcs = *funcs;
  result->user_data = user_data;
  result->notify = notify;

  /* only add the read fd, the write fd is only added when we have data
   * to send. */
  g_source_add_poll ((GSource *) result, &result->readfd);

  return result;
}

/**
 * gst_rtsp_watch_reset:
 * @watch: a #GstRTSPWatch
 *
 * Reset @watch, this is usually called after gst_rtsp_connection_do_tunnel()
 * when the file descriptors of the connection might have changed.
 *
 * Since: 0.10.23
 */
void
gst_rtsp_watch_reset (GstRTSPWatch * watch)
{
  if (watch->readfd.fd != -1)
    g_source_remove_poll ((GSource *) watch, &watch->readfd);
  if (watch->writefd.fd != -1)
    g_source_remove_poll ((GSource *) watch, &watch->writefd);

  watch->readfd.fd = watch->conn->readfd->fd;
  watch->readfd.events = READ_COND;
  watch->readfd.revents = 0;

  watch->writefd.fd = watch->conn->writefd->fd;
  watch->writefd.events = WRITE_COND;
  watch->writefd.revents = 0;
  watch->write_added = FALSE;

  g_source_add_poll ((GSource *) watch, &watch->readfd);
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
 * gst_rtsp_watch_unref:
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
 * gst_rtsp_watch_queue_data:
 * @watch: a #GstRTSPWatch
 * @data: the data to queue
 * @size: the size of @data
 *
 * Queue @data for transmission in @watch. It will be transmitted when the
 * connection of the @watch becomes writable.
 *
 * This function will take ownership of @data and g_free() it after use.
 *
 * The return value of this function will be used as the id argument in the
 * message_sent callback.
 *
 * Returns: an id.
 *
 * Since: 0.10.24
 */
guint
gst_rtsp_watch_queue_data (GstRTSPWatch * watch, const guint8 * data,
    guint size)
{
  GstRTSPRec *rec;

  g_return_val_if_fail (watch != NULL, GST_RTSP_EINVAL);
  g_return_val_if_fail (data != NULL, GST_RTSP_EINVAL);
  g_return_val_if_fail (size != 0, GST_RTSP_EINVAL);

  /* make a record with the data and id */
  rec = g_slice_new (GstRTSPRec);
  rec->data = (guint8 *) data;
  rec->size = size;
  do {
    /* make sure rec->id is never 0 */
    rec->id = ++watch->id;
  } while (G_UNLIKELY (rec->id == 0));

  /* add the record to a queue. FIXME we would like to have an upper limit here */
  g_async_queue_push (watch->messages, rec);

  /* FIXME: does the following need to be made thread-safe? (this might be
   * called from a streaming thread, like appsink's render function) */
  /* make sure the main context will now also check for writability on the
   * socket */
  if (!watch->write_added) {
    g_source_add_poll ((GSource *) watch, &watch->writefd);
    watch->write_added = TRUE;
  }

  return rec->id;
}

/**
 * gst_rtsp_watch_queue_message:
 * @watch: a #GstRTSPWatch
 * @message: a #GstRTSPMessage
 *
 * Queue a @message for transmission in @watch. The contents of this
 * message will be serialized and transmitted when the connection of the
 * @watch becomes writable.
 *
 * The return value of this function will be used as the id argument in the
 * message_sent callback.
 *
 * Returns: an id.
 *
 * Since: 0.10.23
 */
guint
gst_rtsp_watch_queue_message (GstRTSPWatch * watch, GstRTSPMessage * message)
{
  GString *str;
  guint size;

  g_return_val_if_fail (watch != NULL, GST_RTSP_EINVAL);
  g_return_val_if_fail (message != NULL, GST_RTSP_EINVAL);

  /* make a record with the message as a string and id */
  str = message_to_string (watch->conn, message);
  size = str->len;
  return gst_rtsp_watch_queue_data (watch,
      (guint8 *) g_string_free (str, FALSE), size);
}
