/* GStreamer
 * Copyright (C) 2008 Wim Taymans <wim.taymans at gmail.com>
 * Copyright (C) 2015 Centricular Ltd
 *     Author: Sebastian Dröge <sebastian@centricular.com>
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

/* define this if you want the server to use TLS */
//#define WITH_TLS

#define DEFAULT_RTSP_PORT "8554"

static char *port = (char *) DEFAULT_RTSP_PORT;

static GOptionEntry entries[] = {
  {"port", 'p', 0, G_OPTION_ARG_STRING, &port,
      "Port to listen on (default: " DEFAULT_RTSP_PORT ")", "PORT"},
  {NULL}
};

int
main (int argc, char *argv[])
{
  GMainLoop *loop;
  GstRTSPServer *server;
  GstRTSPMountPoints *mounts;
  GstRTSPMediaFactory *factory;
  GOptionContext *optctx;
  GError *error = NULL;
  GstRTSPAuth *auth;
  GstRTSPToken *token;
  gchar *basic;
#ifdef WITH_TLS
  GTlsCertificate *cert;
#endif

  optctx = g_option_context_new ("<launch line> - Test RTSP Server, Launch\n\n"
      "Example: \"( decodebin name=depay0 ! autovideosink )\"");
  g_option_context_add_main_entries (optctx, entries, NULL);
  g_option_context_add_group (optctx, gst_init_get_option_group ());
  if (!g_option_context_parse (optctx, &argc, &argv, &error)) {
    g_printerr ("Error parsing options: %s\n", error->message);
    return -1;
  }

  if (argc < 2) {
    g_print ("%s\n", g_option_context_get_help (optctx, TRUE, NULL));
    return 1;
  }
  g_option_context_free (optctx);

  loop = g_main_loop_new (NULL, FALSE);

  /* create a server instance */
  server = gst_rtsp_server_new ();
  g_object_set (server, "service", port, NULL);

  /* get the mount points for this server, every server has a default object
   * that be used to map uri mount points to media factories */
  mounts = gst_rtsp_server_get_mount_points (server);

  /* make a media factory for a test stream. The default media factory can use
   * gst-launch syntax to create pipelines.
   * any launch line works as long as it contains elements named depay%d. Each
   * element with depay%d names will be a stream */
  factory = gst_rtsp_media_factory_new ();
  gst_rtsp_media_factory_set_transport_mode (factory,
      GST_RTSP_TRANSPORT_MODE_RECORD);
  gst_rtsp_media_factory_set_launch (factory, argv[1]);
  gst_rtsp_media_factory_set_latency (factory, 2000);
#ifdef WITH_TLS
  gst_rtsp_media_factory_set_profiles (factory,
      GST_RTSP_PROFILE_SAVP | GST_RTSP_PROFILE_SAVPF);
#else
  gst_rtsp_media_factory_set_profiles (factory,
      GST_RTSP_PROFILE_AVP | GST_RTSP_PROFILE_AVPF);
#endif

  /* allow user to access this resource */
  gst_rtsp_media_factory_add_role (factory, "user",
      GST_RTSP_PERM_MEDIA_FACTORY_ACCESS, G_TYPE_BOOLEAN, TRUE,
      GST_RTSP_PERM_MEDIA_FACTORY_CONSTRUCT, G_TYPE_BOOLEAN, TRUE, NULL);
  /* Anonymous users can see but not construct, so get UNAUTHORIZED */
  gst_rtsp_media_factory_add_role (factory, "anonymous",
      GST_RTSP_PERM_MEDIA_FACTORY_ACCESS, G_TYPE_BOOLEAN, TRUE,
      GST_RTSP_PERM_MEDIA_FACTORY_CONSTRUCT, G_TYPE_BOOLEAN, FALSE, NULL);

  /* attach the test factory to the /test url */
  gst_rtsp_mount_points_add_factory (mounts, "/test", factory);

  /* don't need the ref to the mapper anymore */
  g_object_unref (mounts);

  /* Set up the auth for user account */
  /* make a new authentication manager */
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

  /* make default token - anonymous unauthenticated access */
  token =
      gst_rtsp_token_new (GST_RTSP_TOKEN_MEDIA_FACTORY_ROLE, G_TYPE_STRING,
      "anonymous", NULL);
  gst_rtsp_auth_set_default_token (auth, token);
  gst_rtsp_token_unref (token);

  /* make user token */
  token =
      gst_rtsp_token_new (GST_RTSP_TOKEN_MEDIA_FACTORY_ROLE, G_TYPE_STRING,
      "user", NULL);
  basic = gst_rtsp_auth_make_basic ("user", "password");
  gst_rtsp_auth_add_basic (auth, basic, token);
  g_free (basic);
  gst_rtsp_token_unref (token);

  /* set as the server authentication manager */
  gst_rtsp_server_set_auth (server, auth);
  g_object_unref (auth);

  /* attach the server to the default maincontext */
  gst_rtsp_server_attach (server, NULL);

  /* start serving */
#ifdef WITH_TLS
  g_print ("stream ready at rtsps://127.0.0.1:%s/test\n", port);
#else
  g_print ("stream ready at rtsp://127.0.0.1:%s/test\n", port);
#endif
  g_main_loop_run (loop);

  return 0;
}
