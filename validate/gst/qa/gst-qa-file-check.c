/* GStreamer
 * Copyright (C) 2013 Thiago Santos <thiago.sousa.santos@collabora.com>
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdlib.h>
#include <string.h>

#include <gst/gst.h>
#include <gst/qa/qa.h>
#include <gst/pbutils/encoding-profile.h>

static GstEncodingProfile *encoding_profile = NULL;

/* move this into some utils file */
static gboolean
_parse_encoding_profile (const gchar * option_name, const gchar * value,
    gpointer udata, GError ** error)
{
  GstCaps *caps;
  char *preset_name = NULL;
  gchar **restriction_format, **preset_v;

  guint i, presence = 0;
  GstCaps *restrictioncaps = NULL;
  gchar **strpresence_v, **strcaps_v = g_strsplit (value, ":", 0);

  if (strcaps_v[0] && *strcaps_v[0]) {
    caps = gst_caps_from_string (strcaps_v[0]);
    if (caps == NULL) {
      g_printerr ("Could not parse caps %s", strcaps_v[0]);
      return FALSE;
    }
    encoding_profile =
        GST_ENCODING_PROFILE (gst_encoding_container_profile_new
        ("User profile", "User profile", caps, NULL));
    gst_caps_unref (caps);
  } else {
    encoding_profile = NULL;
  }

  for (i = 1; strcaps_v[i]; i++) {
    GstEncodingProfile *profile = NULL;
    gchar *strcaps, *strpresence;

    restriction_format = g_strsplit (strcaps_v[i], "->", 0);
    if (restriction_format[1]) {
      restrictioncaps = gst_caps_from_string (restriction_format[0]);
      strcaps = g_strdup (restriction_format[1]);
    } else {
      restrictioncaps = NULL;
      strcaps = g_strdup (restriction_format[0]);
    }
    g_strfreev (restriction_format);

    preset_v = g_strsplit (strcaps, "+", 0);
    if (preset_v[1]) {
      strpresence = preset_v[1];
      g_free (strcaps);
      strcaps = g_strdup (preset_v[0]);
    } else {
      strpresence = preset_v[0];
    }

    strpresence_v = g_strsplit (strpresence, "|", 0);
    if (strpresence_v[1]) {     /* We have a presence */
      gchar *endptr;

      if (preset_v[1]) {        /* We have preset and presence */
        preset_name = g_strdup (strpresence_v[0]);
      } else {                  /* We have a presence but no preset */
        g_free (strcaps);
        strcaps = g_strdup (strpresence_v[0]);
      }

      presence = strtoll (strpresence_v[1], &endptr, 10);
      if (endptr == strpresence_v[1]) {
        g_printerr ("Wrong presence %s\n", strpresence_v[1]);

        return FALSE;
      }
    } else {                    /* We have no presence */
      if (preset_v[1]) {        /* Not presence but preset */
        preset_name = g_strdup (preset_v[1]);
        g_free (strcaps);
        strcaps = g_strdup (preset_v[0]);
      }                         /* Else we have no presence nor preset */
    }
    g_strfreev (strpresence_v);
    g_strfreev (preset_v);

    GST_DEBUG ("Creating preset with restrictions: %" GST_PTR_FORMAT
        ", caps: %s, preset %s, presence %d", restrictioncaps, strcaps,
        preset_name ? preset_name : "none", presence);

    caps = gst_caps_from_string (strcaps);
    g_free (strcaps);
    if (caps == NULL) {
      g_warning ("Could not create caps for %s", strcaps_v[i]);

      return FALSE;
    }

    if (g_str_has_prefix (strcaps_v[i], "audio/")) {
      profile = GST_ENCODING_PROFILE (gst_encoding_audio_profile_new (caps,
              preset_name, restrictioncaps, presence));
    } else if (g_str_has_prefix (strcaps_v[i], "video/") ||
        g_str_has_prefix (strcaps_v[i], "image/")) {
      profile = GST_ENCODING_PROFILE (gst_encoding_video_profile_new (caps,
              preset_name, restrictioncaps, presence));
    }

    g_free (preset_name);
    gst_caps_unref (caps);
    if (restrictioncaps)
      gst_caps_unref (restrictioncaps);

    if (profile == NULL) {
      g_warning ("No way to create a preset for caps: %s", strcaps_v[i]);

      return FALSE;
    }

    if (encoding_profile) {
      if (gst_encoding_container_profile_add_profile
          (GST_ENCODING_CONTAINER_PROFILE (encoding_profile),
              profile) == FALSE) {
        g_warning ("Can not create a preset for caps: %s", strcaps_v[i]);

        return FALSE;
      }
    } else {
      encoding_profile = profile;
    }
  }
  g_strfreev (strcaps_v);

  return TRUE;
}

int
main (int argc, gchar ** argv)
{
  GstQaRunner *runner;
  GOptionContext *ctx;
  GstQaFileChecker *fc;

  GError *err = NULL;
  guint count = -1;

  gboolean playback = FALSE, seekable = FALSE, reverse_playback = FALSE;
  gint64 filesize = 0, filesize_tolerance = 0, duration_arg =
      0, duration_tolerance = 0;
  GstClockTime duration = GST_CLOCK_TIME_NONE;

  GOptionEntry options[] = {
    {"expected-profile", 'o', 0, G_OPTION_ARG_CALLBACK,
          &_parse_encoding_profile,
          "Set the properties to use for the encoding profile "
          "to be used as expected for the file. For example:\n"
          "video/mpegts:video/x-raw-yuv,width=1920,height=1080->video/x-h264:audio/x-ac3\n"
          "A preset name can be used by adding +presetname, eg:\n"
          "video/webm:video/x-vp8+mypreset:audio/x-vorbis\n"
          "The presence property of the profile can be specified with |<presence>, eg:\n"
          "video/webm:video/x-vp8|<presence>:audio/x-vorbis\n",
        "properties-values"},
    {"seekable", 's', 0, G_OPTION_ARG_NONE,
          &seekable, "If the file should be seekable",
        NULL},
    {"playback", 'p', 0, G_OPTION_ARG_NONE,
          &playback, "If the file should be tested for playback",
        NULL},
    {"reverse-playback", '\0', 0, G_OPTION_ARG_NONE,
          &reverse_playback,
          "If the file should be tested for reverse playback",
        NULL},
    {"file-size", '\0', 0, G_OPTION_ARG_INT64, &filesize,
        "The expected file size in bytes", NULL},
    {"file-size-tolerance", '\0', 0, G_OPTION_ARG_INT64, &filesize_tolerance,
        "The file size margin tolerance, in bytes", NULL},
    {"duration", 'd', 0, G_OPTION_ARG_INT64, &duration_arg,
        "The expected file duration in nanoseconds", NULL},
    {"duration-tolerance", '\0', 0, G_OPTION_ARG_INT64, &duration_tolerance,
        "The file duration tolerance margin, in nanoseconds", NULL},
    {NULL}
  };

  ctx = g_option_context_new ("- runs QA transcoding test.");
  g_option_context_add_main_entries (ctx, options, NULL);

  if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
    g_printerr ("Error initializing: %s\n", err->message);
    g_option_context_free (ctx);
    exit (1);
  }

  g_option_context_free (ctx);

  gst_init (&argc, &argv);

  if (argc != 2) {
    g_printerr ("%i arguments recived, 1 expected.\n"
        "You should run the test using:\n"
        "    ./gst-qa-file-check-0.10 <uri> [options]\n", argc - 1);
    return 1;
  }

  if (duration_arg > 0)
    duration = (GstClockTime) duration_arg;

  /* Create the pipeline */
  runner = gst_qa_runner_new ();
  fc = g_object_new (GST_TYPE_QA_FILE_CHECKER, "uri",
      argv[1], "profile", encoding_profile, "qa-runner", runner,
      "is-seekable", seekable, "test-playback", playback,
      "test-reverse-playback", reverse_playback,
      "file-size", (guint64) filesize, "file-size-tolerance", (guint64)
      filesize_tolerance, "duration", (guint64) duration,
      "duration-tolerance", (guint64) duration_tolerance, NULL);

  g_print ("Starting tests\n");
  if (!gst_qa_file_checker_run (fc)) {
    g_print ("Failed file checking\n");
  }
  count = gst_qa_runner_get_reports_count (runner);
  g_print ("Tests finished, total issues found: %u\n", count);
  g_object_unref (fc);

  g_object_unref (runner);

  if (count)
    return -1;
  return 0;
}
