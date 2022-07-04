/* GStreamer
 * Copyright (C) 2022 Seungha Yang <seungha@centricular.com>
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

/**
 * SECTION:element-d3d11testsrc
 * @title: d3d11testsrc
 *
 * The videotestsrc element is used to produce test video data
 *
 * ## Example launch line
 * ```
 * gst-launch-1.0 d3d11testsrc ! queue ! d3d11videosink
 * ```
 *
 * Since: 1.22
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstd3d11testsrc.h"
#include "gstd3d11pluginutils.h"
#include "gstd3d11shader.h"
#include "gstd3d11converter.h"
#include <wrl.h>
#include <string.h>

/* *INDENT-OFF* */
using namespace Microsoft::WRL;
/* *INDENT-ON* */

GST_DEBUG_CATEGORY_STATIC (gst_d3d11_test_src_debug);
#define GST_CAT_DEFAULT gst_d3d11_test_src_debug

static GstStaticCaps template_caps =
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
    (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY, GST_D3D11_SRC_FORMATS) ";"
    GST_VIDEO_CAPS_MAKE (GST_D3D11_SRC_FORMATS));

typedef enum
{
  GST_D3D11_TEST_SRC_SMPTE,
  GST_D3D11_TEST_SRC_SNOW,
  GST_D3D11_TEST_SRC_BLACK,
  GST_D3D11_TEST_SRC_WHITE,
  GST_D3D11_TEST_SRC_RED,
  GST_D3D11_TEST_SRC_GREEN,
  GST_D3D11_TEST_SRC_BLUE,
  GST_D3D11_TEST_SRC_CHECKERS1,
  GST_D3D11_TEST_SRC_CHECKERS2,
  GST_D3D11_TEST_SRC_CHECKERS4,
  GST_D3D11_TEST_SRC_CHECKERS8,
} GstD3D11TestSrcPattern;

#define GST_TYPE_D3D11_TEST_SRC_PATTERN (gst_d3d11_test_src_pattern_get_type ())
static GType
gst_d3d11_test_src_pattern_get_type (void)
{
  static gsize pattern_type = 0;

  if (g_once_init_enter (&pattern_type)) {
    static const GEnumValue pattern_types[] = {
      {GST_D3D11_TEST_SRC_SMPTE, "SMPTE 100% color bars", "smpte"},
      {GST_D3D11_TEST_SRC_SNOW, "Random (television snow)", "snow"},
      {GST_D3D11_TEST_SRC_BLACK, "100% Black", "black"},
      {GST_D3D11_TEST_SRC_WHITE, "100% White", "white"},
      {GST_D3D11_TEST_SRC_RED, "Red", "red"},
      {GST_D3D11_TEST_SRC_GREEN, "Green", "green"},
      {GST_D3D11_TEST_SRC_BLUE, "Blue", "blue"},
      {GST_D3D11_TEST_SRC_CHECKERS1, "Checkers 1px", "checkers-1"},
      {GST_D3D11_TEST_SRC_CHECKERS2, "Checkers 2px", "checkers-2"},
      {GST_D3D11_TEST_SRC_CHECKERS4, "Checkers 4px", "checkers-4"},
      {GST_D3D11_TEST_SRC_CHECKERS8, "Checkers 8px", "checkers-8"},
      {0, nullptr, nullptr},
    };
    GType tmp = g_enum_register_static ("GstD3D11TestSrcPattern",
        pattern_types);
    g_once_init_leave (&pattern_type, tmp);
  }

  return (GType) pattern_type;
}

typedef struct
{
  union
  {
    struct
    {
      FLOAT r;
      FLOAT g;
      FLOAT b;
      FLOAT a;
    };
    FLOAT color[4];
  };
} ColorValue;

static const ColorValue color_table[] = {
  /* white */
  {1.0f, 1.0f, 1.0f, 1.0f},
  /* yellow */
  {1.0f, 1.0f, 0.0f, 1.0f},
  /* cyan */
  {0.0f, 1.0f, 1.0f, 1.0f},
  /* green */
  {0.0f, 1.0f, 0.0f, 1.0f},
  /* magenta */
  {1.0f, 0.0f, 1.0f, 1.0f},
  /* red */
  {1.0f, 0.0f, 0.0f, 1.0f},
  /* blue */
  {0.0f, 0.0f, 1.0f, 1.0f},
  /* black */
  {0.0f, 0.0f, 0.0f, 1.0f},
  /* -I */
  {0.0, 0.0f, 0.5f, 1.0f},
  /* +Q */
  {0.0f, 0.5, 1.0f, 1.0f},
  /* superblack */
  {0.0f, 0.0f, 0.0f, 1.0f},
  /* 7.421875% grey */
  {19. / 256.0f, 19. / 256.0f, 19. / 256.0, 1.0f},
};

enum
{
  COLOR_WHITE = 0,
  COLOR_YELLOW,
  COLOR_CYAN,
  COLOR_GREEN,
  COLOR_MAGENTA,
  COLOR_RED,
  COLOR_BLUE,
  COLOR_BLACK,
  COLOR_NEG_I,
  COLOR_POS_Q,
  COLOR_SUPER_BLACK,
  COLOR_DARK_GREY,
};

typedef struct
{
  ID3D11PixelShader *ps;
  ID3D11VertexShader *vs;
  ID3D11InputLayout *layout;
  ID3D11Buffer *vertex_buffer;
  ID3D11Buffer *index_buffer;
  ID3D11Buffer *const_buffer;
  guint vertex_stride;
  guint index_count;
} GstD3D11TestSrcQuad;

typedef struct
{
  const ColorValue *clear_color;
  GstD3D11TestSrcQuad *quad[2];
} GstD3D11TestSrcRender;

struct _GstD3D11TestSrc
{
  GstBaseSrc src;

  GstD3D11Device *device;
  gboolean downstream_supports_d3d11;

  GstVideoInfo info;
  GstD3D11Converter *converter;
  GstBufferPool *render_pool;
  GstBufferPool *convert_pool;

  guint adapter_index;
  GstD3D11TestSrcPattern pattern;
  GstD3D11TestSrcRender *render;
  D3D11_VIEWPORT viewport;

  gboolean reverse;
  gint64 n_frames;
  gint64 accum_frames;
  GstClockTime accum_rtime;
  GstClockTime running_time;
};

typedef struct
{
  FLOAT time;
  FLOAT padding[3];
} TimeConstBuffer;

typedef struct
{
  struct
  {
    FLOAT x;
    FLOAT y;
    FLOAT z;
  } position;
  struct
  {
    FLOAT u;
    FLOAT v;
  } texture;
} UvVertexData;

typedef struct
{
  struct
  {
    FLOAT x;
    FLOAT y;
    FLOAT z;
  } position;
  struct
  {
    FLOAT r;
    FLOAT g;
    FLOAT b;
    FLOAT a;
  } color;
} ColorVertexData;

/* *INDENT-OFF* */
static const gchar templ_vs_coord[] =
    "struct VS_INPUT {\n"
    "  float4 Position: POSITION;\n"
    "  float2 Texture: TEXCOORD;\n"
    "};\n"
    "struct VS_OUTPUT {\n"
    "  float4 Position: SV_POSITION;\n"
    "  float2 Texture: TEXCOORD;\n"
    "};\n"
    "VS_OUTPUT main (VS_INPUT input)\n"
    "{\n"
    "  return input;\n"
    "}";

static const gchar templ_vs_color[] =
    "struct VS_INPUT {\n"
    "  float4 Position: POSITION;\n"
    "  float4 Color: COLOR;\n"
    "};\n"
    "struct VS_OUTPUT {\n"
    "  float4 Position: SV_POSITION;\n"
    "  float4 Color: COLOR;\n"
    "};\n"
    "VS_OUTPUT main (VS_INPUT input)\n"
    "{\n"
    "  return input;\n"
    "}";

static const gchar templ_ps_snow[] =
    "cbuffer TimeConstBuffer : register(b0)\n"
    "{\n"
    "  float time;\n"
    "  float3 padding;\n"
    "}\n"
    "struct PS_INPUT {\n"
    "  float4 Position: SV_POSITION;\n"
    "  float2 Texture: TEXCOORD;\n"
    "};\n"
    "float get_rand(float2 uv)\n"
    "{\n"
    "  return frac(sin(dot(uv, float2(12.9898,78.233))) * 43758.5453);\n"
    "}\n"
    "float4 main(PS_INPUT input) : SV_Target\n"
    "{\n"
    "  float4 output;\n"
    "  float val = get_rand (time * input.Texture);\n"
    "  output.rgb = float3(val, val, val);\n"
    "  output.a = 1.0f;\n"
    "  return output;\n"
    "}";

static const gchar templ_ps_smpte[] =
    "struct PS_INPUT {\n"
    "  float4 Position: SV_POSITION;\n"
    "  float4 Color: COLOR;\n"
    "};\n"
    "float4 main(PS_INPUT input) : SV_TARGET\n"
    "{\n"
    "  return input.Color;\n"
    "}";

static const gchar templ_ps_checker[] =
    "static const float width = %d;\n"
    "static const float height = %d;\n"
    "static const float checker_size = %d;\n"
    "struct PS_INPUT {\n"
    "  float4 Position: SV_POSITION;\n"
    "  float2 Texture: TEXCOORD;\n"
    "};\n"
    "float4 main(PS_INPUT input) : SV_Target\n"
    "{\n"
    "  float4 output;\n"
    "  float2 xy_mod = floor (0.5 * input.Texture * float2 (width, height) / checker_size);\n"
    "  float result = fmod (xy_mod.x + xy_mod.y, 2.0);\n"
    "  output.r = step (result, 0.5);\n"
    "  output.g = 1.0 - output.r;\n"
    "  output.ba = float2 (0.0f, 1.0f);\n"
    "  return output;\n"
    "}";
/* *INDENT-ON* */

static gboolean
setup_snow_render (GstD3D11TestSrc * self, GstD3D11TestSrcRender * render,
    guint on_smpte)
{
  HRESULT hr;
  gboolean ret;
  D3D11_INPUT_ELEMENT_DESC input_desc[2];
  D3D11_BUFFER_DESC buffer_desc;
  D3D11_MAPPED_SUBRESOURCE map;
  UvVertexData *vertex_data;
  WORD *indices;
  ID3D11Device *device_handle =
      gst_d3d11_device_get_device_handle (self->device);
  ID3D11DeviceContext *context_handle =
      gst_d3d11_device_get_device_context_handle (self->device);
  ComPtr < ID3D11PixelShader > ps;
  ComPtr < ID3D11VertexShader > vs;
  ComPtr < ID3D11InputLayout > layout;
  ComPtr < ID3D11Buffer > vertex_buffer;
  ComPtr < ID3D11Buffer > index_buffer;
  ComPtr < ID3D11Buffer > const_buffer;
  GstD3D11TestSrcQuad *quad;

  memset (input_desc, 0, sizeof (input_desc));
  memset (&buffer_desc, 0, sizeof (buffer_desc));

  input_desc[0].SemanticName = "POSITION";
  input_desc[0].SemanticIndex = 0;
  input_desc[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
  input_desc[0].InputSlot = 0;
  input_desc[0].AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
  input_desc[0].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
  input_desc[0].InstanceDataStepRate = 0;

  input_desc[1].SemanticName = "TEXCOORD";
  input_desc[1].SemanticIndex = 0;
  input_desc[1].Format = DXGI_FORMAT_R32G32_FLOAT;
  input_desc[1].InputSlot = 0;
  input_desc[1].AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
  input_desc[1].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
  input_desc[1].InstanceDataStepRate = 0;

  if (!gst_d3d11_create_vertex_shader (self->device, templ_vs_coord,
          input_desc, G_N_ELEMENTS (input_desc), &vs, &layout)) {
    GST_ERROR_OBJECT (self, "Failed to compile vertext shader");
    return FALSE;
  }

  ret = gst_d3d11_create_pixel_shader (self->device, templ_ps_snow, &ps);
  if (!ret) {
    GST_ERROR_OBJECT (self, "Failed to compile pixel shader");
    return FALSE;
  }

  buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
  buffer_desc.ByteWidth = sizeof (UvVertexData) * 4;
  buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

  hr = device_handle->CreateBuffer (&buffer_desc, nullptr, &vertex_buffer);
  if (!gst_d3d11_result (hr, self->device)) {
    GST_ERROR_OBJECT (self, "Failed to create vertex buffer");
    return FALSE;
  }

  buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
  buffer_desc.ByteWidth = sizeof (WORD) * 6;
  buffer_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
  buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

  hr = device_handle->CreateBuffer (&buffer_desc, nullptr, &index_buffer);
  if (!gst_d3d11_result (hr, self->device)) {
    GST_ERROR_OBJECT (self, "Failed to create index buffer");
    return FALSE;
  }

  buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
  buffer_desc.ByteWidth = sizeof (TimeConstBuffer);
  buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  hr = device_handle->CreateBuffer (&buffer_desc, nullptr, &const_buffer);
  if (!gst_d3d11_result (hr, self->device)) {
    GST_ERROR_OBJECT (self, "Failed to create constant buffer");
    return FALSE;
  }

  gst_d3d11_device_lock (self->device);
  hr = context_handle->Map (vertex_buffer.Get (), 0, D3D11_MAP_WRITE_DISCARD, 0,
      &map);
  if (!gst_d3d11_result (hr, self->device)) {
    GST_ERROR_OBJECT (self, "Failed to map vertex buffer");
    gst_d3d11_device_unlock (self->device);
    return FALSE;
  }
  vertex_data = (UvVertexData *) map.pData;

  hr = context_handle->Map (index_buffer.Get (), 0, D3D11_MAP_WRITE_DISCARD, 0,
      &map);
  if (!gst_d3d11_result (hr, self->device)) {
    GST_ERROR_OBJECT (self, "Failed to map index buffer");
    context_handle->Unmap (vertex_buffer.Get (), 0);
    gst_d3d11_device_unlock (self->device);
    return FALSE;
  }
  indices = (WORD *) map.pData;

  if (on_smpte) {
    FLOAT left, right, top, bottom;
    FLOAT left_u, right_u, top_v, bottom_v;

    left = 0.5f;
    right = 1.0f;
    top = -0.5f;
    bottom = -1.0f;

    left_u = 3.0f / 4.0f;
    right_u = 1.0f;
    top_v = 3.0f / 4.0f;
    bottom_v = 1.0f;

    /* bottom left */
    vertex_data[0].position.x = left;
    vertex_data[0].position.y = bottom;
    vertex_data[0].position.z = 0.0f;
    vertex_data[0].texture.u = left_u;
    vertex_data[0].texture.v = bottom_v;

    /* top left */
    vertex_data[1].position.x = left;
    vertex_data[1].position.y = top;
    vertex_data[1].position.z = 0.0f;
    vertex_data[1].texture.u = left_u;
    vertex_data[1].texture.v = top_v;

    /* top right */
    vertex_data[2].position.x = right;
    vertex_data[2].position.y = top;
    vertex_data[2].position.z = 0.0f;
    vertex_data[2].texture.u = right_u;
    vertex_data[2].texture.v = top_v;

    /* bottom right */
    vertex_data[3].position.x = right;
    vertex_data[3].position.y = bottom;
    vertex_data[3].position.z = 0.0f;
    vertex_data[3].texture.u = right_u;
    vertex_data[3].texture.v = bottom_v;
  } else {
    /* bottom left */
    vertex_data[0].position.x = -1.0f;
    vertex_data[0].position.y = -1.0f;
    vertex_data[0].position.z = 0.0f;
    vertex_data[0].texture.u = 0.0f;
    vertex_data[0].texture.v = 1.0f;
    /* top left */
    vertex_data[1].position.x = -1.0f;
    vertex_data[1].position.y = 1.0f;
    vertex_data[1].position.z = 0.0f;
    vertex_data[1].texture.u = 0.0f;
    vertex_data[1].texture.v = 0.0f;

    /* top right */
    vertex_data[2].position.x = 1.0f;
    vertex_data[2].position.y = 1.0f;
    vertex_data[2].position.z = 0.0f;
    vertex_data[2].texture.u = 1.0f;
    vertex_data[2].texture.v = 0.0f;

    /* bottom right */
    vertex_data[3].position.x = 1.0f;
    vertex_data[3].position.y = -1.0f;
    vertex_data[3].position.z = 0.0f;
    vertex_data[3].texture.u = 1.0f;
    vertex_data[3].texture.v = 1.0f;
  }

  /* clockwise indexing */
  indices[0] = 0;               /* bottom left */
  indices[1] = 1;               /* top left */
  indices[2] = 2;               /* top right */

  indices[3] = 3;               /* bottom right */
  indices[4] = 0;               /* bottom left  */
  indices[5] = 2;               /* top right */

  context_handle->Unmap (vertex_buffer.Get (), 0);
  context_handle->Unmap (index_buffer.Get (), 0);
  gst_d3d11_device_unlock (self->device);

  quad = g_new0 (GstD3D11TestSrcQuad, 1);
  if (on_smpte)
    render->quad[1] = quad;
  else
    render->quad[0] = quad;

  quad->ps = ps.Detach ();
  quad->vs = vs.Detach ();
  quad->layout = layout.Detach ();
  quad->vertex_buffer = vertex_buffer.Detach ();
  quad->index_buffer = index_buffer.Detach ();
  quad->const_buffer = const_buffer.Detach ();
  quad->vertex_stride = sizeof (UvVertexData);
  quad->index_count = 6;

  return TRUE;
}

static gboolean
setup_smpte_render (GstD3D11TestSrc * self, GstD3D11TestSrcRender * render)
{
  HRESULT hr;
  gboolean ret;
  D3D11_INPUT_ELEMENT_DESC input_desc[2];
  D3D11_BUFFER_DESC buffer_desc;
  D3D11_MAPPED_SUBRESOURCE map;
  ColorVertexData *vertex_data;
  WORD *indices;
  ID3D11Device *device_handle =
      gst_d3d11_device_get_device_handle (self->device);
  ID3D11DeviceContext *context_handle =
      gst_d3d11_device_get_device_context_handle (self->device);
  ComPtr < ID3D11PixelShader > ps;
  ComPtr < ID3D11VertexShader > vs;
  ComPtr < ID3D11InputLayout > layout;
  ComPtr < ID3D11Buffer > vertex_buffer;
  ComPtr < ID3D11Buffer > index_buffer;
  GstD3D11TestSrcQuad *quad;
  guint num_vertex = 0;
  guint num_index = 0;

  memset (input_desc, 0, sizeof (input_desc));
  memset (&buffer_desc, 0, sizeof (buffer_desc));

  input_desc[0].SemanticName = "POSITION";
  input_desc[0].SemanticIndex = 0;
  input_desc[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
  input_desc[0].InputSlot = 0;
  input_desc[0].AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
  input_desc[0].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
  input_desc[0].InstanceDataStepRate = 0;

  input_desc[1].SemanticName = "COLOR";
  input_desc[1].SemanticIndex = 0;
  input_desc[1].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
  input_desc[1].InputSlot = 0;
  input_desc[1].AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
  input_desc[1].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
  input_desc[1].InstanceDataStepRate = 0;

  if (!gst_d3d11_create_vertex_shader (self->device, templ_vs_color,
          input_desc, G_N_ELEMENTS (input_desc), &vs, &layout)) {
    GST_ERROR_OBJECT (self, "Failed to compile vertext shader");
    return FALSE;
  }

  ret = gst_d3d11_create_pixel_shader (self->device, templ_ps_smpte, &ps);
  if (!ret) {
    GST_ERROR_OBJECT (self, "Failed to compile pixel shader");
    return FALSE;
  }

  buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
  buffer_desc.ByteWidth = sizeof (ColorVertexData) * 4 * 20;
  buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

  hr = device_handle->CreateBuffer (&buffer_desc, nullptr, &vertex_buffer);
  if (!gst_d3d11_result (hr, self->device)) {
    GST_ERROR_OBJECT (self, "Failed to create vertex buffer");
    return FALSE;
  }

  buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
  buffer_desc.ByteWidth = sizeof (WORD) * 6 * 20;
  buffer_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
  buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

  hr = device_handle->CreateBuffer (&buffer_desc, nullptr, &index_buffer);
  if (!gst_d3d11_result (hr, self->device)) {
    GST_ERROR_OBJECT (self, "Failed to create index buffer");
    return FALSE;
  }

  gst_d3d11_device_lock (self->device);
  hr = context_handle->Map (vertex_buffer.Get (), 0, D3D11_MAP_WRITE_DISCARD, 0,
      &map);
  if (!gst_d3d11_result (hr, self->device)) {
    GST_ERROR_OBJECT (self, "Failed to map vertex buffer");
    gst_d3d11_device_unlock (self->device);
    return FALSE;
  }
  vertex_data = (ColorVertexData *) map.pData;

  hr = context_handle->Map (index_buffer.Get (), 0, D3D11_MAP_WRITE_DISCARD, 0,
      &map);
  if (!gst_d3d11_result (hr, self->device)) {
    GST_ERROR_OBJECT (self, "Failed to map index buffer");
    context_handle->Unmap (vertex_buffer.Get (), 0);
    gst_d3d11_device_unlock (self->device);
    return FALSE;
  }
  indices = (WORD *) map.pData;

  /* top row */
  for (guint i = 0; i < 7; i++) {
    FLOAT left, right, top, bottom;
    FLOAT scale = 2.0f / 7.0f;
    guint base = i * 4;
    guint idx_base = i * 6;
    const ColorValue *color = &color_table[i];

    left = -1.0f + i * scale;
    right = -1.0f + (i + 1) * scale;
    top = 1.0f;
    bottom = -1.0f / 3.0f;

    /* bottom left */
    vertex_data[base].position.x = left;
    vertex_data[base].position.y = bottom;
    vertex_data[base].position.z = 0.0f;
    vertex_data[base].color.r = color->r;
    vertex_data[base].color.g = color->g;
    vertex_data[base].color.b = color->b;
    vertex_data[base].color.a = color->a;

    /* top left */
    vertex_data[base + 1].position.x = left;
    vertex_data[base + 1].position.y = top;
    vertex_data[base + 1].position.z = 0.0f;
    vertex_data[base + 1].color = vertex_data[base].color;

    /* top right */
    vertex_data[base + 2].position.x = right;
    vertex_data[base + 2].position.y = top;
    vertex_data[base + 2].position.z = 0.0f;
    vertex_data[base + 2].color = vertex_data[base].color;

    /* bottom right */
    vertex_data[base + 3].position.x = right;
    vertex_data[base + 3].position.y = bottom;
    vertex_data[base + 3].position.z = 0.0f;
    vertex_data[base + 3].color = vertex_data[base].color;

    /* clockwise indexing */
    indices[idx_base] = base;   /* bottom left */
    indices[idx_base + 1] = base + 1;   /* top left */
    indices[idx_base + 2] = base + 2;   /* top right */

    indices[idx_base + 3] = base + 3;   /* bottom right */
    indices[idx_base + 4] = base;       /* bottom left  */
    indices[idx_base + 5] = base + 2;   /* top right */
  }
  num_vertex += 4 * 7;
  num_index += 6 * 7;

  /* middle row */
  for (guint i = 0; i < 7; i++) {
    FLOAT left, right, top, bottom;
    FLOAT scale = 2.0f / 7.0f;
    guint base = i * 4 + num_vertex;
    guint idx_base = i * 6 + num_index;
    const ColorValue *color;

    if ((i % 2) != 0)
      color = &color_table[COLOR_BLACK];
    else
      color = &color_table[COLOR_BLUE - i];

    left = -1.0f + i * scale;
    right = -1.0f + (i + 1) * scale;
    top = -1.0f / 3.0f;
    bottom = -0.5f;

    /* bottom left */
    vertex_data[base].position.x = left;
    vertex_data[base].position.y = bottom;
    vertex_data[base].position.z = 0.0f;
    vertex_data[base].color.r = color->r;
    vertex_data[base].color.g = color->g;
    vertex_data[base].color.b = color->b;
    vertex_data[base].color.a = color->a;

    /* top left */
    vertex_data[base + 1].position.x = left;
    vertex_data[base + 1].position.y = top;
    vertex_data[base + 1].position.z = 0.0f;
    vertex_data[base + 1].color = vertex_data[base].color;

    /* top right */
    vertex_data[base + 2].position.x = right;
    vertex_data[base + 2].position.y = top;
    vertex_data[base + 2].position.z = 0.0f;
    vertex_data[base + 2].color = vertex_data[base].color;

    /* bottom right */
    vertex_data[base + 3].position.x = right;
    vertex_data[base + 3].position.y = bottom;
    vertex_data[base + 3].position.z = 0.0f;
    vertex_data[base + 3].color = vertex_data[base].color;

    /* clockwise indexing */
    indices[idx_base] = base;   /* bottom left */
    indices[idx_base + 1] = base + 1;   /* top left */
    indices[idx_base + 2] = base + 2;   /* top right */

    indices[idx_base + 3] = base + 3;   /* bottom right */
    indices[idx_base + 4] = base;       /* bottom left  */
    indices[idx_base + 5] = base + 2;   /* top right */
  }
  num_vertex += 4 * 7;
  num_index += 6 * 7;

  /* bottom row, left three */
  for (guint i = 0; i < 3; i++) {
    FLOAT left, right, top, bottom;
    FLOAT scale = 1.0f / 3.0f;
    guint base = i * 4 + num_vertex;
    guint idx_base = i * 6 + num_index;
    const ColorValue *color;

    if (i == 0)
      color = &color_table[COLOR_NEG_I];
    else if (i == 1)
      color = &color_table[COLOR_WHITE];
    else
      color = &color_table[COLOR_POS_Q];

    left = -1.0f + i * scale;
    right = -1.0f + (i + 1) * scale;
    top = -0.5f;
    bottom = -1.0f;

    /* bottom left */
    vertex_data[base].position.x = left;
    vertex_data[base].position.y = bottom;
    vertex_data[base].position.z = 0.0f;
    vertex_data[base].color.r = color->r;
    vertex_data[base].color.g = color->g;
    vertex_data[base].color.b = color->b;
    vertex_data[base].color.a = color->a;

    /* top left */
    vertex_data[base + 1].position.x = left;
    vertex_data[base + 1].position.y = top;
    vertex_data[base + 1].position.z = 0.0f;
    vertex_data[base + 1].color = vertex_data[base].color;

    /* top right */
    vertex_data[base + 2].position.x = right;
    vertex_data[base + 2].position.y = top;
    vertex_data[base + 2].position.z = 0.0f;
    vertex_data[base + 2].color = vertex_data[base].color;

    /* bottom right */
    vertex_data[base + 3].position.x = right;
    vertex_data[base + 3].position.y = bottom;
    vertex_data[base + 3].position.z = 0.0f;
    vertex_data[base + 3].color = vertex_data[base].color;

    /* clockwise indexing */
    indices[idx_base] = base;   /* bottom left */
    indices[idx_base + 1] = base + 1;   /* top left */
    indices[idx_base + 2] = base + 2;   /* top right */

    indices[idx_base + 3] = base + 3;   /* bottom right */
    indices[idx_base + 4] = base;       /* bottom left  */
    indices[idx_base + 5] = base + 2;   /* top right */
  }
  num_vertex += 4 * 3;
  num_index += 6 * 3;

  /* bottom row, middle three */
  for (guint i = 0; i < 3; i++) {
    FLOAT left, right, top, bottom;
    FLOAT scale = 1.0f / 6.0f;
    guint base = i * 4 + num_vertex;
    guint idx_base = i * 6 + num_index;
    const ColorValue *color;

    if (i == 0)
      color = &color_table[COLOR_SUPER_BLACK];
    else if (i == 1)
      color = &color_table[COLOR_BLACK];
    else
      color = &color_table[COLOR_DARK_GREY];

    left = i * scale;
    right = (i + 1) * scale;
    top = -0.5f;
    bottom = -1.0f;

    /* bottom left */
    vertex_data[base].position.x = left;
    vertex_data[base].position.y = bottom;
    vertex_data[base].position.z = 0.0f;
    vertex_data[base].color.r = color->r;
    vertex_data[base].color.g = color->g;
    vertex_data[base].color.b = color->b;
    vertex_data[base].color.a = color->a;

    /* top left */
    vertex_data[base + 1].position.x = left;
    vertex_data[base + 1].position.y = top;
    vertex_data[base + 1].position.z = 0.0f;
    vertex_data[base + 1].color = vertex_data[base].color;

    /* top right */
    vertex_data[base + 2].position.x = right;
    vertex_data[base + 2].position.y = top;
    vertex_data[base + 2].position.z = 0.0f;
    vertex_data[base + 2].color = vertex_data[base].color;

    /* bottom right */
    vertex_data[base + 3].position.x = right;
    vertex_data[base + 3].position.y = bottom;
    vertex_data[base + 3].position.z = 0.0f;
    vertex_data[base + 3].color = vertex_data[base].color;

    /* clockwise indexing */
    indices[idx_base] = base;   /* bottom left */
    indices[idx_base + 1] = base + 1;   /* top left */
    indices[idx_base + 2] = base + 2;   /* top right */

    indices[idx_base + 3] = base + 3;   /* bottom right */
    indices[idx_base + 4] = base;       /* bottom left  */
    indices[idx_base + 5] = base + 2;   /* top right */
  }

  context_handle->Unmap (vertex_buffer.Get (), 0);
  context_handle->Unmap (index_buffer.Get (), 0);
  gst_d3d11_device_unlock (self->device);

  render->quad[0] = quad = g_new0 (GstD3D11TestSrcQuad, 1);

  quad->ps = ps.Detach ();
  quad->vs = vs.Detach ();
  quad->layout = layout.Detach ();
  quad->vertex_buffer = vertex_buffer.Detach ();
  quad->index_buffer = index_buffer.Detach ();
  quad->vertex_stride = sizeof (ColorVertexData);
  quad->index_count = 6 * 20;

  return setup_snow_render (self, render, TRUE);
}

static gboolean
setup_checker_render (GstD3D11TestSrc * self, GstD3D11TestSrcRender * render,
    guint checker_size)
{
  HRESULT hr;
  gboolean ret;
  D3D11_INPUT_ELEMENT_DESC input_desc[2];
  D3D11_BUFFER_DESC buffer_desc;
  D3D11_MAPPED_SUBRESOURCE map;
  UvVertexData *vertex_data;
  WORD *indices;
  ID3D11Device *device_handle =
      gst_d3d11_device_get_device_handle (self->device);
  ID3D11DeviceContext *context_handle =
      gst_d3d11_device_get_device_context_handle (self->device);
  ComPtr < ID3D11PixelShader > ps;
  ComPtr < ID3D11VertexShader > vs;
  ComPtr < ID3D11InputLayout > layout;
  ComPtr < ID3D11Buffer > vertex_buffer;
  ComPtr < ID3D11Buffer > index_buffer;
  GstD3D11TestSrcQuad *quad;
  gchar *ps_src;

  memset (input_desc, 0, sizeof (input_desc));
  memset (&buffer_desc, 0, sizeof (buffer_desc));

  input_desc[0].SemanticName = "POSITION";
  input_desc[0].SemanticIndex = 0;
  input_desc[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
  input_desc[0].InputSlot = 0;
  input_desc[0].AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
  input_desc[0].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
  input_desc[0].InstanceDataStepRate = 0;

  input_desc[1].SemanticName = "TEXCOORD";
  input_desc[1].SemanticIndex = 0;
  input_desc[1].Format = DXGI_FORMAT_R32G32_FLOAT;
  input_desc[1].InputSlot = 0;
  input_desc[1].AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
  input_desc[1].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
  input_desc[1].InstanceDataStepRate = 0;

  if (!gst_d3d11_create_vertex_shader (self->device, templ_vs_coord,
          input_desc, G_N_ELEMENTS (input_desc), &vs, &layout)) {
    GST_ERROR_OBJECT (self, "Failed to compile vertext shader");
    return FALSE;
  }

  ps_src = g_strdup_printf (templ_ps_checker,
      self->info.width, self->info.height, checker_size);
  ret = gst_d3d11_create_pixel_shader (self->device, ps_src, &ps);
  g_free (ps_src);
  if (!ret) {
    GST_ERROR_OBJECT (self, "Failed to compile pixel shader");
    return FALSE;
  }

  buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
  buffer_desc.ByteWidth = sizeof (UvVertexData) * 4;
  buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

  hr = device_handle->CreateBuffer (&buffer_desc, nullptr, &vertex_buffer);
  if (!gst_d3d11_result (hr, self->device)) {
    GST_ERROR_OBJECT (self, "Failed to create vertex buffer");
    return FALSE;
  }

  buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
  buffer_desc.ByteWidth = sizeof (WORD) * 6;
  buffer_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
  buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

  hr = device_handle->CreateBuffer (&buffer_desc, nullptr, &index_buffer);
  if (!gst_d3d11_result (hr, self->device)) {
    GST_ERROR_OBJECT (self, "Failed to create index buffer");
    return FALSE;
  }

  gst_d3d11_device_lock (self->device);
  hr = context_handle->Map (vertex_buffer.Get (), 0, D3D11_MAP_WRITE_DISCARD, 0,
      &map);
  if (!gst_d3d11_result (hr, self->device)) {
    GST_ERROR_OBJECT (self, "Failed to map vertex buffer");
    gst_d3d11_device_unlock (self->device);
    return FALSE;
  }
  vertex_data = (UvVertexData *) map.pData;

  hr = context_handle->Map (index_buffer.Get (), 0, D3D11_MAP_WRITE_DISCARD, 0,
      &map);
  if (!gst_d3d11_result (hr, self->device)) {
    GST_ERROR_OBJECT (self, "Failed to map index buffer");
    context_handle->Unmap (vertex_buffer.Get (), 0);
    gst_d3d11_device_unlock (self->device);
    return FALSE;
  }
  indices = (WORD *) map.pData;

  /* bottom left */
  vertex_data[0].position.x = -1.0f;
  vertex_data[0].position.y = -1.0f;
  vertex_data[0].position.z = 0.0f;
  vertex_data[0].texture.u = 0.0f;
  vertex_data[0].texture.v = 1.0f;
  /* top left */
  vertex_data[1].position.x = -1.0f;
  vertex_data[1].position.y = 1.0f;
  vertex_data[1].position.z = 0.0f;
  vertex_data[1].texture.u = 0.0f;
  vertex_data[1].texture.v = 0.0f;

  /* top right */
  vertex_data[2].position.x = 1.0f;
  vertex_data[2].position.y = 1.0f;
  vertex_data[2].position.z = 0.0f;
  vertex_data[2].texture.u = 1.0f;
  vertex_data[2].texture.v = 0.0f;

  /* bottom right */
  vertex_data[3].position.x = 1.0f;
  vertex_data[3].position.y = -1.0f;
  vertex_data[3].position.z = 0.0f;
  vertex_data[3].texture.u = 1.0f;
  vertex_data[3].texture.v = 1.0f;

  /* clockwise indexing */
  indices[0] = 0;               /* bottom left */
  indices[1] = 1;               /* top left */
  indices[2] = 2;               /* top right */

  indices[3] = 3;               /* bottom right */
  indices[4] = 0;               /* bottom left  */
  indices[5] = 2;               /* top right */

  context_handle->Unmap (vertex_buffer.Get (), 0);
  context_handle->Unmap (index_buffer.Get (), 0);
  gst_d3d11_device_unlock (self->device);

  render->quad[0] = quad = g_new0 (GstD3D11TestSrcQuad, 1);

  quad->ps = ps.Detach ();
  quad->vs = vs.Detach ();
  quad->layout = layout.Detach ();
  quad->vertex_buffer = vertex_buffer.Detach ();
  quad->index_buffer = index_buffer.Detach ();
  quad->vertex_stride = sizeof (UvVertexData);
  quad->index_count = 6;

  return TRUE;
}

enum
{
  PROP_0,
  PROP_ADAPTER,
  PROP_IS_LIVE,
  PROP_PATTERN,
};

#define DEFAULT_ADAPTER -1
#define DEFAULT_PATTERN GST_D3D11_TEST_SRC_SMPTE

static void gst_d3d11_test_src_dispose (GObject * object);
static void gst_d3d11_test_src_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_d3d11_test_src_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static void gst_d3d11_test_src_set_context (GstElement * element,
    GstContext * context);

static gboolean gst_d3d11_test_src_is_seekable (GstBaseSrc * bsrc);
static gboolean gst_d3d11_test_src_do_seek (GstBaseSrc * bsrc,
    GstSegment * segment);
static GstCaps *gst_d3d11_test_src_fixate (GstBaseSrc * bsrc, GstCaps * caps);
static gboolean gst_d3d11_test_src_set_caps (GstBaseSrc * bsrc, GstCaps * caps);
static gboolean gst_d3d11_test_src_decide_allocation (GstBaseSrc *
    bsrc, GstQuery * query);
static gboolean gst_d3d11_test_src_start (GstBaseSrc * bsrc);
static gboolean gst_d3d11_test_src_stop (GstBaseSrc * bsrc);
static gboolean gst_d3d11_test_src_src_query (GstBaseSrc * bsrc,
    GstQuery * query);
static void gst_d3d11_test_src_get_times (GstBaseSrc * bsrc,
    GstBuffer * buffer, GstClockTime * start, GstClockTime * end);
static GstFlowReturn gst_d3d11_test_src_create (GstBaseSrc * bsrc,
    guint64 offset, guint size, GstBuffer ** buf);

#define gst_d3d11_test_src_parent_class parent_class
G_DEFINE_TYPE (GstD3D11TestSrc, gst_d3d11_test_src, GST_TYPE_BASE_SRC);

static void
gst_d3d11_test_src_class_init (GstD3D11TestSrcClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseSrcClass *basesrc_class = GST_BASE_SRC_CLASS (klass);
  GstCaps *caps;

  gobject_class->dispose = gst_d3d11_test_src_dispose;
  gobject_class->set_property = gst_d3d11_test_src_set_property;
  gobject_class->get_property = gst_d3d11_test_src_get_property;

  g_object_class_install_property (gobject_class, PROP_ADAPTER,
      g_param_spec_int ("adapter", "Adapter",
          "DXGI Adapter index (-1 for any device)",
          -1, G_MAXINT32, DEFAULT_ADAPTER,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
              G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_IS_LIVE,
      g_param_spec_boolean ("is-live", "Is Live",
          "Whether to act as a live source", FALSE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_PATTERN,
      g_param_spec_enum ("pattern", "Pattern",
          "Type of test pattern to generate", GST_TYPE_D3D11_TEST_SRC_PATTERN,
          DEFAULT_PATTERN,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
              G_PARAM_STATIC_STRINGS)));

  element_class->set_context =
      GST_DEBUG_FUNCPTR (gst_d3d11_test_src_set_context);

  gst_element_class_set_static_metadata (element_class,
      "Direct3D11 test src", "Source/Video",
      "Creates a test video stream", "Seungha Yang <seungha@centricular.com>");

  caps = gst_d3d11_get_updated_template_caps (&template_caps);
  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS, caps));
  gst_caps_unref (caps);

  basesrc_class->is_seekable =
      GST_DEBUG_FUNCPTR (gst_d3d11_test_src_is_seekable);
  basesrc_class->do_seek = GST_DEBUG_FUNCPTR (gst_d3d11_test_src_do_seek);
  basesrc_class->fixate = GST_DEBUG_FUNCPTR (gst_d3d11_test_src_fixate);
  basesrc_class->set_caps = GST_DEBUG_FUNCPTR (gst_d3d11_test_src_set_caps);
  basesrc_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_d3d11_test_src_decide_allocation);
  basesrc_class->start = GST_DEBUG_FUNCPTR (gst_d3d11_test_src_start);
  basesrc_class->stop = GST_DEBUG_FUNCPTR (gst_d3d11_test_src_stop);
  basesrc_class->query = GST_DEBUG_FUNCPTR (gst_d3d11_test_src_src_query);
  basesrc_class->get_times = GST_DEBUG_FUNCPTR (gst_d3d11_test_src_get_times);
  basesrc_class->create = GST_DEBUG_FUNCPTR (gst_d3d11_test_src_create);

  GST_DEBUG_CATEGORY_INIT (gst_d3d11_test_src_debug, "d3d11testsrc", 0,
      "d3d11testsrc");

  gst_type_mark_as_plugin_api (GST_TYPE_D3D11_TEST_SRC_PATTERN,
      (GstPluginAPIFlags) 0);
}

static void
gst_d3d11_test_src_init (GstD3D11TestSrc * self)
{
  gst_base_src_set_format (GST_BASE_SRC (self), GST_FORMAT_TIME);

  self->adapter_index = DEFAULT_ADAPTER;
  self->pattern = DEFAULT_PATTERN;
}

static void
gst_d3d11_test_src_dispose (GObject * object)
{
  GstD3D11TestSrc *self = GST_D3D11_TEST_SRC (object);

  gst_clear_object (&self->device);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_d3d11_test_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstD3D11TestSrc *self = GST_D3D11_TEST_SRC (object);

  switch (prop_id) {
    case PROP_ADAPTER:
      self->adapter_index = g_value_get_int (value);
      break;
    case PROP_IS_LIVE:
      gst_base_src_set_live (GST_BASE_SRC (self), g_value_get_boolean (value));
      break;
    case PROP_PATTERN:
      self->pattern = (GstD3D11TestSrcPattern) g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_d3d11_test_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstD3D11TestSrc *self = GST_D3D11_TEST_SRC (object);

  switch (prop_id) {
    case PROP_ADAPTER:
      g_value_set_int (value, self->adapter_index);
      break;
    case PROP_IS_LIVE:
      g_value_set_boolean (value, gst_base_src_is_live (GST_BASE_SRC (self)));
      break;
    case PROP_PATTERN:
      g_value_set_enum (value, self->pattern);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_d3d11_test_src_set_context (GstElement * element, GstContext * context)
{
  GstD3D11TestSrc *self = GST_D3D11_TEST_SRC (element);

  gst_d3d11_handle_set_context (element,
      context, self->adapter_index, &self->device);

  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

static gboolean
gst_d3d11_test_src_is_seekable (GstBaseSrc * bsrc)
{
  return TRUE;
}

static gboolean
gst_d3d11_test_src_do_seek (GstBaseSrc * bsrc, GstSegment * segment)
{
  GstD3D11TestSrc *self = GST_D3D11_TEST_SRC (bsrc);
  GstClockTime position;

  segment->time = segment->start;
  position = segment->position;
  self->reverse = segment->rate < 0;

  /* now move to the position indicated */
  if (self->info.fps_n) {
    self->n_frames = gst_util_uint64_scale (position,
        self->info.fps_n, self->info.fps_d * GST_SECOND);
  } else {
    self->n_frames = 0;
  }
  self->accum_frames = 0;
  self->accum_rtime = 0;
  if (self->info.fps_n) {
    self->running_time = gst_util_uint64_scale (self->n_frames,
        self->info.fps_d * GST_SECOND, self->info.fps_n);
  } else {
    /* FIXME : Not sure what to set here */
    self->running_time = 0;
  }

  return TRUE;
}

static GstCaps *
gst_d3d11_test_src_fixate (GstBaseSrc * bsrc, GstCaps * caps)
{
  GstStructure *s;

  caps = gst_caps_make_writable (caps);
  s = gst_caps_get_structure (caps, 0);

  gst_structure_fixate_field_nearest_int (s, "width", 320);
  gst_structure_fixate_field_nearest_int (s, "height", 240);
  gst_structure_fixate_field_nearest_fraction (s, "framerate", 30, 1);

  return GST_BASE_SRC_CLASS (parent_class)->fixate (bsrc, caps);
}

static void
gst_d3d11_test_src_quad_free (GstD3D11TestSrcQuad * quad)
{
  if (!quad)
    return;

  GST_D3D11_CLEAR_COM (quad->ps);
  GST_D3D11_CLEAR_COM (quad->vs);
  GST_D3D11_CLEAR_COM (quad->layout);
  GST_D3D11_CLEAR_COM (quad->vertex_buffer);
  GST_D3D11_CLEAR_COM (quad->index_buffer);
  GST_D3D11_CLEAR_COM (quad->const_buffer);

  g_free (quad);
}

static void
gst_d3d11_test_src_render_free (GstD3D11TestSrcRender * render)
{
  if (!render)
    return;

  for (guint i = 0; i < G_N_ELEMENTS (render->quad); i++)
    g_clear_pointer (&render->quad[i], gst_d3d11_test_src_quad_free);

  g_free (render);
}

static void
gst_d3d11_test_src_clear_resource (GstD3D11TestSrc * self)
{
  if (self->render_pool) {
    gst_buffer_pool_set_active (self->render_pool, FALSE);
    gst_clear_object (&self->render_pool);
  }

  if (self->convert_pool) {
    gst_buffer_pool_set_active (self->convert_pool, FALSE);
    gst_clear_object (&self->convert_pool);
  }

  g_clear_pointer (&self->render, gst_d3d11_test_src_render_free);
  gst_clear_object (&self->converter);
}

static gboolean
gst_d3d11_test_src_setup_resource (GstD3D11TestSrc * self, GstCaps * caps)
{
  GstVideoInfo draw_info;
  GstCaps *draw_caps;
  GstD3D11AllocationParams *params;
  GstD3D11TestSrcRender *render;
  GstD3D11ConverterMethod method = GST_D3D11_CONVERTER_METHOD_SHADER;

  gst_video_info_set_format (&draw_info, GST_VIDEO_FORMAT_RGBA,
      self->info.width, self->info.height);
  self->converter = gst_d3d11_converter_new (self->device,
      &draw_info, &self->info, &method);

  if (!self->converter) {
    GST_ERROR_OBJECT (self, "Failed to create converter");
    goto error;
  }

  draw_caps = gst_video_info_to_caps (&draw_info);
  params = gst_d3d11_allocation_params_new (self->device, &draw_info,
      GST_D3D11_ALLOCATION_FLAG_DEFAULT,
      D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, 0);

  self->render_pool = gst_d3d11_buffer_pool_new_with_options (self->device,
      draw_caps, params, 0, 0);
  gst_d3d11_allocation_params_free (params);
  gst_caps_unref (draw_caps);

  if (!self->render_pool
      || !gst_buffer_pool_set_active (self->render_pool, TRUE)) {
    GST_ERROR_OBJECT (self, "Failed to configure draw pool");
    goto error;
  }

  params = gst_d3d11_allocation_params_new (self->device, &self->info,
      GST_D3D11_ALLOCATION_FLAG_DEFAULT, D3D11_BIND_RENDER_TARGET, 0);
  self->convert_pool = gst_d3d11_buffer_pool_new_with_options (self->device,
      caps, params, 0, 0);
  gst_d3d11_allocation_params_free (params);

  if (!self->convert_pool ||
      !gst_buffer_pool_set_active (self->convert_pool, TRUE)) {
    GST_ERROR_OBJECT (self, "Failed to configure draw pool");
    goto error;
  }

  self->viewport.TopLeftX = 0;
  self->viewport.TopLeftY = 0;
  self->viewport.Width = self->info.width;
  self->viewport.Height = self->info.height;
  self->viewport.MinDepth = 0.0f;
  self->viewport.MaxDepth = 1.0f;

  self->render = render = g_new0 (GstD3D11TestSrcRender, 1);

  switch (self->pattern) {
    case GST_D3D11_TEST_SRC_SMPTE:
      if (!setup_smpte_render (self, render))
        goto error;
      break;
    case GST_D3D11_TEST_SRC_SNOW:
      if (!setup_snow_render (self, render, FALSE))
        goto error;
      break;
    case GST_D3D11_TEST_SRC_BLACK:
      render->clear_color = &color_table[COLOR_BLACK];
      break;
    case GST_D3D11_TEST_SRC_WHITE:
      render->clear_color = &color_table[COLOR_WHITE];
      break;
    case GST_D3D11_TEST_SRC_RED:
      render->clear_color = &color_table[COLOR_RED];
      break;
    case GST_D3D11_TEST_SRC_GREEN:
      render->clear_color = &color_table[COLOR_GREEN];
      break;
    case GST_D3D11_TEST_SRC_BLUE:
      render->clear_color = &color_table[COLOR_BLUE];
      break;
    case GST_D3D11_TEST_SRC_CHECKERS1:
      if (!setup_checker_render (self, render, 1))
        goto error;
      break;
    case GST_D3D11_TEST_SRC_CHECKERS2:
      if (!setup_checker_render (self, render, 2))
        goto error;
      break;
    case GST_D3D11_TEST_SRC_CHECKERS4:
      if (!setup_checker_render (self, render, 4))
        goto error;
      break;
    case GST_D3D11_TEST_SRC_CHECKERS8:
      if (!setup_checker_render (self, render, 8))
        goto error;
      break;
  }

  return TRUE;

error:
  gst_d3d11_test_src_clear_resource (self);
  return FALSE;
}

static gboolean
gst_d3d11_test_src_set_caps (GstBaseSrc * bsrc, GstCaps * caps)
{
  GstD3D11TestSrc *self = GST_D3D11_TEST_SRC (bsrc);
  GstCapsFeatures *features;

  GST_DEBUG_OBJECT (self, "Set caps %" GST_PTR_FORMAT, caps);

  gst_d3d11_test_src_clear_resource (self);

  features = gst_caps_get_features (caps, 0);
  if (features && gst_caps_features_contains (features,
          GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY)) {
    self->downstream_supports_d3d11 = TRUE;
  } else {
    self->downstream_supports_d3d11 = FALSE;
  }

  GST_OBJECT_LOCK (self);
  gst_video_info_from_caps (&self->info, caps);
  GST_OBJECT_UNLOCK (self);
  if (self->info.fps_d <= 0 || self->info.fps_n <= 0) {
    GST_ERROR_OBJECT (self, "Invalid framerate %d/%d", self->info.fps_n,
        self->info.fps_d);
    return FALSE;
  }

  gst_base_src_set_blocksize (bsrc, GST_VIDEO_INFO_SIZE (&self->info));

  return gst_d3d11_test_src_setup_resource (self, caps);
}

static gboolean
gst_d3d11_test_src_decide_allocation (GstBaseSrc * bsrc, GstQuery * query)
{
  GstD3D11TestSrc *self = GST_D3D11_TEST_SRC (bsrc);
  GstBufferPool *pool = nullptr;
  GstStructure *config;
  GstD3D11AllocationParams *d3d11_params;
  GstCaps *caps;
  guint min, max, size;
  gboolean update_pool;
  GstVideoInfo vinfo;

  gst_query_parse_allocation (query, &caps, nullptr);

  if (!caps) {
    GST_ERROR_OBJECT (self, "No output caps");
    return FALSE;
  }

  gst_video_info_from_caps (&vinfo, caps);

  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
    update_pool = TRUE;
  } else {
    size = GST_VIDEO_INFO_SIZE (&vinfo);

    min = max = 0;
    update_pool = FALSE;
  }

  if (pool && self->downstream_supports_d3d11) {
    if (!GST_IS_D3D11_BUFFER_POOL (pool)) {
      gst_clear_object (&pool);
    } else {
      GstD3D11BufferPool *dpool = GST_D3D11_BUFFER_POOL (pool);
      if (dpool->device != self->device)
        gst_clear_object (&pool);
    }
  }

  if (!pool) {
    if (self->downstream_supports_d3d11)
      pool = gst_d3d11_buffer_pool_new (self->device);
    else
      pool = gst_video_buffer_pool_new ();
  }

  config = gst_buffer_pool_get_config (pool);

  gst_buffer_pool_config_set_params (config, caps, size, min, max);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  if (self->downstream_supports_d3d11) {
    d3d11_params = gst_buffer_pool_config_get_d3d11_allocation_params (config);
    if (!d3d11_params) {
      d3d11_params = gst_d3d11_allocation_params_new (self->device, &vinfo,
          GST_D3D11_ALLOCATION_FLAG_DEFAULT,
          D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, 0);
    } else {
      d3d11_params->desc[0].BindFlags |= D3D11_BIND_RENDER_TARGET;
    }

    gst_buffer_pool_config_set_d3d11_allocation_params (config, d3d11_params);
    gst_d3d11_allocation_params_free (d3d11_params);
  }

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_ERROR_OBJECT (self, "Failed to set config");
    gst_clear_object (&pool);
    return FALSE;
  }

  if (update_pool)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);

  gst_object_unref (pool);

  return TRUE;
}

static gboolean
gst_d3d11_test_src_start (GstBaseSrc * bsrc)
{
  GstD3D11TestSrc *self = GST_D3D11_TEST_SRC (bsrc);

  if (!gst_d3d11_ensure_element_data (GST_ELEMENT (bsrc), self->adapter_index,
          &self->device)) {
    GST_ELEMENT_ERROR (self, RESOURCE, OPEN_READ,
        ("Failed to prepare device"), (nullptr));
    return FALSE;
  }

  self->running_time = 0;
  self->reverse = FALSE;
  self->n_frames = 0;
  self->accum_frames = 0;
  self->accum_rtime = 0;

  gst_video_info_init (&self->info);

  return TRUE;
}

static gboolean
gst_d3d11_test_src_stop (GstBaseSrc * bsrc)
{
  GstD3D11TestSrc *self = GST_D3D11_TEST_SRC (bsrc);

  gst_d3d11_test_src_clear_resource (self);
  gst_clear_object (&self->device);

  return TRUE;
}

static gboolean
gst_d3d11_test_src_src_query (GstBaseSrc * bsrc, GstQuery * query)
{
  GstD3D11TestSrc *self = GST_D3D11_TEST_SRC (bsrc);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
      if (gst_d3d11_handle_context_query (GST_ELEMENT_CAST (self), query,
              self->device)) {
        return TRUE;
      }
      break;
    case GST_QUERY_LATENCY:
      GST_OBJECT_LOCK (self);
      if (self->info.fps_n > 0 && self->info.fps_d > 0) {
        GstClockTime latency;

        latency =
            gst_util_uint64_scale (GST_SECOND, self->info.fps_d,
            self->info.fps_n);
        GST_OBJECT_UNLOCK (self);
        gst_query_set_latency (query,
            gst_base_src_is_live (bsrc), latency, GST_CLOCK_TIME_NONE);
        GST_DEBUG_OBJECT (self, "Reporting latency of %" GST_TIME_FORMAT,
            GST_TIME_ARGS (latency));
        return TRUE;
      }
      GST_OBJECT_UNLOCK (self);
      break;
    case GST_QUERY_DURATION:
      if (bsrc->num_buffers > 0) {
        GstFormat format;

        gst_query_parse_duration (query, &format, nullptr);
        if (format != GST_FORMAT_TIME)
          return FALSE;

        GST_OBJECT_LOCK (self);
        if (format == GST_FORMAT_TIME && self->info.fps_n > 0 &&
            self->info.fps_d > 0) {
          gint64 dur;
          dur = gst_util_uint64_scale_int_round (bsrc->num_buffers
              * GST_SECOND, self->info.fps_d, self->info.fps_n);
          gst_query_set_duration (query, GST_FORMAT_TIME, dur);
          GST_OBJECT_UNLOCK (self);
          return TRUE;
        }

        GST_OBJECT_UNLOCK (self);
      }
      break;
    default:
      break;
  }

  return GST_BASE_SRC_CLASS (parent_class)->query (bsrc, query);
}

static void
gst_d3d11_test_src_get_times (GstBaseSrc * bsrc, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end)
{
  /* for live sources, sync on the timestamp of the buffer */
  if (gst_base_src_is_live (bsrc)) {
    GstClockTime timestamp = GST_BUFFER_PTS (buffer);

    if (GST_CLOCK_TIME_IS_VALID (timestamp)) {
      /* get duration to calculate end time */
      GstClockTime duration = GST_BUFFER_DURATION (buffer);

      if (GST_CLOCK_TIME_IS_VALID (duration)) {
        *end = timestamp + duration;
      }
      *start = timestamp;
    }
  } else {
    *start = -1;
    *end = -1;
  }
}

static gboolean
gst_d3d11_test_src_draw_pattern (GstD3D11TestSrc * self,
    ID3D11DeviceContext * context, ID3D11RenderTargetView * rtv)
{
  GstD3D11TestSrcRender *render = self->render;
  HRESULT hr;
  TimeConstBuffer *time_buf;
  D3D11_MAPPED_SUBRESOURCE map;
  UINT offsets = 0;

  if (render->clear_color) {
    context->ClearRenderTargetView (rtv, render->clear_color->color);
    return TRUE;
  }

  context->IASetPrimitiveTopology (D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context->RSSetViewports (1, &self->viewport);
  context->OMSetRenderTargets (1, &rtv, nullptr);
  context->OMSetBlendState (nullptr, nullptr, 0xffffffff);

  for (guint i = 0; i < G_N_ELEMENTS (render->quad); i++) {
    GstD3D11TestSrcQuad *quad = render->quad[i];

    if (!quad)
      break;

    if (quad->const_buffer) {
      hr = context->Map (quad->const_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0,
          &map);
      if (!gst_d3d11_result (hr, self->device)) {
        GST_ERROR_OBJECT (self, "Failed to map constant buffer");
        return FALSE;
      }

      time_buf = (TimeConstBuffer *) map.pData;
      time_buf->time = (FLOAT) ((FLOAT) (self->running_time + 1) / GST_SECOND);
      context->Unmap (quad->const_buffer, 0);

      context->PSSetConstantBuffers (0, 1, &quad->const_buffer);
    } else {
      context->PSSetConstantBuffers (0, 0, nullptr);
    }

    context->IASetInputLayout (quad->layout);
    context->IASetVertexBuffers (0, 1, &quad->vertex_buffer,
        &quad->vertex_stride, &offsets);
    context->IASetIndexBuffer (quad->index_buffer, DXGI_FORMAT_R16_UINT, 0);

    context->VSSetShader (quad->vs, nullptr, 0);
    context->PSSetShader (quad->ps, nullptr, 0);

    context->DrawIndexed (quad->index_count, 0, 0);
  }

  context->OMSetRenderTargets (0, nullptr, nullptr);

  return TRUE;
}

static GstFlowReturn
gst_d3d11_test_src_create (GstBaseSrc * bsrc, guint64 offset,
    guint size, GstBuffer ** buf)
{
  GstD3D11TestSrc *self = GST_D3D11_TEST_SRC (bsrc);
  GstBuffer *buffer = nullptr;
  GstBuffer *render_buffer = nullptr;
  GstBuffer *convert_buffer = nullptr;
  GstFlowReturn ret = GST_FLOW_ERROR;
  GstClockTime pts;
  GstClockTime next_time;
  GstMapInfo render_info;
  ID3D11DeviceContext *context_handle =
      gst_d3d11_device_get_device_context_handle (self->device);
  GstMemory *mem;
  GstD3D11Memory *dmem;
  ID3D11RenderTargetView *pattern_rtv;
  gboolean convert_ret;

  ret = GST_BASE_SRC_CLASS (parent_class)->alloc (bsrc, offset, size, &buffer);
  if (ret != GST_FLOW_OK)
    return ret;

  ret =
      gst_buffer_pool_acquire_buffer (self->render_pool, &render_buffer,
      nullptr);
  if (ret != GST_FLOW_OK)
    goto error;

  if (self->downstream_supports_d3d11) {
    convert_buffer = buffer;
  } else {
    ret = gst_buffer_pool_acquire_buffer (self->convert_pool,
        &convert_buffer, nullptr);
    if (ret != GST_FLOW_OK)
      goto error;
  }

  mem = gst_buffer_peek_memory (render_buffer, 0);

  gst_d3d11_device_lock (self->device);
  if (!gst_memory_map (mem, &render_info,
          (GstMapFlags) (GST_MAP_WRITE | GST_MAP_D3D11))) {
    GST_ERROR_OBJECT (self, "Failed to map render buffer");
    gst_d3d11_device_unlock (self->device);
    goto error;
  }

  dmem = GST_D3D11_MEMORY_CAST (mem);
  pattern_rtv = gst_d3d11_memory_get_render_target_view (dmem, 0);
  if (!pattern_rtv) {
    GST_ERROR_OBJECT (self, "RTV is not available");
    gst_memory_unmap (mem, &render_info);
    gst_d3d11_device_unlock (self->device);
    goto error;
  }

  gst_d3d11_test_src_draw_pattern (self, context_handle, pattern_rtv);
  gst_memory_unmap (mem, &render_info);
  convert_ret = gst_d3d11_converter_convert_buffer_unlocked (self->converter,
      render_buffer, convert_buffer);
  gst_d3d11_device_unlock (self->device);

  if (!convert_ret) {
    GST_ERROR_OBJECT (self, "Failed to convert buffer");
    goto error;
  }

  if (self->downstream_supports_d3d11) {
    convert_buffer = nullptr;
  } else {
    gst_d3d11_buffer_copy_into (buffer, convert_buffer, &self->info);
    gst_clear_buffer (&convert_buffer);
  }

  gst_clear_buffer (&render_buffer);

  pts = self->accum_rtime + self->running_time;

  GST_BUFFER_PTS (buffer) = pts;
  GST_BUFFER_DTS (buffer) = GST_CLOCK_TIME_NONE;
  GST_BUFFER_OFFSET (buffer) = self->accum_frames + self->n_frames;
  if (self->reverse) {
    self->n_frames--;
  } else {
    self->n_frames++;
  }
  GST_BUFFER_OFFSET_END (buffer) = GST_BUFFER_OFFSET (buffer) + 1;

  next_time = gst_util_uint64_scale (self->n_frames,
      self->info.fps_d * GST_SECOND, self->info.fps_n);
  if (self->reverse) {
    /* We already decremented to next frame */
    GstClockTime prev_pts = gst_util_uint64_scale (self->n_frames + 2,
        self->info.fps_d * GST_SECOND, self->info.fps_n);

    GST_BUFFER_DURATION (buffer) = prev_pts - GST_BUFFER_PTS (buffer);
  } else {
    GST_BUFFER_DURATION (buffer) = next_time - self->running_time;
  }

  self->running_time = next_time;
  *buf = buffer;

  return GST_FLOW_OK;

error:
  gst_clear_buffer (&buffer);
  gst_clear_buffer (&render_buffer);
  if (!self->downstream_supports_d3d11)
    gst_clear_buffer (&convert_buffer);

  return ret;
}
