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

/**
 * GstGLTestSrcPattern:
 * @GST_GL_TEST_SRC_SMPTE: A standard SMPTE test pattern
 * @GST_GL_TEST_SRC_SNOW: Random noise
 * @GST_GL_TEST_SRC_BLACK: A black image
 * @GST_GL_TEST_SRC_WHITE: A white image
 * @GST_GL_TEST_SRC_RED: A red image
 * @GST_GL_TEST_SRC_GREEN: A green image
 * @GST_GL_TEST_SRC_BLUE: A blue image
 * @GST_GL_TEST_SRC_CHECKERS1: Checkers pattern (1px)
 * @GST_GL_TEST_SRC_CHECKERS2: Checkers pattern (2px)
 * @GST_GL_TEST_SRC_CHECKERS4: Checkers pattern (4px)
 * @GST_GL_TEST_SRC_CHECKERS8: Checkers pattern (8px)
 * @GST_GL_TEST_SRC_CIRCULAR: Circular pattern
 * @GST_GL_TEST_SRC_BLINK: Alternate between black and white
 *
 * The test pattern to produce.
 */
typedef enum {
    GST_GL_TEST_SRC_SMPTE,
    GST_GL_TEST_SRC_SNOW,
    GST_GL_TEST_SRC_BLACK,
    GST_GL_TEST_SRC_WHITE,
    GST_GL_TEST_SRC_RED,
    GST_GL_TEST_SRC_GREEN,
    GST_GL_TEST_SRC_BLUE,
    GST_GL_TEST_SRC_CHECKERS1,
    GST_GL_TEST_SRC_CHECKERS2,
    GST_GL_TEST_SRC_CHECKERS4,
    GST_GL_TEST_SRC_CHECKERS8,
    GST_GL_TEST_SRC_CIRCULAR,
    GST_GL_TEST_SRC_BLINK
} GstGLTestSrcPattern;

typedef struct _GstGLTestSrc GstGLTestSrc;
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
    GstGLTestSrcPattern pattern_type;

    /* video state */
    char *format_name;
    GstVideoInfo out_info;

    GLuint fbo;
    GLuint depthbuffer;

    GstBuffer* buffer;
    GstBufferPool *pool;

    guint out_tex_id;
    GstGLDownload *download;

    GstGLDisplay *display;
    GstGLContext *context;
    gint64 timestamp_offset;              /* base offset */
    GstClockTime running_time;            /* total running time */
    gint64 n_frames;                      /* total frames sent */
    gboolean negotiated;

    void (*make_image) (GstGLTestSrc* v, GstBuffer* buffer, gint w, gint h);
};

struct _GstGLTestSrcClass {
    GstPushSrcClass parent_class;
};

GType gst_gl_test_src_get_type (void);

G_END_DECLS

#endif /* __GST_GL_TEST_SRC_H__ */
