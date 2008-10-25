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

/**
 * SECTION:element-glcolorscale
 *
 * video frame scaling and colorspace conversion.
 *
 * <refsect2>
 * <title>Scaling and Color space conversion</title>
 * <para>
 * Equivalent to glupload ! gldownload.
 * </para>
 * </refsect2>
 * <refsect2>
 * <title>Examples</title>
 * |[
 * gst-launch -v videotestsrc ! "video/x-raw-yuv" ! glcolorscale ! ximagesink
 * ]| A pipeline to test colorspace conversion.
 * FBO is required.
  |[
 * gst-launch -v videotestsrc ! "video/x-raw-yuv, width=640, height=480, format=(fourcc)AYUV" ! glcolorscale ! \
 *   "video/x-raw-yuv, width=320, height=240, format=(fourcc)YV12" ! autovideosink
 * ]| A pipeline to test hardware scaling and colorspace conversion.
 * FBO and GLSL are required.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstglcolorscale.h"


#define GST_CAT_DEFAULT gst_gl_colorscale_debug
	GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

static const GstElementDetails element_details =
    GST_ELEMENT_DETAILS ("OpenGL color scale",
        "Filter/Effect",
        "Colorspace converter and video scaler",
        "Julien Isorce <julien.isorce@gmail.com>");

/* Source pad definition */
static GstStaticPadTemplate gst_gl_colorscale_src_pad_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_RGBx ";"
        GST_VIDEO_CAPS_BGRx ";"
        GST_VIDEO_CAPS_xRGB ";"
        GST_VIDEO_CAPS_xBGR ";"
        GST_VIDEO_CAPS_YUV ("{ I420, YV12, YUY2, UYVY, AYUV }"))
    );

/* Source pad definition */
static GstStaticPadTemplate gst_gl_colorscale_sink_pad_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_RGBx ";"
        GST_VIDEO_CAPS_BGRx ";"
        GST_VIDEO_CAPS_xRGB ";"
        GST_VIDEO_CAPS_xBGR ";"
        GST_VIDEO_CAPS_YUV ("{ I420, YV12, YUY2, UYVY, AYUV }"))
    );

/* Properties */
enum
{
    PROP_0
};

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_gl_colorscale_debug, "glcolorscale", 0, "glcolorscale element");

GST_BOILERPLATE_FULL (GstGLColorscale, gst_gl_colorscale, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM, DEBUG_INIT);

static void gst_gl_colorscale_set_property (GObject* object, guint prop_id,
    const GValue* value, GParamSpec* pspec);
static void gst_gl_colorscale_get_property (GObject* object, guint prop_id,
    GValue* value, GParamSpec* pspec);

static void gst_gl_colorscale_reset (GstGLColorscale* colorscale);
static gboolean gst_gl_colorscale_set_caps (GstBaseTransform* bt,
    GstCaps* incaps, GstCaps* outcaps);
static GstCaps *gst_gl_colorscale_transform_caps (GstBaseTransform* bt,
    GstPadDirection direction, GstCaps* caps);
static void gst_gl_colorscale_fixate_caps (GstBaseTransform* base, GstPadDirection direction,
    GstCaps* caps, GstCaps* othercaps);
static gboolean gst_gl_colorscale_start (GstBaseTransform* bt);
static gboolean gst_gl_colorscale_stop (GstBaseTransform* bt);
static GstFlowReturn gst_gl_colorscale_transform (GstBaseTransform* trans,
    GstBuffer* inbuf, GstBuffer * outbuf);
static gboolean gst_gl_colorscale_get_unit_size (GstBaseTransform* trans,
    GstCaps* caps, guint* size);


static void
gst_gl_colorscale_base_init (gpointer klass)
{
    GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

    gst_element_class_set_details (element_class, &element_details);

    gst_element_class_add_pad_template (element_class,
        gst_static_pad_template_get (&gst_gl_colorscale_src_pad_template));
    gst_element_class_add_pad_template (element_class,
        gst_static_pad_template_get (&gst_gl_colorscale_sink_pad_template));
}

static void
gst_gl_colorscale_class_init (GstGLColorscaleClass* klass)
{
    GObjectClass* gobject_class;

    gobject_class = (GObjectClass *) klass;
    gobject_class->set_property = gst_gl_colorscale_set_property;
    gobject_class->get_property = gst_gl_colorscale_get_property;

    GST_BASE_TRANSFORM_CLASS (klass)->transform_caps =
        gst_gl_colorscale_transform_caps;
    GST_BASE_TRANSFORM_CLASS (klass)->fixate_caps = gst_gl_colorscale_fixate_caps;
    GST_BASE_TRANSFORM_CLASS (klass)->transform = gst_gl_colorscale_transform;
    GST_BASE_TRANSFORM_CLASS (klass)->start = gst_gl_colorscale_start;
    GST_BASE_TRANSFORM_CLASS (klass)->stop = gst_gl_colorscale_stop;
    GST_BASE_TRANSFORM_CLASS (klass)->set_caps = gst_gl_colorscale_set_caps;
    GST_BASE_TRANSFORM_CLASS (klass)->get_unit_size = gst_gl_colorscale_get_unit_size;
}

static void
gst_gl_colorscale_init (GstGLColorscale* colorscale, GstGLColorscaleClass* klass)
{
    gst_gl_colorscale_reset (colorscale);
}

static void
gst_gl_colorscale_set_property (GObject* object, guint prop_id,
    const GValue* value, GParamSpec* pspec)
{
    //GstGLColorscale* colorscale = GST_GL_COLORSCALE (object);

    switch (prop_id)
    {
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

static void
gst_gl_colorscale_get_property (GObject* object, guint prop_id,
    GValue* value, GParamSpec* pspec)
{
    //GstGLColorscale *colorscale = GST_GL_COLORSCALE (object);

    switch (prop_id)
    {
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

static void
gst_gl_colorscale_reset (GstGLColorscale* colorscale)
{
    if (colorscale->display)
    {
        g_object_unref (colorscale->display);
        colorscale->display = NULL;
    }
}

static gboolean
gst_gl_colorscale_start (GstBaseTransform* bt)
{
    //GstGLColorscale* colorscale = GST_GL_COLORSCALE (bt);

    return TRUE;
}

static gboolean
gst_gl_colorscale_stop (GstBaseTransform* bt)
{
    GstGLColorscale* colorscale = GST_GL_COLORSCALE (bt);

    gst_gl_colorscale_reset (colorscale);

    return TRUE;
}

static GstCaps*
gst_gl_colorscale_transform_caps (GstBaseTransform* bt,
    GstPadDirection direction, GstCaps* caps)
{
	//GstGLColorscale* colorscale = GST_GL_COLORSCALE (bt);
	GstStructure* structure = gst_caps_get_structure (caps, 0);
	GstCaps* newcaps = gst_caps_new_simple ("video/x-raw-yuv", NULL);
    GstCaps* newothercaps = gst_caps_new_simple ("video/x-raw-rgb", NULL);
	const GValue* framerate_value = NULL;
	const GValue* par_value = NULL;

	GST_DEBUG ("transform caps %" GST_PTR_FORMAT, caps);

    framerate_value = gst_structure_get_value (structure, "framerate");
	par_value = gst_structure_get_value (structure, "pixel-aspect-ratio");

    gst_caps_append(newcaps, newothercaps);


    structure = gst_structure_copy (gst_caps_get_structure (newcaps, 0));

    gst_structure_set (structure,
        "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
        "height", GST_TYPE_INT_RANGE, 1, G_MAXINT, NULL);

	gst_structure_set_value (structure, "framerate", framerate_value);
	if (par_value)
		gst_structure_set_value (structure, "pixel-aspect-ratio", par_value);
	else
		gst_structure_set (structure, "pixel-aspect-ratio", GST_TYPE_FRACTION,
		                   1, 1, NULL);

    gst_caps_merge_structure (newcaps, gst_structure_copy (structure));

	GST_DEBUG ("new caps %" GST_PTR_FORMAT, newcaps);

	return newcaps;
}

/* from gst-plugins-base "videoscale" code */
static void
gst_gl_colorscale_fixate_caps (GstBaseTransform* base, GstPadDirection direction,
    GstCaps* caps, GstCaps* othercaps)
{
    GstStructure *ins, *outs;

    const GValue *from_par, *to_par;

    g_return_if_fail (gst_caps_is_fixed (caps));

    GST_DEBUG_OBJECT (base, "trying to fixate othercaps %" GST_PTR_FORMAT
      " based on caps %" GST_PTR_FORMAT, othercaps, caps);

    ins = gst_caps_get_structure (caps, 0);
    outs = gst_caps_get_structure (othercaps, 0);

    from_par = gst_structure_get_value (ins, "pixel-aspect-ratio");
    to_par = gst_structure_get_value (outs, "pixel-aspect-ratio");

    //we have both PAR but they might not be fixated
    if (from_par && to_par)
    {
        gint from_w, from_h, from_par_n, from_par_d, to_par_n, to_par_d;

        gint count = 0, w = 0, h = 0;

        guint num, den;

        //from_par should be fixed
        g_return_if_fail (gst_value_is_fixed (from_par));

        from_par_n = gst_value_get_fraction_numerator (from_par);
        from_par_d = gst_value_get_fraction_denominator (from_par);

        //fixate the out PAR
        if (!gst_value_is_fixed (to_par))
        {
          GST_DEBUG_OBJECT (base, "fixating to_par to %dx%d", from_par_n,
              from_par_d);
          gst_structure_fixate_field_nearest_fraction (outs, "pixel-aspect-ratio",
              from_par_n, from_par_d);
        }

        to_par_n = gst_value_get_fraction_numerator (to_par);
        to_par_d = gst_value_get_fraction_denominator (to_par);

        //f both width and height are already fixed, we can't do anything
        //about it anymore
        if (gst_structure_get_int (outs, "width", &w))
            ++count;
        if (gst_structure_get_int (outs, "height", &h))
            ++count;
        if (count == 2)
        {
            GST_DEBUG_OBJECT (base, "dimensions already set to %dx%d, not fixating",
                w, h);
            return;
        }

        gst_structure_get_int (ins, "width", &from_w);
        gst_structure_get_int (ins, "height", &from_h);

        if (!gst_video_calculate_display_ratio (&num, &den, from_w, from_h,
                from_par_n, from_par_d, to_par_n, to_par_d))
        {
          GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (NULL),
              ("Error calculating the output scaled size - integer overflow"));
          return;
        }

        GST_DEBUG_OBJECT (base,
            "scaling input with %dx%d and PAR %d/%d to output PAR %d/%d",
            from_w, from_h, from_par_n, from_par_d, to_par_n, to_par_d);
        GST_DEBUG_OBJECT (base, "resulting output should respect ratio of %d/%d",
            num, den);

        //now find a width x height that respects this display ratio.
        //prefer those that have one of w/h the same as the incoming video
        //using wd / hd = num / den

        //if one of the output width or height is fixed, we work from there
        if (h)
        {
            GST_DEBUG_OBJECT (base, "height is fixed,scaling width");
            w = (guint) gst_util_uint64_scale_int (h, num, den);
        }
        else if (w)
        {
            GST_DEBUG_OBJECT (base, "width is fixed, scaling height");
            h = (guint) gst_util_uint64_scale_int (w, den, num);
        }
        else
        {
            //none of width or height is fixed, figure out both of them based only on
            //the input width and height
            //check hd / den is an integer scale factor, and scale wd with the PAR
            if (from_h % den == 0)
            {
                GST_DEBUG_OBJECT (base, "keeping video height");
                h = from_h;
                w = (guint) gst_util_uint64_scale_int (h, num, den);
            }
            else if (from_w % num == 0)
            {
                GST_DEBUG_OBJECT (base, "keeping video width");
                w = from_w;
                h = (guint) gst_util_uint64_scale_int (w, den, num);
            }
            else
            {
                GST_DEBUG_OBJECT (base, "approximating but keeping video height");
                h = from_h;
                w = (guint) gst_util_uint64_scale_int (h, num, den);
            }
        }
        GST_DEBUG_OBJECT (base, "scaling to %dx%d", w, h);

        //now fixate
        gst_structure_fixate_field_nearest_int (outs, "width", w);
        gst_structure_fixate_field_nearest_int (outs, "height", h);
    }
    else
    {
        gint width, height;

        if (gst_structure_get_int (ins, "width", &width))
        {
          if (gst_structure_has_field (outs, "width"))
            gst_structure_fixate_field_nearest_int (outs, "width", width);
        }
        if (gst_structure_get_int (ins, "height", &height)) {
          if (gst_structure_has_field (outs, "height")) {
            gst_structure_fixate_field_nearest_int (outs, "height", height);
          }
        }
    }

    GST_DEBUG_OBJECT (base, "fixated othercaps to %" GST_PTR_FORMAT, othercaps);
}

static gboolean
gst_gl_colorscale_set_caps (GstBaseTransform* bt, GstCaps* incaps,
    GstCaps* outcaps)
{
    GstGLColorscale* colorscale = GST_GL_COLORSCALE (bt);
    gboolean ret = FALSE;

    GST_DEBUG ("called with %" GST_PTR_FORMAT, incaps);

    ret = gst_video_format_parse_caps (outcaps, &colorscale->output_video_format,
        &colorscale->output_video_width, &colorscale->output_video_height);

    ret |= gst_video_format_parse_caps (incaps, &colorscale->input_video_format,
        &colorscale->input_video_width, &colorscale->input_video_height);

    if (!ret)
    {
      GST_DEBUG ("bad caps");
      return FALSE;
    }

    colorscale->display = gst_gl_display_new ();

    //init unvisible opengl context
    gst_gl_display_create_context (colorscale->display,
        colorscale->output_video_width, colorscale->output_video_height,
        0);

    //blocking call, init colorspace conversion if needed
    gst_gl_display_init_upload (colorscale->display, colorscale->input_video_format,
        colorscale->output_video_width, colorscale->output_video_height,
        colorscale->input_video_width, colorscale->input_video_height);

    //blocking call, init colorspace conversion if needed
    gst_gl_display_init_download (colorscale->display, colorscale->output_video_format,
        colorscale->output_video_width, colorscale->output_video_height);

    return ret;
}

static gboolean
gst_gl_colorscale_get_unit_size (GstBaseTransform* trans, GstCaps* caps,
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

    return TRUE;
}

static GstFlowReturn
gst_gl_colorscale_transform (GstBaseTransform* trans, GstBuffer* inbuf,
    GstBuffer* outbuf)
{
    GstGLColorscale* colorscale = GST_GL_COLORSCALE (trans);
    gboolean isAlive = TRUE;

    //blocking call
    GstGLBuffer* gl_temp_buffer = gst_gl_buffer_new (colorscale->display,
        colorscale->output_video_width, colorscale->output_video_height);

    GST_DEBUG ("input size %p size %d",
        GST_BUFFER_DATA (inbuf), GST_BUFFER_SIZE (inbuf));

    //blocking call
    isAlive = gst_gl_display_do_upload (colorscale->display, gl_temp_buffer->texture,
        colorscale->input_video_width, colorscale->input_video_height,
        GST_BUFFER_DATA (inbuf));

    GST_DEBUG ("output size %p size %d",
      GST_BUFFER_DATA (outbuf), GST_BUFFER_SIZE (outbuf));

    //blocking call
    isAlive &= gst_gl_display_do_download(colorscale->display, gl_temp_buffer->texture,
        gl_temp_buffer->width, gl_temp_buffer->height,
        GST_BUFFER_DATA (outbuf));

    gst_buffer_unref (gl_temp_buffer);

    if (isAlive)
        return GST_FLOW_OK;
    else
        return GST_FLOW_UNEXPECTED;
}
