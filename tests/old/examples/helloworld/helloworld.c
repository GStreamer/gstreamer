#include <stdlib.h>
#include <gst/gst.h>

gboolean playing;

/* eos will be called when the src element has an end of stream */
void eos(GstElement *element) 
{
  g_print("have eos, quitting\n");

  playing = FALSE;
}

int main(int argc,char *argv[]) 
{
  GstElement *bin, *disksrc, *parse, *decoder, *downmix, *mulaw, *mulawdec, *osssink;

  gst_init(&argc,&argv);

  if (argc != 2) {
    g_print("usage: %s <filename>\n", argv[0]);
    exit(-1);
  }

  /* create a new bin to hold the elements */
  bin = gst_pipeline_new("pipeline");

  /* create a disk reader */
  disksrc = gst_elementfactory_make("disksrc", "disk_source");
  gtk_object_set(GTK_OBJECT(disksrc),"location", argv[1],NULL);
  gtk_signal_connect(GTK_OBJECT(disksrc),"eos",
                     GTK_SIGNAL_FUNC(eos),NULL);

  /* now it's time to get the parser */
  parse = gst_elementfactory_make("mp3parse","parse");
  decoder = gst_elementfactory_make("mpg123","decoder");
  downmix = gst_elementfactory_make("stereo2mono","stereo2mono");
  mulaw = gst_elementfactory_make("mulawencode","mulaw");
  mulawdec = gst_elementfactory_make("mulawdecode","mulawdec");
  /* and an audio sink */
  osssink = gst_elementfactory_make("osssink", "play_audio");

  /* add objects to the main pipeline */
  gst_bin_add(GST_BIN(bin), disksrc);
  gst_bin_add(GST_BIN(bin), parse);
  gst_bin_add(GST_BIN(bin), decoder);
  gst_bin_add(GST_BIN(bin), downmix);
  gst_bin_add(GST_BIN(bin), mulaw);
  gst_bin_add(GST_BIN(bin), mulawdec);
  gst_bin_add(GST_BIN(bin), osssink);

  /* connect src to sink */
  gst_pad_connect(gst_element_get_pad(disksrc,"src"),
                  gst_element_get_pad(parse,"sink"));
  gst_pad_connect(gst_element_get_pad(parse,"src"),
                  gst_element_get_pad(decoder,"sink"));
  gst_pad_connect(gst_element_get_pad(decoder,"src"),
                  gst_element_get_pad(downmix,"sink"));
  gst_pad_connect(gst_element_get_pad(downmix,"src"),
                  gst_element_get_pad(mulaw,"sink"));
  gst_pad_connect(gst_element_get_pad(mulaw,"src"),
                  gst_element_get_pad(mulawdec,"sink"));
  gst_pad_connect(gst_element_get_pad(mulawdec,"src"),
                  gst_element_get_pad(osssink,"sink"));

  /* start playing */
  gst_element_set_state(bin, GST_STATE_PLAYING);

  playing = TRUE;

  while (playing) {
    gst_bin_iterate(GST_BIN(bin));
  }

  /* stop the bin */
  gst_element_set_state(bin, GST_STATE_NULL);

  gst_object_destroy(GST_OBJECT(osssink));
  gst_object_destroy(GST_OBJECT(parse));
  gst_object_destroy(GST_OBJECT(decoder));
  gst_object_destroy(GST_OBJECT(downmix));
  gst_object_destroy(GST_OBJECT(mulaw));
  gst_object_destroy(GST_OBJECT(mulawdec));
  gst_object_destroy(GST_OBJECT(disksrc));
  gst_object_destroy(GST_OBJECT(bin));

  exit(0);
}

