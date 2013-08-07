/* GStreamer
 * Copyright (C) 2013 Thibault Saunier <thibault.saunier@collabora.com>
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdlib.h>
#include <string.h>

#include <gst/gst.h>
#include <gst/qa/qa.h>
#include <gst/pbutils/encoding-profile.h>

#include "gst-qa-file-checker.h"


static GMainLoop *mainloop;
static GstElement *pipeline;
static GstEncodingProfile *encoding_profile = NULL;

static gboolean
bus_callback (GstBus * bus, GstMessage * message, gpointer data)
{
  GMainLoop *loop = data;
  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_STATE_CHANGED:
    {
      if (GST_MESSAGE_SRC (message) == GST_OBJECT_CAST (pipeline)) {
        GstState old, new, pending;

        gst_message_parse_state_changed (message, &old, &new, &pending);

        if (new == GST_STATE_PLAYING) {
          GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (pipeline),
              GST_DEBUG_GRAPH_SHOW_ALL, "gst-qa-transcode.playing");
        }

      }
      break;
    }
    case GST_MESSAGE_ERROR:
    {
      GError *err;
      gchar *debug;
      gst_message_parse_error (message, &err, &debug);
      g_print ("Error: %s\n", err->message);
      g_error_free (err);
      g_free (debug);
      g_main_loop_quit (loop);
      break;
    }
    case GST_MESSAGE_EOS:
      g_main_loop_quit (loop);
      break;
    default:
      break;
  }

  return TRUE;
}

static void
pad_added_cb (GstElement * uridecodebin, GstPad * pad, GstElement * encodebin)
{
  GstCaps *caps;
  GstPad *sinkpad = NULL;

  GST_DEBUG_OBJECT (uridecodebin, "Pad added, caps: %" GST_PTR_FORMAT,
      gst_pad_get_caps (pad));


  /* Ask encodebin for a compatible pad */
  caps = gst_pad_get_caps_reffed (pad);
  g_signal_emit_by_name (encodebin, "request-pad", caps, &sinkpad);
  if (caps)
    gst_caps_unref (caps);

  if (sinkpad == NULL) {
    GST_WARNING ("Couldn't get an encoding pad for pad %s:%s\n",
        GST_DEBUG_PAD_NAME (pad));
    return;
  }

  if (G_UNLIKELY (gst_pad_link (pad, sinkpad) != GST_PAD_LINK_OK))
    GST_ERROR ("Couldn't link pads \n\n%" GST_PTR_FORMAT "\n\n  and \n\n %"
        GST_PTR_FORMAT "\n\n", gst_pad_get_caps (pad),
        gst_pad_get_caps (sinkpad));

  return;
}

static void
create_transcoding_pipeline (gchar * uri, gchar * outuri)
{
  GstElement *src, *ebin, *sink;

  mainloop = g_main_loop_new (NULL, FALSE);

  pipeline = gst_pipeline_new ("encoding-pipeline");
  src = gst_element_factory_make ("uridecodebin", NULL);

  ebin = gst_element_factory_make ("encodebin", NULL);
  sink = gst_element_make_from_uri (GST_URI_SINK, outuri, "sink");
  g_assert (sink);

  g_object_set (src, "uri", uri, NULL);
  g_object_set (ebin, "profile", encoding_profile, NULL);

  g_signal_connect (src, "pad-added", G_CALLBACK (pad_added_cb), ebin);

  gst_bin_add_many (GST_BIN (pipeline), src, ebin, sink, NULL);
  gst_element_link (ebin, sink);
}

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
  GstBus *bus;
  GstQaRunner *runner;
  GOptionContext *ctx;

  GError *err = NULL;
  const gchar *scenario = NULL;
  guint count = -1;
  gboolean run_file_checks = FALSE;

  GOptionEntry options[] = {
    {"output-format", 'o', 0, G_OPTION_ARG_CALLBACK, &_parse_encoding_profile,
          "Set the properties to use for the encoding profile "
          "(in case of transcoding.) For example:\n"
          "video/mpegts:video/x-raw-yuv,width=1920,height=1080->video/x-h264:audio/x-ac3\n"
          "A preset name can be used by adding +presetname, eg:\n"
          "video/webm:video/x-vp8+mypreset:audio/x-vorbis\n"
          "The presence property of the profile can be specified with |<presence>, eg:\n"
          "video/webm:video/x-vp8|<presence>:audio/x-vorbis\n",
        "properties-values"},
    {"set-scenario", '\0', 0, G_OPTION_ARG_STRING, &scenario,
        "Let you set a scenario, it will override the GST_QA_SCENARIO "
          "environment variable", NULL},
    {"run-file-checks", 'c', 0, G_OPTION_ARG_NONE,
          &run_file_checks, "If post file transcoding checks should be run",
        NULL},
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

  if (scenario)
    g_setenv ("GST_QA_SCENARIO", scenario, TRUE);

  gst_init (&argc, &argv);

  if (argc != 3) {
    g_printerr ("%i arguments recived, 2 expected.\n"
        "You should run the test using:\n"
        "    ./gst-qa-transcoding-0.10 <input-file> <output-file> [options]\n",
        argc - 1);
    return 1;
  }

  if (encoding_profile == NULL) {
    GST_INFO ("Creating default encoding profile");

    _parse_encoding_profile ("encoding-profile",
        "application/ogg:video/x-theora:audio/x-vorbis", NULL, NULL);
  }

  /* Create the pipeline */
  create_transcoding_pipeline (argv[1], argv[2]);

  runner = gst_qa_runner_new (pipeline);
  mainloop = g_main_loop_new (NULL, FALSE);

  if (!runner) {
    g_printerr ("Failed to setup QA Runner\n");
    exit (1);
  }

  bus = gst_element_get_bus (pipeline);
  gst_bus_add_watch (bus, bus_callback, mainloop);
  gst_object_unref (bus);

  g_print ("Starting pipeline\n");
  if (gst_element_set_state (pipeline,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
    goto exit;
  g_main_loop_run (mainloop);

  count = gst_qa_runner_get_reports_count (runner);
  g_print ("Pipeline finished, total issues found: %u\n", count);

exit:
  gst_element_set_state (pipeline, GST_STATE_NULL);
  g_main_loop_unref (mainloop);
  g_object_unref (runner);
  g_object_unref (pipeline);

  if (run_file_checks) {
    GstQaFileChecker *fc = g_object_new (GST_TYPE_QA_FILE_CHECKER, "uri",
        argv[2], "profile", encoding_profile, "test-playback", TRUE, NULL);

    if (!gst_qa_file_checker_run (fc)) {
      g_print ("Failed file checking\n");
    }

    g_object_unref (fc);
  }

  if (count)
    return -1;
  return 0;
}
