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

#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "rtspconnection.h"

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

  /* FIXME check fd */

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

  if (conn == NULL || message == NULL)
    return RTSP_EINVAL;

  str = g_string_new ("");

  g_string_append_printf (str, "%s %s RTSP/1.0\r\n"
      "CSeq: %d\r\n",
      rtsp_method_as_text (message->type_data.request.method),
      message->type_data.request.uri, conn->cseq);

  if (conn->session_id[0] != '\0') {
    rtsp_message_add_header (message, RTSP_HDR_SESSION, conn->session_id);
  }

  g_hash_table_foreach (message->hdr_fields, (GHFunc) append_header, str);


  g_string_append (str, "\r\n");

  write (conn->fd, str->str, str->len);
  g_string_free (str, TRUE);

  return RTSP_OK;
}

static RTSPResult
read_line (gint fd, gchar * buffer, guint size)
{
  gint idx;
  gchar c;
  gint ret;

  idx = 0;
  while (TRUE) {
    ret = read (fd, &c, 1);
    if (ret < 1)
      goto error;

    if (c == '\n')              /* end on \n */
      break;
    if (c == '\r')              /* ignore \r */
      continue;

    if (idx < size - 1)
      buffer[idx++] = c;
  }
  buffer[idx] = '\0';

  return RTSP_OK;

error:
  {
    perror ("read");
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
parse_line (gchar * buffer, RTSPMessage * msg)
{
  gchar key[32];
  gchar *bptr;
  RTSPHeaderField field;

  bptr = buffer;

  read_key (key, sizeof (key), &bptr);
  if (*bptr != ':')
    return RTSP_EINVAL;

  bptr++;

  field = rtsp_find_header_field (key);
  if (field == -1) {
    g_warning ("unknown header field '%s'\n", key);
  } else {
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
    rtsp_message_set_body (msg, NULL, 0);
    return RTSP_OK;
  }

  body = g_malloc (content_length);
  bodyptr = body;
  to_read = content_length;
  while (to_read > 0) {
    r = read (fd, bodyptr, to_read);

    to_read -= r;
    bodyptr += r;
  }

  rtsp_message_set_body (msg, body, content_length);

  return RTSP_OK;
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

  /* parse first line and headers */
  while (TRUE) {
    gchar c;
    gint ret;

    ret = read (conn->fd, &c, 1);
    if (ret < 0)
      goto read_error;
    if (ret < 1)
      break;

    /* check for data packet */
    if (c == '$') {
      guint16 size;

      /* read channel */
      ret = read (conn->fd, &c, 1);
      if (ret < 0)
        goto read_error;
      if (ret < 1)
        goto error;

      rtsp_message_init_data ((gint) c, msg);

      ret = read (conn->fd, &size, 2);
      if (ret < 0)
        goto read_error;
      if (ret < 2)
        goto error;

      size = GUINT16_FROM_BE (size);

      read_body (conn->fd, size, msg);
      need_body = FALSE;
      break;
    } else {
      gint offset = 0;

      if (c != '\r') {
        buffer[0] = c;
        offset = 1;
      }
      /* should not happen */
      if (c == '\n')
        break;

      read_line (conn->fd, buffer + offset, sizeof (buffer) - offset);

      if (buffer[0] == '\0')
        break;

      if (line == 0) {
        if (g_str_has_prefix (buffer, "RTSP")) {
          parse_response_status (buffer, msg);
        } else {
          g_warning ("parsing request not implemented\n");
          goto error;
        }
      } else {
        parse_line (buffer, msg);
      }
    }
    line++;
  }

  if (need_body) {
    /* parse body */
    res = rtsp_message_get_header (msg, RTSP_HDR_CONTENT_LENGTH, &hdrval);
    if (res == RTSP_OK) {
      content_length = atol (hdrval);
      read_body (conn->fd, content_length, msg);
    }

    /* save session id */
    {
      gchar *session_id;

      if (rtsp_message_get_header (msg, RTSP_HDR_SESSION,
              &session_id) == RTSP_OK) {
        strncpy (conn->session_id, session_id, sizeof (conn->session_id) - 1);
        conn->session_id[sizeof (conn->session_id) - 1] = '\0';
      }
    }
  }

  return RTSP_OK;

error:
  {
    return RTSP_EPARSE;
  }
read_error:
  {
    perror ("read");
    return RTSP_ESYS;
  }
}

RTSPResult
rtsp_connection_close (RTSPConnection * conn)
{
  gint res;

  if (conn == NULL)
    return RTSP_EINVAL;

  res = close (conn->fd);
  conn->fd = -1;
  if (res != 0)
    goto sys_error;

  return RTSP_OK;

sys_error:
  {
    return RTSP_ESYS;
  }
}
