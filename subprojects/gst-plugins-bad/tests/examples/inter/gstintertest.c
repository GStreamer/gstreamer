/* GstInterTest
 * Copyright (C) 2011 David Schleef <ds@schleef.org>
 * Copyright (C) 2010 Entropy Wave Inc
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <stdlib.h>

typedef struct _GstInterTest GstInterTest;
struct _GstInterTest
{
  GstElement *pipeline;
  GstBus *bus;
  GMainLoop *main_loop;

  GstElement *source_element;
  GstElement *sink_element;

  gboolean paused_for_buffering;
  guint timer_id;
};

GstInterTest *gst_inter_test_new (void);
void gst_inter_test_free (GstInterTest * intertest);
void gst_inter_test_create_pipeline_server (GstInterTest * intertest);
void gst_inter_test_create_pipeline_test_sources (GstInterTest * intertest);
void gst_inter_test_create_pipeline_playbin (GstInterTest * intertest,
    const char *uri);
void gst_inter_test_start (GstInterTest * intertest);
void gst_inter_test_stop (GstInterTest * intertest);

static gboolean gst_inter_test_handle_message (GstBus * bus,
    GstMessage * message, gpointer data);
static gboolean onesecond_timer (gpointer priv);


gboolean verbose;

static const gchar **uri_arg = NULL;

static GOptionEntry entries[] = {
  {"verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose, "Be verbose", NULL},
  {G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &uri_arg, 0,
      "[URL]"},

  {NULL}

};

int
main (int argc, char *argv[])
{
  GError *error = NULL;
  GOptionContext *context;
  GstInterTest *intertest1;
  GstInterTest *intertest2;
  GMainLoop *main_loop;
  const gchar *uri = NULL;

  context = g_option_context_new ("- Internal src/sink test");
  g_option_context_add_main_entries (context, entries, GETTEXT_PACKAGE);
  g_option_context_add_group (context, gst_init_get_option_group ());
  if (!g_option_context_parse (context, &argc, &argv, &error)) {
    gst_println ("option parsing failed: %s", error->message);
    g_option_context_free (context);
    g_clear_error (&error);
    exit (1);
  }
  g_option_context_free (context);

  if (uri_arg)
    uri = uri_arg[0];

  intertest1 = gst_inter_test_new ();
  gst_inter_test_create_pipeline_server (intertest1);
  gst_inter_test_start (intertest1);

  intertest2 = gst_inter_test_new ();
  gst_inter_test_create_pipeline_playbin (intertest2, uri);
  gst_inter_test_start (intertest2);

  main_loop = g_main_loop_new (NULL, TRUE);
  intertest1->main_loop = main_loop;
  intertest2->main_loop = main_loop;

  g_main_loop_run (main_loop);
  g_main_loop_unref (main_loop);

  gst_inter_test_free (intertest1);
  gst_inter_test_free (intertest2);

  gst_deinit ();
  exit (0);
}


GstInterTest *
gst_inter_test_new (void)
{
  GstInterTest *intertest;

  intertest = g_new0 (GstInterTest, 1);

  return intertest;
}

void
gst_inter_test_free (GstInterTest * intertest)
{
  if (!intertest)
    return;

  gst_clear_object (&intertest->source_element);
  gst_clear_object (&intertest->sink_element);

  if (intertest->bus) {
    gst_bus_remove_watch (intertest->bus);
    gst_bus_set_flushing (intertest->bus, TRUE);
    gst_clear_object (&intertest->bus);
  }

  if (intertest->pipeline) {
    gst_element_set_state (intertest->pipeline, GST_STATE_NULL);
    gst_clear_object (&intertest->pipeline);
  }
  g_free (intertest);
}

void
gst_inter_test_create_pipeline_playbin (GstInterTest * intertest,
    const char *uri)
{
  GstElement *pipeline;
  GstElement *playbin;
  GstElement *audio_sink;
  GstElement *video_sink;

  if (uri == NULL) {
    gst_inter_test_create_pipeline_test_sources (intertest);
    return;
  }

  pipeline = gst_pipeline_new (NULL);
  playbin = gst_element_factory_make ("playbin3", "source");
  audio_sink = gst_element_factory_make ("interaudiosink", NULL);
  video_sink = gst_element_factory_make ("intervideosink", NULL);
  g_object_set (playbin, "audio-sink", audio_sink, "video-sink", video_sink,
      NULL);
  gst_bin_add (GST_BIN_CAST (pipeline), playbin);

  intertest->pipeline = pipeline;

  gst_pipeline_set_auto_flush_bus (GST_PIPELINE (pipeline), FALSE);
  intertest->bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  gst_bus_add_watch (intertest->bus, gst_inter_test_handle_message, intertest);

  intertest->source_element =
      gst_bin_get_by_name (GST_BIN (pipeline), "source");
  gst_println ("source_element is %" GST_PTR_FORMAT, intertest->source_element);

  gst_println ("setting uri to %s", uri);
  g_object_set (intertest->source_element, "uri", uri, NULL);
}

void
gst_inter_test_create_pipeline_test_sources (GstInterTest * intertest)
{
  GString *pipe_desc;
  GstElement *pipeline;
  GError *error = NULL;

  pipe_desc = g_string_new ("");

  g_string_append (pipe_desc, "videotestsrc name=source num-buffers=100 ! ");
  g_string_append (pipe_desc,
      "video/x-raw,format=(string)I420,width=320,height=240 ! ");
  g_string_append (pipe_desc, "timeoverlay ! ");
  g_string_append (pipe_desc, "intervideosink name=sink sync=true ");
  g_string_append (pipe_desc,
      "audiotestsrc samplesperbuffer=1600 num-buffers=100 ! audio/x-raw,format=F32LE ! audioconvert ! ");
  g_string_append (pipe_desc, "interaudiosink sync=true ");

  if (verbose)
    gst_println ("pipeline: %s", pipe_desc->str);

  pipeline = gst_parse_launch (pipe_desc->str, &error);
  g_string_free (pipe_desc, TRUE);

  if (error) {
    gst_println ("pipeline parsing error: %s", error->message);
    gst_object_unref (pipeline);
    g_clear_error (&error);
    return;
  }

  intertest->pipeline = pipeline;

  gst_pipeline_set_auto_flush_bus (GST_PIPELINE (pipeline), FALSE);
  intertest->bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  gst_bus_add_watch (intertest->bus, gst_inter_test_handle_message, intertest);

  intertest->source_element =
      gst_bin_get_by_name (GST_BIN (pipeline), "source");
  intertest->sink_element = gst_bin_get_by_name (GST_BIN (pipeline), "sink");
}

void
gst_inter_test_create_pipeline_server (GstInterTest * intertest)
{
  GString *pipe_desc;
  GstElement *pipeline;
  GError *error = NULL;

  pipe_desc = g_string_new ("");

  g_string_append (pipe_desc, "intervideosrc ! queue ! ");
  g_string_append (pipe_desc, "autovideosink name=sink ");
  g_string_append (pipe_desc, "interaudiosrc ! queue ! ");
  g_string_append (pipe_desc, "autoaudiosink ");

  if (verbose)
    gst_println ("pipeline: %s", pipe_desc->str);

  pipeline = (GstElement *) gst_parse_launch (pipe_desc->str, &error);
  g_string_free (pipe_desc, TRUE);

  if (error) {
    gst_println ("pipeline parsing error: %s", error->message);
    gst_object_unref (pipeline);
    g_clear_error (&error);
    return;
  }

  intertest->pipeline = pipeline;

  gst_pipeline_set_auto_flush_bus (GST_PIPELINE (pipeline), FALSE);
  intertest->bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  gst_bus_add_watch (intertest->bus, gst_inter_test_handle_message, intertest);

  intertest->source_element =
      gst_bin_get_by_name (GST_BIN (pipeline), "source");
  intertest->sink_element = gst_bin_get_by_name (GST_BIN (pipeline), "sink");
}

void
gst_inter_test_start (GstInterTest * intertest)
{
  gst_element_set_state (intertest->pipeline, GST_STATE_READY);

  intertest->timer_id = g_timeout_add_seconds (1, onesecond_timer, intertest);
}

void
gst_inter_test_stop (GstInterTest * intertest)
{
  gst_element_set_state (intertest->pipeline, GST_STATE_NULL);

  g_source_remove (intertest->timer_id);
}

static void
gst_inter_test_handle_eos (GstInterTest * intertest)
{
  gst_inter_test_stop (intertest);
}

static void
gst_inter_test_handle_error (GstInterTest * intertest, GError * error,
    const char *debug)
{
  gst_printerrln ("error: %s", error->message);
  gst_inter_test_stop (intertest);
}

static void
gst_inter_test_handle_warning (GstInterTest * intertest, GError * error,
    const char *debug)
{
  gst_printerrln ("warning: %s", error->message);
}

static void
gst_inter_test_handle_info (GstInterTest * intertest, GError * error,
    const char *debug)
{
  gst_println ("info: %s", error->message);
}

static void
gst_inter_test_handle_null_to_ready (GstInterTest * intertest)
{
  gst_element_set_state (intertest->pipeline, GST_STATE_PAUSED);

}

static void
gst_inter_test_handle_ready_to_paused (GstInterTest * intertest)
{
  if (!intertest->paused_for_buffering) {
    gst_element_set_state (intertest->pipeline, GST_STATE_PLAYING);
  }
}

static void
gst_inter_test_handle_paused_to_playing (GstInterTest * intertest)
{

}

static void
gst_inter_test_handle_playing_to_paused (GstInterTest * intertest)
{

}

static void
gst_inter_test_handle_paused_to_ready (GstInterTest * intertest)
{

}

static void
gst_inter_test_handle_ready_to_null (GstInterTest * intertest)
{
  g_main_loop_quit (intertest->main_loop);
}


static gboolean
gst_inter_test_handle_message (GstBus * bus, GstMessage * message,
    gpointer data)
{
  GstInterTest *intertest = (GstInterTest *) data;

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_EOS:
      gst_inter_test_handle_eos (intertest);
      break;
    case GST_MESSAGE_ERROR:
    {
      GError *error = NULL;
      gchar *debug;

      gst_message_parse_error (message, &error, &debug);
      gst_inter_test_handle_error (intertest, error, debug);
      g_clear_error (&error);
      g_free (debug);
    }
      break;
    case GST_MESSAGE_WARNING:
    {
      GError *error = NULL;
      gchar *debug;

      gst_message_parse_warning (message, &error, &debug);
      gst_inter_test_handle_warning (intertest, error, debug);
      g_clear_error (&error);
      g_free (debug);
    }
      break;
    case GST_MESSAGE_INFO:
    {
      GError *error = NULL;
      gchar *debug;

      gst_message_parse_info (message, &error, &debug);
      gst_inter_test_handle_info (intertest, error, debug);
      g_clear_error (&error);
      g_free (debug);
    }
      break;
    case GST_MESSAGE_TAG:
    {
      GstTagList *tag_list;

      gst_message_parse_tag (message, &tag_list);
      if (verbose)
        gst_println ("tag: %" GST_PTR_FORMAT, tag_list);
      gst_tag_list_unref (tag_list);
    }
      break;
    case GST_MESSAGE_STATE_CHANGED:
    {
      GstState oldstate, newstate, pending;

      gst_message_parse_state_changed (message, &oldstate, &newstate, &pending);
      if (GST_ELEMENT (message->src) == intertest->pipeline) {
        if (verbose)
          gst_println ("state change from %s to %s",
              gst_state_get_name (oldstate), gst_state_get_name (newstate));
        switch (GST_STATE_TRANSITION (oldstate, newstate)) {
          case GST_STATE_CHANGE_NULL_TO_READY:
            gst_inter_test_handle_null_to_ready (intertest);
            break;
          case GST_STATE_CHANGE_READY_TO_PAUSED:
            gst_inter_test_handle_ready_to_paused (intertest);
            break;
          case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
            gst_inter_test_handle_paused_to_playing (intertest);
            break;
          case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
            gst_inter_test_handle_playing_to_paused (intertest);
            break;
          case GST_STATE_CHANGE_PAUSED_TO_READY:
            gst_inter_test_handle_paused_to_ready (intertest);
            break;
          case GST_STATE_CHANGE_READY_TO_NULL:
            gst_inter_test_handle_ready_to_null (intertest);
            break;
          default:
            if (verbose)
              gst_println ("unknown state change from %s to %s",
                  gst_state_get_name (oldstate), gst_state_get_name (newstate));
        }
      }
    }
      break;
    case GST_MESSAGE_BUFFERING:
    {
      int percent;
      gst_message_parse_buffering (message, &percent);
      //gst_println("buffering %d", percent);
      if (!intertest->paused_for_buffering && percent < 100) {
        gst_println ("pausing for buffing");
        intertest->paused_for_buffering = TRUE;
        gst_element_set_state (intertest->pipeline, GST_STATE_PAUSED);
      } else if (intertest->paused_for_buffering && percent == 100) {
        gst_println ("unpausing for buffing");
        intertest->paused_for_buffering = FALSE;
        gst_element_set_state (intertest->pipeline, GST_STATE_PLAYING);
      }
    }
      break;
    case GST_MESSAGE_STATE_DIRTY:
    case GST_MESSAGE_CLOCK_PROVIDE:
    case GST_MESSAGE_CLOCK_LOST:
    case GST_MESSAGE_NEW_CLOCK:
    case GST_MESSAGE_STRUCTURE_CHANGE:
    case GST_MESSAGE_STREAM_STATUS:
      break;
    case GST_MESSAGE_STEP_DONE:
    case GST_MESSAGE_APPLICATION:
    case GST_MESSAGE_ELEMENT:
    case GST_MESSAGE_SEGMENT_START:
    case GST_MESSAGE_SEGMENT_DONE:
    case GST_MESSAGE_LATENCY:
    case GST_MESSAGE_ASYNC_START:
    case GST_MESSAGE_ASYNC_DONE:
    case GST_MESSAGE_REQUEST_STATE:
    case GST_MESSAGE_STEP_START:
    default:
      if (verbose) {
        gst_println ("message: %s", GST_MESSAGE_TYPE_NAME (message));
      }
      break;
    case GST_MESSAGE_QOS:
      break;
  }

  return TRUE;
}

static gboolean
onesecond_timer (gpointer priv)
{
  //GstInterTest *intertest = (GstInterTest *)priv;

  gst_println (".");

  return G_SOURCE_CONTINUE;
}
