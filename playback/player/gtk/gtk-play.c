#include <string.h>

#include <gst/gst.h>
#include <gst/video/videooverlay.h>

#include <gdk/gdk.h>
#if defined (GDK_WINDOWING_X11)
#include <gdk/gdkx.h>
#elif defined (GDK_WINDOWING_WIN32)
#include <gdk/gdkwin32.h>
#elif defined (GDK_WINDOWING_QUARTZ)
#include <gdk/gdkquartz.h>
#endif

#include <gtk/gtk.h>

#include <gst/player/player.h>

typedef struct
{
  GstPlayer *player;
  gchar *uri;

  GtkWidget *window;
  GtkWidget *play_button, *pause_button;
  GtkWidget *seekbar;
  GtkWidget *video_area;
  gulong seekbar_value_changed_signal_id;
} GtkPlay;

static void
delete_event_cb (GtkWidget * widget, GdkEvent * event, GtkPlay * play)
{
  gst_player_stop (play->player);
  gtk_main_quit ();
}

static void
video_area_realize_cb (GtkWidget * widget, GtkPlay * play)
{
  GdkWindow *window = gtk_widget_get_window (widget);
  guintptr window_handle;

  if (!gdk_window_ensure_native (window))
    g_error ("Couldn't create native window needed for GstXOverlay!");

#if defined (GDK_WINDOWING_WIN32)
  window_handle = (guintptr) GDK_WINDOW_HWND (window);
#elif defined (GDK_WINDOWING_QUARTZ)
  window_handle = gdk_quartz_window_get_nsview (window);
#elif defined (GDK_WINDOWING_X11)
  window_handle = GDK_WINDOW_XID (window);
#endif
  g_object_set (play->player, "window-handle", (gpointer) window_handle, NULL);
}

static void
play_clicked_cb (GtkButton * button, GtkPlay * play)
{
  gst_player_play (play->player);
}

static void
pause_clicked_cb (GtkButton * button, GtkPlay * play)
{
  gst_player_pause (play->player);
}

static void
seekbar_value_changed_cb (GtkRange * range, GtkPlay * play)
{
  gdouble value = gtk_range_get_value (GTK_RANGE (play->seekbar));
  gst_player_seek (play->player, gst_util_uint64_scale (value, GST_SECOND, 1));
}

static void
create_ui (GtkPlay * play)
{
  GtkWidget *controls, *main_hbox, *main_vbox;

  play->window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  g_signal_connect (G_OBJECT (play->window), "delete-event",
      G_CALLBACK (delete_event_cb), play);

  play->video_area = gtk_drawing_area_new ();
  gtk_widget_set_double_buffered (play->video_area, FALSE);
  g_signal_connect (play->video_area, "realize",
      G_CALLBACK (video_area_realize_cb), play);

  play->play_button =
      gtk_button_new_from_icon_name ("media-playback-start",
      GTK_ICON_SIZE_BUTTON);
  g_signal_connect (G_OBJECT (play->play_button), "clicked",
      G_CALLBACK (play_clicked_cb), play);

  play->pause_button =
      gtk_button_new_from_icon_name ("media-playback-pause",
      GTK_ICON_SIZE_BUTTON);
  g_signal_connect (G_OBJECT (play->pause_button), "clicked",
      G_CALLBACK (pause_clicked_cb), play);

  play->seekbar =
      gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
  gtk_scale_set_draw_value (GTK_SCALE (play->seekbar), 0);
  play->seekbar_value_changed_signal_id =
      g_signal_connect (G_OBJECT (play->seekbar), "value-changed",
      G_CALLBACK (seekbar_value_changed_cb), play);

  controls = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_box_pack_start (GTK_BOX (controls), play->play_button, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (controls), play->pause_button, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (controls), play->seekbar, TRUE, TRUE, 2);

  main_hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_box_pack_start (GTK_BOX (main_hbox), play->video_area, TRUE, TRUE, 0);

  main_vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_pack_start (GTK_BOX (main_vbox), main_hbox, TRUE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (main_vbox), controls, FALSE, FALSE, 0);
  gtk_container_add (GTK_CONTAINER (play->window), main_vbox);

  gtk_widget_realize (play->video_area);

  gtk_widget_show_all (play->window);

  gtk_widget_hide (play->video_area);
}

static void
play_clear (GtkPlay * play)
{
  g_free (play->uri);
  g_object_unref (play->player);
}

static void
duration_changed_cb (GstPlayer * unused, GstClockTime duration, GtkPlay * play)
{
  gtk_range_set_range (GTK_RANGE (play->seekbar), 0,
      (gdouble) duration / GST_SECOND);
}

static void
position_updated_cb (GstPlayer * unused, GstClockTime position, GtkPlay * play)
{
  g_signal_handler_block (play->seekbar, play->seekbar_value_changed_signal_id);
  gtk_range_set_value (GTK_RANGE (play->seekbar),
      (gdouble) position / GST_SECOND);
  g_signal_handler_unblock (play->seekbar,
      play->seekbar_value_changed_signal_id);
}

static void
video_dimensions_changed_cb (GstPlayer * unused, gint width, gint height,
    GtkPlay * play)
{
  if (width > 0 && height > 0)
    gtk_widget_show (play->video_area);
  else
    gtk_widget_hide (play->video_area);
}

int
main (gint argc, gchar ** argv)
{
  GtkPlay play;
  GOptionContext *ctx;
  GOptionEntry options[] = {
    {NULL}
  };
  GError *err = NULL;

  memset (&play, 0, sizeof (play));

  g_set_prgname ("gtk-play");

  ctx = g_option_context_new ("FILE|URI");
  g_option_context_add_main_entries (ctx, options, NULL);
  g_option_context_add_group (ctx, gtk_get_option_group (TRUE));
  g_option_context_add_group (ctx, gst_init_get_option_group ());
  if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
    g_print ("Error initializing: %s\n", GST_STR_NULL (err->message));
    return 1;
  }
  g_option_context_free (ctx);

  // FIXME: Add support for playlists and stuff
  play.uri = g_strdup (argv[1]);
  if (!play.uri) {
    g_print ("Usage: %s FILE|URI\n", argv[0]);
    return 1;
  }

  play.player = gst_player_new ();

  g_object_set (play.player, "dispatch-to-main-context", TRUE, NULL);

  if (!gst_uri_is_valid (play.uri)) {
    gchar *uri = gst_filename_to_uri (play.uri, NULL);
    g_free (play.uri);
    play.uri = uri;
  }
  g_object_set (play.player, "uri", play.uri, NULL);

  create_ui (&play);

  g_signal_connect (play.player, "position-updated",
      G_CALLBACK (position_updated_cb), &play);
  g_signal_connect (play.player, "duration-changed",
      G_CALLBACK (duration_changed_cb), &play);
  g_signal_connect (play.player, "video-dimensions-changed",
      G_CALLBACK (video_dimensions_changed_cb), &play);

  gst_player_pause (play.player);

  gtk_main ();

  play_clear (&play);

  return 0;
}
