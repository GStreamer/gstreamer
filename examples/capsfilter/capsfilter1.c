#include <gst/gst.h>

/* This app uses a filter to connect colorspace and videosink
 * so that only RGB data can pass the connection, colorspace will use
 * a converter to convert the I420 data to RGB. Without a filter, this
 * connection would use the I420 format (assuming Xv is enabled) */

static void
new_pad_func (GstElement * element, GstPad * newpad, gpointer data)
{
  GstElement *pipeline = (GstElement *) data;
  GstElement *queue = gst_bin_get_by_name (GST_BIN (pipeline), "queue");

  if (!strcmp (gst_pad_get_name (newpad), "video_00")) {
    gst_element_set_state (pipeline, GST_STATE_PAUSED);
    gst_pad_link (newpad, gst_element_get_pad (queue, "sink"));
    gst_element_set_state (pipeline, GST_STATE_PLAYING);
  }
}

gint
main (gint argc, gchar * argv[])
{
  GstElement *pipeline;
  GstElement *filesrc;
  GstElement *demux;
  GstElement *thread;
  GstElement *queue;
  GstElement *mpeg2dec;
  GstElement *colorspace;
  GstElement *xvideosink;
  gboolean res;

  gst_init (&argc, &argv);

  if (argc < 2) {
    g_print ("usage: %s <mpeg1 system stream>\n", argv[0]);
    return (-1);
  }

  pipeline = gst_pipeline_new ("main_pipeline");
  filesrc = gst_element_factory_make ("filesrc", "filesrc");
  g_return_val_if_fail (filesrc, -1);
  g_object_set (G_OBJECT (filesrc), "location", argv[1], NULL);
  demux = gst_element_factory_make ("mpegdemux", "demux");
  g_return_val_if_fail (demux, -1);
  g_signal_connect (G_OBJECT (demux), "new_pad", G_CALLBACK (new_pad_func),
      pipeline);

  thread = gst_thread_new ("thread");
  queue = gst_element_factory_make ("queue", "queue");
  mpeg2dec = gst_element_factory_make ("mpeg2dec", "mpeg2dec");
  g_return_val_if_fail (mpeg2dec, -1);
  colorspace = gst_element_factory_make ("colorspace", "colorspace");
  g_return_val_if_fail (colorspace, -1);
  xvideosink = gst_element_factory_make ("xvideosink", "xvideosink");
  g_return_val_if_fail (xvideosink, -1);
  g_object_set (G_OBJECT (xvideosink), "toplevel", TRUE, NULL);

  gst_bin_add (GST_BIN (pipeline), filesrc);
  gst_bin_add (GST_BIN (pipeline), demux);

  gst_bin_add (GST_BIN (thread), queue);
  gst_bin_add (GST_BIN (thread), mpeg2dec);
  gst_bin_add (GST_BIN (thread), colorspace);
  gst_bin_add (GST_BIN (thread), xvideosink);
  gst_bin_add (GST_BIN (pipeline), thread);

  gst_element_link (filesrc, "src", demux, "sink");
  gst_element_link (queue, "src", mpeg2dec, "sink");
  gst_element_link (mpeg2dec, "src", colorspace, "sink");
  /* force RGB data passing between colorspace and xvideosink */
  res = gst_element_link_filtered (colorspace, "src", xvideosink, "sink",
      GST_CAPS_NEW ("filtercaps",
          "video/raw", "format", GST_PROPS_FOURCC (GST_STR_FOURCC ("RGB "))
      ));
  if (!res) {
    g_print ("could not connect colorspace and xvideosink\n");
    return -1;
  }

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  while (gst_bin_iterate (GST_BIN (pipeline)));

  gst_element_set_state (pipeline, GST_STATE_NULL);

  return 0;
}
