/*
 * GStreamer
 * Copyright (C) 2008 Filippo Argiolas <filippo.argiolas@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <gstgleffects.h>
#include <gstgleffectssources.h>
#include <math.h>

/* A common file for sources is needed since shader sources can be
 * generic and reused by several effects */

/* FIXME */
/* Move sooner or later into single .frag .vert files and either bake
 * them into a c file at compile time or load them at run time */


/* fill a normalized and zero centered gaussian vector for separable
 * gaussian convolution */

void
fill_gaussian_kernel (float *kernel, int size, float sigma)
{
  int i;
  float sum;
  int l;

  /* need an odd sized vector to center it at zero */
  g_return_if_fail ((size % 2) != 0);

  sum = 0.0;
  l = (size - 1) / 2.0;

  for (i = 0; i < size; i++) {
    kernel[i] = exp (-pow ((i - l), 2.0) / (2 * sigma));
    sum += kernel[i];
  }

  for (i = 0; i < size; i++) {
    kernel[i] /= sum;
  }
}

/* *INDENT-OFF* */

/* Vertex shader */
const gchar *vertex_shader_source =
  "attribute vec4 a_position;"
  "attribute vec2 a_texCoord;"
  "varying vec2 v_texCoord;"
  "void main()"
  "{"
  "   gl_Position = a_position;"
  "   v_texCoord = a_texCoord;"
  "}";

/* Identity effect */
const gchar *identity_fragment_source =
  "precision mediump float;"
  "varying vec2 v_texCoord;"
  "uniform sampler2D tex;"
  "void main()"
  "{"
  "  gl_FragColor = texture2D(tex, v_texCoord);"
  "}";

/* Mirror effect */
const gchar *mirror_fragment_source =
#ifndef OPENGL_ES2
  "#extension GL_ARB_texture_rectangle : enable\n"
  "uniform sampler2DRect tex;"
  "uniform float width, height;"
#else
  "precision mediump float;"
  "varying vec2 v_texCoord;"
  "uniform sampler2D tex;"
#endif
  "void main () {"
#ifndef OPENGL_ES2
  "  vec2 tex_size = vec2 (width, height);"
  "  vec2 texturecoord = gl_TexCoord[0].xy;"
  "  vec2 normcoord;"
  "  normcoord = texturecoord / tex_size - 1.0;"
  "  normcoord.x *= sign (normcoord.x);"
  "  texturecoord = (normcoord + 1.0) * tex_size;"
  "  vec4 color = texture2DRect (tex, texturecoord);"
  "  gl_FragColor = color * gl_Color;"
#else
  "  vec2 texturecoord = v_texCoord.xy;"
  "  float normcoord = texturecoord.x - 0.5;"
  "  normcoord *= sign (normcoord);"
  "  texturecoord.x = (normcoord + 0.5);"
  "  gl_FragColor = texture2D (tex, texturecoord);"
#endif
  "}";


/* Squeeze effect */
const gchar *squeeze_fragment_source =
#ifndef OPENGL_ES2
  "#extension GL_ARB_texture_rectangle : enable\n"
  "uniform sampler2DRect tex;"
  "uniform float width, height;"
#else
  "precision mediump float;"
  "varying vec2 v_texCoord;"
  "uniform sampler2D tex;"
#endif
  "void main () {"
#ifndef OPENGL_ES2
  "  vec2 tex_size = vec2 (width, height);"
  "  vec2 texturecoord = gl_TexCoord[0].xy;"
  "  vec2 normcoord;"
  "  normcoord = texturecoord / tex_size - 1.0; "
  "  float r = length (normcoord);"
  "  r = pow(r, 0.40)*1.3;"
  "  normcoord = normcoord / r;"
  "  texturecoord = (normcoord + 1.0) * tex_size;"
  "  vec4 color = texture2DRect (tex, texturecoord); "
  "  gl_FragColor = color * gl_Color;"
#else
  "  vec2 texturecoord = v_texCoord.xy;"
  "  vec2 normcoord = texturecoord - 0.5;"
  "  float r = length (normcoord);"
  "  r = pow(r, 0.40)*1.3;"
  "  normcoord = normcoord / r;"
  "  texturecoord = (normcoord + 0.5);"
  "  gl_FragColor = texture2D (tex, texturecoord);"
#endif
  "}";


/* Stretch Effect */
const gchar *stretch_fragment_source =
  "#extension GL_ARB_texture_rectangle : enable\n"
  "uniform sampler2DRect tex;"
  "uniform float width, height;"
  "void main () {"
  "  vec2 tex_size = vec2 (width, height);"
  "  vec2 texturecoord = gl_TexCoord[0].xy;"
  "  vec2 normcoord;"
  "  normcoord = texturecoord / tex_size - 1.0;"
  "  float r = length (normcoord);"
  "  normcoord *= 2.0 - smoothstep(0.0, 0.7, r);"
  "  texturecoord = (normcoord + 1.0) * tex_size;"
  "  vec4 color = texture2DRect (tex, texturecoord);"
  "  gl_FragColor = color * gl_Color;"
  "}";

/* Light Tunnel effect */
const gchar *tunnel_fragment_source =
  "#extension GL_ARB_texture_rectangle : enable\n"
  "uniform sampler2DRect tex;"
  "uniform float width, height;"
  "void main () {"
  "  vec2 tex_size = vec2 (width, height);"
  "  vec2 texturecoord = gl_TexCoord[0].xy;"
  "  vec2 normcoord;"
  /* little trick with normalized coords to obtain a circle with
   * rect textures */
  "  normcoord = (texturecoord - tex_size) / tex_size.x;"
  "  float r = length(normcoord);"
  "  normcoord *= clamp (r, 0.0, 0.5) / r;"
  "  texturecoord = (normcoord * tex_size.x) + tex_size;"
  "  vec4 color = texture2DRect (tex, texturecoord); "
  "  gl_FragColor = color;"
  "}";

/* FishEye effect */
const gchar *fisheye_fragment_source =
  "#extension GL_ARB_texture_rectangle : enable\n"
  "uniform sampler2DRect tex;"
  "uniform float width, height;"
  "void main () {"
  "  vec2 tex_size = vec2 (width, height);"
  "  vec2 texturecoord = gl_TexCoord[0].xy;"
  "  vec2 normcoord;"
  "  normcoord = texturecoord / tex_size - 1.0;"
  "  float r =  length (normcoord);"
  "  normcoord *= r/sqrt(2.0);"
  "  texturecoord = (normcoord + 1.0) * tex_size;"
  "  vec4 color = texture2DRect (tex, texturecoord);"
  "  gl_FragColor = color;"
  "}";


/* Twirl effect */
const gchar *twirl_fragment_source =
  "#extension GL_ARB_texture_rectangle : enable\n"
  "uniform sampler2DRect tex;"
  "uniform float width, height;"
  "void main () {"
  "  vec2 tex_size = vec2 (width, height);"
  "  vec2 texturecoord = gl_TexCoord[0].xy;"
  "  vec2 normcoord;"
  "  normcoord = texturecoord / tex_size - 1.0;"
  "  float r = length (normcoord);"
  /* calculate rotation angle: maximum (about pi/2) at the origin and
   * gradually decrease it up to 0.6 of each quadrant */
  "  float phi = (1.0 - smoothstep (0.0, 0.6, r)) * 1.6;"
  /* precalculate sin phi and cos phi, save some alu */
  "  float s = sin(phi);"
  "  float c = cos(phi);"
  /* rotate */
  "  normcoord *= mat2(c, s, -s, c);"
  "  texturecoord = (normcoord + 1.0) * tex_size;"
  "  vec4 color = texture2DRect (tex, texturecoord); "
  "  gl_FragColor = color;"
  "}";


/* Bulge effect */
const gchar *bulge_fragment_source =
  "#extension GL_ARB_texture_rectangle : enable\n"
  "uniform sampler2DRect tex;"
  "uniform float width, height;"
  "void main () {"
  "  vec2 tex_size = vec2 (width, height);"
  "  vec2 texturecoord = gl_TexCoord[0].xy;"
  "  vec2 normcoord;"
  "  normcoord = texturecoord / tex_size - 1.0;"
  "  float r =  length (normcoord);"
  "  normcoord *= smoothstep (-0.1, 0.5, r);"
  "  texturecoord = (normcoord + 1.0) * tex_size;"
  "  vec4 color = texture2DRect (tex, texturecoord);"
  "  gl_FragColor = color;"
  "}";


/* Square Effect */
const gchar *square_fragment_source =
  "#extension GL_ARB_texture_rectangle : enable\n"
  "uniform sampler2DRect tex;"
  "uniform float width;"
  "uniform float height;"
  "void main () {"
  "  vec2 tex_size = vec2 (width, height);"
  "  vec2 texturecoord = gl_TexCoord[0].xy;"
  "  vec2 normcoord;"
  "  normcoord = texturecoord / tex_size - 1.0;"
  "  float r = length (normcoord);"
  "  normcoord *= 1.0 + smoothstep(0.25, 0.5, abs(normcoord));"
  "  normcoord /= 2.0; /* zoom amount */"
  "  texturecoord = (normcoord + 1.0) * tex_size;"
  "  vec4 color = texture2DRect (tex, texturecoord);"
  "  gl_FragColor = color * gl_Color;"
  "}";


const gchar *luma_threshold_fragment_source =
  "#extension GL_ARB_texture_rectangle : enable\n"
  "uniform sampler2DRect tex;"
  "void main () {"
  "  vec2 texturecoord = gl_TexCoord[0].st;"
  "  int i;"
  "  vec4 color = texture2DRect(tex, texturecoord);"
  "  float luma = dot(color.rgb, vec3(0.2125, 0.7154, 0.0721));"    /* BT.709 (from orange book) */
  "  gl_FragColor = vec4 (vec3 (smoothstep (0.30, 0.50, luma)), color.a);"
  "}";

const gchar *sobel_fragment_source =
  "#extension GL_ARB_texture_rectangle : enable\n"
  "uniform sampler2DRect tex;"
  "uniform float hkern[9];"
  "uniform float vkern[9];"
  "uniform bool invert;"
  "void main () {"
  "  vec2 offset[9] = vec2[9] ( vec2(-1.0,-1.0), vec2( 0.0,-1.0), vec2( 1.0,-1.0),"
  "                             vec2(-1.0, 0.0), vec2( 0.0, 0.0), vec2( 1.0, 0.0),"
  "                             vec2(-1.0, 1.0), vec2( 0.0, 1.0), vec2( 1.0, 1.0) );"
  "  vec2 texturecoord = gl_TexCoord[0].st;"
  "  int i;"
  "  float luma;"
  "  float gx = 0.0;"
  "  float gy = 0.0 ;"
  "  for (i = 0; i < 9; i++) { "
  "    if(hkern[i] != 0.0 || vkern[i] != 0.0) {"
  "      vec4 neighbor = texture2DRect(tex, texturecoord + vec2(offset[i])); "
  "      luma = dot(neighbor, vec4(0.2125, 0.7154, 0.0721, neighbor.a));"
  "      gx += luma * hkern[i]; "
  "      gy += luma * vkern[i]; "
  "    }"
  "  }"
  "  float g = sqrt(gx*gx + gy*gy);"
  "  if (invert) g = 1.0 - g;"
  "  gl_FragColor = vec4(vec3(g), 1.0);"
  "}";


/* horizontal convolution */
const gchar *hconv9_fragment_source =
  "#extension GL_ARB_texture_rectangle : enable\n"
  "uniform sampler2DRect tex;"
  "uniform float kernel[9];"
  "void main () {"
  "  vec2 texturecoord = gl_TexCoord[0].st;"
  "  texturecoord.s -= 4.0;"
  "  int i;"
  "  vec4 sum = vec4 (0.0);"
  "  for (i = 0; i < 9; i++) { "
  "    vec4 neighbor = texture2DRect(tex, texturecoord); "
  "    ++texturecoord.s;"
  "    sum += neighbor * kernel[i];"
  "  }"
  "  gl_FragColor = sum;"
  "}";

/* vertical convolution */
const gchar *vconv9_fragment_source =
  "#extension GL_ARB_texture_rectangle : enable\n"
  "uniform sampler2DRect tex;"
  "uniform float kernel[9];"
  "void main () {"
  "  vec2 texturecoord = gl_TexCoord[0].st;"
  "  texturecoord.t -= 4.0;"
  "  int i;"
  "  vec4 sum = vec4 (0.0);"
  "  for (i = 0; i < 9; i++) { "
  "    vec4 neighbor = texture2DRect(tex, texturecoord); "
  "    ++texturecoord.t;"
  "    sum += neighbor * kernel[i]; "
  "  }"
  "  gl_FragColor = sum;"
  "}";


/* TODO: support several blend modes */
const gchar *sum_fragment_source =
  "#extension GL_ARB_texture_rectangle : enable\n"
  "uniform sampler2DRect base;"
  "uniform sampler2DRect blend;"
  "uniform float alpha;"
  "uniform float beta;"
  "void main () {"
  "  vec4 basecolor = texture2DRect (base, gl_TexCoord[0].st);"
  "  vec4 blendcolor = texture2DRect (blend, gl_TexCoord[0].st);"
  "  gl_FragColor = alpha * basecolor + beta * blendcolor;"
  "}";

const gchar *multiply_fragment_source =
  "#extension GL_ARB_texture_rectangle : enable\n"
  "uniform sampler2DRect base;"
  "uniform sampler2DRect blend;"
  "uniform float alpha;"
  "void main () {"
  "  vec4 basecolor = texture2DRect (base, gl_TexCoord[0].st);"
  "  vec4 blendcolor = texture2DRect (blend, gl_TexCoord[0].st);"
  "  gl_FragColor = (1 - alpha) * basecolor + alpha * basecolor * blendcolor;"
  "}";

/* lut operations, map luma to tex1d, see orange book (chapter 19) */
const gchar *luma_to_curve_fragment_source =
  "#extension GL_ARB_texture_rectangle : enable\n"
  "uniform sampler2DRect tex;"
  "uniform sampler1D curve;"
  "void main () {"
  "  vec2 texturecoord = gl_TexCoord[0].st;"
  "  vec4 color = texture2DRect (tex, texturecoord);"
  "  float luma = dot(color.rgb, vec3(0.2125, 0.7154, 0.0721));"
  "  color = texture1D(curve, luma);"
  "  gl_FragColor = color;"
  "}";


/* lut operations, map rgb to tex1d, see orange book (chapter 19) */
const gchar *rgb_to_curve_fragment_source =
  "#extension GL_ARB_texture_rectangle : enable\n"
  "uniform sampler2DRect tex;"
  "uniform sampler1D curve;"
  "void main () {"
  "  vec2 texturecoord = gl_TexCoord[0].st;"
  "  vec4 color = texture2DRect (tex, texturecoord);"
  "  vec4 outcolor;"
  "  outcolor.r = texture1D(curve, color.r).r;"
  "  outcolor.g = texture1D(curve, color.g).g;"
  "  outcolor.b = texture1D(curve, color.b).b;"
  "  outcolor.a = color.a;"
  "  gl_FragColor = outcolor;"
  "}";

const gchar *sin_fragment_source =
  "#extension GL_ARB_texture_rectangle : enable\n"
  "uniform sampler2DRect tex;"
  "vec3 rgb2hsl (vec3 v)"
  "{"
/* TODO: check this algorythm */
  "  float MIN, MAX;"
  "  float r, g, b;"
  "  float h, l, s;"
  "  float delta;"
  "  h = 0.0; l = 0.0; s = 0.0;"
  "  r = v.r; g = v.g; b = v.b;"
  "  MIN = min (r, min (g, b));"
  "  MAX = max (r, max (g, b));"
  "  delta = MAX - MIN;"
  "  l = (MAX + MIN) / 2.0;"
  "  if ((MAX - MIN) < 0.0001) { h = 0.0; s = 0.0; }"
  "  else {"
  "    if (l <= 0.5) s = (MAX - MIN) / (MAX + MIN);"
  "    else s = (MAX - MIN) / (2.0 - MAX - MIN);"
  "    if (r == MAX) h = (g - b) / delta;"
  "    else if (g == MAX) h = 2.0 + (b - r) / delta;"
  "    else h = 4.0 + (r - g) / delta;"
  "    h *= 60.0;"
  "    if (h < 0.0) h += 360.0;"
  "  }"
  "  return vec3 (h, l, s);"
  "}"
  "void main () {"
  "  vec3 HSL, RGB;"
  "  vec4 color = texture2DRect (tex, vec2(gl_TexCoord[0].st));"
  "  float luma = dot(color.rgb, vec3(0.2125, 0.7154, 0.0721));"
  "  HSL = rgb2hsl (color.rgb);"
/* move hls discontinuity away from the desired red zone so we can use
 * smoothstep.. to try: convert degrees in radiants, divide by 2 and
 * smoothstep cosine */
  "  HSL.x += 180.0;"
  "  if ((HSL.x) > 360.0) HSL.x -= 360.0;"
/* damn, it is extremely hard to get rid of human face reds! */
/* picked hue is slightly shifted towards violet to prevent this but
 * still fails.. maybe hsl is not well suited for this */
  "  float a = smoothstep (110.0, 150.0, HSL.x);"
  "  float b = smoothstep (170.0, 210.0, HSL.x);"
  "  float alpha = a - b;"
  "  gl_FragColor = color * alpha + luma * (1.0 - alpha);"
  "}";

const gchar *interpolate_fragment_source =
  "#extension GL_ARB_texture_rectangle : enable\n"
  "uniform sampler2DRect base;"
  "uniform sampler2DRect blend;"
  "void main () {"
  "vec4 basecolor = texture2DRect (base, gl_TexCoord[0].st);"
  "vec4 blendcolor = texture2DRect (blend, gl_TexCoord[0].st);"
  "vec4 white = vec4(1.0);"
  "gl_FragColor = blendcolor + (1.0 - blendcolor.a) * basecolor;"
  "}";

const gchar *texture_interp_fragment_source =
  "#extension GL_ARB_texture_rectangle : enable\n"
  "uniform sampler2DRect base;"
  "uniform sampler2DRect blend;"
  "uniform sampler2DRect alpha;"
  "uniform float final_width, final_height;"
  "uniform float base_width, base_height;"
/*
  "uniform float blend_width, blend_height;"
  "uniform float alpha_width, alpha_height;"
*/
  "void main () {"
  "vec2 base_scale = vec2 (base_width, base_height) / vec2 (final_width, final_height);"
/*
  "vec2 blend_scale = vec2 (blend_width, blend_height) / vec2 (final_width, final_height);"
  "vec2 alpha_scale = vec2 (alpha_width, alpha_height) / vec2 (final_width, final_height);"
*/

  "vec4 basecolor = texture2DRect (base, gl_TexCoord[0].st * base_scale);"
  "vec4 blendcolor = texture2DRect (blend, gl_TexCoord[0].st);"
  "vec4 alphacolor = texture2DRect (alpha, gl_TexCoord[0].st);"
//  "gl_FragColor = alphacolor;"
  "gl_FragColor = (alphacolor * blendcolor) + (1.0 - alphacolor) * basecolor;"
  "}";

const gchar *difference_fragment_source =
  "#extension GL_ARB_texture_rectangle : enable\n"
  "uniform sampler2DRect saved;"
  "uniform sampler2DRect current;"
  "void main () {"
  "vec4 savedcolor = texture2DRect (saved, gl_TexCoord[0].st);"
  "vec4 currentcolor = texture2DRect (current, gl_TexCoord[0].st);"
  "gl_FragColor = vec4 (step (0.12, length (savedcolor - currentcolor)));"
  "}";

/* *INDENT-ON* */
