#ifndef _SWIZZLE_H_
#define _SWIZZLE_H_

vec4 swizzle(in vec4 texel, in ivec4 swizzle_idx)
{
  return vec4(texel[swizzle_idx[0]], texel[swizzle_idx[1]], texel[swizzle_idx[2]], texel[swizzle_idx[3]]);
}

vec3 swizzle(in vec3 texel, in ivec3 swizzle_idx)
{
  return vec3(texel[swizzle_idx[0]], texel[swizzle_idx[1]], texel[swizzle_idx[2]]);
}
#endif
