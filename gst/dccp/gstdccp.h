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

#ifndef __GST_DCCP_H__
#define __GST_DCCP_H__

#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include "gstdccp_common.h"

/* DCCP socket general options */
#define DCCP_BACKLOG	5
#ifndef SOCK_DCCP
	#define SOCK_DCCP		6
#endif

#ifndef IPPROTO_DCCP
	#define IPPROTO_DCCP	33
#endif

#ifndef SOL_DCCP
	#define SOL_DCCP		269
#endif

/* dccp socket specific options */
#define DCCP_SOCKOPT_PACKET_SIZE        1 /* XXX deprecated, without effect */
#define DCCP_SOCKOPT_SERVICE            2
#define DCCP_SOCKOPT_CHANGE_L           3
#define DCCP_SOCKOPT_CHANGE_R           4
#define DCCP_SOCKOPT_GET_CUR_MPS        5
#define DCCP_SOCKOPT_SERVER_TIMEWAIT    6
#define DCCP_SOCKOPT_SEND_CSCOV         10
#define DCCP_SOCKOPT_RECV_CSCOV         11
#define DCCP_SOCKOPT_AVAILABLE_CCIDS    12
#define DCCP_SOCKOPT_CCID               13
#define DCCP_SOCKOPT_TX_CCID            14
#define DCCP_SOCKOPT_RX_CCID            15
#define DCCP_SOCKOPT_CCID_RX_INFO       128
#define DCCP_SOCKOPT_CCID_TX_INFO       192

/* Default parameters for the gst dccp element property */
#define DCCP_DEFAULT_PORT		 5001
#define DCCP_DEFAULT_SOCK_FD		 -1
#define DCCP_DEFAULT_CLIENT_SOCK_FD	 -1
#define DCCP_DEFAULT_CLOSED		 TRUE
#define DCCP_DEFAULT_WAIT_CONNECTIONS	 FALSE
#define DCCP_DEFAULT_HOST		 "127.0.0.1"
#define DCCP_DEFAULT_CCID		 2

#define DCCP_DELTA			 100

gchar *gst_dccp_host_to_ip (GstElement * element, const gchar * host);

GstFlowReturn gst_dccp_read_buffer (GstElement * this, int socket,
				GstBuffer ** buf);

gint gst_dccp_create_new_socket (GstElement * element);
gboolean gst_dccp_connect_to_server (GstElement * element,
				 struct sockaddr_in server_sin,
				 int sock_fd);

gint gst_dccp_server_wait_connections (GstElement * element, int server_sock_fd);

gboolean gst_dccp_bind_server_socket (GstElement * element, int server_sock_fd,
					  struct sockaddr_in server_sin);

gboolean gst_dccp_listen_server_socket (GstElement * element, int server_sock_fd);
gboolean gst_dccp_set_ccid (GstElement * element, int sock_fd, uint8_t ccid);

gint gst_dccp_get_max_packet_size(GstElement * element, int sock);

GstFlowReturn gst_dccp_send_buffer (GstElement * element, GstBuffer * buffer,
					int client_sock_fd, int packet_size);

gboolean gst_dccp_make_address_reusable (GstElement * element, int sock_fd);
void gst_dccp_socket_close (GstElement * element, int * socket);

#endif /* __GST_DCCP_H__ */
