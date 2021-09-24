/* GStreamer
 * Copyright (C) 2017 Sebastian Dröge <sebastian@centricular.com>
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
/**
 * SECTION:rtsp-onvif-server
 * @short_description: The main server object
 * @see_also: #GstRTSPOnvifMediaFactory, #GstRTSPClient
 *
 * The server object is the object listening for connections on a port and
 * creating #GstRTSPOnvifClient objects to handle those connections.
 *
 * The only different to #GstRTSPServer is that #GstRTSPOnvifServer creates
 * #GstRTSPOnvifClient that have special handling for ONVIF specific features,
 * like a backchannel that allows clients to send back media to the server.
 *
 * Since: 1.14
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "rtsp-context.h"
#include "rtsp-onvif-server.h"
#include "rtsp-onvif-client.h"

G_DEFINE_TYPE (GstRTSPOnvifServer, gst_rtsp_onvif_server, GST_TYPE_RTSP_SERVER);

static GstRTSPClient *
gst_rtsp_onvif_server_create_client (GstRTSPServer * server)
{
  GstRTSPClient *client;
  GstRTSPSessionPool *session_pool;
  GstRTSPMountPoints *mount_points;
  GstRTSPAuth *auth;
  GstRTSPThreadPool *thread_pool;

  /* a new client connected, create a session to handle the client. */
  client = g_object_new (GST_TYPE_RTSP_ONVIF_CLIENT, NULL);

  /* set the session pool that this client should use */
  session_pool = gst_rtsp_server_get_session_pool (server);
  gst_rtsp_client_set_session_pool (client, session_pool);
  g_object_unref (session_pool);
  /* set the mount points that this client should use */
  mount_points = gst_rtsp_server_get_mount_points (server);
  gst_rtsp_client_set_mount_points (client, mount_points);
  g_object_unref (mount_points);
  /* set authentication manager */
  auth = gst_rtsp_server_get_auth (server);
  gst_rtsp_client_set_auth (client, auth);
  if (auth)
    g_object_unref (auth);
  /* set threadpool */
  thread_pool = gst_rtsp_server_get_thread_pool (server);
  gst_rtsp_client_set_thread_pool (client, thread_pool);
  g_object_unref (thread_pool);

  return client;
}

/**
 * gst_rtsp_onvif_server_new:
 *
 * Create a new #GstRTSPOnvifServer instance.
 *
 * Returns: (transfer full): a new #GstRTSPOnvifServer
 */
GstRTSPServer *
gst_rtsp_onvif_server_new (void)
{
  return g_object_new (GST_TYPE_RTSP_ONVIF_SERVER, NULL);
}

static void
gst_rtsp_onvif_server_class_init (GstRTSPOnvifServerClass * klass)
{
  GstRTSPServerClass *server_klass = (GstRTSPServerClass *) klass;

  server_klass->create_client = gst_rtsp_onvif_server_create_client;
}

static void
gst_rtsp_onvif_server_init (GstRTSPOnvifServer * server)
{
}
