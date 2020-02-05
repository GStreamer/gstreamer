/* GStreamer
 * Copyright (C) 2019 Mathieu Duponchelle <mathieu@centricular.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include <stdio.h>

#include <gst/gst.h>
#include <gst/rtsp/rtsp.h>

typedef struct
{
  gchar *range;
  gdouble speed;
  gchar *frames;
  gchar *rate_control;
  gboolean reverse;
} SeekParameters;

typedef struct
{
  GstElement *src;
  GstElement *sink;
  GstElement *pipe;
  SeekParameters *seek_params;
  GMainLoop *loop;
  GIOChannel *io;
  gboolean new_range;
  guint io_watch_id;
  gboolean reset_sync;
} Context;

typedef struct
{
  const gchar *name;
  gboolean has_argument;
  const gchar *help;
    gboolean (*func) (Context * ctx, gchar * arg, gboolean * async);
} Command;

static gboolean cmd_help (Context * ctx, gchar * arg, gboolean * async);
static gboolean cmd_pause (Context * ctx, gchar * arg, gboolean * async);
static gboolean cmd_play (Context * ctx, gchar * arg, gboolean * async);
static gboolean cmd_reverse (Context * ctx, gchar * arg, gboolean * async);
static gboolean cmd_range (Context * ctx, gchar * arg, gboolean * async);
static gboolean cmd_speed (Context * ctx, gchar * arg, gboolean * async);
static gboolean cmd_frames (Context * ctx, gchar * arg, gboolean * async);
static gboolean cmd_rate_control (Context * ctx, gchar * arg, gboolean * async);
static gboolean cmd_step_forward (Context * ctx, gchar * arg, gboolean * async);

static Command commands[] = {
  {"help", FALSE, "Display list of valid commands", cmd_help},
  {"pause", FALSE, "Pause playback", cmd_pause},
  {"play", FALSE, "Resume playback", cmd_play},
  {"reverse", FALSE, "Reverse playback direction", cmd_reverse},
  {"range", TRUE,
        "Seek to the specified range, example: \"range: 19000101T000000Z-19000101T000200Z\"",
      cmd_range},
  {"speed", TRUE, "Set the playback speed, example: \"speed: 1.0\"", cmd_speed},
  {"frames", TRUE,
        "Set the frames trickmode, example: \"frames: intra\", \"frames: predicted\", \"frames: intra/1000\"",
      cmd_frames},
  {"rate-control", TRUE,
        "Set the rate control mode, example: \"rate-control: no\"",
      cmd_rate_control},
  {"s", FALSE, "Step to the following frame (in current playback direction)",
      cmd_step_forward},
  {NULL},
};

static gchar *rtsp_address;

#define MAKE_AND_ADD(var, pipe, name, label, elem_name) \
G_STMT_START { \
  if (G_UNLIKELY (!(var = (gst_element_factory_make (name, elem_name))))) { \
    GST_ERROR ("Could not create element %s", name); \
    goto label; \
  } \
  if (G_UNLIKELY (!gst_bin_add (GST_BIN_CAST (pipe), var))) { \
    GST_ERROR ("Could not add element %s", name); \
    goto label; \
  } \
} G_STMT_END

#define DEFAULT_RANGE "19000101T000000Z-19000101T000200Z"
#define DEFAULT_SPEED 1.0
#define DEFAULT_FRAMES "none"
#define DEFAULT_RATE_CONTROL "yes"
#define DEFAULT_REVERSE FALSE

static void
pad_added_cb (GstElement * src, GstPad * srcpad, GstElement * peer)
{
  GstPad *sinkpad = gst_element_get_static_pad (peer, "sink");

  gst_pad_link (srcpad, sinkpad);

  gst_object_unref (sinkpad);
}

static gboolean
setup (Context * ctx)
{
  GstElement *onvifparse, *queue, *vdepay, *vdec, *vconv, *toverlay, *tee,
      *vqueue;
  gboolean ret = FALSE;

  MAKE_AND_ADD (ctx->src, ctx->pipe, "rtspsrc", done, NULL);
  MAKE_AND_ADD (queue, ctx->pipe, "queue", done, NULL);
  MAKE_AND_ADD (onvifparse, ctx->pipe, "rtponvifparse", done, NULL);
  MAKE_AND_ADD (vdepay, ctx->pipe, "rtph264depay", done, NULL);
  MAKE_AND_ADD (vdec, ctx->pipe, "avdec_h264", done, NULL);
  MAKE_AND_ADD (vconv, ctx->pipe, "videoconvert", done, NULL);
  MAKE_AND_ADD (toverlay, ctx->pipe, "timeoverlay", done, NULL);
  MAKE_AND_ADD (tee, ctx->pipe, "tee", done, NULL);
  MAKE_AND_ADD (vqueue, ctx->pipe, "queue", done, NULL);
  MAKE_AND_ADD (ctx->sink, ctx->pipe, "xvimagesink", done, NULL);

  g_object_set (ctx->src, "location", rtsp_address, NULL);
  g_object_set (ctx->src, "onvif-mode", TRUE, NULL);
  g_object_set (ctx->src, "tcp-timeout", 0, NULL);
  g_object_set (toverlay, "show-times-as-dates", TRUE, NULL);

  g_object_set (toverlay, "datetime-format", "%a %d, %b %Y - %T", NULL);

  g_signal_connect (ctx->src, "pad-added", G_CALLBACK (pad_added_cb), queue);

  if (!gst_element_link_many (queue, onvifparse, vdepay, vdec, vconv, toverlay,
          tee, vqueue, ctx->sink, NULL)) {
    goto done;
  }

  g_object_set (ctx->src, "onvif-rate-control", FALSE, "is-live", FALSE, NULL);

  if (!g_strcmp0 (ctx->seek_params->rate_control, "no")) {
    g_object_set (ctx->sink, "sync", FALSE, NULL);
  }

  ret = TRUE;

done:
  return ret;
}

static GstClockTime
get_current_position (Context * ctx, gboolean reverse)
{
  GstSample *sample;
  GstBuffer *buffer;
  GstClockTime ret;

  g_object_get (ctx->sink, "last-sample", &sample, NULL);

  buffer = gst_sample_get_buffer (sample);

  ret = GST_BUFFER_PTS (buffer);

  if (reverse && GST_CLOCK_TIME_IS_VALID (GST_BUFFER_DURATION (buffer)))
    ret += GST_BUFFER_DURATION (buffer);

  gst_sample_unref (sample);

  return ret;
}

static GstEvent *
translate_seek_parameters (Context * ctx, SeekParameters * seek_params)
{
  GstEvent *ret = NULL;
  gchar *range_str = NULL;
  GstRTSPTimeRange *rtsp_range;
  GstSeekType start_type, stop_type;
  GstClockTime start, stop;
  gdouble rate;
  GstSeekFlags flags;
  gchar **split = NULL;
  GstClockTime trickmode_interval = 0;

  range_str = g_strdup_printf ("clock=%s", seek_params->range);

  if (gst_rtsp_range_parse (range_str, &rtsp_range) != GST_RTSP_OK) {
    GST_ERROR ("Failed to parse range %s", range_str);
    goto done;
  }

  gst_rtsp_range_get_times (rtsp_range, &start, &stop);

  if (start > stop) {
    GST_ERROR ("Invalid range, start > stop: %s", seek_params->range);
    goto done;
  }

  start_type = GST_SEEK_TYPE_SET;
  stop_type = GST_SEEK_TYPE_SET;

  if (!ctx->new_range) {
    GstClockTime current_position =
        get_current_position (ctx, seek_params->reverse);

    if (seek_params->reverse) {
      stop_type = GST_SEEK_TYPE_SET;
      stop = current_position;
    } else {
      start_type = GST_SEEK_TYPE_SET;
      start = current_position;
    }
  }

  ctx->new_range = FALSE;

  flags = GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE;

  split = g_strsplit (seek_params->frames, "/", 2);

  if (!g_strcmp0 (split[0], "intra")) {
    if (split[1]) {
      guint64 interval;
      gchar *end;

      interval = g_ascii_strtoull (split[1], &end, 10);

      if (!end || *end != '\0') {
        GST_ERROR ("Unexpected interval value %s", split[1]);
        goto done;
      }

      trickmode_interval = interval * GST_MSECOND;
    }
    flags |= GST_SEEK_FLAG_TRICKMODE_KEY_UNITS;
  } else if (!g_strcmp0 (split[0], "predicted")) {
    if (split[1]) {
      GST_ERROR ("Predicted frames mode does not allow an interval (%s)",
          seek_params->frames);
      goto done;
    }
    flags |= GST_SEEK_FLAG_TRICKMODE_FORWARD_PREDICTED;
  } else if (g_strcmp0 (split[0], "none")) {
    GST_ERROR ("Invalid frames mode (%s)", seek_params->frames);
    goto done;
  }

  if (seek_params->reverse) {
    rate = -1.0 * seek_params->speed;
  } else {
    rate = 1.0 * seek_params->speed;
  }

  ret = gst_event_new_seek (rate, GST_FORMAT_TIME, flags,
      start_type, start, stop_type, stop);

  if (trickmode_interval)
    gst_event_set_seek_trickmode_interval (ret, trickmode_interval);

done:
  if (split)
    g_strfreev (split);
  g_free (range_str);
  return ret;
}

static void prompt_on (Context * ctx);
static void prompt_off (Context * ctx);

static gboolean
cmd_help (Context * ctx, gchar * arg, gboolean * async)
{
  gboolean ret = TRUE;
  guint i;

  *async = FALSE;

  for (i = 0; commands[i].name; i++) {
    g_print ("%s: %s\n", commands[i].name, commands[i].help);
  }

  return ret;
}

static gboolean
cmd_pause (Context * ctx, gchar * arg, gboolean * async)
{
  gboolean ret;
  GstStateChangeReturn state_ret;

  g_print ("Pausing\n");

  state_ret = gst_element_set_state (ctx->pipe, GST_STATE_PAUSED);

  *async = state_ret == GST_STATE_CHANGE_ASYNC;
  ret = state_ret != GST_STATE_CHANGE_FAILURE;

  return ret;
}

static gboolean
cmd_play (Context * ctx, gchar * arg, gboolean * async)
{
  gboolean ret;
  GstStateChangeReturn state_ret;

  g_print ("Playing\n");

  state_ret = gst_element_set_state (ctx->pipe, GST_STATE_PLAYING);

  *async = state_ret == GST_STATE_CHANGE_ASYNC;
  ret = state_ret != GST_STATE_CHANGE_FAILURE;

  return ret;
}

static gboolean
do_seek (Context * ctx)
{
  gboolean ret = FALSE;
  GstEvent *event;

  if (!(event = translate_seek_parameters (ctx, ctx->seek_params))) {
    GST_ERROR ("Failed to create seek event");
    goto done;
  }

  if (ctx->seek_params->reverse)
    g_object_set (ctx->src, "onvif-rate-control", FALSE, NULL);

  if (ctx->reset_sync) {
    g_object_set (ctx->sink, "sync", TRUE, NULL);
    ctx->reset_sync = FALSE;
  }

  if (!gst_element_send_event (ctx->src, event)) {
    GST_ERROR ("Failed to seek rtspsrc");
    g_main_loop_quit (ctx->loop);
    goto done;
  }

  ret = TRUE;

done:
  return ret;
}

static gboolean
cmd_reverse (Context * ctx, gchar * arg, gboolean * async)
{
  gboolean ret = TRUE;

  g_print ("Reversing playback direction\n");

  ctx->seek_params->reverse = !ctx->seek_params->reverse;

  ret = do_seek (ctx);

  *async = ret == TRUE;

  return ret;
}

static gboolean
cmd_range (Context * ctx, gchar * arg, gboolean * async)
{
  gboolean ret = TRUE;

  g_print ("Switching to new range\n");

  g_free (ctx->seek_params->range);
  ctx->seek_params->range = g_strdup (arg);
  ctx->new_range = TRUE;

  ret = do_seek (ctx);

  *async = ret == TRUE;

  return ret;
}

static gboolean
cmd_speed (Context * ctx, gchar * arg, gboolean * async)
{
  gboolean ret = FALSE;
  gchar *endptr = NULL;
  gdouble new_speed;

  new_speed = g_ascii_strtod (arg, &endptr);

  g_print ("Switching gears\n");

  if (endptr == NULL || *endptr != '\0' || new_speed <= 0.0) {
    GST_ERROR ("Invalid value for speed: %s", arg);
    goto done;
  }

  ctx->seek_params->speed = new_speed;
  ret = do_seek (ctx);

done:
  *async = ret == TRUE;
  return ret;
}

static gboolean
cmd_frames (Context * ctx, gchar * arg, gboolean * async)
{
  gboolean ret = TRUE;

  g_print ("Changing Frames trickmode\n");

  g_free (ctx->seek_params->frames);
  ctx->seek_params->frames = g_strdup (arg);
  ret = do_seek (ctx);
  *async = ret == TRUE;

  return ret;
}

static gboolean
cmd_rate_control (Context * ctx, gchar * arg, gboolean * async)
{
  gboolean ret = FALSE;

  *async = FALSE;

  if (!g_strcmp0 (arg, "no")) {
    g_object_set (ctx->sink, "sync", FALSE, NULL);
    ret = TRUE;
  } else if (!g_strcmp0 (arg, "yes")) {
    /* TODO: there probably is a solution that doesn't involve sending
     * a request to the server to reset our position */
    ctx->reset_sync = TRUE;
    ret = do_seek (ctx);
    *async = TRUE;
  } else {
    GST_ERROR ("Invalid rate-control: %s", arg);
    goto done;
  }

  ret = TRUE;

done:
  return ret;
}

static gboolean
cmd_step_forward (Context * ctx, gchar * arg, gboolean * async)
{
  gboolean ret = FALSE;
  GstEvent *event;

  event = gst_event_new_step (GST_FORMAT_BUFFERS, 1, 1.0, TRUE, FALSE);

  g_print ("Stepping\n");

  if (!gst_element_send_event (ctx->sink, event)) {
    GST_ERROR ("Failed to step forward");
    goto done;
  }

  ret = TRUE;

done:
  *async = ret == TRUE;
  return ret;
}

static void
handle_command (Context * ctx, gchar * cmd)
{
  gchar **split;
  guint i;
  gboolean valid_command = FALSE;

  split = g_strsplit (cmd, ":", 0);

  cmd = g_strstrip (split[0]);

  if (cmd == NULL || *cmd == '\0') {
    g_print ("> ");
    goto done;
  }

  for (i = 0; commands[i].name; i++) {
    if (!g_strcmp0 (commands[i].name, cmd)) {
      valid_command = TRUE;
      if (commands[i].has_argument && g_strv_length (split) != 2) {
        g_print ("Command %s expects exactly one argument:\n%s: %s\n", cmd,
            commands[i].name, commands[i].help);
      } else if (!commands[i].has_argument && g_strv_length (split) != 1) {
        g_print ("Command %s expects no argument:\n%s: %s\n", cmd,
            commands[i].name, commands[i].help);
      } else {
        gboolean async = FALSE;

        if (commands[i].func (ctx,
                commands[i].has_argument ? g_strstrip (split[1]) : NULL, &async)
            && async)
          prompt_off (ctx);
        else
          g_print ("> ");
      }
      break;
    }
  }

  if (!valid_command) {
    g_print ("Invalid command %s\n> ", cmd);
  }

done:
  g_strfreev (split);
}

static gboolean
io_callback (GIOChannel * io, GIOCondition condition, Context * ctx)
{
  gboolean ret = TRUE;
  gchar *str;
  GError *error = NULL;

  switch (condition) {
    case G_IO_PRI:
    case G_IO_IN:
      switch (g_io_channel_read_line (io, &str, NULL, NULL, &error)) {
        case G_IO_STATUS_ERROR:
          GST_ERROR ("Failed to read commands from stdin: %s", error->message);
          g_clear_error (&error);
          g_main_loop_quit (ctx->loop);
          break;
        case G_IO_STATUS_EOF:
          g_print ("EOF received, bye\n");
          g_main_loop_quit (ctx->loop);
          break;
        case G_IO_STATUS_AGAIN:
          break;
        case G_IO_STATUS_NORMAL:
          handle_command (ctx, str);
          g_free (str);
          break;
      }
      break;
    case G_IO_ERR:
    case G_IO_HUP:
      GST_ERROR ("Failed to read commands from stdin");
      g_main_loop_quit (ctx->loop);
      break;
    case G_IO_OUT:
    default:
      break;
  }

  return ret;
}

#ifndef STDIN_FILENO
#ifdef G_OS_WIN32
#define STDIN_FILENO _fileno(stdin)
#else /* !G_OS_WIN32 */
#define STDIN_FILENO 0
#endif /* G_OS_WIN32 */
#endif /* STDIN_FILENO */

static void
prompt_on (Context * ctx)
{
  g_assert (!ctx->io);
  ctx->io = g_io_channel_unix_new (STDIN_FILENO);
  ctx->io_watch_id =
      g_io_add_watch (ctx->io, G_IO_IN, (GIOFunc) io_callback, ctx);
  g_print ("> ");
}

static void
prompt_off (Context * ctx)
{
  g_assert (ctx->io);
  g_source_remove (ctx->io_watch_id);
  g_io_channel_unref (ctx->io);
  ctx->io = NULL;
}

static gboolean
bus_message_cb (GstBus * bus, GstMessage * message, Context * ctx)
{
  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_STATE_CHANGED:{
      GstState olds, news, pendings;

      if (GST_MESSAGE_SRC (message) == GST_OBJECT (ctx->pipe)) {
        gst_message_parse_state_changed (message, &olds, &news, &pendings);
        GST_DEBUG_BIN_TO_DOT_FILE (GST_BIN (ctx->pipe),
            GST_DEBUG_GRAPH_SHOW_ALL, "playing");
      }
      break;
    }
    case GST_MESSAGE_ERROR:{
      GError *error = NULL;
      gchar *debug;

      gst_message_parse_error (message, &error, &debug);

      gst_printerr ("Error: %s (%s)\n", error->message, debug);
      g_clear_error (&error);
      g_free (debug);
      g_main_loop_quit (ctx->loop);
      break;
    }
    case GST_MESSAGE_LATENCY:{
      gst_bin_recalculate_latency (GST_BIN (ctx->pipe));
      break;
    }
    case GST_MESSAGE_ASYNC_DONE:{
      prompt_on (ctx);
    }
    default:
      break;
  }

  return TRUE;
}

int
main (int argc, char **argv)
{
  GOptionContext *optctx;
  Context ctx = { 0, };
  GstBus *bus;
  gint ret = 1;
  GError *error = NULL;
  const gchar *range = NULL;
  const gchar *frames = NULL;
  const gchar *rate_control = NULL;
  gchar *default_speed =
      g_strdup_printf ("Speed to request (default: %.1f)", DEFAULT_SPEED);
  SeekParameters seek_params =
      { NULL, DEFAULT_SPEED, NULL, NULL, DEFAULT_REVERSE };
  GOptionEntry entries[] = {
    {"range", 0, 0, G_OPTION_ARG_STRING, &range,
        "Range to seek (default: " DEFAULT_RANGE ")", "RANGE"},
    {"speed", 0, 0, G_OPTION_ARG_DOUBLE, &seek_params.speed,
        default_speed, "SPEED"},
    {"frames", 0, 0, G_OPTION_ARG_STRING, &frames,
        "Frames to request (default: " DEFAULT_FRAMES ")", "FRAMES"},
    {"rate-control", 0, 0, G_OPTION_ARG_STRING, &rate_control,
        "Apply rate control on the client side (default: "
          DEFAULT_RATE_CONTROL ")", "RATE_CONTROL"},
    {"reverse", 0, 0, G_OPTION_ARG_NONE, &seek_params.reverse,
        "Playback direction", ""},
    {NULL}
  };

  optctx = g_option_context_new ("<rtsp-url> - ONVIF RTSP Client");
  g_option_context_add_main_entries (optctx, entries, NULL);
  g_option_context_add_group (optctx, gst_init_get_option_group ());
  if (!g_option_context_parse (optctx, &argc, &argv, &error)) {
    g_printerr ("Error parsing options: %s\n", error->message);
    g_option_context_free (optctx);
    g_clear_error (&error);
    return -1;
  }
  if (argc < 2) {
    g_print ("%s\n", g_option_context_get_help (optctx, TRUE, NULL));
    return 1;
  }
  rtsp_address = argv[1];
  g_option_context_free (optctx);

  seek_params.range = g_strdup (range ? range : DEFAULT_RANGE);
  seek_params.frames = g_strdup (frames ? frames : DEFAULT_FRAMES);
  seek_params.rate_control =
      g_strdup (rate_control ? rate_control : DEFAULT_RATE_CONTROL);

  if (seek_params.speed <= 0.0) {
    GST_ERROR ("SPEED must be a positive number");
    return 1;
  }

  ctx.seek_params = &seek_params;
  ctx.new_range = TRUE;
  ctx.reset_sync = FALSE;

  ctx.pipe = gst_pipeline_new (NULL);
  if (!setup (&ctx)) {
    g_printerr ("Damn\n");
    goto done;
  }

  g_print ("Type help for the list of available commands\n");

  do_seek (&ctx);

  ctx.loop = g_main_loop_new (NULL, FALSE);

  bus = gst_pipeline_get_bus (GST_PIPELINE (ctx.pipe));
  gst_bus_add_watch (bus, (GstBusFunc) bus_message_cb, &ctx);

  /* This will make rtspsrc progress to the OPEN state, at which point we can seek it */
  if (!gst_element_set_state (ctx.pipe, GST_STATE_PLAYING))
    goto done;

  g_main_loop_run (ctx.loop);

  g_main_loop_unref (ctx.loop);

  gst_bus_remove_watch (bus);
  gst_object_unref (bus);
  gst_element_set_state (ctx.pipe, GST_STATE_NULL);
  gst_object_unref (ctx.pipe);

  ret = 0;

done:
  g_free (seek_params.range);
  g_free (seek_params.frames);
  g_free (seek_params.rate_control);
  g_free (default_speed);
  return ret;
}
