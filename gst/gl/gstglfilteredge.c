/* 
 * GStreamer
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

#include "gstglfilteredge.h"

#define GST_CAT_DEFAULT gst_gl_filter_edge_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

static const GstElementDetails element_details = 
    GST_ELEMENT_DETAILS ("OpenGL edge filter",
        "Filter/Effect",
        "Edge detection using GLSL",
        "Julien Isorce <julien.isorce@gmail.com>");

enum
{
    PROP_0
};

#define DEBUG_INIT(bla) \
    GST_DEBUG_CATEGORY_INIT (gst_gl_filter_edge_debug, "glfilteredge", 0, "glfilteredge element");

GST_BOILERPLATE_FULL (GstGLFilterEdge, gst_gl_filter_edge, GstGLFilter,
    GST_TYPE_GL_FILTER, DEBUG_INIT);

static void gst_gl_filter_edge_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gl_filter_edge_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_gl_filter_edge_reset (GstGLFilter* filter);
static void gst_gl_filter_edge_init_shader (GstGLFilter* filter);
static gboolean gst_gl_filter_edge_filter (GstGLFilter * filter,
    GstGLBuffer * inbuf, GstGLBuffer * outbuf);
static void gst_gl_filter_edge_callback (gint width, gint height, guint texture, gpointer stuff);

static const gchar *sobel_fragment_source = 
    "uniform sampler2DRect tex;\n"
    "void main(void) {\n"
    "  const int N = 8;\n"
    "  const vec2 delta[N] = vec2[N](\n"
    "    vec2( -1.0,  -1.0 ),\n" 
    "    vec2( -1.0 ,  0.0 ),\n"
    "    vec2( -1.0 ,  1.0 ),\n"
    "    vec2(  0.0 ,  1.0 ),\n"
    "    vec2(  1.0 ,  1.0 ),\n"
    "    vec2(  1.0 ,  0.0 ),\n"
    "    vec2(  1.0 , -1.0 ),\n"
    "    vec2(  0.0 , -1.0 )\n"
    "  );\n"
    "  const float filterH[N] = float[N]\n"
    "    (-1.0, 0.0, 1.0, 2.0, 1.0, 0.0, -1.0, -2.0);\n"
    "  const float filterV[N] = float[N]\n"
    "    (-1.0, -2.0, -1.0, 0.0, 1.0, 2.0, 1.0, 0.0);\n"
    "  float gH = 0.0;\n"
    "  float gV = 0.0;\n"
    "  int i;\n"
    "  vec2 nxy = gl_TexCoord[0].xy;\n"
    "  for (i = 0; i < N; i++) {\n"
    "    vec4 vcolor_i = texture2DRect(tex, nxy + delta[i]);\n"
    "    float gray_i = (vcolor_i.r + vcolor_i.g + vcolor_i.b) / 3.0;\n"
    "    gH += gH +  filterH[i] * gray_i;\n"
    "    gV += gV +  filterV[i] * gray_i;\n"
    "  }\n"
    "  float g = sqrt(gH * gH + gV * gV) / 256.0;\n"
    "  gl_FragColor = vec4(g, g, g, 1.0);\n"
    "}\n";

static void
gst_gl_filter_edge_base_init (gpointer klass)
{
    GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

    gst_element_class_set_details (element_class, &element_details);
}

static void
gst_gl_filter_edge_class_init (GstGLFilterEdgeClass* klass)
{
    GObjectClass* gobject_class;

    gobject_class = (GObjectClass *) klass;
    gobject_class->set_property = gst_gl_filter_edge_set_property;
    gobject_class->get_property = gst_gl_filter_edge_get_property;

    GST_GL_FILTER_CLASS (klass)->filter = gst_gl_filter_edge_filter;
    GST_GL_FILTER_CLASS (klass)->onInitFBO = gst_gl_filter_edge_init_shader;
    GST_GL_FILTER_CLASS (klass)->onReset = gst_gl_filter_edge_reset;
}

static void
gst_gl_filter_edge_init (GstGLFilterEdge* filter,
    GstGLFilterEdgeClass* klass)
{
    filter->shader = NULL;  
}

static void
gst_gl_filter_edge_reset (GstGLFilter* filter)
{
    GstGLFilterEdge* edge_filter = GST_GL_FILTER_EDGE(filter);

    //blocking call, wait the opengl thread has destroyed the shader
    gst_gl_display_del_shader (filter->display, edge_filter->shader);
}

static void
gst_gl_filter_edge_set_property (GObject* object, guint prop_id,
    const GValue* value, GParamSpec* pspec)
{
    //GstGLFilterEdge *filter = GST_GL_FILTER_EDGE (object);

    switch (prop_id) 
    {
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

static void
gst_gl_filter_edge_get_property (GObject* object, guint prop_id,
    GValue* value, GParamSpec* pspec)
{
    //GstGLFilterEdge *filter = GST_GL_FILTER_EDGE (object);

    switch (prop_id) 
    {
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

static void
gst_gl_filter_edge_init_shader (GstGLFilter* filter)
{
    GstGLFilterEdge* edge_filter = GST_GL_FILTER_EDGE (filter);
    
    //blocking call, wait the opengl thread has compiled the shader program
    gst_gl_display_gen_shader (filter->display, sobel_fragment_source, &edge_filter->shader);
}

static gboolean
gst_gl_filter_edge_filter (GstGLFilter* filter, GstGLBuffer* inbuf,
    GstGLBuffer* outbuf)
{
    gpointer edge_filter = GST_GL_FILTER_EDGE (filter);
    
    //blocking call, generate a FBO
    gst_gl_display_use_fbo (filter->display, filter->width, filter->height,
        filter->fbo, filter->depthbuffer, outbuf->texture, gst_gl_filter_edge_callback,
        inbuf->width, inbuf->height, inbuf->texture,
        0, filter->width, 0, filter->height,
        GST_GL_DISPLAY_PROJECTION_ORTHO2D, edge_filter);

    return TRUE;
}

//opengl scene, params: input texture (not the output filter->texture)
static void
gst_gl_filter_edge_callback (gint width, gint height, guint texture, gpointer stuff)
{
    GstGLFilterEdge* edge_filter = GST_GL_FILTER_EDGE (stuff);
    gint i=0;
    
    glMatrixMode (GL_PROJECTION);
    glLoadIdentity ();

    gst_gl_shader_use (edge_filter->shader);

    glActiveTextureARB(GL_TEXTURE0_ARB);
    gst_gl_shader_set_uniform_1i  (edge_filter->shader, "tex", 0);
    glBindTexture (GL_TEXTURE_RECTANGLE_ARB, texture);

    glBegin (GL_QUADS);
        glTexCoord2i (0, 0);
        glVertex2f (-1.0f, -1.0f);
        glTexCoord2i (width, 0);
        glVertex2f (1.0f, -1.0f);
        glTexCoord2i (width, height);
        glVertex2f (1.0f, 1.0f);
        glTexCoord2i (0, height);
        glVertex2f (-1.0f, 1.0f);     
    glEnd ();
}
