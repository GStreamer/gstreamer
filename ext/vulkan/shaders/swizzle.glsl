vec4 swizzle(in vec4 texel, in ivec4 swizzle_idx)
{
  return vec4(texel[swizzle_idx[0]], texel[swizzle_idx[1]], texel[swizzle_idx[2]], texel[swizzle_idx[3]]);
}
