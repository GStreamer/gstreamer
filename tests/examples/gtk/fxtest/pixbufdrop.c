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

static gint delay = 0;
static gint saveddelay = 0;
static gint method = 1;

struct _SourceData
{
  gpointer data;
  gpointer nick;
  gpointer value;
};
typedef struct _SourceData SourceData;

static GstBusSyncReply 
create_window (GstBus *bus, GstMessage *message, GtkWidget *widget)
{
    // ignore anything but 'prepare-xwindow-id' element messages
    if (GST_MESSAGE_TYPE (message) != GST_MESSAGE_ELEMENT)
        return GST_BUS_PASS;

    if (!gst_structure_has_name (message->structure, "prepare-xwindow-id"))
        return GST_BUS_PASS;

#ifdef WIN32
    gst_x_overlay_set_xwindow_id (GST_X_OVERLAY (GST_MESSAGE_SRC (message)),
        (gulong)GDK_WINDOW_HWND(widget->window));
#else
    gst_x_overlay_set_xwindow_id (GST_X_OVERLAY (GST_MESSAGE_SRC (message)),
        GDK_WINDOW_XWINDOW(widget->window));
#endif

    gst_message_unref (message);

    return GST_BUS_DROP;
}

static gboolean 
expose_cb(GtkWidget *widget, GdkEventExpose *event, GstElement *videosink)
{
    gst_x_overlay_expose (GST_X_OVERLAY (videosink));
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

static gboolean
set_location_delayed (gpointer data)
{
  SourceData *sdata = (SourceData *) data;
  delay--;
  g_print ("%d\n", delay);
  if (delay > 0) {
    return TRUE;
  }
  g_object_set (G_OBJECT (sdata->data), sdata->nick, sdata->value, NULL);
  delay = saveddelay;
  return FALSE;
}

static void
on_drag_data_received (GtkWidget * widget,
                       GdkDragContext * context, int x, int y,
                       GtkSelectionData * seldata, guint inf, guint time,
                       gpointer data)
{
  GdkPixbufFormat *format;
  SourceData *userdata = g_new0 (SourceData, 1);
  gchar **uris = gtk_selection_data_get_uris (seldata);
  gchar *filename = NULL;

  g_return_if_fail (uris != NULL);
  filename = g_filename_from_uri (uris[0], NULL, NULL);
  g_return_if_fail (filename != NULL);
  format = gdk_pixbuf_get_file_info (filename, NULL, NULL);
  g_return_if_fail (format);
  g_print ("received %s image: %s\n", filename,
           gdk_pixbuf_format_get_name (format));
  userdata->nick = "location";
  userdata->value = g_strdup (filename);
  userdata->data = data;
  saveddelay = delay;
  if (delay > 0) {
    g_print ("%d\n", delay);
    g_timeout_add_seconds (1, set_location_delayed, userdata);
  }
  else
    g_object_set (G_OBJECT (userdata->data), userdata->nick, userdata->value, NULL);
  g_free (filename);
}


gint
main (gint argc, gchar * argv[])
{
  GstStateChangeReturn ret;
  GstElement *pipeline;
  GstElement *uload, *filter, *sink;
  GstElement *sourcebin;
  GstBus *bus;
  GError *error = NULL;

  GtkWidget *window;
  GtkWidget *screen;
  GtkWidget *vbox;
  GtkWidget *hbox;
  GtkWidget *play, *pause, *null, *ready;

  gchar **source_desc_array = NULL;
  gchar *source_desc = NULL;

  GOptionContext *context;
  GOptionEntry options[] = {
    { "source-bin", 's', 0, G_OPTION_ARG_STRING_ARRAY, &source_desc_array,
      "Use a custom source bin description (gst-launch style)", NULL },
    { "method", 'm', 0, G_OPTION_ARG_INT, &method, "1 for gstdifferencematte, 2 for gloverlay", "M" },
    { "delay", 'd', 0, G_OPTION_ARG_INT, &delay, "Wait N seconds before to send the image to gstreamer (useful with differencematte)", "N" },
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
  if (method == 2) {
    filter = gst_element_factory_make ("gloverlay", "flt");
  } else {
    filter = gst_element_factory_make ("gldifferencematte", "flt");
  }
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

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  gst_bus_set_sync_handler (bus, (GstBusSyncHandler) create_window, screen);
  gst_object_unref (bus);
  g_signal_connect(screen, "expose-event", G_CALLBACK(expose_cb), sink);

  gtk_drag_dest_set (screen, GTK_DEST_DEFAULT_ALL, NULL, 0, GDK_ACTION_COPY);
  gtk_drag_dest_add_uri_targets (screen);

  g_signal_connect (screen, "drag-data-received",
      G_CALLBACK (on_drag_data_received), filter);

  ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_print ("Failed to start up pipeline!\n");
    return -1;
  }

  gtk_widget_show_all (GTK_WIDGET (window));

  gtk_main ();

  return 0;
}
