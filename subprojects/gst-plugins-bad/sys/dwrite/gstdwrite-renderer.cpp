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
#include "config.h"
#endif

#include "gstdwrite-renderer.h"
#include <wrl.h>
#include <vector>

GST_DEBUG_CATEGORY_EXTERN (gst_dwrite_debug);
#define GST_CAT_DEFAULT gst_dwrite_debug

/* *INDENT-OFF* */
using namespace Microsoft::WRL;

struct RenderContext
{
  gboolean collect_geometry;
  std::vector<ComPtr<ID2D1Geometry>> backgrounds;
  D2D1_RECT_F background_padding = D2D1::RectF ();
  ID2D1Factory *factory;
  ID2D1RenderTarget *target;
  RECT client_rect;
  D2D1_SIZE_F scale;
  gboolean enable_color_font;
};
/* *INDENT-ON* */

static HRESULT
CombineTwoGeometries (ID2D1Factory * factory, ID2D1Geometry * a,
    ID2D1Geometry * b, ID2D1Geometry ** result)
{
  HRESULT hr;
  ComPtr < ID2D1GeometrySink > sink;
  ComPtr < ID2D1PathGeometry > geometry;

  hr = factory->CreatePathGeometry (&geometry);
  if (FAILED (hr)) {
    GST_WARNING ("Couldn't create path geometry, 0x%x", (guint) hr);
    return hr;
  }

  hr = geometry->Open (&sink);
  if (FAILED (hr)) {
    GST_WARNING ("Couldn't open path geometry, 0x%x", (guint) hr);
    return hr;
  }

  hr = a->CombineWithGeometry (b,
      D2D1_COMBINE_MODE_UNION, nullptr, sink.Get ());
  if (FAILED (hr)) {
    GST_WARNING ("Couldn't combine geometry, 0x%x", (guint) hr);
    return hr;
  }

  hr = sink->Close ();
  if (FAILED (hr)) {
    GST_WARNING ("Couldn't close sink, 0x%x", (guint) hr);
    return hr;
  }

  *result = geometry.Detach ();

  return S_OK;
}

static void
CombineGeometries (ID2D1Factory * factory,
    std::vector < ComPtr < ID2D1Geometry >> &geometries,
    ID2D1Geometry ** result)
{
  ComPtr < ID2D1Geometry > combined;
  HRESULT hr;

  if (geometries.empty ())
    return;

  /* *INDENT-OFF* */
  for (const auto & it: geometries) {
    if (!combined) {
      combined = it;
    } else {
      ComPtr <ID2D1Geometry> tmp;
      hr = CombineTwoGeometries (factory, it.Get (), combined.Get (), &tmp);
      if (FAILED (hr))
        return;

      combined = tmp;
    }
  }
  /* *INDENT-ON* */

  if (!combined)
    return;

  *result = combined.Detach ();
}

STDMETHODIMP
    IGstDWriteTextRenderer::CreateInstance (IDWriteFactory * factory,
    IGstDWriteTextRenderer ** renderer)
{
  IGstDWriteTextRenderer *self = new IGstDWriteTextRenderer ();

  if (!self)
    return E_OUTOFMEMORY;

  self->factory_ = factory;
  factory->AddRef ();
  *renderer = self;

  return S_OK;
}

/* IUnknown */
STDMETHODIMP_ (ULONG)
    IGstDWriteTextRenderer::AddRef (void)
{
  return InterlockedIncrement (&ref_count_);
}

STDMETHODIMP_ (ULONG)
    IGstDWriteTextRenderer::Release (void)
{
  ULONG ref_count;

  ref_count = InterlockedDecrement (&ref_count_);

  if (ref_count == 0)
    delete this;

  return ref_count;
}

STDMETHODIMP
IGstDWriteTextRenderer::QueryInterface (REFIID riid, void **object)
{
  if (riid == __uuidof (IUnknown)) {
    *object = static_cast < IUnknown * >
        (static_cast < IGstDWriteTextRenderer * >(this));
  } else if (riid == __uuidof (IDWritePixelSnapping)) {
    *object = static_cast < IDWritePixelSnapping * >
        (static_cast < IGstDWriteTextRenderer * >(this));
  } else if (riid == IID_IGstDWriteTextRenderer) {
    *object = static_cast < IDWriteTextRenderer * >
        (static_cast < IGstDWriteTextRenderer * >(this));
  } else {
    *object = nullptr;
    return E_NOINTERFACE;
  }

  AddRef ();

  return S_OK;
}

/* IDWritePixelSnapping */
STDMETHODIMP
    IGstDWriteTextRenderer::IsPixelSnappingDisabled (void *context,
    BOOL * is_disabled)
{
  *is_disabled = FALSE;
  return S_OK;
}

STDMETHODIMP
    IGstDWriteTextRenderer::GetCurrentTransform (void *context,
    DWRITE_MATRIX * transform)
{
  RenderContext *render_ctx;

  g_assert (context != nullptr);

  render_ctx = (RenderContext *) context;
  render_ctx->target->GetTransform (reinterpret_cast <
      D2D1_MATRIX_3X2_F * >(transform));

  return S_OK;
}

STDMETHODIMP
    IGstDWriteTextRenderer::GetPixelsPerDip (void *context,
    FLOAT * pixels_per_dip)
{
  *pixels_per_dip = 1.0f;

  return S_OK;
}

/* IDWriteTextRenderer */
STDMETHODIMP
    IGstDWriteTextRenderer::DrawGlyphRun (void *context, FLOAT origin_x,
    FLOAT origin_y, DWRITE_MEASURING_MODE mode,
    DWRITE_GLYPH_RUN const *glyph_run,
    DWRITE_GLYPH_RUN_DESCRIPTION const *glyph_run_desc,
    IUnknown * client_effect)
{
  ComPtr < ID2D1PathGeometry > geometry;
  ComPtr < ID2D1GeometrySink > sink;
  ComPtr < ID2D1TransformedGeometry > transformed;
  ComPtr < ID2D1TransformedGeometry > shadow_transformed;
  ComPtr < IGstDWriteTextEffect > effect;
  ComPtr < ID2D1SolidColorBrush > brush;
  ComPtr < ID2D1SolidColorBrush > outline_brush;
  ComPtr < ID2D1SolidColorBrush > shadow_brush;
  RenderContext *render_ctx;
  ID2D1RenderTarget *target;
  ID2D1Factory *factory;
  HRESULT hr;
  RECT client_rect;
  D2D1_COLOR_F fg_color = D2D1::ColorF (D2D1::ColorF::Black);

  g_assert (context != nullptr);

  render_ctx = (RenderContext *) context;
  client_rect = render_ctx->client_rect;
  target = render_ctx->target;
  factory = render_ctx->factory;

  hr = factory->CreatePathGeometry (&geometry);
  if (FAILED (hr))
    return hr;

  hr = geometry->Open (&sink);
  if (FAILED (hr))
    return hr;

  hr = glyph_run->fontFace->GetGlyphRunOutline (glyph_run->fontEmSize,
      glyph_run->glyphIndices, glyph_run->glyphAdvances,
      glyph_run->glyphOffsets, glyph_run->glyphCount, glyph_run->isSideways,
      glyph_run->bidiLevel % 2, sink.Get ());

  if (FAILED (hr))
    return hr;

  sink->Close ();

  hr = factory->CreateTransformedGeometry (geometry.Get (),
      D2D1::Matrix3x2F::Translation (origin_x, origin_y) *
      D2D1::Matrix3x2F::Scale (render_ctx->scale), &transformed);

  if (FAILED (hr))
    return hr;

  /* Create new path geometry from the bound rect.
   * Note that rect geometry cannot be used since the combined background
   * geometry might not be represented as a single rectangle */
  if (render_ctx->collect_geometry) {
    D2D1_RECT_F bounds;
    ComPtr < ID2D1RectangleGeometry > rect_geometry;
    ComPtr < ID2D1PathGeometry > path_geometry;
    ComPtr < ID2D1GeometrySink > path_sink;

    hr = transformed->GetBounds (nullptr, &bounds);
    if (FAILED (hr))
      return hr;

    bounds.left += render_ctx->background_padding.left;
    bounds.right += render_ctx->background_padding.right;
    bounds.top += render_ctx->background_padding.top;
    bounds.bottom += render_ctx->background_padding.bottom;

    bounds.left = MAX (bounds.left, (FLOAT) client_rect.left);
    bounds.right = MIN (bounds.right, (FLOAT) client_rect.right);
    bounds.top = MAX (bounds.top, (FLOAT) client_rect.top);
    bounds.bottom = MIN (bounds.bottom, (FLOAT) client_rect.bottom);

    hr = factory->CreateRectangleGeometry (bounds, &rect_geometry);
    if (FAILED (hr))
      return hr;

    hr = factory->CreatePathGeometry (&path_geometry);
    if (FAILED (hr))
      return hr;

    hr = path_geometry->Open (&path_sink);
    if (FAILED (hr))
      return hr;

    hr = rect_geometry->Outline (nullptr, path_sink.Get ());
    if (FAILED (hr))
      return hr;

    path_sink->Close ();
    render_ctx->backgrounds.push_back (path_geometry);

    return S_OK;
  }

  if (client_effect)
    client_effect->QueryInterface (IID_IGstDWriteTextEffect, &effect);

  if (effect) {
    D2D1_COLOR_F color;
    BOOL enabled;

    effect->GetBrushColor (GST_DWRITE_BRUSH_TEXT, &color, &enabled);
    if (enabled) {
      target->CreateSolidColorBrush (color, &brush);
      fg_color = color;
    }

    effect->GetBrushColor (GST_DWRITE_BRUSH_TEXT_OUTLINE, &color, &enabled);
    if (enabled)
      target->CreateSolidColorBrush (color, &outline_brush);

    effect->GetBrushColor (GST_DWRITE_BRUSH_SHADOW, &color, &enabled);
    if (enabled)
      target->CreateSolidColorBrush (color, &shadow_brush);
  } else {
    target->CreateSolidColorBrush (D2D1::ColorF (D2D1::ColorF::Black), &brush);
    outline_brush = brush;
  }

#ifdef HAVE_DWRITE_COLOR_FONT
  if (render_ctx->enable_color_font) {
    const DWRITE_GLYPH_IMAGE_FORMATS supported_formats =
        DWRITE_GLYPH_IMAGE_FORMATS_TRUETYPE |
        DWRITE_GLYPH_IMAGE_FORMATS_CFF |
        DWRITE_GLYPH_IMAGE_FORMATS_COLR |
        DWRITE_GLYPH_IMAGE_FORMATS_SVG |
        DWRITE_GLYPH_IMAGE_FORMATS_PNG |
        DWRITE_GLYPH_IMAGE_FORMATS_JPEG |
        DWRITE_GLYPH_IMAGE_FORMATS_TIFF |
        DWRITE_GLYPH_IMAGE_FORMATS_PREMULTIPLIED_B8G8R8A8;

    ComPtr < IDWriteColorGlyphRunEnumerator1 > glyph_run_enum;
    ComPtr < IDWriteFactory4 > factory4;
    ComPtr < ID2D1DeviceContext4 > ctx4;
    hr = factory_->QueryInterface (IID_PPV_ARGS (&factory4));
    if (SUCCEEDED (hr))
      hr = target->QueryInterface (IID_PPV_ARGS (&ctx4));

    if (SUCCEEDED (hr)) {
      hr = factory4->TranslateColorGlyphRun (D2D1::Point2 (origin_x, origin_y),
          glyph_run, glyph_run_desc, supported_formats,
          DWRITE_MEASURING_MODE_NATURAL, nullptr, 0, &glyph_run_enum);
      if (hr != DWRITE_E_NOCOLOR && SUCCEEDED (hr)) {
        ComPtr < ID2D1SolidColorBrush > tmp_brush;
        BOOL has_run = FALSE;
        do {
          DWRITE_COLOR_GLYPH_RUN1 const *color_run;
          hr = glyph_run_enum->MoveNext (&has_run);

          if (FAILED (hr))
            return hr;

          if (!has_run)
            return S_OK;

          hr = glyph_run_enum->GetCurrentRun (&color_run);
          if (FAILED (hr))
            return hr;

          const auto cur_origin = D2D1::Point2F (color_run->baselineOriginX,
              color_run->baselineOriginY);
          switch (color_run->glyphImageFormat) {
            case DWRITE_GLYPH_IMAGE_FORMATS_PNG:
            case DWRITE_GLYPH_IMAGE_FORMATS_JPEG:
            case DWRITE_GLYPH_IMAGE_FORMATS_TIFF:
            case DWRITE_GLYPH_IMAGE_FORMATS_PREMULTIPLIED_B8G8R8A8:
              ctx4->DrawColorBitmapGlyphRun (color_run->glyphImageFormat,
                  cur_origin, &color_run->glyphRun,
                  DWRITE_MEASURING_MODE_NATURAL);
              break;
            case DWRITE_GLYPH_IMAGE_FORMATS_SVG:
            {
              ComPtr < ID2D1SolidColorBrush > svg_brush;
              if (brush)
                svg_brush = brush;
              else if (outline_brush)
                svg_brush = outline_brush;

              ctx4->DrawSvgGlyphRun (cur_origin, &color_run->glyphRun,
                  svg_brush.Get ());
              break;
            }
            case DWRITE_GLYPH_IMAGE_FORMATS_TRUETYPE:
            case DWRITE_GLYPH_IMAGE_FORMATS_CFF:
            case DWRITE_GLYPH_IMAGE_FORMATS_COLR:
            default:
              if (!tmp_brush) {
                hr = target->CreateSolidColorBrush (fg_color, &tmp_brush);
                if (FAILED (hr))
                  return hr;
              }

              if (color_run->paletteIndex == 0xffff)
                tmp_brush->SetColor (fg_color);
              else
                tmp_brush->SetColor (color_run->runColor);

              target->DrawGlyphRun (cur_origin, &color_run->glyphRun,
                  tmp_brush.Get ());
              break;
          }

        } while (has_run && SUCCEEDED (hr));

        return S_OK;
      }
    }
  }
#endif /* HAVE_DWRITE_COLOR_FONT */

  if (shadow_brush) {
    /* TODO: do we want to make this shadow configurable ? */
    hr = factory->CreateTransformedGeometry (geometry.Get (),
        D2D1::Matrix3x2F::Translation (origin_x + 1.5, origin_y + 1.5) *
        D2D1::Matrix3x2F::Scale (render_ctx->scale), &shadow_transformed);

    if (FAILED (hr))
      return hr;
  }

  if (shadow_brush)
    target->FillGeometry (shadow_transformed.Get (), shadow_brush.Get ());

  if (outline_brush)
    target->DrawGeometry (transformed.Get (), outline_brush.Get ());

  if (brush)
    target->FillGeometry (transformed.Get (), brush.Get ());

  return S_OK;
}

STDMETHODIMP
    IGstDWriteTextRenderer::DrawUnderline (void *context, FLOAT origin_x,
    FLOAT origin_y, DWRITE_UNDERLINE const *underline, IUnknown * client_effect)
{
  ComPtr < ID2D1RectangleGeometry > geometry;
  ComPtr < ID2D1TransformedGeometry > transformed;
  ComPtr < IGstDWriteTextEffect > effect;
  ComPtr < ID2D1SolidColorBrush > brush;
  ComPtr < ID2D1SolidColorBrush > outline_brush;
  RenderContext *render_ctx;
  ID2D1RenderTarget *target;
  ID2D1Factory *factory;
  HRESULT hr;

  g_assert (context != nullptr);

  render_ctx = (RenderContext *) context;
  if (render_ctx->collect_geometry)
    return S_OK;

  target = render_ctx->target;
  factory = render_ctx->factory;

  hr = factory->CreateRectangleGeometry (D2D1::RectF (0, underline->offset,
          underline->width, underline->offset + underline->thickness),
      &geometry);
  if (FAILED (hr)) {
    GST_WARNING ("Couldn't create geometry, 0x%x", (guint) hr);
    return hr;
  }

  hr = factory->CreateTransformedGeometry (geometry.Get (),
      D2D1::Matrix3x2F::Translation (origin_x, origin_y) *
      D2D1::Matrix3x2F::Scale (render_ctx->scale), &transformed);
  if (FAILED (hr)) {
    GST_WARNING ("Couldn't create transformed geometry, 0x%x", (guint) hr);
    return hr;
  }

  if (client_effect)
    client_effect->QueryInterface (IID_IGstDWriteTextEffect, &effect);

  if (effect) {
    D2D1_COLOR_F color;
    BOOL enabled;

    effect->GetBrushColor (GST_DWRITE_BRUSH_UNDERLINE, &color, &enabled);
    if (enabled)
      target->CreateSolidColorBrush (color, &brush);

    effect->GetBrushColor (GST_DWRITE_BRUSH_UNDERLINE_OUTLINE, &color,
        &enabled);
    if (enabled)
      target->CreateSolidColorBrush (color, &outline_brush);
  } else {
    target->CreateSolidColorBrush (D2D1::ColorF (D2D1::ColorF::Black), &brush);
    outline_brush = brush;
  }

  if (outline_brush)
    target->DrawGeometry (transformed.Get (), outline_brush.Get ());

  if (brush)
    target->FillGeometry (transformed.Get (), brush.Get ());

  return S_OK;
}

STDMETHODIMP
    IGstDWriteTextRenderer::DrawStrikethrough (void *context, FLOAT origin_x,
    FLOAT origin_y, DWRITE_STRIKETHROUGH const *strikethrough,
    IUnknown * client_effect)
{
  ComPtr < ID2D1RectangleGeometry > geometry;
  ComPtr < ID2D1TransformedGeometry > transformed;
  ComPtr < IGstDWriteTextEffect > effect;
  ComPtr < ID2D1SolidColorBrush > brush;
  ComPtr < ID2D1SolidColorBrush > outline_brush;
  RenderContext *render_ctx;
  ID2D1RenderTarget *target;
  ID2D1Factory *factory;
  HRESULT hr;

  g_assert (context != nullptr);

  render_ctx = (RenderContext *) context;
  if (render_ctx->collect_geometry)
    return S_OK;

  target = render_ctx->target;
  factory = render_ctx->factory;

  hr = factory->CreateRectangleGeometry (D2D1::RectF (0,
          strikethrough->offset, strikethrough->width,
          strikethrough->offset + strikethrough->thickness), &geometry);

  if (FAILED (hr)) {
    GST_WARNING ("Couldn't create geometry, 0x%x", (guint) hr);
    return hr;
  }

  hr = factory->CreateTransformedGeometry (geometry.Get (),
      D2D1::Matrix3x2F::Translation (origin_x, origin_y) *
      D2D1::Matrix3x2F::Scale (render_ctx->scale), &transformed);

  if (FAILED (hr)) {
    GST_WARNING ("Couldn't create transformed geometry, 0x%x", (guint) hr);
    return hr;
  }

  if (client_effect)
    client_effect->QueryInterface (IID_IGstDWriteTextEffect, &effect);

  if (effect) {
    D2D1_COLOR_F color;
    BOOL enabled;

    effect->GetBrushColor (GST_DWRITE_BRUSH_STRIKETHROUGH, &color, &enabled);
    if (enabled)
      target->CreateSolidColorBrush (color, &brush);

    effect->GetBrushColor (GST_DWRITE_BRUSH_STRIKETHROUGH_OUTLINE, &color,
        &enabled);
    if (enabled)
      target->CreateSolidColorBrush (color, &outline_brush);
  } else {
    target->CreateSolidColorBrush (D2D1::ColorF (D2D1::ColorF::Black), &brush);
    outline_brush = brush;
  }

  if (outline_brush)
    target->DrawGeometry (transformed.Get (), outline_brush.Get ());

  if (brush)
    target->FillGeometry (transformed.Get (), brush.Get ());

  return S_OK;
}

STDMETHODIMP
    IGstDWriteTextRenderer::DrawInlineObject (void *context, FLOAT origin_x,
    FLOAT origin_y, IDWriteInlineObject * inline_object, BOOL is_sideways,
    BOOL is_right_to_left, IUnknown * client_effect)
{
  GST_WARNING ("Not implemented");
  return E_NOTIMPL;
}

IGstDWriteTextRenderer::IGstDWriteTextRenderer (void)
{
}

IGstDWriteTextRenderer::~IGstDWriteTextRenderer (void)
{
  factory_->Release ();
}

STDMETHODIMP
    IGstDWriteTextRenderer::Draw (const D2D1_POINT_2F & origin,
    const D2D1_SIZE_F & scale, const RECT & client_rect,
    const D2D1_COLOR_F & background_color,
    const D2D1_RECT_F & background_padding,
    gboolean enable_color_font,
    IDWriteTextLayout * layout, ID2D1RenderTarget * target)
{
  HRESULT hr;
  RenderContext context;
  ComPtr < ID2D1Factory > d2d_factory;

  g_return_val_if_fail (layout != nullptr, E_INVALIDARG);
  g_return_val_if_fail (target != nullptr, E_INVALIDARG);

  target->GetFactory (&d2d_factory);
  context.client_rect = client_rect;
  context.target = target;
  context.factory = d2d_factory.Get ();
  context.scale = scale;
  context.enable_color_font = enable_color_font;

  if (IGstDWriteTextEffect::IsEnabledColor (background_color)) {
    ComPtr < ID2D1Geometry > geometry;

    context.collect_geometry = TRUE;
    context.background_padding = background_padding;

    hr = layout->Draw (&context, this, origin.x, origin.y);
    if (FAILED (hr)) {
      GST_WARNING ("Couldn't draw layout for collecting geometry, 0x%x",
          (guint) hr);
      return hr;
    }

    CombineGeometries (d2d_factory.Get (), context.backgrounds, &geometry);
    if (geometry) {
      ComPtr < ID2D1SolidColorBrush > brush;
      hr = target->CreateSolidColorBrush (background_color, &brush);
      if (FAILED (hr)) {
        GST_WARNING ("Couldn't create solid brush, 0x%x", (guint) hr);
        return hr;
      }

      target->FillGeometry (geometry.Get (), brush.Get ());
    }
  }

  context.collect_geometry = FALSE;

  hr = layout->Draw (&context, this, origin.x, origin.y);

  if (FAILED (hr)) {
    GST_WARNING ("Draw failed with 0x%x", (guint) hr);
  }

  return hr;
}
