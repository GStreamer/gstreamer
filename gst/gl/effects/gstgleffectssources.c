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
"  float offset[9] = float[9] (-4.0, -3.0, -2.0, -1.0, 0.0, 1.0, 2.0, 3.0, 4.0);"
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
"  float offset[9] = float[9] (-4.0, -3.0, -2.0, -1.0, 0.0, 1.0, 2.0, 3.0, 4.0);"
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
