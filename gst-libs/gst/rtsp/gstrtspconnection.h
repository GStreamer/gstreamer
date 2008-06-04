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

#ifndef __GST_RTSP_CONNECTION_H__
#define __GST_RTSP_CONNECTION_H__

#include <glib.h>

#include <gst/rtsp/gstrtspdefs.h>
#include <gst/rtsp/gstrtspurl.h>
#include <gst/rtsp/gstrtspmessage.h>

G_BEGIN_DECLS

typedef struct _GstRTSPConnection GstRTSPConnection;

/**
 * GstRTSPConnection:
 *
 * Opaque RTSP connection object.
 */
struct _GstRTSPConnection
{
  /*< private >*/
  /* URL for the connection */
  GstRTSPUrl *url;

  /* connection state */
  GstPollFD   fd;
  GstPoll    *fdset;
  gchar      *ip;

  /* Session state */
  gint          cseq;                   /* sequence number */
  gchar         session_id[512];        /* session id */
  gint          timeout;                /* session timeout in seconds */
  GTimer       *timer;                  /* timeout timer */

  /* Authentication */
  GstRTSPAuthMethod  auth_method;
  gchar             *username;
  gchar             *passwd;
  GHashTable        *auth_params;
};

/* opening/closing a connection */
GstRTSPResult      gst_rtsp_connection_create        (GstRTSPUrl *url, GstRTSPConnection **conn);
GstRTSPResult      gst_rtsp_connection_connect       (GstRTSPConnection *conn, GTimeVal *timeout);
GstRTSPResult      gst_rtsp_connection_close         (GstRTSPConnection *conn);
GstRTSPResult      gst_rtsp_connection_free          (GstRTSPConnection *conn);

/* sending/receiving raw bytes */
GstRTSPResult      gst_rtsp_connection_read          (GstRTSPConnection * conn, guint8 * data,
                                                      guint size, GTimeVal * timeout);
GstRTSPResult      gst_rtsp_connection_write         (GstRTSPConnection * conn, const guint8 * data, 
		                                      guint size, GTimeVal * timeout);

/* sending/receiving messages */
GstRTSPResult      gst_rtsp_connection_send          (GstRTSPConnection *conn, GstRTSPMessage *message,
                                                      GTimeVal *timeout);
GstRTSPResult      gst_rtsp_connection_receive       (GstRTSPConnection *conn, GstRTSPMessage *message,
                                                      GTimeVal *timeout);

/* status management */
GstRTSPResult      gst_rtsp_connection_poll          (GstRTSPConnection *conn, GstRTSPEvent events,
                                                      GstRTSPEvent *revents, GTimeVal *timeout);

/* reset the timeout */
GstRTSPResult      gst_rtsp_connection_next_timeout  (GstRTSPConnection *conn, GTimeVal *timeout);
GstRTSPResult      gst_rtsp_connection_reset_timeout (GstRTSPConnection *conn);

/* flushing state */
GstRTSPResult      gst_rtsp_connection_flush         (GstRTSPConnection *conn, gboolean flush);

/* configure authentication data */
GstRTSPResult      gst_rtsp_connection_set_auth      (GstRTSPConnection *conn, GstRTSPAuthMethod method,
                                                      const gchar *user, const gchar *pass);

void               gst_rtsp_connection_set_auth_param    (GstRTSPConnection *conn,
		                                          const gchar * param,
							  const gchar *value);
void               gst_rtsp_connection_clear_auth_params (GstRTSPConnection *conn);

/* configure DSCP */
GstRTSPResult      gst_rtsp_connection_set_qos_dscp  (GstRTSPConnection *conn,
                                                      guint qos_dscp);

/* accessors */
const gchar *      gst_rtsp_connection_get_ip        (const GstRTSPConnection *conn);

G_END_DECLS

#endif /* __GST_RTSP_CONNECTION_H__ */
