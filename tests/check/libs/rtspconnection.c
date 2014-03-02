/* GStreamer unit tests for the GstRTSPConnection API (RTSP support
 * library)
 *
 * Copyright (C) 2014 Ognyan Tonchev <ognyan axis com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/check/gstcheck.h>

#include <gst/rtsp/gstrtspconnection.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>


static const gchar *get_msg =
    "GET /example/url HTTP/1.0\r\n"
    "Host: 127.0.0.1\r\n"
    "x-sessioncookie: 805849328\r\n"
    "\r\n";
static const gchar *post_msg =
    "POST /example/url HTTP/1.0\r\n"
    "Host: 127.0.0.1\r\n"
    "x-sessioncookie: 805849328\r\n"
    "Content-Length: 0\r\n"
    "Content-Type: application/x-rtsp-tunnelled\r\n"
    "\r\n";

static guint tunnel_start_count;
static guint tunnel_complete_count;
static guint tunnel_lost_count;
static guint closed_count;

typedef struct
{
  GMainLoop *loop;
  guint16 port;
  GSocketConnection *conn;
  GMutex mutex;
  GCond cond;
  gboolean started;
} ServiceData;

static gboolean
incoming_callback (GSocketService *service, GSocketConnection *connection,
    GObject *source_object, gpointer user_data)
{
  ServiceData *data = user_data;

  GST_DEBUG ("new incoming connection");
  data->conn = g_object_ref (connection);
  g_main_loop_quit (data->loop);
  return FALSE;
}

static gpointer
service_thread_func (gpointer user_data)
{
  ServiceData *data = user_data;
  GMainContext *service_context;
  GSocketService *service;

  service_context = g_main_context_new ();
  g_main_context_push_thread_default (service_context);

  data->loop = g_main_loop_new (service_context, FALSE);

  /* find available port and start service */
  service = g_socket_service_new ();
  data->port = g_socket_listener_add_any_inet_port ((GSocketListener *)service,
      NULL, NULL);
  fail_unless (data->port != 0);

  /* get notified upon new connection */
  g_signal_connect (service, "incoming", G_CALLBACK (incoming_callback), data);

  g_socket_service_start (service);

  /* service is started */
  g_mutex_lock (&data->mutex);
  data->started = TRUE;
  g_cond_signal (&data->cond);
  g_mutex_unlock (&data->mutex);

  /* our service will run in the main context of this main loop */
  g_main_loop_run (data->loop);

  g_main_context_pop_thread_default (service_context);

  g_main_loop_unref (data->loop);
  data->loop = NULL;

  return NULL;
}

static void
create_connection (GSocketConnection **client_conn,
    GSocketConnection **server_conn)
{
  ServiceData *data;
  GThread *service_thread;
  GSocketClient * client = g_socket_client_new ();

  data =  g_new0 (ServiceData, 1);
  g_mutex_init (&data->mutex);
  g_cond_init (&data->cond);

  service_thread = g_thread_new ("service thread", service_thread_func, data);
  fail_unless (service_thread != NULL);

  /* wait for the service to start */
  g_mutex_lock (&data->mutex);
  while (!data->started) {
    g_cond_wait (&data->cond, &data->mutex);
  }
  g_mutex_unlock (&data->mutex);

  /* create the tcp link */
  *client_conn = g_socket_client_connect_to_host (client, (gchar *)"localhost",
      data->port, NULL, NULL);
  fail_unless (*client_conn != NULL);
  fail_unless (g_socket_connection_is_connected (*client_conn));

  g_thread_join (service_thread);
  *server_conn = data->conn;
  data->conn = NULL;
  fail_unless (g_socket_connection_is_connected (*server_conn));

  g_mutex_clear (&data->mutex);
  g_cond_clear (&data->cond);
  g_free (data);
  g_object_unref (client);
}

static GstRTSPStatusCode
tunnel_start (GstRTSPWatch *watch, gpointer user_data)
{
  tunnel_start_count++;
  return GST_RTSP_STS_OK;
}

static GstRTSPResult
tunnel_complete (GstRTSPWatch *watch, gpointer user_data)
{
  tunnel_complete_count++;
  return GST_RTSP_OK;
}

static GstRTSPResult
tunnel_lost (GstRTSPWatch *watch, gpointer user_data)
{
  tunnel_lost_count++;
  return GST_RTSP_OK;
}

static GstRTSPResult
closed (GstRTSPWatch *watch, gpointer user_data)
{
  closed_count++;
  return GST_RTSP_OK;
}

static GstRTSPWatchFuncs watch_funcs = {
  NULL,
  NULL,
  closed,
  NULL,
  tunnel_start,
  tunnel_complete,
  NULL,
  tunnel_lost
};

/* setts up a new tunnel, then disconnects the read connection and creates it
 * again */
GST_START_TEST (test_rtspconnection_tunnel_setup)
{
  GstRTSPConnection *rtsp_conn1 = NULL;
  GstRTSPConnection *rtsp_conn2 = NULL;
  GstRTSPWatch *watch1;
  GstRTSPWatch *watch2;
  GstRTSPResult res;
  GSocketConnection *client_get = NULL;
  GSocketConnection *server_get = NULL;
  GSocketConnection *client_post = NULL;
  GSocketConnection *server_post = NULL;
  GSocket *server_sock;
  GOutputStream *ostream_get;
  GInputStream *istream_get;
  GOutputStream *ostream_post;
  gsize size = 0;
  gchar buffer[1024];

  /* create GET connection */
  create_connection (&client_get, &server_get);
  server_sock = g_socket_connection_get_socket (server_get);
  fail_unless (server_sock != NULL);

  res = gst_rtsp_connection_create_from_socket (server_sock, "127.0.0.1", 4444,
      NULL, &rtsp_conn1);
  fail_unless (res == GST_RTSP_OK);
  fail_unless (rtsp_conn1 != NULL);

  watch1 = gst_rtsp_watch_new (rtsp_conn1, &watch_funcs, NULL, NULL);
  fail_unless (watch1 != NULL);
  fail_unless (gst_rtsp_watch_attach (watch1, NULL) > 0);

  ostream_get = g_io_stream_get_output_stream (G_IO_STREAM (client_get));
  fail_unless (ostream_get != NULL);

  istream_get = g_io_stream_get_input_stream (G_IO_STREAM (client_get));
  fail_unless (istream_get != NULL);

  /* initiate the tunnel by sending HTTP GET */
  fail_unless (g_output_stream_write_all (ostream_get, get_msg,
      strlen (get_msg), &size, NULL, NULL));
  fail_unless (size == strlen (get_msg));

  while (!g_main_context_iteration (NULL, TRUE));
  fail_unless (tunnel_start_count == 1);
  fail_unless (tunnel_complete_count == 0);
  fail_unless (tunnel_lost_count == 0);
  fail_unless (closed_count == 0);

  /* read the HTTP GET response */
  size = g_input_stream_read (istream_get, buffer, 1024, NULL, NULL);
  fail_unless (size > 0);
  buffer[size] = 0;
  fail_unless (g_strrstr (buffer, "HTTP/1.0 200 OK") != NULL);

  /* create POST channel */
  create_connection (&client_post, &server_post);
  server_sock = g_socket_connection_get_socket (server_post);
  fail_unless (server_sock != NULL);

  res = gst_rtsp_connection_create_from_socket (server_sock, "127.0.0.1", 4444,
      NULL, &rtsp_conn2);
  fail_unless (res == GST_RTSP_OK);
  fail_unless (rtsp_conn2 != NULL);

  watch2 = gst_rtsp_watch_new (rtsp_conn2, &watch_funcs, NULL, NULL);
  fail_unless (watch2 != NULL);
  fail_unless (gst_rtsp_watch_attach (watch2, NULL) > 0);

  ostream_post = g_io_stream_get_output_stream (G_IO_STREAM (client_post));
  fail_unless (ostream_post != NULL);

  /* complete the tunnel by sending HTTP POST */
  fail_unless (g_output_stream_write_all (ostream_post, post_msg,
      strlen (post_msg), &size, NULL, NULL));
  fail_unless (size == strlen (post_msg));

  while (!g_main_context_iteration (NULL, TRUE));
  fail_unless (tunnel_start_count == 1);
  fail_unless (tunnel_complete_count == 1);
  fail_unless (tunnel_lost_count == 0);
  fail_unless (closed_count == 0);

  /* merge the two connections together */
  fail_unless (gst_rtsp_connection_do_tunnel (rtsp_conn1, rtsp_conn2) ==
      GST_RTSP_OK);
  gst_rtsp_watch_reset (watch1);
  g_source_destroy ((GSource *)watch2);
  gst_rtsp_connection_free (rtsp_conn2);
  rtsp_conn2 = NULL;

  /* it must be possible to reconnect the POST channel */
  g_object_unref (client_post);
  while (!g_main_context_iteration (NULL, TRUE));
  fail_unless (tunnel_start_count == 1);
  fail_unless (tunnel_complete_count == 1);
  fail_unless (tunnel_lost_count == 1);
  fail_unless (closed_count == 0);
  g_object_unref (server_post);

  /* no other source should get dispatched */
  fail_if (g_main_context_iteration (NULL, FALSE));

  /* create new POST connection */
  create_connection (&client_post, &server_post);
  server_sock = g_socket_connection_get_socket (server_post);
  fail_unless (server_sock != NULL);

  res = gst_rtsp_connection_create_from_socket (server_sock, "127.0.0.1", 4444,
      NULL, &rtsp_conn2);
  fail_unless (res == GST_RTSP_OK);
  fail_unless (rtsp_conn2 != NULL);

  watch2 = gst_rtsp_watch_new (rtsp_conn2, &watch_funcs, NULL, NULL);
  fail_unless (watch2 != NULL);
  fail_unless (gst_rtsp_watch_attach (watch2, NULL) > 0);

  ostream_post = g_io_stream_get_output_stream (G_IO_STREAM (client_post));
  fail_unless (ostream_post != NULL);

  /* complete the tunnel by sending HTTP POST */
  fail_unless (g_output_stream_write_all (ostream_post, post_msg,
      strlen (post_msg), &size, NULL, NULL));
  fail_unless (size == strlen (post_msg));

  while (!g_main_context_iteration (NULL, TRUE));
  fail_unless (tunnel_start_count == 1);
  fail_unless (tunnel_complete_count == 2);
  fail_unless (tunnel_lost_count == 1);
  fail_unless (closed_count == 0);

  /* merge the two connections together */
  fail_unless (gst_rtsp_connection_do_tunnel (rtsp_conn1, rtsp_conn2) ==
      GST_RTSP_OK);
  gst_rtsp_watch_reset (watch1);
  g_source_destroy ((GSource *)watch2);
  gst_rtsp_connection_free (rtsp_conn2);
  rtsp_conn2 = NULL;

  /* check if rtspconnection can detect close of the get channel */
  g_object_unref (client_get);
  while (!g_main_context_iteration (NULL, TRUE));
  fail_unless (tunnel_start_count == 1);
  fail_unless (tunnel_complete_count == 2);
  fail_unless (tunnel_lost_count == 1);
  fail_unless (closed_count == 1);

  fail_unless (gst_rtsp_connection_close (rtsp_conn1) == GST_RTSP_OK);
  fail_unless (gst_rtsp_connection_free (rtsp_conn1) == GST_RTSP_OK);

  g_object_unref (client_post);
  g_object_unref (server_post);
  g_object_unref (server_get);
}

GST_END_TEST;

static Suite *
rtspconnection_suite (void)
{
  Suite *s = suite_create ("rtsp support library(rtspconnection)");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_rtspconnection_tunnel_setup);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = rtspconnection_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
