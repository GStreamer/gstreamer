#version 450 core

#include "color_convert_generic.glsl"
#include "upsample_nv12.glsl"
#include "swizzle.glsl"

layout(location = 0) in vec2 inTexCoord;

layout(set = 0, binding = 0) uniform sampler2D inTexture0;
layout(set = 0, binding = 1) uniform sampler2D inTexture1;
layout(set = 0, binding = 2) uniform reorder {
  ivec4 in_reorder_idx;
  ivec4 out_reorder_idx;
  ivec2 texSize;
  ColorMatrices matrices;
};

layout(location = 0) out vec4 outColor0;

void main()
{
  vec3 yuv = upsample_NV12 (inTexture0, inTexture1, inTexCoord, in_reorder_idx);
  vec4 rgba = vec4(1.0);
  rgba.rgb = color_convert_texel (yuv, matrices);
  outColor0 = swizzle(rgba, out_reorder_idx);
}
