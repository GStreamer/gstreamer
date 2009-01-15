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

/**
 * SECTION:element-gldownload
 *
 * download opengl textures into video frames.
 *
 * <refsect2>
 * <title>Color space conversion</title>
 * <para>
 * When needed, the color space conversion is made in a fragment shader using 
 * one frame buffer object instance.
 * </para>
 * </refsect2>
 * <refsect2>
 * <title>Examples</title>
 * |[
 * gst-launch -v videotestsrc ! "video/x-raw-rgb" ! glupload ! gldownload ! \
 *   "video/x-raw-rgb" ! ximagesink
 * ]| A pipeline to test downloading.
 * No special opengl extension is used in this pipeline, that's why it should work
 * with OpenGL >= 1.1. That's the case if you are using the MESA3D driver v1.3.
  |[
 * gst-launch -v videotestsrc ! "video/x-raw-rgb, width=640, height=480" ! glupload ! gldownload ! \
 *   "video/x-raw-rgb, width=320, height=240" ! ximagesink
 * ]| A pipeline to test hardware scaling.
 * Frame buffer extension is required. Inded one FBO is used bettween glupload and gldownload,
 * because the texture needs to be resized.
 * |[
 * gst-launch -v gltestsrc ! gldownload ! xvimagesink
 * ]| A pipeline to test hardware colorspace conversion.
 * Your driver must support GLSL (OpenGL Shading Language needs OpenGL >= 2.1). 
 * Texture RGB32 is converted to one of the 4 following format YUY2, UYVY, I420, YV12 and AYUV,
 * through some fragment shaders and using one framebuffer (FBO extension OpenGL >= 1.4).
 * MESA >= 7.1 supports GLSL but it's made in software.
 * |[
 * gst-launch -v videotestsrc ! glupload ! gldownload ! "video/x-raw-yuv, format=(fourcc)YUY2" ! glimagesink
 * ]| A pipeline to test hardware colorspace conversion
 * FBO and GLSL are required. 
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstgldownload.h"

#define GST_CAT_DEFAULT gst_gl_download_debug
	GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

static const GstElementDetails element_details =
    GST_ELEMENT_DETAILS ("OpenGL video maker",
        "Filter/Effect",
        "A from GL to video flow filter",
        "Julien Isorce <julien.isorce@gmail.com>");

static GstStaticPadTemplate gst_gl_download_src_pad_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (
        GST_VIDEO_CAPS_RGB ";" GST_VIDEO_CAPS_BGR ";"
        GST_VIDEO_CAPS_RGBx ";" GST_VIDEO_CAPS_BGRx ";"
        GST_VIDEO_CAPS_xRGB ";" GST_VIDEO_CAPS_xBGR ";"
        GST_VIDEO_CAPS_RGBA ";" GST_VIDEO_CAPS_BGRA ";"
        GST_VIDEO_CAPS_ARGB ";" GST_VIDEO_CAPS_ABGR ";"
        GST_VIDEO_CAPS_YUV ("{ I420, YV12, YUY2, UYVY, AYUV }"))
    );

static GstStaticPadTemplate gst_gl_download_sink_pad_template =
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
    GST_DEBUG_CATEGORY_INIT (gst_gl_download_debug, "gldownload", 0, "gldownload element");

GST_BOILERPLATE_FULL (GstGLDownload, gst_gl_download, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM, DEBUG_INIT);

static void gst_gl_download_set_property (GObject* object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gl_download_get_property (GObject* object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_gl_download_reset (GstGLDownload* download);
static gboolean gst_gl_download_set_caps (GstBaseTransform* bt,
    GstCaps* incaps, GstCaps* outcaps);
static GstCaps* gst_gl_download_transform_caps (GstBaseTransform* bt,
    GstPadDirection direction, GstCaps* caps);
static gboolean gst_gl_download_start (GstBaseTransform* bt);
static gboolean gst_gl_download_stop (GstBaseTransform* bt);
static GstFlowReturn gst_gl_download_transform (GstBaseTransform* trans,
    GstBuffer* inbuf, GstBuffer* outbuf);
static gboolean gst_gl_download_get_unit_size (GstBaseTransform* trans, GstCaps* caps,
    guint* size);


static void
gst_gl_download_base_init (gpointer klass)
{
    GstElementClass* element_class = GST_ELEMENT_CLASS (klass);

    gst_element_class_set_details (element_class, &element_details);

    gst_element_class_add_pad_template (element_class,
        gst_static_pad_template_get (&gst_gl_download_src_pad_template));
    gst_element_class_add_pad_template (element_class,
        gst_static_pad_template_get (&gst_gl_download_sink_pad_template));
}


static void
gst_gl_download_class_init (GstGLDownloadClass* klass)
{
    GObjectClass* gobject_class;

    gobject_class = (GObjectClass *) klass;
    gobject_class->set_property = gst_gl_download_set_property;
    gobject_class->get_property = gst_gl_download_get_property;

    GST_BASE_TRANSFORM_CLASS (klass)->transform_caps =
        gst_gl_download_transform_caps;
    GST_BASE_TRANSFORM_CLASS (klass)->transform = gst_gl_download_transform;
    GST_BASE_TRANSFORM_CLASS (klass)->start = gst_gl_download_start;
    GST_BASE_TRANSFORM_CLASS (klass)->stop = gst_gl_download_stop;
    GST_BASE_TRANSFORM_CLASS (klass)->set_caps = gst_gl_download_set_caps;
    GST_BASE_TRANSFORM_CLASS (klass)->get_unit_size =
        gst_gl_download_get_unit_size;
}


static void
gst_gl_download_init (GstGLDownload* download, GstGLDownloadClass* klass)
{
    gst_gl_download_reset (download);
}


static void
gst_gl_download_set_property (GObject* object, guint prop_id,
    const GValue* value, GParamSpec* pspec)
{
    //GstGLDownload *download = GST_GL_DOWNLOAD (object);

    switch (prop_id)
    {
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}


static void
gst_gl_download_get_property (GObject* object, guint prop_id,
    GValue* value, GParamSpec* pspec)
{
    //GstGLDownload *download = GST_GL_DOWNLOAD (object);

    switch (prop_id) {
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}


static void
gst_gl_download_reset (GstGLDownload* download)
{
    if (download->display)
    {
        g_object_unref (download->display);
        download->display = NULL;
    }
}


static gboolean
gst_gl_download_start (GstBaseTransform* bt)
{
    //GstGLDownload* download = GST_GL_DOWNLOAD (bt);

    return TRUE;
}

static gboolean
gst_gl_download_stop (GstBaseTransform* bt)
{
    GstGLDownload* download = GST_GL_DOWNLOAD (bt);

    gst_gl_download_reset (download);

    return TRUE;
}

static GstCaps*
gst_gl_download_transform_caps (GstBaseTransform * bt,
    GstPadDirection direction, GstCaps* caps)
{
    GstGLDownload* download;
    GstStructure* structure;
    GstCaps *newcaps, *newothercaps;
    GstStructure* newstruct;
    const GValue* width_value;
    const GValue* height_value;
    const GValue* framerate_value;
    const GValue* par_value;

    download = GST_GL_DOWNLOAD (bt);

    GST_DEBUG ("transform caps %" GST_PTR_FORMAT, caps);

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

    GST_DEBUG ("new caps %" GST_PTR_FORMAT, newcaps);

    return newcaps;
}

static gboolean
gst_gl_download_set_caps (GstBaseTransform* bt, GstCaps* incaps,
    GstCaps* outcaps)
{
    GstGLDownload* download;
    gboolean ret;

    download = GST_GL_DOWNLOAD (bt);

    GST_DEBUG ("called with %" GST_PTR_FORMAT, incaps);

    ret = gst_video_format_parse_caps (outcaps, &download->video_format,
        &download->width, &download->height);

    if (!ret)
    {
        GST_ERROR ("bad caps");
        return FALSE;
    }

    return ret;
}

static gboolean
gst_gl_download_get_unit_size (GstBaseTransform* trans, GstCaps* caps,
    guint* size)
{
	gboolean ret;
	GstStructure *structure;
	gint width;
	gint height;

	structure = gst_caps_get_structure (caps, 0);
	if (gst_structure_has_name (structure, "video/x-raw-gl"))
	{
		ret = gst_gl_buffer_parse_caps (caps, &width, &height);
		if (ret)
			*size = gst_gl_buffer_get_size (width, height);
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
gst_gl_download_transform (GstBaseTransform* trans, GstBuffer* inbuf,
    GstBuffer* outbuf)
{
    GstGLDownload* download = GST_GL_DOWNLOAD (trans);
    GstGLBuffer* gl_inbuf = GST_GL_BUFFER (inbuf);

    if (download->display == NULL)
    {
        download->display = g_object_ref (gl_inbuf->display);

        //blocking call, init color space conversion if needed
        gst_gl_display_init_download (download->display, download->video_format,
            download->width, download->height);
    }
    else
        g_assert (download->display == gl_inbuf->display);

    GST_DEBUG ("making video %p size %d",
        GST_BUFFER_DATA (outbuf), GST_BUFFER_SIZE (outbuf));

    //blocking call
    if (gst_gl_display_do_download(download->display, gl_inbuf->texture,
            gl_inbuf->width, gl_inbuf->height, GST_BUFFER_DATA (outbuf)))
        return GST_FLOW_OK;
    else
        return GST_FLOW_UNEXPECTED;

}
