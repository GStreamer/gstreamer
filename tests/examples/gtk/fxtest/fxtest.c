#include <gst/gst.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#ifdef WIN32
#include <gdk/gdkwin32.h>
#else
#include <gdk/gdkx.h>
#endif

#include <gst/interfaces/xoverlay.h>


/* TODO: use x overlay in the proper way (like suggested in docs, see gtkxoverlay example) */
static gboolean
expose_cb (GtkWidget * widget, GdkEventExpose * event, gpointer data)
{
  GstXOverlay *overlay =
    GST_X_OVERLAY (gst_bin_get_by_interface (GST_BIN (data),
                                             GST_TYPE_X_OVERLAY));
#ifdef WIN32
  gst_x_overlay_set_xwindow_id (overlay, (gulong)GDK_WINDOW_HWND(widget->window));
#else
  gst_x_overlay_set_xwindow_id (overlay, GDK_WINDOW_XWINDOW (widget->window));
#endif

  return FALSE;
}

static void
destroy_cb (GtkWidget *widget, GdkEvent *event, GstElement *pipeline)
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
  GstElement *pipeline;
  GstElement *uload, *filter, *sink;
  GstElement *sourcebin;
  GError *error = NULL;

  GtkWidget *window;
  GtkWidget *screen;
  GtkWidget *vbox, *combo;
  GtkWidget *hbox;
  GtkWidget *play, *pause, *null, *ready;

  gchar **source_desc_array = NULL;
  gchar *source_desc = NULL;

  GOptionContext *context;
  GOptionEntry options[] = {
    { "source-bin", 's', 0, G_OPTION_ARG_STRING_ARRAY, &source_desc_array,
      "Use a custom source bin description (gst-launch style)", NULL },
    { NULL }
  };

  g_thread_init (NULL);

  context = g_option_context_new (NULL);
  g_option_context_add_main_entries (context, options, NULL);
  g_option_context_add_group (context, gst_init_get_option_group ());
  g_option_context_add_group (context, gtk_get_option_group (TRUE));
  if (!g_option_context_parse (context, &argc, &argv, &error)) {
    g_print ("Inizialization error: %s\n", GST_STR_NULL (error->message));
    return -1;
  }
  g_option_context_free (context);
  
  if (source_desc_array != NULL) {
    source_desc = g_strjoinv (" ", source_desc_array);
    g_strfreev (source_desc_array);
  }
  if (source_desc == NULL) {
    source_desc = g_strdup ("videotestsrc ! video/x-raw-rgb, width=352, height=288 ! identity");
  }

  sourcebin = gst_parse_bin_from_description (g_strdup (source_desc), TRUE, &error);
  g_free (source_desc);
  if (error) {
    g_print ("Error while parsing source bin description: %s\n", 
             GST_STR_NULL (error->message));
    return -1;
  }

  g_set_application_name ("gst-gl-effects test app");

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_container_set_border_width (GTK_CONTAINER (window), 3);

  pipeline = gst_pipeline_new ("pipeline");

  uload = gst_element_factory_make ("glupload", "glu");
  filter = gst_element_factory_make ("gleffects", "flt");
  sink = gst_element_factory_make ("glimagesink", "glsink");

  gst_bin_add_many (GST_BIN (pipeline), sourcebin, uload, filter, sink, NULL);

  if (!gst_element_link_many (sourcebin, uload, filter, sink, NULL)) {
    g_print ("Failed to link one or more elements!\n");
    return -1;
  }

  g_signal_connect (G_OBJECT (window), "delete-event",
                    G_CALLBACK (destroy_cb), pipeline);
  g_signal_connect (G_OBJECT (window), "destroy-event",
                    G_CALLBACK (destroy_cb), pipeline);

  screen = gtk_drawing_area_new ();

  gtk_widget_set_size_request (screen, 640, 480);       // 500 x 376

  vbox = gtk_vbox_new (FALSE, 2);

  gtk_box_pack_start (GTK_BOX (vbox), screen, TRUE, TRUE, 0);

  combo = gtk_combo_box_new_text ();

  gtk_combo_box_append_text (GTK_COMBO_BOX (combo), "identity");
  gtk_combo_box_append_text (GTK_COMBO_BOX (combo), "mirror");
  gtk_combo_box_append_text (GTK_COMBO_BOX (combo), "squeeze");
  gtk_combo_box_append_text (GTK_COMBO_BOX (combo), "stretch");
  gtk_combo_box_append_text (GTK_COMBO_BOX (combo), "fisheye");
  gtk_combo_box_append_text (GTK_COMBO_BOX (combo), "twirl");
  gtk_combo_box_append_text (GTK_COMBO_BOX (combo), "bulge");
  gtk_combo_box_append_text (GTK_COMBO_BOX (combo), "tunnel");
  gtk_combo_box_append_text (GTK_COMBO_BOX (combo), "square");
  gtk_combo_box_append_text (GTK_COMBO_BOX (combo), "heat");
  gtk_combo_box_append_text (GTK_COMBO_BOX (combo), "xpro");
  gtk_combo_box_append_text (GTK_COMBO_BOX (combo), "lumaxpro");
  gtk_combo_box_append_text (GTK_COMBO_BOX (combo), "sepia");
  gtk_combo_box_append_text (GTK_COMBO_BOX (combo), "xray");
  gtk_combo_box_append_text (GTK_COMBO_BOX (combo), "sin");
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

  ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_print ("Failed to start up pipeline!\n");
    return -1;
  }

  gtk_widget_show_all (GTK_WIDGET (window));

  gtk_main ();

  return 0;
}
