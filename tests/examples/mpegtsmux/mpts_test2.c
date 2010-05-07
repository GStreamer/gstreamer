#include <gst/gst.h>

static gboolean
bus_call (GstBus * bus, GstMessage * msg, gpointer data)
{
  GMainLoop *loop = (GMainLoop *) data;

  switch (GST_MESSAGE_TYPE (msg)) {

    case GST_MESSAGE_EOS:
      g_print ("End of stream\n");
      g_main_loop_quit (loop);
      break;

    case GST_MESSAGE_ERROR:{
      gchar *debug;
      GError *error;

      gst_message_parse_error (msg, &error, &debug);
      g_free (debug);

      g_printerr ("Error: %s\n", error->message);
      g_error_free (error);

      g_main_loop_quit (loop);
      break;
    }
    default:
      break;
  }

  return TRUE;
}

static void
on_pad_added (GstElement * element, GstPad * pad, gpointer data)
{
  GstPad *sinkpad;
  GstElement *decoder = (GstElement *) data;

  /* We can now link this pad with the vorbis-decoder sink pad */
  g_print ("Dynamic pad created, linking demuxer/decoder\n");

  sinkpad = gst_element_get_static_pad (decoder, "sink");

  gst_pad_link (pad, sinkpad);

  gst_object_unref (sinkpad);
}


#define NR_PROG 3

int
main (int argc, char *argv[])
{
  GMainLoop *loop;
  GstElement *pipeline, *sink, *mux;
  GstElement *vsrc[NR_PROG];
  GstElement *asrc[NR_PROG];
  GstElement *vparse[NR_PROG];
  GstElement *vdemux[NR_PROG];
  GstElement *aparse[NR_PROG];
  GstPad *tl_pad, *pad;
  GstStructure *pm;
  GstBus *bus;

  FILE *xml_of;

  gchar vname[][60] = {
    "/Users/lyang/src/res/mpts.test/mpts110.mpv",
    "/Users/lyang/src/res/mpts.test/mpts120.mpv",
    "/Users/lyang/src/res/mpts.test/mpts130.mpv",
    "/Users/lyang/src/res/mpts.test/mpts140.mpv",
    "/Users/lyang/src/res/mpts.test/mpts150.mpv",
    "/Users/lyang/src/res/mpts.test/mpts160.mpv",
    "/Users/lyang/src/res/mpts.test/mpts170.mpv"
  };
  gchar aname[][60] = {
    "/Users/lyang/src/res/mpts.test/mpts113.mpa",
    "/Users/lyang/src/res/mpts.test/mpts123.mpa",
    "/Users/lyang/src/res/mpts.test/mpts133.mpa",
    "/Users/lyang/src/res/mpts.test/mpts143.mpa",
    "/Users/lyang/src/res/mpts.test/mpts153.mpa",
    "/Users/lyang/src/res/mpts.test/mpts163.mpa",
    "/Users/lyang/src/res/mpts.test/mpts173.mpa"
  };
  gchar dest_dir[60];
  gchar dest_xml[60];

  gint i;

  gst_init (&argc, &argv);
  loop = g_main_loop_new (NULL, FALSE);

  pipeline = gst_pipeline_new ("mpeg-ts-muxer");
  mux = gst_element_factory_make ("mpegtsmux", "muxer");
  sink = gst_element_factory_make ("filesink", "sink");
  if (!pipeline || !mux || !sink) {
    g_printerr ("Some element could not be created.\n");
    return -1;
  }

  for (i = 0; i < NR_PROG; i++) {
    vsrc[i] = gst_element_factory_make ("filesrc", NULL);
    vdemux[i] = gst_element_factory_make ("mpegpsdemux", NULL);
    vparse[i] = gst_element_factory_make ("mpegvideoparse", NULL);

    asrc[i] = gst_element_factory_make ("filesrc", NULL);
    aparse[i] = gst_element_factory_make ("mp3parse", NULL);

    if (!vsrc[i] || !vparse[i] || !vdemux[i] || !asrc[i] || !aparse[i]) {
      g_printerr ("Some element could not be created. i=%d.\n", i);
      return -1;
    }
  }

  /* Setting paths */
  for (i = 0; i < NR_PROG; i++) {
    g_object_set (G_OBJECT (vsrc[i]), "location", vname[i], NULL);
    g_object_set (G_OBJECT (asrc[i]), "location", aname[i], NULL);
  }

  sprintf (dest_dir, "/Users/lyang/src/res/mpts.test/mpts_%02d.ts", NR_PROG);
  g_object_set (G_OBJECT (sink), "location", dest_dir, NULL);

  /* construct the pipeline */
  gst_bin_add_many (GST_BIN (pipeline), mux, sink, NULL);
  gst_element_link (mux, sink);
  for (i = 0; i < NR_PROG; i++) {
    gst_bin_add_many (GST_BIN (pipeline), vsrc[i], vdemux[i], vparse[i], NULL);
    gst_element_link (vsrc[i], vdemux[i]);

    g_signal_connect (vdemux[i], "pad-added", G_CALLBACK (on_pad_added),
        vparse[i]);

    gst_bin_add_many (GST_BIN (pipeline), asrc[i], aparse[i], NULL);
    gst_element_link (asrc[i], aparse[i]);
  }

  /* construct the program map */
  pm = gst_structure_empty_new ("program_map");

  /* Program 1 */
  for (i = 0; i < NR_PROG; i++) {
    /* vparse <-> mux */
    tl_pad = gst_element_get_static_pad (vparse[i], "src");
    if (tl_pad == NULL) {
      g_printerr ("vparse[%d] src pad getting failed.\n", i);
      return -1;
    }
    pad = gst_element_get_compatible_pad (mux, tl_pad, NULL);
    gst_pad_link (tl_pad, pad);
    gst_structure_set (pm, gst_pad_get_name (pad), G_TYPE_INT, i, NULL);
    gst_object_unref (GST_OBJECT (tl_pad));
    gst_object_unref (GST_OBJECT (pad));

    /* aparse <-> mux */
    tl_pad = gst_element_get_static_pad (aparse[i], "src");
    if (tl_pad == NULL) {
      g_printerr ("aparse[%d] src pad getting failed.\n", i);
      return -1;
    }
    pad = gst_element_get_compatible_pad (mux, tl_pad, NULL);
    gst_pad_link (tl_pad, pad);
    gst_structure_set (pm, gst_pad_get_name (pad), G_TYPE_INT, i, NULL);
    gst_object_unref (GST_OBJECT (tl_pad));
    gst_object_unref (GST_OBJECT (pad));
  }

  /* set the program map */
  g_object_set (G_OBJECT (mux), "prog-map", pm, NULL);

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  gst_bus_add_watch (bus, bus_call, loop);
  gst_object_unref (bus);

  /* Write the pipeline to XML */
  sprintf (dest_xml, "/Users/lyang/src/res/mpts.test/mpts_%02d.xml", NR_PROG);
  xml_of = fopen (dest_xml, "w");
  gst_xml_write_file (GST_ELEMENT (pipeline), xml_of);

  g_print ("Now playing: %s\n", dest_dir);
  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  /* Run! */
  g_print ("Running...\n");
  g_main_loop_run (loop);

  /* Out of the main loop, clean up nicely */
  g_print ("Returned, stopping playback\n");
  gst_element_set_state (pipeline, GST_STATE_NULL);

  g_print ("Deleting pipeline\n");
  gst_object_unref (GST_OBJECT (pipeline));

  return 0;

}
