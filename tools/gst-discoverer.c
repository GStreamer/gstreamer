/* GStreamer
 * Copyright (C) 2009 Edward Hervey <edward.hervey@collabora.co.uk>
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

#include <stdlib.h>
#include <glib.h>
#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>

/* *INDENT-OFF* */
static void my_g_string_append_printf (GString * str, int depth, const gchar * format, ...) G_GNUC_PRINTF (3, 4);
/* *INDENT-ON* */

static gboolean async = FALSE;
static gboolean show_toc = FALSE;
static gboolean verbose = FALSE;

typedef struct
{
  GstDiscoverer *dc;
  int argc;
  char **argv;
} PrivStruct;

static void
my_g_string_append_printf (GString * str, int depth, const gchar * format, ...)
{
  va_list args;

  while (depth-- > 0) {
    g_string_append (str, "  ");
  }

  va_start (args, format);
  g_string_append_vprintf (str, format, args);
  va_end (args);
}

static void
gst_stream_information_to_string (GstDiscovererStreamInfo * info, GString * s,
    gint depth)
{
  gchar *tmp;
  GstCaps *caps;
  const GstStructure *misc;

  my_g_string_append_printf (s, depth, "Codec:\n");
  caps = gst_discoverer_stream_info_get_caps (info);
  tmp = gst_caps_to_string (caps);
  gst_caps_unref (caps);
  my_g_string_append_printf (s, depth, "  %s\n", tmp);
  g_free (tmp);

  my_g_string_append_printf (s, depth, "Additional info:\n");
  if ((misc = gst_discoverer_stream_info_get_misc (info))) {
    tmp = gst_structure_to_string (misc);
    my_g_string_append_printf (s, depth, "  %s\n", tmp);
    g_free (tmp);
  } else {
    my_g_string_append_printf (s, depth, "  None\n");
  }

  my_g_string_append_printf (s, depth, "Stream ID: %s\n",
      gst_discoverer_stream_info_get_stream_id (info));
}

static gchar *
gst_stream_audio_information_to_string (GstDiscovererStreamInfo * info,
    gint depth)
{
  GstDiscovererAudioInfo *audio_info;
  GString *s;
  gchar *tmp;
  const gchar *ctmp;
  int len = 400;
  const GstTagList *tags;

  g_return_val_if_fail (info != NULL, NULL);

  s = g_string_sized_new (len);

  gst_stream_information_to_string (info, s, depth);

  audio_info = (GstDiscovererAudioInfo *) info;
  ctmp = gst_discoverer_audio_info_get_language (audio_info);
  my_g_string_append_printf (s, depth, "Language: %s\n",
      ctmp ? ctmp : "<unknown>");
  my_g_string_append_printf (s, depth, "Channels: %u\n",
      gst_discoverer_audio_info_get_channels (audio_info));
  my_g_string_append_printf (s, depth, "Sample rate: %u\n",
      gst_discoverer_audio_info_get_sample_rate (audio_info));
  my_g_string_append_printf (s, depth, "Depth: %u\n",
      gst_discoverer_audio_info_get_depth (audio_info));

  my_g_string_append_printf (s, depth, "Bitrate: %u\n",
      gst_discoverer_audio_info_get_bitrate (audio_info));
  my_g_string_append_printf (s, depth, "Max bitrate: %u\n",
      gst_discoverer_audio_info_get_max_bitrate (audio_info));

  my_g_string_append_printf (s, depth, "Tags:\n");
  tags = gst_discoverer_stream_info_get_tags (info);
  if (tags) {
    tmp = gst_tag_list_to_string (tags);
    my_g_string_append_printf (s, depth, "  %s\n", tmp);
    g_free (tmp);
  } else {
    my_g_string_append_printf (s, depth, "  None\n");
  }
  if (verbose)
    my_g_string_append_printf (s, depth, "\n");

  return g_string_free (s, FALSE);
}

static gchar *
gst_stream_video_information_to_string (GstDiscovererStreamInfo * info,
    gint depth)
{
  GstDiscovererVideoInfo *video_info;
  GString *s;
  gchar *tmp;
  int len = 500;
  const GstTagList *tags;

  g_return_val_if_fail (info != NULL, NULL);

  s = g_string_sized_new (len);

  gst_stream_information_to_string (info, s, depth);

  video_info = (GstDiscovererVideoInfo *) info;
  my_g_string_append_printf (s, depth, "Width: %u\n",
      gst_discoverer_video_info_get_width (video_info));
  my_g_string_append_printf (s, depth, "Height: %u\n",
      gst_discoverer_video_info_get_height (video_info));
  my_g_string_append_printf (s, depth, "Depth: %u\n",
      gst_discoverer_video_info_get_depth (video_info));

  my_g_string_append_printf (s, depth, "Frame rate: %u/%u\n",
      gst_discoverer_video_info_get_framerate_num (video_info),
      gst_discoverer_video_info_get_framerate_denom (video_info));

  my_g_string_append_printf (s, depth, "Pixel aspect ratio: %u/%u\n",
      gst_discoverer_video_info_get_par_num (video_info),
      gst_discoverer_video_info_get_par_denom (video_info));

  my_g_string_append_printf (s, depth, "Interlaced: %s\n",
      gst_discoverer_video_info_is_interlaced (video_info) ? "true" : "false");

  my_g_string_append_printf (s, depth, "Bitrate: %u\n",
      gst_discoverer_video_info_get_bitrate (video_info));
  my_g_string_append_printf (s, depth, "Max bitrate: %u\n",
      gst_discoverer_video_info_get_max_bitrate (video_info));

  my_g_string_append_printf (s, depth, "Tags:\n");
  tags = gst_discoverer_stream_info_get_tags (info);
  if (tags) {
    tmp = gst_tag_list_to_string (tags);
    my_g_string_append_printf (s, depth, "  %s\n", tmp);
    g_free (tmp);
  } else {
    my_g_string_append_printf (s, depth, "  None\n");
  }
  if (verbose)
    my_g_string_append_printf (s, depth, "\n");

  return g_string_free (s, FALSE);
}

static gchar *
gst_stream_subtitle_information_to_string (GstDiscovererStreamInfo * info,
    gint depth)
{
  GstDiscovererSubtitleInfo *subtitle_info;
  GString *s;
  gchar *tmp;
  const gchar *ctmp;
  int len = 400;
  const GstTagList *tags;

  g_return_val_if_fail (info != NULL, NULL);

  s = g_string_sized_new (len);

  gst_stream_information_to_string (info, s, depth);

  subtitle_info = (GstDiscovererSubtitleInfo *) info;
  ctmp = gst_discoverer_subtitle_info_get_language (subtitle_info);
  my_g_string_append_printf (s, depth, "Language: %s\n",
      ctmp ? ctmp : "<unknown>");

  my_g_string_append_printf (s, depth, "Tags:\n");
  tags = gst_discoverer_stream_info_get_tags (info);
  if (tags) {
    tmp = gst_tag_list_to_string (tags);
    my_g_string_append_printf (s, depth, "  %s\n", tmp);
    g_free (tmp);
  } else {
    my_g_string_append_printf (s, depth, "  None\n");
  }
  if (verbose)
    my_g_string_append_printf (s, depth, "\n");

  return g_string_free (s, FALSE);
}

static void
print_stream_info (GstDiscovererStreamInfo * info, void *depth)
{
  gchar *desc = NULL;
  GstCaps *caps;

  caps = gst_discoverer_stream_info_get_caps (info);

  if (caps) {
    if (gst_caps_is_fixed (caps) && !verbose)
      desc = gst_pb_utils_get_codec_description (caps);
    else
      desc = gst_caps_to_string (caps);
    gst_caps_unref (caps);
  }

  g_print ("%*s%s: %s\n", 2 * GPOINTER_TO_INT (depth), " ",
      gst_discoverer_stream_info_get_stream_type_nick (info), desc);

  if (desc) {
    g_free (desc);
    desc = NULL;
  }

  if (verbose) {
    if (GST_IS_DISCOVERER_AUDIO_INFO (info))
      desc =
          gst_stream_audio_information_to_string (info,
          GPOINTER_TO_INT (depth) + 1);
    else if (GST_IS_DISCOVERER_VIDEO_INFO (info))
      desc =
          gst_stream_video_information_to_string (info,
          GPOINTER_TO_INT (depth) + 1);
    else if (GST_IS_DISCOVERER_SUBTITLE_INFO (info))
      desc =
          gst_stream_subtitle_information_to_string (info,
          GPOINTER_TO_INT (depth) + 1);
    if (desc) {
      g_print ("%s", desc);
      g_free (desc);
    }
  }
}

static void
print_topology (GstDiscovererStreamInfo * info, gint depth)
{
  GstDiscovererStreamInfo *next;

  if (!info)
    return;

  print_stream_info (info, GINT_TO_POINTER (depth));

  next = gst_discoverer_stream_info_get_next (info);
  if (next) {
    print_topology (next, depth + 1);
    gst_discoverer_stream_info_unref (next);
  } else if (GST_IS_DISCOVERER_CONTAINER_INFO (info)) {
    GList *tmp, *streams;

    streams =
        gst_discoverer_container_info_get_streams (GST_DISCOVERER_CONTAINER_INFO
        (info));
    for (tmp = streams; tmp; tmp = tmp->next) {
      GstDiscovererStreamInfo *tmpinf = (GstDiscovererStreamInfo *) tmp->data;
      print_topology (tmpinf, depth + 1);
    }
    gst_discoverer_stream_info_list_free (streams);
  }
}

static void
print_tag_foreach (const GstTagList * tags, const gchar * tag,
    gpointer user_data)
{
  GValue val = { 0, };
  gchar *str;
  gint depth = GPOINTER_TO_INT (user_data);

  gst_tag_list_copy_value (&val, tags, tag);

  if (G_VALUE_HOLDS_STRING (&val))
    str = g_value_dup_string (&val);
  else
    str = gst_value_serialize (&val);

  g_print ("%*s%s: %s\n", 2 * depth, " ", gst_tag_get_nick (tag), str);
  g_free (str);

  g_value_unset (&val);
}

#define MAX_INDENT 40

static void
print_toc_entry (gpointer data, gpointer user_data)
{
  GstTocEntry *entry = (GstTocEntry *) data;
  gint depth = GPOINTER_TO_INT (user_data);
  guint indent = MIN (GPOINTER_TO_UINT (user_data), MAX_INDENT);
  GstTagList *tags;
  GList *subentries;
  gint64 start, stop;

  gst_toc_entry_get_start_stop_times (entry, &start, &stop);
  g_print ("%*s%s: start: %" GST_TIME_FORMAT " stop: %" GST_TIME_FORMAT "\n",
      depth, " ",
      gst_toc_entry_type_get_nick (gst_toc_entry_get_entry_type (entry)),
      GST_TIME_ARGS (start), GST_TIME_ARGS (stop));
  indent += 2;

  /* print tags */
  tags = gst_toc_entry_get_tags (entry);
  if (tags) {
    g_print ("%*sTags:\n", 2 * depth, " ");
    gst_tag_list_foreach (tags, print_tag_foreach, GUINT_TO_POINTER (indent));
  }

  /* loop over sub-toc entries */
  subentries = gst_toc_entry_get_sub_entries (entry);
  g_list_foreach (subentries, print_toc_entry, GUINT_TO_POINTER (indent));
}

static void
print_properties (GstDiscovererInfo * info, gint tab)
{
  const GstTagList *tags;
  const GstToc *toc;

  g_print ("%*sDuration: %" GST_TIME_FORMAT "\n", tab + 1, " ",
      GST_TIME_ARGS (gst_discoverer_info_get_duration (info)));
  g_print ("%*sSeekable: %s\n", tab + 1, " ",
      (gst_discoverer_info_get_seekable (info) ? "yes" : "no"));
  if ((tags = gst_discoverer_info_get_tags (info))) {
    g_print ("%*sTags: \n", tab + 1, " ");
    gst_tag_list_foreach (tags, print_tag_foreach, GUINT_TO_POINTER (tab + 2));
  }
  if (show_toc && (toc = gst_discoverer_info_get_toc (info))) {
    GList *entries;

    g_print ("%*sTOC: \n", tab + 1, " ");
    entries = gst_toc_get_entries (toc);
    g_list_foreach (entries, print_toc_entry, GUINT_TO_POINTER (tab + 5));
  }
}

static void
print_info (GstDiscovererInfo * info, GError * err)
{
  GstDiscovererResult result = gst_discoverer_info_get_result (info);
  GstDiscovererStreamInfo *sinfo;

  g_print ("Done discovering %s\n", gst_discoverer_info_get_uri (info));
  switch (result) {
    case GST_DISCOVERER_OK:
    {
      break;
    }
    case GST_DISCOVERER_URI_INVALID:
    {
      g_print ("URI is not valid\n");
      break;
    }
    case GST_DISCOVERER_ERROR:
    {
      g_print ("An error was encountered while discovering the file\n");
      g_print (" %s\n", err->message);
      break;
    }
    case GST_DISCOVERER_TIMEOUT:
    {
      g_print ("Analyzing URI timed out\n");
      break;
    }
    case GST_DISCOVERER_BUSY:
    {
      g_print ("Discoverer was busy\n");
      break;
    }
    case GST_DISCOVERER_MISSING_PLUGINS:
    {
      g_print ("Missing plugins\n");
      if (verbose) {
        gint i = 0;
        const gchar **installer_details =
            gst_discoverer_info_get_missing_elements_installer_details (info);

        while (installer_details[i]) {
          g_print (" (%s)\n", installer_details[i]);

          i++;
        }
      }
      break;
    }
  }

  if ((sinfo = gst_discoverer_info_get_stream_info (info))) {
    g_print ("\nTopology:\n");
    print_topology (sinfo, 1);
    g_print ("\nProperties:\n");
    print_properties (info, 1);
    gst_discoverer_stream_info_unref (sinfo);
  }

  g_print ("\n");
}

static void
process_file (GstDiscoverer * dc, const gchar * filename)
{
  GError *err = NULL;
  GDir *dir;
  gchar *uri, *path;
  GstDiscovererInfo *info;

  if (!gst_uri_is_valid (filename)) {
    /* Recurse into directories */
    if ((dir = g_dir_open (filename, 0, NULL))) {
      const gchar *entry;

      while ((entry = g_dir_read_name (dir))) {
        gchar *path;
        path = g_strconcat (filename, G_DIR_SEPARATOR_S, entry, NULL);
        process_file (dc, path);
        g_free (path);
      }

      g_dir_close (dir);
      return;
    }

    if (!g_path_is_absolute (filename)) {
      gchar *cur_dir;

      cur_dir = g_get_current_dir ();
      path = g_build_filename (cur_dir, filename, NULL);
      g_free (cur_dir);
    } else {
      path = g_strdup (filename);
    }

    uri = g_filename_to_uri (path, NULL, &err);
    g_free (path);
    path = NULL;

    if (err) {
      g_warning ("Couldn't convert filename to URI: %s\n", err->message);
      g_error_free (err);
      return;
    }
  } else {
    uri = g_strdup (filename);
  }

  if (async == FALSE) {
    g_print ("Analyzing %s\n", uri);
    info = gst_discoverer_discover_uri (dc, uri, &err);
    print_info (info, err);
    if (err)
      g_error_free (err);
    gst_discoverer_info_unref (info);
  } else {
    gst_discoverer_discover_uri_async (dc, uri);
  }

  g_free (uri);
}

static void
_new_discovered_uri (GstDiscoverer * dc, GstDiscovererInfo * info, GError * err)
{
  print_info (info, err);
}

static gboolean
_run_async (PrivStruct * ps)
{
  gint i;

  for (i = 1; i < ps->argc; i++)
    process_file (ps->dc, ps->argv[i]);

  return FALSE;
}

static void
_discoverer_finished (GstDiscoverer * dc, GMainLoop * ml)
{
  g_main_loop_quit (ml);
}

int
main (int argc, char **argv)
{
  GError *err = NULL;
  GstDiscoverer *dc;
  gint timeout = 10;
  GOptionEntry options[] = {
    {"async", 'a', 0, G_OPTION_ARG_NONE, &async,
        "Run asynchronously", NULL},
    {"timeout", 't', 0, G_OPTION_ARG_INT, &timeout,
        "Specify timeout (in seconds, default 10)", "T"},
    /* {"elem", 'e', 0, G_OPTION_ARG_NONE, &elem_seek, */
    /*     "Seek on elements instead of pads", NULL}, */
    {"toc", 'c', 0, G_OPTION_ARG_NONE, &show_toc,
        "Output TOC (chapters and editions)", NULL},
    {"verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
        "Verbose properties", NULL},
    {NULL}
  };
  GOptionContext *ctx;

  setlocale (LC_ALL, "");

  ctx =
      g_option_context_new
      ("- discover files synchronously with GstDiscoverer");
  g_option_context_add_main_entries (ctx, options, NULL);
  g_option_context_add_group (ctx, gst_init_get_option_group ());

  if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
    g_print ("Error initializing: %s\n", err->message);
    exit (1);
  }

  g_option_context_free (ctx);

  if (argc < 2) {
    g_print ("usage: %s <uris>\n", argv[0]);
    exit (-1);
  }

  dc = gst_discoverer_new (timeout * GST_SECOND, &err);
  if (G_UNLIKELY (dc == NULL)) {
    g_print ("Error initializing: %s\n", err->message);
    exit (1);
  }

  if (async == FALSE) {
    gint i;
    for (i = 1; i < argc; i++)
      process_file (dc, argv[i]);
  } else {
    PrivStruct *ps = g_new0 (PrivStruct, 1);
    GMainLoop *ml = g_main_loop_new (NULL, FALSE);

    ps->dc = dc;
    ps->argc = argc;
    ps->argv = argv;

    /* adding uris will be started when the mainloop runs */
    g_idle_add ((GSourceFunc) _run_async, ps);

    /* connect signals */
    g_signal_connect (dc, "discovered", G_CALLBACK (_new_discovered_uri), NULL);
    g_signal_connect (dc, "finished", G_CALLBACK (_discoverer_finished), ml);

    gst_discoverer_start (dc);
    /* run mainloop */
    g_main_loop_run (ml);

    gst_discoverer_stop (dc);
    g_free (ps);
    g_main_loop_unref (ml);
  }
  g_object_unref (dc);

  return 0;
}
