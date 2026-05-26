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

/* GStreamer URI fuzzing target
 *
 * Exercises:
 *   gst/gsturi.c
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

  gchar *str = g_strndup ((const gchar *) data, size);
  gchar *half1 = g_strndup ((const gchar *) data, size / 2);
  gchar *half2 = g_strndup ((const gchar *) (data + size / 2),
      size - size / 2);
  guint port = (size >= 2) ? (((guint) data[0] << 8) | data[1]) : data[0];

  /* String-level helpers */
  {
    gchar *protocol = gst_uri_get_protocol (str);
    g_free (protocol);
  }
  {
    gchar *location = gst_uri_get_location (str);
    g_free (location);
  }
  gst_uri_is_valid (str);
  gst_uri_protocol_is_valid (str);
  gst_uri_has_protocol (str, half2);

  /* Filename to URI: takes any path-like string */
  {
    GError *err = NULL;
    gchar *file_uri = gst_filename_to_uri (str, &err);
    g_free (file_uri);
    g_clear_error (&err);
  }

  /* gst_uri_construct: requires a valid protocol */
  if (gst_uri_protocol_is_valid (half1)) {
    gchar *constructed = gst_uri_construct (half1, half2);
    g_free (constructed);
  }

  /* Programmatic construction with fuzz slices; tier on input length. */
  if (size >= 2) {
    guint slot_mask = (size >= 3) ? data[2] : 0u;
    if (size / 2 < 2)
      slot_mask |= 0x01 | 0x08;
    if ((size - size / 2) < 2)
      slot_mask |= 0x02 | 0x10;

    GstUri *uri = gst_uri_new ((slot_mask & 0x01) ? NULL : half1,
        (slot_mask & 0x02) ? NULL : half2,
        (slot_mask & 0x04) ? NULL : str,
        port,
        (slot_mask & 0x08) ? NULL : half1,
        (slot_mask & 0x10) ? NULL : half2,
        (slot_mask & 0x20) ? NULL : str);
    if (uri) {
      gchar *serialized = gst_uri_to_string (uri);
      g_free (serialized);
      gst_uri_unref (uri);
    }
  }

  /* Full structural parser + accessors + setters */
  {
    GstUri *uri = gst_uri_from_string (str);
    if (uri) {
      gchar *tmp_string;
      GList *components;

      gst_uri_get_scheme (uri);
      gst_uri_get_userinfo (uri);
      gst_uri_get_host (uri);
      gst_uri_get_port (uri);
      gst_uri_get_fragment (uri);
      gst_uri_is_normalized (uri);

      tmp_string = gst_uri_get_path (uri);
      g_free (tmp_string);
      tmp_string = gst_uri_get_path_string (uri);
      g_free (tmp_string);
      tmp_string = gst_uri_get_query_string (uri);
      g_free (tmp_string);

      components = gst_uri_get_path_segments (uri);
      g_list_free_full (components, g_free);
      components = gst_uri_get_query_keys (uri);
      g_list_free (components);

      tmp_string = gst_uri_to_string (uri);
      g_free (tmp_string);
      gst_uri_normalize (uri);
      tmp_string = gst_uri_to_string (uri);
      g_free (tmp_string);

      /* Mutation paths */
      uri = gst_uri_make_writable (uri);
      gst_uri_set_scheme (uri, half1);
      gst_uri_set_userinfo (uri, half2);
      gst_uri_set_host (uri, str);
      gst_uri_set_port (uri, port);
      gst_uri_set_path (uri, half1);
      gst_uri_set_query_string (uri, half2);
      gst_uri_set_fragment (uri, str);

      tmp_string = gst_uri_to_string (uri);
      g_free (tmp_string);

      gst_uri_unref (uri);
    }
  }

  /* Two-URI APIs: equal, join, from_string_with_base */
  {
    GstUri *base_uri = gst_uri_from_string (half1);
    GstUri *ref_uri = gst_uri_from_string (half2);

    if (base_uri && ref_uri) {
      gst_uri_equal (base_uri, ref_uri);
      {
        GstUri *joined = gst_uri_join (base_uri, ref_uri);
        if (joined)
          gst_uri_unref (joined);
      }
    }

    if (base_uri) {
      GstUri *with_base = gst_uri_from_string_with_base (base_uri, str);
      if (with_base)
        gst_uri_unref (with_base);
    }

    if (base_uri)
      gst_uri_unref (base_uri);
    if (ref_uri)
      gst_uri_unref (ref_uri);
  }

  /* Escaped variant */
  {
    GstUri *uri = gst_uri_from_string_escaped (str);
    if (uri)
      gst_uri_unref (uri);
  }

  /* Joiner with a fixed base */
  {
    gchar *joined = gst_uri_join_strings ("http://example.com/", str);
    g_free (joined);
  }

  g_free (str);
  g_free (half1);
  g_free (half2);
  return 0;
}
