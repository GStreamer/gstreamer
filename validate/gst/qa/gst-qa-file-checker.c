/* GStreamer
 * Copyright (C) 2013 Thiago Santos <thiago.sousa.santos@collabora.com>
 *
 * gst-qa-file-checker.c - QA File conformance check utility functions / structs
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

#include "gst-qa-file-checker.h"
#include "gst-qa-reporter.h"

#include <glib/gstdio.h>
#include <gst/pbutils/pbutils.h>

enum
{
  PROP_0,
  PROP_RUNNER,
  PROP_URI,
  PROP_PROFILE,
  PROP_DURATION,
  PROP_DURATION_TOLERANCE,
  PROP_FILE_SIZE,
  PROP_FILE_SIZE_TOLERANCE,
  PROP_SEEKABLE,
  PROP_LAST
};

#define DEFAULT_DURATION GST_CLOCK_TIME_NONE
#define DEFAULT_DURATION_TOLERANCE 0
#define DEFAULT_FILE_SIZE 0
#define DEFAULT_FILE_SIZE_TOLERANCE 0
#define DEFAULT_SEEKABLE FALSE

GST_DEBUG_CATEGORY_STATIC (gst_qa_file_checker_debug);
#define GST_CAT_DEFAULT gst_qa_file_checker_debug

static void
gst_qa_file_checker_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void
gst_qa_file_checker_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

#define _do_init \
  GST_DEBUG_CATEGORY_INIT (gst_qa_file_checker_debug, "qa_file_checker", 0, "QA FileChecker");\
  G_IMPLEMENT_INTERFACE (GST_TYPE_QA_REPORTER, _reporter_iface_init)

static void
_reporter_iface_init (GstQaReporterInterface * iface)
{
}

#define gst_qa_file_checker_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstQaFileChecker, gst_qa_file_checker,
    G_TYPE_OBJECT, _do_init);

static void
gst_qa_file_checker_dispose (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_qa_file_checker_finalize (GObject * object)
{
  GstQaFileChecker *fc = GST_QA_FILE_CHECKER_CAST (object);

  gst_qa_reporter_set_name (GST_QA_REPORTER (object), NULL);

  g_free (fc->uri);
  if (fc->profile)
    gst_encoding_profile_unref (fc->profile);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_qa_file_checker_class_init (GstQaFileCheckerClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = gst_qa_file_checker_get_property;
  gobject_class->set_property = gst_qa_file_checker_set_property;
  gobject_class->dispose = gst_qa_file_checker_dispose;
  gobject_class->finalize = gst_qa_file_checker_finalize;

  g_object_class_install_property (gobject_class, PROP_RUNNER,
      g_param_spec_object ("qa-runner", "QA Runner", "The QA runner to "
          "report errors to", GST_TYPE_QA_RUNNER,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_URI,
      g_param_spec_string ("uri", "URI", "The URI of the file to be checked",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_NAME));

  g_object_class_install_property (gobject_class, PROP_PROFILE,
      gst_param_spec_mini_object ("profile", "Profile",
          "The GstEncodingProfile " "that should match what the file contains",
          GST_TYPE_ENCODING_PROFILE, G_PARAM_READWRITE | G_PARAM_STATIC_NAME));

  g_object_class_install_property (gobject_class, PROP_DURATION,
      g_param_spec_uint64 ("duration", "duration", "Stream duration "
          "in nanosecs, use GST_CLOCK_TIME_NONE to disable this check",
          0, G_MAXUINT64, DEFAULT_DURATION,
          G_PARAM_READWRITE | G_PARAM_STATIC_NAME));

  g_object_class_install_property (gobject_class, PROP_DURATION_TOLERANCE,
      g_param_spec_uint64 ("duration-tolerance", "duration tolerance",
          "Acceptable margin of error of the duration check (in nanoseconds)",
          0, G_MAXUINT64, DEFAULT_DURATION_TOLERANCE,
          G_PARAM_READWRITE | G_PARAM_STATIC_NAME));

  g_object_class_install_property (gobject_class, PROP_FILE_SIZE,
      g_param_spec_uint64 ("file-size", "file size", "File size in bytes",
          0, G_MAXUINT64, DEFAULT_FILE_SIZE,
          G_PARAM_READWRITE | G_PARAM_STATIC_NAME));

  g_object_class_install_property (gobject_class, PROP_FILE_SIZE_TOLERANCE,
      g_param_spec_uint64 ("file-size-tolerance", "file size tolerance",
          "Acceptable margin of error of the file size check (in bytes)",
          0, G_MAXUINT64, DEFAULT_FILE_SIZE_TOLERANCE,
          G_PARAM_READWRITE | G_PARAM_STATIC_NAME));

  g_object_class_install_property (gobject_class, PROP_SEEKABLE,
      g_param_spec_boolean ("is-seekable", "is seekable",
          "If the resulting file should be seekable", DEFAULT_SEEKABLE,
          G_PARAM_READWRITE | G_PARAM_STATIC_NAME));
}

static void
gst_qa_file_checker_init (GstQaFileChecker * fc)
{
  fc->uri = NULL;
  fc->profile = NULL;
  fc->duration = DEFAULT_DURATION;
  fc->duration_tolerance = DEFAULT_DURATION_TOLERANCE;
  fc->file_size = DEFAULT_FILE_SIZE;
  fc->file_size_tolerance = DEFAULT_FILE_SIZE_TOLERANCE;
  fc->seekable = DEFAULT_SEEKABLE;
}

static void
gst_qa_file_checker_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstQaFileChecker *fc;

  fc = GST_QA_FILE_CHECKER_CAST (object);

  switch (prop_id) {
    case PROP_RUNNER:
      gst_qa_reporter_set_runner (GST_QA_REPORTER (fc),
          g_value_get_object (value));
      break;
    case PROP_URI:
      g_free (fc->uri);
      fc->uri = g_value_dup_string (value);
      break;
    case PROP_PROFILE:
      if (fc->profile)
        gst_encoding_profile_unref (fc->profile);
      fc->profile = (GstEncodingProfile *) gst_value_dup_mini_object (value);
      break;
    case PROP_DURATION:
      fc->duration = g_value_get_uint64 (value);
      break;
    case PROP_DURATION_TOLERANCE:
      fc->duration_tolerance = g_value_get_uint64 (value);
      break;
    case PROP_FILE_SIZE:
      fc->file_size = g_value_get_uint64 (value);
      break;
    case PROP_FILE_SIZE_TOLERANCE:
      fc->file_size_tolerance = g_value_get_uint64 (value);
      break;
    case PROP_SEEKABLE:
      fc->seekable = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_qa_file_checker_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstQaFileChecker *fc;

  fc = GST_QA_FILE_CHECKER_CAST (object);

  switch (prop_id) {
    case PROP_RUNNER:
      g_value_set_object (value,
          gst_qa_reporter_get_runner (GST_QA_REPORTER (fc)));
      break;
    case PROP_URI:
      g_value_set_string (value, fc->uri);
      break;
    case PROP_PROFILE:
      gst_value_set_mini_object (value, GST_MINI_OBJECT_CAST (fc->profile));
      break;
    case PROP_DURATION:
      g_value_set_uint64 (value, fc->duration);
      break;
    case PROP_DURATION_TOLERANCE:
      g_value_set_uint64 (value, fc->duration_tolerance);
      break;
    case PROP_FILE_SIZE:
      g_value_set_uint64 (value, fc->file_size);
      break;
    case PROP_FILE_SIZE_TOLERANCE:
      g_value_set_uint64 (value, fc->file_size_tolerance);
      break;
    case PROP_SEEKABLE:
      g_value_set_boolean (value, fc->seekable);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
check_file_size (GstQaFileChecker * fc)
{
  GStatBuf statbuf;
  gchar *filepath;
  guint64 size = 0;
  gboolean ret = TRUE;
  GError *err;

  filepath = g_filename_from_uri (fc->uri, NULL, &err);
  if (!filepath) {
    GST_QA_REPORT (fc, GST_QA_ISSUE_ID_FILE_NOT_FOUND,
        "Failed to get filepath from uri %s. %s", fc->uri, err->message);
    g_error_free (err);
    return FALSE;
  }

  if (g_stat (filepath, &statbuf) == 0) {
    size = statbuf.st_size;
  } else {
    GST_QA_REPORT (fc, GST_QA_ISSUE_ID_FILE_NOT_FOUND,
        "Failed to get file stats from uri %s", fc->uri);
    ret = FALSE;
    goto end;
  }

  if (size == 0) {
    GST_QA_REPORT (fc, GST_QA_ISSUE_ID_FILE_SIZE_IS_ZERO, "File %s has size 0",
        fc->uri);
    ret = FALSE;
  } else if (size < fc->file_size - fc->file_size_tolerance ||
      size > fc->file_size + fc->file_size_tolerance) {
    GST_QA_REPORT (fc, GST_QA_ISSUE_ID_FILE_SIZE_INCORRECT,
        "File %s has size %" G_GUINT64_FORMAT ", it was expected to have %"
        G_GUINT64_FORMAT " (+-%" G_GUINT64_FORMAT ")",
        fc->uri, size, fc->file_size, fc->file_size_tolerance);
    ret = FALSE;
    goto end;
  }

end:
  g_free (filepath);
  return ret;
}

static gboolean
check_file_duration (GstQaFileChecker * fc, GstDiscovererInfo * info)
{
  GstClockTime real_duration;

  if (!GST_CLOCK_TIME_IS_VALID (fc->duration))
    return TRUE;

  real_duration = gst_discoverer_info_get_duration (info);
  if (real_duration < fc->duration - fc->duration_tolerance ||
      real_duration > fc->duration + fc->duration_tolerance) {
    GST_QA_REPORT (fc, GST_QA_ISSUE_ID_FILE_SIZE_INCORRECT,
        "File %s has duration %" GST_TIME_FORMAT ", it was expected to have %"
        GST_TIME_FORMAT " (+-%" GST_TIME_FORMAT ")",
        fc->uri, GST_TIME_ARGS (real_duration), GST_TIME_ARGS (fc->duration),
        GST_TIME_ARGS (fc->duration_tolerance));
    return FALSE;
  }
  return TRUE;
}

static gboolean
check_seekable (GstQaFileChecker * fc, GstDiscovererInfo * info)
{
  gboolean real_seekable;

  real_seekable = gst_discoverer_info_get_seekable (info);
  if (real_seekable != fc->seekable) {
    GST_QA_REPORT (fc, GST_QA_ISSUE_ID_FILE_SEEKABLE_INCORRECT,
        "File was expected to %s be seekable, but it %s",
        fc->seekable ? "" : "not", real_seekable ? "is" : "isn't");
    return FALSE;
  }
  return TRUE;
}

static gboolean
check_encoding_profile (GstQaFileChecker * fc, GstDiscovererInfo * info)
{
  GstEncodingProfile *profile = fc->profile;
  GstEncodingProfile *result_profile;
  gboolean ret = TRUE;

  if (profile == NULL)
    return TRUE;

  result_profile = gst_encoding_profile_from_discoverer (info);

  /* TODO doesn't do subtitle checks */
  if (!gst_encoding_profile_is_equal (result_profile, profile)) {
    GST_QA_REPORT (fc, GST_QA_ISSUE_ID_FILE_PROFILE_INCORRECT, "Wrong profile "
        "found on file %s", fc->uri);
    ret = FALSE;
  }

  gst_encoding_profile_unref (result_profile);
  return ret;
}


gboolean
gst_qa_file_checker_run (GstQaFileChecker * fc)
{
  GError *err = NULL;
  GstDiscovererInfo *info;
  GstDiscoverer *discoverer = gst_discoverer_new (GST_SECOND * 60, &err);
  gboolean ret = TRUE;

  g_return_val_if_fail (fc->uri != NULL, FALSE);

  if (!discoverer) {
    GST_QA_REPORT (fc, GST_QA_ISSUE_ID_ALLOCATION_FAILURE,
        "Failed to create GstDiscoverer");
    return FALSE;
  }

  info = gst_discoverer_discover_uri (discoverer, fc->uri, &err);

  if (gst_discoverer_info_get_result (info) != GST_DISCOVERER_OK) {
    GST_QA_REPORT (fc, GST_QA_ISSUE_ID_FILE_CHECK_FAILURE,
        "Discoverer failed to discover the file, result: %d",
        gst_discoverer_info_get_result (info));
    return FALSE;
  }

  ret = check_file_size (fc) & ret;
  ret = check_file_duration (fc, info) & ret;
  ret = check_seekable (fc, info) & ret;
  ret = check_encoding_profile (fc, info) & ret;

  return ret;
}
