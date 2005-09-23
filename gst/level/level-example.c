#include <string.h>
#include <math.h>

#include <gst/gst.h>

gboolean
message_handler (GstBus * bus, GstMessage * message, gpointer data)
{

  if (message->type == GST_MESSAGE_APPLICATION) {
    const GstStructure *s = gst_message_get_structure (message);
    const gchar *name = gst_structure_get_name (s);

    if (strcmp (name, "level") == 0) {
      gint channels;
      GstClockTime endtime;
      gdouble rms_dB, peak_dB, decay_dB;
      gdouble rms;
      const GValue *list;
      const GValue *value;

      gint i;

      if (!gst_structure_get_clock_time (s, "endtime", &endtime))
        g_warning ("Could not parse endtime");
      /* we can get the number of channels as the length of any of the value
       * lists */
      list = gst_structure_get_value (s, "rms");
      channels = gst_value_list_get_size (list);

      g_print ("endtime: %" GST_TIME_FORMAT ", channels: %d\n",
          GST_TIME_ARGS (endtime), channels);
      for (i = 0; i < channels; ++i) {
        g_print ("channel %d\n", i);
        list = gst_structure_get_value (s, "rms");
        value = gst_value_list_get_value (list, i);
        rms_dB = g_value_get_double (value);
        list = gst_structure_get_value (s, "peak");
        value = gst_value_list_get_value (list, i);
        peak_dB = g_value_get_double (value);
        list = gst_structure_get_value (s, "decay");
        value = gst_value_list_get_value (list, i);
        decay_dB = g_value_get_double (value);
        g_print ("    RMS: %f dB, peak: %f dB, decay: %f dB\n",
            rms_dB, peak_dB, decay_dB);

        /* converting from dB to normal gives us a value between 0.0 and 1.0 */
        rms = pow (10, rms_dB / 20);
        g_print ("    normalized rms value: %f\n", rms);
      }
    }
  }
  /* we handled the message we want, and ignored the ones we didn't want.
   * so the core can unref the message for us */
  return TRUE;
}

int
main (int argc, char *argv[])
{
  GstElement *sinesrc, *audioconvert, *level, *fakesink;
  GstElement *pipeline;
  GstCaps *caps;
  GstBus *bus;
  gint watch_id;
  GMainLoop *loop;

  gst_init (&argc, &argv);

  caps = gst_caps_from_string ("audio/x-raw-int,channels=2");

  pipeline = gst_pipeline_new (NULL);
  g_assert (pipeline);
  sinesrc = gst_element_factory_make ("sinesrc", NULL);
  g_assert (sinesrc);
  audioconvert = gst_element_factory_make ("audioconvert", NULL);
  g_assert (audioconvert);
  level = gst_element_factory_make ("level", NULL);
  g_assert (level);
  fakesink = gst_element_factory_make ("fakesink", NULL);
  g_assert (fakesink);

  gst_bin_add_many (GST_BIN (pipeline), sinesrc, audioconvert, level,
      fakesink, NULL);
  g_assert (gst_element_link (sinesrc, audioconvert));
  g_assert (gst_element_link_filtered (audioconvert, level, caps));
  g_assert (gst_element_link (level, fakesink));

  /* make sure we'll get messages */
  g_object_set (G_OBJECT (level), "message", TRUE, NULL);

  bus = gst_element_get_bus (pipeline);
  watch_id = gst_bus_add_watch (bus, GST_MESSAGE_ANY, message_handler, NULL);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  /* we need to run a GLib main loop to get the messages */
  loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (loop);

  return 0;
}
