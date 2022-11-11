/* GStreamer
 *
 * Copyright (C) 2013 Collabora Ltd.
 *  Author: Thiago Sousa Santos <thiago.sousa.santos@collabora.com>
 *
 * gst-validate-media-check.c - Media Check CLI tool
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdlib.h>
#include <string.h>

#include <gst/gst.h>
#include <gst/validate/validate.h>
#include <gst/validate/media-descriptor-writer.h>
#include <gst/validate/media-descriptor-parser.h>
#include <gst/validate/media-descriptor.h>
#include <gst/validate/gst-validate-utils.h>
#include <gst/pbutils/encoding-profile.h>
#include <locale.h>             /* for LC_ALL */

int
main (int argc, gchar ** argv)
{
  GOptionContext *ctx;

  guint ret = 0;
  GError *err = NULL;
  gboolean full = FALSE;
  gboolean skip_parsers = FALSE;
  gchar *output_file = NULL;
  gchar *expected_file = NULL;
  gchar *output = NULL;
  GstValidateMediaDescriptorWriterFlags writer_flags =
      GST_VALIDATE_MEDIA_DESCRIPTOR_WRITER_FLAGS_HANDLE_GLOGS;
  GstValidateMediaDescriptorWriter *writer = NULL;
  GstValidateRunner *runner = NULL;
  GstValidateMediaDescriptorParser *reference = NULL;

  GOptionEntry options[] = {
    {"output-file", 'o', 0, G_OPTION_ARG_FILENAME,
          &output_file, "The output file to store the results",
        NULL},
    {"full", 'f', 0, G_OPTION_ARG_NONE,
          &full, "Fully analyze the file frame by frame",
        NULL},
    {"expected-results", 'e', 0, G_OPTION_ARG_FILENAME,
          &expected_file, "Path to file containing the expected results "
          "(or the last results found) for comparison with new results",
        NULL},
    {"skip-parsers", 's', 0, G_OPTION_ARG_NONE,
          &skip_parsers, "Do not plug a parser after demuxer.",
        NULL},
    {NULL}
  };

  setlocale (LC_ALL, "");
  g_set_prgname ("gst-validate-media-check-" GST_API_VERSION);
  ctx = g_option_context_new ("[URI]");
  g_option_context_set_summary (ctx, "Analyzes a media file and writes "
      "the results to stdout or a file. Can also compare the results found "
      "with another results file for identifying regressions. The monitoring"
      " lib from gst-validate will be enabled during the tests to identify "
      "issues with the gstreamer elements involved with the media file's "
      "container and codec types");
  g_option_context_add_main_entries (ctx, options, NULL);

  if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
    g_printerr ("Error initializing: %s\n", err->message);
    g_option_context_free (ctx);
    g_clear_error (&err);
    exit (1);
  }

  gst_init (&argc, &argv);
  gst_validate_init ();

  if (argc != 2) {
    gchar *msg = g_option_context_get_help (ctx, TRUE, NULL);
    g_printerr ("%s\n", msg);
    g_free (msg);
    g_option_context_free (ctx);
    ret = 1;
    goto out;
  }
  g_option_context_free (ctx);

  gst_validate_spin_on_fault_signals ();

  runner = gst_validate_runner_new ();

  if (expected_file) {
    reference =
        gst_validate_media_descriptor_parser_new (runner, expected_file, NULL);

    if (reference == NULL) {
      gst_validate_printf (NULL, "Could not parse file: %s\n", expected_file);
      ret = 1;
      goto out;
    }

    if (!full
        &&
        gst_validate_media_descriptor_has_frame_info (
            (GstValidateMediaDescriptor *)
            reference))
      full = TRUE;              /* Reference has frame info, activate to do comparison */
  }

  if (full)
    writer_flags |= GST_VALIDATE_MEDIA_DESCRIPTOR_WRITER_FLAGS_FULL;

  if (skip_parsers)
    writer_flags |= GST_VALIDATE_MEDIA_DESCRIPTOR_WRITER_FLAGS_NO_PARSER;


  writer =
      gst_validate_media_descriptor_writer_new_discover (runner, argv[1],
      writer_flags, NULL);
  if (writer == NULL) {
    gst_validate_printf (NULL, "Could not discover file: %s\n", argv[1]);
    ret = 1;
    goto out;
  }

  if (output_file) {
    if (!gst_validate_media_descriptor_writer_write (writer, output_file)) {
      ret = 1;
      goto out;
    }
  }

  if (reference) {
    if (!gst_validate_media_descriptors_compare (GST_VALIDATE_MEDIA_DESCRIPTOR
            (reference), GST_VALIDATE_MEDIA_DESCRIPTOR (writer))) {
      ret = 1;
      goto out;
    }
  } else {
    output = gst_validate_media_descriptor_writer_serialize (writer);
    gst_validate_printf (NULL, "Media info:\n%s\n", output);
    g_free (output);
  }

out:
  if (runner)
    ret += gst_validate_runner_exit (runner, TRUE);

  g_free (output_file);
  g_free (expected_file);

  if (reference) {
    gst_validate_reporter_purge_reports (GST_VALIDATE_REPORTER (reference));
    gst_object_unref (reference);
  }
  if (writer) {
    gst_validate_reporter_purge_reports (GST_VALIDATE_REPORTER (writer));
    gst_object_unref (writer);
  }
  if (runner)
    gst_object_unref (runner);

  gst_validate_printf (NULL, "\n=======> Test %s (Return value: %i)\n\n",
      ret == 0 ? "PASSED" : "FAILED", ret);

  gst_validate_deinit ();
  gst_deinit ();

  return ret;
}
