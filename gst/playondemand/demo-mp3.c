#include <glib.h>
#include <gtk/gtk.h>
#include <gst/gst.h>

void play (GtkButton *button, gpointer data)
{
  g_signal_emit_by_name(G_OBJECT(data), "play", NULL, NULL);
}
  
void reset (GtkButton *button, gpointer data)
{
  g_signal_emit_by_name(G_OBJECT(data), "reset", NULL, NULL);
}
  
int main(int argc, char **argv) 
{
  guint channels;
  GtkWidget *window, *vbox, *play_button, *reset_button, *quit_button;
  GstElement *src, *mad, *pod, *osssink, *pipeline;
    
  gst_init (&argc, &argv);
  gtk_init (&argc, &argv);
    
  if (argc!=2) {
    g_print("usage: %s <mp3-filename>\n", argv[0]);
    exit(-1);
  }
    
  src = gst_elementfactory_make("filesrc", "filesrc");
  mad = gst_elementfactory_make("mad", "mad");
  pod = gst_elementfactory_make("playondemand", "playondemand");
  osssink = gst_elementfactory_make("osssink", "osssink");

  g_object_set(G_OBJECT(src), "location", argv[1], NULL);
  g_object_set(G_OBJECT(osssink), "fragment", 0x00180008, NULL);
  g_object_get(G_OBJECT(osssink), "channels", &channels, NULL);

  pipeline = gst_pipeline_new("app");

  gst_bin_add(GST_BIN(pipeline), src);
  gst_bin_add(GST_BIN(pipeline), mad);
  gst_bin_add(GST_BIN(pipeline), pod);
  gst_bin_add(GST_BIN(pipeline), osssink);

  gst_element_connect(src, mad);
  gst_element_connect(pod, osssink);
  gst_element_connect(mad, pod);

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
  g_signal_connect(G_OBJECT(play_button), "clicked", G_CALLBACK(play), pod);
  g_signal_connect(G_OBJECT(reset_button), "clicked", G_CALLBACK(reset), pod);
  g_signal_connect(G_OBJECT(quit_button), "clicked", gtk_main_quit, NULL);

  /* show the gui. */
  gtk_widget_show(play_button);
  gtk_widget_show(reset_button);
  gtk_widget_show(quit_button);
  gtk_widget_show(vbox);
  gtk_widget_show(window);
  gtk_idle_add((GtkFunction)gst_bin_iterate, pipeline);
    
  gtk_main();

  return 0;
}
