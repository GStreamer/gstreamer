/*
 * GStreamer
 * Copyright (C) 2008 Julien Isorce <julien.isorce@gmail.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstglfilter.h"

#define GST_CAT_DEFAULT gst_gl_filter_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);


static GstStaticPadTemplate gst_gl_filter_src_pad_template =
  GST_STATIC_PAD_TEMPLATE ("src",
			   GST_PAD_SRC,
			   GST_PAD_ALWAYS,
			   GST_STATIC_CAPS (GST_GL_VIDEO_CAPS)
    );

static GstStaticPadTemplate gst_gl_filter_sink_pad_template =
  GST_STATIC_PAD_TEMPLATE ("sink",
			   GST_PAD_SINK,
			   GST_PAD_ALWAYS,
			   GST_STATIC_CAPS (GST_GL_VIDEO_CAPS)
    );

#define DEBUG_INIT(bla)							\
  GST_DEBUG_CATEGORY_INIT (gst_gl_filter_debug, "glfilter", 0, "glfilter element");

GST_BOILERPLATE_FULL (GstGLFilter, gst_gl_filter, GstBaseTransform,
		      GST_TYPE_BASE_TRANSFORM, DEBUG_INIT);

static void gst_gl_filter_set_property (GObject * object, guint prop_id,
					const GValue * value, GParamSpec * pspec);
static void gst_gl_filter_get_property (GObject * object, guint prop_id,
					GValue * value, GParamSpec * pspec);

static GstCaps* gst_gl_filter_transform_caps (GstBaseTransform* bt,
					      GstPadDirection direction, GstCaps* caps);
static void gst_gl_filter_reset (GstGLFilter * filter);
static gboolean gst_gl_filter_start (GstBaseTransform * bt);
static gboolean gst_gl_filter_stop (GstBaseTransform * bt);
static gboolean gst_gl_filter_get_unit_size (GstBaseTransform * trans,
					     GstCaps * caps, guint * size);
static GstFlowReturn gst_gl_filter_transform (GstBaseTransform * bt,
					      GstBuffer * inbuf, GstBuffer * outbuf);
static GstFlowReturn gst_gl_filter_prepare_output_buffer (GstBaseTransform *
							  trans, GstBuffer * input, gint size, GstCaps * caps, GstBuffer ** buf);
static gboolean gst_gl_filter_set_caps (GstBaseTransform * bt, GstCaps * incaps,
					GstCaps * outcaps);
static gboolean gst_gl_filter_do_transform (GstGLFilter * filter,
					    GstGLBuffer * inbuf, GstGLBuffer * outbuf);
/* GstGLDisplayThreadFunc */
static void gst_gl_filter_start_gl (GstGLDisplay *display, gpointer data);
static void gst_gl_filter_stop_gl (GstGLDisplay *display, gpointer data);


static void
gst_gl_filter_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
				      gst_static_pad_template_get (&gst_gl_filter_src_pad_template));
  gst_element_class_add_pad_template (element_class,
				      gst_static_pad_template_get (&gst_gl_filter_sink_pad_template));
}

static void
gst_gl_filter_class_init (GstGLFilterClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;
  gobject_class->set_property = gst_gl_filter_set_property;
  gobject_class->get_property = gst_gl_filter_get_property;

  GST_BASE_TRANSFORM_CLASS (klass)->transform_caps =
    gst_gl_filter_transform_caps;
  GST_BASE_TRANSFORM_CLASS (klass)->transform = gst_gl_filter_transform;
  GST_BASE_TRANSFORM_CLASS (klass)->start = gst_gl_filter_start;
  GST_BASE_TRANSFORM_CLASS (klass)->stop = gst_gl_filter_stop;
  GST_BASE_TRANSFORM_CLASS (klass)->set_caps = gst_gl_filter_set_caps;
  GST_BASE_TRANSFORM_CLASS (klass)->get_unit_size = gst_gl_filter_get_unit_size;
  GST_BASE_TRANSFORM_CLASS (klass)->prepare_output_buffer =
    gst_gl_filter_prepare_output_buffer;

  klass->set_caps = NULL;
  klass->filter = NULL;
  klass->display_init_cb = NULL;
  klass->display_reset_cb = NULL;
  klass->onInitFBO = NULL;
  klass->onReset = NULL;
}

static void
gst_gl_filter_init (GstGLFilter * filter, GstGLFilterClass * klass)
{
  //gst_element_create_all_pads (GST_ELEMENT (filter));

  filter->sinkpad = gst_element_get_static_pad (GST_ELEMENT (filter), "sink");
  filter->srcpad = gst_element_get_static_pad (GST_ELEMENT (filter), "src");

  gst_gl_filter_reset (filter);
}

static void
gst_gl_filter_set_property (GObject * object, guint prop_id,
			    const GValue * value, GParamSpec * pspec)
{
  //GstGLFilter *filter = GST_GL_FILTER (object);

  switch (prop_id)
  {
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    break;
  }
}

static void
gst_gl_filter_get_property (GObject * object, guint prop_id,
			    GValue * value, GParamSpec * pspec)
{
  //GstGLFilter *filter = GST_GL_FILTER (object);

  switch (prop_id)
  {
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    break;
  }
}

static void
gst_gl_filter_reset (GstGLFilter* filter)
{
  GstGLFilterClass* filter_class = GST_GL_FILTER_GET_CLASS (filter);

  if (filter_class->onReset)
    filter_class->onReset (filter);

  if (filter->display)
  {
    if (filter_class->display_reset_cb != NULL) {
      gst_gl_display_thread_add (filter->display, gst_gl_filter_stop_gl, filter);
    }
    //blocking call, delete the FBO
    gst_gl_display_del_fbo (filter->display, filter->fbo,
			    filter->depthbuffer);
    g_object_unref (filter->display);
    filter->display = NULL;
  }
  filter->width = 0;
  filter->height = 0;
  filter->fbo = 0;
  filter->depthbuffer = 0;
}

static gboolean
gst_gl_filter_start (GstBaseTransform* bt)
{
  return TRUE;
}

static gboolean
gst_gl_filter_stop (GstBaseTransform* bt)
{
  GstGLFilter *filter = GST_GL_FILTER (bt);

  gst_gl_filter_reset (filter);

  return TRUE;
}

static void
gst_gl_filter_start_gl (GstGLDisplay *display, gpointer data)
{
  GstGLFilter *filter = GST_GL_FILTER (data);
  GstGLFilterClass *filter_class = GST_GL_FILTER_GET_CLASS (filter);

  filter_class->display_init_cb (filter);
}

static void
gst_gl_filter_stop_gl (GstGLDisplay *display, gpointer data)
{
  GstGLFilter *filter = GST_GL_FILTER (data);
  GstGLFilterClass *filter_class = GST_GL_FILTER_GET_CLASS (filter);

  filter_class->display_reset_cb (filter);
}

static GstCaps*
gst_gl_filter_transform_caps (GstBaseTransform* bt,
			      GstPadDirection direction, GstCaps* caps)
{
  //GstGLFilter* filter = GST_GL_FILTER (bt);
  GstStructure* structure = gst_caps_get_structure (caps, 0);
  GstCaps* ret = gst_caps_copy (caps);
  const GValue* par = NULL;

  structure = gst_structure_copy (gst_caps_get_structure (ret, 0));

  gst_structure_set (structure,
		     "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
		     "height", GST_TYPE_INT_RANGE, 1, G_MAXINT, NULL);

  gst_caps_merge_structure (ret, gst_structure_copy (structure));

  if ((par = gst_structure_get_value (structure, "pixel-aspect-ratio")))
  {
    gst_structure_set (structure,
		       "pixel-aspect-ratio", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);
    gst_caps_merge_structure (ret, structure);
  }
  else
    gst_structure_free (structure);

  GST_DEBUG_OBJECT (bt, "returning caps: %" GST_PTR_FORMAT, ret);

  return ret;
}


static gboolean
gst_gl_filter_get_unit_size (GstBaseTransform* trans, GstCaps* caps,
			     guint* size)
{
  gboolean ret = FALSE;
  gint width = 0;
  gint height = 0;

  ret = gst_gl_buffer_parse_caps (caps, &width, &height);
  if (ret)
    *size = gst_gl_buffer_get_size (width, height);

  return TRUE;
}

static GstFlowReturn
gst_gl_filter_prepare_output_buffer (GstBaseTransform* trans,
				     GstBuffer* inbuf, gint size, GstCaps* caps, GstBuffer** buf)
{
  GstGLFilter* filter = NULL;
  GstGLBuffer* gl_inbuf = GST_GL_BUFFER (inbuf);
  GstGLBuffer* gl_outbuf = NULL;

  filter = GST_GL_FILTER (trans);

  if (filter->display == NULL)
  {
    GstGLFilterClass* filter_class = GST_GL_FILTER_GET_CLASS (filter);

    filter->display = g_object_ref (gl_inbuf->display);

    //blocking call, generate a FBO
    gst_gl_display_gen_fbo (filter->display, filter->width, filter->height,
			    &filter->fbo, &filter->depthbuffer);

    if (filter_class->display_init_cb != NULL) {
      gst_gl_display_thread_add (filter->display, gst_gl_filter_start_gl, filter);
    }

    if (filter_class->onInitFBO)
      filter_class->onInitFBO (filter);
  }

  gl_outbuf = gst_gl_buffer_new (filter->display,
				 filter->width, filter->height);

  *buf = GST_BUFFER (gl_outbuf);
  gst_buffer_set_caps (*buf, caps);

  if (gl_outbuf->texture)
    return GST_FLOW_OK;
  else
    return GST_FLOW_UNEXPECTED;
}

static gboolean
gst_gl_filter_set_caps (GstBaseTransform* bt, GstCaps* incaps,
			GstCaps* outcaps)
{
  GstGLFilter* filter = GST_GL_FILTER (bt);
  gboolean ret = FALSE;
  GstGLFilterClass* filter_class = GST_GL_FILTER_GET_CLASS (filter);

  ret = gst_gl_buffer_parse_caps (outcaps, &filter->width, &filter->height);

  if (filter_class->set_caps)
    filter_class->set_caps (filter, incaps, outcaps);

  if (!ret)
  {
    GST_DEBUG ("bad caps");
    return FALSE;
  }

  GST_DEBUG ("set_caps %d %d", filter->width, filter->height);

  return ret;
}

static GstFlowReturn
gst_gl_filter_transform (GstBaseTransform* bt, GstBuffer* inbuf,
			 GstBuffer* outbuf)
{
  GstGLFilter* filter;
  GstGLBuffer* gl_inbuf = GST_GL_BUFFER (inbuf);
  GstGLBuffer* gl_outbuf = GST_GL_BUFFER (outbuf);

  filter = GST_GL_FILTER (bt);

  gst_gl_filter_do_transform (filter, gl_inbuf, gl_outbuf);

  return GST_FLOW_OK;
}

static gboolean
gst_gl_filter_do_transform (GstGLFilter* filter,
			    GstGLBuffer* inbuf, GstGLBuffer* outbuf)
{
  GstGLFilterClass* filter_class = GST_GL_FILTER_GET_CLASS (filter);

  filter_class->filter (filter, inbuf, outbuf);

  return TRUE;
}

/* convenience functions to simplify filter development */

void
gst_gl_filter_render_to_target (GstGLFilter *filter,
				GLuint input, GLuint target,
				GLCB func, gpointer data)
{
  gst_gl_display_use_fbo (filter->display, filter->width, filter->height,
			  filter->fbo, filter->depthbuffer, target,
			  func,
			  filter->width, filter->height, input,
			  0, filter->width, 0, filter->height,
			  GST_GL_DISPLAY_PROJECTION_ORTHO2D,
			  data);
}
