#include "swizzle.glsl"

vec4 upsample_AYUV(in sampler2D tex, in vec2 texCoord, in ivec4 inReorderIdx)
{
  vec4 yuva = texture(tex, texCoord);

  return swizzle(yuva, inReorderIdx);
}
