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
#include <gst/app/gstappsink.h>
#include "fake_http_src.h"

#define GST_FAKE_SOUP_HTTP_SRC_NAME            "fake-soup-http-src"

/* forward declarations */
struct _GstDashDemuxTestData;

/* the seek test will use a separate task to perform the seek operation.
 * After starting the task, the caller will block until the seek task
 * flushes the AppSink and changes the GstFakeSoupHTTPSrc element state from
 * PLAYING to PAUSED.
 * When that event is detected, the caller is allowed to resume.
 * Any data that will be sent to AppSink after resume will be rejected because
 * AppSink is in flushing mode.
 */
typedef enum
{
  SEEK_TASK_STATE_NOT_STARTED,
  SEEK_TASK_STATE_WAITING_FOR_FAKESRC_STATE_CHANGE,
  SEEK_TASK_STATE_EXITING,
} SeekTaskState;

/* scratchData space used by tests to store output stream related data.
 * Structure is set to 0 when the test begins.
 * If a test does not use some of the fields, they will remain 0.
 * As new tests will want new functionality, this structure can gain new
 * members without affecting existing tests.
 */
typedef struct _GstDashDemuxTestOutputStreamScratchData
{
  /* the GstAppSink element getting the data for this stream */
  GstAppSink *appsink;
  /* the internal pad of adaptivedemux element used to send data to the GstAppSink element */
  GstPad *internalPad;
  /* current segment start offset */
  guint64 segmentStart;
  /* the size received so far on this segment */
  guint64 segmentReceivedSize;
  /* the total size received so far on this stream, excluding current segment */
  guint64 totalReceivedSize;
} GstDashDemuxTestOutputStreamScratchData;

/* structure to store output stream related data.
 * Will be used by the test during output validation.
 * The fields are set by the user in the test using static arrays.
 * The scratchData field is set to a NULL pointer and later allocated by the
 * test and initialised with 0.
 */
typedef struct _GstDashDemuxTestOutputStreamData
{
  /* the name of the dashdemux src pad generating this stream */
  const char *name;
  /* the expected size on this stream */
  guint expectedSize;
  /* stream level scratchData space to be used by test */
  GstDashDemuxTestOutputStreamScratchData *scratchData;
} GstDashDemuxTestOutputStreamData;

/* callback that will be called before starting the pipeline.
 */
typedef void (*PreTestCallback) (struct _GstDashDemuxTestData * testData);

/* callback that will be called after stopping the pipeline.
 */
typedef void (*PostTestCallback) (struct _GstDashDemuxTestData * testData);

/* callback that will be called each time AppSink received data.
 * Can be used by a test to perform additional operations (eg validate
 * output data)
 */
typedef gboolean (*AppSinkGotDataCallback) (struct _GstDashDemuxTestData *
    testData, GstDashDemuxTestOutputStreamData * testOutputStreamData,
    GstBuffer * buffer);

/* callback that will be called each time AppSink received eos.
 * Can be used by a test to perform additional operations (eg validate
 * output data)
 */
typedef gboolean (*AppSinkGotEosCallback) (struct _GstDashDemuxTestData *
    testData, GstDashDemuxTestOutputStreamData * testOutputStreamData);

/* callback that will be called each time dashdemux sends data to AppSink
 */
typedef gboolean (*DashdemuxSendsDataCallback) (struct _GstDashDemuxTestData
    * testData, GstDashDemuxTestOutputStreamData * testOutputStreamData,
    GstBuffer * buffer);

/* structure containing various callbacks that can be registered by a test
 * Not all callbacks needs to be configured by a test.
 * As new tests will want new functionality, this structure can gain new
 * members without affecting existing tests.
 */
typedef struct
{
  PreTestCallback preTestCallback;
  PostTestCallback postTestCallback;
  AppSinkGotDataCallback appSinkGotDataCallback;
  AppSinkGotEosCallback appSinkGotEosCallback;
  DashdemuxSendsDataCallback dashdemuxSendsDataCallback;
} Callbacks;

/* scratchData space used by tests to store data.
 * Structure is set to 0 when the test begins.
 * If a test does not use some of the fields, they will remain 0.
 * As new tests will want new functionality, this structure can gain new
 * members without affecting existing tests.
 */
typedef struct _GstDashDemuxTestScratchData
{
  GstElement *pipeline;
  GstElement *dashdemux;
  GstElement *mpd_source;
  GMainLoop *loop;

  /* task used by the seek test to perform the seek operation */
  GstTask *seekTask;
  GRecMutex seekTaskLock;

  /* the state of the seekTask */
  SeekTaskState seekTaskState;
  GMutex seekTaskStateLock;
  GCond seekTaskStateCond;

  /* seek test will wait for this amount of bytes to be sent by dash to AppSink
   * before triggering a seek request
   */
  guint thresholdForSeek;

} GstDashDemuxTestScratchData;

/* structure containing all data used by a test
 * Any callback defined by a test will receive this as first parameter
 */
typedef struct _GstDashDemuxTestData
{
  /* input data to configure the GstFakeSoupHTTPSrc element
   * Null terminaled array of GstFakeHttpSrcInputData, one entry per stream
   */
  const GstFakeHttpSrcInputData *inputStreamArray;

  /* output data used to validate the test
   * array of GstDashDemuxTestOutputStreamData, one entry per stream
   */
  GstDashDemuxTestOutputStreamData *outputStreamArray;
  guint outputStreamArraySize;

  /* test level scratchData data to be used by test */
  GstDashDemuxTestScratchData *scratchData;

  /* the number of streams that finished.
   * Main thread will stop the pipeline when all streams are finished
   * (countStreamFinished == outputStreamArraySize)
   */
  guint countStreamFinished;
  GMutex lockStreamFinished;

  /* callbacks to be registered by test to influence the pipeline */
  const Callbacks *callbacks;

} GstDashDemuxTestData;

/* global pointer to test data, set before each test.
 * We need it global so that gst_fake_soup_http_src element can get the data.
 * Due to this, tests cannot be run in parallel.
*/
static GstDashDemuxTestData *g_testData = NULL;

static void
test_setup (void)
{
  GstRegistry *registry;
  gboolean ret;

  registry = gst_registry_get ();
  ret =
      gst_fake_soup_http_src_register_plugin (registry,
      GST_FAKE_SOUP_HTTP_SRC_NAME);
  fail_unless (ret);
}

static void
test_teardown (void)
{
}

/* get the testOutput entry in testData corresponding to the given AppSink */
static GstDashDemuxTestOutputStreamData *
getTestOutputDataByAppsink (GstDashDemuxTestData * testData,
    GstAppSink * appsink)
{
  for (guint i = 0; i < testData->outputStreamArraySize; ++i) {
    if (testData->outputStreamArray[i].scratchData->appsink == appsink) {
      return &testData->outputStreamArray[i];
    }
  }
  ck_abort_msg ("cannot find appsink %p in the output data", appsink);
  return NULL;
}

/* get the testOutput entry in testData corresponding to the given stream name */
static GstDashDemuxTestOutputStreamData *
getTestOutputDataByName (GstDashDemuxTestData * testData, const char *name)
{
  for (guint i = 0; i < testData->outputStreamArraySize; ++i) {
    if (strcmp (testData->outputStreamArray[i].name, name) == 0) {
      return &testData->outputStreamArray[i];
    }
  }
  ck_abort_msg ("cannot find stream '%s' in the output data", name);
  return NULL;
}

/* get the testOutput entry in testData corresponding to the given internal pad */
static GstDashDemuxTestOutputStreamData *
getTestOutputDataByPad (GstDashDemuxTestData * testData, const GstPad * pad)
{
  for (guint i = 0; i < testData->outputStreamArraySize; ++i) {
    if (testData->outputStreamArray[i].scratchData->internalPad == pad) {
      return &testData->outputStreamArray[i];
    }
  }
  ck_abort_msg ("cannot find pad '%p' in the output data", pad);
  return NULL;
}

/* callback called when AppSink receives data */
static GstFlowReturn
on_appSinkNewSample (GstAppSink * appsink, gpointer user_data)
{
  GstDashDemuxTestData *testData = (GstDashDemuxTestData *) user_data;
  GstDashDemuxTestOutputStreamData *testOutputStreamData = NULL;
  GstSample *sample;
  GstBuffer *buffer;
  gboolean ret = TRUE;

  fail_unless (testData != NULL);
  testOutputStreamData = getTestOutputDataByAppsink (testData, appsink);

  sample = gst_app_sink_pull_sample (appsink);
  buffer = gst_sample_get_buffer (sample);

  /* call the test callback, if registered */
  if (testData->callbacks->appSinkGotDataCallback)
    ret = testData->callbacks->appSinkGotDataCallback (testData,
        testOutputStreamData, buffer);

  testOutputStreamData->scratchData->segmentReceivedSize +=
      gst_buffer_get_size (buffer);

  gst_sample_unref (sample);

  if (!ret)
    return GST_FLOW_EOS;

  return GST_FLOW_OK;
}

/* callback called when AppSink receives eos */
static void
on_appSinkEOS (GstAppSink * appsink, gpointer user_data)
{
  GstDashDemuxTestData *testData = (GstDashDemuxTestData *) user_data;
  GstDashDemuxTestOutputStreamData *testOutputStreamData = NULL;
  gboolean ret = TRUE;

  fail_unless (testData != NULL);
  testOutputStreamData = getTestOutputDataByAppsink (testData, appsink);

  testOutputStreamData->scratchData->totalReceivedSize +=
      testOutputStreamData->scratchData->segmentReceivedSize;
  testOutputStreamData->scratchData->segmentReceivedSize = 0;

  if (testData->callbacks->appSinkGotEosCallback)
    ret = testData->callbacks->appSinkGotEosCallback (testData,
        testOutputStreamData);

  if (ret) {
    /* signal to the application that another stream has finished */
    g_mutex_lock (&testData->lockStreamFinished);
    testData->countStreamFinished++;

    if (testData->countStreamFinished == testData->outputStreamArraySize) {
      g_main_loop_quit (testData->scratchData->loop);
    }

    g_mutex_unlock (&testData->lockStreamFinished);
  } else {
    /* ignore this eos event, the stream is not finished yet */
  }
}

/* callback called when dash sends data to AppSink */
static GstPadProbeReturn
on_dashSendsData (GstPad * pad, GstPadProbeInfo * info, gpointer data)
{
  GstDashDemuxTestData *testData = (GstDashDemuxTestData *) data;
  GstDashDemuxTestOutputStreamData *testOutputStreamData = NULL;
  GstBuffer *buffer;
  char *streamName;

  buffer = GST_PAD_PROBE_INFO_BUFFER (info);

  streamName = gst_pad_get_name (pad);
  testOutputStreamData = getTestOutputDataByName (testData, streamName);
  g_free (streamName);

  if (testData->callbacks->dashdemuxSendsDataCallback) {
    (*testData->callbacks->dashdemuxSendsDataCallback) (testData,
        testOutputStreamData, buffer);
  }

  return GST_PAD_PROBE_OK;
}

/* callback called when dash receives events from GstFakeSoupHTTPSrc */
static GstPadProbeReturn
on_dashReceivesEvent (GstPad * pad, GstPadProbeInfo * info, gpointer data)
{
  GstDashDemuxTestData *testData = (GstDashDemuxTestData *) data;
  GstDashDemuxTestOutputStreamData *testOutputStreamData = NULL;
  GstEvent *event;
  const GstSegment *segment;

  event = GST_PAD_PROBE_INFO_EVENT (info);
  GST_DEBUG ("dash received event %" GST_PTR_FORMAT " on pad %p", event, pad);

  if (GST_EVENT_TYPE (event) == GST_EVENT_SEGMENT) {
    /* a new segment will start arriving
     * update segmentStart used by pattern validation
     */
    gst_event_parse_segment (event, &segment);

    testOutputStreamData = getTestOutputDataByPad (testData, pad);

    testOutputStreamData->scratchData->totalReceivedSize +=
        testOutputStreamData->scratchData->segmentReceivedSize;
    testOutputStreamData->scratchData->segmentReceivedSize = 0;
    testOutputStreamData->scratchData->segmentStart = segment->start;
  }

  return GST_PAD_PROBE_OK;
}

/* callback called when dashdemux creates a src pad.
 * We will create an AppSink to get the data
 */
static void
on_dashNewPad (GstElement * dashdemux, GstPad * pad, gpointer data)
{
  GstElement *pipeline;
  GstElement *sink;
  gboolean ret;
  char *name;
  GstPad *internalPad;
  GstAppSinkCallbacks appSinkCallbacks;
  GstDashDemuxTestData *testData = (GstDashDemuxTestData *) data;
  GstDashDemuxTestOutputStreamData *testOutputStreamData = NULL;

  fail_unless (testData != NULL);
  name = gst_pad_get_name (pad);

  GST_DEBUG ("created pad %p", pad);

  sink = gst_element_factory_make ("appsink", name);
  fail_unless (sink != NULL);

  testOutputStreamData = getTestOutputDataByName (testData, name);
  g_free (name);

  /* register the AppSink pointer in the test output data */
  gst_object_ref (sink);
  testOutputStreamData->scratchData->appsink = GST_APP_SINK (sink);

  appSinkCallbacks.eos = on_appSinkEOS;
  appSinkCallbacks.new_preroll = NULL;
  appSinkCallbacks.new_sample = on_appSinkNewSample;

  gst_app_sink_set_callbacks (GST_APP_SINK (sink), &appSinkCallbacks, testData,
      NULL);

  gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_BUFFER,
      (GstPadProbeCallback) on_dashSendsData, testData, NULL);

  internalPad = GST_PAD_CAST (gst_proxy_pad_get_internal (GST_PROXY_PAD (pad)));

  gst_pad_add_probe (internalPad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
      (GstPadProbeCallback) on_dashReceivesEvent, testData, NULL);

  /* keep the reference to the internalPad.
   * We will need it to identify the stream in the on_dashReceivesEvent callback
   */
  testOutputStreamData->scratchData->internalPad = internalPad;

  pipeline = GST_ELEMENT (gst_element_get_parent (dashdemux));
  fail_unless (pipeline != NULL);
  ret = gst_bin_add (GST_BIN (pipeline), sink);
  fail_unless_equals_int (ret, TRUE);
  gst_object_unref (pipeline);
  ret = gst_element_link (dashdemux, sink);
  fail_unless_equals_int (ret, TRUE);
  ret = gst_element_sync_state_with_parent (sink);
  fail_unless_equals_int (ret, TRUE);

}

/*
 * Create a dashdemux element, run a test using the input data and check
 * the output data
 */
static void
runTest (const GstFakeHttpSrcInputData * inputStreamArray,
    GstDashDemuxTestOutputStreamData * outputStreamArray,
    guint outputStreamArraySize, const Callbacks * callbacks)
{
  GstElement *pipeline;
  GstElement *dashdemux;
  GstElement *mpd_source;
  gboolean ret;
  GstStateChangeReturn stateChange;
  GstDashDemuxTestData *testData;

  testData = g_slice_new0 (GstDashDemuxTestData);
  testData->inputStreamArray = inputStreamArray;
  testData->outputStreamArray = outputStreamArray;
  testData->outputStreamArraySize = outputStreamArraySize;
  g_mutex_init (&testData->lockStreamFinished);
  testData->callbacks = callbacks;

  /* allocate the scratchData space */
  testData->scratchData = g_slice_new0 (GstDashDemuxTestScratchData);
  for (guint i = 0; i < outputStreamArraySize; ++i) {
    outputStreamArray[i].scratchData =
        g_slice_new0 (GstDashDemuxTestOutputStreamScratchData);
  }

  fail_unless (g_testData == NULL);
  g_testData = testData;
  gst_fake_soup_http_src_set_input_data (inputStreamArray);

  testData->scratchData->loop = g_main_loop_new (NULL, TRUE);

  pipeline = gst_pipeline_new ("pipeline");
  fail_unless (pipeline != NULL);
  testData->scratchData->pipeline = pipeline;
  GST_DEBUG ("created pipeline %p", pipeline);

  dashdemux = gst_check_setup_element ("dashdemux");
  fail_unless (dashdemux != NULL);
  testData->scratchData->dashdemux = dashdemux;
  GST_DEBUG ("created dash %p", dashdemux);

  g_signal_connect (dashdemux, "pad-added", G_CALLBACK (on_dashNewPad),
      testData);

  ret = gst_bin_add (GST_BIN (pipeline), dashdemux);
  fail_unless_equals_int (ret, TRUE);

  /* assume first entry in inputStreamArray configures the mpd stream */
  mpd_source =
      gst_element_make_from_uri (GST_URI_SRC, inputStreamArray[0].uri, NULL,
      NULL);
  fail_unless (mpd_source != NULL);
  testData->scratchData->mpd_source = mpd_source;

  ret = gst_bin_add (GST_BIN (pipeline), mpd_source);
  fail_unless_equals_int (ret, TRUE);

  ret = gst_element_link (mpd_source, dashdemux);
  fail_unless_equals_int (ret, TRUE);

  /* call a test callback before we start the pipeline */
  if (callbacks->preTestCallback)
    (*callbacks->preTestCallback) (testData);

  GST_DEBUG ("starting pipeline");
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  stateChange = gst_element_get_state (pipeline, NULL, NULL,
      GST_CLOCK_TIME_NONE);
  fail_unless_equals_int (stateChange, GST_STATE_CHANGE_SUCCESS);

  /* block until all output streams received an eos event */
  GST_DEBUG ("main thread waiting for streams to finish");
  g_main_loop_run (testData->scratchData->loop);
  GST_DEBUG ("main thread all streams finished. Stopping pipeline");

  g_main_loop_unref (testData->scratchData->loop);

  stateChange = gst_element_set_state (pipeline, GST_STATE_NULL);
  fail_unless_equals_int (stateChange, GST_STATE_CHANGE_SUCCESS);
  GST_DEBUG ("main thread pipeline stopped");
  gst_object_unref (pipeline);

  /* call a test callback after the stop of the pipeline */
  if (callbacks->postTestCallback)
    (*callbacks->postTestCallback) (testData);

  for (guint i = 0; i < outputStreamArraySize; ++i) {
    if (outputStreamArray[i].scratchData->appsink)
      gst_object_unref (outputStreamArray[i].scratchData->appsink);

    if (outputStreamArray[i].scratchData->internalPad)
      gst_object_unref (outputStreamArray[i].scratchData->internalPad);

    g_slice_free (GstDashDemuxTestOutputStreamScratchData,
        outputStreamArray[i].scratchData);
  }
  g_slice_free (GstDashDemuxTestScratchData, testData->scratchData);

  g_mutex_clear (&testData->lockStreamFinished);
  g_testData = NULL;
  g_slice_free (GstDashDemuxTestData, testData);
}

/******************** Dash demux test engine definition ends here *************/

/******************** Test specific code starts here **************************/

/* function to validate data received by AppSink */
static gboolean
checkDataReceived (GstDashDemuxTestData * testData,
    GstDashDemuxTestOutputStreamData * testOutputStreamData, GstBuffer * buffer)
{
  GstMapInfo info;
  guint pattern;
  guint64 streamOffset;

  gst_buffer_map (buffer, &info, GST_MAP_READ);

  GST_DEBUG
      ("segmentStart = %" G_GUINT64_FORMAT " segmentReceivedSize = %"
      G_GUINT64_FORMAT " bufferSize=%d",
      testOutputStreamData->scratchData->segmentStart,
      testOutputStreamData->scratchData->segmentReceivedSize, (gint) info.size);

  streamOffset = testOutputStreamData->scratchData->segmentStart +
      testOutputStreamData->scratchData->segmentReceivedSize;
  pattern = streamOffset - streamOffset % sizeof (pattern);
  for (guint64 i = 0; i != info.size; ++i) {
    guint received = info.data[i];
    guint expected;
    gchar pattern_byte_to_read;

    pattern_byte_to_read = (streamOffset + i) % sizeof (pattern);
    if (pattern_byte_to_read == 0) {
      pattern = streamOffset + i;
    }
    expected = (pattern >> (pattern_byte_to_read * 8)) & 0xFF;

/*
    GST_DEBUG
        ("received '0x%02x' expected '0x%02x' offset %" G_GUINT64_FORMAT
        " pattern=%08x byte_to_read=%d",
        received, expected, i, pattern, pattern_byte_to_read);
*/

    fail_unless (received == expected,
        "output validation failed: received '0x%02x' expected '0x%02x' offset %"
        G_GUINT64_FORMAT " pattern=%08x byte_to_read=%d\n", received, expected,
        i, pattern, pattern_byte_to_read);
  }

  gst_buffer_unmap (buffer, &info);
  return TRUE;
}

/* function to check total size of data received by AppSink
 * will be called when AppSink receives eos.
 */
static gboolean
checkSizeOfDataReceived (GstDashDemuxTestData * testData,
    GstDashDemuxTestOutputStreamData * testOutputStreamData)
{
  fail_unless (testOutputStreamData->scratchData->totalReceivedSize ==
      testOutputStreamData->expectedSize,
      "size validation failed for %s, expected %d received %d",
      testOutputStreamData->name, testOutputStreamData->expectedSize,
      testOutputStreamData->scratchData->totalReceivedSize);

  return TRUE;
}

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
      "     xmlns:yt=\"http://youtube.com/yt/2012/10/10\""
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

  const GstFakeHttpSrcInputData inputTestData[] = {
    {"http://unit.test/test.mpd", mpd, 0},
    {"http://unit.test/audio.webm", NULL, 5000},
    {"http://unit.test/video.webm", NULL, 9000},
    {NULL, NULL, 0},
  };

  GstDashDemuxTestOutputStreamData outputTestData[] = {
    {"audio_00", 5000, NULL},
    {"video_00", 9000, NULL},
  };

  Callbacks cb = { 0 };
  cb.appSinkGotDataCallback = checkDataReceived;
  cb.appSinkGotEosCallback = checkSizeOfDataReceived;

  runTest (inputTestData,
      outputTestData, sizeof (outputTestData) / sizeof (outputTestData[0]),
      &cb);
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
      "     xmlns:yt=\"http://youtube.com/yt/2012/10/10\""
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

  const GstFakeHttpSrcInputData inputTestData[] = {
    {"http://unit.test/test.mpd", mpd, 0},
    {"http://unit.test/audio1.webm", NULL, 5001},
    {"http://unit.test/video1.webm", NULL, 9001},
    {"http://unit.test/audio2.webm", NULL, 5002},
    {"http://unit.test/video2.webm", NULL, 9002},
    {NULL, NULL, 0},
  };

  GstDashDemuxTestOutputStreamData outputTestData[] = {
    {"audio_00", 5001, NULL},
    {"video_00", 9001, NULL},
    {"audio_01", 5002, NULL},
    {"video_01", 9002, NULL},
  };

  Callbacks cb = { 0 };
  cb.appSinkGotDataCallback = checkDataReceived;
  cb.appSinkGotEosCallback = checkSizeOfDataReceived;

  runTest (inputTestData,
      outputTestData, sizeof (outputTestData) / sizeof (outputTestData[0]),
      &cb);

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
setAndTestDashParams (GstDashDemuxTestData * testData)
{
  GstElement *dashdemux = testData->scratchData->dashdemux;

  /* num-lookback-fragments can only be set at construction */
  test_invalid_int_prop (G_OBJECT (dashdemux), "num-lookback-fragments", 10);

  test_int_prop (G_OBJECT (dashdemux), "connection-speed", 1000);
  test_invalid_int_prop (G_OBJECT (dashdemux), "connection-speed", 4294967 + 1);

  test_float_prop (G_OBJECT (dashdemux), "bitrate-limit", 1);
  test_invalid_float_prop (G_OBJECT (dashdemux), "bitrate-limit", 2.1);

  test_int_prop (G_OBJECT (dashdemux), "max-buffering-time", 15);
  test_invalid_int_prop (G_OBJECT (dashdemux), "max-buffering-time", 1);

  test_float_prop (G_OBJECT (dashdemux), "bandwidth-usage", 0.5);
  test_invalid_float_prop (G_OBJECT (dashdemux), "bandwidth-usage", 2);

  test_int_prop (G_OBJECT (dashdemux), "max-bitrate", 1000);
  test_invalid_int_prop (G_OBJECT (dashdemux), "max-bitrate", 10);
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
      "     xmlns:yt=\"http://youtube.com/yt/2012/10/10\""
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

  const GstFakeHttpSrcInputData inputTestData[] = {
    {"http://unit.test/test.mpd", mpd, 0},
    {"http://unit.test/audio.webm", NULL, 5000},
    {NULL, NULL, 0},
  };

  GstDashDemuxTestOutputStreamData outputTestData[] = {
    {"audio_00", 5000, NULL},
  };

  Callbacks cb = { 0 };
  cb.preTestCallback = setAndTestDashParams;
  cb.appSinkGotDataCallback = checkDataReceived;
  cb.appSinkGotEosCallback = checkSizeOfDataReceived;

  runTest (inputTestData,
      outputTestData, sizeof (outputTestData) / sizeof (outputTestData[0]),
      &cb);
}

GST_END_TEST;

/* check data received by AppSink when eos is received */
static gboolean
testSeekCheckSizeOfDataReceived (GstDashDemuxTestData * testData,
    GstDashDemuxTestOutputStreamData * testOutputStreamData)
{
  guint64 expectedSize;

  GST_DEBUG ("checkDataReceivedTestSeek for %s received %" G_GUINT64_FORMAT,
      testOutputStreamData->name,
      testOutputStreamData->scratchData->totalReceivedSize);

  /* the seek was to the beginning of the file, so expect to receive
   * thresholdForSeek + a whole file
   */
  expectedSize = testData->scratchData->thresholdForSeek +
      testOutputStreamData->expectedSize;
  fail_unless (testOutputStreamData->scratchData->totalReceivedSize ==
      expectedSize,
      "size validation failed for %s, expected %" G_GUINT64_FORMAT " received %"
      G_GUINT64_FORMAT, testOutputStreamData->name, expectedSize,
      testOutputStreamData->scratchData->totalReceivedSize);

  return TRUE;
}

/* function to generate a seek event. Will be run in a separate thread */
static void
testSeekTaskDoSeek (GstDashDemuxTestData * testData)
{
  GST_DEBUG ("testSeekTaskDoSeek calling seek");

  /* seek to 5ms.
   * Because there is only one fragment, we expect the whole file to be
   * downloaded again
   */
  if (!gst_element_seek_simple (GST_ELEMENT (testData->scratchData->pipeline),
          GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT,
          5 * GST_MSECOND)) {
    fail ("Seek failed!\n");
  }
  GST_DEBUG ("seek ok");
  gst_task_stop (testData->scratchData->seekTask);
}

/* function to be called during seek test when dash sends data to AppSink
 * It monitors the data sent and after a while will generate a seek request.
 */
static gboolean
testSeekDashdemuxSendsData (GstDashDemuxTestData * testData,
    GstDashDemuxTestOutputStreamData * testOutputStreamData, GstBuffer * buffer)
{
  if (strcmp (testOutputStreamData->name, "audio_00") == 0 &&
      testData->scratchData->seekTask == NULL &&
      testOutputStreamData->scratchData->totalReceivedSize +
      testOutputStreamData->scratchData->segmentReceivedSize >=
      testData->scratchData->thresholdForSeek) {

    GST_DEBUG ("starting seek task");

    g_mutex_lock (&testData->scratchData->seekTaskStateLock);
    testData->scratchData->seekTaskState =
        SEEK_TASK_STATE_WAITING_FOR_FAKESRC_STATE_CHANGE;
    g_mutex_unlock (&testData->scratchData->seekTaskStateLock);

    g_rec_mutex_init (&testData->scratchData->seekTaskLock);
    testData->scratchData->seekTask =
        gst_task_new ((GstTaskFunction) testSeekTaskDoSeek, testData, NULL);
    gst_task_set_lock (testData->scratchData->seekTask,
        &testData->scratchData->seekTaskLock);
    gst_task_start (testData->scratchData->seekTask);

    GST_DEBUG ("seek task started");

    g_mutex_lock (&testData->scratchData->seekTaskStateLock);

    GST_DEBUG ("waiting for seek task to change state on fakesrc");

    /* wait for seekTask to run, send a flush start event to AppSink
     * and change the fakesouphttpsrc element state from PLAYING to PAUSED
     */
    while (testData->scratchData->seekTaskState ==
        SEEK_TASK_STATE_WAITING_FOR_FAKESRC_STATE_CHANGE) {
      g_cond_wait (&testData->scratchData->seekTaskStateCond,
          &testData->scratchData->seekTaskStateLock);
    }
    g_mutex_unlock (&testData->scratchData->seekTaskStateLock);
    /* we can continue now, but this buffer will be rejected by AppSink
     * because it is in flushing mode
     */
    GST_DEBUG ("seek task changed state on fakesrc, resuming");
  }

  return TRUE;
}

/* callback called when main_loop detects a state changed event */
static void
testSeekOnStateChanged (GstBus * bus, GstMessage * msg, gpointer data)
{
  GstDashDemuxTestData *testData = (GstDashDemuxTestData *) data;
  GstDashDemuxTestOutputStreamData *testOutputStreamData;
  GstState old_state, new_state;
  const char *srcName = GST_OBJECT_NAME (msg->src);
  GstPad *internalPad;

  gst_message_parse_state_changed (msg, &old_state, &new_state, NULL);
  GST_DEBUG ("Element %s changed state from %s to %s",
      GST_OBJECT_NAME (msg->src),
      gst_element_state_get_name (old_state),
      gst_element_state_get_name (new_state));

  if (strstr (srcName, "fakesouphttpsrc") == srcName &&
      old_state == GST_STATE_PLAYING && new_state == GST_STATE_PAUSED) {
    GList *pads = GST_ELEMENT_PADS (msg->src);

    /* src is a fake http src element. It should have only 1 pad */
    fail_unless (pads != NULL);
    fail_unless (g_list_length (pads) == 1);

    internalPad = gst_pad_get_peer (GST_PAD (pads->data));
    testOutputStreamData = getTestOutputDataByPad (testData, internalPad);
    gst_object_unref (internalPad);

    if (strcmp (testOutputStreamData->name, "audio_00") == 0) {
      g_mutex_lock (&testData->scratchData->seekTaskStateLock);
      if (testData->scratchData->seekTaskState ==
          SEEK_TASK_STATE_WAITING_FOR_FAKESRC_STATE_CHANGE) {
        GST_DEBUG ("changing seekTaskState");
        testData->scratchData->seekTaskState = SEEK_TASK_STATE_EXITING;
        g_cond_signal (&testData->scratchData->seekTaskStateCond);
      }
      g_mutex_unlock (&testData->scratchData->seekTaskStateLock);
    }
  }
}

static void
testSeekPreTestCallback (GstDashDemuxTestData * testData)
{
  GstBus *bus;

  /* media segment starts at 4687
   * Issue a seek request after media segment has started to be downloaded
   * on audio_00 stream and the first chunk of GST_FAKE_SOUP_HTTP_SRC_MAX_BUF_SIZE
   * has already arrived in AppSink
   */
  testData->scratchData->thresholdForSeek = 4687 +
      GST_FAKE_SOUP_HTTP_SRC_MAX_BUF_SIZE;

  g_mutex_init (&testData->scratchData->seekTaskStateLock);
  g_cond_init (&testData->scratchData->seekTaskStateCond);

  /* register a callback to listen for state change events */
  bus = gst_pipeline_get_bus (GST_PIPELINE (testData->scratchData->pipeline));
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message::state-changed",
      G_CALLBACK (testSeekOnStateChanged), testData);
}

/* function to do test seek cleanup */
static void
testSeekPostTestCallback (GstDashDemuxTestData * testData)
{
  GstDashDemuxTestScratchData *scratchData = testData->scratchData;

  fail_if (scratchData->seekTask == NULL,
      "seek test did not create task to perform the seek");
  gst_task_stop (scratchData->seekTask);
  gst_task_join (scratchData->seekTask);
  GST_DEBUG ("task stopped");
  gst_object_unref (scratchData->seekTask);
  g_rec_mutex_clear (&scratchData->seekTaskLock);

  g_cond_clear (&testData->scratchData->seekTaskStateCond);
  g_mutex_clear (&testData->scratchData->seekTaskStateLock);
}

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
      "     xmlns:yt=\"http://youtube.com/yt/2012/10/10\""
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

  const GstFakeHttpSrcInputData inputTestData[] = {
    {"http://unit.test/test.mpd", mpd, 0},
    {"http://unit.test/audio.webm", NULL, 10000},
    {NULL, NULL, 0},
  };

  GstDashDemuxTestOutputStreamData outputTestData[] = {
    {"audio_00", 10000, NULL},
  };

  Callbacks cb = { 0 };
  cb.appSinkGotDataCallback = checkDataReceived;
  cb.appSinkGotEosCallback = testSeekCheckSizeOfDataReceived;
  cb.preTestCallback = testSeekPreTestCallback;
  cb.postTestCallback = testSeekPostTestCallback;
  cb.dashdemuxSendsDataCallback = testSeekDashdemuxSendsData;

  runTest (inputTestData,
      outputTestData, sizeof (outputTestData) / sizeof (outputTestData[0]),
      &cb);
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

  tcase_add_unchecked_fixture (tc_basicTest, test_setup, test_teardown);

  suite_add_tcase (s, tc_basicTest);

  return s;
}

GST_CHECK_MAIN (dash_demux);
