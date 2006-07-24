/* GStreamer
 * Copyright (C) <2005> Wim Taymans <wim@fluendo.com>
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

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

/* we include this here to get the G_OS_* defines */
#include <glib.h>

#ifdef G_OS_WIN32
#include <winsock2.h>
#else
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif


#include "rtspconnection.h"

#ifdef G_OS_WIN32
#define CLOSE_SOCKET(sock) closesocket(sock);
#else
#define CLOSE_SOCKET(sock) close(sock);
#endif

#ifdef G_OS_WIN32
/* note that inet_aton is deprecated on unix because
 * inet_addr returns -1 (INADDR_NONE) for the valid 255.255.255.255
 * address. */
static int
inet_aton (const char *c, struct in_addr *paddr)
{
  paddr->s_addr = inet_addr (c);

  if (paddr->s_addr == INADDR_NONE)
    return 0;

  return 1;
}
#endif

RTSPResult
rtsp_connection_open (RTSPUrl * url, RTSPConnection ** conn)
{
  gint fd;
  struct sockaddr_in sin;
  struct hostent *hostinfo;
  char **addrs;
  gchar *ip;
  struct in_addr addr;
  gint ret;

  if (url == NULL || conn == NULL)
    return RTSP_EINVAL;

  if (url->protocol != RTSP_PROTO_TCP)
    return RTSP_ENOTIMPL;

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

  fd = socket (AF_INET, SOCK_STREAM, 0);
  if (fd == -1)
    goto sys_error;

  memset (&sin, 0, sizeof (sin));
  sin.sin_family = AF_INET;     /* network socket */
  sin.sin_port = htons (url->port);     /* on port */
  sin.sin_addr.s_addr = inet_addr (ip); /* on host ip */

  ret = connect (fd, (struct sockaddr *) &sin, sizeof (sin));
  if (ret != 0)
    goto sys_error;

  return rtsp_connection_create (fd, conn);

sys_error:
  {
    return RTSP_ESYS;
  }
not_resolved:
  {
    g_warning ("could not resolve host \"%s\"\n", url->host);
    return RTSP_ESYS;
  }
not_ip:
  {
    g_warning ("host \"%s\" is not IP\n", url->host);
    return RTSP_ESYS;
  }
}

RTSPResult
rtsp_connection_create (gint fd, RTSPConnection ** conn)
{
  RTSPConnection *newconn;

  /* FIXME check fd, must be connected SOCK_STREAM */

  newconn = g_new (RTSPConnection, 1);

  newconn->fd = fd;
  newconn->cseq = 0;
  newconn->session_id[0] = 0;
  newconn->state = RTSP_STATE_INIT;

  *conn = newconn;

  return RTSP_OK;
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

  if (conn == NULL || message == NULL)
    return RTSP_EINVAL;

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

  rtsp_message_init_response (code, bptr, NULL, msg);

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

  rtsp_message_init_request (method, urlstr, msg);

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

static RTSPResult
read_body (gint fd, glong content_length, RTSPMessage * msg)
{
  gchar *body, *bodyptr;
  gint to_read, r;

  if (content_length <= 0) {
    body = NULL;
    content_length = 0;
    goto done;
  }

  body = g_malloc (content_length + 1);
  body[content_length] = '\0';
  bodyptr = body;
  to_read = content_length;
  while (to_read > 0) {
    r = read (fd, bodyptr, to_read);
    if (r < 0) {
      if (errno != EAGAIN && errno != EINTR)
        goto read_error;
    } else {
      to_read -= r;
      bodyptr += r;
    }
  }
  content_length += 1;

done:
  rtsp_message_set_body (msg, (guint8 *) body, content_length);

  return RTSP_OK;

read_error:
  {
    g_free (body);
    return RTSP_ESYS;
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

  if (conn == NULL || msg == NULL)
    return RTSP_EINVAL;

  line = 0;

  need_body = TRUE;

  res = RTSP_OK;
  /* parse first line and headers */
  while (res == RTSP_OK) {
    gchar c;
    gint ret;

    /* read first character, this identifies data messages */
    ret = read (conn->fd, &c, 1);
    if (ret < 0)
      goto read_error;
    if (ret < 1)
      break;

    /* check for data packet, first character is $ */
    if (c == '$') {
      guint16 size;

      /* data packets are $<1 byte channel><2 bytes length,BE><data bytes> */

      /* read channel, which is the next char */
      ret = read (conn->fd, &c, 1);
      if (ret < 0)
        goto read_error;
      if (ret < 1)
        goto error;

      /* now we create a data message */
      rtsp_message_init_data ((gint) c, msg);

      /* next two bytes are the length of the data */
      ret = read (conn->fd, &size, 2);
      if (ret < 0)
        goto read_error;
      if (ret < 2)
        goto error;

      size = GUINT16_FROM_BE (size);

      /* and read the body */
      res = read_body (conn->fd, size, msg);
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
      res = read_line (conn->fd, buffer + offset, sizeof (buffer) - offset);
      if (res != RTSP_OK)
        goto read_error;

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
      res = read_body (conn->fd, content_length, msg);
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

error:
  {
    return RTSP_EPARSE;
  }
read_error:
  {
    return RTSP_ESYS;
  }
}

RTSPResult
rtsp_connection_close (RTSPConnection * conn)
{
  gint res;

  if (conn == NULL)
    return RTSP_EINVAL;

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
  if (conn == NULL)
    return RTSP_EINVAL;

#ifdef G_OS_WIN32
  WSACleanup ();
#endif

  g_free (conn);

  return RTSP_OK;
}
