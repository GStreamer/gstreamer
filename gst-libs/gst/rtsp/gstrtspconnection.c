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

#ifdef G_OS_WIN32
#define FIONREAD_TYPE gulong
#define IOCTL_SOCKET ioctlsocket
#define CLOSE_SOCKET(sock) closesocket(sock);
#else
#define FIONREAD_TYPE gint
#define IOCTL_SOCKET ioctl
#define CLOSE_SOCKET(sock) close(sock);
#endif

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

/**
 * gst_rtsp_connection_create:
 * @url: a #GstRTSPUrl 
 * @conn: a #GstRTSPConnection
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

  g_return_val_if_fail (conn != NULL, GST_RTSP_EINVAL);

  newconn = g_new0 (GstRTSPConnection, 1);

  if ((newconn->fdset = gst_poll_new (GST_POLL_MODE_AUTO, TRUE)) == NULL)
    goto no_fdset;

  newconn->url = url;
  newconn->fd.fd = -1;
  newconn->timer = g_timer_new ();

  newconn->auth_method = GST_RTSP_AUTH_NONE;
  newconn->username = NULL;
  newconn->passwd = NULL;

  *conn = newconn;

  return GST_RTSP_OK;

  /* ERRORS */
no_fdset:
  {
    g_free (newconn);
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
  unsigned long flags;
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
  if (errno != EINPROGRESS)
    goto sys_error;

  /* wait for connect to complete up to the specified timeout or until we got
   * interrupted. */
  gst_poll_fd_ctl_write (conn->fdset, &conn->fd, TRUE);

  to = timeout ? GST_TIMEVAL_TO_TIME (*timeout) : GST_CLOCK_TIME_NONE;

  do {
    retval = gst_poll_wait (conn->fdset, to);
  } while (retval == -1 && errno == EINTR);

  if (retval == 0)
    goto timeout;
  else if (retval == -1)
    goto sys_error;

done:
  conn->ip = g_strdup (ip);

  return GST_RTSP_OK;

sys_error:
  {
    if (conn->fd.fd >= 0) {
      gst_poll_remove_fd (conn->fdset, &conn->fd);
      conn->fd.fd = -1;
    }
    if (fd >= 0)
      CLOSE_SOCKET (fd);
    return GST_RTSP_ESYS;
  }
not_resolved:
  {
    return GST_RTSP_ENET;
  }
not_ip:
  {
    return GST_RTSP_ENOTIP;
  }
timeout:
  {
    if (conn->fd.fd >= 0) {
      gst_poll_remove_fd (conn->fdset, &conn->fd);
      conn->fd.fd = -1;
    }
    if (fd >= 0)
      CLOSE_SOCKET (fd);
    return GST_RTSP_ETIMEOUT;
  }
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

      gst_rtsp_message_add_header (message, GST_RTSP_HDR_AUTHORIZATION,
          auth_string);

      g_free (user_pass);
      g_free (user_pass64);
      g_free (auth_string);
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
  guint towrite;
  gint retval;
  GstClockTime to;

  g_return_val_if_fail (conn != NULL, GST_RTSP_EINVAL);
  g_return_val_if_fail (data != NULL || size == 0, GST_RTSP_EINVAL);

  gst_poll_set_controllable (conn->fdset, TRUE);
  gst_poll_fd_ctl_write (conn->fdset, &conn->fd, TRUE);
  gst_poll_fd_ctl_read (conn->fdset, &conn->fd, FALSE);

  to = timeout ? GST_TIMEVAL_TO_TIME (*timeout) : GST_CLOCK_TIME_NONE;

  towrite = size;

  while (towrite > 0) {
    gint written;

    do {
      retval = gst_poll_wait (conn->fdset, to);
    } while (retval == -1 && errno == EINTR);

    if (retval == 0)
      goto timeout;

    if (retval == -1) {
      if (errno == EBUSY)
        goto stopped;
      else
        goto select_error;
    }

    /* now we can write */
    written = write (conn->fd.fd, data, towrite);
    if (written < 0) {
      if (errno != EAGAIN && errno != EINTR)
        goto write_error;
    } else {
      towrite -= written;
      data += written;
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
    return GST_RTSP_ESYS;
  }
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

#ifdef G_OS_WIN32
  WSADATA w;
  int error;
#endif

  g_return_val_if_fail (conn != NULL, GST_RTSP_EINVAL);
  g_return_val_if_fail (message != NULL, GST_RTSP_EINVAL);

#ifdef G_OS_WIN32
  error = WSAStartup (0x0202, &w);

  if (error)
    goto startup_error;

  if (w.wVersion != 0x0202)
    goto version_error;
#endif

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
      g_return_val_if_reached (GST_RTSP_EINVAL);
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

  /* write request */
  res =
      gst_rtsp_connection_write (conn, (guint8 *) str->str, str->len, timeout);

  g_string_free (str, TRUE);

  return res;

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
}

static GstRTSPResult
read_line (gint fd, gchar * buffer, guint size)
{
  guint idx;
  gchar c;
  gint r;

  idx = 0;
  while (TRUE) {
    r = read (fd, &c, 1);
    if (r == 0) {
      goto eof;
    } else if (r < 0) {
      if (errno != EAGAIN && errno != EINTR)
        goto read_error;
    } else {
      if (c == '\n')            /* end on \n */
        break;
      if (c == '\r')            /* ignore \r */
        continue;

      if (idx < size - 1)
        buffer[idx++] = c;
    }
  }
  buffer[idx] = '\0';

  return GST_RTSP_OK;

eof:
  {
    return GST_RTSP_EEOF;
  }
read_error:
  {
    return GST_RTSP_ESYS;
  }
}

static void
read_string (gchar * dest, gint size, gchar ** src)
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
read_key (gchar * dest, gint size, gchar ** src)
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
parse_response_status (gchar * buffer, GstRTSPMessage * msg)
{
  GstRTSPResult res;
  gchar versionstr[20];
  gchar codestr[4];
  gint code;
  gchar *bptr;

  bptr = buffer;

  read_string (versionstr, sizeof (versionstr), &bptr);
  read_string (codestr, sizeof (codestr), &bptr);
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
parse_request_line (gchar * buffer, GstRTSPMessage * msg)
{
  GstRTSPResult res = GST_RTSP_OK;
  gchar versionstr[20];
  gchar methodstr[20];
  gchar urlstr[4096];
  gchar *bptr;
  GstRTSPMethod method;

  bptr = buffer;

  read_string (methodstr, sizeof (methodstr), &bptr);
  method = gst_rtsp_find_method (methodstr);

  read_string (urlstr, sizeof (urlstr), &bptr);
  if (*urlstr == '\0')
    res = GST_RTSP_EPARSE;

  read_string (versionstr, sizeof (versionstr), &bptr);

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
parse_line (gchar * buffer, GstRTSPMessage * msg)
{
  gchar key[32];
  gchar *bptr;
  GstRTSPHeaderField field;

  bptr = buffer;

  /* read key */
  read_key (key, sizeof (key), &bptr);
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

/**
 * gst_rtsp_connection_read_internal:
 * @conn: a #GstRTSPConnection
 * @data: the data to read
 * @size: the size of @data
 * @timeout: a timeout value or #NULL
 * @allow_interrupt: can the pending read be interrupted
 *
 * Attempt to read @size bytes into @data from the connected @conn, blocking up to
 * the specified @timeout. @timeout can be #NULL, in which case this function
 * might block forever.
 * 
 * This function can be cancelled with gst_rtsp_connection_flush() only if
 * @allow_interrupt is set.
 *
 * Returns: #GST_RTSP_OK on success.
 */
static GstRTSPResult
gst_rtsp_connection_read_internal (GstRTSPConnection * conn, guint8 * data,
    guint size, GTimeVal * timeout, gboolean allow_interrupt)
{
  guint toread;
  gint retval;
  GstClockTime to;
  FIONREAD_TYPE avail;

  g_return_val_if_fail (conn != NULL, GST_RTSP_EINVAL);
  g_return_val_if_fail (data != NULL, GST_RTSP_EINVAL);

  if (size == 0)
    return GST_RTSP_OK;

  toread = size;

  /* configure timeout if any */
  to = timeout ? GST_TIMEVAL_TO_TIME (*timeout) : GST_CLOCK_TIME_NONE;

  /* if the call fails, just go in the select.. it should not fail. Else if
   * there is enough data to read, skip the select call al together.*/
  if (IOCTL_SOCKET (conn->fd.fd, FIONREAD, &avail) < 0)
    avail = 0;
  else if (avail >= toread)
    goto do_read;

  gst_poll_set_controllable (conn->fdset, allow_interrupt);
  gst_poll_fd_ctl_write (conn->fdset, &conn->fd, FALSE);
  gst_poll_fd_ctl_read (conn->fdset, &conn->fd, TRUE);

  while (toread > 0) {
    gint bytes;

    do {
      retval = gst_poll_wait (conn->fdset, to);
    } while (retval == -1 && errno == EINTR);

    if (retval == -1) {
      if (errno == EBUSY)
        goto stopped;
      else
        goto select_error;
    }

    /* check for timeout */
    if (retval == 0)
      goto select_timeout;

  do_read:
    /* if we get here there is activity on the real fd since the select
     * completed and the control socket was not readable. */
    bytes = read (conn->fd.fd, data, toread);

    if (bytes == 0) {
      goto eof;
    } else if (bytes < 0) {
      if (errno != EAGAIN && errno != EINTR)
        goto read_error;
    } else {
      toread -= bytes;
      data += bytes;
    }
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
    return GST_RTSP_ESYS;
  }
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
  return gst_rtsp_connection_read_internal (conn, data, size, timeout, TRUE);
}


static GstRTSPResult
read_body (GstRTSPConnection * conn, glong content_length, GstRTSPMessage * msg,
    GTimeVal * timeout)
{
  guint8 *body;
  GstRTSPResult res;

  if (content_length <= 0) {
    body = NULL;
    content_length = 0;
    goto done;
  }

  body = g_malloc (content_length + 1);
  body[content_length] = '\0';

  GST_RTSP_CHECK (gst_rtsp_connection_read_internal (conn, body, content_length,
          timeout, FALSE), read_error);

  content_length += 1;

done:
  gst_rtsp_message_take_body (msg, (guint8 *) body, content_length);

  return GST_RTSP_OK;

  /* ERRORS */
read_error:
  {
    g_free (body);
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
  gchar buffer[4096];
  gint line;
  glong content_length;
  GstRTSPResult res;
  gboolean need_body;

  g_return_val_if_fail (conn != NULL, GST_RTSP_EINVAL);
  g_return_val_if_fail (message != NULL, GST_RTSP_EINVAL);

  line = 0;

  need_body = TRUE;

  res = GST_RTSP_OK;
  /* parse first line and headers */
  while (res == GST_RTSP_OK) {
    guint8 c;

    /* read first character, this identifies data messages */
    /* This is the only read() that we allow to be interrupted */
    GST_RTSP_CHECK (gst_rtsp_connection_read_internal (conn, &c, 1, timeout,
            TRUE), read_error);

    /* check for data packet, first character is $ */
    if (c == '$') {
      guint16 size;

      /* data packets are $<1 byte channel><2 bytes length,BE><data bytes> */

      /* read channel, which is the next char */
      GST_RTSP_CHECK (gst_rtsp_connection_read_internal (conn, &c, 1, timeout,
              FALSE), read_error);

      /* now we create a data message */
      gst_rtsp_message_init_data (message, c);

      /* next two bytes are the length of the data */
      GST_RTSP_CHECK (gst_rtsp_connection_read_internal (conn,
              (guint8 *) & size, 2, timeout, FALSE), read_error);

      size = GUINT16_FROM_BE (size);

      /* and read the body */
      res = read_body (conn, size, message, timeout);
      need_body = FALSE;
      break;
    } else {
      gint offset = 0;

      /* we have a regular response */
      if (c != '\r') {
        buffer[0] = c;
        offset = 1;
      }
      /* should not happen */
      if (c == '\n')
        break;

      /* read lines */
      GST_RTSP_CHECK (read_line (conn->fd.fd, buffer + offset,
              sizeof (buffer) - offset), read_error);

      if (buffer[0] == '\0')
        break;

      if (line == 0) {
        /* first line, check for response status */
        if (g_str_has_prefix (buffer, "RTSP")) {
          res = parse_response_status (buffer, message);
        } else {
          res = parse_request_line (buffer, message);
        }
      } else {
        /* else just parse the line */
        parse_line (buffer, message);
      }
    }
    line++;
  }

  /* read the rest of the body if needed */
  if (need_body) {
    gchar *session_id;
    gchar *hdrval;

    /* see if there is a Content-Length header */
    if (gst_rtsp_message_get_header (message, GST_RTSP_HDR_CONTENT_LENGTH,
            &hdrval, 0) == GST_RTSP_OK) {
      /* there is, read the body */
      content_length = atol (hdrval);
      GST_RTSP_CHECK (read_body (conn, content_length, message, timeout),
          read_error);
    }

    /* save session id in the connection for further use */
    if (gst_rtsp_message_get_header (message, GST_RTSP_HDR_SESSION,
            &session_id, 0) == GST_RTSP_OK) {
      gint maxlen, i;

      /* default session timeout */
      conn->timeout = 60;

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
  }
  return res;

read_error:
  {
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
#ifdef G_OS_WIN32
    WSACleanup ();
#endif
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
#ifdef G_OS_WIN32
  WSACleanup ();
#endif
  g_timer_destroy (conn->timer);
  g_free (conn->username);
  g_free (conn->passwd);
  g_free (conn);

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

  gst_poll_set_controllable (conn->fdset, TRUE);

  /* add fd to writer set when asked to */
  gst_poll_fd_ctl_write (conn->fdset, &conn->fd, events & GST_RTSP_EV_WRITE);

  /* add fd to reader set when asked to */
  gst_poll_fd_ctl_read (conn->fdset, &conn->fd, events & GST_RTSP_EV_READ);

  /* configure timeout if any */
  to = timeout ? GST_TIMEVAL_TO_TIME (*timeout) : GST_CLOCK_TIME_NONE;

  do {
    retval = gst_poll_wait (conn->fdset, to);
  } while (retval == -1 && errno == EINTR);

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
  /* Digest isn't implemented yet */
  if (method == GST_RTSP_AUTH_DIGEST)
    return GST_RTSP_ENOTIMPL;

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
