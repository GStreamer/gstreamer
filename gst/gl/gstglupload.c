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
 * SECTION:element-glupload
 *
 * upload video frames video frames into opengl textures.
 *
 * <refsect2>
 * <title>Color space conversion</title>
 * <para>
 * Depends on the driver and when needed, the color space conversion is made
 * in a fragment shader using one frame buffer object instance, or using
 * mesa ycbcr .
 * </para>
 * </refsect2>
 * <refsect2>
 * <title>Examples</title>
 * |[
 * gst-launch -v videotestsrc ! "video/x-raw-rgb" ! glupload ! glimagesink
 * ]| A pipeline to test hardware scaling.
 * No special opengl extension is used in this pipeline, that's why it should work
 * with OpenGL >= 1.1. That's the case if you are using the MESA3D driver v1.3.
 * |[
 * gst-launch -v videotestsrc ! "video/x-raw-yuv, format=(fourcc)I420" ! glupload ! glimagesink
 * ]| A pipeline to test hardware scaling and hardware colorspace conversion.
 * When your driver supports GLSL (OpenGL Shading Language needs OpenGL >= 2.1),
 * the 4 following format YUY2, UYVY, I420, YV12 and AYUV are converted to RGB32
 * through some fragment shaders and using one framebuffer (FBO extension OpenGL >= 1.4).
 * If your driver does not support GLSL but supports MESA_YCbCr extension then
 * the you can use YUY2 and UYVY. In this case the colorspace conversion is automatically
 * made when loading the texture and therefore no framebuffer is used.
 * |[
 * gst-launch -v videotestsrc ! "video/x-raw-rgb, width=320, height=240" ! glupload ! \
 *    "video/x-raw-gl, width=640, height=480" ! glimagesink
 * ]| A pipeline to test hardware scaling.
 * Frame buffer extension is required. Inded one FBO is used bettween glupload and glimagesink,
 * because the texture needs to be resized.
 * |[
 * gst-launch -v videotestsrc ! "video/x-raw-yuv, width=320, height=240" ! glupload ! \
 *    "video/x-raw-gl, width=640, height=480" ! glimagesink
 * ]| A pipeline to test hardware scaling.
 * Frame buffer extension is required. Inded one FBO is used bettween glupload and glimagesink,
 * because the texture needs to be resized. Depends on your driver the color space conversion
 * is made in a fragment shader using one frame buffer object instance, or using mesa ycbcr .
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstglupload.h"


#define GST_CAT_DEFAULT gst_gl_upload_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

/* Source pad definition */
static GstStaticPadTemplate gst_gl_upload_src_pad_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_GL_VIDEO_CAPS)
    );

/* Sink pad definition */
static GstStaticPadTemplate gst_gl_upload_sink_pad_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_GL_VIDEO_CAPS)
    );

/* Properties */
enum
{
  PROP_0,
  PROP_EXTERNAL_OPENGL_CONTEXT
};

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_gl_upload_debug, "glupload", 0, "glupload element");

G_DEFINE_TYPE_WITH_CODE (GstGLUpload, gst_gl_upload, GST_TYPE_BASE_TRANSFORM,
    DEBUG_INIT);

static void gst_gl_upload_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gl_upload_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_gl_upload_src_query (GstPad * pad, GstObject * object,
    GstQuery * query);

static void gst_gl_upload_reset (GstGLUpload * upload);
static gboolean gst_gl_upload_set_caps (GstBaseTransform * bt,
    GstCaps * incaps, GstCaps * outcaps);
static GstCaps *gst_gl_upload_transform_caps (GstBaseTransform * bt,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static GstCaps *gst_gl_upload_fixate_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps);
static gboolean gst_gl_upload_start (GstBaseTransform * bt);
static gboolean gst_gl_upload_stop (GstBaseTransform * bt);
static GstFlowReturn gst_gl_upload_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf);
static gboolean gst_gl_upload_get_unit_size (GstBaseTransform * trans,
    GstCaps * caps, gsize * size);
static gboolean gst_gl_upload_decide_allocation (GstBaseTransform * trans,
    GstQuery * query);
static gboolean gst_gl_upload_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_allocation, GstQuery * query);

static void
gst_gl_upload_class_init (GstGLUploadClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;

  gobject_class = (GObjectClass *) klass;
  element_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_gl_upload_set_property;
  gobject_class->get_property = gst_gl_upload_get_property;

  GST_BASE_TRANSFORM_CLASS (klass)->transform_caps =
      gst_gl_upload_transform_caps;
  GST_BASE_TRANSFORM_CLASS (klass)->fixate_caps = gst_gl_upload_fixate_caps;
  GST_BASE_TRANSFORM_CLASS (klass)->transform = gst_gl_upload_transform;
  GST_BASE_TRANSFORM_CLASS (klass)->start = gst_gl_upload_start;
  GST_BASE_TRANSFORM_CLASS (klass)->stop = gst_gl_upload_stop;
  GST_BASE_TRANSFORM_CLASS (klass)->set_caps = gst_gl_upload_set_caps;
  GST_BASE_TRANSFORM_CLASS (klass)->get_unit_size = gst_gl_upload_get_unit_size;
  GST_BASE_TRANSFORM_CLASS (klass)->decide_allocation =
      gst_gl_upload_decide_allocation;
  GST_BASE_TRANSFORM_CLASS (klass)->propose_allocation =
      gst_gl_upload_propose_allocation;

  g_object_class_install_property (gobject_class, PROP_EXTERNAL_OPENGL_CONTEXT,
      g_param_spec_ulong ("external-opengl-context",
          "External OpenGL context",
          "Give an external OpenGL context with which to share textures",
          0, G_MAXULONG, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_details_simple (element_class, "OpenGL upload",
      "Filter/Effect", "A from video to GL flow filter",
      "Julien Isorce <julien.isorce@gmail.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_gl_upload_src_pad_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_gl_upload_sink_pad_template));
}

static void
gst_gl_upload_init (GstGLUpload * upload)
{
  GstBaseTransform *base_trans = GST_BASE_TRANSFORM (upload);

  gst_pad_set_query_function (base_trans->srcpad,
      GST_DEBUG_FUNCPTR (gst_gl_upload_src_query));

  gst_gl_upload_reset (upload);
}

static void
gst_gl_upload_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGLUpload *upload = GST_GL_UPLOAD (object);

  switch (prop_id) {
    case PROP_EXTERNAL_OPENGL_CONTEXT:
    {
      upload->external_gl_context = g_value_get_ulong (value);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gl_upload_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  //GstGLUpload *upload = GST_GL_UPLOAD (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_gl_upload_src_query (GstPad * pad, GstObject * object, GstQuery * query)
{
  gboolean res;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CUSTOM:
    {
      const GstStructure *structure = gst_query_get_structure (query);
      res =
          g_strcmp0 (gst_element_get_name (object),
          gst_structure_get_name (structure)) == 0;
      if (!res)
        res = gst_pad_query_default (pad, object, query);
      break;
    }
    default:
      res = gst_pad_query_default (pad, object, query);
      break;
  }

  return res;
}

static void
gst_gl_upload_reset (GstGLUpload * upload)
{
  if (upload->display) {
    g_object_unref (upload->display);
    upload->display = NULL;
  }
  upload->external_gl_context = 0;
}

static gboolean
gst_gl_upload_start (GstBaseTransform * bt)
{
  GstGLUpload *upload = GST_GL_UPLOAD (bt);
  GstElement *parent = GST_ELEMENT (gst_element_get_parent (upload));
  GstStructure *structure = NULL;
  GstQuery *query = NULL;
  gboolean isPerformed = FALSE;

  if (!parent) {
    GST_ELEMENT_ERROR (upload, CORE, STATE_CHANGE, (NULL),
        ("A parent bin is required"));
    return FALSE;
  }

  structure = gst_structure_new_empty (gst_element_get_name (upload));
  query = gst_query_new_custom (GST_QUERY_CUSTOM, structure);

  isPerformed = gst_element_query (parent, query);

  if (isPerformed) {
    const GValue *id_value =
        gst_structure_get_value (structure, "gstgldisplay");
    if (G_VALUE_HOLDS_POINTER (id_value))
      /* at least one gl element is after in our gl chain */
      upload->display =
          g_object_ref (GST_GL_DISPLAY (g_value_get_pointer (id_value)));
    else {
      /* this gl filter is a sink in terms of the gl chain */
      upload->display = gst_gl_display_new ();
      isPerformed = gst_gl_display_create_context (upload->display,
          upload->external_gl_context);

      if (!isPerformed)
        GST_ELEMENT_ERROR (upload, RESOURCE, NOT_FOUND,
            GST_GL_DISPLAY_ERR_MSG (upload->display), (NULL));
    }
  }

  gst_query_unref (query);
  gst_object_unref (GST_OBJECT (parent));

  return isPerformed;
}

static gboolean
gst_gl_upload_stop (GstBaseTransform * bt)
{
  GstGLUpload *upload = GST_GL_UPLOAD (bt);

  gst_gl_upload_reset (upload);

  return TRUE;
}

static GstCaps *
gst_gl_upload_transform_caps (GstBaseTransform * bt,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  //GstGLUpload* upload = GST_GL_UPLOAD (bt);
  GstCaps *newcaps;

  GST_DEBUG ("transform caps %" GST_PTR_FORMAT, caps);

  if (filter)
    newcaps = gst_caps_intersect (caps, filter);
  else
    newcaps = gst_caps_ref (caps);

  GST_DEBUG ("new caps %" GST_PTR_FORMAT, newcaps);

  return newcaps;
}

/* from gst-plugins-base "videoscale" code */
static GstCaps *
gst_gl_upload_fixate_caps (GstBaseTransform * base, GstPadDirection direction,
    GstCaps * caps, GstCaps * othercaps)
{
  GstStructure *ins, *outs;
  const GValue *from_par, *to_par;
  GValue fpar = { 0, }, tpar = {
  0,};

  othercaps = gst_caps_truncate (othercaps);
  othercaps = gst_caps_make_writable (othercaps);

  GST_DEBUG_OBJECT (base, "trying to fixate othercaps %" GST_PTR_FORMAT
      " based on caps %" GST_PTR_FORMAT, othercaps, caps);

  ins = gst_caps_get_structure (caps, 0);
  outs = gst_caps_get_structure (othercaps, 0);

  from_par = gst_structure_get_value (ins, "pixel-aspect-ratio");
  to_par = gst_structure_get_value (outs, "pixel-aspect-ratio");

  /* If we're fixating from the sinkpad we always set the PAR andcould not cop
   * assume that missing PAR on the sinkpad means 1/1 and
   * missing PAR on the srcpad means undefined
   */
  if (direction == GST_PAD_SINK) {
    if (!from_par) {
      g_value_init (&fpar, GST_TYPE_FRACTION);
      gst_value_set_fraction (&fpar, 1, 1);
      from_par = &fpar;
    }
    if (!to_par) {
      g_value_init (&tpar, GST_TYPE_FRACTION_RANGE);
      gst_value_set_fraction_range_full (&tpar, 1, G_MAXINT, G_MAXINT, 1);
      to_par = &tpar;
    }
  } else {
    if (!to_par) {
      g_value_init (&tpar, GST_TYPE_FRACTION);
      gst_value_set_fraction (&tpar, 1, 1);
      to_par = &tpar;

      gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1,
          NULL);
    }
    if (!from_par) {
      g_value_init (&fpar, GST_TYPE_FRACTION);
      gst_value_set_fraction (&fpar, 1, 1);
      from_par = &fpar;
    }
  }

  /* we have both PAR but they might not be fixated */
  {
    gint from_w, from_h, from_par_n, from_par_d, to_par_n, to_par_d;
    gint w = 0, h = 0;
    gint from_dar_n, from_dar_d;
    gint num, den;

    /* from_par should be fixed */
    g_return_val_if_fail (gst_value_is_fixed (from_par), othercaps);

    from_par_n = gst_value_get_fraction_numerator (from_par);
    from_par_d = gst_value_get_fraction_denominator (from_par);

    gst_structure_get_int (ins, "width", &from_w);
    gst_structure_get_int (ins, "height", &from_h);

    gst_structure_get_int (outs, "width", &w);
    gst_structure_get_int (outs, "height", &h);

    /* if both width and height are already fixed, we can't do anything
     * about it anymore */
    if (w && h) {
      guint n, d;

      GST_DEBUG_OBJECT (base, "dimensions already set to %dx%d, not fixating",
          w, h);
      if (!gst_value_is_fixed (to_par)) {
        if (gst_video_calculate_display_ratio (&n, &d, from_w, from_h,
                from_par_n, from_par_d, w, h)) {
          GST_DEBUG_OBJECT (base, "fixating to_par to %dx%d", n, d);
          if (gst_structure_has_field (outs, "pixel-aspect-ratio"))
            gst_structure_fixate_field_nearest_fraction (outs,
                "pixel-aspect-ratio", n, d);
          else if (n != d)
            gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
                n, d, NULL);
        }
      }
      goto done;
    }

    /* Calculate input DAR */
    if (!gst_util_fraction_multiply (from_w, from_h, from_par_n, from_par_d,
            &from_dar_n, &from_dar_d)) {
      GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (NULL),
          ("Error calculating the output scaled size - integer overflow"));
      goto done;
    }

    GST_DEBUG_OBJECT (base, "Input DAR is %d/%d", from_dar_n, from_dar_d);

    /* If either width or height are fixed there's not much we
     * can do either except choosing a height or width and PAR
     * that matches the DAR as good as possible
     */
    if (h) {
      GstStructure *tmp;
      gint set_w, set_par_n, set_par_d;

      GST_DEBUG_OBJECT (base, "height is fixed (%d)", h);

      /* If the PAR is fixed too, there's not much to do
       * except choosing the width that is nearest to the
       * width with the same DAR */
      if (gst_value_is_fixed (to_par)) {
        to_par_n = gst_value_get_fraction_numerator (to_par);
        to_par_d = gst_value_get_fraction_denominator (to_par);

        GST_DEBUG_OBJECT (base, "PAR is fixed %d/%d", to_par_n, to_par_d);

        if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, to_par_d,
                to_par_n, &num, &den)) {
          GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (NULL),
              ("Error calculating the output scaled size - integer overflow"));
          goto done;
        }

        w = (guint) gst_util_uint64_scale_int (h, num, den);
        gst_structure_fixate_field_nearest_int (outs, "width", w);

        goto done;
      }

      /* The PAR is not fixed and it's quite likely that we can set
       * an arbitrary PAR. */

      /* Check if we can keep the input width */
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "width", from_w);
      gst_structure_get_int (tmp, "width", &set_w);

      /* Might have failed but try to keep the DAR nonetheless by
       * adjusting the PAR */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, h, set_w,
              &to_par_n, &to_par_d)) {
        GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scaled size - integer overflow"));
        gst_structure_free (tmp);
        goto done;
      }

      if (!gst_structure_has_field (tmp, "pixel-aspect-ratio"))
        gst_structure_set_value (tmp, "pixel-aspect-ratio", to_par);
      gst_structure_fixate_field_nearest_fraction (tmp, "pixel-aspect-ratio",
          to_par_n, to_par_d);
      gst_structure_get_fraction (tmp, "pixel-aspect-ratio", &set_par_n,
          &set_par_d);
      gst_structure_free (tmp);

      /* Check if the adjusted PAR is accepted */
      if (set_par_n == to_par_n && set_par_d == to_par_d) {
        if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
            set_par_n != set_par_d)
          gst_structure_set (outs, "width", G_TYPE_INT, set_w,
              "pixel-aspect-ratio", GST_TYPE_FRACTION, set_par_n, set_par_d,
              NULL);
        goto done;
      }

      /* Otherwise scale the width to the new PAR and check if the
       * adjusted with is accepted. If all that fails we can't keep
       * the DAR */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, set_par_d,
              set_par_n, &num, &den)) {
        GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scaled size - integer overflow"));
        goto done;
      }

      w = (guint) gst_util_uint64_scale_int (h, num, den);
      gst_structure_fixate_field_nearest_int (outs, "width", w);
      if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
          set_par_n != set_par_d)
        gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
            set_par_n, set_par_d, NULL);

      goto done;
    } else if (w) {
      GstStructure *tmp;
      gint set_h, set_par_n, set_par_d;

      GST_DEBUG_OBJECT (base, "width is fixed (%d)", w);

      /* If the PAR is fixed too, there's not much to do
       * except choosing the height that is nearest to the
       * height with the same DAR */
      if (gst_value_is_fixed (to_par)) {
        to_par_n = gst_value_get_fraction_numerator (to_par);
        to_par_d = gst_value_get_fraction_denominator (to_par);

        GST_DEBUG_OBJECT (base, "PAR is fixed %d/%d", to_par_n, to_par_d);

        if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, to_par_d,
                to_par_n, &num, &den)) {
          GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (NULL),
              ("Error calculating the output scaled size - integer overflow"));
          goto done;
        }

        h = (guint) gst_util_uint64_scale_int (w, den, num);
        gst_structure_fixate_field_nearest_int (outs, "height", h);

        goto done;
      }

      /* The PAR is not fixed and it's quite likely that we can set
       * an arbitrary PAR. */

      /* Check if we can keep the input height */
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "height", from_h);
      gst_structure_get_int (tmp, "height", &set_h);

      /* Might have failed but try to keep the DAR nonetheless by
       * adjusting the PAR */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, set_h, w,
              &to_par_n, &to_par_d)) {
        GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scaled size - integer overflow"));
        gst_structure_free (tmp);
        goto done;
      }
      if (!gst_structure_has_field (tmp, "pixel-aspect-ratio"))
        gst_structure_set_value (tmp, "pixel-aspect-ratio", to_par);
      gst_structure_fixate_field_nearest_fraction (tmp, "pixel-aspect-ratio",
          to_par_n, to_par_d);
      gst_structure_get_fraction (tmp, "pixel-aspect-ratio", &set_par_n,
          &set_par_d);
      gst_structure_free (tmp);

      /* Check if the adjusted PAR is accepted */
      if (set_par_n == to_par_n && set_par_d == to_par_d) {
        if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
            set_par_n != set_par_d)
          gst_structure_set (outs, "height", G_TYPE_INT, set_h,
              "pixel-aspect-ratio", GST_TYPE_FRACTION, set_par_n, set_par_d,
              NULL);
        goto done;
      }

      /* Otherwise scale the height to the new PAR and check if the
       * adjusted with is accepted. If all that fails we can't keep
       * the DAR */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, set_par_d,
              set_par_n, &num, &den)) {
        GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scaled size - integer overflow"));
        goto done;
      }

      h = (guint) gst_util_uint64_scale_int (w, den, num);
      gst_structure_fixate_field_nearest_int (outs, "height", h);
      if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
          set_par_n != set_par_d)
        gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
            set_par_n, set_par_d, NULL);

      goto done;
    } else if (gst_value_is_fixed (to_par)) {
      GstStructure *tmp;
      gint set_h, set_w, f_h, f_w;

      to_par_n = gst_value_get_fraction_numerator (to_par);
      to_par_d = gst_value_get_fraction_denominator (to_par);

      /* Calculate scale factor for the PAR change */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, to_par_n,
              to_par_d, &num, &den)) {
        GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scaled size - integer overflow"));
        goto done;
      }

      /* Try to keep the input height (because of interlacing) */
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "height", from_h);
      gst_structure_get_int (tmp, "height", &set_h);

      /* This might have failed but try to scale the width
       * to keep the DAR nonetheless */
      w = (guint) gst_util_uint64_scale_int (set_h, num, den);
      gst_structure_fixate_field_nearest_int (tmp, "width", w);
      gst_structure_get_int (tmp, "width", &set_w);
      gst_structure_free (tmp);

      /* We kept the DAR and the height is nearest to the original height */
      if (set_w == w) {
        gst_structure_set (outs, "width", G_TYPE_INT, set_w, "height",
            G_TYPE_INT, set_h, NULL);
        goto done;
      }

      f_h = set_h;
      f_w = set_w;

      /* If the former failed, try to keep the input width at least */
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "width", from_w);
      gst_structure_get_int (tmp, "width", &set_w);

      /* This might have failed but try to scale the width
       * to keep the DAR nonetheless */
      h = (guint) gst_util_uint64_scale_int (set_w, den, num);
      gst_structure_fixate_field_nearest_int (tmp, "height", h);
      gst_structure_get_int (tmp, "height", &set_h);
      gst_structure_free (tmp);

      /* We kept the DAR and the width is nearest to the original width */
      if (set_h == h) {
        gst_structure_set (outs, "width", G_TYPE_INT, set_w, "height",
            G_TYPE_INT, set_h, NULL);
        goto done;
      }

      /* If all this failed, keep the height that was nearest to the orignal
       * height and the nearest possible width. This changes the DAR but
       * there's not much else to do here.
       */
      gst_structure_set (outs, "width", G_TYPE_INT, f_w, "height", G_TYPE_INT,
          f_h, NULL);
      goto done;
    } else {
      GstStructure *tmp;
      gint set_h, set_w, set_par_n, set_par_d, tmp2;

      /* width, height and PAR are not fixed but passthrough is not possible */

      /* First try to keep the height and width as good as possible
       * and scale PAR */
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "height", from_h);
      gst_structure_get_int (tmp, "height", &set_h);
      gst_structure_fixate_field_nearest_int (tmp, "width", from_w);
      gst_structure_get_int (tmp, "width", &set_w);

      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, set_h, set_w,
              &to_par_n, &to_par_d)) {
        GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scaled size - integer overflow"));
        goto done;
      }

      if (!gst_structure_has_field (tmp, "pixel-aspect-ratio"))
        gst_structure_set_value (tmp, "pixel-aspect-ratio", to_par);
      gst_structure_fixate_field_nearest_fraction (tmp, "pixel-aspect-ratio",
          to_par_n, to_par_d);
      gst_structure_get_fraction (tmp, "pixel-aspect-ratio", &set_par_n,
          &set_par_d);
      gst_structure_free (tmp);

      if (set_par_n == to_par_n && set_par_d == to_par_d) {
        gst_structure_set (outs, "width", G_TYPE_INT, set_w, "height",
            G_TYPE_INT, set_h, NULL);

        if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
            set_par_n != set_par_d)
          gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
              set_par_n, set_par_d, NULL);
        goto done;
      }

      /* Otherwise try to scale width to keep the DAR with the set
       * PAR and height */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, set_par_d,
              set_par_n, &num, &den)) {
        GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scaled size - integer overflow"));
        goto done;
      }

      w = (guint) gst_util_uint64_scale_int (set_h, num, den);
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "width", w);
      gst_structure_get_int (tmp, "width", &tmp2);
      gst_structure_free (tmp);

      if (tmp2 == w) {
        gst_structure_set (outs, "width", G_TYPE_INT, tmp2, "height",
            G_TYPE_INT, set_h, NULL);
        if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
            set_par_n != set_par_d)
          gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
              set_par_n, set_par_d, NULL);
        goto done;
      }

      /* ... or try the same with the height */
      h = (guint) gst_util_uint64_scale_int (set_w, den, num);
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "height", h);
      gst_structure_get_int (tmp, "height", &tmp2);
      gst_structure_free (tmp);

      if (tmp2 == h) {
        gst_structure_set (outs, "width", G_TYPE_INT, set_w, "height",
            G_TYPE_INT, tmp2, NULL);
        if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
            set_par_n != set_par_d)
          gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
              set_par_n, set_par_d, NULL);
        goto done;
      }

      /* If all fails we can't keep the DAR and take the nearest values
       * for everything from the first try */
      gst_structure_set (outs, "width", G_TYPE_INT, set_w, "height",
          G_TYPE_INT, set_h, NULL);
      if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
          set_par_n != set_par_d)
        gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
            set_par_n, set_par_d, NULL);
    }
  }

done:
  GST_DEBUG_OBJECT (base, "fixated othercaps to %" GST_PTR_FORMAT, othercaps);

  if (from_par == &fpar)
    g_value_unset (&fpar);
  if (to_par == &tpar)
    g_value_unset (&tpar);

  return othercaps;
}

static gboolean
gst_gl_upload_set_caps (GstBaseTransform * bt, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstGLUpload *upload;
  gboolean ret;
  GstVideoInfo in_vinfo, out_vinfo;

  GST_DEBUG ("called with %" GST_PTR_FORMAT, incaps);

  upload = GST_GL_UPLOAD (bt);

  ret = gst_video_info_from_caps (&in_vinfo, incaps);
  ret |= gst_video_info_from_caps (&out_vinfo, outcaps);

  if (!ret) {
    GST_DEBUG ("caps connot be parsed");
    return FALSE;
  }

  upload->in_info = in_vinfo;
  upload->out_info = out_vinfo;

  //init colorspace conversion if needed
  ret = gst_gl_display_init_upload (upload->display,
      GST_VIDEO_INFO_FORMAT (&upload->in_info),
      GST_VIDEO_INFO_WIDTH (&upload->out_info),
      GST_VIDEO_INFO_HEIGHT (&upload->out_info),
      GST_VIDEO_INFO_WIDTH (&upload->in_info),
      GST_VIDEO_INFO_HEIGHT (&upload->in_info));

  if (!ret)
    GST_ELEMENT_ERROR (upload, RESOURCE, NOT_FOUND,
        GST_GL_DISPLAY_ERR_MSG (upload->display), (NULL));

  return ret;
}

static gboolean
gst_gl_upload_get_unit_size (GstBaseTransform * trans, GstCaps * caps,
    gsize * size)
{
  gboolean ret;
  GstVideoInfo vinfo;

  ret = gst_video_info_from_caps (&vinfo, caps);
  if (ret)
    *size = GST_VIDEO_INFO_SIZE (&vinfo);

  return TRUE;
}

static GstFlowReturn
gst_gl_upload_transform (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstGLUpload *upload = GST_GL_UPLOAD (trans);
  GstVideoMeta *smeta, *dmeta;
  GstGLMeta *gl_meta;
  GstVideoFrame frame;

  smeta = gst_buffer_get_video_meta (inbuf);
  dmeta = gst_buffer_get_video_meta (outbuf);
  gl_meta = gst_buffer_get_gl_meta (outbuf);

  if (!smeta) {
    GST_ERROR ("Input buffer does not have required GstVideoMeta");
    goto error;
  }
  if (!dmeta || !gl_meta) {
    GST_ERROR
        ("Output buffer does not have required GstVideoMeta or GstGLMeta");
    goto error;
  }

  if (!gst_video_frame_map (&frame, &upload->in_info, inbuf, GST_MAP_READ)) {
    GST_WARNING ("Could not map data for reading");
    goto error;
  }

  if (!gst_gl_display_do_upload (upload->display, gl_meta->memory->tex_id,
          &frame)) {
    GST_WARNING ("Failed to upload data");
  }

  gst_video_frame_unmap (&frame);

  return GST_FLOW_OK;

/* ERRORS */
error:
  {
    return GST_FLOW_ERROR;
  }
}

static gboolean
gst_gl_upload_decide_allocation (GstBaseTransform * trans, GstQuery * query)
{
  GstGLUpload *upload = GST_GL_UPLOAD (trans);
  GstBufferPool *pool = NULL;
  GstStructure *config;
  GstCaps *caps;
  guint min, max, size;
  gboolean update_pool;

  gst_query_parse_allocation (query, &caps, NULL);

  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);

    update_pool = TRUE;
  } else {
    GstVideoInfo vinfo;

    gst_video_info_init (&vinfo);
    gst_video_info_from_caps (&vinfo, caps);
    size = vinfo.size;
    min = max = 0;
    update_pool = FALSE;
  }

  if (!pool)
    pool = gst_gl_buffer_pool_new (upload->display);

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, caps, size, min, max);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_GL_META);
  gst_buffer_pool_set_config (pool, config);

  if (update_pool)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);

  gst_object_unref (pool);

  return TRUE;
}

static gboolean
gst_gl_upload_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query)
{
  GstGLUpload *upload = GST_GL_UPLOAD (trans);
  GstBufferPool *pool;
  GstStructure *config;
  GstCaps *caps;
  guint size;
  gboolean need_pool;

  gst_query_parse_allocation (query, &caps, &need_pool);

  if (caps == NULL)
    goto no_caps;

  if ((pool = upload->pool))
    gst_object_ref (pool);

  if (pool != NULL) {
    GstCaps *pcaps;

    /* we had a pool, check caps */
    GST_DEBUG_OBJECT (upload, "check existing pool caps");
    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_get_params (config, &pcaps, &size, NULL, NULL);

    if (!gst_caps_is_equal (caps, pcaps)) {
      GST_DEBUG_OBJECT (upload, "pool has different caps");
      /* different caps, we can't use this pool */
      gst_object_unref (pool);
      pool = NULL;
    }
    gst_structure_free (config);
  }
  if (pool == NULL && need_pool) {
    GstVideoInfo info;

    if (!gst_video_info_from_caps (&info, caps))
      goto invalid_caps;

    GST_DEBUG_OBJECT (upload, "create new pool");
    pool = gst_video_buffer_pool_new ();

    /* the normal size of a frame */
    size = info.size;

    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_set_params (config, caps, size, 0, 0);
    if (!gst_buffer_pool_set_config (pool, config))
      goto config_failed;
  }
  /* we need at least 2 buffer because we hold on to the last one */
  gst_query_add_allocation_pool (query, pool, size, 2, 0);

  /* we also support various metadata */
  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE);
  //gst_query_add_allocation_meta (query, GST_VIDEO_CROP_META_API_TYPE);
  gst_query_add_allocation_meta (query, GST_GL_META_API_TYPE);

  gst_object_unref (pool);

  return TRUE;

  /* ERRORS */
no_caps:
  {
    GST_DEBUG_OBJECT (trans, "no caps specified");
    return FALSE;
  }
invalid_caps:
  {
    GST_DEBUG_OBJECT (trans, "invalid caps specified");
    return FALSE;
  }
config_failed:
  {
    GST_DEBUG_OBJECT (trans, "failed setting config");
    return FALSE;
  }
}
