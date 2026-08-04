/* GStreamer unit tests for the pulseaudio elements
 *
 * Copyright (C) 2026 Igalia S.L.
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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <gst/check/gstcheck.h>

#define NUM_RACERS 4
#define NUM_TOGGLES 200
#define NUM_CYCLES 10
#define TIMEOUT_US (3 * G_USEC_PER_SEC)

typedef struct
{
  GstElement *element;
  GstPad *pad;
  GstCaps *caps;
  GstClock *clock;
  GstState low;
  GstState high;
  gint clock_reads;
  gint stop;
} RaceCtx;

static void
assert_no_error (GstElement * element)
{
  GstBus *bus;
  GstMessage *msg;

  bus = gst_element_get_bus (element);
  msg = gst_bus_pop_filtered (bus, GST_MESSAGE_ERROR);
  gst_object_unref (bus);

  if (msg) {
    GError *err = NULL;

    gst_message_parse_error (msg, &err, NULL);
    fail ("%s posted an error: %s", GST_MESSAGE_SRC_NAME (msg), err->message);
  }
}

static GstElement *
start_pipeline (const gchar * description, GstState state)
{
  GstElement *pipeline;
  GstStateChangeReturn ret;

  pipeline = gst_parse_launch (description, NULL);
  fail_unless (pipeline != NULL);

  if (gst_element_set_state (pipeline,
          GST_STATE_READY) != GST_STATE_CHANGE_SUCCESS) {
    GST_INFO ("no usable pulseaudio daemon, skipping");
    gst_element_set_state (pipeline, GST_STATE_NULL);
    gst_object_unref (pipeline);
    return NULL;
  }

  ret = gst_element_set_state (pipeline, state);
  if (ret != GST_STATE_CHANGE_FAILURE)
    ret = gst_element_get_state (pipeline, NULL, NULL, 5 * GST_SECOND);

  if (ret == GST_STATE_CHANGE_FAILURE || ret == GST_STATE_CHANGE_ASYNC) {
    gst_element_set_state (pipeline, GST_STATE_NULL);
    fail ("%s did not reach %s", description,
        gst_element_state_get_name (state));
  }

  return pipeline;
}

static gpointer
toggle_state_func (gpointer data)
{
  RaceCtx *ctx = data;
  gint64 deadline = g_get_monotonic_time () + TIMEOUT_US;
  guint i;

  for (i = 0; i < NUM_TOGGLES && g_get_monotonic_time () < deadline; i++) {
    gst_element_set_state (ctx->element, ctx->high);
    gst_element_get_state (ctx->element, NULL, NULL, GST_SECOND);
    gst_element_set_state (ctx->element, ctx->low);
  }

  g_atomic_int_set (&ctx->stop, 1);
  return NULL;
}

static gpointer
query_caps_func (gpointer data)
{
  RaceCtx *ctx = data;

  while (!g_atomic_int_get (&ctx->stop)) {
    GstCaps *caps;
    GstQuery *query;

    caps = gst_pad_query_caps (ctx->pad, NULL);
    if (caps)
      gst_caps_unref (caps);

    query = gst_query_new_accept_caps (ctx->caps);
    gst_pad_query (ctx->pad, query);
    gst_query_unref (query);
  }

  return NULL;
}

static gpointer
access_properties_func (gpointer data)
{
  RaceCtx *ctx = data;

  while (!g_atomic_int_get (&ctx->stop)) {
    gchar *device, *device_name, *current_device;
    gdouble volume;
    gboolean mute;

    g_object_get (ctx->element, "device", &device, "device-name", &device_name,
        "current-device", &current_device, "volume", &volume, "mute", &mute,
        NULL);
    g_free (device_name);
    g_free (current_device);

    g_object_set (ctx->element, "device", device, "volume", volume, "mute",
        mute, NULL);
    g_free (device);
  }

  return NULL;
}

static gpointer
query_device_name_func (gpointer data)
{
  RaceCtx *ctx = data;
  gint64 deadline = g_get_monotonic_time () + TIMEOUT_US;

  while (g_get_monotonic_time () < deadline) {
    gchar *device_name;

    g_object_get (ctx->element, "device-name", &device_name, NULL);
    g_free (device_name);
  }

  return NULL;
}

static gpointer
access_string_properties_func (gpointer data)
{
  RaceCtx *ctx = data;
  gint64 deadline = g_get_monotonic_time () + TIMEOUT_US;
  GstStructure *properties;

  properties = gst_structure_new ("properties", "media.role", G_TYPE_STRING,
      "test", NULL);

  while (g_get_monotonic_time () < deadline) {
    GstStructure *stream_properties;
    gchar *device;

    g_object_set (ctx->element, "device", "test", "stream-properties",
        properties, NULL);
    g_object_get (ctx->element, "device", &device, "stream-properties",
        &stream_properties, NULL);
    g_free (device);
    gst_structure_free (stream_properties);
  }

  gst_structure_free (properties);

  return NULL;
}

static gpointer
read_clock_func (gpointer data)
{
  RaceCtx *ctx = data;

  while (!g_atomic_int_get (&ctx->stop))
    gst_clock_get_time (ctx->clock);

  return NULL;
}

static void
release_element (GstElement * element)
{
  GstBus *bus;

  gst_element_set_state (element, GST_STATE_NULL);
  bus = gst_element_get_bus (element);
  gst_bus_set_flushing (bus, TRUE);
  gst_object_unref (bus);
  gst_object_unref (element);
}

static void
race_ctx_init_element (RaceCtx * ctx, const gchar * factory)
{
  GstBus *bus;

  memset (ctx, 0, sizeof (*ctx));
  ctx->low = GST_STATE_NULL;
  ctx->high = GST_STATE_READY;

  ctx->element = gst_element_factory_make (factory, NULL);
  fail_unless (ctx->element != NULL);
  gst_object_ref_sink (ctx->element);

  bus = gst_bus_new ();
  gst_element_set_bus (ctx->element, bus);
  gst_object_unref (bus);
}

static gboolean
race_ctx_init (RaceCtx * ctx, const gchar * factory)
{
  race_ctx_init_element (ctx, factory);

  if (gst_element_set_state (ctx->element,
          GST_STATE_READY) != GST_STATE_CHANGE_SUCCESS) {
    GST_INFO ("no usable pulseaudio daemon, skipping");
    release_element (ctx->element);
    ctx->element = NULL;
    return FALSE;
  }

  gst_element_set_state (ctx->element, GST_STATE_NULL);

  return TRUE;
}

static void
race_ctx_run (RaceCtx * ctx, GThreadFunc racer_func, guint n_racers)
{
  GThread *racers[NUM_RACERS];
  GThread *toggler;
  guint i;

  g_assert (n_racers <= NUM_RACERS);

  toggler = g_thread_new ("toggler", toggle_state_func, ctx);
  for (i = 0; i < n_racers; i++)
    racers[i] = g_thread_new ("racer", racer_func, ctx);

  g_thread_join (toggler);
  for (i = 0; i < n_racers; i++)
    g_thread_join (racers[i]);

  assert_no_error (ctx->element);
}

static void
race_ctx_clear (RaceCtx * ctx)
{
  gst_clear_caps (&ctx->caps);
  gst_clear_object (&ctx->clock);
  if (ctx->pad)
    gst_object_unref (ctx->pad);
  release_element (ctx->element);
}

static GstBusSyncReply
read_properties_on_error (GstBus * bus, GstMessage * msg, gpointer data)
{
  gint *errors = data;
  gchar *device, *device_name, *current_device;
  gdouble volume;

  if (GST_MESSAGE_TYPE (msg) != GST_MESSAGE_ERROR)
    return GST_BUS_PASS;

  g_object_get (GST_MESSAGE_SRC (msg), "device", &device, "device-name",
      &device_name, "current-device", &current_device, "volume", &volume, NULL);
  g_free (device);
  g_free (device_name);
  g_free (current_device);
  g_atomic_int_inc (errors);

  return GST_BUS_PASS;
}

static void
check_properties_on_connection_error (const gchar * factory)
{
  GstElement *element;
  GstStateChangeReturn ret;
  GstBus *bus;
  gint errors = 0;

  element = gst_element_factory_make (factory, NULL);
  fail_unless (element != NULL);
  gst_object_ref_sink (element);
  g_object_set (element, "server", "unix:/nonexistent/native", NULL);

  bus = gst_bus_new ();
  gst_element_set_bus (element, bus);
  gst_bus_set_sync_handler (bus, read_properties_on_error, &errors, NULL);

  ret = gst_element_set_state (element, GST_STATE_READY);

  gst_bus_set_sync_handler (bus, NULL, NULL, NULL);
  gst_object_unref (bus);
  release_element (element);

  fail_unless_equals_int (ret, GST_STATE_CHANGE_FAILURE);
  fail_unless (g_atomic_int_get (&errors) > 0);
}

static void
check_current_device_after_playing (const gchar * description)
{
  GstElement *pipeline, *element;
  gchar *current_device = NULL;
  gint64 deadline;

  pipeline = start_pipeline (description, GST_STATE_PLAYING);
  if (!pipeline)
    return;

  element = gst_bin_get_by_name (GST_BIN (pipeline), "pulse");
  fail_unless (element != NULL);

  deadline = g_get_monotonic_time () + TIMEOUT_US;
  do {
    g_free (current_device);
    g_object_get (element, "current-device", &current_device, NULL);
  } while (*current_device == '\0' && g_get_monotonic_time () < deadline);

  gst_element_set_state (pipeline, GST_STATE_READY);
  fail_if (*current_device == '\0');
  g_free (current_device);

  g_object_get (element, "current-device", &current_device, NULL);
  fail_unless_equals_string (current_device, "");
  g_free (current_device);

  assert_no_error (pipeline);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (element);
  gst_object_unref (pipeline);
}

static GstBusSyncReply
read_clock_on_stream_status (GstBus * bus, GstMessage * msg, gpointer data)
{
  RaceCtx *ctx = data;

  if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_STREAM_STATUS &&
      GST_MESSAGE_SRC (msg) == GST_OBJECT_CAST (ctx->element)) {
    gst_clock_get_time (ctx->clock);
    g_atomic_int_inc (&ctx->clock_reads);
  }

  return GST_BUS_PASS;
}

/* The pa_threaded_mainloop is shared by all pulsesinks and freed when the last
 * one leaves READY, which used to race with the CAPS and ACCEPT_CAPS queries
 * taking that same mainloop's lock. */
GST_START_TEST (test_pulsesink_query_caps_while_leaving_ready)
{
  RaceCtx ctx;

  if (!race_ctx_init (&ctx, "pulsesink"))
    return;

  ctx.pad = gst_element_get_static_pad (ctx.element, "sink");
  fail_unless (ctx.pad != NULL);
  ctx.caps = gst_caps_new_simple ("audio/x-raw",
      "format", G_TYPE_STRING, "S16LE", "rate", G_TYPE_INT, 44100,
      "channels", G_TYPE_INT, 2, "layout", G_TYPE_STRING, "interleaved", NULL);

  race_ctx_run (&ctx, query_caps_func, NUM_RACERS);
  race_ctx_clear (&ctx);
}

GST_END_TEST;

/* Same race as above, this time through the properties that talk to the daemon.
 * One racer only: property accesses still race against each other over the
 * element's cached values, which is a different problem. */
GST_START_TEST (test_pulsesink_properties_while_leaving_ready)
{
  RaceCtx ctx;

  if (!race_ctx_init (&ctx, "pulsesink"))
    return;

  race_ctx_run (&ctx, access_properties_func, 1);
  race_ctx_clear (&ctx);
}

GST_END_TEST;

/* pulsesrc has a main loop per element, but the properties race against its
 * READY_TO_NULL just the same */
GST_START_TEST (test_pulsesrc_properties_while_leaving_ready)
{
  RaceCtx ctx;

  if (!race_ctx_init (&ctx, "pulsesrc"))
    return;

  race_ctx_run (&ctx, access_properties_func, 1);
  race_ctx_clear (&ctx);
}

GST_END_TEST;

static void
check_concurrent_access (const gchar * factory, GstState state,
    GThreadFunc racer_func)
{
  RaceCtx ctx;
  GThread *racers[NUM_RACERS];
  guint i;

  if (state == GST_STATE_NULL)
    race_ctx_init_element (&ctx, factory);
  else if (!race_ctx_init (&ctx, factory))
    return;

  if (gst_element_set_state (ctx.element, state) != GST_STATE_CHANGE_SUCCESS) {
    GST_INFO ("lost the pulseaudio daemon, skipping");
    race_ctx_clear (&ctx);
    return;
  }

  for (i = 0; i < NUM_RACERS; i++)
    racers[i] = g_thread_new ("racer", racer_func, &ctx);
  for (i = 0; i < NUM_RACERS; i++)
    g_thread_join (racers[i]);

  assert_no_error (ctx.element);
  race_ctx_clear (&ctx);
}

/* No state changes here: querying the device description used to keep its
 * result in the element, so two readers would free it under each other */
GST_START_TEST (test_pulsesink_concurrent_device_name)
{
  check_concurrent_access ("pulsesink", GST_STATE_READY,
      query_device_name_func);
}

GST_END_TEST;

GST_START_TEST (test_pulsesink_concurrent_string_properties)
{
  check_concurrent_access ("pulsesink", GST_STATE_READY,
      access_string_properties_func);
}

GST_END_TEST;

GST_START_TEST (test_pulsesrc_concurrent_string_properties)
{
  check_concurrent_access ("pulsesrc", GST_STATE_READY,
      access_string_properties_func);
}

GST_END_TEST;

GST_START_TEST (test_pulsesink_concurrent_string_properties_in_null)
{
  check_concurrent_access ("pulsesink", GST_STATE_NULL,
      access_string_properties_func);
}

GST_END_TEST;

GST_START_TEST (test_pulsesrc_concurrent_string_properties_in_null)
{
  check_concurrent_access ("pulsesrc", GST_STATE_NULL,
      access_string_properties_func);
}

GST_END_TEST;

GST_START_TEST (test_pulsesink_current_device_after_playing)
{
  check_current_device_after_playing
      ("audiotestsrc wave=silence ! pulsesink name=pulse");
}

GST_END_TEST;

GST_START_TEST (test_pulsesrc_current_device_after_playing)
{
  check_current_device_after_playing
      ("pulsesrc name=pulse device=@DEFAULT_MONITOR@ ! fakesink");
}

GST_END_TEST;

GST_START_TEST (test_pulsesink_clock_in_stream_status)
{
  GstElement *pipeline;
  GThread *racer;
  GstBus *bus;
  RaceCtx ctx;
  guint i;

  pipeline = start_pipeline ("audiotestsrc wave=silence ! pulsesink name=pulse",
      GST_STATE_PAUSED);
  if (!pipeline)
    return;

  memset (&ctx, 0, sizeof (ctx));
  ctx.element = gst_bin_get_by_name (GST_BIN (pipeline), "pulse");
  fail_unless (ctx.element != NULL);
  ctx.pad = gst_element_get_static_pad (ctx.element, "sink");
  ctx.caps = gst_pad_get_current_caps (ctx.pad);
  fail_unless (ctx.caps != NULL);
  ctx.clock = gst_element_provide_clock (ctx.element);
  fail_unless (ctx.clock != NULL);

  bus = gst_element_get_bus (pipeline);
  gst_bus_set_sync_handler (bus, read_clock_on_stream_status, &ctx, NULL);

  racer = g_thread_new ("racer", query_caps_func, &ctx);
  for (i = 0; i < NUM_CYCLES; i++) {
    gint clock_reads = g_atomic_int_get (&ctx.clock_reads);
    gint64 deadline;

    gst_element_set_state (pipeline, GST_STATE_PLAYING);
    gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);
    deadline = g_get_monotonic_time () + G_USEC_PER_SEC;
    while (g_atomic_int_get (&ctx.clock_reads) == clock_reads &&
        g_get_monotonic_time () < deadline)
      g_usleep (1000);
    gst_element_set_state (pipeline, GST_STATE_PAUSED);
    gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);
  }
  g_atomic_int_set (&ctx.stop, 1);
  g_thread_join (racer);

  gst_bus_set_sync_handler (bus, NULL, NULL, NULL);
  gst_object_unref (bus);

  gst_element_set_state (pipeline, GST_STATE_READY);
  fail_unless (g_atomic_int_get (&ctx.clock_reads) > 0);
  assert_no_error (pipeline);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  race_ctx_clear (&ctx);
  gst_object_unref (pipeline);
}

GST_END_TEST;

GST_START_TEST (test_pulsesink_clock_while_leaving_paused)
{
  GstElement *sink;
  RaceCtx ctx;

  memset (&ctx, 0, sizeof (ctx));
  ctx.low = GST_STATE_READY;
  ctx.high = GST_STATE_PAUSED;

  ctx.element =
      start_pipeline ("audiotestsrc wave=silence ! pulsesink name=pulse",
      GST_STATE_PAUSED);
  if (!ctx.element)
    return;

  sink = gst_bin_get_by_name (GST_BIN (ctx.element), "pulse");
  fail_unless (sink != NULL);
  ctx.clock = gst_element_provide_clock (sink);
  fail_unless (ctx.clock != NULL);
  gst_object_unref (sink);

  race_ctx_run (&ctx, read_clock_func, NUM_RACERS);
  race_ctx_clear (&ctx);
}

GST_END_TEST;

GST_START_TEST (test_pulsesink_properties_on_connection_error)
{
  check_properties_on_connection_error ("pulsesink");
}

GST_END_TEST;

GST_START_TEST (test_pulsesrc_properties_on_connection_error)
{
  check_properties_on_connection_error ("pulsesrc");
}

GST_END_TEST;

static Suite *
pulse_suite (void)
{
  Suite *s = suite_create ("pulse");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_pulsesink_query_caps_while_leaving_ready);
  tcase_add_test (tc_chain, test_pulsesink_properties_while_leaving_ready);
  tcase_add_test (tc_chain, test_pulsesrc_properties_while_leaving_ready);
  tcase_add_test (tc_chain, test_pulsesink_concurrent_device_name);
  tcase_add_test (tc_chain, test_pulsesink_concurrent_string_properties);
  tcase_add_test (tc_chain, test_pulsesrc_concurrent_string_properties);
  tcase_add_test (tc_chain,
      test_pulsesink_concurrent_string_properties_in_null);
  tcase_add_test (tc_chain, test_pulsesrc_concurrent_string_properties_in_null);
  tcase_add_test (tc_chain, test_pulsesink_current_device_after_playing);
  tcase_add_test (tc_chain, test_pulsesrc_current_device_after_playing);
  tcase_add_test (tc_chain, test_pulsesink_clock_in_stream_status);
  tcase_add_test (tc_chain, test_pulsesink_clock_while_leaving_paused);
  tcase_add_test (tc_chain, test_pulsesink_properties_on_connection_error);
  tcase_add_test (tc_chain, test_pulsesrc_properties_on_connection_error);

  return s;
}

GST_CHECK_MAIN (pulse);
