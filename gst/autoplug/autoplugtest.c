#include <gst/gst.h>
#include <string.h>

GstElement *pipeline, *src, *autobin, *cache, *typefind, *decoder, *sink;

/* callback for when we have the type of the file 
 * we set up a playing pipeline based on the mime type
 */
void 
have_type (GstElement *element, GstCaps *caps, GstCaps **private_caps) 
{
  gchar *mime = g_strdup_printf (gst_caps_get_mime (caps));
  fprintf (stderr, "have caps, mime type is %s\n", mime);

  gst_element_set_state (pipeline, GST_STATE_PAUSED);

  /* unlink the typefind from the pipeline and remove it,
   * since we now know what the type is */
  gst_element_unlink (cache, typefind);
  gst_bin_remove (GST_BIN (autobin), typefind);

  gst_scheduler_show (GST_ELEMENT_SCHED (pipeline));

  /* now based on the mime type set up the pipeline properly */
  if (strstr (mime, "mp3")) {
    decoder = gst_element_factory_make ("mad", "decoder");
  }
  else if (strstr (mime, "x-ogg")) {
    decoder = gst_element_factory_make ("vorbisfile", "decoder");
  }
  else if (strstr (mime, "x-wav")) {
    decoder = gst_element_factory_make ("wavparse", "decoder");
  }
  else if (strstr (mime, "x-flac")) {
    decoder = gst_element_factory_make ("flacdec", "decoder");
  }
  else {
    g_print ("mime type %s not handled in this program, exiting.\n", mime);
    g_free (mime);
    exit (-1);
  }
  g_free (mime);

  /* handle playback */
  sink = gst_element_factory_make ("osssink", "sink");
  gst_bin_add (GST_BIN (autobin), decoder);
  gst_bin_add (GST_BIN (autobin), sink);
  gst_element_link (decoder, sink);

  g_object_set (G_OBJECT (cache), "reset", TRUE, NULL);

  gst_element_link (cache, decoder);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  fprintf (stderr, "done with have_type signal, playing\n");
}

void cache_empty (GstElement *element, gpointer private) {
  fprintf(stderr,"have cache empty\n");

  gst_element_set_state (pipeline, GST_STATE_PAUSED);

  gst_element_unlink_pads (src,"src",cache,"sink");
  gst_scheduler_show (GST_ELEMENT_SCHED(pipeline));
  gst_element_unlink_pads (cache,"src",decoder,"sink");
  gst_scheduler_show (GST_ELEMENT_SCHED(pipeline));
  gst_bin_remove (GST_BIN(autobin), cache);
  gst_scheduler_show (GST_ELEMENT_SCHED(pipeline));
  gst_element_link_pads (src,"src",decoder,"sink");
  gst_scheduler_show (GST_ELEMENT_SCHED(pipeline));

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  gst_scheduler_show (GST_ELEMENT_SCHED(pipeline));

  fprintf(stderr,"done with cache_empty\n");
}

int main (int argc,char *argv[]) {
  GstCaps *caps;

  gst_init (&argc, &argv);

  pipeline = gst_pipeline_new ("pipeline");
  src = gst_element_factory_make ("filesrc","src");
  if (argc < 2) {
    g_print ("Please give a file to test the autoplugger on.\n");
    return -1;
  }
  g_object_set (G_OBJECT (src), "location", argv[1], NULL);
  gst_bin_add (GST_BIN (pipeline), src);

  /* the autobin will be used to do the autoplugging in */
  autobin = gst_bin_new ("autobin");

  /* a cache is used to make autoplugging quicker */
  cache = gst_element_factory_make ("autoplugcache", "cache");
  g_signal_connect (G_OBJECT (cache), "cache_empty",
		    G_CALLBACK (cache_empty), NULL);
  /* typefind does the type detection */
  typefind = gst_element_factory_make ("typefind", "typefind");

  /* execute the callback when we find the type */
  g_signal_connect (G_OBJECT (typefind), "have_type",
		    G_CALLBACK (have_type), &caps);
  gst_bin_add (GST_BIN (autobin), cache);
  gst_bin_add (GST_BIN (autobin), typefind);
  gst_element_link (cache, typefind);
  gst_element_add_ghost_pad (autobin, 
		             gst_element_get_pad (cache, "sink") ,"sink");

  gst_bin_add (GST_BIN (pipeline), autobin);
  gst_element_link (src, autobin);

  /* pipeline is now src ! autobin 
   * with autobin: cache ! typefind
   */
  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  while (gst_bin_iterate (GST_BIN (pipeline)));

  return 0;
}
