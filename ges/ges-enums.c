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
#include "ges-transition-clip.h"

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
    {C_ENUM (GES_PIPELINE_MODE_PREVIEW_AUDIO),
          "GES_PIPELINE_MODE_PREVIEW_AUDIO",
        "audio_preview"},
    {C_ENUM (GES_PIPELINE_MODE_PREVIEW_VIDEO),
          "GES_PIPELINE_MODE_PREVIEW_VIDEO",
        "video_preview"},
    {C_ENUM (GES_PIPELINE_MODE_PREVIEW), "GES_PIPELINE_MODE_PREVIEW",
        "full_preview"},
    {C_ENUM (GES_PIPELINE_MODE_RENDER), "GES_PIPELINE_MODE_RENDER", "render"},
    {C_ENUM (GES_PIPELINE_MODE_SMART_RENDER), "GES_PIPELINE_MODE_SMART_RENDER",
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
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_NONE,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_NONE",
      "none"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_BAR_WIPE_LR,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_BAR_WIPE_LR",
      "bar-wipe-lr"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_BAR_WIPE_TB,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_BAR_WIPE_TB",
      "bar-wipe-tb"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_BOX_WIPE_TL,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_BOX_WIPE_TL",
      "box-wipe-tl"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_BOX_WIPE_TR,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_BOX_WIPE_TR",
      "box-wipe-tr"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_BOX_WIPE_BR,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_BOX_WIPE_BR",
      "box-wipe-br"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_BOX_WIPE_BL,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_BOX_WIPE_BL",
      "box-wipe-bl"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_FOUR_BOX_WIPE_CI,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_FOUR_BOX_WIPE_CI",
      "four-box-wipe-ci"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_FOUR_BOX_WIPE_CO,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_FOUR_BOX_WIPE_CO",
      "four-box-wipe-co"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_BARNDOOR_V,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_BARNDOOR_V",
      "barndoor-v"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_BARNDOOR_H,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_BARNDOOR_H",
      "barndoor-h"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_BOX_WIPE_TC,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_BOX_WIPE_TC",
      "box-wipe-tc"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_BOX_WIPE_RC,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_BOX_WIPE_RC",
      "box-wipe-rc"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_BOX_WIPE_BC,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_BOX_WIPE_BC",
      "box-wipe-bc"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_BOX_WIPE_LC,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_BOX_WIPE_LC",
      "box-wipe-lc"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_DIAGONAL_TL,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_DIAGONAL_TL",
      "diagonal-tl"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_DIAGONAL_TR,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_DIAGONAL_TR",
      "diagonal-tr"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_BOWTIE_V,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_BOWTIE_V",
      "bowtie-v"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_BOWTIE_H,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_BOWTIE_H",
      "bowtie-h"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_BARNDOOR_DBL,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_BARNDOOR_DBL",
      "barndoor-dbl"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_BARNDOOR_DTL,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_BARNDOOR_DTL",
      "barndoor-dtl"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_MISC_DIAGONAL_DBD,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_MISC_DIAGONAL_DBD",
      "misc-diagonal-dbd"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_MISC_DIAGONAL_DD,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_MISC_DIAGONAL_DD",
      "misc-diagonal-dd"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_VEE_D,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_VEE_D",
      "vee-d"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_VEE_L,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_VEE_L",
      "vee-l"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_VEE_U,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_VEE_U",
      "vee-u"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_VEE_R,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_VEE_R",
      "vee-r"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_BARNVEE_D,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_BARNVEE_D",
      "barnvee-d"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_BARNVEE_L,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_BARNVEE_L",
      "barnvee-l"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_BARNVEE_U,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_BARNVEE_U",
      "barnvee-u"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_BARNVEE_R,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_BARNVEE_R",
      "barnvee-r"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_IRIS_RECT,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_IRIS_RECT",
      "iris-rect"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_CLOCK_CW12,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_CLOCK_CW12",
      "clock-cw12"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_CLOCK_CW3,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_CLOCK_CW3",
      "clock-cw3"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_CLOCK_CW6,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_CLOCK_CW6",
      "clock-cw6"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_CLOCK_CW9,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_CLOCK_CW9",
      "clock-cw9"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_PINWHEEL_TBV,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_PINWHEEL_TBV",
      "pinwheel-tbv"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_PINWHEEL_TBH,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_PINWHEEL_TBH",
      "pinwheel-tbh"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_PINWHEEL_FB,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_PINWHEEL_FB",
      "pinwheel-fb"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_FAN_CT,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_FAN_CT",
      "fan-ct"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_FAN_CR,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_FAN_CR",
      "fan-cr"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_DOUBLEFAN_FOV,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_DOUBLEFAN_FOV",
      "doublefan-fov"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_DOUBLEFAN_FOH,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_DOUBLEFAN_FOH",
      "doublefan-foh"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_SINGLESWEEP_CWT,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_SINGLESWEEP_CWT",
      "singlesweep-cwt"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_SINGLESWEEP_CWR,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_SINGLESWEEP_CWR",
      "singlesweep-cwr"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_SINGLESWEEP_CWB,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_SINGLESWEEP_CWB",
      "singlesweep-cwb"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_SINGLESWEEP_CWL,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_SINGLESWEEP_CWL",
      "singlesweep-cwl"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_DOUBLESWEEP_PV,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_DOUBLESWEEP_PV",
      "doublesweep-pv"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_DOUBLESWEEP_PD,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_DOUBLESWEEP_PD",
      "doublesweep-pd"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_DOUBLESWEEP_OV,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_DOUBLESWEEP_OV",
      "doublesweep-ov"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_DOUBLESWEEP_OH,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_DOUBLESWEEP_OH",
      "doublesweep-oh"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_FAN_T,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_FAN_T",
      "fan-t"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_FAN_R,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_FAN_R",
      "fan-r"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_FAN_B,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_FAN_B",
      "fan-b"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_FAN_L,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_FAN_L",
      "fan-l"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_DOUBLEFAN_FIV,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_DOUBLEFAN_FIV",
      "doublefan-fiv"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_DOUBLEFAN_FIH,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_DOUBLEFAN_FIH",
      "doublefan-fih"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_SINGLESWEEP_CWTL,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_SINGLESWEEP_CWTL",
      "singlesweep-cwtl"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_SINGLESWEEP_CWBL,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_SINGLESWEEP_CWBL",
      "singlesweep-cwbl"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_SINGLESWEEP_CWBR,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_SINGLESWEEP_CWBR",
      "singlesweep-cwbr"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_SINGLESWEEP_CWTR,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_SINGLESWEEP_CWTR",
      "singlesweep-cwtr"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_DOUBLESWEEP_PDTL,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_DOUBLESWEEP_PDTL",
      "doublesweep-pdtl"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_DOUBLESWEEP_PDBL,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_DOUBLESWEEP_PDBL",
      "doublesweep-pdbl"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_SALOONDOOR_T,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_SALOONDOOR_T",
      "saloondoor-t"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_SALOONDOOR_L,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_SALOONDOOR_L",
      "saloondoor-l"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_SALOONDOOR_B,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_SALOONDOOR_B",
      "saloondoor-b"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_SALOONDOOR_R,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_SALOONDOOR_R",
      "saloondoor-r"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_WINDSHIELD_R,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_WINDSHIELD_R",
      "windshield-r"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_WINDSHIELD_U,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_WINDSHIELD_U",
      "windshield-u"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_WINDSHIELD_V,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_WINDSHIELD_V",
      "windshield-v"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_WINDSHIELD_H,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_WINDSHIELD_H",
      "windshield-h"},
  {GES_VIDEO_STANDARD_TRANSITION_TYPE_CROSSFADE,
        "GES_VIDEO_STANDARD_TRANSITION_TYPE_CROSSFADE",
      "crossfade"},
  {0, NULL, NULL}
};

void
_init_standard_transition_assets (void)
{
  guint i;

  for (i = 1; i < G_N_ELEMENTS (transition_types) - 1; i++) {
    GESAsset *asset = ges_asset_request (GES_TYPE_TRANSITION_CLIP,
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
    {GES_TEXT_VALIGN_BASELINE, "GES_TEXT_VALIGN_BASELINE", "baseline"},
    {GES_TEXT_VALIGN_BOTTOM, "GES_TEXT_VALIGN_BOTTOM", "bottom"},
    {GES_TEXT_VALIGN_TOP, "GES_TEXT_VALIGN_TOP", "top"},
    {GES_TEXT_VALIGN_POSITION, "GES_TEXT_VALIGN_POSITION", "position"},
    {GES_TEXT_VALIGN_CENTER, "GES_TEXT_VALIGN_CENTER", "center"},
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
    {GES_TEXT_HALIGN_LEFT, "GES_TEXT_HALIGN_LEFT", "left"},
    {GES_TEXT_HALIGN_CENTER, "GES_TEXT_HALIGN_CENTER", "center"},
    {GES_TEXT_HALIGN_RIGHT, "GES_TEXT_HALIGN_RIGHT", "right"},
    {GES_TEXT_HALIGN_POSITION, "GES_TEXT_HALIGN_POSITION", "position"},
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
  {GES_VIDEO_TEST_PATTERN_SMPTE, "GES_VIDEO_TEST_PATTERN_SMPTE", "smpte"}
  ,
  {GES_VIDEO_TEST_PATTERN_SNOW, "GES_VIDEO_TEST_PATTERN_SNOW", "snow"}
  ,
  {GES_VIDEO_TEST_PATTERN_BLACK, "GES_VIDEO_TEST_PATTERN_BLACK", "black"}
  ,
  {GES_VIDEO_TEST_PATTERN_WHITE, "GES_VIDEO_TEST_PATTERN_WHITE", "white"}
  ,
  {GES_VIDEO_TEST_PATTERN_RED, "GES_VIDEO_TEST_PATTERN_RED", "red"}
  ,
  {GES_VIDEO_TEST_PATTERN_GREEN, "GES_VIDEO_TEST_PATTERN_GREEN", "green"}
  ,
  {GES_VIDEO_TEST_PATTERN_BLUE, "GES_VIDEO_TEST_PATTERN_BLUE", "blue"}
  ,
  {GES_VIDEO_TEST_PATTERN_CHECKERS1,
      "GES_VIDEO_TEST_PATTERN_CHECKERS1", "checkers-1"}
  ,
  {GES_VIDEO_TEST_PATTERN_CHECKERS2,
      "GES_VIDEO_TEST_PATTERN_CHECKERS2", "checkers-2"}
  ,
  {GES_VIDEO_TEST_PATTERN_CHECKERS4,
      "GES_VIDEO_TEST_PATTERN_CHECKERS4", "checkers-4"}
  ,
  {GES_VIDEO_TEST_PATTERN_CHECKERS8,
      "GES_VIDEO_TEST_PATTERN_CHECKERS8", "checkers-8"}
  ,
  {GES_VIDEO_TEST_PATTERN_CIRCULAR,
      "GES_VIDEO_TEST_PATTERN_CIRCULAR", "circular"}
  ,
  {GES_VIDEO_TEST_PATTERN_BLINK, "GES_VIDEO_TEST_PATTERN_BLINK", "blink"}
  ,
  {GES_VIDEO_TEST_PATTERN_SMPTE75, "GES_VIDEO_TEST_PATTERN_SMPTE75", "smpte75"}
  ,
  {GES_VIDEO_TEST_ZONE_PLATE, "GES_VIDEO_TEST_ZONE_PLATE", "zone-plate"}
  ,
  {GES_VIDEO_TEST_GAMUT, "GES_VIDEO_TEST_GAMUT", "gamut"}
  ,
  {GES_VIDEO_TEST_CHROMA_ZONE_PLATE, "GES_VIDEO_TEST_CHROMA_ZONE_PLATE",
      "chroma-zone-plate"}
  ,
  {GES_VIDEO_TEST_PATTERN_SOLID, "GES_VIDEO_TEST_PATTERN_SOLID", "solid-color"}
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
