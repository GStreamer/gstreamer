/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

#include <string.h>
#include "gstplaybasebin.h"
#include "gststreamselector.h"
#include "gstplay-marshal.h"

GST_DEBUG_CATEGORY_STATIC (gst_play_base_bin_debug);
#define GST_CAT_DEFAULT gst_play_base_bin_debug

#define DEFAULT_QUEUE_THRESHOLD (2 * GST_SECOND)
#define DEFAULT_QUEUE_SIZE  (3 * GST_SECOND)

/* props */
enum
{
  ARG_0,
  ARG_URI,
  ARG_SUBURI,
  ARG_QUEUE_SIZE,
  ARG_QUEUE_THRESHOLD,
  ARG_NSTREAMS,
  ARG_STREAMINFO,
  ARG_SOURCE,
  ARG_VIDEO,
  ARG_AUDIO,
  ARG_TEXT
};

/* signals */
enum
{
  SETUP_OUTPUT_PADS_SIGNAL,
  REMOVED_OUTPUT_PAD_SIGNAL,
  BUFFERING_SIGNAL,
  GROUP_SWITCH_SIGNAL,
  LINK_STREAM_SIGNAL,
  UNLINK_STREAM_SIGNAL,
  REDIRECT,
  LAST_SIGNAL
};

static void gst_play_base_bin_class_init (GstPlayBaseBinClass * klass);
static void gst_play_base_bin_init (GstPlayBaseBin * play_base_bin);
static void gst_play_base_bin_dispose (GObject * object);

static void gst_play_base_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * spec);
static void gst_play_base_bin_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * spec);
static GstElementStateReturn gst_play_base_bin_change_state (GstElement *
    element);

static gboolean gst_play_base_bin_add_element (GstBin * bin,
    GstElement * element);
static gboolean gst_play_base_bin_remove_element (GstBin * bin,
    GstElement * element);

extern GstElementStateReturn gst_element_set_state_func (GstElement * element,
    GstElementState state);

static void gst_play_base_bin_error (GstElement * element,
    GstElement * source, GError * error, gchar * debug, gpointer data);
static void gst_play_base_bin_found_tag (GstElement * element,
    GstElement * source, const GstTagList * taglist, gpointer data);

static void set_active_source (GstPlayBaseBin * play_base_bin,
    GstStreamType type, gint source_num);
static gboolean probe_triggered (GstProbe * probe, GstData ** data,
    gpointer user_data);
static void setup_substreams (GstPlayBaseBin * play_base_bin);

static GstElementClass *element_class;
static GstElementClass *parent_class;
static guint gst_play_base_bin_signals[LAST_SIGNAL] = { 0 };

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

    gst_play_base_bin_type = g_type_register_static (GST_TYPE_BIN,
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

  element_class = g_type_class_ref (gst_element_get_type ());
  parent_class = g_type_class_ref (gst_bin_get_type ());

  gobject_klass->set_property = gst_play_base_bin_set_property;
  gobject_klass->get_property = gst_play_base_bin_get_property;

  g_object_class_install_property (gobject_klass, ARG_URI,
      g_param_spec_string ("uri", "URI", "URI of the media to play",
          NULL, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_klass, ARG_SUBURI,
      g_param_spec_string ("suburi", ".sub-URI", "Optional URI of a subtitle",
          NULL, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_klass, ARG_QUEUE_SIZE,
      g_param_spec_uint64 ("queue-size", "Queue size",
          "Size of internal queues in nanoseconds", 0, G_MAXINT64,
          DEFAULT_QUEUE_SIZE, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_klass, ARG_QUEUE_THRESHOLD,
      g_param_spec_uint64 ("queue-threshold", "Queue threshold",
          "Buffering threshold of internal queues in nanoseconds", 0,
          G_MAXINT64, DEFAULT_QUEUE_THRESHOLD, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_klass, ARG_NSTREAMS,
      g_param_spec_int ("nstreams", "NStreams", "number of streams",
          0, G_MAXINT, 0, G_PARAM_READABLE));
  g_object_class_install_property (gobject_klass, ARG_STREAMINFO,
      g_param_spec_pointer ("stream-info", "Stream info", "List of streaminfo",
          G_PARAM_READABLE));
  g_object_class_install_property (gobject_klass, ARG_SOURCE,
      g_param_spec_object ("source", "Source", "Source element",
          GST_TYPE_ELEMENT, G_PARAM_READABLE));

  g_object_class_install_property (gobject_klass, ARG_VIDEO,
      g_param_spec_int ("current-video", "Current video",
          "Currently playing video stream (-1 = none)",
          -1, G_MAXINT, -1, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_klass, ARG_AUDIO,
      g_param_spec_int ("current-audio", "Current audio",
          "Currently playing audio stream (-1 = none)",
          -1, G_MAXINT, -1, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_klass, ARG_TEXT,
      g_param_spec_int ("current-text", "Current text",
          "Currently playing text stream (-1 = none)",
          -1, G_MAXINT, -1, G_PARAM_READWRITE));

  GST_DEBUG_CATEGORY_INIT (gst_play_base_bin_debug, "playbasebin", 0,
      "playbasebin");

  /* signals */
  gst_play_base_bin_signals[SETUP_OUTPUT_PADS_SIGNAL] =
      g_signal_new ("setup-output-pads", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstPlayBaseBinClass, setup_output_pads),
      NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
  gst_play_base_bin_signals[REMOVED_OUTPUT_PAD_SIGNAL] =
      g_signal_new ("removed-output-pad", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstPlayBaseBinClass, removed_output_pad),
      NULL, NULL, gst_marshal_VOID__OBJECT, G_TYPE_NONE, 1, G_TYPE_OBJECT);
  gst_play_base_bin_signals[BUFFERING_SIGNAL] =
      g_signal_new ("buffering", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstPlayBaseBinClass, buffering),
      NULL, NULL, g_cclosure_marshal_VOID__INT, G_TYPE_NONE, 1, G_TYPE_INT);
  gst_play_base_bin_signals[GROUP_SWITCH_SIGNAL] =
      g_signal_new ("group-switch", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstPlayBaseBinClass, group_switch),
      NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
  gst_play_base_bin_signals[REDIRECT] =
      g_signal_new ("got-redirect", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstPlayBaseBinClass, got_redirect),
      NULL, NULL, g_cclosure_marshal_VOID__STRING, G_TYPE_NONE, 1,
      G_TYPE_STRING);

  /* action signals */
  gst_play_base_bin_signals[LINK_STREAM_SIGNAL] =
      g_signal_new ("link-stream", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (GstPlayBaseBinClass, link_stream),
      NULL, NULL, gst_play_marshal_BOOLEAN__OBJECT_OBJECT, G_TYPE_BOOLEAN, 2,
      G_TYPE_OBJECT, GST_TYPE_PAD);
  gst_play_base_bin_signals[UNLINK_STREAM_SIGNAL] =
      g_signal_new ("unlink-stream", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (GstPlayBaseBinClass, unlink_stream),
      NULL, NULL, gst_marshal_VOID__OBJECT, G_TYPE_NONE, 1, G_TYPE_OBJECT);

  gobject_klass->dispose = GST_DEBUG_FUNCPTR (gst_play_base_bin_dispose);

  /* we handle state changes like an element */
  gstelement_klass->change_state =
      GST_DEBUG_FUNCPTR (gst_play_base_bin_change_state);

  gstbin_klass->add_element = GST_DEBUG_FUNCPTR (gst_play_base_bin_add_element);
  gstbin_klass->remove_element =
      GST_DEBUG_FUNCPTR (gst_play_base_bin_remove_element);

  klass->link_stream = gst_play_base_bin_link_stream;
  klass->unlink_stream = gst_play_base_bin_unlink_stream;
}

static void
gst_play_base_bin_init (GstPlayBaseBin * play_base_bin)
{
  play_base_bin->uri = NULL;
  play_base_bin->suburi = NULL;
  play_base_bin->need_rebuild = TRUE;
  play_base_bin->is_stream = FALSE;
  play_base_bin->source = NULL;
  play_base_bin->decoder = NULL;
  play_base_bin->subtitle = NULL;

  play_base_bin->group_lock = g_mutex_new ();
  play_base_bin->group_cond = g_cond_new ();

  play_base_bin->building_group = NULL;
  play_base_bin->queued_groups = NULL;

  play_base_bin->queue_size = DEFAULT_QUEUE_SIZE;
  play_base_bin->queue_threshold = DEFAULT_QUEUE_THRESHOLD;
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

  if (G_OBJECT_CLASS (parent_class)->dispose) {
    G_OBJECT_CLASS (parent_class)->dispose (object);
  }
}

static GstPlayBaseGroup *
group_create (GstPlayBaseBin * play_base_bin)
{
  GstPlayBaseGroup *group;

  group = g_new0 (GstPlayBaseGroup, 1);
  group->bin = play_base_bin;

  return group;
}

static GstPlayBaseGroup *
get_active_group (GstPlayBaseBin * play_base_bin)
{
  if (play_base_bin->queued_groups) {
    return (GstPlayBaseGroup *) play_base_bin->queued_groups->data;
  }
  return NULL;
}

/* get the group used for discovering the different streams.
 * This function creates a group is there is none.
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

static void
group_destroy (GstPlayBaseGroup * group)
{
  GstPlayBaseBin *play_base_bin = group->bin;
  GList *infos;
  gint n;

  GST_LOG ("removing group %p", group);

  /* remove the preroll queues */
  for (n = 0; n < NUM_TYPES; n++) {
    GstElement *element = group->type[n].preroll;
    GstElement *fakesrc;
    const GList *item;

    if (!element)
      continue;

    /* remove any fakesrc elements for this preroll element */
    for (item = GST_ELEMENT (group->type[n].selector)->pads;
        item != NULL; item = item->next) {
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
            gst_pad_get_name (pad),
            GST_ELEMENT_NAME (gst_pad_get_parent (pad)));
        gst_bin_remove (GST_BIN (play_base_bin->thread), fakesrc);
      }
    }

    /* if the group is currently being played, we have to remove the element 
     * from the thread */
    if (get_active_group (play_base_bin) == group) {
      GST_LOG ("removing preroll element %s", gst_element_get_name (element));
      gst_bin_remove (group->type[n].bin, element);
      gst_bin_remove (group->type[n].bin, group->type[n].selector);
    } else {
      /* else we can just unref it */
      gst_object_unref (GST_OBJECT (element));
      gst_object_unref (GST_OBJECT (group->type[n].selector));
    }

    group->type[n].preroll = NULL;
    group->type[n].selector = NULL;
    group->type[n].bin = NULL;
  }

  /* free the streaminfo too */
  for (infos = group->streaminfo; infos; infos = g_list_next (infos)) {
    GstStreamInfo *info = GST_STREAM_INFO (infos->data);

    g_object_unref (info);
  }
  g_list_free (group->streaminfo);
  g_free (group);
}

/* is called when the current building group is completely finished 
 * and ready for playback
 */
static void
group_commit (GstPlayBaseBin * play_base_bin, gboolean fatal)
{
  GstPlayBaseGroup *group = play_base_bin->building_group;
  gboolean had_active_group = (get_active_group (play_base_bin) != NULL);

  /* if an element signalled a no-more-pads after we stopped due
   * to preroll, the group is NULL. This is not an error */
  if (group == NULL) {
    if (!fatal) {
      return;
    } else {
      GST_DEBUG ("Group loading failed, bailing out");
    }
  } else {
    gint n;

    GST_DEBUG ("group %p done", group);

    play_base_bin->queued_groups = g_list_append (play_base_bin->queued_groups,
        group);

    play_base_bin->building_group = NULL;

    /* remove signals. We don't want anymore signals from the preroll
     * elements at this stage. */
    for (n = 0; n < NUM_TYPES; n++) {
      GstElement *element = group->type[n].preroll;
      guint sig_id;

      if (!element)
        continue;

      sig_id =
          GPOINTER_TO_INT (g_object_get_data (G_OBJECT (element), "signal_id"));

      GST_LOG ("removing preroll signal %s", gst_element_get_name (element));
      g_signal_handler_disconnect (G_OBJECT (element), sig_id);
    }
  }

  g_mutex_lock (play_base_bin->group_lock);
  GST_DEBUG ("signal group done");
  g_cond_signal (play_base_bin->group_cond);
  GST_DEBUG ("signaled group done");
  g_mutex_unlock (play_base_bin->group_lock);

  if (group && !had_active_group && GST_STATE (play_base_bin) > GST_STATE_READY) {
    setup_substreams (play_base_bin);
    GST_DEBUG ("Emitting signal");
    g_signal_emit (play_base_bin,
        gst_play_base_bin_signals[SETUP_OUTPUT_PADS_SIGNAL], 0);
    GST_DEBUG ("done");
    g_object_notify (G_OBJECT (play_base_bin), "stream-info");
  }
}

/* check if there are streams in the group that are not muted */
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

static gboolean
check_queue (GstProbe * probe, GstData ** data, gpointer user_data)
{
  GstElement *queue = GST_ELEMENT (user_data);
  GstPlayBaseBin *play_base_bin = g_object_get_data (G_OBJECT (queue), "pbb");
  guint64 level = 0;

  g_object_get (G_OBJECT (queue), "current-level-time", &level, NULL);
  GST_DEBUG ("Queue size: %" GST_TIME_FORMAT, GST_TIME_ARGS (level));
  level = level * 100 / play_base_bin->queue_threshold;
  if (level > 100)
    level = 100;

  g_signal_emit (play_base_bin,
      gst_play_base_bin_signals[BUFFERING_SIGNAL], 0, level);

  /* continue! */
  return TRUE;
}

/* this signal will be fired when one of the queues with raw
 * data is filled. This means that the group building stage is over 
 * and playback of the new queued group should start */
static void
queue_overrun (GstElement * element, GstPlayBaseBin * play_base_bin)
{
  GST_DEBUG ("queue %s overrun", gst_element_get_name (element));

  group_commit (play_base_bin, FALSE);

  g_signal_handlers_disconnect_by_func (element,
      G_CALLBACK (queue_overrun), play_base_bin);
}

/* Used for time-based buffering. */
static void
queue_threshold_reached (GstElement * queue, GstPlayBaseBin * play_base_bin)
{
  GstProbe *probe;

  GST_DEBUG ("Running");

  /* play */
  g_object_set (queue, "min-threshold-time", (guint64) 0, NULL);

  if ((probe = g_object_get_data (G_OBJECT (queue), "probe"))) {
    GstPad *sinkpad;

    sinkpad = gst_element_get_pad (queue, "sink");
    GST_DEBUG_OBJECT (play_base_bin,
        "Removing buffer probe %p from pad %s:%s (%p)",
        probe, GST_DEBUG_PAD_NAME (sinkpad), sinkpad);

    g_signal_emit (play_base_bin,
        gst_play_base_bin_signals[BUFFERING_SIGNAL], 0, 100);

    g_object_set_data (G_OBJECT (queue), "probe", NULL);
    gst_pad_remove_probe (sinkpad, probe);
    gst_probe_destroy (probe);

    g_signal_emit (play_base_bin,
        gst_play_base_bin_signals[BUFFERING_SIGNAL], 0, 100);
  }
}

static void
queue_out_of_data (GstElement * queue, GstPlayBaseBin * play_base_bin)
{
  GstProbe *probe;

  GST_DEBUG ("Underrun, re-caching");

  /* On underrun, we want to temoprarily pause playback, set a "min-size"
   * threshold and wait for the running signal and then play again. Take
   * care of possible deadlocks and so on, */
  g_object_set (queue, "min-threshold-time",
      (guint64) play_base_bin->queue_threshold, NULL);

  /* re-connect probe */
  if (!(probe = g_object_get_data (G_OBJECT (queue), "probe"))) {
    GstPad *sinkpad;

    probe = gst_probe_new (FALSE, check_queue, queue);
    sinkpad = gst_element_get_pad (queue, "sink");
    gst_pad_add_probe (sinkpad, probe);
    g_object_set_data (G_OBJECT (queue), "probe", probe);
    GST_DEBUG_OBJECT (play_base_bin,
        "Re-attaching buffering probe %p to pad %s:%s (%p)",
        probe, GST_DEBUG_PAD_NAME (sinkpad), sinkpad);

    g_signal_emit (play_base_bin,
        gst_play_base_bin_signals[BUFFERING_SIGNAL], 0, 0);
  }
}

/* generate a preroll element which is simply a queue. While there
 * are still dynamic elements in the pipeline, we wait for one
 * of the queues to fill. The assumption is that all the dynamic
 * streams will be detected by that time. 
 */
static void
gen_preroll_element (GstPlayBaseBin * play_base_bin,
    GstPlayBaseGroup * group, GstStreamType type, GstPad * pad,
    GstStreamInfo * info)
{
  GstElement *selector, *preroll;
  gchar *name;
  const gchar *prename;
  guint sig;
  GstPad *preroll_pad;
  GstProbe *probe;

  if (type == GST_STREAM_TYPE_VIDEO)
    prename = "video";
  else if (type == GST_STREAM_TYPE_TEXT)
    prename = "text";
  else if (type == GST_STREAM_TYPE_AUDIO)
    prename = "audio";
  else
    g_return_if_reached ();

  /* create stream selector */
  name = g_strdup_printf ("selector_%s_%s", prename, gst_pad_get_name (pad));
  selector = g_object_new (GST_TYPE_STREAM_SELECTOR, NULL);
  gst_object_set_name (GST_OBJECT (selector), name);
  g_free (name);

  /* create preroll queue */
  name = g_strdup_printf ("preroll_%s_%s", prename, gst_pad_get_name (pad));
  preroll = gst_element_factory_make ("queue", name);
  g_object_set (G_OBJECT (preroll),
      "max-size-buffers", 0, "max-size-bytes", 10 * 1024 * 1024,
      "max-size-time", play_base_bin->queue_size, NULL);
  sig = g_signal_connect (G_OBJECT (preroll), "overrun",
      G_CALLBACK (queue_overrun), play_base_bin);
  if (play_base_bin->is_stream &&
      ((type == GST_STREAM_TYPE_VIDEO &&
              group->type[GST_STREAM_TYPE_AUDIO - 1].npads == 0) ||
          (type == GST_STREAM_TYPE_AUDIO &&
              group->type[GST_STREAM_TYPE_VIDEO - 1].npads == 0))) {
    GstProbe *probe;
    GstPad *sinkpad;

    g_signal_connect (G_OBJECT (preroll), "running",
        G_CALLBACK (queue_threshold_reached), play_base_bin);
    g_object_set (G_OBJECT (preroll),
        "min-threshold-time", (guint64) play_base_bin->queue_threshold, NULL);

    /* give updates on queue size */
    probe = gst_probe_new (FALSE, check_queue, preroll);
    sinkpad = gst_element_get_pad (preroll, "sink");
    gst_pad_add_probe (sinkpad, probe);
    GST_DEBUG_OBJECT (play_base_bin, "Attaching probe %p to pad %s:%s (%p)",
        probe, GST_DEBUG_PAD_NAME (sinkpad), sinkpad);
    g_object_set_data (G_OBJECT (preroll), "pbb", play_base_bin);
    g_object_set_data (G_OBJECT (preroll), "probe", probe);

    g_signal_connect (G_OBJECT (preroll), "underrun",
        G_CALLBACK (queue_out_of_data), play_base_bin);
  }
  /* keep a ref to the signal id so that we can disconnect the signal callback
   * when we are done with the preroll */
  g_object_set_data (G_OBJECT (preroll), "signal_id", GINT_TO_POINTER (sig));
  g_free (name);

  /* listen for EOS */
  preroll_pad = gst_element_get_pad (preroll, "src");
  probe = gst_probe_new (FALSE, probe_triggered, info);
  /* have to REALIZE the pad as we cannot attach a padprobe to a ghostpad */
  gst_pad_add_probe (preroll_pad, probe);

  /* add to group list */
  gst_element_link (selector, preroll);
  group->type[type - 1].selector = selector;
  group->type[type - 1].preroll = preroll;
  if (type == GST_STREAM_TYPE_TEXT && play_base_bin->subtitle) {
    group->type[type - 1].bin = GST_BIN (play_base_bin->subtitle);
    gst_bin_add (GST_BIN (play_base_bin->subtitle), selector);
    gst_bin_add (GST_BIN (play_base_bin->subtitle), preroll);
    gst_element_set_state (selector, GST_STATE_PAUSED);
  } else {
    group->type[type - 1].bin = GST_BIN (play_base_bin->thread);
    gst_bin_add (GST_BIN (play_base_bin->thread), selector);
    gst_bin_add (GST_BIN (play_base_bin->thread), preroll);
    gst_element_set_state (selector, GST_STATE_PLAYING);
  }
  if (GST_STATE (play_base_bin) == GST_STATE_PLAYING &&
      !get_active_group (play_base_bin)) {
    gst_element_set_state (preroll, GST_STATE_PLAYING);
  } else {
    gst_element_set_state (preroll, GST_STATE_PAUSED);
  }

  play_base_bin->threaded = TRUE;
}

static void
remove_groups (GstPlayBaseBin * play_base_bin)
{
  GList *groups;

  /* first destroy the group we were building if any */
  if (play_base_bin->building_group) {
    group_destroy (play_base_bin->building_group);
    play_base_bin->building_group = NULL;
  }

  /* remove the queued groups */
  for (groups = play_base_bin->queued_groups; groups;
      groups = g_list_next (groups)) {
    GstPlayBaseGroup *group = (GstPlayBaseGroup *) groups->data;

    group_destroy (group);
  }
  g_list_free (play_base_bin->queued_groups);
  play_base_bin->queued_groups = NULL;

  /* clear subs */
  if (play_base_bin->subtitle) {
    gst_bin_remove (GST_BIN (play_base_bin->thread), play_base_bin->subtitle);
    play_base_bin->subtitle = NULL;
  }
}

/* Add/remove a single stream to current  building group.
 */
static void
add_stream (GstPlayBaseGroup * group, GstStreamInfo * info)
{
  GST_DEBUG ("add stream to group %p", group);

  /* keep ref to the group */
  g_object_set_data (G_OBJECT (info), "group", group);

  group->streaminfo = g_list_append (group->streaminfo, info);
  if (info->type > 0 && info->type <= NUM_TYPES) {
    group->type[info->type - 1].npads++;
  }
}

/* signal fired when an unknown stream is found. We create a new
 * UNKNOWN streaminfo object 
 */
static void
unknown_type (GstElement * element, GstPad * pad, GstCaps * caps,
    GstPlayBaseBin * play_base_bin)
{
  gchar *capsstr;
  GstStreamInfo *info;
  GstPlayBaseGroup *group = get_building_group (play_base_bin);

  capsstr = gst_caps_to_string (caps);
  g_message ("don't know how to handle %s", capsstr);

  /* add the stream to the list */
  info = gst_stream_info_new (GST_OBJECT (pad), GST_STREAM_TYPE_UNKNOWN,
      NULL, caps);
  info->origin = GST_OBJECT (pad);
  add_stream (group, info);

  g_free (capsstr);
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
  GstPlayBaseGroup *group = get_building_group (play_base_bin);

  /* add the stream to the list */
  info =
      gst_stream_info_new (GST_OBJECT (element), GST_STREAM_TYPE_ELEMENT,
      NULL, NULL);
  info->origin = GST_OBJECT (element);
  add_stream (group, info);
}

/* when the decoder element signals that no more pads will be generated, we
 * can commit the current group.
 */
static void
no_more_pads (GstElement * element, GstPlayBaseBin * play_base_bin)
{
  /* setup phase */
  GST_DEBUG ("no more pads");
  /* we can commit this group for playback now */
  group_commit (play_base_bin, FALSE);
}

static gboolean
probe_triggered (GstProbe * probe, GstData ** data, gpointer user_data)
{
  GstPlayBaseGroup *group;
  GstPlayBaseBin *play_base_bin;
  GstStreamInfo *info = GST_STREAM_INFO (user_data);

  group = (GstPlayBaseGroup *) g_object_get_data (G_OBJECT (info), "group");
  play_base_bin = group->bin;

  if (GST_IS_EVENT (*data)) {
    GstEvent *event = GST_EVENT (*data);

    if (GST_EVENT_TYPE (event) == GST_EVENT_EOS) {
      gint num_groups = 0;
      gboolean have_left;

      GST_DEBUG ("probe got EOS in group %p", group);

      /* mute this stream */
      //g_object_set (G_OBJECT (info), "mute", TRUE, NULL);
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
        /* remove the EOS if we have something left */
        return !have_left;
      }

      if (have_left) {
        /* error? */
        if (GST_STATE (play_base_bin->thread) != GST_STATE_PLAYING)
          return TRUE;

        GST_DEBUG ("changing state for group change");
        gst_element_set_state (play_base_bin->thread, GST_STATE_PAUSED);
        /* ok, get rid of the current group then */
        group_destroy (group);
        g_signal_emit (play_base_bin,
            gst_play_base_bin_signals[GROUP_SWITCH_SIGNAL], 0);
        /* removing the current group brings the next group
         * active */
        play_base_bin->queued_groups =
            g_list_remove (play_base_bin->queued_groups, group);
        if (play_base_bin->queued_groups) {
          setup_substreams (play_base_bin);
          GST_DEBUG ("switching to next group %p",
              play_base_bin->queued_groups->data);
          /* and signal the new group */
          GST_DEBUG ("emit signal");
          g_signal_emit (play_base_bin,
              gst_play_base_bin_signals[SETUP_OUTPUT_PADS_SIGNAL], 0);
        }
        /* else, it'll be handled in commit_group */
        GST_DEBUG ("Syncing state from %d", GST_STATE (play_base_bin->thread));
        gst_element_set_state (play_base_bin->thread, GST_STATE_PLAYING);
        GST_DEBUG ("done");

        /* get rid of the EOS event */
        return FALSE;
      } else {
        GST_LOG ("Last group done, EOS");
      }
    } else if (GST_EVENT_TYPE (event) == GST_EVENT_TAG) {
      GstTagList *taglist;
      GstObject *source;

      /* ref some to be sure.. */
      gst_event_ref (event);
      gst_object_ref (GST_OBJECT (play_base_bin));
      taglist = gst_event_tag_get_list (event);
      /* now try to find the source of this tag */
      source = event->src;
      if (source == NULL || !GST_IS_ELEMENT (source)) {
        /* no source, just use playbasebin then. This happens almost
         * all the time, it seems the source is never filled in... */
        source = GST_OBJECT (play_base_bin);
      }
      /* emit the signal now */
      g_signal_emit_by_name (G_OBJECT (play_base_bin),
          "found-tag", source, taglist);
      /* and unref */
      gst_object_unref (GST_OBJECT (play_base_bin));
      gst_event_unref (event);
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

  /* make a fakesrc that will just emit one EOS */
  fakesrc = gst_element_factory_make ("fakesrc", NULL);
  g_object_set (G_OBJECT (fakesrc), "num_buffers", 0, NULL);

  GST_DEBUG ("patching unlinked pad %s:%s", GST_DEBUG_PAD_NAME (pad));

  gst_pad_link (gst_element_get_pad (fakesrc, "src"), pad);
  gst_bin_add (GST_BIN (play_base_bin->thread), fakesrc);

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
mute_stream (GstProbe * probe, GstData ** d, gpointer data)
{
  GstStreamInfo *info = GST_STREAM_INFO (data);

  if (GST_IS_BUFFER (*d)) {
    g_object_set (G_OBJECT (info), "mute", TRUE, NULL);
    gst_pad_remove_probe ((GstPad *) GST_PAD_REALIZE (info->object), probe);
    gst_probe_destroy (probe);
  }

  /* no data */
  return FALSE;
}

/* Eat data. */
static gboolean
silence_stream (GstProbe * probe, GstData ** d, gpointer data)
{
  /* no data */
  return FALSE;
}

/* signal fired when decodebin has found a new raw pad. We create
 * a preroll element if needed and the appropriate streaminfo.
 */
static void
new_decoded_pad (GstElement * element, GstPad * pad, gboolean last,
    GstPlayBaseBin * play_base_bin)
{
  GstStructure *structure;
  const gchar *mimetype;
  GstCaps *caps;
  GstStreamInfo *info;
  GstStreamType type;
  GstPad *sinkpad;
  GstPlayBaseGroup *group;
  GstProbe *probe;
  guint sig;

  GST_DEBUG ("play base: new decoded pad %d", last);

  /* first see if this pad has interesting caps */
  caps = gst_pad_get_caps (pad);
  if (caps == NULL || gst_caps_is_empty (caps) || gst_caps_is_any (caps)) {
    g_warning ("no type on pad %s:%s",
        GST_DEBUG_PAD_NAME (GST_PAD_REALIZE (pad)));
    if (caps)
      gst_caps_unref (caps);
    return;
  }

  /* get the mime type */
  structure = gst_caps_get_structure (caps, 0);
  mimetype = gst_structure_get_name (structure);

  group = get_building_group (play_base_bin);

  group->nstreams++;

  if (g_str_has_prefix (mimetype, "audio/")) {
    type = GST_STREAM_TYPE_AUDIO;
  } else if (g_str_has_prefix (mimetype, "video/")) {
    type = GST_STREAM_TYPE_VIDEO;
  } else if (g_str_has_prefix (mimetype, "text/")) {
    type = GST_STREAM_TYPE_TEXT;
  } else {
    type = GST_STREAM_TYPE_UNKNOWN;
  }

  info = gst_stream_info_new (GST_OBJECT (pad), type, NULL, caps);
  if (type > 0 && type <= NUM_TYPES) {
    /* first pad of each type gets a selector + preroll queue */
    if (group->type[type - 1].npads == 0) {
      GST_DEBUG ("play base: pad needs new preroll");
      gen_preroll_element (play_base_bin, group, type, pad, info);
    }
  }

  /* add to stream selector */
  sinkpad = gst_element_get_pad (group->type[type - 1].selector, "sink%d");
  /* make sure we catch unlink signals */
  sig = g_signal_connect (G_OBJECT (sinkpad), "unlinked",
      G_CALLBACK (preroll_unlinked), play_base_bin);
  /* keep a ref to the signal id so that we can disconnect the signal callback */
  g_object_set_data (G_OBJECT (sinkpad), "unlinked_id", GINT_TO_POINTER (sig));
  gst_pad_link (pad, sinkpad);

  /* add the stream to the list */
  gst_caps_unref (caps);
  info->origin = GST_OBJECT (pad);

  /* select 1st for now - we'll select a preferred one after preroll */
  if (type == GST_STREAM_TYPE_UNKNOWN || group->type[type - 1].npads > 0) {
    probe = gst_probe_new (FALSE, silence_stream, info);
    gst_pad_add_probe (GST_PAD_REALIZE (pad), probe);
    g_object_set_data (G_OBJECT (pad), "eat_probe", probe);
  }

  add_stream (group, info);

  /* signal the no more pads after adding the stream */
  if (last)
    no_more_pads (NULL, play_base_bin);
}


/* nothing, really... We have already dealt with this because
 * we have the EOS padprobe installed on each pad */
static void
removed_decoded_pad (GstElement * element, GstPad * pad,
    GstPlayBaseBin * play_base_bin)
{
  return;
}

/*
 * Cache errors...
 */
static void
thread_error (GstElement * element,
    GstElement * orig, GError * error, const gchar * debug, gpointer data)
{
  GError **_error = data;

  if (!*_error)
    *_error = g_error_copy (error);
}

/* signal fired when the internal thread performed an unexpected  
 * state change. This usually indicated an error occured. We stop the
 * preroll stage.
 */
static void
state_change (GstElement * element,
    GstElementState old_state, GstElementState new_state, gpointer data)
{
  GstPlayBaseBin *play_base_bin = GST_PLAY_BASE_BIN (data);

  if (old_state > new_state) {
    /* EOS or error occurred, we have to commit the current group */
    GST_DEBUG ("state changed downwards");
    group_commit (play_base_bin, TRUE);
  }
}

/*
 * Generate source ! subparse bins.
 */

static GstElement *
setup_subtitle (GstPlayBaseBin * play_base_bin, gchar * sub_uri)
{
  GstElement *source, *subparse, *subbin;
  gchar *name;

  source = gst_element_make_from_uri (GST_URI_SRC, sub_uri, NULL);
  if (!source)
    return NULL;

  if (!(subparse = gst_element_factory_make ("subparse", NULL))) {
    gst_object_unref (GST_OBJECT (source));
    return NULL;
  }
  name = g_strdup_printf ("subbin");
  subbin = gst_bin_new (name);
  g_free (name);

  gst_bin_add_many (GST_BIN (subbin), source, subparse, NULL);
  gst_element_link (source, subparse);
  gst_element_add_ghost_pad (subbin,
      gst_element_get_pad (subparse, "src"), "src");

  /* return the subtitle GstElement object */
  return subbin;
}

/*
 * Generate a source element that does caching for network streams.
 */

static GstElement *
gen_source_element (GstPlayBaseBin * play_base_bin, GstElement ** subbin)
{
  GstElement *source;

  /* stip subtitle from uri */
  if (!play_base_bin->uri)
    return NULL;
  if (play_base_bin->suburi) {
    /* subtitle specified */
    *subbin = setup_subtitle (play_base_bin, play_base_bin->suburi);
  } else {
    /* no subtitle specified */
    *subbin = NULL;
  }

  source = gst_element_make_from_uri (GST_URI_SRC, play_base_bin->uri,
      "source");
  if (!source)
    return NULL;

  /* lame - FIXME, maybe we can use seek_types/mask here? */
  play_base_bin->is_stream = !strncmp (play_base_bin->uri, "http://", 7) ||
      !strncmp (play_base_bin->uri, "mms://", 6) ||
      !strncmp (play_base_bin->uri, "rtp://", 6) ||
      !strncmp (play_base_bin->uri, "rtsp://", 7);

  return source;
}

/* Setup the substreams (to be called right after group_commit ()) */
static void
setup_substreams (GstPlayBaseBin * play_base_bin)
{
  GstPlayBaseGroup *group = get_active_group (play_base_bin);
  GstProbe *probe;
  gint n;
  const GList *item;

  /* Remove the eat probes */
  for (item = group->streaminfo; item; item = item->next) {
    GstStreamInfo *info = item->data;

    probe = g_object_get_data (G_OBJECT (info->object), "eat_probe");
    if (probe) {
      gst_pad_remove_probe (GST_PAD_REALIZE (info->object), probe);
      gst_probe_destroy (probe);
    }

    /* now remove unknown pads */
    if (info->type == GST_STREAM_TYPE_UNKNOWN) {
      GstProbe *probe;

      probe = gst_probe_new (FALSE, mute_stream, info);
      gst_pad_add_probe (GST_PAD_REALIZE (info->object), probe);
    }
  }

  /* now check if the requested current streams exist. If
   * current >= num_streams, decrease current so at least
   * we have output. Always keep it enabled. */
  for (n = 0; n < NUM_TYPES; n++) {
    if (play_base_bin->current[n] >= group->type[n].npads) {
      play_base_bin->current[n] = 0;
    }
  }

  /* now acticate the right sources. Don't forget that during preroll,
   * we set the first source to forwarding and ignored the rest. */
  for (n = 0; n < NUM_TYPES; n++) {
    set_active_source (play_base_bin, n + 1, play_base_bin->current[n]);
  }
}

/*
 * Called when we're redirected to a new URI.
 */
static void
got_redirect (GstElement * element, const gchar * new_location, gpointer data)
{
  gchar **location = data;

  if (!*location)
    *location = g_strdup (new_location);
}

/* construct and run the source and decoder elements until we found
 * all the streams or until a preroll queue has been filled.
 * jesus christ this is a long function!
*/
static gboolean
setup_source (GstPlayBaseBin * play_base_bin,
    gchar ** new_location, GError ** error)
{
  GstElement *old_src;
  GstElement *old_dec;
  GstPad *srcpad = NULL;
  GstElement *subbin = NULL;

  if (!play_base_bin->need_rebuild)
    return TRUE;

  play_base_bin->threaded = FALSE;

  /* keep ref to old souce in case creating the new source fails */
  old_src = play_base_bin->source;

  /* create and configure an element that can handle the uri */
  play_base_bin->source = gen_source_element (play_base_bin, &subbin);

  if (!play_base_bin->source) {
    /* whoops, could not create the source element */
    g_set_error (error, 0, 0,
        "No URI handler implemented for \"%s\"", play_base_bin->uri);
    GST_WARNING ("don't know how to read %s", play_base_bin->uri);
    play_base_bin->source = old_src;
    return FALSE;
  } else {
    if (old_src) {
      GST_LOG ("removing old src element %s", gst_element_get_name (old_src));
      gst_element_set_state (old_src, GST_STATE_NULL);
      gst_bin_remove (GST_BIN (play_base_bin->thread), old_src);
    }
    gst_bin_add (GST_BIN (play_base_bin->thread), play_base_bin->source);
    g_object_notify (G_OBJECT (play_base_bin), "source");

    /* make sure the new element has the same state as the parent */
#if 0
    if (gst_bin_sync_children_state (GST_BIN (play_base_bin->thread)) ==
        GST_STATE_FAILURE) {
      return FALSE;
    }
#endif
  }

  /* remove the old decoder now, if any */
  old_dec = play_base_bin->decoder;
  if (old_dec) {
    GST_LOG ("removing old decoder element %s", gst_element_get_name (old_dec));
    /* keep a ref to the old decoder as we might need to add it again
     * to the bin if we can't find a new decoder */
    gst_object_ref (GST_OBJECT (old_dec));
    gst_bin_remove (GST_BIN (play_base_bin->thread), old_dec);
  }

  /* remove our previous preroll queues */
  remove_groups (play_base_bin);

  /* do subs */
  if (subbin) {
    play_base_bin->subtitle = subbin;

    /* don't add yet, because we will preroll, and subs shouldn't
     * preroll (we shouldn't preroll more than once source). */
    gst_element_set_state (subbin, GST_STATE_PAUSED);
    new_decoded_pad (subbin, gst_element_get_pad (subbin, "src"), FALSE,
        play_base_bin);
  }

  /* now see if the source element emits raw audio/video all by itself,
   * if so, we can create streams for the pads and be done with it.
   * Also check that is has source pads, if not, we assume it will
   * do everything itself.
   */
  {
    const GList *pads;
    gboolean is_raw = FALSE;

    /* assume we are going to have no output streams */
    gboolean no_out = TRUE;

    for (pads = GST_ELEMENT (play_base_bin->source)->pads;
        pads; pads = g_list_next (pads)) {
      GstPad *pad = GST_PAD (pads->data);
      GstStructure *structure;
      const gchar *mimetype;
      GstCaps *caps;

      if (GST_PAD_IS_SINK (pad))
        continue;

      no_out = FALSE;

      srcpad = pad;
      caps = gst_pad_get_caps (pad);

      if (caps == NULL || gst_caps_is_empty (caps) ||
          gst_caps_get_size (caps) == 0) {
        if (caps != NULL)
          gst_caps_unref (caps);
        continue;
      }

      structure = gst_caps_get_structure (caps, 0);
      mimetype = gst_structure_get_name (structure);

      if (g_str_has_prefix (mimetype, "audio/x-raw") ||
          g_str_has_prefix (mimetype, "video/x-raw")) {
        new_decoded_pad (play_base_bin->source, pad, g_list_next (pads) == NULL,
            play_base_bin);
        is_raw = TRUE;
      }

      gst_caps_unref (caps);
    }
    if (is_raw) {
      no_more_pads (play_base_bin->source, play_base_bin);
      return TRUE;
    }
    if (no_out) {
      /* create a stream to indicate that this uri is handled by a self
       * contained element */
      add_element_stream (play_base_bin->source, play_base_bin);
      no_more_pads (play_base_bin->source, play_base_bin);
      return TRUE;
    }
  }

  {
    gboolean res;
    gint sig1, sig2, sig3, sig4, sig5, sig6;

    /* now create the decoder element */
    play_base_bin->decoder = gst_element_factory_make ("decodebin", "decoder");
    if (!play_base_bin->decoder) {
      g_warning ("can't find decoder element");
      play_base_bin->decoder = old_dec;
      return FALSE;
    } else {
      /* ref decoder so that the bin does not take ownership */
      gst_object_ref (GST_OBJECT (play_base_bin->decoder));
      gst_bin_add (GST_BIN (play_base_bin->thread), play_base_bin->decoder);
      /* now we can really get rid of the old decoder */
      if (old_dec)
        gst_object_unref (GST_OBJECT (old_dec));
    }

    g_signal_connect (play_base_bin->decoder, "got_redirect",
        G_CALLBACK (got_redirect), new_location);
    res = gst_pad_link (srcpad,
        gst_element_get_pad (play_base_bin->decoder, "sink"));
    if (!res) {
      g_warning ("can't link source to decoder element");
      return FALSE;
    }
    sig1 = g_signal_connect (G_OBJECT (play_base_bin->decoder),
        "new-decoded-pad", G_CALLBACK (new_decoded_pad), play_base_bin);
    sig2 = g_signal_connect (G_OBJECT (play_base_bin->decoder),
        "removed-decoded-pad", G_CALLBACK (removed_decoded_pad), play_base_bin);
    sig3 = g_signal_connect (G_OBJECT (play_base_bin->decoder), "no-more-pads",
        G_CALLBACK (no_more_pads), play_base_bin);

    if (!play_base_bin->is_stream) {
      sig4 = g_signal_connect (G_OBJECT (play_base_bin->decoder),
          "unknown-type", G_CALLBACK (unknown_type), play_base_bin);
      sig5 = g_signal_connect (G_OBJECT (play_base_bin->thread), "error",
          G_CALLBACK (thread_error), error);

      /* either when the queues are filled or when the decoder element
       * has no more dynamic streams, the cond is unlocked. We can remove
       * the signal handlers then
       */
      g_mutex_lock (play_base_bin->group_lock);
      if (gst_element_set_state (play_base_bin->thread, GST_STATE_PAUSED) ==
          GST_STATE_SUCCESS) {
        GST_DEBUG ("waiting for first group...");
        sig6 = g_signal_connect (G_OBJECT (play_base_bin->thread),
            "state-change", G_CALLBACK (state_change), play_base_bin);
        g_cond_wait (play_base_bin->group_cond, play_base_bin->group_lock);
        GST_DEBUG ("group done !");
      } else {
        GST_DEBUG ("state change failed, media cannot be loaded");
        sig6 = 0;
      }
      g_mutex_unlock (play_base_bin->group_lock);

      if (sig6 != 0)
        g_signal_handler_disconnect (G_OBJECT (play_base_bin->thread), sig6);

      g_signal_handler_disconnect (G_OBJECT (play_base_bin->thread), sig5);
      g_signal_handler_disconnect (G_OBJECT (play_base_bin->decoder), sig4);
    } else {
      GST_DEBUG ("Source is a stream, delaying stream initialization");
    }

    play_base_bin->need_rebuild = FALSE;
  }

  if (get_active_group (play_base_bin) != NULL) {
    if (play_base_bin->subtitle) {
      /* make subs iterate from now on */
      gst_bin_add (GST_BIN (play_base_bin->thread), play_base_bin->subtitle);
    }
    if (!play_base_bin->is_stream) {
      setup_substreams (play_base_bin);
    }
  }

  return TRUE;
}

/*
 * Multi-stream management. -1 = none.
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
      if (!info->mute) {
        return num;
      } else {
        num++;
      }
    }
  }

  return -1;
}

/* Kill pad reactivation on state change. */

static void muted_group_change_state (GstElement * element,
    gint old_state, gint new_state, gpointer data);

static void
mute_group_type (GstPlayBaseGroup * group, GstStreamType type, gboolean mute)
{
  gboolean active = !mute;

  gst_pad_set_active (gst_element_get_pad (group->type[type - 1].preroll,
          "src"), active);
  gst_pad_set_active (gst_element_get_pad (group->type[type - 1].preroll,
          "sink"), active);
  gst_pad_set_active (gst_element_get_pad (group->type[type - 1].selector,
          "src"), active);

  if (mute) {
    g_signal_connect (group->type[type - 1].preroll, "state-change",
        G_CALLBACK (muted_group_change_state), group);
  } else {
    g_signal_handlers_disconnect_by_func (group->type[type - 1].preroll,
        G_CALLBACK (muted_group_change_state), group);
  }
}

static void
muted_group_change_state (GstElement * element,
    gint old_state, gint new_state, gpointer data)
{
  GstPlayBaseGroup *group = data;

  if (new_state == GST_STATE_PLAYING) {
    gint n;

    for (n = 0; n < NUM_TYPES; n++) {
      if (group->type[n].selector == element) {
        mute_group_type (group, n + 1, TRUE);
      }
    }
  }
}

static void
set_active_source (GstPlayBaseBin * play_base_bin,
    GstStreamType type, gint source_num)
{
  GstPlayBaseGroup *group;
  GList *s;
  gint num = 0;
  gboolean have_active = FALSE;

  GST_LOG ("Changing active source of type %d to %d", type, source_num);
  play_base_bin->current[type - 1] = source_num;

  group = get_active_group (play_base_bin);
  if (!group || !group->type[type - 1].preroll)
    return;

  for (s = group->streaminfo; s; s = s->next) {
    GstStreamInfo *info = s->data;

    if (info->type == type) {
      if (num == source_num) {
        g_object_set (s->data, "mute", FALSE, NULL);
        have_active = TRUE;
      } else {
        GstProbe *probe;

        probe = gst_probe_new (FALSE, mute_stream, info);
        gst_pad_add_probe (GST_PAD_REALIZE (info->object), probe);
      }
      num++;
    }
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
    case ARG_VIDEO:
      set_active_source (play_base_bin,
          GST_STREAM_TYPE_VIDEO, g_value_get_int (value));
      break;
    case ARG_AUDIO:
      set_active_source (play_base_bin,
          GST_STREAM_TYPE_AUDIO, g_value_get_int (value));
      break;
    case ARG_TEXT:
      set_active_source (play_base_bin,
          GST_STREAM_TYPE_TEXT, g_value_get_int (value));
      break;
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
      GstPlayBaseGroup *group = get_active_group (play_base_bin);

      if (group) {
        g_value_set_int (value, group->nstreams);
      } else {
        g_value_set_int (value, 0);
      }
      break;
    }
    case ARG_QUEUE_SIZE:
      g_value_set_uint64 (value, play_base_bin->queue_size);
      break;
    case ARG_STREAMINFO:
      g_value_set_pointer (value,
          (gpointer) gst_play_base_bin_get_streaminfo (play_base_bin));
      break;
    case ARG_SOURCE:
      if (GST_IS_BIN (play_base_bin->source)) {
        GstElement *kid;

        kid = gst_bin_get_by_name (GST_BIN (play_base_bin->source), "source");
        g_value_set_object (value, kid);
      } else
        g_value_set_object (value, play_base_bin->source);
      break;
    case ARG_VIDEO:
      g_value_set_int (value, get_active_source (play_base_bin,
              GST_STREAM_TYPE_VIDEO));
      break;
    case ARG_AUDIO:
      g_value_set_int (value, get_active_source (play_base_bin,
              GST_STREAM_TYPE_AUDIO));
      break;
    case ARG_TEXT:
      g_value_set_int (value, get_active_source (play_base_bin,
              GST_STREAM_TYPE_TEXT));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
play_base_eos (GstBin * bin, GstPlayBaseBin * play_base_bin)
{
  no_more_pads (GST_ELEMENT (bin), play_base_bin);

  GST_LOG ("forwarding EOS");

  //gst_element_set_eos (GST_ELEMENT (play_base_bin));
}

static GstElementStateReturn
gst_play_base_bin_change_state (GstElement * element)
{
  GstElementStateReturn ret = GST_STATE_SUCCESS;
  GstPlayBaseBin *play_base_bin;

  play_base_bin = GST_PLAY_BASE_BIN (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
    {
      play_base_bin->thread = gst_bin_new ("internal_thread");

      gst_element_set_state (play_base_bin->thread, GST_STATE_READY);

      g_signal_connect (play_base_bin->thread, "found_tag",
          G_CALLBACK (gst_play_base_bin_found_tag), play_base_bin);
      break;
    }
    case GST_STATE_READY_TO_PAUSED:
    {
      GError *error = NULL;
      gchar *new_location = NULL;

      if (!setup_source (play_base_bin, &new_location, &error) || error != NULL) {
        if (!error) {
          /* opening failed but no error - hellup */
          GST_ELEMENT_ERROR (GST_ELEMENT (play_base_bin), STREAM,
              NOT_IMPLEMENTED,
              ("cannot open file \"%s\"", play_base_bin->uri), (NULL));
        } else {
          /* just copy the cached error - type doesn't matter */
          GST_ELEMENT_ERROR (play_base_bin, STREAM, TOO_LAZY,
              (error->message), (NULL));
          g_error_free (error);
        }
        ret = GST_STATE_FAILURE;
      } else if (new_location) {
        g_signal_emit (play_base_bin, gst_play_base_bin_signals[REDIRECT],
            0, new_location);
        g_free (new_location);
        ret = GST_STATE_FAILURE;
      } else if (play_base_bin->is_stream) {
        ret = gst_element_set_state (play_base_bin->thread, GST_STATE_PAUSED);
      } else {
        const GList *item;
        gboolean stream_found = FALSE, no_media = FALSE;
        GstPlayBaseGroup *group;

        group = get_active_group (play_base_bin);

        /* check if we found any supported stream... if not, then
         * we detected stream type (or the above would've failed),
         * but linking/decoding failed - plugin probably missing. */
        for (item = group ? group->streaminfo : NULL;
            item != NULL; item = item->next) {
          GstStreamInfo *info = GST_STREAM_INFO (item->data);

          if (info->type != GST_STREAM_TYPE_UNKNOWN) {
            stream_found = TRUE;
            break;
          } else if (!item->prev && !item->next) {
            /* We're no audio/video and the only stream... We could
             * be something not-media that's detected because then our
             * typefind doesn't mess up with mp3 (bz2, gz, elf, ...) */
            if (info->caps && !gst_caps_is_empty (info->caps)) {
              const gchar *mime =
                  gst_structure_get_name (gst_caps_get_structure (info->caps,
                      0));

              if (!strcmp (mime, "application/x-executable") ||
                  !strcmp (mime, "application/x-bzip") ||
                  !strcmp (mime, "application/x-gzip") ||
                  !strcmp (mime, "application/zip") ||
                  !strcmp (mime, "application/x-compress")) {
                no_media = TRUE;
              }
            }
          }
        }

        if (!stream_found) {
          if (!no_media) {
            GST_ELEMENT_ERROR (play_base_bin, STREAM, CODEC_NOT_FOUND,
                ("There were no decoders found to handle the stream in file "
                    "\"%s\", you might need to install the corresponding plugins",
                    play_base_bin->uri), (NULL));
          } else {
            GST_ELEMENT_ERROR (play_base_bin, STREAM, WRONG_TYPE,
                ("File \"%s\" is not a media file", play_base_bin->uri),
                (NULL));
          }
          gst_element_set_state (play_base_bin->thread, GST_STATE_READY);
          ret = GST_STATE_FAILURE;
        } else {
          ret = gst_element_set_state (play_base_bin->thread, GST_STATE_PAUSED);
        }
      }
      if (ret == GST_STATE_SUCCESS) {
        /* error forwarding:
         * we only connect after the stream has been set up. If that failed,
         * we simply emit our own error. This also prevents us from failing
         * because one stream was unrecognized. */
        g_signal_connect (play_base_bin->thread, "error",
            G_CALLBACK (gst_play_base_bin_error), play_base_bin);
        g_signal_connect (G_OBJECT (play_base_bin->thread), "eos",
            G_CALLBACK (play_base_eos), play_base_bin);
        if (!play_base_bin->is_stream) {
          GST_DEBUG ("emit signal");
          g_signal_emit (play_base_bin,
              gst_play_base_bin_signals[SETUP_OUTPUT_PADS_SIGNAL], 0);
          GST_DEBUG ("done");
        }
      } else {
        /* clean up leftover groups */
        remove_groups (play_base_bin);
      }
      break;
    }
    case GST_STATE_PAUSED_TO_PLAYING:
      ret = gst_element_set_state (play_base_bin->thread, GST_STATE_PLAYING);
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      ret = gst_element_set_state (play_base_bin->thread, GST_STATE_PAUSED);
      break;
    case GST_STATE_PAUSED_TO_READY:
      g_signal_handlers_disconnect_by_func (play_base_bin->thread,
          G_CALLBACK (gst_play_base_bin_error), play_base_bin);
      g_signal_handlers_disconnect_by_func (play_base_bin->thread,
          G_CALLBACK (play_base_eos), play_base_bin);
      ret = gst_element_set_state (play_base_bin->thread, GST_STATE_READY);
      play_base_bin->need_rebuild = TRUE;
      remove_groups (play_base_bin);
      break;
    case GST_STATE_READY_TO_NULL:
      gst_element_set_state (play_base_bin->thread, GST_STATE_NULL);
      gst_object_unref (GST_OBJECT (play_base_bin->thread));
      play_base_bin->source = NULL;
      play_base_bin->decoder = NULL;
      break;
    default:
      break;
  }

  if (ret == GST_STATE_SUCCESS) {
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);
  }

  return ret;
}

/* virtual function to add elements to this bin. The idea is to
 * wrap the element in a thread automatically.
 */
static gboolean
gst_play_base_bin_add_element (GstBin * bin, GstElement * element)
{
  GstPlayBaseBin *play_base_bin;

  play_base_bin = GST_PLAY_BASE_BIN (bin);

  if (play_base_bin->thread) {
    if (play_base_bin->threaded) {
      gchar *name;
      GstElement *thread;

      name = g_strdup_printf ("thread_%s", gst_element_get_name (element));
      thread = gst_bin_new (name);
      g_free (name);

      gst_bin_add (GST_BIN (thread), element);
      element = thread;
    }
    gst_bin_add (GST_BIN (play_base_bin->thread), element);
    if (GST_STATE (play_base_bin) > GST_STATE_READY) {
      gst_element_set_state (element, GST_STATE (play_base_bin));
    }
  } else {
    g_warning ("adding elements is not allowed in NULL");
    return FALSE;
  }

  return TRUE;
}

/* virtual function to remove an element from this bin. We have to make
 * sure that we also remove the thread that we used as a container for
 * this element.
 */
static gboolean
gst_play_base_bin_remove_element (GstBin * bin, GstElement * element)
{
  GstPlayBaseBin *play_base_bin;

  play_base_bin = GST_PLAY_BASE_BIN (bin);

  if (play_base_bin->thread) {
    if (play_base_bin->threaded) {
      gchar *name;
      GstElement *thread;

      name = g_strdup_printf ("thread_%s", gst_element_get_name (element));
      thread = gst_bin_get_by_name (GST_BIN (play_base_bin->thread), name);
      g_free (name);

      if (!thread) {
        g_warning ("cannot find element to remove");
      } else {
        if (gst_element_get_parent (element) == GST_OBJECT (thread)) {
          /* we remove the element from the thread first so that the
           * state is not affected when someone holds a reference to it */
          gst_bin_remove (GST_BIN (thread), element);
        }
        element = thread;
      }
    }
    if (gst_element_get_parent (element) == GST_OBJECT (play_base_bin->thread)) {
      GST_LOG ("removing element %s", gst_element_get_name (element));
      gst_bin_remove (GST_BIN (play_base_bin->thread), element);
    }
  } else {
    g_warning ("removing elements is not allowed in NULL");
    return FALSE;
  }
  return TRUE;
}

static void
gst_play_base_bin_error (GstElement * element,
    GstElement * _source, GError * error, gchar * debug, gpointer data)
{
  GstObject *source, *parent;

  source = GST_OBJECT (_source);
  parent = GST_OBJECT (data);

  /* tell ourselves */
  gst_object_ref (source);
  gst_object_ref (parent);
  GST_DEBUG ("forwarding error \"%s\" from %s to %s", error->message,
      GST_ELEMENT_NAME (source), GST_OBJECT_NAME (parent));

  g_signal_emit_by_name (G_OBJECT (parent), "error", source, error, debug);

  GST_DEBUG ("forwarded error \"%s\" from %s to %s", error->message,
      GST_ELEMENT_NAME (source), GST_OBJECT_NAME (parent));
  gst_object_unref (source);
  gst_object_unref (parent);
}

/* this code does not do anything usefull as it catches the tags
 * in the preroll and playback stage so that it is very difficult
 * to link them to the actual playback point. 
 *
 * An alternative codepath can be found in the probe_triggered function
 * where the tag is signaled when it is found inside the stream. The
 * drawback is that we don't know the source anymore at that point because
 * the event->src field appears to be left empty most of the time...
 */
static void
gst_play_base_bin_found_tag (GstElement * element,
    GstElement * _source, const GstTagList * taglist, gpointer data)
{
  GstObject *source;
  GstPlayBaseBin *play_base_bin;

  source = GST_OBJECT (_source);
  play_base_bin = GST_PLAY_BASE_BIN (data);

  /* tell ourselves */
  gst_object_ref (source);
  gst_object_ref (GST_OBJECT (play_base_bin));
  GST_DEBUG ("forwarding taglist %p from %s to %s", taglist,
      GST_ELEMENT_NAME (source), GST_OBJECT_NAME (play_base_bin));

  /* this would signal completely out-of-band */
  //g_signal_emit_by_name (G_OBJECT (play_base_bin), "found-tag", source, taglist);

  GST_DEBUG ("forwarded taglist %p from %s to %s", taglist,
      GST_ELEMENT_NAME (source), GST_OBJECT_NAME (play_base_bin));
  gst_object_unref (source);
  gst_object_unref (GST_OBJECT (play_base_bin));
}

gboolean
gst_play_base_bin_link_stream (GstPlayBaseBin * play_base_bin,
    GstStreamInfo * info, GstPad * pad)
{
  GST_DEBUG ("link stream");

  if (info == NULL) {
    GList *streams;
    GstPlayBaseGroup *group = get_active_group (play_base_bin);

    if (group == NULL) {
      GST_DEBUG ("no current group");
      return FALSE;
    }

    for (streams = group->streaminfo; streams; streams = g_list_next (streams)) {
      GstStreamInfo *sinfo = (GstStreamInfo *) streams->data;

      if (sinfo->type == GST_STREAM_TYPE_ELEMENT)
        continue;

      if (gst_pad_is_linked (GST_PAD (sinfo->object)))
        continue;

      if (gst_pad_can_link (GST_PAD (sinfo->object), pad)) {
        info = sinfo;
        break;
      }
    }
  }
  if (info) {
    if (!gst_pad_link (GST_PAD (info->object), pad)) {
      GST_DEBUG ("could not link");
      g_object_set (G_OBJECT (info), "mute", TRUE, NULL);
      return FALSE;
    }
  } else {
    GST_DEBUG ("could not find pad to link");
    return FALSE;
  }
  return TRUE;
}

void
gst_play_base_bin_unlink_stream (GstPlayBaseBin * play_base_bin,
    GstStreamInfo * info)
{
  GST_DEBUG ("unlink");
}

const GList *
gst_play_base_bin_get_streaminfo (GstPlayBaseBin * play_base_bin)
{
  GstPlayBaseGroup *group = get_active_group (play_base_bin);
  GList *info = NULL;

  if (group) {
    info = group->streaminfo;
  }
  return info;
}
