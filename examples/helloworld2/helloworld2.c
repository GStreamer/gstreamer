#include <gst/gst.h>

static void
gst_play_have_type (GstElement * typefind, GstCaps * caps,
    GstElement * pipeline)
{
  GstElement *osssink;
  GstElement *new_element;
  GstAutoplug *autoplug;
  GstElement *autobin;
  GstElement *filesrc;
  GstElement *cache;

  GST_DEBUG ("GstPipeline: play have type");

  gst_element_set_state (pipeline, GST_STATE_PAUSED);

  filesrc = gst_bin_get_by_name (GST_BIN (pipeline), "disk_source");
  autobin = gst_bin_get_by_name (GST_BIN (pipeline), "autobin");
  cache = gst_bin_get_by_name (GST_BIN (autobin), "cache");

  /* unlink_pads the typefind from the pipeline and remove it */
  gst_element_unlink_pads (cache, "src", typefind, "sink");
  gst_bin_remove (GST_BIN (autobin), typefind);

  /* and an audio sink */
  osssink = gst_element_factory_make ("osssink", "play_audio");
  g_assert (osssink != NULL);

  autoplug = gst_autoplug_factory_make ("staticrender");
  g_assert (autoplug != NULL);

  new_element = gst_autoplug_to_renderers (autoplug, caps, osssink, NULL);

  if (!new_element) {
    g_print ("could not autoplug, no suitable codecs found...\n");
    exit (-1);
  }

  gst_element_set_name (new_element, "new_element");

  gst_bin_add (GST_BIN (autobin), new_element);

  g_object_set (G_OBJECT (cache), "reset", TRUE, NULL);

  gst_element_link_pads (cache, "src", new_element, "sink");

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
}

static void
gst_play_cache_empty (GstElement * element, GstElement * pipeline)
{
  GstElement *autobin;
  GstElement *filesrc;
  GstElement *cache;
  GstElement *new_element;

  fprintf (stderr, "have cache empty\n");

  gst_element_set_state (pipeline, GST_STATE_PAUSED);

  filesrc = gst_bin_get_by_name (GST_BIN (pipeline), "disk_source");
  autobin = gst_bin_get_by_name (GST_BIN (pipeline), "autobin");
  cache = gst_bin_get_by_name (GST_BIN (autobin), "cache");
  new_element = gst_bin_get_by_name (GST_BIN (autobin), "new_element");

  gst_element_unlink_pads (filesrc, "src", cache, "sink");
  gst_element_unlink_pads (cache, "src", new_element, "sink");
  gst_bin_remove (GST_BIN (autobin), cache);
  gst_element_link_pads (filesrc, "src", new_element, "sink");

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  fprintf (stderr, "done with cache_empty\n");
}

int
main (int argc, char *argv[])
{
  GstElement *filesrc;
  GstElement *pipeline;
  GstElement *autobin;
  GstElement *typefind;
  GstElement *cache;

  gst_init (&argc, &argv);

  if (argc != 2) {
    g_print ("usage: %s <filename>\n", argv[0]);
    exit (-1);
  }

  /* create a new pipeline to hold the elements */
  pipeline = gst_pipeline_new ("pipeline");
  g_assert (pipeline != NULL);

  /* create a disk reader */
  filesrc = gst_element_factory_make ("filesrc", "disk_source");
  g_assert (filesrc != NULL);
  g_object_set (G_OBJECT (filesrc), "location", argv[1], NULL);
  gst_bin_add (GST_BIN (pipeline), filesrc);

  autobin = gst_bin_new ("autobin");
  cache = gst_element_factory_make ("autoplugcache", "cache");
  g_signal_connect (G_OBJECT (cache), "cache_empty",
      G_CALLBACK (gst_play_cache_empty), pipeline);

  typefind = gst_element_factory_make ("typefind", "typefind");
  g_signal_connect (G_OBJECT (typefind), "have_type",
      G_CALLBACK (gst_play_have_type), pipeline);
  gst_bin_add (GST_BIN (autobin), cache);
  gst_bin_add (GST_BIN (autobin), typefind);

  gst_element_link_pads (cache, "src", typefind, "sink");
  gst_element_add_ghost_pad (autobin, gst_element_get_pad (cache, "sink"),
      "sink");

  gst_bin_add (GST_BIN (pipeline), autobin);
  gst_element_link_pads (filesrc, "src", autobin, "sink");

  /* start playing */
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);

  while (gst_bin_iterate (GST_BIN (pipeline)));

  /* stop the pipeline */
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);

  gst_object_unref (GST_OBJECT (pipeline));

  exit (0);
}
