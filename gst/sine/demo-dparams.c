#include <stdio.h>
#include <math.h>
#include <gtk/gtk.h>
#include <gst/gst.h>
#include <gst/control/control.h>

static gint quit_live(GtkWidget *window, GdkEventAny *e, gpointer data) {
  gtk_main_quit();
  return FALSE;
}

static void dynparm_log_value_changed(GtkAdjustment *adj,GstDParam *dparam) {
  GValue **point;
  g_return_if_fail(dparam != NULL);
  g_return_if_fail(GST_IS_DPARAM (dparam));

  point = GST_DPARAM_GET_POINT(dparam, 0LL);
  
  GST_DPARAM_LOCK(dparam);
  g_print("setting value from %f to %f\n", g_value_get_float(point[0]), (gfloat)exp(adj->value));  
  g_value_set_float(point[0], (gfloat)exp(adj->value));
  GST_DPARAM_READY_FOR_UPDATE(dparam) = TRUE;
  GST_DPARAM_UNLOCK(dparam);
}

static void dynparm_value_changed(GtkAdjustment *adj,GstDParam *dparam) {
  GValue **point;
  g_return_if_fail(dparam != NULL);
  g_return_if_fail(GST_IS_DPARAM (dparam));

  point = GST_DPARAM_GET_POINT(dparam, 0LL);
  
  GST_DPARAM_LOCK(dparam);
  g_print("setting value from %f to %f\n", g_value_get_float(point[0]), adj->value);  
  g_value_set_float(point[0], adj->value);
  GST_DPARAM_READY_FOR_UPDATE(dparam) = TRUE;
  GST_DPARAM_UNLOCK(dparam);
}


int main(int argc,char *argv[]) {
  GtkWidget *window;
  GtkWidget *hbox;
  GtkAdjustment *volume_adj;
  GtkAdjustment *freq_adj;
  GtkWidget *volume_slider;
  GtkWidget *freq_slider;

  GstElement *thread, *sinesrc, *osssink;
  GstDParamManager *dpman;
  GstDParam *volume;
  GstDParam *freq;
  GstDParamSpec *spec;
  
  GValue **vals;

  gtk_init(&argc,&argv);
  gst_init(&argc,&argv);
  gst_control_init(&argc,&argv);

  /***** construct the pipeline *****/
  
  g_print("creating elements\n");
  thread = gst_thread_new("live-example");
  sinesrc = gst_elementfactory_make("sinesrc","sine-source");
  osssink = gst_elementfactory_make("osssink","sound-sink");
  gst_bin_add(GST_BIN(thread),sinesrc);
  gst_bin_add(GST_BIN(thread),osssink);
  gst_element_connect(sinesrc,"src",osssink,"sink");
  g_object_set(G_OBJECT(osssink),"fragment",0x00180008,NULL);
  g_object_set(G_OBJECT(sinesrc),"buffersize",64,NULL);
 
  dpman = gst_dpman_get_manager (sinesrc);

  freq = gst_dparam_smooth_new(G_TYPE_FLOAT);
  vals = GST_DPARAM_GET_POINT(freq, 0LL);
  
  g_value_set_float(vals[0], 10.0);

  // this defines the maximum slope that this
  // param can change.  This says that in 50ms
  // the value can change by a maximum of one semitone
  // (the log of one semitone is 0.693)
  g_value_set_float(vals[1], 0.693);
  g_value_set_float(vals[2], 50000000.0);
  
  // set the default update period to 0.5ms, or 2000Hz
  GST_DPARAM_DEFAULT_UPDATE_PERIOD(freq) = 2000000LL;
  
  volume = gst_dparam_smooth_new(G_TYPE_FLOAT);
  vals = GST_DPARAM_GET_POINT(volume, 0LL);
  
  // this defines the maximum slope that this
  // param can change.  This says that in 10ms
  // the value can change by a maximum of 0.2
  g_value_set_float(vals[1], 0.2);
  g_value_set_float(vals[2], 10000000.0);
  
  // set the default update period to 0.5ms, or 2000Hz
  GST_DPARAM_DEFAULT_UPDATE_PERIOD(volume) = 2000000LL;
  
  g_assert(gst_dpman_attach_dparam (dpman, "volume", volume));
  g_assert(gst_dpman_attach_dparam (dpman, "freq", freq));
  
  gst_dpman_set_mode(dpman, "synchronous");

  /***** set up the GUI *****/
  window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_default_size(GTK_WINDOW(window), 80, 400);
  g_signal_connect(window,"delete_event",GTK_SIGNAL_FUNC(quit_live),NULL);
  hbox = gtk_hbox_new(TRUE,0);
  gtk_container_add(GTK_CONTAINER(window),hbox);

  spec = gst_dpman_get_dparam_spec (dpman, "volume");
  volume_adj = (GtkAdjustment*)gtk_adjustment_new(g_value_get_float(spec->default_val), 
                                                  g_value_get_float(spec->min_val),
                                                  g_value_get_float(spec->max_val), 0.1, 0.01, 0.01);
  volume_slider = gtk_vscale_new(volume_adj);
  gtk_scale_set_digits(GTK_SCALE(volume_slider), 2);
  gtk_box_pack_start(GTK_BOX(hbox),volume_slider,TRUE,TRUE,0);

  spec = gst_dpman_get_dparam_spec (dpman, "freq");
  freq_adj = (GtkAdjustment*)gtk_adjustment_new((gfloat)log(g_value_get_float(spec->default_val)), 
                                                (gfloat)log(g_value_get_float(spec->min_val)),
                                                (gfloat)log(g_value_get_float(spec->max_val)), 0.1, 0.01, 0.01);
  freq_slider = gtk_vscale_new(freq_adj);
  gtk_scale_set_digits(GTK_SCALE(freq_slider), 2);
  gtk_box_pack_start(GTK_BOX(hbox),freq_slider,TRUE,TRUE,0);
  
  
  /***** set up the handlers and such *****/
  //gtk_signal_connect(volume_adj,"value-changed",GTK_SIGNAL_FUNC(volume_changed),sinesrc);
  g_signal_connect(volume_adj,"value-changed",
					 GTK_SIGNAL_FUNC(dynparm_value_changed),
					 volume);

  g_signal_connect(freq_adj,"value-changed",
					 GTK_SIGNAL_FUNC(dynparm_log_value_changed),
					 freq);
  gtk_adjustment_value_changed(volume_adj);
  gtk_adjustment_value_changed(freq_adj);
  
  g_print("starting pipeline\n");
 
  /***** start everything up *****/
  gst_element_set_state(thread,GST_STATE_PLAYING);


  gtk_widget_show_all(window);
  gtk_main();
  
  return 0;
}
