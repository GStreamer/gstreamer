#include <gtk/gtk.h>
#include <gst/gst.h>

void set_speed (GtkAdjustment *adj, gpointer data)
{
    GstElement *speed = GST_ELEMENT(data);
    gtk_object_set(GTK_OBJECT(speed), "speed", adj->value, NULL);
}

int main(int argc, char **argv) 
{
    GtkWidget *window, *vbox, *hscale, *button;
    GstElement *filesrc, *mad, *stereo2mono, *speed, *osssink, *pipeline;
    
    gst_init (&argc, &argv);
    gtk_init (&argc, &argv);
    
    if (argc!=2) {
        g_print("usage: %s <your.mp3>\n", argv[0]);
        exit(-1);
    }
    
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(window), 400, 80);
    vbox = gtk_vbox_new(FALSE, 0);
    gtk_widget_show(vbox);
    hscale = gtk_hscale_new(GTK_ADJUSTMENT(gtk_adjustment_new(1.0, 0.01, 4.0,
                                                              0.1, 0.0, 0.0)));
    gtk_scale_set_digits(GTK_SCALE(hscale), 2);
    gtk_range_set_update_policy(GTK_RANGE(hscale), GTK_UPDATE_CONTINUOUS);
    button = gtk_button_new_with_label("quit");
    gtk_container_add(GTK_CONTAINER(window), vbox);
    gtk_box_pack_start(GTK_BOX(vbox), hscale, TRUE, TRUE, 2);
    gtk_box_pack_start(GTK_BOX(vbox), button, FALSE, FALSE, 2);
    gtk_widget_show(hscale);
    gtk_signal_connect(GTK_OBJECT(button), "clicked", gtk_main_quit, NULL);
    gtk_widget_show(button);
    
    filesrc = gst_elementfactory_make("filesrc", "filesrc");
    mad = gst_elementfactory_make("mad", "mad");
    stereo2mono = gst_elementfactory_make("stereo2mono", "stereo2mono");
    speed = gst_elementfactory_make("speed", "speed");
    osssink = gst_elementfactory_make("osssink", "osssink");
    gtk_object_set(GTK_OBJECT(osssink), "fragment", 0x00180008, NULL);
    
    gtk_signal_connect(GTK_OBJECT(gtk_range_get_adjustment(GTK_RANGE(hscale))),
                       "value_changed", set_speed, speed);
    
    pipeline = gst_pipeline_new("app");
    gst_bin_add(GST_BIN(pipeline), filesrc);
    gst_bin_add(GST_BIN(pipeline), mad);
    gst_bin_add(GST_BIN(pipeline), stereo2mono);
    gst_bin_add(GST_BIN(pipeline), speed);
    gst_bin_add(GST_BIN(pipeline), osssink);
    gst_element_connect(filesrc, "src", mad, "sink");
    gst_element_connect(mad, "src", stereo2mono, "sink");
    gst_element_connect(stereo2mono, "src", speed, "sink");
    gst_element_connect(speed, "src", osssink, "sink");
    gtk_object_set(GTK_OBJECT(filesrc), "location", argv[1], NULL);
    
    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    
    gtk_widget_show(window);
    gtk_idle_add((GtkFunction)gst_bin_iterate, pipeline);
    
    gtk_main();
}
