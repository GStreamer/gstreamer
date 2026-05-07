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

/* Audio parser fuzzing target
 *
 * Exercises all parsers from gst-plugins-good/gst/audioparsers/ by pushing
 * raw data through each parser's sink pad with minimal caps.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <glib.h>
#include <gst/gst.h>

/* Minimum total bytes required before splitting into multiple chunks. */
#define MIN_CHUNKABLE_SIZE 8
#define MAX_CHUNKS         4

static void
custom_logger (const gchar * log_domain, GLogLevelFlags log_level,
    const gchar * message, gpointer unused_data)
{
  if (log_level & G_LOG_LEVEL_CRITICAL) {
    g_printerr ("CRITICAL ERROR : %s\n", message);
    abort ();
  } else if (log_level & G_LOG_LEVEL_WARNING) {
    g_printerr ("WARNING : %s\n", message);
  }
}

static void
fuzz_audioparser (const gchar * element_name, GstCaps * caps,
    const guint8 * data, gsize size)
{
  GstElement *parser;
  GstPad *sinkpad;
  GstSegment segment;
  guint chunk_count = 1;
  guint i;
  gsize offset = 0;
  gsize remaining = size;

  /* Chunking exercises GstBaseParse's streaming-path state machine. */
  if (size >= MIN_CHUNKABLE_SIZE)
    chunk_count = (data[0] % MAX_CHUNKS) + 1;

  parser = gst_element_factory_make (element_name, NULL);
  if (!parser) {
    g_error ("Failed to create element '%s': plugin not available. "
        "Check the build configuration.", element_name);
  }

  sinkpad = gst_element_get_static_pad (parser, "sink");
  g_assert (sinkpad != NULL);

  gst_element_set_state (parser, GST_STATE_PLAYING);

  gst_pad_send_event (sinkpad, gst_event_new_stream_start ("fuzz-stream"));
  gst_pad_send_event (sinkpad, gst_event_new_caps (caps));

  gst_segment_init (&segment, GST_FORMAT_TIME);
  gst_pad_send_event (sinkpad, gst_event_new_segment (&segment));

  /* Push the payload as chunk_count sequential buffers. */
  for (i = 0; i < chunk_count; i++) {
    gsize chunk_size;
    GstBuffer *buffer;

    if (i == chunk_count - 1)
      chunk_size = remaining;
    else
      chunk_size = remaining / (chunk_count - i);

    buffer = gst_buffer_new_wrapped_full (GST_MEMORY_FLAG_READONLY,
        (gpointer) (data + offset), chunk_size, 0, chunk_size, NULL, NULL);
    gst_pad_chain (sinkpad, buffer);

    offset += chunk_size;
    remaining -= chunk_size;
  }

  gst_pad_send_event (sinkpad, gst_event_new_eos ());

  gst_pad_send_event (sinkpad, gst_event_new_flush_start ());
  gst_pad_send_event (sinkpad, gst_event_new_flush_stop (FALSE));

  gst_element_set_state (parser, GST_STATE_NULL);
  gst_object_unref (sinkpad);
  gst_object_unref (parser);
}

int
LLVMFuzzerTestOneInput (const guint8 * data, size_t size)
{
  static gboolean initialized = FALSE;
  GstCaps *caps;

  if (!initialized) {
    g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL);
    g_log_set_default_handler (custom_logger, NULL);
    gst_init (NULL, NULL);
    initialized = TRUE;
  }

  if (size == 0)
    return 0;

  caps = gst_caps_new_simple ("audio/mpeg", "mpegversion", G_TYPE_INT, 4, NULL);
  fuzz_audioparser ("aacparse", caps, data, size);
  gst_caps_unref (caps);

  caps = gst_caps_new_simple ("audio/mpeg", "mpegversion", G_TYPE_INT, 1, NULL);
  fuzz_audioparser ("mpegaudioparse", caps, data, size);
  gst_caps_unref (caps);

  caps = gst_caps_new_empty_simple ("audio/x-ac3");
  fuzz_audioparser ("ac3parse", caps, data, size);
  gst_caps_unref (caps);

  caps = gst_caps_new_empty_simple ("audio/x-dts");
  fuzz_audioparser ("dcaparse", caps, data, size);
  gst_caps_unref (caps);

  caps = gst_caps_new_empty_simple ("audio/x-amr-nb-sh");
  fuzz_audioparser ("amrparse", caps, data, size);
  gst_caps_unref (caps);

  caps = gst_caps_new_empty_simple ("audio/x-sbc");
  fuzz_audioparser ("sbcparse", caps, data, size);
  gst_caps_unref (caps);

  caps = gst_caps_new_empty_simple ("audio/x-wavpack");
  fuzz_audioparser ("wavpackparse", caps, data, size);
  gst_caps_unref (caps);

  return 0;
}
