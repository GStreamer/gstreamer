/* GStreamer
 * Copyright (C) <2003> David A. Schleef <ds@schleef.org>
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

#ifndef __GL_TEST_SRC_H__
#define __GL_TEST_SRC_H__

#include <glib.h>
#include "gstglbuffer.h"

struct vts_color_struct {
        guint8 Y, U, V;
        guint8 R, G, B;
	guint8 A;
};

void    gst_gl_test_src_smpte        (GstGLTestSrc * v,
                                         GstGLBuffer *buffer, int w, int h);
void    gst_gl_test_src_snow         (GstGLTestSrc * v,
                                         GstGLBuffer *buffer, int w, int h);
void    gst_gl_test_src_black        (GstGLTestSrc * v,
                                         GstGLBuffer *buffer, int w, int h);
void    gst_gl_test_src_white        (GstGLTestSrc * v,
                                         GstGLBuffer *buffer, int w, int h);
void    gst_gl_test_src_red          (GstGLTestSrc * v,
                                         GstGLBuffer *buffer, int w, int h);
void    gst_gl_test_src_green        (GstGLTestSrc * v,
                                         GstGLBuffer *buffer, int w, int h);
void    gst_gl_test_src_blue         (GstGLTestSrc * v,
                                         GstGLBuffer *buffer, int w, int h);
void    gst_gl_test_src_checkers1    (GstGLTestSrc * v,
                                         GstGLBuffer *buffer, int w, int h);
void    gst_gl_test_src_checkers2    (GstGLTestSrc * v,
                                         GstGLBuffer *buffer, int w, int h);
void    gst_gl_test_src_checkers4    (GstGLTestSrc * v,
                                         GstGLBuffer *buffer, int w, int h);
void    gst_gl_test_src_checkers8    (GstGLTestSrc * v,
                                         GstGLBuffer *buffer, int w, int h);
void    gst_gl_test_src_circular     (GstGLTestSrc * v,
                                         GstGLBuffer *buffer, int w, int h);

#endif
