#include <gst/gst.h>
#include <gnome.h>

static gboolean playing;

/* eos will be called when the src element has an end of stream */
void eos(GstSrc *src) 
{
  g_print("have eos, quitting\n");

  playing = FALSE;
}

gboolean idle_func(gpointer data) {
  gst_bin_iterate(GST_BIN(data));
  return TRUE;
}

int main(int argc,char *argv[]) 
{
  GstElement *disksrc, *audiosink, *videosink;
  GstElement *pipeline;
  GtkWidget *appwindow;

  if (argc != 2) {
    g_print("usage: %s <filename>\n", argv[0]);
    exit(-1);
  }

  g_thread_init(NULL);
  gst_init(&argc,&argv);
  gnome_init("autoplug","0.0.1", argc,argv);


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

  /* and an video sink */
  videosink = gst_elementfactory_make("videosink", "play_video");
  g_assert(videosink != NULL);
  gtk_object_set(GTK_OBJECT(videosink),"xv_enabled", FALSE,NULL);

  appwindow = gnome_app_new("autoplug demo","autoplug demo");
  gnome_app_set_contents(GNOME_APP(appwindow),
    gst_util_get_widget_arg(GTK_OBJECT(videosink),"widget"));
  gtk_widget_show_all(appwindow);

  /* add objects to the main pipeline */
  gst_pipeline_add_src(GST_PIPELINE(pipeline), disksrc);
  gst_pipeline_add_sink(GST_PIPELINE(pipeline), audiosink);
  gst_pipeline_add_sink(GST_PIPELINE(pipeline), videosink);

  if (!gst_pipeline_autoplug(GST_PIPELINE(pipeline))) {
    g_print("unable to handle stream\n");
    exit(-1);
  }

  /* make it ready */
  gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_READY);
  /* start playing */
  gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_PLAYING);

  playing = TRUE;

  gtk_idle_add(idle_func, pipeline);

  gst_main();

  /* stop the bin */
  gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_NULL);

  gst_pipeline_destroy(pipeline);

  exit(0);
}

