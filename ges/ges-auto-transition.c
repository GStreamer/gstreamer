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
neighbour_changed_cb (GESClip * clip, GParamSpec * arg G_GNUC_UNUSED,
    GESAutoTransition * self)
{
  gint64 new_duration;
  GESTimelineElement *parent =
      ges_timeline_element_get_toplevel_parent (GES_TIMELINE_ELEMENT (clip));

  if (!g_strcmp0 (g_param_spec_get_name (arg), "priority") && parent) {
    GESTimelineElement *prev_topparent =
        ges_timeline_element_get_toplevel_parent (GES_TIMELINE_ELEMENT
        (self->next_source));
    GESTimelineElement *next_topparent =
        ges_timeline_element_get_toplevel_parent (GES_TIMELINE_ELEMENT
        (self->previous_source));

    if (parent == prev_topparent && parent == next_topparent) {
      GST_DEBUG_OBJECT (self,
          "Moving all inside the same group, nothing to do");
      return;
    }
  }

  if (_ges_track_element_get_layer_priority (self->next_source) !=
      _ges_track_element_get_layer_priority (self->previous_source)) {
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

  self->positioning = TRUE;
  _set_start0 (GES_TIMELINE_ELEMENT (self->transition_clip),
      _START (self->next_source));
  _set_duration0 (GES_TIMELINE_ELEMENT (self->transition_clip), new_duration);
  self->positioning = FALSE;
}

static void
_track_changed_cb (GESTrackElement * track_element,
    GParamSpec * arg G_GNUC_UNUSED, GESAutoTransition * self)
{
  if (ges_track_element_get_track (track_element) == NULL) {
    GST_DEBUG_OBJECT (self, "Neighboor %" GST_PTR_FORMAT
        " removed from track ... auto destructing", track_element);

    g_signal_emit (self, auto_transition_signals[DESTROY_ME], 0);
  }

}

static void
ges_auto_transition_init (GESAutoTransition * ges_auto_transition)
{
}

static void
ges_auto_transition_finalize (GObject * object)
{
  GESAutoTransition *self = GES_AUTO_TRANSITION (object);

  g_signal_handlers_disconnect_by_func (self->previous_source,
      neighbour_changed_cb, self);
  g_signal_handlers_disconnect_by_func (self->next_source, neighbour_changed_cb,
      self);
  g_signal_handlers_disconnect_by_func (self->next_source, _track_changed_cb,
      self);
  g_signal_handlers_disconnect_by_func (self->previous_source,
      _track_changed_cb, self);

  g_free (self->key);

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

  self->previous_source = previous_source;
  self->next_source = next_source;
  self->transition = transition;

  self->previous_clip =
      GES_CLIP (GES_TIMELINE_ELEMENT_PARENT (previous_source));
  self->next_clip = GES_CLIP (GES_TIMELINE_ELEMENT_PARENT (next_source));
  self->transition_clip = GES_CLIP (GES_TIMELINE_ELEMENT_PARENT (transition));

  g_signal_connect (previous_source, "notify::start",
      G_CALLBACK (neighbour_changed_cb), self);
  g_signal_connect_after (previous_source, "notify::priority",
      G_CALLBACK (neighbour_changed_cb), self);
  g_signal_connect (next_source, "notify::start",
      G_CALLBACK (neighbour_changed_cb), self);
  g_signal_connect (next_source, "notify::priority",
      G_CALLBACK (neighbour_changed_cb), self);
  g_signal_connect (previous_source, "notify::duration",
      G_CALLBACK (neighbour_changed_cb), self);
  g_signal_connect (next_source, "notify::duration",
      G_CALLBACK (neighbour_changed_cb), self);

  g_signal_connect (next_source, "notify::track",
      G_CALLBACK (_track_changed_cb), self);
  g_signal_connect (previous_source, "notify::track",
      G_CALLBACK (_track_changed_cb), self);

  GST_DEBUG_OBJECT (self, "Created transition %" GST_PTR_FORMAT
      " between %" GST_PTR_FORMAT " and: %" GST_PTR_FORMAT
      " in layer nb %i, start: %" GST_TIME_FORMAT " duration: %"
      GST_TIME_FORMAT, transition, next_source, previous_source,
      ges_layer_get_priority (ges_clip_get_layer
          (self->previous_clip)),
      GST_TIME_ARGS (_START (transition)),
      GST_TIME_ARGS (_DURATION (transition)));

  self->key = g_strdup_printf ("%p%p", self->previous_source,
      self->next_source);

  return self;
}
