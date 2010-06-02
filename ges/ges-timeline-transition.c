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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION: ges-timeline-transition
 * @short_description: Base Class for transitions in a #GESTimelineLayer
 */

#include "ges-internal.h"
#include "ges-timeline-transition.h"
#include "ges-track-transition.h"

G_DEFINE_TYPE (GESTimelineTransition, ges_timeline_transition,
    GES_TYPE_TIMELINE_OBJECT);

static GESTrackObject *ges_tl_transition_create_track_object (GESTimelineObject
    *, GESTrack *);

static void
ges_timeline_transition_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_timeline_transition_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_timeline_transition_dispose (GObject * object)
{
  G_OBJECT_CLASS (ges_timeline_transition_parent_class)->dispose (object);
}

static void
ges_timeline_transition_finalize (GObject * object)
{
  G_OBJECT_CLASS (ges_timeline_transition_parent_class)->finalize (object);
}

static void
ges_timeline_transition_class_init (GESTimelineTransitionClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESTimelineObjectClass *timobj_class = GES_TIMELINE_OBJECT_CLASS (klass);

  object_class->get_property = ges_timeline_transition_get_property;
  object_class->set_property = ges_timeline_transition_set_property;
  object_class->dispose = ges_timeline_transition_dispose;
  object_class->finalize = ges_timeline_transition_finalize;

  timobj_class->create_track_object = ges_tl_transition_create_track_object;
  timobj_class->need_fill_track = FALSE;
}

static void
ges_timeline_transition_init (GESTimelineTransition * self)
{
  self->vtype = 0;
}

static GESTrackObject *
ges_tl_transition_create_track_object (GESTimelineObject * obj,
    GESTrack * track)
{
  GESTimelineTransition *transition = (GESTimelineTransition *) obj;
  GESTrackObject *res;

  GST_DEBUG ("Creating a GESTrackTransition");

  res = GES_TRACK_OBJECT (ges_track_transition_new (transition->vtype));

  return res;
}

GESTimelineTransition *
ges_timeline_transition_new (GEnumValue * vtype)
{
  GESTimelineTransition *ret = g_object_new
      (GES_TYPE_TIMELINE_TRANSITION, NULL);

  ret->vtype = vtype ? vtype->value : 0;
  return ret;
}

static GEnumClass *smpte_enum_class = NULL;

static
_ensure_smpte_enum_class ()
{
  if (!smpte_enum_class) {
    GstElement *element = gst_element_factory_make ("smpte", NULL);
    GstElementClass *element_class = GST_ELEMENT_GET_CLASS (element);

    GParamSpec *pspec =
        g_object_class_find_property (G_OBJECT_CLASS (element_class), "type");

    smpte_enum_class = G_ENUM_CLASS (g_type_class_ref (pspec->value_type));
  }

}

GESTimelineTransition *
ges_timeline_transition_new_for_nick (char *nick)
{

  _ensure_smpte_enum_class ();

  if (!strcmp ("crossfade", nick)) {
    return ges_timeline_transition_new (NULL);
  }

  GEnumValue *value = g_enum_get_value_by_nick (smpte_enum_class, nick);

  if (!value) {
    return NULL;
  }

  return ges_timeline_transition_new (value);
}
