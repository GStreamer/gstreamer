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

/* define this if you want the resource to only be available when using
 * user/password as the password */
#undef WITH_AUTH

/* define this if you want the server to use TLS (it will also need WITH_AUTH
 * to be defined) */
#undef WITH_TLS

/* this timeout is periodically run to clean up the expired sessions from the
 * pool. This needs to be run explicitly currently but might be done
 * automatically as part of the mainloop. */
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
#ifdef WITH_AUTH
  GstRTSPAuth *auth;
  GstRTSPToken *token;
  gchar *basic;
  GstRTSPPermissions *permissions;
#endif
#ifdef WITH_TLS
  GTlsCertificate *cert;
  GError *error = NULL;
#endif

  gst_init (&argc, &argv);

  loop = g_main_loop_new (NULL, FALSE);

  /* create a server instance */
  server = gst_rtsp_server_new ();

#ifdef WITH_AUTH
  /* make a new authentication manager. it can be added to control access to all
   * the factories on the server or on individual factories. */
  auth = gst_rtsp_auth_new ();
#ifdef WITH_TLS
  cert = g_tls_certificate_new_from_pem ("-----BEGIN CERTIFICATE-----"
      "MIICJjCCAY+gAwIBAgIBBzANBgkqhkiG9w0BAQUFADCBhjETMBEGCgmSJomT8ixk"
      "ARkWA0NPTTEXMBUGCgmSJomT8ixkARkWB0VYQU1QTEUxHjAcBgNVBAsTFUNlcnRp"
      "ZmljYXRlIEF1dGhvcml0eTEXMBUGA1UEAxMOY2EuZXhhbXBsZS5jb20xHTAbBgkq"
      "hkiG9w0BCQEWDmNhQGV4YW1wbGUuY29tMB4XDTExMDExNzE5NDcxN1oXDTIxMDEx"
      "NDE5NDcxN1owSzETMBEGCgmSJomT8ixkARkWA0NPTTEXMBUGCgmSJomT8ixkARkW"
      "B0VYQU1QTEUxGzAZBgNVBAMTEnNlcnZlci5leGFtcGxlLmNvbTBcMA0GCSqGSIb3"
      "DQEBAQUAA0sAMEgCQQDYScTxk55XBmbDM9zzwO+grVySE4rudWuzH2PpObIonqbf"
      "hRoAalKVluG9jvbHI81eXxCdSObv1KBP1sbN5RzpAgMBAAGjIjAgMAkGA1UdEwQC"
      "MAAwEwYDVR0lBAwwCgYIKwYBBQUHAwEwDQYJKoZIhvcNAQEFBQADgYEAYx6fMqT1"
      "Gvo0jq88E8mc+bmp4LfXD4wJ7KxYeadQxt75HFRpj4FhFO3DOpVRFgzHlOEo3Fwk"
      "PZOKjvkT0cbcoEq5whLH25dHoQxGoVQgFyAP5s+7Vp5AlHh8Y/vAoXeEVyy/RCIH"
      "QkhUlAflfDMcrrYjsmwoOPSjhx6Mm/AopX4="
      "-----END CERTIFICATE-----"
      "-----BEGIN PRIVATE KEY-----"
      "MIIBVAIBADANBgkqhkiG9w0BAQEFAASCAT4wggE6AgEAAkEA2EnE8ZOeVwZmwzPc"
      "88DvoK1ckhOK7nVrsx9j6TmyKJ6m34UaAGpSlZbhvY72xyPNXl8QnUjm79SgT9bG"
      "zeUc6QIDAQABAkBRFJZ32VbqWMP9OVwDJLiwC01AlYLnka0mIQZbT/2xq9dUc9GW"
      "U3kiVw4lL8v/+sPjtTPCYYdzHHOyDen6znVhAiEA9qJT7BtQvRxCvGrAhr9MS022"
      "tTdPbW829BoUtIeH64cCIQDggG5i48v7HPacPBIH1RaSVhXl8qHCpQD3qrIw3FMw"
      "DwIga8PqH5Sf5sHedy2+CiK0V4MRfoU4c3zQ6kArI+bEgSkCIQCLA1vXBiE31B5s"
      "bdHoYa1BXebfZVd+1Hd95IfEM5mbRwIgSkDuQwV55BBlvWph3U8wVIMIb4GStaH8"
      "W535W8UBbEg=" "-----END PRIVATE KEY-----", -1, &error);
  if (cert == NULL) {
    g_printerr ("failed to parse PEM: %s\n", error->message);
    return -1;
  }
  gst_rtsp_auth_set_tls_certificate (auth, cert);
  g_object_unref (cert);
#endif

  /* make user token */
  token =
      gst_rtsp_token_new (GST_RTSP_TOKEN_MEDIA_FACTORY_ROLE, G_TYPE_STRING,
      "user", NULL);
  basic = gst_rtsp_auth_make_basic ("user", "password");
  gst_rtsp_auth_add_basic (auth, basic, token);
  g_free (basic);
  gst_rtsp_token_unref (token);

  /* configure in the server */
  gst_rtsp_server_set_auth (server, auth);
#endif

  /* get the mount points for this server, every server has a default object
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
#ifdef WITH_AUTH
  /* add permissions for the user media role */
  permissions = gst_rtsp_permissions_new ();
  gst_rtsp_permissions_add_role (permissions, "user",
      GST_RTSP_PERM_MEDIA_FACTORY_ACCESS, G_TYPE_BOOLEAN, TRUE,
      GST_RTSP_PERM_MEDIA_FACTORY_CONSTRUCT, G_TYPE_BOOLEAN, TRUE, NULL);
  gst_rtsp_media_factory_set_permissions (factory, permissions);
  gst_rtsp_permissions_unref (permissions);
#ifdef WITH_TLS
  gst_rtsp_media_factory_set_profiles (factory, GST_RTSP_PROFILE_SAVP);
#endif
#endif

  /* attach the test factory to the /test url */
  gst_rtsp_mount_points_add_factory (mounts, "/test", factory);

  /* don't need the ref to the mapper anymore */
  g_object_unref (mounts);

  /* attach the server to the default maincontext */
  if (gst_rtsp_server_attach (server, NULL) == 0)
    goto failed;

  /* add a timeout for the session cleanup */
  g_timeout_add_seconds (2, (GSourceFunc) timeout, server);

  /* start serving, this never stops */
#ifdef WITH_TLS
  g_print ("stream ready at rtsps://127.0.0.1:8554/test\n");
#else
  g_print ("stream ready at rtsp://127.0.0.1:8554/test\n");
#endif
  g_main_loop_run (loop);

  return 0;

  /* ERRORS */
failed:
  {
    g_print ("failed to attach the server\n");
    return -1;
  }
}
