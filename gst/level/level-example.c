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
      gdouble endtime, rms_dB, peak_dB, decay_dB;
      gdouble rms;
      const GValue *list;
      const GValue *value;

      if (!gst_structure_get_double (s, "endtime", &endtime))
        g_warning ("Could not parse endtime");
      list = gst_structure_get_value (s, "rms");
      /* we can get the number of channels as the length of any of the value
       * lists */
      channels = gst_value_list_get_size (list);

      /* we will only get values for the first channel, since we know sinesrc
       * is mono */
      value = gst_value_list_get_value (list, 0);
      rms_dB = g_value_get_double (value);
      list = gst_structure_get_value (s, "peak");
      value = gst_value_list_get_value (list, 0);
      peak_dB = g_value_get_double (value);
      list = gst_structure_get_value (s, "decay");
      value = gst_value_list_get_value (list, 0);
      decay_dB = g_value_get_double (value);
      g_print ("endtime: %f, channels: %d\n", endtime, channels);
      g_print ("RMS: %f dB, peak: %f dB, decay: %f dB\n",
          rms_dB, peak_dB, decay_dB);

      /* converting from dB to normal gives us a value between 0.0 and 1.0 */
      rms = pow (10, rms_dB / 20);
      g_print ("normalized rms value: %f\n", rms);
    }
  }
  /* we handled the message we want, and ignored the ones we didn't want.
   * so the core can unref the message for us */
  return TRUE;
}

int
main (int argc, char *argv[])
{
  GstElement *sinesrc, *level, *fakesink;
  GstElement *pipeline;
  GstBus *bus;
  gint watch_id;
  GMainLoop *loop;

  gst_init (&argc, &argv);

  pipeline = gst_pipeline_new (NULL);
  g_assert (pipeline);
  sinesrc = gst_element_factory_make ("sinesrc", NULL);
  g_assert (sinesrc);
  level = gst_element_factory_make ("level", NULL);
  g_assert (level);
  fakesink = gst_element_factory_make ("fakesink", NULL);
  g_assert (fakesink);

  gst_bin_add_many (GST_BIN (pipeline), sinesrc, level, fakesink, NULL);
  gst_element_link (sinesrc, level);
  gst_element_link (level, fakesink);

  /* make sure we'll get messages */
  g_object_set (G_OBJECT (level), "message", TRUE, NULL);

  bus = gst_element_get_bus (pipeline);
  watch_id = gst_bus_add_watch (bus, message_handler, NULL);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  /* we need to run a GLib main loop to get the messages */
  loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (loop);

  return 0;
}
