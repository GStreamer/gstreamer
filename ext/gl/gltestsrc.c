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

void
gst_gl_test_src_snow (GstGLTestSrc * v, GstBuffer * buffer, int w, int h)
{
#if GST_GL_HAVE_OPENGL
  if (gst_gl_context_get_gl_api (v->context) & GST_GL_API_OPENGL) {
    glClearColor (0.0, 0.0, 0.0, 1.0);
    glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glMatrixMode (GL_PROJECTION);
    glLoadIdentity ();

    glMatrixMode (GL_MODELVIEW);
    glLoadIdentity ();

    /* FIXME snow requires a fragment shader.  Please write. */
    glColor4f (0.5, 0.5, 0.5, 1.0);
    glBegin (GL_QUADS);
    glVertex3f (-1.0 + 2.0 * (0.0), -1.0 + 2.0 * 1, 0);
    glVertex3f (-1.0 + 2.0 * (1.0), -1.0 + 2.0 * 1, 0);
    glVertex3f (-1.0 + 2.0 * (1.0), -1.0 + 2.0 * (0.0), 0);
    glVertex3f (-1.0 + 2.0 * (0.0), -1.0 + 2.0 * (0.0), 0);
    glEnd ();
  }
#endif
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

void
gst_gl_test_src_checkers1 (GstGLTestSrc * v, GstBuffer * buffer, int w, int h)
{
#if 0
  int x, y;
  paintinfo pi = { NULL, };
  paintinfo *p = &pi;
  struct fourcc_list_struct *fourcc;

  p->width = w;
  p->height = h;
  fourcc = v->fourcc;
  if (fourcc == NULL)
    return;

  fourcc->paint_setup (p, dest);
  p->paint_hline = fourcc->paint_hline;

  for (y = 0; y < h; y++) {
    p->color = vts_colors + COLOR_GREEN;
    p->paint_hline (p, 0, y, w);
    for (x = (y % 2); x < w; x += 2) {
      p->color = vts_colors + COLOR_RED;
      p->paint_hline (p, x, y, 1);
    }
  }
#endif
}

void
gst_gl_test_src_checkers2 (GstGLTestSrc * v, GstBuffer * buffer, int w, int h)
{
#if 0
  int x, y;
  paintinfo pi = { NULL, };
  paintinfo *p = &pi;
  struct fourcc_list_struct *fourcc;

  p->width = w;
  p->height = h;
  fourcc = v->fourcc;
  if (fourcc == NULL)
    return;

  fourcc->paint_setup (p, dest);
  p->paint_hline = fourcc->paint_hline;

  p->color = vts_colors + COLOR_GREEN;
  for (y = 0; y < h; y++) {
    p->paint_hline (p, 0, y, w);
  }

  for (y = 0; y < h; y += 2) {
    for (x = ((y % 4) == 0) ? 0 : 2; x < w; x += 4) {
      guint len = (x < (w - 1)) ? 2 : (w - x);

      p->color = vts_colors + COLOR_RED;
      p->paint_hline (p, x, y + 0, len);
      if (G_LIKELY ((y + 1) < h)) {
        p->paint_hline (p, x, y + 1, len);
      }
    }
  }
#endif
}

void
gst_gl_test_src_checkers4 (GstGLTestSrc * v, GstBuffer * buffer, int w, int h)
{
#if 0
  int x, y;
  paintinfo pi = { NULL, };
  paintinfo *p = &pi;
  struct fourcc_list_struct *fourcc;

  p->width = w;
  p->height = h;
  fourcc = v->fourcc;
  if (fourcc == NULL)
    return;

  fourcc->paint_setup (p, dest);
  p->paint_hline = fourcc->paint_hline;

  p->color = vts_colors + COLOR_GREEN;
  for (y = 0; y < h; y++) {
    p->paint_hline (p, 0, y, w);
  }

  for (y = 0; y < h; y += 4) {
    for (x = ((y % 8) == 0) ? 0 : 4; x < w; x += 8) {
      guint len = (x < (w - 3)) ? 4 : (w - x);

      p->color = vts_colors + COLOR_RED;
      p->paint_hline (p, x, y + 0, len);
      if (G_LIKELY ((y + 1) < h)) {
        p->paint_hline (p, x, y + 1, len);
        if (G_LIKELY ((y + 2) < h)) {
          p->paint_hline (p, x, y + 2, len);
          if (G_LIKELY ((y + 3) < h)) {
            p->paint_hline (p, x, y + 3, len);
          }
        }
      }
    }
  }
#endif
}

void
gst_gl_test_src_checkers8 (GstGLTestSrc * v, GstBuffer * buffer, int w, int h)
{
#if 0
  int x, y;
  paintinfo pi = { NULL, };
  paintinfo *p = &pi;
  struct fourcc_list_struct *fourcc;

  p->width = w;
  p->height = h;
  fourcc = v->fourcc;
  if (fourcc == NULL)
    return;

  fourcc->paint_setup (p, dest);
  p->paint_hline = fourcc->paint_hline;

  p->color = vts_colors + COLOR_GREEN;
  for (y = 0; y < h; y++) {
    p->paint_hline (p, 0, y, w);
  }

  for (y = 0; y < h; y += 8) {
    for (x = ((GST_ROUND_UP_8 (y) % 16) == 0) ? 0 : 8; x < w; x += 16) {
      guint len = (x < (w - 7)) ? 8 : (w - x);

      p->color = vts_colors + COLOR_RED;
      p->paint_hline (p, x, y + 0, len);
      if (G_LIKELY ((y + 1) < h)) {
        p->paint_hline (p, x, y + 1, len);
        if (G_LIKELY ((y + 2) < h)) {
          p->paint_hline (p, x, y + 2, len);
          if (G_LIKELY ((y + 3) < h)) {
            p->paint_hline (p, x, y + 3, len);
            if (G_LIKELY ((y + 4) < h)) {
              p->paint_hline (p, x, y + 4, len);
              if (G_LIKELY ((y + 5) < h)) {
                p->paint_hline (p, x, y + 5, len);
                if (G_LIKELY ((y + 6) < h)) {
                  p->paint_hline (p, x, y + 6, len);
                  if (G_LIKELY ((y + 7) < h)) {
                    p->paint_hline (p, x, y + 7, len);
                  }
                }
              }
            }
          }
        }
      }
    }
  }
#endif
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
