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
  float inorder = mod (inTexCoord.x * texSize.x, 2.0);
  float fx = inTexCoord.x;
  float dx = -1.0 / texSize.x;
  if (inorder > 1.0)
    dx = -dx;
  vec4 texel1 = swizzle (texture(inTexture0, inTexCoord), in_reorder_idx);
  vec4 texel2 = swizzle (texture(inTexture0, inTexCoord + vec2(dx, 0.0)), in_reorder_idx);
  vec3 yuv1 = color_convert_texel (texel1.rgb, matrices);
  vec3 yuv2 = color_convert_texel (texel2.rgb, matrices);
  vec3 yuv;
  yuv.x = yuv1.x;
  yuv.yz = (yuv1.yz + yuv2.yz) * 0.5;

  if (inorder < 1.0)
    outColor0 = vec4(yuv[out_reorder_idx[0]], yuv[out_reorder_idx[1]], 0.0, 1.0);
  else
    outColor0 = vec4(yuv[out_reorder_idx[2]], yuv[out_reorder_idx[3]], 0.0, 1.0);
}
