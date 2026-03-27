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
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <gst/gst.h>

/* Subtitle parsing fuzzing target
 *
 * This application can be compiled with libFuzzer to fuzz
 * subtitle parsing in gst-plugins-base/gst/subparse/
 *
 * The goal is to cover subtitle parsing code for:
 * - SubRip (SRT) - Most common subtitle format with timestamps
 * - WebVTT - Web Video Text Tracks with cue settings
 * - SSA/ASS - Advanced SubStation Alpha with styling
 * - MicroDVD - Frame-based format with {frame}{frame} syntax
 * - SAMI - Microsoft HTML-based subtitle format
 * - SubViewer - Another time-based format
 * - MPSub - MPlayer subtitle format
 * - TMPlayer - Frame/time based format
 * - MPL2 - Frame-based variant
 * - DKS - Frame-based format
 * - QuickTime Text - Apple subtitle format
 * - LRC - Lyrics format with embedded timestamps
 *
 * All parsers handle complex text/binary formats with potential for:
 * - Memory corruption (buffer overflows)
 * - Regular expression DoS (ReDoS) attacks
 * - Integer overflows in timestamp parsing
 * - HTML/XML injection in tag handling
 * - Logic errors in format detection
 * - Encoding conversion vulnerabilities
 **/

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
test_subtitle_parsing_direct_pad_chain (const guint8 * data, size_t size)
{
  GstElement *subparse, *fakesink;
  GstPad *sinkpad, *srcpad, *fakesink_pad;
  GstBuffer *buffer;
  GstCaps *caps;
  GstSegment segment;

  /* Create subparse element */
  subparse = gst_element_factory_make ("subparse", NULL);
  g_assert (subparse != NULL);

  /* Create fakesink to receive output */
  fakesink = gst_element_factory_make ("fakesink", NULL);
  g_assert (fakesink != NULL);

  /* Get pads */
  sinkpad = gst_element_get_static_pad (subparse, "sink");
  srcpad = gst_element_get_static_pad (subparse, "src");
  fakesink_pad = gst_element_get_static_pad (fakesink, "sink");

  g_assert (sinkpad);
  g_assert (srcpad);
  g_assert (fakesink_pad);

  /* Link subparse to fakesink */
  g_assert (gst_pad_link (srcpad, fakesink_pad) == GST_PAD_LINK_OK);

  /* Set elements to PLAYING state (sink to source order) */
  gst_element_set_state (fakesink, GST_STATE_PLAYING);
  gst_element_set_state (subparse, GST_STATE_PLAYING);

  /* Send stream-start event */
  gst_pad_send_event (sinkpad, gst_event_new_stream_start ("fuzz-stream"));

  /* Send segment event */
  gst_segment_init (&segment, GST_FORMAT_TIME);
  gst_pad_send_event (sinkpad, gst_event_new_segment (&segment));

  /* Create buffer with fuzzer data */
  buffer =
      gst_buffer_new_wrapped_full (GST_MEMORY_FLAG_READONLY, (gpointer) data,
      size, 0, size, NULL, NULL);
  if (buffer) {
    /* Chain buffer directly to pad - errors are expected with bad data */
    gst_pad_chain (sinkpad, buffer);

    /* Send EOS event */
    gst_pad_send_event (sinkpad, gst_event_new_eos ());
  }

  /* Cleanup (source to sink order) */
  gst_element_set_state (subparse, GST_STATE_NULL);
  gst_element_set_state (fakesink, GST_STATE_NULL);
  gst_object_unref (sinkpad);
  gst_object_unref (srcpad);
  gst_object_unref (fakesink_pad);
  gst_object_unref (subparse);
  gst_object_unref (fakesink);
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

  /* Test direct pad chain approach */
  test_subtitle_parsing_direct_pad_chain (data, size);

  return 0;
}
