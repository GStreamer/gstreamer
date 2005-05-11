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

#ifndef __RTSP_CONNECTION_H__
#define __RTSP_CONNECTION_H__

#include <glib.h>

#include <rtspdefs.h>
#include <rtspurl.h>
#include <rtspstream.h>
#include <rtspmessage.h>

G_BEGIN_DECLS

typedef struct _RTSPConnection
{
  gint          fd;

  gint 		cseq;
  gchar		session_id[512];
  
  RTSPState     state;

  int           num_streams;
  RTSPStream  **streams;
  
} RTSPConnection;

RTSPResult	rtsp_connection_open 	(RTSPUrl *url, RTSPConnection **conn);
RTSPResult	rtsp_connection_create 	(gint fd, RTSPConnection **conn);

RTSPResult	rtsp_connection_send    (RTSPConnection *conn, RTSPMessage *message);
RTSPResult	rtsp_connection_receive (RTSPConnection *conn, RTSPMessage *message);

RTSPResult	rtsp_connection_close 	(RTSPConnection *conn);

G_END_DECLS

#endif /* __RTSP_CONNECTION_H__ */
