#include <gst/gst.h>

static GstElement *src1, *src2, *sink, *pipeline, *bin;
static gint state = 0;

static gboolean
notify (GstProbe * probe, GstData ** data, gpointer user_data)
{
  switch (state) {
    case 0:
      if (GST_BUFFER_TIMESTAMP (*data) == 10) {
        gst_element_set_state (GST_ELEMENT (bin), GST_STATE_PAUSED);

        gst_element_unlink_pads (GST_ELEMENT (src1), "src", sink, "sink");
        gst_bin_add (GST_BIN (bin), src2);
        gst_bin_remove (GST_BIN (bin), src1);
        gst_element_link_pads (GST_ELEMENT (src2), "src", sink, "sink");
        gst_element_set_state (GST_ELEMENT (bin), GST_STATE_PLAYING);
        state++;
        gst_data_unref (*data);
        return FALSE;
      }
      break;
    case 1:
      GST_BUFFER_TIMESTAMP (*data) = GST_BUFFER_TIMESTAMP (*data) + 10;
      if (GST_BUFFER_TIMESTAMP (*data) == 20) {
        gst_data_unref (*data);
        *data = GST_DATA (gst_event_new (GST_EVENT_EOS));
        gst_element_set_state (src2, GST_STATE_PAUSED);
        return TRUE;
      }
      break;
    default:
      break;
  }

  return TRUE;
}

int
main (int argc, gchar * argv[])
{
  gst_init (&argc, &argv);

  pipeline = gst_pipeline_new ("main_pipeline");
  bin = gst_bin_new ("control");

  src1 = gst_element_factory_make ("fakesrc", "src1");
  src2 = gst_element_factory_make ("fakesrc", "src2");

  gst_bin_add (GST_BIN (bin), src1);

  sink = gst_element_factory_make ("fakesink", "sink");
  gst_bin_add (GST_BIN (pipeline), sink);
  gst_bin_add (GST_BIN (pipeline), bin);

  gst_element_link_pads (GST_ELEMENT (src1), "src", sink, "sink");

  g_signal_connect (pipeline, "deep_notify",
      G_CALLBACK (gst_element_default_deep_notify), NULL);
  g_signal_connect (pipeline, "error", G_CALLBACK (gst_element_default_error),
      NULL);

  gst_pad_add_probe (gst_element_get_pad (src1, "src"),
      gst_probe_new (FALSE, notify, NULL));

  gst_pad_add_probe (gst_element_get_pad (src2, "src"),
      gst_probe_new (FALSE, notify, NULL));

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);

  while (gst_bin_iterate (GST_BIN (pipeline)));

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);

  gst_object_unref (GST_OBJECT (pipeline));

  return 0;
}
