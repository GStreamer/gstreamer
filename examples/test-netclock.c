/* GStreamer
 * Copyright (C) 2008 Wim Taymans <wim.taymans at gmail.com>
 * Copyright (C) 2014 Jan Schmidt <jan@centricular.com>
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

#include <gst/net/gstnettimeprovider.h>
#include <gst/rtsp-server/rtsp-server.h>

GstClock *global_clock;

#define TEST_TYPE_RTSP_MEDIA_FACTORY      (test_rtsp_media_factory_get_type ())
#define TEST_TYPE_RTSP_MEDIA              (test_rtsp_media_get_type ())

GType test_rtsp_media_get_type (void);

typedef struct TestRTSPMediaClass TestRTSPMediaClass;
typedef struct TestRTSPMedia TestRTSPMedia;

struct TestRTSPMediaClass
{
  GstRTSPMediaClass parent;
};

struct TestRTSPMedia
{
  GstRTSPMedia parent;
};

static gboolean custom_setup_rtpbin (GstRTSPMedia * media, GstElement * rtpbin);

G_DEFINE_TYPE (TestRTSPMedia, test_rtsp_media, GST_TYPE_RTSP_MEDIA);

static void
test_rtsp_media_class_init (TestRTSPMediaClass * test_klass)
{
  GstRTSPMediaClass *klass = (GstRTSPMediaClass *) (test_klass);
  klass->setup_rtpbin = custom_setup_rtpbin;
}

static void
test_rtsp_media_init (TestRTSPMedia * media)
{
}

static gboolean
custom_setup_rtpbin (GstRTSPMedia * media, GstElement * rtpbin)
{
  g_object_set (rtpbin, "ntp-time-source", 3, NULL);
  return TRUE;
}

int
main (int argc, char *argv[])
{
  GMainLoop *loop;
  GstRTSPServer *server;
  GstRTSPMountPoints *mounts;
  GstRTSPMediaFactory *factory;

  gst_init (&argc, &argv);

  if (argc < 2) {
    g_print ("usage: %s <launch line> \n"
        "example: %s \"( videotestsrc is-live=true ! x264enc ! rtph264pay name=pay0 pt=96 )\"\n"
        "Pipeline must be live for synchronisation to work properly with this method!\n",
        argv[0], argv[0]);
    return -1;
  }

  loop = g_main_loop_new (NULL, FALSE);

  global_clock = gst_system_clock_obtain ();
  gst_net_time_provider_new (global_clock, "0.0.0.0", 8554);

  /* create a server instance */
  server = gst_rtsp_server_new ();

  /* get the mount points for this server, every server has a default object
   * that be used to map uri mount points to media factories */
  mounts = gst_rtsp_server_get_mount_points (server);

  /* make a media factory for a test stream. The default media factory can use
   * gst-launch syntax to create pipelines.
   * any launch line works as long as it contains elements named pay%d. Each
   * element with pay%d names will be a stream */
  factory = gst_rtsp_media_factory_new ();
  gst_rtsp_media_factory_set_launch (factory, argv[1]);
  gst_rtsp_media_factory_set_shared (factory, TRUE);
  gst_rtsp_media_factory_set_media_gtype (factory, TEST_TYPE_RTSP_MEDIA);
  gst_rtsp_media_factory_set_clock (factory, global_clock);

  /* attach the test factory to the /test url */
  gst_rtsp_mount_points_add_factory (mounts, "/test", factory);

  /* don't need the ref to the mapper anymore */
  g_object_unref (mounts);

  /* attach the server to the default maincontext */
  gst_rtsp_server_attach (server, NULL);

  /* start serving */
  g_print ("stream ready at rtsp://127.0.0.1:8554/test\n");
  g_main_loop_run (loop);

  return 0;
}
