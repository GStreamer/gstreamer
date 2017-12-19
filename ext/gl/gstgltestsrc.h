/* 
 * GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) 2002,2007 David A. Schleef <ds@schleef.org>
 * Copyright (C) 2008 Julien Isorce <julien.isorce@gmail.com>
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

#ifndef __GST_GL_TEST_SRC_H__
#define __GST_GL_TEST_SRC_H__

#include <gst/gst.h>
#include <gst/base/gstpushsrc.h>

#include <gst/gl/gl.h>

#include "gltestsrc.h"

G_BEGIN_DECLS

#define GST_TYPE_GL_TEST_SRC \
    (gst_gl_test_src_get_type())
#define GST_GL_TEST_SRC(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GL_TEST_SRC,GstGLTestSrc))
#define GST_GL_TEST_SRC_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_GL_TEST_SRC,GstGLTestSrcClass))
#define GST_IS_GL_TEST_SRC(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_GL_TEST_SRC))
#define GST_IS_GL_TEST_SRC_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_GL_TEST_SRC))

typedef struct _GstGLTestSrcClass GstGLTestSrcClass;

/**
 * GstGLTestSrc:
 *
 * Opaque data structure.
 */
struct _GstGLTestSrc {
    GstPushSrc element;

    /*< private >*/

    /* type of output */
    GstGLTestSrcPattern set_pattern;
    GstGLTestSrcPattern active_pattern;

    /* video state */
    GstVideoInfo out_info;

    GstGLFramebuffer *fbo;
    GstGLMemory *out_tex;

    GstGLShader *shader;

    GstBufferPool *pool;

    GstGLDisplay *display;
    GstGLContext *context, *other_context;
    gint64 timestamp_offset;              /* base offset */
    GstClockTime running_time;            /* total running time */
    gint64 n_frames;                      /* total frames sent */
    gboolean negotiated;

    gboolean gl_result;
    const struct SrcFuncs *src_funcs;
    gpointer src_impl;

    GstCaps *out_caps;
};

struct _GstGLTestSrcClass {
    GstPushSrcClass parent_class;
};

GType gst_gl_test_src_get_type (void);

G_END_DECLS

#endif /* __GST_GL_TEST_SRC_H__ */
