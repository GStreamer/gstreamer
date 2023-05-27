/* GStreamer
 * Copyright (C) 2023 Seungha Yang <seungha@centricular.com>
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gst/d3d11/gstd3d11-private.h>
#include "gstdwritebaseoverlay.h"
#include "gstdwritebitmappool.h"
#include "gstdwrite-renderer.h"
#include "gstdwrite-effect.h"
#include <wrl.h>
#include <mutex>
#include <condition_variable>

GST_DEBUG_CATEGORY_STATIC (dwrite_base_overlay_debug);
#define GST_CAT_DEFAULT dwrite_base_overlay_debug

/* *INDENT-OFF* */
using namespace Microsoft::WRL;
/* *INDENT-ON* */

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_DWRITE_CAPS)
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_DWRITE_CAPS)
    );

enum
{
  PROP_0,
  PROP_VISIBLE,
  PROP_FONT_FAMILY,
  PROP_FONT_SIZE,
  PROP_REFERENCE_FRAME_SIZE,
  PROP_AUTO_RESIZE,
  PROP_FONT_WEIGHT,
  PROP_FONT_STYLE,
  PROP_FONT_STRETCH,
  PROP_TEXT,
  PROP_COLOR,
  PROP_OUTLINE_COLOR,
  PROP_UNDERLINE_COLOR,
  PROP_UNDERLINE_OUTLINE_COLOR,
  PROP_STRIKETHROUGH_COLOR,
  PROP_STRIKETHROUGH_OUTLINE_COLOR,
  PROP_SHADOW_COLOR,
  PROP_BACKGROUND_COLOR,
  PROP_LAYOUT_X,
  PROP_LAYOUT_Y,
  PROP_LAYOUT_WIDTH,
  PROP_LAYOUT_HEIGHT,
  PROP_TEXT_ALIGNMENT,
  PROP_PARAGRAPH_ALIGNMENT,
  PROP_ENABLE_COLOR_FONT,
};

enum class GstDWriteBaseOverlayBlendMode
{
  UNKNOWN,

  /* attach meta with d3d11 texture buffer. */
  ATTACH_TEXTURE,

  /* attach meta with bitmap buffer */
  ATTACH_BITMAP,

  /* software blending */
  SW_BLEND,

  /* 1) renders text on BGRA
   * 2) blends */
  BLEND,

  /* 1) convert texture to BGRA
   * 2) render text on another BGRA texture
   * 3) blends two textures
   * 3) converts back to original format */
  CONVERT,

  /* 1) converts texture to RGBA64_LE
   * 2) renders text on BGRA texture
   * 3) blends two textures
   * 3) converts back original format */
  CONVERT_64,
};

#define DEFAULT_VISIBLE TRUE
#define DEFAULT_FONT_FAMILY "MS Reference Sans Serif"
#define DEFAULT_FONT_SIZE 24
#define DEFAULT_REFERENCE_FRAME_SIZE 640
#define DEFAULT_AUTO_RESIZE TRUE
#define DEFAULT_FONT_WEIGHT DWRITE_FONT_WEIGHT_NORMAL
#define DEFAULT_FONT_STYLE DWRITE_FONT_STYLE_NORMAL
#define DEFAULT_FONT_STRETCH DWRITE_FONT_STRETCH_NORMAL
#define DEFAULT_COLOR 0xffffffff
#define DEFAULT_OUTLINE_COLOR 0xff000000
#define DEFAULT_UNDERLINE_COLOR 0x0
#define DEFAULT_UNDERLINE_OUTLINE_COLOR 0x0
#define DEFAULT_STRIKETHROUGH_COLOR 0x0
#define DEFAULT_STRIKETHROUGH_OUTLINE_COLOR 0x0
#define DEFAULT_SHADOW_COLOR 0x80000000
#define DEFAULT_BACKGROUND_COLOR 0x0
#define DEFAULT_LAYOUT_XY 0.03f
#define DEFAULT_LAYOUT_WH 0.92f
#define DEFAULT_TEXT_ALIGNMENT DWRITE_TEXT_ALIGNMENT_LEADING
#define DEFAULT_PARAGRAPH_ALIGNMENT DWRITE_PARAGRAPH_ALIGNMENT_NEAR
#define DEFAULT_COLOR_FONT TRUE

/* *INDENT-OFF* */
struct _GstDWriteBaseOverlayPrivate
{
  GstPad *video_pad = nullptr;
  GstPad *text_pad = nullptr;
  GstPad *src_pad = nullptr;

  GstD3D11Device *device = nullptr;

  GstVideoInfo bgra_info;
  GstSegment text_segment;

  std::mutex prop_lock;
  std::condition_variable cond;
  std::recursive_mutex context_lock;

  ComPtr<IDWriteFactory> dwrite_factory;
  ComPtr<IDWriteFontCollection> font_collection;
  ComPtr<IDWriteTextFormat> text_format;
  ComPtr<IDWriteTextLayout> layout;

  ComPtr<ID2D1Factory> d2d_factory;

  ComPtr<IGstDWriteTextRenderer> renderer;

  GstD3D11Converter *pre_conv = nullptr;
  GstD3D11Converter *blend_conv = nullptr;
  GstD3D11Converter *post_conv = nullptr;

  GstDWriteBaseOverlayBlendMode blend_mode =
      GstDWriteBaseOverlayBlendMode::UNKNOWN;
  gboolean attach_meta = FALSE;
  gboolean is_d3d11 = FALSE;

  GstBufferPool *bitmap_pool = nullptr;
  GstBufferPool *text_pool = nullptr;
  GstBufferPool *blend_pool = nullptr;
  GstBuffer *text_buf = nullptr;
  GstVideoOverlayRectangle *overlay_rect = nullptr;
  D2D_POINT_2F layout_origin;
  D2D_POINT_2F layout_size;
  D2D1_RECT_F background_padding;

  std::wstring prev_text;
  std::wstring cur_text;

  /* properties */
  gboolean visible = DEFAULT_VISIBLE;
  std::string font_family = DEFAULT_FONT_FAMILY;
  gfloat font_size = DEFAULT_FONT_SIZE;
  guint reference_frame_size = DEFAULT_REFERENCE_FRAME_SIZE;
  gboolean auto_resize = DEFAULT_AUTO_RESIZE;
  DWRITE_FONT_WEIGHT font_weight = DEFAULT_FONT_WEIGHT;
  DWRITE_FONT_STYLE font_style = DEFAULT_FONT_STYLE;
  DWRITE_FONT_STRETCH font_stretch = DEFAULT_FONT_STRETCH;

  std::wstring default_text;
  guint text_color = DEFAULT_COLOR;
  guint outline_color = DEFAULT_OUTLINE_COLOR;
  guint underline_color = DEFAULT_UNDERLINE_COLOR;
  guint underline_outline_color = DEFAULT_UNDERLINE_OUTLINE_COLOR;
  guint strikethrough_color = DEFAULT_STRIKETHROUGH_COLOR;
  guint strikethrough_outline_color = DEFAULT_STRIKETHROUGH_OUTLINE_COLOR;
  guint shadow_color = DEFAULT_SHADOW_COLOR;
  guint background_color = DEFAULT_BACKGROUND_COLOR;

  gdouble layout_x = DEFAULT_LAYOUT_XY;
  gdouble layout_y = DEFAULT_LAYOUT_XY;
  gdouble layout_width = DEFAULT_LAYOUT_WH;
  gdouble layout_height = DEFAULT_LAYOUT_WH;
  DWRITE_TEXT_ALIGNMENT text_align = DEFAULT_TEXT_ALIGNMENT;
  DWRITE_PARAGRAPH_ALIGNMENT paragraph_align = DEFAULT_PARAGRAPH_ALIGNMENT;

  gboolean color_font = FALSE;
};
/* *INDENT-ON* */

static void gst_dwrite_base_overlay_finalize (GObject * object);
static void gst_dwrite_base_overlay_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_dwrite_base_overlay_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_dwrite_base_overlay_set_context (GstElement * elem,
    GstContext * context);
static gboolean gst_dwrite_base_overlay_start (GstBaseTransform * trans);
static gboolean gst_dwrite_base_overlay_stop (GstBaseTransform * trans);
static gboolean gst_dwrite_base_overlay_query (GstBaseTransform * trans,
    GstPadDirection direction, GstQuery * query);
static gboolean gst_dwrite_base_overlay_set_caps (GstBaseTransform * trans,
    GstCaps * incaps, GstCaps * outcaps);
static gboolean
gst_dwrite_base_overlay_decide_allocation (GstBaseTransform * trans,
    GstQuery * query);
static gboolean
gst_dwrite_base_overlay_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query);
static GstCaps *gst_dwrite_base_overlay_transform_caps (GstBaseTransform *
    trans, GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static void gst_dwrite_base_overlay_before_transform (GstBaseTransform * trans,
    GstBuffer * buf);
static GstFlowReturn
gst_dwrite_base_overlay_prepare_output_buffer (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer ** outbuf);
static GstFlowReturn gst_dwrite_base_overlay_transform (GstBaseTransform *
    trans, GstBuffer * inbuf, GstBuffer * outbuf);

#define gst_dwrite_base_overlay_parent_class parent_class
G_DEFINE_TYPE (GstDWriteBaseOverlay, gst_dwrite_base_overlay,
    GST_TYPE_BASE_TRANSFORM);

static void
gst_dwrite_base_overlay_class_init (GstDWriteBaseOverlayClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *trans_class = GST_BASE_TRANSFORM_CLASS (klass);

  object_class->finalize = gst_dwrite_base_overlay_finalize;
  object_class->set_property = gst_dwrite_base_overlay_set_property;
  object_class->get_property = gst_dwrite_base_overlay_get_property;

  g_object_class_install_property (object_class, PROP_VISIBLE,
      g_param_spec_boolean ("visible", "Visible",
          "Whether to draw text", DEFAULT_VISIBLE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (object_class, PROP_FONT_FAMILY,
      g_param_spec_string ("font-family", "Font Family",
          "Font family to use", DEFAULT_FONT_FAMILY,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (object_class, PROP_FONT_SIZE,
      g_param_spec_float ("font-size", "Font Size",
          "Font size to use", 0.1f, 1638.f, DEFAULT_FONT_SIZE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (object_class, PROP_REFERENCE_FRAME_SIZE,
      g_param_spec_uint ("reference-frame-size", "Reference Frame Size",
          "Reference Frame size used for \"auto-resize\"", 16, 16384,
          DEFAULT_REFERENCE_FRAME_SIZE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (object_class, PROP_AUTO_RESIZE,
      g_param_spec_boolean ("auto-resize", "Auto Resize",
          "Calculate font size to be equivalent to \"font-size\" at "
          "\"reference-frame-size\"", DEFAULT_AUTO_RESIZE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (object_class, PROP_FONT_WEIGHT,
      g_param_spec_enum ("font-weight", "Font Weight",
          "Font Weight", GST_TYPE_DWRITE_FONT_WEIGHT,
          DEFAULT_FONT_WEIGHT,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (object_class, PROP_FONT_STYLE,
      g_param_spec_enum ("font-style", "Font Style",
          "Font Style", GST_TYPE_DWRITE_FONT_STYLE,
          DEFAULT_FONT_STYLE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (object_class, PROP_FONT_STRETCH,
      g_param_spec_enum ("font-stretch", "Font Stretch",
          "Font Stretch", GST_TYPE_DWRITE_FONT_STRETCH,
          DEFAULT_FONT_STRETCH,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (object_class, PROP_TEXT,
      g_param_spec_string ("text", "Text",
          "Text to render", "",
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (object_class, PROP_COLOR,
      g_param_spec_uint ("color", "Color",
          "Text color to use (big-endian ARGB)", 0, G_MAXUINT32, DEFAULT_COLOR,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (object_class, PROP_OUTLINE_COLOR,
      g_param_spec_uint ("outline-color", "Outline Color",
          "Text outline color to use (big-endian ARGB)", 0, G_MAXUINT32,
          DEFAULT_OUTLINE_COLOR,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (object_class, PROP_UNDERLINE_COLOR,
      g_param_spec_uint ("underline-color", "Underline Color",
          "Underline color to use (big-endian ARGB)", 0, G_MAXUINT32,
          DEFAULT_UNDERLINE_COLOR,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (object_class, PROP_UNDERLINE_OUTLINE_COLOR,
      g_param_spec_uint ("underline-outline-color", "Underline Outline Color",
          "Outline of underline color to use (big-endian ARGB)", 0, G_MAXUINT32,
          DEFAULT_UNDERLINE_OUTLINE_COLOR,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (object_class, PROP_STRIKETHROUGH_COLOR,
      g_param_spec_uint ("strikethrough-color", "Strikethrough Color",
          "Strikethrough color to use (big-endian ARGB)", 0, G_MAXUINT32,
          DEFAULT_STRIKETHROUGH_COLOR,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (object_class,
      PROP_STRIKETHROUGH_OUTLINE_COLOR,
      g_param_spec_uint ("strikethrough-outline-color",
          "Strikethrough Outline Color",
          "Outline of strikethrough color to use (big-endian ARGB)",
          0, G_MAXUINT32, DEFAULT_STRIKETHROUGH_OUTLINE_COLOR,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (object_class, PROP_SHADOW_COLOR,
      g_param_spec_uint ("shadow-color", "Shadow Color",
          "Shadow color to use (big-endian ARGB)", 0, G_MAXUINT32,
          DEFAULT_SHADOW_COLOR,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (object_class, PROP_BACKGROUND_COLOR,
      g_param_spec_uint ("background-color", "Background Color",
          "Background color to use (big-endian ARGB)", 0, G_MAXUINT32,
          DEFAULT_BACKGROUND_COLOR,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (object_class, PROP_LAYOUT_X,
      g_param_spec_double ("layout-x", "Layout X",
          "Normalized X coordinate of text layout", 0, 1,
          DEFAULT_LAYOUT_XY,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (object_class, PROP_LAYOUT_Y,
      g_param_spec_double ("layout-y", "Layout Y",
          "Normalized Y coordinate of text layout", 0, 1,
          DEFAULT_LAYOUT_XY,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (object_class, PROP_LAYOUT_WIDTH,
      g_param_spec_double ("layout-width", "Layout Width",
          "Normalized width of text layout", 0, 1,
          DEFAULT_LAYOUT_WH,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (object_class, PROP_LAYOUT_HEIGHT,
      g_param_spec_double ("layout-height", "Layout Height",
          "Normalized height of text layout", 0, 1,
          DEFAULT_LAYOUT_WH,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (object_class, PROP_TEXT_ALIGNMENT,
      g_param_spec_enum ("text-alignment", "Text Alignment",
          "Text Alignment", GST_TYPE_DWRITE_TEXT_ALIGNMENT,
          DEFAULT_TEXT_ALIGNMENT,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (object_class, PROP_PARAGRAPH_ALIGNMENT,
      g_param_spec_enum ("paragraph-alignment", "Paragraph alignment",
          "Paragraph Alignment", GST_TYPE_DWRITE_PARAGRAPH_ALIGNMENT,
          DEFAULT_PARAGRAPH_ALIGNMENT,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
#ifdef HAVE_DWRITE_COLOR_FONT
  if (gst_dwrite_is_windows_10_or_greater ()) {
    g_object_class_install_property (object_class, PROP_VISIBLE,
        g_param_spec_boolean ("color-font", "Color Font",
            "Enable color font, requires Windows 10 or newer",
            DEFAULT_COLOR_FONT,
            (GParamFlags) (GST_PARAM_CONDITIONALLY_AVAILABLE |
                G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  }
#endif

  gst_element_class_add_static_pad_template (element_class, &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);

  element_class->set_context =
      GST_DEBUG_FUNCPTR (gst_dwrite_base_overlay_set_context);

  trans_class->start = GST_DEBUG_FUNCPTR (gst_dwrite_base_overlay_start);
  trans_class->stop = GST_DEBUG_FUNCPTR (gst_dwrite_base_overlay_stop);
  trans_class->query = GST_DEBUG_FUNCPTR (gst_dwrite_base_overlay_query);
  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_dwrite_base_overlay_set_caps);
  trans_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_dwrite_base_overlay_decide_allocation);
  trans_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_dwrite_base_overlay_propose_allocation);
  trans_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_dwrite_base_overlay_transform_caps);
  trans_class->before_transform =
      GST_DEBUG_FUNCPTR (gst_dwrite_base_overlay_before_transform);
  trans_class->prepare_output_buffer =
      GST_DEBUG_FUNCPTR (gst_dwrite_base_overlay_prepare_output_buffer);
  trans_class->transform =
      GST_DEBUG_FUNCPTR (gst_dwrite_base_overlay_transform);

  GST_DEBUG_CATEGORY_INIT (dwrite_base_overlay_debug,
      "dwritebaseoverlay", 0, "dwritebaseoverlay");

  gst_type_mark_as_plugin_api (GST_TYPE_DWRITE_BASE_OVERLAY,
      (GstPluginAPIFlags) 0);
}

static void
gst_dwrite_base_overlay_init (GstDWriteBaseOverlay * self)
{
  GstDWriteBaseOverlayPrivate *priv = self->priv;

  self->priv = priv = new GstDWriteBaseOverlayPrivate ();
  if (gst_dwrite_is_windows_10_or_greater ())
    priv->color_font = DEFAULT_COLOR_FONT;
}

static void
gst_dwrite_base_overlay_finalize (GObject * object)
{
  GstDWriteBaseOverlay *self = GST_DWRITE_BASE_OVERLAY (object);

  delete self->priv;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_dwrite_base_overlay_clear_layout (GstDWriteBaseOverlay * self)
{
  GstDWriteBaseOverlayPrivate *priv = self->priv;

  priv->layout = nullptr;
  gst_clear_buffer (&priv->text_buf);
  g_clear_pointer (&priv->overlay_rect, gst_video_overlay_rectangle_unref);
}

static void
update_uint (GstDWriteBaseOverlay * self, guint * prev, const GValue * value)
{
  guint val = g_value_get_uint (value);
  if (val != *prev) {
    *prev = val;
    gst_dwrite_base_overlay_clear_layout (self);
  }
}

static void
update_double (GstDWriteBaseOverlay * self, gdouble * prev,
    const GValue * value)
{
  gdouble val = g_value_get_double (value);
  if (val != *prev) {
    *prev = val;
    gst_dwrite_base_overlay_clear_layout (self);
  }
}

static void
update_enum (GstDWriteBaseOverlay * self, gint * prev, const GValue * value)
{
  gint val = g_value_get_enum (value);
  if (val != *prev) {
    *prev = val;
    gst_dwrite_base_overlay_clear_layout (self);
  }
}

static void
gst_dwrite_base_overlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDWriteBaseOverlay *self = GST_DWRITE_BASE_OVERLAY (object);
  GstDWriteBaseOverlayPrivate *priv = self->priv;
  std::lock_guard < std::mutex > lk (priv->prop_lock);

  switch (prop_id) {
    case PROP_VISIBLE:
      priv->visible = g_value_get_boolean (value);
      break;
    case PROP_FONT_FAMILY:{
      const gchar *font_family = g_value_get_string (value);
      std::string font;

      if (font_family)
        font = font_family;
      else
        font = DEFAULT_FONT_FAMILY;

      if (font != priv->font_family) {
        priv->font_family = font;
        priv->text_format = nullptr;
        gst_dwrite_base_overlay_clear_layout (self);
      }
      break;
    }
    case PROP_FONT_SIZE:
    {
      gfloat font_size = g_value_get_float (value);
      if (font_size != priv->font_size) {
        priv->font_size = font_size;
        priv->text_format = nullptr;
        gst_dwrite_base_overlay_clear_layout (self);
      }
      break;
    }
    case PROP_REFERENCE_FRAME_SIZE:
    {
      guint size = g_value_get_uint (value);
      if (size != priv->reference_frame_size) {
        priv->reference_frame_size = size;
        priv->text_format = nullptr;
        gst_dwrite_base_overlay_clear_layout (self);
      }
      break;
    }
    case PROP_AUTO_RESIZE:
    {
      gboolean auto_resize = g_value_get_uint (value);
      if (auto_resize != priv->auto_resize) {
        priv->auto_resize = auto_resize;
        priv->text_format = nullptr;
        gst_dwrite_base_overlay_clear_layout (self);
      }
      break;
    }
    case PROP_FONT_WEIGHT:
    {
      DWRITE_FONT_WEIGHT weight = (DWRITE_FONT_WEIGHT) g_value_get_enum (value);
      if (weight != priv->font_weight) {
        priv->font_weight = weight;
        priv->text_format = nullptr;
        gst_dwrite_base_overlay_clear_layout (self);
      }
      break;
    }
    case PROP_FONT_STYLE:
    {
      DWRITE_FONT_STYLE style = (DWRITE_FONT_STYLE) g_value_get_enum (value);
      if (style != priv->font_style) {
        priv->font_style = style;
        priv->text_format = nullptr;
        gst_dwrite_base_overlay_clear_layout (self);
      }
      break;
    }
    case PROP_FONT_STRETCH:
    {
      DWRITE_FONT_STRETCH stretch =
          (DWRITE_FONT_STRETCH) g_value_get_enum (value);
      if (stretch != priv->font_stretch) {
        priv->font_stretch = stretch;
        priv->text_format = nullptr;
        gst_dwrite_base_overlay_clear_layout (self);
      }
      break;
    }
    case PROP_TEXT:
    {
      const gchar *new_text = g_value_get_string (value);
      std::wstring new_string;

      if (new_text)
        new_string = gst_dwrite_string_to_wstring (new_text);

      if (priv->default_text != new_string) {
        priv->default_text = new_string;
        gst_dwrite_base_overlay_clear_layout (self);
      }
      break;
    }
    case PROP_COLOR:
      update_uint (self, &priv->text_color, value);
      break;
    case PROP_OUTLINE_COLOR:
      update_uint (self, &priv->outline_color, value);
      break;
    case PROP_UNDERLINE_COLOR:
      update_uint (self, &priv->underline_color, value);
      break;
    case PROP_UNDERLINE_OUTLINE_COLOR:
      update_uint (self, &priv->underline_outline_color, value);
      break;
    case PROP_STRIKETHROUGH_COLOR:
      update_uint (self, &priv->strikethrough_color, value);
      break;
    case PROP_STRIKETHROUGH_OUTLINE_COLOR:
      update_uint (self, &priv->strikethrough_outline_color, value);
      break;
    case PROP_SHADOW_COLOR:
      update_uint (self, &priv->shadow_color, value);
      break;
    case PROP_BACKGROUND_COLOR:
      update_uint (self, &priv->background_color, value);
      break;
    case PROP_LAYOUT_X:
      update_double (self, &priv->layout_x, value);
      break;
    case PROP_LAYOUT_Y:
      update_double (self, &priv->layout_y, value);
      break;
    case PROP_LAYOUT_WIDTH:
      update_double (self, &priv->layout_width, value);
      break;
    case PROP_LAYOUT_HEIGHT:
      update_double (self, &priv->layout_height, value);
      break;
    case PROP_TEXT_ALIGNMENT:
      update_enum (self, (gint *) & priv->text_align, value);
      break;
    case PROP_PARAGRAPH_ALIGNMENT:
      update_enum (self, (gint *) & priv->paragraph_align, value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_dwrite_base_overlay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstDWriteBaseOverlay *self = GST_DWRITE_BASE_OVERLAY (object);
  GstDWriteBaseOverlayPrivate *priv = self->priv;
  std::lock_guard < std::mutex > lk (priv->prop_lock);

  switch (prop_id) {
    case PROP_VISIBLE:
      g_value_set_boolean (value, priv->visible);
      break;
    case PROP_FONT_FAMILY:
      g_value_set_string (value, priv->font_family.c_str ());
      break;
    case PROP_FONT_SIZE:
      g_value_set_float (value, priv->font_size);
      break;
    case PROP_REFERENCE_FRAME_SIZE:
      g_value_set_uint (value, priv->reference_frame_size);
      break;
    case PROP_AUTO_RESIZE:
      g_value_set_boolean (value, priv->auto_resize);
      break;
    case PROP_FONT_WEIGHT:
      g_value_set_enum (value, priv->font_weight);
      break;
    case PROP_FONT_STYLE:
      g_value_set_enum (value, priv->font_style);
      break;
    case PROP_FONT_STRETCH:
      g_value_set_enum (value, priv->font_stretch);
      break;
    case PROP_TEXT:
      if (priv->default_text.empty ()) {
        g_value_set_string (value, "");
      } else {
        std::string str = gst_dwrite_wstring_to_string (priv->default_text);
        g_value_set_string (value, str.c_str ());
      }
      break;
    case PROP_COLOR:
      g_value_set_uint (value, priv->text_color);
      break;
    case PROP_OUTLINE_COLOR:
      g_value_set_uint (value, priv->outline_color);
      break;
    case PROP_UNDERLINE_COLOR:
      g_value_set_uint (value, priv->underline_color);
      break;
    case PROP_UNDERLINE_OUTLINE_COLOR:
      g_value_set_uint (value, priv->underline_outline_color);
      break;
    case PROP_STRIKETHROUGH_COLOR:
      g_value_set_uint (value, priv->strikethrough_color);
      break;
    case PROP_STRIKETHROUGH_OUTLINE_COLOR:
      g_value_set_uint (value, priv->strikethrough_outline_color);
      break;
    case PROP_SHADOW_COLOR:
      g_value_set_uint (value, priv->shadow_color);
      break;
    case PROP_BACKGROUND_COLOR:
      g_value_set_uint (value, priv->background_color);
      break;
    case PROP_LAYOUT_X:
      g_value_set_double (value, priv->layout_x);
      break;
    case PROP_LAYOUT_Y:
      g_value_set_double (value, priv->layout_y);
      break;
    case PROP_LAYOUT_WIDTH:
      g_value_set_double (value, priv->layout_width);
      break;
    case PROP_LAYOUT_HEIGHT:
      g_value_set_double (value, priv->layout_height);
      break;
    case PROP_TEXT_ALIGNMENT:
      g_value_set_enum (value, priv->text_align);
      break;
    case PROP_PARAGRAPH_ALIGNMENT:
      g_value_set_enum (value, priv->paragraph_align);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_dwrite_base_overlay_set_context (GstElement * elem, GstContext * context)
{
  GstDWriteBaseOverlay *self = GST_DWRITE_BASE_OVERLAY (elem);
  GstDWriteBaseOverlayPrivate *priv = self->priv;

  priv->context_lock.lock ();
  gst_d3d11_handle_set_context (elem, context, -1, &priv->device);
  priv->context_lock.unlock ();

  GST_ELEMENT_CLASS (parent_class)->set_context (elem, context);
}

static gboolean
gst_dwrite_base_overlay_start (GstBaseTransform * trans)
{
  GstDWriteBaseOverlay *self = GST_DWRITE_BASE_OVERLAY (trans);
  GstDWriteBaseOverlayPrivate *priv = self->priv;
  HRESULT hr;

  hr = DWriteCreateFactory (DWRITE_FACTORY_TYPE_SHARED,
      __uuidof (IDWriteFactory), (IUnknown **) (&priv->dwrite_factory));
  if (FAILED (hr)) {
    GST_ERROR_OBJECT (self, "Couldn't create dwrite factory");
    goto error;
  }

  hr = D2D1CreateFactory (D2D1_FACTORY_TYPE_MULTI_THREADED,
      IID_PPV_ARGS (&priv->d2d_factory));
  if (FAILED (hr)) {
    GST_ERROR_OBJECT (self, "Couldn't create d2d factory");
    goto error;
  }

  hr = IGstDWriteTextRenderer::CreateInstance (priv->dwrite_factory.Get (),
      &priv->renderer);
  if (FAILED (hr)) {
    GST_ERROR_OBJECT (self, "Couldn't create renderer");
    goto error;
  }

  gst_video_info_init (&self->info);
  priv->blend_mode = GstDWriteBaseOverlayBlendMode::UNKNOWN;

  return TRUE;

error:
  priv->renderer = nullptr;
  priv->dwrite_factory = nullptr;
  priv->d2d_factory = nullptr;

  return FALSE;
}

static void
gst_dwrite_base_overlay_clear_resource (GstDWriteBaseOverlay * self)
{
  GstDWriteBaseOverlayPrivate *priv = self->priv;

  gst_dwrite_base_overlay_clear_layout (self);

  if (priv->text_pool) {
    gst_buffer_pool_set_active (priv->text_pool, FALSE);
    gst_clear_object (&priv->text_pool);
  }

  if (priv->blend_pool) {
    gst_buffer_pool_set_active (priv->blend_pool, FALSE);
    gst_clear_object (&priv->blend_pool);
  }

  if (priv->bitmap_pool) {
    gst_buffer_pool_set_active (priv->bitmap_pool, FALSE);
    gst_clear_object (&priv->bitmap_pool);
  }

  gst_clear_object (&priv->pre_conv);
  gst_clear_object (&priv->blend_conv);
  gst_clear_object (&priv->post_conv);
}

static gboolean
gst_dwrite_base_overlay_stop (GstBaseTransform * trans)
{
  GstDWriteBaseOverlay *self = GST_DWRITE_BASE_OVERLAY (trans);
  GstDWriteBaseOverlayPrivate *priv = self->priv;

  gst_dwrite_base_overlay_clear_resource (self);

  priv->renderer = nullptr;
  priv->dwrite_factory = nullptr;
  priv->d2d_factory = nullptr;
  gst_clear_object (&priv->device);

  return TRUE;
}

static gboolean
gst_dwrite_base_overlay_query (GstBaseTransform * trans,
    GstPadDirection direction, GstQuery * query)
{
  GstDWriteBaseOverlay *self = GST_DWRITE_BASE_OVERLAY (trans);
  GstDWriteBaseOverlayPrivate *priv = self->priv;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
    {
      std::lock_guard < std::recursive_mutex > lk (priv->context_lock);
      if (gst_d3d11_handle_context_query (GST_ELEMENT (self),
              query, priv->device)) {
        return TRUE;
      }
      break;
    }
    default:
      break;
  }

  return GST_BASE_TRANSFORM_CLASS (parent_class)->query (trans,
      direction, query);
}

static gboolean
gst_dwrite_base_overlay_decide_allocation (GstBaseTransform * trans,
    GstQuery * query)
{
  GstDWriteBaseOverlay *self = GST_DWRITE_BASE_OVERLAY (trans);
  GstDWriteBaseOverlayPrivate *priv = self->priv;
  guint min, max, size;
  gboolean update_pool;
  gboolean is_d3d11 = FALSE;
  GstCaps *caps = nullptr;
  GstVideoInfo info;
  GstBufferPool *pool = nullptr;
  GstCapsFeatures *features;
  GstStructure *config;

  GST_DEBUG_OBJECT (self, "Decide allocation");

  gst_query_parse_allocation (query, &caps, nullptr);
  if (!caps) {
    GST_WARNING_OBJECT (self, "Query without caps");
    return FALSE;
  }

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (self, "Invalid caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }

  features = gst_caps_get_features (caps, 0);
  if (features && gst_caps_features_contains (features,
          GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY)) {
    is_d3d11 = TRUE;
  }

  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
    update_pool = TRUE;
  } else {
    min = max = 0;
    size = info.size;
  }

  if (pool && is_d3d11) {
    std::lock_guard < std::recursive_mutex > lk (priv->context_lock);
    if (!gst_d3d11_ensure_element_data (GST_ELEMENT_CAST (self), -1,
            &priv->device)) {
      GST_ERROR_OBJECT (self, "Couldn't create deice");
      return FALSE;
    }

    if (!GST_IS_D3D11_BUFFER_POOL (pool)) {
      gst_clear_object (&pool);
    } else {
      GstD3D11BufferPool *dpool = GST_D3D11_BUFFER_POOL (pool);
      if (dpool->device != priv->device)
        gst_clear_object (&pool);
    }
  }

  if (!pool) {
    if (is_d3d11)
      pool = gst_d3d11_buffer_pool_new (priv->device);
    else
      pool = gst_video_buffer_pool_new ();
  }

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  gst_buffer_pool_config_set_params (config, caps, size, min, max);

  if (is_d3d11) {
    GstD3D11AllocationParams *params;
    guint bind_flags = 0;
    GstD3D11Format d3d11_format;

    gst_d3d11_device_get_format (priv->device, GST_VIDEO_INFO_FORMAT (&info),
        &d3d11_format);
    if ((d3d11_format.format_support[0] &
            D3D11_FORMAT_SUPPORT_SHADER_SAMPLE) != 0) {
      bind_flags |= D3D11_BIND_SHADER_RESOURCE;
    }

    if ((d3d11_format.format_support[0] &
            D3D11_FORMAT_SUPPORT_RENDER_TARGET) != 0) {
      bind_flags |= D3D11_BIND_RENDER_TARGET;
    }

    params = gst_buffer_pool_config_get_d3d11_allocation_params (config);
    if (!params) {
      params = gst_d3d11_allocation_params_new (priv->device, &info,
          GST_D3D11_ALLOCATION_FLAG_DEFAULT, bind_flags, 0);
    } else {
      for (guint i = 0; i < GST_VIDEO_INFO_N_PLANES (&info); i++)
        params->desc[i].BindFlags |= bind_flags;
    }

    gst_buffer_pool_config_set_d3d11_allocation_params (config, params);
    gst_d3d11_allocation_params_free (params);
  }

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_ERROR_OBJECT (self, "Couldn't set config");
    gst_object_unref (pool);
    return FALSE;
  }

  /* Get updated size in case of d3d11 */
  if (is_d3d11) {
    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_get_params (config, nullptr, &size, nullptr,
        nullptr);
    gst_structure_free (config);
  }

  if (update_pool)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);

  gst_object_unref (pool);

  return GST_BASE_TRANSFORM_CLASS (parent_class)->decide_allocation (trans,
      query);
}

static gboolean
gst_dwrite_base_overlay_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query)
{
  GstDWriteBaseOverlay *self = GST_DWRITE_BASE_OVERLAY (trans);
  GstDWriteBaseOverlayPrivate *priv = self->priv;
  GstCaps *caps = nullptr;
  GstVideoInfo info;
  GstCapsFeatures *features;
  gboolean is_d3d11 = FALSE;
  gboolean has_meta = FALSE;

  GST_DEBUG_OBJECT (self, "Propose allocation");

  if (!GST_BASE_TRANSFORM_CLASS (parent_class)->propose_allocation (trans,
          decide_query, query))
    return FALSE;

  if (!decide_query)
    return TRUE;

  gst_query_parse_allocation (query, &caps, nullptr);

  if (!caps) {
    GST_WARNING_OBJECT (self, "Query without caps");
    return FALSE;
  }

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (self, "Invalid caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }

  features = gst_caps_get_features (caps, 0);
  if (features) {
    if (gst_caps_features_contains (features,
            GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY)) {
      is_d3d11 = TRUE;
    }

    if (gst_caps_features_contains (features,
            GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION)) {
      has_meta = TRUE;
    }
  }

  if (gst_query_get_n_allocation_pools (query) == 0) {
    GstBufferPool *pool;
    GstStructure *config;
    guint size;

    if (is_d3d11) {
      std::lock_guard < std::recursive_mutex > lk (priv->context_lock);
      if (!gst_d3d11_ensure_element_data (GST_ELEMENT_CAST (self), -1,
              &priv->device)) {
        GST_ERROR_OBJECT (self, "Couldn't create deice");
        return FALSE;
      }

      pool = gst_d3d11_buffer_pool_new (priv->device);
    } else {
      pool = gst_video_buffer_pool_new ();
    }

    GST_DEBUG_OBJECT (self, "Creating new pool with caps %" GST_PTR_FORMAT,
        caps);

    config = gst_buffer_pool_get_config (pool);

    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);
    if (!is_d3d11) {
      gst_buffer_pool_config_add_option (config,
          GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);
    }

    size = info.size;
    gst_buffer_pool_config_set_params (config, caps, size, 0, 0);

    if (is_d3d11) {
      GstD3D11AllocationParams *params;
      guint bind_flags = 0;
      GstD3D11Format d3d11_format;

      gst_d3d11_device_get_format (priv->device, GST_VIDEO_INFO_FORMAT (&info),
          &d3d11_format);
      if ((d3d11_format.format_support[0] &
              D3D11_FORMAT_SUPPORT_SHADER_SAMPLE) != 0) {
        bind_flags |= D3D11_BIND_SHADER_RESOURCE;
      }

      if ((d3d11_format.format_support[0] &
              D3D11_FORMAT_SUPPORT_RENDER_TARGET) != 0) {
        bind_flags |= D3D11_BIND_RENDER_TARGET;
      }

      params = gst_buffer_pool_config_get_d3d11_allocation_params (config);
      if (!params) {
        params = gst_d3d11_allocation_params_new (priv->device, &info,
            GST_D3D11_ALLOCATION_FLAG_DEFAULT, bind_flags, 0);
      } else {
        for (guint i = 0; i < GST_VIDEO_INFO_N_PLANES (&info); i++)
          params->desc[i].BindFlags |= bind_flags;
      }

      gst_buffer_pool_config_set_d3d11_allocation_params (config, params);
      gst_d3d11_allocation_params_free (params);
    }

    gst_buffer_pool_config_set_params (config, caps, size, 0, 0);

    if (!gst_buffer_pool_set_config (pool, config)) {
      GST_ERROR_OBJECT (self, "Couldn't set config");
      gst_object_unref (pool);
      return FALSE;
    }

    /* Get updated size in case of d3d11 */
    if (is_d3d11) {
      config = gst_buffer_pool_get_config (pool);
      gst_buffer_pool_config_get_params (config, nullptr, &size, nullptr,
          nullptr);
      gst_structure_free (config);
    }

    gst_query_add_allocation_pool (query, pool, size, 0, 0);
    gst_object_unref (pool);
  }

  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, nullptr);

  if (has_meta && !gst_query_find_allocation_meta (query,
          GST_VIDEO_OVERLAY_COMPOSITION_META_API_TYPE, nullptr)) {
    gst_query_add_allocation_meta (query,
        GST_VIDEO_OVERLAY_COMPOSITION_META_API_TYPE, nullptr);
  }

  return TRUE;
}

static GstCaps *
gst_dwrite_base_overlay_add_feature (GstCaps * caps)
{
  GstCaps *new_caps = gst_caps_new_empty ();
  guint caps_size = gst_caps_get_size (caps);

  for (guint i = 0; i < caps_size; i++) {
    GstStructure *s = gst_caps_get_structure (caps, i);
    GstCapsFeatures *f =
        gst_caps_features_copy (gst_caps_get_features (caps, i));
    GstCaps *c = gst_caps_new_full (gst_structure_copy (s), nullptr);

    if (!gst_caps_features_contains (f,
            GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION)) {
      gst_caps_features_add (f,
          GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION);
    }

    gst_caps_set_features (c, 0, f);
    gst_caps_append (new_caps, c);
  }

  return new_caps;
}

static GstCaps *
gst_dwrite_overlay_remove_feature (GstCaps * caps)
{
  GstCaps *new_caps = gst_caps_new_empty ();
  guint caps_size = gst_caps_get_size (caps);

  for (guint i = 0; i < caps_size; i++) {
    GstStructure *s = gst_caps_get_structure (caps, i);
    GstCapsFeatures *f =
        gst_caps_features_copy (gst_caps_get_features (caps, i));
    GstCaps *c = gst_caps_new_full (gst_structure_copy (s), nullptr);

    gst_caps_features_remove (f,
        GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION);
    gst_caps_set_features (c, 0, f);
    gst_caps_append (new_caps, c);
  }

  return new_caps;
}

static GstCaps *
gst_dwrite_base_overlay_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstCaps *result, *tmp;

  GST_DEBUG_OBJECT (trans,
      "Transforming caps %" GST_PTR_FORMAT " in direction %s", caps,
      (direction == GST_PAD_SINK) ? "sink" : "src");

  if (direction == GST_PAD_SINK) {
    tmp = gst_dwrite_base_overlay_add_feature (caps);
    tmp = gst_caps_merge (tmp, gst_caps_ref (caps));
  } else {
    tmp = gst_dwrite_overlay_remove_feature (caps);
    tmp = gst_caps_merge (gst_caps_ref (caps), tmp);
  }

  if (filter) {
    result = gst_caps_intersect_full (filter, tmp, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (tmp);
  } else {
    result = tmp;
  }

  GST_DEBUG_OBJECT (trans, "returning caps: %" GST_PTR_FORMAT, result);

  return result;
}

static gboolean
is_subsampled_yuv (const GstVideoInfo * info)
{
  if (!GST_VIDEO_INFO_IS_YUV (info))
    return FALSE;

  for (guint i = 0; i < GST_VIDEO_MAX_COMPONENTS; i++) {
    if (info->finfo->w_sub[i] != 0 || info->finfo->h_sub[i] != 0)
      return TRUE;
  }

  return FALSE;
}

static GstD3D11Converter *
gst_dwrite_base_overlay_create_converter (GstDWriteBaseOverlay * self,
    const GstVideoInfo * in_info, const GstVideoInfo * out_info)
{
  GstDWriteBaseOverlayPrivate *priv = self->priv;
  GstStructure *config;
  D3D11_FILTER filter = D3D11_FILTER_MIN_MAG_MIP_POINT;

  if (is_subsampled_yuv (in_info) || is_subsampled_yuv (out_info))
    filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;

  config = gst_structure_new ("convert-config",
      GST_D3D11_CONVERTER_OPT_BACKEND, GST_TYPE_D3D11_CONVERTER_BACKEND,
      GST_D3D11_CONVERTER_BACKEND_SHADER,
      GST_D3D11_CONVERTER_OPT_SAMPLER_FILTER,
      GST_TYPE_D3D11_CONVERTER_SAMPLER_FILTER, filter, nullptr);

  return gst_d3d11_converter_new (priv->device, in_info, out_info, config);
}

static GstBufferPool *
gst_dwrite_base_overlay_create_d3d11_pool (GstDWriteBaseOverlay * self,
    const GstVideoInfo * info)
{
  GstDWriteBaseOverlayPrivate *priv = self->priv;
  GstCaps *caps = nullptr;
  GstStructure *config;
  GstD3D11AllocationParams *params;
  GstBufferPool *pool = nullptr;

  caps = gst_video_info_to_caps (info);
  if (!caps) {
    GST_ERROR_OBJECT (self, "Couldn't create caps");
    return nullptr;
  }

  pool = gst_d3d11_buffer_pool_new (priv->device);
  config = gst_buffer_pool_get_config (pool);

  params = gst_d3d11_allocation_params_new (priv->device, info,
      GST_D3D11_ALLOCATION_FLAG_DEFAULT,
      D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, 0);
  gst_buffer_pool_config_set_d3d11_allocation_params (config, params);
  gst_d3d11_allocation_params_free (params);
  gst_buffer_pool_config_set_params (config, caps, info->size, 0, 0);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_ERROR_OBJECT (self, "Couldn't set pool config");
    goto error;
  }

  if (!gst_buffer_pool_set_active (pool, TRUE)) {
    GST_ERROR_OBJECT (self, "Couldn't activate pool");
    goto error;
  }

  return pool;

error:
  gst_clear_caps (&caps);
  gst_clear_object (&pool);

  return nullptr;
}

static gboolean
gst_dwrite_base_overlay_setup_bitmap_pool (GstDWriteBaseOverlay * self,
    const GstVideoInfo * info)
{
  GstDWriteBaseOverlayPrivate *priv = self->priv;
  GstStructure *config;
  GstCaps *caps;

  if (priv->bitmap_pool) {
    gst_buffer_pool_set_active (priv->bitmap_pool, FALSE);
    gst_clear_object (&priv->bitmap_pool);
  }

  caps = gst_video_info_to_caps (info);

  priv->bitmap_pool = gst_dwrite_bitmap_pool_new ();
  config = gst_buffer_pool_get_config (priv->bitmap_pool);
  gst_buffer_pool_config_set_params (config, caps, info->size, 0, 0);
  gst_caps_unref (caps);

  if (!gst_buffer_pool_set_config (priv->bitmap_pool, config)) {
    GST_ERROR_OBJECT (self, "Couldn't set config");
    gst_clear_object (&priv->bitmap_pool);
    return FALSE;
  }

  if (!gst_buffer_pool_set_active (priv->bitmap_pool, TRUE)) {
    GST_ERROR_OBJECT (self, "Couldn't set active");
    gst_clear_object (&priv->bitmap_pool);
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_dwrite_base_overlay_prepare_resource (GstDWriteBaseOverlay * self)
{
  GstDWriteBaseOverlayPrivate *priv = self->priv;

  gst_dwrite_base_overlay_clear_resource (self);

  switch (priv->blend_mode) {
    case GstDWriteBaseOverlayBlendMode::ATTACH_TEXTURE:
      priv->text_pool = gst_dwrite_base_overlay_create_d3d11_pool (self,
          &priv->bgra_info);
      if (!priv->text_pool)
        goto error;
      break;
    case GstDWriteBaseOverlayBlendMode::ATTACH_BITMAP:
    case GstDWriteBaseOverlayBlendMode::SW_BLEND:
      if (!gst_dwrite_base_overlay_setup_bitmap_pool (self, &priv->bgra_info))
        goto error;
      break;
    case GstDWriteBaseOverlayBlendMode::BLEND:
      priv->text_pool = gst_dwrite_base_overlay_create_d3d11_pool (self,
          &priv->bgra_info);
      if (!priv->text_pool)
        goto error;

      priv->blend_conv = gst_dwrite_base_overlay_create_converter (self,
          &priv->bgra_info, &self->info);
      if (!priv->blend_conv) {
        GST_ERROR_OBJECT (self, "Couldn't create blend converter");
        goto error;
      }
      break;
    case GstDWriteBaseOverlayBlendMode::CONVERT:
      priv->text_pool = gst_dwrite_base_overlay_create_d3d11_pool (self,
          &priv->bgra_info);
      if (!priv->text_pool)
        goto error;

      priv->pre_conv = gst_dwrite_base_overlay_create_converter (self,
          &self->info, &priv->bgra_info);
      if (!priv->pre_conv)
        goto error;

      priv->blend_conv = gst_dwrite_base_overlay_create_converter (self,
          &priv->bgra_info, &priv->bgra_info);
      if (!priv->blend_conv) {
        GST_ERROR_OBJECT (self, "Couldn't create blend converter");
        goto error;
      }

      priv->post_conv = gst_dwrite_base_overlay_create_converter (self,
          &priv->bgra_info, &self->info);
      if (!priv->post_conv)
        goto error;
      break;
    case GstDWriteBaseOverlayBlendMode::CONVERT_64:
    {
      GstVideoInfo blend_info;

      gst_video_info_set_format (&blend_info, GST_VIDEO_FORMAT_RGBA64_LE,
          self->info.width, self->info.height);

      priv->blend_pool = gst_dwrite_base_overlay_create_d3d11_pool (self,
          &blend_info);
      if (!priv->blend_pool)
        goto error;

      priv->text_pool = gst_dwrite_base_overlay_create_d3d11_pool (self,
          &priv->bgra_info);
      if (!priv->text_pool)
        goto error;

      priv->pre_conv = gst_dwrite_base_overlay_create_converter (self,
          &self->info, &blend_info);
      if (!priv->pre_conv)
        goto error;

      priv->blend_conv = gst_dwrite_base_overlay_create_converter (self,
          &priv->bgra_info, &blend_info);
      if (!priv->blend_conv)
        goto error;

      priv->post_conv = gst_dwrite_base_overlay_create_converter (self,
          &blend_info, &self->info);
      if (!priv->post_conv)
        goto error;

      break;
    }
    default:
      g_assert_not_reached ();
      return FALSE;
  }

  if (priv->blend_conv) {
    D3D11_BLEND_DESC desc = { 0, };
    ComPtr < ID3D11BlendState > blend;
    ID3D11Device *device = gst_d3d11_device_get_device_handle (priv->device);
    HRESULT hr;

    desc.AlphaToCoverageEnable = FALSE;
    desc.IndependentBlendEnable = FALSE;
    desc.RenderTarget[0].BlendEnable = TRUE;
    desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    hr = device->CreateBlendState (&desc, &blend);
    if (!gst_d3d11_result (hr, priv->device)) {
      GST_ERROR_OBJECT (self, "Couldn't create blend state");
      goto error;
    }

    g_object_set (priv->blend_conv, "blend-state", blend.Get (),
        "src-alpha-mode", GST_D3D11_CONVERTER_ALPHA_MODE_PREMULTIPLIED,
        nullptr);
  }

  return TRUE;

error:
  gst_dwrite_base_overlay_clear_resource (self);
  return FALSE;
}

static const gchar *
blend_mode_to_string (GstDWriteBaseOverlayBlendMode mode)
{
  switch (mode) {
    case GstDWriteBaseOverlayBlendMode::ATTACH_TEXTURE:
      return "attach-texture";
    case GstDWriteBaseOverlayBlendMode::ATTACH_BITMAP:
      return "attach-bitmap";
    case GstDWriteBaseOverlayBlendMode::SW_BLEND:
      return "software-blend";
    case GstDWriteBaseOverlayBlendMode::BLEND:
      return "blend";
    case GstDWriteBaseOverlayBlendMode::CONVERT:
      return "convert";
    case GstDWriteBaseOverlayBlendMode::CONVERT_64:
      return "convert-64";
    default:
      return "unknown";
  }
}

static void
gst_dwrite_base_overlay_decide_blend_mode (GstDWriteBaseOverlay * self)
{
  GstDWriteBaseOverlayPrivate *priv = self->priv;

  priv->blend_mode = GstDWriteBaseOverlayBlendMode::UNKNOWN;

  if (priv->attach_meta) {
    if (priv->is_d3d11)
      priv->blend_mode = GstDWriteBaseOverlayBlendMode::ATTACH_TEXTURE;
    else
      priv->blend_mode = GstDWriteBaseOverlayBlendMode::ATTACH_BITMAP;
    return;
  }

  if (!priv->is_d3d11) {
    priv->blend_mode = GstDWriteBaseOverlayBlendMode::SW_BLEND;
    return;
  }

  /* Decide best blend mode to use based on format */
  switch (GST_VIDEO_INFO_FORMAT (&self->info)) {
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_VUYA:
    case GST_VIDEO_FORMAT_RGBA64_LE:
    case GST_VIDEO_FORMAT_RGB10A2_LE:
      /* Alpha aware formats */
      priv->blend_mode = GstDWriteBaseOverlayBlendMode::BLEND;
      break;
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_GRAY8:
    case GST_VIDEO_FORMAT_AYUV:
    case GST_VIDEO_FORMAT_RGBP:
    case GST_VIDEO_FORMAT_BGRP:
    case GST_VIDEO_FORMAT_GBR:
    case GST_VIDEO_FORMAT_GBRA:
      /* 8bits formats */
      priv->blend_mode = GstDWriteBaseOverlayBlendMode::CONVERT;
      break;
    case GST_VIDEO_FORMAT_P010_10LE:
    case GST_VIDEO_FORMAT_P012_LE:
    case GST_VIDEO_FORMAT_P016_LE:
    case GST_VIDEO_FORMAT_I420_10LE:
    case GST_VIDEO_FORMAT_I420_12LE:
    case GST_VIDEO_FORMAT_I422_10LE:
    case GST_VIDEO_FORMAT_I422_12LE:
    case GST_VIDEO_FORMAT_Y444_10LE:
    case GST_VIDEO_FORMAT_Y444_12LE:
    case GST_VIDEO_FORMAT_Y444_16LE:
    case GST_VIDEO_FORMAT_GRAY16_LE:
    case GST_VIDEO_FORMAT_AYUV64:
    case GST_VIDEO_FORMAT_GBR_10LE:
    case GST_VIDEO_FORMAT_GBR_12LE:
    case GST_VIDEO_FORMAT_GBRA_10LE:
    case GST_VIDEO_FORMAT_GBRA_12LE:
      /* high bitdept formats */
      priv->blend_mode = GstDWriteBaseOverlayBlendMode::CONVERT_64;
      break;
    default:
      /* d3d11 blending is not supported, fallback to software blending */
      priv->blend_mode = GstDWriteBaseOverlayBlendMode::SW_BLEND;
      break;
  }
}

static gboolean
gst_dwrite_base_overlay_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstDWriteBaseOverlay *self = GST_DWRITE_BASE_OVERLAY (trans);
  GstDWriteBaseOverlayPrivate *priv = self->priv;
  GstCapsFeatures *features;

  GST_DEBUG_OBJECT (self, "Set caps, in caps %" GST_PTR_FORMAT
      ", out caps %" GST_PTR_FORMAT, incaps, outcaps);

  gst_dwrite_base_overlay_clear_resource (self);
  priv->blend_mode = GstDWriteBaseOverlayBlendMode::UNKNOWN;
  priv->is_d3d11 = FALSE;
  priv->attach_meta = FALSE;

  if (!gst_video_info_from_caps (&self->info, incaps)) {
    GST_WARNING_OBJECT (self, "Invalid caps %" GST_PTR_FORMAT, incaps);
    return FALSE;
  }

  gst_video_info_set_format (&priv->bgra_info, GST_VIDEO_FORMAT_BGRA,
      self->info.width, self->info.height);

  features = gst_caps_get_features (incaps, 0);

  if (features && gst_caps_features_contains (features,
          GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY)) {
    priv->is_d3d11 = TRUE;

    std::lock_guard < std::recursive_mutex > lk (priv->context_lock);
    if (!gst_d3d11_ensure_element_data (GST_ELEMENT_CAST (self), -1,
            &priv->device)) {
      GST_ERROR_OBJECT (self, "Couldn't create deice");
      return FALSE;
    }
  }

  features = gst_caps_get_features (outcaps, 0);
  if (features && gst_caps_features_contains (features,
          GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION)) {
    priv->attach_meta = TRUE;
    GST_DEBUG_OBJECT (self, "Downstream support overlay meta");
  }

  priv->prop_lock.lock ();
  priv->text_format = nullptr;
  priv->layout = nullptr;
  priv->layout_origin.x = priv->layout_x * self->info.width;
  priv->layout_origin.y = priv->layout_y * self->info.height;
  priv->layout_size.x = priv->layout_width * self->info.width;
  priv->layout_size.y = priv->layout_height * self->info.height;
  priv->prop_lock.unlock ();

  gst_dwrite_base_overlay_decide_blend_mode (self);

  GST_DEBUG_OBJECT (self, "Decided blend mode \"%s\"",
      blend_mode_to_string (priv->blend_mode));

  if (!gst_dwrite_base_overlay_prepare_resource (self)) {
    GST_ERROR_OBJECT (self, "Couldn't prepare resource");
    return FALSE;
  }

  return TRUE;
}

static void
gst_dwrite_base_overlay_before_transform (GstBaseTransform * trans,
    GstBuffer * buf)
{
  GstDWriteBaseOverlay *self = GST_DWRITE_BASE_OVERLAY (trans);
  GstDWriteBaseOverlayPrivate *priv = self->priv;

  if (!priv->is_d3d11)
    return;

  GstMemory *mem = gst_buffer_peek_memory (buf, 0);
  if (!gst_is_d3d11_memory (mem))
    return;

  GstD3D11Memory *dmem = GST_D3D11_MEMORY_CAST (mem);

  std::lock_guard < std::recursive_mutex > lk (priv->context_lock);
  if (dmem->device == priv->device)
    return;

  GST_DEBUG_OBJECT (self, "Updating device");
  gst_object_unref (priv->device);
  priv->device = (GstD3D11Device *) gst_object_ref (dmem->device);
  gst_dwrite_base_overlay_clear_resource (self);

  gst_base_transform_reconfigure (trans);
  gst_dwrite_base_overlay_prepare_resource (self);
}

static gboolean
gst_dwrite_base_overlay_upload_system (GstDWriteBaseOverlay * self,
    GstBuffer * dst, GstBuffer * src, const GstVideoInfo * info)
{
  GstVideoFrame in_frame, out_frame;
  gboolean ret;

  GST_LOG_OBJECT (self, "system copy");

  if (!gst_video_frame_map (&in_frame, (GstVideoInfo *) info, src,
          (GstMapFlags) (GST_MAP_READ | GST_VIDEO_FRAME_MAP_FLAG_NO_REF))) {
    GST_ERROR_OBJECT (self, "Couldn't map input frame");
    return FALSE;
  }

  if (!gst_video_frame_map (&out_frame, (GstVideoInfo *) info, dst,
          (GstMapFlags) (GST_MAP_WRITE | GST_VIDEO_FRAME_MAP_FLAG_NO_REF))) {
    gst_video_frame_unmap (&in_frame);
    GST_ERROR_OBJECT (self, "Couldn't map output frame");
  }

  ret = gst_video_frame_copy (&out_frame, &in_frame);

  gst_video_frame_unmap (&in_frame);
  gst_video_frame_unmap (&out_frame);

  return ret;
}

static gboolean
gst_dwrite_base_overlay_upload_d3d11 (GstDWriteBaseOverlay * self,
    GstBuffer * dst, GstBuffer * src)
{
  GST_LOG_OBJECT (self, "d3d11 copy");

  for (guint i = 0; i < gst_buffer_n_memory (dst); i++) {
    GstMemory *dst_mem, *src_mem;
    GstD3D11Memory *dst_dmem, *src_dmem;
    GstMapInfo dst_info;
    GstMapInfo src_info;
    ID3D11Resource *dst_texture, *src_texture;
    ID3D11DeviceContext *device_context;
    GstD3D11Device *device;
    D3D11_BOX src_box = { 0, };
    D3D11_TEXTURE2D_DESC dst_desc, src_desc;
    guint dst_subidx, src_subidx;

    dst_mem = gst_buffer_peek_memory (dst, i);
    src_mem = gst_buffer_peek_memory (src, i);

    dst_dmem = (GstD3D11Memory *) dst_mem;
    src_dmem = (GstD3D11Memory *) src_mem;

    device = dst_dmem->device;

    gst_d3d11_memory_get_texture_desc (dst_dmem, &dst_desc);
    gst_d3d11_memory_get_texture_desc (src_dmem, &src_desc);

    device_context = gst_d3d11_device_get_device_context_handle (device);

    if (!gst_memory_map (dst_mem, &dst_info,
            (GstMapFlags) (GST_MAP_WRITE | GST_MAP_D3D11))) {
      GST_ERROR_OBJECT (self, "Cannot map dst d3d11 memory");
      return FALSE;
    }

    if (!gst_memory_map (src_mem, &src_info,
            (GstMapFlags) (GST_MAP_READ | GST_MAP_D3D11))) {
      GST_ERROR_OBJECT (self, "Cannot map src d3d11 memory");
      gst_memory_unmap (dst_mem, &dst_info);
      return FALSE;
    }

    dst_texture = (ID3D11Resource *) dst_info.data;
    src_texture = (ID3D11Resource *) src_info.data;

    /* src/dst texture size might be different if padding was used.
     * select smaller size */
    src_box.left = 0;
    src_box.top = 0;
    src_box.front = 0;
    src_box.back = 1;
    src_box.right = MIN (src_desc.Width, dst_desc.Width);
    src_box.bottom = MIN (src_desc.Height, dst_desc.Height);

    dst_subidx = gst_d3d11_memory_get_subresource_index (dst_dmem);
    src_subidx = gst_d3d11_memory_get_subresource_index (src_dmem);

    GstD3D11DeviceLockGuard lk (device);
    device_context->CopySubresourceRegion (dst_texture, dst_subidx, 0, 0, 0,
        src_texture, src_subidx, &src_box);

    gst_memory_unmap (src_mem, &src_info);
    gst_memory_unmap (dst_mem, &dst_info);
  }

  return TRUE;
}

static GstFlowReturn
gst_dwrite_base_overlay_prepare_output_buffer (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer ** outbuf)
{
  GstDWriteBaseOverlay *self = GST_DWRITE_BASE_OVERLAY (trans);
  GstDWriteBaseOverlayPrivate *priv = self->priv;
  GstDWriteBaseOverlayClass *klass = GST_DWRITE_BASE_OVERLAY_GET_CLASS (self);
  GstMemory *mem = gst_buffer_peek_memory (inbuf, 0);
  GstFlowReturn ret;
  gboolean is_d3d11 = FALSE;
  gboolean upload_ret;

  std::lock_guard < std::mutex > lk (priv->prop_lock);
  /* Invisible, do passthrough */
  if (!priv->visible) {
    gst_base_transform_set_passthrough (trans, TRUE);
    *outbuf = inbuf;
    return GST_FLOW_OK;
  }

  priv->cur_text = klass->get_text (self, priv->default_text, inbuf);
  if (priv->cur_text.empty ()) {
    priv->prev_text.clear ();
    gst_dwrite_base_overlay_clear_layout (self);
    /* Nothing to render, passthrough */
    gst_base_transform_set_passthrough (trans, TRUE);
    *outbuf = inbuf;
    return GST_FLOW_OK;
  }

  gst_base_transform_set_passthrough (trans, FALSE);
  if (priv->prev_text != priv->cur_text)
    gst_dwrite_base_overlay_clear_layout (self);

  priv->prev_text = priv->cur_text;

  /* attaching meta or software blending can be in-place processing */
  if (priv->attach_meta || !priv->is_d3d11 ||
      priv->blend_mode == GstDWriteBaseOverlayBlendMode::SW_BLEND) {
    goto inplace;
  }

  if (gst_is_d3d11_memory (mem)) {
    D3D11_TEXTURE2D_DESC desc;
    GstD3D11Memory *dmem;
    const guint bind_flags = (D3D11_BIND_RENDER_TARGET |
        D3D11_BIND_SHADER_RESOURCE);

    is_d3d11 = TRUE;

    dmem = GST_D3D11_MEMORY_CAST (mem);
    gst_d3d11_memory_get_texture_desc (dmem, &desc);

    /* Cannot write on decoder resource */
    if ((desc.BindFlags & D3D11_BIND_DECODER) == 0 &&
        (desc.BindFlags & bind_flags) == bind_flags) {
      goto inplace;
    }
  }

  /* Needs to allocate new buffer */
  ret = GST_BASE_TRANSFORM_CLASS (parent_class)->prepare_output_buffer (trans,
      inbuf, outbuf);
  if (ret != GST_FLOW_OK)
    return ret;

  if (is_d3d11)
    upload_ret = gst_dwrite_base_overlay_upload_d3d11 (self, *outbuf, inbuf);
  else
    upload_ret = gst_dwrite_base_overlay_upload_system (self, *outbuf, inbuf,
        &self->info);

  if (!upload_ret) {
    gst_clear_buffer (outbuf);
    return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;

inplace:
  if (gst_buffer_is_writable (inbuf))
    *outbuf = inbuf;
  else
    *outbuf = gst_buffer_copy (inbuf);

  return GST_FLOW_OK;
}

static gboolean
gst_dwrite_base_overlay_get_d2d_target (GstDWriteBaseOverlay * self,
    ID3D11Texture2D * texture, ID2D1RenderTarget ** target)
{
  GstDWriteBaseOverlayPrivate *priv = self->priv;
  ComPtr < IDXGISurface > surface;
  HRESULT hr;
  static const D2D1_RENDER_TARGET_PROPERTIES props = {
    D2D1_RENDER_TARGET_TYPE_DEFAULT, DXGI_FORMAT_UNKNOWN,
    D2D1_ALPHA_MODE_PREMULTIPLIED, 0, 0, D2D1_RENDER_TARGET_USAGE_NONE,
    D2D1_FEATURE_LEVEL_DEFAULT
  };

  hr = texture->QueryInterface (IID_PPV_ARGS (&surface));
  if (!gst_d3d11_result (hr, priv->device))
    return FALSE;

  hr = priv->d2d_factory->CreateDxgiSurfaceRenderTarget (surface.Get (), props,
      target);
  if (!gst_d3d11_result (hr, priv->device))
    return FALSE;

  return TRUE;
}

static void
gst_dwrite_base_overlay_attach (GstDWriteBaseOverlay * self, GstBuffer * buffer)
{
  GstDWriteBaseOverlayPrivate *priv = self->priv;
  GstVideoOverlayCompositionMeta *meta;

  meta = gst_buffer_get_video_overlay_composition_meta (buffer);
  if (meta) {
    if (meta->overlay) {
      meta->overlay =
          gst_video_overlay_composition_make_writable (meta->overlay);
      gst_video_overlay_composition_add_rectangle (meta->overlay,
          priv->overlay_rect);
    } else {
      meta->overlay = gst_video_overlay_composition_new (priv->overlay_rect);
    }
  } else {
    GstVideoOverlayComposition *comp =
        gst_video_overlay_composition_new (priv->overlay_rect);
    meta = gst_buffer_add_video_overlay_composition_meta (buffer, comp);
    gst_video_overlay_composition_unref (comp);
  }
}

static gboolean
gst_dwrite_base_overlay_mode_sw_blend (GstDWriteBaseOverlay * self,
    GstBuffer * buffer)
{
  GstDWriteBaseOverlayPrivate *priv = self->priv;
  GstVideoFrame dst_frame, src_frame;
  gboolean ret;

  if (!gst_video_frame_map (&dst_frame, &self->info, buffer,
          (GstMapFlags) (GST_MAP_WRITE | GST_VIDEO_FRAME_MAP_FLAG_NO_REF))) {
    GST_ERROR_OBJECT (self, "Couldn't map input buffer");
    return FALSE;
  }

  if (!gst_video_frame_map (&src_frame, &priv->bgra_info, priv->text_buf,
          (GstMapFlags) (GST_MAP_READ | GST_VIDEO_FRAME_MAP_FLAG_NO_REF))) {
    gst_video_frame_unmap (&dst_frame);
    GST_ERROR_OBJECT (self, "Couldn't map text buffer");
    return FALSE;
  }

  src_frame.info.flags = (GstVideoFlags)
      (src_frame.info.flags | GST_VIDEO_FLAG_PREMULTIPLIED_ALPHA);
  ret = gst_video_blend (&dst_frame, &src_frame, 0, 0, 1.0);
  gst_video_frame_unmap (&src_frame);
  gst_video_frame_unmap (&dst_frame);

  return ret;
}

static gboolean
gst_d3d11_bast_text_overlay_mode_blend (GstDWriteBaseOverlay * self,
    GstBuffer * buffer)
{
  GstDWriteBaseOverlayPrivate *priv = self->priv;

  if (!gst_d3d11_converter_convert_buffer (priv->blend_conv,
          priv->text_buf, buffer)) {
    GST_ERROR_OBJECT (self, "Couldn't blend texture");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_d3d11_bast_text_overlay_mode_convert (GstDWriteBaseOverlay * self,
    GstBuffer * buffer)
{
  GstDWriteBaseOverlayPrivate *priv = self->priv;
  GstBuffer *pre_buf = nullptr;

  gst_buffer_pool_acquire_buffer (priv->text_pool, &pre_buf, nullptr);
  if (!pre_buf) {
    GST_ERROR_OBJECT (self, "Couldn't acquire preconv buffer");
    return FALSE;
  }

  gst_d3d11_device_lock (priv->device);
  if (!gst_d3d11_converter_convert_buffer_unlocked (priv->pre_conv,
          buffer, pre_buf)) {
    GST_ERROR_OBJECT (self, "pre-convert failed");
    gst_d3d11_device_unlock (priv->device);
    goto error;
  }

  if (!gst_d3d11_converter_convert_buffer_unlocked (priv->blend_conv,
          priv->text_buf, pre_buf)) {
    GST_ERROR_OBJECT (self, "blend-convert failed");
    gst_d3d11_device_unlock (priv->device);
    goto error;
  }

  if (!gst_d3d11_converter_convert_buffer_unlocked (priv->post_conv,
          pre_buf, buffer)) {
    GST_ERROR_OBJECT (self, "post-convert failed");
    gst_d3d11_device_unlock (priv->device);
    goto error;
  }

  gst_d3d11_device_unlock (priv->device);
  gst_buffer_unref (pre_buf);

  return TRUE;

error:
  gst_clear_buffer (&pre_buf);
  return FALSE;
}

static gboolean
gst_d3d11_bast_text_overlay_mode_convert_64 (GstDWriteBaseOverlay * self,
    GstBuffer * buffer)
{
  GstDWriteBaseOverlayPrivate *priv = self->priv;
  GstBuffer *pre_buf = nullptr;

  gst_buffer_pool_acquire_buffer (priv->blend_pool, &pre_buf, nullptr);
  if (!pre_buf) {
    GST_ERROR_OBJECT (self, "Couldn't acquire pre-convert buffer");
    return FALSE;
  }

  gst_d3d11_device_lock (priv->device);
  if (!gst_d3d11_converter_convert_buffer_unlocked (priv->pre_conv,
          buffer, pre_buf)) {
    GST_ERROR_OBJECT (self, "pre-convert failed");
    gst_d3d11_device_unlock (priv->device);
    goto error;
  }

  if (!gst_d3d11_converter_convert_buffer_unlocked (priv->blend_conv,
          priv->text_buf, pre_buf)) {
    GST_ERROR_OBJECT (self, "Couldn't blend texture");
    gst_d3d11_device_unlock (priv->device);
    goto error;
  }

  if (!gst_d3d11_converter_convert_buffer_unlocked (priv->post_conv,
          pre_buf, buffer)) {
    GST_ERROR_OBJECT (self, "post-convert failed");
    gst_d3d11_device_unlock (priv->device);
    goto error;
  }

  gst_d3d11_device_unlock (priv->device);
  gst_buffer_unref (pre_buf);

  return TRUE;

error:
  gst_clear_buffer (&pre_buf);

  return FALSE;
}

static gboolean
gst_dwrite_base_overlay_update_text_format (GstDWriteBaseOverlay * self)
{
  GstDWriteBaseOverlayPrivate *priv = self->priv;
  FLOAT font_size;
  FLOAT background_padding;
  HRESULT hr;

  if (priv->text_format)
    return TRUE;

  gst_dwrite_base_overlay_clear_layout (self);

  if (priv->auto_resize) {
    font_size = (FLOAT) self->info.width * priv->font_size /
        priv->reference_frame_size;
  } else {
    font_size = priv->font_size;
  }

  background_padding = (font_size / priv->font_size) * 5.0f;
  priv->background_padding.left = priv->background_padding.top =
      -background_padding;
  priv->background_padding.right = priv->background_padding.bottom =
      background_padding;

  std::wstring wfont_family = gst_dwrite_string_to_wstring (priv->font_family);

  hr = priv->dwrite_factory->CreateTextFormat (wfont_family.c_str (), nullptr,
      priv->font_weight, priv->font_style, priv->font_stretch,
      font_size, L"en-us", &priv->text_format);
  if (FAILED (hr)) {
    GST_ERROR_OBJECT (self, "Couldn't create text format");
    return FALSE;
  }

  return TRUE;
}

static inline D2D1_COLOR_F
unpack_argb (guint packed)
{
  D2D1_COLOR_F ret;
  ret.a = ((packed >> 24) & 0xff) / 255.f;
  ret.r = ((packed >> 16) & 0xff) / 255.f;
  ret.g = ((packed >> 8) & 0xff) / 255.f;
  ret.b = ((packed >> 0) & 0xff) / 255.f;

  return ret;
}

static gboolean
gst_dwrite_base_overlay_create_layout (GstDWriteBaseOverlay * self,
    const std::wstring & text)
{
  GstDWriteBaseOverlayPrivate *priv = self->priv;
  D2D1_COLOR_F color;
  ComPtr < IGstDWriteTextEffect > effect;
  HRESULT hr;
  DWRITE_TEXT_RANGE range;

  hr = priv->dwrite_factory->CreateTextLayout (text.c_str (), text.length (),
      priv->text_format.Get (), self->info.width, self->info.height,
      &priv->layout);
  if (FAILED (hr)) {
    GST_ERROR_OBJECT (self, "Couldn't create text layout");
    return FALSE;
  }

  hr = IGstDWriteTextEffect::CreateInstance (&effect);
  if (FAILED (hr)) {
    GST_ERROR_OBJECT (self, "Couldn't create text effect");
    priv->layout = nullptr;
    return FALSE;
  }

  range.startPosition = 0;
  range.length = G_MAXUINT32;

  priv->layout->SetTextAlignment (priv->text_align);
  priv->layout->SetParagraphAlignment (priv->paragraph_align);
  priv->layout->SetMaxWidth (priv->layout_size.x);
  priv->layout->SetMaxHeight (priv->layout_size.y);

  color = unpack_argb (priv->text_color);
  effect->SetBrushColor (GST_DWRITE_BRUSH_TEXT, &color);

  color = unpack_argb (priv->outline_color);
  effect->SetBrushColor (GST_DWRITE_BRUSH_TEXT_OUTLINE, &color);

  color = unpack_argb (priv->underline_color);
  effect->SetBrushColor (GST_DWRITE_BRUSH_UNDERLINE, &color);

  color = unpack_argb (priv->underline_outline_color);
  effect->SetBrushColor (GST_DWRITE_BRUSH_UNDERLINE_OUTLINE, &color);

  if (priv->underline_color || priv->underline_outline_color) {
    priv->layout->SetUnderline (TRUE, range);
  } else {
    priv->layout->SetUnderline (FALSE, range);
  }

  color = unpack_argb (priv->strikethrough_color);
  effect->SetBrushColor (GST_DWRITE_BRUSH_STRIKETHROUGH, &color);

  color = unpack_argb (priv->strikethrough_outline_color);
  effect->SetBrushColor (GST_DWRITE_BRUSH_STRIKETHROUGH_OUTLINE, &color);

  if (priv->strikethrough_color || priv->strikethrough_outline_color) {
    priv->layout->SetStrikethrough (TRUE, range);
  } else {
    priv->layout->SetStrikethrough (FALSE, range);
  }

  color = unpack_argb (priv->shadow_color);
  effect->SetBrushColor (GST_DWRITE_BRUSH_SHADOW, &color);

  hr = priv->layout->SetDrawingEffect (effect.Get (), range);
  if (FAILED (hr)) {
    GST_ERROR_OBJECT (self, "Couldn't set drawing effect");
    priv->layout = nullptr;
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_dwrite_base_overlay_render_text (GstDWriteBaseOverlay * self)
{
  GstDWriteBaseOverlayPrivate *priv = self->priv;
  GstBuffer *text_buf = nullptr;
  gboolean bitmap_render = FALSE;
  ComPtr < ID2D1RenderTarget > target;
  GstMemory *mem;
  HRESULT hr;
  GstMapInfo info;
  D2D1_COLOR_F bg_color;
  D2D1_RECT_F bg_padding = D2D1::RectF ();

  if (priv->text_buf)
    return TRUE;

  if (priv->blend_mode == GstDWriteBaseOverlayBlendMode::ATTACH_BITMAP ||
      priv->blend_mode == GstDWriteBaseOverlayBlendMode::SW_BLEND) {
    gst_buffer_pool_acquire_buffer (priv->bitmap_pool, &text_buf, nullptr);
    bitmap_render = TRUE;
  } else {
    gst_buffer_pool_acquire_buffer (priv->text_pool, &text_buf, nullptr);
  }

  if (!text_buf) {
    GST_ERROR_OBJECT (self, "Couldn't get text buffer");
    return FALSE;
  }

  mem = gst_buffer_peek_memory (text_buf, 0);
  if (bitmap_render) {
    static const D2D1_RENDER_TARGET_PROPERTIES props = {
      D2D1_RENDER_TARGET_TYPE_DEFAULT, DXGI_FORMAT_B8G8R8A8_UNORM,
      D2D1_ALPHA_MODE_PREMULTIPLIED, 0, 0, D2D1_RENDER_TARGET_USAGE_NONE,
      D2D1_FEATURE_LEVEL_DEFAULT
    };

    GstDWriteBitmapMemory *bmem = (GstDWriteBitmapMemory *) mem;
    hr = priv->d2d_factory->CreateWicBitmapRenderTarget (bmem->bitmap, &props,
        &target);
    if (FAILED (hr)) {
      GST_ERROR_OBJECT (self, "Couldn't create render target, hr: 0x%x",
          (guint) hr);
      goto error;
    }
  } else {
    ID3D11Texture2D *texture;

    if (!gst_memory_map (mem, &info,
            (GstMapFlags) (GST_MAP_WRITE | GST_MAP_D3D11))) {
      GST_ERROR_OBJECT (self, "Could not map buffer");
      goto error;
    }

    texture = (ID3D11Texture2D *) info.data;
    if (!gst_dwrite_base_overlay_get_d2d_target (self, texture, &target)) {
      gst_memory_unmap (mem, &info);
      goto error;
    }

    gst_d3d11_device_lock (priv->device);
  }

  bg_color = unpack_argb (priv->background_color);
  if (priv->background_color)
    bg_padding = priv->background_padding;

  target->BeginDraw ();
  target->Clear (D2D1::ColorF (D2D1::ColorF::Black, 0.0));
  priv->renderer->Draw (priv->layout_origin, D2D1::SizeF (1.0, 1.0),
      D2D1::Rect (0, 0, self->info.width, self->info.height),
      bg_color, bg_padding, priv->color_font, priv->layout.Get (),
      target.Get ());
  target->EndDraw ();

  if (!bitmap_render) {
    gst_d3d11_device_unlock (priv->device);
    gst_memory_unmap (mem, &info);
  }

  priv->text_buf = text_buf;
  priv->overlay_rect = gst_video_overlay_rectangle_new_raw (text_buf, 0, 0,
      self->info.width, self->info.height,
      GST_VIDEO_OVERLAY_FORMAT_FLAG_PREMULTIPLIED_ALPHA);

  return TRUE;

error:
  gst_clear_buffer (&text_buf);
  return FALSE;
}

static GstFlowReturn
gst_dwrite_base_overlay_transform (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstDWriteBaseOverlay *self = GST_DWRITE_BASE_OVERLAY (trans);
  GstDWriteBaseOverlayPrivate *priv = self->priv;
  gboolean ret = FALSE;
  std::lock_guard < std::mutex > lk (priv->prop_lock);

  if (!gst_dwrite_base_overlay_update_text_format (self))
    return GST_FLOW_ERROR;

  if (!priv->layout &&
      !gst_dwrite_base_overlay_create_layout (self, priv->cur_text)) {
    return GST_FLOW_ERROR;
  }

  if (!gst_dwrite_base_overlay_render_text (self))
    return GST_FLOW_ERROR;

  GST_LOG_OBJECT (self,
      "Blending mode \"%s\"", blend_mode_to_string (priv->blend_mode));

  switch (priv->blend_mode) {
    case GstDWriteBaseOverlayBlendMode::ATTACH_TEXTURE:
    case GstDWriteBaseOverlayBlendMode::ATTACH_BITMAP:
      gst_dwrite_base_overlay_attach (self, outbuf);
      ret = TRUE;
      break;
    case GstDWriteBaseOverlayBlendMode::SW_BLEND:
      ret = gst_dwrite_base_overlay_mode_sw_blend (self, outbuf);
      break;
    case GstDWriteBaseOverlayBlendMode::BLEND:
      ret = gst_d3d11_bast_text_overlay_mode_blend (self, outbuf);
      break;
    case GstDWriteBaseOverlayBlendMode::CONVERT:
      ret = gst_d3d11_bast_text_overlay_mode_convert (self, outbuf);
      break;
    case GstDWriteBaseOverlayBlendMode::CONVERT_64:
      ret = gst_d3d11_bast_text_overlay_mode_convert_64 (self, outbuf);
      break;
    case GstDWriteBaseOverlayBlendMode::UNKNOWN:
      GST_ERROR_OBJECT (self, "Conversion mode was not configured");
      ret = FALSE;
  }

  if (!ret)
    return GST_FLOW_ERROR;

  return GST_FLOW_OK;
}
