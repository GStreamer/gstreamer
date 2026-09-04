/* GStreamer Editing Services
 *
 * Copyright (C) 2026 Thibault Saunier <tsaunier@igalia.com>
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

/* Refuse dangerous elements and properties when GES loads effects from an
 * untrusted project file.
 *
 * In 1.30+ this is guarded with new gst-core API (GST_PARSE_FLAG_NO_UNTRUSTED,
 * GST_PARAM_UNTRUSTED_SENSITIVE, per-element marks). The stable branch cannot
 * add that API, so GES enforces a stricter, simplified version of the same
 * policy itself, entirely internally:
 *
 *  - the effect bin-description is refused unless it matches the grammar
 *      element ( '!' element )*
 *    where element is [a-z0-9][a-z0-9_-]* (the charset of every allowed
 *    factory name). Any '=' makes it fail: gst_parse's lexer keys on '=' for
 *    both property assignment and preset loading, so a '='-free description
 *    can set neither. Rejecting the whole exotic grammar (caps filters,
 *    nested bins, pad links, quotes) instead of trying to parse it keeps this
 *    a strict, unambiguous subset of what gst_parse accepts, so there is no
 *    differential-parser gap. Every element token must be on the allowlist.
 *  - sensitive properties (paths, URIs, hosts, shaders/scripts) set from the
 *    project's children-properties are refused by name.
 *
 * Both checks only run inside an "untrusted context", pushed by the formatters
 * around the parts of project loading that construct effects, so programmatic
 * effect creation is unaffected.
 *
 * GES_ALLOW_UNTRUSTED=1 disables the enforcement entirely, for applications
 * that only ever load trusted projects.
 *
 * The two lists below are generated from the untrusted-aware element marks and
 * the GST_PARAM_UNTRUSTED_SENSITIVE property marks in 1.30+. They are a frozen
 * snapshot for this stable branch; an incomplete allowlist only over-refuses
 * (fails safe), it never over-allows.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ges-internal.h"

#include <string.h>

/* 252 allowed element factory names, sorted (bsearch). */
static const gchar *const ges_untrusted_allowed_elements[] = {
  "accurip", "aesdec", "aesenc", "agingtv", "alpha", "alphacolor",
  "aspectratiocrop", "audioamplify", "audiobuffersplit", "audiochebband",
  "audiocheblimit", "audioconvert", "audiodynamic", "audioecho",
  "audiofirfilter", "audioiirfilter", "audioinvert", "audiokaraoke",
  "audiomixmatrix", "audiopanorama", "audioparse", "audiorate", "audioresample",
  "audiosegmentclip", "audiowsincband", "audiowsinclimit", "autovideoflip",
  "avdeinterlace", "avvideocompare", "avwait", "bayer2rgb", "bpmdetect", "bs2b",
  "bulge", "burn", "cairooverlay", "cameraundistort", "capsfilter",
  "cccombiner",
  "ccconverter", "ccextractor", "chromahold", "chromaprint", "chromium",
  "circle", "clockoverlay", "coloreffects", "combdetect", "compare",
  "compositor", "concat", "cutter", "cvdilate", "cvequalizehist", "cverode",
  "cvlaplace", "cvsmooth", "cvsobel", "cvtracker", "debugspy", "deinterlace",
  "deinterleave", "dewarp", "dicetv", "diffuse", "dilate", "disparity", "dodge",
  "downloadbuffer", "dsdconvert", "dtmfdetect", "edgedetect", "edgetv",
  "equalizer-10bands", "equalizer-3bands", "equalizer-nbands", "exclusion",
  "faceblur", "facedetect", "faceoverlay", "fakesink", "fakesrc",
  "fieldanalysis", "filesink", "filesrc", "fisheye", "freeverb", "funnel",
  "gamma", "gaussianblur", "gdkpixbufoverlay", "glalpha", "glalphacombine",
  "glcolorbalance", "glcolorconvert", "glcolorscale", "gldeinterlace",
  "gldifferencematte", "gldownload", "gleffects", "gleffects_blur",
  "gleffects_bulge", "gleffects_fisheye", "gleffects_glow", "gleffects_heat",
  "gleffects_identity", "gleffects_laplacian", "gleffects_lumaxpro",
  "gleffects_mirror", "gleffects_sepia", "gleffects_sin", "gleffects_sobel",
  "gleffects_square", "gleffects_squeeze", "gleffects_stretch",
  "gleffects_tunnel", "gleffects_twirl", "gleffects_xpro", "gleffects_xray",
  "glfilterapp", "glfilterbin", "glfiltercube", "glfilterglass", "glmixerbin",
  "glmosaic", "gloverlay", "gloverlaycompositor", "glshader", "glstereomix",
  "gltransformation", "glupload", "glvideoflip", "glvideomixer",
  "glvideomixerelement", "glviewconvert", "grabcut", "handdetect", "identity",
  "imagefreeze", "input-selector", "interlace", "interleave",
  "ivtc", "jp2kdecimator", "kaleidoscope", "ladspa-amp-so-amp-mono",
  "ladspa-amp-so-amp-stereo", "ladspa-delay-so-delay-5s",
  "ladspa-filter-so-hpf",
  "ladspa-filter-so-lpf", "ladspa-sine-so-sine-faaa",
  "ladspa-sine-so-sine-faac",
  "ladspa-sine-so-sine-fcaa", "ladspasrc-noise-so-noise-white",
  "ladspasrc-sine-so-sine-fcac", "lcms", "level", "line21decoder",
  "line21encoder", "marble", "mirror", "motioncells", "multiqueue",
  "navigationtest", "navseek", "onnxinference", "opencvtextoverlay", "optv",
  "output-selector", "overlaycomposition", "perspective", "pinch", "pitch",
  "qml6gloverlay", "qmlgloverlay", "quarktv", "queue", "queue2", "radioactv",
  "removesilence", "retinex", "revtv", "rganalysis", "rgb2bayer", "rglimiter",
  "rgvolume", "rippletv", "rotate", "rsvgoverlay", "rtponvifparse",
  "rtponviftimestamp", "scaletempo", "scenechange", "segmentation",
  "shagadelictv", "shapewipe", "simplevideomark", "simplevideomarkdetect",
  "skindetect", "smooth", "smpte", "smptealpha", "solarize", "spanplc",
  "spectrum", "speed", "sphere", "square", "stereo", "streaktv",
  "streamiddemux",
  "stretch", "tee", "templatematch", "textoverlay", "textrender",
  "timecodestamper", "timeoverlay", "tunnel", "twirl", "typefind", "valve",
  "vertigotv", "videoanalyse", "videobalance", "videobox", "videoconvert",
  "videoconvertscale", "videocrop", "videodiff", "videoflip",
  "videoframe-audiolevel", "videomedian", "videomixer", "videoparse",
  "videorate", "videoscale", "videosegmentclip", "videotestsrc", "vmaf",
  "volume", "vulkancolorconvert", "vulkandownload", "vulkanimageidentity",
  "vulkanoverlaycompositor", "vulkanshaderspv", "vulkanupload",
  "vulkanviewconvert", "warptv", "waterripple", "zbar", "zebrastripe"
};

/* Property names refused when set from untrusted content (I/O or external
 * references). Name-based, so a differently named dangerous property is not
 * caught; the main branch marks (element, property) pairs precisely. Sorted. */
static const gchar *const ges_untrusted_sensitive_properties[] = {
  "datafile", "fragment", "fragment-location", "http-proxy", "location",
  "qml-scene", "results-filename", "shader", "stats-file", "stun-server",
  "temp-template", "turn-server", "vertex", "vertex-location"
};

static GPrivate ges_untrusted_depth = G_PRIVATE_INIT (NULL);

static gboolean ges_untrusted_enforcement_enabled (void);

void
ges_untrusted_context_push (void)
{
  gsize depth = GPOINTER_TO_SIZE (g_private_get (&ges_untrusted_depth));
  g_private_replace (&ges_untrusted_depth, GSIZE_TO_POINTER (depth + 1));
}

void
ges_untrusted_context_pop (void)
{
  gsize depth = GPOINTER_TO_SIZE (g_private_get (&ges_untrusted_depth));
  if (depth > 0)
    g_private_replace (&ges_untrusted_depth, GSIZE_TO_POINTER (depth - 1));
}

gboolean
ges_untrusted_context_active (void)
{
  if (!ges_untrusted_enforcement_enabled ())
    return FALSE;
  return GPOINTER_TO_SIZE (g_private_get (&ges_untrusted_depth)) > 0;
}

static gboolean
ges_untrusted_enforcement_enabled (void)
{
  static gsize init = 0;
  static gboolean enabled = TRUE;

  if (g_once_init_enter (&init)) {
    const gchar *env = g_getenv ("GES_ALLOW_UNTRUSTED");

    if (env && env[0] != '\0' && g_strcmp0 (env, "0") != 0)
      enabled = FALSE;
    g_once_init_leave (&init, 1);
  }

  return enabled;
}

static int
compare_str (const void *a, const void *b)
{
  return strcmp (*(const gchar * const *) a, *(const gchar * const *) b);
}

static gboolean
element_is_allowed (const gchar * name)
{
  return bsearch (&name, ges_untrusted_allowed_elements,
      G_N_ELEMENTS (ges_untrusted_allowed_elements),
      sizeof (gchar *), compare_str) != NULL;
}

static gboolean
property_is_sensitive (const gchar * name)
{
  return bsearch (&name, ges_untrusted_sensitive_properties,
      G_N_ELEMENTS (ges_untrusted_sensitive_properties),
      sizeof (gchar *), compare_str) != NULL;
}

gboolean
ges_untrusted_bin_desc_check (const gchar * bin_desc, GError ** error)
{
  const gchar *p = bin_desc;

  if (!ges_untrusted_enforcement_enabled () || bin_desc == NULL)
    return TRUE;

  while (*p) {
    const gchar *start;
    gchar *token;
    gboolean allowed;

    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '!')
      p++;
    if (*p == '\0')
      break;

    start = p;
    while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r'
        && *p != '!') {
      gchar c = *p;

      if (!(c >= 'a' && c <= 'z') && !(c >= '0' && c <= '9') && c != '-'
          && c != '_') {
        g_set_error (error, GES_ERROR, GES_ERROR_ASSET_WRONG_ID,
            "Refusing untrusted effect \"%s\": it sets a property or uses "
            "unsupported syntax (character '%c')", bin_desc, c);
        return FALSE;
      }
      p++;
    }

    token = g_strndup (start, p - start);
    allowed = element_is_allowed (token);
    if (!allowed) {
      g_set_error (error, GES_ERROR, GES_ERROR_ASSET_WRONG_ID,
          "Refusing untrusted effect \"%s\": element \"%s\" is not allowed "
          "from an untrusted project", bin_desc, token);
      g_free (token);
      return FALSE;
    }
    g_free (token);
  }

  return TRUE;
}

gboolean
ges_untrusted_property_check (const gchar * property_name, GError ** error)
{
  /* This is only reached while loading a project, which is untrusted content, so
   * gate on the GES_ALLOW_UNTRUSTED override only, not the thread-local context,
   * which may not be active in the asynchronous asset-loading callback. */
  if (!ges_untrusted_enforcement_enabled ())
    return TRUE;

  if (property_is_sensitive (property_name)) {
    g_set_error (error, GES_ERROR, GES_ERROR_FORMATTER_MALFORMED_INPUT_FILE,
        "Refusing to set property \"%s\" from an untrusted project",
        property_name);
    return FALSE;
  }

  return TRUE;
}
