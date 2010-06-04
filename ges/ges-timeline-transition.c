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
  self->vtype = VTYPE_CROSSFADE;
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

static GEnumValue transition_types[] = {
  {
        1,
        "A bar moves from left to right",
      "bar-wipe-lr"},
  {
        2,
        "A bar moves from top to bottom",
      "bar-wipe-tb"},
  {
        3,
        "A box expands from the upper-left corner to the lower-right corner",
      "box-wipe-tl"},
  {
        4,
        "A box expands from the upper-right corner to the lower-left corner",
      "box-wipe-tr"},
  {
        5,
        "A box expands from the lower-right corner to the upper-left corner",
      "box-wipe-br"},
  {
        6,
        "A box expands from the lower-left corner to the upper-right corner",
      "box-wipe-bl"},
  {
        7,
        "A box shape expands from each of the four corners toward the center",
      "four-box-wipe-ci"},
  {
        8,
        "A box shape expands from the center of each quadrant toward the corners of each quadrant",
      "four-box-wipe-co"},
  {
        21,
        "A central, vertical line splits and expands toward the left and right edges",
      "barndoor-v"},
  {
        22,
        "A central, horizontal line splits and expands toward the top and bottom edges",
      "barndoor-h"},
  {
        23,
        "A box expands from the top edge's midpoint to the bottom corners",
      "box-wipe-tc"},
  {
        24,
        "A box expands from the right edge's midpoint to the left corners",
      "box-wipe-rc"},
  {
        25,
        "A box expands from the bottom edge's midpoint to the top corners",
      "box-wipe-bc"},
  {
        26,
        "A box expands from the left edge's midpoint to the right corners",
      "box-wipe-lc"},
  {
        41,
        "A diagonal line moves from the upper-left corner to the lower-right corner",
      "diagonal-tl"},
  {
        42,
        "A diagonal line moves from the upper right corner to the lower-left corner",
      "diagonal-tr"},
  {
        43,
        "Two wedge shapes slide in from the top and bottom edges toward the center",
      "bowtie-v"},
  {
        44,
        "Two wedge shapes slide in from the left and right edges toward the center",
      "bowtie-h"},
  {
        45,
        "A diagonal line from the lower-left to upper-right corners splits and expands toward the opposite corners",
      "barndoor-dbl"},
  {
        46,
        "A diagonal line from upper-left to lower-right corners splits and expands toward the opposite corners",
      "barndoor-dtl"},
  {
        47,
        "Four wedge shapes split from the center and retract toward the four edges",
      "misc-diagonal-dbd"},
  {
        48,
        "A diamond connecting the four edge midpoints simultaneously contracts toward the center and expands toward the edges",
      "misc-diagonal-dd"},
  {
        61,
        "A wedge shape moves from top to bottom",
      "vee-d"},
  {
        62,
        "A wedge shape moves from right to left",
      "vee-l"},
  {
        63,
        "A wedge shape moves from bottom to top",
      "vee-u"},
  {
        64,
        "A wedge shape moves from left to right",
      "vee-r"},
  {
        65,
        "A 'V' shape extending from the bottom edge's midpoint to the opposite corners contracts toward the center and expands toward the edges",
      "barnvee-d"},
  {
        66,
        "A 'V' shape extending from the left edge's midpoint to the opposite corners contracts toward the center and expands toward the edges",
      "barnvee-l"},
  {
        67,
        "A 'V' shape extending from the top edge's midpoint to the opposite corners contracts toward the center and expands toward the edges",
      "barnvee-u"},
  {
        68,
        "A 'V' shape extending from the right edge's midpoint to the opposite corners contracts toward the center and expands toward the edges",
      "barnvee-r"},
  {
        101,
        "A rectangle expands from the center.",
      "iris-rect"},
  {
        201,
        "A radial hand sweeps clockwise from the twelve o'clock position",
      "clock-cw12"},
  {
        202,
        "A radial hand sweeps clockwise from the three o'clock position",
      "clock-cw3"},
  {
        203,
        "A radial hand sweeps clockwise from the six o'clock position",
      "clock-cw6"},
  {
        204,
        "A radial hand sweeps clockwise from the nine o'clock position",
      "clock-cw9"},
  {
        205,
        "Two radial hands sweep clockwise from the twelve and six o'clock positions",
      "pinwheel-tbv"},
  {
        206,
        "Two radial hands sweep clockwise from the nine and three o'clock positions",
      "pinwheel-tbh"},
  {
        207,
        "Four radial hands sweep clockwise",
      "pinwheel-fb"},
  {
        211,
        "A fan unfolds from the top edge, the fan axis at the center",
      "fan-ct"},
  {
        212,
        "A fan unfolds from the right edge, the fan axis at the center",
      "fan-cr"},
  {
        213,
        "Two fans, their axes at the center, unfold from the top and bottom",
      "doublefan-fov"},
  {
        214,
        "Two fans, their axes at the center, unfold from the left and right",
      "doublefan-foh"},
  {
        221,
        "A radial hand sweeps clockwise from the top edge's midpoint",
      "singlesweep-cwt"},
  {
        222,
        "A radial hand sweeps clockwise from the right edge's midpoint",
      "singlesweep-cwr"},
  {
        223,
        "A radial hand sweeps clockwise from the bottom edge's midpoint",
      "singlesweep-cwb"},
  {
        224,
        "A radial hand sweeps clockwise from the left edge's midpoint",
      "singlesweep-cwl"},
  {
        225,
        "Two radial hands sweep clockwise and counter-clockwise from the top and bottom edges' midpoints",
      "doublesweep-pv"},
  {
        226,
        "Two radial hands sweep clockwise and counter-clockwise from the left and right edges' midpoints",
      "doublesweep-pd"},
  {
        227,
        "Two radial hands attached at the top and bottom edges' midpoints sweep from right to left",
      "doublesweep-ov"},
  {
        228,
        "Two radial hands attached at the left and right edges' midpoints sweep from top to bottom",
      "doublesweep-oh"},
  {
        231,
        "A fan unfolds from the bottom, the fan axis at the top edge's midpoint",
      "fan-t"},
  {
        232,
        "A fan unfolds from the left, the fan axis at the right edge's midpoint",
      "fan-r"},
  {
        233,
        "A fan unfolds from the top, the fan axis at the bottom edge's midpoint",
      "fan-b"},
  {
        234,
        "A fan unfolds from the right, the fan axis at the left edge's midpoint",
      "fan-l"},
  {
        235,
        "Two fans, their axes at the top and bottom, unfold from the center",
      "doublefan-fiv"},
  {
        236,
        "Two fans, their axes at the left and right, unfold from the center",
      "doublefan-fih"},
  {
        241,
        "A radial hand sweeps clockwise from the upper-left corner",
      "singlesweep-cwtl"},
  {
        242,
        "A radial hand sweeps counter-clockwise from the lower-left corner.",
      "singlesweep-cwbl"},
  {
        243,
        "A radial hand sweeps clockwise from the lower-right corner",
      "singlesweep-cwbr"},
  {
        244,
        "A radial hand sweeps counter-clockwise from the upper-right corner",
      "singlesweep-cwtr"},
  {
        245,
        "Two radial hands attached at the upper-left and lower-right corners sweep down and up",
      "doublesweep-pdtl"},
  {
        246,
        "Two radial hands attached at the lower-left and upper-right corners sweep down and up",
      "doublesweep-pdbl"},
  {
        251,
        "Two radial hands attached at the upper-left and upper-right corners sweep down",
      "saloondoor-t"},
  {
        252,
        "Two radial hands attached at the upper-left and lower-left corners sweep to the right",
      "saloondoor-l"},
  {
        253,
        "Two radial hands attached at the lower-left and lower-right corners sweep up",
      "saloondoor-b"},
  {
        254,
        "Two radial hands attached at the upper-right and lower-right corners sweep to the left",
      "saloondoor-r"},
  {
        261,
        "Two radial hands attached at the midpoints of the top and bottom halves sweep from right to left",
      "windshield-r"},
  {
        262,
        "Two radial hands attached at the midpoints of the left and right halves sweep from top to bottom",
      "windshield-u"},
  {
        263,
        "Two sets of radial hands attached at the midpoints of the top and bottom halves sweep from top to bottom and bottom to top",
      "windshield-v"},
  {
        264,
        "Two sets of radial hands attached at the midpoints of the left and right halves sweep from left to right and right to left",
      "windshield-h"},
  {
        VTYPE_CROSSFADE,
        "Crossfade between two clips",
      "crossfade"},
  {0, NULL, NULL}
};

/* how many types could GType type if GType could type types? */

static GType
ges_type_timeline_transition_vtype_get_type (void)
{
  static GType the_type = 0;
  static gsize once = 0;

  if (g_once_init_enter (&once)) {
    g_assert (!once);

    the_type = g_enum_register_static ("GESTimelineTransitionVType",
        transition_types);
    g_once_init_leave (&once, 1);
  }

  return the_type;
}

GESTimelineTransition *
ges_timeline_transition_new (gint vtype)
{
  GESTimelineTransition *ret;

  ret = g_object_new (GES_TYPE_TIMELINE_TRANSITION, "vtype", (gint) vtype,
      NULL);
  return ret;
}

GESTimelineTransition *
ges_timeline_transition_new_for_nick (gchar * nick)
{
  GESTimelineTransition *ret;
  ret = g_object_new (GES_TYPE_TIMELINE_TRANSITION, NULL);
  g_object_set (ret, "vtype", (gchar *) nick, NULL);

  return ret;
}
