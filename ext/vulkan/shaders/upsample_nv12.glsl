#include "swizzle.glsl"

vec3 upsample_NV12(in sampler2D Ytex, in sampler2D UVtex, in vec2 texCoord, in ivec4 inReorderIdx)
{
  vec3 yuv;
  yuv.x = texture(Ytex, texCoord).x;
  yuv.yz = texture(UVtex, texCoord).xy;

  return swizzle(yuv, inReorderIdx.xyz);
}
