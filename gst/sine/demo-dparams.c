#include <stdio.h>
#include <string.h>
#include <math.h>
#include <gtk/gtk.h>
#include <gst/gst.h>
#include <gst/control/control.h>

#define ZERO(mem) memset(&mem, 0, sizeof(mem))

static gint
quit_live (GtkWidget * window, GdkEventAny * e, gpointer data)
{
  gtk_main_quit ();
  return FALSE;
}

static void
dynparm_log_value_changed (GtkAdjustment * adj, GstDParam * dparam)
{
  gdouble value;

  g_return_if_fail (dparam != NULL);
  g_return_if_fail (GST_IS_DPARAM (dparam));

  value = exp (adj->value);

  g_print ("setting value to %f\n", value);
  g_object_set (G_OBJECT (dparam), "value_double", value, NULL);
}

static void
dynparm_value_changed (GtkAdjustment * adj, GstDParam * dparam)
{
  g_return_if_fail (dparam != NULL);
  g_return_if_fail (GST_IS_DPARAM (dparam));

  g_print ("setting value to %f\n", adj->value);
  g_object_set (G_OBJECT (dparam), "value_double", (gdouble) adj->value, NULL);

}


int
main (int argc, char *argv[])
{
  GtkWidget *window;
  GtkWidget *hbox;
  GtkAdjustment *volume_adj;
  GtkAdjustment *freq_adj;
  GtkWidget *volume_slider;
  GtkWidget *freq_slider;

  GstElement *thread, *sinesrc, *volfilter, *osssink;
  GstDParamManager *dpman;
  GstDParam *volume;
  GstDParam *freq;
  GParamSpecDouble *spec;

  gtk_init (&argc, &argv);
  gst_init (&argc, &argv);
  gst_control_init (&argc, &argv);

  /***** construct the pipeline *****/

  g_print ("creating elements\n");
  thread = gst_thread_new ("live-example");
  sinesrc = gst_element_factory_make ("sinesrc", "sine-source");
  osssink = gst_element_factory_make ("osssink", "sound-sink");
  volfilter = gst_element_factory_make ("volume", "volume-filter");
  gst_bin_add_many (GST_BIN (thread), sinesrc, volfilter, osssink, NULL);
  gst_element_link_many (sinesrc, volfilter, osssink, NULL);
  /* this breaks with current alsa oss compat lib */
  g_object_set (G_OBJECT (osssink), "fragment", 0x00180008, NULL);
  g_object_set (G_OBJECT (osssink), "sync", FALSE, NULL);

  g_object_set (G_OBJECT (sinesrc), "samplesperbuffer", 1024, NULL);

  /***** set up the GUI *****/
  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_window_set_default_size (GTK_WINDOW (window), 80, 400);
  g_signal_connect (window, "delete_event", GTK_SIGNAL_FUNC (quit_live), NULL);
  hbox = gtk_hbox_new (TRUE, 0);
  gtk_container_add (GTK_CONTAINER (window), hbox);

  /***** set up the dparams *****/

  freq = gst_dpsmooth_new (G_TYPE_DOUBLE);

  g_object_set (G_OBJECT (freq), "update_period", 2000000LL, NULL);

  /* this defines the maximum slope that this *
   * param can change.  This says that in 50ms *
   * the value can change by a maximum of one semitone *
   * (the log of one semitone is 0.693) */
  g_object_set (G_OBJECT (freq), "slope_delta_double", 0.693, NULL);
  g_object_set (G_OBJECT (freq), "slope_time", 50000000LL, NULL);

  dpman = gst_dpman_get_manager (sinesrc);
  if (!gst_dpman_attach_dparam (dpman, "freq", freq))
    g_assert_not_reached ();
  gst_dpman_set_mode (dpman, "asynchronous");

  spec = (GParamSpecDouble *) gst_dpman_get_param_spec (dpman, "freq");
  freq_adj = (GtkAdjustment *) gtk_adjustment_new (log (spec->default_value),
      log (spec->minimum), log (spec->maximum), 0.1, 0.01, 0.01);


  freq_slider = gtk_vscale_new (freq_adj);
  gtk_scale_set_digits (GTK_SCALE (freq_slider), 2);
  gtk_box_pack_start (GTK_BOX (hbox), freq_slider, TRUE, TRUE, 0);

  volume = gst_dpsmooth_new (G_TYPE_DOUBLE);

  g_object_set (G_OBJECT (volume), "update_period", 2000000LL, NULL);

  /* this defines the maximum slope that this *
   * param can change.  This says that in 50ms *
   * the value can change from 0.0 to 1.0 */
  g_object_set (G_OBJECT (volume), "slope_delta_double", 0.1, NULL);
  g_object_set (G_OBJECT (volume), "slope_time", 50000000LL, NULL);

  dpman = gst_dpman_get_manager (volfilter);
  if (!gst_dpman_attach_dparam (dpman, "volume", volume))
    g_assert_not_reached ();
  gst_dpman_set_mode (dpman, "asynchronous");

  g_object_set (G_OBJECT (volfilter), "mute", FALSE, NULL);

  spec = (GParamSpecDouble *) gst_dpman_get_param_spec (dpman, "volume");
  volume_adj =
      (GtkAdjustment *) gtk_adjustment_new (spec->default_value, 0.0, 1.2, 0.1,
      0.01, 0.01);
  volume_slider = gtk_vscale_new (volume_adj);
  gtk_scale_set_digits (GTK_SCALE (volume_slider), 2);
  gtk_box_pack_start (GTK_BOX (hbox), volume_slider, TRUE, TRUE, 0);

  /***** set up the handlers and such *****/
  /*gtk_signal_connect(volume_adj,"value-changed",GTK_SIGNAL_FUNC(volume_changed),sinesrc); */
  g_signal_connect (volume_adj, "value-changed",
      GTK_SIGNAL_FUNC (dynparm_value_changed), volume);

  g_signal_connect (freq_adj, "value-changed",
      GTK_SIGNAL_FUNC (dynparm_log_value_changed), freq);
  gtk_adjustment_value_changed (volume_adj);
  gtk_adjustment_value_changed (freq_adj);

  g_print ("starting pipeline\n");

  /***** start everything up *****/
  gst_element_set_state (thread, GST_STATE_PLAYING);


  gtk_widget_show_all (window);
  gtk_main ();

  return 0;
}
