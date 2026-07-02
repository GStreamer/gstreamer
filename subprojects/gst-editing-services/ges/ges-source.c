/* GStreamer Editing Services
 * Copyright (C) 2009 Edward Hervey <edward.hervey@collabora.co.uk>
 *               2009 Nokia Corporation
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
 * SECTION:gessource
 * @title: GESSource
 * @short_description: Base Class for single-media sources
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ges-internal.h"
#include "ges/ges-meta-container.h"
#include "ges-track-element.h"
#include "ges-source.h"
#include "ges-clip.h"
#include "ges-base-effect.h"
#include "ges-layer.h"
#include "gstframepositioner.h"
struct _GESSourcePrivate
{
  GstElement *topbin;
  GstElement *first_converter;
  GstElement *last_converter;
  GstPad *ghostpad;

  GList *sub_element_probes;
  GMutex sub_element_lock;

  gboolean is_rendering_smartly;

  /* effects added through ges_source_add_effect() while the source was not yet
   * in a clip; added to the clip once the source is parented to one */
  GList *pending_effects;

  /* the track this source is meant to land in, when it was created from a
   * GESUriClip source-track-map; a GWeakRef so it never keeps the track alive.
   * @wanted_track_set tells "map-managed, unplaced (NULL track)" apart from
   * "not map-managed". */
  GWeakRef wanted_track;
  gboolean wanted_track_set;
};

typedef struct
{
  GESBaseEffect *effect;        /* owned (sunk) */
  gint index;
} PendingEffect;

static void
_pending_effect_free (gpointer data)
{
  PendingEffect *pe = data;

  gst_object_unref (pe->effect);
  g_free (pe);
}

G_DEFINE_TYPE_WITH_PRIVATE (GESSource, ges_source, GES_TYPE_TRACK_ELEMENT);

/******************************
 *   Internal helper methods  *
 ******************************/
static GstElement *
link_elements (GstElement * bin, GPtrArray * elements)
{
  GstElement *element, *prev = NULL, *first = NULL;
  gint i;

  for (i = 0; i < elements->len; i++) {
    element = elements->pdata[i];
    if (!element)
      continue;

    gst_bin_add (GST_BIN (bin), element);
    if (prev) {
      if (!gst_element_link_pads_full (prev, "src", element, "sink",
              GST_PAD_LINK_CHECK_NOTHING)) {
        if (!gst_element_link (prev, element)) {
          g_error ("Could not link %s and %s", GST_OBJECT_NAME (prev),
              GST_OBJECT_NAME (element));
        }
      }
    }
    prev = element;
    if (first == NULL)
      first = element;
  }

  return prev;
}

typedef struct
{
  GstPad *pad;
  gulong probe_id;
} ProbeData;

static GstPadProbeReturn
pad_probe_cb (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  return GST_PAD_PROBE_OK;
}

static void
_release_probe_data (ProbeData * pdata)
{
  gst_pad_remove_probe (pdata->pad, pdata->probe_id);
  gst_object_unref (pdata->pad);
  g_free (pdata);
}

static void
_no_more_pads_cb (GstElement * element, GESSource * self)
{
  GESSourcePrivate *priv = self->priv;

  GST_DEBUG_OBJECT (self,
      "Unblocking after no more pads from sub_element %" GST_PTR_FORMAT,
      element);
  g_mutex_lock (&priv->sub_element_lock);
  g_list_free_full (priv->sub_element_probes,
      (GDestroyNotify) _release_probe_data);
  priv->sub_element_probes = NULL;
  g_mutex_unlock (&priv->sub_element_lock);
}

static void
_set_ghost_pad_target (GESSource * self, GstPad * srcpad, GstElement * element)
{
  GstPadLinkReturn link_return;
  GESSourcePrivate *priv = self->priv;
  GESSourceClass *source_klass = GES_SOURCE_GET_CLASS (self);
  gboolean use_converter = !!priv->first_converter;

  if (source_klass->select_pad && !source_klass->select_pad (self, srcpad)) {
    GST_INFO_OBJECT (self, "Ignoring pad %" GST_PTR_FORMAT, srcpad);
    return;
  }


  if (use_converter && priv->is_rendering_smartly) {
    GstPad *pad = gst_element_get_static_pad (priv->first_converter, "sink");
    use_converter = gst_pad_can_link (srcpad, pad);
    gst_object_unref (pad);
  }

  if (use_converter) {
    GstPad *converter_src, *sinkpad;

    converter_src = gst_element_get_static_pad (priv->last_converter, "src");
    if (!gst_ghost_pad_set_target (GST_GHOST_PAD (priv->ghostpad),
            converter_src)) {
      GST_ERROR_OBJECT (self, "Could not set ghost target");
    }

    sinkpad = gst_element_get_static_pad (priv->first_converter, "sink");
    link_return = gst_pad_link (srcpad, sinkpad);
#ifndef GST_DISABLE_GST_DEBUG
    if (link_return != GST_PAD_LINK_OK) {
      GstCaps *srccaps = NULL;
      GstCaps *sinkcaps = NULL;

      srccaps = gst_pad_query_caps (srcpad, NULL);
      sinkcaps = gst_pad_query_caps (sinkpad, NULL);

      GST_ERROR_OBJECT (element, "Could not link source with "
          "conversion bin: %s (srcpad caps %" GST_PTR_FORMAT
          " sinkpad caps: %" GST_PTR_FORMAT ")",
          gst_pad_link_get_name (link_return), srccaps, sinkcaps);
      gst_caps_unref (srccaps);
      gst_caps_unref (sinkcaps);
    }
#endif

    gst_object_unref (converter_src);
    gst_object_unref (sinkpad);
  } else {
    if (!gst_ghost_pad_set_target (GST_GHOST_PAD (priv->ghostpad), srcpad))
      GST_ERROR_OBJECT (self, "Could not set ghost target");
  }
}

static void
_pad_added_cb (GstElement * element, GstPad * srcpad, GESSource * self)
{
  GESSourcePrivate *priv = self->priv;
  ProbeData *pdata = g_new0 (ProbeData, 1);

  GST_LOG_OBJECT (self, "blocking sub_element srcpad %" GST_PTR_FORMAT, srcpad);

  pdata->probe_id = gst_pad_add_probe (srcpad,
      GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM,
      (GstPadProbeCallback) pad_probe_cb, NULL, NULL);
  pdata->pad = gst_object_ref (srcpad);

  g_mutex_lock (&priv->sub_element_lock);
  priv->sub_element_probes = g_list_append (priv->sub_element_probes, pdata);
  g_mutex_unlock (&priv->sub_element_lock);

  _set_ghost_pad_target (self, srcpad, element);
}

/* @elements: (transfer-full) */
GstElement *
ges_source_create_topbin (GESSource * source, const gchar * bin_name,
    GstElement * sub_element, GPtrArray * elements)
{
  GstElement *last;
  GstElement *bin;
  GstPad *sub_srcpad;
  GESSourcePrivate *priv = source->priv;

  bin = gst_bin_new (bin_name);
  if (!gst_bin_add (GST_BIN (bin), sub_element)) {
    GST_ERROR_OBJECT (source, "Could not add sub element: %" GST_PTR_FORMAT,
        sub_element);
    gst_object_unref (bin);
    return NULL;
  }

  /* We may be creating an embedded topbin */
  gst_clear_object (&priv->first_converter);
  gst_clear_object (&priv->last_converter);
  gst_clear_object (&priv->topbin);
  gst_clear_object (&priv->ghostpad);

  priv->ghostpad = gst_object_ref (gst_ghost_pad_new_no_target ("src",
          GST_PAD_SRC));
  gst_pad_set_active (priv->ghostpad, TRUE);
  gst_element_add_pad (bin, priv->ghostpad);
  priv->topbin = gst_object_ref (bin);
  last = link_elements (bin, elements);
  if (last) {
    gint i = 0;

    while (!elements->pdata[i])
      i++;

    priv->first_converter = gst_object_ref (elements->pdata[i]);
    priv->last_converter = gst_object_ref (last);
  }

  sub_srcpad = gst_element_get_static_pad (sub_element, "src");
  if (sub_srcpad) {
    _set_ghost_pad_target (source, sub_srcpad, sub_element);
    gst_object_unref (sub_srcpad);
  } else {
    GST_INFO_OBJECT (source, "Waiting for pad added");
    g_signal_connect (sub_element, "pad-added",
        G_CALLBACK (_pad_added_cb), source);
    g_signal_connect (sub_element, "no-more-pads",
        G_CALLBACK (_no_more_pads_cb), source);
  }
  g_ptr_array_free (elements, TRUE);

  return bin;
}


void
ges_source_set_rendering_smartly (GESSource * source,
    gboolean is_rendering_smartly)
{

  if (is_rendering_smartly) {
    GESTrack *track = ges_track_element_get_track (GES_TRACK_ELEMENT (source));

    if (track && ges_track_get_mixing (track)) {
      GST_DEBUG_OBJECT (source, "Not rendering smartly as track is mixing!");

      source->priv->is_rendering_smartly = FALSE;
      return;
    }
  }
  source->priv->is_rendering_smartly = is_rendering_smartly;
}

gboolean
ges_source_get_rendering_smartly (GESSource * source)
{
  return source->priv->is_rendering_smartly;
}

static GstElement *
ges_source_create_nle_object (GESTrackElement * self)
{
  GParamSpec *pspec;
  GstElement *nleobject;

  nleobject =
      GES_TRACK_ELEMENT_CLASS (ges_source_parent_class)->create_gnl_object
      (self);

  if (!nleobject)
    return NULL;

  pspec =
      g_object_class_find_property (G_OBJECT_GET_CLASS (nleobject), "reverse");
  g_assert (pspec);


  if (!ges_timeline_element_add_child_property_full (GES_TIMELINE_ELEMENT
          (self), NULL, pspec, G_OBJECT (nleobject),
          GES_TIMELINE_ELEMENT_CHILD_PROP_FLAG_SET_ON_ALL_INSTANCES))
    GST_ERROR_OBJECT (self,
        "Could not register the child property 'reverse' for %" GST_PTR_FORMAT,
        nleobject);

  g_param_spec_unref (pspec);

  return nleobject;
}

static void
ges_source_dispose (GObject * object)
{
  GESSourcePrivate *priv = GES_SOURCE (object)->priv;

  gst_clear_object (&priv->first_converter);
  gst_clear_object (&priv->last_converter);
  gst_clear_object (&priv->topbin);
  gst_clear_object (&priv->ghostpad);
  g_list_free_full (priv->sub_element_probes,
      (GDestroyNotify) _release_probe_data);
  g_list_free_full (priv->pending_effects, _pending_effect_free);
  priv->pending_effects = NULL;
  g_weak_ref_clear (&priv->wanted_track);
  g_mutex_clear (&priv->sub_element_lock);

  G_OBJECT_CLASS (ges_source_parent_class)->dispose (object);
}

static void
ges_source_class_init (GESSourceClass * klass)
{
  GESTrackElementClass *track_class = GES_TRACK_ELEMENT_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  track_class->nleobject_factorytype = "nlesource";
  track_class->create_element = NULL;
  track_class->create_gnl_object = ges_source_create_nle_object;
  object_class->dispose = ges_source_dispose;

  GES_TRACK_ELEMENT_CLASS_DEFAULT_HAS_INTERNAL_SOURCE (klass) = TRUE;
}

/* Translate a per-source effect index (the position within @source's own
 * effect chain) into the clip-global index ges_clip_add_top_effect() expects,
 * and add @effect there. Effects bound to different sources live in different
 * tracks, so their relative (clip-global) order is irrelevant; only @source's
 * own chain position is honored. @src_index < 0, or beyond @source's current
 * count, appends. */
static gboolean
_clip_add_effect_for_source (GESClip * clip, GESSource * source,
    GESBaseEffect * effect, gint src_index, GError ** error)
{
  gint clip_index = -1;

  if (src_index >= 0) {
    GList *top = ges_clip_get_top_effects (clip);
    GList *tmp;
    gint seen = 0, global = 0;

    for (tmp = top; tmp; tmp = tmp->next, global ++) {
      GESSource * es = ges_base_effect_get_bound_source (tmp->data);
      gboolean same = (es == source);

      g_clear_object (&es);
      if (same) {
        if (seen == src_index) {
          clip_index = global;
          break;
        }
        seen++;
      }
    }
    g_list_free_full (top, gst_object_unref);
  }

  return ges_clip_add_top_effect (clip, effect, clip_index, error);
}

/* Once the source is parented to a clip, add any effects that were requested
 * through ges_source_add_effect() while it had no clip. */
static void
_source_parent_notify_cb (GESSource * self, GParamSpec * pspec, gpointer unused)
{
  GESTimelineElement *parent = GES_TIMELINE_ELEMENT_PARENT (self);
  GList *pending, *tmp;

  if (!GES_IS_CLIP (parent) || !self->priv->pending_effects)
    return;

  pending = self->priv->pending_effects;
  self->priv->pending_effects = NULL;

  for (tmp = pending; tmp; tmp = tmp->next) {
    PendingEffect *pe = tmp->data;
    GError *err = NULL;

    if (!_clip_add_effect_for_source (GES_CLIP (parent), self, pe->effect,
            pe->index, &err)) {
      GST_ERROR_OBJECT (self, "Could not add deferred effect %" GES_FORMAT
          ": %s", GES_ARGS (pe->effect), err ? err->message : "unknown");
      g_clear_error (&err);
    }
  }

  g_list_free_full (pending, _pending_effect_free);
}

/* Records @track as the track @source is meant to land in, for sources created
 * from a GESUriClip source-track-map (see ges-uri-clip.c). Kept as a GWeakRef
 * so it never keeps the track alive; a NULL @track still marks the source as
 * map-managed but unplaced. */
void
ges_source_set_wanted_track (GESSource * source, GESTrack * track)
{
  g_return_if_fail (GES_IS_SOURCE (source));

  g_weak_ref_set (&source->priv->wanted_track, track);
  source->priv->wanted_track_set = TRUE;
}

gboolean
ges_source_has_wanted_track (GESSource * source)
{
  g_return_val_if_fail (GES_IS_SOURCE (source), FALSE);

  return source->priv->wanted_track_set;
}

/* Returns: (transfer full) (nullable): the wanted track, or %NULL. */
GESTrack *
ges_source_get_wanted_track (GESSource * source)
{
  g_return_val_if_fail (GES_IS_SOURCE (source), NULL);

  return g_weak_ref_get (&source->priv->wanted_track);
}

static void
ges_source_init (GESSource * self)
{
  self->priv = ges_source_get_instance_private (self);
  g_mutex_init (&self->priv->sub_element_lock);
  g_weak_ref_init (&self->priv->wanted_track, NULL);
  g_signal_connect (self, "notify::parent",
      G_CALLBACK (_source_parent_notify_cb), NULL);
}

/**
 * ges_source_add_effect:
 * @source: a core #GESSource
 * @effect: (transfer floating): the #GESBaseEffect to add on top of @source
 * @index: the priority index for @effect within @source's effect chain, or -1
 *   to append it at the lowest priority
 * @error: (nullable): return location for an error
 *
 * Adds @effect on top of @source, binding it to @source so that it follows it
 * to whichever #GESTrack(s) @source is placed in (see #GESSourceTrackMap),
 * instead of being applied to whatever core element happens to share its track.
 *
 * If @source is not yet in a #GESClip, the effect is remembered and added once
 * @source is parented to one.
 *
 * Returns: %TRUE if @effect was added.
 *
 * Since: 1.30
 */
gboolean
ges_source_add_effect (GESSource * source, GESBaseEffect * effect, gint index,
    GError ** error)
{
  GESTimelineElement *parent;
  PendingEffect *pe;

  g_return_val_if_fail (GES_IS_SOURCE (source), FALSE);
  g_return_val_if_fail (GES_IS_BASE_EFFECT (effect), FALSE);
  g_return_val_if_fail (!error || !*error, FALSE);

  /* Bind before adding: the effect's placement (through the clip's
   * select_element_tracks vmethod) reads the binding. */
  ges_base_effect_set_source (effect, source);

  parent = GES_TIMELINE_ELEMENT_PARENT (source);
  if (GES_IS_CLIP (parent))
    return _clip_add_effect_for_source (GES_CLIP (parent), source, effect,
        index, error);

  /* Not in a clip yet: remember the effect and add it once @source is
   * parented to one (see _source_parent_notify_cb). */
  pe = g_new0 (PendingEffect, 1);
  pe->effect = gst_object_ref_sink (effect);
  pe->index = index;
  source->priv->pending_effects =
      g_list_append (source->priv->pending_effects, pe);

  return TRUE;
}
