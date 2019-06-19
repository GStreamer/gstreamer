struct ColorMatrices
{
  mat4 to_RGB_matrix;
  mat4 primaries_matrix;
  mat4 to_YUV_matrix;
};

vec3 color_matrix_convert (in vec3 texel, in mat4 color_matrix)
{
  vec4 rgb_ = color_matrix * vec4(texel, 1.0);
  return rgb_.rgb;
}

vec3 color_convert_texel (in vec3 texel, in ColorMatrices m)
{
  /* FIXME: need to add gamma remapping between these stages */
  vec3 tmp = color_matrix_convert (texel, m.to_RGB_matrix);
  tmp = color_matrix_convert (tmp, m.primaries_matrix);
  return color_matrix_convert (tmp, m.to_YUV_matrix);
}
