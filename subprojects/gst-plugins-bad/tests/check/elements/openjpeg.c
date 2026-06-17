/* GStreamer
 *
 * Copyright (c) 2010 Sebastian Dröge <sebastian.droege@collabora.co.uk>
 * Copyright (c) 2010 David Schleef <ds@schleef.org>
 * Copyright (c) 2014 Thijs Vermeir <thijs.vermeir@barco.com>
 * Copyright (c) 2021 Stéphane Cerveau <scerveau@collabora.com>
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
#include <gst/check/gstharness.h>
#include <gst/video/video.h>

static GstStaticPadTemplate enc_sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("image/x-j2c, "
        "width = (int) [16, MAX], "
        "height = (int) [16, MAX], " "framerate = (fraction) [0, MAX]"));

static GstStaticPadTemplate enc_srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw, "
        "format = (string) I420, "
        "width = (int) [16, MAX], "
        "height = (int) [16, MAX], " "framerate = (fraction) [0, MAX]"));

static GstStaticPadTemplate dec_sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw"));

static GstStaticPadTemplate jp2_srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("image/jp2"));


#define MAX_THREADS 8
#define NUM_BUFFERS 4
#define FRAME_RATE 1000

static GstPad *sinkpad, *srcpad;

typedef struct _OpenJPEGData
{
  GMainLoop *loop;
  gboolean failing_pipeline;
} OpenJPEGData;

static GstElement *
setup_openjpegenc (const gchar * src_caps_str, gint num_stripes)
{
  GstElement *openjpegenc;
  GstCaps *srccaps = NULL;
  GstBus *bus;

  if (src_caps_str) {
    srccaps = gst_caps_from_string (src_caps_str);
    fail_unless (srccaps != NULL);
  }

  openjpegenc = gst_check_setup_element ("openjpegenc");
  fail_unless (openjpegenc != NULL);
  g_object_set (openjpegenc, "num-stripes", num_stripes, NULL);
  srcpad = gst_check_setup_src_pad (openjpegenc, &enc_srctemplate);
  sinkpad = gst_check_setup_sink_pad (openjpegenc, &enc_sinktemplate);
  gst_pad_set_active (srcpad, TRUE);
  gst_pad_set_active (sinkpad, TRUE);

  gst_check_setup_events (srcpad, openjpegenc, srccaps, GST_FORMAT_TIME);

  bus = gst_bus_new ();
  gst_element_set_bus (openjpegenc, bus);

  fail_unless (gst_element_set_state (openjpegenc,
          GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE,
      "could not set to playing");

  if (srccaps)
    gst_caps_unref (srccaps);

  buffers = NULL;
  return openjpegenc;
}

static void
cleanup_openjpegenc (GstElement * openjpegenc)
{
  GstBus *bus;

  /* Free parsed buffers */
  gst_check_drop_buffers ();

  bus = GST_ELEMENT_BUS (openjpegenc);
  gst_bus_set_flushing (bus, TRUE);
  gst_object_unref (bus);

  gst_pad_set_active (srcpad, FALSE);
  gst_pad_set_active (sinkpad, FALSE);
  gst_check_teardown_src_pad (openjpegenc);
  gst_check_teardown_sink_pad (openjpegenc);
  gst_check_teardown_element (openjpegenc);
}

GST_START_TEST (test_openjpeg_encode_simple)
{
  GstElement *openjpegenc;
  GstBuffer *buffer;
  gint i;
  GList *l;
  GstCaps *outcaps, *sinkcaps;
  GstSegment seg;

  openjpegenc =
      setup_openjpegenc
      ("video/x-raw,format=(string)I420,width=(int)320,height=(int)240,framerate=(fraction)25/1",
      1);

  gst_segment_init (&seg, GST_FORMAT_TIME);
  seg.stop = gst_util_uint64_scale (10, GST_SECOND, 25);

  fail_unless (gst_pad_push_event (srcpad, gst_event_new_segment (&seg)));

  buffer = gst_buffer_new_allocate (NULL, 320 * 240 + 2 * 160 * 120, NULL);
  gst_buffer_memset (buffer, 0, 0, -1);

  for (i = 0; i < 10; i++) {
    GST_BUFFER_TIMESTAMP (buffer) = gst_util_uint64_scale (i, GST_SECOND, 25);
    GST_BUFFER_DURATION (buffer) = gst_util_uint64_scale (1, GST_SECOND, 25);
    fail_unless (gst_pad_push (srcpad, gst_buffer_ref (buffer)) == GST_FLOW_OK);
  }

  gst_buffer_unref (buffer);

  fail_unless (gst_pad_push_event (srcpad, gst_event_new_eos ()));

  /* All buffers must be there now */
  fail_unless_equals_int (g_list_length (buffers), 10);

  outcaps =
      gst_caps_from_string
      ("image/x-j2c,width=(int)320,height=(int)240,framerate=(fraction)25/1");

  for (l = buffers, i = 0; l; l = l->next, i++) {
    buffer = l->data;

    fail_unless_equals_uint64 (GST_BUFFER_DURATION (buffer),
        gst_util_uint64_scale (1, GST_SECOND, 25));

    sinkcaps = gst_pad_get_current_caps (sinkpad);
    fail_unless (gst_caps_can_intersect (sinkcaps, outcaps));
    gst_caps_unref (sinkcaps);
  }

  gst_caps_unref (outcaps);

  cleanup_openjpegenc (openjpegenc);
}

GST_END_TEST;

static gboolean
bus_cb (GstBus * bus, GstMessage * message, gpointer data)
{
  OpenJPEGData *opj_data = (OpenJPEGData *) data;
  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:{
      GError *err = NULL;
      gchar *debug = NULL;

      gst_message_parse_error (message, &err, &debug);

      GST_ERROR ("Error: %s : %s", err->message, debug);
      g_error_free (err);
      g_free (debug);
      fail_if (!opj_data->failing_pipeline, "failed");
      g_main_loop_quit (opj_data->loop);
      break;
    }
    case GST_MESSAGE_EOS:
      fail_if (opj_data->failing_pipeline, "failed");
      g_main_loop_quit (opj_data->loop);
      break;
    default:
      break;
  }
  return TRUE;
}

static void
run_openjpeg_pipeline (const gchar * in_format, gint width, gint height,
    gint num_stripes, gint enc_threads, gint dec_threads,
    gboolean failing_pipeline)
{
  GstBus *bus;
  OpenJPEGData opj_data;
  GstElement *pipeline;
  gchar *pipeline_str =
      g_strdup_printf ("videotestsrc num-buffers=%d ! "
      "video/x-raw,format=%s, width=%d, height=%d, framerate=%d/1 ! openjpegenc num-stripes=%d num-threads=%d ! jpeg2000parse"
      " ! openjpegdec max-threads=%d ! fakevideosink",
      NUM_BUFFERS, in_format, width, height, FRAME_RATE, num_stripes,
      enc_threads, dec_threads);
  GST_LOG ("Running pipeline: %s", pipeline_str);
  pipeline = gst_parse_launch (pipeline_str, NULL);
  fail_unless (pipeline != NULL);
  g_free (pipeline_str);

  opj_data.loop = g_main_loop_new (NULL, FALSE);
  opj_data.failing_pipeline = failing_pipeline;

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  gst_bus_add_watch (bus, (GstBusFunc) bus_cb, &opj_data);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  g_main_loop_run (opj_data.loop);
  gst_element_set_state (pipeline, GST_STATE_NULL);

  gst_bus_remove_watch (bus);
  gst_object_unref (bus);
  gst_object_unref (pipeline);
  g_main_loop_unref (opj_data.loop);
}


GST_START_TEST (test_openjpeg_simple)
{
  int i;
  const gchar *in_format_list[] = {
    "ARGB64", "ARGB", "xRGB", "AYUV64", "Y444_10LE", "I422_10LE", "I420_10LE",
    "Y444_12LE", "I422_12LE", "I420_12LE", "Y444_16LE",
    "GBR_10LE", "GBR_12LE", "GBR_16LE",
    "AYUV", "Y444", "Y42B", "Y41B", "YUV9", "I420", "GRAY8", "GRAY16_LE"
  };

  for (i = 0; i < G_N_ELEMENTS (in_format_list); i++) {
    run_openjpeg_pipeline (in_format_list[i], 320, 200, 1, 1, 1, FALSE);
  }

  /* Check that the pipeline is failing properly */
  run_openjpeg_pipeline (in_format_list[0], 16, 16, 1, 0, 0, TRUE);
  run_openjpeg_pipeline (in_format_list[0], 16, 16, 1, 1, 1, TRUE);

  for (i = 1; i < 8; i++) {
    run_openjpeg_pipeline (in_format_list[0], 320, 200, i, 0, 0, FALSE);
    run_openjpeg_pipeline (in_format_list[0], 320, 200, i, 1, 0, FALSE);
    run_openjpeg_pipeline (in_format_list[0], 320, 200, i, 0, 1, FALSE);
    run_openjpeg_pipeline (in_format_list[0], 320, 200, i, 0, 4, FALSE);
    run_openjpeg_pipeline (in_format_list[0], 320, 200, i, 5, 3, FALSE);
    run_openjpeg_pipeline (in_format_list[0], 320, 200, i, 8, 8, FALSE);
  }
}

GST_END_TEST;


GST_START_TEST (test_openjpeg_yuv_format_validate_caps)
{
  GstElement *pipeline, *dec;
  GstBus *bus;
  GstMessage *msg;
  GstPad *srcpad;
  GstCaps *caps;
  const GstStructure *s;
  const gchar *format;

  pipeline = gst_parse_launch ("videotestsrc num-buffers=1 pattern=gradient ! "
      "video/x-raw,format=Y444,width=32,height=32 ! "
      "openjpegenc ! jpeg2000parse ! openjpegdec name=dec ! fakesink", NULL);
  fail_unless (pipeline != NULL);

  fail_unless (gst_element_set_state (pipeline,
          GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE,
      "could not set pipeline to playing");

  bus = gst_element_get_bus (pipeline);
  msg = gst_bus_timed_pop_filtered (bus, GST_SECOND * 5,
      GST_MESSAGE_EOS | GST_MESSAGE_ERROR);
  fail_unless (msg != NULL);
  fail_unless (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_EOS);
  gst_message_unref (msg);

  dec = gst_bin_get_by_name (GST_BIN (pipeline), "dec");
  fail_unless (dec != NULL);
  srcpad = gst_element_get_static_pad (dec, "src");
  fail_unless (srcpad != NULL);
  caps = gst_pad_get_current_caps (srcpad);
  fail_unless (caps != NULL);

  s = gst_caps_get_structure (caps, 0);
  fail_unless (gst_structure_has_name (s, "video/x-raw"));
  format = gst_structure_get_string (s, "format");
  fail_unless (format != NULL);
  fail_unless_equals_string (format, "Y444");

  gst_caps_unref (caps);
  gst_object_unref (srcpad);
  gst_object_unref (dec);
  gst_object_unref (bus);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
}

GST_END_TEST;


/* Push a JP2 file through openjpegdec and return the decoded video buffer.
 * Caller must gst_buffer_unref() the result. */
static GstBuffer *
decode_jp2_reference (const gchar * path)
{
  GstElement *dec;
  GstPad *src, *sink;
  GstBuffer *in_buf, *out_buf;
  GstCaps *caps;
  guint8 *data;
  gsize size;
  GError *err = NULL;

  fail_unless (g_file_get_contents (path, (gchar **) & data, &size, &err),
      "Failed to load JP2 reference file %s: %s", path,
      err ? err->message : "unknown error");
  g_clear_error (&err);

  dec = gst_check_setup_element ("openjpegdec");
  src = gst_check_setup_src_pad (dec, &jp2_srctemplate);
  sink = gst_check_setup_sink_pad (dec, &dec_sinktemplate);
  gst_pad_set_active (src, TRUE);
  gst_pad_set_active (sink, TRUE);

  caps = gst_caps_new_simple ("image/jp2",
      "width", G_TYPE_INT, 32,
      "height", G_TYPE_INT, 32, "framerate", GST_TYPE_FRACTION, 1, 1, NULL);
  gst_check_setup_events (src, dec, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);

  fail_unless (gst_element_set_state (dec,
          GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);

  in_buf = gst_buffer_new_wrapped (data, size);
  GST_BUFFER_PTS (in_buf) = 0;
  GST_BUFFER_DURATION (in_buf) = GST_SECOND;
  fail_unless_equals_int (gst_pad_push (src, in_buf), GST_FLOW_OK);
  fail_unless_equals_int (g_list_length (buffers), 1);

  out_buf = gst_buffer_ref (buffers->data);

  gst_check_drop_buffers ();
  gst_pad_set_active (src, FALSE);
  gst_pad_set_active (sink, FALSE);
  gst_check_teardown_src_pad (dec);
  gst_check_teardown_sink_pad (dec);
  gst_check_teardown_element (dec);

  return out_buf;
}

/* Run videotestsrc for format and return the raw video buffer.
 * Caller must gst_buffer_unref() the result. */
static GstBuffer *
get_raw_frame (const gchar * format)
{
  GstElement *pipeline, *sink;
  GstBuffer *out_buf;
  GstBus *bus;
  GstMessage *msg;
  GstSample *sample = NULL;
  gchar *desc;

  desc = g_strdup_printf ("videotestsrc num-buffers=1 pattern=25 ! "
      "video/x-raw,format=%s,width=32,height=32,framerate=1/1 ! "
      "appsink name=sink sync=false wait-on-eos=false", format);
  pipeline = gst_parse_launch (desc, NULL);
  g_free (desc);
  fail_unless (pipeline != NULL);

  sink = gst_bin_get_by_name (GST_BIN (pipeline), "sink");

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  bus = gst_element_get_bus (pipeline);
  msg = gst_bus_timed_pop_filtered (bus, 5 * GST_SECOND,
      GST_MESSAGE_EOS | GST_MESSAGE_ERROR);
  fail_unless (msg != NULL);
  fail_unless (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_EOS,
      "Pipeline error getting raw frame for format %s", format);
  gst_message_unref (msg);
  gst_object_unref (bus);

  g_signal_emit_by_name (sink, "pull-sample", &sample);
  fail_unless (sample != NULL, "No raw sample for format %s", format);
  out_buf = gst_buffer_ref (gst_sample_get_buffer (sample));
  gst_sample_unref (sample);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (sink);
  gst_object_unref (pipeline);

  return out_buf;
}

/* Push raw_buf through openjpegenc ! openjpegdec and return the decoded buffer.
 * Caller must gst_buffer_unref() the result. */
static GstBuffer *
round_trip_buffer (GstBuffer * raw_buf, const gchar * format)
{
  GstHarness *h;
  GstBuffer *out_buf;
  GstCaps *caps;

  h = gst_harness_new_parse ("openjpegenc ! jpeg2000parse ! openjpegdec");

  caps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, format,
      "width", G_TYPE_INT, 32,
      "height", G_TYPE_INT, 32, "framerate", GST_TYPE_FRACTION, 1, 1, NULL);
  gst_harness_set_src_caps (h, caps);

  out_buf = gst_harness_push_and_pull (h, gst_buffer_ref (raw_buf));
  fail_unless (out_buf != NULL, "No buffer for format %s", format);

  gst_harness_teardown (h);

  return out_buf;
}

GST_START_TEST (test_openjpeg_encode_decode_formats)
{
  static const struct
  {
    const gchar *format;
    const gchar *filename;
  } refs[] = {
    {"ARGB64", "ref_ARGB64.jp2"},
    {"ARGB", "ref_ARGB.jp2"},
    {"xRGB", "ref_xRGB.jp2"},
    {"GBR_10LE", "ref_GBR_10LE.jp2"},
    {"GBR_12LE", "ref_GBR_12LE.jp2"},
    {"GBR_16LE", "ref_GBR_16LE.jp2"},
    {"AYUV64", "ref_AYUV64.jp2"},
    {"Y444_10LE", "ref_Y444_10LE.jp2"},
    {"I422_10LE", "ref_I422_10LE.jp2"},
    {"I420_10LE", "ref_I420_10LE.jp2"},
    {"Y444_12LE", "ref_Y444_12LE.jp2"},
    {"I422_12LE", "ref_I422_12LE.jp2"},
    {"I420_12LE", "ref_I420_12LE.jp2"},
    {"Y444_16LE", "ref_Y444_16LE.jp2"},
    {"AYUV", "ref_AYUV.jp2"},
    {"Y444", "ref_Y444.jp2"},
    {"Y42B", "ref_Y42B.jp2"},
    {"I420", "ref_I420.jp2"},
    {"Y41B", "ref_Y41B.jp2"},
    {"YUV9", "ref_YUV9.jp2"},
    {"GRAY8", "ref_GRAY8.jp2"},
    {"GRAY16_LE", "ref_GRAY16_LE.jp2"},
  };
  gint i;

  for (i = 0; i < (gint) G_N_ELEMENTS (refs); i++) {
    GstBuffer *raw_buf, *ref_jp2_decoded_buf, *rt_raw_buf;
    GstMapInfo raw_map, ref_jp2_decoded_map, rt_decoded_map;
    gchar *path;

    GST_LOG ("Testing format: %s", refs[i].format);

    path = g_build_filename (GST_OPENJPEG_TEST_DATA_PATH, refs[i].filename,
        NULL);
    raw_buf = get_raw_frame (refs[i].format);
    ref_jp2_decoded_buf = decode_jp2_reference (path);
    g_free (path);
    rt_raw_buf = round_trip_buffer (raw_buf, refs[i].format);

    fail_unless (gst_buffer_map (raw_buf, &raw_map, GST_MAP_READ));
    fail_unless (gst_buffer_map (ref_jp2_decoded_buf, &ref_jp2_decoded_map,
            GST_MAP_READ));
    fail_unless (gst_buffer_map (rt_raw_buf, &rt_decoded_map, GST_MAP_READ));

    /* Decoder validation: decoding the embedded reference must reproduce the
     * original raw frame (valid because encoding is lossless by default).
     *
     * xRGB is compared pixel-by-pixel skipping the X padding byte: the encoder
     * treats it as 3-component RGB, so the decoded output is RGB (3 bytes/pixel)
     * rather than xRGB (4 bytes/pixel). */
    if (g_strcmp0 (refs[i].format, "xRGB") == 0) {
      gsize p, num_pixels = raw_map.size / 4;
      fail_unless_equals_int (ref_jp2_decoded_map.size, num_pixels * 3);
      for (p = 0; p < num_pixels; p++) {
        guint8 *raw_px = raw_map.data + p * 4;
        guint8 *dec_px = ref_jp2_decoded_map.data + p * 3;
        fail_unless (raw_px[1] == dec_px[0] && raw_px[2] == dec_px[1]
            && raw_px[3] == dec_px[2],
            "Decoder pixel mismatch for xRGB at pixel %" G_GSIZE_FORMAT, p);
      }
    } else {
      fail_unless (raw_map.size == ref_jp2_decoded_map.size,
          "Buffer size mismatch for format %s: raw=%" G_GSIZE_FORMAT
          " ref=%" G_GSIZE_FORMAT, refs[i].format, raw_map.size,
          ref_jp2_decoded_map.size);
      fail_unless (memcmp (raw_map.data, ref_jp2_decoded_map.data,
              raw_map.size) == 0, "Decoder pixel mismatch for format %s",
          refs[i].format);
    }

    /* Encoder validation: round-trip output must match the embedded reference. */
    fail_unless (ref_jp2_decoded_map.size == rt_decoded_map.size,
        "Buffer size mismatch for format %s: ref=%" G_GSIZE_FORMAT
        " rt=%" G_GSIZE_FORMAT, refs[i].format, ref_jp2_decoded_map.size,
        rt_decoded_map.size);
    fail_unless (memcmp (ref_jp2_decoded_map.data, rt_decoded_map.data,
            ref_jp2_decoded_map.size) == 0,
        "Encoder round-trip pixel mismatch for format %s", refs[i].format);

    gst_buffer_unmap (raw_buf, &raw_map);
    gst_buffer_unmap (ref_jp2_decoded_buf, &ref_jp2_decoded_map);
    gst_buffer_unmap (rt_raw_buf, &rt_decoded_map);
    gst_buffer_unref (raw_buf);
    gst_buffer_unref (ref_jp2_decoded_buf);
    gst_buffer_unref (rt_raw_buf);
  }
}

GST_END_TEST;

static Suite *
openjpeg_suite (void)
{
  Suite *s = suite_create ("openjpeg");
  TCase *tc_chain = tcase_create ("general");
  TCase *tc_chain_content = tcase_create ("payload-validation");

  suite_add_tcase (s, tc_chain);
  suite_add_tcase (s, tc_chain_content);

  tcase_add_test (tc_chain, test_openjpeg_encode_simple);
  tcase_add_test (tc_chain, test_openjpeg_simple);
  tcase_add_test (tc_chain, test_openjpeg_yuv_format_validate_caps);

  tcase_set_timeout (tc_chain, 5 * 60);

  tcase_add_test (tc_chain_content, test_openjpeg_encode_decode_formats);
  tcase_set_timeout (tc_chain_content, 5 * 60);

  return s;
}

GST_CHECK_MAIN (openjpeg);
