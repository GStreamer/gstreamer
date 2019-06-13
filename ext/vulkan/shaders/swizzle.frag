#version 450 core

#include "swizzle.glsl"

layout(location = 0) in vec2 inTexCoord;

layout(push_constant) uniform reorder {
  ivec4 in_reorder_idx;
  ivec4 out_reorder_idx;
};
layout(set = 0, binding = 0) uniform sampler2D inTexture0;

layout(location = 0) out vec4 outColor0;

void main()
{
  vec4 rgba = swizzle (texture(inTexture0, inTexCoord), in_reorder_idx);
  outColor0 = swizzle (rgba, out_reorder_idx);
}
