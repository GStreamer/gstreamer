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
#include <gst/tag/tag.h>
#include <gst/video/videooverlay.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

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
  GtkWidget *image_area;
  GtkWidget *volume_button;
  GtkWidget *media_info_button;
  GtkWidget *repeat_button;
  GtkWidget *fullscreen_button;
  GtkWidget *toolbar;
  GdkCursor *default_cursor;
  gulong seekbar_value_changed_signal_id;
  GdkPixbuf *image_pixbuf;
  gboolean playing;
  gboolean loop;
  gboolean fullscreen;
  gint toolbar_hide_timeout;
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

static void display_cover_art (GtkPlay * play, GstPlayerMediaInfo * media_info);

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
    gst_player_play (play->player);
    image =
        gtk_image_new_from_icon_name ("media-playback-pause",
        GTK_ICON_SIZE_BUTTON);
    gtk_button_set_image (GTK_BUTTON (play->play_pause_button), image);
    play->playing = TRUE;
  }
}

static void
play_current_uri (GtkPlay * play, GList * uri)
{
  /* reset the button/widget state to default */
  if (play->image_pixbuf)
    g_object_unref (play->image_pixbuf);
  play->image_pixbuf = NULL;
  gtk_widget_set_sensitive (play->media_info_button, FALSE);
  gtk_range_set_range (GTK_RANGE (play->seekbar), 0, 0);
  gtk_widget_set_sensitive (play->prev_button, g_list_previous (uri) != NULL);
  gtk_widget_set_sensitive (play->next_button, g_list_next (uri) != NULL);

  /* play uri */
  gst_player_set_uri (play->player, uri->data);
  play->current_uri = uri;
  gst_player_play (play->player);
  set_title (play, uri->data);
}

static void
skip_prev_clicked_cb (GtkButton * button, GtkPlay * play)
{
  GList *prev;

  prev = g_list_previous (play->current_uri);
  g_return_if_fail (prev != NULL);

  play_current_uri (play, prev);
}

static GList *
open_file_dialog (GtkPlay *play)
{
  int res;
  GList *uris = NULL;
  GtkWidget *chooser;
  GtkWidget *parent;

  parent = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  chooser = gtk_file_chooser_dialog_new ("Select files to play", NULL,
      GTK_FILE_CHOOSER_ACTION_OPEN,
      "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, NULL);
  g_object_set (chooser, "local-only", FALSE, "select-multiple", TRUE, NULL);
  gtk_window_set_transient_for (GTK_WINDOW (chooser), GTK_WINDOW (parent));

  res = gtk_dialog_run (GTK_DIALOG (chooser));
  if (res == GTK_RESPONSE_ACCEPT) {
    GSList *l;

    l = gtk_file_chooser_get_uris (GTK_FILE_CHOOSER (chooser));
    while (l) {
      uris = g_list_append (uris, l->data);
      l = g_slist_delete_link (l, l);
    }
  }

  gtk_widget_destroy (chooser);
  return uris;
}

static void
open_file_clicked_cb (GtkWidget * unused, GtkPlay *play)
{
  GList * uris, *current;

  uris = open_file_dialog (play);
  if (uris) {
    /* free existing playlist */
    g_list_free_full (play->uris, g_free);

    play->uris = uris;
    play_current_uri (play, g_list_first (play->uris));
  }
}

static void
skip_next_clicked_cb (GtkButton * button, GtkPlay * play)
{
  GList *next;

  next = g_list_next (play->current_uri);
  g_return_if_fail (next != NULL);

  play_current_uri (play, next);
}

static const gchar *
audio_channels_string (gint num)
{
  if (num == 1)
    return "mono";
  else if (num == 2)
    return "stereo";
  else if (num > 2)
    return "surround";
  else
    return "unknown";
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
      if (!gst_player_audio_info_get_language (audio))
        return NULL;
      buffer = g_strdup_printf ("%s%s", label ? "Language : " : "",
          gst_player_audio_info_get_language (audio));
      return buffer;
    }
    case AUDIO_INFO_CHANNELS:
    {
      gchar *buffer;
      GstPlayerAudioInfo *audio = (GstPlayerAudioInfo *) stream;
      buffer = g_strdup_printf ("%s%s", label ? "Channels : " : "",
          audio_channels_string (gst_player_audio_info_get_channels (audio)));
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
      guint par_d, par_n;
      gchar *buffer;
      GstPlayerVideoInfo *video = (GstPlayerVideoInfo *) stream;

      gst_player_video_info_get_pixel_aspect_ratio (video, &par_n, &par_d);
      buffer = g_strdup_printf ("%s%u:%u", label ? "pixel-aspect-ratio : " :
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

static gboolean
toolbar_hide_func (GtkPlay * play)
{
  GdkCursor *cursor;

  /* TODO: add some animation while hiding the toolbar. */
  gtk_widget_hide (play->toolbar);

  /* hide the mouse pointer */
  cursor = gdk_cursor_new_for_display (
      gtk_widget_get_display (play->window), GDK_BLANK_CURSOR);
  gdk_window_set_cursor (gtk_widget_get_window (play->window), cursor);
  g_object_unref (cursor);

  play->toolbar_hide_timeout = 0;
  return FALSE;
}

static void
fullscreen_toggle_cb (GtkToggleButton * widget, GtkPlay * play)
{
  GtkWidget *image;

  if (gtk_toggle_button_get_active (widget)) {
    image = gtk_image_new_from_icon_name ("view-restore",
        GTK_ICON_SIZE_BUTTON);
    gtk_window_fullscreen (GTK_WINDOW(play->window));
    gtk_button_set_image (GTK_BUTTON (play->fullscreen_button), image);

    /* start timer to hide toolbar */
    if (play->toolbar_hide_timeout)
      g_source_remove (play->toolbar_hide_timeout);
    play->toolbar_hide_timeout = g_timeout_add_seconds (5,
        (GSourceFunc) toolbar_hide_func, play);
  }
  else {
    /* if toolbar hide timer is running then kill it */
    if (play->toolbar_hide_timeout) {
      g_source_remove (play->toolbar_hide_timeout);
      play->toolbar_hide_timeout = 0;
    }

    image = gtk_image_new_from_icon_name ("view-fullscreen",
        GTK_ICON_SIZE_BUTTON);
    gtk_window_unfullscreen (GTK_WINDOW(play->window));
    gtk_button_set_image (GTK_BUTTON (play->fullscreen_button), image);
  }
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

static gint
_get_current_track_index (GtkPlay * play, void *(*func) (GstPlayer * player))
{
  void *obj;
  gint index = -1;

  obj = func (play->player);
  if (obj) {
    index = gst_player_stream_info_get_index ((GstPlayerStreamInfo *) obj);
    g_object_unref (obj);
  }

  return index;
}

static gint
get_current_track_index (GtkPlay * play, GType type)
{
  if (type == GST_TYPE_PLAYER_VIDEO_INFO)
    return _get_current_track_index (play,
        (void *) gst_player_get_current_video_track);
  else if (type == GST_TYPE_PLAYER_AUDIO_INFO)
    return _get_current_track_index (play,
        (void *) gst_player_get_current_audio_track);
  else
    return _get_current_track_index (play,
        (void *) gst_player_get_current_subtitle_track);
}

static gchar *
get_menu_label (GstPlayerStreamInfo * stream, GType type)
{
  if (type == GST_TYPE_PLAYER_AUDIO_INFO) {
    gchar *label = NULL;
    gchar *lang, *codec, *channels;

    /* label format: <codec_name> <channel> [language] */
    lang = stream_info_get_string (stream, AUDIO_INFO_LANGUAGE, FALSE);
    codec = stream_info_get_string (stream, AUDIO_INFO_CODEC, FALSE);
    channels = stream_info_get_string (stream, AUDIO_INFO_CHANNELS, FALSE);

    if (lang) {
      label = g_strdup_printf ("%s %s [%s]", codec ? codec : "",
          channels ? channels : "", lang);
      g_free (lang);
    } else
      label = g_strdup_printf ("%s %s", codec ? codec : "",
          channels ? channels : "");

    g_free (codec);
    g_free (channels);
    return label;
  } else if (type == GST_TYPE_PLAYER_VIDEO_INFO) {
    /* label format: <codec_name> */
    return stream_info_get_string (stream, VIDEO_INFO_CODEC, FALSE);
  } else {
    /* label format: <langauge> */
    return stream_info_get_string (stream, SUBTITLE_INFO_LANGUAGE, FALSE);
  }

  return NULL;
}

static void
disable_track (GtkPlay * play, GType type)
{
  if (type == GST_TYPE_PLAYER_VIDEO_INFO) {
    gst_player_set_video_track_enabled (play->player, FALSE);
    display_cover_art (play, NULL);  /* display cover art */
  }
  else if (type == GST_TYPE_PLAYER_AUDIO_INFO)
    gst_player_set_audio_track_enabled (play->player, FALSE);
  else
    gst_player_set_subtitle_track_enabled (play->player, FALSE);
}

static void
change_track (GtkPlay * play, gint index, GType type)
{
  if (type == GST_TYPE_PLAYER_VIDEO_INFO) {
    gst_player_set_video_track (play->player, index);
    gst_player_set_video_track_enabled (play->player, TRUE);
    /* if video area widget is not visible then make it visible */
    if (!gtk_widget_is_visible (play->video_area)) {
      gtk_widget_hide (play->image_area);
      gtk_widget_show (play->video_area);
    }
  } else if (type == GST_TYPE_PLAYER_AUDIO_INFO) {
    gst_player_set_audio_track (play->player, index);
    gst_player_set_audio_track_enabled (play->player, TRUE);
  } else {
    gst_player_set_subtitle_track (play->player, index);
    gst_player_set_subtitle_track_enabled (play->player, TRUE);
  }
}

static void
track_changed_cb (GtkWidget * widget, GtkPlay * play)
{
  GType type;
  gint index;

  /* check if button is toggled */
  if (!gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (widget)))
    return;

  index = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (widget), "index"));
  type = GPOINTER_TO_SIZE (g_object_get_data (G_OBJECT (widget), "type"));

  if (index == -1)
    disable_track (play, type);
  else
    change_track (play, index, type);
}

static GtkWidget *
create_tracks_menu (GtkPlay * play, GstPlayerMediaInfo * media_info, GType type)
{
  GtkWidget *menu;
  GtkWidget *item;
  GList *list, *l;
  gint current_index;
  GSList *group = NULL;

  if (!media_info)
    return NULL;

  current_index = get_current_track_index (play, type);

  if (type == GST_TYPE_PLAYER_VIDEO_INFO)
    list = gst_player_get_video_streams (media_info);
  else if (type == GST_TYPE_PLAYER_AUDIO_INFO)
    list = gst_player_get_audio_streams (media_info);
  else
    list = gst_player_get_subtitle_streams (media_info);

  menu = gtk_menu_new ();

  for (l = list; l != NULL; l = l->next) {
    gint index;
    gchar *buffer;
    GstPlayerStreamInfo *s = (GstPlayerStreamInfo *) l->data;

    buffer = get_menu_label (s, type);
    item = gtk_radio_menu_item_new_with_label (group, buffer);
    group = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (item));
    index = gst_player_stream_info_get_index (s);
    g_object_set_data (G_OBJECT (item), "index", GINT_TO_POINTER (index));
    g_object_set_data (G_OBJECT (item), "type", GSIZE_TO_POINTER (type));
    if (current_index == index)
      gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (item), True);
    g_free (buffer);
    g_signal_connect (G_OBJECT (item), "toggled",
        G_CALLBACK (track_changed_cb), play);
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
  }
  item = gtk_radio_menu_item_new_with_label (group, "Disable");
  group = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (item));
  g_object_set_data (G_OBJECT (item), "index", GINT_TO_POINTER (-1));
  g_object_set_data (G_OBJECT (item), "type", GSIZE_TO_POINTER (type));
  if (current_index == -1)
    gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (item), True);
  g_signal_connect (G_OBJECT (item), "toggled",
      G_CALLBACK (track_changed_cb), play);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
  return menu;
}

static void
player_quit_clicked_cb (GtkButton * button, GtkPlay * play)
{
  gst_player_stop (play->player);
  gtk_main_quit ();
}

static void
gtk_player_popup_menu_create (GtkPlay * play, GdkEventButton * event)
{
  GtkWidget *menu;
  GtkWidget *info;
  GtkWidget *audio;
  GtkWidget *video;
  GtkWidget *sub;
  GtkWidget *quit;
  GtkWidget *next;
  GtkWidget *prev;
  GtkWidget *open;
  GtkWidget *image;
  GtkWidget *submenu;
  GstPlayerMediaInfo *media_info;

  menu = gtk_menu_new ();
  info = gtk_menu_item_new_with_label ("Media Information");
  audio = gtk_menu_item_new_with_label ("Audio");
  video = gtk_menu_item_new_with_label ("Video");
  sub = gtk_menu_item_new_with_label ("Subtitle");
  open = gtk_menu_item_new_with_label ("Open");
  next = gtk_menu_item_new_with_label ("Next");
  prev = gtk_menu_item_new_with_label ("Prev");
  quit = gtk_menu_item_new_with_label ("Quit");

  media_info = gst_player_get_media_info (play->player);

  if (media_info && !gst_player_get_video_streams (media_info))
    gtk_widget_set_sensitive (video, FALSE);
  else {
    submenu = create_tracks_menu (play, media_info, GST_TYPE_PLAYER_VIDEO_INFO);
    if (submenu)
      gtk_menu_item_set_submenu (GTK_MENU_ITEM (video), submenu);
    else
      gtk_widget_set_sensitive (video, FALSE);
  }

  if (media_info && !gst_player_get_audio_streams (media_info))
    gtk_widget_set_sensitive (audio, FALSE);
  else {
    submenu = create_tracks_menu (play, media_info, GST_TYPE_PLAYER_AUDIO_INFO);
    if (submenu)
      gtk_menu_item_set_submenu (GTK_MENU_ITEM (audio), submenu);
    else
      gtk_widget_set_sensitive (audio, FALSE);
  }

  if (media_info && !gst_player_get_subtitle_streams (media_info))
    gtk_widget_set_sensitive (sub, FALSE);
  else {
    submenu = create_tracks_menu (play, media_info,
        GST_TYPE_PLAYER_SUBTITLE_INFO);
    if (submenu)
      gtk_menu_item_set_submenu (GTK_MENU_ITEM (sub), submenu);
    else
      gtk_widget_set_sensitive (sub, FALSE);
  }

  gtk_widget_set_sensitive (next, g_list_next
      (play->current_uri) ? TRUE : FALSE);
  gtk_widget_set_sensitive (prev, g_list_previous
      (play->current_uri) ? TRUE : FALSE);
  gtk_widget_set_sensitive (info, media_info ? TRUE : FALSE);

  g_signal_connect (G_OBJECT (open), "activate",
      G_CALLBACK (open_file_clicked_cb), play);
  g_signal_connect (G_OBJECT (next), "activate",
      G_CALLBACK (skip_next_clicked_cb), play);
  g_signal_connect (G_OBJECT (prev), "activate",
      G_CALLBACK (skip_prev_clicked_cb), play);
  g_signal_connect (G_OBJECT (info), "activate",
      G_CALLBACK (media_info_clicked_cb), play);
  g_signal_connect (G_OBJECT (quit), "activate",
      G_CALLBACK (player_quit_clicked_cb), play);

  gtk_menu_shell_append (GTK_MENU_SHELL (menu), open);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), next);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), prev);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), video);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), audio);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), sub);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), info);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), quit);

  gtk_widget_show_all (menu);
  gtk_menu_popup (GTK_MENU (menu), NULL, NULL, NULL, NULL,
      (event != NULL) ? event->button : 0,
      gdk_event_get_time ((GdkEvent *) event));

  if (media_info)
    g_object_unref (media_info);
}

static void
mouse_button_pressed_cb (GtkWidget * unused, GdkEventButton * event,
    GtkPlay * play)
{
  if (event->type == GDK_2BUTTON_PRESS) {
    /* toggle fullscreen on double button click */
    if (gtk_toggle_button_get_active
        (GTK_TOGGLE_BUTTON (play->fullscreen_button)))
      gtk_toggle_button_set_active
        (GTK_TOGGLE_BUTTON (play->fullscreen_button), FALSE);
    else
      gtk_toggle_button_set_active
        (GTK_TOGGLE_BUTTON (play->fullscreen_button), TRUE);
  } else if ((event->type == GDK_BUTTON_PRESS) && (event->button == 3)) {
    /* popup menu on right button click */
    gtk_player_popup_menu_create (play, event);
  }
}

static gboolean
image_area_draw_cb (GtkWidget * widget, cairo_t * cr, GtkPlay * play)
{
  if (play->image_pixbuf) {
    gint width, height;
    gint pix_width, pix_height;
    gint x = 0, y = 0;
    gdouble scalex = 0.0, scaley = 0.0;

    width = gtk_widget_get_allocated_width (widget);
    height = gtk_widget_get_allocated_height (widget);
    pix_width = gdk_pixbuf_get_width (play->image_pixbuf);
    pix_height = gdk_pixbuf_get_height (play->image_pixbuf);

    /* if image is bigger than widget then scale down otherwise center it. */
    if (width <= pix_width)
      scalex = (gdouble)width / (gdouble)pix_width;
    else
      x = (width - pix_width) / 2;
    if (height <= pix_height)
      scaley = (gdouble)height / (gdouble)pix_height;
    else
      y = (height - pix_height) / 2;

    /* fill background with black */
    cairo_set_source_rgb (cr, 0.0, 0.0, 0.0);
    cairo_rectangle (cr, 0, 0, width, height);
    cairo_fill (cr);

    if (scalex > 0.0 && scaley > 0.0)
       cairo_scale (cr, scalex, scaley);

    gdk_cairo_set_source_pixbuf(cr, play->image_pixbuf, x, y);
    cairo_paint (cr);
  } else {
    /* fill background with black */
    cairo_set_source_rgb (cr, 0.0, 0.0, 0.0);
    cairo_paint (cr);
  }

  return FALSE;
}

static gboolean
gtk_show_toolbar_cb (GtkWidget * widget, GdkEvent * event, GtkPlay * play)
{
  if (gtk_toggle_button_get_active
      (GTK_TOGGLE_BUTTON (play->fullscreen_button))) {

    GdkCursor *cursor;

    /* if timer is running then kill it */
    if (play->toolbar_hide_timeout) {
      g_source_remove (play->toolbar_hide_timeout);
      play->toolbar_hide_timeout = 0;
    }

    /* show mouse pointer */
    gdk_window_set_cursor (gtk_widget_get_window (play->window),
        play->default_cursor);

    gtk_widget_show (play->toolbar);
    play->toolbar_hide_timeout = g_timeout_add_seconds (5,
        (GSourceFunc) toolbar_hide_func, play);
  }

  return TRUE;
}

static void
create_ui (GtkPlay * play)
{
  GtkWidget *image;
  GtkWidget *controls, *main_hbox, *main_vbox;

  play->window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  g_signal_connect (G_OBJECT (play->window), "delete-event",
      G_CALLBACK (delete_event_cb), play);
  set_title (play, APP_NAME);

  play->video_area = gtk_drawing_area_new ();
  g_signal_connect (play->video_area, "realize",
      G_CALLBACK (video_area_realize_cb), play);
  g_signal_connect (play->video_area, "button-press-event",
      G_CALLBACK (mouse_button_pressed_cb), play);
  g_signal_connect (play->video_area, "motion-notify-event",
      G_CALLBACK (gtk_show_toolbar_cb), play);
  g_signal_connect (play->video_area, "scroll-event",
      G_CALLBACK (gtk_show_toolbar_cb), play);
  gtk_widget_set_events (play->video_area, GDK_EXPOSURE_MASK
      | GDK_LEAVE_NOTIFY_MASK
      | GDK_BUTTON_PRESS_MASK
      | GDK_POINTER_MOTION_MASK
      | GDK_POINTER_MOTION_HINT_MASK
      | GDK_ENTER_NOTIFY_MASK);

  play->image_area = gtk_drawing_area_new ();
  g_signal_connect (play->image_area, "button-press-event",
      G_CALLBACK (mouse_button_pressed_cb), play);
  g_signal_connect (play->image_area, "draw",
      G_CALLBACK (image_area_draw_cb), play);
  g_signal_connect (play->image_area, "motion-notify-event",
      G_CALLBACK (gtk_show_toolbar_cb), play);
  g_signal_connect (play->image_area, "scroll-event",
      G_CALLBACK (gtk_show_toolbar_cb), play);
  gtk_widget_set_events (play->image_area, GDK_EXPOSURE_MASK
        | GDK_LEAVE_NOTIFY_MASK
        | GDK_BUTTON_PRESS_MASK
        | GDK_POINTER_MOTION_MASK
        | GDK_POINTER_MOTION_HINT_MASK
        | GDK_ENTER_NOTIFY_MASK);

  /* Unified play/pause button */
  play->play_pause_button =
      gtk_button_new_from_icon_name ("media-playback-pause",
      GTK_ICON_SIZE_BUTTON);
  g_signal_connect (G_OBJECT (play->play_pause_button), "clicked",
      G_CALLBACK (play_pause_clicked_cb), play);

  play->seekbar =
      gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
  gtk_scale_set_draw_value (GTK_SCALE (play->seekbar), 0);
  gtk_range_set_range (GTK_RANGE (play->seekbar), 0, 0);
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

  /* Playlist repeat button */
  play->repeat_button = gtk_toggle_button_new ();
  image = gtk_image_new_from_icon_name ("media-playlist-repeat",
            GTK_ICON_SIZE_BUTTON);
  gtk_button_set_image (GTK_BUTTON (play->repeat_button), image);
  if (play->loop)
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (play->repeat_button),
        TRUE);

  /* Volume control button */
  play->volume_button = gtk_volume_button_new ();
  gtk_scale_button_set_value (GTK_SCALE_BUTTON (play->volume_button),
      gst_player_get_volume (play->player));
  g_signal_connect (G_OBJECT (play->volume_button), "value-changed",
      G_CALLBACK (volume_changed_cb), play);

  /* media information button */
  play->media_info_button = gtk_button_new_from_icon_name ("dialog-information",
      GTK_ICON_SIZE_BUTTON);
  g_signal_connect (G_OBJECT (play->media_info_button), "clicked",
      G_CALLBACK (media_info_clicked_cb), play);
  gtk_widget_set_sensitive (play->media_info_button, FALSE);

  /* Full screen button */
  play->fullscreen_button = gtk_toggle_button_new ();
  image = gtk_image_new_from_icon_name ("view-fullscreen",
      GTK_ICON_SIZE_BUTTON);
  gtk_button_set_image (GTK_BUTTON (play->fullscreen_button), image);
  g_signal_connect (G_OBJECT (play->fullscreen_button), "toggled",
      G_CALLBACK (fullscreen_toggle_cb), play);
  if (play->fullscreen)
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (play->fullscreen_button),
        TRUE);

  play->toolbar = controls = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_box_pack_start (GTK_BOX (controls), play->prev_button, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (controls), play->play_pause_button, FALSE,
      FALSE, 2);
  gtk_box_pack_start (GTK_BOX (controls), play->next_button, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (controls), play->repeat_button,
      FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (controls), play->seekbar, TRUE, TRUE, 2);
  gtk_box_pack_start (GTK_BOX (controls), play->volume_button, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (controls), play->media_info_button,
      FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (controls), play->fullscreen_button,
      FALSE, FALSE, 2);

  main_hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_box_pack_start (GTK_BOX (main_hbox), play->video_area, TRUE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (main_hbox), play->image_area, TRUE, TRUE, 0);

  main_vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_pack_start (GTK_BOX (main_vbox), main_hbox, TRUE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (main_vbox), controls, FALSE, FALSE, 0);
  gtk_container_add (GTK_CONTAINER (play->window), main_vbox);

  gtk_widget_realize (play->video_area);
  gtk_widget_hide (play->video_area);

  gtk_widget_show_all (play->window);

  play->default_cursor = gdk_window_get_cursor
    (gtk_widget_get_window (play->toolbar));
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
eos_cb (GstPlayer * unused, GtkPlay * play)
{
  if (play->playing) {
    GList *next = NULL;
    gchar *uri;

    next = g_list_next (play->current_uri);
    if (!next && gtk_toggle_button_get_active
          (GTK_TOGGLE_BUTTON(play->repeat_button)))
      next = g_list_first (play->uris);

    if (next) {
      play_current_uri (play, next);
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

static gboolean
_has_active_stream (GtkPlay * play, void * (*func) (GstPlayer * player))
{
  void *obj;

  obj = func (play->player);
  if (obj) {
    g_object_unref (obj);
    return TRUE;
  }

  return FALSE;
}

static GdkPixbuf *
gst_sample_to_pixbuf (GtkPlay * play, GstSample * sample)
{
  GdkPixbufLoader *loader;
  GdkPixbuf *pixbuf = NULL;
  GError *err = NULL;
  GstMapInfo info;
  GstBuffer *buffer;
  const GstStructure *caps_struct;
  GstTagImageType type = GST_TAG_IMAGE_TYPE_UNDEFINED;

  buffer = gst_sample_get_buffer (sample);
  caps_struct = gst_sample_get_info (sample);

  /* if sample is retrieved from preview-image tag then caps struct
   * will not be defined. */
  if (caps_struct)
    gst_structure_get_enum (caps_struct, "image-type",
        GST_TYPE_TAG_IMAGE_TYPE, &type);

  /* FIXME: Should we check more type ?? */
  if ((type != GST_TAG_IMAGE_TYPE_FRONT_COVER) &&
      (type != GST_TAG_IMAGE_TYPE_UNDEFINED) &&
      (type != GST_TAG_IMAGE_TYPE_NONE)) {
    g_print ("unsupport type ... %d \n", type);
    return NULL;
  }

  if (!gst_buffer_map (buffer, &info, GST_MAP_READ)) {
    g_print ("failed to map gst buffer \n");
    return NULL;
  }

  loader = gdk_pixbuf_loader_new ();
  if (gdk_pixbuf_loader_write (loader, info.data, info.size, &err) &&
      gdk_pixbuf_loader_close (loader, &err)) {
    pixbuf = gdk_pixbuf_loader_get_pixbuf (loader);
    if (pixbuf) {
      g_object_ref (pixbuf);
    }
    else {
      g_print ("failed to convert gst buffer to pixbuf %s \n", err->message);
      g_error_free (err);
    }
  }

  g_object_unref (loader);
  gst_buffer_unmap (buffer, &info);

  return pixbuf;
}

static void
display_cover_art (GtkPlay * play, GstPlayerMediaInfo * media_info)
{
  GstSample *sample;
  GstPlayerMediaInfo *temp_media_info = NULL;

  /* hide the video widget and show image widget */
  gtk_widget_hide (play->video_area);
  gtk_widget_show (play->image_area);

  /* if media information is not passed then get it from player */
  if (!media_info)
    temp_media_info = media_info = gst_player_get_media_info (play->player);

  sample = gst_player_media_info_get_image_sample (media_info);
  if (!sample)
    goto cleanup;

  if (play->image_pixbuf)
    g_object_unref (play->image_pixbuf);

  play->image_pixbuf = gst_sample_to_pixbuf (play, sample);

cleanup:
  gtk_widget_queue_draw (play->image_area); /* send expose event to widget */

  if (temp_media_info)
    g_object_unref (temp_media_info);
}

static gboolean
has_active_stream (GtkPlay * play, GType type)
{
  if (type == GST_TYPE_PLAYER_VIDEO_INFO)
    return _has_active_stream (play,
        (void *) gst_player_get_current_video_track);
  else if (type == GST_TYPE_PLAYER_AUDIO_INFO)
    return _has_active_stream (play,
        (void *) gst_player_get_current_audio_track);
  else
    return _has_active_stream (play,
        (void *) gst_player_get_current_subtitle_track);
}

static void
media_info_updated_cb (GstPlayer * player, GstPlayerMediaInfo * media_info,
    GtkPlay * play)
{
  if (!gtk_widget_is_sensitive (play->media_info_button)) {
    const gchar *title;

    title = gst_player_media_info_get_title (media_info);
    if (title)
      set_title (play, title);

    gtk_widget_set_sensitive (play->media_info_button, TRUE);

    /* if we have active video stream then hide image widget
     * and show video widget otherwise show the cover art.
     */
    if (has_active_stream (play, GST_TYPE_PLAYER_VIDEO_INFO)) {
      gtk_widget_hide (play->image_area);
      gtk_widget_show (play->video_area);
    } else {
      display_cover_art (play, media_info);
    }
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
    {"loop", 'l', 0, G_OPTION_ARG_NONE, &play.loop, "Repeat all"},
    {"fullscreen", 'f', 0, G_OPTION_ARG_NONE, &play.fullscreen,
      "Show the player in fullscreen"},
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
    play.uris = open_file_dialog (&play);
    if (!play.uris)
      return 0;
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

  create_ui (&play);

  play_current_uri (&play, g_list_first (play.uris));

  g_signal_connect (play.player, "position-updated",
      G_CALLBACK (position_updated_cb), &play);
  g_signal_connect (play.player, "duration-changed",
      G_CALLBACK (duration_changed_cb), &play);
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
