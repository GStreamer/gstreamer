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

#pragma once

#include <gst/gst.h>
#include <ges/ges-prelude.h>

G_BEGIN_DECLS

#define GES_TYPE_TRACK_TYPE (ges_track_type_get_type ())
GES_API
GType ges_track_type_get_type (void);

/**
 * GESTrackType:
 * @GES_TRACK_TYPE_UNKNOWN: A track of unknown type (i.e. invalid)
 * @GES_TRACK_TYPE_AUDIO: An audio track
 * @GES_TRACK_TYPE_VIDEO: A video track
 * @GES_TRACK_TYPE_TEXT: A text (subtitle) track
 * @GES_TRACK_TYPE_CUSTOM: A custom-content track
 *
 * Types of content handled by a track. If the content is not one of
 * @GES_TRACK_TYPE_AUDIO, @GES_TRACK_TYPE_VIDEO or @GES_TRACK_TYPE_TEXT,
 * the user of the #GESTrack must set the type to @GES_TRACK_TYPE_CUSTOM.
 *
 * @GES_TRACK_TYPE_UNKNOWN is for internal purposes and should not be used
 * by users
 */

typedef enum {
  GES_TRACK_TYPE_UNKNOWN = 1 << 0,
  GES_TRACK_TYPE_AUDIO   = 1 << 1,
  GES_TRACK_TYPE_VIDEO   = 1 << 2,
  GES_TRACK_TYPE_TEXT    = 1 << 3,
  GES_TRACK_TYPE_CUSTOM  = 1 << 4,
} GESTrackType;

#define GES_META_FLAG_TYPE (ges_meta_flag_get_type ())
GES_API
GType ges_meta_flag_get_type (void);

/**
 * GESMetaFlag:
 * @GES_META_READABLE: The metadata is readable
 * @GES_META_WRITABLE: The metadata is writable
 * @GES_META_READ_WRITE: The metadata is readable and writable
 */
typedef enum {
  GES_META_READABLE  = 1 << 0,
  GES_META_WRITABLE = 1 << 1,
  GES_META_READ_WRITE = GES_META_READABLE | GES_META_WRITABLE
} GESMetaFlag;

/**
 * GESVideoStandardTransitionType:
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_NONE: Transition type has not been set,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_BAR_WIPE_LR: A bar moves from left to right,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_BAR_WIPE_TB: A bar moves from top to bottom,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_BOX_WIPE_TL: A box expands from the upper-left corner to the lower-right corner,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_BOX_WIPE_TR: A box expands from the upper-right corner to the lower-left corner,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_BOX_WIPE_BR: A box expands from the lower-right corner to the upper-left corner,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_BOX_WIPE_BL: A box expands from the lower-left corner to the upper-right corner,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_FOUR_BOX_WIPE_CI: A box shape expands from each of the four corners toward the center,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_FOUR_BOX_WIPE_CO: A box shape expands from the center of each quadrant toward the corners of each quadrant,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_BARNDOOR_V: A central, vertical line splits and expands toward the left and right edges,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_BARNDOOR_H: A central, horizontal line splits and expands toward the top and bottom edges,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_BOX_WIPE_TC: A box expands from the top edge's midpoint to the bottom corners,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_BOX_WIPE_RC: A box expands from the right edge's midpoint to the left corners,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_BOX_WIPE_BC: A box expands from the bottom edge's midpoint to the top corners,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_BOX_WIPE_LC: A box expands from the left edge's midpoint to the right corners,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_DIAGONAL_TL: A diagonal line moves from the upper-left corner to the lower-right corner,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_DIAGONAL_TR: A diagonal line moves from the upper right corner to the lower-left corner,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_BOWTIE_V: Two wedge shapes slide in from the top and bottom edges toward the center,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_BOWTIE_H: Two wedge shapes slide in from the left and right edges toward the center,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_BARNDOOR_DBL: A diagonal line from the lower-left to upper-right corners splits and expands toward the opposite corners,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_BARNDOOR_DTL: A diagonal line from upper-left to lower-right corners splits and expands toward the opposite corners,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_MISC_DIAGONAL_DBD: Four wedge shapes split from the center and retract toward the four edges,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_MISC_DIAGONAL_DD: A diamond connecting the four edge midpoints simultaneously contracts toward the center and expands toward the edges,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_VEE_D: A wedge shape moves from top to bottom,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_VEE_L: A wedge shape moves from right to left,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_VEE_U: A wedge shape moves from bottom to top,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_VEE_R: A wedge shape moves from left to right,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_BARNVEE_D: A 'V' shape extending from the bottom edge's midpoint to the opposite corners contracts toward the center and expands toward the edges,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_BARNVEE_L: A 'V' shape extending from the left edge's midpoint to the opposite corners contracts toward the center and expands toward the edges,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_BARNVEE_U: A 'V' shape extending from the top edge's midpoint to the opposite corners contracts toward the center and expands toward the edges,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_BARNVEE_R: A 'V' shape extending from the right edge's midpoint to the opposite corners contracts toward the center and expands toward the edges,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_IRIS_RECT: A rectangle expands from the center.,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_CLOCK_CW12: A radial hand sweeps clockwise from the twelve o'clock position,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_CLOCK_CW3: A radial hand sweeps clockwise from the three o'clock position,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_CLOCK_CW6: A radial hand sweeps clockwise from the six o'clock position,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_CLOCK_CW9: A radial hand sweeps clockwise from the nine o'clock position,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_PINWHEEL_TBV: Two radial hands sweep clockwise from the twelve and six o'clock positions,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_PINWHEEL_TBH: Two radial hands sweep clockwise from the nine and three o'clock positions,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_PINWHEEL_FB: Four radial hands sweep clockwise,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_FAN_CT: A fan unfolds from the top edge, the fan axis at the center,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_FAN_CR: A fan unfolds from the right edge, the fan axis at the center,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_DOUBLEFAN_FOV: Two fans, their axes at the center, unfold from the top and bottom,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_DOUBLEFAN_FOH: Two fans, their axes at the center, unfold from the left and right,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_SINGLESWEEP_CWT: A radial hand sweeps clockwise from the top edge's midpoint,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_SINGLESWEEP_CWR: A radial hand sweeps clockwise from the right edge's midpoint,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_SINGLESWEEP_CWB: A radial hand sweeps clockwise from the bottom edge's midpoint,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_SINGLESWEEP_CWL: A radial hand sweeps clockwise from the left edge's midpoint,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_DOUBLESWEEP_PV: Two radial hands sweep clockwise and counter-clockwise from the top and bottom edges' midpoints,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_DOUBLESWEEP_PD: Two radial hands sweep clockwise and counter-clockwise from the left and right edges' midpoints,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_DOUBLESWEEP_OV: Two radial hands attached at the top and bottom edges' midpoints sweep from right to left,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_DOUBLESWEEP_OH: Two radial hands attached at the left and right edges' midpoints sweep from top to bottom,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_FAN_T: A fan unfolds from the bottom, the fan axis at the top edge's midpoint,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_FAN_R: A fan unfolds from the left, the fan axis at the right edge's midpoint,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_FAN_B: A fan unfolds from the top, the fan axis at the bottom edge's midpoint,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_FAN_L: A fan unfolds from the right, the fan axis at the left edge's midpoint,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_DOUBLEFAN_FIV: Two fans, their axes at the top and bottom, unfold from the center,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_DOUBLEFAN_FIH: Two fans, their axes at the left and right, unfold from the center,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_SINGLESWEEP_CWTL: A radial hand sweeps clockwise from the upper-left corner,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_SINGLESWEEP_CWBL: A radial hand sweeps counter-clockwise from the lower-left corner.,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_SINGLESWEEP_CWBR: A radial hand sweeps clockwise from the lower-right corner,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_SINGLESWEEP_CWTR: A radial hand sweeps counter-clockwise from the upper-right corner,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_DOUBLESWEEP_PDTL: Two radial hands attached at the upper-left and lower-right corners sweep down and up,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_DOUBLESWEEP_PDBL: Two radial hands attached at the lower-left and upper-right corners sweep down and up,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_SALOONDOOR_T: Two radial hands attached at the upper-left and upper-right corners sweep down,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_SALOONDOOR_L: Two radial hands attached at the upper-left and lower-left corners sweep to the right,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_SALOONDOOR_B: Two radial hands attached at the lower-left and lower-right corners sweep up,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_SALOONDOOR_R: Two radial hands attached at the upper-right and lower-right corners sweep to the left,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_WINDSHIELD_R: Two radial hands attached at the midpoints of the top and bottom halves sweep from right to left,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_WINDSHIELD_U: Two radial hands attached at the midpoints of the left and right halves sweep from top to bottom,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_WINDSHIELD_V: Two sets of radial hands attached at the midpoints of the top and bottom halves sweep from top to bottom and bottom to top,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_WINDSHIELD_H: Two sets of radial hands attached at the midpoints of the left and right halves sweep from left to right and right to left,
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_CROSSFADE: Crossfade
 * @GES_VIDEO_STANDARD_TRANSITION_TYPE_FADE_IN: Similar to crossfade, but fade in the front video without fading out the background one (Since: 1.22)
 *
 */

typedef enum {
  GES_VIDEO_STANDARD_TRANSITION_TYPE_NONE = 0,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_BAR_WIPE_LR = 1,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_BAR_WIPE_TB = 2,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_BOX_WIPE_TL = 3,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_BOX_WIPE_TR = 4,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_BOX_WIPE_BR = 5,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_BOX_WIPE_BL = 6,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_FOUR_BOX_WIPE_CI = 7,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_FOUR_BOX_WIPE_CO = 8,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_BARNDOOR_V = 21,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_BARNDOOR_H = 22,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_BOX_WIPE_TC = 23,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_BOX_WIPE_RC = 24,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_BOX_WIPE_BC = 25,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_BOX_WIPE_LC = 26,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_DIAGONAL_TL = 41,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_DIAGONAL_TR = 42,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_BOWTIE_V = 43,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_BOWTIE_H = 44,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_BARNDOOR_DBL = 45,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_BARNDOOR_DTL = 46,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_MISC_DIAGONAL_DBD = 47,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_MISC_DIAGONAL_DD = 48,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_VEE_D = 61,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_VEE_L = 62,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_VEE_U = 63,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_VEE_R = 64,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_BARNVEE_D = 65,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_BARNVEE_L = 66,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_BARNVEE_U = 67,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_BARNVEE_R = 68,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_IRIS_RECT = 101,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_CLOCK_CW12 = 201,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_CLOCK_CW3 = 202,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_CLOCK_CW6 = 203,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_CLOCK_CW9 = 204,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_PINWHEEL_TBV = 205,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_PINWHEEL_TBH = 206,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_PINWHEEL_FB = 207,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_FAN_CT = 211,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_FAN_CR = 212,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_DOUBLEFAN_FOV = 213,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_DOUBLEFAN_FOH = 214,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_SINGLESWEEP_CWT = 221,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_SINGLESWEEP_CWR = 222,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_SINGLESWEEP_CWB = 223,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_SINGLESWEEP_CWL = 224,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_DOUBLESWEEP_PV = 225,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_DOUBLESWEEP_PD = 226,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_DOUBLESWEEP_OV = 227,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_DOUBLESWEEP_OH = 228,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_FAN_T = 231,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_FAN_R = 232,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_FAN_B = 233,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_FAN_L = 234,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_DOUBLEFAN_FIV = 235,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_DOUBLEFAN_FIH = 236,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_SINGLESWEEP_CWTL = 241,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_SINGLESWEEP_CWBL = 242,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_SINGLESWEEP_CWBR = 243,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_SINGLESWEEP_CWTR = 244,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_DOUBLESWEEP_PDTL = 245,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_DOUBLESWEEP_PDBL = 246,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_SALOONDOOR_T = 251,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_SALOONDOOR_L = 252,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_SALOONDOOR_B = 253,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_SALOONDOOR_R = 254,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_WINDSHIELD_R = 261,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_WINDSHIELD_U = 262,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_WINDSHIELD_V = 263,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_WINDSHIELD_H = 264,
  GES_VIDEO_STANDARD_TRANSITION_TYPE_CROSSFADE = 512,
  /**
   * GES_VIDEO_STANDARD_TRANSITION_TYPE_FADE_IN:
   *
   * Similar to crossfade, but fade in the front video without fading out
   * the background one
   *
   * Since: 1.22
   */
  GES_VIDEO_STANDARD_TRANSITION_TYPE_FADE_IN = 513
} GESVideoStandardTransitionType;

#define GES_VIDEO_STANDARD_TRANSITION_TYPE_TYPE \
    (ges_video_standard_transition_type_get_type())

GES_API
GType ges_video_standard_transition_type_get_type (void);

/**
 * GESTextVAlign:
 * @GES_TEXT_VALIGN_BASELINE: draw text on the baseline
 * @GES_TEXT_VALIGN_BOTTOM: draw text on the bottom
 * @GES_TEXT_VALIGN_TOP: draw text on top
 * @GES_TEXT_VALIGN_POSITION: draw text on ypos position
 * @GES_TEXT_VALIGN_CENTER: draw text on the center
 *
 * Vertical alignment of the text.
 */
typedef enum {
    GES_TEXT_VALIGN_BASELINE,
    GES_TEXT_VALIGN_BOTTOM,
    GES_TEXT_VALIGN_TOP,
    GES_TEXT_VALIGN_POSITION,
    GES_TEXT_VALIGN_CENTER,
    GES_TEXT_VALIGN_ABSOLUTE
} GESTextVAlign;

#define DEFAULT_VALIGNMENT GES_TEXT_VALIGN_BASELINE

#define GES_TEXT_VALIGN_TYPE\
  (ges_text_valign_get_type ())

GES_API
GType ges_text_valign_get_type (void);

/**
 * GESTextHAlign:
 * @GES_TEXT_HALIGN_LEFT: align text left
 * @GES_TEXT_HALIGN_CENTER: align text center
 * @GES_TEXT_HALIGN_RIGHT: align text right
 * @GES_TEXT_HALIGN_POSITION: align text on xpos position
 *
 * Horizontal alignment of the text.
 */
typedef enum {
    GES_TEXT_HALIGN_LEFT = 0,
    GES_TEXT_HALIGN_CENTER = 1,
    GES_TEXT_HALIGN_RIGHT = 2,
    GES_TEXT_HALIGN_POSITION = 4,
    GES_TEXT_HALIGN_ABSOLUTE = 5
} GESTextHAlign;

#define DEFAULT_HALIGNMENT GES_TEXT_HALIGN_CENTER

#define GES_TEXT_HALIGN_TYPE\
  (ges_text_halign_get_type ())

GES_API
GType ges_text_halign_get_type (void);

/**
 * GESVideoTestPattern:
 * @GES_VIDEO_TEST_PATTERN_SMPTE: A standard SMPTE test pattern
 * @GES_VIDEO_TEST_PATTERN_SNOW: Random noise
 * @GES_VIDEO_TEST_PATTERN_BLACK: A black image
 * @GES_VIDEO_TEST_PATTERN_WHITE: A white image
 * @GES_VIDEO_TEST_PATTERN_RED: A red image
 * @GES_VIDEO_TEST_PATTERN_GREEN: A green image
 * @GES_VIDEO_TEST_PATTERN_BLUE: A blue image
 * @GES_VIDEO_TEST_PATTERN_CHECKERS1: Checkers pattern (1px)
 * @GES_VIDEO_TEST_PATTERN_CHECKERS2: Checkers pattern (2px)
 * @GES_VIDEO_TEST_PATTERN_CHECKERS4: Checkers pattern (4px)
 * @GES_VIDEO_TEST_PATTERN_CHECKERS8: Checkers pattern (8px)
 * @GES_VIDEO_TEST_PATTERN_CIRCULAR: Circular pattern
 * @GES_VIDEO_TEST_PATTERN_SOLID: Solid color
 * @GES_VIDEO_TEST_PATTERN_BLINK: Alternate between black and white
 * @GES_VIDEO_TEST_ZONE_PLATE: Zone plate
 * @GES_VIDEO_TEST_GAMUT: Gamut checkers
 * @GES_VIDEO_TEST_CHROMA_ZONE_PLATE: Chroma zone plate
 * @GES_VIDEO_TEST_PATTERN_SMPTE75: SMPTE test pattern (75% color bars)
 *
 * The test pattern to produce
 */

typedef enum {
  GES_VIDEO_TEST_PATTERN_SMPTE,
  GES_VIDEO_TEST_PATTERN_SNOW,
  GES_VIDEO_TEST_PATTERN_BLACK,
  GES_VIDEO_TEST_PATTERN_WHITE,
  GES_VIDEO_TEST_PATTERN_RED,
  GES_VIDEO_TEST_PATTERN_GREEN,
  GES_VIDEO_TEST_PATTERN_BLUE,
  GES_VIDEO_TEST_PATTERN_CHECKERS1,
  GES_VIDEO_TEST_PATTERN_CHECKERS2,
  GES_VIDEO_TEST_PATTERN_CHECKERS4,
  GES_VIDEO_TEST_PATTERN_CHECKERS8,
  GES_VIDEO_TEST_PATTERN_CIRCULAR,
  GES_VIDEO_TEST_PATTERN_BLINK,
  GES_VIDEO_TEST_PATTERN_SMPTE75,
  GES_VIDEO_TEST_ZONE_PLATE,
  GES_VIDEO_TEST_GAMUT,
  GES_VIDEO_TEST_CHROMA_ZONE_PLATE,
  GES_VIDEO_TEST_PATTERN_SOLID,
} GESVideoTestPattern;


#define GES_VIDEO_TEST_PATTERN_TYPE\
  ges_video_test_pattern_get_type()

GES_API
GType ges_video_test_pattern_get_type (void);

/**
 * GESPipelineFlags:
 * @GES_PIPELINE_MODE_PREVIEW_AUDIO: Output the #GESPipeline:timeline's
 * audio to the soundcard
 * @GES_PIPELINE_MODE_PREVIEW_VIDEO: Output the #GESPipeline:timeline's
 * video to the screen
 * @GES_PIPELINE_MODE_PREVIEW: Output both the #GESPipeline:timeline's
 * audio and video to the soundcard and screen (default)
 * @GES_PIPELINE_MODE_RENDER: Render the #GESPipeline:timeline with
 * forced decoding (the underlying #encodebin has its
 * #encodebin:avoid-reencoding property set to %FALSE)
 * @GES_PIPELINE_MODE_SMART_RENDER: Render the #GESPipeline:timeline,
 * avoiding decoding/reencoding (the underlying #encodebin has its
 * #encodebin:avoid-reencoding property set to %TRUE).
 * > NOTE: Smart rendering can not work in tracks where #GESTrack:mixing
 * > is enabled.
 *
 * The various modes a #GESPipeline can be configured to.
 */
typedef enum {
  GES_PIPELINE_MODE_PREVIEW_AUDIO	= 1 << 0,
  GES_PIPELINE_MODE_PREVIEW_VIDEO	= 1 << 1,
  GES_PIPELINE_MODE_PREVIEW		= GES_PIPELINE_MODE_PREVIEW_AUDIO | GES_PIPELINE_MODE_PREVIEW_VIDEO,
  GES_PIPELINE_MODE_RENDER		= 1 << 2,
  GES_PIPELINE_MODE_SMART_RENDER	= 1 << 3
} GESPipelineFlags;

#define GES_TYPE_PIPELINE_FLAGS\
  ges_pipeline_flags_get_type()

GES_API
GType ges_pipeline_flags_get_type (void);

/**
 * GESEditMode:
 * @GES_EDIT_MODE_NORMAL: The element is edited the normal way (default).
 *  If acting on the element as a whole (#GES_EDGE_NONE), this will MOVE
 *  the element by MOVING its toplevel. When acting on the start of the
 *  element (#GES_EDGE_START), this will only MOVE the element, but not
 *  its toplevel parent. This can allow you to move a #GESClip or
 *  #GESGroup to a new start time or layer within its container group,
 *  without effecting other members of the group. When acting on the end
 *  of the element (#GES_EDGE_END), this will END-TRIM the element,
 *  leaving its toplevel unchanged.
 * @GES_EDIT_MODE_RIPPLE: The element is edited in ripple mode: moving
 *  itself as well as later elements, keeping their relative times. This
 *  edits the element the same as #GES_EDIT_MODE_NORMAL. In addition, if
 *  acting on the element as a whole, or the start of the element, any
 *  toplevel element in the same timeline (including different layers)
 *  whose start time is later than the *current* start time of the MOVED
 *  element will also be MOVED by the same shift as the edited element.
 *  If acting on the end of the element, any toplevel element whose start
 *  time is later than the *current* end time of the edited element will
 *  also be MOVED by the same shift as the change in the end of the
 *  edited element. These additional elements will also be shifted by
 *  the same shift in layers as the edited element.
 * @GES_EDIT_MODE_ROLL: The element is edited in roll mode: swapping its
 *  content for its neighbour's, or vis versa, in the timeline output.
 *  This edits the element the same as #GES_EDIT_MODE_TRIM. In addition,
 *  any neighbours are also TRIMMED at their opposite edge to the same
 *  timeline position. When acting on the start of the element, a
 *  neighbour is any earlier element in the timeline whose end time
 *  matches the *current* start time of the edited element. When acting on
 *  the end of the element, a neighbour is any later element in the
 *  timeline whose start time matches the *current* start time of the
 *  edited element. In addition, a neighbour have a #GESSource at its
 *  end/start edge that shares a track with a #GESSource at the start/end
 *  edge of the edited element. Basically, a neighbour is an element that
 *  can be extended, or cut, to have its content replace, or be replaced
 *  by, the content of the edited element. Acting on the element as a
 *  whole (#GES_EDGE_NONE) is not defined. The element can not shift
 *  layers under this mode.
 * @GES_EDIT_MODE_TRIM: The element is edited in trim mode. When acting
 *  on the start of the element, this will START-TRIM it. When acting on
 *  the end of the element, this will END-TRIM it. Acting on the element
 *  as a whole (#GES_EDGE_NONE) is not defined.
 * @GES_EDIT_MODE_SLIDE: The element is edited in slide mode (not yet
 *  implemented): moving the element replacing or consuming content on
 *  each end. When acting on the element as a whole, this will MOVE the
 *  element, and TRIM any neighbours on either side. A neighbour is
 *  defined in the same way as in #GES_EDIT_MODE_ROLL, but they may be on
 *  either side of the edited elements. Elements at the end with be
 *  START-TRIMMED to the new end position of the edited element. Elements
 *  at the start will be END-TRIMMED to the new start position of the
 *  edited element. Acting on the start or end of the element
 *  (#GES_EDGE_START and #GES_EDGE_END) is not defined. The element can
 *  not shift layers under this mode.
 *
 * When a single timeline element is edited within its timeline at some
 * position, using ges_timeline_element_edit(), depending on the edit
 * mode, its #GESTimelineElement:start, #GESTimelineElement:duration or
 * #GESTimelineElement:in-point will be adjusted accordingly. In addition,
 * any clips may change #GESClip:layer.
 *
 * Each edit can be broken down into a combination of three basic edits:
 *
 * + MOVE: This moves the start of the element to the edit position.
 * + START-TRIM: This cuts or grows the start of the element, whilst
 *   maintaining the time at which its internal content appears in the
 *   timeline data output. If the element is made shorter, the data that
 *   appeared at the edit position will still appear in the timeline at
 *   the same time. If the element is made longer, the data that appeared
 *   at the previous start of the element will still appear in the
 *   timeline at the same time.
 * + END-TRIM: Similar to START-TRIM, but the end of the element is cut or
 *   grown.
 *
 * In particular, when editing a #GESClip:
 *
 * + MOVE: This will set the #GESTimelineElement:start of the clip to the
 *   edit position.
 * + START-TRIM: This will set the #GESTimelineElement:start of the clip
 *   to the edit position. To keep the end time the same, the
 *   #GESTimelineElement:duration of the clip will be adjusted in the
 *   opposite direction. In addition, the #GESTimelineElement:in-point of
 *   the clip will be shifted such that the content that appeared at the
 *   new or previous start time, whichever is latest, still appears at the
 *   same timeline time. For example, if a frame appeared at the start of
 *   the clip, and the start of the clip is reduced, the in-point of the
 *   clip will also reduce such that the frame will appear later within
 *   the clip, but at the same timeline position.
 * + END-TRIM: This will set the #GESTimelineElement:duration of the clip
 *   such that its end time will match the edit position.
 *
 * When editing a #GESGroup:
 *
 * + MOVE: This will set the #GESGroup:start of the clip to the edit
 *   position by shifting all of its children by the same amount. So each
 *   child will maintain their relative positions.
 * + START-TRIM: If the group is made shorter, this will START-TRIM any
 *   clips under the group that start after the edit position to the same
 *   edit position. If the group is made longer, this will START-TRIM any
 *   clip under the group whose start matches the start of the group to
 *   the same edit position.
 * + END-TRIM: If the group is made shorter, this will END-TRIM any clips
 *   under the group that end after the edit position to the same edit
 *   position. If the group is made longer, this will END-TRIM any clip
 *   under the group whose end matches the end of the group to the same
 *   edit position.
 *
 * When editing a #GESTrackElement, if it has a #GESClip parent, this
 * will be edited instead. Otherwise it is edited in the same way as a
 * #GESClip.
 *
 * The layer priority of a #GESGroup is the lowest layer priority of any
 * #GESClip underneath it. When a group is edited to a new layer
 * priority, it will shift all clips underneath it by the same amount,
 * such that their relative layers stay the same.
 *
 * If the #GESTimeline has a #GESTimeline:snapping-distance, then snapping
 * may occur for some of the edges of the **main** edited element:
 *
 * + MOVE: The start or end edge of *any* #GESSource under the element may
 *   be snapped.
 * + START-TRIM: The start edge of a #GESSource whose start edge touches
 *   the start edge of the element may snap.
 * + END-TRIM: The end edge of a #GESSource whose end edge touches the end
 *   edge of the element may snap.
 *
 * These edges may snap with either the start or end edge of *any* other
 * #GESSource in the timeline that is not also being moved by the element,
 * including those in different layers, if they are within the
 * #GESTimeline:snapping-distance. During an edit, only up to one snap can
 * occur. This will shift the edit position such that the snapped edges
 * will touch once the edit has completed.
 *
 * Note that snapping can cause an edit to fail where it would have
 * otherwise succeeded because it may push the edit position such that the
 * edit would result in an unsupported timeline configuration. Similarly,
 * snapping can cause an edit to succeed where it would have otherwise
 * failed.
 *
 * For example, in #GES_EDIT_MODE_RIPPLE acting on #GES_EDGE_NONE, the
 * main element is the MOVED toplevel of the edited element. Any source
 * under the main MOVED toplevel may have its start or end edge snapped.
 * Note, these sources cannot snap with each other. The edit may also
 * push other elements, but any sources under these elements cannot snap,
 * nor can they be snapped with. If a snap does occur, the MOVE of the
 * toplevel *and* all other elements pushed by the ripple will be shifted
 * by the same amount such that the snapped edges will touch.
 *
 * You can also find more explanation about the behaviour of those modes at:
 * [trim, ripple and roll](http://pitivi.org/manual/trimming.html)
 * and [clip management](http://pitivi.org/manual/usingclips.html).
 */
typedef enum {
    GES_EDIT_MODE_NORMAL,
    GES_EDIT_MODE_RIPPLE,
    GES_EDIT_MODE_ROLL,
    GES_EDIT_MODE_TRIM,
    GES_EDIT_MODE_SLIDE
} GESEditMode;

/**
 * ges_edit_mode_name:
 * @mode: a #GESEditMode
 *
 * Return a string representation of @mode.
 *
 * Returns: (transfer none): a string representation of @mode.
 * Since: 1.18
 */
GES_API
const gchar * ges_edit_mode_name (GESEditMode mode);

#define GES_TYPE_EDIT_MODE ges_edit_mode_get_type()

GES_API
GType ges_edit_mode_get_type (void);

/**
 * GESEdge:
 * @GES_EDGE_START: Represents the start of an object.
 * @GES_EDGE_END: Represents the end of an object.
 * @GES_EDGE_NONE: Represent the fact we are not working with any edge of an
 *   object.
 *
 * The edges of an object contain in a #GESTimeline or #GESTrack
 */
typedef enum {
    GES_EDGE_START,
    GES_EDGE_END,
    GES_EDGE_NONE
} GESEdge;

GES_API
const gchar * ges_edge_name (GESEdge edge);

#define GES_TYPE_EDGE ges_edge_get_type()

GES_API
GType ges_edge_get_type (void);

#define GES_TYPE_MARKER_FLAGS (ges_marker_flags_get_type ())

GES_API
GType ges_marker_flags_get_type (void);

/**
 * GESMarkerFlags:
 * @GES_MARKER_FLAG_NONE: Marker does not serve any special purpose.
 * @GES_MARKER_FLAG_SNAPPABLE: Marker can be a snapping target.
 *
 * Since: 1.20
 */
typedef enum {
  GES_MARKER_FLAG_NONE = 0,
  GES_MARKER_FLAG_SNAPPABLE = 1 << 0,
} GESMarkerFlags;


GES_API
const gchar * ges_track_type_name (GESTrackType type);
G_END_DECLS
