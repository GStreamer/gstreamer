/*
 * Copyright 2026 Google Inc.
 * author: Arthur SC Chan <arthur.chan@adalogics.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* GStreamer pipeline-syntax fuzzing target
 *
 * Exercises:
 *   gst/parse/grammar.y
 *   gst/parse/parse.l
 *   gst/gstparse.c
 *   gst/gstutils.c (gst_parse_bin_from_description_full)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <gst/gst.h>

static void
custom_logger (const gchar * log_domain,
    GLogLevelFlags log_level, const gchar * message, gpointer unused_data)
{
  if (log_level & G_LOG_LEVEL_CRITICAL) {
    g_printerr ("CRITICAL ERROR : %s\n", message);
    abort ();
  } else if (log_level & G_LOG_LEVEL_WARNING) {
    g_printerr ("WARNING : %s\n", message);
  }
}

int
LLVMFuzzerTestOneInput (const guint8 * data, size_t size)
{
  static gboolean initialized = FALSE;

  if (!initialized) {
    g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL);
    g_log_set_default_handler (custom_logger, NULL);
    gst_init (NULL, NULL);
    initialized = TRUE;
  }

  if (size == 0)
    return 0;

  gchar *desc = g_strndup ((const gchar *) data, size);

  /* gst_parse_launch_full: full pipeline DSL parser. Never call set_state on
   * the result so no element actually runs; only construction is fuzzed. */
  {
    GError *err = NULL;
    GstParseContext *context = gst_parse_context_new ();
    GstParseFlags flags = GST_PARSE_FLAG_FATAL_ERRORS
        | GST_PARSE_FLAG_PLACE_IN_BIN;
    GstElement *pipeline = gst_parse_launch_full (desc, context, flags, &err);
    if (pipeline)
      gst_object_unref (pipeline);
    if (context) {
      gchar **missing = gst_parse_context_get_missing_elements (context);
      if (missing)
        g_strfreev (missing);
      gst_parse_context_free (context);
    }
    g_clear_error (&err);
  }

  /* gst_parse_bin_from_description_full toggling the ghost-pad flag. */
  {
    GError *err = NULL;
    GstElement *bin = gst_parse_bin_from_description_full (desc, FALSE, NULL,
        GST_PARSE_FLAG_FATAL_ERRORS, &err);
    if (bin)
      gst_object_unref (bin);
    g_clear_error (&err);
  }
  {
    GError *err = NULL;
    GstElement *bin = gst_parse_bin_from_description_full (desc, TRUE, NULL,
        GST_PARSE_FLAG_FATAL_ERRORS, &err);
    if (bin)
      gst_object_unref (bin);
    g_clear_error (&err);
  }

  /* gst_parse_launchv splits the description on NUL into separate argv
   * tokens, exercising the argv-style entry that the gst-launch tool uses. */
  {
    GError *err = NULL;
    gchar **argv = g_strsplit (desc, "\x01", -1);
    GstElement *pipeline = gst_parse_launchv ((const gchar **) argv, &err);
    if (pipeline)
      gst_object_unref (pipeline);
    g_strfreev (argv);
    g_clear_error (&err);
  }

  g_free (desc);
  return 0;
}
