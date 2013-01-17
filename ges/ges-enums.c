/* GStreamer Editing Services
 * Copyright (C) 2010 Brandon Lewis <brandon.lewis@collabora.co.uk>
 *               2010 Nokia Corporation
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
 * SECTION:ges-enums
 * @short_description: Various enums for the Gstreamer Editing Services
 */

#include "ges-enums.h"
#include "ges-internal.h"
#include "ges-asset.h"
#include "ges-meta-container.h"
#include "ges-standard-transition-clip.h"

#define C_ENUM(v) ((guint) v)

static const GFlagsValue track_types_values[] = {
  {C_ENUM (GES_TRACK_TYPE_UNKNOWN), "GES_TRACK_TYPE_UNKNOWN", "unknown"},
  {C_ENUM (GES_TRACK_TYPE_AUDIO), "GES_TRACK_TYPE_AUDIO", "audio"},
  {C_ENUM (GES_TRACK_TYPE_VIDEO), "GES_TRACK_TYPE_VIDEO", "video"},
  {C_ENUM (GES_TRACK_TYPE_TEXT), "GES_TRACK_TYPE_TEXT", "text"},
  {C_ENUM (GES_TRACK_TYPE_CUSTOM), "GES_TRACK_TYPE_CUSTOM", "custom"},
  {0, NULL, NULL}
};

static void
register_ges_track_type_select_result (GType * id)
{
  *id = g_flags_register_static ("GESTrackType", track_types_values);
}

const gchar *
ges_track_type_name (GESTrackType type)
{
  guint i;

  for (i = 0; i < G_N_ELEMENTS (track_types_values); i++) {
    if (type == track_types_values[i].value)
      return track_types_values[i].value_nick;
  }

  return "Unknown (mixed?) ";
}

GType
ges_track_type_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_ges_track_type_select_result, &id);
  return id;
}

static void
register_ges_pipeline_flags (GType * id)
{
  static const GFlagsValue values[] = {
    {C_ENUM (TIMELINE_MODE_PREVIEW_AUDIO), "TIMELINE_MODE_PREVIEW_AUDIO",
        "audio_preview"},
    {C_ENUM (TIMELINE_MODE_PREVIEW_VIDEO), "TIMELINE_MODE_PREVIEW_VIDEO",
        "video_preview"},
    {C_ENUM (TIMELINE_MODE_PREVIEW), "TIMELINE_MODE_PREVIEW", "full_preview"},
    {C_ENUM (TIMELINE_MODE_RENDER), "TIMELINE_MODE_RENDER", "render"},
    {C_ENUM (TIMELINE_MODE_SMART_RENDER), "TIMELINE_MODE_SMART_RENDER",
        "smart_render"},
    {0, NULL, NULL}
  };

  *id = g_flags_register_static ("GESPipelineFlags", values);
}

GType
ges_pipeline_flags_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_ges_pipeline_flags, &id);
  return id;
}

static void
register_ges_edit_mode (GType * id)
{
  static const GEnumValue edit_mode[] = {
    {C_ENUM (GES_EDIT_MODE_NORMAL), "GES_EDIT_MODE_NORMAL",
        "edit_normal"},

    {C_ENUM (GES_EDIT_MODE_RIPPLE), "GES_EDIT_MODE_RIPPLE",
        "edit_ripple"},

    {C_ENUM (GES_EDIT_MODE_ROLL), "GES_EDIT_MODE_ROLL",
        "edit_roll"},

    {C_ENUM (GES_EDIT_MODE_TRIM), "GES_EDIT_MODE_TRIM",
        "edit_trim"},

    {C_ENUM (GES_EDIT_MODE_SLIDE), "GES_EDIT_MODE_SLIDE",
        "edit_slide"},

    {0, NULL, NULL}
  };

  *id = g_enum_register_static ("GESEditMode", edit_mode);
}

GType
ges_edit_mode_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_ges_edit_mode, &id);
  return id;
}

static void
register_ges_edge (GType * id)
{
  static const GEnumValue edges[] = {
    {C_ENUM (GES_EDGE_START), "GES_EDGE_START", "edge_start"},
    {C_ENUM (GES_EDGE_END), "GES_EDGE_END", "edge_end"},
    {C_ENUM (GES_EDGE_NONE), "GES_EDGE_NONE", "edge_none"},
    {0, NULL, NULL}
  };

  *id = g_enum_register_static ("GESEdge", edges);
}

GType
ges_edge_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_ges_edge, &id);
  return id;
}

static GEnumValue transition_types[] = {
  {
        0,
        "Transition has not been set",
      "none"}
  ,
  {
        1,
        "A bar moves from left to right",
      "bar-wipe-lr"}
  ,
  {
        2,
        "A bar moves from top to bottom",
      "bar-wipe-tb"}
  ,
  {
        3,
        "A box expands from the upper-left corner to the lower-right corner",
      "box-wipe-tl"}
  ,
  {
        4,
        "A box expands from the upper-right corner to the lower-left corner",
      "box-wipe-tr"}
  ,
  {
        5,
        "A box expands from the lower-right corner to the upper-left corner",
      "box-wipe-br"}
  ,
  {
        6,
        "A box expands from the lower-left corner to the upper-right corner",
      "box-wipe-bl"}
  ,
  {
        7,
        "A box shape expands from each of the four corners toward the center",
      "four-box-wipe-ci"}
  ,
  {
        8,
        "A box shape expands from the center of each quadrant toward the corners of each quadrant",
      "four-box-wipe-co"}
  ,
  {
        21,
        "A central, vertical line splits and expands toward the left and right edges",
      "barndoor-v"}
  ,
  {
        22,
        "A central, horizontal line splits and expands toward the top and bottom edges",
      "barndoor-h"}
  ,
  {
        23,
        "A box expands from the top edge's midpoint to the bottom corners",
      "box-wipe-tc"}
  ,
  {
        24,
        "A box expands from the right edge's midpoint to the left corners",
      "box-wipe-rc"}
  ,
  {
        25,
        "A box expands from the bottom edge's midpoint to the top corners",
      "box-wipe-bc"}
  ,
  {
        26,
        "A box expands from the left edge's midpoint to the right corners",
      "box-wipe-lc"}
  ,
  {
        41,
        "A diagonal line moves from the upper-left corner to the lower-right corner",
      "diagonal-tl"}
  ,
  {
        42,
        "A diagonal line moves from the upper right corner to the lower-left corner",
      "diagonal-tr"}
  ,
  {
        43,
        "Two wedge shapes slide in from the top and bottom edges toward the center",
      "bowtie-v"}
  ,
  {
        44,
        "Two wedge shapes slide in from the left and right edges toward the center",
      "bowtie-h"}
  ,
  {
        45,
        "A diagonal line from the lower-left to upper-right corners splits and expands toward the opposite corners",
      "barndoor-dbl"}
  ,
  {
        46,
        "A diagonal line from upper-left to lower-right corners splits and expands toward the opposite corners",
      "barndoor-dtl"}
  ,
  {
        47,
        "Four wedge shapes split from the center and retract toward the four edges",
      "misc-diagonal-dbd"}
  ,
  {
        48,
        "A diamond connecting the four edge midpoints simultaneously contracts toward the center and expands toward the edges",
      "misc-diagonal-dd"}
  ,
  {
        61,
        "A wedge shape moves from top to bottom",
      "vee-d"}
  ,
  {
        62,
        "A wedge shape moves from right to left",
      "vee-l"}
  ,
  {
        63,
        "A wedge shape moves from bottom to top",
      "vee-u"}
  ,
  {
        64,
        "A wedge shape moves from left to right",
      "vee-r"}
  ,
  {
        65,
        "A 'V' shape extending from the bottom edge's midpoint to the opposite corners contracts toward the center and expands toward the edges",
      "barnvee-d"}
  ,
  {
        66,
        "A 'V' shape extending from the left edge's midpoint to the opposite corners contracts toward the center and expands toward the edges",
      "barnvee-l"}
  ,
  {
        67,
        "A 'V' shape extending from the top edge's midpoint to the opposite corners contracts toward the center and expands toward the edges",
      "barnvee-u"}
  ,
  {
        68,
        "A 'V' shape extending from the right edge's midpoint to the opposite corners contracts toward the center and expands toward the edges",
      "barnvee-r"}
  ,
  {
        101,
        "A rectangle expands from the center.",
      "iris-rect"}
  ,
  {
        201,
        "A radial hand sweeps clockwise from the twelve o'clock position",
      "clock-cw12"}
  ,
  {
        202,
        "A radial hand sweeps clockwise from the three o'clock position",
      "clock-cw3"}
  ,
  {
        203,
        "A radial hand sweeps clockwise from the six o'clock position",
      "clock-cw6"}
  ,
  {
        204,
        "A radial hand sweeps clockwise from the nine o'clock position",
      "clock-cw9"}
  ,
  {
        205,
        "Two radial hands sweep clockwise from the twelve and six o'clock positions",
      "pinwheel-tbv"}
  ,
  {
        206,
        "Two radial hands sweep clockwise from the nine and three o'clock positions",
      "pinwheel-tbh"}
  ,
  {
        207,
        "Four radial hands sweep clockwise",
      "pinwheel-fb"}
  ,
  {
        211,
        "A fan unfolds from the top edge, the fan axis at the center",
      "fan-ct"}
  ,
  {
        212,
        "A fan unfolds from the right edge, the fan axis at the center",
      "fan-cr"}
  ,
  {
        213,
        "Two fans, their axes at the center, unfold from the top and bottom",
      "doublefan-fov"}
  ,
  {
        214,
        "Two fans, their axes at the center, unfold from the left and right",
      "doublefan-foh"}
  ,
  {
        221,
        "A radial hand sweeps clockwise from the top edge's midpoint",
      "singlesweep-cwt"}
  ,
  {
        222,
        "A radial hand sweeps clockwise from the right edge's midpoint",
      "singlesweep-cwr"}
  ,
  {
        223,
        "A radial hand sweeps clockwise from the bottom edge's midpoint",
      "singlesweep-cwb"}
  ,
  {
        224,
        "A radial hand sweeps clockwise from the left edge's midpoint",
      "singlesweep-cwl"}
  ,
  {
        225,
        "Two radial hands sweep clockwise and counter-clockwise from the top and bottom edges' midpoints",
      "doublesweep-pv"}
  ,
  {
        226,
        "Two radial hands sweep clockwise and counter-clockwise from the left and right edges' midpoints",
      "doublesweep-pd"}
  ,
  {
        227,
        "Two radial hands attached at the top and bottom edges' midpoints sweep from right to left",
      "doublesweep-ov"}
  ,
  {
        228,
        "Two radial hands attached at the left and right edges' midpoints sweep from top to bottom",
      "doublesweep-oh"}
  ,
  {
        231,
        "A fan unfolds from the bottom, the fan axis at the top edge's midpoint",
      "fan-t"}
  ,
  {
        232,
        "A fan unfolds from the left, the fan axis at the right edge's midpoint",
      "fan-r"}
  ,
  {
        233,
        "A fan unfolds from the top, the fan axis at the bottom edge's midpoint",
      "fan-b"}
  ,
  {
        234,
        "A fan unfolds from the right, the fan axis at the left edge's midpoint",
      "fan-l"}
  ,
  {
        235,
        "Two fans, their axes at the top and bottom, unfold from the center",
      "doublefan-fiv"}
  ,
  {
        236,
        "Two fans, their axes at the left and right, unfold from the center",
      "doublefan-fih"}
  ,
  {
        241,
        "A radial hand sweeps clockwise from the upper-left corner",
      "singlesweep-cwtl"}
  ,
  {
        242,
        "A radial hand sweeps counter-clockwise from the lower-left corner.",
      "singlesweep-cwbl"}
  ,
  {
        243,
        "A radial hand sweeps clockwise from the lower-right corner",
      "singlesweep-cwbr"}
  ,
  {
        244,
        "A radial hand sweeps counter-clockwise from the upper-right corner",
      "singlesweep-cwtr"}
  ,
  {
        245,
        "Two radial hands attached at the upper-left and lower-right corners sweep down and up",
      "doublesweep-pdtl"}
  ,
  {
        246,
        "Two radial hands attached at the lower-left and upper-right corners sweep down and up",
      "doublesweep-pdbl"}
  ,
  {
        251,
        "Two radial hands attached at the upper-left and upper-right corners sweep down",
      "saloondoor-t"}
  ,
  {
        252,
        "Two radial hands attached at the upper-left and lower-left corners sweep to the right",
      "saloondoor-l"}
  ,
  {
        253,
        "Two radial hands attached at the lower-left and lower-right corners sweep up",
      "saloondoor-b"}
  ,
  {
        254,
        "Two radial hands attached at the upper-right and lower-right corners sweep to the left",
      "saloondoor-r"}
  ,
  {
        261,
        "Two radial hands attached at the midpoints of the top and bottom halves sweep from right to left",
      "windshield-r"}
  ,
  {
        262,
        "Two radial hands attached at the midpoints of the left and right halves sweep from top to bottom",
      "windshield-u"}
  ,
  {
        263,
        "Two sets of radial hands attached at the midpoints of the top and bottom halves sweep from top to bottom and bottom to top",
      "windshield-v"}
  ,
  {
        264,
        "Two sets of radial hands attached at the midpoints of the left and right halves sweep from left to right and right to left",
      "windshield-h"}
  ,
  {
        GES_VIDEO_STANDARD_TRANSITION_TYPE_CROSSFADE,
        "Crossfade between two clips",
      "crossfade"}
  ,
  {0, NULL, NULL}
};

void
_init_standard_transition_assets (void)
{
  guint i;

  for (i = 1; i < G_N_ELEMENTS (transition_types) - 1; i++) {
    GESAsset *asset = ges_asset_request (GES_TYPE_STANDARD_TRANSITION_CLIP,
        transition_types[i].value_nick, NULL);

    ges_meta_container_register_meta_string (GES_META_CONTAINER (asset),
        GES_META_READABLE, GES_META_DESCRIPTION,
        transition_types[i].value_name);
  }

}

GType
ges_video_standard_transition_type_get_type (void)
{
  static GType the_type = 0;
  static gsize once = 0;

  if (g_once_init_enter (&once)) {
    g_assert (!once);

    the_type = g_enum_register_static ("GESVideoStandardTransitionType",
        transition_types);
    g_once_init_leave (&once, 1);
  }

  return the_type;
}

GType
ges_text_valign_get_type (void)
{
  static GType text_overlay_valign_type = 0;
  static gsize initialized = 0;
  static const GEnumValue text_overlay_valign[] = {
    {GES_TEXT_VALIGN_BASELINE, "baseline", "baseline"},
    {GES_TEXT_VALIGN_BOTTOM, "bottom", "bottom"},
    {GES_TEXT_VALIGN_TOP, "top", "top"},
    {GES_TEXT_VALIGN_POSITION, "position", "position"},
    {GES_TEXT_VALIGN_CENTER, "center", "center"},
    {0, NULL, NULL},
  };

  if (g_once_init_enter (&initialized)) {
    text_overlay_valign_type =
        g_enum_register_static ("GESTextVAlign", text_overlay_valign);
    g_once_init_leave (&initialized, 1);
  }
  return text_overlay_valign_type;
}

GType
ges_text_halign_get_type (void)
{
  static GType text_overlay_halign_type = 0;
  static gsize initialized = 0;
  static const GEnumValue text_overlay_halign[] = {
    {GES_TEXT_HALIGN_LEFT, "left", "left"},
    {GES_TEXT_HALIGN_CENTER, "center", "center"},
    {GES_TEXT_HALIGN_RIGHT, "right", "right"},
    {GES_TEXT_HALIGN_POSITION, "position", "position"},
    {0, NULL, NULL},
  };

  if (g_once_init_enter (&initialized)) {
    text_overlay_halign_type =
        g_enum_register_static ("GESTextHAlign", text_overlay_halign);
    g_once_init_leave (&initialized, 1);
  }
  return text_overlay_halign_type;
}

/* table more-or-less copied from gstvideotestsrc.c */
static GEnumValue vpattern_enum_values[] = {
  {GES_VIDEO_TEST_PATTERN_SMPTE, "SMPTE 100% color bars", "smpte"}
  ,
  {GES_VIDEO_TEST_PATTERN_SNOW, "Random (television snow)", "snow"}
  ,
  {GES_VIDEO_TEST_PATTERN_BLACK, "100% Black", "black"}
  ,
  {GES_VIDEO_TEST_PATTERN_WHITE, "100% White", "white"}
  ,
  {GES_VIDEO_TEST_PATTERN_RED, "Red", "red"}
  ,
  {GES_VIDEO_TEST_PATTERN_GREEN, "Green", "green"}
  ,
  {GES_VIDEO_TEST_PATTERN_BLUE, "Blue", "blue"}
  ,
  {GES_VIDEO_TEST_PATTERN_CHECKERS1, "Checkers 1px", "checkers-1"}
  ,
  {GES_VIDEO_TEST_PATTERN_CHECKERS2, "Checkers 2px", "checkers-2"}
  ,
  {GES_VIDEO_TEST_PATTERN_CHECKERS4, "Checkers 4px", "checkers-4"}
  ,
  {GES_VIDEO_TEST_PATTERN_CHECKERS8, "Checkers 8px", "checkers-8"}
  ,
  {GES_VIDEO_TEST_PATTERN_CIRCULAR, "Circular", "circular"}
  ,
  {GES_VIDEO_TEST_PATTERN_BLINK, "Blink", "blink"}
  ,
  {GES_VIDEO_TEST_PATTERN_SMPTE75, "SMPTE 75% color bars", "smpte75"}
  ,
  {GES_VIDEO_TEST_ZONE_PLATE, "Zone plate", "zone-plate"}
  ,
  {GES_VIDEO_TEST_GAMUT, "Gamut checkers", "gamut"}
  ,
  {GES_VIDEO_TEST_CHROMA_ZONE_PLATE, "Chroma zone plate",
      "chroma-zone-plate"}
  ,
  {GES_VIDEO_TEST_PATTERN_SOLID, "Solid color", "solid-color"}
  ,
  {0, NULL, NULL}
};

GType
ges_video_test_pattern_get_type (void)
{

  static gsize once = 0;
  static GType theType = 0;

  if (g_once_init_enter (&once)) {
    theType = g_enum_register_static ("GESVideoTestPattern",
        vpattern_enum_values);
    g_once_init_leave (&once, 1);
  };

  return theType;
}

static void
register_ges_meta_flag (GType * id)
{
  static const GFlagsValue values[] = {
    {C_ENUM (GES_META_READABLE), "GES_META_READABLE", "readable"},
    {C_ENUM (GES_META_WRITABLE), "GES_META_WRITABLE", "writable"},
    {C_ENUM (GES_META_READ_WRITE), "GES_META_READ_WRITE", "readwrite"},
    {0, NULL, NULL}
  };

  *id = g_flags_register_static ("GESMetaFlag", values);
}

GType
ges_meta_flag_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_ges_meta_flag, &id);
  return id;
}
