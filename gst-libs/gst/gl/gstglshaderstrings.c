/*
 * GStreamer
 * Copyright (C) 2015 Matthew Waters <matthew@centricular.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstglshaderstrings.h"

/* *INDENT-OFF* */
const gchar *gst_gl_shader_string_vertex_default =
    "attribute vec4 a_position;\n"
    "attribute vec2 a_texcoord;\n"
    "varying vec2 v_texcoord;\n"
    "void main()\n"
    "{\n"
    "   gl_Position = a_position;\n"
    "   v_texcoord = a_texcoord;\n"
    "}\n";

const gchar *gst_gl_shader_string_vertex_mat4_texture_transform =
    "uniform mat4 u_transformation;\n"
    "attribute vec4 a_position;\n"
    "attribute vec2 a_texcoord;\n"
    "varying vec2 v_texcoord;\n"
    "void main()\n"
    "{\n"
    "   gl_Position = a_position;\n"
    "   v_texcoord = (u_transformation * vec4(a_texcoord, 0, 1)).xy;\n"
    "}\n";

const gchar *gst_gl_shader_string_vertex_mat4_vertex_transform =
    "uniform mat4 u_transformation;\n"
    "attribute vec4 a_position;\n"
    "attribute vec2 a_texcoord;\n"
    "varying vec2 v_texcoord;\n"
    "void main()\n"
    "{\n"
    "   gl_Position = u_transformation * a_position;\n"
    "   v_texcoord = a_texcoord;\n"
    "}\n";

const gchar *gst_gl_shader_string_fragment_default =
    "#ifdef GL_ES\n"
    "precision mediump float;\n"
    "#endif\n"
    "varying vec2 v_texcoord;\n"
    "uniform sampler2D tex;\n"
    "void main()\n"
    "{\n"
    "  gl_FragColor = texture2D(tex, v_texcoord);\n"
    "}";

const gchar *gst_gl_shader_string_fragment_external_oes_default =
    "#extension GL_OES_EGL_image_external : require\n"
    "#ifdef GL_ES\n"
    "precision mediump float;\n"
    "#endif\n"
    "varying vec2 v_texcoord;\n"
    "uniform samplerExternalOES tex;\n"
    "void main()\n"
    "{\n"
    "  gl_FragColor = texture2D(tex, v_texcoord);\n"
    "}";
/* *INDENT-ON* */
