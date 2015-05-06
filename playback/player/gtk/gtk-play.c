/* GStreamer
 *
 * Copyright (C) 2014-2015 Sebastian Dröge <sebastian@centricular.com>
 * Copyright (C) 2015 Brijesh Singh <brijesh.ksingh@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

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

#define APP_NAME "gtk-play"

typedef struct
{
  GstPlayer *player;
  gchar *uri;

  GList *uris;
  GList *current_uri;

  GtkWidget *window;
  GtkWidget *play_pause_button;
  GtkWidget *prev_button, *next_button;
  GtkWidget *seekbar;
  GtkWidget *video_area;
  GtkWidget *volume_button;
  GtkWidget *media_info;
  gulong seekbar_value_changed_signal_id;
  gboolean playing;
} GtkPlay;

enum
{
  COL_TEXT = 0,
  COL_NUM
};

enum
{
  VIDEO_INFO_START,
  VIDEO_INFO_RESOLUTION,
  VIDEO_INFO_FPS,
  VIDEO_INFO_PAR,
  VIDEO_INFO_CODEC,
  VIDEO_INFO_MAX_BITRATE,
  VIDEO_INFO_END,
  AUDIO_INFO_START,
  AUDIO_INFO_CHANNELS,
  AUDIO_INFO_RATE,
  AUDIO_INFO_LANGUAGE,
  AUDIO_INFO_CODEC,
  AUDIO_INFO_MAX_BITRATE,
  AUDIO_INFO_END,
  SUBTITLE_INFO_START,
  SUBTITLE_INFO_LANGUAGE,
  SUBTITLE_INFO_CODEC,
  SUBTITLE_INFO_END,
};

static void
set_title (GtkPlay * play, const gchar * title)
{
  if (title == NULL) {
    gtk_window_set_title (GTK_WINDOW (play->window), APP_NAME);
  } else {
    gtk_window_set_title (GTK_WINDOW (play->window), title);
  }
}

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
play_pause_clicked_cb (GtkButton * button, GtkPlay * play)
{
  GtkWidget *image;

  if (play->playing) {
    gst_player_pause (play->player);
    image =
        gtk_image_new_from_icon_name ("media-playback-start",
        GTK_ICON_SIZE_BUTTON);
    gtk_button_set_image (GTK_BUTTON (play->play_pause_button), image);
    play->playing = FALSE;
  } else {
    gchar *title;

    gst_player_play (play->player);
    image =
        gtk_image_new_from_icon_name ("media-playback-pause",
        GTK_ICON_SIZE_BUTTON);
    gtk_button_set_image (GTK_BUTTON (play->play_pause_button), image);

    title = gst_player_get_uri (play->player);
    set_title (play, title);
    g_free (title);
    play->playing = TRUE;
  }
}

static void
skip_prev_clicked_cb (GtkButton * button, GtkPlay * play)
{
  GList *prev;
  gchar *cur_uri;

  prev = g_list_previous (play->current_uri);
  g_return_if_fail (prev != NULL);

  gtk_widget_set_sensitive (play->next_button, TRUE);
  gtk_widget_set_sensitive (play->media_info, FALSE);
  gst_player_set_uri (play->player, prev->data);
  play->current_uri = prev;
  gst_player_play (play->player);
  set_title (play, prev->data);
  gtk_widget_set_sensitive (play->prev_button, g_list_previous (prev) != NULL);
}

static void
skip_next_clicked_cb (GtkButton * button, GtkPlay * play)
{
  GList *next, *l;
  gchar *cur_uri;

  next = g_list_next (play->current_uri);
  g_return_if_fail (next != NULL);

  gtk_widget_set_sensitive (play->prev_button, TRUE);
  gtk_widget_set_sensitive (play->media_info, FALSE);
  gst_player_set_uri (play->player, next->data);
  play->current_uri = next;
  gst_player_play (play->player);
  set_title (play, next->data);
  gtk_widget_set_sensitive (play->next_button, g_list_next (next) != NULL);
}

static gchar *
stream_info_get_string (GstPlayerStreamInfo * stream, gint type, gboolean label)
{
  switch (type) {
    case AUDIO_INFO_RATE:
    {
      gchar *buffer;
      GstPlayerAudioInfo *audio = (GstPlayerAudioInfo *) stream;
      buffer = g_strdup_printf ("%s%d", label ? "Sample rate : " : "",
          gst_player_audio_info_get_sample_rate (audio));
      return buffer;
    }
    case AUDIO_INFO_LANGUAGE:
    {
      gchar *buffer;
      GstPlayerAudioInfo *audio = (GstPlayerAudioInfo *) stream;
      buffer = g_strdup_printf ("%s%s", label ? "Language : " : "",
          gst_player_audio_info_get_language (audio));
      return buffer;
    }
    case AUDIO_INFO_CHANNELS:
    {
      gchar *buffer;
      GstPlayerAudioInfo *audio = (GstPlayerAudioInfo *) stream;
      buffer = g_strdup_printf ("%s%d", label ? "Channels : " : "",
          gst_player_audio_info_get_channels (audio));
      return buffer;
    }
    case SUBTITLE_INFO_CODEC:
    case VIDEO_INFO_CODEC:
    case AUDIO_INFO_CODEC:
    {
      gchar *buffer;
      buffer = g_strdup_printf ("%s%s", label ? "Codec : " : "",
          gst_player_stream_info_get_codec (stream));
      return buffer;
    }
    case AUDIO_INFO_MAX_BITRATE:
    {
      gchar *buffer = NULL;
      GstPlayerAudioInfo *audio = (GstPlayerAudioInfo *) stream;
      gint bitrate = gst_player_audio_info_get_max_bitrate (audio);

      if (bitrate > 0)
        buffer = g_strdup_printf ("%s%d", label ? "Max bitrate : " : "",
            bitrate);
      return buffer;
    }
    case VIDEO_INFO_MAX_BITRATE:
    {
      gchar *buffer = NULL;
      GstPlayerVideoInfo *video = (GstPlayerVideoInfo *) stream;
      gint bitrate = gst_player_video_info_get_max_bitrate (video);

      if (bitrate > 0)
        buffer = g_strdup_printf ("%s%d", label ? "Max bitrate : " : "",
            bitrate);
      return buffer;
    }
    case VIDEO_INFO_PAR:
    {
      gint par_d, par_n;
      gchar *buffer;
      GstPlayerVideoInfo *video = (GstPlayerVideoInfo *) stream;

      gst_player_video_info_get_pixel_aspect_ratio (video, &par_n, &par_d);
      buffer = g_strdup_printf ("%s%d:%d", label ? "pixel-aspect-ratio : " :
          "", par_n, par_d);
      return buffer;
    }
    case VIDEO_INFO_FPS:
    {
      gint fps_d, fps_n;
      gchar *buffer;
      GstPlayerVideoInfo *video = (GstPlayerVideoInfo *) stream;

      gst_player_video_info_get_framerate (video, &fps_n, &fps_d);
      buffer = g_strdup_printf ("%s%.2f", label ? "Framerate : " : "",
          (gdouble) fps_n / fps_d);
      return buffer;
    }
    case VIDEO_INFO_RESOLUTION:
    {
      gchar *buffer;
      GstPlayerVideoInfo *video = (GstPlayerVideoInfo *) stream;
      buffer = g_strdup_printf ("%s%dx%d", label ? "Resolution : " : "",
          gst_player_video_info_get_width (video),
          gst_player_video_info_get_height (video));
      return buffer;
    }
    case SUBTITLE_INFO_LANGUAGE:
    {
      gchar *buffer;
      GstPlayerSubtitleInfo *sub = (GstPlayerSubtitleInfo *) stream;
      buffer = g_strdup_printf ("%s%s", label ? "Language : " : "",
          gst_player_subtitle_info_get_language (sub));
      return buffer;
    }
    default:
    {
      return NULL;
    }
  }
}

static gboolean
is_current_stream (GtkPlay * play, GstPlayerStreamInfo * stream)
{
  gboolean ret = FALSE;
  GstPlayerStreamInfo *s;
  GstPlayerVideoInfo *video = gst_player_get_current_video_track (play->player);
  GstPlayerAudioInfo *audio = gst_player_get_current_audio_track (play->player);
  GstPlayerSubtitleInfo *sub =
      gst_player_get_current_subtitle_track (play->player);

  if (GST_IS_PLAYER_VIDEO_INFO (stream))
    s = (GstPlayerStreamInfo *) video;
  else if (GST_IS_PLAYER_AUDIO_INFO (stream))
    s = (GstPlayerStreamInfo *) audio;
  else
    s = (GstPlayerStreamInfo *) sub;

  if (s)
    if (gst_player_stream_info_get_index (stream) ==
        gst_player_stream_info_get_index (s))
      ret = TRUE;

  if (audio)
    g_object_unref (audio);

  if (video)
    g_object_unref (video);

  if (sub)
    g_object_unref (sub);

  return ret;
}

static GtkTreeModel *
create_and_fill_model (GtkPlay * play, GstPlayerMediaInfo * info)
{
  GList *l;
  guint count;
  GtkTreeStore *tree;
  GtkTreeIter child, parent;

  count = 0;
  tree = gtk_tree_store_new (COL_NUM, G_TYPE_STRING);

  for (l = gst_player_media_info_get_stream_list (info); l != NULL; l = l->next) {
    gchar *buffer;
    gint i, start, end;
    GstPlayerStreamInfo *stream = (GstPlayerStreamInfo *) l->data;

    /* define the field range based on stream type */
    if (GST_IS_PLAYER_VIDEO_INFO (stream)) {
      start = VIDEO_INFO_START + 1;
      end = VIDEO_INFO_END;
    } else if (GST_IS_PLAYER_AUDIO_INFO (stream)) {
      start = AUDIO_INFO_START + 1;
      end = AUDIO_INFO_END;
    } else {
      start = SUBTITLE_INFO_START + 1;
      end = SUBTITLE_INFO_END;
    }

    buffer = g_strdup_printf ("Stream %u %s", count++,
        is_current_stream (play, stream) ? "(current)" : "");
    gtk_tree_store_append (tree, &parent, NULL);
    gtk_tree_store_set (tree, &parent, COL_TEXT, buffer, -1);
    g_free (buffer);

    buffer = g_strdup_printf ("Type : %s",
        gst_player_stream_info_get_stream_type (stream));
    gtk_tree_store_append (tree, &child, &parent);
    gtk_tree_store_set (tree, &child, COL_TEXT, buffer, -1);
    g_free (buffer);

    for (i = start; i < end; i++) {
      buffer = stream_info_get_string (stream, i, TRUE);
      if (buffer) {
        gtk_tree_store_append (tree, &child, &parent);
        gtk_tree_store_set (tree, &child, COL_TEXT, buffer, -1);
        g_free (buffer);
      }
    }
  }

  return GTK_TREE_MODEL (tree);
}

static GtkWidget *
create_view_and_model (GtkPlay * play, GstPlayerMediaInfo * info)
{
  GtkWidget *view;
  GtkTreeModel *model;
  GtkTreeViewColumn *col;
  GtkCellRenderer *renderer;

  view = gtk_tree_view_new ();
  col = gtk_tree_view_column_new ();
  gtk_tree_view_append_column (GTK_TREE_VIEW (view), col);
  gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (view), FALSE);

  renderer = gtk_cell_renderer_text_new ();
  gtk_tree_view_column_pack_start (col, renderer, TRUE);
  gtk_tree_view_column_add_attribute (col, renderer, "text", COL_TEXT);

  model = create_and_fill_model (play, info);
  gtk_tree_view_set_model (GTK_TREE_VIEW (view), model);
  g_object_unref (model);

  return view;
}

static void
delete_media_info_window (GtkWidget * button, GtkWindow * window)
{
  gtk_window_close (window);
}

static void
create_media_info_window (GtkPlay * play, GstPlayerMediaInfo * info)
{
  GtkWidget *sw;
  GtkWidget *vbox;
  GtkWidget *label;
  GtkWidget *view;
  GtkWidget *hbox;
  GtkWidget *uri;
  GtkWidget *loc;
  GtkTextIter iter;
  GtkWidget *window;
  GtkTextBuffer *buffer;
  GtkWidget *hbox_close;
  GtkWidget *button_close;

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title (GTK_WINDOW (window), "Media information");
  gtk_window_set_default_size (GTK_WINDOW (window), 550, 450);
  gtk_window_set_position (GTK_WINDOW (window), GTK_WIN_POS_CENTER);
  gtk_container_set_border_width (GTK_CONTAINER (window), 10);

  vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
  gtk_container_add (GTK_CONTAINER (window), vbox);

  label = gtk_label_new (NULL);
  gtk_label_set_markup (GTK_LABEL (label),
      "Information about all the streams contains in your media. \n"
      "Current selected streams are marked as (current).");
  gtk_label_set_justify (GTK_LABEL (label), GTK_JUSTIFY_LEFT);
  gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 2);

  sw = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (sw),
      GTK_SHADOW_ETCHED_IN);
  gtk_box_pack_start (GTK_BOX (vbox), sw, TRUE, TRUE, 0);

  view = create_view_and_model (play, info);
  gtk_container_add (GTK_CONTAINER (sw), view);
  g_signal_connect (view, "realize",
      G_CALLBACK (gtk_tree_view_expand_all), NULL);

  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 2);

  loc = gtk_label_new ("Location : ");
  gtk_box_pack_start (GTK_BOX (hbox), loc, FALSE, FALSE, 2);

  buffer = gtk_text_buffer_new (NULL);
  gtk_text_buffer_get_start_iter (buffer, &iter);
  gtk_text_buffer_insert (buffer, &iter,
      gst_player_media_info_get_uri (info), -1);
  uri = gtk_text_view_new_with_buffer (buffer);
  gtk_box_pack_start (GTK_BOX (hbox), uri, FALSE, FALSE, 2);
  gtk_text_view_set_editable (GTK_TEXT_VIEW (uri), FALSE);
  g_object_unref (buffer);

  hbox_close = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_box_pack_start (GTK_BOX (vbox), hbox_close, FALSE, FALSE, 2);
  button_close = gtk_button_new_with_label (" Close ");
  g_signal_connect (button_close, "clicked",
      G_CALLBACK (delete_media_info_window), window);
  gtk_box_pack_end (GTK_BOX (hbox_close), button_close, FALSE, FALSE, 3);

  gtk_widget_show_all (window);
}

static void
media_info_clicked_cb (GtkButton * button, GtkPlay * play)
{
  GstPlayerMediaInfo *info;

  info = gst_player_get_media_info (play->player);
  if (!info)
    return;

  create_media_info_window (play, info);

  g_object_unref (info);
}

static void
seekbar_value_changed_cb (GtkRange * range, GtkPlay * play)
{
  gdouble value = gtk_range_get_value (GTK_RANGE (play->seekbar));
  gst_player_seek (play->player, gst_util_uint64_scale (value, GST_SECOND, 1));
}

void
volume_changed_cb (GtkScaleButton * button, gdouble value, GtkPlay * play)
{
  gst_player_set_volume (play->player, value);
}

static void
create_ui (GtkPlay * play)
{
  GtkWidget *controls, *main_hbox, *main_vbox;

  play->window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  g_signal_connect (G_OBJECT (play->window), "delete-event",
      G_CALLBACK (delete_event_cb), play);
  set_title (play, APP_NAME);

  play->video_area = gtk_drawing_area_new ();
  g_signal_connect (play->video_area, "realize",
      G_CALLBACK (video_area_realize_cb), play);

  /* Unified play/pause button */
  play->play_pause_button =
      gtk_button_new_from_icon_name ("media-playback-pause",
      GTK_ICON_SIZE_BUTTON);
  g_signal_connect (G_OBJECT (play->play_pause_button), "clicked",
      G_CALLBACK (play_pause_clicked_cb), play);

  play->seekbar =
      gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
  gtk_scale_set_draw_value (GTK_SCALE (play->seekbar), 0);
  play->seekbar_value_changed_signal_id =
      g_signal_connect (G_OBJECT (play->seekbar), "value-changed",
      G_CALLBACK (seekbar_value_changed_cb), play);

  /* Skip backward button */
  play->prev_button =
      gtk_button_new_from_icon_name ("media-skip-backward",
      GTK_ICON_SIZE_BUTTON);
  g_signal_connect (G_OBJECT (play->prev_button), "clicked",
      G_CALLBACK (skip_prev_clicked_cb), play);
  gtk_widget_set_sensitive (play->prev_button, FALSE);

  /* Skip forward button */
  play->next_button =
      gtk_button_new_from_icon_name ("media-skip-forward",
      GTK_ICON_SIZE_BUTTON);
  g_signal_connect (G_OBJECT (play->next_button), "clicked",
      G_CALLBACK (skip_next_clicked_cb), play);
  gtk_widget_set_sensitive (play->next_button, FALSE);

  /* Volume control button */
  play->volume_button = gtk_volume_button_new ();
  gtk_scale_button_set_value (GTK_SCALE_BUTTON (play->volume_button),
      gst_player_get_volume (play->player));
  g_signal_connect (G_OBJECT (play->volume_button), "value-changed",
      G_CALLBACK (volume_changed_cb), play);

  /* media information button */
  play->media_info = gtk_button_new_from_icon_name ("dialog-information",
      GTK_ICON_SIZE_BUTTON);
  g_signal_connect (G_OBJECT (play->media_info), "clicked",
      G_CALLBACK (media_info_clicked_cb), play);
  gtk_widget_set_sensitive (play->media_info, FALSE);

  controls = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_box_pack_start (GTK_BOX (controls), play->prev_button, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (controls), play->play_pause_button, FALSE,
      FALSE, 2);
  gtk_box_pack_start (GTK_BOX (controls), play->next_button, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (controls), play->seekbar, TRUE, TRUE, 2);
  gtk_box_pack_start (GTK_BOX (controls), play->volume_button, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (controls), play->media_info, FALSE, FALSE, 2);

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
  g_list_free_full (play->uris, g_free);
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

static void
eos_cb (GstPlayer * unused, GtkPlay * play)
{
  if (play->playing) {
    GList *next = NULL;
    gchar *uri;

    next = g_list_next (play->current_uri);
    if (next) {
      if (!gtk_widget_is_sensitive (play->prev_button))
        gtk_widget_set_sensitive (play->prev_button, TRUE);
      gtk_widget_set_sensitive (play->next_button, g_list_next (next) != NULL);

      gtk_widget_set_sensitive (play->media_info, FALSE);

      gst_player_set_uri (play->player, next->data);
      play->current_uri = next;
      gst_player_play (play->player);
      set_title (play, next->data);
    } else {
      GtkWidget *image;

      gst_player_pause (play->player);
      image =
          gtk_image_new_from_icon_name ("media-playback-start",
          GTK_ICON_SIZE_BUTTON);
      gtk_button_set_image (GTK_BUTTON (play->play_pause_button), image);
      play->playing = FALSE;
    }
  }
}

static void
media_info_updated_cb (GstPlayer * player, GstPlayerMediaInfo * media_info,
    GtkPlay * play)
{
  if (!gtk_widget_is_sensitive (play->media_info)) {
    const gchar *title;

    title = gst_player_media_info_get_title (media_info);
    if (title)
      set_title (play, title);

    gtk_widget_set_sensitive (play->media_info, TRUE);
  }
}

int
main (gint argc, gchar ** argv)
{
  GtkPlay play;
  gchar **file_names = NULL;
  GOptionContext *ctx;
  GOptionEntry options[] = {
    {G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &file_names,
        "Files to play"},
    {NULL}
  };
  guint list_length = 0;
  GError *err = NULL;
  GList *l;

  memset (&play, 0, sizeof (play));

  g_set_prgname (APP_NAME);

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
  /* Parse the list of the file names we have to play. */
  if (!file_names) {
    GtkWidget *chooser;
    int res;

    chooser = gtk_file_chooser_dialog_new ("Select files to play", NULL,
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, NULL);
    g_object_set (chooser, "local-only", FALSE, "select-multiple", TRUE, NULL);

    res = gtk_dialog_run (GTK_DIALOG (chooser));
    if (res == GTK_RESPONSE_ACCEPT) {
      GSList *l;

      l = gtk_file_chooser_get_uris (GTK_FILE_CHOOSER (chooser));
      while (l) {
        play.uris = g_list_append (play.uris, l->data);
        l = g_slist_delete_link (l, l);
      }
    } else {
      return 0;
    }
    gtk_widget_destroy (chooser);
  } else {
    guint i;

    list_length = g_strv_length (file_names);
    for (i = 0; i < list_length; i++) {
      play.uris =
          g_list_append (play.uris,
          gst_uri_is_valid (file_names[i]) ?
          g_strdup (file_names[i]) : gst_filename_to_uri (file_names[i], NULL));
    }

    g_strfreev (file_names);
    file_names = NULL;
  }

  play.player = gst_player_new ();
  play.playing = TRUE;

  g_object_set (play.player, "dispatch-to-main-context", TRUE, NULL);

  gst_player_set_uri (play.player, g_list_first (play.uris)->data);

  create_ui (&play);

  if (list_length > 1)
    gtk_widget_set_sensitive (play.next_button, TRUE);

  g_signal_connect (play.player, "position-updated",
      G_CALLBACK (position_updated_cb), &play);
  g_signal_connect (play.player, "duration-changed",
      G_CALLBACK (duration_changed_cb), &play);
  g_signal_connect (play.player, "video-dimensions-changed",
      G_CALLBACK (video_dimensions_changed_cb), &play);
  g_signal_connect (play.player, "end-of-stream", G_CALLBACK (eos_cb), &play);
  g_signal_connect (play.player, "media-info-updated",
      G_CALLBACK (media_info_updated_cb), &play);

  /* We have file(s) that need playing. */
  set_title (&play, g_list_first (play.uris)->data);
  gst_player_play (play.player);
  play.current_uri = g_list_first (play.uris);

  gtk_main ();

  play_clear (&play);

  return 0;
}
