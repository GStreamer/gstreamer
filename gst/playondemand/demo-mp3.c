#include <stdlib.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <gst/gst.h>

#define NUM_BEATS 12
#define TICK_RATE(x) (x * 1e-6)

GtkWidget *window, *vbox, *beat_box, *button_box;
GtkWidget *play_button, *clear_button, *reset_button, *quit_button;
GtkWidget **beat_button;
GtkWidget *speed_scale;
GtkObject *speed_adj;
GstElement *src, *dec, *pod, *sink, *pipeline;
GstClock *element_clock;
guint32 *beats;

void
played (GstElement * pod, gpointer data)
{
  gint i;

  g_print ("Played beat at %02u, beats are ",
      (guint) (gst_clock_get_time (element_clock) / GST_SECOND *
          (GTK_ADJUSTMENT (speed_adj))->value) % NUM_BEATS);

  for (i = 0; i <= NUM_BEATS / 32; i++)
    g_print ("%08x ", beats[i]);

  g_print ("\n");
}

void
play (GtkButton * button, gpointer data)
{
  g_signal_emit_by_name (G_OBJECT (pod), "play", NULL, NULL);
}

void
clear (GtkButton * button, gpointer data)
{
  g_signal_emit_by_name (G_OBJECT (pod), "clear", NULL, NULL);
}

void
reset (GtkButton * button, gpointer data)
{
  guint i;

  g_signal_emit_by_name (G_OBJECT (pod), "reset", NULL, NULL);
  for (i = 0; i < NUM_BEATS; i++)
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (beat_button[i]), 0);
}

void
beat (GtkToggleButton * button, gpointer data)
{
  guint b = GPOINTER_TO_UINT (data);

  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button)))
    beats[b / 32] |= 1 << (b % 32);
  else
    beats[b / 32] &= ~(1 << (b % 32));
}

void
speed (GtkAdjustment * adjustment, gpointer data)
{
  /*g_signal_stop_emission_by_name(G_OBJECT(pod), "deep-notify"); */
  g_object_set (G_OBJECT (pod), "tick-rate", TICK_RATE (adjustment->value),
      NULL);
  /*gst_clock_set_speed(element_clock, adjustment->value); */
}

void
setup_pipeline (gchar * filename)
{
  src = gst_element_factory_make ("filesrc", "source");
  dec = gst_element_factory_make ("vorbisfile", "decoder");
  pod = gst_element_factory_make ("playondemand", "sequencer");
  sink = gst_element_factory_make ("alsasink", "sink");

  g_object_set (G_OBJECT (src), "location", filename, NULL);
  g_object_set (G_OBJECT (sink), "period-count", 64, "period-size", 512, NULL);
  g_object_set (G_OBJECT (pod), "total-ticks", NUM_BEATS,
      "tick-rate", 1.0e-6, "max-plays", NUM_BEATS * 2, NULL);

  g_object_get (G_OBJECT (pod), "ticks", &beats, NULL);

  pipeline = gst_pipeline_new ("app");

  gst_bin_add_many (GST_BIN (pipeline), src, dec, pod, sink, NULL);
  gst_element_link_many (src, dec, pod, sink, NULL);

  element_clock = gst_element_get_clock (GST_ELEMENT (sink));
  gst_element_set_clock (GST_ELEMENT (pod), element_clock);
}

void
setup_gui (void)
{
  guint i;

  beat_button = g_new (GtkWidget *, NUM_BEATS);

  /* initialize gui elements ... */
  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_container_set_border_width (GTK_CONTAINER (window), 12);

  vbox = gtk_vbox_new (TRUE, 0);
  gtk_box_set_spacing (GTK_BOX (vbox), 12);

  beat_box = gtk_hbox_new (TRUE, 0);
  button_box = gtk_hbox_new (TRUE, 0);

  play_button = gtk_button_new_with_label ("Play");
  clear_button = gtk_button_new_with_label ("Reset Sound");
  reset_button = gtk_button_new_with_label ("Reset All");
  quit_button = gtk_button_new_with_label ("Quit");

  for (i = 0; i < NUM_BEATS; i++)
    beat_button[i] =
        gtk_toggle_button_new_with_label (g_strdup_printf ("%2d", i + 1));

  speed_adj = gtk_adjustment_new (1, 0.0, 10.0, 0.1, 1.0, 0.0);
  speed_scale = gtk_hscale_new (GTK_ADJUSTMENT (speed_adj));
  gtk_scale_set_digits (GTK_SCALE (speed_scale), 4);
  gtk_range_set_update_policy (GTK_RANGE (speed_scale),
      GTK_UPDATE_DISCONTINUOUS);

  /* do the packing stuff ... */
  gtk_window_set_default_size (GTK_WINDOW (window), 96, 96);
  gtk_container_add (GTK_CONTAINER (window), vbox);

  gtk_box_pack_start (GTK_BOX (button_box), play_button, TRUE, TRUE, 2);
  gtk_box_pack_start (GTK_BOX (button_box), clear_button, TRUE, TRUE, 2);
  gtk_box_pack_start (GTK_BOX (button_box), reset_button, TRUE, TRUE, 2);
  gtk_box_pack_start (GTK_BOX (button_box), quit_button, TRUE, TRUE, 2);

  for (i = 0; i < NUM_BEATS; i++)
    gtk_box_pack_start (GTK_BOX (beat_box), beat_button[i], TRUE, TRUE, 2);

  gtk_box_pack_start (GTK_BOX (vbox), button_box, TRUE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (vbox), beat_box, TRUE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (vbox), speed_scale, TRUE, FALSE, 2);

  /* connect things ... */
  g_signal_connect (G_OBJECT (play_button), "clicked", G_CALLBACK (play), NULL);
  g_signal_connect (G_OBJECT (clear_button), "clicked", G_CALLBACK (clear),
      NULL);
  g_signal_connect (G_OBJECT (reset_button), "clicked", G_CALLBACK (reset),
      NULL);
  g_signal_connect (G_OBJECT (quit_button), "clicked", gtk_main_quit, NULL);
  g_signal_connect (G_OBJECT (pod), "played", G_CALLBACK (played), NULL);
  g_signal_connect (G_OBJECT (speed_adj), "value_changed", G_CALLBACK (speed),
      NULL);
  for (i = 0; i < NUM_BEATS; i++)
    g_signal_connect (G_OBJECT (beat_button[i]), "toggled", G_CALLBACK (beat),
        GUINT_TO_POINTER (i));

  /* show the gui. */
  gtk_widget_show_all (window);

  gtk_idle_add ((GtkFunction) gst_bin_iterate, pipeline);
}

int
main (int argc, char **argv)
{
  gst_init (&argc, &argv);
  gtk_init (&argc, &argv);

  if (argc != 2) {
    g_print ("usage: %s <mp3-filename>\n", argv[0]);
    exit (-1);
  }

  setup_pipeline (argv[1]);
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  setup_gui ();
  gtk_main ();
  g_free (beat_button);
  return 0;
}
