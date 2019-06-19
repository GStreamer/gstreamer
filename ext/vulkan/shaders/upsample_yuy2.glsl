vec3 upsample_YUY2(in sampler2D tex, in vec2 texCoord, in vec2 texSize, in ivec4 inReorderIdx)
{
  vec2 dx = vec2(-1.0 / texSize.x, 0.0);
  if (mod (texCoord.x * texSize.x, 2.0) < 1.0) {
    dx[1] = -dx[0];
    dx[0] = 0.0;
  }

  vec3 yuv;
  yuv.x = texture(tex, texCoord)[inReorderIdx[0]];
  /* FIXME: should get cosited sampling right... */
  vec4 texel;
  texel.xy = texture(tex, texCoord + vec2(dx[0], 0.0)).rg;
  texel.zw = texture(tex, texCoord + vec2(dx[1], 0.0)).rg;
  yuv.y = texel[inReorderIdx[1]];
  yuv.z = texel[inReorderIdx[3]];

  return yuv;
}
