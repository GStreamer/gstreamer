#include <gst/gst.h>

static gboolean playing;

/* eos will be called when the src element has an end of stream */
void eos(GstSrc *src) 
{
  g_print("have eos, quitting\n");

  playing = FALSE;
}

int main(int argc,char *argv[]) 
{
  GstElement *disksrc, *audiosink;
  GstElement *pipeline;

  if (argc != 2) {
    g_print("usage: %s <filename>\n", argv[0]);
    exit(-1);
  }

  gst_init(&argc,&argv);

  /* create a new bin to hold the elements */
  pipeline = gst_pipeline_new("pipeline");
  g_assert(pipeline != NULL);

  /* create a disk reader */
  disksrc = gst_elementfactory_make("disksrc", "disk_source");
  g_assert(disksrc != NULL);
  gtk_object_set(GTK_OBJECT(disksrc),"location", argv[1],NULL);
  gtk_signal_connect(GTK_OBJECT(disksrc),"eos",
                     GTK_SIGNAL_FUNC(eos),NULL);

  /* and an audio sink */
  audiosink = gst_elementfactory_make("audiosink", "play_audio");
  g_assert(audiosink != NULL);

  /* add objects to the main pipeline */
  gst_bin_add(GST_BIN(pipeline), disksrc);
  gst_bin_add(GST_BIN(pipeline), audiosink);

  if (!gst_pipeline_autoplug(GST_PIPELINE(pipeline))) {
    g_print("unable to handle stream\n");
    exit(-1);
  }

  /* find out how to handle this bin */
  gst_bin_create_plan(GST_BIN(pipeline));

  /* make it ready */
  gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_READY);
  /* start playing */
  gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_PLAYING);

  playing = TRUE;

  while (playing) {
    gst_bin_iterate(GST_BIN(pipeline));
  }

  /* stop the bin */
  gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_NULL);

  gst_pipeline_destroy(pipeline);

  exit(0);
}

