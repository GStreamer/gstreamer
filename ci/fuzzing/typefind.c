/*
 * Copyright 2016 Google Inc.
 * author: Edward Hervey <bilboed@bilboed.com>
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
#include <glib.h>
#include <gst/gst.h>

/* push-based typefind fuzzing target
 *
 * This application can be compiled with libFuzzer to simulate
 * a push-based typefind execution.
 *
 * To reproduce the failing behaviour, use:
 * $ gst-launch-1.0 pushfile:///.. ! typefind ! fakesink
 *
 * The goal is to cover typefind code and implementation.
 *
 **/
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
  GstElement *pipeline, *source, *typefind, *fakesink;
  GstBuffer *buf;
  GstFlowReturn flowret;
  GstState state;

  if (!initialized) {
    /* We want critical warnings to assert so we can fix them */
    g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL);
    g_log_set_default_handler (custom_logger, NULL);

    /* Only initialize and register plugins once */
    gst_init (NULL, NULL);

    initialized = TRUE;
  }

  /* Create the pipeline */
  pipeline = gst_pipeline_new ("pipeline");
  source = gst_element_factory_make ("appsrc", "source");
  typefind = gst_element_factory_make ("typefind", "typefind");
  fakesink = gst_element_factory_make ("fakesink", "fakesink");

  gst_bin_add_many (GST_BIN (pipeline), source, typefind, fakesink, NULL);
  gst_element_link_many (source, typefind, fakesink, NULL);

  /* Set pipeline to READY so we can provide data to appsrc */
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_READY);
  buf = gst_buffer_new_wrapped_full (0, (gpointer) data, size,
      0, size, NULL, NULL);
  g_object_set (G_OBJECT (source), "size", size, NULL);
  g_signal_emit_by_name (G_OBJECT (source), "push-buffer", buf, &flowret);
  gst_buffer_unref (buf);

  /* Set pipeline to PAUSED and wait (typefind will either fail or succeed) */
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PAUSED);

  /* wait until state change either completes or fails */
  gst_element_get_state (GST_ELEMENT (pipeline), &state, NULL, -1);

  /* Go back to NULL */
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);

  /* And release the pipeline */
  gst_object_unref (pipeline);

  return 0;
}
