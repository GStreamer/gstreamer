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
  GtkWidget *streams_list;
  gboolean updating_slider;
  
  GstState state;
  gint64 duration;
} CustomData;
  
/* Forward definition of the message processing function */
static gboolean handle_message (GstBus *bus, GstMessage *msg, CustomData *data);
    
static void realize_cb (GtkWidget *widget, CustomData *data) {
  GdkWindow *window = gtk_widget_get_window (widget);
  guintptr window_handle;
  
  /* This is here just for pedagogical purposes, GDK_WINDOW_XID will call it
   * as well */
  if (!gdk_window_ensure_native (window))
    g_error ("Couldn't create native window needed for GstXOverlay!");
  
#if defined (GDK_WINDOWING_WIN32)
  window_handle = (guintptr)GDK_WINDOW_HWND (window);
#elif defined (GDK_WINDOWING_QUARTZ)
  window_handle = gdk_quartz_window_get_nsview (window);
#elif defined (GDK_WINDOWING_X11)
  window_handle = GDK_WINDOW_XID (window);
#endif
  gst_x_overlay_set_window_handle (GST_X_OVERLAY (data->playbin2), window_handle);
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
  
  return FALSE;
}
  
static void slider_cb (GtkRange *range, CustomData *data) {
  if (!data->updating_slider) {
    gdouble value = gtk_range_get_value (GTK_RANGE (data->slider));
    gst_element_seek_simple (data->playbin2, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT, (gint64)(value * GST_SECOND));
  }
}
  
static void create_ui (CustomData *data) {
  GtkWidget *controls, *main_box, *main_hbox;
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
  g_signal_connect (G_OBJECT (data->slider), "value-changed", G_CALLBACK (slider_cb), data);

  data->streams_list = gtk_text_view_new ();
  gtk_text_view_set_editable (GTK_TEXT_VIEW (data->streams_list), FALSE);
  
  controls = gtk_hbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (controls), play_button, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (controls), pause_button, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (controls), stop_button, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (controls), data->slider, TRUE, TRUE, 2);

  main_hbox = gtk_hbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (main_hbox), data->video_window, TRUE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (main_hbox), data->streams_list, FALSE, FALSE, 2);

  main_box = gtk_vbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (main_box), main_hbox, TRUE, TRUE, 0);
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
      gtk_range_set_range (GTK_RANGE (data->slider), 0, (gdouble)data->duration / GST_SECOND);
    }
  }
  
  if (gst_element_query_position (data->playbin2, &fmt, &current)) {
    data->updating_slider = TRUE;
    gtk_range_set_value (GTK_RANGE (data->slider), (gdouble)current / GST_SECOND);
    data->updating_slider = FALSE;
  }
  return TRUE;
}
  
static void reset_app (CustomData *data) {
  gst_object_unref (data->playbin2);
}
  
static void tags_cb (GstElement *playbin2, gint stream, CustomData *data) {
  gst_element_post_message (playbin2,
    gst_message_new_application (GST_OBJECT (playbin2),
      gst_structure_new ("tags-changed", NULL)));
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
  g_object_set (data.playbin2, "uri", "http://docs.gstreamer.com/media/sintel_trailer-480p.webm", NULL);
  
  /* Connect to interesting signals in playbin2 */
  g_signal_connect (G_OBJECT (data.playbin2), "video-tags-changed", (GCallback) tags_cb, &data);
  g_signal_connect (G_OBJECT (data.playbin2), "audio-tags-changed", (GCallback) tags_cb, &data);
  g_signal_connect (G_OBJECT (data.playbin2), "text-tags-changed", (GCallback) tags_cb, &data);
  
  create_ui (&data);
  
  bus = gst_element_get_bus (data.playbin2);
  gst_bus_add_watch (bus, (GstBusFunc)handle_message, &data);
  gst_object_unref (bus);
  
  /* Start playing */
  ret = gst_element_set_state (data.playbin2, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("Unable to set the pipeline to the playing state.\n");
    gst_object_unref (data.playbin2);
    return -1;
  }
  
  /* Register a function that GTK will call every second */
  g_timeout_add (1000, (GSourceFunc)refresh_ui, &data);
  
  gtk_main ();
  
  /* Free resources */
  gst_element_set_state (data.playbin2, GST_STATE_NULL);
  reset_app (&data);
  return 0;
}
  
/* Extract metadata from all the streams */
static void analyze_streams (CustomData *data) {
  gint i;
  GstTagList *tags;
  gchar *str, total_str[8192];
  guint rate;
  gint n_video, n_audio, n_text;
  GtkTextBuffer *text;
  GtkTextIter text_start, text_end;
  
  /* Clean current contents of the widget */
  text = gtk_text_view_get_buffer (GTK_TEXT_VIEW (data->streams_list));
  gtk_text_buffer_get_start_iter (text, &text_start);
  gtk_text_buffer_get_end_iter (text, &text_end);
  gtk_text_buffer_delete (text, &text_start, &text_end);
  
  /* Read some properties */
  g_object_get (data->playbin2, "n-video", &n_video, NULL);
  g_object_get (data->playbin2, "n-audio", &n_audio, NULL);
  g_object_get (data->playbin2, "n-text", &n_text, NULL);
  
  for (i = 0; i < n_video; i++) {
    tags = NULL;
    /* Retrieve the stream's video tags */
    g_signal_emit_by_name (data->playbin2, "get-video-tags", i, &tags);
    if (tags) {
      g_snprintf (total_str, sizeof (total_str), "video stream %d:\n", i);
      gtk_text_buffer_insert_at_cursor (text, total_str, -1);
      gst_tag_list_get_string (tags, GST_TAG_VIDEO_CODEC, &str);
      g_snprintf (total_str, sizeof (total_str), "  codec: %s\n", str ? str : "unknown");
      gtk_text_buffer_insert_at_cursor (text, total_str, -1);
      g_free (str);
      gst_tag_list_free (tags);
    }
  }
  
  for (i = 0; i < n_audio; i++) {
    tags = NULL;
    /* Retrieve the stream's audio tags */
    g_signal_emit_by_name (data->playbin2, "get-audio-tags", i, &tags);
    if (tags) {
      g_snprintf (total_str, sizeof (total_str), "\naudio stream %d:\n", i);
      gtk_text_buffer_insert_at_cursor (text, total_str, -1);
      if (gst_tag_list_get_string (tags, GST_TAG_AUDIO_CODEC, &str)) {
        g_snprintf (total_str, sizeof (total_str), "  codec: %s\n", str);
        gtk_text_buffer_insert_at_cursor (text, total_str, -1);
        g_free (str);
      }
      if (gst_tag_list_get_string (tags, GST_TAG_LANGUAGE_CODE, &str)) {
        g_snprintf (total_str, sizeof (total_str), "  language: %s\n", str);
        gtk_text_buffer_insert_at_cursor (text, total_str, -1);
        g_free (str);
      }
      if (gst_tag_list_get_uint (tags, GST_TAG_BITRATE, &rate)) {
        g_snprintf (total_str, sizeof (total_str), "  bitrate: %d\n", rate);
        gtk_text_buffer_insert_at_cursor (text, total_str, -1);
      }
      gst_tag_list_free (tags);
    }
  }
  
  for (i = 0; i < n_text; i++) {
    tags = NULL;
    /* Retrieve the stream's subtitle tags */
    g_signal_emit_by_name (data->playbin2, "get-text-tags", i, &tags);
    if (tags) {
      g_snprintf (total_str, sizeof (total_str), "\nsubtitle stream %d:\n", i);
      gtk_text_buffer_insert_at_cursor (text, total_str, -1);
      if (gst_tag_list_get_string (tags, GST_TAG_LANGUAGE_CODE, &str)) {
        g_snprintf (total_str, sizeof (total_str), "  language: %s\n", str);
        gtk_text_buffer_insert_at_cursor (text, total_str, -1);
        g_free (str);
      }
      gst_tag_list_free (tags);
    }
  }
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
      gst_element_set_state (data->playbin2, GST_STATE_READY);
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
    case GST_MESSAGE_APPLICATION:
      if (g_strcmp0 (gst_structure_get_name (msg->structure), "tags-changed") == 0) {
        analyze_streams (data);
      }
      break;
  }
  
  return TRUE;
}
