/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

/* non-GST-specific stuff */

#include "gltestsrc.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>

#ifndef M_PI
#define M_PI  3.14159265358979323846
#endif

enum
{
  COLOR_WHITE = 0,
  COLOR_YELLOW,
  COLOR_CYAN,
  COLOR_GREEN,
  COLOR_MAGENTA,
  COLOR_RED,
  COLOR_BLUE,
  COLOR_BLACK,
  COLOR_NEG_I,
  COLOR_POS_Q,
  COLOR_SUPER_BLACK,
  COLOR_DARK_GREY
};

static const struct vts_color_struct vts_colors[] = {
  /* 100% white */
  {255, 128, 128, 255, 255, 255, 255},
  /* yellow */
  {226, 0, 155, 255, 255, 0, 255},
  /* cyan */
  {179, 170, 0, 0, 255, 255, 255},
  /* green */
  {150, 46, 21, 0, 255, 0, 255},
  /* magenta */
  {105, 212, 235, 255, 0, 255, 255},
  /* red */
  {76, 85, 255, 255, 0, 0, 255},
  /* blue */
  {29, 255, 107, 0, 0, 255, 255},
  /* black */
  {16, 128, 128, 0, 0, 0, 255},
  /* -I */
  {16, 198, 21, 0, 0, 128, 255},
  /* +Q */
  {16, 235, 198, 0, 128, 255, 255},
  /* superblack */
  {0, 128, 128, 0, 0, 0, 255},
  /* 5% grey */
  {32, 128, 128, 32, 32, 32, 255},
};

static void
gst_gl_test_src_unicolor (GstGLTestSrc * v, GstBuffer * buffer, int w,
    int h, const struct vts_color_struct *color);

void
gst_gl_test_src_smpte (GstGLTestSrc * v, GstBuffer * buffer, int w, int h)
{
#if GST_GL_HAVE_OPENGL
  int i;

  if (gst_gl_context_get_gl_api (v->context) & GST_GL_API_OPENGL) {

    glClearColor (0.0, 0.0, 0.0, 1.0);
    glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glDisable (GL_CULL_FACE);
    glDisable (GL_TEXTURE_2D);

    glMatrixMode (GL_PROJECTION);
    glLoadIdentity ();

    for (i = 0; i < 7; i++) {
      glColor4f (vts_colors[i].R * (1 / 255.0f), vts_colors[i].G * (1 / 255.0f),
          vts_colors[i].B * (1 / 255.0f), 1.0f);
      glBegin (GL_QUADS);
      glVertex3f (-1.0f + i * (2.0f / 7.0f), -1.0f + 2.0 * (2.0f / 3.0f), 0);
      glVertex3f (-1.0f + (i + 1.0f) * (2.0f / 7.0f),
          -1.0f + 2.0f * (2.0f / 3.0f), 0);
      glVertex3f (-1.0f + (i + 1.0f) * (2.0f / 7.0f), -1.0f, 0);
      glVertex3f (-1.0f + i * (2.0f / 7.0f), -1.0f, 0);
      glEnd ();
    }

    for (i = 0; i < 7; i++) {
      int k;

      if (i & 1) {
        k = 7;
      } else {
        k = 6 - i;
      }

      glColor4f (vts_colors[k].R * (1 / 255.0f), vts_colors[k].G * (1 / 255.0f),
          vts_colors[k].B * (1 / 255.0f), 1.0f);
      glBegin (GL_QUADS);
      glVertex3f (-1.0f + i * (2.0f / 7.0f), -1.0f + 2.0f * (3.0f / 4.0f), 0);
      glVertex3f (-1.0f + (i + 1) * (2.0f / 7.0f), -1.0f + 2.0f * (3.0f / 4.0f),
          0);
      glVertex3f (-1.0f + (i + 1) * (2.0f / 7.0f), -1.0f + 2.0f * (2.0f / 3.0f),
          0);
      glVertex3f (-1.0f + i * (2.0f / 7.0f), -1.0f + 2.0f * (2.0f / 3.0f), 0);
      glEnd ();
    }

    for (i = 0; i < 3; i++) {
      int k;

      if (i == 0) {
        k = 8;
      } else if (i == 1) {
        k = 0;
      } else {
        k = 9;
      }

      glColor4f (vts_colors[k].R * (1 / 255.0f), vts_colors[k].G * (1 / 255.0f),
          vts_colors[k].B * (1 / 255.0f), 1.0f);
      glBegin (GL_QUADS);
      glVertex3f (-1.0f + i * (2.0f / 6.0f), -1.0f + 2.0f * 1, 0);
      glVertex3f (-1.0f + (i + 1) * (2.0f / 6.0f), -1.0f + 2.0f * 1, 0);
      glVertex3f (-1.0f + (i + 1) * (2.0f / 6.0f), -1.0f + 2.0f * (3.0f / 4.0f),
          0);
      glVertex3f (-1.0f + i * (2.0f / 6.0f), -1.0f + 2.0f * (3.0f / 4.0f), 0);
      glEnd ();
    }

    for (i = 0; i < 3; i++) {
      int k;

      if (i == 0) {
        k = COLOR_SUPER_BLACK;
      } else if (i == 1) {
        k = COLOR_BLACK;
      } else {
        k = COLOR_DARK_GREY;
      }

      glColor4f (vts_colors[k].R * (1 / 255.0f), vts_colors[k].G * (1 / 255.0f),
          vts_colors[k].B * (1 / 255.0f), 1.0f);
      glBegin (GL_QUADS);
      glVertex3f (-1.0f + 2.0f * (0.5f + i * (1.0f / 12.0f)), -1.0 + 2.0f * 1,
          0);
      glVertex3f (-1.0f + 2.0f * (0.5f + (i + 1) * (1.0f / 12.0f)),
          -1.0f + 2.0f * 1, 0);
      glVertex3f (-1.0f + 2.0f * (0.5f + (i + 1) * (1.0f / 12.0f)),
          -1.0f + 2.0f * (3.0f / 4.0f), 0);
      glVertex3f (-1.0f + 2.0f * (0.5f + i * (1.0f / 12.0f)),
          -1.0f + 2.0f * (3.0f / 4.0f), 0);
      glEnd ();
    }

    glColor4f (1.0, 1.0, 1.0, 1.0);
    glBegin (GL_QUADS);
    glVertex3f (-1.0 + 2.0 * (0.75), -1.0 + 2.0 * 1, 0);
    glVertex3f (-1.0 + 2.0 * (1.0), -1.0 + 2.0 * 1, 0);
    glVertex3f (-1.0 + 2.0 * (1.0), -1.0 + 2.0 * (3.0 / 4.0), 0);
    glVertex3f (-1.0 + 2.0 * (0.75), -1.0 + 2.0 * (3.0 / 4.0), 0);
    glEnd ();
  }
#endif
}

/* *INDENT-OFF* */

static const GLfloat positions[] = {
     -1.0,  1.0,  0.0, 1.0,
      1.0,  1.0,  0.0, 1.0,
      1.0, -1.0,  0.0, 1.0,
     -1.0, -1.0,  0.0, 1.0,
  };
static const GLfloat identitiy_matrix[] = {
      1.0,  0.0,  0.0, 0.0,
      0.0,  1.0,  0.0, 0.0,
      0.0,  0.0,  1.0, 0.0,
      0.0,  0.0,  0.0, 1.0,
  };

/* *INDENT-ON* */

void
gst_gl_test_src_shader (GstGLTestSrc * v, GstBuffer * buffer, int w, int h)
{

  GstGLFuncs *gl = v->context->gl_vtable;

/* *INDENT-OFF* */
  const GLfloat uvs[] = {
     0.0,  1.0,
     1.0,  1.0,
     1.0,  0.0,
     0.0,  0.0,
  };
/* *INDENT-ON* */

  GLushort indices[] = { 0, 1, 2, 3, 0 };

  GLint attr_position_loc = -1;
  GLint attr_uv_loc = -1;

  if (gst_gl_context_get_gl_api (v->context)) {

    gst_gl_context_clear_shader (v->context);
    gl->BindTexture (GL_TEXTURE_2D, 0);
    gl->Disable (GL_TEXTURE_2D);

    gst_gl_shader_use (v->shader);

    attr_position_loc =
        gst_gl_shader_get_attribute_location (v->shader, "position");

    attr_uv_loc = gst_gl_shader_get_attribute_location (v->shader, "uv");

    /* Load the vertex position */
    gl->VertexAttribPointer (attr_position_loc, 4, GL_FLOAT,
        GL_FALSE, 0, positions);
    /* Load the texture coordinate */
    gl->VertexAttribPointer (attr_uv_loc, 2, GL_FLOAT, GL_FALSE, 0, uvs);

    gl->EnableVertexAttribArray (attr_position_loc);
    gl->EnableVertexAttribArray (attr_uv_loc);

    gst_gl_shader_set_uniform_matrix_4fv (v->shader, "mvp",
        1, GL_FALSE, identitiy_matrix);

    gst_gl_shader_set_uniform_1f (v->shader, "time",
        (gfloat) v->running_time / GST_SECOND);

    gst_gl_shader_set_uniform_1f (v->shader, "aspect_ratio",
        (gfloat) w / (gfloat) h);

    gl->DrawElements (GL_TRIANGLE_STRIP, 5, GL_UNSIGNED_SHORT, indices);

    gl->DisableVertexAttribArray (attr_position_loc);
    gl->DisableVertexAttribArray (attr_uv_loc);

    gst_gl_context_clear_shader (v->context);
  }
}

static void
gst_gl_test_src_unicolor (GstGLTestSrc * v, GstBuffer * buffer, int w,
    int h, const struct vts_color_struct *color)
{
#if GST_GL_HAVE_OPENGL
  if (gst_gl_context_get_gl_api (v->context) & GST_GL_API_OPENGL) {
    glClearColor (color->R * (1 / 255.0f), color->G * (1 / 255.0f),
        color->B * (1 / 255.0f), 1.0f);
    glClear (GL_COLOR_BUFFER_BIT);
  }
#endif
}

void
gst_gl_test_src_black (GstGLTestSrc * v, GstBuffer * buffer, int w, int h)
{
  gst_gl_test_src_unicolor (v, buffer, w, h, vts_colors + COLOR_BLACK);
}

void
gst_gl_test_src_white (GstGLTestSrc * v, GstBuffer * buffer, int w, int h)
{
  gst_gl_test_src_unicolor (v, buffer, w, h, vts_colors + COLOR_WHITE);
}

void
gst_gl_test_src_red (GstGLTestSrc * v, GstBuffer * buffer, int w, int h)
{
  gst_gl_test_src_unicolor (v, buffer, w, h, vts_colors + COLOR_RED);
}

void
gst_gl_test_src_green (GstGLTestSrc * v, GstBuffer * buffer, int w, int h)
{
  gst_gl_test_src_unicolor (v, buffer, w, h, vts_colors + COLOR_GREEN);
}

void
gst_gl_test_src_blue (GstGLTestSrc * v, GstBuffer * buffer, int w, int h)
{
  gst_gl_test_src_unicolor (v, buffer, w, h, vts_colors + COLOR_BLUE);
}

static void
gst_gl_test_src_checkers (GstGLTestSrc * v, gint checker_width)
{

  GstGLFuncs *gl = v->context->gl_vtable;

  GLushort indices[] = { 0, 1, 2, 3, 0 };

  GLint attr_position_loc = -1;

  if (gst_gl_context_get_gl_api (v->context)) {

    gst_gl_context_clear_shader (v->context);
    gl->BindTexture (GL_TEXTURE_2D, 0);
    gl->Disable (GL_TEXTURE_2D);

    gst_gl_shader_use (v->shader);

    attr_position_loc =
        gst_gl_shader_get_attribute_location (v->shader, "position");

    /* Load the vertex position */
    gl->VertexAttribPointer (attr_position_loc, 4, GL_FLOAT,
        GL_FALSE, 0, positions);

    gl->EnableVertexAttribArray (attr_position_loc);

    gst_gl_shader_set_uniform_matrix_4fv (v->shader, "mvp",
        1, GL_FALSE, identitiy_matrix);

    gst_gl_shader_set_uniform_1f (v->shader, "checker_width", checker_width);

    gl->DrawElements (GL_TRIANGLE_STRIP, 5, GL_UNSIGNED_SHORT, indices);

    gl->DisableVertexAttribArray (attr_position_loc);

    gst_gl_context_clear_shader (v->context);
  }
}


void
gst_gl_test_src_checkers1 (GstGLTestSrc * v, GstBuffer * buffer, int w, int h)
{
  gst_gl_test_src_checkers (v, 1);
}


void
gst_gl_test_src_checkers2 (GstGLTestSrc * v, GstBuffer * buffer, int w, int h)
{
  gst_gl_test_src_checkers (v, 2);
}

void
gst_gl_test_src_checkers4 (GstGLTestSrc * v, GstBuffer * buffer, int w, int h)
{
  gst_gl_test_src_checkers (v, 4);

}

void
gst_gl_test_src_checkers8 (GstGLTestSrc * v, GstBuffer * buffer, int w, int h)
{
  gst_gl_test_src_checkers (v, 8);
}

void
gst_gl_test_src_circular (GstGLTestSrc * v, GstBuffer * buffer, int w, int h)
{
#if 0
  int i;
  int j;
  paintinfo pi = { NULL, };
  paintinfo *p = &pi;
  struct fourcc_list_struct *fourcc;
  struct vts_color_struct color;
  static uint8_t sine_array[256];
  static int sine_array_inited = FALSE;
  double freq[8];

#ifdef SCALE_AMPLITUDE
  double ampl[8];
#endif
  int d;

  if (!sine_array_inited) {
    for (i = 0; i < 256; i++) {
      sine_array[i] =
          floor (255 * (0.5 + 0.5 * sin (i * 2 * M_PI / 256)) + 0.5);
    }
    sine_array_inited = TRUE;
  }

  p->width = w;
  p->height = h;
  fourcc = v->fourcc;
  if (fourcc == NULL)
    return;

  fourcc->paint_setup (p, dest);
  p->paint_hline = fourcc->paint_hline;

  color = vts_colors[COLOR_BLACK];
  p->color = &color;

  for (i = 1; i < 8; i++) {
    freq[i] = 200 * pow (2.0, -(i - 1) / 4.0);
#ifdef SCALE_AMPLITUDE
    {
      double x;

      x = 2 * M_PI * freq[i] / w;
      ampl[i] = sin (x) / x;
    }
#endif
  }

  for (i = 0; i < w; i++) {
    for (j = 0; j < h; j++) {
      double dist;
      int seg;

      dist =
          sqrt ((2 * i - w) * (2 * i - w) + (2 * j - h) * (2 * j -
              h)) / (2 * w);
      seg = floor (dist * 16);
      if (seg == 0 || seg >= 8) {
        color.Y = 255;
      } else {
#ifdef SCALE_AMPLITUDE
        double a;
#endif
        d = floor (256 * dist * freq[seg] + 0.5);
#ifdef SCALE_AMPLITUDE
        a = ampl[seg];
        if (a < 0)
          a = 0;
        color.Y = 128 + a * (sine_array[d & 0xff] - 128);
#else
        color.Y = sine_array[d & 0xff];
#endif
      }
      color.R = color.Y;
      color.G = color.Y;
      color.B = color.Y;
      p->paint_hline (p, i, j, 1);
    }
  }
#endif
}
