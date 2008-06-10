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

#include "gstglvideomaker.h"

#define GST_CAT_DEFAULT gst_gl_videomaker_debug
	GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

static const GstElementDetails element_details = 
    GST_ELEMENT_DETAILS ("OpenGL video maker",
        "Filter/Effect",
        "A from GL to video flow filter",
        "Julien Isorce <julien.isorce@gmail.com>");

static GstStaticPadTemplate gst_gl_videomaker_src_pad_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_xRGB ";"
        GST_VIDEO_CAPS_RGBx ";"
        GST_VIDEO_CAPS_BGRx ";"
        GST_VIDEO_CAPS_xBGR ";"
        GST_VIDEO_CAPS_YUV ("{ I420, YV12, YUY2, UYVY, AYUV }"))
    );

static GstStaticPadTemplate gst_gl_videomaker_sink_pad_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_GL_VIDEO_CAPS)
    );

enum
{
    PROP_0
};

#define DEBUG_INIT(bla) \
    GST_DEBUG_CATEGORY_INIT (gst_gl_videomaker_debug, "glvideomaker", 0, "glvideomaker element");

GST_BOILERPLATE_FULL (GstGLVideomaker, gst_gl_videomaker, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM, DEBUG_INIT);

static void gst_gl_videomaker_set_property (GObject* object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gl_videomaker_get_property (GObject* object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_gl_videomaker_reset (GstGLVideomaker* videomaker);
static gboolean gst_gl_videomaker_set_caps (GstBaseTransform* bt,
    GstCaps* incaps, GstCaps* outcaps);
static GstCaps* gst_gl_videomaker_transform_caps (GstBaseTransform* bt,
    GstPadDirection direction, GstCaps* caps);
static gboolean gst_gl_videomaker_start (GstBaseTransform* bt);
static gboolean gst_gl_videomaker_stop (GstBaseTransform* bt);
static GstFlowReturn gst_gl_videomaker_transform (GstBaseTransform* trans,
    GstBuffer* inbuf, GstBuffer* outbuf);
static gboolean gst_gl_videomaker_get_unit_size (GstBaseTransform* trans, GstCaps* caps,
    guint* size);


static void
gst_gl_videomaker_base_init (gpointer klass)
{
    GstElementClass* element_class = GST_ELEMENT_CLASS (klass);

    gst_element_class_set_details (element_class, &element_details);

    gst_element_class_add_pad_template (element_class,
        gst_static_pad_template_get (&gst_gl_videomaker_src_pad_template));
    gst_element_class_add_pad_template (element_class,
        gst_static_pad_template_get (&gst_gl_videomaker_sink_pad_template));
}


static void
gst_gl_videomaker_class_init (GstGLVideomakerClass* klass)
{
    GObjectClass* gobject_class;

    gobject_class = (GObjectClass *) klass;
    gobject_class->set_property = gst_gl_videomaker_set_property;
    gobject_class->get_property = gst_gl_videomaker_get_property;

    GST_BASE_TRANSFORM_CLASS (klass)->transform_caps =
        gst_gl_videomaker_transform_caps;
    GST_BASE_TRANSFORM_CLASS (klass)->transform = gst_gl_videomaker_transform;
    GST_BASE_TRANSFORM_CLASS (klass)->start = gst_gl_videomaker_start;
    GST_BASE_TRANSFORM_CLASS (klass)->stop = gst_gl_videomaker_stop;
    GST_BASE_TRANSFORM_CLASS (klass)->set_caps = gst_gl_videomaker_set_caps;
    GST_BASE_TRANSFORM_CLASS (klass)->get_unit_size =
        gst_gl_videomaker_get_unit_size;
}


static void
gst_gl_videomaker_init (GstGLVideomaker* videomaker, GstGLVideomakerClass* klass)
{
    gst_gl_videomaker_reset (videomaker);
}


static void
gst_gl_videomaker_set_property (GObject* object, guint prop_id,
    const GValue* value, GParamSpec* pspec)
{
    //GstGLVideomaker *videomaker = GST_GL_VIDEOMAKER (object);

    switch (prop_id) 
    {
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}


static void
gst_gl_videomaker_get_property (GObject* object, guint prop_id,
    GValue* value, GParamSpec* pspec)
{
    //GstGLVideomaker *videomaker = GST_GL_VIDEOMAKER (object);

    switch (prop_id) {
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}


static void
gst_gl_videomaker_reset (GstGLVideomaker* videomaker)
{
    if (videomaker->display) 
    {
        g_object_unref (videomaker->display);
        videomaker->display = NULL;
    }
}


static gboolean
gst_gl_videomaker_start (GstBaseTransform* bt)
{
    //GstGLVideomaker* videomaker = GST_GL_VIDEOMAKER (bt);

    return TRUE;
}

static gboolean
gst_gl_videomaker_stop (GstBaseTransform* bt)
{
    GstGLVideomaker* videomaker = GST_GL_VIDEOMAKER (bt);

    gst_gl_videomaker_reset (videomaker);

    return TRUE;
}

static GstCaps*
gst_gl_videomaker_transform_caps (GstBaseTransform * bt,
    GstPadDirection direction, GstCaps* caps)
{
    GstGLVideomaker* videomaker;
    GstStructure* structure;
    GstCaps *newcaps, *newothercaps;
    GstStructure* newstruct;
    const GValue* width_value;
    const GValue* height_value;
    const GValue* framerate_value;
    const GValue* par_value;

    videomaker = GST_GL_VIDEOMAKER (bt);

    GST_ERROR ("transform caps %" GST_PTR_FORMAT, caps);

    structure = gst_caps_get_structure (caps, 0);

    width_value = gst_structure_get_value (structure, "width");
    height_value = gst_structure_get_value (structure, "height");
    framerate_value = gst_structure_get_value (structure, "framerate");
    par_value = gst_structure_get_value (structure, "pixel-aspect-ratio");

    if (direction == GST_PAD_SINK) 
    {
	    newothercaps = gst_caps_new_simple ("video/x-raw-rgb", NULL);
	    newstruct = gst_caps_get_structure (newothercaps, 0);
	    gst_structure_set_value (newstruct, "width", width_value);
	    gst_structure_set_value (newstruct, "height", height_value);
	    gst_structure_set_value (newstruct, "framerate", framerate_value);
	    if (par_value)
		    gst_structure_set_value (newstruct, "pixel-aspect-ratio", par_value);
	    else
		    gst_structure_set (newstruct, "pixel-aspect-ratio", GST_TYPE_FRACTION,
						       1, 1, NULL);
	    newcaps = gst_caps_new_simple ("video/x-raw-yuv", NULL);
	    gst_caps_append(newcaps, newothercaps);
    } 
    else newcaps = gst_caps_new_simple ("video/x-raw-gl", NULL);

    newstruct = gst_caps_get_structure (newcaps, 0);
    gst_structure_set_value (newstruct, "width", width_value);
    gst_structure_set_value (newstruct, "height", height_value);
    gst_structure_set_value (newstruct, "framerate", framerate_value);
    if (par_value)
	    gst_structure_set_value (newstruct, "pixel-aspect-ratio", par_value);
    else
	    gst_structure_set (newstruct, "pixel-aspect-ratio", GST_TYPE_FRACTION,
	                       1, 1, NULL);

    GST_ERROR ("new caps %" GST_PTR_FORMAT, newcaps);

    return newcaps;
}

static gboolean
gst_gl_videomaker_set_caps (GstBaseTransform* bt, GstCaps* incaps,
    GstCaps* outcaps)
{
    GstGLVideomaker* videomaker;
    gboolean ret;

    videomaker = GST_GL_VIDEOMAKER (bt);

    GST_DEBUG ("called with %" GST_PTR_FORMAT, incaps);

    ret = gst_video_format_parse_caps (outcaps, &videomaker->video_format,
        &videomaker->width, &videomaker->height);

    if (!ret) 
    {
        GST_ERROR ("bad caps");
        return FALSE;
    }

    return ret;
}

static gboolean
gst_gl_videomaker_get_unit_size (GstBaseTransform* trans, GstCaps* caps,
    guint* size)
{
	gboolean ret;
	GstStructure *structure;
	gint width;
	gint height;

	structure = gst_caps_get_structure (caps, 0);
	if (gst_structure_has_name (structure, "video/x-raw-gl")) 
	{
		GstVideoFormat video_format;

		ret = gst_gl_buffer_format_parse_caps (caps, &video_format, &width, &height);
		if (ret) 
			*size = gst_gl_buffer_format_get_size (video_format, width, height);
	} 
	else 
	{
		GstVideoFormat video_format;
		ret = gst_video_format_parse_caps (caps, &video_format, &width, &height);
		if (ret) 
			*size = gst_video_format_get_size (video_format, width, height);
	}

	return ret;
}

static GstFlowReturn
gst_gl_videomaker_transform (GstBaseTransform* trans, GstBuffer* inbuf,
    GstBuffer* outbuf)
{
    GstGLVideomaker* videomaker = NULL;
    GstGLBuffer* gl_inbuf = GST_GL_BUFFER (inbuf);

    videomaker = GST_GL_VIDEOMAKER (trans);

    if (videomaker->display == NULL) 
    {
        videomaker->display = g_object_ref (gl_inbuf->display);
        gst_gl_display_initDonwloadFBO (videomaker->display, videomaker->width, videomaker->height);
    }
    else 
        g_assert (videomaker->display == gl_inbuf->display);

    GST_DEBUG ("making video %p size %d",
      GST_BUFFER_DATA (outbuf), GST_BUFFER_SIZE (outbuf));

    //blocking call
    gst_gl_display_videoChanged(videomaker->display, videomaker->video_format, 
        gl_inbuf->width, gl_inbuf->height, gl_inbuf->textureGL, GST_BUFFER_DATA (outbuf));

    return GST_FLOW_OK;
}
