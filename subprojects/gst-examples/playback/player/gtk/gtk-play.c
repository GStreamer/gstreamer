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
#include <math.h>

#include <gst/gst.h>
#include <gst/tag/tag.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#include <gtk/gtk.h>

#if defined (GDK_WINDOWING_X11)
#include <gdk/gdkx.h>
#elif defined (GDK_WINDOWING_WIN32)
#include <gdk/gdkwin32.h>
#elif defined (GDK_WINDOWING_QUARTZ)
#include <gdk/gdkquartz.h>
#if GTK_CHECK_VERSION(3, 24, 10)
#include <AppKit/AppKit.h>
NSView *gdk_quartz_window_get_nsview (GdkWindow * window);
#endif
#endif

#include <gst/play/play.h>
#include "gtk-video-renderer.h"

#define APP_NAME "gtk-play"

#define TOOLBAR_GET_OBJECT(x) \
  (GtkWidget *)gtk_builder_get_object (play->toolbar_ui, #x)

#define TOOLBAR_GET_LABEL(x) \
  (GtkLabel *) gtk_builder_get_object (play->toolbar_ui, #x)

typedef GtkApplication GtkPlayApp;
typedef GtkApplicationClass GtkPlayAppClass;

GType gtk_play_app_get_type (void);
G_DEFINE_TYPE (GtkPlayApp, gtk_play_app, GTK_TYPE_APPLICATION);

typedef struct
{
  GtkApplicationWindow parent;

  GstPlay *player;
  GstPlaySignalAdapter *signal_adapter;
  GstPlayVideoRenderer *renderer;

  GList *uris;
  GList *current_uri;

  guint inhibit_cookie;

  GtkWidget *play_pause_button;
  GtkWidget *prev_button, *next_button;
  GtkWidget *seekbar;
  GtkWidget *video_area;
  GtkWidget *volume_button;
  GtkWidget *fullscreen_button;
  GtkWidget *toolbar;
  GtkWidget *toolbar_overlay;
  GtkWidget *media_info_dialog;
  GtkLabel *title_label;
  GtkLabel *elapshed_label;
  GtkLabel *remain_label;
  GtkLabel *rate_label;
  GdkCursor *default_cursor;
  gboolean playing;
  gboolean loop;
  gboolean fullscreen;
  gint toolbar_hide_timeout;

  GtkBuilder *toolbar_ui;
} GtkPlay;

typedef GtkApplicationWindowClass GtkPlayClass;

GType gtk_play_get_type (void);
G_DEFINE_TYPE (GtkPlay, gtk_play, GTK_TYPE_APPLICATION_WINDOW);

/* *INDENT-OFF* */
G_MODULE_EXPORT
void rewind_button_clicked_cb (GtkButton * button, GtkPlay * play);
G_MODULE_EXPORT
void forward_button_clicked_cb (GtkButton * button, GtkPlay * play);
G_MODULE_EXPORT
void play_pause_button_clicked_cb (GtkButton * button, GtkPlay * play);
G_MODULE_EXPORT
void prev_button_clicked_cb (GtkButton * button, GtkPlay * play);
G_MODULE_EXPORT
void next_button_clicked_cb (GtkButton * button, GtkPlay * play);
G_MODULE_EXPORT
void media_info_dialog_button_clicked_cb (GtkButton * button, GtkPlay * play);
G_MODULE_EXPORT
void fullscreen_button_toggled_cb (GtkToggleButton * widget, GtkPlay * play);
G_MODULE_EXPORT
void seekbar_value_changed_cb (GtkRange * range, GtkPlay * play);
G_MODULE_EXPORT
void volume_button_value_changed_cb (GtkScaleButton * button, gdouble value,
    GtkPlay * play);
/* *INDENT-ON* */

enum
{
  PROP_0,
  PROP_LOOP,
  PROP_FULLSCREEN,
  PROP_URIS,

  LAST_PROP
};

static GParamSpec *gtk_play_properties[LAST_PROP] = { NULL, };

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
    gtk_window_set_title (GTK_WINDOW (play), APP_NAME);
  } else {
    gtk_window_set_title (GTK_WINDOW (play), title);
  }
}

static GtkBuilder *
load_from_builder (const gchar * filename, gboolean register_sig_handler,
    GtkPlay * play)
{
  GtkBuilder *builder;

  builder = gtk_builder_new_from_resource (filename);
  if (builder == NULL) {
    gst_print ("ERROR: failed to load %s \n", filename);
    return NULL;
  }

  if (register_sig_handler)
    gtk_builder_connect_signals (builder, play);

  return builder;
}


static void
delete_event_cb (GtkWidget * widget, GdkEvent * event, GtkPlay * play)
{
  gtk_widget_destroy (GTK_WIDGET (play));
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
  window_handle = (guintptr) gdk_quartz_window_get_nsview (window);
#elif defined (GDK_WINDOWING_X11)
  window_handle = GDK_WINDOW_XID (window);
#endif
  g_object_set (play->renderer, "window-handle", (gpointer) window_handle,
      NULL);
}

static void
gtk_play_set_rate (GtkPlay * play, gdouble step)
{
  gdouble val;

  val = gst_play_get_rate (play->player);
  val += step;
  if (val == 0.0)
    val = step;
  gst_play_set_rate (play->player, val);

  if (val == 1.0)
    gtk_label_set_label (play->rate_label, NULL);
  else {
    gchar *data;

    data = g_strdup_printf ("%.2fx", val);
    gtk_label_set_label (play->rate_label, data);
    g_free (data);
  }
}

static inline void
seekbar_add_delta (GtkPlay * play, gint delta_sec)
{
  gdouble value = gtk_range_get_value (GTK_RANGE (play->seekbar));
  gtk_range_set_value (GTK_RANGE (play->seekbar), value + delta_sec);
}

/* this mapping follow the mplayer key-bindings */
static gboolean
key_press_event_cb (GtkWidget * widget, GdkEventKey * event, gpointer data)
{
  GtkPlay *play = (GtkPlay *) widget;

  if (event->state != 0 &&
      ((event->state & GDK_CONTROL_MASK) || (event->state & GDK_MOD1_MASK) ||
          (event->state & GDK_MOD3_MASK) || (event->state & GDK_MOD4_MASK)))
    return FALSE;

  if (event->type != GDK_KEY_PRESS)
    return FALSE;

  switch (event->keyval) {
    case GDK_KEY_KP_Right:
    case GDK_KEY_Right:{
      /* seek forward 10 seconds */
      seekbar_add_delta (play, 10);
      break;
    }
    case GDK_KEY_KP_Left:
    case GDK_KEY_Left:{
      /* seek backward 10 seconds */
      seekbar_add_delta (play, -10);
      break;
    }
    case GDK_KEY_KP_Up:
    case GDK_KEY_Up:{
      /* seek forward 1 minute */
      seekbar_add_delta (play, 60);
      break;
    }
    case GDK_KEY_KP_Down:
    case GDK_KEY_Down:{
      /* seek backward 1 minute */
      seekbar_add_delta (play, -60);
      break;
    }
    case GDK_KEY_KP_Page_Up:
    case GDK_KEY_Page_Up:{
      /* Seek forward 10 minutes */
      seekbar_add_delta (play, 600);
      break;
    }
    case GDK_KEY_KP_Page_Down:
    case GDK_KEY_Page_Down:{
      /* Seek backward 10 minutes */
      seekbar_add_delta (play, -600);
      break;
    }
    case GDK_KEY_bracketleft:{
      /* Decrease current playback speed by 10% */
      gtk_play_set_rate (play, -0.1);
      break;
    }
    case GDK_KEY_bracketright:{
      /* Increase current playback speed by 10% */
      gtk_play_set_rate (play, 0.1);
      break;
      break;
    }
    case GDK_KEY_braceleft:{
      /* Decrease current playback speed by 10% */
      gtk_play_set_rate (play, -1.0);
      break;
    }
    case GDK_KEY_braceright:{
      /* Increase current playback speed by 10% */
      gtk_play_set_rate (play, 1.0);
      break;
    }
    case GDK_KEY_BackSpace:{
      /* Reset playback speed to normal */
      gdouble val = gst_play_get_rate (play->player);
      gtk_play_set_rate (play, 1.0 - val);
      break;
    }
    case GDK_KEY_less:{
      /* Go backward in the playlist */
      if (g_list_previous (play->current_uri))
        gtk_button_clicked (GTK_BUTTON (play->prev_button));
      break;
    }
    case GDK_KEY_Return:
    case GDK_KEY_greater:{
      /* Go forward in the playlist */
      if (g_list_next (play->current_uri))
        gtk_button_clicked (GTK_BUTTON (play->next_button));
      break;
    }
    case GDK_KEY_KP_9:
    case GDK_KEY_9:{
      /* Increase volume */
      gdouble volume = gst_play_get_volume (play->player);
      gtk_scale_button_set_value (GTK_SCALE_BUTTON (play->volume_button),
          volume * 1.10);
      break;
    }
    case GDK_KEY_KP_0:
    case GDK_KEY_0:{
      /* Decrease volume */
      gdouble volume = gst_play_get_volume (play->player);
      gtk_scale_button_set_value (GTK_SCALE_BUTTON (play->volume_button),
          volume * 0.9);
      break;
    }
    case GDK_KEY_m:{
      /* Mute sound */
      gboolean mute = gst_play_get_mute (play->player);
      gst_play_set_mute (play->player, !mute);
      break;
    }
    case GDK_KEY_f:{
      /* Toggle fullscreen */
      GtkToggleButton *fs = GTK_TOGGLE_BUTTON (play->fullscreen_button);
      gboolean active = !gtk_toggle_button_get_active (fs);
      gtk_toggle_button_set_active (fs, active);
      break;
    }
    case GDK_KEY_p:
    case GDK_KEY_space:
      /* toggle pause/play */
      gtk_button_clicked (GTK_BUTTON (play->play_pause_button));
      break;
    case GDK_KEY_q:
    case GDK_KEY_Escape:
    default:
      break;
  }

  return FALSE;
}

G_MODULE_EXPORT void
rewind_button_clicked_cb (GtkButton * button, GtkPlay * play)
{
  gtk_play_set_rate (play, -0.5);
}

G_MODULE_EXPORT void
forward_button_clicked_cb (GtkButton * button, GtkPlay * play)
{
  gtk_play_set_rate (play, 0.5);
}

G_MODULE_EXPORT void
play_pause_button_clicked_cb (GtkButton * button, GtkPlay * play)
{
  GtkWidget *image;

  if (play->playing) {
    gst_play_pause (play->player);
    image = TOOLBAR_GET_OBJECT (play_image);
    gtk_button_set_image (GTK_BUTTON (play->play_pause_button), image);
    play->playing = FALSE;

    if (play->inhibit_cookie)
      gtk_application_uninhibit (GTK_APPLICATION (g_application_get_default ()),
          play->inhibit_cookie);
    play->inhibit_cookie = 0;
  } else {
    if (play->inhibit_cookie)
      gtk_application_uninhibit (GTK_APPLICATION (g_application_get_default ()),
          play->inhibit_cookie);
    play->inhibit_cookie =
        gtk_application_inhibit (GTK_APPLICATION (g_application_get_default ()),
        GTK_WINDOW (play), GTK_APPLICATION_INHIBIT_IDLE, "Playing media");

    gst_play_play (play->player);
    image = TOOLBAR_GET_OBJECT (pause_image);
    gtk_button_set_image (GTK_BUTTON (play->play_pause_button), image);
    play->playing = TRUE;
  }
}

static void
play_current_uri (GtkPlay * play, GList * uri, const gchar * ext_suburi)
{
  /* reset the button/widget state to default */
  g_signal_handlers_block_by_func (play->seekbar,
      seekbar_value_changed_cb, play);
  gtk_range_set_range (GTK_RANGE (play->seekbar), 0, 0);
  g_signal_handlers_unblock_by_func (play->seekbar,
      seekbar_value_changed_cb, play);
  gtk_widget_set_sensitive (play->prev_button, g_list_previous (uri) != NULL);
  gtk_widget_set_sensitive (play->next_button, g_list_next (uri) != NULL);
  gtk_label_set_label (play->rate_label, NULL);

  /* set uri or suburi */
  if (ext_suburi)
    gst_play_set_subtitle_uri (play->player, ext_suburi);
  else
    gst_play_set_uri (play->player, uri->data);
  play->current_uri = uri;
  if (play->playing) {
    if (play->inhibit_cookie)
      gtk_application_uninhibit (GTK_APPLICATION (g_application_get_default ()),
          play->inhibit_cookie);
    play->inhibit_cookie =
        gtk_application_inhibit (GTK_APPLICATION (g_application_get_default ()),
        GTK_WINDOW (play), GTK_APPLICATION_INHIBIT_IDLE, "Playing media");
    gst_play_play (play->player);
  } else {
    gst_play_pause (play->player);
    if (play->inhibit_cookie)
      gtk_application_uninhibit (GTK_APPLICATION (g_application_get_default ()),
          play->inhibit_cookie);
    play->inhibit_cookie = 0;
  }
  set_title (play, uri->data);
}

G_MODULE_EXPORT void
prev_button_clicked_cb (GtkButton * button, GtkPlay * play)
{
  GList *prev;

  prev = g_list_previous (play->current_uri);
  g_return_if_fail (prev != NULL);

  play_current_uri (play, prev, NULL);
}

static gboolean
color_balance_channel_change_value_cb (GtkRange * range, GtkScrollType scroll,
    gdouble value, GtkPlay * play)
{
  GstPlayColorBalanceType type;

  type = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (range), "type"));

  value = CLAMP (value, 0.0, 1.0);
  gst_play_set_color_balance (play->player, type, value);

  return FALSE;
}

static gboolean
color_balance_channel_button_press_cb (GtkWidget * widget,
    GdkEventButton * event, GtkPlay * play)
{
  GstPlayColorBalanceType type;

  if (event->type != GDK_2BUTTON_PRESS)
    return FALSE;

  type = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (widget), "type"));
  gtk_range_set_value (GTK_RANGE (widget), 0.5);
  gst_play_set_color_balance (play->player, type, 0.5);

  return FALSE;
}

static void
color_balance_dialog (GtkPlay * play)
{
  GtkWidget *dialog;
  GtkWidget *content;
  GtkWidget *box;
  GtkWidget *ctlbox;
  GtkWidget *label;
  GtkWidget *scale;
  gdouble value;
  guint i;

  dialog = gtk_dialog_new_with_buttons ("Color Balance", GTK_WINDOW (play),
      GTK_DIALOG_DESTROY_WITH_PARENT, "_Close", GTK_RESPONSE_CLOSE, NULL);
  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (play));

  content = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
  box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 2);
  gtk_box_set_homogeneous (GTK_BOX (box), TRUE);
  gtk_box_pack_start (GTK_BOX (content), box, TRUE, TRUE, 5);

  for (i = GST_PLAY_COLOR_BALANCE_BRIGHTNESS;
      i <= GST_PLAY_COLOR_BALANCE_HUE; i++) {
    ctlbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    label = gtk_label_new (gst_play_color_balance_type_get_name (i));
    scale = gtk_scale_new_with_range (GTK_ORIENTATION_VERTICAL, 0, 1, 0.5);
    gtk_widget_set_size_request (scale, 0, 200);
    gtk_box_pack_start (GTK_BOX (ctlbox), label, FALSE, TRUE, 2);
    gtk_box_pack_end (GTK_BOX (ctlbox), scale, TRUE, TRUE, 2);

    gtk_box_pack_end (GTK_BOX (box), ctlbox, TRUE, TRUE, 2);

    value = gst_play_get_color_balance (play->player, i);
    gtk_range_set_value (GTK_RANGE (scale), value);
    g_object_set_data (G_OBJECT (scale), "type", GUINT_TO_POINTER (i));

    g_signal_connect (scale, "change-value",
        G_CALLBACK (color_balance_channel_change_value_cb), play);
    g_signal_connect (scale, "button-press-event",
        G_CALLBACK (color_balance_channel_button_press_cb), play);
  }

  gtk_widget_show_all (dialog);

  gtk_dialog_run (GTK_DIALOG (dialog));

  gtk_widget_destroy (dialog);
}

static void
color_balance_clicked_cb (GtkWidget * unused, GtkPlay * play)
{
  if (gst_play_has_color_balance (play->player)) {
    color_balance_dialog (play);
    return;
  }

  g_warning ("No color balance channels available.");
  return;
}

static GList *
open_file_dialog (GtkPlay * play, gboolean multi)
{
  int res;
  GQueue uris = G_QUEUE_INIT;
  GtkWidget *chooser;
  GtkWidget *parent;

  if (play) {
    parent = GTK_WIDGET (play);
  } else {
    parent = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    gtk_application_add_window (GTK_APPLICATION (g_application_get_default ()),
        GTK_WINDOW (parent));
  }

  chooser = gtk_file_chooser_dialog_new ("Select files to play", NULL,
      GTK_FILE_CHOOSER_ACTION_OPEN,
      "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, NULL);
  g_object_set (chooser, "local-only", FALSE, "select-multiple", multi, NULL);
  gtk_window_set_transient_for (GTK_WINDOW (chooser), GTK_WINDOW (parent));

  res = gtk_dialog_run (GTK_DIALOG (chooser));
  if (res == GTK_RESPONSE_ACCEPT) {
    GSList *l;

    l = gtk_file_chooser_get_uris (GTK_FILE_CHOOSER (chooser));
    while (l) {
      g_queue_push_tail (&uris, l->data);
      l = g_slist_delete_link (l, l);
    }
  }

  gtk_widget_destroy (chooser);
  if (!play)
    gtk_widget_destroy (parent);

  return uris.head;
}

static void
open_file_clicked_cb (GtkWidget * unused, GtkPlay * play)
{
  GList *uris;

  uris = open_file_dialog (play, TRUE);
  if (uris) {
    /* free existing playlist */
    g_list_free_full (play->uris, g_free);

    play->uris = uris;
    play_current_uri (play, g_list_first (play->uris), NULL);
  }
}

G_MODULE_EXPORT void
next_button_clicked_cb (GtkButton * button, GtkPlay * play)
{
  GList *next;

  next = g_list_next (play->current_uri);
  g_return_if_fail (next != NULL);

  play_current_uri (play, next, NULL);
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
stream_info_get_string (GstPlayStreamInfo * stream, gint type, gboolean label)
{
  gchar *buffer = NULL;

  switch (type) {
    case AUDIO_INFO_RATE:
    {
      GstPlayAudioInfo *audio = (GstPlayAudioInfo *) stream;
      buffer = g_strdup_printf ("%s%d", label ? "Sample rate : " : "",
          gst_play_audio_info_get_sample_rate (audio));
      break;
    }
    case AUDIO_INFO_LANGUAGE:
    {
      GstPlayAudioInfo *audio = (GstPlayAudioInfo *) stream;
      const gchar *lang = gst_play_audio_info_get_language (audio);
      if (lang)
        buffer = g_strdup_printf ("%s%s", label ? "Language : " : "", lang);
      break;
    }
    case AUDIO_INFO_CHANNELS:
    {
      GstPlayAudioInfo *audio = (GstPlayAudioInfo *) stream;
      buffer = g_strdup_printf ("%s%s", label ? "Channels : " : "",
          audio_channels_string (gst_play_audio_info_get_channels (audio)));
      break;
    }
    case SUBTITLE_INFO_CODEC:
    case VIDEO_INFO_CODEC:
    case AUDIO_INFO_CODEC:
    {
      buffer = g_strdup_printf ("%s%s", label ? "Codec : " : "",
          gst_play_stream_info_get_codec (stream));
      break;
    }
    case AUDIO_INFO_MAX_BITRATE:
    {
      GstPlayAudioInfo *audio = (GstPlayAudioInfo *) stream;
      gint bitrate = gst_play_audio_info_get_max_bitrate (audio);

      if (bitrate > 0)
        buffer = g_strdup_printf ("%s%d", label ? "Max bitrate : " : "",
            bitrate);
      break;
    }
    case VIDEO_INFO_MAX_BITRATE:
    {
      GstPlayVideoInfo *video = (GstPlayVideoInfo *) stream;
      gint bitrate = gst_play_video_info_get_max_bitrate (video);

      if (bitrate > 0)
        buffer = g_strdup_printf ("%s%d", label ? "Max bitrate : " : "",
            bitrate);
      break;
    }
    case VIDEO_INFO_PAR:
    {
      guint par_d, par_n;
      GstPlayVideoInfo *video = (GstPlayVideoInfo *) stream;

      gst_play_video_info_get_pixel_aspect_ratio (video, &par_n, &par_d);
      buffer = g_strdup_printf ("%s%u:%u", label ? "pixel-aspect-ratio : " :
          "", par_n, par_d);
      break;
    }
    case VIDEO_INFO_FPS:
    {
      gint fps_d, fps_n;
      GstPlayVideoInfo *video = (GstPlayVideoInfo *) stream;

      gst_play_video_info_get_framerate (video, &fps_n, &fps_d);
      buffer = g_strdup_printf ("%s%.2f", label ? "Framerate : " : "",
          (gdouble) fps_n / fps_d);
      break;
    }
    case VIDEO_INFO_RESOLUTION:
    {
      GstPlayVideoInfo *video = (GstPlayVideoInfo *) stream;
      buffer = g_strdup_printf ("%s%dx%d", label ? "Resolution : " : "",
          gst_play_video_info_get_width (video),
          gst_play_video_info_get_height (video));
      break;
    }
    case SUBTITLE_INFO_LANGUAGE:
    {
      GstPlaySubtitleInfo *sub = (GstPlaySubtitleInfo *) stream;
      buffer = g_strdup_printf ("%s%s", label ? "Language : " : "",
          gst_play_subtitle_info_get_language (sub));
      break;
    }
    default:
      break;
  }
  return buffer;
}

static void
fill_tree_model (GtkTreeStore * tree, GtkPlay * play, GstPlayMediaInfo * info)
{
  GList *l;
  guint count;
  GtkTreeIter child, parent;

  count = 0;
  for (l = gst_play_media_info_get_stream_list (info); l != NULL; l = l->next) {
    gchar *buffer;
    gint i, start, end;
    GstPlayStreamInfo *stream = (GstPlayStreamInfo *) l->data;

    /* define the field range based on stream type */
    if (GST_IS_PLAY_VIDEO_INFO (stream)) {
      start = VIDEO_INFO_START + 1;
      end = VIDEO_INFO_END;
    } else if (GST_IS_PLAY_AUDIO_INFO (stream)) {
      start = AUDIO_INFO_START + 1;
      end = AUDIO_INFO_END;
    } else {
      start = SUBTITLE_INFO_START + 1;
      end = SUBTITLE_INFO_END;
    }

    buffer = g_strdup_printf ("Stream %u", count++);
    gtk_tree_store_append (tree, &parent, NULL);
    gtk_tree_store_set (tree, &parent, COL_TEXT, buffer, -1);
    g_free (buffer);

    buffer = g_strdup_printf ("Type : %s",
        gst_play_stream_info_get_stream_type (stream));
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
}

G_MODULE_EXPORT void
media_info_dialog_button_clicked_cb (GtkButton * button, GtkPlay * play)
{
  gtk_widget_destroy (GTK_WIDGET (play->media_info_dialog));
  play->media_info_dialog = NULL;
}

static void
media_info_dialog (GtkPlay * play, GstPlayMediaInfo * media_info)
{
  GtkBuilder *dialog_ui;
  GtkWidget *view;
  GtkTreeStore *tree;
  GtkTreeViewColumn *col;
  GtkCellRenderer *renderer;

  dialog_ui = load_from_builder ("/ui/media_info_dialog.ui", TRUE, play);
  if (!dialog_ui)
    return;

  play->media_info_dialog =
      (GtkWidget *) gtk_builder_get_object (dialog_ui, "media_info_dialog");
  gtk_window_set_transient_for (GTK_WINDOW (play->media_info_dialog),
      GTK_WINDOW (play));

  view = (GtkWidget *) gtk_builder_get_object (dialog_ui, "view");
  col = (GtkTreeViewColumn *) gtk_builder_get_object (dialog_ui, "col");

  /* TODO: use glade cell renderer (not working for me) */
  renderer = gtk_cell_renderer_text_new ();
  gtk_tree_view_column_pack_start (col, renderer, TRUE);
  gtk_tree_view_column_add_attribute (col, renderer, "text", COL_TEXT);

  tree = (GtkTreeStore *) gtk_builder_get_object (dialog_ui, "tree");
  fill_tree_model (tree, play, media_info);

  g_signal_connect (view, "realize",
      G_CALLBACK (gtk_tree_view_expand_all), NULL);

  gtk_widget_set_size_request (play->media_info_dialog, 550, 450);

  gtk_widget_show_all (play->media_info_dialog);
  gtk_dialog_run (GTK_DIALOG (play->media_info_dialog));
}

static void
media_info_clicked_cb (GtkButton * button, GtkPlay * play)
{
  GstPlayMediaInfo *media_info;

  media_info = gst_play_get_media_info (play->player);
  if (!media_info)
    return;

  media_info_dialog (play, media_info);
  g_object_unref (media_info);
}

static gboolean
toolbar_hide_cb (GtkPlay * play)
{
  GdkCursor *cursor;

  /* hide mouse pointer and toolbar */
  gtk_widget_hide (play->toolbar);
  cursor =
      gdk_cursor_new_for_display (gtk_widget_get_display (GTK_WIDGET (play)),
      GDK_BLANK_CURSOR);
  gdk_window_set_cursor (gtk_widget_get_window (GTK_WIDGET (play->video_area)),
      cursor);
  g_object_unref (cursor);

  play->toolbar_hide_timeout = 0;
  return FALSE;
}

static void
toolbar_show (GtkPlay * play)
{
  /* if timer is running then kill it */
  if (play->toolbar_hide_timeout) {
    g_source_remove (play->toolbar_hide_timeout);
    play->toolbar_hide_timeout = 0;
  }

  /* show toolbar and mouse pointer */
  gdk_window_set_cursor (gtk_widget_get_window (GTK_WIDGET
          (play->video_area)), play->default_cursor);
  gtk_widget_show (play->toolbar);
}

static void
start_toolbar_hide_timer (GtkPlay * play)
{
  /* hide toolbar only if its playing */
  if (!play->playing)
    return;

  /* start timer to hide toolbar */
  if (play->toolbar_hide_timeout)
    g_source_remove (play->toolbar_hide_timeout);
  play->toolbar_hide_timeout = g_timeout_add_seconds (5,
      (GSourceFunc) toolbar_hide_cb, play);
}

G_MODULE_EXPORT void
fullscreen_button_toggled_cb (GtkToggleButton * widget, GtkPlay * play)
{
  GtkWidget *image;

  if (gtk_toggle_button_get_active (widget)) {
    image = TOOLBAR_GET_OBJECT (restore_image);
    gtk_window_fullscreen (GTK_WINDOW (play));
    gtk_button_set_image (GTK_BUTTON (play->fullscreen_button), image);
  } else {
    image = TOOLBAR_GET_OBJECT (fullscreen_image);
    gtk_window_unfullscreen (GTK_WINDOW (play));
    gtk_button_set_image (GTK_BUTTON (play->fullscreen_button), image);
  }
}

G_MODULE_EXPORT void
seekbar_value_changed_cb (GtkRange * range, GtkPlay * play)
{
  gdouble value = gtk_range_get_value (GTK_RANGE (play->seekbar));
  gst_play_seek (play->player, gst_util_uint64_scale (value, GST_SECOND, 1));
}

G_MODULE_EXPORT void
volume_button_value_changed_cb (GtkScaleButton * button, gdouble value,
    GtkPlay * play)
{
  gst_play_set_volume (play->player, value);
}

static gint
_get_current_track_index (GtkPlay * play, void *(*func) (GstPlay * player))
{
  void *obj;
  gint index = -1;

  obj = func (play->player);
  if (obj) {
    index = gst_play_stream_info_get_index ((GstPlayStreamInfo *) obj);
    g_object_unref (obj);
  }

  return index;
}

static gint
get_current_track_index (GtkPlay * play, GType type)
{
  if (type == GST_TYPE_PLAY_VIDEO_INFO)
    return _get_current_track_index (play,
        (void *) gst_play_get_current_video_track);
  else if (type == GST_TYPE_PLAY_AUDIO_INFO)
    return _get_current_track_index (play,
        (void *) gst_play_get_current_audio_track);
  else
    return _get_current_track_index (play,
        (void *) gst_play_get_current_subtitle_track);
}

static gchar *
get_menu_label (GstPlayStreamInfo * stream, GType type)
{
  if (type == GST_TYPE_PLAY_AUDIO_INFO) {
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
  } else if (type == GST_TYPE_PLAY_VIDEO_INFO) {
    /* label format: <codec_name> */
    return stream_info_get_string (stream, VIDEO_INFO_CODEC, FALSE);
  } else {
    /* label format: <langauge> */
    return stream_info_get_string (stream, SUBTITLE_INFO_LANGUAGE, FALSE);
  }

  return NULL;
}

static void
new_subtitle_clicked_cb (GtkWidget * unused, GtkPlay * play)
{
  GList *uri;

  uri = open_file_dialog (play, FALSE);
  if (uri) {
    play_current_uri (play, play->current_uri, uri->data);
    g_list_free_full (uri, g_free);
  }
}

static void
disable_track (GtkPlay * play, GType type)
{
  if (type == GST_TYPE_PLAY_VIDEO_INFO) {
    gst_play_set_video_track_enabled (play->player, FALSE);
  } else if (type == GST_TYPE_PLAY_AUDIO_INFO)
    gst_play_set_audio_track_enabled (play->player, FALSE);
  else
    gst_play_set_subtitle_track_enabled (play->player, FALSE);
}

static void
change_track (GtkPlay * play, gint index, GType type)
{
  if (type == GST_TYPE_PLAY_VIDEO_INFO) {
    gst_play_set_video_track (play->player, index);
    gst_play_set_video_track_enabled (play->player, TRUE);
  } else if (type == GST_TYPE_PLAY_AUDIO_INFO) {
    gst_play_set_audio_track (play->player, index);
    gst_play_set_audio_track_enabled (play->player, TRUE);
  } else {
    gst_play_set_subtitle_track (play->player, index);
    gst_play_set_subtitle_track_enabled (play->player, TRUE);
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

static void
visualization_changed_cb (GtkWidget * widget, GtkPlay * play)
{
  gchar *name;

  if (gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (widget))) {
    name = g_object_get_data (G_OBJECT (widget), "name");
    if (g_strcmp0 (name, "disable") == 0) {
      gst_play_set_visualization_enabled (play->player, FALSE);
    } else {
      const gchar *vis_name;

      gst_play_set_visualization (play->player, name);
      /* if visualization is not enabled then enable it */
      if (!(vis_name = gst_play_get_current_visualization (play->player))) {
        gst_play_set_visualization_enabled (play->player, TRUE);
      }
    }
  }
}

static GtkWidget *
create_visualization_menu (GtkPlay * play)
{
  GtkWidget *menu;
  GtkWidget *item;
  GtkWidget *sep;
  GSList *group = NULL;
  const gchar *cur_vis;
  GstPlayVisualization **viss, **p;

  menu = gtk_menu_new ();
  cur_vis = gst_play_get_current_visualization (play->player);
  viss = gst_play_visualizations_get ();

  p = viss;
  while (*p) {
    gchar *label = g_strdup ((*p)->name);

    item = gtk_radio_menu_item_new_with_label (group, label);
    group = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (item));
    if (g_strcmp0 (label, cur_vis) == 0)
      gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (item), TRUE);
    g_object_set_data_full (G_OBJECT (item), "name", label,
        (GDestroyNotify) g_free);
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
    g_signal_connect (G_OBJECT (item), "toggled",
        G_CALLBACK (visualization_changed_cb), play);
    p++;
  }
  gst_play_visualizations_free (viss);

  sep = gtk_separator_menu_item_new ();
  item = gtk_radio_menu_item_new_with_label (group, "Disable");
  group = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (item));
  g_object_set_data (G_OBJECT (item), "name", (gpointer) "disable");
  if (cur_vis == NULL)
    gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (item), TRUE);
  g_signal_connect (G_OBJECT (item), "toggled",
      G_CALLBACK (visualization_changed_cb), play);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), sep);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

  return menu;
}

static GtkWidget *
create_tracks_menu (GtkPlay * play, GstPlayMediaInfo * media_info, GType type)
{
  GtkWidget *menu;
  GtkWidget *item;
  GtkWidget *sep;
  GList *list, *l;
  gint current_index;
  GSList *group = NULL;

  if (!media_info)
    return NULL;

  current_index = get_current_track_index (play, type);

  if (type == GST_TYPE_PLAY_VIDEO_INFO)
    list = gst_play_media_info_get_video_streams (media_info);
  else if (type == GST_TYPE_PLAY_AUDIO_INFO)
    list = gst_play_media_info_get_audio_streams (media_info);
  else
    list = gst_play_media_info_get_subtitle_streams (media_info);

  menu = gtk_menu_new ();

  if (type == GST_TYPE_PLAY_SUBTITLE_INFO) {
    GtkWidget *ext_subtitle;
    ext_subtitle = gtk_menu_item_new_with_label ("New File");
    sep = gtk_separator_menu_item_new ();
    g_signal_connect (G_OBJECT (ext_subtitle), "activate",
        G_CALLBACK (new_subtitle_clicked_cb), play);
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), ext_subtitle);
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), sep);
  }

  for (l = list; l != NULL; l = l->next) {
    gint index;
    gchar *buffer;
    GstPlayStreamInfo *s = (GstPlayStreamInfo *) l->data;

    buffer = get_menu_label (s, type);
    item = gtk_radio_menu_item_new_with_label (group, buffer);
    group = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (item));
    index = gst_play_stream_info_get_index (s);
    g_object_set_data (G_OBJECT (item), "index", GINT_TO_POINTER (index));
    g_object_set_data (G_OBJECT (item), "type", GSIZE_TO_POINTER (type));
    if (current_index == index)
      gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (item), TRUE);
    g_free (buffer);
    g_signal_connect (G_OBJECT (item), "toggled",
        G_CALLBACK (track_changed_cb), play);
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
  }

  sep = gtk_separator_menu_item_new ();
  item = gtk_radio_menu_item_new_with_label (group, "Disable");
  group = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (item));
  g_object_set_data (G_OBJECT (item), "index", GINT_TO_POINTER (-1));
  g_object_set_data (G_OBJECT (item), "type", GSIZE_TO_POINTER (type));
  if (current_index == -1)
    gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (item), TRUE);
  g_signal_connect (G_OBJECT (item), "toggled",
      G_CALLBACK (track_changed_cb), play);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), sep);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

  return menu;
}

static void
player_quit_clicked_cb (GtkButton * button, GtkPlay * play)
{
  gtk_widget_destroy (GTK_WIDGET (play));
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
  GtkWidget *submenu;
  GtkWidget *vis;
  GtkWidget *cb;
  GstPlayMediaInfo *media_info;

  menu = gtk_menu_new ();
  info = gtk_menu_item_new_with_label ("Media Information");
  audio = gtk_menu_item_new_with_label ("Audio");
  video = gtk_menu_item_new_with_label ("Video");
  sub = gtk_menu_item_new_with_label ("Subtitle");
  open = gtk_menu_item_new_with_label ("Open");
  next = gtk_menu_item_new_with_label ("Next");
  prev = gtk_menu_item_new_with_label ("Prev");
  quit = gtk_menu_item_new_with_label ("Quit");
  vis = gtk_menu_item_new_with_label ("Visualization");
  cb = gtk_menu_item_new_with_label ("Color Balance");

  media_info = gst_play_get_media_info (play->player);

  if (media_info && !gst_play_media_info_get_video_streams (media_info))
    gtk_widget_set_sensitive (video, FALSE);
  else {
    submenu = create_tracks_menu (play, media_info, GST_TYPE_PLAY_VIDEO_INFO);
    if (submenu)
      gtk_menu_item_set_submenu (GTK_MENU_ITEM (video), submenu);
    else
      gtk_widget_set_sensitive (video, FALSE);
  }

  if (media_info && !gst_play_media_info_get_audio_streams (media_info))
    gtk_widget_set_sensitive (audio, FALSE);
  else {
    submenu = create_tracks_menu (play, media_info, GST_TYPE_PLAY_AUDIO_INFO);
    if (submenu)
      gtk_menu_item_set_submenu (GTK_MENU_ITEM (audio), submenu);
    else
      gtk_widget_set_sensitive (audio, FALSE);
  }

  /* enable visualization menu for audio stream */
  if (media_info &&
      gst_play_media_info_get_audio_streams (media_info) &&
      !gst_play_media_info_get_video_streams (media_info)) {
    submenu = create_visualization_menu (play);
    gtk_menu_item_set_submenu (GTK_MENU_ITEM (vis), submenu);
  } else {
    gtk_widget_set_sensitive (vis, FALSE);
  }

  if (media_info && gst_play_media_info_get_video_streams (media_info)) {
    submenu = create_tracks_menu (play, media_info,
        GST_TYPE_PLAY_SUBTITLE_INFO);
    gtk_menu_item_set_submenu (GTK_MENU_ITEM (sub), submenu);
  } else {
    gtk_widget_set_sensitive (sub, FALSE);
  }

  gtk_widget_set_sensitive (next, g_list_next
      (play->current_uri) ? TRUE : FALSE);
  gtk_widget_set_sensitive (prev, g_list_previous
      (play->current_uri) ? TRUE : FALSE);
  gtk_widget_set_sensitive (info, media_info ? TRUE : FALSE);
  gtk_widget_set_sensitive (cb, gst_play_has_color_balance (play->player) ?
      TRUE : FALSE);

  g_signal_connect (G_OBJECT (open), "activate",
      G_CALLBACK (open_file_clicked_cb), play);
  g_signal_connect (G_OBJECT (cb), "activate",
      G_CALLBACK (color_balance_clicked_cb), play);
  g_signal_connect (G_OBJECT (next), "activate",
      G_CALLBACK (next_button_clicked_cb), play);
  g_signal_connect (G_OBJECT (prev), "activate",
      G_CALLBACK (prev_button_clicked_cb), play);
  g_signal_connect (G_OBJECT (info), "activate",
      G_CALLBACK (media_info_clicked_cb), play);
  g_signal_connect (G_OBJECT (quit), "activate",
      G_CALLBACK (player_quit_clicked_cb), play);

  gtk_menu_shell_append (GTK_MENU_SHELL (menu), open);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), next);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), prev);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), video);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), audio);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), vis);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), sub);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), info);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), cb);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), quit);

  gtk_widget_show_all (menu);
#if GTK_CHECK_VERSION(3,22,00)
  gtk_menu_popup_at_pointer (GTK_MENU (menu), (GdkEvent *) event);
#else
  gtk_menu_popup (GTK_MENU (menu), NULL, NULL, NULL, NULL,
      (event != NULL) ? event->button : 0,
      gdk_event_get_time ((GdkEvent *) event));
#endif

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
video_area_leave_notify_cb (GtkWidget * widget, GdkEvent * event,
    GtkPlay * play)
{
  start_toolbar_hide_timer (play);

  return TRUE;
}

static gboolean
video_area_toolbar_show_cb (GtkWidget * widget, GdkEvent * event,
    GtkPlay * play)
{
  toolbar_show (play);

  start_toolbar_hide_timer (play);

  return TRUE;
}

static gboolean
overlay_leave_notify_event_cb (GtkWidget * widget, GdkEvent * event,
    GtkPlay * play)
{
  start_toolbar_hide_timer (play);

  return TRUE;
}

static gboolean
overlay_enter_notify_event_cb (GtkWidget * widget, GdkEvent * event,
    GtkPlay * play)
{
  toolbar_show (play);

  return TRUE;
}

static void
apply_css (GtkWidget * widget, GtkStyleProvider * provider)
{
  gtk_style_context_add_provider (gtk_widget_get_style_context (widget),
      provider, G_MAXUINT);
  if (GTK_IS_CONTAINER (widget)) {
    gtk_container_forall (GTK_CONTAINER (widget),
        (GtkCallback) apply_css, provider);
  }
}

static void
gtk_widget_apply_css (GtkWidget * widget, const gchar * filename)
{
  GBytes *bytes;
  gsize data_size;
  const guint8 *data;
  GError *err = NULL;
  GtkStyleProvider *provider;

  if (widget == NULL)
    return;

  provider = GTK_STYLE_PROVIDER (gtk_css_provider_new ());
  bytes = g_resources_lookup_data (filename, 0, &err);
  if (err) {
    gst_print ("ERROR: failed to apply css %s '%s' \n", filename, err->message);
    return;
  }
  data = g_bytes_get_data (bytes, &data_size);
  gtk_css_provider_load_from_data (GTK_CSS_PROVIDER (provider),
      (gchar *) data, data_size, NULL);
  g_bytes_unref (bytes);

  apply_css (widget, provider);
}

static gboolean
get_child_position (GtkOverlay * overlay, GtkWidget * widget,
    GtkAllocation * alloc, GtkPlay * play)
{
  GtkRequisition req;
  GtkWidget *child;
  GtkAllocation main_alloc;
  gint x, y;
  GtkWidget *relative = play->video_area;

  child = gtk_bin_get_child (GTK_BIN (overlay));
  gtk_widget_translate_coordinates (relative, child, 0, 0, &x, &y);
  main_alloc.x = x;
  main_alloc.y = y;
  main_alloc.width = gtk_widget_get_allocated_width (relative);
  main_alloc.height = gtk_widget_get_allocated_height (relative);

  gtk_widget_get_preferred_size (widget, NULL, &req);

  alloc->x = (main_alloc.width - req.width) / 2;
  if (alloc->x < 0)
    alloc->x = 0;
  alloc->width = MIN (main_alloc.width, req.width);
  if (gtk_widget_get_halign (widget) == GTK_ALIGN_END)
    alloc->x += main_alloc.width - req.width;

  alloc->y = main_alloc.height - req.height - 20;
  if (alloc->y < 0)
    alloc->y = 0;
  alloc->height = MIN (main_alloc.height, req.height);
  if (gtk_widget_get_valign (widget) == GTK_ALIGN_END)
    alloc->y += main_alloc.height - req.height;

  return TRUE;
}

static void
create_ui (GtkPlay * play)
{
  GtkWidget *main_hbox;

  gtk_window_set_default_size (GTK_WINDOW (play), 640, 480);

  g_signal_connect (G_OBJECT (play), "delete-event",
      G_CALLBACK (delete_event_cb), play);

  gtk_widget_set_events (GTK_WIDGET (play),
      GDK_KEY_RELEASE_MASK | GDK_KEY_PRESS_MASK);
  g_signal_connect (G_OBJECT (play), "key-press-event",
      G_CALLBACK (key_press_event_cb), NULL);

  set_title (play, APP_NAME);
  gtk_application_add_window (GTK_APPLICATION (g_application_get_default ()),
      GTK_WINDOW (play));

  play->renderer = gst_play_gtk_video_renderer_new ();
  if (play->renderer) {
    play->video_area =
        gst_play_gtk_video_renderer_get_widget (GST_PLAY_GTK_VIDEO_RENDERER
        (play->renderer));
    g_object_unref (play->video_area);
  } else {
    play->renderer = gst_play_video_overlay_video_renderer_new (NULL);

    play->video_area = gtk_drawing_area_new ();
    g_signal_connect (play->video_area, "realize",
        G_CALLBACK (video_area_realize_cb), play);
  }
  gtk_widget_set_events (play->video_area, GDK_EXPOSURE_MASK
      | GDK_LEAVE_NOTIFY_MASK
      | GDK_BUTTON_PRESS_MASK
      | GDK_POINTER_MOTION_MASK
      | GDK_POINTER_MOTION_HINT_MASK | GDK_ENTER_NOTIFY_MASK);
  g_signal_connect (play->video_area, "motion-notify-event",
      G_CALLBACK (video_area_toolbar_show_cb), play);
  g_signal_connect (play->video_area, "scroll-event",
      G_CALLBACK (video_area_toolbar_show_cb), play);
  g_signal_connect (play->video_area, "button-press-event",
      G_CALLBACK (mouse_button_pressed_cb), play);
  g_signal_connect (play->video_area, "leave-notify-event",
      G_CALLBACK (video_area_leave_notify_cb), play);

  /* load toolbar UI */
  play->toolbar_ui = load_from_builder ("/ui/toolbar.ui", TRUE, play);
  if (!play->toolbar_ui)
    return;

  play->toolbar = TOOLBAR_GET_OBJECT (toolbar);
  play->play_pause_button = TOOLBAR_GET_OBJECT (play_pause_button);
  play->seekbar = TOOLBAR_GET_OBJECT (seekbar);
  play->next_button = TOOLBAR_GET_OBJECT (next_button);
  play->prev_button = TOOLBAR_GET_OBJECT (prev_button);
  play->fullscreen_button = TOOLBAR_GET_OBJECT (fullscreen_button);
  play->volume_button = TOOLBAR_GET_OBJECT (volume_button);
  play->elapshed_label = TOOLBAR_GET_LABEL (elapshed_time);
  play->remain_label = TOOLBAR_GET_LABEL (remain_time);
  play->rate_label = TOOLBAR_GET_LABEL (rate_label);
  play->title_label = TOOLBAR_GET_LABEL (title_label);

  main_hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_box_pack_start (GTK_BOX (main_hbox), play->video_area, TRUE, TRUE, 0);

  /* set minimum window size */
  gtk_widget_set_size_request (main_hbox, 320, 240);

  /* set the toolbar size */
  gtk_widget_set_size_request (play->toolbar, 500, 50);

  play->toolbar_overlay = gtk_overlay_new ();
  gtk_overlay_add_overlay (GTK_OVERLAY (play->toolbar_overlay), play->toolbar);
  gtk_container_add (GTK_CONTAINER (play->toolbar_overlay), main_hbox);
  gtk_container_add (GTK_CONTAINER (play), play->toolbar_overlay);
  gtk_widget_set_events (play->toolbar_overlay, GDK_EXPOSURE_MASK
      | GDK_LEAVE_NOTIFY_MASK
      | GDK_BUTTON_PRESS_MASK
      | GDK_POINTER_MOTION_MASK
      | GDK_POINTER_MOTION_HINT_MASK | GDK_ENTER_NOTIFY_MASK);

  g_signal_connect (play->toolbar_overlay, "get-child-position",
      G_CALLBACK (get_child_position), play);
  g_signal_connect (play->toolbar_overlay, "leave-notify-event",
      G_CALLBACK (overlay_leave_notify_event_cb), play);
  g_signal_connect (play->toolbar_overlay, "enter-notify-event",
      G_CALLBACK (overlay_enter_notify_event_cb), play);

  /* apply css on widgets */
  gtk_widget_apply_css (play->toolbar, "/css/toolbar.css");

  gtk_widget_realize (play->video_area);
  gtk_widget_hide (play->video_area);

  /* start toolbar autohide timer */
  start_toolbar_hide_timer (play);

  /* check if we need to enable fullscreen */
  if (play->fullscreen)
    gtk_toggle_button_set_active
        (GTK_TOGGLE_BUTTON (play->fullscreen_button), TRUE);
}

static void
duration_changed_cb (GstPlay * unused, GstClockTime duration, GtkPlay * play)
{
  g_signal_handlers_block_by_func (play->seekbar,
      seekbar_value_changed_cb, play);
  gtk_range_set_range (GTK_RANGE (play->seekbar), 0.0,
      (gdouble) duration / GST_SECOND);
  g_signal_handlers_unblock_by_func (play->seekbar,
      seekbar_value_changed_cb, play);
}

static void
update_position_label (GtkLabel * label, guint64 seconds)
{
  gchar *data;
  gint hrs, mins;

  hrs = seconds / 3600;
  seconds -= hrs * 3600;
  mins = seconds / 60;
  seconds -= mins * 60;

  if (hrs)
    data = g_strdup_printf ("%d:%02d:%02" G_GUINT64_FORMAT, hrs, mins, seconds);
  else
    data = g_strdup_printf ("%02d:%02" G_GUINT64_FORMAT, mins, seconds);

  gtk_label_set_label (label, data);
  g_free (data);
}

static void
position_updated_cb (GstPlaySignalAdapter * unused, GstClockTime position,
    GtkPlay * play)
{
  if (!GST_IS_PLAY (play->player))
    return;

  g_signal_handlers_block_by_func (play->seekbar,
      seekbar_value_changed_cb, play);
  gtk_range_set_value (GTK_RANGE (play->seekbar),
      (gdouble) position / GST_SECOND);
  update_position_label (play->elapshed_label, position / GST_SECOND);
  update_position_label (play->remain_label,
      GST_CLOCK_DIFF (position, gst_play_get_duration (play->player)) /
      GST_SECOND);
  g_signal_handlers_unblock_by_func (play->seekbar,
      seekbar_value_changed_cb, play);
}

static void
eos_cb (GstPlaySignalAdapter * unused, GtkPlay * play)
{
  if (play->playing) {
    GList *next = NULL;

    next = g_list_next (play->current_uri);
    if (!next && play->loop)
      next = g_list_first (play->uris);

    if (next) {
      play_current_uri (play, next, NULL);
    } else {
      GtkWidget *image;

      gst_play_pause (play->player);
      image = TOOLBAR_GET_OBJECT (play_image);
      gtk_button_set_image (GTK_BUTTON (play->play_pause_button), image);
      play->playing = FALSE;
      if (play->inhibit_cookie)
        gtk_application_uninhibit (GTK_APPLICATION (g_application_get_default
                ()), play->inhibit_cookie);
      play->inhibit_cookie = 0;
    }
  }
}

static GdkPixbuf *
gtk_play_get_cover_image (GstPlayMediaInfo * media_info)
{
  GstSample *sample;
  GstMapInfo info;
  GstBuffer *buffer;
  GError *err = NULL;
  GdkPixbufLoader *loader;
  GdkPixbuf *pixbuf = NULL;
  const GstStructure *caps_struct;
  GstTagImageType type = GST_TAG_IMAGE_TYPE_UNDEFINED;

  /* get image sample buffer from media */
  sample = gst_play_media_info_get_image_sample (media_info);
  if (!sample)
    return NULL;

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
    gst_print ("unsupport type ... %d \n", type);
    return NULL;
  }

  if (!gst_buffer_map (buffer, &info, GST_MAP_READ)) {
    gst_print ("failed to map gst buffer \n");
    return NULL;
  }

  loader = gdk_pixbuf_loader_new ();
  if (gdk_pixbuf_loader_write (loader, info.data, info.size, &err) &&
      gdk_pixbuf_loader_close (loader, &err)) {
    pixbuf = gdk_pixbuf_loader_get_pixbuf (loader);
    if (pixbuf) {
      g_object_ref (pixbuf);
    } else {
      gst_print ("failed to convert gst buffer to pixbuf %s \n", err->message);
      g_error_free (err);
    }
  }

  g_object_unref (loader);
  gst_buffer_unmap (buffer, &info);

  return pixbuf;
}

static void
media_info_updated_cb (GstPlaySignalAdapter * adapter,
    GstPlayMediaInfo * media_info, GtkPlay * play)
{
  const gchar *title;
  GdkPixbuf *pixbuf;
  gchar *basename = NULL;
  gchar *filename = NULL;

  title = gst_play_media_info_get_title (media_info);

  if (!title) {
    filename =
        g_filename_from_uri (gst_play_media_info_get_uri (media_info), NULL,
        NULL);
    basename = g_path_get_basename (filename);
  }

  gtk_label_set_label (play->title_label, title ? title : basename);
  set_title (play, title ? title : filename);
  g_free (basename);
  g_free (filename);

  pixbuf = gtk_play_get_cover_image (media_info);

  if (pixbuf) {
    gtk_window_set_icon (GTK_WINDOW (play), pixbuf);
    g_object_unref (pixbuf);
  }
}

static void
player_volume_changed_cb (GstPlaySignalAdapter * adapter, GtkPlay * play)
{
  gdouble new_val, cur_val;

  cur_val = gtk_scale_button_get_value (GTK_SCALE_BUTTON (play->volume_button));
  new_val = gst_play_get_volume (play->player);

  if (fabs (cur_val - new_val) > 0.001) {
    g_signal_handlers_block_by_func (play->volume_button,
        volume_button_value_changed_cb, play);
    gtk_scale_button_set_value (GTK_SCALE_BUTTON (play->volume_button),
        new_val);
    g_signal_handlers_unblock_by_func (play->volume_button,
        volume_button_value_changed_cb, play);
  }
}

static void
gtk_play_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GtkPlay *self = (GtkPlay *) object;

  switch (prop_id) {
    case PROP_LOOP:
      self->loop = g_value_get_boolean (value);
      break;
    case PROP_FULLSCREEN:
      self->fullscreen = g_value_get_boolean (value);
      break;
    case PROP_URIS:
      self->uris = g_value_get_pointer (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
show_cb (GtkWidget * widget, gpointer user_data)
{
  GtkPlay *self = (GtkPlay *) widget;

  self->default_cursor = gdk_window_get_cursor
      (gtk_widget_get_window (GTK_WIDGET (self)));

  play_current_uri (self, g_list_first (self->uris), NULL);
}

static GObject *
gtk_play_constructor (GType type, guint n_construct_params,
    GObjectConstructParam * construct_params)
{
  GtkPlay *self;

  self =
      (GtkPlay *) G_OBJECT_CLASS (gtk_play_parent_class)->constructor (type,
      n_construct_params, construct_params);

  self->playing = TRUE;

  if (self->inhibit_cookie)
    gtk_application_uninhibit (GTK_APPLICATION (g_application_get_default ()),
        self->inhibit_cookie);
  self->inhibit_cookie =
      gtk_application_inhibit (GTK_APPLICATION (g_application_get_default ()),
      GTK_WINDOW (self), GTK_APPLICATION_INHIBIT_IDLE, "Playing media");

  create_ui (self);

  self->player = gst_play_new (self->renderer);
  self->signal_adapter = gst_play_signal_adapter_new (self->player);

  g_signal_connect (self->signal_adapter, "position-updated",
      G_CALLBACK (position_updated_cb), self);
  g_signal_connect (self->signal_adapter, "duration-changed",
      G_CALLBACK (duration_changed_cb), self);
  g_signal_connect (self->signal_adapter, "end-of-stream", G_CALLBACK (eos_cb),
      self);
  g_signal_connect (self->signal_adapter, "media-info-updated",
      G_CALLBACK (media_info_updated_cb), self);
  g_signal_connect (self->signal_adapter, "volume-changed",
      G_CALLBACK (player_volume_changed_cb), self);

  /* enable visualization (by default playbin uses goom) */
  /* if visualization is enabled then use the first element */
  gst_play_set_visualization_enabled (self->player, TRUE);

  g_signal_connect (G_OBJECT (self), "show", G_CALLBACK (show_cb), NULL);

  return G_OBJECT (self);
}

static void
gtk_play_dispose (GObject * object)
{
  GtkPlay *self = (GtkPlay *) object;

  if (self->inhibit_cookie)
    gtk_application_uninhibit (GTK_APPLICATION (g_application_get_default ()),
        self->inhibit_cookie);
  self->inhibit_cookie = 0;

  if (self->uris)
    g_list_free_full (self->uris, g_free);
  self->uris = NULL;

  g_clear_object (&self->signal_adapter);

  if (self->player) {
    gst_play_stop (self->player);
    gst_object_unref (self->player);
  }
  self->player = NULL;

  G_OBJECT_CLASS (gtk_play_parent_class)->dispose (object);
}

static void
gtk_play_init (GtkPlay * self)
{
}

static void
gtk_play_class_init (GtkPlayClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructor = gtk_play_constructor;
  object_class->dispose = gtk_play_dispose;
  object_class->set_property = gtk_play_set_property;

  gtk_play_properties[PROP_LOOP] =
      g_param_spec_boolean ("loop", "Loop", "Loop the playlist",
      FALSE,
      G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  gtk_play_properties[PROP_FULLSCREEN] =
      g_param_spec_boolean ("fullscreen", "Fullscreen", "Fullscreen mode",
      FALSE,
      G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  gtk_play_properties[PROP_URIS] =
      g_param_spec_pointer ("uris", "URIs", "URIs to play",
      G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, LAST_PROP,
      gtk_play_properties);
}

static gint
gtk_play_app_command_line (GApplication * application,
    GApplicationCommandLine * command_line)
{
  GVariantDict *options;
  GtkPlay *play;
  GList *uris = NULL;
  gboolean loop = FALSE, fullscreen = FALSE;
  gchar **uris_array = NULL;

  options = g_application_command_line_get_options_dict (command_line);

  g_variant_dict_lookup (options, "loop", "b", &loop);
  g_variant_dict_lookup (options, "fullscreen", "b", &fullscreen);
  g_variant_dict_lookup (options, G_OPTION_REMAINING, "^a&ay", &uris_array);

  if (uris_array) {
    gchar **p;
    GQueue uris_builder = G_QUEUE_INIT;

    p = uris_array;
    while (*p) {
      g_queue_push_tail (&uris_builder, gst_uri_is_valid (*p) ?
          g_strdup (*p) : gst_filename_to_uri (*p, NULL));
      p++;
    }
    uris = uris_builder.head;
  } else {
    uris = open_file_dialog (NULL, TRUE);
  }

  if (!uris)
    return -1;

  play =
      g_object_new (gtk_play_get_type (), "loop", loop, "fullscreen",
      fullscreen, "uris", uris, NULL);
  gtk_widget_show_all (GTK_WIDGET (play));

  return
      G_APPLICATION_CLASS (gtk_play_app_parent_class)->command_line
      (application, command_line);
}

static void
gtk_play_app_init (GtkPlayApp * self)
{
}

static void
gtk_play_app_class_init (GtkPlayAppClass * klass)
{
  GApplicationClass *application_class = G_APPLICATION_CLASS (klass);

  application_class->command_line = gtk_play_app_command_line;
}

static GtkPlayApp *
gtk_play_app_new (void)
{
  GtkPlayApp *self;
  GOptionEntry options[] = {
    {G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_FILENAME_ARRAY, NULL,
        "Files to play"},
    {"loop", 'l', 0, G_OPTION_ARG_NONE, NULL, "Repeat all"},
    {"fullscreen", 'f', 0, G_OPTION_ARG_NONE, NULL,
        "Show the player in fullscreen"},
    {NULL}
  };

  g_set_prgname (APP_NAME);
  g_set_application_name (APP_NAME);

  self = g_object_new (gtk_play_app_get_type (),
      "application-id", "org.freedesktop.gstreamer.GTKPlay",
      "flags", G_APPLICATION_HANDLES_COMMAND_LINE,
      "register-session", TRUE, NULL);

  g_application_set_default (G_APPLICATION (self));
  g_application_add_main_option_entries (G_APPLICATION (self), options);
  g_application_add_option_group (G_APPLICATION (self),
      gst_init_get_option_group ());

  return self;
}

int
main (gint argc, gchar ** argv)
{
  GtkPlayApp *app;
  gint status;

#if defined (GDK_WINDOWING_X11)
  XInitThreads ();
#endif

  app = gtk_play_app_new ();
  status = g_application_run (G_APPLICATION (app), argc, argv);;
  g_object_unref (app);

  gst_deinit ();
  return status;
}
