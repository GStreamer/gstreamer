#include <gst/gst.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdkx.h>
#include <gst/interfaces/xoverlay.h>


GstElement *pipeline;

static gboolean
expose_cb (GtkWidget * widget, GdkEventExpose * event, gpointer data)
{
  GstXOverlay *overlay =
    GST_X_OVERLAY (gst_bin_get_by_interface (GST_BIN (data),
                                             GST_TYPE_X_OVERLAY));
  gst_x_overlay_set_xwindow_id (overlay, GDK_WINDOW_XWINDOW (widget->window));
  return FALSE;
}

static void
destroy_cb (gpointer data)
{
  g_message ("destroy callback");

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);

  gtk_main_quit ();
}

gboolean
apply_fx (GtkWidget * widget, gpointer data)
{
  gchar *fx;
  GEnumClass *p_class;

/* heeeellppppp!! */
  p_class =
      G_PARAM_SPEC_ENUM (g_object_class_find_property (G_OBJECT_GET_CLASS
          (G_OBJECT (data)), "effect")
      )->enum_class;

  fx = gtk_combo_box_get_active_text (GTK_COMBO_BOX (widget));
  g_print ("setting: %s - %s\n", fx, g_enum_get_value_by_nick (p_class,
          fx)->value_name);
  g_object_set (G_OBJECT (data), "effect", g_enum_get_value_by_nick (p_class,
          fx)->value, NULL);
  return FALSE;
}

gboolean
play_cb (GtkWidget * widget, gpointer data)
{
  g_message ("playing");
  gst_element_set_state (GST_ELEMENT (data), GST_STATE_PLAYING);
  return FALSE;
}

gboolean
null_cb (GtkWidget * widget, gpointer data)
{
  g_message ("nulling");
  gst_element_set_state (GST_ELEMENT (data), GST_STATE_NULL);
  return FALSE;
}

gboolean
ready_cb (GtkWidget * widget, gpointer data)
{
  g_message ("readying");
  gst_element_set_state (GST_ELEMENT (data), GST_STATE_READY);
  return FALSE;
}

gboolean
pause_cb (GtkWidget * widget, gpointer data)
{
  g_message ("pausing");
  gst_element_set_state (GST_ELEMENT (data), GST_STATE_PAUSED);
  return FALSE;
}

gint
main (gint argc, gchar * argv[])
{
  GstStateChangeReturn ret;
  GstElement *src, *capsflt, *uload, *filter, *sink;
  GstCaps *caps;

  GtkWidget *window;
  GtkWidget *screen;
  GtkWidget *vbox, *combo;
  GtkWidget *hbox;
  GtkWidget *play, *pause, *null, *ready;

  gtk_init (&argc, &argv);
  gst_init (&argc, &argv);

  g_set_application_name ("gst-gl-effects test app");

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_container_set_border_width (GTK_CONTAINER (window), 3);

  g_signal_connect (G_OBJECT (window), "delete-event",
      G_CALLBACK (destroy_cb), NULL);
  g_signal_connect (G_OBJECT (window), "destroy-event",
      G_CALLBACK (destroy_cb), NULL);

  pipeline = gst_pipeline_new ("pipeline");

  src = gst_element_factory_make ("v4l2src", "myv4l2src");
  capsflt = gst_element_factory_make ("capsfilter", "cflt");
  uload = gst_element_factory_make ("glupload", "glu");
  filter = gst_element_factory_make ("gleffects", "flt");
  sink = gst_element_factory_make ("glimagesink", "glsink");

  gst_bin_add_many (GST_BIN (pipeline), src, capsflt, uload, filter, sink, NULL);

  if (!gst_element_link_many (src, capsflt, uload, filter, sink, NULL)) {
    g_print ("Failed to link one or more elements!\n");
    return -1;
  }

  screen = gtk_drawing_area_new ();

  gtk_widget_set_size_request (screen, 640, 480);       // 500 x 376

  vbox = gtk_vbox_new (FALSE, 2);

  gtk_box_pack_start (GTK_BOX (vbox), screen, TRUE, TRUE, 0);

  combo = gtk_combo_box_new_text ();

  gtk_combo_box_append_text (GTK_COMBO_BOX (combo), "identity");
  gtk_combo_box_append_text (GTK_COMBO_BOX (combo), "squeeze");
  gtk_combo_box_append_text (GTK_COMBO_BOX (combo), "stretch");
  gtk_combo_box_append_text (GTK_COMBO_BOX (combo), "bulge");
  gtk_combo_box_append_text (GTK_COMBO_BOX (combo), "twirl");
  gtk_combo_box_append_text (GTK_COMBO_BOX (combo), "tunnel");
  gtk_combo_box_append_text (GTK_COMBO_BOX (combo), "fisheye");
  gtk_combo_box_append_text (GTK_COMBO_BOX (combo), "square");
  gtk_combo_box_append_text (GTK_COMBO_BOX (combo), "mirror");
  gtk_combo_box_append_text (GTK_COMBO_BOX (combo), "heat");
  gtk_combo_box_append_text (GTK_COMBO_BOX (combo), "xpro");
  gtk_combo_box_append_text (GTK_COMBO_BOX (combo), "sepia");
  gtk_combo_box_append_text (GTK_COMBO_BOX (combo), "glow");

  g_signal_connect (G_OBJECT (combo), "changed", G_CALLBACK (apply_fx), filter);

  gtk_box_pack_start (GTK_BOX (vbox), combo, FALSE, FALSE, 0);

  hbox = gtk_hbox_new (FALSE, 0);

  play = gtk_button_new_with_label ("PLAY");

  g_signal_connect (G_OBJECT (play), "clicked", G_CALLBACK (play_cb), pipeline);

  pause = gtk_button_new_with_label ("PAUSE");

  g_signal_connect (G_OBJECT (pause), "clicked",
      G_CALLBACK (pause_cb), pipeline);

  null = gtk_button_new_with_label ("NULL");

  g_signal_connect (G_OBJECT (null), "clicked", G_CALLBACK (null_cb), pipeline);

  ready = gtk_button_new_with_label ("READY");

  g_signal_connect (G_OBJECT (ready), "clicked",
      G_CALLBACK (ready_cb), pipeline);

  gtk_box_pack_start (GTK_BOX (hbox), null, TRUE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (hbox), ready, TRUE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (hbox), play, TRUE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (hbox), pause, TRUE, TRUE, 0);

  gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);

  gtk_container_add (GTK_CONTAINER (window), vbox);

  g_signal_connect (screen, "expose-event", G_CALLBACK (expose_cb), pipeline);

  caps = gst_caps_new_simple ("video/x-raw-yuv",
      "width", G_TYPE_INT, 640,
      "height", G_TYPE_INT, 480, "framerate", GST_TYPE_FRACTION, 30, 1, NULL);
  g_object_set (G_OBJECT (capsflt), "caps", caps, NULL);
  gst_caps_unref (caps);

  ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_print ("Failed to start up pipeline!\n");
    return -1;
  }
//     g_timeout_add (2000, (GSourceFunc) set_fx, filter);

  gtk_widget_show_all (GTK_WIDGET (window));

  gtk_main ();

//     event_loop (pipeline);

  return 0;
}
