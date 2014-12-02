/* GStreamer command line playback testing utility
 *
 * Copyright (C) 2013-2014 Tim-Philipp Müller <tim centricular net>
 * Copyright (C) 2013 Collabora Ltd.
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <locale.h>

#include <gst/gst.h>
#include <gst/gst-i18n-app.h>
#include <gst/audio/audio.h>
#include <gst/pbutils/pbutils.h>
#include <gst/math-compat.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "gst-play-kb.h"

#define VOLUME_STEPS 20

GST_DEBUG_CATEGORY (play_debug);
#define GST_CAT_DEFAULT play_debug

typedef struct
{
  gchar **uris;
  guint num_uris;
  gint cur_idx;

  GstElement *playbin;

  GMainLoop *loop;
  guint bus_watch;
  guint timeout;

  /* missing plugin messages */
  GList *missing;

  gboolean buffering;
  gboolean is_live;

  GstState desired_state;       /* as per user interaction, PAUSED or PLAYING */

  /* configuration */
  gboolean gapless;
} GstPlay;

static gboolean play_bus_msg (GstBus * bus, GstMessage * msg, gpointer data);
static gboolean play_next (GstPlay * play);
static gboolean play_prev (GstPlay * play);
static gboolean play_timeout (gpointer user_data);
static void play_about_to_finish (GstElement * playbin, gpointer user_data);
static void play_reset (GstPlay * play);
static void play_set_relative_volume (GstPlay * play, gdouble volume_step);

static GstPlay *
play_new (gchar ** uris, const gchar * audio_sink, const gchar * video_sink,
    gboolean gapless, gdouble initial_volume)
{
  GstElement *sink;
  GstPlay *play;

  play = g_new0 (GstPlay, 1);

  play->uris = uris;
  play->num_uris = g_strv_length (uris);
  play->cur_idx = -1;

  play->playbin = gst_element_factory_make ("playbin", "playbin");

  if (audio_sink != NULL) {
    if (strchr (audio_sink, ' ') != NULL)
      sink = gst_parse_bin_from_description (audio_sink, TRUE, NULL);
    else
      sink = gst_element_factory_make (audio_sink, NULL);

    if (sink != NULL)
      g_object_set (play->playbin, "audio-sink", sink, NULL);
    else
      g_warning ("Couldn't create specified audio sink '%s'", audio_sink);
  }
  if (video_sink != NULL) {
    if (strchr (video_sink, ' ') != NULL)
      sink = gst_parse_bin_from_description (video_sink, TRUE, NULL);
    else
      sink = gst_element_factory_make (video_sink, NULL);

    if (sink != NULL)
      g_object_set (play->playbin, "video-sink", sink, NULL);
    else
      g_warning ("Couldn't create specified video sink '%s'", video_sink);
  }

  play->loop = g_main_loop_new (NULL, FALSE);

  play->bus_watch = gst_bus_add_watch (GST_ELEMENT_BUS (play->playbin),
      play_bus_msg, play);

  /* FIXME: make configurable incl. 0 for disable */
  play->timeout = g_timeout_add (100, play_timeout, play);

  play->missing = NULL;
  play->buffering = FALSE;
  play->is_live = FALSE;

  play->desired_state = GST_STATE_PLAYING;

  play->gapless = gapless;
  if (gapless) {
    g_signal_connect (play->playbin, "about-to-finish",
        G_CALLBACK (play_about_to_finish), play);
  }

  if (initial_volume != -1)
    play_set_relative_volume (play, initial_volume - 1.0);

  return play;
}

static void
play_free (GstPlay * play)
{
  play_reset (play);

  gst_element_set_state (play->playbin, GST_STATE_NULL);
  gst_object_unref (play->playbin);

  g_source_remove (play->bus_watch);
  g_source_remove (play->timeout);
  g_main_loop_unref (play->loop);

  g_strfreev (play->uris);
  g_free (play);
}

/* reset for new file/stream */
static void
play_reset (GstPlay * play)
{
  g_list_foreach (play->missing, (GFunc) gst_message_unref, NULL);
  play->missing = NULL;

  play->buffering = FALSE;
  play->is_live = FALSE;
}

static void
play_set_relative_volume (GstPlay * play, gdouble volume_step)
{
  gdouble volume;

  volume = gst_stream_volume_get_volume (GST_STREAM_VOLUME (play->playbin),
      GST_STREAM_VOLUME_FORMAT_CUBIC);

  volume = round ((volume + volume_step) * VOLUME_STEPS) / VOLUME_STEPS;
  volume = CLAMP (volume, 0.0, 10.0);

  gst_stream_volume_set_volume (GST_STREAM_VOLUME (play->playbin),
      GST_STREAM_VOLUME_FORMAT_CUBIC, volume);

  g_print ("Volume: %.0f%%                  \n", volume * 100);
}

/* returns TRUE if something was installed and we should restart playback */
static gboolean
play_install_missing_plugins (GstPlay * play)
{
  /* FIXME: implement: try to install any missing plugins we haven't
   * tried to install before */
  return FALSE;
}

static gboolean
play_bus_msg (GstBus * bus, GstMessage * msg, gpointer user_data)
{
  GstPlay *play = user_data;

  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_ASYNC_DONE:

      /* dump graph on preroll */
      GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (play->playbin),
          GST_DEBUG_GRAPH_SHOW_ALL, "gst-play.async-done");

      g_print ("Prerolled.\r");
      if (play->missing != NULL && play_install_missing_plugins (play)) {
        g_print ("New plugins installed, trying again...\n");
        --play->cur_idx;
        play_next (play);
      }
      break;
    case GST_MESSAGE_BUFFERING:{
      gint percent;

      if (!play->buffering)
        g_print ("\n");

      gst_message_parse_buffering (msg, &percent);
      g_print ("%s %d%%  \r", _("Buffering..."), percent);

      if (percent == 100) {
        /* a 100% message means buffering is done */
        if (play->buffering) {
          play->buffering = FALSE;
          /* no state management needed for live pipelines */
          if (!play->is_live)
            gst_element_set_state (play->playbin, play->desired_state);
        }
      } else {
        /* buffering... */
        if (!play->buffering) {
          if (!play->is_live)
            gst_element_set_state (play->playbin, GST_STATE_PAUSED);
          play->buffering = TRUE;
        }
      }
      break;
    }
    case GST_MESSAGE_CLOCK_LOST:{
      g_print (_("Clock lost, selecting a new one\n"));
      gst_element_set_state (play->playbin, GST_STATE_PAUSED);
      gst_element_set_state (play->playbin, GST_STATE_PLAYING);
      break;
    }
    case GST_MESSAGE_LATENCY:
      g_print ("Redistribute latency...\n");
      gst_bin_recalculate_latency (GST_BIN (play->playbin));
      break;
    case GST_MESSAGE_REQUEST_STATE:{
      GstState state;
      gchar *name;

      name = gst_object_get_path_string (GST_MESSAGE_SRC (msg));

      gst_message_parse_request_state (msg, &state);

      g_print ("Setting state to %s as requested by %s...\n",
          gst_element_state_get_name (state), name);

      gst_element_set_state (play->playbin, state);
      g_free (name);
      break;
    }
    case GST_MESSAGE_EOS:
      /* print final position at end */
      play_timeout (play);
      g_print ("\n");
      /* and switch to next item in list */
      if (!play_next (play)) {
        g_print ("Reached end of play list.\n");
        g_main_loop_quit (play->loop);
      }
      break;
    case GST_MESSAGE_WARNING:{
      GError *err;
      gchar *dbg = NULL;

      /* dump graph on warning */
      GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (play->playbin),
          GST_DEBUG_GRAPH_SHOW_ALL, "gst-play.warning");

      gst_message_parse_warning (msg, &err, &dbg);
      g_printerr ("WARNING %s\n", err->message);
      if (dbg != NULL)
        g_printerr ("WARNING debug information: %s\n", dbg);
      g_error_free (err);
      g_free (dbg);
      break;
    }
    case GST_MESSAGE_ERROR:{
      GError *err;
      gchar *dbg;

      /* dump graph on error */
      GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (play->playbin),
          GST_DEBUG_GRAPH_SHOW_ALL, "gst-play.error");

      gst_message_parse_error (msg, &err, &dbg);
      g_printerr ("ERROR %s for %s\n", err->message, play->uris[play->cur_idx]);
      if (dbg != NULL)
        g_printerr ("ERROR debug information: %s\n", dbg);
      g_error_free (err);
      g_free (dbg);

      /* flush any other error messages from the bus and clean up */
      gst_element_set_state (play->playbin, GST_STATE_NULL);

      if (play->missing != NULL && play_install_missing_plugins (play)) {
        g_print ("New plugins installed, trying again...\n");
        --play->cur_idx;
        play_next (play);
        break;
      }
      /* try next item in list then */
      if (!play_next (play)) {
        g_print ("Reached end of play list.\n");
        g_main_loop_quit (play->loop);
      }
      break;
    }
    default:
      if (gst_is_missing_plugin_message (msg)) {
        gchar *desc;

        desc = gst_missing_plugin_message_get_description (msg);
        g_print ("Missing plugin: %s\n", desc);
        g_free (desc);
        play->missing = g_list_append (play->missing, gst_message_ref (msg));
      }
      break;
  }

  return TRUE;
}

static gboolean
play_timeout (gpointer user_data)
{
  GstPlay *play = user_data;
  gint64 pos = -1, dur = -1;
  gchar status[64] = { 0, };

  if (play->buffering)
    return TRUE;

  gst_element_query_position (play->playbin, GST_FORMAT_TIME, &pos);
  gst_element_query_duration (play->playbin, GST_FORMAT_TIME, &dur);

  if (play->desired_state == GST_STATE_PAUSED)
    g_snprintf (status, sizeof (status), "Paused");
  else
    memset (status, ' ', sizeof (status) - 1);

  if (pos >= 0 && dur > 0) {
    gchar dstr[32], pstr[32];

    /* FIXME: pretty print in nicer format */
    g_snprintf (pstr, 32, "%" GST_TIME_FORMAT, GST_TIME_ARGS (pos));
    pstr[9] = '\0';
    g_snprintf (dstr, 32, "%" GST_TIME_FORMAT, GST_TIME_ARGS (dur));
    dstr[9] = '\0';
    g_print ("%s / %s %s\r", pstr, dstr, status);
  }

  return TRUE;
}

static gchar *
play_uri_get_display_name (GstPlay * play, const gchar * uri)
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
play_uri (GstPlay * play, const gchar * next_uri)
{
  GstStateChangeReturn sret;
  gchar *loc;

  gst_element_set_state (play->playbin, GST_STATE_READY);
  play_reset (play);

  loc = play_uri_get_display_name (play, next_uri);
  g_print ("Now playing %s\n", loc);
  g_free (loc);

  g_object_set (play->playbin, "uri", next_uri, NULL);

  sret = gst_element_set_state (play->playbin, GST_STATE_PAUSED);
  switch (sret) {
    case GST_STATE_CHANGE_FAILURE:
      /* ignore, we should get an error message posted on the bus */
      break;
    case GST_STATE_CHANGE_NO_PREROLL:
      g_print ("Pipeline is live.\n");
      play->is_live = TRUE;
      break;
    case GST_STATE_CHANGE_ASYNC:
      g_print ("Prerolling...\r");
      break;
    default:
      break;
  }
  if (play->desired_state != GST_STATE_PAUSED)
    sret = gst_element_set_state (play->playbin, play->desired_state);
}

/* returns FALSE if we have reached the end of the playlist */
static gboolean
play_next (GstPlay * play)
{
  if ((play->cur_idx + 1) >= play->num_uris)
    return FALSE;

  play_uri (play, play->uris[++play->cur_idx]);
  return TRUE;
}

/* returns FALSE if we have reached the beginning of the playlist */
static gboolean
play_prev (GstPlay * play)
{
  if (play->cur_idx == 0 || play->num_uris <= 1)
    return FALSE;

  play_uri (play, play->uris[--play->cur_idx]);
  return TRUE;
}

static void
play_about_to_finish (GstElement * playbin, gpointer user_data)
{
  GstPlay *play = user_data;
  const gchar *next_uri;
  gchar *loc;
  guint next_idx;

  if (!play->gapless)
    return;

  next_idx = play->cur_idx + 1;
  if (next_idx >= play->num_uris)
    return;

  next_uri = play->uris[next_idx];
  loc = play_uri_get_display_name (play, next_uri);
  g_print ("About to finish, preparing next title: %s\n", loc);
  g_free (loc);

  g_object_set (play->playbin, "uri", next_uri, NULL);
  play->cur_idx = next_idx;
}

static void
do_play (GstPlay * play)
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
toggle_paused (GstPlay * play)
{
  if (play->desired_state == GST_STATE_PLAYING)
    play->desired_state = GST_STATE_PAUSED;
  else
    play->desired_state = GST_STATE_PLAYING;

  if (!play->buffering) {
    gst_element_set_state (play->playbin, play->desired_state);
  } else if (play->desired_state == GST_STATE_PLAYING) {
    g_print ("\nWill play as soon as buffering finishes)\n");
  }
}

static void
relative_seek (GstPlay * play, gdouble percent)
{
  GstQuery *query;
  gboolean seekable = FALSE;
  gint64 dur = -1, pos = -1;

  g_return_if_fail (percent >= -1.0 && percent <= 1.0);

  if (!gst_element_query_position (play->playbin, GST_FORMAT_TIME, &pos))
    goto seek_failed;

  query = gst_query_new_seeking (GST_FORMAT_TIME);
  if (!gst_element_query (play->playbin, query)) {
    gst_query_unref (query);
    goto seek_failed;
  }

  gst_query_parse_seeking (query, NULL, &seekable, NULL, &dur);
  gst_query_unref (query);

  if (!seekable || dur <= 0)
    goto seek_failed;

  pos = pos + dur * percent;
  if (pos > dur) {
    if (!play_next (play)) {
      g_print ("\nReached end of play list.\n");
      g_main_loop_quit (play->loop);
    }
  } else {
    if (pos < 0)
      pos = 0;
    if (!gst_element_seek_simple (play->playbin, GST_FORMAT_TIME,
            GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT, pos))
      goto seek_failed;
  }

  return;

seek_failed:
  {
    g_print ("\nCould not seek.\n");
  }
}

static void
keyboard_cb (const gchar * key_input, gpointer user_data)
{
  GstPlay *play = (GstPlay *) user_data;

  switch (g_ascii_tolower (key_input[0])) {
    case ' ':
      toggle_paused (play);
      break;
    case 'q':
    case 'Q':
      g_main_loop_quit (play->loop);
      break;
    case '>':
      if (!play_next (play)) {
        g_print ("\nReached end of play list.\n");
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
  GstPlay *play;
  GPtrArray *playlist;
  gboolean print_version = FALSE;
  gboolean interactive = FALSE; /* FIXME: maybe enable by default? */
  gboolean gapless = FALSE;
  gboolean shuffle = FALSE;
  gdouble volume = -1;
  gchar **filenames = NULL;
  gchar *audio_sink = NULL;
  gchar *video_sink = NULL;
  gchar **uris;
  guint num, i;
  GError *err = NULL;
  GOptionContext *ctx;
  gchar *playlist_file = NULL;
  GOptionEntry options[] = {
    {"version", 0, 0, G_OPTION_ARG_NONE, &print_version,
        N_("Print version information and exit"), NULL},
    {"videosink", 0, 0, G_OPTION_ARG_STRING, &video_sink,
        N_("Video sink to use (default is autovideosink)"), NULL},
    {"audiosink", 0, 0, G_OPTION_ARG_STRING, &audio_sink,
        N_("Audio sink to use (default is autoaudiosink)"), NULL},
    {"gapless", 0, 0, G_OPTION_ARG_NONE, &gapless,
        N_("Enable gapless playback"), NULL},
    {"shuffle", 0, 0, G_OPTION_ARG_NONE, &shuffle,
        N_("Shuffle playlist"), NULL},
    {"interactive", 0, 0, G_OPTION_ARG_NONE, &interactive,
        N_("Interactive control via keyboard"), NULL},
    {"volume", 0, 0, G_OPTION_ARG_DOUBLE, &volume,
        N_("Volume"), NULL},
    {"playlist", 0, 0, G_OPTION_ARG_FILENAME, &playlist_file,
        N_("Playlist file containing input media files"), NULL},
    {G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &filenames, NULL},
    {NULL}
  };

  setlocale (LC_ALL, "");

#ifdef ENABLE_NLS
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);
#endif

  g_set_prgname ("gst-play-" GST_API_VERSION);

  ctx = g_option_context_new ("FILE1|URI1 [FILE2|URI2] [FILE3|URI3] ...");
  g_option_context_add_main_entries (ctx, options, GETTEXT_PACKAGE);
  g_option_context_add_group (ctx, gst_init_get_option_group ());
  if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
    g_print ("Error initializing: %s\n", GST_STR_NULL (err->message));
    return 1;
  }
  g_option_context_free (ctx);

  GST_DEBUG_CATEGORY_INIT (play_debug, "play", 0, "gst-play");

  if (print_version) {
    gchar *version_str;

    version_str = gst_version_string ();
    g_print ("%s version %s\n", g_get_prgname (), PACKAGE_VERSION);
    g_print ("%s\n", version_str);
    g_print ("%s\n", GST_PACKAGE_ORIGIN);
    g_free (version_str);

    g_free (audio_sink);
    g_free (video_sink);
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
      g_printerr ("Could not read playlist: %s\n", err->message);
      g_clear_error (&err);
    }
    g_free (playlist_file);
    playlist_file = NULL;
  }

  if (playlist->len == 0 && (filenames == NULL || *filenames == NULL)) {
    g_printerr (_("Usage: %s FILE1|URI1 [FILE2|URI2] [FILE3|URI3] ..."),
        "gst-play-" GST_API_VERSION);
    g_printerr ("\n\n"),
        g_printerr ("%s\n\n",
        _("You must provide at least one filename or URI to play."));
    /* No input provided. Free array */
    g_ptr_array_free (playlist, TRUE);

    g_free (audio_sink);
    g_free (video_sink);

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
  play = play_new (uris, audio_sink, video_sink, gapless, volume);

  if (interactive) {
    if (gst_play_kb_set_key_handler (keyboard_cb, play)) {
      atexit (restore_terminal);
    } else {
      g_print ("Interactive keyboard handling in terminal not available.\n");
    }
  }

  /* play */
  do_play (play);

  /* clean up */
  play_free (play);

  g_free (audio_sink);
  g_free (video_sink);

  g_print ("\n");
  return 0;
}
