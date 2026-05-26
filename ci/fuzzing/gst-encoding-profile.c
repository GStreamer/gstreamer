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

/* GStreamer encoding-profile / encoding-target fuzzing target
 *
 * Exercises:
 *   gst-libs/gst/pbutils/encoding-profile.c
 *   gst-libs/gst/pbutils/encoding-target.c
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>

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

static void
exercise_profile (GstEncodingProfile * profile)
{
  GstCaps *caps;
  gchar *serialized;

  if (!profile)
    return;

  caps = gst_encoding_profile_get_format (profile);
  if (caps)
    gst_caps_unref (caps);
  caps = gst_encoding_profile_get_restriction (profile);
  if (caps)
    gst_caps_unref (caps);
  caps = gst_encoding_profile_get_input_caps (profile);
  if (caps)
    gst_caps_unref (caps);

  serialized = gst_encoding_profile_to_string (profile);
  g_free (serialized);

  /* Recurse into container profile children */
  if (GST_IS_ENCODING_CONTAINER_PROFILE (profile)) {
    const GList *subprofiles =
        gst_encoding_container_profile_get_profiles
        (GST_ENCODING_CONTAINER_PROFILE (profile));
    for (const GList * item = subprofiles; item; item = item->next)
      exercise_profile (GST_ENCODING_PROFILE (item->data));
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
    gst_pb_utils_init ();
    initialized = TRUE;
  }

  if (size == 0)
    return 0;

  gchar *desc = g_strndup ((const gchar *) data, size);

  {
    GstEncodingProfile *profile = gst_encoding_profile_from_string (desc);
    if (profile) {
      exercise_profile (profile);
      {
        GstEncodingProfile *clone = gst_encoding_profile_copy (profile);
        if (clone) {
          exercise_profile (clone);
          gst_encoding_profile_unref (clone);
        }
      }
      gst_encoding_profile_unref (profile);
    }
  }

  /* Dump fuzz bytes into a temp file and parse as a .gep target description. */
  {
    gchar *path = NULL;
    gint fd = g_file_open_tmp ("fuzz-gepXXXXXX", &path, NULL);
    if (fd >= 0) {
      close (fd);
      if (g_file_set_contents (path, (const gchar *) data, (gssize) size, NULL)) {
        GError *err = NULL;
        GstEncodingTarget *target =
            gst_encoding_target_load_from_file (path, &err);
        if (target) {
          const GList *profiles = gst_encoding_target_get_profiles (target);
          for (const GList * item = profiles; item; item = item->next)
            exercise_profile (GST_ENCODING_PROFILE (item->data));
          gst_encoding_target_unref (target);
        }
        g_clear_error (&err);
      }
      g_unlink (path);
      g_free (path);
    }
  }

  g_free (desc);
  return 0;
}
