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

#define GES_TYPE_TIMELINE_TRANSITION_VTYPE_TYPE \
    (ges_type_timeline_transition_vtype_get_type())

static GType ges_type_timeline_transition_vtype_get_type (void);

enum
{
  VTYPE_CROSSFADE = 0,
};

enum
{
  PROP_VTYPE = 5,
};

G_DEFINE_TYPE (GESTimelineTransition, ges_timeline_transition,
    GES_TYPE_TIMELINE_OBJECT);

static GESTrackObject *ges_tl_transition_create_track_object (GESTimelineObject
    *, GESTrack *);

void
ges_timeline_transition_update_vtype_internal (GESTimelineObject * self,
    gint value)
{
  GList *tmp;
  GESTrackTransition *tr;
  GESTrackObject *to;

  for (tmp = g_list_first (self->trackobjects); tmp; tmp = g_list_next (tmp)) {
    tr = GES_TRACK_TRANSITION (tmp->data);
    to = (GESTrackObject *) tr;

    if ((to->track) && (to->track->type == GES_TRACK_TYPE_VIDEO)) {
      ges_track_transition_set_vtype (tr, value);
    }
  }
}

static void
ges_timeline_transition_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec)
{
  GESTimelineTransition *self = GES_TIMELINE_TRANSITION (object);
  gint value_int;
  switch (property_id) {
    case PROP_VTYPE:
      self->vtype = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_timeline_transition_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GESTimelineObject *self = GES_TIMELINE_OBJECT (object);
  GESTimelineTransition *trself = GES_TIMELINE_TRANSITION (object);

  switch (property_id) {
    case PROP_VTYPE:
      trself->vtype = g_value_get_enum (value);
      ges_timeline_transition_update_vtype_internal (self, trself->vtype);
      break;
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

  /**
   * GESTimelineTransition: vtype
   *
   * The SMPTE wipe to use, or 0 for crossfade.
   */
  g_object_class_install_property (object_class, PROP_VTYPE,
      g_param_spec_enum ("vtype", "VType",
          "The SMPTE video wipe to use, or 0 for crossfade",
          GES_TYPE_TIMELINE_TRANSITION_VTYPE_TYPE, VTYPE_CROSSFADE,
          G_PARAM_READWRITE));


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
ges_timeline_transition_new (gint vtype)
{
  GESTimelineTransition *ret = g_object_new
      (GES_TYPE_TIMELINE_TRANSITION, NULL);

  g_object_set (ret, "vtype", (gint) vtype, NULL);
  return ret;
}

static GEnumClass *smpte_enum_class = NULL;

static void
_ensure_smpte_enum_class ()
{
  /* is there a better way to do this? */

  if (!smpte_enum_class) {
    GstElement *element = gst_element_factory_make ("smpte", NULL);
    GstElementClass *element_class = GST_ELEMENT_GET_CLASS (element);

    GParamSpec *pspec =
        g_object_class_find_property (G_OBJECT_CLASS (element_class), "type");

    smpte_enum_class = G_ENUM_CLASS (g_type_class_ref (pspec->value_type));
  }

}

/* how many types could GType type if GType could type types? */

static GType
ges_type_timeline_transition_vtype_get_type (void)
{
  _ensure_smpte_enum_class ();

  static GType the_type = 0;

  if (!the_type) {
    GEnumValue *values, *src, *dst;
    gint i, n;

    n = smpte_enum_class->n_values;

    /* plus one for sentinel, plus another for the crossfade GEnumValue */
    values = g_new0 (GEnumValue, 2 + n);

    values->value = 0;
    values->value_name = "Cross-fade between two sources";
    values->value_nick = "crossfade";

    for (i = 0, dst = (values + 1), src = smpte_enum_class->values; i < n;
        i++, dst++, src++) {
      dst->value = src->value;
      dst->value_nick = src->value_nick;
      dst->value_name = src->value_name;
    }

    dst->value = 0;
    dst->value_nick = NULL;
    dst->value_name = NULL;

    the_type = g_enum_register_static ("GESTimelineTransitionVType", values);
  }

  return the_type;
}

GESTimelineTransition *
ges_timeline_transition_new_for_nick (char *nick)
{

  _ensure_smpte_enum_class ();

  if (!strcmp ("crossfade", nick)) {
    return ges_timeline_transition_new (0);
  }

  GEnumValue *value = g_enum_get_value_by_nick (smpte_enum_class, nick);

  if (!value) {
    return NULL;
  }

  return ges_timeline_transition_new (value->value);
}
