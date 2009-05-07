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
 * SECTION:element-textrender
 * @see_also: #GstTextOverlay
 *
 * This plugin renders text received on the text sink pad to a video
 * buffer (retaining the alpha channel), so it can later be overlayed
 * on top of video streams using other elements.
 *
 * The text can contain newline characters. (FIXME: What about text 
 * wrapping? It does not make sense in this context)
 *
 * <refsect2>
 * <title>Example launch lines</title>
 * |[
 * gst-launch -v filesrc location=subtitles.srt ! subparse ! textrender ! xvimagesink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gst/gst.h>
#include <gst/video/video.h>

#include "gsttextrender.h"

GST_DEBUG_CATEGORY_EXTERN (pango_debug);
#define GST_CAT_DEFAULT pango_debug

static const GstElementDetails text_render_details =
GST_ELEMENT_DETAILS ("Text renderer",
    "Filter/Editor/Video",
    "Renders a text string to an image bitmap",
    "David Schleef <ds@schleef.org>, "
    "Ronald S. Bultje <rbultje@ronald.bitfreak.net>");

#define DEFAULT_PROP_VALIGNMENT GST_TEXT_RENDER_VALIGN_BASELINE
#define DEFAULT_PROP_HALIGNMENT GST_TEXT_RENDER_HALIGN_CENTER
#define DEFAULT_PROP_LINE_ALIGNMENT GST_TEXT_RENDER_LINE_ALIGN_CENTER
#define DEFAULT_PROP_XPAD       25
#define DEFAULT_PROP_YPAD       25

#define DEFAULT_RENDER_WIDTH 720
#define DEFAULT_RENDER_HEIGHT 576

enum
{
  ARG_0,
  PROP_HALIGNMENT,
  PROP_VALIGNMENT,
  PROP_LINE_ALIGNMENT,
  PROP_XPAD,
  PROP_YPAD,
  ARG_FONT_DESC
};


static GstStaticPadTemplate src_template_factory =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-yuv, format = (fourcc) AYUV; "
        "video/x-raw-rgb, "
        "bpp = (int) 32, endianness = (int) 4321, red_mask = (int) 16711680, "
        "green_mask = (int) 65280, blue_mask = (int) 255, "
        " alpha_mask = (int) -16777216, depth = (int) 32")
    );

static GstStaticPadTemplate sink_template_factory =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("text/x-pango-markup; text/plain")
    );

#define GST_TYPE_TEXT_RENDER_VALIGN (gst_text_render_valign_get_type())
static GType
gst_text_render_valign_get_type (void)
{
  static GType text_render_valign_type = 0;
  static const GEnumValue text_render_valign[] = {
    {GST_TEXT_RENDER_VALIGN_BASELINE, "baseline", "baseline"},
    {GST_TEXT_RENDER_VALIGN_BOTTOM, "bottom", "bottom"},
    {GST_TEXT_RENDER_VALIGN_TOP, "top", "top"},
    {0, NULL, NULL},
  };

  if (!text_render_valign_type) {
    text_render_valign_type =
        g_enum_register_static ("GstTextRenderVAlign", text_render_valign);
  }
  return text_render_valign_type;
}

#define GST_TYPE_TEXT_RENDER_HALIGN (gst_text_render_halign_get_type())
static GType
gst_text_render_halign_get_type (void)
{
  static GType text_render_halign_type = 0;
  static const GEnumValue text_render_halign[] = {
    {GST_TEXT_RENDER_HALIGN_LEFT, "left", "left"},
    {GST_TEXT_RENDER_HALIGN_CENTER, "center", "center"},
    {GST_TEXT_RENDER_HALIGN_RIGHT, "right", "right"},
    {0, NULL, NULL},
  };

  if (!text_render_halign_type) {
    text_render_halign_type =
        g_enum_register_static ("GstTextRenderHAlign", text_render_halign);
  }
  return text_render_halign_type;
}

#define GST_TYPE_TEXT_RENDER_LINE_ALIGN (gst_text_render_line_align_get_type())
static GType
gst_text_render_line_align_get_type (void)
{
  static GType text_render_line_align_type = 0;
  static const GEnumValue text_render_line_align[] = {
    {GST_TEXT_RENDER_LINE_ALIGN_LEFT, "left", "left"},
    {GST_TEXT_RENDER_LINE_ALIGN_CENTER, "center", "center"},
    {GST_TEXT_RENDER_LINE_ALIGN_RIGHT, "right", "right"},
    {0, NULL, NULL}
  };

  if (!text_render_line_align_type) {
    text_render_line_align_type =
        g_enum_register_static ("GstTextRenderLineAlign",
        text_render_line_align);
  }
  return text_render_line_align_type;
}

GST_BOILERPLATE (GstTextRender, gst_text_render, GstElement, GST_TYPE_ELEMENT);

static void gst_text_render_finalize (GObject * object);
static void gst_text_render_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_text_render_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static void
gst_text_render_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template_factory));

  gst_element_class_set_details (element_class, &text_render_details);
}

static void
gst_text_render_class_init (GstTextRenderClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = gst_text_render_finalize;
  gobject_class->set_property = gst_text_render_set_property;
  gobject_class->get_property = gst_text_render_get_property;

  klass->pango_context = pango_ft2_get_context (72, 72);
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_FONT_DESC,
      g_param_spec_string ("font-desc", "font description",
          "Pango font description of font "
          "to be used for rendering. "
          "See documentation of "
          "pango_font_description_from_string"
          " for syntax.", "", G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_VALIGNMENT,
      g_param_spec_enum ("valignment", "vertical alignment",
          "Vertical alignment of the text", GST_TYPE_TEXT_RENDER_VALIGN,
          DEFAULT_PROP_VALIGNMENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_HALIGNMENT,
      g_param_spec_enum ("halignment", "horizontal alignment",
          "Horizontal alignment of the text", GST_TYPE_TEXT_RENDER_HALIGN,
          DEFAULT_PROP_HALIGNMENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_XPAD,
      g_param_spec_int ("xpad", "horizontal paddding",
          "Horizontal paddding when using left/right alignment", 0, G_MAXINT,
          DEFAULT_PROP_XPAD, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_YPAD,
      g_param_spec_int ("ypad", "vertical padding",
          "Vertical padding when using top/bottom alignment", 0, G_MAXINT,
          DEFAULT_PROP_YPAD, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_LINE_ALIGNMENT,
      g_param_spec_enum ("line-alignment", "line alignment",
          "Alignment of text lines relative to each other.",
          GST_TYPE_TEXT_RENDER_LINE_ALIGN, DEFAULT_PROP_LINE_ALIGNMENT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}


static void
resize_bitmap (GstTextRender * render, gint width, gint height)
{
  FT_Bitmap *bitmap = &render->bitmap;
  gint pitch = (width | 3) + 1;
  gint size = pitch * height;

  /* no need to keep reallocating; just keep the maximum size so far */
  if (size <= render->bitmap_buffer_size) {
    bitmap->rows = height;
    bitmap->width = width;
    bitmap->pitch = pitch;
    memset (bitmap->buffer, 0, render->bitmap_buffer_size);
    return;
  }
  if (!bitmap->buffer) {
    /* initialize */
    bitmap->pixel_mode = ft_pixel_mode_grays;
    bitmap->num_grays = 256;
  }
  if (bitmap->buffer)
    bitmap->buffer = g_realloc (bitmap->buffer, size);
  else
    bitmap->buffer = g_malloc (size);
  bitmap->rows = height;
  bitmap->width = width;
  bitmap->pitch = pitch;
  memset (bitmap->buffer, 0, size);
  render->bitmap_buffer_size = size;
}

static void
gst_text_render_render_text (GstTextRender * render)
{
  PangoRectangle ink_rect, logical_rect;

  pango_layout_get_pixel_extents (render->layout, &ink_rect, &logical_rect);
  resize_bitmap (render, ink_rect.width, ink_rect.height + ink_rect.y);
  pango_ft2_render_layout (&render->bitmap, render->layout, -ink_rect.x, 0);
  render->baseline_y = ink_rect.y;
}

static void
gst_text_render_check_argb (GstTextRender * render)
{
  GstCaps *peer_caps;
  peer_caps = gst_pad_get_allowed_caps (render->srcpad);
  if (G_LIKELY (peer_caps)) {
    guint i = 0, n = 0;

    n = gst_caps_get_size (peer_caps);
    GST_DEBUG_OBJECT (render, "peer allowed caps (%u structure(s)) are %"
        GST_PTR_FORMAT, n, peer_caps);

    for (i = 0; i < n; i++) {
      GstStructure *s = gst_caps_get_structure (peer_caps, i);
      /* Check if the peer pad support ARGB format, if yes change caps */
      if (gst_structure_has_name (s, "video/x-raw-rgb") &&
          gst_structure_has_field (s, "alpha_mask")) {
        render->use_ARGB = TRUE;
      }
    }
    gst_caps_unref (peer_caps);
    render->check_ARGB = TRUE;
  }
}

static gboolean
gst_text_render_setcaps (GstPad * pad, GstCaps * caps)
{
  GstTextRender *render = GST_TEXT_RENDER (gst_pad_get_parent (pad));
  GstStructure *structure;
  gboolean ret = FALSE;
  gint width = 0, height = 0;

  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_int (structure, "width", &width);
  gst_structure_get_int (structure, "height", &height);

  GST_DEBUG ("Got caps %" GST_PTR_FORMAT, caps);

  if (width >= render->bitmap.width && height >= render->bitmap.rows) {
    render->width = width;
    render->height = height;
    ret = TRUE;
  }

  gst_text_render_check_argb (render);

  gst_object_unref (render);
  return ret;
}

static void
gst_text_render_fixate_caps (GstPad * pad, GstCaps * caps)
{
  GstTextRender *render = GST_TEXT_RENDER (gst_pad_get_parent (pad));
  GstStructure *s = gst_caps_get_structure (caps, 0);

  GST_DEBUG ("Fixating caps %" GST_PTR_FORMAT, caps);
  gst_structure_fixate_field_nearest_int (s, "width", render->width);
  gst_structure_fixate_field_nearest_int (s, "height", render->height);
  GST_DEBUG ("Fixated to    %" GST_PTR_FORMAT, caps);

  gst_object_unref (render);
}

static void
gst_text_renderer_bitmap_to_ayuv (GstTextRender * render, FT_Bitmap * bitmap,
    guchar * pixbuf, gint x0, gint x1, gint y0, gint y1)
{
  int y;                        /* text bitmap coordinates */
  int rowinc, bit_rowinc;
  guchar *p, *bitp;
  guchar v;

  x0 = CLAMP (x0, 0, render->width);
  x1 = CLAMP (x1, 0, render->width);

  y0 = CLAMP (y0, 0, render->height);
  y1 = CLAMP (y1, 0, render->height);


  rowinc = render->width - bitmap->width;
  bit_rowinc = bitmap->pitch - bitmap->width;

  bitp = bitmap->buffer;
  p = pixbuf + ((x0 + (render->width * y0)) * 4);

  for (y = y0; y < y1; y++) {
    int n;

    for (n = x0; n < x1; n++) {
      v = *bitp;
      if (v) {
        p[0] = v;
        p[1] = 255;
        p[2] = 0x80;
        p[3] = 0x80;
      }
      p += 4;
      bitp++;
    }
    p += rowinc * 4;
    bitp += bit_rowinc;
  }
}

static void
gst_text_renderer_bitmap_to_argb (GstTextRender * render, FT_Bitmap * bitmap,
    guchar * pixbuf, gint x0, gint x1, gint y0, gint y1)
{
  int y;                        /* text bitmap coordinates */
  int rowinc, bit_rowinc;
  guchar *p, *bitp;
  guchar v;

  x0 = CLAMP (x0, 0, render->width);
  x1 = CLAMP (x1, 0, render->width);

  y0 = CLAMP (y0, 0, render->height);
  y1 = CLAMP (y1, 0, render->height);


  rowinc = render->width - bitmap->width;
  bit_rowinc = bitmap->pitch - bitmap->width;

  bitp = bitmap->buffer;
  p = pixbuf + ((x0 + (render->width * y0)) * 4);

  for (y = y0; y < y1; y++) {
    int n;

    for (n = x0; n < x1; n++) {
      v = *bitp;
      if (v) {
        p[0] = v;
        p[1] = 255;
        p[2] = 255;
        p[3] = 255;
      }
      p += 4;
      bitp++;
    }
    p += rowinc * 4;
    bitp += bit_rowinc;
  }
}

static GstFlowReturn
gst_text_render_chain (GstPad * pad, GstBuffer * inbuf)
{
  GstTextRender *render;
  GstFlowReturn ret;
  GstBuffer *outbuf;
  GstCaps *caps = NULL;
  guint8 *data = GST_BUFFER_DATA (inbuf);
  guint size = GST_BUFFER_SIZE (inbuf);
  gint n;
  gint xpos, ypos;

  render = GST_TEXT_RENDER (gst_pad_get_parent (pad));

  /* somehow pango barfs over "\0" buffers... */
  while (size > 0 &&
      (data[size - 1] == '\r' ||
          data[size - 1] == '\n' || data[size - 1] == '\0')) {
    size--;
  }

  /* render text */
  GST_DEBUG ("rendering '%*s'", size, data);
  pango_layout_set_markup (render->layout, (gchar *) data, size);
  gst_text_render_render_text (render);

  if (G_UNLIKELY (!render->check_ARGB)) {
    gst_text_render_check_argb (render);
  }

  if (!render->use_ARGB) {
    caps = gst_caps_new_simple ("video/x-raw-yuv", "format", GST_TYPE_FOURCC,
        GST_MAKE_FOURCC ('A', 'Y', 'U', 'V'), "width", G_TYPE_INT,
        render->width, "height", G_TYPE_INT, render->height,
        "framerate", GST_TYPE_FRACTION, 1, 1, NULL);
  } else {
    caps = gst_caps_new_simple ("video/x-raw-rgb",
        "width", G_TYPE_INT, render->width,
        "height", G_TYPE_INT, render->height,
        "framerate", GST_TYPE_FRACTION, 0, 1,
        "bpp", G_TYPE_INT, 32,
        "depth", G_TYPE_INT, 32,
        "red_mask", G_TYPE_INT, 16711680,
        "green_mask", G_TYPE_INT, 65280,
        "blue_mask", G_TYPE_INT, 255,
        "alpha_mask", G_TYPE_INT, -16777216,
        "endianness", G_TYPE_INT, G_BIG_ENDIAN, NULL);
  }

  if (!gst_pad_set_caps (render->srcpad, caps)) {
    gst_caps_unref (caps);
    GST_ELEMENT_ERROR (render, CORE, NEGOTIATION, (NULL), (NULL));
    ret = GST_FLOW_ERROR;
    goto done;
  }

  GST_DEBUG ("Allocating buffer WxH = %dx%d", render->width, render->height);
  ret =
      gst_pad_alloc_buffer_and_set_caps (render->srcpad, GST_BUFFER_OFFSET_NONE,
      render->width * render->height * 4, caps, &outbuf);

  if (ret != GST_FLOW_OK)
    goto done;

  gst_buffer_copy_metadata (outbuf, inbuf, GST_BUFFER_COPY_TIMESTAMPS);
  data = GST_BUFFER_DATA (outbuf);

  for (n = 0; n < render->width * render->height; n++) {
    data[n * 4] = 0;
    data[n * 4 + 1] = 0;
    data[n * 4 + 2] = data[n * 4 + 3] = 128;
  }

  switch (render->halign) {
    case GST_TEXT_RENDER_HALIGN_LEFT:
      xpos = render->xpad;
      break;
    case GST_TEXT_RENDER_HALIGN_CENTER:
      xpos = (render->width - render->bitmap.width) / 2;
      break;
    case GST_TEXT_RENDER_HALIGN_RIGHT:
      xpos = render->width - render->bitmap.width - render->xpad;
      break;
    default:
      xpos = 0;
  }

  switch (render->valign) {
    case GST_TEXT_RENDER_VALIGN_BOTTOM:
      ypos = render->height - render->bitmap.rows - render->ypad;
      break;
    case GST_TEXT_RENDER_VALIGN_BASELINE:
      ypos = render->height - (render->bitmap.rows + render->ypad);
      break;
    case GST_TEXT_RENDER_VALIGN_TOP:
      ypos = render->ypad;
      break;
    default:
      ypos = render->ypad;
      break;
  }

  if (render->bitmap.buffer) {
    if (render->use_ARGB) {
      gst_text_renderer_bitmap_to_argb (render, &render->bitmap, data, xpos,
          xpos + render->bitmap.width, ypos, ypos + render->bitmap.rows);
    } else {
      gst_text_renderer_bitmap_to_ayuv (render, &render->bitmap, data, xpos,
          xpos + render->bitmap.width, ypos, ypos + render->bitmap.rows);
    }
  }

  ret = gst_pad_push (render->srcpad, outbuf);

done:
  if (caps)
    gst_caps_unref (caps);
  gst_buffer_unref (inbuf);
  gst_object_unref (render);
  return ret;
}

static void
gst_text_render_finalize (GObject * object)
{
  GstTextRender *render = GST_TEXT_RENDER (object);

  g_free (render->bitmap.buffer);

  if (render->layout)
    g_object_unref (render->layout);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_text_render_init (GstTextRender * render, GstTextRenderClass * klass)
{
  GstPadTemplate *template;

  /* sink */
  template = gst_static_pad_template_get (&sink_template_factory);
  render->sinkpad = gst_pad_new_from_template (template, "sink");
  gst_object_unref (template);
  gst_pad_set_chain_function (render->sinkpad,
      GST_DEBUG_FUNCPTR (gst_text_render_chain));
  gst_element_add_pad (GST_ELEMENT (render), render->sinkpad);

  /* source */
  template = gst_static_pad_template_get (&src_template_factory);
  render->srcpad = gst_pad_new_from_template (template, "src");
  gst_object_unref (template);
  gst_pad_set_fixatecaps_function (render->srcpad,
      GST_DEBUG_FUNCPTR (gst_text_render_fixate_caps));
  gst_pad_set_setcaps_function (render->srcpad,
      GST_DEBUG_FUNCPTR (gst_text_render_setcaps));

  gst_element_add_pad (GST_ELEMENT (render), render->srcpad);

  render->line_align = DEFAULT_PROP_LINE_ALIGNMENT;
  render->layout =
      pango_layout_new (GST_TEXT_RENDER_GET_CLASS (render)->pango_context);
  pango_layout_set_alignment (render->layout,
      (PangoAlignment) render->line_align);
  memset (&render->bitmap, 0, sizeof (render->bitmap));

  render->halign = DEFAULT_PROP_HALIGNMENT;
  render->valign = DEFAULT_PROP_VALIGNMENT;
  render->xpad = DEFAULT_PROP_XPAD;
  render->ypad = DEFAULT_PROP_YPAD;

  render->width = DEFAULT_RENDER_WIDTH;
  render->height = DEFAULT_RENDER_HEIGHT;

  render->use_ARGB = FALSE;
  render->check_ARGB = FALSE;
}

static void
gst_text_render_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstTextRender *render = GST_TEXT_RENDER (object);

  switch (prop_id) {
    case PROP_VALIGNMENT:
      render->valign = g_value_get_enum (value);
      break;
    case PROP_HALIGNMENT:
      render->halign = g_value_get_enum (value);
      break;
    case PROP_LINE_ALIGNMENT:
      render->line_align = g_value_get_enum (value);
      pango_layout_set_alignment (render->layout,
          (PangoAlignment) render->line_align);
      break;
    case PROP_XPAD:
      render->xpad = g_value_get_int (value);
      break;
    case PROP_YPAD:
      render->ypad = g_value_get_int (value);
      break;
    case ARG_FONT_DESC:
    {
      PangoFontDescription *desc;

      desc = pango_font_description_from_string (g_value_get_string (value));
      if (desc) {
        GST_LOG ("font description set: %s", g_value_get_string (value));
        GST_OBJECT_LOCK (render);
        pango_layout_set_font_description (render->layout, desc);
        pango_font_description_free (desc);
        gst_text_render_render_text (render);
        GST_OBJECT_UNLOCK (render);
      } else {
        GST_WARNING ("font description parse failed: %s",
            g_value_get_string (value));
      }
      break;
    }

    default:
      break;
  }
}

static void
gst_text_render_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstTextRender *render = GST_TEXT_RENDER (object);

  switch (prop_id) {
    case PROP_VALIGNMENT:
      g_value_set_enum (value, render->valign);
      break;
    case PROP_HALIGNMENT:
      g_value_set_enum (value, render->halign);
      break;
    case PROP_LINE_ALIGNMENT:
      g_value_set_enum (value, render->line_align);
      break;
    case PROP_XPAD:
      g_value_set_int (value, render->xpad);
      break;
    case PROP_YPAD:
      g_value_set_int (value, render->ypad);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
