/*
 * Copyright (C) 2026 Fraunhofer Institute for Integrated Circuits IIS
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
 * SECTION:element-mpeghuimanager
 * @title: mpeghuimanager
 * @see_also: mpeghdec
 *
 * The mpeghuimanager element handles user interactivity for MPEG-H Audio,
 * using the UI manager component of the Fraunhofer MPEG-H decoder library
 * (mpeghdec). It takes framed MHAS frames and outputs them with the user's
 * personalization settings (preset, dialogue level, object position, ...)
 * embedded as MPEG-H UI packets. It is placed before an MPEG-H decoder such
 * as mpeghdec, which renders the audio scene accordingly.
 *
 * ## XML interface
 *
 * The element exchanges two XML message types with the application. Both
 * are defined by the MPEG-H UI manager, not by GStreamer, and the element
 * passes them on unchanged:
 *
 * * `AudioSceneConfig` (from the element): the AudioSceneConfig XML message,
 *   containing the audio scene description and its interactivity options,
 *   such as preset selection, audio object selection and adjustments, etc.
 *   The application's MPEG-H UI renderer uses this message to present these
 *   options to the user.
 * * `ActionEvent` (to the element): a user or system setting, such as
 *   selecting a preset or changing the prominence level of an audio element.
 *
 * Both message types and the XML interface format are specified in
 * [MPEG-H UI manager XML format](https://github.com/Fraunhofer-IIS/mpeghdec/wiki/MPEG-H-UI-manager-XML-format).
 *
 * ## Audio scene configuration
 *
 * When the MPEG-H UI manager provides a new `AudioSceneConfig` XML message,
 * the element posts an element message named `mpegh-ui-config` with these
 * fields:
 *
 * * `config` (string): the new `AudioSceneConfig` XML message.
 * * `timestamp`, `stream-time`, `running-time` and `duration` (guint64):
 *   the times of the frame it belongs to, #GST_CLOCK_TIME_NONE when unknown.
 *
 * The message is posted when the frame is processed, usually ahead of
 * playback. To update the UI in sync with playback, compare the running time
 * with the pipeline clock. The latest posted `AudioSceneConfig` XML message
 * can also be read from the #GstMpeghUiManager:config property.
 *
 * ## User interaction commands (ActionEvent)
 *
 * User actions are sent to the element as custom events named
 * `mpegh-ui-event`. The string field `xml` holds an `ActionEvent` XML
 * message, whose `uuid` is the one of the current `AudioSceneConfig`. The
 * event can be sent in two ways:
 *
 * * As a #GST_EVENT_CUSTOM_UPSTREAM event. It is not serialized with the data
 *   flow and is applied to the next frame that is processed. It can be sent to
 *   the whole pipeline with gst_element_send_event(), which passes it to the
 *   sinks, from where it travels upstream to this element. The application
 *   doesn't need a reference to the element, which also works when the element
 *   is inside a bin such as playbin.
 * * As a #GST_EVENT_CUSTOM_DOWNSTREAM event sent into the element's sink pad,
 *   for example from a pad probe. It is serialized with the data flow and is
 *   applied to exactly the frame that follows it.
 *
 * ## Persistence
 *
 * The UI manager remembers user settings, such as the chosen preset, per
 * stream in a persistence memory, which the element creates and manages. To
 * keep the settings across sessions, the application stores its content and
 * passes it back the next time:
 *
 * 1. Optionally, in the NULL or READY state, set
 *    #GstMpeghUiManager:input-persistence to the data stored in a previous
 *    session. Without it, the element starts without stored settings.
 * 2. Read #GstMpeghUiManager:output-persistence and store the data. It always
 *    holds the latest state and is notified when it changes: after UI events
 *    have been applied, on EOS and when the element stops. The application
 *    can store the data on each notification or once after the element has
 *    stopped.
 *
 * The data is opaque to the application. When the element is started again
 * without setting #GstMpeghUiManager:input-persistence in between, it
 * continues with the settings of the previous run.
 *
 * On a gst-launch-1.0 command line #GstMpeghUiManager:input-persistence can
 * be set as a base64 string. With `-v`, gst-launch-1.0 prints each update of
 * #GstMpeghUiManager:output-persistence in base64, which can be passed back as
 * #GstMpeghUiManager:input-persistence in the next run. This requires the
 * stream to end with EOS or UI events to have been applied, as gst-launch-1.0
 * stops printing updates before it stops the pipeline, e.g. on Ctrl-C without
 * `-e`.
 *
 * ## Inactive UI manager and errors
 *
 * Since the element is not required for decoding, it never stops the
 * pipeline due to stream errors:
 *
 * * While the UI manager is inactive, frames are passed through unchanged.
 *   It is inactive until the first configuration of the stream has been
 *   received, and while the stream already contains MPEG-H UI packets, i.e.
 *   another UI manager upstream takes precedence. This is not an error and
 *   posts no message.
 * * A frame the UI manager can't process is passed through unchanged as
 *   well. The first frame of each run of such frames posts a warning
 *   message.
 *
 * ## Example launch line
 * |[
 * gst-launch-1.0 filesrc location=test.mp4 ! qtdemux ! mpeghuimanager ! mpeghdec ! audioconvert ! audioresample ! autoaudiosink
 * ]| This pipeline plays an MPEG-H Audio MP4 file through the UI manager.
 * Without UI events, the scene plays with its default settings.
 *
 * Since: 1.30
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "gstmpeghuimanager.h"

/* Maximum size in bytes of an input MPEG-H frame the UI manager processes.
 * Larger input frames are passed through unchanged. The output frame can be
 * up to UPDATE_MHAS_HEADROOM bytes larger. */
#define MAX_MPEGH_FRAME_SIZE 65536
/* Room mpegh_UI_UpdateMHAS() needs for inserting packets: according to
 * mpeghUIManager.h its buffer should be at least the size of the input MHAS
 * data plus 256 bytes */
#define UPDATE_MHAS_HEADROOM 256
#define MAX_CONFIG_LENGTH 104226

/* Custom event carrying an MPEG-H Action Event XML message in its string field
 * "xml". It is accepted as a non-serialized upstream event
 * (GST_EVENT_CUSTOM_UPSTREAM) and as a serialized downstream event
 * (GST_EVENT_CUSTOM_DOWNSTREAM), which applies to the next frame. */
#define MPEGH_UI_EVENT_NAME "mpegh-ui-event"

/* Maximum number of UI events waiting to be applied, so they can't pile up
 * while no valid scene is available */
#define MAX_PENDING_UI_EVENTS 64

/* Size in bytes of the persistence memory created without stored data. The
 * UI manager divides it into entries for the settings of one stream each
 * (44 bytes on average) and needs at least 10 of them; this size holds about
 * 90. Stored data keeps the size it was created with, as the layout of the
 * memory depends on it. */
#define DEFAULT_PERSISTENCE_MEMORY_SIZE 4096

GST_DEBUG_CATEGORY_STATIC (gst_mpeghuimanager_debug);
#define GST_CAT_DEFAULT gst_mpeghuimanager_debug

enum
{
  PROP_0,
  PROP_CONFIG,
  PROP_INPUT_PERSISTENCE,
  PROP_OUTPUT_PERSISTENCE,
};

#define gst_mpeghuimanager_parent_class parent_class
G_DEFINE_TYPE (GstMpeghUiManager, gst_mpeghuimanager, GST_TYPE_BASE_TRANSFORM);
GST_ELEMENT_REGISTER_DEFINE_WITH_CODE (mpeghuimanager, "mpeghuimanager",
    GST_RANK_NONE, GST_TYPE_MPEGHUIMANAGER,
    GST_DEBUG_CATEGORY_INIT (gst_mpeghuimanager_debug, "mpeghuimanager", 0,
        "MPEG-H UI Manager"));

#define MPEGHUIMANAGER_CAPS \
  "audio/x-mpeg-h, " \
  "profile = (string) baseline, " \
  "stream-format = (string) mhas, " \
  "framed = (boolean) true, " \
  "level = (int) { 1, 2, 3, 4 }, " \
  "rate = (int) 48000, " \
  "stream-type = (string) single"

static GstStaticPadTemplate sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS (MPEGHUIMANAGER_CAPS));

static GstStaticPadTemplate src_factory =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS (MPEGHUIMANAGER_CAPS));

static void gst_mpeghuimanager_finalize (GObject * object);
static void gst_mpeghuimanager_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_mpeghuimanager_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_mpeghuimanager_start (GstBaseTransform * trans);
static gboolean gst_mpeghuimanager_stop (GstBaseTransform * trans);
static gboolean gst_mpeghuimanager_sink_event (GstBaseTransform * trans,
    GstEvent * event);
static gboolean gst_mpeghuimanager_src_event (GstBaseTransform * trans,
    GstEvent * event);
static GstFlowReturn gst_mpeghuimanager_generate_output (GstBaseTransform *
    trans, GstBuffer ** outbuf);
static void gst_mpeghuimanager_publish_persistence (GstMpeghUiManager *
    filter);

static void
gst_mpeghuimanager_class_init (GstMpeghUiManagerClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  GstBaseTransformClass *trans_class = (GstBaseTransformClass *) klass;

  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_mpeghuimanager_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_mpeghuimanager_get_property);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_mpeghuimanager_finalize);

  gst_element_class_set_static_metadata (gstelement_class,
      "MPEG-H UI Manager", "Filter/Audio",
      "Handles MPEG-H Audio user interactivity",
      "Fraunhofer IIS <mpeg-h-techsupport@iis.fraunhofer.de>");

  gst_element_class_add_static_pad_template (gstelement_class, &src_factory);
  gst_element_class_add_static_pad_template (gstelement_class, &sink_factory);

  /**
   * GstMpeghUiManager:config:
   *
   * The latest MPEG-H Audio Scene Information XML reported by the UI manager,
   * the same as in the last `mpegh-ui-config` message. It is %NULL until the
   * first scene of the stream has been reported, and is cleared when the
   * element stops. Changes are not signalled with notify, use the
   * `mpegh-ui-config` message to follow them.
   *
   * The XML format is described in
   * [MPEG-H UI manager XML format](https://github.com/Fraunhofer-IIS/mpeghdec/wiki/MPEG-H-UI-manager-XML-format).
   *
   * Since: 1.30
   */
  g_object_class_install_property (gobject_class, PROP_CONFIG,
      g_param_spec_string ("config", "Config",
          "Latest MPEG-H Audio Scene Information XML "
          "(NULL until the first scene has been received)", NULL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /**
   * GstMpeghUiManager:input-persistence:
   *
   * Persistence data stored by the application in a previous session, as
   * read from #GstMpeghUiManager:output-persistence. Its content is opaque
   * to the application. Set it in the NULL or READY state; it is used from
   * the next start of the element. Data the UI manager can't use, e.g.
   * because it is too small, is ignored with a warning, and the element
   * starts without stored settings.
   *
   * As a string, for example on a gst-launch-1.0 command line, the data is
   * given in base64.
   *
   * %NULL, the default, starts without stored settings. When the element is
   * started again without setting this property in between, it continues
   * with the settings of the previous run.
   *
   * Since: 1.30
   */
  g_object_class_install_property (gobject_class, PROP_INPUT_PERSISTENCE,
      g_param_spec_boxed ("input-persistence", "Input persistence",
          "Persistence data stored in a previous session, as read from "
          "output-persistence (NULL to start without stored settings). "
          "Base64 as a string", G_TYPE_BYTES,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  /**
   * GstMpeghUiManager:output-persistence:
   *
   * The current persistence data of the UI manager, in which it keeps user
   * settings across sessions. Its content is opaque to the application.
   * Store it to pass it to #GstMpeghUiManager:input-persistence in a later
   * session.
   *
   * It always holds a complete and consistent state and can be read at any
   * time. It is updated and notified when it changes: when the element
   * starts, with the data it starts from, after UI events have been applied,
   * on EOS and when the element stops. The notifications while running are
   * emitted from the streaming thread. It is %NULL before the element has
   * been started.
   *
   * Since: 1.30
   */
  g_object_class_install_property (gobject_class, PROP_OUTPUT_PERSISTENCE,
      g_param_spec_boxed ("output-persistence", "Output persistence",
          "Current persistence data, notified when it changes, to be stored "
          "by the application and passed to input-persistence in a later "
          "session. Base64 as a string",
          G_TYPE_BYTES, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  trans_class->start = GST_DEBUG_FUNCPTR (gst_mpeghuimanager_start);
  trans_class->stop = GST_DEBUG_FUNCPTR (gst_mpeghuimanager_stop);
  trans_class->sink_event = GST_DEBUG_FUNCPTR (gst_mpeghuimanager_sink_event);
  trans_class->src_event = GST_DEBUG_FUNCPTR (gst_mpeghuimanager_src_event);
  trans_class->generate_output =
      GST_DEBUG_FUNCPTR (gst_mpeghuimanager_generate_output);
}

static void
gst_mpeghuimanager_init (GstMpeghUiManager * filter)
{
  /* Without a transform() vfunc GstBaseTransform defaults to passthrough */
  gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (filter), FALSE);

  filter->mpegh_ui_manager = NULL;
  filter->input_persistence = NULL;
  filter->output_persistence = NULL;
  filter->persistence_memory = NULL;
  filter->persistence_memory_size = 0;
  filter->consecutive_errors = 0;
  filter->scene_valid = FALSE;
  filter->inactive = FALSE;
  filter->scene_state_buffer = NULL;
  g_queue_init (&filter->ui_event_queue);
  filter->latest_config = NULL;
}

static void
gst_mpeghuimanager_finalize (GObject * object)
{
  GstMpeghUiManager *filter = GST_MPEGHUIMANAGER (object);

  g_queue_clear_full (&filter->ui_event_queue, g_free);
  g_free (filter->latest_config);
  g_clear_pointer (&filter->input_persistence, g_bytes_unref);
  g_clear_pointer (&filter->output_persistence, g_bytes_unref);
  g_free (filter->persistence_memory);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_mpeghuimanager_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMpeghUiManager *filter = GST_MPEGHUIMANAGER (object);
  GBytes *persistence;
  gsize size;

  switch (prop_id) {
    case PROP_INPUT_PERSISTENCE:
      persistence = g_value_get_boxed (value);
      size = persistence ? g_bytes_get_size (persistence) : 0;
      /* The UI manager stores the size as unsigned short */
      if (size > G_MAXUINT16) {
        GST_WARNING_OBJECT (filter, "Ignoring persistence data: size %"
            G_GSIZE_FORMAT " exceeds maximum of %u", size, G_MAXUINT16);
        break;
      }
      GST_OBJECT_LOCK (filter);
      /* The UI manager uses the memory in place from start() to stop() */
      if (filter->mpegh_ui_manager != NULL) {
        GST_OBJECT_UNLOCK (filter);
        GST_WARNING_OBJECT (filter, "Changing the persistence data is only "
            "supported in NULL or READY state");
        break;
      }
      g_clear_pointer (&filter->input_persistence, g_bytes_unref);
      if (size > 0)
        filter->input_persistence = g_bytes_ref (persistence);
      /* Replaces the memory of the previous run, start() creates it anew */
      g_clear_pointer (&filter->persistence_memory, g_free);
      filter->persistence_memory_size = 0;
      GST_INFO_OBJECT (filter, "setting persistence data of %" G_GSIZE_FORMAT
          " bytes", size);
      GST_OBJECT_UNLOCK (filter);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_mpeghuimanager_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMpeghUiManager *filter = GST_MPEGHUIMANAGER (object);

  switch (prop_id) {
    case PROP_INPUT_PERSISTENCE:
      GST_OBJECT_LOCK (filter);
      g_value_set_boxed (value, filter->input_persistence);
      GST_OBJECT_UNLOCK (filter);
      break;

    case PROP_OUTPUT_PERSISTENCE:
      /* A copy, as the UI manager updates persistence_memory in place from
       * the streaming thread */
      GST_OBJECT_LOCK (filter);
      g_value_set_boxed (value, filter->output_persistence);
      GST_OBJECT_UNLOCK (filter);
      break;

    case PROP_CONFIG:
      GST_OBJECT_LOCK (filter);
      g_value_set_string (value, filter->latest_config);
      GST_OBJECT_UNLOCK (filter);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* Queues the XML message of an "mpegh-ui-event" event, to be applied before
 * the next frame is updated. Takes ownership of the event. */
static gboolean
gst_mpeghuimanager_queue_ui_event (GstMpeghUiManager * filter, GstEvent * event)
{
  const gchar *xml;

  xml = gst_structure_get_string (gst_event_get_structure (event), "xml");
  if (xml == NULL || xml[0] == '\0') {
    GST_WARNING_OBJECT (filter, "Ignoring %" GST_PTR_FORMAT
        " without \"xml\" string field", event);
    gst_event_unref (event);
    return FALSE;
  }

  GST_OBJECT_LOCK (filter);
  if (g_queue_get_length (&filter->ui_event_queue) >= MAX_PENDING_UI_EVENTS) {
    GST_OBJECT_UNLOCK (filter);
    GST_WARNING_OBJECT (filter, "Refusing UI event %s: %d events are already "
        "pending", xml, MAX_PENDING_UI_EVENTS);
    gst_event_unref (event);
    return FALSE;
  }
  GST_INFO_OBJECT (filter, "adding UI event %s", xml);
  g_queue_push_tail (&filter->ui_event_queue, g_strdup (xml));
  GST_OBJECT_UNLOCK (filter);

  gst_event_unref (event);
  return TRUE;
}

static gboolean
gst_mpeghuimanager_sink_event (GstBaseTransform * trans, GstEvent * event)
{
  GstMpeghUiManager *filter = GST_MPEGHUIMANAGER (trans);

  /* Serialized, so it is queued right before the frame it applies to */
  if (GST_EVENT_TYPE (event) == GST_EVENT_CUSTOM_DOWNSTREAM
      && gst_event_has_name (event, MPEGH_UI_EVENT_NAME))
    return gst_mpeghuimanager_queue_ui_event (filter, event);

  /* Publish the final state before EOS is forwarded, so it is notified
   * before the EOS message is posted, e.g. for gst-launch-1.0 -v, which stops
   * watching notifications before shutting the pipeline down */
  if (GST_EVENT_TYPE (event) == GST_EVENT_EOS)
    gst_mpeghuimanager_publish_persistence (filter);

  return GST_BASE_TRANSFORM_CLASS (parent_class)->sink_event (trans, event);
}

static gboolean
gst_mpeghuimanager_src_event (GstBaseTransform * trans, GstEvent * event)
{
  GstMpeghUiManager *filter = GST_MPEGHUIMANAGER (trans);

  if (GST_EVENT_TYPE (event) == GST_EVENT_CUSTOM_UPSTREAM
      && gst_event_has_name (event, MPEGH_UI_EVENT_NAME))
    return gst_mpeghuimanager_queue_ui_event (filter, event);

  return GST_BASE_TRANSFORM_CLASS (parent_class)->src_event (trans, event);
}

/* Reports a frame that could not be processed and is passed through
 * unchanged. The UI manager is optional for playback, so this is not fatal:
 * only the first frame of a run of failures posts a warning message. */
static void
gst_mpeghuimanager_processing_failed (GstMpeghUiManager * filter,
    const gchar * format, ...)
{
  va_list args;
  gchar *debug;

  va_start (args, format);
  debug = g_strdup_vprintf (format, args);
  va_end (args);

  if (filter->consecutive_errors++ == 0) {
    GST_ELEMENT_WARNING (filter, STREAM, DECODE,
        ("Failed to process MPEG-H stream, passing it through unchanged"),
        ("%s", debug));
  } else {
    GST_DEBUG_OBJECT (filter, "%s (%u frames in a row)", debug,
        filter->consecutive_errors);
  }
  g_free (debug);
}

/* Times of the frame being processed, for the element messages. They are only
 * worked out when a message is posted, which most frames don't. */
typedef struct
{
  gboolean valid;
  GstClockTime timestamp;
  GstClockTime duration;
  GstClockTime stream_time;
  GstClockTime running_time;
} GstMpeghUiManagerFrameTimes;

/* Creates an element message from structure, adding the timestamp,
 * stream-time, running-time and duration of the frame (as in "level"), so the
 * application can relate it to playback. The times are GST_CLOCK_TIME_NONE
 * when unknown. Takes ownership of structure. */
static GstMessage *
gst_mpeghuimanager_new_message (GstMpeghUiManager * filter, GstBuffer * inbuf,
    GstMpeghUiManagerFrameTimes * times, GstStructure * structure)
{
  if (!times->valid) {
    GstSegment *segment = &GST_BASE_TRANSFORM (filter)->segment;

    times->timestamp = GST_BUFFER_PTS (inbuf);
    times->duration = GST_BUFFER_DURATION (inbuf);
    times->stream_time = GST_CLOCK_TIME_NONE;
    times->running_time = GST_CLOCK_TIME_NONE;
    if (segment->format == GST_FORMAT_TIME) {
      times->stream_time = gst_segment_to_stream_time (segment,
          GST_FORMAT_TIME, times->timestamp);
      times->running_time = gst_segment_to_running_time (segment,
          GST_FORMAT_TIME, times->timestamp);
    }
    times->valid = TRUE;
  }

  gst_structure_set (structure,
      "timestamp", G_TYPE_UINT64, times->timestamp,
      "stream-time", G_TYPE_UINT64, times->stream_time,
      "running-time", G_TYPE_UINT64, times->running_time,
      "duration", G_TYPE_UINT64, times->duration, NULL);

  return gst_message_new_element (GST_OBJECT (filter), structure);
}

/* Whether the AudioSceneConfig XML describes a valid audio scene. When none
 * is available, the UI manager reports a short AudioSceneConfig without
 * content: with the all-zero uuid if there is no UI (e.g. no configuration
 * received yet), or, in XML interface version 11.0, with configChanged="true"
 * on a configuration change. Only a full one lists the presets. */
static gboolean
gst_mpeghuimanager_scene_is_valid (const gchar * xml)
{
  return strstr (xml, "<presets>") != NULL;
}

/* Runs the MHAS frame of inbuf through the UI manager. data holds a copy of
 * it of size bytes, in a writable buffer of capacity bytes, which the UI
 * manager updates in place. On success the size of the updated frame is
 * returned in outsize. Returns FALSE if the frame could not be processed. */
static gboolean
gst_mpeghuimanager_process (GstMpeghUiManager * filter, GstBuffer * inbuf,
    guint8 * data, guint size, guint capacity, guint * outsize)
{
  GstMpeghUiManagerFrameTimes times = { FALSE, };
  MPEGH_UI_ERROR ret;
  GQueue ui_events;
  gchar *event;
  gboolean events_applied = FALSE;

  GST_DEBUG_OBJECT (filter, "input %" GST_PTR_FORMAT, inbuf);

  /* Feed data into the MPEG-H UI manager */
  ret = mpegh_UI_FeedMHAS (filter->mpegh_ui_manager, data, size);
  if (ret != MPEGH_UI_OK) {
    /* Pending UI events stay queued for the next frame */
    gst_mpeghuimanager_processing_failed (filter,
        "mpegh_UI_FeedMHAS failed with: %d", ret);
    return FALSE;
  }

  /* Apply all pending UI events */
  GST_OBJECT_LOCK (filter);
  ui_events = filter->ui_event_queue;
  g_queue_init (&filter->ui_event_queue);
  GST_OBJECT_UNLOCK (filter);

  while ((event = g_queue_pop_head (&ui_events))) {
    /* Not used by the UI manager yet */
    guint flags_out = 0;
    gsize len = strlen (event);

    GST_DEBUG_OBJECT (filter, "process UI event: %s len:%" G_GSIZE_FORMAT,
        event, len);
    ret = mpegh_UI_ApplyXmlAction (filter->mpegh_ui_manager, event, len,
        &flags_out);

    /* While no valid scene is available, the UI manager may not know the
     * groups and presets an event refers to. Keep the event and the ones
     * after it, in order, and retry them on the next frame. */
    if (ret != MPEGH_UI_OK && !filter->scene_valid) {
      GST_DEBUG_OBJECT (filter, "mpegh_UI_ApplyXmlAction failed with: %d "
          "without a valid scene, retrying on the next frame", ret);
      g_queue_push_head (&ui_events, event);
      break;
    }

    if (ret != MPEGH_UI_OK) {
      GST_WARNING_OBJECT (filter, "mpegh_UI_ApplyXmlAction failed with: %d",
          ret);
    } else {
      events_applied = TRUE;
    }

    /* Element message "mpegh-ui-event-result" telling the application
     * whether the UI event in the string field "xml" was applied ("success")
     * and otherwise the UI manager's error code ("error"), with the times of
     * the frame it was applied to */
    gst_element_post_message (GST_ELEMENT (filter),
        gst_mpeghuimanager_new_message (filter, inbuf, &times,
            gst_structure_new ("mpegh-ui-event-result",
                "xml", G_TYPE_STRING, event,
                "success", G_TYPE_BOOLEAN, ret == MPEGH_UI_OK,
                "error", G_TYPE_INT, (gint) ret, NULL)));
    g_free (event);
  }

  /* Put events kept for retrying back in front of those queued meanwhile */
  if (!g_queue_is_empty (&ui_events)) {
    GST_OBJECT_LOCK (filter);
    while ((event = g_queue_pop_tail (&ui_events)))
      g_queue_push_head (&filter->ui_event_queue, event);
    GST_OBJECT_UNLOCK (filter);
  }

  /* The UI manager stores the settings of applied events in the persistence
   * memory, publish them so the application can store them at any time */
  if (events_applied)
    gst_mpeghuimanager_publish_persistence (filter);

  /* Obtain the UI config from the MPEG-H UI manager. A scene state that
   * doesn't fit into the buffer is returned in parts, which are joined. */
  {
    gchar *part = filter->scene_state_buffer;
    GString *scene = NULL;
    guint flags_in = 0;
    guint flags_out = 0;

    while (!(flags_out & MPEGH_UI_NO_CHANGE)) {
      GstMessage *message;

      part[0] = '\0';
      ret = mpegh_UI_GetXmlSceneState (filter->mpegh_ui_manager,
          part, MAX_CONFIG_LENGTH, flags_in, &flags_out);
      flags_in = 0;
      if (ret != MPEGH_UI_OK) {
        GST_DEBUG_OBJECT (filter, "mpegh_UI_GetXmlSceneState failed with: %d",
            ret);
        break;
      }

      if (flags_out & MPEGH_UI_CONTINUES_XML) {
        /* The first parts were lost, as an earlier call failed. Restart the
         * output from the beginning. */
        if (scene == NULL) {
          GST_DEBUG_OBJECT (filter, "restarting incomplete scene state");
          flags_in = MPEGH_UI_FORCE_RESTART_XML;
          continue;
        }
      } else if (scene != NULL) {
        /* A new scene state replaces the incomplete one */
        g_string_truncate (scene, 0);
      }

      if (part[0] == '\0')
        continue;

      if (scene == NULL)
        scene = g_string_new (NULL);
      g_string_append (scene, part);

      if (flags_out & MPEGH_UI_INCOMPLETE_XML) {
        GST_DEBUG_OBJECT (filter, "scene state incomplete after %"
            G_GSIZE_FORMAT " bytes", scene->len);
        continue;
      }

      filter->scene_valid = gst_mpeghuimanager_scene_is_valid (scene->str);

      GST_INFO_OBJECT (filter, "new scene state of %" G_GSIZE_FORMAT " bytes",
          scene->len);
      GST_LOG_OBJECT (filter, "scene state: %s", scene->str);
      /* Element message "mpegh-ui-config" with a string field "config"
       * holding the MPEG-H Audio Scene Information XML, and the times of the
       * frame it belongs to, so the application can apply it in sync with
       * playback */
      message = gst_mpeghuimanager_new_message (filter, inbuf, &times,
          gst_structure_new ("mpegh-ui-config",
              "config", G_TYPE_STRING, scene->str, NULL));

      /* Keep the XML for the "config" property rather than the message: the
       * message holds a reference to the element as its source, so keeping
       * it would create a reference cycle */
      GST_OBJECT_LOCK (filter);
      g_free (filter->latest_config);
      filter->latest_config = g_string_free (scene, FALSE);
      GST_OBJECT_UNLOCK (filter);
      scene = NULL;

      gst_element_post_message (GST_ELEMENT (filter), message);
    }

    /* Drop an incomplete scene state left after a failure. The next call
     * continues it, which is detected and restarted above. */
    if (scene != NULL)
      g_string_free (scene, TRUE);
  }

  /* Update the bitstream data */
  *outsize = size;
  ret = mpegh_UI_UpdateMHAS (filter->mpegh_ui_manager, data, capacity, outsize);

  /* Not an error: the UI manager is inactive and leaves the frame as it is,
   * either because no configuration has been received yet, e.g. when
   * starting at a frame that is not a random access point, or because the
   * stream already contains UI packets, i.e. there is another UI manager
   * upstream which takes precedence */
  if (ret == MPEGH_UI_NOT_ALLOWED) {
    if (!filter->inactive) {
      GST_INFO_OBJECT (filter, "UI manager is inactive, passing frames "
          "through unchanged");
      filter->inactive = TRUE;
    }
    *outsize = size;
    return TRUE;
  }
  if (filter->inactive && ret == MPEGH_UI_OK) {
    GST_INFO_OBJECT (filter, "UI manager is active");
    filter->inactive = FALSE;
  }

  if (ret != MPEGH_UI_OK) {
    gst_mpeghuimanager_processing_failed (filter,
        "mpegh_UI_UpdateMHAS failed with: %d", ret);
    return FALSE;
  }
  /* mpegh_UI_UpdateMHAS() fails with MPEGH_UI_BUFFER_TOO_SMALL rather than
   * exceeding capacity */
  GST_DEBUG_OBJECT (filter, "input size: %u   output size: %u", size, *outsize);

  if (filter->consecutive_errors > 0) {
    GST_INFO_OBJECT (filter, "Processing recovered after %u frames passed "
        "through unchanged", filter->consecutive_errors);
    filter->consecutive_errors = 0;
  }

  return TRUE;
}

static GstFlowReturn
gst_mpeghuimanager_generate_output (GstBaseTransform * trans,
    GstBuffer ** outbuf)
{
  GstMpeghUiManager *filter = GST_MPEGHUIMANAGER (trans);
  GstBaseTransformClass *bclass = GST_BASE_TRANSFORM_GET_CLASS (trans);
  GstAllocator *allocator = NULL;
  GstAllocationParams params;
  GstBuffer *inbuf, *buf;
  GstMapInfo map;
  gsize size;
  guint capacity, outsize = 0;
  gboolean processed;

  *outbuf = NULL;

  /* Take ownership of the buffer stashed by the default
   * submit_input_buffer() */
  inbuf = trans->queued_buf;
  trans->queued_buf = NULL;
  if (inbuf == NULL)
    return GST_FLOW_OK;

  /* Empty and GAP buffers carry no MHAS frame, pass them through unchanged */
  if (gst_buffer_get_size (inbuf) == 0
      || GST_BUFFER_FLAG_IS_SET (inbuf, GST_BUFFER_FLAG_GAP)) {
    GST_DEBUG_OBJECT (filter, "passing through empty or gap buffer %"
        GST_PTR_FORMAT, inbuf);
    *outbuf = inbuf;
    return GST_FLOW_OK;
  }

  /* A frame the UI manager cannot handle is still valid input for the
   * decoder, so it is passed through unchanged */
  size = gst_buffer_get_size (inbuf);
  if (size > MAX_MPEGH_FRAME_SIZE) {
    gst_mpeghuimanager_processing_failed (filter,
        "Input frame of %" G_GSIZE_FORMAT " bytes exceeds maximum of %d",
        size, MAX_MPEGH_FRAME_SIZE);
    *outbuf = inbuf;
    return GST_FLOW_OK;
  }

  /* The UI manager updates the frame in place and may insert packets, so
   * copy it into an output buffer with room for that, and shrink the buffer
   * to the updated size afterwards */
  capacity = size + UPDATE_MHAS_HEADROOM;
  gst_base_transform_get_allocator (trans, &allocator, &params);
  buf = gst_buffer_new_allocate (allocator, capacity, &params);
  if (allocator)
    gst_object_unref (allocator);
  if (buf == NULL) {
    GST_ELEMENT_ERROR (filter, RESOURCE, FAILED, (NULL),
        ("Failed to allocate output buffer of %u bytes", capacity));
    gst_buffer_unref (inbuf);
    return GST_FLOW_ERROR;
  }

  /* Read and write: the UI manager parses the frame and then updates it */
  if (!gst_buffer_map (buf, &map, GST_MAP_READWRITE)) {
    gst_mpeghuimanager_processing_failed (filter,
        "Failed to map output buffer");
    processed = FALSE;
  } else {
    /* The one copy of the frame: the input can't be updated in place, as it
     * may not be writable and has no room for inserted packets. Unlike
     * mapping it, extracting doesn't merge an input of several memories. */
    if (gst_buffer_extract (inbuf, 0, map.data, size) != size) {
      gst_mpeghuimanager_processing_failed (filter,
          "Failed to read input buffer");
      processed = FALSE;
    } else {
      processed = gst_mpeghuimanager_process (filter, inbuf, map.data, size,
          capacity, &outsize);
    }
    gst_buffer_unmap (buf, &map);
  }

  if (!processed) {
    gst_buffer_unref (buf);
    *outbuf = inbuf;
    return GST_FLOW_OK;
  }

  gst_buffer_resize (buf, 0, outsize);

  if (bclass->copy_metadata && !bclass->copy_metadata (trans, inbuf, buf)) {
    GST_ELEMENT_WARNING (filter, STREAM, NOT_IMPLEMENTED,
        ("could not copy metadata"), (NULL));
  }

  GST_DEBUG_OBJECT (filter, "output %" GST_PTR_FORMAT, buf);

  *outbuf = buf;
  gst_buffer_unref (inbuf);
  return GST_FLOW_OK;
}

/* Brings the persistence memory up to date, copies it to
 * "output-persistence" and notifies it if it changed. Called without the
 * object lock from start() and stop(), and from the streaming thread, which
 * is the only other user of the UI manager while it is open. */
static void
gst_mpeghuimanager_publish_persistence (GstMpeghUiManager * filter)
{
  gpointer memory = NULL;
  guint16 size = 0;
  MPEGH_UI_ERROR err;
  GBytes *persistence;

  GST_OBJECT_LOCK (filter);
  if (filter->mpegh_ui_manager == NULL || filter->persistence_memory == NULL)
    goto done;

  err = mpegh_UI_GetPersistenceMemory (filter->mpegh_ui_manager, &memory,
      &size);
  if (err != MPEGH_UI_OK) {
    GST_WARNING_OBJECT (filter, "Unable to get persistence memory: %d", err);
    goto done;
  }
  if (memory == NULL || size == 0) {
    GST_DEBUG_OBJECT (filter, "Persistence handling is not active");
    goto done;
  }

  /* The size can be smaller than the memory, as the UI manager only uses an
   * even number of bytes */
  persistence = g_bytes_new (memory, size);
  if (filter->output_persistence != NULL
      && g_bytes_equal (persistence, filter->output_persistence)) {
    g_bytes_unref (persistence);
    goto done;
  }
  g_clear_pointer (&filter->output_persistence, g_bytes_unref);
  filter->output_persistence = persistence;
  GST_OBJECT_UNLOCK (filter);

  GST_DEBUG_OBJECT (filter, "persistence data of %u bytes updated", size);
  g_object_notify (G_OBJECT (filter), "output-persistence");
  return;

done:
  GST_OBJECT_UNLOCK (filter);
}

/* Lets the UI manager use the persistence memory, creating it from
 * "input-persistence", or empty with the default size, unless the previous
 * run left one. Called with the object lock held. */
static void
gst_mpeghuimanager_set_persistence_memory (GstMpeghUiManager * filter)
{
  gboolean fresh = FALSE;
  MPEGH_UI_ERROR err;

  if (filter->persistence_memory == NULL) {
    /* g_malloc() memory satisfies the 16 bit alignment the UI manager
     * requires */
    if (filter->input_persistence != NULL) {
      gsize size;
      gconstpointer data = g_bytes_get_data (filter->input_persistence, &size);

      filter->persistence_memory = g_memdup2 (data, size);
      filter->persistence_memory_size = size;
    } else {
      filter->persistence_memory = g_malloc0 (DEFAULT_PERSISTENCE_MEMORY_SIZE);
      filter->persistence_memory_size = DEFAULT_PERSISTENCE_MEMORY_SIZE;
      fresh = TRUE;
    }
  }

  err = mpegh_UI_SetPersistenceMemory (filter->mpegh_ui_manager,
      filter->persistence_memory, filter->persistence_memory_size);

  /* Data the UI manager can't use, e.g. because it is too small: start
   * without stored settings instead of without persistence */
  if (err != MPEGH_UI_OK && err != MPEGH_UI_OK_BUT_NO_VALID_DATA && !fresh) {
    GST_WARNING_OBJECT (filter, "Ignoring persistence data of %u bytes: "
        "mpegh_UI_SetPersistenceMemory failed with: %d",
        filter->persistence_memory_size, err);
    g_free (filter->persistence_memory);
    filter->persistence_memory = g_malloc0 (DEFAULT_PERSISTENCE_MEMORY_SIZE);
    filter->persistence_memory_size = DEFAULT_PERSISTENCE_MEMORY_SIZE;
    fresh = TRUE;
    err = mpegh_UI_SetPersistenceMemory (filter->mpegh_ui_manager,
        filter->persistence_memory, filter->persistence_memory_size);
  }

  if (err == MPEGH_UI_OK) {
    GST_INFO_OBJECT (filter, "Restored persistence data of %u bytes",
        filter->persistence_memory_size);
  } else if (err == MPEGH_UI_OK_BUT_NO_VALID_DATA) {
    /* The memory is in use, but holds no stored settings, e.g. data of
     * another version of the UI manager */
    if (fresh) {
      GST_INFO_OBJECT (filter, "Starting without stored settings, with "
          "persistence memory of %u bytes", filter->persistence_memory_size);
    } else {
      GST_WARNING_OBJECT (filter, "Persistence data of %u bytes holds no "
          "valid settings, starting without stored settings",
          filter->persistence_memory_size);
    }
  } else {
    GST_WARNING_OBJECT (filter, "mpegh_UI_SetPersistenceMemory with %u "
        "bytes failed with: %d, persistence is disabled",
        filter->persistence_memory_size, err);
    g_clear_pointer (&filter->persistence_memory, g_free);
    filter->persistence_memory_size = 0;
  }
}

static gboolean
gst_mpeghuimanager_start (GstBaseTransform * trans)
{
  GstMpeghUiManager *filter = GST_MPEGHUIMANAGER (trans);
  HANDLE_MPEGH_UI_MANAGER mpegh_ui_manager;

  mpegh_ui_manager = mpegh_UI_Manager_Open ();
  if (mpegh_ui_manager == NULL) {
    GST_ELEMENT_ERROR (filter, LIBRARY, INIT,
        ("Unable to initialize MPEG-H UI manager"),
        ("mpegh_UI_Manager_Open failed"));
    return FALSE;
  }

  filter->scene_state_buffer = g_malloc (MAX_CONFIG_LENGTH);
  filter->consecutive_errors = 0;
  filter->scene_valid = FALSE;
  filter->inactive = FALSE;

  GST_OBJECT_LOCK (filter);
  filter->mpegh_ui_manager = mpegh_ui_manager;
  /* The UI manager updates the memory in place until it is closed */
  gst_mpeghuimanager_set_persistence_memory (filter);
  GST_OBJECT_UNLOCK (filter);

  /* Publish the data the element starts from */
  gst_mpeghuimanager_publish_persistence (filter);

  return TRUE;
}

static gboolean
gst_mpeghuimanager_stop (GstBaseTransform * trans)
{
  GstMpeghUiManager *filter = GST_MPEGHUIMANAGER (trans);

  if (filter->mpegh_ui_manager == NULL)
    return TRUE;

  /* Publish the settings made since the last update, e.g. when stopping
   * without EOS */
  gst_mpeghuimanager_publish_persistence (filter);

  GST_OBJECT_LOCK (filter);
  mpegh_UI_Manager_Close (filter->mpegh_ui_manager);
  filter->mpegh_ui_manager = NULL;
  GST_OBJECT_UNLOCK (filter);

  g_clear_pointer (&filter->scene_state_buffer, g_free);

  /* Drop UI events that were not applied and the scene of this stream */
  GST_OBJECT_LOCK (filter);
  g_queue_clear_full (&filter->ui_event_queue, g_free);
  g_clear_pointer (&filter->latest_config, g_free);
  GST_OBJECT_UNLOCK (filter);

  return TRUE;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return GST_ELEMENT_REGISTER (mpeghuimanager, plugin);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR, mpeghuimanager,
    "MPEG-H UI Manager", plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN)
