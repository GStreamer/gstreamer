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

/* A common file for sources is needed since shader sources can be
 * generic and reused by several effects */


/* Mirror effect */
const gchar *mirror_fragment_source =
  "#extension GL_ARB_texture_rectangle : enable\n"
  "uniform sampler2DRect tex;"
  "uniform float width, height;"
  "void main () {"
  "  vec2 tex_size = vec2 (width, height);"
  "  vec2 texturecoord = gl_TexCoord[0].xy;"
  "  vec2 normcoord;"
  "  normcoord = texturecoord / tex_size - 1.0;"
  "  normcoord.x *= sign (normcoord.x);"
  "  texturecoord = (normcoord + 1.0) * tex_size;"
  "  vec4 color = texture2DRect (tex, texturecoord); "
  "  gl_FragColor = color * gl_Color;"
  "}";


/* Squeeze effect */
const gchar *squeeze_fragment_source =
  "#extension GL_ARB_texture_rectangle : enable\n"
  "uniform sampler2DRect tex;"
  "uniform float width, height;"
  "void main () {"
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
  /* little trick with normalized coords to obtain a circle */
  "  normcoord = texturecoord / tex_size.x - tex_size / tex_size.x;"
  "  float r = length(normcoord);"
  "  float phi = atan(normcoord.y, normcoord.x);"
  "  r = clamp (r, 0.0, 0.5);" /* is there a way to do this without polars? */
  "  normcoord.x = r * cos(phi);"
  "  normcoord.y = r * sin(phi); "
  "  texturecoord = (normcoord + tex_size/tex_size.x) * tex_size.x;"
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
  "  float r =  length (normcoord);"
  "  float phi = atan (normcoord.y, normcoord.x);"
  "  phi += (1.0 - smoothstep (-0.6, 0.6, r)) * 4.8;" 
  "  normcoord.x = r * cos(phi);"
  "  normcoord.y = r * sin(phi);"
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
  "  float luma = dot(color.rgb, vec3(0.2125, 0.7154, 0.0721));" /* BT.709 (from orange book) */
  "  gl_FragColor = vec4 (vec3 (smoothstep (0.30, 0.50, luma)), color.a);"
  "}";


/* horizontal convolution */
const gchar *hconv9_fragment_source =
"#extension GL_ARB_texture_rectangle : enable\n"
"uniform sampler2DRect tex;"
"uniform float norm_const;"
"uniform float norm_offset;"
"uniform float kernel[9];"
"void main () {"
/* "float offset[9] = float[9] (-4.0, -3.0, -2.0, -1.0, 0.0, 1.0, 2.0, 3.0, 4.0);" */
/* don't use array constructor so we don't have to depend on #version 120 */
"  float offset[9];" 
"  offset[0] = -4.0;"
"  offset[1] = -3.0;"
"  offset[2] = -2.0;"
"  offset[3] = -1.0;"
"  offset[4] =  0.0;"
"  offset[5] =  1.0;"
"  offset[6] =  2.0;"
"  offset[7] =  3.0;"
"  offset[8] =  4.0;"
"  vec2 texturecoord = gl_TexCoord[0].st;"
"  int i;"
"  vec4 sum = vec4 (0.0);"
"  for (i = 0; i < 9; i++) { "
"    if (kernel[i] != 0.0) {"
"        vec4 neighbor = texture2DRect(tex, vec2(texturecoord.s+offset[i], texturecoord.t)); "
"        sum += neighbor * kernel[i]/norm_const; "
"      }"
"  }"
"  gl_FragColor = sum + norm_offset;"
"}";


/* vertical convolution */
const gchar *vconv9_fragment_source =
"#extension GL_ARB_texture_rectangle : enable\n"
"uniform sampler2DRect tex;"
"uniform float norm_const;"
"uniform float norm_offset;"
"uniform float kernel[9];"
"void main () {"
/* "float offset[9] = float[9] (-4.0, -3.0, -2.0, -1.0, 0.0, 1.0, 2.0, 3.0, 4.0);" */
/* don't use array constructor so we don't have to depend on #version 120 */
"  float offset[9];"
"  offset[0] = -4.0;"
"  offset[1] = -3.0;"
"  offset[2] = -2.0;"
"  offset[3] = -1.0;"
"  offset[4] =  0.0;"
"  offset[5] =  1.0;"
"  offset[6] =  2.0;"
"  offset[7] =  3.0;"
"  offset[8] =  4.0;"
"  vec2 texturecoord = gl_TexCoord[0].st;"
"  int i;"
"  vec4 sum = vec4 (0.0);"
"  for (i = 0; i < 9; i++) { "
"    if (kernel[i] != 0.0) {"
"        vec4 neighbor = texture2DRect(tex, vec2(texturecoord.s, texturecoord.t+offset[i])); "
"        sum += neighbor * kernel[i]/norm_const; "
"      }"
"  }"
"  gl_FragColor = sum + norm_offset;"
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


/* lut operations, map luma to tex1d, see orange book (chapter 19) */
const gchar *luma_to_curve_fragment_source =
"#extension GL_ARB_texture_rectangle : enable\n"
"uniform sampler2DRect tex;"
"uniform sampler1D curve;"
"void main () {"
"  vec2 texturecoord = gl_TexCoord[0].st;"
"  vec4 color = texture2DRect (tex, texturecoord);"
"  float luma = dot(color.rgb, vec3(0.2125, 0.7154, 0.0721));"
"  vec4 outcolor;"
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
"vec3 rgb2hsl (vec3 v) "
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
  "void main () {"
  "vec4 basecolor = texture2DRect (base, gl_TexCoord[0].st);"
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
