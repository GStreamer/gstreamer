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

#ifndef __RTSP_CONNECTION_H__
#define __RTSP_CONNECTION_H__

#include <glib.h>

#include <rtspdefs.h>
#include <rtspurl.h>
#include <rtspmessage.h>

G_BEGIN_DECLS

typedef struct _RTSPConnection
{
  /* URL for the connection */
  RTSPUrl *url;

  /* connection state */
  gint fd;
  gint control_sock[2];

  /* Session state */
  gint          cseq;                   /* sequence number */
  gchar         session_id[512];        /* session id */

  /* Authentication */
  RTSPAuthMethod auth_method;
  gchar *username;
  gchar *passwd;
} RTSPConnection;

/* opening/closing a connection */
RTSPResult      rtsp_connection_create  (RTSPUrl *url, RTSPConnection **conn);
RTSPResult      rtsp_connection_connect (RTSPConnection *conn);
RTSPResult      rtsp_connection_close   (RTSPConnection *conn);
RTSPResult      rtsp_connection_free    (RTSPConnection *conn);

/* sending/receiving messages */
RTSPResult      rtsp_connection_send    (RTSPConnection *conn, RTSPMessage *message);
RTSPResult      rtsp_connection_receive (RTSPConnection *conn, RTSPMessage *message);

RTSPResult      rtsp_connection_flush   (RTSPConnection *conn, gboolean flush);

/* Configure Authentication data */
RTSPResult      rtsp_connection_set_auth (RTSPConnection *conn, 
                    RTSPAuthMethod method, gchar *user, gchar *pass);

G_END_DECLS

#endif /* __RTSP_CONNECTION_H__ */
