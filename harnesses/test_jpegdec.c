/* GStreamer
 * harness for smokedec
 */


 #include <unistd.h>

 #include <glib.h>
 #include <gst/gst.h>
 #include <gio/gio.h>
 //#include <gst/check/gstcheck.h>
 #include <gst/app/gstappsink.h>
 //#include <gst/pbutils/gstdiscoverer.h>

static void custom_logger (const gchar * log_domain, GLogLevelFlags log_level, const gchar * message, gpointer unused_data)
{
  if (log_level & G_LOG_LEVEL_CRITICAL) {
    g_printerr ("CRITICAL ERROR : %s\n", message);
    abort ();
  } else if (log_level & G_LOG_LEVEL_WARNING) {
    g_printerr ("WARNING : %s\n", message);
  }
}

int LLVMFuzzerTestOneInput(const char *data, size_t size)
{
  static gboolean initialized = FALSE;
  GstElement *pipeline, *source, *dec, *sink;
  GstBuffer *buf;
  GstFlowReturn flowret;
  GstState state;
  //GstSample *sample;

  if (!initialized) {
    /* We want critical warnings to assert so we can fix them */
    g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL);
    g_log_set_default_handler (custom_logger, NULL);

    /* Only initialize and register plugins once */
    gst_init (NULL, NULL);

    initialized = TRUE;
  }

  /* construct a pipeline that explicitly uses jpegdec */
  pipeline = gst_pipeline_new (NULL);
  g_assert (pipeline);
  source = gst_element_factory_make ("appsrc", NULL);
  g_assert (source);
  dec = gst_element_factory_make ("jpegdec", NULL);
  g_assert (dec);
  sink = gst_element_factory_make ("appsink", NULL);
  g_assert (sink);

  gst_bin_add_many (GST_BIN (pipeline), source, dec, sink, NULL);
  gst_element_link_many (source, dec, sink, NULL);

  /* Set pipeline to READY so we can provide data to appsrc */
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_READY);
  buf = gst_buffer_new_wrapped_full (0, (gpointer) data, size,
      0, size, NULL, NULL);
  g_object_set (G_OBJECT (source), "size", size, NULL);
  g_signal_emit_by_name (G_OBJECT (source), "push-buffer", buf, &flowret);
  gst_buffer_unref (buf);

  /* Set pipeline to PAUSED and wait (typefind will either fail or succeed) */
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PAUSED);

  /* wait until state change either completes or fails */
  gst_element_get_state (GST_ELEMENT (pipeline), &state, NULL, -1);

  // Need to include gst-check somehow...
  //sample = gst_app_sink_pull_sample (GST_APP_SINK (sink));
  //fail_unless (GST_IS_SAMPLE (sample));

  /* do some basic checks to verify image decoding */
  {
    //GstCaps *decoded;
    //GstCaps *expected;

    //decoded = gst_sample_get_caps (sample);
    //expected = gst_caps_from_string ("video/x-raw, width=120, height=160");

    //fail_unless (gst_caps_is_always_compatible (decoded, expected));

    //gst_caps_unref (expected);
  }
  //gst_sample_unref (sample);

  /* Go back to NULL */
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);

  /* And release the pipeline */
  gst_object_unref (pipeline);
  return 0;
}
