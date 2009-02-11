/*
 * GStreamer
 * Copyright (C) 2007 David Schleef <ds@schleef.org>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstglbuffer.h"

static GObjectClass* gst_gl_buffer_parent_class;

static void
gst_gl_buffer_finalize (GstGLBuffer* buffer)
{
    //blocking call, put the texture in the pool
    gst_gl_display_del_texture (buffer->display, buffer->texture,
      buffer->width, buffer->height);

    g_object_unref (buffer->display);

    GST_MINI_OBJECT_CLASS (gst_gl_buffer_parent_class)->
	    finalize (GST_MINI_OBJECT (buffer));
}

static void
gst_gl_buffer_init (GstGLBuffer* buffer, gpointer g_class)
{
    buffer->display = NULL;

    buffer->width = 0;
    buffer->height = 0;
    buffer->texture = 0;
}

static void
gst_gl_buffer_class_init (gpointer g_class, gpointer class_data)
{
    GstMiniObjectClass* mini_object_class = GST_MINI_OBJECT_CLASS (g_class);

    gst_gl_buffer_parent_class = g_type_class_peek_parent (g_class);

    mini_object_class->finalize = (GstMiniObjectFinalizeFunction)
        gst_gl_buffer_finalize;
}


GType
gst_gl_buffer_get_type (void)
{
  static GType _gst_gl_buffer_type;

  if (G_UNLIKELY (_gst_gl_buffer_type == 0)) {
    static const GTypeInfo info = {
      sizeof (GstBufferClass),
      NULL,
      NULL,
      gst_gl_buffer_class_init,
      NULL,
      NULL,
      sizeof (GstGLBuffer),
      0,
      (GInstanceInitFunc) gst_gl_buffer_init,
      NULL
    };
    _gst_gl_buffer_type = g_type_register_static (GST_TYPE_BUFFER,
        "GstGLBuffer", &info, 0);
  }
  return _gst_gl_buffer_type;
}


GstGLBuffer*
gst_gl_buffer_new (GstGLDisplay* display,
    gint gl_width, gint gl_height)
{
    GstGLBuffer* gl_buffer = (GstGLBuffer *) gst_mini_object_new (GST_TYPE_GL_BUFFER);

    gl_buffer->display = g_object_ref (display);
    gl_buffer->width = gl_width;
    gl_buffer->height = gl_height;

    //it does not depends on the video format because gl buffer has always one texture.
    //the one attached to the upload FBO
    GST_BUFFER_SIZE (gl_buffer) = gst_gl_buffer_get_size (gl_width, gl_height);

    //blocking call, generate a texture using the pool
    gst_gl_display_gen_texture (gl_buffer->display, &gl_buffer->texture, gl_width, gl_height) ;

    return gl_buffer;
}


gint
gst_gl_buffer_get_size (gint width, gint height)
{
    //this is not strictly true, but it's used for compatibility with
    //queue and BaseTransform
    return width * height * 4;
}


gboolean
gst_gl_buffer_parse_caps (GstCaps* caps, gint* width, gint* height)
{
    GstStructure* structure = gst_caps_get_structure (caps, 0);
    gboolean ret = gst_structure_has_name (structure, "video/x-raw-gl");

    if (!ret)
        return ret;

    ret = gst_structure_get_int (structure, "width", width);
    ret &= gst_structure_get_int (structure, "height", height);

    return ret;
}
