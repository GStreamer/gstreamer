/*
 * GStreamer
 * Copyright 2005 Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright 2008 Vincent Penquerc'h <ogg.k.ogg.k@googlemail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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
 * SECTION:element-tiger
 * @see_also: katedec
 *
 * <refsect2>
 * <para>
 * This element decodes and renders Kate streams
 * <ulink url="http://libkate.googlecode.com/">Kate</ulink> is a free codec
 * for text based data, such as subtitles. Any number of kate streams can be
 * embedded in an Ogg stream.
 * </para>
 * <para>
 * libkate (see above url) and <ulink url="http://libtiger.googlecode.com/">libtiger</ulink>
 * are needed to build this element.
 * </para>
 * <title>Example pipeline</title>
 * <para>
 * This pipeline renders a Kate stream on top of a Theora video multiplexed
 * in the same stream:
 * <programlisting>
 * gst-launch \
 *   filesrc location=video.ogg ! oggdemux name=demux \
 *   demux. ! queue ! theoradec ! ffmpegcolorspace ! tiger name=tiger \
 *   demux. ! queue ! kateparse ! tiger. \
 *   tiger. ! ffmpegcolorspace ! autovideosink
 * </programlisting>
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include <gst/gst.h>
#include <gst/video/video.h>

#include "gstkate.h"
#include "gstkatetiger.h"

GST_DEBUG_CATEGORY_EXTERN (gst_katetiger_debug);
#define GST_CAT_DEFAULT gst_katetiger_debug

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_DEFAULT_FONT_DESC = DECODER_BASE_ARG_COUNT,
  ARG_QUALITY,
  ARG_DEFAULT_FONT_EFFECT,
  ARG_DEFAULT_FONT_EFFECT_STRENGTH,
  ARG_DEFAULT_FONT_RED,
  ARG_DEFAULT_FONT_GREEN,
  ARG_DEFAULT_FONT_BLUE,
  ARG_DEFAULT_FONT_ALPHA,
  ARG_DEFAULT_BACKGROUND_RED,
  ARG_DEFAULT_BACKGROUND_GREEN,
  ARG_DEFAULT_BACKGROUND_BLUE,
  ARG_DEFAULT_BACKGROUND_ALPHA
};

static GstStaticPadTemplate kate_sink_factory =
    GST_STATIC_PAD_TEMPLATE ("subtitle_sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("subtitle/x-kate; application/x-kate")
    );

static GstStaticPadTemplate video_sink_factory =
GST_STATIC_PAD_TEMPLATE ("video_sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-rgb, bpp=(int)32, depth=(int)24")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-rgb, bpp=(int)32, depth=(int)24")
    );

GST_BOILERPLATE (GstKateTiger, gst_kate_tiger, GstElement, GST_TYPE_ELEMENT);

static GType
gst_kate_tiger_font_effect_get_type ()
{
  static GType font_effect_type = 0;

  if (!font_effect_type) {
    static const GEnumValue font_effects[] = {
      {tiger_font_plain, "none", "none"},
      {tiger_font_shadow, "shadow", "shadow"},
      {tiger_font_outline, "outline", "outline"},
      {0, NULL, NULL}
    };
    font_effect_type = g_enum_register_static ("GstFontEffect", font_effects);
  }

  return font_effect_type;
}

static void gst_kate_tiger_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_kate_tiger_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_kate_tiger_dispose (GObject * object);

static GstFlowReturn gst_kate_tiger_kate_chain (GstPad * pad, GstBuffer * buf);
static GstFlowReturn gst_kate_tiger_video_chain (GstPad * pad, GstBuffer * buf);
static GstStateChangeReturn gst_kate_tiger_change_state (GstElement * element,
    GstStateChange transition);
static gboolean gst_kate_tiger_kate_sink_query (GstPad * pad, GstQuery * query);
static gboolean gst_kate_tiger_kate_event (GstPad * pad, GstEvent * event);
static gboolean gst_kate_tiger_video_set_caps (GstPad * pad, GstCaps * caps);
static gboolean gst_kate_tiger_source_event (GstPad * pad, GstEvent * event);

static void
gst_kate_tiger_base_init (gpointer gclass)
{
  static GstElementDetails element_details =
      GST_ELEMENT_DETAILS ("Kate stream renderer",
      "Mixer/Video/Overlay/Subtitle",
      "Decodes and renders Kate streams on top of a video",
      "Vincent Penquerc'h <ogg.k.ogg.k@googlemail.com>");
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&kate_sink_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&video_sink_factory));
  gst_element_class_set_details (element_class, &element_details);
}

/* initialize the plugin's class */
static void
gst_kate_tiger_class_init (GstKateTigerClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_kate_tiger_get_property);
  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_kate_tiger_set_property);
  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_kate_tiger_dispose);

  gst_kate_util_install_decoder_base_properties (gobject_class);

  g_object_class_install_property (gobject_class, ARG_QUALITY,
      g_param_spec_double ("quality", "Rendering quality",
          "Rendering quality (0 is faster, 1 is best and slower)",
          0.0, 1.0, 1.0, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_DEFAULT_FONT_DESC,
      g_param_spec_string ("default-font-desc", "Default font description",
          "Default font description (Pango style) to render text with",
          "", G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_DEFAULT_FONT_EFFECT,
      g_param_spec_enum ("default-font-effect", "Default font effect",
          "Whether to apply an effect to text by default, for increased readability",
          gst_kate_tiger_font_effect_get_type (),
          tiger_font_plain, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  g_object_class_install_property (gobject_class,
      ARG_DEFAULT_FONT_EFFECT_STRENGTH,
      g_param_spec_double ("default-font-effect-strength",
          "Default font effect strength",
          "How pronounced should the font effect be (effect dependent)", 0.0,
          1.0, 0.5, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_DEFAULT_FONT_RED,
      g_param_spec_int ("default-font-red",
          "Default font color (red component)",
          "Default font color (red component, between 0 and 255) to render text with",
          0, 255, 255, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_DEFAULT_FONT_GREEN,
      g_param_spec_int ("default-font-green",
          "Default font color (green component)",
          "Default font color (green component, between 0 and 255) to render text with",
          0, 255, 255, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_DEFAULT_FONT_BLUE,
      g_param_spec_int ("default-font-blue",
          "Default font color (blue component)",
          "Default font color (blue component, between 0 and 255) to render text with",
          0, 255, 255, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_DEFAULT_FONT_ALPHA,
      g_param_spec_int ("default-font-alpha",
          "Default font color (alpha component)",
          "Default font color (alpha component, between 0 and 255) to render text with",
          0, 255, 255, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_DEFAULT_BACKGROUND_RED,
      g_param_spec_int ("default-background-red",
          "Default background color (red component)",
          "Default background color (red component, between 0 and 255) to render text with",
          0, 255, 255, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_DEFAULT_BACKGROUND_GREEN,
      g_param_spec_int ("default-background-green",
          "Default background color (green component)",
          "Default background color (green component, between 0 and 255) to render text with",
          0, 255, 255, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_DEFAULT_BACKGROUND_BLUE,
      g_param_spec_int ("default-background-blue",
          "Default background color (blue component)",
          "Default background color (blue component, between 0 and 255) to render text with",
          0, 255, 255, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_DEFAULT_BACKGROUND_ALPHA,
      g_param_spec_int ("default-background-alpha",
          "Default background color (alpha component)",
          "Default background color (alpha component, between 0 and 255) to render text with",
          0, 255, 255, G_PARAM_READWRITE));

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_kate_tiger_change_state);
}

/* initialize the new element
 * instantiate pads and add them to element
 * set functions
 * initialize structure
 */
static void
gst_kate_tiger_init (GstKateTiger * tiger, GstKateTigerClass * gclass)
{
  GST_DEBUG_OBJECT (tiger, "gst_kate_tiger_init");

  tiger->mutex = g_mutex_new ();

  tiger->katesinkpad =
      gst_pad_new_from_static_template (&kate_sink_factory, "kate_sink");
  gst_pad_set_chain_function (tiger->katesinkpad,
      GST_DEBUG_FUNCPTR (gst_kate_tiger_kate_chain));
  gst_pad_set_query_function (tiger->katesinkpad,
      GST_DEBUG_FUNCPTR (gst_kate_tiger_kate_sink_query));
  gst_pad_set_event_function (tiger->katesinkpad,
      GST_DEBUG_FUNCPTR (gst_kate_tiger_kate_event));
  gst_element_add_pad (GST_ELEMENT (tiger), tiger->katesinkpad);

  tiger->videosinkpad =
      gst_pad_new_from_static_template (&video_sink_factory, "video_sink");
  gst_pad_set_chain_function (tiger->videosinkpad,
      GST_DEBUG_FUNCPTR (gst_kate_tiger_video_chain));
  //gst_pad_set_query_function (tiger->videosinkpad, GST_DEBUG_FUNCPTR (gst_kate_tiger_video_sink_query));
  gst_pad_use_fixed_caps (tiger->videosinkpad);
  gst_pad_set_caps (tiger->videosinkpad,
      gst_static_pad_template_get_caps (&video_sink_factory));
  gst_pad_set_setcaps_function (tiger->videosinkpad,
      GST_DEBUG_FUNCPTR (gst_kate_tiger_video_set_caps));
  gst_element_add_pad (GST_ELEMENT (tiger), tiger->videosinkpad);

  tiger->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  gst_pad_set_event_function (tiger->srcpad, gst_kate_tiger_source_event);
  gst_element_add_pad (GST_ELEMENT (tiger), tiger->srcpad);

  gst_kate_util_decode_base_init (&tiger->decoder);

  tiger->tr = NULL;

  tiger->default_font_desc = NULL;
  tiger->quality = -1.0;
  tiger->default_font_effect = tiger_font_plain;
  tiger->default_font_effect_strength = 0.5;
  tiger->default_font_r = 255;
  tiger->default_font_g = 255;
  tiger->default_font_b = 255;
  tiger->default_font_a = 255;
  tiger->default_background_r = 0;
  tiger->default_background_g = 0;
  tiger->default_background_b = 0;
  tiger->default_background_a = 0;

  tiger->video_width = 0;
  tiger->video_height = 0;
}

static void
gst_kate_tiger_dispose (GObject * object)
{
  GstKateTiger *tiger = GST_KATE_TIGER (object);

  GST_LOG_OBJECT (tiger, "disposing");

  if (tiger->default_font_desc) {
    g_free (tiger->default_font_desc);
    tiger->default_font_desc = NULL;
  }

  g_mutex_free (tiger->mutex);
  tiger->mutex = NULL;

  GST_CALL_PARENT (G_OBJECT_CLASS, dispose, (object));
}

static void
gst_kate_tiger_update_quality (GstKateTiger * tiger)
{
  if (tiger->tr && tiger->quality >= 0.0) {
    tiger_renderer_set_quality (tiger->tr, tiger->quality);
  }
}

static void
gst_kate_tiger_update_default_font_effect (GstKateTiger * tiger)
{
  if (tiger->tr) {
    tiger_renderer_set_default_font_effect (tiger->tr,
        tiger->default_font_effect, tiger->default_font_effect_strength);
  }
}

static void
gst_kate_tiger_update_default_font_color (GstKateTiger * tiger)
{
  if (tiger->tr) {
    tiger_renderer_set_default_font_color (tiger->tr,
        tiger->default_font_r / 255.0,
        tiger->default_font_g / 255.0,
        tiger->default_font_b / 255.0, tiger->default_font_a / 255.0);
  }
}

static void
gst_kate_tiger_update_default_background_color (GstKateTiger * tiger)
{
  if (tiger->tr) {
    tiger_renderer_set_default_background_fill_color (tiger->tr,
        tiger->default_background_r / 255.0,
        tiger->default_background_g / 255.0,
        tiger->default_background_b / 255.0,
        tiger->default_background_a / 255.0);
  }
}

static void
gst_kate_tiger_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstKateTiger *tiger = GST_KATE_TIGER (object);
  const char *str;

  g_mutex_lock (tiger->mutex);

  switch (prop_id) {
    case ARG_DEFAULT_FONT_DESC:
      if (tiger->default_font_desc) {
        g_free (tiger->default_font_desc);
        tiger->default_font_desc = NULL;
      }
      str = g_value_get_string (value);
      if (str) {
        tiger->default_font_desc = g_strdup (str);
        if (tiger->tr)
          tiger_renderer_set_default_font_description (tiger->tr,
              tiger->default_font_desc);
      }
      break;
    case ARG_QUALITY:
      tiger->quality = g_value_get_double (value);
      gst_kate_tiger_update_quality (tiger);
      break;
    case ARG_DEFAULT_FONT_EFFECT:
      tiger->default_font_effect = g_value_get_enum (value);
      gst_kate_tiger_update_default_font_effect (tiger);
      break;
    case ARG_DEFAULT_FONT_EFFECT_STRENGTH:
      tiger->default_font_effect_strength = g_value_get_double (value);
      gst_kate_tiger_update_default_font_effect (tiger);
      break;
    case ARG_DEFAULT_FONT_RED:
      tiger->default_font_r = g_value_get_int (value);
      gst_kate_tiger_update_default_font_color (tiger);
      break;
    case ARG_DEFAULT_FONT_GREEN:
      tiger->default_font_g = g_value_get_int (value);
      gst_kate_tiger_update_default_font_color (tiger);
      break;
    case ARG_DEFAULT_FONT_BLUE:
      tiger->default_font_b = g_value_get_int (value);
      gst_kate_tiger_update_default_font_color (tiger);
      break;
    case ARG_DEFAULT_FONT_ALPHA:
      tiger->default_font_a = g_value_get_int (value);
      gst_kate_tiger_update_default_font_color (tiger);
      break;
    case ARG_DEFAULT_BACKGROUND_RED:
      tiger->default_background_r = g_value_get_int (value);
      gst_kate_tiger_update_default_background_color (tiger);
      break;
    case ARG_DEFAULT_BACKGROUND_GREEN:
      tiger->default_background_g = g_value_get_int (value);
      gst_kate_tiger_update_default_background_color (tiger);
      break;
    case ARG_DEFAULT_BACKGROUND_BLUE:
      tiger->default_background_b = g_value_get_int (value);
      gst_kate_tiger_update_default_background_color (tiger);
      break;
    case ARG_DEFAULT_BACKGROUND_ALPHA:
      tiger->default_background_a = g_value_get_int (value);
      gst_kate_tiger_update_default_background_color (tiger);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  g_mutex_unlock (tiger->mutex);
}

static void
gst_kate_tiger_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstKateTiger *tiger = GST_KATE_TIGER (object);

  g_mutex_lock (tiger->mutex);

  switch (prop_id) {
    case ARG_DEFAULT_FONT_DESC:
      g_value_set_string (value,
          tiger->default_font_desc ? tiger->default_font_desc : "");
      break;
    case ARG_QUALITY:
      g_value_set_double (value, tiger->quality);
      break;
    case ARG_DEFAULT_FONT_EFFECT:
      g_value_set_enum (value, tiger->default_font_effect);
      break;
    case ARG_DEFAULT_FONT_EFFECT_STRENGTH:
      g_value_set_double (value, tiger->default_font_effect_strength);
      break;
    case ARG_DEFAULT_FONT_RED:
      g_value_set_int (value, tiger->default_font_r);
      break;
    case ARG_DEFAULT_FONT_GREEN:
      g_value_set_int (value, tiger->default_font_g);
      break;
    case ARG_DEFAULT_FONT_BLUE:
      g_value_set_int (value, tiger->default_font_b);
      break;
    case ARG_DEFAULT_FONT_ALPHA:
      g_value_set_int (value, tiger->default_font_a);
      break;
    case ARG_DEFAULT_BACKGROUND_RED:
      g_value_set_int (value, tiger->default_background_r);
      break;
    case ARG_DEFAULT_BACKGROUND_GREEN:
      g_value_set_int (value, tiger->default_background_g);
      break;
    case ARG_DEFAULT_BACKGROUND_BLUE:
      g_value_set_int (value, tiger->default_background_b);
      break;
    case ARG_DEFAULT_BACKGROUND_ALPHA:
      g_value_set_int (value, tiger->default_background_a);
      break;
    default:
      if (!gst_kate_util_decoder_base_get_property (&tiger->decoder, object,
              prop_id, value, pspec)) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      }
      break;
  }

  g_mutex_unlock (tiger->mutex);
}

/* GstElement vmethod implementations */

/* chain function
 * this function does the actual processing
 */

static GstFlowReturn
gst_kate_tiger_kate_chain (GstPad * pad, GstBuffer * buf)
{
  GstKateTiger *tiger = GST_KATE_TIGER (gst_pad_get_parent (pad));
  const kate_event *ev = NULL;
  GstFlowReturn rflow = GST_FLOW_OK;

  g_mutex_lock (tiger->mutex);

  GST_LOG_OBJECT (tiger, "Got kate buffer");

  rflow =
      gst_kate_util_decoder_base_chain_kate_packet (&tiger->decoder,
      GST_ELEMENT_CAST (tiger), pad, buf, tiger->srcpad, &ev);
  if (G_LIKELY (rflow == GST_FLOW_OK)) {
    if (ev) {
      int ret = tiger_renderer_add_event (tiger->tr, ev->ki, ev);
      GST_INFO_OBJECT (tiger, "adding event for %p from %f to %f: %p, \"%s\"",
          ev->ki, ev->start_time, ev->end_time, ev->bitmap, ev->text);
      if (G_UNLIKELY (ret < 0)) {
        GST_WARNING_OBJECT (tiger,
            "failed to add Kate event to Tiger renderer: %d", ret);
      }
    }
  }

  gst_object_unref (tiger);
  gst_buffer_unref (buf);

  g_mutex_unlock (tiger->mutex);

  return rflow;
}

static gboolean
gst_kate_tiger_video_set_caps (GstPad * pad, GstCaps * caps)
{
  GstKateTiger *tiger = GST_KATE_TIGER (gst_pad_get_parent (pad));
  GstStructure *s;
  gint w, h;
  gboolean res = FALSE;

  g_mutex_lock (tiger->mutex);

  s = gst_caps_get_structure (caps, 0);

  if (G_LIKELY (gst_structure_get_int (s, "width", &w))
      && G_LIKELY (gst_structure_get_int (s, "height", &h))) {
    GST_INFO_OBJECT (tiger, "video sink: %d %d", w, h);
    tiger->video_width = w;
    tiger->video_height = h;
    res = TRUE;
  }

  g_mutex_unlock (tiger->mutex);

  gst_object_unref (tiger);
  return TRUE;
}

static GstFlowReturn
gst_kate_tiger_video_chain (GstPad * pad, GstBuffer * buf)
{
  GstKateTiger *tiger = GST_KATE_TIGER (gst_pad_get_parent (pad));
  GstFlowReturn rflow = GST_FLOW_OK;
  unsigned char *ptr;
  int ret;

  g_mutex_lock (tiger->mutex);

  GST_LOG_OBJECT (tiger, "got video frame, %u bytes", GST_BUFFER_SIZE (buf));

  /* draw on it */
  buf = gst_buffer_make_writable (buf);
  if (G_UNLIKELY (!buf)) {
    GST_WARNING_OBJECT (tiger, "Failed to make video buffer writable");
  } else {
    ptr = GST_BUFFER_DATA (buf);
    if (!ptr) {
      GST_WARNING_OBJECT (tiger,
          "Failed to get a pointer to video buffer data");
    } else {
      ret = tiger_renderer_set_buffer (tiger->tr, ptr, tiger->video_width, tiger->video_height, tiger->video_width * 4, 0);     // TODO: stride ?
      if (G_UNLIKELY (ret < 0)) {
        GST_WARNING_OBJECT (tiger,
            "Tiger renderer failed to set buffer to video frame: %d", ret);
      } else {
        kate_float t = GST_BUFFER_TIMESTAMP (buf) / (gdouble) GST_SECOND;
        ret = tiger_renderer_update (tiger->tr, t, 1);
        if (G_UNLIKELY (ret < 0)) {
          GST_WARNING_OBJECT (tiger, "Tiger renderer failed to update: %d",
              ret);
        } else {
          ret = tiger_renderer_render (tiger->tr);
          if (G_UNLIKELY (ret < 0)) {
            GST_WARNING_OBJECT (tiger,
                "Tiger renderer failed to render to video frame: %d", ret);
          } else {
            GST_LOG_OBJECT (tiger,
                "Tiger renderer rendered on video frame at %f", t);
          }
        }
      }
    }
  }
  rflow = gst_pad_push (tiger->srcpad, buf);

  gst_object_unref (tiger);

  g_mutex_unlock (tiger->mutex);

  return rflow;
}

static GstStateChangeReturn
gst_kate_tiger_change_state (GstElement * element, GstStateChange transition)
{
  GstKateTiger *tiger = GST_KATE_TIGER (element);
  GstStateChangeReturn res;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      GST_DEBUG_OBJECT (tiger, "PAUSED -> READY, clearing kate state");
      g_mutex_lock (tiger->mutex);
      if (tiger->tr) {
        tiger_renderer_destroy (tiger->tr);
        tiger->tr = NULL;
      }
      g_mutex_unlock (tiger->mutex);
      break;
    default:
      break;
  }

  res =
      gst_kate_decoder_base_change_state (&tiger->decoder, element,
      parent_class, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      GST_DEBUG_OBJECT (tiger, "READY -> PAUSED, initializing kate state");
      g_mutex_lock (tiger->mutex);
      if (tiger->decoder.initialized) {
        int ret = tiger_renderer_create (&tiger->tr);
        if (ret < 0) {
          GST_WARNING_OBJECT (tiger, "failed to create tiger renderer: %d",
              ret);
        } else {
          ret =
              tiger_renderer_set_default_font_description (tiger->tr,
              tiger->default_font_desc);
          if (ret < 0) {
            GST_WARNING_OBJECT (tiger,
                "failed to set tiger default font description: %d", ret);
          }
          gst_kate_tiger_update_default_font_color (tiger);
          gst_kate_tiger_update_default_background_color (tiger);
          gst_kate_tiger_update_default_font_effect (tiger);
          gst_kate_tiger_update_quality (tiger);
        }
      }
      g_mutex_unlock (tiger->mutex);
      break;
    default:
      break;
  }

  return res;
}

static gboolean
gst_kate_tiger_seek (GstKateTiger * tiger, GstPad * pad, GstEvent * event)
{
  GstFormat format;
  gdouble rate;
  GstSeekFlags flags;
  GstSeekType cur_type, stop_type;
  gint64 cur, stop;

  gst_event_parse_seek (event, &rate, &format, &flags, &cur_type, &cur,
      &stop_type, &stop);

  /* forward to both sinks */
  gst_event_ref (event);
  if (gst_pad_push_event (tiger->videosinkpad, event)) {
    if (gst_pad_push_event (tiger->katesinkpad, event)) {
      if (format == GST_FORMAT_TIME) {
        /* if seeking in time, we can update tiger to remove any appropriate events */
        kate_float target = cur / (gdouble) GST_SECOND;
        GST_INFO_OBJECT (tiger, "Seeking in time to %f", target);
        g_mutex_lock (tiger->mutex);
        tiger_renderer_seek (tiger->tr, target);
        g_mutex_unlock (tiger->mutex);
      }
      return TRUE;
    } else {
      return FALSE;
    }
  } else {
    gst_event_unref (event);
    return FALSE;
  }
}

static gboolean
gst_kate_tiger_source_event (GstPad * pad, GstEvent * event)
{
  GstKateTiger *tiger =
      (GstKateTiger *) (gst_object_get_parent (GST_OBJECT (pad)));
  gboolean res = TRUE;

  g_return_val_if_fail (tiger != NULL, FALSE);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      GST_INFO_OBJECT (tiger, "Seek on source pad");
      res = gst_kate_tiger_seek (tiger, pad, event);
      break;
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }

  gst_object_unref (tiger);

  return res;
}

static gboolean
gst_kate_tiger_kate_event (GstPad * pad, GstEvent * event)
{
  GstKateTiger *tiger =
      (GstKateTiger *) (gst_object_get_parent (GST_OBJECT (pad)));
  gboolean res = TRUE;

  g_return_val_if_fail (tiger != NULL, FALSE);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:
      GST_INFO_OBJECT (tiger, "New segment on Kate pad");
      gst_event_unref (event);
      break;
    case GST_EVENT_EOS:
      /* we ignore this, it just means we don't have anymore Kate packets, but
         the Tiger renderer will still draw (if appropriate) on incoming video */
      GST_INFO_OBJECT (tiger, "EOS on Kate pad");
      gst_event_unref (event);
      break;
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }

  gst_object_unref (tiger);

  return res;
}

gboolean
gst_kate_tiger_kate_sink_query (GstPad * pad, GstQuery * query)
{
  GstKateTiger *tiger = GST_KATE_TIGER (gst_pad_get_parent (pad));
  gboolean res = gst_kate_decoder_base_sink_query (&tiger->decoder,
      GST_ELEMENT_CAST (tiger), pad, query);
  gst_object_unref (tiger);
  return res;
}
