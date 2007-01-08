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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

/* we include this here to get the G_OS_* defines */
#include <glib.h>

#ifdef G_OS_WIN32
#include <winsock2.h>
#else
#include <sys/ioctl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#ifdef HAVE_FIONREAD_IN_SYS_FILIO
#include <sys/filio.h>
#endif

#include "rtspconnection.h"

/* the select call is also performed on the control sockets, that way
 * we can send special commands to unlock or restart the select call */
#define CONTROL_RESTART        'R'      /* restart the select call */
#define CONTROL_STOP           'S'      /* stop the select call */
#define CONTROL_SOCKETS(conn)  conn->control_sock
#define WRITE_SOCKET(conn)     conn->control_sock[1]
#define READ_SOCKET(conn)      conn->control_sock[0]

#define SEND_COMMAND(conn, command)              \
G_STMT_START {                                  \
  unsigned char c; c = command;                 \
  write (WRITE_SOCKET(conn), &c, 1);             \
} G_STMT_END

#define READ_COMMAND(conn, command, res)         \
G_STMT_START {                                  \
  res = read(READ_SOCKET(conn), &command, 1);    \
} G_STMT_END

#ifdef G_OS_WIN32
#define IOCTL_SOCKET ioctlsocket
#define CLOSE_SOCKET(sock) closesocket(sock);
#else
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

RTSPResult
rtsp_connection_create (RTSPUrl * url, RTSPConnection ** conn)
{
  gint ret;
  RTSPConnection *newconn;

  g_return_val_if_fail (url != NULL, RTSP_EINVAL);
  g_return_val_if_fail (conn != NULL, RTSP_EINVAL);

  newconn = g_new (RTSPConnection, 1);

#ifdef G_OS_WIN32
  /* This should work on UNIX too. PF_UNIX sockets replaced with pipe */
  /* pipe( CONTROL_SOCKETS(newconn) ) */
  if ((ret = pipe (CONTROL_SOCKETS (newconn))) < 0)
    goto no_socket_pair;
#else
  if ((ret =
          socketpair (PF_UNIX, SOCK_STREAM, 0, CONTROL_SOCKETS (newconn))) < 0)
    goto no_socket_pair;

  fcntl (READ_SOCKET (newconn), F_SETFL, O_NONBLOCK);
  fcntl (WRITE_SOCKET (newconn), F_SETFL, O_NONBLOCK);
#endif

  newconn->url = url;
  newconn->cseq = 0;
  newconn->session_id[0] = 0;
  newconn->state = RTSP_STATE_INIT;

  *conn = newconn;

  return RTSP_OK;

  /* ERRORS */
no_socket_pair:
  {
    g_free (newconn);
    return RTSP_ESYS;
  }
}

RTSPResult
rtsp_connection_connect (RTSPConnection * conn)
{
  gint fd;
  struct sockaddr_in sin;
  struct hostent *hostinfo;
  char **addrs;
  gchar *ip;
  struct in_addr addr;
  gint ret;
  guint16 port;
  RTSPUrl *url;

  g_return_val_if_fail (conn != NULL, RTSP_EINVAL);

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

    addrs = hostinfo->h_addr_list;
    ip = inet_ntoa (*(struct in_addr *) *addrs);
  }

  /* get the port from the url */
  rtsp_url_get_port (url, &port);

  memset (&sin, 0, sizeof (sin));
  sin.sin_family = AF_INET;     /* network socket */
  sin.sin_port = htons (port);  /* on port */
  sin.sin_addr.s_addr = inet_addr (ip); /* on host ip */

  fd = socket (AF_INET, SOCK_STREAM, 0);
  if (fd == -1)
    goto sys_error;

  ret = connect (fd, (struct sockaddr *) &sin, sizeof (sin));
  if (ret != 0)
    goto sys_error;

  conn->fd = fd;

  return RTSP_OK;

sys_error:
  {
    return RTSP_ESYS;
  }
not_resolved:
  {
    return RTSP_ENET;
  }
not_ip:
  {
    return RTSP_ENOTIP;
  }
}

static void
append_header (gint key, gchar * value, GString * str)
{
  const gchar *keystr = rtsp_header_as_text (key);

  g_string_append_printf (str, "%s: %s\r\n", keystr, value);
}

RTSPResult
rtsp_connection_send (RTSPConnection * conn, RTSPMessage * message)
{
  GString *str;
  gint towrite;
  gchar *data;

  g_return_val_if_fail (conn != NULL, RTSP_EINVAL);
  g_return_val_if_fail (message != NULL, RTSP_EINVAL);

#ifdef G_OS_WIN32
  WSADATA w;
  int error = WSAStartup (0x0202, &w);

  if (error)
    goto startup_error;

  if (w.wVersion != 0x0202)
    goto version_error;
#endif

  str = g_string_new ("");

  /* create request string, add CSeq */
  g_string_append_printf (str, "%s %s RTSP/1.0\r\n"
      "CSeq: %d\r\n",
      rtsp_method_as_text (message->type_data.request.method),
      message->type_data.request.uri, conn->cseq);

  /* append session id if we have one */
  if (conn->session_id[0] != '\0') {
    rtsp_message_add_header (message, RTSP_HDR_SESSION, conn->session_id);
  }

  /* append headers */
  g_hash_table_foreach (message->hdr_fields, (GHFunc) append_header, str);

  /* append Content-Length and body if needed */
  if (message->body != NULL && message->body_size > 0) {
    gchar *len;

    len = g_strdup_printf ("%d", message->body_size);
    append_header (RTSP_HDR_CONTENT_LENGTH, len, str);
    g_free (len);
    /* header ends here */
    g_string_append (str, "\r\n");
    str =
        g_string_append_len (str, (gchar *) message->body, message->body_size);
  } else {
    /* just end headers */
    g_string_append (str, "\r\n");
  }

  /* write request */
  towrite = str->len;
  data = str->str;

  while (towrite > 0) {
    gint written;

    written = write (conn->fd, data, towrite);
    if (written < 0) {
      if (errno != EAGAIN && errno != EINTR)
        goto write_error;
    } else {
      towrite -= written;
      data += written;
    }
  }
  g_string_free (str, TRUE);

  conn->cseq++;

  return RTSP_OK;

#ifdef G_OS_WIN32
startup_error:
  {
    g_warning ("Error %d on WSAStartup", error);
    return RTSP_EWSASTART;
  }
version_error:
  {
    g_warning ("Windows sockets are not version 0x202 (current 0x%x)",
        w.wVersion);
    WSACleanup ();
    return RTSP_EWSAVERSION;
  }
#endif
write_error:
  {
    g_string_free (str, TRUE);
    return RTSP_ESYS;
  }
}

static RTSPResult
read_line (gint fd, gchar * buffer, guint size)
{
  gint idx;
  gchar c;
  gint r;

  idx = 0;
  while (TRUE) {
    r = read (fd, &c, 1);
    if (r < 1) {
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

  return RTSP_OK;

read_error:
  {
    return RTSP_ESYS;
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

static RTSPResult
parse_response_status (gchar * buffer, RTSPMessage * msg)
{
  gchar versionstr[20];
  gchar codestr[4];
  gint code;
  gchar *bptr;

  bptr = buffer;

  read_string (versionstr, sizeof (versionstr), &bptr);
  if (strcmp (versionstr, "RTSP/1.0") != 0)
    goto wrong_version;

  read_string (codestr, sizeof (codestr), &bptr);
  code = atoi (codestr);

  while (g_ascii_isspace (*bptr))
    bptr++;

  rtsp_message_init_response (msg, code, bptr, NULL);

  return RTSP_OK;

wrong_version:
  {
    return RTSP_EINVAL;
  }
}

static RTSPResult
parse_request_line (gchar * buffer, RTSPMessage * msg)
{
  gchar versionstr[20];
  gchar methodstr[20];
  gchar urlstr[4096];
  gchar *bptr;
  RTSPMethod method;

  bptr = buffer;

  read_string (methodstr, sizeof (methodstr), &bptr);
  method = rtsp_find_method (methodstr);
  if (method == -1)
    goto wrong_method;

  read_string (urlstr, sizeof (urlstr), &bptr);

  read_string (versionstr, sizeof (versionstr), &bptr);
  if (strcmp (versionstr, "RTSP/1.0") != 0)
    goto wrong_version;

  rtsp_message_init_request (msg, method, urlstr);

  return RTSP_OK;

wrong_method:
  {
    return RTSP_EINVAL;
  }
wrong_version:
  {
    return RTSP_EINVAL;
  }
}

/* parsing lines means reading a Key: Value pair */
static RTSPResult
parse_line (gchar * buffer, RTSPMessage * msg)
{
  gchar key[32];
  gchar *bptr;
  RTSPHeaderField field;

  bptr = buffer;

  /* read key */
  read_key (key, sizeof (key), &bptr);
  if (*bptr != ':')
    return RTSP_EINVAL;

  bptr++;

  field = rtsp_find_header_field (key);
  if (field != -1) {
    while (g_ascii_isspace (*bptr))
      bptr++;
    rtsp_message_add_header (msg, field, bptr);
  }

  return RTSP_OK;
}

RTSPResult
rtsp_connection_read (RTSPConnection * conn, gpointer data, guint size)
{
  fd_set readfds;
  guint toread;
  gint retval;

#ifndef G_OS_WIN32
  gint avail;
#else
  gulong avail;
#endif

  g_return_val_if_fail (conn != NULL, RTSP_EINVAL);
  g_return_val_if_fail (data != NULL, RTSP_EINVAL);

  if (size == 0)
    return RTSP_OK;

  toread = size;

  /* if the call fails, just go in the select.. it should not fail. Else if
   * there is enough data to read, skip the select call al together.*/
  if (IOCTL_SOCKET (conn->fd, FIONREAD, &avail) < 0)
    avail = 0;
  else if (avail >= toread)
    goto do_read;

  FD_ZERO (&readfds);
  FD_SET (conn->fd, &readfds);
  FD_SET (READ_SOCKET (conn), &readfds);

  while (toread > 0) {
    gint bytes;

    do {
      retval = select (FD_SETSIZE, &readfds, NULL, NULL, NULL);
    } while ((retval == -1 && errno == EINTR));

    if (retval == -1)
      goto select_error;

    if (FD_ISSET (READ_SOCKET (conn), &readfds)) {
      /* read all stop commands */
      while (TRUE) {
        gchar command;
        int res;

        READ_COMMAND (conn, command, res);
        if (res <= 0) {
          /* no more commands */
          break;
        }
      }
      goto stopped;
    }

  do_read:
    /* if we get here there is activity on the real fd since the select
     * completed and the control socket was not readable. */
    bytes = read (conn->fd, data, toread);

    if (bytes == 0) {
      goto eof;
    } else if (bytes < 0) {
      if (errno != EAGAIN && errno != EINTR)
        goto read_error;
    } else {
      toread -= bytes;
      data = (char *) data + bytes;
    }
  }
  return RTSP_OK;

  /* ERRORS */
select_error:
  {
    return RTSP_ESYS;
  }
stopped:
  {
    return RTSP_EINTR;
  }
eof:
  {
    return RTSP_EEOF;
  }
read_error:
  {
    return RTSP_ESYS;
  }
}

static RTSPResult
read_body (RTSPConnection * conn, glong content_length, RTSPMessage * msg)
{
  gchar *body;
  RTSPResult res;

  if (content_length <= 0) {
    body = NULL;
    content_length = 0;
    goto done;
  }

  body = g_malloc (content_length + 1);
  body[content_length] = '\0';

  RTSP_CHECK (rtsp_connection_read (conn, body, content_length), read_error);

  content_length += 1;

done:
  rtsp_message_take_body (msg, (guint8 *) body, content_length);

  return RTSP_OK;

  /* ERRORS */
read_error:
  {
    g_free (body);
    return res;
  }
}

RTSPResult
rtsp_connection_receive (RTSPConnection * conn, RTSPMessage * msg)
{
  gchar buffer[4096];
  gint line;
  gchar *hdrval;
  glong content_length;
  RTSPResult res;
  gboolean need_body;

  g_return_val_if_fail (conn != NULL, RTSP_EINVAL);
  g_return_val_if_fail (msg != NULL, RTSP_EINVAL);

  line = 0;

  need_body = TRUE;

  res = RTSP_OK;
  /* parse first line and headers */
  while (res == RTSP_OK) {
    gchar c;

    /* read first character, this identifies data messages */
    RTSP_CHECK (rtsp_connection_read (conn, &c, 1), read_error);

    /* check for data packet, first character is $ */
    if (c == '$') {
      guint16 size;

      /* data packets are $<1 byte channel><2 bytes length,BE><data bytes> */

      /* read channel, which is the next char */
      RTSP_CHECK (rtsp_connection_read (conn, &c, 1), read_error);

      /* now we create a data message */
      rtsp_message_init_data (msg, (gint) c);

      /* next two bytes are the length of the data */
      RTSP_CHECK (rtsp_connection_read (conn, &size, 2), read_error);

      size = GUINT16_FROM_BE (size);

      /* and read the body */
      res = read_body (conn, size, msg);
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
      RTSP_CHECK (read_line (conn->fd, buffer + offset,
              sizeof (buffer) - offset), read_error);

      if (buffer[0] == '\0')
        break;

      if (line == 0) {
        /* first line, check for response status */
        if (g_str_has_prefix (buffer, "RTSP")) {
          res = parse_response_status (buffer, msg);
        } else {
          res = parse_request_line (buffer, msg);
        }
      } else {
        /* else just parse the line */
        parse_line (buffer, msg);
      }
    }
    line++;
  }

  /* read the rest of the body if needed */
  if (need_body) {
    /* see if there is a Content-Length header */
    if (rtsp_message_get_header (msg, RTSP_HDR_CONTENT_LENGTH,
            &hdrval) == RTSP_OK) {
      /* there is, read the body */
      content_length = atol (hdrval);
      RTSP_CHECK (read_body (conn, content_length, msg), read_error);
    }

    /* save session id in the connection for further use */
    {
      gchar *session_id;

      if (rtsp_message_get_header (msg, RTSP_HDR_SESSION,
              &session_id) == RTSP_OK) {
        gint sesslen, maxlen, i;

        sesslen = strlen (session_id);
        maxlen = sizeof (conn->session_id) - 1;
        /* the sessionid can have attributes marked with ;
         * Make sure we strip them */
        for (i = 0; i < sesslen; i++) {
          if (session_id[i] == ';')
            maxlen = i;
        }

        /* make sure to not overflow */
        strncpy (conn->session_id, session_id, maxlen);
        conn->session_id[maxlen] = '\0';
      }
    }
  }
  return res;

read_error:
  {
    return res;
  }
}

RTSPResult
rtsp_connection_close (RTSPConnection * conn)
{
  gint res;

  g_return_val_if_fail (conn != NULL, RTSP_EINVAL);

  res = CLOSE_SOCKET (conn->fd);
#ifdef G_OS_WIN32
  WSACleanup ();
#endif
  conn->fd = -1;
  if (res != 0)
    goto sys_error;

  return RTSP_OK;

sys_error:
  {
    return RTSP_ESYS;
  }
}

RTSPResult
rtsp_connection_free (RTSPConnection * conn)
{
  g_return_val_if_fail (conn != NULL, RTSP_EINVAL);

#ifdef G_OS_WIN32
  WSACleanup ();
#endif

  g_free (conn);

  return RTSP_OK;
}

RTSPResult
rtsp_connection_flush (RTSPConnection * conn, gboolean flush)
{
  g_return_val_if_fail (conn != NULL, RTSP_EINVAL);

  if (flush) {
    SEND_COMMAND (conn, CONTROL_STOP);
  } else {
    while (TRUE) {
      gchar command;
      int res;

      READ_COMMAND (conn, command, res);
      if (res <= 0) {
        /* no more commands */
        break;
      }
    }
  }
  return RTSP_OK;
}
