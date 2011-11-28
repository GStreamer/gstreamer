/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2003> David Schleef <ds@schleef.org>
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
 * SECTION:element-cairotextoverlay
 *
 * cairotextoverlay renders the text on top of the video frames.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch videotestsrc ! cairotextoverlay text="hello" ! autovideosink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <string.h>
#include <gst/video/video.h>
#include "gsttextoverlay.h"

#include <cairo.h>

/* FIXME:
 *   - calculating the position of the shading rectangle is 
 *     not really right (try with text "L"), to say the least.
 *     Seems to work at least with latin script though.
 *   - check final x/y position and text width/height so that
 *     we don't do out-of-memory access when blitting the text.
 *     Also, we do not want to blit over the right or left margin.
 *   - what about text with newline characters? Cairo doesn't deal
 *     with that (we'd need to fix text_height usage for that as well)
 *   - upstream caps renegotiation, ie. when video window gets resized
 */

GST_DEBUG_CATEGORY_EXTERN (cairo_debug);
#define GST_CAT_DEFAULT cairo_debug

enum
{
  ARG_0,
  ARG_TEXT,
  ARG_SHADING,
  ARG_VALIGN,
  ARG_HALIGN,
  ARG_XPAD,
  ARG_YPAD,
  ARG_DELTAX,
  ARG_DELTAY,
  ARG_SILENT,
  ARG_FONT_DESC
};

#define DEFAULT_YPAD 25
#define DEFAULT_XPAD 25
#define DEFAULT_FONT "sans"
#define DEFAULT_SILENT FALSE

#define GST_CAIRO_TEXT_OVERLAY_DEFAULT_SCALE   20.0

static GstStaticPadTemplate cairo_text_overlay_src_template_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("I420"))
    );

static GstStaticPadTemplate video_sink_template_factory =
GST_STATIC_PAD_TEMPLATE ("video_sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("I420"))
    );

static GstStaticPadTemplate text_sink_template_factory =
GST_STATIC_PAD_TEMPLATE ("text_sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("text/plain")
    );

static void gst_text_overlay_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static GstStateChangeReturn gst_text_overlay_change_state (GstElement * element,
    GstStateChange transition);
static GstCaps *gst_text_overlay_getcaps (GstPad * pad);
static gboolean gst_text_overlay_setcaps (GstPad * pad, GstCaps * caps);
static GstPadLinkReturn gst_text_overlay_text_pad_linked (GstPad * pad,
    GstPad * peer);
static void gst_text_overlay_text_pad_unlinked (GstPad * pad);
static GstFlowReturn gst_text_overlay_collected (GstCollectPads * pads,
    gpointer data);
static void gst_text_overlay_finalize (GObject * object);
static void gst_text_overlay_font_init (GstCairoTextOverlay * overlay);
static gboolean gst_text_overlay_src_event (GstPad * pad, GstEvent * event);
static gboolean gst_text_overlay_video_event (GstPad * pad, GstEvent * event);

/* These macros are adapted from videotestsrc.c */
#define I420_Y_ROWSTRIDE(width) (GST_ROUND_UP_4(width))
#define I420_U_ROWSTRIDE(width) (GST_ROUND_UP_8(width)/2)
#define I420_V_ROWSTRIDE(width) ((GST_ROUND_UP_8(I420_Y_ROWSTRIDE(width)))/2)

#define I420_Y_OFFSET(w,h) (0)
#define I420_U_OFFSET(w,h) (I420_Y_OFFSET(w,h)+(I420_Y_ROWSTRIDE(w)*GST_ROUND_UP_2(h)))
#define I420_V_OFFSET(w,h) (I420_U_OFFSET(w,h)+(I420_U_ROWSTRIDE(w)*GST_ROUND_UP_2(h)/2))

#define I420_SIZE(w,h)     (I420_V_OFFSET(w,h)+(I420_V_ROWSTRIDE(w)*GST_ROUND_UP_2(h)/2))

GST_BOILERPLATE (GstCairoTextOverlay, gst_text_overlay, GstElement,
    GST_TYPE_ELEMENT);

static void
gst_text_overlay_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class,
      &cairo_text_overlay_src_template_factory);
  gst_element_class_add_static_pad_template (element_class,
      &video_sink_template_factory);
  gst_element_class_add_static_pad_template (element_class,
      &text_sink_template_factory);

  gst_element_class_set_details_simple (element_class, "Text overlay",
      "Filter/Editor/Video",
      "Adds text strings on top of a video buffer",
      "David Schleef <ds@schleef.org>");
}

static void
gst_text_overlay_class_init (GstCairoTextOverlayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->finalize = gst_text_overlay_finalize;
  gobject_class->set_property = gst_text_overlay_set_property;

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_text_overlay_change_state);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_TEXT,
      g_param_spec_string ("text", "text",
          "Text to be display.", "",
          G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_SHADING,
      g_param_spec_boolean ("shaded-background", "shaded background",
          "Whether to shade the background under the text area", FALSE,
          G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_VALIGN,
      g_param_spec_string ("valign", "vertical alignment",
          "Vertical alignment of the text. "
          "Can be either 'baseline', 'bottom', or 'top'",
          "baseline", G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_HALIGN,
      g_param_spec_string ("halign", "horizontal alignment",
          "Horizontal alignment of the text. "
          "Can be either 'left', 'right', or 'center'",
          "center", G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_XPAD,
      g_param_spec_int ("xpad", "horizontal paddding",
          "Horizontal paddding when using left/right alignment",
          G_MININT, G_MAXINT, DEFAULT_XPAD,
          G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_YPAD,
      g_param_spec_int ("ypad", "vertical padding",
          "Vertical padding when using top/bottom alignment",
          G_MININT, G_MAXINT, DEFAULT_YPAD,
          G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_DELTAX,
      g_param_spec_int ("deltax", "X position modifier",
          "Shift X position to the left or to the right. Unit is pixels.",
          G_MININT, G_MAXINT, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_DELTAY,
      g_param_spec_int ("deltay", "Y position modifier",
          "Shift Y position up or down. Unit is pixels.",
          G_MININT, G_MAXINT, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_FONT_DESC,
      g_param_spec_string ("font-desc", "font description",
          "Pango font description of font "
          "to be used for rendering. "
          "See documentation of "
          "pango_font_description_from_string"
          " for syntax.", "", G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));
  /* FIXME 0.11: rename to "visible" or "text-visible" or "render-text" */
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_SILENT,
      g_param_spec_boolean ("silent", "silent",
          "Whether to render the text string",
          DEFAULT_SILENT, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));
}

static void
gst_text_overlay_finalize (GObject * object)
{
  GstCairoTextOverlay *overlay = GST_CAIRO_TEXT_OVERLAY (object);

  gst_collect_pads_stop (overlay->collect);
  gst_object_unref (overlay->collect);

  g_free (overlay->text_fill_image);
  g_free (overlay->text_outline_image);

  g_free (overlay->default_text);
  g_free (overlay->font);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_text_overlay_init (GstCairoTextOverlay * overlay,
    GstCairoTextOverlayClass * klass)
{
  /* video sink */
  overlay->video_sinkpad =
      gst_pad_new_from_static_template (&video_sink_template_factory,
      "video_sink");
  gst_pad_set_getcaps_function (overlay->video_sinkpad,
      GST_DEBUG_FUNCPTR (gst_text_overlay_getcaps));
  gst_pad_set_setcaps_function (overlay->video_sinkpad,
      GST_DEBUG_FUNCPTR (gst_text_overlay_setcaps));
  gst_element_add_pad (GST_ELEMENT (overlay), overlay->video_sinkpad);

  /* text sink */
  overlay->text_sinkpad =
      gst_pad_new_from_static_template (&text_sink_template_factory,
      "text_sink");
  gst_pad_set_link_function (overlay->text_sinkpad,
      GST_DEBUG_FUNCPTR (gst_text_overlay_text_pad_linked));
  gst_pad_set_unlink_function (overlay->text_sinkpad,
      GST_DEBUG_FUNCPTR (gst_text_overlay_text_pad_unlinked));
  gst_element_add_pad (GST_ELEMENT (overlay), overlay->text_sinkpad);

  /* (video) source */
  overlay->srcpad =
      gst_pad_new_from_static_template
      (&cairo_text_overlay_src_template_factory, "src");
  gst_pad_set_getcaps_function (overlay->srcpad,
      GST_DEBUG_FUNCPTR (gst_text_overlay_getcaps));
  gst_pad_set_event_function (overlay->srcpad,
      GST_DEBUG_FUNCPTR (gst_text_overlay_src_event));
  gst_element_add_pad (GST_ELEMENT (overlay), overlay->srcpad);

  overlay->halign = GST_CAIRO_TEXT_OVERLAY_HALIGN_CENTER;
  overlay->valign = GST_CAIRO_TEXT_OVERLAY_VALIGN_BASELINE;
  overlay->xpad = DEFAULT_XPAD;
  overlay->ypad = DEFAULT_YPAD;
  overlay->deltax = 0;
  overlay->deltay = 0;

  overlay->default_text = g_strdup ("");
  overlay->need_render = TRUE;

  overlay->font = g_strdup (DEFAULT_FONT);
  gst_text_overlay_font_init (overlay);

  overlay->silent = DEFAULT_SILENT;

  overlay->fps_n = 0;
  overlay->fps_d = 1;

  overlay->collect = gst_collect_pads_new ();

  gst_collect_pads_set_function (overlay->collect,
      GST_DEBUG_FUNCPTR (gst_text_overlay_collected), overlay);

  overlay->video_collect_data = gst_collect_pads_add_pad (overlay->collect,
      overlay->video_sinkpad, sizeof (GstCollectData));

  /* FIXME: hacked way to override/extend the event function of
   * GstCollectPads; because it sets its own event function giving the
   * element no access to events. Nicked from avimux. */
  overlay->collect_event =
      (GstPadEventFunction) GST_PAD_EVENTFUNC (overlay->video_sinkpad);
  gst_pad_set_event_function (overlay->video_sinkpad,
      GST_DEBUG_FUNCPTR (gst_text_overlay_video_event));

  /* text pad will be added when it is linked */
  overlay->text_collect_data = NULL;
}

static void
gst_text_overlay_font_init (GstCairoTextOverlay * overlay)
{
  cairo_font_extents_t font_extents;
  cairo_surface_t *surface;
  cairo_t *cr;
  gchar *font_desc, *sep;

  font_desc = g_ascii_strdown (overlay->font, -1);

  /* cairo_select_font_face() does not parse the size at the end, so we have
   * to do that ourselves; same for slate and weight */
  sep = MAX (strrchr (font_desc, ' '), strrchr (font_desc, ','));
  if (sep != NULL && g_strtod (sep, NULL) > 0.0) {
    /* there may be a suffix such as 'px', but we just ignore that for now */
    overlay->scale = g_strtod (sep, NULL);
  } else {
    overlay->scale = GST_CAIRO_TEXT_OVERLAY_DEFAULT_SCALE;
  }
  if (strstr (font_desc, "bold"))
    overlay->weight = CAIRO_FONT_WEIGHT_BOLD;
  else
    overlay->weight = CAIRO_FONT_WEIGHT_NORMAL;

  if (strstr (font_desc, "italic"))
    overlay->slant = CAIRO_FONT_SLANT_ITALIC;
  else if (strstr (font_desc, "oblique"))
    overlay->slant = CAIRO_FONT_SLANT_OBLIQUE;
  else
    overlay->slant = CAIRO_FONT_SLANT_NORMAL;

  GST_LOG_OBJECT (overlay, "Font desc: '%s', scale=%f, weight=%d, slant=%d",
      overlay->font, overlay->scale, overlay->weight, overlay->slant);

  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, 256, 256);
  cr = cairo_create (surface);

  cairo_select_font_face (cr, overlay->font, overlay->slant, overlay->weight);
  cairo_set_font_size (cr, overlay->scale);

  /* this has a static leak:
   * http://lists.freedesktop.org/archives/cairo/2007-May/010623.html
   */
  cairo_font_extents (cr, &font_extents);
  overlay->font_height = GST_ROUND_UP_2 ((guint) font_extents.height);
  overlay->need_render = TRUE;

  cairo_destroy (cr);
  cairo_surface_destroy (surface);
  g_free (font_desc);
}

static void
gst_text_overlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCairoTextOverlay *overlay = GST_CAIRO_TEXT_OVERLAY (object);

  GST_OBJECT_LOCK (overlay);

  switch (prop_id) {
    case ARG_TEXT:{
      g_free (overlay->default_text);
      overlay->default_text = g_value_dup_string (value);
      break;
    }
    case ARG_SHADING:{
      overlay->want_shading = g_value_get_boolean (value);
      break;
    }
    case ARG_VALIGN:{
      const gchar *s = g_value_get_string (value);

      if (g_ascii_strcasecmp (s, "baseline") == 0)
        overlay->valign = GST_CAIRO_TEXT_OVERLAY_VALIGN_BASELINE;
      else if (g_ascii_strcasecmp (s, "bottom") == 0)
        overlay->valign = GST_CAIRO_TEXT_OVERLAY_VALIGN_BOTTOM;
      else if (g_ascii_strcasecmp (s, "top") == 0)
        overlay->valign = GST_CAIRO_TEXT_OVERLAY_VALIGN_TOP;
      else
        g_warning ("Invalid 'valign' property value: %s", s);
      break;
    }
    case ARG_HALIGN:{
      const gchar *s = g_value_get_string (value);

      if (g_ascii_strcasecmp (s, "left") == 0)
        overlay->halign = GST_CAIRO_TEXT_OVERLAY_HALIGN_LEFT;
      else if (g_ascii_strcasecmp (s, "right") == 0)
        overlay->halign = GST_CAIRO_TEXT_OVERLAY_HALIGN_RIGHT;
      else if (g_ascii_strcasecmp (s, "center") == 0)
        overlay->halign = GST_CAIRO_TEXT_OVERLAY_HALIGN_CENTER;
      else
        g_warning ("Invalid 'halign' property value: %s", s);
      break;
    }
    case ARG_XPAD:{
      overlay->xpad = g_value_get_int (value);
      break;
    }
    case ARG_YPAD:{
      overlay->ypad = g_value_get_int (value);
      break;
    }
    case ARG_DELTAX:{
      overlay->deltax = g_value_get_int (value);
      break;
    }
    case ARG_DELTAY:{
      overlay->deltay = g_value_get_int (value);
      break;
    }
    case ARG_FONT_DESC:{
      g_free (overlay->font);
      overlay->font = g_value_dup_string (value);
      if (overlay->font == NULL)
        overlay->font = g_strdup (DEFAULT_FONT);
      gst_text_overlay_font_init (overlay);
      break;
    }
    case ARG_SILENT:
      overlay->silent = g_value_get_boolean (value);
      break;
    default:{
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
  }

  overlay->need_render = TRUE;

  GST_OBJECT_UNLOCK (overlay);
}

static void
gst_text_overlay_render_text (GstCairoTextOverlay * overlay,
    const gchar * text, gint textlen)
{
  cairo_text_extents_t extents;
  cairo_surface_t *surface;
  cairo_t *cr;
  gchar *string;
  double x, y;

  if (overlay->silent) {
    GST_DEBUG_OBJECT (overlay, "Silent mode, not rendering");
    return;
  }

  if (textlen < 0)
    textlen = strlen (text);

  if (!overlay->need_render) {
    GST_DEBUG ("Using previously rendered text.");
    g_return_if_fail (overlay->text_fill_image != NULL);
    g_return_if_fail (overlay->text_outline_image != NULL);
    return;
  }

  string = g_strndup (text, textlen);
  GST_DEBUG ("Rendering text '%s' on cairo RGBA surface", string);

  overlay->text_fill_image =
      g_realloc (overlay->text_fill_image,
      4 * overlay->width * overlay->font_height);

  surface = cairo_image_surface_create_for_data (overlay->text_fill_image,
      CAIRO_FORMAT_ARGB32, overlay->width, overlay->font_height,
      overlay->width * 4);

  cr = cairo_create (surface);

  cairo_select_font_face (cr, overlay->font, overlay->slant, overlay->weight);
  cairo_set_font_size (cr, overlay->scale);

  cairo_save (cr);
  cairo_rectangle (cr, 0, 0, overlay->width, overlay->font_height);
  cairo_set_source_rgba (cr, 0, 0, 0, 1.0);

  cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
  cairo_fill (cr);
  cairo_restore (cr);

  cairo_save (cr);
  cairo_text_extents (cr, string, &extents);
  cairo_set_source_rgba (cr, 1, 1, 1, 1.0);

  switch (overlay->halign) {
    case GST_CAIRO_TEXT_OVERLAY_HALIGN_LEFT:
      x = overlay->xpad;
      break;
    case GST_CAIRO_TEXT_OVERLAY_HALIGN_CENTER:
      x = (overlay->width - extents.width) / 2;
      break;
    case GST_CAIRO_TEXT_OVERLAY_HALIGN_RIGHT:
      x = overlay->width - extents.width - overlay->xpad;
      break;
    default:
      x = 0;
  }
  x += overlay->deltax;

  overlay->text_x0 = x;
  overlay->text_x1 = x + extents.x_advance;

  overlay->text_dy = (extents.height + extents.y_bearing);
  y = overlay->font_height - overlay->text_dy;

  cairo_move_to (cr, x, y);
  cairo_show_text (cr, string);
  cairo_restore (cr);

  cairo_destroy (cr);
  cairo_surface_destroy (surface);

  /* ----------- */

  overlay->text_outline_image =
      g_realloc (overlay->text_outline_image,
      4 * overlay->width * overlay->font_height);

  surface = cairo_image_surface_create_for_data (overlay->text_outline_image,
      CAIRO_FORMAT_ARGB32, overlay->width, overlay->font_height,
      overlay->width * 4);

  cr = cairo_create (surface);

  cairo_select_font_face (cr, overlay->font, overlay->slant, overlay->weight);
  cairo_set_font_size (cr, overlay->scale);

  cairo_save (cr);
  cairo_rectangle (cr, 0, 0, overlay->width, overlay->font_height);
  cairo_set_source_rgba (cr, 0, 0, 0, 1.0);
  cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
  cairo_fill (cr);
  cairo_restore (cr);

  cairo_save (cr);
  cairo_move_to (cr, x, y);
  cairo_set_source_rgba (cr, 1, 1, 1, 1.0);
  cairo_set_line_width (cr, 1.0);
  cairo_text_path (cr, string);
  cairo_stroke (cr);
  cairo_restore (cr);

  g_free (string);

  cairo_destroy (cr);
  cairo_surface_destroy (surface);

  overlay->need_render = FALSE;
}

static GstCaps *
gst_text_overlay_getcaps (GstPad * pad)
{
  GstCairoTextOverlay *overlay;
  GstPad *otherpad;
  GstCaps *caps;

  overlay = GST_CAIRO_TEXT_OVERLAY (gst_pad_get_parent (pad));

  if (pad == overlay->srcpad)
    otherpad = overlay->video_sinkpad;
  else
    otherpad = overlay->srcpad;

  /* we can do what the peer can */
  caps = gst_pad_peer_get_caps (otherpad);
  if (caps) {
    GstCaps *temp;
    const GstCaps *templ;

    GST_DEBUG_OBJECT (pad, "peer caps  %" GST_PTR_FORMAT, caps);

    /* filtered against our padtemplate */
    templ = gst_pad_get_pad_template_caps (otherpad);
    GST_DEBUG_OBJECT (pad, "our template  %" GST_PTR_FORMAT, templ);
    temp = gst_caps_intersect (caps, templ);
    GST_DEBUG_OBJECT (pad, "intersected %" GST_PTR_FORMAT, temp);
    gst_caps_unref (caps);
    /* this is what we can do */
    caps = temp;
  } else {
    /* no peer, our padtemplate is enough then */
    caps = gst_caps_copy (gst_pad_get_pad_template_caps (pad));
  }

  GST_DEBUG_OBJECT (overlay, "returning  %" GST_PTR_FORMAT, caps);

  gst_object_unref (overlay);

  return caps;
}

/* FIXME: upstream nego (e.g. when the video window is resized) */

static gboolean
gst_text_overlay_setcaps (GstPad * pad, GstCaps * caps)
{
  GstCairoTextOverlay *overlay;
  GstStructure *structure;
  gboolean ret = FALSE;
  const GValue *fps;

  if (!GST_PAD_IS_SINK (pad))
    return TRUE;

  g_return_val_if_fail (gst_caps_is_fixed (caps), FALSE);

  overlay = GST_CAIRO_TEXT_OVERLAY (gst_pad_get_parent (pad));

  overlay->width = 0;
  overlay->height = 0;
  structure = gst_caps_get_structure (caps, 0);
  fps = gst_structure_get_value (structure, "framerate");

  if (gst_structure_get_int (structure, "width", &overlay->width) &&
      gst_structure_get_int (structure, "height", &overlay->height) &&
      fps != NULL) {
    ret = gst_pad_set_caps (overlay->srcpad, caps);
  }

  overlay->fps_n = gst_value_get_fraction_numerator (fps);
  overlay->fps_d = gst_value_get_fraction_denominator (fps);

  gst_object_unref (overlay);

  return ret;
}

static GstPadLinkReturn
gst_text_overlay_text_pad_linked (GstPad * pad, GstPad * peer)
{
  GstCairoTextOverlay *overlay;

  overlay = GST_CAIRO_TEXT_OVERLAY (GST_PAD_PARENT (pad));

  GST_DEBUG_OBJECT (overlay, "Text pad linked");

  if (overlay->text_collect_data == NULL) {
    overlay->text_collect_data = gst_collect_pads_add_pad (overlay->collect,
        overlay->text_sinkpad, sizeof (GstCollectData));
  }

  overlay->need_render = TRUE;

  return GST_PAD_LINK_OK;
}

static void
gst_text_overlay_text_pad_unlinked (GstPad * pad)
{
  GstCairoTextOverlay *overlay;

  /* don't use gst_pad_get_parent() here, will deadlock */
  overlay = GST_CAIRO_TEXT_OVERLAY (GST_PAD_PARENT (pad));

  GST_DEBUG_OBJECT (overlay, "Text pad unlinked");

  if (overlay->text_collect_data) {
    gst_collect_pads_remove_pad (overlay->collect, overlay->text_sinkpad);
    overlay->text_collect_data = NULL;
  }

  overlay->need_render = TRUE;
}

#define BOX_SHADING_VAL -80
#define BOX_XPAD         6
#define BOX_YPAD         6

static inline void
gst_text_overlay_shade_y (GstCairoTextOverlay * overlay, guchar * dest,
    guint dest_stride, gint y0, gint y1)
{
  gint i, j, x0, x1;

  x0 = CLAMP (overlay->text_x0 - BOX_XPAD, 0, overlay->width);
  x1 = CLAMP (overlay->text_x1 + BOX_XPAD, 0, overlay->width);

  y0 = CLAMP (y0 - BOX_YPAD, 0, overlay->height);
  y1 = CLAMP (y1 + BOX_YPAD, 0, overlay->height);

  for (i = y0; i < y1; ++i) {
    for (j = x0; j < x1; ++j) {
      gint y = dest[(i * dest_stride) + j] + BOX_SHADING_VAL;

      dest[(i * dest_stride) + j] = CLAMP (y, 0, 255);
    }
  }
}

static inline void
gst_text_overlay_blit_1 (GstCairoTextOverlay * overlay, guchar * dest,
    guchar * text_image, gint val, guint dest_stride, gint y0)
{
  gint i, j;
  gint x, a, y;
  gint y1;

  y = val;
  y0 = MIN (y0, overlay->height);
  y1 = MIN (y0 + overlay->font_height, overlay->height);

  for (i = y0; i < y1; i++) {
    for (j = 0; j < overlay->width; j++) {
      x = dest[i * dest_stride + j];
      a = text_image[4 * ((i - y0) * overlay->width + j) + 1];
      dest[i * dest_stride + j] = (y * a + x * (255 - a)) / 255;
    }
  }
}

static inline void
gst_text_overlay_blit_sub2x2 (GstCairoTextOverlay * overlay, guchar * dest,
    guchar * text_image, gint val, guint dest_stride, gint y0)
{
  gint i, j;
  gint x, a, y;
  gint y1;

  y0 = MIN (y0, overlay->height);
  y1 = MIN (y0 + overlay->font_height, overlay->height);

  y = val;

  for (i = y0; i < y1; i += 2) {
    for (j = 0; j < overlay->width; j += 2) {
      x = dest[(i / 2) * dest_stride + j / 2];
      a = (text_image[4 * ((i - y0) * overlay->width + j) + 1] +
          text_image[4 * ((i - y0) * overlay->width + j + 1) + 1] +
          text_image[4 * ((i - y0 + 1) * overlay->width + j) + 1] +
          text_image[4 * ((i - y0 + 1) * overlay->width + j + 1) + 1] + 2) / 4;
      dest[(i / 2) * dest_stride + j / 2] = (y * a + x * (255 - a)) / 255;
    }
  }
}


static GstFlowReturn
gst_text_overlay_push_frame (GstCairoTextOverlay * overlay,
    GstBuffer * video_frame)
{
  guchar *y, *u, *v;
  gint ypos;

  video_frame = gst_buffer_make_writable (video_frame);

  switch (overlay->valign) {
    case GST_CAIRO_TEXT_OVERLAY_VALIGN_BOTTOM:
      ypos = overlay->height - overlay->font_height - overlay->ypad;
      break;
    case GST_CAIRO_TEXT_OVERLAY_VALIGN_BASELINE:
      ypos = overlay->height - (overlay->font_height - overlay->text_dy)
          - overlay->ypad;
      break;
    case GST_CAIRO_TEXT_OVERLAY_VALIGN_TOP:
      ypos = overlay->ypad;
      break;
    default:
      ypos = overlay->ypad;
      break;
  }

  ypos += overlay->deltay;

  y = GST_BUFFER_DATA (video_frame);
  u = y + I420_U_OFFSET (overlay->width, overlay->height);
  v = y + I420_V_OFFSET (overlay->width, overlay->height);

  /* shaded background box */
  if (overlay->want_shading) {
    gst_text_overlay_shade_y (overlay,
        y, I420_Y_ROWSTRIDE (overlay->width),
        ypos + overlay->text_dy, ypos + overlay->font_height);
  }

  /* blit outline text on video image */
  gst_text_overlay_blit_1 (overlay,
      y,
      overlay->text_outline_image, 0, I420_Y_ROWSTRIDE (overlay->width), ypos);
  gst_text_overlay_blit_sub2x2 (overlay,
      u,
      overlay->text_outline_image, 128, I420_U_ROWSTRIDE (overlay->width),
      ypos);
  gst_text_overlay_blit_sub2x2 (overlay, v, overlay->text_outline_image, 128,
      I420_V_ROWSTRIDE (overlay->width), ypos);

  /* blit text on video image */
  gst_text_overlay_blit_1 (overlay,
      y,
      overlay->text_fill_image, 255, I420_Y_ROWSTRIDE (overlay->width), ypos);
  gst_text_overlay_blit_sub2x2 (overlay,
      u,
      overlay->text_fill_image, 128, I420_U_ROWSTRIDE (overlay->width), ypos);
  gst_text_overlay_blit_sub2x2 (overlay,
      v,
      overlay->text_fill_image, 128, I420_V_ROWSTRIDE (overlay->width), ypos);

  return gst_pad_push (overlay->srcpad, video_frame);
}

static void
gst_text_overlay_pop_video (GstCairoTextOverlay * overlay)
{
  GstBuffer *buf;

  buf = gst_collect_pads_pop (overlay->collect, overlay->video_collect_data);
  g_return_if_fail (buf != NULL);
  gst_buffer_unref (buf);
}

static void
gst_text_overlay_pop_text (GstCairoTextOverlay * overlay)
{
  GstBuffer *buf;

  if (overlay->text_collect_data) {
    buf = gst_collect_pads_pop (overlay->collect, overlay->text_collect_data);
    g_return_if_fail (buf != NULL);
    gst_buffer_unref (buf);
  }

  overlay->need_render = TRUE;
}

/* This function is called when there is data on all pads */
static GstFlowReturn
gst_text_overlay_collected (GstCollectPads * pads, gpointer data)
{
  GstCairoTextOverlay *overlay;
  GstFlowReturn ret = GST_FLOW_OK;
  GstClockTime now, txt_end, frame_end;
  GstBuffer *video_frame = NULL;
  GstBuffer *text_buf = NULL;
  gchar *text;
  gint text_len;

  overlay = GST_CAIRO_TEXT_OVERLAY (data);

  GST_DEBUG ("Collecting");

  video_frame = gst_collect_pads_peek (overlay->collect,
      overlay->video_collect_data);

  /* send EOS if video stream EOSed regardless of text stream */
  if (video_frame == NULL) {
    GST_DEBUG ("Video stream at EOS");
    if (overlay->text_collect_data) {
      text_buf = gst_collect_pads_pop (overlay->collect,
          overlay->text_collect_data);
    }
    gst_pad_push_event (overlay->srcpad, gst_event_new_eos ());
    ret = GST_FLOW_UNEXPECTED;
    goto done;
  }

  if (GST_BUFFER_TIMESTAMP (video_frame) == GST_CLOCK_TIME_NONE) {
    g_warning ("%s: video frame has invalid timestamp", G_STRLOC);
  }

  now = GST_BUFFER_TIMESTAMP (video_frame);

  if (GST_BUFFER_DURATION (video_frame) != GST_CLOCK_TIME_NONE) {
    frame_end = now + GST_BUFFER_DURATION (video_frame);
  } else if (overlay->fps_n > 0) {
    frame_end = now + gst_util_uint64_scale_int (GST_SECOND,
        overlay->fps_d, overlay->fps_n);
  } else {
    /* magic value, does not really matter since texts
     * tend to span quite a few frames in practice anyway */
    frame_end = now + GST_SECOND / 25;
  }

  GST_DEBUG ("Got video frame: %" GST_TIME_FORMAT " - %" GST_TIME_FORMAT,
      GST_TIME_ARGS (now), GST_TIME_ARGS (frame_end));

  /* text pad not linked? */
  if (overlay->text_collect_data == NULL) {
    GST_DEBUG ("Text pad not linked, rendering default text: '%s'",
        GST_STR_NULL (overlay->default_text));
    if (overlay->default_text && *overlay->default_text != '\0') {
      gst_text_overlay_render_text (overlay, overlay->default_text, -1);
      ret = gst_text_overlay_push_frame (overlay, video_frame);
    } else {
      ret = gst_pad_push (overlay->srcpad, video_frame);
    }
    gst_text_overlay_pop_video (overlay);
    video_frame = NULL;
    goto done;
  }

  text_buf = gst_collect_pads_peek (overlay->collect,
      overlay->text_collect_data);

  /* just push the video frame if the text stream has EOSed */
  if (text_buf == NULL) {
    GST_DEBUG ("Text pad EOSed, just pushing video frame as is");
    ret = gst_pad_push (overlay->srcpad, video_frame);
    gst_text_overlay_pop_video (overlay);
    video_frame = NULL;
    goto done;
  }

  /* if the text buffer isn't stamped right, pop it off the
   *  queue and display it for the current video frame only */
  if (GST_BUFFER_TIMESTAMP (text_buf) == GST_CLOCK_TIME_NONE ||
      GST_BUFFER_DURATION (text_buf) == GST_CLOCK_TIME_NONE) {
    GST_WARNING ("Got text buffer with invalid time stamp or duration");
    gst_text_overlay_pop_text (overlay);
    GST_BUFFER_TIMESTAMP (text_buf) = now;
    GST_BUFFER_DURATION (text_buf) = frame_end - now;
  }

  txt_end = GST_BUFFER_TIMESTAMP (text_buf) + GST_BUFFER_DURATION (text_buf);

  GST_DEBUG ("Got text buffer: %" GST_TIME_FORMAT " - %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (text_buf)), GST_TIME_ARGS (txt_end));

  /* if the text buffer is too old, pop it off the
   * queue and return so we get a new one next time */
  if (txt_end < now) {
    GST_DEBUG ("Text buffer too old, popping off the queue");
    gst_text_overlay_pop_text (overlay);
    ret = GST_FLOW_OK;
    goto done;
  }

  /* if the video frame ends before the text even starts,
   * just push it out as is and pop it off the queue */
  if (frame_end < GST_BUFFER_TIMESTAMP (text_buf)) {
    GST_DEBUG ("Video buffer before text, pushing out and popping off queue");
    ret = gst_pad_push (overlay->srcpad, video_frame);
    gst_text_overlay_pop_video (overlay);
    video_frame = NULL;
    goto done;
  }

  /* text duration overlaps video frame duration */
  text = g_strndup ((gchar *) GST_BUFFER_DATA (text_buf),
      GST_BUFFER_SIZE (text_buf));
  g_strdelimit (text, "\n\r\t", ' ');
  text_len = strlen (text);

  if (text_len > 0) {
    GST_DEBUG ("Rendering text '%*s'", text_len, text);;
    gst_text_overlay_render_text (overlay, text, text_len);
  } else {
    GST_DEBUG ("No text to render (empty buffer)");
    gst_text_overlay_render_text (overlay, " ", 1);
  }

  g_free (text);

  gst_text_overlay_pop_video (overlay);
  ret = gst_text_overlay_push_frame (overlay, video_frame);
  video_frame = NULL;
  goto done;

done:
  {
    if (text_buf)
      gst_buffer_unref (text_buf);

    if (video_frame)
      gst_buffer_unref (video_frame);

    return ret;
  }
}

static gboolean
gst_text_overlay_src_event (GstPad * pad, GstEvent * event)
{
  GstCairoTextOverlay *overlay =
      GST_CAIRO_TEXT_OVERLAY (gst_pad_get_parent (pad));
  gboolean ret = TRUE;

  /* forward events to the video sink, and, if it is linked, the text sink */
  if (overlay->text_collect_data) {
    gst_event_ref (event);
    ret &= gst_pad_push_event (overlay->text_sinkpad, event);
  }
  ret &= gst_pad_push_event (overlay->video_sinkpad, event);

  gst_object_unref (overlay);
  return ret;
}

static gboolean
gst_text_overlay_video_event (GstPad * pad, GstEvent * event)
{
  gboolean ret = FALSE;
  GstCairoTextOverlay *overlay = NULL;

  overlay = GST_CAIRO_TEXT_OVERLAY (gst_pad_get_parent (pad));

  if (GST_EVENT_TYPE (event) == GST_EVENT_NEWSEGMENT) {
    GST_DEBUG_OBJECT (overlay,
        "received new segment on video sink pad, forwarding");
    gst_event_ref (event);
    gst_pad_push_event (overlay->srcpad, event);
  }

  /* now GstCollectPads can take care of the rest, e.g. EOS */
  ret = overlay->collect_event (pad, event);
  gst_object_unref (overlay);
  return ret;
}

static GstStateChangeReturn
gst_text_overlay_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstCairoTextOverlay *overlay = GST_CAIRO_TEXT_OVERLAY (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_collect_pads_start (overlay->collect);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      /* need to unblock the collectpads before calling the
       * parent change_state so that streaming can finish */
      gst_collect_pads_stop (overlay->collect);
      break;
    default:
      break;
  }

  ret = parent_class->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    default:
      break;
  }

  return ret;
}
