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
 * <refsect2>
 * <para>
 * This plugin renders text received on the text sink pad to a video
 * buffer (retaining the alpha channel), so it can later be overlayed
 * on top of video streams using other elements.
 * </para>
 * <para>
 * The text can contain newline characters. (FIXME: What about text 
 * wrapping? It does not make sense in this context)
 * </para>
 * <para>
 * Example pipeline:
 * <programlisting>
 * gst-launch -v filesrc location=subtitles.srt ! subparse ! textrender ! xvimagesink
 * </programlisting>
 * </para>
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

enum
{
  ARG_0,
  ARG_FONT_DESC
};


static GstStaticPadTemplate src_template_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("AYUV"))
    );

static GstStaticPadTemplate sink_template_factory =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("text/x-pango-markup; text/plain")
    );

GST_BOILERPLATE (GstTextRender, gst_text_render, GstElement, GST_TYPE_ELEMENT)

     static void gst_text_render_finalize (GObject * object);
     static void gst_text_render_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);

     static void gst_text_render_base_init (gpointer g_class)
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

  klass->pango_context = pango_ft2_get_context (72, 72);
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_FONT_DESC,
      g_param_spec_string ("font-desc", "font description",
          "Pango font description of font "
          "to be used for rendering. "
          "See documentation of "
          "pango_font_description_from_string"
          " for syntax.", "", G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));
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

  gst_object_unref (render);
  return ret;
}

static void
gst_text_render_fixate_caps (GstPad * pad, GstCaps * caps)
{
  GstTextRender *render = GST_TEXT_RENDER (gst_pad_get_parent (pad));
  GstStructure *s = gst_caps_get_structure (caps, 0);

  GST_DEBUG ("Fixating caps %" GST_PTR_FORMAT, caps);
  gst_structure_fixate_field_nearest_int (s, "width", render->bitmap.width);
  gst_structure_fixate_field_nearest_int (s, "height", render->bitmap.rows);
  GST_DEBUG ("Fixated to    %" GST_PTR_FORMAT, caps);

  gst_object_unref (render);
}

static void
gst_text_renderer_bitmap_to_ayuv (GstTextRender * render, FT_Bitmap * bitmap,
    guchar * pixbuf)
{
  int y;                        /* text bitmap coordinates */
  int rowinc, bit_rowinc;
  guchar *p, *bitp;
  guchar v;

  rowinc = render->width - bitmap->width;
  bit_rowinc = bitmap->pitch - bitmap->width;

  bitp = bitmap->buffer;
  p = pixbuf;

  for (y = 0; y < bitmap->rows; y++) {
    int n;

    for (n = bitmap->width; n > 0; --n) {
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

  caps = gst_caps_new_simple ("video/x-raw-yuv", "format", GST_TYPE_FOURCC,
      GST_MAKE_FOURCC ('A', 'Y', 'U', 'V'), "width", G_TYPE_INT,
      render->bitmap.width, "height", G_TYPE_INT, render->bitmap.rows,
      "framerate", GST_TYPE_FRACTION, 1, 1, NULL);

  if (!gst_pad_set_caps (render->srcpad, caps)) {
    gst_caps_unref (caps);
    GST_ELEMENT_ERROR (render, CORE, NEGOTIATION, (NULL), (NULL));
    ret = GST_FLOW_ERROR;
    goto done;
  }

  GST_DEBUG ("Allocating AYUV buffer WxH = %dx%d", render->width,
      render->height);
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

  if (render->bitmap.buffer) {
    gst_text_renderer_bitmap_to_ayuv (render, &render->bitmap, data);
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

  render->layout =
      pango_layout_new (GST_TEXT_RENDER_GET_CLASS (render)->pango_context);
  memset (&render->bitmap, 0, sizeof (render->bitmap));
}

static void
gst_text_render_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstTextRender *render = GST_TEXT_RENDER (object);

  switch (prop_id) {
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
