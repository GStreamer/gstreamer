#include <gtk/gtk.h>
#include <gst/gst.h>

void play (GtkButton *button, gpointer data)
{
  gtk_signal_emit_by_name(GTK_OBJECT(data), "play");
}
  
void reset (GtkButton *button, gpointer data)
{
  gtk_signal_emit_by_name(GTK_OBJECT(data), "reset");
}
  
int main(int argc, char **argv) 
{
  guint channels;
  GtkWidget *window, *vbox, *play_button, *reset_button, *quit_button;
  GstElement *filesrc, *mad, *stereo2mono, *pod, *osssink, *pipeline;
    
  gst_init (&argc, &argv);
  gtk_init (&argc, &argv);
    
  if (argc!=2) {
    g_print("usage: %s <mp3-filename>\n", argv[0]);
    exit(-1);
  }
    
  filesrc = gst_elementfactory_make("filesrc", "filesrc");
  mad = gst_elementfactory_make("mad", "mad");
  pod = gst_elementfactory_make("playondemand", "playondemand");
  osssink = gst_elementfactory_make("osssink", "osssink");

  gtk_object_set(GTK_OBJECT(filesrc), "location", argv[1], NULL);
  gtk_object_set(GTK_OBJECT(osssink), "fragment", 0x00180008, NULL);
  gtk_object_get(GTK_OBJECT(osssink), "channels", &channels, NULL);

  pipeline = gst_pipeline_new("app");

  gst_bin_add(GST_BIN(pipeline), filesrc);
  gst_bin_add(GST_BIN(pipeline), mad);
  gst_bin_add(GST_BIN(pipeline), pod);
  gst_bin_add(GST_BIN(pipeline), osssink);

  gst_element_connect(filesrc, "src", mad, "sink");
  gst_element_connect(pod, "src", osssink, "sink");

  if (channels != 2) {
    gst_element_connect(mad, "src", pod, "sink");
  } else {
    stereo2mono = gst_elementfactory_make("stereo2mono", "stereo2mono");
    gst_bin_add(GST_BIN(pipeline), stereo2mono);
    gst_element_connect(mad, "src", stereo2mono, "sink");
    gst_element_connect(stereo2mono, "src", pod, "sink");
  }

  gst_element_set_state(pipeline, GST_STATE_PLAYING);

  /* initialize gui elements ... */
  window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  vbox = gtk_vbox_new(FALSE, 0);
  play_button = gtk_button_new_with_label("play");
  reset_button = gtk_button_new_with_label("reset");
  quit_button = gtk_button_new_with_label("quit");

  /* do the packing stuff ... */
  gtk_window_set_default_size(GTK_WINDOW(window), 96, 96);
  gtk_container_add(GTK_CONTAINER(window), vbox);
  gtk_box_pack_start(GTK_BOX(vbox), play_button, FALSE, FALSE, 2);
  gtk_box_pack_start(GTK_BOX(vbox), reset_button, FALSE, FALSE, 2);
  gtk_box_pack_start(GTK_BOX(vbox), quit_button, FALSE, FALSE, 2);

  /* connect things ... */
  gtk_signal_connect(GTK_OBJECT(play_button), "clicked", play, pod);
  gtk_signal_connect(GTK_OBJECT(reset_button), "clicked", reset, pod);
  gtk_signal_connect(GTK_OBJECT(quit_button), "clicked", gtk_main_quit, NULL);

  /* show the gui. */
  gtk_widget_show(play_button);
  gtk_widget_show(reset_button);
  gtk_widget_show(quit_button);
  gtk_widget_show(vbox);
  gtk_widget_show(window);
  gtk_idle_add((GtkFunction)gst_bin_iterate, pipeline);
    
  gtk_main();
}
