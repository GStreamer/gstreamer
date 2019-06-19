#version 450 core

#include "yuy2_uyvy_to_rgb.glsl"

layout(location = 0) in vec2 inTexCoord;

layout(set = 0, binding = 0) uniform sampler2D inTexture0;
layout(set = 0, binding = 1) uniform YUVCoefficients_ubo { YUVCoefficients coeff; };

layout(set = 0, binding = 2) uniform TexelOrdering
{
  vec2 tex_size;
  vec2 poffset;
  ivec4 in_reorder_idx;
  ivec4 out_reorder_idx;
} ordering;

layout(location = 0) out vec4 outColor0;

void main()
{
  float dx;
  if (mod (inTexCoord.x * ordering.tex_size.x, 2.0) < 1.0) {
    dx = -ordering.poffset.x;
  } else {
    dx = 0.0;
  }

  outColor0 = YUY2_UYVY_to_RGB (inTexture0, inTexCoord, dx, coeff, ordering.in_reorder_idx, ordering.out_reorder_idx);
}
