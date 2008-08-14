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
