/* GStreamer unit test for rtspclientsink
 * Copyright (C) 2012 Axis Communications <dev-gstreamer at axis dot com>
 *   @author David Svensson Fors <davidsf at axis dot com>
 * Copyright (C) 2015 Centricular Ltd
 *   @author Tim-Philipp Müller <tim@centricular.com>
 *   @author Jan Schmidt <jan@centricular.com>
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

#include <gst/check/gstcheck.h>
#include <gst/sdp/gstsdpmessage.h>
#include <gst/rtp/gstrtpbuffer.h>
#include <gst/rtp/gstrtcpbuffer.h>

#include <stdio.h>
#include <netinet/in.h>

#include "rtsp-server.h"

#define TEST_MOUNT_POINT  "/test"

/* tested rtsp server */
static GstRTSPServer *server = NULL;

/* tcp port that the test server listens for rtsp requests on */
static gint test_port = 0;

/* id of the server's source within the GMainContext */
static guint source_id;

/* iterate the default main context until there are no events to dispatch */
static void
iterate (void)
{
  while (g_main_context_iteration (NULL, FALSE)) {
    GST_DEBUG ("iteration");
  }
}

/* start the testing rtsp server for RECORD mode */
static GstRTSPMediaFactory *
start_record_server (const gchar * launch_line)
{
  GstRTSPMediaFactory *factory;
  GstRTSPMountPoints *mounts;
  gchar *service;

  mounts = gst_rtsp_server_get_mount_points (server);

  factory = gst_rtsp_media_factory_new ();

  gst_rtsp_media_factory_set_transport_mode (factory,
      GST_RTSP_TRANSPORT_MODE_RECORD);
  gst_rtsp_media_factory_set_launch (factory, launch_line);
  gst_rtsp_mount_points_add_factory (mounts, TEST_MOUNT_POINT, factory);
  g_object_unref (mounts);

  /* set port to any */
  gst_rtsp_server_set_service (server, "0");

  /* attach to default main context */
  source_id = gst_rtsp_server_attach (server, NULL);
  fail_if (source_id == 0);

  /* get port */
  service = gst_rtsp_server_get_service (server);
  test_port = atoi (service);
  fail_unless (test_port != 0);
  g_free (service);

  GST_DEBUG ("rtsp server listening on port %d", test_port);
  return factory;
}

/* stop the tested rtsp server */
static void
stop_server (void)
{
  g_source_remove (source_id);
  source_id = 0;

  GST_DEBUG ("rtsp server stopped");
}

/* fixture setup function */
static void
setup (void)
{
  server = gst_rtsp_server_new ();
}

/* fixture clean-up function */
static void
teardown (void)
{
  if (server) {
    g_object_unref (server);
    server = NULL;
  }
  test_port = 0;
}

/* create an rtsp connection to the server on test_port */
static gchar *
get_server_uri (gint port, const gchar * mount_point)
{
  gchar *address;
  gchar *uri_string;
  GstRTSPUrl *url = NULL;

  address = gst_rtsp_server_get_address (server);
  uri_string = g_strdup_printf ("rtsp://%s:%d%s", address, port, mount_point);
  g_free (address);

  fail_unless (gst_rtsp_url_parse (uri_string, &url) == GST_RTSP_OK);
  gst_rtsp_url_free (url);

  return uri_string;
}

static void
media_constructed_cb (GstRTSPMediaFactory * mfactory, GstRTSPMedia * media,
    gpointer user_data)
{
  GstElement **p_sink = user_data;
  GstElement *bin;

  bin = gst_rtsp_media_get_element (media);
  *p_sink = gst_bin_get_by_name (GST_BIN (bin), "sink");
  GST_INFO ("media constructed!: %" GST_PTR_FORMAT, *p_sink);
}

#define AUDIO_PIPELINE "audiotestsrc num-buffers=%d ! " \
  "audio/x-raw,rate=8000 ! alawenc ! rtspclientsink name=sink location=%s"
#define RECORD_N_BUFS 10

GST_START_TEST (test_record)
{
  GstRTSPMediaFactory *mfactory;
  GstElement *server_sink = NULL;
  gint i;

  mfactory =
      start_record_server ("( rtppcmadepay name=depay0 ! appsink name=sink async=false )");

  g_signal_connect (mfactory, "media-constructed",
      G_CALLBACK (media_constructed_cb), &server_sink);

  /* Create an rtspclientsink and send some data */
  {
    gchar *uri = get_server_uri (test_port, TEST_MOUNT_POINT);
    gchar *pipe_str;
    GstMessage *msg;
    GstElement *pipeline;
    GstBus *bus;

    pipe_str = g_strdup_printf (AUDIO_PIPELINE, RECORD_N_BUFS, uri);
    g_free (uri);

    pipeline = gst_parse_launch (pipe_str, NULL);
    g_free (pipe_str);

    fail_unless (pipeline != NULL);

    bus = gst_element_get_bus (pipeline);
    fail_if (bus == NULL);

    gst_element_set_state (pipeline, GST_STATE_PLAYING);

    msg = gst_bus_poll (bus, GST_MESSAGE_EOS | GST_MESSAGE_ERROR, -1);
    fail_if (GST_MESSAGE_TYPE (msg) != GST_MESSAGE_EOS);
    gst_message_unref (msg);

    gst_element_set_state (pipeline, GST_STATE_NULL);
    gst_object_unref (pipeline);
  }

  iterate ();

  /* check received data (we assume every buffer created by audiotestsrc and
   * subsequently encoded by mulawenc results in exactly one RTP packet) */
  for (i = 0; i < RECORD_N_BUFS; ++i) {
    GstSample *sample = NULL;

    g_signal_emit_by_name (G_OBJECT (server_sink), "pull-sample", &sample);
    GST_INFO ("%2d recv sample: %p", i, sample);
    if (sample)
      gst_sample_unref (sample);
  }

  /* clean up and iterate so the clean-up can finish */
  stop_server ();
  iterate ();
}

GST_END_TEST;

static Suite *
rtspclientsink_suite (void)
{
  Suite *s = suite_create ("rtspclientsink");
  TCase *tc = tcase_create ("general");

  suite_add_tcase (s, tc);
  tcase_add_checked_fixture (tc, setup, teardown);
  tcase_set_timeout (tc, 120);
  tcase_add_test (tc, test_record);
  return s;
}

GST_CHECK_MAIN (rtspclientsink);
