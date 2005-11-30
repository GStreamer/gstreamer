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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gst/video/video.h>

#include "gsttextoverlay.h"
#include "gsttimeoverlay.h"
#include "gstclockoverlay.h"
#include "gsttextrender.h"

/* FIXME:
 *  - use proper strides and offset for I420
 *  - if text is wider than the video picture, it does not get
 *    clipped properly during blitting (if wrapping is disabled)
 *  - make 'shading_value' a property (or enum:  light/normal/dark/verydark)?
 */

GST_DEBUG_CATEGORY (pango_debug);
#define GST_CAT_DEFAULT pango_debug

static GstElementDetails text_overlay_details = {
  "Text Overlay",
  "Filter/Editor/Video",
  "Adds text strings on top of a video buffer",
  "David Schleef <ds@schleef.org>"
};

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
  ARG_WRAP_MODE,
  ARG_FONT_DESC
};


static GstStaticPadTemplate src_template_factory =
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
    GST_STATIC_CAPS ("text/x-pango-markup; text/plain")
    );

/* These macros are adapted from videotestsrc.c */
#define I420_Y_ROWSTRIDE(width) (GST_ROUND_UP_4(width))
#define I420_U_ROWSTRIDE(width) (GST_ROUND_UP_8(width)/2)
#define I420_V_ROWSTRIDE(width) ((GST_ROUND_UP_8(I420_Y_ROWSTRIDE(width)))/2)

#define I420_Y_OFFSET(w,h) (0)
#define I420_U_OFFSET(w,h) (I420_Y_OFFSET(w,h)+(I420_Y_ROWSTRIDE(w)*GST_ROUND_UP_2(h)))
#define I420_V_OFFSET(w,h) (I420_U_OFFSET(w,h)+(I420_U_ROWSTRIDE(w)*GST_ROUND_UP_2(h)/2))

#define I420_SIZE(w,h)     (I420_V_OFFSET(w,h)+(I420_V_ROWSTRIDE(w)*GST_ROUND_UP_2(h)/2))


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
static void gst_text_overlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);


GST_BOILERPLATE (GstTextOverlay, gst_text_overlay, GstElement, GST_TYPE_ELEMENT)
#define DEFAULT_YPAD    25
#define DEFAULT_XPAD    25
#define DEFAULT_DELTAX   0
#define DEFAULT_DELTAY   0
/* keep wrap enum in sync with string in class_init */
#define DEFAULT_WRAP_MODE        GST_TEXT_OVERLAY_WRAP_MODE_WORD_CHAR
#define DEFAULT_SHADING_VALUE    -80
     static void gst_text_overlay_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&video_sink_template_factory));

  /* ugh */
  if (!GST_IS_TIME_OVERLAY_CLASS (g_class) &&
      !GST_IS_CLOCK_OVERLAY_CLASS (g_class)) {
    gst_element_class_add_pad_template (element_class,
        gst_static_pad_template_get (&text_sink_template_factory));
  }

  gst_element_class_set_details (element_class, &text_overlay_details);
}

static gchar *
gst_text_overlay_get_text (GstTextOverlay * overlay, GstBuffer * video_frame)
{
  return g_strdup (overlay->default_text);
}

static void
gst_text_overlay_class_init (GstTextOverlayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->finalize = gst_text_overlay_finalize;
  gobject_class->set_property = gst_text_overlay_set_property;

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_text_overlay_change_state);

  klass->get_text = gst_text_overlay_get_text;
  klass->pango_context = pango_ft2_get_context (72, 72);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_TEXT,
      g_param_spec_string ("text", "text",
          "Text to be display.", "", G_PARAM_WRITABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_SHADING,
      g_param_spec_boolean ("shaded-background", "shaded background",
          "Whether to shade the background under the text area", FALSE,
          G_PARAM_WRITABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_VALIGN,
      g_param_spec_string ("valign", "vertical alignment",
          "Vertical alignment of the text. "
          "Can be either 'baseline', 'bottom', or 'top'",
          "baseline", G_PARAM_WRITABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_HALIGN,
      g_param_spec_string ("halign", "horizontal alignment",
          "Horizontal alignment of the text. "
          "Can be either 'left', 'right', or 'center'",
          "center", G_PARAM_WRITABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_XPAD,
      g_param_spec_int ("xpad", "horizontal paddding",
          "Horizontal paddding when using left/right alignment",
          0, G_MAXINT, DEFAULT_XPAD, G_PARAM_WRITABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_YPAD,
      g_param_spec_int ("ypad", "vertical padding",
          "Vertical padding when using top/bottom alignment",
          0, G_MAXINT, DEFAULT_YPAD, G_PARAM_WRITABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_DELTAX,
      g_param_spec_int ("deltax", "X position modifier",
          "Shift X position to the left or to the right. Unit is pixels.",
          G_MININT, G_MAXINT, 0, G_PARAM_WRITABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_DELTAY,
      g_param_spec_int ("deltay", "Y position modifier",
          "Shift Y position up or down. Unit is pixels.",
          G_MININT, G_MAXINT, 0, G_PARAM_WRITABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_WRAP_MODE,
      g_param_spec_string ("wrap-mode", "wrap mode",
          "Whether to wrap the text and if so how."
          "Can be either 'none', 'word', 'char' or 'wordchar'",
          "wordchar", G_PARAM_WRITABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_FONT_DESC,
      g_param_spec_string ("font-desc", "font description",
          "Pango font description of font "
          "to be used for rendering. "
          "See documentation of "
          "pango_font_description_from_string"
          " for syntax.", "", G_PARAM_WRITABLE));
}

static void
gst_text_overlay_finalize (GObject * object)
{
  GstTextOverlay *overlay = GST_TEXT_OVERLAY (object);

  gst_collect_pads_stop (overlay->collect);
  gst_object_unref (overlay->collect);

  g_free (overlay->default_text);
  g_free (overlay->bitmap.buffer);

  if (overlay->layout)
    g_object_unref (overlay->layout);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_text_overlay_init (GstTextOverlay * overlay, GstTextOverlayClass * klass)
{
  /* video sink */
  overlay->video_sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&video_sink_template_factory), "video_sink");
  gst_pad_set_getcaps_function (overlay->video_sinkpad,
      GST_DEBUG_FUNCPTR (gst_text_overlay_getcaps));
  gst_pad_set_setcaps_function (overlay->video_sinkpad,
      GST_DEBUG_FUNCPTR (gst_text_overlay_setcaps));
  gst_element_add_pad (GST_ELEMENT (overlay), overlay->video_sinkpad);

  if (!GST_IS_TIME_OVERLAY_CLASS (klass) && !GST_IS_CLOCK_OVERLAY_CLASS (klass)) {
    /* text sink */
    overlay->text_sinkpad =
        gst_pad_new_from_template (gst_static_pad_template_get
        (&text_sink_template_factory), "text_sink");
    gst_pad_set_link_function (overlay->text_sinkpad,
        GST_DEBUG_FUNCPTR (gst_text_overlay_text_pad_linked));
    gst_pad_set_unlink_function (overlay->text_sinkpad,
        GST_DEBUG_FUNCPTR (gst_text_overlay_text_pad_unlinked));
    gst_element_add_pad (GST_ELEMENT (overlay), overlay->text_sinkpad);
  }

  /* (video) source */
  overlay->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&src_template_factory), "src");
  gst_pad_set_getcaps_function (overlay->srcpad,
      GST_DEBUG_FUNCPTR (gst_text_overlay_getcaps));
  gst_element_add_pad (GST_ELEMENT (overlay), overlay->srcpad);

  overlay->layout =
      pango_layout_new (GST_TEXT_OVERLAY_GET_CLASS (overlay)->pango_context);
  memset (&overlay->bitmap, 0, sizeof (overlay->bitmap));

  overlay->halign = GST_TEXT_OVERLAY_HALIGN_CENTER;
  overlay->valign = GST_TEXT_OVERLAY_VALIGN_BASELINE;
  overlay->xpad = DEFAULT_XPAD;
  overlay->ypad = DEFAULT_YPAD;
  overlay->deltax = 0;
  overlay->deltay = 0;

  overlay->wrap_mode = DEFAULT_WRAP_MODE;

  overlay->want_shading = FALSE;
  overlay->shading_value = DEFAULT_SHADING_VALUE;

  overlay->default_text = g_strdup ("");
  overlay->need_render = TRUE;

  overlay->fps_n = 0;
  overlay->fps_d = 1;

  overlay->collect = gst_collect_pads_new ();

  gst_collect_pads_set_function (overlay->collect,
      GST_DEBUG_FUNCPTR (gst_text_overlay_collected), overlay);

  overlay->video_collect_data = gst_collect_pads_add_pad (overlay->collect,
      overlay->video_sinkpad, sizeof (GstCollectData));

  /* text pad will be added when it is linked */
  overlay->text_collect_data = NULL;
}

static void
gst_text_overlay_update_wrap_mode (GstTextOverlay * overlay)
{
  if (overlay->wrap_mode == GST_TEXT_OVERLAY_WRAP_MODE_NONE) {
    GST_DEBUG_OBJECT (overlay, "Set wrap mode NONE");
    pango_layout_set_width (overlay->layout, -1);
  } else {
    GST_DEBUG_OBJECT (overlay, "Set layout width %d", overlay->width);
    GST_DEBUG_OBJECT (overlay, "Set wrap mode    %d", overlay->wrap_mode);
    pango_layout_set_width (overlay->layout, overlay->width * PANGO_SCALE);
    pango_layout_set_wrap (overlay->layout, (PangoWrapMode) overlay->wrap_mode);
  }
}


/* FIXME: upstream nego (e.g. when the video window is resized) */

static gboolean
gst_text_overlay_setcaps (GstPad * pad, GstCaps * caps)
{
  GstTextOverlay *overlay;
  GstStructure *structure;
  gboolean ret = FALSE;
  const GValue *fps;

  if (!GST_PAD_IS_SINK (pad))
    return TRUE;

  g_return_val_if_fail (gst_caps_is_fixed (caps), FALSE);

  overlay = GST_TEXT_OVERLAY (gst_pad_get_parent (pad));

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

  if (ret) {
    GST_OBJECT_LOCK (overlay);
    gst_text_overlay_update_wrap_mode (overlay);
    GST_OBJECT_UNLOCK (overlay);
  }

  return ret;
}

static GstPadLinkReturn
gst_text_overlay_text_pad_linked (GstPad * pad, GstPad * peer)
{
  GstTextOverlay *overlay;

  overlay = GST_TEXT_OVERLAY (GST_PAD_PARENT (pad));

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
  GstTextOverlay *overlay;

  /* don't use gst_pad_get_parent() here, will deadlock */
  overlay = GST_TEXT_OVERLAY (GST_PAD_PARENT (pad));

  GST_DEBUG_OBJECT (overlay, "Text pad unlinked");

  if (overlay->text_collect_data) {
    gst_collect_pads_remove_pad (overlay->collect, overlay->text_sinkpad);
    overlay->text_collect_data = NULL;
  }

  overlay->need_render = TRUE;
}

static void
gst_text_overlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstTextOverlay *overlay = GST_TEXT_OVERLAY (object);

  GST_OBJECT_LOCK (overlay);

  switch (prop_id) {
    case ARG_TEXT:
      g_free (overlay->default_text);
      overlay->default_text = g_value_dup_string (value);
      overlay->need_render = TRUE;
      break;

    case ARG_SHADING:{
      overlay->want_shading = g_value_get_boolean (value);
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

    case ARG_VALIGN:{
      const gchar *s = g_value_get_string (value);

      if (g_ascii_strcasecmp (s, "baseline") == 0)
        overlay->valign = GST_TEXT_OVERLAY_VALIGN_BASELINE;
      else if (g_ascii_strcasecmp (s, "bottom") == 0)
        overlay->valign = GST_TEXT_OVERLAY_VALIGN_BOTTOM;
      else if (g_ascii_strcasecmp (s, "top") == 0)
        overlay->valign = GST_TEXT_OVERLAY_VALIGN_TOP;
      else
        g_warning ("Invalid 'valign' property value: %s", s);
      break;
    }

    case ARG_HALIGN:{
      const gchar *s = g_value_get_string (value);

      if (g_ascii_strcasecmp (s, "left") == 0)
        overlay->halign = GST_TEXT_OVERLAY_HALIGN_LEFT;
      else if (g_ascii_strcasecmp (s, "right") == 0)
        overlay->halign = GST_TEXT_OVERLAY_HALIGN_RIGHT;
      else if (g_ascii_strcasecmp (s, "center") == 0)
        overlay->halign = GST_TEXT_OVERLAY_HALIGN_CENTER;
      else
        g_warning ("Invalid 'halign' property value: %s", s);
      break;
    }

    case ARG_WRAP_MODE:{
      const gchar *s = g_value_get_string (value);

      if (g_ascii_strcasecmp (s, "none") == 0)
        overlay->wrap_mode = GST_TEXT_OVERLAY_WRAP_MODE_NONE;
      else if (g_ascii_strcasecmp (s, "char") == 0)
        overlay->wrap_mode = GST_TEXT_OVERLAY_WRAP_MODE_CHAR;
      else if (g_ascii_strcasecmp (s, "word") == 0)
        overlay->wrap_mode = GST_TEXT_OVERLAY_WRAP_MODE_WORD;
      else if (g_ascii_strcasecmp (s, "wordchar") == 0)
        overlay->wrap_mode = GST_TEXT_OVERLAY_WRAP_MODE_WORD_CHAR;
      else
        g_warning ("Invalid 'wrap-mode' property value: %s", s);

      gst_text_overlay_update_wrap_mode (overlay);
      break;
    }

    case ARG_FONT_DESC:
    {
      PangoFontDescription *desc;
      const gchar *fontdesc_str;

      fontdesc_str = g_value_get_string (value);
      desc = pango_font_description_from_string (fontdesc_str);
      if (desc) {
        GST_LOG ("font description set: %s", fontdesc_str);
        pango_layout_set_font_description (overlay->layout, desc);
        pango_font_description_free (desc);
      } else {
        GST_WARNING ("font description parse failed: %s", fontdesc_str);
      }
      break;
    }

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  overlay->need_render = TRUE;

  GST_OBJECT_UNLOCK (overlay);
}


static GstCaps *
gst_text_overlay_getcaps (GstPad * pad)
{
  GstTextOverlay *overlay;
  GstPad *otherpad;
  GstCaps *caps;

  overlay = GST_TEXT_OVERLAY (gst_pad_get_parent (pad));

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

#define BOX_XPAD         6
#define BOX_YPAD         6

static inline void
gst_text_overlay_shade_y (GstTextOverlay * overlay, guchar * dest,
    guint dest_stride, gint x0, gint x1, gint y0, gint y1)
{
  gint i, j;

  x0 = CLAMP (x0 - BOX_XPAD, 0, overlay->width);
  x1 = CLAMP (x1 + BOX_XPAD, 0, overlay->width);

  y0 = CLAMP (y0 - BOX_YPAD, 0, overlay->height);
  y1 = CLAMP (y1 + BOX_YPAD, 0, overlay->height);

  for (i = y0; i < y1; ++i) {
    for (j = x0; j < x1; ++j) {
      gint y = dest[(i * dest_stride) + j] + overlay->shading_value;

      dest[(i * dest_stride) + j] = CLAMP (y, 0, 255);
    }
  }
}

/* FIXME:
 *  - use proper strides and offset for I420
 *  - don't draw over the edge of the picture (try a longer
 *    text with a huge font size)
 */

static inline void
gst_text_overlay_blit_yuv420 (GstTextOverlay * overlay, FT_Bitmap * bitmap,
    guint8 * yuv_pixels, gint x0, gint y0)
{
  int y;                        /* text bitmap coordinates */
  int x1, y1;                   /* video buffer coordinates */
  int bit_rowinc, uv_rowinc;
  guint8 *p, *bitp, *u_p;
  int video_width, video_height;
  int bitmap_x0 = 0;            //x0 < 1 ? -(x0 - 1) : 1;       /* 1 pixel border */
  int bitmap_y0 = y0 < 1 ? -(y0 - 1) : 1;       /* 1 pixel border */
  int bitmap_width = bitmap->width - bitmap_x0;
  int bitmap_height = bitmap->rows - bitmap_y0;
  int u_plane_size;
  int skip_y, skip_x;
  guint8 v;

  video_width = I420_Y_ROWSTRIDE (overlay->width);
  video_height = overlay->height;

/*
  if (x0 < 0 && abs (x0) < bitmap_width) {
    bitmap_x0 = abs (x0);
    x0 = 0;
  }
*/

  if (x0 + bitmap_x0 + bitmap_width > overlay->width - 1)       /* 1 pixel border */
    bitmap_width -= x0 + bitmap_x0 + bitmap_width - overlay->width + 1;
  if (y0 + bitmap_y0 + bitmap_height > video_height - 1)        /* 1 pixel border */
    bitmap_height -= y0 + bitmap_y0 + bitmap_height - video_height + 1;

  uv_rowinc = video_width / 2 - bitmap_width / 2;
  bit_rowinc = bitmap->pitch - bitmap_width;
  u_plane_size = (video_width / 2) * (video_height / 2);

  y1 = y0 + bitmap_y0;
  x1 = x0 + bitmap_x0;
  bitp = bitmap->buffer + bitmap->pitch * bitmap_y0 + bitmap_x0;
  for (y = bitmap_y0; y < bitmap_y0 + bitmap_height; y++) {
    int n;

    p = yuv_pixels + (y + y0) * I420_Y_ROWSTRIDE (overlay->width) + x1;
    for (n = bitmap_width; n > 0; --n) {
      v = *bitp;
      if (v) {
        p[-1] = CLAMP (p[-1] - v, 0, 255);
        p[1] = CLAMP (p[1] - v, 0, 255);
        p[-video_width] = CLAMP (p[-video_width] - v, 0, 255);
        p[video_width] = CLAMP (p[video_width] - v, 0, 255);
      }
      p++;
      bitp++;
    }
    bitp += bit_rowinc;
  }

  y = bitmap_y0;
  y1 = y0 + bitmap_y0;
  x1 = x0 + bitmap_x0;
  bitp = bitmap->buffer + bitmap->pitch * bitmap_y0 + bitmap_x0;
  p = yuv_pixels + video_width * y1 + x1;
  u_p =
      yuv_pixels + video_width * video_height + (video_width >> 1) * (y1 >> 1) +
      (x1 >> 1);
  skip_y = 0;
  skip_x = 0;

  for (; y < bitmap_y0 + bitmap_height; y++) {
    int n;

    x1 = x0 + bitmap_x0;
    skip_x = 0;
    for (n = bitmap_width; n > 0; --n) {
      v = *bitp;
      if (v) {
        *p = v;
        if (!skip_y) {
          u_p[0] = u_p[u_plane_size] = 0x80;
        }
      }
      if (!skip_y) {
        skip_x = !skip_x;
        if (!skip_x)
          u_p++;
      }
      p++;
      bitp++;
    }
    /*if (!skip_x && !skip_y) u_p--; */
    p += I420_Y_ROWSTRIDE (overlay->width) - bitmap_width;
    bitp += bit_rowinc;
    skip_y = !skip_y;
    u_p += skip_y ? uv_rowinc : 0;
  }
}

static void
gst_text_overlay_resize_bitmap (GstTextOverlay * overlay, gint width,
    gint height)
{
  FT_Bitmap *bitmap = &overlay->bitmap;
  int pitch = (width | 3) + 1;
  int size = pitch * height;

  /* no need to keep reallocating; just keep the maximum size so far */
  if (size <= overlay->bitmap_buffer_size) {
    bitmap->rows = height;
    bitmap->width = width;
    bitmap->pitch = pitch;
    memset (bitmap->buffer, 0, overlay->bitmap_buffer_size);
    return;
  }
  if (!bitmap->buffer) {
    /* initialize */
    bitmap->pixel_mode = ft_pixel_mode_grays;
    bitmap->num_grays = 256;
  }
  overlay->bitmap_buffer_size = size;
  bitmap->buffer = g_realloc (bitmap->buffer, size);
  memset (bitmap->buffer, 0, size);
  bitmap->rows = height;
  bitmap->width = width;
  bitmap->pitch = pitch;
}

static void
gst_text_overlay_render_text (GstTextOverlay * overlay,
    const gchar * text, gint textlen)
{
  PangoRectangle ink_rect, logical_rect;
  gchar *string;

  if (textlen < 0)
    textlen = strlen (text);

  string = g_strndup (text, textlen);
  g_strdelimit (string, "\n\r\t", ' ');
  textlen = strlen (string);

  /* FIXME: should we check for UTF-8 here? */

  if (!overlay->need_render) {
    GST_DEBUG ("Using previously rendered text.");
    return;
  }

  GST_DEBUG ("Rendering '%s'", string);
  pango_layout_set_markup (overlay->layout, string, textlen);

  pango_layout_get_pixel_extents (overlay->layout, &ink_rect, &logical_rect);
  gst_text_overlay_resize_bitmap (overlay, ink_rect.width,
      ink_rect.height + ink_rect.y);
  pango_ft2_render_layout (&overlay->bitmap, overlay->layout, -ink_rect.x, 0);
  overlay->baseline_y = ink_rect.y;

  g_free (string);

  overlay->need_render = FALSE;
}

static GstFlowReturn
gst_text_overlay_push_frame (GstTextOverlay * overlay, GstBuffer * video_frame)
{
  gint xpos, ypos;

  video_frame = gst_buffer_make_writable (video_frame);

  switch (overlay->halign) {
    case GST_TEXT_OVERLAY_HALIGN_LEFT:
      xpos = overlay->xpad;
      break;
    case GST_TEXT_OVERLAY_HALIGN_CENTER:
      xpos = (overlay->width - overlay->bitmap.width) / 2;
      break;
    case GST_TEXT_OVERLAY_HALIGN_RIGHT:
      xpos = overlay->width - overlay->bitmap.width - overlay->xpad;
      break;
    default:
      xpos = 0;
  }
  xpos += overlay->deltax;


  switch (overlay->valign) {
    case GST_TEXT_OVERLAY_VALIGN_BOTTOM:
      ypos = overlay->height - overlay->bitmap.rows - overlay->ypad;
      break;
    case GST_TEXT_OVERLAY_VALIGN_BASELINE:
      ypos = overlay->height - (overlay->bitmap.rows + overlay->ypad);
      break;
    case GST_TEXT_OVERLAY_VALIGN_TOP:
      ypos = overlay->ypad;
      break;
    default:
      ypos = overlay->ypad;
      break;
  }
  ypos += overlay->deltay;

  /* shaded background box */
  if (overlay->want_shading) {
    gst_text_overlay_shade_y (overlay,
        GST_BUFFER_DATA (video_frame),
        I420_Y_ROWSTRIDE (overlay->width),
        xpos, xpos + overlay->bitmap.width, ypos, ypos + overlay->bitmap.rows);
  }


  if (overlay->bitmap.buffer) {
    gst_text_overlay_blit_yuv420 (overlay, &overlay->bitmap,
        GST_BUFFER_DATA (video_frame), xpos, ypos);
  }

  return gst_pad_push (overlay->srcpad, video_frame);
}

static void
gst_text_overlay_pop_video (GstTextOverlay * overlay)
{
  GstBuffer *buf;

  buf = gst_collect_pads_pop (overlay->collect, overlay->video_collect_data);
  g_return_if_fail (buf != NULL);
  gst_buffer_unref (buf);
}

static void
gst_text_overlay_pop_text (GstTextOverlay * overlay)
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
  GstTextOverlayClass *klass;
  GstTextOverlay *overlay;
  GstFlowReturn ret = GST_FLOW_OK;
  GstClockTime now, txt_end, frame_end;
  GstBuffer *video_frame = NULL;
  GstBuffer *text_buf = NULL;
  gchar *text;
  gint text_len;

  overlay = GST_TEXT_OVERLAY (data);
  klass = GST_TEXT_OVERLAY_GET_CLASS (data);

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
    gchar *txt;

    if (klass->get_text)
      txt = klass->get_text (overlay, video_frame);
    else
      txt = g_strdup (overlay->default_text);

    GST_DEBUG ("Text pad not linked, rendering default text: '%s'",
        GST_STR_NULL (txt));
    if (txt != NULL && *txt != '\0') {
      gst_text_overlay_render_text (overlay, txt, -1);
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

static GstStateChangeReturn
gst_text_overlay_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstTextOverlay *overlay = GST_TEXT_OVERLAY (element);

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
    "pango", "Pango-based text rendering and overlay", plugin_init,
    VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
