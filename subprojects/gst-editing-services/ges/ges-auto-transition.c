/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 2; tab-width: 2 -*-  */
/*
 * gst-editing-services
 * Copyright (C) 2013 Thibault Saunier <thibault.saunier@collabora.com>
 *
 * gst-editing-services is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * gst-editing-services is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.";
 */

/* This class warps a GESBaseTransitionClip, letting any implementation
 * of a GESBaseTransitionClip to be used.
 *
 * NOTE: This is for internal use exclusively
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ges-auto-transition.h"
#include "ges-internal.h"
enum
{
  DESTROY_ME,
  LAST_SIGNAL
};

static guint auto_transition_signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (GESAutoTransition, ges_auto_transition, G_TYPE_OBJECT);

static void
neighbour_changed_cb (G_GNUC_UNUSED GObject * object,
    G_GNUC_UNUSED GParamSpec * arg, GESAutoTransition * self)
{
  gint64 new_duration;
  guint32 layer_prio;
  GESLayer *layer;
  GESTimeline *timeline;

  if (self->frozen) {
    GST_LOG_OBJECT (self, "Not updating because frozen");
    return;
  }

  if (self->positioning) {
    /* this can happen when the transition is moved layers as the layer
     * may resync its priorities */
    GST_LOG_OBJECT (self, "Not updating because positioning");
    return;
  }

  layer_prio = GES_TIMELINE_ELEMENT_LAYER_PRIORITY (self->next_source);

  if (layer_prio != GES_TIMELINE_ELEMENT_LAYER_PRIORITY (self->previous_source)) {
    GST_DEBUG_OBJECT (self, "Destroy changed layer");
    g_signal_emit (self, auto_transition_signals[DESTROY_ME], 0);
    return;
  }

  new_duration =
      (_START (self->previous_source) +
      _DURATION (self->previous_source)) - _START (self->next_source);

  if (new_duration <= 0 || new_duration >= _DURATION (self->previous_source)
      || new_duration >= _DURATION (self->next_source)) {

    GST_DEBUG_OBJECT (self, "Destroy %" G_GINT64_FORMAT " not a valid duration",
        new_duration);
    g_signal_emit (self, auto_transition_signals[DESTROY_ME], 0);
    return;
  }

  timeline = GES_TIMELINE_ELEMENT_TIMELINE (self->transition_clip);
  layer = timeline ? ges_timeline_get_layer (timeline, layer_prio) : NULL;
  if (!layer) {
    GST_DEBUG_OBJECT (self, "Destroy no layer");
    g_signal_emit (self, auto_transition_signals[DESTROY_ME], 0);
    return;
  }

  self->positioning = TRUE;
  GES_TIMELINE_ELEMENT_SET_BEING_EDITED (self->transition_clip);
  _set_start0 (GES_TIMELINE_ELEMENT (self->transition_clip),
      _START (self->next_source));
  _set_duration0 (GES_TIMELINE_ELEMENT (self->transition_clip), new_duration);
  ges_clip_move_to_layer (self->transition_clip, layer);
  GES_TIMELINE_ELEMENT_UNSET_BEING_EDITED (self->transition_clip);
  self->positioning = FALSE;

  gst_object_unref (layer);
}

static void
_track_changed_cb (GESTrackElement * track_element,
    GParamSpec * arg G_GNUC_UNUSED, GESAutoTransition * self)
{
  if (self->frozen) {
    GST_LOG_OBJECT (self, "Not updating because frozen");
    return;
  }

  if (ges_track_element_get_track (track_element) == NULL) {
    GST_DEBUG_OBJECT (self, "Neighboor %" GST_PTR_FORMAT
        " removed from track ... auto destructing", track_element);

    g_signal_emit (self, auto_transition_signals[DESTROY_ME], 0);
  }
}

static void
_connect_to_source (GESAutoTransition * self, GESTrackElement * source)
{
  g_signal_connect (source, "notify::start",
      G_CALLBACK (neighbour_changed_cb), self);
  g_signal_connect_after (source, "notify::priority",
      G_CALLBACK (neighbour_changed_cb), self);
  g_signal_connect (source, "notify::duration",
      G_CALLBACK (neighbour_changed_cb), self);

  g_signal_connect (source, "notify::track",
      G_CALLBACK (_track_changed_cb), self);
}

static void
_disconnect_from_source (GESAutoTransition * self, GESTrackElement * source)
{
  g_signal_handlers_disconnect_by_func (source, neighbour_changed_cb, self);
  g_signal_handlers_disconnect_by_func (source, _track_changed_cb, self);
}

void
ges_auto_transition_set_source (GESAutoTransition * self,
    GESTrackElement * source, GESEdge edge)
{
  _disconnect_from_source (self, self->previous_source);
  _connect_to_source (self, source);

  if (edge == GES_EDGE_END)
    self->next_source = source;
  else
    self->previous_source = source;
}

static void
ges_auto_transition_init (GESAutoTransition * ges_auto_transition)
{
}

static void
ges_auto_transition_finalize (GObject * object)
{
  GESAutoTransition *self = GES_AUTO_TRANSITION (object);

  _disconnect_from_source (self, self->previous_source);
  _disconnect_from_source (self, self->next_source);

  G_OBJECT_CLASS (ges_auto_transition_parent_class)->finalize (object);
}

static void
ges_auto_transition_class_init (GESAutoTransitionClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  auto_transition_signals[DESTROY_ME] =
      g_signal_new ("destroy-me", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST | G_SIGNAL_NO_RECURSE, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 0);
  object_class->finalize = ges_auto_transition_finalize;
}

GESAutoTransition *
ges_auto_transition_new (GESTrackElement * transition,
    GESTrackElement * previous_source, GESTrackElement * next_source)
{
  GESAutoTransition *self = g_object_new (GES_TYPE_AUTO_TRANSITION, NULL);

  self->frozen = FALSE;
  self->previous_source = previous_source;
  self->next_source = next_source;
  self->transition = transition;

  self->transition_clip = GES_CLIP (GES_TIMELINE_ELEMENT_PARENT (transition));

  _connect_to_source (self, previous_source);
  _connect_to_source (self, next_source);

  GST_DEBUG_OBJECT (self, "Created transition %" GST_PTR_FORMAT
      " between %" GST_PTR_FORMAT "[%" GST_TIME_FORMAT
      " - %" GST_TIME_FORMAT "] and: %" GST_PTR_FORMAT
      "[%" GST_TIME_FORMAT " - %" GST_TIME_FORMAT "]"
      " in layer nb %" G_GUINT32_FORMAT ", start: %" GST_TIME_FORMAT
      " duration: %" GST_TIME_FORMAT, transition, previous_source,
      GST_TIME_ARGS (_START (previous_source)),
      GST_TIME_ARGS (_END (previous_source)),
      next_source,
      GST_TIME_ARGS (_START (next_source)),
      GST_TIME_ARGS (_END (next_source)),
      GES_TIMELINE_ELEMENT_LAYER_PRIORITY (next_source),
      GST_TIME_ARGS (_START (transition)),
      GST_TIME_ARGS (_DURATION (transition)));

  return self;
}

void
ges_auto_transition_update (GESAutoTransition * self)
{
  GST_INFO ("Updating info %s",
      GES_TIMELINE_ELEMENT_NAME (self->transition_clip));
  neighbour_changed_cb (NULL, NULL, self);
}
