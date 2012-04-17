#include <gtk/gtk.h>
#include <gst/gst.h>
  
#include <gdk/gdk.h>
#if defined (GDK_WINDOWING_X11)
#include <gdk/gdkx.h>
#elif defined (GDK_WINDOWING_WIN32)
#include <gdk/gdkwin32.h>
#elif defined (GDK_WINDOWING_QUARTZ)
#include <gdk/gdkquartzwindow.h>
#endif
  
#include <gst/interfaces/xoverlay.h>
#include <memory.h>
  
/* Structure to contain all our information, so we can pass it around */
typedef struct _CustomData {
  GstElement *playbin2;   /* Our one and only pipeline */
  GtkWidget *main_window;
  GtkWidget *video_window;
  GtkWidget *slider;
  
  GstState state;
  gint64 duration;
  
  guintptr embed_xid;
} CustomData;
  
/* Forward definition of the message processing function */
static gboolean handle_message (GstBus *bus, GstMessage *msg, CustomData *data);
static GstBusSyncReply bus_sync_handler (GstBus *bus, GstMessage *msg, CustomData *data);
    
static void realize_cb (GtkWidget *widget, CustomData *data) {
  GdkWindow *window = gtk_widget_get_window (widget);

  /* This is here just for pedagogical purposes, GDK_WINDOW_XID will call it
   * as well */ /*
  if (!gdk_window_ensure_native (window))
    g_error ("Couldn't create native window needed for GstXOverlay!");*/

#if defined (GDK_WINDOWING_WIN32)
  data->embed_xid = (guintptr)GDK_WINDOW_HWND (window);
#elif defined (GDK_WINDOWING_QUARTZ)
  data->embed_xid = gdk_quartz_window_get_nsview (window);
#elif defined (GDK_WINDOWING_X11)
  data->embed_xid = GDK_WINDOW_XID (window);
#endif
}
  
static void play_cb (GtkButton *button, CustomData *data) {
  gst_element_set_state (data->playbin2, GST_STATE_PLAYING);
}
  
static void pause_cb (GtkButton *button, CustomData *data) {
  gst_element_set_state (data->playbin2, GST_STATE_PAUSED);
}
  
static void stop_cb (GtkButton *button, CustomData *data) {
  gst_element_set_state (data->playbin2, GST_STATE_READY);
}
  
static void delete_event_cb (GtkWidget *widget, GdkEvent *event, CustomData *data) {
  stop_cb (NULL, data);
  gtk_main_quit ();
}
  
static gboolean draw_cb (GtkWidget *widget, GdkEventExpose *event, CustomData *data) {
  if (data->state < GST_STATE_PAUSED) {
    GtkAllocation allocation;
    GdkWindow *window = gtk_widget_get_window (widget);
    cairo_t *cr;
    
    gtk_widget_get_allocation (widget, &allocation);
    cr = gdk_cairo_create (window);
    cairo_set_source_rgb (cr, 0, 0, 0);
    cairo_rectangle (cr, 0, 0, allocation.width, allocation.height);
    cairo_fill (cr);
    cairo_destroy (cr);
  }
  /*
  if (app->xoverlay_element)
    gst_x_overlay_expose (GST_X_OVERLAY (app->xoverlay_element));
  */
  return FALSE;
}
  
static void create_ui (CustomData *data) {
  GtkWidget *controls, *main_box;
  GtkWidget *play_button, *pause_button, *stop_button;
  
  data->main_window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  g_signal_connect (G_OBJECT (data->main_window), "delete-event", G_CALLBACK (delete_event_cb), data);

  data->video_window = gtk_drawing_area_new ();
  gtk_widget_set_double_buffered (data->video_window, FALSE);
  g_signal_connect (data->video_window, "realize", G_CALLBACK (realize_cb), data);
  g_signal_connect (data->video_window, "expose_event", G_CALLBACK (draw_cb), data);
  
  play_button = gtk_button_new_from_stock (GTK_STOCK_MEDIA_PLAY);
  g_signal_connect (G_OBJECT (play_button), "clicked", G_CALLBACK (play_cb), data);
  
  pause_button = gtk_button_new_from_stock (GTK_STOCK_MEDIA_PAUSE);
  g_signal_connect (G_OBJECT (pause_button), "clicked", G_CALLBACK (pause_cb), data);
  
  stop_button = gtk_button_new_from_stock (GTK_STOCK_MEDIA_STOP);
  g_signal_connect (G_OBJECT (stop_button), "clicked", G_CALLBACK (stop_cb), data);
  
  data->slider = gtk_hscale_new_with_range (0, 100, 1);
  gtk_scale_set_draw_value (GTK_SCALE (data->slider), 0);
  
  controls = gtk_hbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (controls), play_button, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (controls), pause_button, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (controls), stop_button, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (controls), data->slider, TRUE, TRUE, 2);

  main_box = gtk_vbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (main_box), data->video_window, TRUE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (main_box), controls, FALSE, FALSE, 0);
  gtk_container_add (GTK_CONTAINER (data->main_window), main_box);
  gtk_window_set_default_size (GTK_WINDOW (data->main_window), 640, 480);
  
  gtk_widget_show_all (data->main_window);
  
  gtk_widget_realize (data->main_window);
}
  
static gboolean refresh_ui (CustomData *data) {
  GstFormat fmt = GST_FORMAT_TIME;
  gint64 current = -1;

  /* We do not want to update anything unless we are in the PLAYING state */
  if (data->state != GST_STATE_PLAYING) return TRUE;
  
  /* If we didn't know it yet, query the stream duration */
  if (!GST_CLOCK_TIME_IS_VALID (data->duration)) {
    if (!gst_element_query_duration (data->playbin2, &fmt, &data->duration)) {
      g_printerr ("Could not query current duration.\n");
    } else {
      gtk_range_set_range (GTK_RANGE (data->slider), 0, data->duration / GST_SECOND);
    }
  }
  
  if (gst_element_query_position (data->playbin2, &fmt, &current)) {
    gtk_range_set_value (GTK_RANGE (data->slider), current / GST_SECOND);
  }
  return TRUE;
}
  
int main(int argc, char *argv[]) {
  CustomData data;
  GstStateChangeReturn ret;
  GstBus *bus;
  
  /* Initialize GTK */
  gtk_init (&argc, &argv);
  
  /* Initialize GStreamer */
  gst_init (&argc, &argv);
  
  /* Initialize our data structure */
  memset (&data, 0, sizeof (data));
  data.duration = GST_CLOCK_TIME_NONE;
  
  /* Create the elements */
  data.playbin2 = gst_element_factory_make ("playbin2", "playbin2");
   
  if (!data.playbin2) {
    g_printerr ("Not all elements could be created.\n");
    return -1;
  }
  
  /* Set the URI to play */
//  g_object_set (data.playbin2, "uri", "http://docs.gstreamer.com/media/sintel_trailer-480p.webm", NULL);
  g_object_set (data.playbin2, "uri", "file:///f:/media/big_buck_bunny_480p.H264.mov", NULL);
  
  create_ui (&data);
  
  bus = gst_element_get_bus (data.playbin2);
  gst_bus_set_sync_handler (bus, (GstBusSyncHandler)bus_sync_handler, &data);
  gst_bus_add_watch (bus, (GstBusFunc)handle_message, &data);
  gst_object_unref (bus);
  
  /* Start playing */
  ret = gst_element_set_state (data.playbin2, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("Unable to set the pipeline to the playing state.\n");
    gst_object_unref (data.playbin2);
    return -1;
  }
  
  g_timeout_add (500, (GSourceFunc)refresh_ui, &data);
  // add timeout to refresh UI: query position and duration (if unknown), gtk_range_set_value() on the slider
  gtk_main ();
  
  /* Free resources */
  gst_element_set_state (data.playbin2, GST_STATE_NULL);
  gst_object_unref (data.playbin2);
  return 0;
}
  
static gboolean handle_message (GstBus *bus, GstMessage *msg, CustomData *data) {
  GError *err;
  gchar *debug_info;
  
  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_ERROR:
      gst_message_parse_error (msg, &err, &debug_info);
      g_printerr ("Error received from element %s: %s\n", GST_OBJECT_NAME (msg->src), err->message);
      g_printerr ("Debugging information: %s\n", debug_info ? debug_info : "none");
      g_clear_error (&err);
      g_free (debug_info);
      gtk_main_quit ();
      break;
    case GST_MESSAGE_EOS:
      g_print ("End-Of-Stream reached.\n");
      gst_element_set_state (data->playbin2, GST_STATE_READY);
      break;
    case GST_MESSAGE_STATE_CHANGED: {
      GstState old_state, new_state, pending_state;
      gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);
      if (GST_MESSAGE_SRC (msg) == GST_OBJECT (data->playbin2)) {
        data->state = new_state;
        g_print ("State set to %s\n", gst_element_state_get_name (new_state));
      }
    } break;
  }

  return TRUE;
}
static GstBusSyncReply bus_sync_handler (GstBus *bus, GstMessage *msg, CustomData *data) {
  /*ignore anything but 'prepare-xwindow-id' element messages */
  if (GST_MESSAGE_TYPE (msg) != GST_MESSAGE_ELEMENT)
    return GST_BUS_PASS;
  if (!gst_structure_has_name (msg->structure, "prepare-xwindow-id"))
    return GST_BUS_PASS;
  
  if (data->embed_xid != 0) {
    /* GST_MESSAGE_SRC (message) will be the video sink element */
    GstXOverlay *xoverlay = GST_X_OVERLAY (GST_MESSAGE_SRC (msg));
    gst_x_overlay_set_window_handle (xoverlay, data->embed_xid);
  } else {
    g_warning ("Should have obtained an xid by now!");
  }
  
  gst_message_unref (msg);
  return GST_BUS_DROP;
}
