/* GStreamer GES plugin
 *
 * Copyright (C) 2019 Thibault Saunier <tsaunier@igalia.com>
 *
 * gesbasebin.h
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
 *
 */

#include "gesbasebin.h"

static GstStaticPadTemplate video_src_template =
GST_STATIC_PAD_TEMPLATE ("video_src",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("video/x-raw(ANY)"));

static GstStaticPadTemplate audio_src_template =
    GST_STATIC_PAD_TEMPLATE ("audio_src",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("audio/x-raw(ANY);"));

typedef struct
{
  GstPad *ghost_pad;
  GstElement *queue;
} TrackInfo;

typedef struct
{
  GESTimeline *timeline;
  GstFlowCombiner *flow_combiner;

  GHashTable *tracks;
  gulong track_removed_sigid;
} GESBaseBinPrivate;

enum
{
  PROP_0,
  PROP_TIMELINE,
  PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

G_DEFINE_TYPE_WITH_PRIVATE (GESBaseBin, ges_base_bin, GST_TYPE_BIN);

GST_DEBUG_CATEGORY_STATIC (gesbasebin);
#define GST_CAT_DEFAULT gesbasebin

static void
ges_base_bin_dispose (GObject * object)
{
  GESBaseBin *self = GES_BASE_BIN (object);
  GESBaseBinPrivate *priv = ges_base_bin_get_instance_private (self);

  GST_OBJECT_LOCK (self);
  if (priv->tracks) {
    g_clear_pointer (&priv->tracks, g_hash_table_unref);
  }

  if (priv->timeline) {
    g_signal_handler_disconnect (priv->timeline, priv->track_removed_sigid);
    priv->track_removed_sigid = 0;
    GST_OBJECT_UNLOCK (self);

    gst_bin_remove (GST_BIN (self), GST_ELEMENT (priv->timeline));

    GST_OBJECT_LOCK (self);
    gst_clear_object (&priv->timeline);
  }

  GST_OBJECT_UNLOCK (self);
}

static void
ges_base_bin_finalize (GObject * object)
{
  GESBaseBin *self = GES_BASE_BIN (object);
  GESBaseBinPrivate *priv = ges_base_bin_get_instance_private (self);

  gst_flow_combiner_free (priv->flow_combiner);
}

static void
ges_base_bin_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GESBaseBin *self = GES_BASE_BIN (object);
  GESBaseBinPrivate *priv = ges_base_bin_get_instance_private (self);

  switch (property_id) {
    case PROP_TIMELINE:
      g_value_set_object (value, priv->timeline);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_base_bin_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GESBaseBin *self = GES_BASE_BIN (object);

  switch (property_id) {
    case PROP_TIMELINE:
      ges_base_bin_set_timeline (self, g_value_get_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_base_bin_class_init (GESBaseBinClass * self_class)
{
  GObjectClass *gclass = G_OBJECT_CLASS (self_class);
  GstElementClass *gstelement_klass = GST_ELEMENT_CLASS (self_class);

  GST_DEBUG_CATEGORY_INIT (gesbasebin, "gesbasebin", 0, "ges bin element");

  gst_tag_register ("is-ges-timeline", GST_TAG_FLAG_META, G_TYPE_BOOLEAN,
      "is-ges-timeline", "The stream is a ges timeline.", NULL);

  gclass->get_property = ges_base_bin_get_property;
  gclass->set_property = ges_base_bin_set_property;
  gclass->dispose = ges_base_bin_dispose;
  gclass->finalize = ges_base_bin_finalize;

  /**
   * GESBaseBin:timeline:
   *
   * Timeline to use in this bin.
   *
   * Since: 1.16
   */
  properties[PROP_TIMELINE] = g_param_spec_object ("timeline", "Timeline",
      "Timeline to use in this src.",
      GES_TYPE_TIMELINE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gclass, PROP_LAST, properties);

  gst_element_class_add_pad_template (gstelement_klass,
      gst_static_pad_template_get (&video_src_template));
  gst_element_class_add_pad_template (gstelement_klass,
      gst_static_pad_template_get (&audio_src_template));

  gst_type_mark_as_plugin_api (ges_base_bin_get_type (), 0);
}

static void
ges_base_bin_init (GESBaseBin * self)
{
  GESBaseBinPrivate *priv = ges_base_bin_get_instance_private (self);

  ges_init ();

  priv->flow_combiner = gst_flow_combiner_new ();
  priv->tracks = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL,
      (GDestroyNotify) g_free);
}

static gboolean
ges_base_bin_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_STREAM_START:
    {
      const gchar *stream_id;
      gchar *new_stream_id;
      guint stream_group;
      GstTagList *tlist = gst_tag_list_new ("is-ges-timeline", TRUE, NULL);
      GstPad *peer = gst_pad_get_peer (pad);
      GstEvent *new_event;

      gst_event_parse_stream_start (event, &stream_id);
      new_stream_id =
          gst_pad_create_stream_id (peer,
          GST_ELEMENT (GST_OBJECT_PARENT (parent)), stream_id);
      gst_object_unref (peer);

      new_event = gst_event_new_stream_start (new_stream_id);
      if (gst_event_parse_group_id (event, &stream_group))
        gst_event_set_group_id (new_event, stream_group);
      gst_event_unref (event);
      g_free (new_stream_id);

      gst_pad_event_default (pad, parent, new_event);

      gst_tag_list_set_scope (tlist, GST_TAG_SCOPE_GLOBAL);

      return gst_pad_send_event (pad, gst_event_new_tag (tlist));
    }
    default:
      break;
  }

  return gst_pad_event_default (pad, parent, event);
}

static GstFlowReturn
ges_base_bin_src_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstFlowReturn result, chain_result;
  GESBaseBin *self = GES_BASE_BIN (GST_OBJECT_PARENT (parent));
  GESBaseBinPrivate *priv = ges_base_bin_get_instance_private (self);

  chain_result = gst_proxy_pad_chain_default (pad, GST_OBJECT (self), buffer);
  result =
      gst_flow_combiner_update_pad_flow (priv->flow_combiner, pad,
      chain_result);

  if (result == GST_FLOW_FLUSHING)
    return chain_result;

  return result;
}

static void
ges_base_bin_track_removed_cb (GESTimeline * timeline, GESTrack * track,
    GESBaseBin * self)
{
  GESBaseBinPrivate *priv = ges_base_bin_get_instance_private (self);

  GST_OBJECT_LOCK (self);
  TrackInfo *info = g_hash_table_lookup (priv->tracks, track);
  if (!info) {
    GST_OBJECT_UNLOCK (self);

    return;
  }
  GstPad *ghost = gst_object_ref (info->ghost_pad);
  GstElement *queue = gst_object_ref (info->queue);

  g_hash_table_remove (priv->tracks, track);
  GST_OBJECT_UNLOCK (self);

  gst_element_remove_pad (GST_ELEMENT (self), ghost);
  gst_element_set_state (queue, GST_STATE_NULL);
  gst_bin_remove (GST_BIN (self), queue);

  gst_object_unref (queue);
  gst_object_unref (ghost);
}


gboolean
ges_base_bin_set_timeline (GESBaseBin * self, GESTimeline * timeline)
{
  GList *tmp;
  guint naudiopad = 0, nvideopad = 0;
  GstBin *sbin = GST_BIN (self);
  GESBaseBinPrivate *priv = ges_base_bin_get_instance_private (self);

  g_return_val_if_fail (GES_IS_TIMELINE (timeline), FALSE);

  if (priv->timeline) {
    GST_ERROR_OBJECT (sbin, "Implement changing timeline support");

    return FALSE;
  }

  priv->timeline = gst_object_ref (timeline);
  /* FIXME Handle track-added */
  priv->track_removed_sigid = g_signal_connect (timeline, "track-removed",
      G_CALLBACK (ges_base_bin_track_removed_cb), self);
  GST_INFO_OBJECT (sbin, "Setting timeline: %" GST_PTR_FORMAT, timeline);
  gst_element_set_locked_state (GST_ELEMENT (timeline), TRUE);
  if (!gst_bin_add (sbin, GST_ELEMENT (timeline))) {
    GST_ERROR_OBJECT (sbin, "Could not add timeline to myself!");

    return FALSE;
  }

  ges_timeline_commit (timeline);
  for (tmp = timeline->tracks; tmp; tmp = tmp->next) {
    GstPad *gpad;
    gchar *name = NULL;
    GstElement *queue;
    GESTrack *track = GES_TRACK (tmp->data);
    GstPad *proxy_pad, *tmppad, *pad =
        ges_timeline_get_pad_for_track (timeline, track);
    GstStaticPadTemplate *template;

    if (!pad) {
      GST_WARNING_OBJECT (sbin, "No pad for track: %" GST_PTR_FORMAT, track);

      continue;
    }

    if (track->type == GES_TRACK_TYPE_AUDIO) {
      name = g_strdup_printf ("audio_%u", naudiopad++);
      template = &audio_src_template;
    } else if (track->type == GES_TRACK_TYPE_VIDEO) {
      name = g_strdup_printf ("video_%u", nvideopad++);
      template = &video_src_template;
    } else {
      GST_INFO_OBJECT (sbin, "Track type not handled: %" GST_PTR_FORMAT, track);
      continue;
    }

    queue = gst_element_factory_make ("queue", NULL);
    /* Add queues the same way as in GESPipeline */
    g_object_set (G_OBJECT (queue), "max-size-buffers", 0,
        "max-size-bytes", 0, "max-size-time", (gint64) 2 * GST_SECOND, NULL);
    gst_bin_add (sbin, queue);
    gst_element_sync_state_with_parent (GST_ELEMENT (queue));

    tmppad = gst_element_get_static_pad (queue, "sink");
    if (gst_pad_link (pad, tmppad) != GST_PAD_LINK_OK) {
      GST_ERROR_OBJECT (sbin, "Could not link %s:%s and %s:%s",
          GST_DEBUG_PAD_NAME (pad), GST_DEBUG_PAD_NAME (tmppad));

      gst_object_unref (tmppad);
      gst_object_unref (queue);
      continue;
    }

    tmppad = gst_element_get_static_pad (queue, "src");
    gpad = gst_ghost_pad_new_from_template (name, tmppad,
        gst_static_pad_template_get (template));

    gst_pad_set_active (gpad, TRUE);
    gst_element_add_pad (GST_ELEMENT (sbin), gpad);

    TrackInfo *info = g_new0 (TrackInfo, 1);
    info->queue = queue;
    info->ghost_pad = gpad;

    GST_OBJECT_LOCK (self);
    g_hash_table_insert (priv->tracks, track, info);
    GST_OBJECT_UNLOCK (self);

    proxy_pad = GST_PAD (gst_proxy_pad_get_internal (GST_PROXY_PAD (gpad)));
    gst_flow_combiner_add_pad (priv->flow_combiner, proxy_pad);
    gst_pad_set_chain_function (proxy_pad, ges_base_bin_src_chain);
    gst_pad_set_event_function (proxy_pad, ges_base_bin_event);
    gst_object_unref (proxy_pad);

    GST_DEBUG_OBJECT (sbin,
        "Added pad: %" GST_PTR_FORMAT " for track %" GST_PTR_FORMAT, gpad,
        track);
  }

  gst_element_set_locked_state (GST_ELEMENT (timeline), FALSE);

  gst_element_no_more_pads (GST_ELEMENT (sbin));
  gst_element_sync_state_with_parent (GST_ELEMENT (timeline));

  return TRUE;
}

GESTimeline *
ges_base_bin_get_timeline (GESBaseBin * self)
{
  GESBaseBinPrivate *priv = ges_base_bin_get_instance_private (self);

  return priv->timeline;
}
