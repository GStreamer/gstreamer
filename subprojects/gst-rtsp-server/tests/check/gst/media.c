/* GStreamer
 * Copyright (C) 2012 Wim Taymans <wim.taymans@gmail.com>
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

#include <rtsp-media-factory.h>

/* Check if the media can return a SDP. We don't actually check whether
 * the contents are valid or not */
static gboolean
media_has_sdp (GstRTSPMedia * media)
{
  GstSDPInfo info;
  GstSDPMessage *sdp;
  gchar *sdp_str;

  info.is_ipv6 = FALSE;
  info.server_ip = "0.0.0.0";

  /* Check if media can generate a SDP */
  gst_sdp_message_new (&sdp);
  GST_DEBUG ("Getting SDP");
  if (!gst_rtsp_sdp_from_media (sdp, &info, media)) {
    GST_WARNING ("failed to get the SDP");
    gst_sdp_message_free (sdp);
    return FALSE;
  }
  sdp_str = gst_sdp_message_as_text (sdp);
  GST_DEBUG ("Got SDP\n%s", sdp_str);
  g_free (sdp_str);
  gst_sdp_message_free (sdp);

  return TRUE;
}

GST_START_TEST (test_media_seek)
{
  GstRTSPMediaFactory *factory;
  GstRTSPMedia *media;
  GstRTSPUrl *url;
  GstRTSPStream *stream;
  GstRTSPTimeRange *range;
  gchar *str;
  GstRTSPThreadPool *pool;
  GstRTSPThread *thread;
  GstRTSPTransport *transport;
  gdouble rate = 0;
  gdouble applied_rate = 0;

  factory = gst_rtsp_media_factory_new ();
  fail_if (gst_rtsp_media_factory_is_shared (factory));
  fail_unless (gst_rtsp_url_parse ("rtsp://localhost:8554/test",
          &url) == GST_RTSP_OK);

  gst_rtsp_media_factory_set_launch (factory,
      "( videotestsrc ! rtpvrawpay pt=96 name=pay0 )");

  media = gst_rtsp_media_factory_construct (factory, url);
  fail_unless (GST_IS_RTSP_MEDIA (media));

  fail_unless (gst_rtsp_media_n_streams (media) == 1);

  stream = gst_rtsp_media_get_stream (media, 0);
  fail_unless (stream != NULL);

  pool = gst_rtsp_thread_pool_new ();
  thread = gst_rtsp_thread_pool_get_thread (pool,
      GST_RTSP_THREAD_TYPE_MEDIA, NULL);

  fail_unless (gst_rtsp_media_prepare (media, thread));
  fail_unless (media_has_sdp (media));

  /* define transport */
  fail_unless (gst_rtsp_transport_new (&transport) == GST_RTSP_OK);
  transport->lower_transport = GST_RTSP_LOWER_TRANS_TCP;

  fail_unless (gst_rtsp_stream_complete_stream (stream, transport));

  fail_unless (gst_rtsp_transport_free (transport) == GST_RTSP_OK);
  fail_unless (gst_rtsp_range_parse ("npt=5.0-", &range) == GST_RTSP_OK);

  /* the media is seekable now */
  fail_unless (gst_rtsp_media_seek (media, range));

  str = gst_rtsp_media_get_range_string (media, FALSE, GST_RTSP_RANGE_NPT);
  fail_unless (g_str_equal (str, "npt=5-"));
  g_free (str);

  /* seeking without rate should result in rate == 1.0 */
  fail_unless (gst_rtsp_media_seek (media, range));
  fail_unless (gst_rtsp_media_get_rates (media, &rate, &applied_rate));
  fail_unless (rate == 1.0);
  fail_unless (applied_rate == 1.0);

  /* seeking with rate set to 1.5 should result in rate == 1.5 */
  fail_unless (gst_rtsp_media_seek_trickmode (media, range,
          GST_SEEK_FLAG_NONE, 1.5, 0));
  fail_unless (gst_rtsp_media_get_rates (media, &rate, &applied_rate));
  fail_unless (rate == 1.5);
  fail_unless (applied_rate == 1.0);

  gst_rtsp_range_free (range);

  /* seeking with rate set to -2.0 should result in rate == -2.0 */
  fail_unless (gst_rtsp_range_parse ("npt=10-5", &range) == GST_RTSP_OK);
  fail_unless (gst_rtsp_media_seek_trickmode (media, range,
          GST_SEEK_FLAG_NONE, -2.0, 0));
  fail_unless (gst_rtsp_media_get_rates (media, &rate, &applied_rate));
  fail_unless (rate == -2.0);
  fail_unless (applied_rate == 1.0);

  gst_rtsp_range_free (range);

  fail_unless (gst_rtsp_media_unprepare (media));
  g_object_unref (media);

  gst_rtsp_url_free (url);
  g_object_unref (factory);

  g_object_unref (pool);

  gst_rtsp_thread_pool_cleanup ();
}

GST_END_TEST;

static void
media_playback_seek_one_active_stream (const gchar * launch_line)
{
  GstRTSPMediaFactory *factory;
  GstRTSPMedia *media;
  GstRTSPUrl *url;
  GstRTSPStream *stream1;
  GstRTSPStream *stream2;
  GstRTSPTimeRange *range;
  GstRTSPThreadPool *pool;
  GstRTSPThread *thread;
  GstRTSPTransport *transport;
  char *range_str;
  GstRTSPTimeRange *play_range;

  factory = gst_rtsp_media_factory_new ();
  fail_if (gst_rtsp_media_factory_is_shared (factory));
  fail_unless (gst_rtsp_url_parse ("rtsp://localhost:8554/test",
          &url) == GST_RTSP_OK);

  gst_rtsp_media_factory_set_launch (factory, launch_line);

  media = gst_rtsp_media_factory_construct (factory, url);
  fail_unless (GST_IS_RTSP_MEDIA (media));

  fail_unless (gst_rtsp_media_n_streams (media) == 2);

  stream1 = gst_rtsp_media_get_stream (media, 0);
  fail_unless (stream1 != NULL);

  pool = gst_rtsp_thread_pool_new ();
  thread = gst_rtsp_thread_pool_get_thread (pool,
      GST_RTSP_THREAD_TYPE_MEDIA, NULL);

  fail_unless (gst_rtsp_media_prepare (media, thread));
  fail_unless (media_has_sdp (media));

  /* define transport */
  fail_unless (gst_rtsp_transport_new (&transport) == GST_RTSP_OK);
  transport->lower_transport = GST_RTSP_LOWER_TRANS_TCP;

  fail_unless_equals_int64 (gst_rtsp_media_seekable (media), G_MAXINT64);

  /* video stream is complete and seekable */
  fail_unless (gst_rtsp_stream_complete_stream (stream1, transport));
  fail_unless (gst_rtsp_stream_seekable (stream1));

  /* audio stream is blocked (it does not contain any transport based part),
   * but it's seekable */
  stream2 = gst_rtsp_media_get_stream (media, 1);
  fail_unless (stream2 != NULL);
  fail_unless (gst_rtsp_stream_seekable (stream2));

  fail_unless (gst_rtsp_transport_free (transport) == GST_RTSP_OK);
  fail_unless (gst_rtsp_range_parse ("npt=3.0-5.0", &range) == GST_RTSP_OK);

  /* the media is seekable now */
  fail_unless (gst_rtsp_media_seek (media, range));

  /* verify that we got the expected range, 'npt=3.0-5.0' */
  range_str = gst_rtsp_media_get_range_string (media, TRUE, GST_RTSP_RANGE_NPT);
  fail_unless (gst_rtsp_range_parse (range_str, &play_range) == GST_RTSP_OK);
  fail_unless (play_range->min.seconds == range->min.seconds);
  fail_unless (play_range->max.seconds == range->max.seconds);

  gst_rtsp_range_free (range);
  gst_rtsp_range_free (play_range);
  g_free (range_str);

  fail_unless (gst_rtsp_media_unprepare (media));
  g_object_unref (media);

  gst_rtsp_url_free (url);
  g_object_unref (factory);

  g_object_unref (pool);

  gst_rtsp_thread_pool_cleanup ();
}

/* case: media is complete and contains two streams but only one is active,
   audio & video sources */
GST_START_TEST (test_media_playback_seek_one_active_stream)
{
  media_playback_seek_one_active_stream
      ("( videotestsrc ! rtpvrawpay pt=96 name=pay0 "
      " audiotestsrc ! audioconvert ! rtpL16pay name=pay1 )");
}

GST_END_TEST;

/* case: media is complete and contains two streams but only one is active,
   demux */
GST_START_TEST (test_media_playback_demux_seek_one_active_stream)
{
  /* FIXME: this test produces "Failed to push event" error messages in the
   * GST_DEBUG logs because the incomplete stream has no sinks */
  media_playback_seek_one_active_stream ("( filesrc location="
      GST_TEST_FILES_PATH "/test.avi !"
      " avidemux name=demux demux.audio_0 ! queue ! decodebin ! audioconvert !"
      " audioresample ! rtpL16pay pt=97 name=pay1"
      " demux.video_0 ! queue ! decodebin ! rtpvrawpay pt=96 name=pay0 )");
}

GST_END_TEST;

GST_START_TEST (test_media_seek_no_sinks)
{
  GstRTSPMediaFactory *factory;
  GstRTSPMedia *media;
  GstRTSPUrl *url;
  GstRTSPStream *stream;
  GstRTSPTimeRange *range;
  gchar *str;
  GstRTSPThreadPool *pool;
  GstRTSPThread *thread;

  factory = gst_rtsp_media_factory_new ();
  fail_if (gst_rtsp_media_factory_is_shared (factory));
  fail_unless (gst_rtsp_url_parse ("rtsp://localhost:8554/test",
          &url) == GST_RTSP_OK);

  gst_rtsp_media_factory_set_launch (factory,
      "( videotestsrc ! rtpvrawpay pt=96 name=pay0 )");

  media = gst_rtsp_media_factory_construct (factory, url);
  fail_unless (GST_IS_RTSP_MEDIA (media));

  fail_unless (gst_rtsp_media_n_streams (media) == 1);

  stream = gst_rtsp_media_get_stream (media, 0);
  fail_unless (stream != NULL);

  /* fails, need to be prepared */
  str = gst_rtsp_media_get_range_string (media, FALSE, GST_RTSP_RANGE_NPT);
  fail_unless (str == NULL);

  fail_unless (gst_rtsp_range_parse ("npt=5.0-", &range) == GST_RTSP_OK);
  /* fails, need to be prepared */
  fail_if (gst_rtsp_media_seek (media, range));

  pool = gst_rtsp_thread_pool_new ();
  thread = gst_rtsp_thread_pool_get_thread (pool,
      GST_RTSP_THREAD_TYPE_MEDIA, NULL);

  fail_unless (gst_rtsp_media_prepare (media, thread));
  fail_unless (media_has_sdp (media));

  str = gst_rtsp_media_get_range_string (media, FALSE, GST_RTSP_RANGE_NPT);
  fail_unless (g_str_equal (str, "npt=0-"));
  g_free (str);

  str = gst_rtsp_media_get_range_string (media, TRUE, GST_RTSP_RANGE_NPT);
  fail_unless (g_str_equal (str, "npt=0-"));
  g_free (str);

  /* fails, need to be prepared and contain sink elements */
  fail_if (gst_rtsp_media_seek (media, range));

  fail_unless (gst_rtsp_media_unprepare (media));

  /* should fail again */
  str = gst_rtsp_media_get_range_string (media, FALSE, GST_RTSP_RANGE_NPT);
  fail_unless (str == NULL);
  fail_if (gst_rtsp_media_seek (media, range));

  gst_rtsp_range_free (range);
  g_object_unref (media);

  gst_rtsp_url_free (url);
  g_object_unref (factory);

  g_object_unref (pool);

  gst_rtsp_thread_pool_cleanup ();
}

GST_END_TEST;

GST_START_TEST (test_media)
{
  GstRTSPMedia *media;
  GstElement *bin, *e1, *e2;

  bin = gst_bin_new ("bin");
  fail_if (bin == NULL);

  e1 = gst_element_factory_make ("videotestsrc", NULL);
  fail_if (e1 == NULL);

  e2 = gst_element_factory_make ("rtpvrawpay", "pay0");
  fail_if (e2 == NULL);
  g_object_set (e2, "pt", 96, NULL);

  gst_bin_add_many (GST_BIN_CAST (bin), e1, e2, NULL);
  gst_element_link_many (e1, e2, NULL);

  media = gst_rtsp_media_new (bin);
  fail_unless (GST_IS_RTSP_MEDIA (media));
  g_object_unref (media);
}

GST_END_TEST;

static void
test_prepare_reusable (const gchar * launch_line, gboolean is_live)
{
  GstRTSPMediaFactory *factory;
  GstRTSPMedia *media;
  GstRTSPUrl *url;
  GstRTSPThread *thread;
  GstRTSPThreadPool *pool;

  factory = gst_rtsp_media_factory_new ();
  fail_if (gst_rtsp_media_factory_is_shared (factory));
  fail_unless (gst_rtsp_url_parse ("rtsp://localhost:8554/test",
          &url) == GST_RTSP_OK);

  gst_rtsp_media_factory_set_launch (factory, launch_line);

  media = gst_rtsp_media_factory_construct (factory, url);
  fail_unless (GST_IS_RTSP_MEDIA (media));
  fail_unless (gst_rtsp_media_n_streams (media) == 1);

  g_object_set (G_OBJECT (media), "reusable", TRUE, NULL);

  pool = gst_rtsp_thread_pool_new ();
  thread = gst_rtsp_thread_pool_get_thread (pool,
      GST_RTSP_THREAD_TYPE_MEDIA, NULL);
  fail_unless (gst_rtsp_media_prepare (media, thread));
  fail_unless (media_has_sdp (media));
  if (is_live) {                /* Live is not seekable */
    fail_unless_equals_int64 (gst_rtsp_media_seekable (media), -1);
  } else {
    fail_unless_equals_int64 (gst_rtsp_media_seekable (media), G_MAXINT64);
  }
  fail_unless (gst_rtsp_media_unprepare (media));
  fail_unless (gst_rtsp_media_n_streams (media) == 1);

  thread = gst_rtsp_thread_pool_get_thread (pool,
      GST_RTSP_THREAD_TYPE_MEDIA, NULL);
  fail_unless (gst_rtsp_media_prepare (media, thread));
  fail_unless (media_has_sdp (media));
  fail_unless (gst_rtsp_media_unprepare (media));

  g_object_unref (media);
  gst_rtsp_url_free (url);
  g_object_unref (factory);

  g_object_unref (pool);
}

GST_START_TEST (test_media_reusable)
{

  /* test reusable media */
  test_prepare_reusable ("( videotestsrc ! rtpvrawpay pt=96 name=pay0 )",
      FALSE);
  test_prepare_reusable
      ("( videotestsrc is-live=true ! rtpvrawpay pt=96 name=pay0 )", TRUE);
}

GST_END_TEST;

GST_START_TEST (test_media_prepare)
{
  GstRTSPMediaFactory *factory;
  GstRTSPMedia *media;
  GstRTSPUrl *url;
  GstRTSPThreadPool *pool;
  GstRTSPThread *thread;

  pool = gst_rtsp_thread_pool_new ();

  /* test non-reusable media first */
  factory = gst_rtsp_media_factory_new ();
  fail_if (gst_rtsp_media_factory_is_shared (factory));
  fail_unless (gst_rtsp_url_parse ("rtsp://localhost:8554/test",
          &url) == GST_RTSP_OK);

  gst_rtsp_media_factory_set_launch (factory,
      "( videotestsrc ! rtpvrawpay pt=96 name=pay0 )");

  media = gst_rtsp_media_factory_construct (factory, url);
  fail_unless (GST_IS_RTSP_MEDIA (media));
  fail_unless (gst_rtsp_media_n_streams (media) == 1);

  thread = gst_rtsp_thread_pool_get_thread (pool,
      GST_RTSP_THREAD_TYPE_MEDIA, NULL);
  fail_unless (gst_rtsp_media_prepare (media, thread));
  fail_unless (media_has_sdp (media));
  fail_unless_equals_int64 (gst_rtsp_media_seekable (media), G_MAXINT64);
  fail_unless (gst_rtsp_media_unprepare (media));
  fail_unless (gst_rtsp_media_n_streams (media) == 1);

  thread = gst_rtsp_thread_pool_get_thread (pool,
      GST_RTSP_THREAD_TYPE_MEDIA, NULL);
  fail_if (gst_rtsp_media_prepare (media, thread));

  g_object_unref (media);
  gst_rtsp_url_free (url);
  g_object_unref (factory);

  g_object_unref (pool);
  gst_rtsp_thread_pool_cleanup ();
}

GST_END_TEST;

enum _SyncState
{
  SYNC_STATE_INIT,
  SYNC_STATE_1,
  SYNC_STATE_2,
  SYNC_STATE_RACE
};
typedef enum _SyncState SyncState;

struct _help_thread_data
{
  GstRTSPThreadPool *pool;
  GstRTSPMedia *media;
  GstRTSPTransport *transport;
  GstRTSPStream *stream;
  SyncState *state;
  GMutex *sync_mutex;
  GCond *sync_cond;
};
typedef struct _help_thread_data help_thread_data;

static gpointer
help_thread_main (gpointer user_data)
{
  help_thread_data *data;
  GstRTSPThread *thread;
  GPtrArray *transports;
  GstRTSPStreamTransport *stream_transport;

  data = (help_thread_data *) user_data;
  GST_INFO ("Another thread sharing media");

  /* wait SYNC_STATE_1 */
  g_mutex_lock (data->sync_mutex);
  while (*data->state < SYNC_STATE_1)
    g_cond_wait (data->sync_cond, data->sync_mutex);
  g_mutex_unlock (data->sync_mutex);

  /* prepare */
  thread = gst_rtsp_thread_pool_get_thread (data->pool,
      GST_RTSP_THREAD_TYPE_MEDIA, NULL);
  fail_unless (gst_rtsp_media_prepare (data->media, thread));

  /* set SYNC_STATE_2 */
  g_mutex_lock (data->sync_mutex);
  *data->state = SYNC_STATE_2;
  g_cond_signal (data->sync_cond);
  g_mutex_unlock (data->sync_mutex);

  /* wait SYNC_STATE_RACE */
  g_mutex_lock (data->sync_mutex);
  while (*data->state < SYNC_STATE_RACE)
    g_cond_wait (data->sync_cond, data->sync_mutex);
  g_mutex_unlock (data->sync_mutex);

  /* set state */
  transports = g_ptr_array_new_with_free_func (g_object_unref);
  fail_unless (transports != NULL);
  stream_transport =
      gst_rtsp_stream_transport_new (data->stream, data->transport);
  fail_unless (stream_transport != NULL);
  g_ptr_array_add (transports, stream_transport);
  fail_unless (gst_rtsp_media_set_state (data->media, GST_STATE_NULL,
          transports));

  /* clean up */
  GST_INFO ("Thread exit");
  fail_unless (gst_rtsp_media_unprepare (data->media));
  g_ptr_array_unref (transports);
  return NULL;
}

GST_START_TEST (test_media_shared_race_test_unsuspend_vs_set_state_null)
{
  help_thread_data data;
  GstRTSPMediaFactory *factory;
  GstRTSPMedia *media;
  GstRTSPUrl *url;
  GstRTSPThreadPool *pool;
  GstRTSPThread *thread;
  GThread *sharing_media_thread;
  GstRTSPTransport *transport;
  GstRTSPStream *stream;
  SyncState state = SYNC_STATE_INIT;
  GMutex sync_mutex;
  GCond sync_cond;

  g_mutex_init (&sync_mutex);
  g_cond_init (&sync_cond);

  pool = gst_rtsp_thread_pool_new ();

  /* test non-reusable media first */
  factory = gst_rtsp_media_factory_new ();
  gst_rtsp_media_factory_set_shared (factory, TRUE);
  fail_unless (gst_rtsp_url_parse ("rtsp://localhost:8554/test",
          &url) == GST_RTSP_OK);

  gst_rtsp_media_factory_set_launch (factory,
      "( videotestsrc ! rtpvrawpay pt=96 name=pay0 )");

  media = gst_rtsp_media_factory_construct (factory, url);
  fail_unless (GST_IS_RTSP_MEDIA (media));
  fail_unless (gst_rtsp_media_n_streams (media) == 1);
  gst_rtsp_media_set_suspend_mode (media, GST_RTSP_SUSPEND_MODE_RESET);

  stream = gst_rtsp_media_get_stream (media, 0);
  fail_unless (stream != NULL);

  thread = gst_rtsp_thread_pool_get_thread (pool,
      GST_RTSP_THREAD_TYPE_MEDIA, NULL);
  fail_unless (gst_rtsp_media_prepare (media, thread));

  /* help thread */
  data.pool = pool;
  data.media = media;
  data.stream = stream;
  data.state = &state;
  data.sync_mutex = &sync_mutex;
  data.sync_cond = &sync_cond;
  sharing_media_thread = g_thread_new ("new thread", help_thread_main, &data);
  fail_unless (sharing_media_thread != NULL);

  /* set state SYNC_STATE_1 */
  g_mutex_lock (&sync_mutex);
  state = SYNC_STATE_1;
  g_cond_signal (&sync_cond);
  g_mutex_unlock (&sync_mutex);

  /* wait SYNC_STATE_2 */
  g_mutex_lock (&sync_mutex);
  while (state < SYNC_STATE_2)
    g_cond_wait (&sync_cond, &sync_mutex);
  g_mutex_unlock (&sync_mutex);

  gst_rtsp_media_suspend (media);

  fail_unless (gst_rtsp_transport_new (&transport) == GST_RTSP_OK);
  transport->lower_transport = GST_RTSP_LOWER_TRANS_TCP;
  fail_unless (gst_rtsp_stream_complete_stream (stream, transport));
  data.transport = transport;

  /* set state SYNC_STATE_RACE let the race begin unsuspend <-> set state GST_STATE_NULL */
  g_mutex_lock (&sync_mutex);
  state = SYNC_STATE_RACE;
  g_cond_signal (&sync_cond);
  g_mutex_unlock (&sync_mutex);

  fail_unless (gst_rtsp_media_unsuspend (media));

  /* sync end of other thread */
  g_thread_join (sharing_media_thread);

  /* clean up */
  g_cond_clear (&sync_cond);
  g_mutex_clear (&sync_mutex);
  fail_unless (gst_rtsp_media_unprepare (media));
  g_object_unref (media);
  gst_rtsp_url_free (url);
  g_object_unref (factory);
  g_object_unref (pool);
  gst_rtsp_thread_pool_cleanup ();
}

GST_END_TEST;


#define FLAG_HAVE_CAPS GST_ELEMENT_FLAG_LAST
static void
on_notify_caps (GstPad * pad, GParamSpec * pspec, GstElement * pay)
{
  GstCaps *caps;

  g_object_get (pad, "caps", &caps, NULL);

  GST_DEBUG ("notify %" GST_PTR_FORMAT, caps);

  if (caps) {
    if (!GST_OBJECT_FLAG_IS_SET (pay, FLAG_HAVE_CAPS)) {
      g_signal_emit_by_name (pay, "pad-added", pad);
      g_signal_emit_by_name (pay, "no-more-pads", NULL);
      GST_OBJECT_FLAG_SET (pay, FLAG_HAVE_CAPS);
    }
    gst_caps_unref (caps);
  } else {
    if (GST_OBJECT_FLAG_IS_SET (pay, FLAG_HAVE_CAPS)) {
      g_signal_emit_by_name (pay, "pad-removed", pad);
      GST_OBJECT_FLAG_UNSET (pay, FLAG_HAVE_CAPS);
    }
  }
}

GST_START_TEST (test_media_dyn_prepare)
{
  GstRTSPMedia *media;
  GstElement *bin, *src, *pay;
  GstElement *pipeline;
  GstPad *srcpad;
  GstRTSPThreadPool *pool;
  GstRTSPThread *thread;

  bin = gst_bin_new ("bin");
  fail_if (bin == NULL);

  src = gst_element_factory_make ("videotestsrc", NULL);
  fail_if (src == NULL);

  pay = gst_element_factory_make ("rtpvrawpay", "dynpay0");
  fail_if (pay == NULL);
  g_object_set (pay, "pt", 96, NULL);

  gst_bin_add_many (GST_BIN_CAST (bin), src, pay, NULL);
  gst_element_link_many (src, pay, NULL);

  media = gst_rtsp_media_new (bin);
  fail_unless (GST_IS_RTSP_MEDIA (media));

  g_object_set (G_OBJECT (media), "reusable", TRUE, NULL);

  pipeline = gst_pipeline_new ("media-pipeline");
  gst_rtsp_media_take_pipeline (media, GST_PIPELINE_CAST (pipeline));

  gst_rtsp_media_collect_streams (media);

  srcpad = gst_element_get_static_pad (pay, "src");

  g_signal_connect (srcpad, "notify::caps", (GCallback) on_notify_caps, pay);

  pool = gst_rtsp_thread_pool_new ();

  fail_unless (gst_rtsp_media_n_streams (media) == 0);

  thread = gst_rtsp_thread_pool_get_thread (pool,
      GST_RTSP_THREAD_TYPE_MEDIA, NULL);
  fail_unless (gst_rtsp_media_prepare (media, thread));
  fail_unless (gst_rtsp_media_n_streams (media) == 1);
  fail_unless (media_has_sdp (media));
  fail_unless_equals_int64 (gst_rtsp_media_seekable (media), G_MAXINT64);
  fail_unless (gst_rtsp_media_unprepare (media));
  fail_unless (gst_rtsp_media_n_streams (media) == 0);

  thread = gst_rtsp_thread_pool_get_thread (pool,
      GST_RTSP_THREAD_TYPE_MEDIA, NULL);
  fail_unless (gst_rtsp_media_prepare (media, thread));
  fail_unless (gst_rtsp_media_n_streams (media) == 1);
  fail_unless (media_has_sdp (media));
  fail_unless_equals_int64 (gst_rtsp_media_seekable (media), G_MAXINT64);
  fail_unless (gst_rtsp_media_unprepare (media));
  fail_unless (gst_rtsp_media_n_streams (media) == 0);

  gst_object_unref (srcpad);
  g_object_unref (media);
  g_object_unref (pool);

  gst_rtsp_thread_pool_cleanup ();
}

GST_END_TEST;

GST_START_TEST (test_media_take_pipeline)
{
  GstRTSPMediaFactory *factory;
  GstRTSPMedia *media;
  GstRTSPUrl *url;
  GstElement *pipeline;

  factory = gst_rtsp_media_factory_new ();
  fail_unless (gst_rtsp_url_parse ("rtsp://localhost:8554/test",
          &url) == GST_RTSP_OK);
  gst_rtsp_media_factory_set_launch (factory,
      "( fakesrc ! text/plain ! rtpgstpay name=pay0 )");

  media = gst_rtsp_media_factory_construct (factory, url);
  fail_unless (GST_IS_RTSP_MEDIA (media));

  pipeline = gst_pipeline_new ("media-pipeline");
  gst_rtsp_media_take_pipeline (media, GST_PIPELINE_CAST (pipeline));

  g_object_unref (media);
  gst_rtsp_url_free (url);
  g_object_unref (factory);
}

GST_END_TEST;

GST_START_TEST (test_media_reset)
{
  GstRTSPMediaFactory *factory;
  GstRTSPMedia *media;
  GstRTSPUrl *url;
  GstRTSPThreadPool *pool;
  GstRTSPThread *thread;

  pool = gst_rtsp_thread_pool_new ();

  factory = gst_rtsp_media_factory_new ();
  fail_if (gst_rtsp_media_factory_is_shared (factory));
  gst_rtsp_url_parse ("rtsp://localhost:8554/test", &url);

  gst_rtsp_media_factory_set_launch (factory,
      "( videotestsrc ! rtpvrawpay pt=96 name=pay0 )");

  media = gst_rtsp_media_factory_construct (factory, url);
  fail_unless (GST_IS_RTSP_MEDIA (media));

  thread = gst_rtsp_thread_pool_get_thread (pool,
      GST_RTSP_THREAD_TYPE_MEDIA, NULL);
  fail_unless (gst_rtsp_media_prepare (media, thread));
  fail_unless (media_has_sdp (media));
  fail_unless_equals_int64 (gst_rtsp_media_seekable (media), G_MAXINT64);
  fail_unless (gst_rtsp_media_suspend (media));
  fail_unless (gst_rtsp_media_unprepare (media));
  g_object_unref (media);

  media = gst_rtsp_media_factory_construct (factory, url);
  fail_unless (GST_IS_RTSP_MEDIA (media));

  thread = gst_rtsp_thread_pool_get_thread (pool,
      GST_RTSP_THREAD_TYPE_MEDIA, NULL);
  gst_rtsp_media_set_suspend_mode (media, GST_RTSP_SUSPEND_MODE_RESET);
  fail_unless (gst_rtsp_media_prepare (media, thread));
  fail_unless (media_has_sdp (media));
  fail_unless_equals_int64 (gst_rtsp_media_seekable (media), G_MAXINT64);
  fail_unless (gst_rtsp_media_suspend (media));
  fail_unless (gst_rtsp_media_unprepare (media));
  g_object_unref (media);

  gst_rtsp_url_free (url);
  g_object_unref (factory);
  g_object_unref (pool);

  gst_rtsp_thread_pool_cleanup ();
}

GST_END_TEST;

GST_START_TEST (test_media_multidyn_prepare)
{
  GstRTSPMedia *media;
  GstElement *bin, *src0, *pay0, *src1, *pay1;
  GstElement *pipeline;
  GstPad *srcpad0, *srcpad1;
  GstRTSPThreadPool *pool;
  GstRTSPThread *thread;

  bin = gst_bin_new ("bin");
  fail_if (bin == NULL);

  src0 = gst_element_factory_make ("videotestsrc", NULL);
  fail_if (src0 == NULL);

  pay0 = gst_element_factory_make ("rtpvrawpay", "dynpay0");
  fail_if (pay0 == NULL);
  g_object_set (pay0, "pt", 96, NULL);

  src1 = gst_element_factory_make ("videotestsrc", NULL);
  fail_if (src1 == NULL);

  pay1 = gst_element_factory_make ("rtpvrawpay", "dynpay1");
  fail_if (pay1 == NULL);
  g_object_set (pay1, "pt", 97, NULL);

  gst_bin_add_many (GST_BIN_CAST (bin), src0, pay0, src1, pay1, NULL);
  gst_element_link_many (src0, pay0, NULL);
  gst_element_link_many (src1, pay1, NULL);

  media = gst_rtsp_media_new (bin);
  fail_unless (GST_IS_RTSP_MEDIA (media));

  g_object_set (G_OBJECT (media), "reusable", TRUE, NULL);

  pipeline = gst_pipeline_new ("media-pipeline");
  gst_rtsp_media_take_pipeline (media, GST_PIPELINE_CAST (pipeline));

  gst_rtsp_media_collect_streams (media);

  srcpad0 = gst_element_get_static_pad (pay0, "src");
  srcpad1 = gst_element_get_static_pad (pay1, "src");

  g_signal_connect (srcpad0, "notify::caps", (GCallback) on_notify_caps, pay0);
  g_signal_connect (srcpad1, "notify::caps", (GCallback) on_notify_caps, pay1);

  pool = gst_rtsp_thread_pool_new ();

  fail_unless_equals_int (gst_rtsp_media_n_streams (media), 0);

  thread = gst_rtsp_thread_pool_get_thread (pool,
      GST_RTSP_THREAD_TYPE_MEDIA, NULL);
  fail_unless (gst_rtsp_media_prepare (media, thread));
  fail_unless_equals_int (gst_rtsp_media_n_streams (media), 2);
  fail_unless (media_has_sdp (media));
  fail_unless_equals_int64 (gst_rtsp_media_seekable (media), G_MAXINT64);
  fail_unless (gst_rtsp_media_unprepare (media));
  fail_unless_equals_int (gst_rtsp_media_n_streams (media), 0);

  thread = gst_rtsp_thread_pool_get_thread (pool,
      GST_RTSP_THREAD_TYPE_MEDIA, NULL);
  fail_unless (gst_rtsp_media_prepare (media, thread));
  fail_unless_equals_int (gst_rtsp_media_n_streams (media), 2);
  fail_unless (media_has_sdp (media));
  fail_unless_equals_int64 (gst_rtsp_media_seekable (media), G_MAXINT64);
  fail_unless (gst_rtsp_media_unprepare (media));
  fail_unless_equals_int (gst_rtsp_media_n_streams (media), 0);

  gst_object_unref (srcpad0);
  gst_object_unref (srcpad1);
  g_object_unref (media);
  g_object_unref (pool);

  gst_rtsp_thread_pool_cleanup ();
}

GST_END_TEST;

static gboolean
pipeline_error (GstRTSPMedia * media, GstMessage * message, guint * data)
{
  GError *gerror = NULL;

  /* verify that the correct error was received */
  gst_message_parse_error (message, &gerror, NULL);
  ck_assert_str_eq (GST_MESSAGE_SRC_NAME (message), "src0");
  ck_assert_ptr_ne (gerror, NULL);
  ck_assert_int_eq (gerror->domain, GST_STREAM_ERROR);
  ck_assert_int_eq (gerror->code, GST_STREAM_ERROR_FAILED);
  ck_assert_str_eq (gerror->message, "Internal data stream error.");
  (*data)++;

  return TRUE;
}

GST_START_TEST (test_media_pipeline_error)
{
  GstRTSPMediaFactory *factory;
  GstRTSPMedia *media;
  GstRTSPUrl *url;
  GstRTSPThreadPool *pool;
  GstRTSPThread *thread;
  guint handled_messages = 0;

  pool = gst_rtsp_thread_pool_new ();

  factory = gst_rtsp_media_factory_new ();
  ck_assert (!gst_rtsp_media_factory_is_shared (factory));
  ck_assert (gst_rtsp_url_parse ("rtsp://localhost:8554/test",
          &url) == GST_RTSP_OK);

  /* add faulty caps filter to fail linking when preparing media, this will
   * result in an error being posted on the pipelines bus. */
  gst_rtsp_media_factory_set_launch (factory,
      "( videotestsrc name=src0 ! video/fail_prepare ! rtpvrawpay pt=96 name=pay0 )");

  media = gst_rtsp_media_factory_construct (factory, url);
  ck_assert (GST_IS_RTSP_MEDIA (media));
  ck_assert_int_eq (gst_rtsp_media_n_streams (media), 1);

  /* subscribe to pipeline errors */
  g_signal_connect (media, "handle-message::error", G_CALLBACK (pipeline_error),
      &handled_messages);

  thread = gst_rtsp_thread_pool_get_thread (pool,
      GST_RTSP_THREAD_TYPE_MEDIA, NULL);
  ck_assert (!gst_rtsp_media_prepare (media, thread));
  ck_assert_uint_eq (handled_messages, 1);

  g_object_unref (media);
  gst_rtsp_url_free (url);
  g_object_unref (factory);

  g_object_unref (pool);
  gst_rtsp_thread_pool_cleanup ();
}

GST_END_TEST;


static Suite *
rtspmedia_suite (void)
{
  Suite *s = suite_create ("rtspmedia");
  TCase *tc = tcase_create ("general");
  gboolean has_avidemux;

  suite_add_tcase (s, tc);
  tcase_set_timeout (tc, 20);

  has_avidemux = gst_registry_check_feature_version (gst_registry_get (),
      "avidemux", GST_VERSION_MAJOR, GST_VERSION_MINOR, 0);

  tcase_add_test (tc, test_media_seek);
  tcase_add_test (tc, test_media_seek_no_sinks);
  tcase_add_test (tc, test_media_playback_seek_one_active_stream);
  if (has_avidemux) {
    tcase_add_test (tc, test_media_playback_demux_seek_one_active_stream);
  } else {
    GST_INFO ("Skipping test, missing plugins: avidemux");
  }
  tcase_add_test (tc, test_media);
  tcase_add_test (tc, test_media_prepare);
  tcase_add_test (tc, test_media_shared_race_test_unsuspend_vs_set_state_null);
  tcase_add_test (tc, test_media_reusable);
  tcase_add_test (tc, test_media_dyn_prepare);
  tcase_add_test (tc, test_media_take_pipeline);
  tcase_add_test (tc, test_media_reset);
  tcase_add_test (tc, test_media_multidyn_prepare);
  tcase_add_test (tc, test_media_pipeline_error);

  return s;
}

GST_CHECK_MAIN (rtspmedia);
