/* GStreamer command line playback testing utility
 *
 * Copyright (C) 2013-2014 Tim-Philipp Müller <tim centricular net>
 * Copyright (C) 2013 Collabora Ltd.
 * Copyright (C) 2014 Sebastian Dröge <sebastian@centricular.com>
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

#include <locale.h>

#include <gst/gst.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "gst-play-kb.h"
#include <gst/play/play.h>

#define VOLUME_STEPS 20

GST_DEBUG_CATEGORY (play_debug);
#define GST_CAT_DEFAULT play_debug

typedef struct
{
  gchar **uris;
  guint num_uris;
  gint cur_idx;

  GstPlay *player;
  GstPlaySignalAdapter *signal_adapter;
  GstState desired_state;

  gboolean repeat;

  GMainLoop *loop;
} Player;

static gboolean play_next (Player * play);
static gboolean play_prev (Player * play);
static void play_reset (Player * play);
static void play_set_relative_volume (Player * play, gdouble volume_step);

static void
end_of_stream_cb (GstPlaySignalAdapter * adapter, Player * play)
{
  gst_print ("\n");
  /* and switch to next item in list */
  if (!play_next (play)) {
    gst_print ("Reached end of play list.\n");
    g_main_loop_quit (play->loop);
  }
}

static void
error_cb (GstPlaySignalAdapter * adapter, GError * err, Player * play)
{
  gst_printerr ("ERROR %s for %s\n", err->message, play->uris[play->cur_idx]);

  /* if looping is enabled, then disable it else will keep looping forever */
  play->repeat = FALSE;

  /* try next item in list then */
  if (!play_next (play)) {
    gst_print ("Reached end of play list.\n");
    g_main_loop_quit (play->loop);
  }
}

static void
position_updated_cb (GstPlaySignalAdapter * adapter, GstClockTime pos,
    Player * play)
{
  GstClockTime dur = -1;
  gchar status[64] = { 0, };

  g_object_get (play->player, "duration", &dur, NULL);

  memset (status, ' ', sizeof (status) - 1);

  if (pos != -1 && dur > 0 && dur != -1) {
    gchar dstr[32], pstr[32];

    /* FIXME: pretty print in nicer format */
    g_snprintf (pstr, 32, "%" GST_TIME_FORMAT, GST_TIME_ARGS (pos));
    pstr[9] = '\0';
    g_snprintf (dstr, 32, "%" GST_TIME_FORMAT, GST_TIME_ARGS (dur));
    dstr[9] = '\0';
    gst_print ("%s / %s %s\r", pstr, dstr, status);
  }
}

static void
state_changed_cb (GstPlaySignalAdapter * adapter, GstPlayState state,
    Player * play)
{
  gst_print ("State changed: %s\n", gst_play_state_get_name (state));
}

static void
buffering_cb (GstPlaySignalAdapter * adapter, gint percent, Player * play)
{
  gst_print ("Buffering: %d\n", percent);
}

static void
print_one_tag (const GstTagList * list, const gchar * tag, gpointer user_data)
{
  gint i, num;

  num = gst_tag_list_get_tag_size (list, tag);
  for (i = 0; i < num; ++i) {
    const GValue *val;

    val = gst_tag_list_get_value_index (list, tag, i);
    if (G_VALUE_HOLDS_STRING (val)) {
      gst_print ("    %s : %s \n", tag, g_value_get_string (val));
    } else if (G_VALUE_HOLDS_UINT (val)) {
      gst_print ("    %s : %u \n", tag, g_value_get_uint (val));
    } else if (G_VALUE_HOLDS_DOUBLE (val)) {
      gst_print ("    %s : %g \n", tag, g_value_get_double (val));
    } else if (G_VALUE_HOLDS_BOOLEAN (val)) {
      gst_print ("    %s : %s \n", tag,
          g_value_get_boolean (val) ? "true" : "false");
    } else if (GST_VALUE_HOLDS_DATE_TIME (val)) {
      GstDateTime *dt = g_value_get_boxed (val);
      gchar *dt_str = gst_date_time_to_iso8601_string (dt);

      gst_print ("    %s : %s \n", tag, dt_str);
      g_free (dt_str);
    } else {
      gst_print ("    %s : tag of type '%s' \n", tag, G_VALUE_TYPE_NAME (val));
    }
  }
}

static void
print_video_info (GstPlayVideoInfo * info)
{
  gint fps_n, fps_d;
  guint par_n, par_d;

  if (info == NULL)
    return;

  gst_print ("  width : %d\n", gst_play_video_info_get_width (info));
  gst_print ("  height : %d\n", gst_play_video_info_get_height (info));
  gst_print ("  max_bitrate : %d\n",
      gst_play_video_info_get_max_bitrate (info));
  gst_print ("  bitrate : %d\n", gst_play_video_info_get_bitrate (info));
  gst_play_video_info_get_framerate (info, &fps_n, &fps_d);
  gst_print ("  framerate : %.2f\n", (gdouble) fps_n / fps_d);
  gst_play_video_info_get_pixel_aspect_ratio (info, &par_n, &par_d);
  gst_print ("  pixel-aspect-ratio  %u:%u\n", par_n, par_d);
}

static void
print_audio_info (GstPlayAudioInfo * info)
{
  if (info == NULL)
    return;

  gst_print ("  sample rate : %d\n",
      gst_play_audio_info_get_sample_rate (info));
  gst_print ("  channels : %d\n", gst_play_audio_info_get_channels (info));
  gst_print ("  max_bitrate : %d\n",
      gst_play_audio_info_get_max_bitrate (info));
  gst_print ("  bitrate : %d\n", gst_play_audio_info_get_bitrate (info));
  gst_print ("  language : %s\n", gst_play_audio_info_get_language (info));
}

static void
print_subtitle_info (GstPlaySubtitleInfo * info)
{
  if (info == NULL)
    return;

  gst_print ("  language : %s\n", gst_play_subtitle_info_get_language (info));
}

static void
print_all_stream_info (GstPlayMediaInfo * media_info)
{
  guint count = 0;
  GList *list, *l;

  gst_print ("URI : %s\n", gst_play_media_info_get_uri (media_info));
  gst_print ("Duration: %" GST_TIME_FORMAT "\n",
      GST_TIME_ARGS (gst_play_media_info_get_duration (media_info)));
  gst_print ("Global taglist:\n");
  if (gst_play_media_info_get_tags (media_info))
    gst_tag_list_foreach (gst_play_media_info_get_tags (media_info),
        print_one_tag, NULL);
  else
    gst_print ("  (nil) \n");

  list = gst_play_media_info_get_stream_list (media_info);
  if (!list)
    return;

  gst_print ("All Stream information\n");
  for (l = list; l != NULL; l = l->next) {
    GstTagList *tags = NULL;
    GstPlayStreamInfo *stream = (GstPlayStreamInfo *) l->data;

    gst_print (" Stream # %u \n", count++);
    gst_print ("  type : %s_%u\n",
        gst_play_stream_info_get_stream_type (stream),
        gst_play_stream_info_get_index (stream));
    tags = gst_play_stream_info_get_tags (stream);
    gst_print ("  taglist : \n");
    if (tags) {
      gst_tag_list_foreach (tags, print_one_tag, NULL);
    }

    if (GST_IS_PLAY_VIDEO_INFO (stream))
      print_video_info ((GstPlayVideoInfo *) stream);
    else if (GST_IS_PLAY_AUDIO_INFO (stream))
      print_audio_info ((GstPlayAudioInfo *) stream);
    else
      print_subtitle_info ((GstPlaySubtitleInfo *) stream);
  }
}

static void
print_all_video_stream (GstPlayMediaInfo * media_info)
{
  GList *list, *l;

  list = gst_play_media_info_get_video_streams (media_info);
  if (!list)
    return;

  gst_print ("All video streams\n");
  for (l = list; l != NULL; l = l->next) {
    GstPlayVideoInfo *info = (GstPlayVideoInfo *) l->data;
    GstPlayStreamInfo *sinfo = (GstPlayStreamInfo *) info;
    gst_print (" %s_%d #\n", gst_play_stream_info_get_stream_type (sinfo),
        gst_play_stream_info_get_index (sinfo));
    print_video_info (info);
  }
}

static void
print_all_subtitle_stream (GstPlayMediaInfo * media_info)
{
  GList *list, *l;

  list = gst_play_media_info_get_subtitle_streams (media_info);
  if (!list)
    return;

  gst_print ("All subtitle streams:\n");
  for (l = list; l != NULL; l = l->next) {
    GstPlaySubtitleInfo *info = (GstPlaySubtitleInfo *) l->data;
    GstPlayStreamInfo *sinfo = (GstPlayStreamInfo *) info;
    gst_print (" %s_%d #\n", gst_play_stream_info_get_stream_type (sinfo),
        gst_play_stream_info_get_index (sinfo));
    print_subtitle_info (info);
  }
}

static void
print_all_audio_stream (GstPlayMediaInfo * media_info)
{
  GList *list, *l;

  list = gst_play_media_info_get_audio_streams (media_info);
  if (!list)
    return;

  gst_print ("All audio streams: \n");
  for (l = list; l != NULL; l = l->next) {
    GstPlayAudioInfo *info = (GstPlayAudioInfo *) l->data;
    GstPlayStreamInfo *sinfo = (GstPlayStreamInfo *) info;
    gst_print (" %s_%d #\n", gst_play_stream_info_get_stream_type (sinfo),
        gst_play_stream_info_get_index (sinfo));
    print_audio_info (info);
  }
}

static void
print_current_tracks (Player * play)
{
  GstPlayAudioInfo *audio = NULL;
  GstPlayVideoInfo *video = NULL;
  GstPlaySubtitleInfo *subtitle = NULL;

  gst_print ("Current video track: \n");
  video = gst_play_get_current_video_track (play->player);
  print_video_info (video);

  gst_print ("Current audio track: \n");
  audio = gst_play_get_current_audio_track (play->player);
  print_audio_info (audio);

  gst_print ("Current subtitle track: \n");
  subtitle = gst_play_get_current_subtitle_track (play->player);
  print_subtitle_info (subtitle);

  if (audio)
    g_object_unref (audio);

  if (video)
    g_object_unref (video);

  if (subtitle)
    g_object_unref (subtitle);
}

static void
print_media_info (GstPlayMediaInfo * media_info)
{
  print_all_stream_info (media_info);
  gst_print ("\n");
  print_all_video_stream (media_info);
  gst_print ("\n");
  print_all_audio_stream (media_info);
  gst_print ("\n");
  print_all_subtitle_stream (media_info);
}

static void
media_info_cb (GstPlaySignalAdapter * adapter, GstPlayMediaInfo * info,
    Player * play)
{
  static int once = 0;

  if (!once) {
    print_media_info (info);
    print_current_tracks (play);
    once = 1;
  }
}

static Player *
play_new (gchar ** uris, gdouble initial_volume)
{
  Player *play;

  play = g_new0 (Player, 1);

  play->uris = uris;
  play->num_uris = g_strv_length (uris);
  play->cur_idx = -1;

  play->player = gst_play_new (NULL);

  play->loop = g_main_loop_new (NULL, FALSE);
  play->desired_state = GST_STATE_PLAYING;

  play->signal_adapter
      = gst_play_signal_adapter_new_with_main_context (play->player,
      g_main_loop_get_context (play->loop));

  g_signal_connect (play->signal_adapter, "position-updated",
      G_CALLBACK (position_updated_cb), play);
  g_signal_connect (play->signal_adapter, "state-changed",
      G_CALLBACK (state_changed_cb), play);
  g_signal_connect (play->signal_adapter, "buffering",
      G_CALLBACK (buffering_cb), play);
  g_signal_connect (play->signal_adapter, "end-of-stream",
      G_CALLBACK (end_of_stream_cb), play);
  g_signal_connect (play->signal_adapter, "error", G_CALLBACK (error_cb), play);

  g_signal_connect (play->signal_adapter, "media-info-updated",
      G_CALLBACK (media_info_cb), play);

  play_set_relative_volume (play, initial_volume - 1.0);

  return play;
}

static void
play_free (Player * play)
{
  GstBus *bus = NULL;

  play_reset (play);

  bus = gst_play_get_message_bus (play->player);
  gst_bus_set_flushing (bus, TRUE);
  gst_object_unref (bus);

  g_clear_object (&play->signal_adapter);
  gst_object_unref (play->player);

  g_main_loop_unref (play->loop);

  g_strfreev (play->uris);
  g_free (play);
}

/* reset for new file/stream */
static void
play_reset (Player * play)
{
}

static void
play_set_relative_volume (Player * play, gdouble volume_step)
{
  gdouble volume;

  g_object_get (play->player, "volume", &volume, NULL);
  volume = round ((volume + volume_step) * VOLUME_STEPS) / VOLUME_STEPS;
  volume = CLAMP (volume, 0.0, 10.0);

  g_object_set (play->player, "volume", volume, NULL);

  gst_print ("Volume: %.0f%%                  \n", volume * 100);
}

static gchar *
play_uri_get_display_name (Player * play, const gchar * uri)
{
  gchar *loc;

  if (gst_uri_has_protocol (uri, "file")) {
    loc = g_filename_from_uri (uri, NULL, NULL);
  } else if (gst_uri_has_protocol (uri, "pushfile")) {
    loc = g_filename_from_uri (uri + 4, NULL, NULL);
  } else {
    loc = g_strdup (uri);
  }

  /* Maybe additionally use glib's filename to display name function */
  return loc;
}

static void
play_uri (Player * play, const gchar * next_uri)
{
  gchar *loc;

  play_reset (play);

  loc = play_uri_get_display_name (play, next_uri);
  gst_print ("Now playing %s\n", loc);
  g_free (loc);

  g_object_set (play->player, "uri", next_uri, NULL);
  gst_play_play (play->player);
}

/* returns FALSE if we have reached the end of the playlist */
static gboolean
play_next (Player * play)
{
  if ((play->cur_idx + 1) >= play->num_uris) {
    if (play->repeat) {
      gst_print ("Looping playlist \n");
      play->cur_idx = -1;
    } else
      return FALSE;
  }

  play_uri (play, play->uris[++play->cur_idx]);
  return TRUE;
}

/* returns FALSE if we have reached the beginning of the playlist */
static gboolean
play_prev (Player * play)
{
  if (play->cur_idx == 0 || play->num_uris <= 1)
    return FALSE;

  play_uri (play, play->uris[--play->cur_idx]);
  return TRUE;
}

static void
do_play (Player * play)
{
  gint i;

  /* dump playlist */
  for (i = 0; i < play->num_uris; ++i)
    GST_INFO ("%4u : %s", i, play->uris[i]);

  if (!play_next (play))
    return;

  g_main_loop_run (play->loop);
}

static void
add_to_playlist (GPtrArray * playlist, const gchar * filename)
{
  GDir *dir;
  gchar *uri;

  if (gst_uri_is_valid (filename)) {
    g_ptr_array_add (playlist, g_strdup (filename));
    return;
  }

  if ((dir = g_dir_open (filename, 0, NULL))) {
    const gchar *entry;

    /* FIXME: sort entries for each directory? */
    while ((entry = g_dir_read_name (dir))) {
      gchar *path;

      path = g_strconcat (filename, G_DIR_SEPARATOR_S, entry, NULL);
      add_to_playlist (playlist, path);
      g_free (path);
    }

    g_dir_close (dir);
    return;
  }

  uri = gst_filename_to_uri (filename, NULL);
  if (uri != NULL)
    g_ptr_array_add (playlist, uri);
  else
    g_warning ("Could not make URI out of filename '%s'", filename);
}

static void
shuffle_uris (gchar ** uris, guint num)
{
  gchar *tmp;
  guint i, j;

  if (num < 2)
    return;

  for (i = 0; i < num; i++) {
    /* gets equally distributed random number in 0..num-1 [0;num[ */
    j = g_random_int_range (0, num);
    tmp = uris[j];
    uris[j] = uris[i];
    uris[i] = tmp;
  }
}

static void
restore_terminal (void)
{
  gst_play_kb_set_key_handler (NULL, NULL);
}

static void
toggle_paused (Player * play)
{
  if (play->desired_state == GST_STATE_PLAYING) {
    play->desired_state = GST_STATE_PAUSED;
    gst_play_pause (play->player);
  } else {
    play->desired_state = GST_STATE_PLAYING;
    gst_play_play (play->player);
  }
}

static void
relative_seek (Player * play, gdouble percent)
{
  gint64 dur = -1, pos = -1;

  g_return_if_fail (percent >= -1.0 && percent <= 1.0);

  g_object_get (play->player, "position", &pos, "duration", &dur, NULL);

  if (dur <= 0) {
    gst_print ("\nCould not seek.\n");
    return;
  }

  pos = pos + dur * percent;
  if (pos < 0)
    pos = 0;
  gst_play_seek (play->player, pos);
}

static void
keyboard_cb (const gchar * key_input, gpointer user_data)
{
  Player *play = (Player *) user_data;

  switch (g_ascii_tolower (key_input[0])) {
    case 'i':
    {
      GstPlayMediaInfo *media_info = gst_play_get_media_info (play->player);
      if (media_info) {
        print_media_info (media_info);
        g_object_unref (media_info);
        print_current_tracks (play);
      }
      break;
    }
    case ' ':
      toggle_paused (play);
      break;
    case 'q':
    case 'Q':
      g_main_loop_quit (play->loop);
      break;
    case '>':
      if (!play_next (play)) {
        gst_print ("\nReached end of play list.\n");
        g_main_loop_quit (play->loop);
      }
      break;
    case '<':
      play_prev (play);
      break;
    case 27:                   /* ESC */
      if (key_input[1] == '\0') {
        g_main_loop_quit (play->loop);
        break;
      }
      /* fall through */
    default:
      if (strcmp (key_input, GST_PLAY_KB_ARROW_RIGHT) == 0) {
        relative_seek (play, +0.08);
      } else if (strcmp (key_input, GST_PLAY_KB_ARROW_LEFT) == 0) {
        relative_seek (play, -0.01);
      } else if (strcmp (key_input, GST_PLAY_KB_ARROW_UP) == 0) {
        play_set_relative_volume (play, +1.0 / VOLUME_STEPS);
      } else if (strcmp (key_input, GST_PLAY_KB_ARROW_DOWN) == 0) {
        play_set_relative_volume (play, -1.0 / VOLUME_STEPS);
      } else {
        GST_INFO ("keyboard input:");
        for (; *key_input != '\0'; ++key_input)
          GST_INFO ("  code %3d", *key_input);
      }
      break;
  }
}

int
main (int argc, char **argv)
{
  Player *play;
  GPtrArray *playlist;
  gboolean print_version = FALSE;
  gboolean interactive = FALSE; /* FIXME: maybe enable by default? */
  gboolean shuffle = FALSE;
  gboolean repeat = FALSE;
  gdouble volume = 1.0;
  gchar **filenames = NULL;
  gchar **uris;
  guint num, i;
  GError *err = NULL;
  GOptionContext *ctx;
  gchar *playlist_file = NULL;
  GOptionEntry options[] = {
    {"version", 0, 0, G_OPTION_ARG_NONE, &print_version,
        "Print version information and exit", NULL},
    {"shuffle", 0, 0, G_OPTION_ARG_NONE, &shuffle,
        "Shuffle playlist", NULL},
    {"interactive", 0, 0, G_OPTION_ARG_NONE, &interactive,
        "Interactive control via keyboard", NULL},
    {"volume", 0, 0, G_OPTION_ARG_DOUBLE, &volume,
        "Volume", NULL},
    {"playlist", 0, 0, G_OPTION_ARG_FILENAME, &playlist_file,
        "Playlist file containing input media files", NULL},
    {"loop", 0, 0, G_OPTION_ARG_NONE, &repeat, "Repeat all", NULL},
    {G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &filenames, NULL},
    {NULL}
  };

  g_set_prgname ("gst-play");

  ctx = g_option_context_new ("FILE1|URI1 [FILE2|URI2] [FILE3|URI3] ...");
  g_option_context_add_main_entries (ctx, options, NULL);
  g_option_context_add_group (ctx, gst_init_get_option_group ());
  if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
    gst_print ("Error initializing: %s\n", GST_STR_NULL (err->message));
    g_clear_error (&err);
    g_option_context_free (ctx);
    return 1;
  }
  g_option_context_free (ctx);

  GST_DEBUG_CATEGORY_INIT (play_debug, "play", 0, "gst-play");

  if (print_version) {
    gchar *version_str;

    version_str = gst_version_string ();
    gst_print ("%s version %s\n", g_get_prgname (), "1.0");
    gst_print ("%s\n", version_str);
    g_free (version_str);

    g_free (playlist_file);

    return 0;
  }

  playlist = g_ptr_array_new ();

  if (playlist_file != NULL) {
    gchar *playlist_contents = NULL;
    gchar **lines = NULL;

    if (g_file_get_contents (playlist_file, &playlist_contents, NULL, &err)) {
      lines = g_strsplit (playlist_contents, "\n", 0);
      num = g_strv_length (lines);

      for (i = 0; i < num; i++) {
        if (lines[i][0] != '\0') {
          GST_LOG ("Playlist[%d]: %s", i + 1, lines[i]);
          add_to_playlist (playlist, lines[i]);
        }
      }
      g_strfreev (lines);
      g_free (playlist_contents);
    } else {
      gst_printerr ("Could not read playlist: %s\n", err->message);
      g_clear_error (&err);
    }
    g_free (playlist_file);
    playlist_file = NULL;
  }

  if (playlist->len == 0 && (filenames == NULL || *filenames == NULL)) {
    gst_printerr ("Usage: %s FILE1|URI1 [FILE2|URI2] [FILE3|URI3] ...",
        "gst-play");
    gst_printerr ("\n\n"),
        gst_printerr ("%s\n\n",
        "You must provide at least one filename or URI to play.");
    /* No input provided. Free array */
    g_ptr_array_free (playlist, TRUE);

    return 1;
  }

  /* fill playlist */
  if (filenames != NULL && *filenames != NULL) {
    num = g_strv_length (filenames);
    for (i = 0; i < num; ++i) {
      GST_LOG ("command line argument: %s", filenames[i]);
      add_to_playlist (playlist, filenames[i]);
    }
    g_strfreev (filenames);
  }

  num = playlist->len;
  g_ptr_array_add (playlist, NULL);

  uris = (gchar **) g_ptr_array_free (playlist, FALSE);

  if (shuffle)
    shuffle_uris (uris, num);

  /* prepare */
  play = play_new (uris, volume);
  play->repeat = repeat;

  if (interactive) {
    if (gst_play_kb_set_key_handler (keyboard_cb, play)) {
      atexit (restore_terminal);
    } else {
      gst_print ("Interactive keyboard handling in terminal not available.\n");
    }
  }

  /* play */
  do_play (play);

  /* clean up */
  play_free (play);

  gst_print ("\n");
  gst_deinit ();
  return 0;
}
