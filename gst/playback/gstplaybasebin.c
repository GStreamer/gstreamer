/* GStreamer
 * Copyright (C) <2007> Wim Taymans <wim.taymans@gmail.com>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst-i18n-plugin.h>
#include <string.h>
#include "gstplaybasebin.h"
#include "gststreamselector.h"
#include "gstplay-marshal.h"

#include <gst/pbutils/pbutils.h>

#include "gst/glib-compat-private.h"

GST_DEBUG_CATEGORY_STATIC (gst_play_base_bin_debug);
#define GST_CAT_DEFAULT gst_play_base_bin_debug

#define DEFAULT_QUEUE_SIZE          (3 * GST_SECOND)
#define DEFAULT_QUEUE_MIN_THRESHOLD ((DEFAULT_QUEUE_SIZE * 30) / 100)
#define DEFAULT_QUEUE_THRESHOLD     ((DEFAULT_QUEUE_SIZE * 95) / 100)
#define DEFAULT_CONNECTION_SPEED    0

#define GROUP_LOCK(pbb) g_mutex_lock (pbb->group_lock)
#define GROUP_UNLOCK(pbb) g_mutex_unlock (pbb->group_lock)
#define GROUP_WAIT(pbb) g_cond_wait (pbb->group_cond, pbb->group_lock)
#define GROUP_SIGNAL(pbb) g_cond_signal (pbb->group_cond)

/* props */
enum
{
  ARG_0,
  ARG_URI,
  ARG_SUBURI,
  ARG_QUEUE_SIZE,
  ARG_QUEUE_THRESHOLD,
  ARG_QUEUE_MIN_THRESHOLD,
  ARG_NSTREAMS,
  ARG_STREAMINFO,
  ARG_STREAMINFO_VALUES,
  ARG_SOURCE,
  ARG_VIDEO,
  ARG_AUDIO,
  ARG_TEXT,
  ARG_SUBTITLE_ENCODING,
  ARG_CONNECTION_SPEED
};

static void gst_play_base_bin_class_init (GstPlayBaseBinClass * klass);
static void gst_play_base_bin_init (GstPlayBaseBin * play_base_bin);
static void gst_play_base_bin_dispose (GObject * object);
static void gst_play_base_bin_finalize (GObject * object);

static void gst_play_base_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * spec);
static void gst_play_base_bin_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * spec);
static void gst_play_base_bin_handle_message_func (GstBin * bin,
    GstMessage * msg);

static GstStateChangeReturn gst_play_base_bin_change_state (GstElement *
    element, GstStateChange transition);

static const GList *gst_play_base_bin_get_streaminfo (GstPlayBaseBin * bin);
static GValueArray *gst_play_base_bin_get_streaminfo_value_array (GstPlayBaseBin
    * play_base_bin);
static void preroll_remove_overrun (GstElement * element,
    GstPlayBaseBin * play_base_bin);
static void queue_remove_probe (GstElement * queue, GstPlayBaseBin
    * play_base_bin);

static GstElement *make_decoder (GstPlayBaseBin * play_base_bin);
static gboolean has_all_raw_caps (GstPad * pad, gboolean * all_raw);

static gboolean prepare_output (GstPlayBaseBin * play_base_bin);
static void set_active_source (GstPlayBaseBin * play_base_bin,
    GstStreamType type, gint source_num);
static gboolean probe_triggered (GstPad * pad, GstEvent * event,
    gpointer user_data);
static void setup_substreams (GstPlayBaseBin * play_base_bin);

static GstPipelineClass *parent_class;

/*
 * GObject playbasebin wrappers.
 */

GType
gst_play_base_bin_get_type (void)
{
  static GType gst_play_base_bin_type = 0;

  if (!gst_play_base_bin_type) {
    static const GTypeInfo gst_play_base_bin_info = {
      sizeof (GstPlayBaseBinClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_play_base_bin_class_init,
      NULL,
      NULL,
      sizeof (GstPlayBaseBin),
      0,
      (GInstanceInitFunc) gst_play_base_bin_init,
      NULL
    };

    gst_play_base_bin_type = g_type_register_static (GST_TYPE_PIPELINE,
        "GstPlayBaseBin", &gst_play_base_bin_info, 0);
  }

  return gst_play_base_bin_type;
}

static void
gst_play_base_bin_class_init (GstPlayBaseBinClass * klass)
{
  GObjectClass *gobject_klass;
  GstElementClass *gstelement_klass;
  GstBinClass *gstbin_klass;

  gobject_klass = (GObjectClass *) klass;
  gstelement_klass = (GstElementClass *) klass;
  gstbin_klass = (GstBinClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_klass->set_property = gst_play_base_bin_set_property;
  gobject_klass->get_property = gst_play_base_bin_get_property;

  g_object_class_install_property (gobject_klass, ARG_URI,
      g_param_spec_string ("uri", "URI", "URI of the media to play",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_klass, ARG_SUBURI,
      g_param_spec_string ("suburi", ".sub-URI", "Optional URI of a subtitle",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_klass, ARG_QUEUE_SIZE,
      g_param_spec_uint64 ("queue-size", "Queue size",
          "Size of internal queues in nanoseconds", 0, G_MAXINT64,
          DEFAULT_QUEUE_SIZE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_klass, ARG_QUEUE_THRESHOLD,
      g_param_spec_uint64 ("queue-threshold", "Queue threshold",
          "Buffering threshold of internal queues in nanoseconds", 0,
          G_MAXINT64, DEFAULT_QUEUE_THRESHOLD,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_klass, ARG_QUEUE_MIN_THRESHOLD,
      g_param_spec_uint64 ("queue-min-threshold", "Queue min threshold",
          "Buffering low threshold of internal queues in nanoseconds", 0,
          G_MAXINT64, DEFAULT_QUEUE_MIN_THRESHOLD,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_klass, ARG_NSTREAMS,
      g_param_spec_int ("nstreams", "NStreams", "number of streams",
          0, G_MAXINT, 0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_klass, ARG_STREAMINFO,
      g_param_spec_pointer ("stream-info", "Stream info", "List of streaminfo",
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_klass, ARG_STREAMINFO_VALUES,
      g_param_spec_value_array ("stream-info-value-array",
          "StreamInfo GValueArray", "value array of streaminfo",
          g_param_spec_object ("streaminfo", "StreamInfo", "Streaminfo object",
              GST_TYPE_STREAM_INFO, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS),
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_klass, ARG_SOURCE,
      g_param_spec_object ("source", "Source", "Source element",
          GST_TYPE_ELEMENT, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_klass, ARG_VIDEO,
      g_param_spec_int ("current-video", "Current video",
          "Currently playing video stream (-1 = none)",
          -1, G_MAXINT, -1, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_klass, ARG_AUDIO,
      g_param_spec_int ("current-audio", "Current audio",
          "Currently playing audio stream (-1 = none)",
          -1, G_MAXINT, -1, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_klass, ARG_TEXT,
      g_param_spec_int ("current-text", "Current text",
          "Currently playing text stream (-1 = none)",
          -1, G_MAXINT, -1, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_klass, ARG_SUBTITLE_ENCODING,
      g_param_spec_string ("subtitle-encoding", "subtitle encoding",
          "Encoding to assume if input subtitles are not in UTF-8 encoding. "
          "If not set, the GST_SUBTITLE_ENCODING environment variable will "
          "be checked for an encoding to use. If that is not set either, "
          "ISO-8859-15 will be assumed.", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstPlayBaseBin:connection-speed
   *
   * Network connection speed in kbps (0 = unknown)
   * <note><simpara>
   * Since version 0.10.10 in #GstPlayBin, at 0.10.15 moved to #GstPlayBaseBin
   * </simpara></note>
   *
   * Since: 0.10.10
   */
  g_object_class_install_property (gobject_klass, ARG_CONNECTION_SPEED,
      g_param_spec_uint ("connection-speed", "Connection Speed",
          "Network connection speed in kbps (0 = unknown)",
          0, G_MAXUINT, DEFAULT_CONNECTION_SPEED,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  GST_DEBUG_CATEGORY_INIT (gst_play_base_bin_debug, "playbasebin", 0,
      "playbasebin");

  gobject_klass->dispose = gst_play_base_bin_dispose;
  gobject_klass->finalize = gst_play_base_bin_finalize;

  gstbin_klass->handle_message =
      GST_DEBUG_FUNCPTR (gst_play_base_bin_handle_message_func);

  gstelement_klass->change_state =
      GST_DEBUG_FUNCPTR (gst_play_base_bin_change_state);
}

static void
gst_play_base_bin_init (GstPlayBaseBin * play_base_bin)
{
  play_base_bin->uri = NULL;
  play_base_bin->suburi = NULL;
  play_base_bin->need_rebuild = TRUE;
  play_base_bin->is_stream = FALSE;
  play_base_bin->source = NULL;
  play_base_bin->decoders = NULL;
  play_base_bin->subtitle = NULL;
  play_base_bin->subencoding = NULL;
  play_base_bin->subtitle_elements = NULL;
  play_base_bin->sub_lock = g_mutex_new ();

  play_base_bin->group_lock = g_mutex_new ();
  play_base_bin->group_cond = g_cond_new ();

  play_base_bin->building_group = NULL;
  play_base_bin->queued_groups = NULL;

  play_base_bin->queue_size = DEFAULT_QUEUE_SIZE;
  play_base_bin->queue_threshold = DEFAULT_QUEUE_THRESHOLD;
  play_base_bin->queue_min_threshold = DEFAULT_QUEUE_MIN_THRESHOLD;
  play_base_bin->connection_speed = DEFAULT_CONNECTION_SPEED;
}

static void
gst_play_base_bin_dispose (GObject * object)
{
  GstPlayBaseBin *play_base_bin;

  play_base_bin = GST_PLAY_BASE_BIN (object);
  g_free (play_base_bin->uri);
  play_base_bin->uri = NULL;
  g_free (play_base_bin->suburi);
  play_base_bin->suburi = NULL;
  g_free (play_base_bin->subencoding);
  play_base_bin->subencoding = NULL;
  if (play_base_bin->subtitle_elements) {
    g_slist_free (play_base_bin->subtitle_elements);
    play_base_bin->subtitle_elements = NULL;
  }
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_play_base_bin_finalize (GObject * object)
{
  GstPlayBaseBin *play_base_bin = GST_PLAY_BASE_BIN (object);

  g_mutex_free (play_base_bin->group_lock);
  g_cond_free (play_base_bin->group_cond);

  g_mutex_free (play_base_bin->sub_lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/*
 * playbasebingroup stuff.
 */

static GstPlayBaseGroup *
group_create (GstPlayBaseBin * play_base_bin)
{
  GstPlayBaseGroup *group;

  group = g_new0 (GstPlayBaseGroup, 1);
  group->bin = play_base_bin;
  group->streaminfo_value_array = g_value_array_new (0);

  GST_DEBUG_OBJECT (play_base_bin, "created new group %p", group);

  return group;
}

/*
 * Gets the currently playing group.
 *
 * Callers must have group-lock held when calling this.
 */

static GstPlayBaseGroup *
get_active_group (GstPlayBaseBin * play_base_bin)
{
  GstPlayBaseGroup *group = NULL;

  if (play_base_bin->queued_groups)
    group = play_base_bin->queued_groups->data;

  return group;
}

/*
 * get the group used for discovering the different streams.
 * This function creates a group is there is none.
 *
 * Callers must have group-lock held when calling this.
 */
static GstPlayBaseGroup *
get_building_group (GstPlayBaseBin * play_base_bin)
{
  GstPlayBaseGroup *group;

  group = play_base_bin->building_group;
  if (group == NULL) {
    group = group_create (play_base_bin);
    play_base_bin->building_group = group;
  }

  return group;
}

/*
 * Callers must have lock held when calling this!
 */

static void
group_destroy (GstPlayBaseGroup * group)
{
  GstPlayBaseBin *play_base_bin = group->bin;
  gint n;

  GST_LOG ("removing group %p", group);

  /* remove the preroll queues */
  for (n = 0; n < NUM_TYPES; n++) {
    GstElement *element = group->type[n].preroll;
    GstElement *fakesrc;
    GstElement *sel;
    const GList *item;

    if (!element)
      continue;

    sel = group->type[n].selector;

    /* remove any fakesrc elements for this preroll element */
    for (item = sel->pads; item != NULL; item = item->next) {
      GstPad *pad = GST_PAD (item->data);
      guint sig_id;

      if (GST_PAD_DIRECTION (pad) != GST_PAD_SINK)
        continue;

      sig_id =
          GPOINTER_TO_INT (g_object_get_data (G_OBJECT (pad), "unlinked_id"));

      if (sig_id != 0) {
        GST_LOG ("removing unlink signal %s:%s", GST_DEBUG_PAD_NAME (pad));
        g_signal_handler_disconnect (G_OBJECT (pad), sig_id);
        g_object_set_data (G_OBJECT (pad), "unlinked_id", GINT_TO_POINTER (0));
      }

      fakesrc = (GstElement *) g_object_get_data (G_OBJECT (pad), "fakesrc");
      if (fakesrc != NULL) {
        GST_LOG ("removing fakesrc from %s:%s",
            GST_PAD_NAME (pad), GST_ELEMENT_NAME (GST_PAD_PARENT (pad)));
        gst_element_set_state (fakesrc, GST_STATE_NULL);
        gst_bin_remove (GST_BIN_CAST (play_base_bin), fakesrc);
      }
    }

    /* if the group is currently being played, we have to remove the element
     * from the thread */
    gst_element_set_state (element, GST_STATE_NULL);
    gst_element_set_state (group->type[n].selector, GST_STATE_NULL);

    GST_LOG ("removing preroll element %s", GST_ELEMENT_NAME (element));

    gst_bin_remove (group->type[n].bin, element);
    gst_bin_remove (group->type[n].bin, group->type[n].selector);

    group->type[n].preroll = NULL;
    group->type[n].selector = NULL;
    group->type[n].bin = NULL;
  }

  /* free the streaminfo too */
  g_list_foreach (group->streaminfo, (GFunc) g_object_unref, NULL);
  g_list_free (group->streaminfo);
  g_value_array_free (group->streaminfo_value_array);
  g_free (group);
}

/*
 * is called when the current building group is completely finished
 * and ready for playback
 *
 * This function grabs lock, so take care when calling.
 */
static void
group_commit (GstPlayBaseBin * play_base_bin, gboolean fatal, gboolean subtitle)
{
  GstPlayBaseGroup *group;
  gboolean had_active_group;

  GROUP_LOCK (play_base_bin);
  group = play_base_bin->building_group;
  had_active_group = (get_active_group (play_base_bin) != NULL);

  GST_DEBUG_OBJECT (play_base_bin, "commit group %p, had active %d",
      group, had_active_group);

  /* if an element signalled a no-more-pads after we stopped due
   * to preroll, the group is NULL. This is not an error */
  if (group == NULL) {
    if (!fatal) {
      GROUP_UNLOCK (play_base_bin);
      return;
    } else {
      GST_DEBUG_OBJECT (play_base_bin, "Group loading failed, bailing out");
    }
  } else {
    if (!subtitle) {
      gint n;

      GST_DEBUG_OBJECT (play_base_bin, "group %p done", group);

      play_base_bin->queued_groups =
          g_list_append (play_base_bin->queued_groups, group);

      play_base_bin->building_group = NULL;

      /* remove signals. We don't want anymore signals from the preroll
       * elements at this stage. */
      for (n = 0; n < NUM_TYPES; n++) {
        GstElement *element = group->type[n].preroll;

        if (!element)
          continue;

        preroll_remove_overrun (element, play_base_bin);
        /* if overrun is removed, probe alse has to be removed */
        queue_remove_probe (element, play_base_bin);
      }
    } else {
      /* this is a special subtitle bin, we don't commit the group but
       * mark the subtitles as detected before we signal. */
      GST_DEBUG_OBJECT (play_base_bin, "marking subtitle bin as complete");
      play_base_bin->subtitle_done = TRUE;
    }
  }

  GST_DEBUG_OBJECT (play_base_bin, "signal group done");
  GROUP_SIGNAL (play_base_bin);
  GST_DEBUG_OBJECT (play_base_bin, "signaled group done");

  if (!subtitle && !had_active_group) {
    if (!prepare_output (play_base_bin)) {
      GROUP_UNLOCK (play_base_bin);
      return;
    }

    setup_substreams (play_base_bin);
    GST_DEBUG_OBJECT (play_base_bin, "Emitting signal");
    GST_PLAY_BASE_BIN_GET_CLASS (play_base_bin)->setup_output_pads
        (play_base_bin, group);
    GST_DEBUG_OBJECT (play_base_bin, "done");

    GROUP_UNLOCK (play_base_bin);

    g_object_notify (G_OBJECT (play_base_bin), "stream-info");
  } else {
    GROUP_UNLOCK (play_base_bin);
  }
}

/*
 * check if there are streams in the group that are not muted
 *
 * Callers must have group-lock held when calling this.
 */
static gboolean
group_is_muted (GstPlayBaseGroup * group)
{
  gint n;

  for (n = 0; n < NUM_TYPES; n++) {
    if (group->type[n].preroll && !group->type[n].done)
      return FALSE;
  }

  return TRUE;
}

/*
 * Buffer/cache checking.
 */

static inline void
fill_buffer (GstPlayBaseBin * play_base_bin, gint percent)
{
  GST_DEBUG_OBJECT (play_base_bin, "buffering %d", percent);
  gst_element_post_message (GST_ELEMENT_CAST (play_base_bin),
      gst_message_new_buffering (GST_OBJECT_CAST (play_base_bin), percent));
}

static gboolean
check_queue_event (GstPad * pad, GstEvent * event, gpointer user_data)
{
  GstElement *queue = GST_ELEMENT_CAST (user_data);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      GST_DEBUG ("EOS event, mark EOS");
      g_object_set_data (G_OBJECT (queue), "eos", GINT_TO_POINTER (1));
      break;
    case GST_EVENT_FLUSH_STOP:
      GST_DEBUG ("FLUSH_STOP event, remove EOS");
      g_object_set_data (G_OBJECT (queue), "eos", NULL);
      break;
    default:
      GST_DEBUG ("uninteresting event %s", GST_EVENT_TYPE_NAME (event));
      break;
  }
  return TRUE;
}

static gboolean
check_queue (GstPad * pad, GstBuffer * data, gpointer user_data)
{
  GstElement *queue = GST_ELEMENT_CAST (user_data);
  GstPlayBaseBin *play_base_bin = g_object_get_data (G_OBJECT (queue), "pbb");
  guint64 level = 0;

  GST_DEBUG_OBJECT (queue, "check queue triggered");

  g_object_get (G_OBJECT (queue), "current-level-time", &level, NULL);
  GST_DEBUG_OBJECT (play_base_bin, "Queue size: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (level));

  if (play_base_bin->queue_threshold > 0) {
    level = level * 99 / play_base_bin->queue_threshold;
    if (level > 99)
      level = 99;
  } else
    level = 99;

  fill_buffer (play_base_bin, level);

  /* continue! */
  return TRUE;
}

/* If a queue overruns and we are buffer in streaming mode (we have a min-time)
 * we can potentially create a deadlock when:
 *
 *  1) the max-bytes is hit and
 *  2) the min-time is not hit.
 *
 * We recover from this situation in this callback by
 * setting the max-bytes to unlimited if we see that there is
 * a current-time-level (which means some sort of timestamping is
 * done).
 */
static void
queue_deadlock_check (GstElement * queue, GstPlayBaseBin * play_base_bin)
{
  guint64 time, min_time;
  guint bytes;

  GST_DEBUG_OBJECT (play_base_bin, "overrun signal received from queue %s",
      GST_ELEMENT_NAME (queue));

  /* figure out where we are */
  g_object_get (G_OBJECT (queue), "current-level-time", &time,
      "current-level-bytes", &bytes, "min-threshold-time", &min_time, NULL);

  GST_DEBUG_OBJECT (play_base_bin, "streaming mode, queue %s current %"
      GST_TIME_FORMAT ", min %" GST_TIME_FORMAT
      ", bytes %d", GST_ELEMENT_NAME (queue),
      GST_TIME_ARGS (time), GST_TIME_ARGS (min_time), bytes);

  /* if the bytes in the queue represent time, we disable bytes
   * overrun checking to not cause deadlocks.
   */
  if (bytes && time != 0 && time < min_time) {
    GST_DEBUG_OBJECT (play_base_bin,
        "possible deadlock found, removing byte limit");

    /* queue knows about time but is filled with bytes that do
     * not represent min-threshold time, disable bytes checking so
     * the queue can grow some more. */
    g_object_set (G_OBJECT (queue), "max-size-bytes", 0, NULL);

    /* bytes limit is removed, we cannot deadlock anymore */
    g_signal_handlers_disconnect_by_func (queue,
        (gpointer) queue_deadlock_check, play_base_bin);
  } else {
    GST_DEBUG_OBJECT (play_base_bin, "no deadlock");
  }
}

static void
queue_remove_probe (GstElement * queue, GstPlayBaseBin * play_base_bin)
{
  gpointer data;
  GstPad *sinkpad;

  data = g_object_get_data (G_OBJECT (queue), "probe");
  sinkpad = gst_element_get_static_pad (queue, "sink");

  if (data) {
    GST_DEBUG_OBJECT (play_base_bin,
        "Removing buffer probe from pad %s:%s (%p)",
        GST_DEBUG_PAD_NAME (sinkpad), sinkpad);

    g_object_set_data (G_OBJECT (queue), "probe", NULL);
    gst_pad_remove_buffer_probe (sinkpad, GPOINTER_TO_INT (data));
  } else {
    GST_DEBUG_OBJECT (play_base_bin,
        "No buffer probe to remove from %s:%s (%p)",
        GST_DEBUG_PAD_NAME (sinkpad), sinkpad);
  }
  gst_object_unref (sinkpad);
}

/* Used for time-based buffering in streaming mode and is called when a queue
 * emits the running signal. This means that the high watermark threshold is
 * reached and the buffering is completed. */
static void
queue_threshold_reached (GstElement * queue, GstPlayBaseBin * play_base_bin)
{
  GstPlayBaseGroup *group;
  gint n;

  GST_DEBUG_OBJECT (play_base_bin, "running signal received from queue %s",
      GST_ELEMENT_NAME (queue));

  /* we disconnect the signal so that we don't get called for every buffer. */
  g_signal_handlers_disconnect_by_func (queue,
      (gpointer) queue_threshold_reached, play_base_bin);

  if (g_object_get_data (G_OBJECT (queue), "eos")) {
    GST_DEBUG_OBJECT (play_base_bin, "disable min threshold time, we are EOS");
    g_object_set (queue, "min-threshold-time", (guint64) 0, NULL);
  } else {
    /* now place the limits at the low threshold. When we hit this limit, the
     * underrun signal will be called. The underrun signal is always connected. */
    GST_DEBUG_OBJECT (play_base_bin,
        "setting min threshold time to %" G_GUINT64_FORMAT,
        play_base_bin->queue_min_threshold);
    g_object_set (queue, "min-threshold-time",
        play_base_bin->queue_min_threshold, NULL);
  }

  GROUP_LOCK (play_base_bin);
  group = get_active_group (play_base_bin);
  if (!group) {
    GROUP_UNLOCK (play_base_bin);
    return;
  }

  /* we remove the probe now because we don't need it anymore to give progress
   * about the buffering. */
  for (n = 0; n < NUM_TYPES; n++) {
    GstElement *element = group->type[n].preroll;

    if (!element)
      continue;

    queue_remove_probe (element, play_base_bin);
  }

  GROUP_UNLOCK (play_base_bin);

  /* we post a 100% buffering message to notify the app that buffering is
   * completed and playback can start/continue */
  if (play_base_bin->is_stream)
    fill_buffer (play_base_bin, 100);
}

/* this signal will be fired when one of the queues with raw
 * data is filled. This means that the group building stage is over
 * and playback of the new queued group should start. This is a rather unusual
 * situation because normally the group is committed when the "no_more_pads"
 * signal is fired.
 */
static void
queue_overrun (GstElement * queue, GstPlayBaseBin * play_base_bin)
{
  GST_DEBUG_OBJECT (play_base_bin, "queue %s overrun",
      GST_ELEMENT_NAME (queue));

  preroll_remove_overrun (queue, play_base_bin);

  group_commit (play_base_bin, FALSE,
      GST_OBJECT_PARENT (GST_OBJECT_CAST (queue)) ==
      GST_OBJECT_CAST (play_base_bin->subtitle));

  /* notify end of buffering */
  queue_threshold_reached (queue, play_base_bin);
}

/* this signal is only added when in streaming mode to catch underruns
 */
static void
queue_out_of_data (GstElement * queue, GstPlayBaseBin * play_base_bin)
{
  GST_DEBUG_OBJECT (play_base_bin, "underrun signal received from queue %s",
      GST_ELEMENT_NAME (queue));

  /* On underrun, we want to temporarily pause playback, set a "min-size"
   * threshold and wait for the running signal and then play again.
   *
   * This signal could never be called because the queue max-size limits are set
   * too low. We take care of this possible deadlock in the overrun signal
   * handler. */
  g_signal_connect (G_OBJECT (queue), "pushing",
      G_CALLBACK (queue_threshold_reached), play_base_bin);
  GST_DEBUG_OBJECT (play_base_bin,
      "setting min threshold time to %" G_GUINT64_FORMAT,
      (guint64) play_base_bin->queue_threshold);
  g_object_set (queue, "min-threshold-time",
      (guint64) play_base_bin->queue_threshold, NULL);

  /* re-connect probe, this will fire feedback about the percentage that we
   * buffered and is posted in the BUFFERING message. */
  if (!g_object_get_data (G_OBJECT (queue), "probe")) {
    GstPad *sinkpad;
    guint id;

    sinkpad = gst_element_get_static_pad (queue, "sink");
    id = gst_pad_add_buffer_probe (sinkpad, G_CALLBACK (check_queue), queue);
    g_object_set_data (G_OBJECT (queue), "probe", GINT_TO_POINTER (id));
    GST_DEBUG_OBJECT (play_base_bin,
        "Re-attaching buffering probe to pad %s:%s %p",
        GST_DEBUG_PAD_NAME (sinkpad), sinkpad);
    gst_object_unref (sinkpad);

    fill_buffer (play_base_bin, 0);
  }
}

/*
 * generate a preroll element which is simply a queue. While there
 * are still dynamic elements in the pipeline, we wait for one
 * of the queues to fill. The assumption is that all the dynamic
 * streams will be detected by that time.
 *
 * Callers must have the group-lock held when calling this.
 */
static void
gen_preroll_element (GstPlayBaseBin * play_base_bin,
    GstPlayBaseGroup * group, GstStreamType type, GstPad * pad,
    GstStreamInfo * info)
{
  GstElement *selector, *preroll;
  gchar *name, *padname;
  const gchar *prename;
  guint overrun_sig;
  GstPad *preroll_pad;
  GstBin *target;
  GstState state;

  if (type == GST_STREAM_TYPE_VIDEO)
    prename = "video";
  else if (type == GST_STREAM_TYPE_TEXT)
    prename = "text";
  else if (type == GST_STREAM_TYPE_AUDIO)
    prename = "audio";
  else if (type == GST_STREAM_TYPE_SUBPICTURE)
    prename = "subpicture";
  else
    g_return_if_reached ();

  /* create stream selector */
  padname = gst_pad_get_name (pad);
  name = g_strdup_printf ("selector_%s_%s", prename, padname);
  selector = g_object_new (GST_TYPE_STREAM_SELECTOR, "name", name, NULL);
  g_free (name);

  /* create preroll queue */
  name = g_strdup_printf ("preroll_%s_%s", prename, padname);
  preroll = gst_element_factory_make ("queue", name);
  g_free (name);
  g_free (padname);

  /* for buffering of raw data we ideally want to buffer a
   * very small amount of buffers since the memory used by
   * this raw data can be enormously huge.
   *
   * We use an upper limit of typically a few seconds here but
   * cap in case no timestamps are set on the raw data (bad!).
   *
   * FIXME: we abuse this buffer to do network buffering since
   * we can then easily do time-based buffering. The better
   * solution would be to add a specific network queue right
   * after the source that measures the datarate and scales this
   * queue of encoded data instead.
   */
  if (play_base_bin->raw_decoding_mode) {
    if (type == GST_STREAM_TYPE_VIDEO) {
      g_object_set (G_OBJECT (preroll),
          "max-size-buffers", 2, "max-size-bytes", 0,
          "max-size-time", (guint64) 0, NULL);
    } else {
      g_object_set (G_OBJECT (preroll),
          "max-size-buffers", 0, "max-size-bytes",
          2 * 1024 * 1024, "max-size-time", play_base_bin->queue_size, NULL);
    }
  } else {
    g_object_set (G_OBJECT (preroll),
        "max-size-buffers", 0, "max-size-bytes",
        ((type == GST_STREAM_TYPE_VIDEO) ? 25 : 2) * 1024 * 1024,
        "max-size-time", play_base_bin->queue_size, NULL);
  }

  /* the overrun signal is always attached and serves two purposes:
   *
   *  1) when we are building a group and the overrun is called, we commit the
   *     group. The reason being that if we fill the entire queue without a
   *     normal group commit (with _no_more_pads()) we can assume the
   *     audio/video is completely wacked or the element just does not know when
   *     it is ready with all the pads (mpeg).
   *  2) When we are doing network buffering, we keep track of low/high
   *     watermarks in the queue. It is possible that we set the high watermark
   *     higher than the max-size limits to trigger an overrun. In this case we
   *     will never get a running signal but we can use the overrun signal to
   *     detect this deadlock and correct it.
   */
  overrun_sig = g_signal_connect (G_OBJECT (preroll), "overrun",
      G_CALLBACK (queue_overrun), play_base_bin);

  /* keep a ref to the signal id so that we can disconnect the signal callback
   * when we are done with the preroll */
  g_object_set_data (G_OBJECT (preroll), "overrun_signal_id",
      GINT_TO_POINTER (overrun_sig));

  if (play_base_bin->is_stream &&
      ((type == GST_STREAM_TYPE_VIDEO &&
              group->type[GST_STREAM_TYPE_AUDIO - 1].npads == 0) ||
          (type == GST_STREAM_TYPE_AUDIO &&
              group->type[GST_STREAM_TYPE_VIDEO - 1].npads == 0))) {
    GstPad *sinkpad;
    guint id;

    /* catch deadlocks when we are network buffering in time but the max-limit
     * in bytes is hit. */
    g_signal_connect (G_OBJECT (preroll), "overrun",
        G_CALLBACK (queue_deadlock_check), play_base_bin);

    /* attach pointer to playbasebin */
    g_object_set_data (G_OBJECT (preroll), "pbb", play_base_bin);

    /* give updates on queue size */
    sinkpad = gst_element_get_static_pad (preroll, "sink");
    id = gst_pad_add_buffer_probe (sinkpad, G_CALLBACK (check_queue), preroll);
    GST_DEBUG_OBJECT (play_base_bin, "Attaching probe to pad %s:%s (%p)",
        GST_DEBUG_PAD_NAME (sinkpad), sinkpad);
    g_object_set_data (G_OBJECT (preroll), "probe", GINT_TO_POINTER (id));

    /* catch eos and flush events so that we can ignore underruns */
    id = gst_pad_add_event_probe (sinkpad, G_CALLBACK (check_queue_event),
        preroll);
    g_object_set_data (G_OBJECT (preroll), "eos_probe", GINT_TO_POINTER (id));

    gst_object_unref (sinkpad);

    /* When we connect this queue, it will start running and immediately
     * fire an underrun. */
    g_signal_connect (G_OBJECT (preroll), "underrun",
        G_CALLBACK (queue_out_of_data), play_base_bin);
    /* configure threshold and callbacks */
    queue_out_of_data (preroll, play_base_bin);
  }

  /* listen for EOS so we can switch groups when one ended. */
  preroll_pad = gst_element_get_static_pad (preroll, "src");
  gst_pad_add_event_probe (preroll_pad, G_CALLBACK (probe_triggered), info);
  gst_object_unref (preroll_pad);

  /* add to group list */
  group->type[type - 1].selector = selector;
  group->type[type - 1].preroll = preroll;

  /* figure out where the preroll element should go */
  if (type == GST_STREAM_TYPE_TEXT && play_base_bin->subtitle)
    target = GST_BIN_CAST (play_base_bin->subtitle);
  else
    target = GST_BIN_CAST (play_base_bin);

  group->type[type - 1].bin = target;
  gst_bin_add (target, selector);
  gst_bin_add (target, preroll);

  gst_element_link (selector, preroll);

  /* figure out target state and set */
  state = (GST_STATE (play_base_bin) == GST_STATE_PLAYING ?
      GST_STATE_PLAYING : GST_STATE_PAUSED);

  gst_element_set_state (selector, state);
  gst_element_set_state (preroll, state);
}

static void
preroll_remove_overrun (GstElement * element, GstPlayBaseBin * play_base_bin)
{
  guint overrun_sig;
  GObject *obj = G_OBJECT (element);

  overrun_sig = GPOINTER_TO_INT (g_object_get_data (obj, "overrun_signal_id"));
  if (overrun_sig) {
    GST_LOG_OBJECT (play_base_bin, "removing preroll signal %s",
        GST_ELEMENT_NAME (element));
    g_signal_handler_disconnect (obj, overrun_sig);
    /* We have disconnected this signal, remove the signal_id from the object
     * data */
    g_object_set_data (obj, "overrun_signal_id", NULL);
  }
}

static void
remove_groups (GstPlayBaseBin * play_base_bin)
{
  GROUP_LOCK (play_base_bin);

  /* first destroy the group we were building if any */
  if (play_base_bin->building_group) {
    group_destroy (play_base_bin->building_group);
    play_base_bin->building_group = NULL;
  }

  /* remove the queued groups */
  g_list_foreach (play_base_bin->queued_groups, (GFunc) group_destroy, NULL);
  g_list_free (play_base_bin->queued_groups);
  play_base_bin->queued_groups = NULL;

  /* clear subs */
  if (play_base_bin->subtitle) {
    gst_element_set_state (play_base_bin->subtitle, GST_STATE_NULL);
    gst_bin_remove (GST_BIN_CAST (play_base_bin), play_base_bin->subtitle);
    play_base_bin->subtitle = NULL;
  }

  GROUP_UNLOCK (play_base_bin);
}

/*
 * Add/remove a single stream to current building group.
 *
 * Must be called with group-lock held.
 */
static void
add_stream (GstPlayBaseGroup * group, GstStreamInfo * info)
{
  GValue v = { 0, };
  GST_DEBUG ("add stream to group %p", group);

  /* keep ref to the group */
  g_object_set_data (G_OBJECT (info), "group", group);

  g_value_init (&v, G_TYPE_OBJECT);
  g_value_set_object (&v, info);
  g_value_array_append (group->streaminfo_value_array, &v);
  g_value_unset (&v);
  group->streaminfo = g_list_append (group->streaminfo, info);
  if (info->type > 0 && info->type <= NUM_TYPES) {
    group->type[info->type - 1].npads++;
  }
}

static gboolean
string_arr_has_str (const gchar * values[], const gchar * value)
{
  if (values && value) {
    while (*values != NULL) {
      if (strcmp (value, *values) == 0)
        return TRUE;
      ++values;
    }
  }
  return FALSE;
}

/* mime types we are not handling on purpose right now, don't post a
 * missing-plugin message for these */
static const gchar *blacklisted_mimes[] = {
  "video/x-dvd-subpicture", NULL
};

#define IS_BLACKLISTED_MIME(type) (string_arr_has_str(blacklisted_mimes,type))

static void
gst_play_base_bin_handle_message_func (GstBin * bin, GstMessage * msg)
{
  if (gst_is_missing_plugin_message (msg)) {
    gchar *detail;
    guint i;

    detail = gst_missing_plugin_message_get_installer_detail (msg);
    for (i = 0; detail != NULL && blacklisted_mimes[i] != NULL; ++i) {
      if (strstr (detail, "|decoder-") && strstr (detail, blacklisted_mimes[i])) {
        GST_LOG_OBJECT (bin, "suppressing message %" GST_PTR_FORMAT, msg);
        gst_message_unref (msg);
        g_free (detail);
        return;
      }
    }
    g_free (detail);
  }
  GST_BIN_CLASS (parent_class)->handle_message (bin, msg);
}

/*
 * signal fired when an unknown stream is found. We create a new
 * UNKNOWN streaminfo object.
 */
static void
unknown_type (GstElement * element, GstPad * pad, GstCaps * caps,
    GstPlayBaseBin * play_base_bin)
{
  const gchar *type_name;
  GstStreamInfo *info;
  GstPlayBaseGroup *group;

  type_name = gst_structure_get_name (gst_caps_get_structure (caps, 0));
  if (type_name && !IS_BLACKLISTED_MIME (type_name)) {
    gchar *capsstr;

    capsstr = gst_caps_to_string (caps);
    GST_DEBUG_OBJECT (play_base_bin, "don't know how to handle %s", capsstr);
    /* FIXME, g_message() ? */
    g_message ("don't know how to handle %s", capsstr);
    g_free (capsstr);
  } else {
    /* don't spew stuff to the terminal or send message if it's blacklisted */
    GST_DEBUG_OBJECT (play_base_bin, "media type %s not handled on purpose, "
        "not posting a missing-plugin message on the bus", type_name);
  }

  GROUP_LOCK (play_base_bin);

  group = get_building_group (play_base_bin);

  /* add the stream to the list */
  info = gst_stream_info_new (GST_OBJECT_CAST (pad), GST_STREAM_TYPE_UNKNOWN,
      NULL, caps);
  info->origin = GST_OBJECT_CAST (pad);
  add_stream (group, info);

  GROUP_UNLOCK (play_base_bin);
}

/* add a streaminfo that indicates that the stream is handled by the
 * given element. This usually means that a stream without actual data is
 * produced but one that is sunken by an element. Examples of this are:
 * cdaudio, a hardware decoder/sink, dvd meta bins etc...
 */
static void
add_element_stream (GstElement * element, GstPlayBaseBin * play_base_bin)
{
  GstStreamInfo *info;
  GstPlayBaseGroup *group;

  GROUP_LOCK (play_base_bin);

  group = get_building_group (play_base_bin);

  /* add the stream to the list */
  info =
      gst_stream_info_new (GST_OBJECT_CAST (element), GST_STREAM_TYPE_ELEMENT,
      NULL, NULL);
  info->origin = GST_OBJECT_CAST (element);
  add_stream (group, info);

  GROUP_UNLOCK (play_base_bin);
}

/* when the decoder element signals that no more pads will be generated, we
 * can commit the current group.
 */
static void
no_more_pads_full (GstElement * element, gboolean subs,
    GstPlayBaseBin * play_base_bin)
{
  /* setup phase */
  GST_DEBUG_OBJECT (element, "no more pads, %d pending",
      play_base_bin->pending);

  /* nothing pending, we can exit */
  if (play_base_bin->pending == 0)
    return;

  /* the object has no pending no_more_pads */
  if (!g_object_get_data (G_OBJECT (element), "pending"))
    return;

  g_object_set_data (G_OBJECT (element), "pending", NULL);

  play_base_bin->pending--;

  GST_DEBUG_OBJECT (element, "remove pending, now %d pending",
      play_base_bin->pending);

  if (play_base_bin->pending == 0) {
    /* we can commit this group for playback now */
    group_commit (play_base_bin, play_base_bin->is_stream, subs);
  }
}

static void
no_more_pads (GstElement * element, GstPlayBaseBin * play_base_bin)
{
  no_more_pads_full (element, FALSE, play_base_bin);
}

static void
sub_no_more_pads (GstElement * element, GstPlayBaseBin * play_base_bin)
{
  no_more_pads_full (element, TRUE, play_base_bin);
}

static void
source_no_more_pads (GstElement * element, GstPlayBaseBin * bin)
{
  GST_DEBUG_OBJECT (bin, "No more pads in source element %s.",
      GST_ELEMENT_NAME (element));

  g_signal_handler_disconnect (G_OBJECT (element), bin->src_np_sig_id);
  bin->src_np_sig_id = 0;
  g_signal_handler_disconnect (G_OBJECT (element), bin->src_nmp_sig_id);
  bin->src_nmp_sig_id = 0;

  no_more_pads_full (element, FALSE, bin);
}

static gboolean
probe_triggered (GstPad * pad, GstEvent * event, gpointer user_data)
{
  GstPlayBaseGroup *group;
  GstPlayBaseBin *play_base_bin;
  GstStreamInfo *info;
  GstEventType type;

  type = GST_EVENT_TYPE (event);

  GST_LOG ("probe triggered, (%d) %s", type, gst_event_type_get_name (type));

  /* we only care about EOS */
  if (type != GST_EVENT_EOS)
    return TRUE;

  info = GST_STREAM_INFO (user_data);
  group = (GstPlayBaseGroup *) g_object_get_data (G_OBJECT (info), "group");
  play_base_bin = group->bin;

  if (type == GST_EVENT_EOS) {
    gint num_groups = 0;
    gboolean have_left;

    GST_DEBUG_OBJECT (play_base_bin, "probe got EOS in group %p", group);

    GROUP_LOCK (play_base_bin);

    /* mute this stream */
    g_object_set (G_OBJECT (info), "mute", TRUE, NULL);
    if (info->type > 0 && info->type <= NUM_TYPES)
      group->type[info->type - 1].done = TRUE;

    /* see if we have some more groups left to play */
    num_groups = g_list_length (play_base_bin->queued_groups);
    if (play_base_bin->building_group)
      num_groups++;
    have_left = (num_groups > 1);

    /* see if the complete group is muted */
    if (!group_is_muted (group)) {
      /* group is not completely muted, we remove the EOS event
       * and continue, eventually the other streams will be EOSed and
       * we can switch out this group. */
      GST_DEBUG ("group %p not completely muted", group);

      GROUP_UNLOCK (play_base_bin);

      /* remove the EOS if we have something left */
      return !have_left;
    }

    if (have_left) {
      /* ok, get rid of the current group then */
      //group_destroy (group);
      /* removing the current group brings the next group
       * active */
      play_base_bin->queued_groups =
          g_list_remove (play_base_bin->queued_groups, group);
      /* and wait for the next one to be ready */
      while (!play_base_bin->queued_groups) {
        GROUP_WAIT (play_base_bin);
      }
      group = play_base_bin->queued_groups->data;

      /* now activate the next one */
      setup_substreams (play_base_bin);
      GST_DEBUG ("switching to next group %p - emitting signal", group);
      /* and signal the new group */
      GST_PLAY_BASE_BIN_GET_CLASS (play_base_bin)->setup_output_pads
          (play_base_bin, group);

      GROUP_UNLOCK (play_base_bin);

      g_object_notify (G_OBJECT (play_base_bin), "stream-info");

      /* get rid of the EOS event */
      return FALSE;
    } else {
      GROUP_UNLOCK (play_base_bin);
      GST_LOG ("Last group done, EOS");
    }
  }

  return TRUE;
}

/* This function will be called when the sinkpad of the preroll element
 * is unlinked, we have to connect something to the sinkpad or else the
 * state change will fail..
 */
static void
preroll_unlinked (GstPad * pad, GstPad * peerpad,
    GstPlayBaseBin * play_base_bin)
{
  GstElement *fakesrc;
  guint sig_id;
  GstPad *srcpad;

  /* make a fakesrc that will just emit one EOS */
  fakesrc = gst_element_factory_make ("fakesrc", NULL);
  g_object_set (G_OBJECT (fakesrc), "num-buffers", 0, NULL);

  GST_DEBUG ("patching unlinked pad %s:%s", GST_DEBUG_PAD_NAME (pad));

  srcpad = gst_element_get_static_pad (fakesrc, "src");
  gst_bin_add (GST_BIN_CAST (play_base_bin), fakesrc);
  gst_pad_link (srcpad, pad);
  gst_object_unref (srcpad);

  /* keep track of these patch elements */
  g_object_set_data (G_OBJECT (pad), "fakesrc", fakesrc);

  /* now unlink the unlinked signal so that it is not called again when
   * we destroy the queue */
  sig_id = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (pad), "unlinked_id"));
  if (sig_id != 0) {
    g_signal_handler_disconnect (G_OBJECT (pad), sig_id);
    g_object_set_data (G_OBJECT (pad), "unlinked_id", GINT_TO_POINTER (0));
  }
}

/* Mute stream on first data - for header-is-in-stream-stuff
 * (vorbis, ogmtext). */
static gboolean
mute_stream (GstPad * pad, GstBuffer * buf, gpointer data)
{
  GstStreamInfo *info = GST_STREAM_INFO (data);
  guint id;

  GST_DEBUG ("mute stream triggered");

  g_object_set (G_OBJECT (info), "mute", TRUE, NULL);
  id = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (info), "mute_probe"));
  g_object_set_data (G_OBJECT (info), "mute_probe", NULL);
  if (id > 0)
    gst_pad_remove_buffer_probe (GST_PAD_CAST (info->object), id);

  /* no data */
  return FALSE;
}

/* Eat data. */
static gboolean
silence_stream (GstPad * pad, GstMiniObject * data, gpointer user_data)
{
  GST_DEBUG ("silence stream triggered");

  /* no data */
  return FALSE;
}

/* Called by the signal handlers when a decodebin (main or subtitle) has
 * found a new raw pad.  We create a preroll element if needed and the
 * appropriate streaminfo. Commits the group if there will be no more pads
 * from decodebin */
static void
new_decoded_pad_full (GstElement * element, GstPad * pad, gboolean last,
    GstPlayBaseBin * play_base_bin, gboolean is_subs)
{
  GstStructure *structure;
  const gchar *mimetype;
  GstCaps *caps;
  GstStreamInfo *info;
  GstStreamType type = GST_STREAM_TYPE_UNKNOWN;
  GstPad *sinkpad;
  GstPlayBaseGroup *group;
  guint sig;
  GstObject *parent;
  gboolean first_pad;

  GST_DEBUG ("play base: new decoded pad. Last: %d", last);

  /* first see if this pad has interesting caps */
  caps = gst_pad_get_caps (pad);
  if (caps == NULL || gst_caps_is_empty (caps) || gst_caps_is_any (caps))
    goto no_type;

  /* get the mime type */
  structure = gst_caps_get_structure (caps, 0);
  mimetype = gst_structure_get_name (structure);

  GROUP_LOCK (play_base_bin);

  group = get_building_group (play_base_bin);

  group->nstreams++;

  parent = gst_object_get_parent (GST_OBJECT_CAST (element));
  if (g_str_has_prefix (mimetype, "audio/") &&
      parent != GST_OBJECT_CAST (play_base_bin->subtitle)) {
    type = GST_STREAM_TYPE_AUDIO;
  } else if (g_str_has_prefix (mimetype, "video/x-dvd-subpicture") &&
      parent != GST_OBJECT_CAST (play_base_bin->subtitle)) {
    type = GST_STREAM_TYPE_SUBPICTURE;
  } else if (g_str_has_prefix (mimetype, "video/") &&
      parent != GST_OBJECT_CAST (play_base_bin->subtitle)) {
    type = GST_STREAM_TYPE_VIDEO;
  } else if (g_str_has_prefix (mimetype, "text/")) {
    type = GST_STREAM_TYPE_TEXT;
  }
  gst_object_unref (parent);

  info = gst_stream_info_new (GST_OBJECT_CAST (pad), type, NULL, caps);
  gst_caps_unref (caps);

  if (type == GST_STREAM_TYPE_UNKNOWN) {
    /* Unknown streams get added to the group, but the data
     * just gets ignored */
    add_stream (group, info);

    GROUP_UNLOCK (play_base_bin);

    /* signal the no more pads after adding the stream */
    if (last)
      no_more_pads_full (element, is_subs, play_base_bin);

    return;
  }

  /* first pad of each type gets a selector + preroll queue */
  first_pad = (group->type[type - 1].npads == 0);

  if (first_pad) {
    GST_DEBUG ("play base: pad needs new preroll");
    gen_preroll_element (play_base_bin, group, type, pad, info);
  }

  /* add to stream selector */
  sinkpad =
      gst_element_get_request_pad (group->type[type - 1].selector, "sink%d");

  /* make sure we catch unlink signals */
  sig = g_signal_connect (G_OBJECT (sinkpad), "unlinked",
      G_CALLBACK (preroll_unlinked), play_base_bin);
  /* keep a ref to the signal id so that we can disconnect the signal callback */
  g_object_set_data (G_OBJECT (sinkpad), "unlinked_id", GINT_TO_POINTER (sig));
  /* Store a pointer to the stream selector pad for this stream */
  g_object_set_data (G_OBJECT (pad), "pb_sel_pad", sinkpad);

  gst_pad_link (pad, sinkpad);
  gst_object_unref (sinkpad);

  /* select 1st for now - we'll select a preferred one after preroll */
  if (!first_pad) {
    guint id;

    GST_DEBUG ("Adding silence_stream data probe on type %d (npads %d)", type,
        group->type[type - 1].npads);

    id = gst_pad_add_data_probe (GST_PAD_CAST (pad),
        G_CALLBACK (silence_stream), info);
    g_object_set_data (G_OBJECT (pad), "eat_probe", GINT_TO_POINTER (id));
  }

  /* add the stream to the list */
  add_stream (group, info);

  GROUP_UNLOCK (play_base_bin);

  /* signal the no more pads after adding the stream */
  if (last)
    no_more_pads_full (element, is_subs, play_base_bin);

  return;

  /* ERRORS */
no_type:
  {
    g_warning ("no type on pad %s:%s", GST_DEBUG_PAD_NAME (pad));
    if (caps)
      gst_caps_unref (caps);
    return;
  }
}

static void
new_decoded_pad (GstElement * element, GstPad * pad, gboolean last,
    GstPlayBaseBin * play_base_bin)
{
  new_decoded_pad_full (element, pad, last, play_base_bin, FALSE);
}

static void
subs_new_decoded_pad (GstElement * element, GstPad * pad, gboolean last,
    GstPlayBaseBin * play_base_bin)
{
  new_decoded_pad_full (element, pad, last, play_base_bin, TRUE);
}

static void
set_encoding_element (GstElement * element, gchar * encoding)
{
  GST_DEBUG_OBJECT (element, "setting encoding to %s", GST_STR_NULL (encoding));
  g_object_set (G_OBJECT (element), "subtitle-encoding", encoding, NULL);
}


static void
decodebin_element_added_cb (GstBin * decodebin, GstElement * element,
    gpointer data)
{
  GstPlayBaseBin *play_base_bin = GST_PLAY_BASE_BIN (data);
  gchar *encoding;

  if (!g_object_class_find_property (G_OBJECT_GET_CLASS (element),
          "subtitle-encoding")) {
    return;
  }

  g_mutex_lock (play_base_bin->sub_lock);
  play_base_bin->subtitle_elements =
      g_slist_append (play_base_bin->subtitle_elements, element);
  encoding = g_strdup (play_base_bin->subencoding);
  g_mutex_unlock (play_base_bin->sub_lock);

  set_encoding_element (element, encoding);
  g_free (encoding);
}

static void
decodebin_element_removed_cb (GstBin * decodebin, GstElement * element,
    gpointer data)
{
  GstPlayBaseBin *play_base_bin = GST_PLAY_BASE_BIN (data);

  g_mutex_lock (play_base_bin->sub_lock);
  play_base_bin->subtitle_elements =
      g_slist_remove (play_base_bin->subtitle_elements, element);
  g_mutex_unlock (play_base_bin->sub_lock);
}


/*
 * Generate source ! subparse bins.
 */

static GstElement *
setup_subtitle (GstPlayBaseBin * play_base_bin, gchar * sub_uri)
{
  GstElement *source, *subdecodebin, *subbin;

  if (!gst_uri_is_valid (sub_uri))
    goto invalid_uri;

  source = gst_element_make_from_uri (GST_URI_SRC, sub_uri, NULL);
  if (!source)
    goto unknown_uri;

  if (g_getenv ("USE_DECODEBIN2"))
    subdecodebin = gst_element_factory_make ("decodebin2", "subtitle-decoder");
  else
    subdecodebin = gst_element_factory_make ("decodebin", "subtitle-decoder");
  g_signal_connect (subdecodebin, "element-added",
      G_CALLBACK (decodebin_element_added_cb), play_base_bin);
  g_signal_connect (subdecodebin, "element-removed",
      G_CALLBACK (decodebin_element_removed_cb), play_base_bin);
  subbin = gst_bin_new ("subtitle-bin");
  gst_bin_add_many (GST_BIN_CAST (subbin), source, subdecodebin, NULL);

  gst_element_link (source, subdecodebin);

  /* return the subtitle GstElement object */
  return subbin;

  /* WARNINGS */
invalid_uri:
  {
    GST_ELEMENT_WARNING (play_base_bin, RESOURCE, NOT_FOUND,
        (_("Invalid subtitle URI \"%s\", subtitles disabled."), sub_uri),
        (NULL));
    return NULL;
  }
unknown_uri:
  {
    gchar *prot = gst_uri_get_protocol (sub_uri);

    if (prot) {
      gchar *desc;

      gst_element_post_message (GST_ELEMENT (play_base_bin),
          gst_missing_uri_source_message_new (GST_ELEMENT (play_base_bin),
              prot));

      desc = gst_pb_utils_get_source_description (prot);
      GST_ELEMENT_ERROR (play_base_bin, CORE, MISSING_PLUGIN,
          (_("A %s plugin is required to play this stream, but not installed."),
              desc), ("No URI handler to handle sub_uri: %s", sub_uri));
      g_free (desc);
      g_free (prot);
    } else
      goto invalid_uri;

    return NULL;
  }
}

/* helper function to lookup stuff in lists */
static gboolean
array_has_value (const gchar * values[], const gchar * value)
{
  gint i;

  for (i = 0; values[i]; i++) {
    if (g_str_has_prefix (value, values[i]))
      return TRUE;
  }
  return FALSE;
}

/* list of URIs that we consider to be streams and that need buffering.
 * We have no mechanism yet to figure this out with a query. */
static const gchar *stream_uris[] = { "http://", "mms://", "mmsh://",
  "mmsu://", "mmst://", "myth://", NULL
};

/* blacklisted URIs, we know they will always fail. */
static const gchar *blacklisted_uris[] = { NULL };

/* mime types that we don't consider to be media types */
static const gchar *no_media_mimes[] = {
  "application/x-executable", "application/x-bzip", "application/x-gzip",
  "application/zip", "application/x-compress", NULL
};

/* mime types we consider raw media */
static const gchar *raw_mimes[] = {
  "audio/x-raw", "video/x-raw", "video/x-dvd-subpicture", NULL
};

#define IS_STREAM_URI(uri)          (array_has_value (stream_uris, uri))
#define IS_BLACKLISTED_URI(uri)     (array_has_value (blacklisted_uris, uri))
#define IS_NO_MEDIA_MIME(mime)      (array_has_value (no_media_mimes, mime))
#define IS_RAW_MIME(mime)           (array_has_value (raw_mimes, mime))

/*
 * Generate and configure a source element.
 */
static GstElement *
gen_source_element (GstPlayBaseBin * play_base_bin, GstElement ** subbin)
{
  GstElement *source;

  if (!play_base_bin->uri)
    goto no_uri;

  if (!gst_uri_is_valid (play_base_bin->uri))
    goto invalid_uri;

  if (IS_BLACKLISTED_URI (play_base_bin->uri))
    goto uri_blacklisted;

  if (play_base_bin->suburi) {
    GST_LOG_OBJECT (play_base_bin, "Creating decoder for subtitles URI %s",
        play_base_bin->suburi);
    /* subtitle specified */
    *subbin = setup_subtitle (play_base_bin, play_base_bin->suburi);
  } else {
    /* no subtitle specified */
    *subbin = NULL;
  }

  source = gst_element_make_from_uri (GST_URI_SRC, play_base_bin->uri,
      "source");
  if (!source)
    goto no_source;

  play_base_bin->is_stream = IS_STREAM_URI (play_base_bin->uri);

  /* make HTTP sources send extra headers so we get icecast
   * metadata in case the stream is an icecast stream */
  if (!strncmp (play_base_bin->uri, "http://", 7) &&
      g_object_class_find_property (G_OBJECT_GET_CLASS (source),
          "iradio-mode")) {
    g_object_set (source, "iradio-mode", TRUE, NULL);
  }

  if (g_object_class_find_property (G_OBJECT_GET_CLASS (source),
          "connection-speed")) {
    GST_DEBUG_OBJECT (play_base_bin,
        "setting connection-speed=%d to source element",
        play_base_bin->connection_speed / 1000);
    g_object_set (source, "connection-speed",
        play_base_bin->connection_speed / 1000, NULL);
  }

  return source;

  /* ERRORS */
no_uri:
  {
    GST_ELEMENT_ERROR (play_base_bin, RESOURCE, NOT_FOUND,
        (_("No URI specified to play from.")), (NULL));
    return NULL;
  }
invalid_uri:
  {
    GST_ELEMENT_ERROR (play_base_bin, RESOURCE, NOT_FOUND,
        (_("Invalid URI \"%s\"."), play_base_bin->uri), (NULL));
    return NULL;
  }
uri_blacklisted:
  {
    GST_ELEMENT_ERROR (play_base_bin, RESOURCE, FAILED,
        (_("RTSP streams cannot be played yet.")), (NULL));
    return NULL;
  }
no_source:
  {
    gchar *prot = gst_uri_get_protocol (play_base_bin->uri);

    /* whoops, could not create the source element, dig a little deeper to
     * figure out what might be wrong. */
    if (prot) {
      gchar *desc;

      gst_element_post_message (GST_ELEMENT (play_base_bin),
          gst_missing_uri_source_message_new (GST_ELEMENT (play_base_bin),
              prot));

      desc = gst_pb_utils_get_source_description (prot);
      GST_ELEMENT_ERROR (play_base_bin, CORE, MISSING_PLUGIN,
          (_("A %s plugin is required to play this stream, but not installed."),
              desc), ("No URI handler for %s", prot));
      g_free (desc);
      g_free (prot);
    } else
      goto invalid_uri;

    return NULL;
  }
}

/* is called when a dynamic source element created a new pad. */
static void
source_new_pad (GstElement * element, GstPad * pad, GstPlayBaseBin * bin)
{
  GstElement *decoder;
  gboolean is_raw;

  GST_DEBUG_OBJECT (bin, "Found new pad %s.%s in source element %s",
      GST_DEBUG_PAD_NAME (pad), GST_ELEMENT_NAME (element));

  /* if this is a pad with all raw caps, we can expose it */
  if (has_all_raw_caps (pad, &is_raw) && is_raw) {
    bin->raw_decoding_mode = TRUE;
    /* it's all raw, create output pads. */
    new_decoded_pad_full (element, pad, FALSE, bin, FALSE);
    return;
  }

  /* not raw, create decoder */
  decoder = make_decoder (bin);
  if (!decoder)
    goto no_decodebin;

  /* and link to decoder */
  if (!gst_element_link (bin->source, decoder))
    goto could_not_link;

  gst_element_set_state (decoder, GST_STATE_PAUSED);

  return;

  /* ERRORS */
no_decodebin:
  {
    /* error was posted */
    return;
  }
could_not_link:
  {
    GST_ELEMENT_ERROR (bin, CORE, NEGOTIATION,
        (NULL), ("Can't link source to decoder element"));
    return;
  }
}

/*
 * Setup the substreams (is called right after group_commit () when
 * loading a new group, or after switching groups).
 *
 * Should be called with group-lock held.
 */
static void
setup_substreams (GstPlayBaseBin * play_base_bin)
{
  GstPlayBaseGroup *group;
  gint n;
  const GList *item;

  GST_DEBUG_OBJECT (play_base_bin, "setting up substreams");

  /* Remove the eat probes */
  group = get_active_group (play_base_bin);
  for (item = group->streaminfo; item; item = item->next) {
    GstStreamInfo *info = item->data;
    gpointer data;

    data = g_object_get_data (G_OBJECT (info->object), "eat_probe");
    if (data) {
      gst_pad_remove_data_probe (GST_PAD_CAST (info->object),
          GPOINTER_TO_INT (data));
      g_object_set_data (G_OBJECT (info->object), "eat_probe", NULL);
    }

    /* now remove unknown pads */
    if (info->type == GST_STREAM_TYPE_UNKNOWN) {
      guint id;

      id = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (info), "mute_probe"));
      if (id == 0) {
        id = gst_pad_add_buffer_probe (GST_PAD_CAST (info->object),
            G_CALLBACK (mute_stream), info);
        g_object_set_data (G_OBJECT (info), "mute_probe", GINT_TO_POINTER (id));
      }
    }
  }

  /* now check if the requested current streams exist. If
   * current >= num_streams, decrease current so at least
   * we have output. Always keep it enabled. */
  for (n = 0; n < NUM_TYPES; n++) {
    if (play_base_bin->current[n] >= group->type[n].npads) {
      GST_DEBUG_OBJECT (play_base_bin, "reset type %d to current 0", n);
      play_base_bin->current[n] = 0;
    }
  }

  /* now activate the right sources. Don't forget that during preroll,
   * we set the first source to forwarding and ignored the rest. */
  for (n = 0; n < NUM_TYPES; n++) {
    GST_DEBUG_OBJECT (play_base_bin, "setting type %d to current %d", n,
        play_base_bin->current[n]);
    set_active_source (play_base_bin, n + 1, play_base_bin->current[n]);
  }
}

/**
 * has_all_raw_caps:
 * @pad: a #GstPad
 * @all_raw: pointer to hold the result
 *
 * check if the caps of the pad are all raw. The caps are all raw if
 * all of its structures contain audio/x-raw or video/x-raw.
 *
 * Returns: %FALSE @pad has no caps. Else TRUE and @all_raw set t the result.
 */
static gboolean
has_all_raw_caps (GstPad * pad, gboolean * all_raw)
{
  GstCaps *caps;
  gint capssize;
  guint i, num_raw = 0;
  gboolean res = FALSE;

  caps = gst_pad_get_caps (pad);
  if (caps == NULL)
    return FALSE;

  capssize = gst_caps_get_size (caps);
  /* no caps, skip and move to the next pad */
  if (capssize == 0 || gst_caps_is_empty (caps) || gst_caps_is_any (caps))
    goto done;

  /* count the number of raw formats in the caps */
  for (i = 0; i < capssize; ++i) {
    GstStructure *s;
    const gchar *mime_type;

    s = gst_caps_get_structure (caps, i);
    mime_type = gst_structure_get_name (s);

    if (IS_RAW_MIME (mime_type))
      ++num_raw;
  }

  *all_raw = (num_raw == capssize);
  res = TRUE;

done:
  gst_caps_unref (caps);
  return res;
}

/**
 * analyse_source:
 * @play_base_bin: a #GstPlayBaseBin
 * @is_raw: are all pads raw data
 * @have_out: does the source have output
 * @is_dynamic: is this a dynamic source
 *
 * Check the source of @play_base_bin and collect information about it.
 *
 * @is_raw will be set to TRUE if the source only produces raw pads. When this
 * function returns, all of the raw pad of the source will be added
 * to @play_base_bin.
 *
 * @have_out: will be set to TRUE if the source has output pads.
 *
 * @is_dynamic: TRUE if the element will create (more) pads dynamically later
 * on.
 *
 * Returns: FALSE if a fatal error occured while scanning.
 */
static gboolean
analyse_source (GstPlayBaseBin * play_base_bin, gboolean * is_raw,
    gboolean * have_out, gboolean * is_dynamic)
{
  GstIterator *pads_iter;
  gboolean done = FALSE;
  gboolean res = TRUE;

  *have_out = FALSE;
  *is_raw = FALSE;
  *is_dynamic = FALSE;

  pads_iter = gst_element_iterate_src_pads (play_base_bin->source);
  while (!done) {
    GstPad *pad = NULL;

    switch (gst_iterator_next (pads_iter, (gpointer) & pad)) {
      case GST_ITERATOR_ERROR:
        res = FALSE;
        /* FALLTROUGH */
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
      case GST_ITERATOR_RESYNC:
        /* reset results and resync */
        *have_out = FALSE;
        *is_raw = FALSE;
        *is_dynamic = FALSE;
        gst_iterator_resync (pads_iter);
        break;
      case GST_ITERATOR_OK:
        /* we now officially have an output pad */
        *have_out = TRUE;

        /* if FALSE, this pad has no caps and we continue with the next pad. */
        if (!has_all_raw_caps (pad, is_raw)) {
          gst_object_unref (pad);
          break;
        }

        /* caps on source pad are all raw, we can add the pad */
        if (*is_raw) {
          new_decoded_pad_full (play_base_bin->source, pad, FALSE,
              play_base_bin, FALSE);
        }

        gst_object_unref (pad);
        break;
    }
  }
  gst_iterator_free (pads_iter);

  if (!*have_out) {
    GstElementClass *elemclass;
    GList *walk;

    /* element has no output pads, check for padtemplates that list SOMETIMES
     * pads. */
    elemclass = GST_ELEMENT_GET_CLASS (play_base_bin->source);

    walk = gst_element_class_get_pad_template_list (elemclass);
    while (walk != NULL) {
      GstPadTemplate *templ;

      templ = (GstPadTemplate *) walk->data;
      if (GST_PAD_TEMPLATE_DIRECTION (templ) == GST_PAD_SRC) {
        if (GST_PAD_TEMPLATE_PRESENCE (templ) == GST_PAD_SOMETIMES) {
          *is_dynamic = TRUE;
          break;                /* only break out if we found a sometimes src pad
                                   continue walking through if say a request src pad is found
                                   elements such as mpegtsparse and dvbbasebin have request
                                   and sometimes src pads */
        }
      }
      walk = g_list_next (walk);
    }
  }

  return res;
}

static void
remove_decoders (GstPlayBaseBin * bin)
{
  GSList *walk;

  for (walk = bin->decoders; walk; walk = g_slist_next (walk)) {
    GstElement *decoder = GST_ELEMENT_CAST (walk->data);

    GST_DEBUG_OBJECT (bin, "removing old decoder element");
    /* Disconnect all the signal handlers we attached to the decodebin before
     * we dispose of it */
    g_signal_handlers_disconnect_by_func (decoder,
        (gpointer) (decodebin_element_added_cb), bin);
    g_signal_handlers_disconnect_by_func (decoder,
        (gpointer) (decodebin_element_removed_cb), bin);
    g_signal_handlers_disconnect_by_func (decoder,
        (gpointer) (new_decoded_pad), bin);
    g_signal_handlers_disconnect_by_func (decoder,
        (gpointer) (no_more_pads), bin);
    g_signal_handlers_disconnect_by_func (decoder,
        (gpointer) (unknown_type), bin);

    gst_element_set_state (decoder, GST_STATE_NULL);
    gst_bin_remove (GST_BIN_CAST (bin), decoder);
  }
  g_slist_free (bin->decoders);
  bin->decoders = NULL;
}

static GstElement *
make_decoder (GstPlayBaseBin * play_base_bin)
{
  GstElement *decoder;

  /* now create the decoder element */
  if (g_getenv ("USE_DECODEBIN2"))
    decoder = gst_element_factory_make ("decodebin2", NULL);
  else
    decoder = gst_element_factory_make ("decodebin", NULL);
  if (!decoder)
    goto no_decodebin;

  g_signal_connect (decoder, "element-added",
      G_CALLBACK (decodebin_element_added_cb), play_base_bin);
  g_signal_connect (decoder, "element-removed",
      G_CALLBACK (decodebin_element_removed_cb), play_base_bin);

  gst_bin_add (GST_BIN_CAST (play_base_bin), decoder);

  /* set up callbacks to create the links between decoded data
   * and video/audio/subtitle rendering/output. */
  g_signal_connect (G_OBJECT (decoder),
      "new-decoded-pad", G_CALLBACK (new_decoded_pad), play_base_bin);
  g_signal_connect (G_OBJECT (decoder), "no-more-pads",
      G_CALLBACK (no_more_pads), play_base_bin);
  g_signal_connect (G_OBJECT (decoder),
      "unknown-type", G_CALLBACK (unknown_type), play_base_bin);
  g_object_set_data (G_OBJECT (decoder), "pending", GINT_TO_POINTER (1));
  play_base_bin->pending++;

  GST_DEBUG_OBJECT (play_base_bin, "created decodebin, %d pending",
      play_base_bin->pending);

  play_base_bin->decoders = g_slist_prepend (play_base_bin->decoders, decoder);

  return decoder;

  /* ERRORS */
no_decodebin:
  {
    GST_ELEMENT_ERROR (play_base_bin, CORE, MISSING_PLUGIN,
        (_("Could not create \"decodebin\" element.")), (NULL));
    return NULL;
  }
}

static void
remove_source (GstPlayBaseBin * bin)
{
  GstElement *source = bin->source;

  if (source) {
    GST_DEBUG_OBJECT (bin, "removing old src element");
    gst_element_set_state (source, GST_STATE_NULL);

    if (bin->src_np_sig_id) {
      g_signal_handler_disconnect (G_OBJECT (source), bin->src_np_sig_id);
      bin->src_np_sig_id = 0;
    }
    if (bin->src_nmp_sig_id) {
      g_signal_handler_disconnect (G_OBJECT (source), bin->src_nmp_sig_id);
      bin->src_nmp_sig_id = 0;
    }
    gst_bin_remove (GST_BIN_CAST (bin), source);
    bin->source = NULL;
  }
}

static GstBusSyncReply
subbin_startup_sync_msg (GstBus * bus, GstMessage * msg, gpointer user_data)
{
  if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR) {
    GstPlayBaseBin *play_base_bin;

    play_base_bin = GST_PLAY_BASE_BIN (user_data);
    if (!play_base_bin->subtitle_done) {
      GST_WARNING_OBJECT (play_base_bin, "error starting up subtitle bin: %"
          GST_PTR_FORMAT, msg);
      play_base_bin->subtitle_done = TRUE;
      GST_DEBUG_OBJECT (play_base_bin, "signal group done");
      GROUP_SIGNAL (play_base_bin);
      GST_DEBUG_OBJECT (play_base_bin, "signaled group done");
    }
  }
  return GST_BUS_PASS;
}

/* construct and run the source and decoder elements until we found
 * all the streams or until a preroll queue has been filled.
*/
static gboolean
setup_source (GstPlayBaseBin * play_base_bin)
{
  GstElement *subbin = NULL;
  gboolean is_raw, have_out, is_dynamic;

  if (!play_base_bin->need_rebuild)
    return TRUE;
  play_base_bin->raw_decoding_mode = FALSE;

  GST_DEBUG_OBJECT (play_base_bin, "setup source");

  /* delete old src */
  remove_source (play_base_bin);

  /* create and configure an element that can handle the uri */
  if (!(play_base_bin->source = gen_source_element (play_base_bin, &subbin)))
    goto no_source;

  /* state will be merged later - if file is not found, error will be
   * handled by the application right after. */
  gst_bin_add (GST_BIN_CAST (play_base_bin), play_base_bin->source);
  g_object_notify (G_OBJECT (play_base_bin), "source");

  /* remove the old decoders now, if any */
  remove_decoders (play_base_bin);

  /* remove our previous preroll queues */
  remove_groups (play_base_bin);

  /* clear pending dynamic elements */
  play_base_bin->pending = 0;

  /* do subs */
  if (subbin) {
    GstElement *db;
    GstBus *bus;

    play_base_bin->subtitle = subbin;
    db = gst_bin_get_by_name (GST_BIN_CAST (subbin), "subtitle-decoder");

    /* do type detection, without adding (so no preroll) */
    g_signal_connect (G_OBJECT (db), "new-decoded-pad",
        G_CALLBACK (subs_new_decoded_pad), play_base_bin);
    g_signal_connect (G_OBJECT (db), "no-more-pads",
        G_CALLBACK (sub_no_more_pads), play_base_bin);
    g_signal_connect (G_OBJECT (db), "unknown-type",
        G_CALLBACK (unknown_type), play_base_bin);
    g_object_set_data (G_OBJECT (db), "pending", GINT_TO_POINTER (1));
    play_base_bin->pending++;

    GST_DEBUG_OBJECT (play_base_bin, "we have subtitles, %d pending",
        play_base_bin->pending);

    if (!play_base_bin->is_stream) {
      GstStateChangeReturn sret;

      /* either when the queues are filled or when the decoder element
       * has no more dynamic streams, the cond is unlocked. We can remove
       * the signal handlers then
       */
      GST_DEBUG_OBJECT (play_base_bin, "starting subtitle bin");

      /* for subtitles in a separate bin we will not commit the
       * current building group since we need to add the other
       * audio/video streams to the group. We check if we managed
       * to commit the subtitle group using an extra flag. */
      play_base_bin->subtitle_done = FALSE;

      /* since subbin is still a stand-alone bin, we need to add a custom bus
       * to intercept error messages, so we can stop waiting and continue */
      bus = gst_bus_new ();
      gst_element_set_bus (subbin, bus);
      gst_bus_set_sync_handler (bus, subbin_startup_sync_msg, play_base_bin);

      sret = gst_element_set_state (subbin, GST_STATE_PAUSED);
      if (sret != GST_STATE_CHANGE_FAILURE) {
        GROUP_LOCK (play_base_bin);
        GST_DEBUG ("waiting for subtitle to complete...");
        while (!play_base_bin->subtitle_done)
          GROUP_WAIT (play_base_bin);
        GST_DEBUG ("group done !");
        GROUP_UNLOCK (play_base_bin);

        if (!play_base_bin->building_group ||
            play_base_bin->building_group->type[GST_STREAM_TYPE_TEXT -
                1].npads == 0) {

          GST_DEBUG ("No subtitle found - ignoring");
          gst_element_set_state (subbin, GST_STATE_NULL);
          gst_object_unref (play_base_bin->subtitle);
          play_base_bin->subtitle = NULL;
        } else {
          GST_DEBUG_OBJECT (play_base_bin, "Subtitle set-up successful");
        }
      } else {
        GST_WARNING_OBJECT (play_base_bin, "Failed to start subtitle bin");
        gst_element_set_state (subbin, GST_STATE_NULL);
        gst_object_unref (play_base_bin->subtitle);
        play_base_bin->subtitle = NULL;
      }

      gst_bus_set_sync_handler (bus, NULL, NULL);
      gst_element_set_bus (subbin, NULL);
      gst_object_unref (bus);
    }
    gst_object_unref (db);
  }
  /* see if the source element emits raw audio/video all by itself,
   * if so, we can create streams for the pads and be done with it.
   * Also check that is has source pads, if not, we assume it will
   * do everything itself.  */
  if (!analyse_source (play_base_bin, &is_raw, &have_out, &is_dynamic))
    goto invalid_source;

  if (is_raw) {
    GST_DEBUG_OBJECT (play_base_bin, "Source provides all raw data");
    /* source provides raw data, we added the pads and we can now signal a
     * no_more pads because we are done. */
    group_commit (play_base_bin, play_base_bin->is_stream, FALSE);
    return TRUE;
  }
  if (!have_out && !is_dynamic) {
    GST_DEBUG_OBJECT (play_base_bin, "Source has no output pads");
    /* create a stream to indicate that this uri is handled by a self
     * contained element. We are now done. */
    add_element_stream (play_base_bin->source, play_base_bin);
    group_commit (play_base_bin, play_base_bin->is_stream, FALSE);
    return TRUE;
  }
  if (is_dynamic) {
    /* connect a handler for the new-pad signal */
    play_base_bin->src_np_sig_id =
        g_signal_connect (G_OBJECT (play_base_bin->source), "pad-added",
        G_CALLBACK (source_new_pad), play_base_bin);
    play_base_bin->src_nmp_sig_id =
        g_signal_connect (G_OBJECT (play_base_bin->source), "no-more-pads",
        G_CALLBACK (source_no_more_pads), play_base_bin);
    g_object_set_data (G_OBJECT (play_base_bin->source), "pending",
        GINT_TO_POINTER (1));
    play_base_bin->pending++;
    GST_DEBUG_OBJECT (play_base_bin,
        "Source has dynamic output pads, %d pending", play_base_bin->pending);
  } else {
    GstElement *decoder;

    /* no dynamic source, we can link now */
    decoder = make_decoder (play_base_bin);
    if (!decoder)
      goto no_decodebin;

    if (!gst_element_link (play_base_bin->source, decoder))
      goto could_not_link;
  }

  if (play_base_bin->subtitle)
    gst_bin_add (GST_BIN_CAST (play_base_bin), play_base_bin->subtitle);

  play_base_bin->need_rebuild = FALSE;

  return TRUE;

  /* ERRORS */
no_source:
  {
    /* error message was already posted */
    return FALSE;
  }
invalid_source:
  {
    GST_ELEMENT_ERROR (play_base_bin, CORE, FAILED,
        (_("Source element is invalid.")), (NULL));
    return FALSE;
  }
no_decodebin:
  {
    /* message was posted */
    return FALSE;
  }
could_not_link:
  {
    GST_ELEMENT_ERROR (play_base_bin, CORE, NEGOTIATION,
        (NULL), ("Can't link source to decoder element"));
    return FALSE;
  }
}

static void
finish_source (GstPlayBaseBin * play_base_bin)
{
  /* FIXME: no need to grab the group lock here? (tpm) */
  if (get_active_group (play_base_bin) != NULL) {
    if (play_base_bin->subtitle) {
      /* make subs iterate from now on */
      gst_bin_add (GST_BIN_CAST (play_base_bin), play_base_bin->subtitle);
    }
  }
}

/*
 * Caller must have group-lock held.
 *
 * We iterate over all detected streams in the streaminfo and try to find
 * impossible cases, like subtitles without video.
 */
static gboolean
prepare_output (GstPlayBaseBin * play_base_bin)
{
  const GList *item;
  gboolean stream_found = FALSE, no_media = FALSE;
  gboolean got_video = FALSE, got_subtitle = FALSE;
  GstPlayBaseGroup *group;

  group = get_active_group (play_base_bin);

  /* check if we found any supported stream... if not, then
   * we detected stream type (or the above would've failed),
   * but linking/decoding failed - plugin probably missing. */
  for (item = group ? group->streaminfo : NULL; item != NULL; item = item->next) {
    GstStreamInfo *info = GST_STREAM_INFO (item->data);

    if (info->type == GST_STREAM_TYPE_VIDEO) {
      stream_found = TRUE;
      got_video = TRUE;
      break;
    } else if (info->type == GST_STREAM_TYPE_ELEMENT) {
      stream_found = TRUE;
    } else if (info->type == GST_STREAM_TYPE_AUDIO) {
      stream_found = TRUE;
    } else if (info->type == GST_STREAM_TYPE_TEXT ||
        info->type == GST_STREAM_TYPE_SUBPICTURE) {
      got_subtitle = TRUE;
    } else if (!item->prev && !item->next) {
      /* We're no audio/video and the only stream... We could
       * be something not-media that's detected because then our
       * typefind doesn't mess up with mp3 (bz2, gz, elf, ...) */
      if (info->caps && !gst_caps_is_empty (info->caps)) {
        const gchar *mime =
            gst_structure_get_name (gst_caps_get_structure (info->caps, 0));

        no_media = IS_NO_MEDIA_MIME (mime);
      }
    }
  }

  if (!stream_found) {
    if (got_subtitle) {
      GST_ELEMENT_ERROR (play_base_bin, STREAM, WRONG_TYPE,
          (_("Only a subtitle stream was detected. "
                  "Either you are loading a subtitle file or some other type of "
                  "text file, or the media file was not recognized.")), (NULL));
    } else if (!no_media) {
      GST_ELEMENT_ERROR (play_base_bin, STREAM, CODEC_NOT_FOUND,
          (_("You do not have a decoder installed to handle this file. "
                  "You might need to install the necessary plugins.")), (NULL));
    } else {
      GST_ELEMENT_ERROR (play_base_bin, STREAM, WRONG_TYPE,
          (_("This is not a media file")), (NULL));
    }
    return FALSE;
  } else if (got_subtitle && !got_video) {
    GST_ELEMENT_ERROR (play_base_bin, STREAM, WRONG_TYPE,
        (_("A subtitle stream was detected, but no video stream.")), (NULL));
    return FALSE;
  }

  return TRUE;
}

/*
 * Multi-stream management. -1 = none.
 *
 * Caller has group-lock held.
 */
static gint
get_active_source (GstPlayBaseBin * play_base_bin, GstStreamType type)
{
  GstPlayBaseGroup *group;
  GList *s;
  gint num = 0;

  group = get_active_group (play_base_bin);
  if (!group)
    return -1;

  for (s = group->streaminfo; s; s = s->next) {
    GstStreamInfo *info = s->data;

    if (info->type == type) {
      if (!info->mute && !g_object_get_data (G_OBJECT (info), "mute_probe")) {
        return num;
      } else {
        num++;
      }
    }
  }

  return -1;
}

/* Kill pad reactivation on state change. */

#if 0
static void muted_group_change_state (GstElement * element,
    gint old_state, gint new_state, gpointer data);
#endif

static void
mute_group_type (GstPlayBaseGroup * group, GstStreamType type, gboolean mute)
{
  gboolean active = !mute;
  GstPad *pad;

  pad = gst_element_get_static_pad (group->type[type - 1].preroll, "src");
  gst_pad_set_active (pad, active);
  gst_object_unref (pad);
  pad = gst_element_get_static_pad (group->type[type - 1].preroll, "sink");
  gst_pad_set_active (pad, active);
  gst_object_unref (pad);
  pad = gst_element_get_static_pad (group->type[type - 1].selector, "src");
  gst_pad_set_active (pad, active);
  gst_object_unref (pad);

#if 0
  if (mute) {
    g_signal_connect (group->type[type - 1].preroll, "state-changed",
        G_CALLBACK (muted_group_change_state), group);
  } else {
    g_signal_handlers_disconnect_by_func (group->type[type - 1].preroll,
        G_CALLBACK (muted_group_change_state), group);
  }
#endif
}

#if 0
static void
muted_group_change_state (GstElement * element,
    gint old_state, gint new_state, gpointer data)
{
  GstPlayBaseGroup *group = data;

  GROUP_LOCK (group->bin);

  if (new_state == GST_STATE_PLAYING) {
    gint n;

    for (n = 0; n < NUM_TYPES; n++) {
      if (group->type[n].selector == element) {
        mute_group_type (group, n + 1, TRUE);
      }
    }
  }

  GROUP_UNLOCK (group->bin);
}
#endif

static void
set_subtitles_visible (GstPlayBaseBin * play_base_bin, gboolean visible)
{
  GstPlayBaseBinClass *klass = GST_PLAY_BASE_BIN_GET_CLASS (play_base_bin);

  /* we use a vfunc for this since we don't have a reference to the
   * textoverlay element, but playbin does */
  if (klass != NULL && klass->set_subtitles_visible != NULL)
    klass->set_subtitles_visible (play_base_bin, visible);
}

static void
set_audio_mute (GstPlayBaseBin * play_base_bin, gboolean mute)
{
  GstPlayBaseBinClass *klass = GST_PLAY_BASE_BIN_GET_CLASS (play_base_bin);

  /* we use a vfunc for this since we don't have a reference to the
   * textoverlay element, but playbin does */
  if (klass != NULL && klass->set_audio_mute != NULL)
    klass->set_audio_mute (play_base_bin, mute);
}

/*
 * Caller has group-lock held.
 */

static void
set_active_source (GstPlayBaseBin * play_base_bin,
    GstStreamType type, gint source_num)
{
  GstPlayBaseGroup *group;
  GList *s;
  gint num = 0;
  gboolean have_active = FALSE;
  GstElement *sel;

  GST_LOG ("Changing active source of type %d to %d", type, source_num);
  play_base_bin->current[type - 1] = source_num;

  group = get_active_group (play_base_bin);
  if (!group || !group->type[type - 1].preroll) {
    GST_LOG ("No active group, or group for type %d has no preroll", type);
    return;
  }

  /* HACK: instead of unlinking the subtitle input (= lots of hassle,
   * especially if subtitles come from an external source), just tell
   * textoverlay not to render them */
  if (type == GST_STREAM_TYPE_TEXT) {
    gboolean visible = (source_num != -1);

    set_subtitles_visible (play_base_bin, visible);
    if (!visible)
      return;
  } else if (type == GST_STREAM_TYPE_AUDIO) {
    gboolean mute = (source_num == -1);

    set_audio_mute (play_base_bin, mute);

    if (mute)
      return;
  }

  sel = group->type[type - 1].selector;

  for (s = group->streaminfo; s; s = s->next) {
    GstStreamInfo *info = s->data;

    if (info->type == type) {
      if (num == source_num) {
        GstPad *sel_pad;

        GST_LOG ("Unmuting (if already muted) source %d of type %d", source_num,
            type);
        g_object_set (info, "mute", FALSE, NULL);

        /* Tell the stream selector which pad to accept */
        sel_pad = GST_PAD_CAST (g_object_get_data (G_OBJECT (info->object),
                "pb_sel_pad"));

        if (sel && sel_pad != NULL) {
          g_object_set (G_OBJECT (sel), "active-pad", sel_pad, NULL);
        }

        have_active = TRUE;
      } else {
        guint id;

        GST_LOG_OBJECT (info->object, "Muting source %d of type %d", num, type);

        id = gst_pad_add_buffer_probe (GST_PAD_CAST (info->object),
            G_CALLBACK (mute_stream), info);
        g_object_set_data (G_OBJECT (info), "mute_probe", GINT_TO_POINTER (id));
      }
      num++;
    }
  }

  if (!have_active) {
    GST_LOG ("Muting group type: %d", type);
    g_object_set (sel, "active-pad", NULL, NULL);
  } else {
    GST_LOG ("Unmuting group type: %d", type);
  }
  mute_group_type (group, type, !have_active);
}

static void
gst_play_base_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstPlayBaseBin *play_base_bin;

  g_return_if_fail (GST_IS_PLAY_BASE_BIN (object));

  play_base_bin = GST_PLAY_BASE_BIN (object);

  switch (prop_id) {
    case ARG_URI:
    {
      const gchar *uri = g_value_get_string (value);

      if (uri == NULL) {
        g_warning ("cannot set NULL uri");
        return;
      }
      /* if we have no previous uri, or the new uri is different from the
       * old one, replug */
      if (play_base_bin->uri == NULL || strcmp (play_base_bin->uri, uri) != 0) {
        g_free (play_base_bin->uri);
        play_base_bin->uri = g_strdup (uri);

        GST_DEBUG ("setting new uri to %s", uri);

        play_base_bin->need_rebuild = TRUE;
      }
      break;
    }
    case ARG_SUBURI:{
      const gchar *suburi = g_value_get_string (value);

      if ((!suburi && !play_base_bin->suburi) ||
          (suburi && play_base_bin->suburi &&
              !strcmp (play_base_bin->suburi, suburi)))
        return;
      g_free (play_base_bin->suburi);
      play_base_bin->suburi = g_strdup (suburi);
      GST_DEBUG ("setting new .sub uri to %s", suburi);
      play_base_bin->need_rebuild = TRUE;
      break;
    }
    case ARG_QUEUE_SIZE:
      play_base_bin->queue_size = g_value_get_uint64 (value);
      break;
    case ARG_QUEUE_THRESHOLD:
      play_base_bin->queue_threshold = g_value_get_uint64 (value);
      break;
    case ARG_QUEUE_MIN_THRESHOLD:
      play_base_bin->queue_min_threshold = g_value_get_uint64 (value);
      break;
    case ARG_CONNECTION_SPEED:
      play_base_bin->connection_speed = g_value_get_uint (value) * 1000;
      break;
    case ARG_VIDEO:
      GROUP_LOCK (play_base_bin);
      set_active_source (play_base_bin,
          GST_STREAM_TYPE_VIDEO, g_value_get_int (value));
      GROUP_UNLOCK (play_base_bin);
      break;
    case ARG_AUDIO:
      GROUP_LOCK (play_base_bin);
      set_active_source (play_base_bin,
          GST_STREAM_TYPE_AUDIO, g_value_get_int (value));
      GROUP_UNLOCK (play_base_bin);
      break;
    case ARG_TEXT:
      GROUP_LOCK (play_base_bin);
      set_active_source (play_base_bin,
          GST_STREAM_TYPE_TEXT, g_value_get_int (value));
      GROUP_UNLOCK (play_base_bin);
      break;
    case ARG_SUBTITLE_ENCODING:
    {
      const gchar *encoding;
      GSList *list;

      encoding = g_value_get_string (value);
      if (encoding && play_base_bin->subencoding &&
          !strcmp (play_base_bin->subencoding, encoding)) {
        return;
      }
      if (encoding == NULL && play_base_bin->subencoding == NULL)
        return;

      g_mutex_lock (play_base_bin->sub_lock);
      g_free (play_base_bin->subencoding);
      play_base_bin->subencoding = g_strdup (encoding);
      list = g_slist_copy (play_base_bin->subtitle_elements);
      g_slist_foreach (list, (GFunc) gst_object_ref, NULL);
      g_mutex_unlock (play_base_bin->sub_lock);

      /* we can't hold a lock when calling g_object_set() on a child, since
       * the notify event will trigger GstObject to send a deep-notify event
       * which will try to take the lock ... */
      g_slist_foreach (list, (GFunc) set_encoding_element, (gpointer) encoding);
      g_slist_foreach (list, (GFunc) gst_object_unref, NULL);
      g_slist_free (list);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_play_base_bin_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstPlayBaseBin *play_base_bin;

  g_return_if_fail (GST_IS_PLAY_BASE_BIN (object));

  play_base_bin = GST_PLAY_BASE_BIN (object);

  switch (prop_id) {
    case ARG_URI:
      g_value_set_string (value, play_base_bin->uri);
      break;
    case ARG_SUBURI:
      g_value_set_string (value, play_base_bin->suburi);
      break;
    case ARG_NSTREAMS:
    {
      GstPlayBaseGroup *group;

      GROUP_LOCK (play_base_bin);
      group = get_active_group (play_base_bin);
      if (group) {
        g_value_set_int (value, group->nstreams);
      } else {
        g_value_set_int (value, 0);
      }
      GROUP_UNLOCK (play_base_bin);
      break;
    }
    case ARG_QUEUE_SIZE:
      g_value_set_uint64 (value, play_base_bin->queue_size);
      break;
    case ARG_QUEUE_THRESHOLD:
      g_value_set_uint64 (value, play_base_bin->queue_threshold);
      break;
    case ARG_QUEUE_MIN_THRESHOLD:
      g_value_set_uint64 (value, play_base_bin->queue_min_threshold);
      break;
    case ARG_CONNECTION_SPEED:
      g_value_set_uint (value, play_base_bin->connection_speed / 1000);
      break;
    case ARG_STREAMINFO:
      /* FIXME: hold some kind of lock here, use iterator */
      g_value_set_pointer (value,
          (gpointer) gst_play_base_bin_get_streaminfo (play_base_bin));
      break;
    case ARG_STREAMINFO_VALUES:{
      GValueArray *copy;

      copy = gst_play_base_bin_get_streaminfo_value_array (play_base_bin);
      g_value_take_boxed (value, copy);
      break;
    }
    case ARG_SOURCE:
      g_value_set_object (value, play_base_bin->source);
      break;
    case ARG_VIDEO:
      GROUP_LOCK (play_base_bin);
      g_value_set_int (value, get_active_source (play_base_bin,
              GST_STREAM_TYPE_VIDEO));
      GROUP_UNLOCK (play_base_bin);
      break;
    case ARG_AUDIO:
      GROUP_LOCK (play_base_bin);
      g_value_set_int (value, get_active_source (play_base_bin,
              GST_STREAM_TYPE_AUDIO));
      GROUP_UNLOCK (play_base_bin);
      break;
    case ARG_TEXT:
      GROUP_LOCK (play_base_bin);
      g_value_set_int (value, get_active_source (play_base_bin,
              GST_STREAM_TYPE_TEXT));
      GROUP_UNLOCK (play_base_bin);
      break;
    case ARG_SUBTITLE_ENCODING:
      GST_OBJECT_LOCK (play_base_bin);
      g_value_set_string (value, play_base_bin->subencoding);
      GST_OBJECT_UNLOCK (play_base_bin);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_play_base_bin_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstPlayBaseBin *play_base_bin;

  play_base_bin = GST_PLAY_BASE_BIN (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (!setup_source (play_base_bin))
        goto source_failed;
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (ret == GST_STATE_CHANGE_FAILURE)
        goto cleanup_groups;

      finish_source (play_base_bin);
      break;
      /* clean-up in both cases, READY=>NULL clean-up is if there was an error */
    case GST_STATE_CHANGE_PAUSED_TO_READY:
    case GST_STATE_CHANGE_READY_TO_NULL:
      play_base_bin->need_rebuild = TRUE;
      remove_decoders (play_base_bin);
      remove_groups (play_base_bin);
      remove_source (play_base_bin);
      break;
    default:
      break;
  }
  return ret;

  /* ERRORS */
source_failed:
  {
    play_base_bin->need_rebuild = TRUE;

    return GST_STATE_CHANGE_FAILURE;
  }
cleanup_groups:
  {
    /* clean up leftover groups */
    remove_groups (play_base_bin);
    play_base_bin->need_rebuild = TRUE;

    return GST_STATE_CHANGE_FAILURE;
  }
}

static const GList *
gst_play_base_bin_get_streaminfo (GstPlayBaseBin * play_base_bin)
{
  GstPlayBaseGroup *group = get_active_group (play_base_bin);
  GList *info = NULL;

  if (group) {
    info = group->streaminfo;
  }
  return info;
}

static GValueArray *
gst_play_base_bin_get_streaminfo_value_array (GstPlayBaseBin * play_base_bin)
{
  GstPlayBaseGroup *group;
  GValueArray *array = NULL;

  GROUP_LOCK (play_base_bin);
  group = get_active_group (play_base_bin);
  if (group) {
    array = g_value_array_copy (group->streaminfo_value_array);
  } else {
    array = g_value_array_new (0);
  }
  GROUP_UNLOCK (play_base_bin);

  return array;
}
