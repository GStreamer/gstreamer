#include <stdlib.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <gst/gst.h>

#include "gstplayondemand.h"

guint channels;
GtkWidget *window, *vbox, *play_button, *reset_button, *quit_button;
GtkWidget *hbox, *measure1_button, *measure2_button, *measure3_button, \
  *measure4_button, *measure5_button, *measure6_button, *speed_scale;
GstElement *src, *mad, *pod, *osssink, *pipeline;
GstClock *element_clock;

void
play (GtkButton *button, gpointer data)
{
  g_signal_emit_by_name(G_OBJECT(pod), "play", NULL, NULL);
}

void
reset (GtkButton *button, gpointer data)
{
  g_signal_emit_by_name(G_OBJECT(pod), "reset", NULL, NULL);
}

void
measure (GtkToggleButton *button, gpointer data)
{
  gst_play_on_demand_toggle_beat(GST_PLAYONDEMAND(pod),
                                 GPOINTER_TO_UINT(data), 0);
}

void
speed (GtkAdjustment *scale, gpointer data)
{
  gst_clock_set_speed(element_clock, gtk_adjustment_get_value(scale));
}

void
setup_pipeline (gchar *filename)
{
  src = gst_element_factory_make("filesrc", "filesrc");
  mad = gst_element_factory_make("mad", "mad");
  pod = gst_element_factory_make("playondemand", "playondemand");
  osssink = gst_element_factory_make("osssink", "osssink");

  g_object_set(G_OBJECT(src), "location", filename, NULL);
  g_object_set(G_OBJECT(pod), "silent", FALSE, NULL);
  g_object_set(G_OBJECT(osssink), "fragment", 0x00180008, NULL);
  g_object_get(G_OBJECT(osssink), "channels", &channels, NULL);

  pipeline = gst_pipeline_new("app");

  gst_bin_add_many(GST_BIN(pipeline), src, mad, pod, osssink, NULL);
  gst_element_link_many(src, mad, pod, osssink, NULL);

  element_clock = gst_bin_get_clock(GST_BIN(pipeline));
  gst_element_set_clock(GST_ELEMENT(pod), element_clock);
  /* gst_clock_set_speed(element_clock, 0.00001); */
}

void
setup_gui (void)
{
  /* initialize gui elements ... */
  window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  vbox = gtk_vbox_new(TRUE, 0);
  hbox = gtk_hbox_new(TRUE, 0);
  play_button = gtk_button_new_with_label("play");
  reset_button = gtk_button_new_with_label("reset");
  quit_button = gtk_button_new_with_label("quit");
  measure1_button = gtk_toggle_button_new_with_label("one");
  measure2_button = gtk_toggle_button_new_with_label("two");
  measure3_button = gtk_toggle_button_new_with_label("three");
  measure4_button = gtk_toggle_button_new_with_label("four");
  measure5_button = gtk_toggle_button_new_with_label("five");
  measure6_button = gtk_toggle_button_new_with_label("six");
  speed_scale = gtk_hscale_new_with_range(0.0, 0.001, 0.000001);
  /*  gtk_adjustment_set_value(GTK_ADJUSTMENT(speed_scale), 0.00001); */

  /* do the packing stuff ... */
  gtk_window_set_default_size(GTK_WINDOW(window), 96, 96);
  gtk_container_add(GTK_CONTAINER(window), vbox);
  gtk_box_pack_start(GTK_BOX(vbox), play_button, TRUE, FALSE, 2);
  gtk_box_pack_start(GTK_BOX(vbox), reset_button, TRUE, FALSE, 2);
  gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, FALSE, 2);
  gtk_box_pack_start(GTK_BOX(hbox), measure1_button, TRUE, TRUE, 2);
  gtk_box_pack_start(GTK_BOX(hbox), measure2_button, TRUE, TRUE, 2);
  gtk_box_pack_start(GTK_BOX(hbox), measure3_button, TRUE, TRUE, 2);
  gtk_box_pack_start(GTK_BOX(hbox), measure4_button, TRUE, TRUE, 2);
  gtk_box_pack_start(GTK_BOX(hbox), measure5_button, TRUE, TRUE, 2);
  gtk_box_pack_start(GTK_BOX(hbox), measure6_button, TRUE, TRUE, 2);
  /*gtk_box_pack_start(GTK_BOX(vbox), speed_scale, TRUE, FALSE, 2);*/
  gtk_box_pack_start(GTK_BOX(vbox), quit_button, TRUE, FALSE, 2);

  /* connect things ... */
  g_signal_connect(G_OBJECT(play_button), "clicked", G_CALLBACK(play), NULL);
  g_signal_connect(G_OBJECT(reset_button), "clicked", G_CALLBACK(reset), NULL);
  g_signal_connect(G_OBJECT(quit_button), "clicked", gtk_main_quit, NULL);
  g_signal_connect(G_OBJECT(measure1_button), "toggled", G_CALLBACK(measure), GUINT_TO_POINTER(0));
  g_signal_connect(G_OBJECT(measure2_button), "toggled", G_CALLBACK(measure), GUINT_TO_POINTER(1));
  g_signal_connect(G_OBJECT(measure3_button), "toggled", G_CALLBACK(measure), GUINT_TO_POINTER(2));
  g_signal_connect(G_OBJECT(measure4_button), "toggled", G_CALLBACK(measure), GUINT_TO_POINTER(3));
  g_signal_connect(G_OBJECT(measure5_button), "toggled", G_CALLBACK(measure), GUINT_TO_POINTER(4));
  g_signal_connect(G_OBJECT(measure6_button), "toggled", G_CALLBACK(measure), GUINT_TO_POINTER(5));
  /*g_signal_connect(G_OBJECT(speed_scale), "value-changed", G_CALLBACK(speed), NULL);*/

  /* show the gui. */
  gtk_widget_show(play_button);
  gtk_widget_show(reset_button);
  gtk_widget_show(quit_button);
  gtk_widget_show(measure1_button);
  gtk_widget_show(measure2_button);
  gtk_widget_show(measure3_button);
  gtk_widget_show(measure4_button);
  gtk_widget_show(measure5_button);
  gtk_widget_show(measure6_button);
  gtk_widget_show(hbox);
  /*gtk_widget_show(speed_scale);*/
  gtk_widget_show(vbox);
  gtk_widget_show(window);
  gtk_idle_add((GtkFunction)gst_bin_iterate, pipeline);
}

int
main(int argc, char **argv)
{
  gst_init (&argc, &argv);
  gtk_init (&argc, &argv);

  if (argc!=2) {
    g_print("usage: %s <mp3-filename>\n", argv[0]);
    exit(-1);
  }

  setup_pipeline(argv[1]);
  gst_element_set_state(pipeline, GST_STATE_PLAYING);
  setup_gui();
  gtk_main();
  return 0;
}
