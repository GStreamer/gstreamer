/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2003> David Schleef <ds@schleef.org>
 * Copyright (C) <2006> Julien Moutte <julien@moutte.net>
 * Copyright (C) <2006> Zeeshan Ali <zeeshan.ali@nokia.com>
 * Copyright (C) <2006-2008> Tim-Philipp Müller <tim centricular net>
 * Copyright (C) <2009> Young-Ho Cha <ganadist@gmail.com>
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
 * SECTION:element-textoverlay
 * @see_also: #GstTextRender, #GstClockOverlay, #GstTimeOverlay, #GstSubParse
 *
 * This plugin renders text on top of a video stream. This can be either
 * static text or text from buffers received on the text sink pad, e.g.
 * as produced by the subparse element. If the text sink pad is not linked,
 * the text set via the "text" property will be rendered. If the text sink
 * pad is linked, text will be rendered as it is received on that pad,
 * honouring and matching the buffer timestamps of both input streams.
 *
 * The text can contain newline characters and text wrapping is enabled by
 * default.
 *
 * <refsect2>
 * <title>Example launch lines</title>
 * |[
 * gst-launch -v videotestsrc ! textoverlay text="Room A" valign=top halign=left ! xvimagesink
 * ]| Here is a simple pipeline that displays a static text in the top left
 * corner of the video picture
 * |[
 * gst-launch -v filesrc location=subtitles.srt ! subparse ! txt.   videotestsrc ! timeoverlay ! textoverlay name=txt shaded-background=yes ! xvimagesink
 * ]| Here is another pipeline that displays subtitles from an .srt subtitle
 * file, centered at the bottom of the picture and with a rectangular shading
 * around the text in the background:
 * <para>
 * If you do not have such a subtitle file, create one looking like this
 * in a text editor:
 * |[
 * 1
 * 00:00:03,000 --> 00:00:05,000
 * Hello? (3-5s)
 *
 * 2
 * 00:00:08,000 --> 00:00:13,000
 * Yes, this is a subtitle. Don&apos;t
 * you like it? (8-13s)
 *
 * 3
 * 00:00:18,826 --> 00:01:02,886
 * Uh? What are you talking about?
 * I don&apos;t understand  (18-62s)
 * ]|
 * </para>
 * </refsect2>
 */

/* FIXME: alloc segment as part of instance struct */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>

#include "gstbasetextoverlay.h"
#include "gsttextoverlay.h"
#include "gsttimeoverlay.h"
#include "gstclockoverlay.h"
#include "gsttextrender.h"
#include <string.h>

/* FIXME:
 *  - use proper strides and offset for I420
 *  - if text is wider than the video picture, it does not get
 *    clipped properly during blitting (if wrapping is disabled)
 *  - make 'shading_value' a property (or enum:  light/normal/dark/verydark)?
 */

GST_DEBUG_CATEGORY (pango_debug);
#define GST_CAT_DEFAULT pango_debug

#define DEFAULT_PROP_TEXT 	""
#define DEFAULT_PROP_SHADING	FALSE
#define DEFAULT_PROP_VALIGNMENT	GST_BASE_TEXT_OVERLAY_VALIGN_BASELINE
#define DEFAULT_PROP_HALIGNMENT	GST_BASE_TEXT_OVERLAY_HALIGN_CENTER
#define DEFAULT_PROP_XPAD	25
#define DEFAULT_PROP_YPAD	25
#define DEFAULT_PROP_DELTAX	0
#define DEFAULT_PROP_DELTAY	0
#define DEFAULT_PROP_XPOS       0.5
#define DEFAULT_PROP_YPOS       0.5
#define DEFAULT_PROP_WRAP_MODE  GST_BASE_TEXT_OVERLAY_WRAP_MODE_WORD_CHAR
#define DEFAULT_PROP_FONT_DESC	""
#define DEFAULT_PROP_SILENT	FALSE
#define DEFAULT_PROP_LINE_ALIGNMENT GST_BASE_TEXT_OVERLAY_LINE_ALIGN_CENTER
#define DEFAULT_PROP_WAIT_TEXT	TRUE
#define DEFAULT_PROP_AUTO_ADJUST_SIZE TRUE
#define DEFAULT_PROP_VERTICAL_RENDER  FALSE
#define DEFAULT_PROP_COLOR      0xffffffff
#define DEFAULT_PROP_OUTLINE_COLOR 0xff000000

/* make a property of me */
#define DEFAULT_SHADING_VALUE    -80

#define MINIMUM_OUTLINE_OFFSET 1.0
#define DEFAULT_SCALE_BASIS    640

#define COMP_Y(ret, r, g, b) \
{ \
   ret = (int) (((19595 * r) >> 16) + ((38470 * g) >> 16) + ((7471 * b) >> 16)); \
   ret = CLAMP (ret, 0, 255); \
}

#define COMP_U(ret, r, g, b) \
{ \
   ret = (int) (-((11059 * r) >> 16) - ((21709 * g) >> 16) + ((32768 * b) >> 16) + 128); \
   ret = CLAMP (ret, 0, 255); \
}

#define COMP_V(ret, r, g, b) \
{ \
   ret = (int) (((32768 * r) >> 16) - ((27439 * g) >> 16) - ((5329 * b) >> 16) + 128); \
   ret = CLAMP (ret, 0, 255); \
}

#define BLEND(ret, alpha, v0, v1) \
{ \
	ret = (v0 * alpha + v1 * (255 - alpha)) / 255; \
}

#define OVER(ret, alphaA, Ca, alphaB, Cb, alphaNew)	\
{ \
    gint _tmp; \
    _tmp = (Ca * alphaA + Cb * alphaB * (255 - alphaA) / 255) / alphaNew; \
    ret = CLAMP (_tmp, 0, 255); \
}

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
# define CAIRO_ARGB_A 3
# define CAIRO_ARGB_R 2
# define CAIRO_ARGB_G 1
# define CAIRO_ARGB_B 0
#else
# define CAIRO_ARGB_A 0
# define CAIRO_ARGB_R 1
# define CAIRO_ARGB_G 2
# define CAIRO_ARGB_B 3
#endif

enum
{
  PROP_0,
  PROP_TEXT,
  PROP_SHADING,
  PROP_HALIGNMENT,
  PROP_VALIGNMENT,
  PROP_XPAD,
  PROP_YPAD,
  PROP_DELTAX,
  PROP_DELTAY,
  PROP_XPOS,
  PROP_YPOS,
  PROP_WRAP_MODE,
  PROP_FONT_DESC,
  PROP_SILENT,
  PROP_LINE_ALIGNMENT,
  PROP_WAIT_TEXT,
  PROP_AUTO_ADJUST_SIZE,
  PROP_VERTICAL_RENDER,
  PROP_COLOR,
  PROP_SHADOW,
  PROP_OUTLINE_COLOR,
  PROP_LAST
};

/* FIXME: video-blend.c doesn't support formats with more than 8 bit per
 * component (which get unpacked into ARGB64 or AYUV64) yet, such as:
 *  v210, v216, UYVP, GRAY16_LE, GRAY16_BE */
#define VIDEO_FORMATS "{ BGRx, RGBx, xRGB, xBGR, RGBA, BGRA, ARGB, ABGR, RGB, BGR, \
    I420, YV12, AYUV, YUY2, UYVY, v308, Y41B, Y42B, Y444, \
    NV12, NV21, A420, YUV9, YVU9, IYU1, GRAY8 }"

static GstStaticPadTemplate src_template_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (VIDEO_FORMATS))
    );

static GstStaticPadTemplate video_sink_template_factory =
GST_STATIC_PAD_TEMPLATE ("video_sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (VIDEO_FORMATS))
    );

#define GST_TYPE_BASE_TEXT_OVERLAY_VALIGN (gst_base_text_overlay_valign_get_type())
static GType
gst_base_text_overlay_valign_get_type (void)
{
  static GType base_text_overlay_valign_type = 0;
  static const GEnumValue base_text_overlay_valign[] = {
    {GST_BASE_TEXT_OVERLAY_VALIGN_BASELINE, "baseline", "baseline"},
    {GST_BASE_TEXT_OVERLAY_VALIGN_BOTTOM, "bottom", "bottom"},
    {GST_BASE_TEXT_OVERLAY_VALIGN_TOP, "top", "top"},
    {GST_BASE_TEXT_OVERLAY_VALIGN_POS, "position", "position"},
    {GST_BASE_TEXT_OVERLAY_VALIGN_CENTER, "center", "center"},
    {0, NULL, NULL},
  };

  if (!base_text_overlay_valign_type) {
    base_text_overlay_valign_type =
        g_enum_register_static ("GstBaseTextOverlayVAlign",
        base_text_overlay_valign);
  }
  return base_text_overlay_valign_type;
}

#define GST_TYPE_BASE_TEXT_OVERLAY_HALIGN (gst_base_text_overlay_halign_get_type())
static GType
gst_base_text_overlay_halign_get_type (void)
{
  static GType base_text_overlay_halign_type = 0;
  static const GEnumValue base_text_overlay_halign[] = {
    {GST_BASE_TEXT_OVERLAY_HALIGN_LEFT, "left", "left"},
    {GST_BASE_TEXT_OVERLAY_HALIGN_CENTER, "center", "center"},
    {GST_BASE_TEXT_OVERLAY_HALIGN_RIGHT, "right", "right"},
    {GST_BASE_TEXT_OVERLAY_HALIGN_POS, "position", "position"},
    {0, NULL, NULL},
  };

  if (!base_text_overlay_halign_type) {
    base_text_overlay_halign_type =
        g_enum_register_static ("GstBaseTextOverlayHAlign",
        base_text_overlay_halign);
  }
  return base_text_overlay_halign_type;
}


#define GST_TYPE_BASE_TEXT_OVERLAY_WRAP_MODE (gst_base_text_overlay_wrap_mode_get_type())
static GType
gst_base_text_overlay_wrap_mode_get_type (void)
{
  static GType base_text_overlay_wrap_mode_type = 0;
  static const GEnumValue base_text_overlay_wrap_mode[] = {
    {GST_BASE_TEXT_OVERLAY_WRAP_MODE_NONE, "none", "none"},
    {GST_BASE_TEXT_OVERLAY_WRAP_MODE_WORD, "word", "word"},
    {GST_BASE_TEXT_OVERLAY_WRAP_MODE_CHAR, "char", "char"},
    {GST_BASE_TEXT_OVERLAY_WRAP_MODE_WORD_CHAR, "wordchar", "wordchar"},
    {0, NULL, NULL},
  };

  if (!base_text_overlay_wrap_mode_type) {
    base_text_overlay_wrap_mode_type =
        g_enum_register_static ("GstBaseTextOverlayWrapMode",
        base_text_overlay_wrap_mode);
  }
  return base_text_overlay_wrap_mode_type;
}

#define GST_TYPE_BASE_TEXT_OVERLAY_LINE_ALIGN (gst_base_text_overlay_line_align_get_type())
static GType
gst_base_text_overlay_line_align_get_type (void)
{
  static GType base_text_overlay_line_align_type = 0;
  static const GEnumValue base_text_overlay_line_align[] = {
    {GST_BASE_TEXT_OVERLAY_LINE_ALIGN_LEFT, "left", "left"},
    {GST_BASE_TEXT_OVERLAY_LINE_ALIGN_CENTER, "center", "center"},
    {GST_BASE_TEXT_OVERLAY_LINE_ALIGN_RIGHT, "right", "right"},
    {0, NULL, NULL}
  };

  if (!base_text_overlay_line_align_type) {
    base_text_overlay_line_align_type =
        g_enum_register_static ("GstBaseTextOverlayLineAlign",
        base_text_overlay_line_align);
  }
  return base_text_overlay_line_align_type;
}

#define GST_BASE_TEXT_OVERLAY_GET_LOCK(ov) (&GST_BASE_TEXT_OVERLAY (ov)->lock)
#define GST_BASE_TEXT_OVERLAY_GET_COND(ov) (&GST_BASE_TEXT_OVERLAY (ov)->cond)
#define GST_BASE_TEXT_OVERLAY_LOCK(ov)     (g_mutex_lock (GST_BASE_TEXT_OVERLAY_GET_LOCK (ov)))
#define GST_BASE_TEXT_OVERLAY_UNLOCK(ov)   (g_mutex_unlock (GST_BASE_TEXT_OVERLAY_GET_LOCK (ov)))
#define GST_BASE_TEXT_OVERLAY_WAIT(ov)     (g_cond_wait (GST_BASE_TEXT_OVERLAY_GET_COND (ov), GST_BASE_TEXT_OVERLAY_GET_LOCK (ov)))
#define GST_BASE_TEXT_OVERLAY_SIGNAL(ov)   (g_cond_signal (GST_BASE_TEXT_OVERLAY_GET_COND (ov)))
#define GST_BASE_TEXT_OVERLAY_BROADCAST(ov)(g_cond_broadcast (GST_BASE_TEXT_OVERLAY_GET_COND (ov)))

static GstElementClass *parent_class = NULL;
static void gst_base_text_overlay_base_init (gpointer g_class);
static void gst_base_text_overlay_class_init (GstBaseTextOverlayClass * klass);
static void gst_base_text_overlay_init (GstBaseTextOverlay * overlay,
    GstBaseTextOverlayClass * klass);

static GstStateChangeReturn gst_base_text_overlay_change_state (GstElement *
    element, GstStateChange transition);

static GstCaps *gst_base_text_overlay_getcaps (GstPad * pad,
    GstBaseTextOverlay * overlay, GstCaps * filter);
static gboolean gst_base_text_overlay_setcaps (GstBaseTextOverlay * overlay,
    GstCaps * caps);
static gboolean gst_base_text_overlay_setcaps_txt (GstBaseTextOverlay * overlay,
    GstCaps * caps);
static gboolean gst_base_text_overlay_src_event (GstPad * pad,
    GstObject * parent, GstEvent * event);
static gboolean gst_base_text_overlay_src_query (GstPad * pad,
    GstObject * parent, GstQuery * query);

static gboolean gst_base_text_overlay_video_event (GstPad * pad,
    GstObject * parent, GstEvent * event);
static gboolean gst_base_text_overlay_video_query (GstPad * pad,
    GstObject * parent, GstQuery * query);
static GstFlowReturn gst_base_text_overlay_video_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buffer);

static gboolean gst_base_text_overlay_text_event (GstPad * pad,
    GstObject * parent, GstEvent * event);
static GstFlowReturn gst_base_text_overlay_text_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buffer);
static GstPadLinkReturn gst_base_text_overlay_text_pad_link (GstPad * pad,
    GstObject * parent, GstPad * peer);
static void gst_base_text_overlay_text_pad_unlink (GstPad * pad,
    GstObject * parent);
static void gst_base_text_overlay_pop_text (GstBaseTextOverlay * overlay);
static void gst_base_text_overlay_update_render_mode (GstBaseTextOverlay *
    overlay);

static void gst_base_text_overlay_finalize (GObject * object);
static void gst_base_text_overlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_base_text_overlay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void
gst_base_text_overlay_adjust_values_with_fontdesc (GstBaseTextOverlay * overlay,
    PangoFontDescription * desc);

GType
gst_base_text_overlay_get_type (void)
{
  static GType type = 0;

  if (g_once_init_enter ((gsize *) & type)) {
    static const GTypeInfo info = {
      sizeof (GstBaseTextOverlayClass),
      (GBaseInitFunc) gst_base_text_overlay_base_init,
      NULL,
      (GClassInitFunc) gst_base_text_overlay_class_init,
      NULL,
      NULL,
      sizeof (GstBaseTextOverlay),
      0,
      (GInstanceInitFunc) gst_base_text_overlay_init,
    };

    g_once_init_leave ((gsize *) & type,
        g_type_register_static (GST_TYPE_ELEMENT, "GstBaseTextOverlay", &info,
            0));
  }

  return type;
}

static gchar *
gst_base_text_overlay_get_text (GstBaseTextOverlay * overlay,
    GstBuffer * video_frame)
{
  return g_strdup (overlay->default_text);
}

static void
gst_base_text_overlay_base_init (gpointer g_class)
{
  GstBaseTextOverlayClass *klass = GST_BASE_TEXT_OVERLAY_CLASS (g_class);
  PangoFontMap *fontmap;

  /* Only lock for the subclasses here, the base class
   * doesn't have this mutex yet and it's not necessary
   * here */
  if (klass->pango_lock)
    g_mutex_lock (klass->pango_lock);
  fontmap = pango_cairo_font_map_get_default ();
  klass->pango_context =
      pango_font_map_create_context (PANGO_FONT_MAP (fontmap));
  if (klass->pango_lock)
    g_mutex_unlock (klass->pango_lock);
}

static void
gst_base_text_overlay_class_init (GstBaseTextOverlayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = gst_base_text_overlay_finalize;
  gobject_class->set_property = gst_base_text_overlay_set_property;
  gobject_class->get_property = gst_base_text_overlay_get_property;

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_template_factory));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&video_sink_template_factory));

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_base_text_overlay_change_state);

  klass->pango_lock = g_slice_new (GMutex);
  g_mutex_init (klass->pango_lock);

  klass->get_text = gst_base_text_overlay_get_text;

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_TEXT,
      g_param_spec_string ("text", "text",
          "Text to be display.", DEFAULT_PROP_TEXT,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SHADING,
      g_param_spec_boolean ("shaded-background", "shaded background",
          "Whether to shade the background under the text area",
          DEFAULT_PROP_SHADING, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_VALIGNMENT,
      g_param_spec_enum ("valignment", "vertical alignment",
          "Vertical alignment of the text", GST_TYPE_BASE_TEXT_OVERLAY_VALIGN,
          DEFAULT_PROP_VALIGNMENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_HALIGNMENT,
      g_param_spec_enum ("halignment", "horizontal alignment",
          "Horizontal alignment of the text", GST_TYPE_BASE_TEXT_OVERLAY_HALIGN,
          DEFAULT_PROP_HALIGNMENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_XPAD,
      g_param_spec_int ("xpad", "horizontal paddding",
          "Horizontal paddding when using left/right alignment", 0, G_MAXINT,
          DEFAULT_PROP_XPAD, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_YPAD,
      g_param_spec_int ("ypad", "vertical padding",
          "Vertical padding when using top/bottom alignment", 0, G_MAXINT,
          DEFAULT_PROP_YPAD, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_DELTAX,
      g_param_spec_int ("deltax", "X position modifier",
          "Shift X position to the left or to the right. Unit is pixels.",
          G_MININT, G_MAXINT, DEFAULT_PROP_DELTAX,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_DELTAY,
      g_param_spec_int ("deltay", "Y position modifier",
          "Shift Y position up or down. Unit is pixels.", G_MININT, G_MAXINT,
          DEFAULT_PROP_DELTAY, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstBaseTextOverlay:xpos
   *
   * Horizontal position of the rendered text when using positioned alignment.
   *
   * Since: 0.10.31
   **/
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_XPOS,
      g_param_spec_double ("xpos", "horizontal position",
          "Horizontal position when using position alignment", 0, 1.0,
          DEFAULT_PROP_XPOS,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  /**
   * GstBaseTextOverlay:ypos
   *
   * Vertical position of the rendered text when using positioned alignment.
   *
   * Since: 0.10.31
   **/
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_YPOS,
      g_param_spec_double ("ypos", "vertical position",
          "Vertical position when using position alignment", 0, 1.0,
          DEFAULT_PROP_YPOS,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_WRAP_MODE,
      g_param_spec_enum ("wrap-mode", "wrap mode",
          "Whether to wrap the text and if so how.",
          GST_TYPE_BASE_TEXT_OVERLAY_WRAP_MODE, DEFAULT_PROP_WRAP_MODE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_FONT_DESC,
      g_param_spec_string ("font-desc", "font description",
          "Pango font description of font to be used for rendering. "
          "See documentation of pango_font_description_from_string "
          "for syntax.", DEFAULT_PROP_FONT_DESC,
          G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));
  /**
   * GstBaseTextOverlay:color
   *
   * Color of the rendered text.
   *
   * Since: 0.10.31
   **/
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_COLOR,
      g_param_spec_uint ("color", "Color",
          "Color to use for text (big-endian ARGB).", 0, G_MAXUINT32,
          DEFAULT_PROP_COLOR,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  /**
   * GstTextOverlay:outline-color
   *
   * Color of the outline of the rendered text.
   *
   * Since: 0.10.35
   **/
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_OUTLINE_COLOR,
      g_param_spec_uint ("outline-color", "Text Outline Color",
          "Color to use for outline the text (big-endian ARGB).", 0,
          G_MAXUINT32, DEFAULT_PROP_OUTLINE_COLOR,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));

  /**
   * GstBaseTextOverlay:line-alignment
   *
   * Alignment of text lines relative to each other (for multi-line text)
   *
   * Since: 0.10.15
   **/
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_LINE_ALIGNMENT,
      g_param_spec_enum ("line-alignment", "line alignment",
          "Alignment of text lines relative to each other.",
          GST_TYPE_BASE_TEXT_OVERLAY_LINE_ALIGN, DEFAULT_PROP_LINE_ALIGNMENT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstBaseTextOverlay:silent
   *
   * If set, no text is rendered. Useful to switch off text rendering
   * temporarily without removing the textoverlay element from the pipeline.
   *
   * Since: 0.10.15
   **/
  /* FIXME 0.11: rename to "visible" or "text-visible" or "render-text" */
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SILENT,
      g_param_spec_boolean ("silent", "silent",
          "Whether to render the text string",
          DEFAULT_PROP_SILENT,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  /**
   * GstBaseTextOverlay:wait-text
   *
   * If set, the video will block until a subtitle is received on the text pad.
   * If video and subtitles are sent in sync, like from the same demuxer, this
   * property should be set.
   *
   * Since: 0.10.20
   **/
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_WAIT_TEXT,
      g_param_spec_boolean ("wait-text", "Wait Text",
          "Whether to wait for subtitles",
          DEFAULT_PROP_WAIT_TEXT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_AUTO_ADJUST_SIZE, g_param_spec_boolean ("auto-resize", "auto resize",
          "Automatically adjust font size to screen-size.",
          DEFAULT_PROP_AUTO_ADJUST_SIZE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_VERTICAL_RENDER,
      g_param_spec_boolean ("vertical-render", "vertical render",
          "Vertical Render.", DEFAULT_PROP_VERTICAL_RENDER,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_base_text_overlay_finalize (GObject * object)
{
  GstBaseTextOverlay *overlay = GST_BASE_TEXT_OVERLAY (object);

  g_free (overlay->default_text);

  if (overlay->composition) {
    gst_video_overlay_composition_unref (overlay->composition);
    overlay->composition = NULL;
  }

  if (overlay->text_image) {
    gst_buffer_unref (overlay->text_image);
    overlay->text_image = NULL;
  }

  if (overlay->layout) {
    g_object_unref (overlay->layout);
    overlay->layout = NULL;
  }

  if (overlay->text_buffer) {
    gst_buffer_unref (overlay->text_buffer);
    overlay->text_buffer = NULL;
  }

  g_mutex_clear (&overlay->lock);
  g_cond_clear (&overlay->cond);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_base_text_overlay_init (GstBaseTextOverlay * overlay,
    GstBaseTextOverlayClass * klass)
{
  GstPadTemplate *template;
  PangoFontDescription *desc;

  /* video sink */
  template = gst_static_pad_template_get (&video_sink_template_factory);
  overlay->video_sinkpad = gst_pad_new_from_template (template, "video_sink");
  gst_object_unref (template);
  gst_pad_set_event_function (overlay->video_sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_text_overlay_video_event));
  gst_pad_set_chain_function (overlay->video_sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_text_overlay_video_chain));
  gst_pad_set_query_function (overlay->video_sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_text_overlay_video_query));
  GST_PAD_SET_PROXY_ALLOCATION (overlay->video_sinkpad);
  gst_element_add_pad (GST_ELEMENT (overlay), overlay->video_sinkpad);

  template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (klass),
      "text_sink");
  if (template) {
    /* text sink */
    overlay->text_sinkpad = gst_pad_new_from_template (template, "text_sink");

    gst_pad_set_event_function (overlay->text_sinkpad,
        GST_DEBUG_FUNCPTR (gst_base_text_overlay_text_event));
    gst_pad_set_chain_function (overlay->text_sinkpad,
        GST_DEBUG_FUNCPTR (gst_base_text_overlay_text_chain));
    gst_pad_set_link_function (overlay->text_sinkpad,
        GST_DEBUG_FUNCPTR (gst_base_text_overlay_text_pad_link));
    gst_pad_set_unlink_function (overlay->text_sinkpad,
        GST_DEBUG_FUNCPTR (gst_base_text_overlay_text_pad_unlink));
    gst_element_add_pad (GST_ELEMENT (overlay), overlay->text_sinkpad);
  }

  /* (video) source */
  template = gst_static_pad_template_get (&src_template_factory);
  overlay->srcpad = gst_pad_new_from_template (template, "src");
  gst_object_unref (template);
  gst_pad_set_event_function (overlay->srcpad,
      GST_DEBUG_FUNCPTR (gst_base_text_overlay_src_event));
  gst_pad_set_query_function (overlay->srcpad,
      GST_DEBUG_FUNCPTR (gst_base_text_overlay_src_query));
  gst_element_add_pad (GST_ELEMENT (overlay), overlay->srcpad);

  g_mutex_lock (GST_BASE_TEXT_OVERLAY_GET_CLASS (overlay)->pango_lock);
  overlay->line_align = DEFAULT_PROP_LINE_ALIGNMENT;
  overlay->layout =
      pango_layout_new (GST_BASE_TEXT_OVERLAY_GET_CLASS
      (overlay)->pango_context);
  desc =
      pango_context_get_font_description (GST_BASE_TEXT_OVERLAY_GET_CLASS
      (overlay)->pango_context);
  gst_base_text_overlay_adjust_values_with_fontdesc (overlay, desc);

  overlay->color = DEFAULT_PROP_COLOR;
  overlay->outline_color = DEFAULT_PROP_OUTLINE_COLOR;
  overlay->halign = DEFAULT_PROP_HALIGNMENT;
  overlay->valign = DEFAULT_PROP_VALIGNMENT;
  overlay->xpad = DEFAULT_PROP_XPAD;
  overlay->ypad = DEFAULT_PROP_YPAD;
  overlay->deltax = DEFAULT_PROP_DELTAX;
  overlay->deltay = DEFAULT_PROP_DELTAY;
  overlay->xpos = DEFAULT_PROP_XPOS;
  overlay->ypos = DEFAULT_PROP_YPOS;

  overlay->wrap_mode = DEFAULT_PROP_WRAP_MODE;

  overlay->want_shading = DEFAULT_PROP_SHADING;
  overlay->shading_value = DEFAULT_SHADING_VALUE;
  overlay->silent = DEFAULT_PROP_SILENT;
  overlay->wait_text = DEFAULT_PROP_WAIT_TEXT;
  overlay->auto_adjust_size = DEFAULT_PROP_AUTO_ADJUST_SIZE;

  overlay->default_text = g_strdup (DEFAULT_PROP_TEXT);
  overlay->need_render = TRUE;
  overlay->text_image = NULL;
  overlay->use_vertical_render = DEFAULT_PROP_VERTICAL_RENDER;
  gst_base_text_overlay_update_render_mode (overlay);

  overlay->text_buffer = NULL;
  overlay->text_linked = FALSE;
  g_mutex_init (&overlay->lock);
  g_cond_init (&overlay->cond);
  gst_segment_init (&overlay->segment, GST_FORMAT_TIME);
  g_mutex_unlock (GST_BASE_TEXT_OVERLAY_GET_CLASS (overlay)->pango_lock);
}

static void
gst_base_text_overlay_update_wrap_mode (GstBaseTextOverlay * overlay)
{
  if (overlay->wrap_mode == GST_BASE_TEXT_OVERLAY_WRAP_MODE_NONE) {
    GST_DEBUG_OBJECT (overlay, "Set wrap mode NONE");
    pango_layout_set_width (overlay->layout, -1);
  } else {
    int width;

    if (overlay->auto_adjust_size) {
      width = DEFAULT_SCALE_BASIS * PANGO_SCALE;
      if (overlay->use_vertical_render) {
        width = width * (overlay->height - overlay->ypad * 2) / overlay->width;
      }
    } else {
      width =
          (overlay->use_vertical_render ? overlay->height : overlay->width) *
          PANGO_SCALE;
    }

    GST_DEBUG_OBJECT (overlay, "Set layout width %d", overlay->width);
    GST_DEBUG_OBJECT (overlay, "Set wrap mode    %d", overlay->wrap_mode);
    pango_layout_set_width (overlay->layout, width);
    pango_layout_set_wrap (overlay->layout, (PangoWrapMode) overlay->wrap_mode);
  }
}

static void
gst_base_text_overlay_update_render_mode (GstBaseTextOverlay * overlay)
{
  PangoMatrix matrix = PANGO_MATRIX_INIT;
  PangoContext *context = pango_layout_get_context (overlay->layout);

  if (overlay->use_vertical_render) {
    pango_matrix_rotate (&matrix, -90);
    pango_context_set_base_gravity (context, PANGO_GRAVITY_AUTO);
    pango_context_set_matrix (context, &matrix);
    pango_layout_set_alignment (overlay->layout, PANGO_ALIGN_LEFT);
  } else {
    pango_context_set_base_gravity (context, PANGO_GRAVITY_SOUTH);
    pango_context_set_matrix (context, &matrix);
    pango_layout_set_alignment (overlay->layout,
        (PangoAlignment) overlay->line_align);
  }
}

static gboolean
gst_base_text_overlay_setcaps_txt (GstBaseTextOverlay * overlay, GstCaps * caps)
{
  GstStructure *structure;
  const gchar *format;

  structure = gst_caps_get_structure (caps, 0);
  format = gst_structure_get_string (structure, "format");
  overlay->have_pango_markup = (strcmp (format, "pango-markup") == 0);

  return TRUE;
}

/* FIXME: upstream nego (e.g. when the video window is resized) */

/* only negotiate/query video overlay composition support for now */
static gboolean
gst_base_text_overlay_negotiate (GstBaseTextOverlay * overlay)
{
  GstCaps *target;
  GstQuery *query;
  gboolean attach = FALSE;

  GST_DEBUG_OBJECT (overlay, "performing negotiation");

  target = gst_pad_get_current_caps (overlay->srcpad);

  if (!target || gst_caps_is_empty (target))
    goto no_format;

  /* find supported meta */
  query = gst_query_new_allocation (target, TRUE);

  if (!gst_pad_peer_query (overlay->srcpad, query)) {
    /* no problem, we use the query defaults */
    GST_DEBUG_OBJECT (overlay, "ALLOCATION query failed");
  }

  if (gst_query_find_allocation_meta (query,
          GST_VIDEO_OVERLAY_COMPOSITION_META_API_TYPE, NULL))
    attach = TRUE;

  overlay->attach_compo_to_buffer = attach;

  gst_query_unref (query);
  gst_caps_unref (target);

  return TRUE;

no_format:
  {
    if (target)
      gst_caps_unref (target);
    return FALSE;
  }
}

static gboolean
gst_base_text_overlay_setcaps (GstBaseTextOverlay * overlay, GstCaps * caps)
{
  GstVideoInfo info;
  gboolean ret = FALSE;

  if (!gst_video_info_from_caps (&info, caps))
    goto invalid_caps;

  overlay->info = info;
  overlay->format = GST_VIDEO_INFO_FORMAT (&info);
  overlay->width = GST_VIDEO_INFO_WIDTH (&info);
  overlay->height = GST_VIDEO_INFO_HEIGHT (&info);

  ret = gst_pad_set_caps (overlay->srcpad, caps);

  if (ret) {
    GST_BASE_TEXT_OVERLAY_LOCK (overlay);
    g_mutex_lock (GST_BASE_TEXT_OVERLAY_GET_CLASS (overlay)->pango_lock);
    gst_base_text_overlay_negotiate (overlay);
    gst_base_text_overlay_update_wrap_mode (overlay);
    g_mutex_unlock (GST_BASE_TEXT_OVERLAY_GET_CLASS (overlay)->pango_lock);
    GST_BASE_TEXT_OVERLAY_UNLOCK (overlay);
  }

  return ret;

  /* ERRORS */
invalid_caps:
  {
    GST_DEBUG_OBJECT (overlay, "could not parse caps");
    return FALSE;
  }
}

static void
gst_base_text_overlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstBaseTextOverlay *overlay = GST_BASE_TEXT_OVERLAY (object);

  GST_BASE_TEXT_OVERLAY_LOCK (overlay);
  switch (prop_id) {
    case PROP_TEXT:
      g_free (overlay->default_text);
      overlay->default_text = g_value_dup_string (value);
      overlay->need_render = TRUE;
      break;
    case PROP_SHADING:
      overlay->want_shading = g_value_get_boolean (value);
      break;
    case PROP_XPAD:
      overlay->xpad = g_value_get_int (value);
      break;
    case PROP_YPAD:
      overlay->ypad = g_value_get_int (value);
      break;
    case PROP_DELTAX:
      overlay->deltax = g_value_get_int (value);
      break;
    case PROP_DELTAY:
      overlay->deltay = g_value_get_int (value);
      break;
    case PROP_XPOS:
      overlay->xpos = g_value_get_double (value);
      break;
    case PROP_YPOS:
      overlay->ypos = g_value_get_double (value);
      break;
    case PROP_VALIGNMENT:
      overlay->valign = g_value_get_enum (value);
      break;
    case PROP_HALIGNMENT:
      overlay->halign = g_value_get_enum (value);
      break;
    case PROP_WRAP_MODE:
      overlay->wrap_mode = g_value_get_enum (value);
      g_mutex_lock (GST_BASE_TEXT_OVERLAY_GET_CLASS (overlay)->pango_lock);
      gst_base_text_overlay_update_wrap_mode (overlay);
      g_mutex_unlock (GST_BASE_TEXT_OVERLAY_GET_CLASS (overlay)->pango_lock);
      break;
    case PROP_FONT_DESC:
    {
      PangoFontDescription *desc;
      const gchar *fontdesc_str;

      fontdesc_str = g_value_get_string (value);
      g_mutex_lock (GST_BASE_TEXT_OVERLAY_GET_CLASS (overlay)->pango_lock);
      desc = pango_font_description_from_string (fontdesc_str);
      if (desc) {
        GST_LOG_OBJECT (overlay, "font description set: %s", fontdesc_str);
        pango_layout_set_font_description (overlay->layout, desc);
        gst_base_text_overlay_adjust_values_with_fontdesc (overlay, desc);
        pango_font_description_free (desc);
      } else {
        GST_WARNING_OBJECT (overlay, "font description parse failed: %s",
            fontdesc_str);
      }
      g_mutex_unlock (GST_BASE_TEXT_OVERLAY_GET_CLASS (overlay)->pango_lock);
      break;
    }
    case PROP_COLOR:
      overlay->color = g_value_get_uint (value);
      break;
    case PROP_OUTLINE_COLOR:
      overlay->outline_color = g_value_get_uint (value);
      break;
    case PROP_SILENT:
      overlay->silent = g_value_get_boolean (value);
      break;
    case PROP_LINE_ALIGNMENT:
      overlay->line_align = g_value_get_enum (value);
      g_mutex_lock (GST_BASE_TEXT_OVERLAY_GET_CLASS (overlay)->pango_lock);
      pango_layout_set_alignment (overlay->layout,
          (PangoAlignment) overlay->line_align);
      g_mutex_unlock (GST_BASE_TEXT_OVERLAY_GET_CLASS (overlay)->pango_lock);
      break;
    case PROP_WAIT_TEXT:
      overlay->wait_text = g_value_get_boolean (value);
      break;
    case PROP_AUTO_ADJUST_SIZE:
      overlay->auto_adjust_size = g_value_get_boolean (value);
      overlay->need_render = TRUE;
      break;
    case PROP_VERTICAL_RENDER:
      overlay->use_vertical_render = g_value_get_boolean (value);
      g_mutex_lock (GST_BASE_TEXT_OVERLAY_GET_CLASS (overlay)->pango_lock);
      gst_base_text_overlay_update_render_mode (overlay);
      g_mutex_unlock (GST_BASE_TEXT_OVERLAY_GET_CLASS (overlay)->pango_lock);
      overlay->need_render = TRUE;
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  overlay->need_render = TRUE;
  GST_BASE_TEXT_OVERLAY_UNLOCK (overlay);
}

static void
gst_base_text_overlay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstBaseTextOverlay *overlay = GST_BASE_TEXT_OVERLAY (object);

  GST_BASE_TEXT_OVERLAY_LOCK (overlay);
  switch (prop_id) {
    case PROP_TEXT:
      g_value_set_string (value, overlay->default_text);
      break;
    case PROP_SHADING:
      g_value_set_boolean (value, overlay->want_shading);
      break;
    case PROP_XPAD:
      g_value_set_int (value, overlay->xpad);
      break;
    case PROP_YPAD:
      g_value_set_int (value, overlay->ypad);
      break;
    case PROP_DELTAX:
      g_value_set_int (value, overlay->deltax);
      break;
    case PROP_DELTAY:
      g_value_set_int (value, overlay->deltay);
      break;
    case PROP_XPOS:
      g_value_set_double (value, overlay->xpos);
      break;
    case PROP_YPOS:
      g_value_set_double (value, overlay->ypos);
      break;
    case PROP_VALIGNMENT:
      g_value_set_enum (value, overlay->valign);
      break;
    case PROP_HALIGNMENT:
      g_value_set_enum (value, overlay->halign);
      break;
    case PROP_WRAP_MODE:
      g_value_set_enum (value, overlay->wrap_mode);
      break;
    case PROP_SILENT:
      g_value_set_boolean (value, overlay->silent);
      break;
    case PROP_LINE_ALIGNMENT:
      g_value_set_enum (value, overlay->line_align);
      break;
    case PROP_WAIT_TEXT:
      g_value_set_boolean (value, overlay->wait_text);
      break;
    case PROP_AUTO_ADJUST_SIZE:
      g_value_set_boolean (value, overlay->auto_adjust_size);
      break;
    case PROP_VERTICAL_RENDER:
      g_value_set_boolean (value, overlay->use_vertical_render);
      break;
    case PROP_COLOR:
      g_value_set_uint (value, overlay->color);
      break;
    case PROP_OUTLINE_COLOR:
      g_value_set_uint (value, overlay->outline_color);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  overlay->need_render = TRUE;
  GST_BASE_TEXT_OVERLAY_UNLOCK (overlay);
}

static gboolean
gst_base_text_overlay_src_query (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  gboolean ret = FALSE;
  GstBaseTextOverlay *overlay;

  overlay = GST_BASE_TEXT_OVERLAY (parent);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *filter, *caps;

      gst_query_parse_caps (query, &filter);
      caps = gst_base_text_overlay_getcaps (pad, overlay, filter);
      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);
      ret = TRUE;
      break;
    }
    default:
      ret = gst_pad_peer_query (overlay->video_sinkpad, query);
      break;
  }

  return ret;
}

static gboolean
gst_base_text_overlay_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  gboolean ret = FALSE;
  GstBaseTextOverlay *overlay = NULL;

  overlay = GST_BASE_TEXT_OVERLAY (parent);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:{
      GstSeekFlags flags;

      /* We don't handle seek if we have not text pad */
      if (!overlay->text_linked) {
        GST_DEBUG_OBJECT (overlay, "seek received, pushing upstream");
        ret = gst_pad_push_event (overlay->video_sinkpad, event);
        goto beach;
      }

      GST_DEBUG_OBJECT (overlay, "seek received, driving from here");

      gst_event_parse_seek (event, NULL, NULL, &flags, NULL, NULL, NULL, NULL);

      /* Flush downstream, only for flushing seek */
      if (flags & GST_SEEK_FLAG_FLUSH)
        gst_pad_push_event (overlay->srcpad, gst_event_new_flush_start ());

      /* Mark ourself as flushing, unblock chains */
      GST_BASE_TEXT_OVERLAY_LOCK (overlay);
      overlay->video_flushing = TRUE;
      overlay->text_flushing = TRUE;
      gst_base_text_overlay_pop_text (overlay);
      GST_BASE_TEXT_OVERLAY_UNLOCK (overlay);

      /* Seek on each sink pad */
      gst_event_ref (event);
      ret = gst_pad_push_event (overlay->video_sinkpad, event);
      if (ret) {
        ret = gst_pad_push_event (overlay->text_sinkpad, event);
      } else {
        gst_event_unref (event);
      }
      break;
    }
    default:
      if (overlay->text_linked) {
        gst_event_ref (event);
        ret = gst_pad_push_event (overlay->video_sinkpad, event);
        gst_pad_push_event (overlay->text_sinkpad, event);
      } else {
        ret = gst_pad_push_event (overlay->video_sinkpad, event);
      }
      break;
  }

beach:

  return ret;
}

static GstCaps *
gst_base_text_overlay_getcaps (GstPad * pad, GstBaseTextOverlay * overlay,
    GstCaps * filter)
{
  GstPad *otherpad;
  GstCaps *caps;

  if (G_UNLIKELY (!overlay))
    return gst_pad_get_pad_template_caps (pad);

  if (pad == overlay->srcpad)
    otherpad = overlay->video_sinkpad;
  else
    otherpad = overlay->srcpad;

  /* we can do what the peer can */
  caps = gst_pad_peer_query_caps (otherpad, filter);
  if (caps) {
    GstCaps *temp, *templ;

    GST_DEBUG_OBJECT (pad, "peer caps  %" GST_PTR_FORMAT, caps);

    /* filtered against our padtemplate */
    templ = gst_pad_get_pad_template_caps (otherpad);
    GST_DEBUG_OBJECT (pad, "our template  %" GST_PTR_FORMAT, templ);
    temp = gst_caps_intersect_full (caps, templ, GST_CAPS_INTERSECT_FIRST);
    GST_DEBUG_OBJECT (pad, "intersected %" GST_PTR_FORMAT, temp);
    gst_caps_unref (caps);
    gst_caps_unref (templ);
    /* this is what we can do */
    caps = temp;
  } else {
    /* no peer, our padtemplate is enough then */
    caps = gst_pad_get_pad_template_caps (pad);
    if (filter) {
      GstCaps *intersection;

      intersection =
          gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);
      gst_caps_unref (caps);
      caps = intersection;
    }
  }

  GST_DEBUG_OBJECT (overlay, "returning  %" GST_PTR_FORMAT, caps);

  return caps;
}

static void
gst_base_text_overlay_adjust_values_with_fontdesc (GstBaseTextOverlay * overlay,
    PangoFontDescription * desc)
{
  gint font_size = pango_font_description_get_size (desc) / PANGO_SCALE;
  overlay->shadow_offset = (double) (font_size) / 13.0;
  overlay->outline_offset = (double) (font_size) / 15.0;
  if (overlay->outline_offset < MINIMUM_OUTLINE_OFFSET)
    overlay->outline_offset = MINIMUM_OUTLINE_OFFSET;
}

static void
gst_base_text_overlay_get_pos (GstBaseTextOverlay * overlay,
    gint * xpos, gint * ypos)
{
  gint width, height;
  GstBaseTextOverlayVAlign valign;
  GstBaseTextOverlayHAlign halign;

  width = overlay->image_width;
  height = overlay->image_height;

  if (overlay->use_vertical_render)
    halign = GST_BASE_TEXT_OVERLAY_HALIGN_RIGHT;
  else
    halign = overlay->halign;

  switch (halign) {
    case GST_BASE_TEXT_OVERLAY_HALIGN_LEFT:
      *xpos = overlay->xpad;
      break;
    case GST_BASE_TEXT_OVERLAY_HALIGN_CENTER:
      *xpos = (overlay->width - width) / 2;
      break;
    case GST_BASE_TEXT_OVERLAY_HALIGN_RIGHT:
      *xpos = overlay->width - width - overlay->xpad;
      break;
    case GST_BASE_TEXT_OVERLAY_HALIGN_POS:
      *xpos = (gint) (overlay->width * overlay->xpos) - width / 2;
      *xpos = CLAMP (*xpos, 0, overlay->width - width);
      if (*xpos < 0)
        *xpos = 0;
      break;
    default:
      *xpos = 0;
  }
  *xpos += overlay->deltax;

  if (overlay->use_vertical_render)
    valign = GST_BASE_TEXT_OVERLAY_VALIGN_TOP;
  else
    valign = overlay->valign;

  switch (valign) {
    case GST_BASE_TEXT_OVERLAY_VALIGN_BOTTOM:
      *ypos = overlay->height - height - overlay->ypad;
      break;
    case GST_BASE_TEXT_OVERLAY_VALIGN_BASELINE:
      *ypos = overlay->height - (height + overlay->ypad);
      break;
    case GST_BASE_TEXT_OVERLAY_VALIGN_TOP:
      *ypos = overlay->ypad;
      break;
    case GST_BASE_TEXT_OVERLAY_VALIGN_POS:
      *ypos = (gint) (overlay->height * overlay->ypos) - height / 2;
      *ypos = CLAMP (*ypos, 0, overlay->height - height);
      break;
    case GST_BASE_TEXT_OVERLAY_VALIGN_CENTER:
      *ypos = (overlay->height - height) / 2;
      break;
    default:
      *ypos = overlay->ypad;
      break;
  }
  *ypos += overlay->deltay;
}

static inline void
gst_base_text_overlay_set_composition (GstBaseTextOverlay * overlay)
{
  gint xpos, ypos;
  GstVideoOverlayRectangle *rectangle;

  gst_base_text_overlay_get_pos (overlay, &xpos, &ypos);

  if (overlay->text_image) {
    gst_buffer_add_video_meta (overlay->text_image, GST_VIDEO_FRAME_FLAG_NONE,
        GST_VIDEO_OVERLAY_COMPOSITION_FORMAT_RGB,
        overlay->image_width, overlay->image_height);
    rectangle = gst_video_overlay_rectangle_new_raw (overlay->text_image,
        xpos, ypos, overlay->image_width, overlay->image_height,
        GST_VIDEO_OVERLAY_FORMAT_FLAG_PREMULTIPLIED_ALPHA);

    if (overlay->composition)
      gst_video_overlay_composition_unref (overlay->composition);
    overlay->composition = gst_video_overlay_composition_new (rectangle);
    gst_video_overlay_rectangle_unref (rectangle);

  } else if (overlay->composition) {
    gst_video_overlay_composition_unref (overlay->composition);
    overlay->composition = NULL;
  }
}

static gboolean
gst_text_overlay_filter_foreground_attr (PangoAttribute * attr, gpointer data)
{
  if (attr->klass->type == PANGO_ATTR_FOREGROUND) {
    return FALSE;
  } else {
    return TRUE;
  }
}

static void
gst_base_text_overlay_render_pangocairo (GstBaseTextOverlay * overlay,
    const gchar * string, gint textlen)
{
  cairo_t *cr;
  cairo_surface_t *surface;
  PangoRectangle ink_rect, logical_rect;
  cairo_matrix_t cairo_matrix;
  int width, height;
  double scalef = 1.0;
  double a, r, g, b;
  GstBuffer *buffer;
  GstMapInfo map;

  g_mutex_lock (GST_BASE_TEXT_OVERLAY_GET_CLASS (overlay)->pango_lock);

  if (overlay->auto_adjust_size) {
    /* 640 pixel is default */
    scalef = (double) (overlay->width) / DEFAULT_SCALE_BASIS;
  }
  pango_layout_set_width (overlay->layout, -1);
  /* set text on pango layout */
  pango_layout_set_markup (overlay->layout, string, textlen);

  /* get subtitle image size */
  pango_layout_get_pixel_extents (overlay->layout, &ink_rect, &logical_rect);

  width = (logical_rect.width + overlay->shadow_offset) * scalef;

  if (width + overlay->deltax >
      (overlay->use_vertical_render ? overlay->height : overlay->width)) {
    /*
     * subtitle image width is larger then overlay width
     * so rearrange overlay wrap mode.
     */
    gst_base_text_overlay_update_wrap_mode (overlay);
    pango_layout_get_pixel_extents (overlay->layout, &ink_rect, &logical_rect);
    width = overlay->width;
  }

  height =
      (logical_rect.height + logical_rect.y + overlay->shadow_offset) * scalef;
  if (height > overlay->height) {
    height = overlay->height;
  }
  if (overlay->use_vertical_render) {
    PangoRectangle rect;
    PangoContext *context;
    PangoMatrix matrix = PANGO_MATRIX_INIT;
    int tmp;

    context = pango_layout_get_context (overlay->layout);

    pango_matrix_rotate (&matrix, -90);

    rect.x = rect.y = 0;
    rect.width = width;
    rect.height = height;
    pango_matrix_transform_pixel_rectangle (&matrix, &rect);
    matrix.x0 = -rect.x;
    matrix.y0 = -rect.y;

    pango_context_set_matrix (context, &matrix);

    cairo_matrix.xx = matrix.xx;
    cairo_matrix.yx = matrix.yx;
    cairo_matrix.xy = matrix.xy;
    cairo_matrix.yy = matrix.yy;
    cairo_matrix.x0 = matrix.x0;
    cairo_matrix.y0 = matrix.y0;
    cairo_matrix_scale (&cairo_matrix, scalef, scalef);

    tmp = height;
    height = width;
    width = tmp;
  } else {
    cairo_matrix_init_scale (&cairo_matrix, scalef, scalef);
  }

  /* reallocate overlay buffer */
  buffer = gst_buffer_new_and_alloc (4 * width * height);
  gst_buffer_replace (&overlay->text_image, buffer);
  gst_buffer_unref (buffer);

  gst_buffer_map (buffer, &map, GST_MAP_READWRITE);
  surface = cairo_image_surface_create_for_data (map.data,
      CAIRO_FORMAT_ARGB32, width, height, width * 4);
  cr = cairo_create (surface);

  /* clear surface */
  cairo_set_operator (cr, CAIRO_OPERATOR_CLEAR);
  cairo_paint (cr);

  cairo_set_operator (cr, CAIRO_OPERATOR_OVER);

  if (overlay->want_shading)
    cairo_paint_with_alpha (cr, overlay->shading_value);

  /* apply transformations */
  cairo_set_matrix (cr, &cairo_matrix);

  /* FIXME: We use show_layout everywhere except for the surface
   * because it's really faster and internally does all kinds of
   * caching. Unfortunately we have to paint to a cairo path for
   * the outline and this is slow. Once Pango supports user fonts
   * we should use them, see
   * https://bugzilla.gnome.org/show_bug.cgi?id=598695
   *
   * Idea would the be, to create a cairo user font that
   * does shadow, outline, text painting in the
   * render_glyph function.
   */

  /* draw shadow text */
  {
    PangoAttrList *origin_attr, *filtered_attr, *temp_attr;

    /* Store a ref on the original attributes for later restoration */
    origin_attr =
        pango_attr_list_ref (pango_layout_get_attributes (overlay->layout));
    /* Take a copy of the original attributes, because pango_attr_list_filter
     * modifies the passed list */
    temp_attr = pango_attr_list_copy (origin_attr);
    filtered_attr =
        pango_attr_list_filter (temp_attr,
        gst_text_overlay_filter_foreground_attr, NULL);
    pango_attr_list_unref (temp_attr);

    cairo_save (cr);
    cairo_translate (cr, overlay->shadow_offset, overlay->shadow_offset);
    cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.5);
    pango_layout_set_attributes (overlay->layout, filtered_attr);
    pango_cairo_show_layout (cr, overlay->layout);
    pango_layout_set_attributes (overlay->layout, origin_attr);
    pango_attr_list_unref (filtered_attr);
    pango_attr_list_unref (origin_attr);
    cairo_restore (cr);
  }

  a = (overlay->outline_color >> 24) & 0xff;
  r = (overlay->outline_color >> 16) & 0xff;
  g = (overlay->outline_color >> 8) & 0xff;
  b = (overlay->outline_color >> 0) & 0xff;

  /* draw outline text */
  cairo_save (cr);
  cairo_set_source_rgba (cr, r / 255.0, g / 255.0, b / 255.0, a / 255.0);
  cairo_set_line_width (cr, overlay->outline_offset);
  pango_cairo_layout_path (cr, overlay->layout);
  cairo_stroke (cr);
  cairo_restore (cr);

  a = (overlay->color >> 24) & 0xff;
  r = (overlay->color >> 16) & 0xff;
  g = (overlay->color >> 8) & 0xff;
  b = (overlay->color >> 0) & 0xff;

  /* draw text */
  cairo_save (cr);
  cairo_set_source_rgba (cr, r / 255.0, g / 255.0, b / 255.0, a / 255.0);
  pango_cairo_show_layout (cr, overlay->layout);
  cairo_restore (cr);

  cairo_destroy (cr);
  cairo_surface_destroy (surface);
  gst_buffer_unmap (buffer, &map);
  overlay->image_width = width;
  overlay->image_height = height;
  overlay->baseline_y = ink_rect.y;
  g_mutex_unlock (GST_BASE_TEXT_OVERLAY_GET_CLASS (overlay)->pango_lock);

  gst_base_text_overlay_set_composition (overlay);
}

static inline void
gst_base_text_overlay_shade_planar_Y (GstBaseTextOverlay * overlay,
    GstVideoFrame * dest, gint x0, gint x1, gint y0, gint y1)
{
  gint i, j, dest_stride;
  guint8 *dest_ptr;

  dest_stride = dest->info.stride[0];
  dest_ptr = dest->data[0];

  for (i = y0; i < y1; ++i) {
    for (j = x0; j < x1; ++j) {
      gint y = dest_ptr[(i * dest_stride) + j] + overlay->shading_value;

      dest_ptr[(i * dest_stride) + j] = CLAMP (y, 0, 255);
    }
  }
}

static inline void
gst_base_text_overlay_shade_packed_Y (GstBaseTextOverlay * overlay,
    GstVideoFrame * dest, gint x0, gint x1, gint y0, gint y1)
{
  gint i, j;
  guint dest_stride, pixel_stride;
  guint8 *dest_ptr;

  dest_stride = GST_VIDEO_FRAME_COMP_STRIDE (dest, 0);
  dest_ptr = GST_VIDEO_FRAME_COMP_DATA (dest, 0);
  pixel_stride = GST_VIDEO_FRAME_COMP_PSTRIDE (dest, 0);

  if (x0 != 0)
    x0 = GST_VIDEO_FORMAT_INFO_SCALE_WIDTH (dest->info.finfo, 0, x0);
  if (x1 != 0)
    x1 = GST_VIDEO_FORMAT_INFO_SCALE_WIDTH (dest->info.finfo, 0, x1);

  if (y0 != 0)
    y0 = GST_VIDEO_FORMAT_INFO_SCALE_HEIGHT (dest->info.finfo, 0, y0);
  if (y1 != 0)
    y1 = GST_VIDEO_FORMAT_INFO_SCALE_HEIGHT (dest->info.finfo, 0, y1);

  for (i = y0; i < y1; i++) {
    for (j = x0; j < x1; j++) {
      gint y;
      gint y_pos;

      y_pos = (i * dest_stride) + j * pixel_stride;
      y = dest_ptr[y_pos] + overlay->shading_value;

      dest_ptr[y_pos] = CLAMP (y, 0, 255);
    }
  }
}

#define gst_base_text_overlay_shade_BGRx gst_base_text_overlay_shade_xRGB
#define gst_base_text_overlay_shade_RGBx gst_base_text_overlay_shade_xRGB
#define gst_base_text_overlay_shade_xBGR gst_base_text_overlay_shade_xRGB
static inline void
gst_base_text_overlay_shade_xRGB (GstBaseTextOverlay * overlay,
    GstVideoFrame * dest, gint x0, gint x1, gint y0, gint y1)
{
  gint i, j;
  guint8 *dest_ptr;

  dest_ptr = dest->data[0];

  for (i = y0; i < y1; i++) {
    for (j = x0; j < x1; j++) {
      gint y, y_pos, k;

      y_pos = (i * 4 * overlay->width) + j * 4;
      for (k = 0; k < 4; k++) {
        y = dest_ptr[y_pos + k] + overlay->shading_value;
        dest_ptr[y_pos + k] = CLAMP (y, 0, 255);
      }
    }
  }
}

/* FIXME: orcify */
static void
gst_base_text_overlay_shade_rgb24 (GstBaseTextOverlay * overlay,
    GstVideoFrame * frame, gint x0, gint x1, gint y0, gint y1)
{
  const int pstride = 3;
  gint y, x, stride, shading_val, tmp;
  guint8 *p;

  shading_val = overlay->shading_value;
  stride = GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0);

  for (y = y0; y < y1; ++y) {
    p = GST_VIDEO_FRAME_PLANE_DATA (frame, 0);
    p += (y * stride) + (x0 * pstride);
    for (x = x0; x < x1; ++x) {
      tmp = *p + shading_val;
      *p++ = CLAMP (tmp, 0, 255);
      tmp = *p + shading_val;
      *p++ = CLAMP (tmp, 0, 255);
      tmp = *p + shading_val;
      *p++ = CLAMP (tmp, 0, 255);
    }
  }
}

#define ARGB_SHADE_FUNCTION(name, OFFSET)	\
static inline void \
gst_base_text_overlay_shade_##name (GstBaseTextOverlay * overlay, GstVideoFrame * dest, \
gint x0, gint x1, gint y0, gint y1) \
{ \
  gint i, j;\
  guint8 *dest_ptr;\
  \
  dest_ptr = dest->data[0];\
  \
  for (i = y0; i < y1; i++) {\
    for (j = x0; j < x1; j++) {\
      gint y, y_pos, k;\
      y_pos = (i * 4 * overlay->width) + j * 4;\
      for (k = OFFSET; k < 3+OFFSET; k++) {\
        y = dest_ptr[y_pos + k] + overlay->shading_value;\
        dest_ptr[y_pos + k] = CLAMP (y, 0, 255);\
      }\
    }\
  }\
}
ARGB_SHADE_FUNCTION (ARGB, 1);
ARGB_SHADE_FUNCTION (ABGR, 1);
ARGB_SHADE_FUNCTION (RGBA, 0);
ARGB_SHADE_FUNCTION (BGRA, 0);

static void
gst_base_text_overlay_render_text (GstBaseTextOverlay * overlay,
    const gchar * text, gint textlen)
{
  gchar *string;

  if (!overlay->need_render) {
    GST_DEBUG ("Using previously rendered text.");
    return;
  }

  /* -1 is the whole string */
  if (text != NULL && textlen < 0) {
    textlen = strlen (text);
  }

  if (text != NULL) {
    string = g_strndup (text, textlen);
  } else {                      /* empty string */
    string = g_strdup (" ");
  }
  g_strdelimit (string, "\r\t", ' ');
  textlen = strlen (string);

  /* FIXME: should we check for UTF-8 here? */

  GST_DEBUG ("Rendering '%s'", string);
  gst_base_text_overlay_render_pangocairo (overlay, string, textlen);

  g_free (string);

  overlay->need_render = FALSE;
}

/* FIXME: should probably be relative to width/height (adjusted for PAR) */
#define BOX_XPAD  6
#define BOX_YPAD  6

static void
gst_base_text_overlay_shade_background (GstBaseTextOverlay * overlay,
    GstVideoFrame * frame, gint x0, gint x1, gint y0, gint y1)
{
  x0 = CLAMP (x0 - BOX_XPAD, 0, overlay->width);
  x1 = CLAMP (x1 + BOX_XPAD, 0, overlay->width);

  y0 = CLAMP (y0 - BOX_YPAD, 0, overlay->height);
  y1 = CLAMP (y1 + BOX_YPAD, 0, overlay->height);

  switch (overlay->format) {
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:
    case GST_VIDEO_FORMAT_Y41B:
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_YUV9:
    case GST_VIDEO_FORMAT_YVU9:
    case GST_VIDEO_FORMAT_GRAY8:
      gst_base_text_overlay_shade_planar_Y (overlay, frame, x0, x1, y0, y1);
      break;
    case GST_VIDEO_FORMAT_AYUV:
    case GST_VIDEO_FORMAT_UYVY:
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_v308:
      gst_base_text_overlay_shade_packed_Y (overlay, frame, x0, x1, y0, y1);
      break;
    case GST_VIDEO_FORMAT_xRGB:
      gst_base_text_overlay_shade_xRGB (overlay, frame, x0, x1, y0, y1);
      break;
    case GST_VIDEO_FORMAT_xBGR:
      gst_base_text_overlay_shade_xBGR (overlay, frame, x0, x1, y0, y1);
      break;
    case GST_VIDEO_FORMAT_BGRx:
      gst_base_text_overlay_shade_BGRx (overlay, frame, x0, x1, y0, y1);
      break;
    case GST_VIDEO_FORMAT_RGBx:
      gst_base_text_overlay_shade_RGBx (overlay, frame, x0, x1, y0, y1);
      break;
    case GST_VIDEO_FORMAT_ARGB:
      gst_base_text_overlay_shade_ARGB (overlay, frame, x0, x1, y0, y1);
      break;
    case GST_VIDEO_FORMAT_ABGR:
      gst_base_text_overlay_shade_ABGR (overlay, frame, x0, x1, y0, y1);
      break;
    case GST_VIDEO_FORMAT_RGBA:
      gst_base_text_overlay_shade_RGBA (overlay, frame, x0, x1, y0, y1);
      break;
    case GST_VIDEO_FORMAT_BGRA:
      gst_base_text_overlay_shade_BGRA (overlay, frame, x0, x1, y0, y1);
      break;
    case GST_VIDEO_FORMAT_BGR:
    case GST_VIDEO_FORMAT_RGB:
      gst_base_text_overlay_shade_rgb24 (overlay, frame, x0, x1, y0, y1);
      break;
    default:
      GST_FIXME_OBJECT (overlay, "implement background shading for format %s",
          gst_video_format_to_string (GST_VIDEO_FRAME_FORMAT (frame)));
      break;
  }
}

static GstFlowReturn
gst_base_text_overlay_push_frame (GstBaseTextOverlay * overlay,
    GstBuffer * video_frame)
{
  GstVideoFrame frame;

  if (overlay->composition == NULL)
    goto done;

  if (gst_pad_check_reconfigure (overlay->srcpad))
    gst_base_text_overlay_negotiate (overlay);

  video_frame = gst_buffer_make_writable (video_frame);

  if (overlay->attach_compo_to_buffer) {
    GST_DEBUG_OBJECT (overlay, "Attaching text overlay image to video buffer");
    gst_buffer_add_video_overlay_composition_meta (video_frame,
        overlay->composition);
    /* FIXME: emulate shaded background box if want_shading=true */
    goto done;
  }

  if (!gst_video_frame_map (&frame, &overlay->info, video_frame,
          GST_MAP_READWRITE))
    goto invalid_frame;

  /* shaded background box */
  if (overlay->want_shading) {
    gint xpos, ypos;

    gst_base_text_overlay_get_pos (overlay, &xpos, &ypos);

    gst_base_text_overlay_shade_background (overlay, &frame,
        xpos, xpos + overlay->image_width, ypos, ypos + overlay->image_height);
  }

  gst_video_overlay_composition_blend (overlay->composition, &frame);

  gst_video_frame_unmap (&frame);

done:

  return gst_pad_push (overlay->srcpad, video_frame);

  /* ERRORS */
invalid_frame:
  {
    gst_buffer_unref (video_frame);
    GST_DEBUG_OBJECT (overlay, "received invalid buffer");
    return GST_FLOW_OK;
  }
}

static GstPadLinkReturn
gst_base_text_overlay_text_pad_link (GstPad * pad, GstObject * parent,
    GstPad * peer)
{
  GstBaseTextOverlay *overlay;

  overlay = GST_BASE_TEXT_OVERLAY (parent);
  if (G_UNLIKELY (!overlay))
    return GST_PAD_LINK_REFUSED;

  GST_DEBUG_OBJECT (overlay, "Text pad linked");

  overlay->text_linked = TRUE;

  return GST_PAD_LINK_OK;
}

static void
gst_base_text_overlay_text_pad_unlink (GstPad * pad, GstObject * parent)
{
  GstBaseTextOverlay *overlay;

  /* don't use gst_pad_get_parent() here, will deadlock */
  overlay = GST_BASE_TEXT_OVERLAY (parent);

  GST_DEBUG_OBJECT (overlay, "Text pad unlinked");

  overlay->text_linked = FALSE;

  gst_segment_init (&overlay->text_segment, GST_FORMAT_UNDEFINED);
}

static gboolean
gst_base_text_overlay_text_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  gboolean ret = FALSE;
  GstBaseTextOverlay *overlay = NULL;

  overlay = GST_BASE_TEXT_OVERLAY (parent);

  GST_LOG_OBJECT (pad, "received event %s", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      ret = gst_base_text_overlay_setcaps_txt (overlay, caps);
      gst_event_unref (event);
      break;
    }
    case GST_EVENT_SEGMENT:
    {
      const GstSegment *segment;

      overlay->text_eos = FALSE;

      gst_event_parse_segment (event, &segment);

      if (segment->format == GST_FORMAT_TIME) {
        GST_BASE_TEXT_OVERLAY_LOCK (overlay);
        gst_segment_copy_into (segment, &overlay->text_segment);
        GST_DEBUG_OBJECT (overlay, "TEXT SEGMENT now: %" GST_SEGMENT_FORMAT,
            &overlay->text_segment);
        GST_BASE_TEXT_OVERLAY_UNLOCK (overlay);
      } else {
        GST_ELEMENT_WARNING (overlay, STREAM, MUX, (NULL),
            ("received non-TIME newsegment event on text input"));
      }

      gst_event_unref (event);
      ret = TRUE;

      /* wake up the video chain, it might be waiting for a text buffer or
       * a text segment update */
      GST_BASE_TEXT_OVERLAY_LOCK (overlay);
      GST_BASE_TEXT_OVERLAY_BROADCAST (overlay);
      GST_BASE_TEXT_OVERLAY_UNLOCK (overlay);
      break;
    }
    case GST_EVENT_GAP:
    {
      GstClockTime start, duration;

      gst_event_parse_gap (event, &start, &duration);
      if (GST_CLOCK_TIME_IS_VALID (duration))
        start += duration;
      /* we do not expect another buffer until after gap,
       * so that is our position now */
      overlay->text_segment.position = start;

      /* wake up the video chain, it might be waiting for a text buffer or
       * a text segment update */
      GST_BASE_TEXT_OVERLAY_LOCK (overlay);
      GST_BASE_TEXT_OVERLAY_BROADCAST (overlay);
      GST_BASE_TEXT_OVERLAY_UNLOCK (overlay);
      break;
    }
    case GST_EVENT_FLUSH_STOP:
      GST_BASE_TEXT_OVERLAY_LOCK (overlay);
      GST_INFO_OBJECT (overlay, "text flush stop");
      overlay->text_flushing = FALSE;
      overlay->text_eos = FALSE;
      gst_base_text_overlay_pop_text (overlay);
      gst_segment_init (&overlay->text_segment, GST_FORMAT_TIME);
      GST_BASE_TEXT_OVERLAY_UNLOCK (overlay);
      gst_event_unref (event);
      ret = TRUE;
      break;
    case GST_EVENT_FLUSH_START:
      GST_BASE_TEXT_OVERLAY_LOCK (overlay);
      GST_INFO_OBJECT (overlay, "text flush start");
      overlay->text_flushing = TRUE;
      GST_BASE_TEXT_OVERLAY_BROADCAST (overlay);
      GST_BASE_TEXT_OVERLAY_UNLOCK (overlay);
      gst_event_unref (event);
      ret = TRUE;
      break;
    case GST_EVENT_EOS:
      GST_BASE_TEXT_OVERLAY_LOCK (overlay);
      overlay->text_eos = TRUE;
      GST_INFO_OBJECT (overlay, "text EOS");
      /* wake up the video chain, it might be waiting for a text buffer or
       * a text segment update */
      GST_BASE_TEXT_OVERLAY_BROADCAST (overlay);
      GST_BASE_TEXT_OVERLAY_UNLOCK (overlay);
      gst_event_unref (event);
      ret = TRUE;
      break;
    default:
      ret = gst_pad_event_default (pad, parent, event);
      break;
  }

  return ret;
}

static gboolean
gst_base_text_overlay_video_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  gboolean ret = FALSE;
  GstBaseTextOverlay *overlay = NULL;

  overlay = GST_BASE_TEXT_OVERLAY (parent);

  GST_DEBUG_OBJECT (pad, "received event %s", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      ret = gst_base_text_overlay_setcaps (overlay, caps);
      gst_event_unref (event);
      break;
    }
    case GST_EVENT_SEGMENT:
    {
      const GstSegment *segment;

      GST_DEBUG_OBJECT (overlay, "received new segment");

      gst_event_parse_segment (event, &segment);

      if (segment->format == GST_FORMAT_TIME) {
        GST_DEBUG_OBJECT (overlay, "VIDEO SEGMENT now: %" GST_SEGMENT_FORMAT,
            &overlay->segment);

        gst_segment_copy_into (segment, &overlay->segment);
      } else {
        GST_ELEMENT_WARNING (overlay, STREAM, MUX, (NULL),
            ("received non-TIME newsegment event on video input"));
      }

      ret = gst_pad_event_default (pad, parent, event);
      break;
    }
    case GST_EVENT_EOS:
      GST_BASE_TEXT_OVERLAY_LOCK (overlay);
      GST_INFO_OBJECT (overlay, "video EOS");
      overlay->video_eos = TRUE;
      GST_BASE_TEXT_OVERLAY_UNLOCK (overlay);
      ret = gst_pad_event_default (pad, parent, event);
      break;
    case GST_EVENT_FLUSH_START:
      GST_BASE_TEXT_OVERLAY_LOCK (overlay);
      GST_INFO_OBJECT (overlay, "video flush start");
      overlay->video_flushing = TRUE;
      GST_BASE_TEXT_OVERLAY_BROADCAST (overlay);
      GST_BASE_TEXT_OVERLAY_UNLOCK (overlay);
      ret = gst_pad_event_default (pad, parent, event);
      break;
    case GST_EVENT_FLUSH_STOP:
      GST_BASE_TEXT_OVERLAY_LOCK (overlay);
      GST_INFO_OBJECT (overlay, "video flush stop");
      overlay->video_flushing = FALSE;
      overlay->video_eos = FALSE;
      gst_segment_init (&overlay->segment, GST_FORMAT_TIME);
      GST_BASE_TEXT_OVERLAY_UNLOCK (overlay);
      ret = gst_pad_event_default (pad, parent, event);
      break;
    default:
      ret = gst_pad_event_default (pad, parent, event);
      break;
  }

  return ret;
}

static gboolean
gst_base_text_overlay_video_query (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  gboolean ret = FALSE;
  GstBaseTextOverlay *overlay;

  overlay = GST_BASE_TEXT_OVERLAY (parent);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *filter, *caps;

      gst_query_parse_caps (query, &filter);
      caps = gst_base_text_overlay_getcaps (pad, overlay, filter);
      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);
      ret = TRUE;
      break;
    }
    default:
      ret = gst_pad_query_default (pad, parent, query);
      break;
  }

  return ret;
}

/* Called with lock held */
static void
gst_base_text_overlay_pop_text (GstBaseTextOverlay * overlay)
{
  g_return_if_fail (GST_IS_BASE_TEXT_OVERLAY (overlay));

  if (overlay->text_buffer) {
    GST_DEBUG_OBJECT (overlay, "releasing text buffer %p",
        overlay->text_buffer);
    gst_buffer_unref (overlay->text_buffer);
    overlay->text_buffer = NULL;
  }

  /* Let the text task know we used that buffer */
  GST_BASE_TEXT_OVERLAY_BROADCAST (overlay);
}

/* We receive text buffers here. If they are out of segment we just ignore them.
   If the buffer is in our segment we keep it internally except if another one
   is already waiting here, in that case we wait that it gets kicked out */
static GstFlowReturn
gst_base_text_overlay_text_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstBaseTextOverlay *overlay = NULL;
  gboolean in_seg = FALSE;
  guint64 clip_start = 0, clip_stop = 0;

  overlay = GST_BASE_TEXT_OVERLAY (parent);

  GST_BASE_TEXT_OVERLAY_LOCK (overlay);

  if (overlay->text_flushing) {
    GST_BASE_TEXT_OVERLAY_UNLOCK (overlay);
    ret = GST_FLOW_FLUSHING;
    GST_LOG_OBJECT (overlay, "text flushing");
    goto beach;
  }

  if (overlay->text_eos) {
    GST_BASE_TEXT_OVERLAY_UNLOCK (overlay);
    ret = GST_FLOW_EOS;
    GST_LOG_OBJECT (overlay, "text EOS");
    goto beach;
  }

  GST_LOG_OBJECT (overlay, "%" GST_SEGMENT_FORMAT "  BUFFER: ts=%"
      GST_TIME_FORMAT ", end=%" GST_TIME_FORMAT, &overlay->segment,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)),
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer) +
          GST_BUFFER_DURATION (buffer)));

  if (G_LIKELY (GST_BUFFER_TIMESTAMP_IS_VALID (buffer))) {
    GstClockTime stop;

    if (G_LIKELY (GST_BUFFER_DURATION_IS_VALID (buffer)))
      stop = GST_BUFFER_TIMESTAMP (buffer) + GST_BUFFER_DURATION (buffer);
    else
      stop = GST_CLOCK_TIME_NONE;

    in_seg = gst_segment_clip (&overlay->text_segment, GST_FORMAT_TIME,
        GST_BUFFER_TIMESTAMP (buffer), stop, &clip_start, &clip_stop);
  } else {
    in_seg = TRUE;
  }

  if (in_seg) {
    if (GST_BUFFER_TIMESTAMP_IS_VALID (buffer))
      GST_BUFFER_TIMESTAMP (buffer) = clip_start;
    else if (GST_BUFFER_DURATION_IS_VALID (buffer))
      GST_BUFFER_DURATION (buffer) = clip_stop - clip_start;

    /* Wait for the previous buffer to go away */
    while (overlay->text_buffer != NULL) {
      GST_DEBUG ("Pad %s:%s has a buffer queued, waiting",
          GST_DEBUG_PAD_NAME (pad));
      GST_BASE_TEXT_OVERLAY_WAIT (overlay);
      GST_DEBUG ("Pad %s:%s resuming", GST_DEBUG_PAD_NAME (pad));
      if (overlay->text_flushing) {
        GST_BASE_TEXT_OVERLAY_UNLOCK (overlay);
        ret = GST_FLOW_FLUSHING;
        goto beach;
      }
    }

    if (GST_BUFFER_TIMESTAMP_IS_VALID (buffer))
      overlay->text_segment.position = clip_start;

    overlay->text_buffer = buffer;
    /* That's a new text buffer we need to render */
    overlay->need_render = TRUE;

    /* in case the video chain is waiting for a text buffer, wake it up */
    GST_BASE_TEXT_OVERLAY_BROADCAST (overlay);
  }

  GST_BASE_TEXT_OVERLAY_UNLOCK (overlay);

beach:

  return ret;
}

static GstFlowReturn
gst_base_text_overlay_video_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer)
{
  GstBaseTextOverlayClass *klass;
  GstBaseTextOverlay *overlay;
  GstFlowReturn ret = GST_FLOW_OK;
  gboolean in_seg = FALSE;
  guint64 start, stop, clip_start = 0, clip_stop = 0;
  gchar *text = NULL;

  overlay = GST_BASE_TEXT_OVERLAY (parent);
  klass = GST_BASE_TEXT_OVERLAY_GET_CLASS (overlay);

  if (!GST_BUFFER_TIMESTAMP_IS_VALID (buffer))
    goto missing_timestamp;

  /* ignore buffers that are outside of the current segment */
  start = GST_BUFFER_TIMESTAMP (buffer);

  if (!GST_BUFFER_DURATION_IS_VALID (buffer)) {
    stop = GST_CLOCK_TIME_NONE;
  } else {
    stop = start + GST_BUFFER_DURATION (buffer);
  }

  GST_LOG_OBJECT (overlay, "%" GST_SEGMENT_FORMAT "  BUFFER: ts=%"
      GST_TIME_FORMAT ", end=%" GST_TIME_FORMAT, &overlay->segment,
      GST_TIME_ARGS (start), GST_TIME_ARGS (stop));

  /* segment_clip() will adjust start unconditionally to segment_start if
   * no stop time is provided, so handle this ourselves */
  if (stop == GST_CLOCK_TIME_NONE && start < overlay->segment.start)
    goto out_of_segment;

  in_seg = gst_segment_clip (&overlay->segment, GST_FORMAT_TIME, start, stop,
      &clip_start, &clip_stop);

  if (!in_seg)
    goto out_of_segment;

  /* if the buffer is only partially in the segment, fix up stamps */
  if (clip_start != start || (stop != -1 && clip_stop != stop)) {
    GST_DEBUG_OBJECT (overlay, "clipping buffer timestamp/duration to segment");
    buffer = gst_buffer_make_writable (buffer);
    GST_BUFFER_TIMESTAMP (buffer) = clip_start;
    if (stop != -1)
      GST_BUFFER_DURATION (buffer) = clip_stop - clip_start;
  }

  /* now, after we've done the clipping, fix up end time if there's no
   * duration (we only use those estimated values internally though, we
   * don't want to set bogus values on the buffer itself) */
  if (stop == -1) {
    GstCaps *caps;
    GstStructure *s;
    gint fps_num, fps_denom;

    /* FIXME, store this in setcaps */
    caps = gst_pad_get_current_caps (pad);
    s = gst_caps_get_structure (caps, 0);
    if (gst_structure_get_fraction (s, "framerate", &fps_num, &fps_denom) &&
        fps_num && fps_denom) {
      GST_DEBUG_OBJECT (overlay, "estimating duration based on framerate");
      stop = start + gst_util_uint64_scale_int (GST_SECOND, fps_denom, fps_num);
    } else {
      GST_WARNING_OBJECT (overlay, "no duration, assuming minimal duration");
      stop = start + 1;         /* we need to assume some interval */
    }
    gst_caps_unref (caps);
  }

  gst_object_sync_values (GST_OBJECT (overlay), GST_BUFFER_TIMESTAMP (buffer));

wait_for_text_buf:

  GST_BASE_TEXT_OVERLAY_LOCK (overlay);

  if (overlay->video_flushing)
    goto flushing;

  if (overlay->video_eos)
    goto have_eos;

  if (overlay->silent) {
    GST_BASE_TEXT_OVERLAY_UNLOCK (overlay);
    ret = gst_pad_push (overlay->srcpad, buffer);

    /* Update position */
    overlay->segment.position = clip_start;

    return ret;
  }

  /* Text pad not linked, rendering internal text */
  if (!overlay->text_linked) {
    if (klass->get_text) {
      text = klass->get_text (overlay, buffer);
    } else {
      text = g_strdup (overlay->default_text);
    }

    GST_LOG_OBJECT (overlay, "Text pad not linked, rendering default "
        "text: '%s'", GST_STR_NULL (text));

    GST_BASE_TEXT_OVERLAY_UNLOCK (overlay);

    if (text != NULL && *text != '\0') {
      /* Render and push */
      gst_base_text_overlay_render_text (overlay, text, -1);
      ret = gst_base_text_overlay_push_frame (overlay, buffer);
    } else {
      /* Invalid or empty string */
      ret = gst_pad_push (overlay->srcpad, buffer);
    }
  } else {
    /* Text pad linked, check if we have a text buffer queued */
    if (overlay->text_buffer) {
      gboolean pop_text = FALSE, valid_text_time = TRUE;
      GstClockTime text_start = GST_CLOCK_TIME_NONE;
      GstClockTime text_end = GST_CLOCK_TIME_NONE;
      GstClockTime text_running_time = GST_CLOCK_TIME_NONE;
      GstClockTime text_running_time_end = GST_CLOCK_TIME_NONE;
      GstClockTime vid_running_time, vid_running_time_end;

      /* if the text buffer isn't stamped right, pop it off the
       * queue and display it for the current video frame only */
      if (!GST_BUFFER_TIMESTAMP_IS_VALID (overlay->text_buffer) ||
          !GST_BUFFER_DURATION_IS_VALID (overlay->text_buffer)) {
        GST_WARNING_OBJECT (overlay,
            "Got text buffer with invalid timestamp or duration");
        pop_text = TRUE;
        valid_text_time = FALSE;
      } else {
        text_start = GST_BUFFER_TIMESTAMP (overlay->text_buffer);
        text_end = text_start + GST_BUFFER_DURATION (overlay->text_buffer);
      }

      vid_running_time =
          gst_segment_to_running_time (&overlay->segment, GST_FORMAT_TIME,
          start);
      vid_running_time_end =
          gst_segment_to_running_time (&overlay->segment, GST_FORMAT_TIME,
          stop);

      /* If timestamp and duration are valid */
      if (valid_text_time) {
        text_running_time =
            gst_segment_to_running_time (&overlay->segment, GST_FORMAT_TIME,
            text_start);
        text_running_time_end =
            gst_segment_to_running_time (&overlay->segment, GST_FORMAT_TIME,
            text_end);
      }

      GST_LOG_OBJECT (overlay, "T: %" GST_TIME_FORMAT " - %" GST_TIME_FORMAT,
          GST_TIME_ARGS (text_running_time),
          GST_TIME_ARGS (text_running_time_end));
      GST_LOG_OBJECT (overlay, "V: %" GST_TIME_FORMAT " - %" GST_TIME_FORMAT,
          GST_TIME_ARGS (vid_running_time),
          GST_TIME_ARGS (vid_running_time_end));

      /* Text too old or in the future */
      if (valid_text_time && text_running_time_end <= vid_running_time) {
        /* text buffer too old, get rid of it and do nothing  */
        GST_LOG_OBJECT (overlay, "text buffer too old, popping");
        pop_text = FALSE;
        gst_base_text_overlay_pop_text (overlay);
        GST_BASE_TEXT_OVERLAY_UNLOCK (overlay);
        goto wait_for_text_buf;
      } else if (valid_text_time && vid_running_time_end <= text_running_time) {
        GST_LOG_OBJECT (overlay, "text in future, pushing video buf");
        GST_BASE_TEXT_OVERLAY_UNLOCK (overlay);
        /* Push the video frame */
        ret = gst_pad_push (overlay->srcpad, buffer);
      } else {
        GstMapInfo map;
        gchar *in_text;
        gsize in_size;

        gst_buffer_map (overlay->text_buffer, &map, GST_MAP_READ);
        in_text = (gchar *) map.data;
        in_size = map.size;

        if (in_size > 0) {
          /* g_markup_escape_text() absolutely requires valid UTF8 input, it
           * might crash otherwise. We don't fall back on GST_SUBTITLE_ENCODING
           * here on purpose, this is something that needs fixing upstream */
          if (!g_utf8_validate (in_text, in_size, NULL)) {
            const gchar *end = NULL;

            GST_WARNING_OBJECT (overlay, "received invalid UTF-8");
            in_text = g_strndup (in_text, in_size);
            while (!g_utf8_validate (in_text, in_size, &end) && end)
              *((gchar *) end) = '*';
          }

          /* Get the string */
          if (overlay->have_pango_markup) {
            text = g_strndup (in_text, in_size);
          } else {
            text = g_markup_escape_text (in_text, in_size);
          }

          if (text != NULL && *text != '\0') {
            gint text_len = strlen (text);

            while (text_len > 0 && (text[text_len - 1] == '\n' ||
                    text[text_len - 1] == '\r')) {
              --text_len;
            }
            GST_DEBUG_OBJECT (overlay, "Rendering text '%*s'", text_len, text);
            gst_base_text_overlay_render_text (overlay, text, text_len);
          } else {
            GST_DEBUG_OBJECT (overlay, "No text to render (empty buffer)");
            gst_base_text_overlay_render_text (overlay, " ", 1);
          }
          if (in_text != (gchar *) map.data)
            g_free (in_text);
        } else {
          GST_DEBUG_OBJECT (overlay, "No text to render (empty buffer)");
          gst_base_text_overlay_render_text (overlay, " ", 1);
        }

        gst_buffer_unmap (overlay->text_buffer, &map);

        GST_BASE_TEXT_OVERLAY_UNLOCK (overlay);
        ret = gst_base_text_overlay_push_frame (overlay, buffer);

        if (valid_text_time && text_running_time_end <= vid_running_time_end) {
          GST_LOG_OBJECT (overlay, "text buffer not needed any longer");
          pop_text = TRUE;
        }
      }
      if (pop_text) {
        GST_BASE_TEXT_OVERLAY_LOCK (overlay);
        gst_base_text_overlay_pop_text (overlay);
        GST_BASE_TEXT_OVERLAY_UNLOCK (overlay);
      }
    } else {
      gboolean wait_for_text_buf = TRUE;

      if (overlay->text_eos)
        wait_for_text_buf = FALSE;

      if (!overlay->wait_text)
        wait_for_text_buf = FALSE;

      /* Text pad linked, but no text buffer available - what now? */
      if (overlay->text_segment.format == GST_FORMAT_TIME) {
        GstClockTime text_start_running_time, text_position_running_time;
        GstClockTime vid_running_time;

        vid_running_time =
            gst_segment_to_running_time (&overlay->segment, GST_FORMAT_TIME,
            GST_BUFFER_TIMESTAMP (buffer));
        text_start_running_time =
            gst_segment_to_running_time (&overlay->text_segment,
            GST_FORMAT_TIME, overlay->text_segment.start);
        text_position_running_time =
            gst_segment_to_running_time (&overlay->text_segment,
            GST_FORMAT_TIME, overlay->text_segment.position);

        if ((GST_CLOCK_TIME_IS_VALID (text_start_running_time) &&
                vid_running_time < text_start_running_time) ||
            (GST_CLOCK_TIME_IS_VALID (text_position_running_time) &&
                vid_running_time < text_position_running_time)) {
          wait_for_text_buf = FALSE;
        }
      }

      if (wait_for_text_buf) {
        GST_DEBUG_OBJECT (overlay, "no text buffer, need to wait for one");
        GST_BASE_TEXT_OVERLAY_WAIT (overlay);
        GST_DEBUG_OBJECT (overlay, "resuming");
        GST_BASE_TEXT_OVERLAY_UNLOCK (overlay);
        goto wait_for_text_buf;
      } else {
        GST_BASE_TEXT_OVERLAY_UNLOCK (overlay);
        GST_LOG_OBJECT (overlay, "no need to wait for a text buffer");
        ret = gst_pad_push (overlay->srcpad, buffer);
      }
    }
  }

  g_free (text);

  /* Update position */
  overlay->segment.position = clip_start;

  return ret;

missing_timestamp:
  {
    GST_WARNING_OBJECT (overlay, "buffer without timestamp, discarding");
    gst_buffer_unref (buffer);
    return GST_FLOW_OK;
  }

flushing:
  {
    GST_BASE_TEXT_OVERLAY_UNLOCK (overlay);
    GST_DEBUG_OBJECT (overlay, "flushing, discarding buffer");
    gst_buffer_unref (buffer);
    return GST_FLOW_FLUSHING;
  }
have_eos:
  {
    GST_BASE_TEXT_OVERLAY_UNLOCK (overlay);
    GST_DEBUG_OBJECT (overlay, "eos, discarding buffer");
    gst_buffer_unref (buffer);
    return GST_FLOW_EOS;
  }
out_of_segment:
  {
    GST_DEBUG_OBJECT (overlay, "buffer out of segment, discarding");
    gst_buffer_unref (buffer);
    return GST_FLOW_OK;
  }
}

static GstStateChangeReturn
gst_base_text_overlay_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstBaseTextOverlay *overlay = GST_BASE_TEXT_OVERLAY (element);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      GST_BASE_TEXT_OVERLAY_LOCK (overlay);
      overlay->text_flushing = TRUE;
      overlay->video_flushing = TRUE;
      /* pop_text will broadcast on the GCond and thus also make the video
       * chain exit if it's waiting for a text buffer */
      gst_base_text_overlay_pop_text (overlay);
      GST_BASE_TEXT_OVERLAY_UNLOCK (overlay);
      break;
    default:
      break;
  }

  ret = parent_class->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      GST_BASE_TEXT_OVERLAY_LOCK (overlay);
      overlay->text_flushing = FALSE;
      overlay->video_flushing = FALSE;
      overlay->video_eos = FALSE;
      overlay->text_eos = FALSE;
      gst_segment_init (&overlay->segment, GST_FORMAT_TIME);
      gst_segment_init (&overlay->text_segment, GST_FORMAT_TIME);
      GST_BASE_TEXT_OVERLAY_UNLOCK (overlay);
      break;
    default:
      break;
  }

  return ret;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "textoverlay", GST_RANK_NONE,
          GST_TYPE_TEXT_OVERLAY) ||
      !gst_element_register (plugin, "timeoverlay", GST_RANK_NONE,
          GST_TYPE_TIME_OVERLAY) ||
      !gst_element_register (plugin, "clockoverlay", GST_RANK_NONE,
          GST_TYPE_CLOCK_OVERLAY) ||
      !gst_element_register (plugin, "textrender", GST_RANK_NONE,
          GST_TYPE_TEXT_RENDER)) {
    return FALSE;
  }

  /*texttestsrc_plugin_init(module, plugin); */

  GST_DEBUG_CATEGORY_INIT (pango_debug, "pango", 0, "Pango elements");

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR,
    pango, "Pango-based text rendering and overlay", plugin_init,
    VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
