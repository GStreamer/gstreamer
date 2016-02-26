/* GStreamer unit test for MPEG-DASH
 *
 * Copyright (c) <2015> YouView TV Ltd
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
#include "adaptive_demux_common.h"

#define DEMUX_ELEMENT_NAME "dashdemux"

#define COPY_OUTPUT_TEST_DATA(outputTestData,testData) do { \
    guint otdPos, otdLen = sizeof((outputTestData)) / sizeof((outputTestData)[0]); \
    for(otdPos=0; otdPos<otdLen; ++otdPos){ \
  (testData)->output_streams = g_list_append ((testData)->output_streams, &(outputTestData)[otdPos]); \
    } \
  } while(0)

typedef struct _GstDashDemuxTestInputData
{
  const gchar *uri;
  const guint8 *payload;
  guint64 size;
} GstDashDemuxTestInputData;

static gboolean
gst_dashdemux_http_src_start (GstTestHTTPSrc * src,
    const gchar * uri, GstTestHTTPSrcInput * input_data, gpointer user_data)
{
  const GstDashDemuxTestInputData *input =
      (const GstDashDemuxTestInputData *) user_data;
  for (guint i = 0; input[i].uri; ++i) {
    if (strcmp (input[i].uri, uri) == 0) {
      input_data->context = (gpointer) & input[i];
      input_data->size = input[i].size;
      if (input[i].size == 0)
        input_data->size = strlen ((gchar *) input[i].payload);
      return TRUE;
    }
  }
  return FALSE;
}

static GstFlowReturn
gst_dashdemux_http_src_create (GstTestHTTPSrc * src,
    guint64 offset,
    guint length, GstBuffer ** retbuf, gpointer context, gpointer user_data)
{
  /*  const GstDashDemuxTestInputData *input =
     (const GstDashDemuxTestInputData *) user_data; */
  const GstDashDemuxTestInputData *input =
      (const GstDashDemuxTestInputData *) context;
  GstBuffer *buf;

  buf = gst_buffer_new_allocate (NULL, length, NULL);
  fail_if (buf == NULL, "Not enough memory to allocate buffer");

  if (input->payload) {
    gst_buffer_fill (buf, 0, input->payload + offset, length);
  } else {
    GstMapInfo info;
    guint pattern;

    pattern = offset - offset % sizeof (pattern);

    gst_buffer_map (buf, &info, GST_MAP_WRITE);
    for (guint64 i = 0; i < length; ++i) {
      gchar pattern_byte_to_write = (offset + i) % sizeof (pattern);
      if (pattern_byte_to_write == 0) {
        pattern = offset + i;
      }
      info.data[i] = (pattern >> (pattern_byte_to_write * 8)) & 0xFF;
    }
    gst_buffer_unmap (buf, &info);
  }
  *retbuf = buf;
  return GST_FLOW_OK;
}

/******************** Test specific code starts here **************************/

/*
 * Test an mpd with an audio and a video stream
 *
 */
GST_START_TEST (simpleTest)
{
  const gchar *mpd =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
      "<MPD xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\""
      "     xmlns=\"urn:mpeg:DASH:schema:MPD:2011\""
      "     xsi:schemaLocation=\"urn:mpeg:DASH:schema:MPD:2011 DASH-MPD.xsd\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-on-demand:2011\""
      "     type=\"static\""
      "     minBufferTime=\"PT1.500S\""
      "     mediaPresentationDuration=\"PT135.743S\">"
      "  <Period>"
      "    <AdaptationSet mimeType=\"audio/webm\""
      "                   subsegmentAlignment=\"true\">"
      "      <Representation id=\"171\""
      "                      codecs=\"vorbis\""
      "                      audioSamplingRate=\"44100\""
      "                      startWithSAP=\"1\""
      "                      bandwidth=\"129553\">"
      "        <AudioChannelConfiguration"
      "           schemeIdUri=\"urn:mpeg:dash:23003:3:audio_channel_configuration:2011\""
      "           value=\"2\" />"
      "        <BaseURL>audio.webm</BaseURL>"
      "        <SegmentBase indexRange=\"4452-4686\""
      "                     indexRangeExact=\"true\">"
      "          <Initialization range=\"0-4451\" />"
      "        </SegmentBase>"
      "      </Representation>"
      "    </AdaptationSet>"
      "    <AdaptationSet mimeType=\"video/webm\""
      "                   subsegmentAlignment=\"true\">"
      "      <Representation id=\"242\""
      "                      codecs=\"vp9\""
      "                      width=\"426\""
      "                      height=\"240\""
      "                      startWithSAP=\"1\""
      "                      bandwidth=\"490208\">"
      "        <BaseURL>video.webm</BaseURL>"
      "        <SegmentBase indexRange=\"234-682\""
      "                     indexRangeExact=\"true\">"
      "          <Initialization range=\"0-233\" />"
      "        </SegmentBase>"
      "      </Representation></AdaptationSet></Period></MPD>";
  GstDashDemuxTestInputData inputTestData[] = {
    {"http://unit.test/test.mpd", (guint8 *) mpd, 0},
    {"http://unit.test/audio.webm", NULL, 5000},
    {"http://unit.test/video.webm", NULL, 9000},
    {NULL, NULL, 0},
  };
  GstTestHTTPSrcCallbacks http_src_callbacks = { 0 };
  GstAdaptiveDemuxTestExpectedOutput outputTestData[] = {
    {"audio_00", 5000, NULL},
    {"video_00", 9000, NULL}
  };
  GstAdaptiveDemuxTestCallbacks test_callbacks = { 0 };
  GstAdaptiveDemuxTestCase *testData;

  testData = gst_adaptive_demux_test_case_new ();
  http_src_callbacks.src_start = gst_dashdemux_http_src_start;
  http_src_callbacks.src_create = gst_dashdemux_http_src_create;
  gst_test_http_src_install_callbacks (&http_src_callbacks, inputTestData);

  COPY_OUTPUT_TEST_DATA (outputTestData, testData);
  test_callbacks.appsink_received_data =
      gst_adaptive_demux_test_check_received_data;
  test_callbacks.appsink_eos =
      gst_adaptive_demux_test_check_size_of_received_data;

  gst_adaptive_demux_test_run (DEMUX_ELEMENT_NAME, "http://unit.test/test.mpd",
      &test_callbacks, testData);
  g_object_unref (testData);
}

GST_END_TEST;

/*
 * Test an mpd with 2 periods
 *
 */
GST_START_TEST (testTwoPeriods)
{
  const gchar *mpd =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
      "<MPD xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\""
      "     xmlns=\"urn:mpeg:DASH:schema:MPD:2011\""
      "     xsi:schemaLocation=\"urn:mpeg:DASH:schema:MPD:2011 DASH-MPD.xsd\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-on-demand:2011\""
      "     type=\"static\""
      "     minBufferTime=\"PT1.500S\""
      "     mediaPresentationDuration=\"PT300S\">"
      "  <Period id=\"Period0\" duration=\"PT0.1S\">"
      "    <AdaptationSet mimeType=\"audio/webm\""
      "                   subsegmentAlignment=\"true\">"
      "      <Representation id=\"171\""
      "                      codecs=\"vorbis\""
      "                      audioSamplingRate=\"44100\""
      "                      startWithSAP=\"1\""
      "                      bandwidth=\"129553\">"
      "        <AudioChannelConfiguration"
      "           schemeIdUri=\"urn:mpeg:dash:23003:3:audio_channel_configuration:2011\""
      "           value=\"2\" />"
      "        <BaseURL>audio1.webm</BaseURL>"
      "        <SegmentBase indexRange=\"4452-4686\""
      "                     indexRangeExact=\"true\">"
      "          <Initialization range=\"0-4451\" />"
      "        </SegmentBase>"
      "      </Representation>"
      "    </AdaptationSet>"
      "    <AdaptationSet mimeType=\"video/webm\""
      "                   subsegmentAlignment=\"true\">"
      "      <Representation id=\"242\""
      "                      codecs=\"vp9\""
      "                      width=\"426\""
      "                      height=\"240\""
      "                      startWithSAP=\"1\""
      "                      bandwidth=\"490208\">"
      "        <BaseURL>video1.webm</BaseURL>"
      "        <SegmentBase indexRange=\"234-682\""
      "                     indexRangeExact=\"true\">"
      "          <Initialization range=\"0-233\" />"
      "        </SegmentBase>"
      "      </Representation>"
      "    </AdaptationSet>"
      "  </Period>"
      "  <Period>"
      "    <AdaptationSet mimeType=\"audio/webm\""
      "                   subsegmentAlignment=\"true\">"
      "      <Representation id=\"171\""
      "                      codecs=\"vorbis\""
      "                      audioSamplingRate=\"44100\""
      "                      startWithSAP=\"1\""
      "                      bandwidth=\"129553\">"
      "        <AudioChannelConfiguration"
      "           schemeIdUri=\"urn:mpeg:dash:23003:3:audio_channel_configuration:2011\""
      "           value=\"2\" />"
      "        <BaseURL>audio2.webm</BaseURL>"
      "        <SegmentBase indexRange=\"4452-4686\""
      "                     indexRangeExact=\"true\">"
      "          <Initialization range=\"0-4451\" />"
      "        </SegmentBase>"
      "      </Representation>"
      "    </AdaptationSet>"
      "    <AdaptationSet mimeType=\"video/webm\""
      "                   subsegmentAlignment=\"true\">"
      "      <Representation id=\"242\""
      "                      codecs=\"vp9\""
      "                      width=\"426\""
      "                      height=\"240\""
      "                      startWithSAP=\"1\""
      "                      bandwidth=\"490208\">"
      "        <BaseURL>video2.webm</BaseURL>"
      "        <SegmentBase indexRange=\"234-682\""
      "                     indexRangeExact=\"true\">"
      "          <Initialization range=\"0-233\" />"
      "        </SegmentBase>"
      "      </Representation></AdaptationSet></Period></MPD>";

  GstDashDemuxTestInputData inputTestData[] = {
    {"http://unit.test/test.mpd", (guint8 *) mpd, 0},
    {"http://unit.test/audio1.webm", NULL, 5001},
    {"http://unit.test/video1.webm", NULL, 9001},
    {"http://unit.test/audio2.webm", NULL, 5002},
    {"http://unit.test/video2.webm", NULL, 9002},
    {NULL, NULL, 0},
  };
  GstTestHTTPSrcCallbacks http_src_callbacks = { 0 };
  GstAdaptiveDemuxTestExpectedOutput outputTestData[] = {
    {"audio_00", 5001, NULL},
    {"video_00", 9001, NULL},
    {"audio_01", 5002, NULL},
    {"video_01", 9002, NULL},
  };
  GstAdaptiveDemuxTestCallbacks test_callbacks = { 0 };
  GstAdaptiveDemuxTestCase *testData;

  testData = gst_adaptive_demux_test_case_new ();
  http_src_callbacks.src_start = gst_dashdemux_http_src_start;
  http_src_callbacks.src_create = gst_dashdemux_http_src_create;
  COPY_OUTPUT_TEST_DATA (outputTestData, testData);
  test_callbacks.appsink_received_data =
      gst_adaptive_demux_test_check_received_data;
  test_callbacks.appsink_eos =
      gst_adaptive_demux_test_check_size_of_received_data;

  gst_test_http_src_install_callbacks (&http_src_callbacks, inputTestData);
  gst_adaptive_demux_test_run (DEMUX_ELEMENT_NAME,
      "http://unit.test/test.mpd", &test_callbacks, testData);
  gst_object_unref (testData);
}

GST_END_TEST;

/* test setting a property on an object */
#define test_int_prop(object, name, value) \
do \
{ \
  int val = value; \
  int val_after; \
  g_object_set (object, name, val, NULL); \
  g_object_get (object, name, &val_after, NULL); \
  fail_unless (val_after == val, "property check failed for %s: set to %d, but got %d", \
      name, val, val_after); \
} while (0)

#define test_float_prop(object, name, value) \
do \
{ \
  float val = value; \
  float val_after; \
  g_object_set (object, name, val, NULL); \
  g_object_get (object, name, &val_after, NULL); \
  fail_unless (val_after == val, "property check failed for %s: set to %f, but got %f", \
      name, val, val_after); \
} while (0)

/* test setting an invalid value for a property on an object.
 * Expect an assert and the property to remain unchanged
 */
#define test_invalid_int_prop(object, name, value) \
do \
{ \
  int val_before; \
  int val_after; \
  int val = value; \
  g_object_get (object, name, &val_before, NULL); \
  ASSERT_WARNING (g_object_set (object, name, val, NULL)); \
  g_object_get (object, name, &val_after, NULL); \
  fail_unless (val_after == val_before, "property check failed for %s: before %d, after %d", \
      name, val_before, val_after); \
} while (0)

#define test_invalid_float_prop(object, name, value) \
do \
{ \
  float val_before; \
  float val_after; \
  float val = value; \
  g_object_get (object, name, &val_before, NULL); \
  ASSERT_WARNING (g_object_set (object, name, val, NULL)); \
  g_object_get (object, name, &val_after, NULL); \
  fail_unless (val_after == val_before, "property check failed for %s: before %f, after %f", \
      name, val_before, val_after); \
} while (0)

static void
setAndTestDashParams (GstAdaptiveDemuxTestEngine * engine, gpointer user_data)
{
  /*  GstAdaptiveDemuxTestCase * testData = (GstAdaptiveDemuxTestCase*)user_data; */
  GObject *dashdemux = G_OBJECT (engine->demux);

  test_int_prop (dashdemux, "connection-speed", 1000);
  test_invalid_int_prop (dashdemux, "connection-speed", 4294967 + 1);

  test_float_prop (dashdemux, "bitrate-limit", 1);
  test_invalid_float_prop (dashdemux, "bitrate-limit", 2.1);

  test_int_prop (dashdemux, "max-buffering-time", 15);
  test_invalid_int_prop (dashdemux, "max-buffering-time", 1);

  test_float_prop (dashdemux, "bandwidth-usage", 0.5);
  test_invalid_float_prop (dashdemux, "bandwidth-usage", 2);

  test_int_prop (dashdemux, "max-bitrate", 1000);
  test_invalid_int_prop (dashdemux, "max-bitrate", 10);
}

/*
 * Test setting parameters
 *
 */
GST_START_TEST (testParameters)
{
  const gchar *mpd =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
      "<MPD xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\""
      "     xmlns=\"urn:mpeg:DASH:schema:MPD:2011\""
      "     xsi:schemaLocation=\"urn:mpeg:DASH:schema:MPD:2011 DASH-MPD.xsd\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-on-demand:2011\""
      "     type=\"static\""
      "     minBufferTime=\"PT1.500S\""
      "     mediaPresentationDuration=\"PT135.743S\">"
      "  <Period>"
      "    <AdaptationSet mimeType=\"audio/webm\""
      "                   subsegmentAlignment=\"true\">"
      "      <Representation id=\"171\""
      "                      codecs=\"vorbis\""
      "                      audioSamplingRate=\"44100\""
      "                      startWithSAP=\"1\""
      "                      bandwidth=\"129553\">"
      "        <AudioChannelConfiguration"
      "           schemeIdUri=\"urn:mpeg:dash:23003:3:audio_channel_configuration:2011\""
      "           value=\"2\" />"
      "        <BaseURL>audio.webm</BaseURL>"
      "        <SegmentBase indexRange=\"4452-4686\""
      "                     indexRangeExact=\"true\">"
      "          <Initialization range=\"0-4451\" />"
      "        </SegmentBase>"
      "      </Representation></AdaptationSet></Period></MPD>";

  GstDashDemuxTestInputData inputTestData[] = {
    {"http://unit.test/test.mpd", (guint8 *) mpd, 0},
    {"http://unit.test/audio.webm", NULL, 5000},
    {NULL, NULL, 0},
  };
  GstTestHTTPSrcCallbacks http_src_callbacks = { 0 };
  GstAdaptiveDemuxTestExpectedOutput outputTestData[] = {
    {"audio_00", 5000, NULL},
  };
  GstAdaptiveDemuxTestCallbacks test_callbacks = { 0 };
  GstAdaptiveDemuxTestCase *testData;

  testData = gst_adaptive_demux_test_case_new ();
  http_src_callbacks.src_start = gst_dashdemux_http_src_start;
  http_src_callbacks.src_create = gst_dashdemux_http_src_create;
  COPY_OUTPUT_TEST_DATA (outputTestData, testData);
  test_callbacks.pre_test = setAndTestDashParams;
  test_callbacks.appsink_received_data =
      gst_adaptive_demux_test_check_received_data;
  test_callbacks.appsink_eos =
      gst_adaptive_demux_test_check_size_of_received_data;

  gst_test_http_src_install_callbacks (&http_src_callbacks, inputTestData);
  gst_adaptive_demux_test_run (DEMUX_ELEMENT_NAME, "http://unit.test/test.mpd",
      &test_callbacks, testData);
  gst_object_unref (testData);
}

GST_END_TEST;

/*
 * Test seeking
 *
 */
GST_START_TEST (testSeek)
{
  const gchar *mpd =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
      "<MPD xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\""
      "     xmlns=\"urn:mpeg:DASH:schema:MPD:2011\""
      "     xsi:schemaLocation=\"urn:mpeg:DASH:schema:MPD:2011 DASH-MPD.xsd\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-on-demand:2011\""
      "     type=\"static\""
      "     minBufferTime=\"PT1.500S\""
      "     mediaPresentationDuration=\"PT135.743S\">"
      "  <Period>"
      "    <AdaptationSet mimeType=\"audio/webm\""
      "                   subsegmentAlignment=\"true\">"
      "      <Representation id=\"171\""
      "                      codecs=\"vorbis\""
      "                      audioSamplingRate=\"44100\""
      "                      startWithSAP=\"1\""
      "                      bandwidth=\"129553\">"
      "        <AudioChannelConfiguration"
      "           schemeIdUri=\"urn:mpeg:dash:23003:3:audio_channel_configuration:2011\""
      "           value=\"2\" />"
      "        <BaseURL>audio.webm</BaseURL>"
      "        <SegmentBase indexRange=\"4452-4686\""
      "                     indexRangeExact=\"true\">"
      "          <Initialization range=\"0-4451\" />"
      "        </SegmentBase>"
      "      </Representation></AdaptationSet></Period></MPD>";
  GstDashDemuxTestInputData inputTestData[] = {
    {"http://unit.test/test.mpd", (guint8 *) mpd, 0},
    {"http://unit.test/audio.webm", NULL, 10000},
    {NULL, NULL, 0},
  };
  GstTestHTTPSrcCallbacks http_src_callbacks = { 0 };
  GstAdaptiveDemuxTestExpectedOutput outputTestData[] = {
    {"audio_00", 10000, NULL},
  };
  GstAdaptiveDemuxTestCase *testData;

  testData = gst_adaptive_demux_test_case_new ();

  http_src_callbacks.src_start = gst_dashdemux_http_src_start;
  http_src_callbacks.src_create = gst_dashdemux_http_src_create;
  COPY_OUTPUT_TEST_DATA (outputTestData, testData);

  /* media segment starts at 4687
   * Issue a seek request after media segment has started to be downloaded
   * on the first pad listed in GstAdaptiveDemuxTestOutputStreamData and the
   * first chunk of at least one byte has already arrived in AppSink
   */
  testData->threshold_for_seek = 4687 + 1;

  /* seek to 5ms.
   * Because there is only one fragment, we expect the whole file to be
   * downloaded again
   */
  testData->seek_event =
      gst_event_new_seek (1.0, GST_FORMAT_TIME,
      GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT, GST_SEEK_TYPE_SET,
      5 * GST_MSECOND, GST_SEEK_TYPE_NONE, 0);

  gst_test_http_src_install_callbacks (&http_src_callbacks, inputTestData);
  gst_adaptive_demux_test_seek (DEMUX_ELEMENT_NAME,
      "http://unit.test/test.mpd", testData);
  gst_object_unref (testData);
}

GST_END_TEST;


#define SEGMENT_SIZE 10000
static void
run_seek_position_test (gdouble rate, GstSeekType start_type,
    guint64 seek_start, GstSeekType stop_type, guint64 seek_stop,
    GstSeekFlags flags, guint64 segment_start, guint64 segment_stop,
    gint segments, gint seek_threshold_bytes)
{
  const gchar *mpd =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
      "<MPD xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\""
      "     xmlns=\"urn:mpeg:DASH:schema:MPD:2011\""
      "     xsi:schemaLocation=\"urn:mpeg:DASH:schema:MPD:2011 DASH-MPD.xsd\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-on-demand:2011\""
      "     type=\"static\""
      "     minBufferTime=\"PT1.500S\""
      "     mediaPresentationDuration=\"PT135.743S\">"
      "  <Period>"
      "    <AdaptationSet "
      "        mimeType=\"audio/mp4\" minBandwidth=\"128000\" "
      "        maxBandwidth=\"128000\" segmentAlignment=\"true\">"
      "      <SegmentTemplate timescale=\"48000\" "
      "          initialization=\"init-$RepresentationID$.mp4\" "
      "          media=\"$RepresentationID$-$Number$.mp4\" "
      "          startNumber=\"1\">"
      "        <SegmentTimeline>"
      "          <S t=\"0\" d=\"48000\" /> "
      "          <S d=\"48000\" /> "
      "          <S d=\"48000\" /> "
      "          <S d=\"48000\" /> "
      "        </SegmentTimeline>"
      "      </SegmentTemplate>"
      "      <Representation id=\"audio\" bandwidth=\"128000\" "
      "          codecs=\"mp4a.40.2\" audioSamplingRate=\"48000\"> "
      "        <AudioChannelConfiguration "
      "            schemeIdUri=\"urn:mpeg:dash:23003:3:audio_channel_configuration:2011\""
      "            value=\"2\"> "
      "        </AudioChannelConfiguration> "
      "    </Representation></AdaptationSet></Period></MPD>";
  GstDashDemuxTestInputData inputTestData[] = {
    {"http://unit.test/test.mpd", (guint8 *) mpd, 0},
    {"http://unit.test/init-audio.mp4", NULL, 10000},
    {"http://unit.test/audio-1.mp4", NULL, 10000},
    {"http://unit.test/audio-2.mp4", NULL, 10000},
    {"http://unit.test/audio-3.mp4", NULL, 10000},
    {"http://unit.test/audio-4.mp4", NULL, 10000},
    {NULL, NULL, 0},
  };
  GstTestHTTPSrcCallbacks http_src_callbacks = { 0 };
  GstAdaptiveDemuxTestExpectedOutput outputTestData[] = {
    /* 1 from the init segment */
    {"audio_00", (segments ? 1 + segments : 0) * 10000, NULL},
  };
  GstAdaptiveDemuxTestCase *testData;

  testData = gst_adaptive_demux_test_case_new ();

  http_src_callbacks.src_start = gst_dashdemux_http_src_start;
  http_src_callbacks.src_create = gst_dashdemux_http_src_create;
  COPY_OUTPUT_TEST_DATA (outputTestData, testData);

  /* media segment starts at 4687
   * Issue a seek request after media segment has started to be downloaded
   * on the first pad listed in GstAdaptiveDemuxTestOutputStreamData and the
   * first chunk of at least one byte has already arrived in AppSink
   */
  if (seek_threshold_bytes)
    testData->threshold_for_seek = seek_threshold_bytes;
  else
    testData->threshold_for_seek = 4687 + 1;

  /* FIXME hack to avoid having a 0 seqnum */
  gst_util_seqnum_next ();

  /* seek to 5ms.
   * Because there is only one fragment, we expect the whole file to be
   * downloaded again
   */
  testData->seek_event =
      gst_event_new_seek (rate, GST_FORMAT_TIME, flags, start_type,
      seek_start, stop_type, seek_stop);

  gst_test_http_src_install_callbacks (&http_src_callbacks, inputTestData);
  gst_adaptive_demux_test_seek (DEMUX_ELEMENT_NAME,
      "http://unit.test/test.mpd", testData);
  gst_object_unref (testData);
}

GST_START_TEST (testSeekKeyUnitPosition)
{
  /* Seek to 1.5s with key unit, it should go back to 1.0s. 3 segments will be
   * pushed */
  run_seek_position_test (1.0, GST_SEEK_TYPE_SET, 1500 * GST_MSECOND,
      GST_SEEK_TYPE_NONE, 0, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT,
      1000 * GST_MSECOND, -1, 3, 0);
}

GST_END_TEST;


GST_START_TEST (testSeekUpdateStopPosition)
{
  run_seek_position_test (1.0, GST_SEEK_TYPE_NONE, 1500 * GST_MSECOND,
      GST_SEEK_TYPE_SET, 3000 * GST_MSECOND, 0, 0, 3000 * GST_MSECOND, 3, 0);
}

GST_END_TEST;

GST_START_TEST (testSeekPosition)
{
  /* Seek to 1.5s without key unit, it should keep the 1.5s, but still push
   * from the 1st segment, so 3 segments will be
   * pushed */
  run_seek_position_test (1.0, GST_SEEK_TYPE_SET, 1500 * GST_MSECOND,
      GST_SEEK_TYPE_NONE, 0, GST_SEEK_FLAG_FLUSH, 1500 * GST_MSECOND, -1, 3, 0);
}

GST_END_TEST;

GST_START_TEST (testSeekSnapBeforePosition)
{
  /* Seek to 1.5s, snap before, it go to 1s */
  run_seek_position_test (1.0, GST_SEEK_TYPE_SET, 1500 * GST_MSECOND,
      GST_SEEK_TYPE_NONE, 0, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_SNAP_BEFORE,
      1000 * GST_MSECOND, -1, 3, 0);
}

GST_END_TEST;


GST_START_TEST (testSeekSnapAfterPosition)
{
  /* Seek to 1.5s with snap after, it should move to 2s */
  run_seek_position_test (1.0, GST_SEEK_TYPE_SET, 1500 * GST_MSECOND,
      GST_SEEK_TYPE_NONE, 0, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_SNAP_AFTER,
      2000 * GST_MSECOND, -1, 2, 0);
}

GST_END_TEST;


GST_START_TEST (testSeekSnapBeforeSamePosition)
{
  /* Snap seek without position */
  run_seek_position_test (1.0, GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE,
      GST_SEEK_TYPE_NONE, 0, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_SNAP_BEFORE,
      2 * GST_MSECOND, -1, 2, SEGMENT_SIZE * 3 + 1);
}

GST_END_TEST;


GST_START_TEST (testSeekSnapAfterSamePosition)
{
  /* Snap seek without position */
  run_seek_position_test (1.0, GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE,
      GST_SEEK_TYPE_NONE, 0, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_SNAP_AFTER,
      3 * GST_MSECOND, -1, 1, SEGMENT_SIZE * 3 + 1);
}

GST_END_TEST;



GST_START_TEST (testReverseSeekSnapBeforePosition)
{
  run_seek_position_test (-1.0, GST_SEEK_TYPE_SET, 1000 * GST_MSECOND,
      GST_SEEK_TYPE_SET, 2500 * GST_MSECOND,
      GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_SNAP_BEFORE, 1000 * GST_MSECOND,
      3000 * GST_MSECOND, 2, 0);
}

GST_END_TEST;


GST_START_TEST (testReverseSeekSnapAfterPosition)
{
  run_seek_position_test (-1.0, GST_SEEK_TYPE_SET, 1000 * GST_MSECOND,
      GST_SEEK_TYPE_SET, 2500 * GST_MSECOND,
      GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_SNAP_AFTER, 1000 * GST_MSECOND,
      2000 * GST_MSECOND, 1, 0);
}

GST_END_TEST;

static void
testDownloadErrorMessageCallback (GstAdaptiveDemuxTestEngine * engine,
    GstMessage * msg, gpointer user_data)
{
  GError *err = NULL;
  gchar *dbg_info = NULL;

  fail_unless (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR);
  gst_message_parse_error (msg, &err, &dbg_info);
  GST_DEBUG ("Error from element %s : %s\n",
      GST_OBJECT_NAME (msg->src), err->message);
  fail_unless_equals_string (GST_OBJECT_NAME (msg->src), DEMUX_ELEMENT_NAME);
  /*GST_DEBUG ("dbg_info=%s\n", dbg_info); */
  g_error_free (err);
  g_free (dbg_info);
  g_main_loop_quit (engine->loop);
}

/*
 * Test error case of failing to download a segment
 */
GST_START_TEST (testDownloadError)
{
  const gchar *mpd =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
      "<MPD xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\""
      "     xmlns=\"urn:mpeg:DASH:schema:MPD:2011\""
      "     xsi:schemaLocation=\"urn:mpeg:DASH:schema:MPD:2011 DASH-MPD.xsd\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-on-demand:2011\""
      "     type=\"static\""
      "     minBufferTime=\"PT1.500S\""
      "     mediaPresentationDuration=\"PT0.5S\">"
      "  <Period>"
      "    <AdaptationSet mimeType=\"audio/webm\""
      "                   subsegmentAlignment=\"true\">"
      "      <Representation id=\"171\""
      "                      codecs=\"vorbis\""
      "                      audioSamplingRate=\"44100\""
      "                      startWithSAP=\"1\""
      "                      bandwidth=\"129553\">"
      "        <AudioChannelConfiguration"
      "           schemeIdUri=\"urn:mpeg:dash:23003:3:audio_channel_configuration:2011\""
      "           value=\"2\" />"
      "        <BaseURL>audio_file_not_available.webm</BaseURL>"
      "        <SegmentBase indexRange=\"4452-4686\""
      "                     indexRangeExact=\"true\">"
      "          <Initialization range=\"0-4451\" />"
      "        </SegmentBase>"
      "      </Representation></AdaptationSet></Period></MPD>";

  GstDashDemuxTestInputData inputTestData[] = {
    {"http://unit.test/test.mpd", (guint8 *) mpd, 0},
    {NULL, NULL, 0},
  };
  GstTestHTTPSrcCallbacks http_src_callbacks = { 0 };
  GstAdaptiveDemuxTestExpectedOutput outputTestData[] = {
    {"audio_00", 0, NULL},
  };
  GstAdaptiveDemuxTestCallbacks test_callbacks = { 0 };
  GstAdaptiveDemuxTestCase *testData;

  testData = gst_adaptive_demux_test_case_new ();
  http_src_callbacks.src_start = gst_dashdemux_http_src_start;
  http_src_callbacks.src_create = gst_dashdemux_http_src_create;
  COPY_OUTPUT_TEST_DATA (outputTestData, testData);
  test_callbacks.appsink_received_data =
      gst_adaptive_demux_test_check_received_data;
  test_callbacks.bus_error_message = testDownloadErrorMessageCallback;
  test_callbacks.appsink_eos =
      gst_adaptive_demux_test_check_size_of_received_data;

  gst_test_http_src_install_callbacks (&http_src_callbacks, inputTestData);
  gst_adaptive_demux_test_run (DEMUX_ELEMENT_NAME, "http://unit.test/test.mpd",
      &test_callbacks, testData);
  gst_object_unref (testData);
}

GST_END_TEST;

/* generate queries to adaptive demux */
static gboolean
testQueryCheckDataReceived (GstAdaptiveDemuxTestEngine * engine,
    GstAdaptiveDemuxTestOutputStream * stream,
    GstBuffer * buffer, gpointer user_data)
{
  GList *pads;
  GstPad *pad;
  GstQuery *query;
  gboolean ret;
  gint64 duration;
  gboolean seekable;
  gint64 segment_start;
  gint64 segment_end;
  gchar *uri;
  gchar *redirect_uri;
  gboolean redirect_permanent;

  pads = GST_ELEMENT_PADS (stream->appsink);

  /* AppSink should have only 1 pad */
  fail_unless (pads != NULL);
  fail_unless (g_list_length (pads) == 1);
  pad = GST_PAD (pads->data);

  query = gst_query_new_duration (GST_FORMAT_TIME);
  ret = gst_pad_peer_query (pad, query);
  fail_unless (ret == TRUE);
  gst_query_parse_duration (query, NULL, &duration);
  fail_unless (duration == 135743 * GST_MSECOND);
  gst_query_unref (query);

  query = gst_query_new_seeking (GST_FORMAT_TIME);
  ret = gst_pad_peer_query (pad, query);
  fail_unless (ret == TRUE);
  gst_query_parse_seeking (query, NULL, &seekable, &segment_start,
      &segment_end);
  fail_unless (seekable == TRUE);
  fail_unless (segment_start == 0);
  fail_unless (segment_end == duration);
  gst_query_unref (query);

  query = gst_query_new_uri ();
  ret = gst_pad_peer_query (pad, query);
  fail_unless (ret == TRUE);
  gst_query_parse_uri (query, &uri);
  gst_query_parse_uri_redirection (query, &redirect_uri);
  gst_query_parse_uri_redirection_permanent (query, &redirect_permanent);
  fail_unless (strcmp (uri, "http://unit.test/test.mpd") == 0);
  /* adaptive demux does not reply with redirect information */
  fail_unless (redirect_uri == NULL);
  fail_unless (redirect_permanent == FALSE);
  g_free (uri);
  g_free (redirect_uri);
  gst_query_unref (query);

  return gst_adaptive_demux_test_check_received_data (engine,
      stream, buffer, user_data);
}

/*
 * Test queries
 *
 */
GST_START_TEST (testQuery)
{
  const gchar *mpd =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
      "<MPD xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\""
      "     xmlns=\"urn:mpeg:DASH:schema:MPD:2011\""
      "     xsi:schemaLocation=\"urn:mpeg:DASH:schema:MPD:2011 DASH-MPD.xsd\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-on-demand:2011\""
      "     type=\"static\""
      "     minBufferTime=\"PT1.500S\""
      "     mediaPresentationDuration=\"PT135.743S\">"
      "  <Period>"
      "    <AdaptationSet mimeType=\"audio/webm\""
      "                   subsegmentAlignment=\"true\">"
      "      <Representation id=\"171\""
      "                      codecs=\"vorbis\""
      "                      audioSamplingRate=\"44100\""
      "                      startWithSAP=\"1\""
      "                      bandwidth=\"129553\">"
      "        <AudioChannelConfiguration"
      "           schemeIdUri=\"urn:mpeg:dash:23003:3:audio_channel_configuration:2011\""
      "           value=\"2\" />"
      "        <BaseURL>audio.webm</BaseURL>"
      "        <SegmentBase indexRange=\"4452-4686\""
      "                     indexRangeExact=\"true\">"
      "          <Initialization range=\"0-4451\" />"
      "        </SegmentBase>"
      "      </Representation></AdaptationSet></Period></MPD>";
  GstDashDemuxTestInputData inputTestData[] = {
    {"http://unit.test/test.mpd", (guint8 *) mpd, 0},
    {"http://unit.test/audio.webm", NULL, 5000},
    {NULL, NULL, 0},
  };
  GstTestHTTPSrcCallbacks http_src_callbacks = { 0 };
  GstAdaptiveDemuxTestExpectedOutput outputTestData[] = {
    {"audio_00", 5000, NULL},
  };
  GstAdaptiveDemuxTestCallbacks test_callbacks = { 0 };
  GstAdaptiveDemuxTestCase *testData;

  testData = gst_adaptive_demux_test_case_new ();
  http_src_callbacks.src_start = gst_dashdemux_http_src_start;
  http_src_callbacks.src_create = gst_dashdemux_http_src_create;
  COPY_OUTPUT_TEST_DATA (outputTestData, testData);
  test_callbacks.appsink_received_data = testQueryCheckDataReceived;
  test_callbacks.appsink_eos =
      gst_adaptive_demux_test_check_size_of_received_data;

  gst_test_http_src_install_callbacks (&http_src_callbacks, inputTestData);
  gst_adaptive_demux_test_run (DEMUX_ELEMENT_NAME,
      "http://unit.test/test.mpd", &test_callbacks, testData);
  gst_object_unref (testData);
}

GST_END_TEST;

static GstFlowReturn
test_fragment_download_error_src_create (GstTestHTTPSrc * src,
    guint64 offset,
    guint length, GstBuffer ** retbuf, gpointer context, gpointer user_data)
{
  const GstDashDemuxTestInputData *input =
      (const GstDashDemuxTestInputData *) context;
  fail_unless (input != NULL);
  if (!g_str_has_suffix (input->uri, ".mpd") && offset > 2000) {
    GST_DEBUG ("network_error %s %" G_GUINT64_FORMAT " @ %d",
        input->uri, offset, 2000);
    GST_ELEMENT_ERROR (src, RESOURCE, READ,
        (("A network error occurred, or the server closed the connection unexpectedly.")), ("A network error occurred, or the server closed the connection unexpectedly."));
    return GST_FLOW_ERROR;
  }
  return gst_dashdemux_http_src_create (src, offset, length, retbuf, context,
      user_data);
}

/* function to check total size of data received by AppSink
 * will be called when AppSink receives eos.
 */
static void
testFragmentDownloadErrorCheckSizeOfDataReceived (GstAdaptiveDemuxTestEngine *
    engine, GstAdaptiveDemuxTestOutputStream * stream, gpointer user_data)
{
  GstAdaptiveDemuxTestCase *testData = GST_ADAPTIVE_DEMUX_TEST_CASE (user_data);
  GstAdaptiveDemuxTestExpectedOutput *testOutputStreamData;

  testOutputStreamData =
      gst_adaptive_demux_test_find_test_data_by_stream (testData, stream, NULL);
  fail_unless (testOutputStreamData != NULL);

  /* expect to receive more than 0 */
  fail_unless (stream->total_received_size > 0,
      "size validation failed for %s, expected > 0, received %d",
      testOutputStreamData->name, stream->total_received_size);

  /* expect to receive less than file size */
  fail_unless (stream->total_received_size <
      testOutputStreamData->expected_size,
      "size validation failed for %s, expected < %d received %d",
      testOutputStreamData->name, testOutputStreamData->expected_size,
      stream->total_received_size);
}

/*
 * Test fragment download error
 * Let the adaptive demux download a few bytes, then instruct the
 * GstTestHTTPSrc element to generate an error.
 */
GST_START_TEST (testFragmentDownloadError)
{
  const gchar *mpd =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
      "<MPD xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\""
      "     xmlns=\"urn:mpeg:DASH:schema:MPD:2011\""
      "     xsi:schemaLocation=\"urn:mpeg:DASH:schema:MPD:2011 DASH-MPD.xsd\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-on-demand:2011\""
      "     type=\"static\""
      "     minBufferTime=\"PT1.500S\""
      "     mediaPresentationDuration=\"PT0.5S\">"
      "  <Period>"
      "    <AdaptationSet mimeType=\"audio/webm\""
      "                   subsegmentAlignment=\"true\">"
      "      <Representation id=\"171\""
      "                      codecs=\"vorbis\""
      "                      audioSamplingRate=\"44100\""
      "                      startWithSAP=\"1\""
      "                      bandwidth=\"129553\">"
      "        <AudioChannelConfiguration"
      "           schemeIdUri=\"urn:mpeg:dash:23003:3:audio_channel_configuration:2011\""
      "           value=\"2\" />"
      "        <BaseURL>audio.webm</BaseURL>"
      "        <SegmentBase indexRange=\"4452-4686\""
      "                     indexRangeExact=\"true\">"
      "          <Initialization range=\"0-4451\" />"
      "        </SegmentBase>"
      "      </Representation></AdaptationSet></Period></MPD>";

  GstDashDemuxTestInputData inputTestData[] = {
    {"http://unit.test/test.mpd", (guint8 *) mpd, 0},
    {"http://unit.test/audio.webm", NULL, 5000},
    {NULL, NULL, 0},
  };
  GstTestHTTPSrcCallbacks http_src_callbacks = { 0 };
  GstAdaptiveDemuxTestExpectedOutput outputTestData[] = {
    {"audio_00", 5000, NULL},
  };
  GstAdaptiveDemuxTestCallbacks test_callbacks = { 0 };
  GstAdaptiveDemuxTestCase *testData;

  testData = gst_adaptive_demux_test_case_new ();
  http_src_callbacks.src_start = gst_dashdemux_http_src_start;
  http_src_callbacks.src_create = test_fragment_download_error_src_create;
  COPY_OUTPUT_TEST_DATA (outputTestData, testData);
  test_callbacks.appsink_received_data =
      gst_adaptive_demux_test_check_received_data;
  test_callbacks.appsink_eos = testFragmentDownloadErrorCheckSizeOfDataReceived;
  /*  test_callbacks.demux_sent_eos = gst_adaptive_demux_test_check_size_of_received_data; */

  test_callbacks.bus_error_message = testDownloadErrorMessageCallback;

  gst_test_http_src_install_callbacks (&http_src_callbacks, inputTestData);
  gst_adaptive_demux_test_run (DEMUX_ELEMENT_NAME,
      "http://unit.test/test.mpd", &test_callbacks, testData);
  gst_object_unref (testData);
}

GST_END_TEST;

static Suite *
dash_demux_suite (void)
{
  Suite *s = suite_create ("dash_demux");
  TCase *tc_basicTest = tcase_create ("basicTest");

  tcase_add_test (tc_basicTest, simpleTest);
  tcase_add_test (tc_basicTest, testTwoPeriods);
  tcase_add_test (tc_basicTest, testParameters);
  tcase_add_test (tc_basicTest, testSeek);
  tcase_add_test (tc_basicTest, testSeekKeyUnitPosition);
  tcase_add_test (tc_basicTest, testSeekPosition);
  tcase_add_test (tc_basicTest, testSeekUpdateStopPosition);
  tcase_add_test (tc_basicTest, testSeekSnapBeforePosition);
  tcase_add_test (tc_basicTest, testSeekSnapAfterPosition);
  tcase_add_test (tc_basicTest, testSeekSnapBeforeSamePosition);
  tcase_add_test (tc_basicTest, testSeekSnapAfterSamePosition);
  tcase_add_test (tc_basicTest, testReverseSeekSnapBeforePosition);
  tcase_add_test (tc_basicTest, testReverseSeekSnapAfterPosition);
  tcase_add_test (tc_basicTest, testDownloadError);
  tcase_add_test (tc_basicTest, testFragmentDownloadError);
  tcase_add_test (tc_basicTest, testQuery);

  tcase_add_unchecked_fixture (tc_basicTest, gst_adaptive_demux_test_setup,
      gst_adaptive_demux_test_teardown);

  suite_add_tcase (s, tc_basicTest);

  return s;
}

GST_CHECK_MAIN (dash_demux);
