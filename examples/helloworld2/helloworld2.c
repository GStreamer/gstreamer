#include <stdlib.h>
#include <gst/gst.h>

/* eos will be called when the src element has an end of stream */
void eos(GstElement *element)  
{
  g_print("have eos, quitting\n");

  gst_main_quit();
}

int main(int argc,char *argv[]) 
{
  GstElement *disksrc, *audiosink;
  GstElement *pipeline, *thread;

  if (argc != 2) {
    g_print("usage: %s <filename>\n", argv[0]);
    exit(-1);
  }

  gst_init(&argc,&argv);

  thread = gst_thread_new("main_thread");
  g_assert(thread != NULL);

  /* create a new bin to hold the elements */
  pipeline = gst_pipeline_new("pipeline");
  g_assert(pipeline != NULL);

  gst_bin_add(GST_BIN(thread), pipeline);

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
  gst_pipeline_add_src(GST_PIPELINE(pipeline), disksrc);
  gst_pipeline_add_sink(GST_PIPELINE(pipeline), audiosink);

  if (!gst_pipeline_autoplug(GST_PIPELINE(pipeline))) {
    g_print("unable to handle stream\n");
    exit(-1);
  }

  /* make it ready */
  gst_element_set_state(GST_ELEMENT(thread), GST_STATE_READY);
  /* start playing */
  gst_element_set_state(GST_ELEMENT(thread), GST_STATE_PLAYING);

  gst_main();

  /* stop the bin */
  gst_element_set_state(GST_ELEMENT(thread), GST_STATE_NULL);

  gst_pipeline_destroy(thread);

  exit(0);
}

