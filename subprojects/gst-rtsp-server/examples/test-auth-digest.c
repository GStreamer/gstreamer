/* GStreamer
 * Copyright (C) 2008 Wim Taymans <wim.taymans at gmail.com>
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

#include <gst/gst.h>

#include <gst/rtsp-server/rtsp-server.h>

static gchar *htdigest_path = NULL;
static gchar *realm = NULL;

static GOptionEntry entries[] = {
  {"htdigest-path", 'h', 0, G_OPTION_ARG_STRING, &htdigest_path,
      "Path to an htdigest file to parse (default: None)", "PATH"},
  {"realm", 'r', 0, G_OPTION_ARG_STRING, &realm,
      "Authentication realm (default: None)", "REALM"},
  {NULL}
};


static gboolean
remove_func (GstRTSPSessionPool * pool, GstRTSPSession * session,
    GstRTSPServer * server)
{
  return GST_RTSP_FILTER_REMOVE;
}

static gboolean
remove_sessions (GstRTSPServer * server)
{
  GstRTSPSessionPool *pool;

  g_print ("removing all sessions\n");
  pool = gst_rtsp_server_get_session_pool (server);
  gst_rtsp_session_pool_filter (pool,
      (GstRTSPSessionPoolFilterFunc) remove_func, server);
  g_object_unref (pool);

  return FALSE;
}

static gboolean
timeout (GstRTSPServer * server)
{
  GstRTSPSessionPool *pool;

  pool = gst_rtsp_server_get_session_pool (server);
  gst_rtsp_session_pool_cleanup (pool);
  g_object_unref (pool);

  return TRUE;
}

int
main (int argc, char *argv[])
{
  GMainLoop *loop;
  GstRTSPServer *server;
  GstRTSPMountPoints *mounts;
  GstRTSPMediaFactory *factory;
  GstRTSPAuth *auth;
  GstRTSPToken *token;
  GOptionContext *optctx;
  GError *error = NULL;

  optctx = g_option_context_new (NULL);
  g_option_context_add_main_entries (optctx, entries, NULL);
  g_option_context_add_group (optctx, gst_init_get_option_group ());
  if (!g_option_context_parse (optctx, &argc, &argv, &error)) {
    g_printerr ("Error parsing options: %s\n", error->message);
    g_option_context_free (optctx);
    g_clear_error (&error);
    return -1;
  }
  g_option_context_free (optctx);

  loop = g_main_loop_new (NULL, FALSE);

  /* create a server instance */
  server = gst_rtsp_server_new ();

  /* get the mounts for this server, every server has a default mapper object
   * that be used to map uri mount points to media factories */
  mounts = gst_rtsp_server_get_mount_points (server);


  /* make a media factory for a test stream. The default media factory can use
   * gst-launch syntax to create pipelines. 
   * any launch line works as long as it contains elements named pay%d. Each
   * element with pay%d names will be a stream */
  factory = gst_rtsp_media_factory_new ();
  gst_rtsp_media_factory_set_launch (factory, "( "
      "videotestsrc ! video/x-raw,width=352,height=288,framerate=15/1 ! "
      "x264enc ! rtph264pay name=pay0 pt=96 "
      "audiotestsrc ! audio/x-raw,rate=8000 ! "
      "alawenc ! rtppcmapay name=pay1 pt=97 " ")");
  /* attach the test factory to the /test url */
  gst_rtsp_mount_points_add_factory (mounts, "/test", factory);

  /* allow user and admin to access this resource */
  gst_rtsp_media_factory_add_role (factory, "user",
      GST_RTSP_PERM_MEDIA_FACTORY_ACCESS, G_TYPE_BOOLEAN, TRUE,
      GST_RTSP_PERM_MEDIA_FACTORY_CONSTRUCT, G_TYPE_BOOLEAN, TRUE, NULL);
  gst_rtsp_media_factory_add_role (factory, "admin",
      GST_RTSP_PERM_MEDIA_FACTORY_ACCESS, G_TYPE_BOOLEAN, TRUE,
      GST_RTSP_PERM_MEDIA_FACTORY_CONSTRUCT, G_TYPE_BOOLEAN, TRUE, NULL);
  /* admin2 can look at the media but not construct so he gets a
   * 401 Unauthorized */
  gst_rtsp_media_factory_add_role (factory, "admin2",
      GST_RTSP_PERM_MEDIA_FACTORY_ACCESS, G_TYPE_BOOLEAN, TRUE,
      GST_RTSP_PERM_MEDIA_FACTORY_CONSTRUCT, G_TYPE_BOOLEAN, FALSE, NULL);
  /* Anonymous user can do the same things as admin2 on this resource */
  gst_rtsp_media_factory_add_role (factory, "anonymous",
      GST_RTSP_PERM_MEDIA_FACTORY_ACCESS, G_TYPE_BOOLEAN, TRUE,
      GST_RTSP_PERM_MEDIA_FACTORY_CONSTRUCT, G_TYPE_BOOLEAN, FALSE, NULL);

  /* make another factory */
  factory = gst_rtsp_media_factory_new ();
  gst_rtsp_media_factory_set_launch (factory, "( "
      "videotestsrc ! video/x-raw,width=352,height=288,framerate=30/1 ! "
      "x264enc ! rtph264pay name=pay0 pt=96 )");
  /* attach the test factory to the /test url */
  gst_rtsp_mount_points_add_factory (mounts, "/test2", factory);

  /* allow admin2 to access this resource */
  /* user and admin have no permissions so they can't even see the
   * media and get a 404 Not Found */
  gst_rtsp_media_factory_add_role (factory, "admin2",
      GST_RTSP_PERM_MEDIA_FACTORY_ACCESS, G_TYPE_BOOLEAN, TRUE,
      GST_RTSP_PERM_MEDIA_FACTORY_CONSTRUCT, G_TYPE_BOOLEAN, TRUE, NULL);

  /* don't need the ref to the mapper anymore */
  g_object_unref (mounts);

  /* make a new authentication manager */
  auth = gst_rtsp_auth_new ();
  gst_rtsp_auth_set_supported_methods (auth, GST_RTSP_AUTH_DIGEST);

  /* make default token, it has no permissions */
  token =
      gst_rtsp_token_new (GST_RTSP_TOKEN_MEDIA_FACTORY_ROLE, G_TYPE_STRING,
      "anonymous", NULL);
  gst_rtsp_auth_set_default_token (auth, token);
  gst_rtsp_token_unref (token);

  /* make user token */
  token =
      gst_rtsp_token_new (GST_RTSP_TOKEN_MEDIA_FACTORY_ROLE, G_TYPE_STRING,
      "user", NULL);
  gst_rtsp_auth_add_digest (auth, "user", "password", token);
  gst_rtsp_token_unref (token);

  if (htdigest_path) {
    token =
        gst_rtsp_token_new (GST_RTSP_TOKEN_MEDIA_FACTORY_ROLE, G_TYPE_STRING,
        "user", NULL);

    if (!gst_rtsp_auth_parse_htdigest (auth, htdigest_path, token)) {
      g_printerr ("Could not parse htdigest at %s\n", htdigest_path);
      gst_rtsp_token_unref (token);
      goto failed;
    }

    gst_rtsp_token_unref (token);
  }

  if (realm)
    gst_rtsp_auth_set_realm (auth, realm);

  /* make admin token */
  token =
      gst_rtsp_token_new (GST_RTSP_TOKEN_MEDIA_FACTORY_ROLE, G_TYPE_STRING,
      "admin", NULL);
  gst_rtsp_auth_add_digest (auth, "admin", "power", token);
  gst_rtsp_token_unref (token);

  /* make admin2 token */
  token =
      gst_rtsp_token_new (GST_RTSP_TOKEN_MEDIA_FACTORY_ROLE, G_TYPE_STRING,
      "admin2", NULL);
  gst_rtsp_auth_add_digest (auth, "admin2", "power2", token);
  gst_rtsp_token_unref (token);

  /* set as the server authentication manager */
  gst_rtsp_server_set_auth (server, auth);
  g_object_unref (auth);

  /* attach the server to the default maincontext */
  if (gst_rtsp_server_attach (server, NULL) == 0)
    goto failed;

  g_timeout_add_seconds (2, (GSourceFunc) timeout, server);
  g_timeout_add_seconds (10, (GSourceFunc) remove_sessions, server);

  /* start serving */
  g_print ("stream with user:password ready at rtsp://127.0.0.1:8554/test\n");
  g_print ("stream with admin:power ready at rtsp://127.0.0.1:8554/test\n");
  g_print ("stream with admin2:power2 ready at rtsp://127.0.0.1:8554/test2\n");

  if (htdigest_path)
    g_print
        ("stream with htdigest users ready at rtsp://127.0.0.1:8554/test\n");

  g_main_loop_run (loop);

  return 0;

  /* ERRORS */
failed:
  {
    g_print ("failed to attach the server\n");
    return -1;
  }
}
