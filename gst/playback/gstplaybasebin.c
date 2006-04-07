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

#include <gst/gst-i18n-plugin.h>
#include <string.h>
#include "gstplaybasebin.h"
#include "gststreamselector.h"
#include "gstplay-marshal.h"

GST_DEBUG_CATEGORY_STATIC (gst_play_base_bin_debug);
#define GST_CAT_DEFAULT gst_play_base_bin_debug

#if 0
#define DEFAULT_QUEUE_THRESHOLD ((2 * GST_SECOND) / 3)
#define DEFAULT_QUEUE_SIZE  (GST_SECOND / 2)
#else
#define DEFAULT_QUEUE_THRESHOLD (2 * GST_SECOND)
#define DEFAULT_QUEUE_SIZE  (3 * GST_SECOND)
#endif

#define GROUP_LOCK(pbb) g_mutex_lock (pbb->group_lock)
#define GROUP_UNLOCK(pbb) g_mutex_unlock (pbb->group_lock)
#define GROUP_WAIT(pbb) g_cond_wait (pbb->group_cond, pbb->group_lock)
#define GROUP_SIGNAL(pbb) g_cond_signal (pbb->group_cond)

#ifndef GST_HAVE_GLIB_2_8
#define _gst_gvalue_set_gstobject(gvalue,obj)  \
      if (obj != NULL) {                       \
        gst_object_ref (obj);                  \
        g_value_set_object (gvalue, obj);      \
        g_object_unref (obj);                  \
      } else {                                 \
        g_value_set_object (gvalue, NULL);     \
      }
#else
#define _gst_gvalue_set_gstobject(gvalue,obj)  \
      g_value_set_object (gvalue, obj);
#endif

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

static void gst_play_base_bin_class_init (GstPlayBaseBinClass * klass);
static void gst_play_base_bin_init (GstPlayBaseBin * play_base_bin);
static void gst_play_base_bin_dispose (GObject * object);
static void gst_play_base_bin_finalize (GObject * object);

static void gst_play_base_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * spec);
static void gst_play_base_bin_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * spec);
static GstStateChangeReturn gst_play_base_bin_change_state (GstElement *
    element, GstStateChange transition);
const GList *gst_play_base_bin_get_streaminfo (GstPlayBaseBin * play_base_bin);

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

  parent_class = g_type_class_ref (GST_TYPE_PIPELINE);

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

  gobject_klass->dispose = GST_DEBUG_FUNCPTR (gst_play_base_bin_dispose);
  gobject_klass->finalize = GST_DEBUG_FUNCPTR (gst_play_base_bin_finalize);

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

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_play_base_bin_finalize (GObject * object)
{
  GstPlayBaseBin *play_base_bin = GST_PLAY_BASE_BIN (object);

  g_mutex_free (play_base_bin->group_lock);
  g_cond_free (play_base_bin->group_cond);

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
        gst_bin_remove (GST_BIN (play_base_bin), fakesrc);
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
  gboolean res;

  GROUP_LOCK (play_base_bin);
  group = play_base_bin->building_group;
  had_active_group = (get_active_group (play_base_bin) != NULL);

  /* if an element signalled a no-more-pads after we stopped due
   * to preroll, the group is NULL. This is not an error */
  if (group == NULL) {
    if (!fatal) {
      GROUP_UNLOCK (play_base_bin);
      return;
    } else {
      GST_DEBUG ("Group loading failed, bailing out");
    }
  } else if (!subtitle) {
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

      if (sig_id) {
        GST_LOG ("removing preroll signal %s", GST_ELEMENT_NAME (element));
        g_signal_handler_disconnect (G_OBJECT (element), sig_id);
      }
    }
  }

  GST_DEBUG ("signal group done");
  GROUP_SIGNAL (play_base_bin);
  GST_DEBUG ("signaled group done");

  if (!subtitle && !had_active_group) {
    if (!prepare_output (play_base_bin)) {
      GROUP_UNLOCK (play_base_bin);
      return;
    }

    setup_substreams (play_base_bin);
    GST_DEBUG ("Emitting signal");
    res = GST_PLAY_BASE_BIN_GET_CLASS (play_base_bin)->
        setup_output_pads (play_base_bin, group);
    GST_DEBUG ("done");

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
  gst_element_post_message (GST_ELEMENT (play_base_bin),
      gst_message_new_custom (GST_MESSAGE_BUFFERING,
          GST_OBJECT (play_base_bin),
          gst_structure_new ("GstMessageBuffering",
              "buffer-percent", G_TYPE_INT, percent, NULL)));
}

static gboolean
check_queue (GstPad * pad, GstBuffer * data, gpointer user_data)
{
  GstElement *queue = GST_ELEMENT (user_data);
  GstPlayBaseBin *play_base_bin = g_object_get_data (G_OBJECT (queue), "pbb");
  guint64 level = 0;

  GST_DEBUG ("check queue triggered");

  g_object_get (G_OBJECT (queue), "current-level-time", &level, NULL);
  GST_DEBUG ("Queue size: %" GST_TIME_FORMAT, GST_TIME_ARGS (level));
  if (play_base_bin->queue_threshold > 0) {
    level = level * 100 / play_base_bin->queue_threshold;
    if (level > 100)
      level = 100;
  } else
    level = 100;

  fill_buffer (play_base_bin, level);

  /* continue! */
  return TRUE;
}

/* this signal will be fired when one of the queues with raw
 * data is filled. This means that the group building stage is over 
 * and playback of the new queued group should start */
static void
queue_overrun (GstElement * element, GstPlayBaseBin * play_base_bin)
{
  GST_DEBUG ("queue %s overrun", GST_ELEMENT_NAME (element));

  group_commit (play_base_bin, FALSE,
      GST_OBJECT_PARENT (GST_OBJECT_CAST (element)) ==
      GST_OBJECT (play_base_bin->subtitle));

  g_signal_handlers_disconnect_by_func (element,
      G_CALLBACK (queue_overrun), play_base_bin);
  /* We have disconnected this signal, remove the signal_id from the object
     data */
  g_object_set_data (G_OBJECT (element), "signal_id", NULL);
}

/* Used for time-based buffering. */
static void
queue_threshold_reached (GstElement * queue, GstPlayBaseBin * play_base_bin)
{
  gpointer data;

  GST_DEBUG ("Running");

  /* play */
  g_object_set (queue, "min-threshold-time", (guint64) 0, NULL);

  data = g_object_get_data (G_OBJECT (queue), "probe");
  if (data) {
    GstPad *sinkpad;

    sinkpad = gst_element_get_pad (queue, "sink");
    GST_DEBUG_OBJECT (play_base_bin,
        "Removing buffer probe from pad %s:%s (%p)",
        GST_DEBUG_PAD_NAME (sinkpad), sinkpad);

    fill_buffer (play_base_bin, 100);

    g_object_set_data (G_OBJECT (queue), "probe", NULL);
    gst_pad_remove_buffer_probe (sinkpad, GPOINTER_TO_INT (data));

    gst_object_unref (sinkpad);
  }
}

static void
queue_out_of_data (GstElement * queue, GstPlayBaseBin * play_base_bin)
{
  GST_DEBUG ("Underrun, re-caching");

  /* On underrun, we want to temoprarily pause playback, set a "min-size"
   * threshold and wait for the running signal and then play again. Take
   * care of possible deadlocks and so on, */
  g_object_set (queue, "min-threshold-time",
      (guint64) play_base_bin->queue_threshold, NULL);

  /* re-connect probe */
  if (!g_object_get_data (G_OBJECT (queue), "probe")) {
    GstPad *sinkpad;
    guint id;

    sinkpad = gst_element_get_pad (queue, "sink");
    id = gst_pad_add_buffer_probe (sinkpad, G_CALLBACK (check_queue), queue);
    g_object_set_data (G_OBJECT (queue), "probe", GINT_TO_POINTER (id));
    GST_DEBUG_OBJECT (play_base_bin,
        "Re-attaching buffering probe to pad %s:%s",
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
  guint sig;
  GstPad *preroll_pad;

  if (type == GST_STREAM_TYPE_VIDEO)
    prename = "video";
  else if (type == GST_STREAM_TYPE_TEXT)
    prename = "text";
  else if (type == GST_STREAM_TYPE_AUDIO)
    prename = "audio";
  else
    g_return_if_reached ();

  /* create stream selector */
  selector = g_object_new (GST_TYPE_STREAM_SELECTOR, NULL);
  padname = gst_pad_get_name (pad);
  name = g_strdup_printf ("selector_%s_%s", prename, padname);
  gst_object_set_name (GST_OBJECT (selector), name);
  g_free (name);

  /* create preroll queue */
  name = g_strdup_printf ("preroll_%s_%s", prename, padname);
  preroll = gst_element_factory_make ("queue", name);
  g_free (name);
  g_free (padname);

  g_object_set (G_OBJECT (preroll),
      "max-size-buffers", 0, "max-size-bytes",
      ((type == GST_STREAM_TYPE_VIDEO) ? 25 : 1) * 1024 * 1024,
      "max-size-time", play_base_bin->queue_size, NULL);

  sig = g_signal_connect (G_OBJECT (preroll), "overrun",
      G_CALLBACK (queue_overrun), play_base_bin);

  if (play_base_bin->is_stream &&
      ((type == GST_STREAM_TYPE_VIDEO &&
              group->type[GST_STREAM_TYPE_AUDIO - 1].npads == 0) ||
          (type == GST_STREAM_TYPE_AUDIO &&
              group->type[GST_STREAM_TYPE_VIDEO - 1].npads == 0))) {
    GstPad *sinkpad;
    guint id;

    g_signal_connect (G_OBJECT (preroll), "running",
        G_CALLBACK (queue_threshold_reached), play_base_bin);
    g_object_set (G_OBJECT (preroll),
        "min-threshold-time", (guint64) play_base_bin->queue_threshold, NULL);

    /* give updates on queue size */
    sinkpad = gst_element_get_pad (preroll, "sink");
    id = gst_pad_add_buffer_probe (sinkpad, G_CALLBACK (check_queue), preroll);
    GST_DEBUG_OBJECT (play_base_bin, "Attaching probe to pad %s:%s (%p)",
        GST_DEBUG_PAD_NAME (sinkpad), sinkpad);
    gst_object_unref (sinkpad);
    g_object_set_data (G_OBJECT (preroll), "pbb", play_base_bin);
    g_object_set_data (G_OBJECT (preroll), "probe", GINT_TO_POINTER (id));

    g_signal_connect (G_OBJECT (preroll), "underrun",
        G_CALLBACK (queue_out_of_data), play_base_bin);
  }
  /* keep a ref to the signal id so that we can disconnect the signal callback
   * when we are done with the preroll */
  g_object_set_data (G_OBJECT (preroll), "signal_id", GINT_TO_POINTER (sig));

  /* listen for EOS */
  preroll_pad = gst_element_get_pad (preroll, "src");
  gst_pad_add_event_probe (preroll_pad, G_CALLBACK (probe_triggered), info);
  gst_object_unref (preroll_pad);

  /* add to group list */
  group->type[type - 1].selector = selector;
  group->type[type - 1].preroll = preroll;

  /* gst_bin_add takes ownership, so we need to take a ref beforehand */
  gst_object_ref (preroll);
  gst_object_ref (selector);
  if (type == GST_STREAM_TYPE_TEXT && play_base_bin->subtitle) {
    group->type[type - 1].bin = GST_BIN (play_base_bin->subtitle);
    gst_bin_add (GST_BIN (play_base_bin->subtitle), selector);
    gst_bin_add (GST_BIN (play_base_bin->subtitle), preroll);
  } else {
    group->type[type - 1].bin = GST_BIN (play_base_bin);
    gst_bin_add (GST_BIN (play_base_bin), selector);
    gst_bin_add (GST_BIN (play_base_bin), preroll);
  }
  gst_element_link (selector, preroll);

  gst_element_set_state (selector,
      GST_STATE (play_base_bin) == GST_STATE_PLAYING ?
      GST_STATE_PLAYING : GST_STATE_PAUSED);
  gst_element_set_state (preroll,
      GST_STATE (play_base_bin) == GST_STATE_PLAYING ?
      GST_STATE_PLAYING : GST_STATE_PAUSED);

  gst_object_unref (preroll);
  gst_object_unref (selector);
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
    gst_bin_remove (GST_BIN (play_base_bin), play_base_bin->subtitle);
    play_base_bin->subtitle = NULL;
  }

  GROUP_UNLOCK (play_base_bin);
}

/* 
 * Add/remove a single stream to current  building group.
 *
 * Must be called with group-lock held.
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

/*
 * signal fired when an unknown stream is found. We create a new
 * UNKNOWN streaminfo object.
 */
static void
unknown_type (GstElement * element, GstPad * pad, GstCaps * caps,
    GstPlayBaseBin * play_base_bin)
{
  gchar *capsstr;
  GstStreamInfo *info;
  GstPlayBaseGroup *group;

  capsstr = gst_caps_to_string (caps);
  g_message ("don't know how to handle %s", capsstr);

  GROUP_LOCK (play_base_bin);

  group = get_building_group (play_base_bin);

  /* add the stream to the list */
  info = gst_stream_info_new (GST_OBJECT (pad), GST_STREAM_TYPE_UNKNOWN,
      NULL, caps);
  info->origin = GST_OBJECT (pad);
  add_stream (group, info);

  GROUP_UNLOCK (play_base_bin);

  g_free (capsstr);
}

#if 0
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
      gst_stream_info_new (GST_OBJECT (element), GST_STREAM_TYPE_ELEMENT,
      NULL, NULL);
  info->origin = GST_OBJECT (element);
  add_stream (group, info);

  GROUP_UNLOCK (play_base_bin);
}
#endif

/* when the decoder element signals that no more pads will be generated, we
 * can commit the current group.
 */
static void
no_more_pads (GstElement * element, GstPlayBaseBin * play_base_bin)
{
  /* setup phase */
  GST_DEBUG ("no more pads");

  /* we can commit this group for playback now */
  group_commit (play_base_bin, play_base_bin->is_stream,
      GST_OBJECT_PARENT (GST_OBJECT_CAST (element)) ==
      GST_OBJECT (play_base_bin->subtitle));
}

static gboolean
probe_triggered (GstPad * pad, GstEvent * event, gpointer user_data)
{
  GstPlayBaseGroup *group;
  GstPlayBaseBin *play_base_bin;
  GstStreamInfo *info = GST_STREAM_INFO (user_data);
  gboolean res;

  group = (GstPlayBaseGroup *) g_object_get_data (G_OBJECT (info), "group");
  play_base_bin = group->bin;

  GST_DEBUG ("probe triggered");

  if (GST_EVENT_TYPE (event) == GST_EVENT_EOS) {
    gint num_groups = 0;
    gboolean have_left;

    GST_DEBUG ("probe got EOS in group %p", group);

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
      res = GST_PLAY_BASE_BIN_GET_CLASS (play_base_bin)->
          setup_output_pads (play_base_bin, group);

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
  g_object_set (G_OBJECT (fakesrc), "num_buffers", 0, NULL);

  GST_DEBUG ("patching unlinked pad %s:%s", GST_DEBUG_PAD_NAME (pad));

  srcpad = gst_element_get_pad (fakesrc, "src");
  gst_bin_add (GST_BIN (play_base_bin), fakesrc);
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
  guint sig;
  GstObject *parent;

  GST_DEBUG ("play base: new decoded pad %d", last);

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

  parent = gst_object_get_parent (GST_OBJECT (element));
  if (g_str_has_prefix (mimetype, "audio/") &&
      parent != GST_OBJECT_CAST (play_base_bin->subtitle)) {
    type = GST_STREAM_TYPE_AUDIO;
  } else if (g_str_has_prefix (mimetype, "video/") &&
      parent != GST_OBJECT_CAST (play_base_bin->subtitle)) {
    type = GST_STREAM_TYPE_VIDEO;
  } else if (g_str_has_prefix (mimetype, "text/")) {
    type = GST_STREAM_TYPE_TEXT;
  } else {
    type = GST_STREAM_TYPE_UNKNOWN;
  }
  gst_object_unref (parent);

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
  /* Store a pointer to the stream selector pad for this stream */
  g_object_set_data (G_OBJECT (pad), "pb_sel_pad", sinkpad);

  gst_pad_link (pad, sinkpad);
  gst_object_unref (sinkpad);

  /* add the stream to the list */
  gst_caps_unref (caps);
  info->origin = GST_OBJECT (pad);

  /* select 1st for now - we'll select a preferred one after preroll */
  if (type == GST_STREAM_TYPE_UNKNOWN || group->type[type - 1].npads > 0) {
    guint id;

    GST_DEBUG ("Adding silence_stream data probe on type %d (npads %d)", type,
        group->type[type - 1].npads);

    id = gst_pad_add_data_probe (GST_PAD_CAST (pad),
        G_CALLBACK (silence_stream), info);
    g_object_set_data (G_OBJECT (pad), "eat_probe", GINT_TO_POINTER (id));
  }

  add_stream (group, info);

  GROUP_UNLOCK (play_base_bin);

  /* signal the no more pads after adding the stream */
  if (last)
    no_more_pads (element, play_base_bin);

  return;

no_type:
  {
    g_warning ("no type on pad %s:%s", GST_DEBUG_PAD_NAME (pad));
    if (caps)
      gst_caps_unref (caps);
    return;
  }
}


/*
 * Generate source ! subparse bins.
 */

static GstElement *
setup_subtitle (GstPlayBaseBin * play_base_bin, gchar * sub_uri)
{
  GstElement *source, *subparse, *subbin;

  source = gst_element_make_from_uri (GST_URI_SRC, sub_uri, NULL);
  if (!source)
    return NULL;
  subparse = gst_element_factory_make ("decodebin", "subtitle-decoder");

  subbin = gst_bin_new ("subtitle-bin");
  gst_bin_add_many (GST_BIN (subbin), source, subparse, NULL);
  gst_element_link (source, subparse);

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
      play_base_bin->current[n] = 0;
    }
  }

  /* now activate the right sources. Don't forget that during preroll,
   * we set the first source to forwarding and ignored the rest. */
  for (n = 0; n < NUM_TYPES; n++) {
    set_active_source (play_base_bin, n + 1, play_base_bin->current[n]);
  }
}

/* construct and run the source and decoder elements until we found
 * all the streams or until a preroll queue has been filled.
*/
static gboolean
setup_source (GstPlayBaseBin * play_base_bin, gchar ** new_location)
{
  GstElement *subbin = NULL;

  if (!play_base_bin->need_rebuild)
    return TRUE;

  /* delete old src */
  if (play_base_bin->source) {
    GST_DEBUG_OBJECT (play_base_bin, "removing old src element");
    gst_element_set_state (play_base_bin->source, GST_STATE_NULL);
    gst_bin_remove (GST_BIN (play_base_bin), play_base_bin->source);
  }

  /* create and configure an element that can handle the uri */
  if (!(play_base_bin->source = gen_source_element (play_base_bin, &subbin)))
    goto no_source;

  gst_bin_add (GST_BIN (play_base_bin), play_base_bin->source);
  g_object_notify (G_OBJECT (play_base_bin), "source");

  /* state will be merged later - if file is not found, error will be
   * handled by the application right after. */

  /* remove the old decoder now, if any */
  if (play_base_bin->decoder) {
    GST_DEBUG_OBJECT (play_base_bin, "removing old decoder element");
    gst_element_set_state (play_base_bin->decoder, GST_STATE_NULL);
    gst_bin_remove (GST_BIN (play_base_bin), play_base_bin->decoder);
    play_base_bin->decoder = NULL;
  }

  /* remove our previous preroll queues */
  remove_groups (play_base_bin);

  /* do subs */
  if (subbin) {
    GstElement *db;

    play_base_bin->subtitle = subbin;
    db = gst_bin_get_by_name (GST_BIN (subbin), "subtitle-decoder");

    /* do type detection, without adding (so no preroll) */
    g_signal_connect (G_OBJECT (db), "new-decoded-pad",
        G_CALLBACK (new_decoded_pad), play_base_bin);
    g_signal_connect (G_OBJECT (db), "no-more-pads",
        G_CALLBACK (no_more_pads), play_base_bin);
    g_signal_connect (G_OBJECT (db), "unknown-type",
        G_CALLBACK (unknown_type), play_base_bin);

    if (!play_base_bin->is_stream) {
      /* either when the queues are filled or when the decoder element
       * has no more dynamic streams, the cond is unlocked. We can remove
       * the signal handlers then
       */

      gst_element_set_state (subbin, GST_STATE_PAUSED);

      GROUP_LOCK (play_base_bin);
      GST_DEBUG ("waiting for first group...");
      GROUP_WAIT (play_base_bin);
      GST_DEBUG ("group done !");
      GROUP_UNLOCK (play_base_bin);

      if (!play_base_bin->building_group ||
          play_base_bin->building_group->type[GST_STREAM_TYPE_TEXT - 1].npads ==
          0) {

        GST_DEBUG ("No subtitle found - ignoring");
        gst_element_set_state (subbin, GST_STATE_NULL);
        gst_object_unref (GST_OBJECT (play_base_bin->subtitle));
        play_base_bin->subtitle = NULL;
      } else {
        GST_DEBUG ("Subtitle set-up successful");
      }
    }
  }

  /* now see if the source element emits raw audio/video all by itself,
   * if so, we can create streams for the pads and be done with it.
   * Also check that is has source pads, if not, we assume it will
   * do everything itself.
   */
  {
    GstIterator *pads_iter;
    gboolean done = FALSE;
    gboolean no_out = TRUE, is_raw = FALSE;

    pads_iter = gst_element_iterate_pads (play_base_bin->source);
    while (!done) {
      gpointer data;

      switch (gst_iterator_next (pads_iter, &data)) {
        case GST_ITERATOR_DONE:
          done = TRUE;
        case GST_ITERATOR_ERROR:
        case GST_ITERATOR_RESYNC:
          break;
        case GST_ITERATOR_OK:
        {
          GstCaps *caps;
          GstPad *pad = GST_PAD (data);
          guint i, num_raw = 0;

          if (GST_PAD_IS_SINK (pad))
            break;

          no_out = FALSE;

          caps = gst_pad_get_caps (pad);
          if (caps == NULL || gst_caps_is_empty (caps) ||
              gst_caps_get_size (caps) == 0) {
            if (caps)
              gst_caps_unref (caps);
            break;
          }

          for (i = 0; i < gst_caps_get_size (caps); ++i) {
            GstStructure *s;
            const gchar *mime_type;

            s = gst_caps_get_structure (caps, i);
            mime_type = gst_structure_get_name (s);

            if (g_str_has_prefix (mime_type, "audio/x-raw") ||
                g_str_has_prefix (mime_type, "video/x-raw")) {
              ++num_raw;
            }
          }

          /* if possible caps on source pad are all raw, just add the pad */
          if (num_raw == gst_caps_get_size (caps)) {
            new_decoded_pad (play_base_bin->source, pad, FALSE, play_base_bin);
            is_raw = TRUE;
          } else if (num_raw > 0 && num_raw < gst_caps_get_size (caps)) {
            g_warning ("FIXME: handling of mixed raw/coded caps on source");
          }

          break;
        }
      }
    }

    gst_iterator_free (pads_iter);

    if (is_raw) {
      no_more_pads (play_base_bin->source, play_base_bin);
      return TRUE;
    }
#if 0
    if (no_out) {
      /* create a stream to indicate that this uri is handled by a self
       * contained element */
      add_element_stream (play_base_bin->source, play_base_bin);
      no_more_pads (play_base_bin->source, play_base_bin);
      return TRUE;
    }
#endif
  }

  /* now create the decoder element */
  if (!(play_base_bin->decoder =
          gst_element_factory_make ("decodebin", "decoder")))
    goto no_decodebin;

  gst_bin_add (GST_BIN (play_base_bin), play_base_bin->decoder);

  if (!gst_element_link (play_base_bin->source, play_base_bin->decoder))
    goto could_not_link;

  /* set up callbacks to create the links between decoded data
   * and video/audio/subtitle rendering/output. */
  g_signal_connect (G_OBJECT (play_base_bin->decoder),
      "new-decoded-pad", G_CALLBACK (new_decoded_pad), play_base_bin);
  g_signal_connect (G_OBJECT (play_base_bin->decoder), "no-more-pads",
      G_CALLBACK (no_more_pads), play_base_bin);
  g_signal_connect (G_OBJECT (play_base_bin->decoder),
      "unknown-type", G_CALLBACK (unknown_type), play_base_bin);

  if (play_base_bin->subtitle) {
    gst_bin_add (GST_BIN (play_base_bin), play_base_bin->subtitle);
  }

  play_base_bin->need_rebuild = FALSE;

  return TRUE;

  /* ERRORS */
no_source:
  {
    gchar *prot;

    /* whoops, could not create the source element */
    if (play_base_bin->uri == NULL) {
      GST_ELEMENT_ERROR (play_base_bin, RESOURCE, NOT_FOUND,
          (_("No URI specified to play from.")), (NULL));
      return FALSE;
    }
    prot = gst_uri_get_protocol (play_base_bin->uri);
    if (prot) {
      GST_ELEMENT_ERROR (play_base_bin, RESOURCE, FAILED,
          (_("No URI handler implemented for \"%s\"."), prot), (NULL));
      g_free (prot);
    } else {
      GST_ELEMENT_ERROR (play_base_bin, RESOURCE, NOT_FOUND,
          (_("Invalid URI \"%s\"."), play_base_bin->uri), (NULL));
    }
    return FALSE;
  }
no_decodebin:
  {
    GST_ELEMENT_ERROR (play_base_bin, CORE, FAILED,
        (_("Could not create \"decodebin\" element.")), (NULL));
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
  if (get_active_group (play_base_bin) != NULL) {
    if (play_base_bin->subtitle) {
      /* make subs iterate from now on */
      gst_bin_add (GST_BIN (play_base_bin), play_base_bin->subtitle);
    }
  }
}

/*
 * Caller must have group-lock held.
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

#if 0
static void muted_group_change_state (GstElement * element,
    gint old_state, gint new_state, gpointer data);
#endif

static void
mute_group_type (GstPlayBaseGroup * group, GstStreamType type, gboolean mute)
{
  gboolean active = !mute;
  GstPad *pad;

  pad = gst_element_get_pad (group->type[type - 1].preroll, "src");
  gst_pad_set_active (pad, active);
  gst_object_unref (pad);
  pad = gst_element_get_pad (group->type[type - 1].preroll, "sink");
  gst_pad_set_active (pad, active);
  gst_object_unref (pad);
  pad = gst_element_get_pad (group->type[type - 1].selector, "src");
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
          g_object_set (G_OBJECT (sel), "active-pad", GST_PAD_NAME (sel_pad),
              NULL);
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
    g_object_set (sel, "active-pad", "", NULL);
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
    case ARG_STREAMINFO:
      /* FIXME: hold some kind of lock here, use iterator */
      g_value_set_pointer (value,
          (gpointer) gst_play_base_bin_get_streaminfo (play_base_bin));
      break;
    case ARG_SOURCE:
      _gst_gvalue_set_gstobject (value, play_base_bin->source);
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
  gchar *new_location = NULL;

  play_base_bin = GST_PLAY_BASE_BIN (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (!setup_source (play_base_bin, &new_location))
        goto source_failed;
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (ret != GST_STATE_CHANGE_FAILURE) {
        finish_source (play_base_bin);
      } else {
        /* clean up leftover groups */
        remove_groups (play_base_bin);
        play_base_bin->need_rebuild = TRUE;
      }
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      play_base_bin->need_rebuild = TRUE;
      remove_groups (play_base_bin);
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
