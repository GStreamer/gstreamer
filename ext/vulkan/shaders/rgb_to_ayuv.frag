#version 450 core

#include "color_convert_generic.glsl"
#include "swizzle.glsl"

layout(location = 0) in vec2 inTexCoord;

layout(set = 0, binding = 0) uniform sampler2D inTexture0;
layout(set = 0, binding = 1) uniform reorder {
  ivec4 in_reorder_idx;
  ivec4 out_reorder_idx;
  ivec2 texSize;
  ColorMatrices matrices;
};

layout(location = 0) out vec4 outColor0;

void main()
{
  vec4 rgba = swizzle (texture (inTexture0, inTexCoord), in_reorder_idx);
  vec4 yuva = vec4(1.0);
  yuva.w = rgba.a;
  yuva.xyz = color_convert_texel (rgba.rgb, matrices);
  outColor0 = swizzle(yuva, out_reorder_idx);
}
