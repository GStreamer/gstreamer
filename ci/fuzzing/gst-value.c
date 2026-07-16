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

/* GStreamer value/caps/structure string fuzzing target
 *
 * Exercises:
 *   gst/gstvalue.c
 *   gst/gstcaps.c
 *   gst/gststructure.c
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <gst/gst.h>

/* Filled after gst_init() since GST_TYPE_* globals are set at registration */
static GType deser_types[16];

/* Valid structure name forced onto fuzz input so parsers never assert on it. */
#define FUZZER_NAME_PREFIX "fuzzer/x-data,"

static gchar *
fuzzer_make_trusted_name_input (const gchar * str)
{
  GString *out = g_string_new (FUZZER_NAME_PREFIX);
  const gchar *p;

  for (p = str; *p != '\0'; p++) {
    if (*p != ';')
      g_string_append_c (out, *p);
  }

  return g_string_free (out, FALSE);
}

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

    deser_types[0] = GST_TYPE_FRACTION;
    deser_types[1] = GST_TYPE_LIST;
    deser_types[2] = GST_TYPE_ARRAY;
    deser_types[3] = GST_TYPE_BITMASK;
    deser_types[4] = GST_TYPE_FLAG_SET;
    deser_types[5] = GST_TYPE_CAPS;
    deser_types[6] = GST_TYPE_STRUCTURE;
    deser_types[7] = G_TYPE_INT;
    deser_types[8] = G_TYPE_UINT;
    deser_types[9] = G_TYPE_INT64;
    deser_types[10] = G_TYPE_UINT64;
    deser_types[11] = G_TYPE_BOOLEAN;
    deser_types[12] = G_TYPE_FLOAT;
    deser_types[13] = G_TYPE_DOUBLE;
    deser_types[14] = G_TYPE_STRING;
    deser_types[15] = GST_TYPE_BUFFER;

    initialized = TRUE;
  }

  if (size == 0)
    return 0;

  gchar *str = g_strndup ((const gchar *) data, size);

  /* Trusted-name input for name-sensitive paths; raw str kept for the rest. */
  gchar *name_str = fuzzer_make_trusted_name_input (str);

  /* caps string parsing */
  {
    GstCaps *caps = gst_caps_from_string (name_str);
    if (caps)
      gst_caps_unref (caps);
  }

  /* structure string parsing */
  {
    GstStructure *s = gst_structure_from_string (name_str, NULL);
    if (s)
      gst_structure_free (s);
  }

  /* per-type value deserializers */
  for (gsize i = 0; i < G_N_ELEMENTS (deser_types); i++) {
    GValue v = G_VALUE_INIT;
    const gchar *input = str;

    /* These deserialize via caps/structure from_string, so need trusted name. */
    if (deser_types[i] == GST_TYPE_CAPS || deser_types[i] == GST_TYPE_STRUCTURE)
      input = name_str;

    g_value_init (&v, deser_types[i]);
    gst_value_deserialize (&v, input);
    g_value_unset (&v);
  }

  g_free (name_str);
  g_free (str);

  return 0;
}
