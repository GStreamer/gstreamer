#include <stdlib.h>
#include <gst/gst.h>

static void
error_cb (GstElement * bin, GstElement * error_element, GError * error,
    const gchar * debug_msg, gpointer user_data)
{
  gboolean *p_got_error = (gboolean *) user_data;

  g_printerr ("An error occured: %s\n", error->message);

  *p_got_error = TRUE;
}

int
main (int argc, char *argv[])
{
  GstElement *bin, *filesrc, *decoder, *audioconvert, *audioscale, *osssink;
  gboolean got_error;

  gst_init (&argc, &argv);

  if (argc != 2) {
    g_print ("usage: %s <mp3 file>\n", argv[0]);
    exit (-1);
  }

  /* create a new bin to hold the elements */
  bin = gst_pipeline_new ("pipeline");
  g_assert (bin);

  /* create a disk reader */
  filesrc = gst_element_factory_make ("filesrc", "disk_source");
  g_assert (filesrc);
  g_object_set (G_OBJECT (filesrc), "location", argv[1], NULL);

  /* now it's time to get the decoder */
  decoder = gst_element_factory_make ("mad", "decode");
  if (!decoder) {
    g_print ("could not find plugin \"mad\"");
    return -1;
  }

  /* create standard converters to make sure the decoded
   * samples are converted into a format our audio sink
   * understands (if necessary) */
  audioconvert = gst_element_factory_make ("audioconvert", "audioconvert");
  audioscale = gst_element_factory_make ("audioscale", "audioscale");
  g_assert (audioconvert && audioscale);

  /* and an audio sink */
  osssink = gst_element_factory_make ("osssink", "play_audio");
  g_assert (osssink);

  /* add objects to the main pipeline */
  gst_bin_add_many (GST_BIN (bin), filesrc, decoder, audioconvert, audioscale,
      osssink, NULL);

  /* link the elements */
  if (!gst_element_link_many (filesrc, decoder, audioconvert, audioscale,
          osssink, NULL))
    g_error ("gst_element_link_many() failed!");

  /* check for errors */
  got_error = FALSE;
  g_signal_connect (bin, "error", G_CALLBACK (error_cb), &got_error);

  /* start playing */
  gst_element_set_state (bin, GST_STATE_PLAYING);

  while (!got_error && gst_bin_iterate (GST_BIN (bin))) {
    ;
  }

  /* stop the bin */
  gst_element_set_state (bin, GST_STATE_NULL);

  /* free */
  g_object_unref (bin);

  exit (0);
}
