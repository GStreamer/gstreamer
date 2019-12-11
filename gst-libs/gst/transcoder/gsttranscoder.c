/* GStreamer
 *
 * Copyright (C) 2014-2015 Sebastian Dröge <sebastian@centricular.com>
 * Copyright (C) 2015 Thibault Saunier <tsaunier@gnome.org>
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

/**
 * SECTION:gsttranscoder
 * @short_description: High level API to transcode media files
 * from one format to any other format using the GStreamer framework.
 * @symbols:
 *   - gst_transcoder_error_quark
 */

#include "gsttranscoder.h"

GST_DEBUG_CATEGORY_STATIC (gst_transcoder_debug);
#define GST_CAT_DEFAULT gst_transcoder_debug

#define DEFAULT_URI NULL
#define DEFAULT_POSITION GST_CLOCK_TIME_NONE
#define DEFAULT_DURATION GST_CLOCK_TIME_NONE
#define DEFAULT_POSITION_UPDATE_INTERVAL_MS 100
#define DEFAULT_AVOID_REENCODING   FALSE

GQuark
gst_transcoder_error_quark (void)
{
  static GQuark quark;

  if (!quark)
    quark = g_quark_from_static_string ("gst-transcoder-error-quark");

  return quark;
}

enum
{
  PROP_0,
  PROP_SIGNAL_DISPATCHER,
  PROP_SRC_URI,
  PROP_DEST_URI,
  PROP_PROFILE,
  PROP_POSITION,
  PROP_DURATION,
  PROP_PIPELINE,
  PROP_POSITION_UPDATE_INTERVAL,
  PROP_AVOID_REENCODING,
  PROP_LAST
};

enum
{
  SIGNAL_POSITION_UPDATED,
  SIGNAL_DURATION_CHANGED,
  SIGNAL_DONE,
  SIGNAL_ERROR,
  SIGNAL_WARNING,
  SIGNAL_LAST
};

struct _GstTranscoder
{
  GstObject parent;

  GstTranscoderSignalDispatcher *signal_dispatcher;

  GstEncodingProfile *profile;
  gchar *source_uri;
  gchar *dest_uri;

  GThread *thread;
  GCond cond;
  GMainContext *context;
  GMainLoop *loop;

  GstElement *transcodebin;
  GstBus *bus;
  GstState target_state, current_state;
  gboolean is_live, is_eos;
  GSource *tick_source, *ready_timeout_source;

  guint position_update_interval_ms;
  gint wanted_cpu_usage;

  GstClockTime last_duration;
};

struct _GstTranscoderClass
{
  GstObjectClass parent_class;
};

static void
gst_transcoder_signal_dispatcher_dispatch (GstTranscoderSignalDispatcher * self,
    GstTranscoder * transcoder, void (*emitter) (gpointer data), gpointer data,
    GDestroyNotify destroy);

#define parent_class gst_transcoder_parent_class
G_DEFINE_TYPE (GstTranscoder, gst_transcoder, GST_TYPE_OBJECT);

static guint signals[SIGNAL_LAST] = { 0, };
static GParamSpec *param_specs[PROP_LAST] = { NULL, };

static void gst_transcoder_dispose (GObject * object);
static void gst_transcoder_finalize (GObject * object);
static void gst_transcoder_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_transcoder_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_transcoder_constructed (GObject * object);

static gpointer gst_transcoder_main (gpointer data);

static gboolean gst_transcoder_set_position_update_interval_internal (gpointer
    user_data);


/**
 * gst_transcoder_set_cpu_usage:
 * @self: The GstTranscoder to limit CPU usage on.
 * @cpu_usage: The percentage of the CPU the process running the transcoder
 * should try to use. It takes into account the number of cores available.
 *
 * Sets @cpu_usage as target percentage CPU usage of the process running the
 * transcoding task. It will modulate the transcoding speed to reach that target
 * usage.
 */
void
gst_transcoder_set_cpu_usage (GstTranscoder * self, gint cpu_usage)
{
  GST_OBJECT_LOCK (self);
  self->wanted_cpu_usage = cpu_usage;
  if (self->transcodebin)
    g_object_set (self->transcodebin, "cpu-usage", cpu_usage, NULL);
  GST_OBJECT_UNLOCK (self);
}

static void
gst_transcoder_init (GstTranscoder * self)
{
  GST_TRACE_OBJECT (self, "Initializing");

  self = gst_transcoder_get_instance_private (self);

  g_cond_init (&self->cond);

  self->context = g_main_context_new ();
  self->loop = g_main_loop_new (self->context, FALSE);
  self->wanted_cpu_usage = 100;

  self->position_update_interval_ms = DEFAULT_POSITION_UPDATE_INTERVAL_MS;

  GST_TRACE_OBJECT (self, "Initialized");
}

static void
gst_transcoder_class_init (GstTranscoderClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->set_property = gst_transcoder_set_property;
  gobject_class->get_property = gst_transcoder_get_property;
  gobject_class->dispose = gst_transcoder_dispose;
  gobject_class->finalize = gst_transcoder_finalize;
  gobject_class->constructed = gst_transcoder_constructed;

  param_specs[PROP_SIGNAL_DISPATCHER] =
      g_param_spec_object ("signal-dispatcher",
      "Signal Dispatcher", "Dispatcher for the signals to e.g. event loops",
      GST_TYPE_TRANSCODER_SIGNAL_DISPATCHER,
      G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  param_specs[PROP_SRC_URI] =
      g_param_spec_string ("src-uri", "URI", "Source URI", DEFAULT_URI,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  param_specs[PROP_DEST_URI] =
      g_param_spec_string ("dest-uri", "URI", "Source URI", DEFAULT_URI,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  param_specs[PROP_PROFILE] =
      g_param_spec_object ("profile", "Profile",
      "The GstEncodingProfile to use", GST_TYPE_ENCODING_PROFILE,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  param_specs[PROP_POSITION] =
      g_param_spec_uint64 ("position", "Position", "Current Position",
      0, G_MAXUINT64, DEFAULT_POSITION,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  param_specs[PROP_DURATION] =
      g_param_spec_uint64 ("duration", "Duration", "Duration",
      0, G_MAXUINT64, DEFAULT_DURATION,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  param_specs[PROP_PIPELINE] =
      g_param_spec_object ("pipeline", "Pipeline",
      "GStreamer pipeline that is used",
      GST_TYPE_ELEMENT, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  param_specs[PROP_POSITION_UPDATE_INTERVAL] =
      g_param_spec_uint ("position-update-interval", "Position update interval",
      "Interval in milliseconds between two position-updated signals."
      "Pass 0 to stop updating the position.",
      0, 10000, DEFAULT_POSITION_UPDATE_INTERVAL_MS,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /**
   * GstTranscoder:avoid-reencoding:
   *
   * See #encodebin:avoid-reencoding
   */
  param_specs[PROP_AVOID_REENCODING] =
      g_param_spec_boolean ("avoid-reencoding", "Avoid re-encoding",
      "Whether to re-encode portions of compatible video streams that lay on segment boundaries",
      DEFAULT_AVOID_REENCODING, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class, PROP_LAST, param_specs);

  signals[SIGNAL_POSITION_UPDATED] =
      g_signal_new ("position-updated", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE | G_SIGNAL_NO_HOOKS, 0, NULL,
      NULL, NULL, G_TYPE_NONE, 1, GST_TYPE_CLOCK_TIME);

  signals[SIGNAL_DURATION_CHANGED] =
      g_signal_new ("duration-changed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE | G_SIGNAL_NO_HOOKS, 0, NULL,
      NULL, NULL, G_TYPE_NONE, 1, GST_TYPE_CLOCK_TIME);

  signals[SIGNAL_DONE] =
      g_signal_new ("done", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE | G_SIGNAL_NO_HOOKS, 0, NULL,
      NULL, NULL, G_TYPE_NONE, 0, G_TYPE_INVALID);

  signals[SIGNAL_ERROR] =
      g_signal_new ("error", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE | G_SIGNAL_NO_HOOKS, 0, NULL,
      NULL, NULL, G_TYPE_NONE, 2, G_TYPE_ERROR, GST_TYPE_STRUCTURE);

  signals[SIGNAL_WARNING] =
      g_signal_new ("warning", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE | G_SIGNAL_NO_HOOKS, 0, NULL,
      NULL, NULL, G_TYPE_NONE, 2, G_TYPE_ERROR, GST_TYPE_STRUCTURE);
}

static void
gst_transcoder_dispose (GObject * object)
{
  GstTranscoder *self = GST_TRANSCODER (object);

  GST_TRACE_OBJECT (self, "Stopping main thread");

  if (self->loop) {
    g_main_loop_quit (self->loop);

    g_thread_join (self->thread);
    self->thread = NULL;

    g_main_loop_unref (self->loop);
    self->loop = NULL;

    g_main_context_unref (self->context);
    self->context = NULL;

  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_transcoder_finalize (GObject * object)
{
  GstTranscoder *self = GST_TRANSCODER (object);

  GST_TRACE_OBJECT (self, "Finalizing");

  g_free (self->source_uri);
  g_free (self->dest_uri);
  if (self->signal_dispatcher)
    g_object_unref (self->signal_dispatcher);
  g_cond_clear (&self->cond);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_transcoder_constructed (GObject * object)
{
  GstTranscoder *self = GST_TRANSCODER (object);

  GST_TRACE_OBJECT (self, "Constructed");

  self->transcodebin =
      gst_element_factory_make ("uritranscodebin", "uritranscodebin");

  g_object_set (self->transcodebin, "source-uri", self->source_uri,
      "dest-uri", self->dest_uri, "profile", self->profile,
      "cpu-usage", self->wanted_cpu_usage, NULL);

  GST_OBJECT_LOCK (self);
  self->thread = g_thread_new ("GstTranscoder", gst_transcoder_main, self);
  while (!self->loop || !g_main_loop_is_running (self->loop))
    g_cond_wait (&self->cond, GST_OBJECT_GET_LOCK (self));
  GST_OBJECT_UNLOCK (self);

  G_OBJECT_CLASS (parent_class)->constructed (object);
}

static void
gst_transcoder_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstTranscoder *self = GST_TRANSCODER (object);

  switch (prop_id) {
    case PROP_SIGNAL_DISPATCHER:
      self->signal_dispatcher = g_value_dup_object (value);
      break;
    case PROP_SRC_URI:{
      GST_OBJECT_LOCK (self);
      g_free (self->source_uri);
      self->source_uri = g_value_dup_string (value);
      GST_DEBUG_OBJECT (self, "Set source_uri=%s", self->source_uri);
      GST_OBJECT_UNLOCK (self);
      break;
    }
    case PROP_DEST_URI:{
      GST_OBJECT_LOCK (self);
      g_free (self->dest_uri);
      self->dest_uri = g_value_dup_string (value);
      GST_DEBUG_OBJECT (self, "Set dest_uri=%s", self->dest_uri);
      GST_OBJECT_UNLOCK (self);
      break;
    }
    case PROP_POSITION_UPDATE_INTERVAL:
      GST_OBJECT_LOCK (self);
      self->position_update_interval_ms = g_value_get_uint (value);
      GST_DEBUG_OBJECT (self, "Set position update interval=%u ms",
          g_value_get_uint (value));
      GST_OBJECT_UNLOCK (self);

      gst_transcoder_set_position_update_interval_internal (self);
      break;
    case PROP_PROFILE:
      GST_OBJECT_LOCK (self);
      self->profile = g_value_dup_object (value);
      GST_OBJECT_UNLOCK (self);
      break;
    case PROP_AVOID_REENCODING:
      g_object_set (self->transcodebin, "avoid-reencoding",
          g_value_get_boolean (value), NULL);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_transcoder_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstTranscoder *self = GST_TRANSCODER (object);

  switch (prop_id) {
    case PROP_SRC_URI:
      GST_OBJECT_LOCK (self);
      g_value_set_string (value, self->source_uri);
      GST_OBJECT_UNLOCK (self);
      break;
    case PROP_DEST_URI:
      GST_OBJECT_LOCK (self);
      g_value_set_string (value, self->dest_uri);
      GST_OBJECT_UNLOCK (self);
      break;
    case PROP_POSITION:{
      gint64 position = 0;

      if (self->is_eos)
        position = self->last_duration;
      else
        gst_element_query_position (self->transcodebin, GST_FORMAT_TIME,
            &position);
      g_value_set_uint64 (value, position);
      GST_TRACE_OBJECT (self, "Returning position=%" GST_TIME_FORMAT,
          GST_TIME_ARGS (g_value_get_uint64 (value)));
      break;
    }
    case PROP_DURATION:{
      gint64 duration = 0;

      gst_element_query_duration (self->transcodebin, GST_FORMAT_TIME,
          &duration);
      g_value_set_uint64 (value, duration);
      GST_TRACE_OBJECT (self, "Returning duration=%" GST_TIME_FORMAT,
          GST_TIME_ARGS (g_value_get_uint64 (value)));
      break;
    }
    case PROP_PIPELINE:
      g_value_set_object (value, self->transcodebin);
      break;
    case PROP_POSITION_UPDATE_INTERVAL:
      GST_OBJECT_LOCK (self);
      g_value_set_uint (value,
          gst_transcoder_get_position_update_interval (self));
      GST_OBJECT_UNLOCK (self);
      break;
    case PROP_PROFILE:
      GST_OBJECT_LOCK (self);
      g_value_set_object (value, self->profile);
      GST_OBJECT_UNLOCK (self);
      break;
    case PROP_AVOID_REENCODING:
    {
      gboolean avoid_reencoding;

      g_object_get (self->transcodebin, "avoid-reencoding", &avoid_reencoding,
          NULL);
      g_value_set_boolean (value, avoid_reencoding);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
main_loop_running_cb (gpointer user_data)
{
  GstTranscoder *self = GST_TRANSCODER (user_data);

  GST_TRACE_OBJECT (self, "Main loop running now");

  GST_OBJECT_LOCK (self);
  g_cond_signal (&self->cond);
  GST_OBJECT_UNLOCK (self);

  return G_SOURCE_REMOVE;
}

typedef struct
{
  GstTranscoder *transcoder;
  GstClockTime position;
} PositionUpdatedSignalData;

static void
position_updated_dispatch (gpointer user_data)
{
  PositionUpdatedSignalData *data = user_data;

  if (data->transcoder->target_state >= GST_STATE_PAUSED) {
    g_signal_emit (data->transcoder, signals[SIGNAL_POSITION_UPDATED], 0,
        data->position);
    g_object_notify_by_pspec (G_OBJECT (data->transcoder),
        param_specs[PROP_POSITION]);
  }
}

static void
position_updated_signal_data_free (PositionUpdatedSignalData * data)
{
  g_object_unref (data->transcoder);
  g_free (data);
}

static gboolean
tick_cb (gpointer user_data)
{
  GstTranscoder *self = GST_TRANSCODER (user_data);
  gint64 position;

  if (self->target_state >= GST_STATE_PAUSED
      && gst_element_query_position (self->transcodebin, GST_FORMAT_TIME,
          &position)) {
    GST_LOG_OBJECT (self, "Position %" GST_TIME_FORMAT,
        GST_TIME_ARGS (position));

    if (g_signal_handler_find (self, G_SIGNAL_MATCH_ID,
            signals[SIGNAL_POSITION_UPDATED], 0, NULL, NULL, NULL) != 0) {
      PositionUpdatedSignalData *data = g_new0 (PositionUpdatedSignalData, 1);

      data->transcoder = g_object_ref (self);
      data->position = position;
      gst_transcoder_signal_dispatcher_dispatch (self->signal_dispatcher, self,
          position_updated_dispatch, data,
          (GDestroyNotify) position_updated_signal_data_free);
    }
  }

  return G_SOURCE_CONTINUE;
}

static void
add_tick_source (GstTranscoder * self)
{
  if (self->tick_source)
    return;

  if (!self->position_update_interval_ms)
    return;

  self->tick_source = g_timeout_source_new (self->position_update_interval_ms);
  g_source_set_callback (self->tick_source, (GSourceFunc) tick_cb, self, NULL);
  g_source_attach (self->tick_source, self->context);
}

static void
remove_tick_source (GstTranscoder * self)
{
  if (!self->tick_source)
    return;

  g_source_destroy (self->tick_source);
  g_source_unref (self->tick_source);
  self->tick_source = NULL;
}

typedef struct
{
  GstTranscoder *transcoder;
  GError *err;
  GstStructure *details;
} IssueSignalData;

static void
error_dispatch (gpointer user_data)
{
  IssueSignalData *data = user_data;

  g_signal_emit (data->transcoder, signals[SIGNAL_ERROR], 0, data->err,
      data->details);
}

static void
free_issue_signal_data (IssueSignalData * data)
{
  g_object_unref (data->transcoder);
  if (data->details)
    gst_structure_free (data->details);
  g_clear_error (&data->err);
  g_free (data);
}

static void
emit_error (GstTranscoder * self, GError * err, const GstStructure * details)
{
  if (g_signal_handler_find (self, G_SIGNAL_MATCH_ID,
          signals[SIGNAL_ERROR], 0, NULL, NULL, NULL) != 0) {
    IssueSignalData *data = g_new0 (IssueSignalData, 1);

    data->transcoder = g_object_ref (self);
    data->err = g_error_copy (err);
    if (details)
      data->details = gst_structure_copy (details);
    gst_transcoder_signal_dispatcher_dispatch (self->signal_dispatcher, self,
        error_dispatch, data, (GDestroyNotify) free_issue_signal_data);
  }

  g_error_free (err);

  remove_tick_source (self);

  self->target_state = GST_STATE_NULL;
  self->current_state = GST_STATE_NULL;
  self->is_live = FALSE;
  self->is_eos = FALSE;
  gst_element_set_state (self->transcodebin, GST_STATE_NULL);
}

static void
dump_dot_file (GstTranscoder * self, const gchar * name)
{
  gchar *full_name;

  full_name = g_strdup_printf ("gst-transcoder.%p.%s", self, name);

  GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (self->transcodebin),
      GST_DEBUG_GRAPH_SHOW_VERBOSE, full_name);

  g_free (full_name);
}

static void
warning_dispatch (gpointer user_data)
{
  IssueSignalData *data = user_data;

  g_signal_emit (data->transcoder, signals[SIGNAL_WARNING], 0, data->err,
      data->details);
}

static void
emit_warning (GstTranscoder * self, GError * err, const GstStructure * details)
{
  if (g_signal_handler_find (self, G_SIGNAL_MATCH_ID,
          signals[SIGNAL_WARNING], 0, NULL, NULL, NULL) != 0) {
    IssueSignalData *data = g_new0 (IssueSignalData, 1);

    data->transcoder = g_object_ref (self);
    data->err = g_error_copy (err);
    if (details)
      data->details = gst_structure_copy (details);
    gst_transcoder_signal_dispatcher_dispatch (self->signal_dispatcher, self,
        warning_dispatch, data, (GDestroyNotify) free_issue_signal_data);
  }

  g_error_free (err);
}

static void
error_cb (G_GNUC_UNUSED GstBus * bus, GstMessage * msg, gpointer user_data)
{
  GError *err;
  GstTranscoder *self = GST_TRANSCODER (user_data);
  gchar *name, *debug, *message;
  GstStructure *details = NULL;

  dump_dot_file (self, "error");

  gst_message_parse_error (msg, &err, &debug);
  gst_message_parse_error_details (msg, (const GstStructure **) &details);

  if (!details)
    details = gst_structure_new_empty ("details");
  else
    details = gst_structure_copy (details);

  name = gst_object_get_path_string (msg->src);
  message = gst_error_get_message (err->domain, err->code);

  gst_structure_set (details, "debug", G_TYPE_STRING, debug,
      "msg-source-element-name", G_TYPE_STRING, "name",
      "msg-source-type", G_TYPE_GTYPE, G_OBJECT_TYPE (msg->src),
      "msg-error", G_TYPE_STRING, message, NULL);
  emit_error (self, g_error_copy (err), details);

  gst_structure_free (details);
  g_clear_error (&err);
  g_free (debug);
  g_free (name);
  g_free (message);
}

static void
warning_cb (G_GNUC_UNUSED GstBus * bus, GstMessage * msg, gpointer user_data)
{
  GstTranscoder *self = GST_TRANSCODER (user_data);
  GError *err, *transcoder_err;
  gchar *name, *debug, *message, *full_message;
  const GstStructure *details = NULL;

  dump_dot_file (self, "warning");

  gst_message_parse_warning (msg, &err, &debug);
  gst_message_parse_warning_details (msg, &details);

  name = gst_object_get_path_string (msg->src);
  message = gst_error_get_message (err->domain, err->code);

  if (debug)
    full_message =
        g_strdup_printf ("Warning from element %s: %s\n%s\n%s", name, message,
        err->message, debug);
  else
    full_message =
        g_strdup_printf ("Warning from element %s: %s\n%s", name, message,
        err->message);

  GST_WARNING_OBJECT (self, "WARNING: from element %s: %s", name, err->message);
  if (debug != NULL)
    GST_WARNING_OBJECT (self, "Additional debug info: %s", debug);

  transcoder_err =
      g_error_new_literal (GST_TRANSCODER_ERROR, GST_TRANSCODER_ERROR_FAILED,
      full_message);
  emit_warning (self, transcoder_err, details);

  g_clear_error (&err);
  g_free (debug);
  g_free (name);
  g_free (full_message);
  g_free (message);
}

static void
eos_dispatch (gpointer user_data)
{
  g_signal_emit (user_data, signals[SIGNAL_DONE], 0);
}

static void
eos_cb (G_GNUC_UNUSED GstBus * bus, G_GNUC_UNUSED GstMessage * msg,
    gpointer user_data)
{
  GstTranscoder *self = GST_TRANSCODER (user_data);

  GST_DEBUG_OBJECT (self, "End of stream");

  gst_element_query_duration (self->transcodebin, GST_FORMAT_TIME,
      (gint64 *) & self->last_duration);
  tick_cb (self);
  remove_tick_source (self);

  if (g_signal_handler_find (self, G_SIGNAL_MATCH_ID,
          signals[SIGNAL_DONE], 0, NULL, NULL, NULL) != 0) {
    gst_transcoder_signal_dispatcher_dispatch (self->signal_dispatcher, self,
        eos_dispatch, g_object_ref (self), (GDestroyNotify) g_object_unref);
  }
  self->is_eos = TRUE;
}

static void
clock_lost_cb (G_GNUC_UNUSED GstBus * bus, G_GNUC_UNUSED GstMessage * msg,
    gpointer user_data)
{
  GstTranscoder *self = GST_TRANSCODER (user_data);
  GstStateChangeReturn state_ret;

  GST_DEBUG_OBJECT (self, "Clock lost");
  if (self->target_state >= GST_STATE_PLAYING) {
    state_ret = gst_element_set_state (self->transcodebin, GST_STATE_PAUSED);
    if (state_ret != GST_STATE_CHANGE_FAILURE)
      state_ret = gst_element_set_state (self->transcodebin, GST_STATE_PLAYING);

    if (state_ret == GST_STATE_CHANGE_FAILURE)
      emit_error (self, g_error_new (GST_TRANSCODER_ERROR,
              GST_TRANSCODER_ERROR_FAILED, "Failed to handle clock loss"),
          NULL);
  }
}

typedef struct
{
  GstTranscoder *transcoder;
  GstClockTime duration;
} DurationChangedSignalData;

static void
duration_changed_dispatch (gpointer user_data)
{
  DurationChangedSignalData *data = user_data;

  if (data->transcoder->target_state >= GST_STATE_PAUSED) {
    g_signal_emit (data->transcoder, signals[SIGNAL_DURATION_CHANGED], 0,
        data->duration);
    g_object_notify_by_pspec (G_OBJECT (data->transcoder),
        param_specs[PROP_DURATION]);
  }
}

static void
duration_changed_signal_data_free (DurationChangedSignalData * data)
{
  g_object_unref (data->transcoder);
  g_free (data);
}

static void
emit_duration_changed (GstTranscoder * self, GstClockTime duration)
{
  GST_DEBUG_OBJECT (self, "Duration changed %" GST_TIME_FORMAT,
      GST_TIME_ARGS (duration));

  if (g_signal_handler_find (self, G_SIGNAL_MATCH_ID,
          signals[SIGNAL_DURATION_CHANGED], 0, NULL, NULL, NULL) != 0) {
    DurationChangedSignalData *data = g_new0 (DurationChangedSignalData, 1);

    data->transcoder = g_object_ref (self);
    data->duration = duration;
    gst_transcoder_signal_dispatcher_dispatch (self->signal_dispatcher, self,
        duration_changed_dispatch, data,
        (GDestroyNotify) duration_changed_signal_data_free);
  }
}

static void
state_changed_cb (G_GNUC_UNUSED GstBus * bus, GstMessage * msg,
    gpointer user_data)
{
  GstTranscoder *self = GST_TRANSCODER (user_data);
  GstState old_state, new_state, pending_state;

  gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);

  if (GST_MESSAGE_SRC (msg) == GST_OBJECT (self->transcodebin)) {
    gchar *transition_name;

    GST_DEBUG_OBJECT (self, "Changed state old: %s new: %s pending: %s",
        gst_element_state_get_name (old_state),
        gst_element_state_get_name (new_state),
        gst_element_state_get_name (pending_state));

    transition_name = g_strdup_printf ("%s_%s",
        gst_element_state_get_name (old_state),
        gst_element_state_get_name (new_state));
    dump_dot_file (self, transition_name);
    g_free (transition_name);

    self->current_state = new_state;

    if (new_state == GST_STATE_PLAYING
        && pending_state == GST_STATE_VOID_PENDING) {
      add_tick_source (self);
    }
  }
}

static void
duration_changed_cb (G_GNUC_UNUSED GstBus * bus, G_GNUC_UNUSED GstMessage * msg,
    gpointer user_data)
{
  GstTranscoder *self = GST_TRANSCODER (user_data);
  gint64 duration;

  if (gst_element_query_duration (self->transcodebin, GST_FORMAT_TIME,
          &duration)) {
    emit_duration_changed (self, duration);
  }
}

static void
latency_cb (G_GNUC_UNUSED GstBus * bus, G_GNUC_UNUSED GstMessage * msg,
    gpointer user_data)
{
  GstTranscoder *self = GST_TRANSCODER (user_data);

  GST_DEBUG_OBJECT (self, "Latency changed");

  gst_bin_recalculate_latency (GST_BIN (self->transcodebin));
}

static void
request_state_cb (G_GNUC_UNUSED GstBus * bus, GstMessage * msg,
    gpointer user_data)
{
  GstTranscoder *self = GST_TRANSCODER (user_data);
  GstState state;
  GstStateChangeReturn state_ret;

  gst_message_parse_request_state (msg, &state);

  GST_DEBUG_OBJECT (self, "State %s requested",
      gst_element_state_get_name (state));

  self->target_state = state;
  state_ret = gst_element_set_state (self->transcodebin, state);
  if (state_ret == GST_STATE_CHANGE_FAILURE)
    emit_error (self, g_error_new (GST_TRANSCODER_ERROR,
            GST_TRANSCODER_ERROR_FAILED,
            "Failed to change to requested state %s",
            gst_element_state_get_name (state)), NULL);
}

static void
element_cb (G_GNUC_UNUSED GstBus * bus, GstMessage * msg, gpointer user_data)
{
  GstTranscoder *self = GST_TRANSCODER (user_data);
  const GstStructure *s;

  s = gst_message_get_structure (msg);
  if (gst_structure_has_name (s, "redirect")) {
    const gchar *new_location;

    new_location = gst_structure_get_string (s, "new-location");
    if (!new_location) {
      const GValue *locations_list, *location_val;
      guint i, size;

      locations_list = gst_structure_get_value (s, "locations");
      size = gst_value_list_get_size (locations_list);
      for (i = 0; i < size; ++i) {
        const GstStructure *location_s;

        location_val = gst_value_list_get_value (locations_list, i);
        if (!GST_VALUE_HOLDS_STRUCTURE (location_val))
          continue;

        location_s = (const GstStructure *) g_value_get_boxed (location_val);
        if (!gst_structure_has_name (location_s, "redirect"))
          continue;

        new_location = gst_structure_get_string (location_s, "new-location");
        if (new_location)
          break;
      }
    }

    if (new_location) {
      GST_FIXME_OBJECT (self, "Handle redirection to '%s'", new_location);
    }
  }
}


static gpointer
gst_transcoder_main (gpointer data)
{
  GstTranscoder *self = GST_TRANSCODER (data);
  GstBus *bus;
  GSource *source;
  GSource *bus_source;

  GST_TRACE_OBJECT (self, "Starting main thread");

  g_main_context_push_thread_default (self->context);

  source = g_idle_source_new ();
  g_source_set_callback (source, (GSourceFunc) main_loop_running_cb, self,
      NULL);
  g_source_attach (source, self->context);
  g_source_unref (source);

  self->bus = bus = gst_element_get_bus (self->transcodebin);
  bus_source = gst_bus_create_watch (bus);
  g_source_set_callback (bus_source, (GSourceFunc) gst_bus_async_signal_func,
      NULL, NULL);
  g_source_attach (bus_source, self->context);

  g_signal_connect (G_OBJECT (bus), "message::error", G_CALLBACK (error_cb),
      self);
  g_signal_connect (G_OBJECT (bus), "message::warning", G_CALLBACK (warning_cb),
      self);
  g_signal_connect (G_OBJECT (bus), "message::eos", G_CALLBACK (eos_cb), self);
  g_signal_connect (G_OBJECT (bus), "message::state-changed",
      G_CALLBACK (state_changed_cb), self);
  g_signal_connect (G_OBJECT (bus), "message::clock-lost",
      G_CALLBACK (clock_lost_cb), self);
  g_signal_connect (G_OBJECT (bus), "message::duration-changed",
      G_CALLBACK (duration_changed_cb), self);
  g_signal_connect (G_OBJECT (bus), "message::latency",
      G_CALLBACK (latency_cb), self);
  g_signal_connect (G_OBJECT (bus), "message::request-state",
      G_CALLBACK (request_state_cb), self);
  g_signal_connect (G_OBJECT (bus), "message::element",
      G_CALLBACK (element_cb), self);

  self->target_state = GST_STATE_NULL;
  self->current_state = GST_STATE_NULL;
  self->is_eos = FALSE;
  self->is_live = FALSE;

  GST_TRACE_OBJECT (self, "Starting main loop");
  g_main_loop_run (self->loop);
  GST_TRACE_OBJECT (self, "Stopped main loop");

  g_source_destroy (bus_source);
  g_source_unref (bus_source);
  gst_object_unref (bus);

  remove_tick_source (self);

  g_main_context_pop_thread_default (self->context);

  self->target_state = GST_STATE_NULL;
  self->current_state = GST_STATE_NULL;
  if (self->transcodebin) {
    gst_element_set_state (self->transcodebin, GST_STATE_NULL);
    g_clear_object (&self->transcodebin);
  }

  GST_TRACE_OBJECT (self, "Stopped main thread");

  return NULL;
}

static gpointer
gst_transcoder_init_once (G_GNUC_UNUSED gpointer user_data)
{
  gst_init (NULL, NULL);

  GST_DEBUG_CATEGORY_INIT (gst_transcoder_debug, "gst-transcoder", 0,
      "GstTranscoder");
  gst_transcoder_error_quark ();

  return NULL;
}

static GstEncodingProfile *
create_encoding_profile (const gchar * pname)
{
  GstEncodingProfile *profile;
  GValue value = G_VALUE_INIT;

  g_value_init (&value, GST_TYPE_ENCODING_PROFILE);

  if (!gst_value_deserialize (&value, pname)) {
    g_value_reset (&value);

    return NULL;
  }

  profile = g_value_dup_object (&value);
  g_value_reset (&value);

  return profile;
}

/**
 * gst_transcoder_new:
 * @source_uri: The URI of the media stream to transcode
 * @dest_uri: The URI of the destination of the transcoded stream
 * @encoding_profile: The serialized #GstEncodingProfile defining the output
 * format. Have a look at the #GstEncodingProfile documentation to find more
 * about the serialization format.
 *
 * Returns: a new #GstTranscoder instance
 */
GstTranscoder *
gst_transcoder_new (const gchar * source_uri,
    const gchar * dest_uri, const gchar * encoding_profile)
{
  GstEncodingProfile *profile;

  profile = create_encoding_profile (encoding_profile);

  return gst_transcoder_new_full (source_uri, dest_uri, profile, NULL);
}

/**
 * gst_transcoder_new_full:
 * @source_uri: The URI of the media stream to transcode
 * @dest_uri: The URI of the destination of the transcoded stream
 * @profile: The #GstEncodingProfile defining the output format
 * have a look at the #GstEncodingProfile documentation to find more
 * about the serialization format.
 * @signal_dispatcher: The #GstTranscoderSignalDispatcher to be used
 * to dispatch the various signals.
 *
 * Returns: a new #GstTranscoder instance
 */
GstTranscoder *
gst_transcoder_new_full (const gchar * source_uri,
    const gchar * dest_uri, GstEncodingProfile * profile,
    GstTranscoderSignalDispatcher * signal_dispatcher)
{
  static GOnce once = G_ONCE_INIT;

  g_once (&once, gst_transcoder_init_once, NULL);

  g_return_val_if_fail (source_uri, NULL);
  g_return_val_if_fail (dest_uri, NULL);

  return g_object_new (GST_TYPE_TRANSCODER, "src-uri", source_uri,
      "dest-uri", dest_uri, "profile", profile,
      "signal-dispatcher", signal_dispatcher, NULL);
}

typedef struct
{
  GError **user_error;
  GMutex m;
  GCond cond;

  gboolean done;

} RunSyncData;

static void
_error_cb (GstTranscoder * self, GError * error, GstStructure * details,
    RunSyncData * data)
{
  g_mutex_lock (&data->m);
  data->done = TRUE;
  if (data->user_error && (*data->user_error) == NULL)
    g_propagate_error (data->user_error, error);
  g_cond_broadcast (&data->cond);
  g_mutex_unlock (&data->m);
}

static void
_done_cb (GstTranscoder * self, RunSyncData * data)
{
  g_mutex_lock (&data->m);
  data->done = TRUE;
  g_cond_broadcast (&data->cond);
  g_mutex_unlock (&data->m);
}

/**
 * gst_transcoder_run:
 * @self: The GstTranscoder to run
 * @error: (allow-none): An error to be set if transcoding fails
 *
 * Run the transcoder task synchonously. You can connect
 * to the 'position' signal to get information about the
 * progress of the transcoding.
 */
gboolean
gst_transcoder_run (GstTranscoder * self, GError ** error)
{
  RunSyncData data = { 0, };

  g_mutex_init (&data.m);
  g_cond_init (&data.cond);

  g_signal_connect (self, "error", G_CALLBACK (_error_cb), &data);
  g_signal_connect (self, "done", G_CALLBACK (_done_cb), &data);
  gst_transcoder_run_async (self);

  g_mutex_lock (&data.m);
  while (!data.done) {
    g_cond_wait (&data.cond, &data.m);
  }
  g_mutex_unlock (&data.m);

  if (data.user_error) {
    g_propagate_error (error, *data.user_error);

    return FALSE;
  }

  return TRUE;
}

/**
 * gst_transcoder_run_async:
 * @self: The GstTranscoder to run
 *
 * Run the transcoder task asynchronously. You should connect
 * to the 'done' signal to be notified about when the
 * transcoding is done, and to the 'error' signal to be
 * notified about any error.
 */
void
gst_transcoder_run_async (GstTranscoder * self)
{
  GstStateChangeReturn state_ret;

  GST_DEBUG_OBJECT (self, "Play");

  if (!self->profile) {
    emit_error (self, g_error_new (GST_TRANSCODER_ERROR,
            GST_TRANSCODER_ERROR_FAILED, "No \"profile\" provided"), NULL);

    return;
  }

  self->target_state = GST_STATE_PLAYING;
  state_ret = gst_element_set_state (self->transcodebin, GST_STATE_PLAYING);

  if (state_ret == GST_STATE_CHANGE_FAILURE) {
    emit_error (self, g_error_new (GST_TRANSCODER_ERROR,
            GST_TRANSCODER_ERROR_FAILED, "Could not start transcoding"), NULL);
    return;
  } else if (state_ret == GST_STATE_CHANGE_NO_PREROLL) {
    self->is_live = TRUE;
    GST_DEBUG_OBJECT (self, "Pipeline is live");
  }

  return;
}

static gboolean
gst_transcoder_set_position_update_interval_internal (gpointer user_data)
{
  GstTranscoder *self = user_data;

  GST_OBJECT_LOCK (self);

  if (self->tick_source) {
    remove_tick_source (self);
    add_tick_source (self);
  }

  GST_OBJECT_UNLOCK (self);

  return G_SOURCE_REMOVE;
}

/**
 * gst_transcoder_set_position_update_interval:
 * @self: #GstTranscoder instance
 * @interval: interval in ms
 *
 * Set interval in milliseconds between two position-updated signals.
 * Pass 0 to stop updating the position.
 */
void
gst_transcoder_set_position_update_interval (GstTranscoder * self,
    guint interval)
{
  g_return_if_fail (GST_IS_TRANSCODER (self));
  g_return_if_fail (interval <= 10000);

  GST_OBJECT_LOCK (self);
  self->position_update_interval_ms = interval;
  GST_OBJECT_UNLOCK (self);

  gst_transcoder_set_position_update_interval_internal (self);
}

/**
 * gst_transcoder_get_position_update_interval:
 * @self: #GstTranscoder instance
 *
 * Returns: current position update interval in milliseconds
 */
guint
gst_transcoder_get_position_update_interval (GstTranscoder * self)
{
  g_return_val_if_fail (GST_IS_TRANSCODER (self),
      DEFAULT_POSITION_UPDATE_INTERVAL_MS);

  return self->position_update_interval_ms;
}

/**
 * gst_transcoder_get_source_uri:
 * @self: #GstTranscoder instance
 *
 * Gets the URI of the currently-transcoding stream.
 *
 * Returns: (transfer full): a string containing the URI of the
 * source stream. g_free() after usage.
 */
gchar *
gst_transcoder_get_source_uri (GstTranscoder * self)
{
  gchar *val;

  g_return_val_if_fail (GST_IS_TRANSCODER (self), DEFAULT_URI);

  g_object_get (self, "src-uri", &val, NULL);

  return val;
}

/**
 * gst_transcoder_get_dest_uri:
 * @self: #GstTranscoder instance
 *
 * Gets the URI of the destination of the transcoded stream.
 *
 * Returns: (transfer full): a string containing the URI of the
 * destination of the transcoded stream. g_free() after usage.
 */
gchar *
gst_transcoder_get_dest_uri (GstTranscoder * self)
{
  gchar *val;

  g_return_val_if_fail (GST_IS_TRANSCODER (self), DEFAULT_URI);

  g_object_get (self, "dest-uri", &val, NULL);

  return val;
}

/**
 * gst_transcoder_get_position:
 * @self: #GstTranscoder instance
 *
 * Returns: the absolute position time, in nanoseconds, of the
 * transcoding stream.
 */
GstClockTime
gst_transcoder_get_position (GstTranscoder * self)
{
  GstClockTime val;

  g_return_val_if_fail (GST_IS_TRANSCODER (self), DEFAULT_POSITION);

  g_object_get (self, "position", &val, NULL);

  return val;
}

/**
 * gst_transcoder_get_duration:
 * @self: #GstTranscoder instance
 *
 * Retrieves the duration of the media stream that self represents.
 *
 * Returns: the duration of the transcoding media stream, in
 * nanoseconds.
 */
GstClockTime
gst_transcoder_get_duration (GstTranscoder * self)
{
  GstClockTime val;

  g_return_val_if_fail (GST_IS_TRANSCODER (self), DEFAULT_DURATION);

  g_object_get (self, "duration", &val, NULL);

  return val;
}

/**
 * gst_transcoder_get_pipeline:
 * @self: #GstTranscoder instance
 *
 * Returns: (transfer full): The internal uritranscodebin instance
 */
GstElement *
gst_transcoder_get_pipeline (GstTranscoder * self)
{
  GstElement *val;

  g_return_val_if_fail (GST_IS_TRANSCODER (self), NULL);

  g_object_get (self, "pipeline", &val, NULL);

  return val;
}

/**
 * gst_transcoder_get_avoid_reencoding:
 * @self: The #GstTranscoder to check whether reencoding is avoided or not.
 *
 * Returns: %TRUE if the transcoder tries to avoid reencoding streams where
 * reencoding is not strictly needed, %FALSE otherwise.
 */
gboolean
gst_transcoder_get_avoid_reencoding (GstTranscoder * self)
{
  gboolean val;

  g_return_val_if_fail (GST_IS_TRANSCODER (self), FALSE);

  g_object_get (self->transcodebin, "avoid-reencoding", &val, NULL);

  return val;
}

/**
 * gst_transcoder_set_avoid_reencoding:
 * @self: The #GstTranscoder to set whether reencoding should be avoided or not.
 * @avoid_reencoding: %TRUE if the transcoder should try to avoid reencoding
 * streams where * reencoding is not strictly needed, %FALSE otherwise.
 */
void
gst_transcoder_set_avoid_reencoding (GstTranscoder * self,
    gboolean avoid_reencoding)
{
  g_return_if_fail (GST_IS_TRANSCODER (self));

  g_object_set (self->transcodebin, "avoid-reencoding", avoid_reencoding, NULL);
}

#define C_ENUM(v) ((gint) v)
#define C_FLAGS(v) ((guint) v)

GType
gst_transcoder_error_get_type (void)
{
  static gsize id = 0;
  static const GEnumValue values[] = {
    {C_ENUM (GST_TRANSCODER_ERROR_FAILED), "GST_TRANSCODER_ERROR_FAILED",
        "failed"},
    {0, NULL, NULL}
  };

  if (g_once_init_enter (&id)) {
    GType tmp = g_enum_register_static ("GstTranscoderError", values);
    g_once_init_leave (&id, tmp);
  }

  return (GType) id;
}

/**
 * gst_transcoder_error_get_name:
 * @error: a #GstTranscoderError
 *
 * Gets a string representing the given error.
 *
 * Returns: (transfer none): a string with the given error.
 */
const gchar *
gst_transcoder_error_get_name (GstTranscoderError error)
{
  switch (error) {
    case GST_TRANSCODER_ERROR_FAILED:
      return "failed";
  }

  g_assert_not_reached ();
  return NULL;
}

G_DEFINE_INTERFACE (GstTranscoderSignalDispatcher,
    gst_transcoder_signal_dispatcher, G_TYPE_OBJECT);

static void
gst_transcoder_signal_dispatcher_default_init (G_GNUC_UNUSED
    GstTranscoderSignalDispatcherInterface * iface)
{

}

static void
gst_transcoder_signal_dispatcher_dispatch (GstTranscoderSignalDispatcher * self,
    GstTranscoder * transcoder, void (*emitter) (gpointer data), gpointer data,
    GDestroyNotify destroy)
{
  GstTranscoderSignalDispatcherInterface *iface;

  if (!self) {
    emitter (data);
    if (destroy)
      destroy (data);
    return;
  }

  g_return_if_fail (GST_IS_TRANSCODER_SIGNAL_DISPATCHER (self));
  iface = GST_TRANSCODER_SIGNAL_DISPATCHER_GET_INTERFACE (self);
  g_return_if_fail (iface->dispatch != NULL);

  iface->dispatch (self, transcoder, emitter, data, destroy);
}

struct _GstTranscoderGMainContextSignalDispatcher
{
  GObject parent;
  GMainContext *application_context;
};

struct _GstTranscoderGMainContextSignalDispatcherClass
{
  GObjectClass parent_class;
};

static void
    gst_transcoder_g_main_context_signal_dispatcher_interface_init
    (GstTranscoderSignalDispatcherInterface * iface);

enum
{
  G_MAIN_CONTEXT_SIGNAL_DISPATCHER_PROP_0,
  G_MAIN_CONTEXT_SIGNAL_DISPATCHER_PROP_APPLICATION_CONTEXT,
  G_MAIN_CONTEXT_SIGNAL_DISPATCHER_PROP_LAST
};

G_DEFINE_TYPE_WITH_CODE (GstTranscoderGMainContextSignalDispatcher,
    gst_transcoder_g_main_context_signal_dispatcher, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (GST_TYPE_TRANSCODER_SIGNAL_DISPATCHER,
        gst_transcoder_g_main_context_signal_dispatcher_interface_init));

static GParamSpec
    * g_main_context_signal_dispatcher_param_specs
    [G_MAIN_CONTEXT_SIGNAL_DISPATCHER_PROP_LAST] = { NULL, };

static void
gst_transcoder_g_main_context_signal_dispatcher_finalize (GObject * object)
{
  GstTranscoderGMainContextSignalDispatcher *self =
      GST_TRANSCODER_G_MAIN_CONTEXT_SIGNAL_DISPATCHER (object);

  if (self->application_context)
    g_main_context_unref (self->application_context);

  G_OBJECT_CLASS
      (gst_transcoder_g_main_context_signal_dispatcher_parent_class)->finalize
      (object);
}

static void
gst_transcoder_g_main_context_signal_dispatcher_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstTranscoderGMainContextSignalDispatcher *self =
      GST_TRANSCODER_G_MAIN_CONTEXT_SIGNAL_DISPATCHER (object);

  switch (prop_id) {
    case G_MAIN_CONTEXT_SIGNAL_DISPATCHER_PROP_APPLICATION_CONTEXT:
      self->application_context = g_value_dup_boxed (value);
      if (!self->application_context)
        self->application_context = g_main_context_ref_thread_default ();
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_transcoder_g_main_context_signal_dispatcher_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstTranscoderGMainContextSignalDispatcher *self =
      GST_TRANSCODER_G_MAIN_CONTEXT_SIGNAL_DISPATCHER (object);

  switch (prop_id) {
    case G_MAIN_CONTEXT_SIGNAL_DISPATCHER_PROP_APPLICATION_CONTEXT:
      g_value_set_boxed (value, self->application_context);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
    gst_transcoder_g_main_context_signal_dispatcher_class_init
    (GstTranscoderGMainContextSignalDispatcherClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize =
      gst_transcoder_g_main_context_signal_dispatcher_finalize;
  gobject_class->set_property =
      gst_transcoder_g_main_context_signal_dispatcher_set_property;
  gobject_class->get_property =
      gst_transcoder_g_main_context_signal_dispatcher_get_property;

  g_main_context_signal_dispatcher_param_specs
      [G_MAIN_CONTEXT_SIGNAL_DISPATCHER_PROP_APPLICATION_CONTEXT] =
      g_param_spec_boxed ("application-context", "Application Context",
      "Application GMainContext to dispatch signals to", G_TYPE_MAIN_CONTEXT,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class,
      G_MAIN_CONTEXT_SIGNAL_DISPATCHER_PROP_LAST,
      g_main_context_signal_dispatcher_param_specs);
}

static void
    gst_transcoder_g_main_context_signal_dispatcher_init
    (G_GNUC_UNUSED GstTranscoderGMainContextSignalDispatcher * self)
{
}

typedef struct
{
  void (*emitter) (gpointer data);
  gpointer data;
  GDestroyNotify destroy;
} GMainContextSignalDispatcherData;

static gboolean
g_main_context_signal_dispatcher_dispatch_gsourcefunc (gpointer user_data)
{
  GMainContextSignalDispatcherData *data = user_data;

  data->emitter (data->data);

  return G_SOURCE_REMOVE;
}

static void
g_main_context_signal_dispatcher_dispatch_destroy (gpointer user_data)
{
  GMainContextSignalDispatcherData *data = user_data;

  if (data->destroy)
    data->destroy (data->data);
  g_free (data);
}

/* *INDENT-OFF* */
static void
gst_transcoder_g_main_context_signal_dispatcher_dispatch (GstTranscoderSignalDispatcher * iface,
    G_GNUC_UNUSED GstTranscoder * transcoder, void (*emitter) (gpointer data),
    gpointer data, GDestroyNotify destroy)
{
  GstTranscoderGMainContextSignalDispatcher *self =
      GST_TRANSCODER_G_MAIN_CONTEXT_SIGNAL_DISPATCHER (iface);
  GMainContextSignalDispatcherData *gsourcefunc_data =
      g_new0 (GMainContextSignalDispatcherData, 1);

  gsourcefunc_data->emitter = emitter;
  gsourcefunc_data->data = data;
  gsourcefunc_data->destroy = destroy;

  g_main_context_invoke_full (self->application_context,
      G_PRIORITY_DEFAULT, g_main_context_signal_dispatcher_dispatch_gsourcefunc,
      gsourcefunc_data, g_main_context_signal_dispatcher_dispatch_destroy);
}

static void
gst_transcoder_g_main_context_signal_dispatcher_interface_init (GstTranscoderSignalDispatcherInterface * iface)
{
  iface->dispatch = gst_transcoder_g_main_context_signal_dispatcher_dispatch;
}
/* *INDENT-ON* */

/**
 * gst_transcoder_g_main_context_signal_dispatcher_new:
 * @application_context: (allow-none): GMainContext to use or %NULL
 *
 * Returns: (transfer full):
 */
GstTranscoderSignalDispatcher *
gst_transcoder_g_main_context_signal_dispatcher_new (GMainContext *
    application_context)
{
  return g_object_new (GST_TYPE_TRANSCODER_G_MAIN_CONTEXT_SIGNAL_DISPATCHER,
      "application-context", application_context, NULL);
}
