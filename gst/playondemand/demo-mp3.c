#include <stdlib.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <gst/gst.h>

#define NUM_BEATS 16
#define SPEED 1e-9

GtkWidget *window, *vbox, *beat_box, *button_box;
GtkWidget *play_button, *clear_button, *reset_button, *quit_button;
GtkWidget **beat_button;
GtkWidget *speed_scale;
GtkObject *speed_adj;
GstElement *src, *mad, *pod, *osssink, *pipeline;
GstClock *element_clock;
GSList *beats;

void
played (GstElement *pod, gpointer data)
{
  g_print("Played beat at %u\n",
          ((guint) (gst_clock_get_time(element_clock) *
                    (GTK_ADJUSTMENT(speed_adj))->value * SPEED)) % NUM_BEATS);
}

void
play (GtkButton *button, gpointer data)
{
  g_signal_emit_by_name(G_OBJECT(pod), "play", NULL, NULL);
}

void
clear (GtkButton *button, gpointer data)
{
  g_signal_emit_by_name(G_OBJECT(pod), "clear", NULL, NULL);
}

void
reset (GtkButton *button, gpointer data)
{
  guint i;
  g_signal_emit_by_name(G_OBJECT(pod), "reset", NULL, NULL);
  for (i = 0; i < NUM_BEATS; i++)
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(beat_button[i]), 0);
}

void
beat (GtkToggleButton *button, gpointer data)
{
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button)))
    beats = g_slist_append(beats, data);
  else
    beats = g_slist_remove(beats, data);
  g_object_set(G_OBJECT(pod), "tick-list", beats, NULL);
}

void
speed (GtkAdjustment *adjustment, gpointer data)
{
  g_object_set(G_OBJECT(pod), "clock-speed", adjustment->value * SPEED, NULL);
  /*gst_clock_set_speed(element_clock, adjustment->value * SPEED);*/
}

void
setup_pipeline (gchar *filename)
{
  src = gst_element_factory_make("filesrc", "filesrc");
  mad = gst_element_factory_make("mad", "mad");
  pod = gst_element_factory_make("playondemand", "playondemand");
  osssink = gst_element_factory_make("osssink", "osssink");

  g_object_set(G_OBJECT(src), "location", filename, NULL);
  g_object_set(G_OBJECT(osssink), "fragment", 0x00180008, NULL);
  g_object_set(G_OBJECT(pod), "total-ticks", NUM_BEATS,
                              "clock-speed", SPEED, NULL);

  pipeline = gst_pipeline_new("app");

  gst_bin_add_many(GST_BIN(pipeline), src, mad, pod, osssink, NULL);
  gst_element_link_many(src, mad, pod, osssink, NULL);

  element_clock = gst_bin_get_clock(GST_BIN(pipeline));
  gst_element_set_clock(GST_ELEMENT(pod), element_clock);
}

void
setup_gui (void)
{
  guint i;

  beat_button = g_new(GtkWidget *, NUM_BEATS);

  /* initialize gui elements ... */
  window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_container_set_border_width(GTK_CONTAINER(window), 12);

  vbox = gtk_vbox_new(TRUE, 0);
  gtk_box_set_spacing(GTK_BOX(vbox), 12);

  beat_box = gtk_hbox_new(TRUE, 0);
  button_box = gtk_hbox_new(TRUE, 0);

  play_button = gtk_button_new_with_label("Play");
  clear_button = gtk_button_new_with_label("Reset Sound");
  reset_button = gtk_button_new_with_label("Reset All");
  quit_button = gtk_button_new_with_label("Quit");

  for (i = 0; i < NUM_BEATS; i++)
    beat_button[i] = gtk_toggle_button_new_with_label(g_strdup_printf("%2d", i));

  speed_adj = gtk_adjustment_new(1, 0.0, 2, 0.01, 0.1, 0.0);
  speed_scale = gtk_hscale_new(GTK_ADJUSTMENT(speed_adj));
  gtk_scale_set_digits(GTK_SCALE(speed_scale), 4);
  gtk_range_set_update_policy(GTK_RANGE(speed_scale), GTK_UPDATE_DISCONTINUOUS);

  /* do the packing stuff ... */
  gtk_window_set_default_size(GTK_WINDOW(window), 96, 96);
  gtk_container_add(GTK_CONTAINER(window), vbox);

  gtk_box_pack_start(GTK_BOX(button_box), play_button, TRUE, TRUE, 2);
  gtk_box_pack_start(GTK_BOX(button_box), clear_button, TRUE, TRUE, 2);
  gtk_box_pack_start(GTK_BOX(button_box), reset_button, TRUE, TRUE, 2);
  gtk_box_pack_start(GTK_BOX(button_box), quit_button, TRUE, TRUE, 2);

  for (i = 0; i < NUM_BEATS; i++)
    gtk_box_pack_start(GTK_BOX(beat_box), beat_button[i], TRUE, TRUE, 2);

  gtk_box_pack_start(GTK_BOX(vbox), button_box, TRUE, FALSE, 2);
  gtk_box_pack_start(GTK_BOX(vbox), beat_box, TRUE, FALSE, 2);
  gtk_box_pack_start(GTK_BOX(vbox), speed_scale, TRUE, FALSE, 2);

  /* connect things ... */
  g_signal_connect(G_OBJECT(play_button), "clicked", G_CALLBACK(play), NULL);
  g_signal_connect(G_OBJECT(clear_button), "clicked", G_CALLBACK(clear), NULL);
  g_signal_connect(G_OBJECT(reset_button), "clicked", G_CALLBACK(reset), NULL);
  g_signal_connect(G_OBJECT(quit_button), "clicked", gtk_main_quit, NULL);
  g_signal_connect(G_OBJECT(pod), "played", G_CALLBACK(played), NULL);
  g_signal_connect(G_OBJECT(speed_adj), "value_changed", G_CALLBACK(speed), NULL);
  for (i = 0; i < NUM_BEATS; i++)
    g_signal_connect(G_OBJECT(beat_button[i]), "toggled", G_CALLBACK(beat), GUINT_TO_POINTER(i));

  /* show the gui. */
  gtk_widget_show(play_button);
  gtk_widget_show(clear_button);
  gtk_widget_show(reset_button);
  gtk_widget_show(quit_button);

  for (i = 0; i < NUM_BEATS; i++)
    gtk_widget_show(beat_button[i]);

  gtk_widget_show(beat_box);
  gtk_widget_show(button_box);
  gtk_widget_show(speed_scale);
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
  g_free(beat_button);
  return 0;
}
