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

/* Source pad definition */
static GstStaticPadTemplate gst_gl_colorscale_src_pad_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_GL_VIDEO_CAPS)
    );

/* Source pad definition */
static GstStaticPadTemplate gst_gl_colorscale_sink_pad_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_GL_VIDEO_CAPS)
    );

/* Properties */
enum
{
  PROP_0
};

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_gl_colorscale_debug, "glcolorscale", 0, "glcolorscale element");

G_DEFINE_TYPE_WITH_CODE (GstGLColorscale, gst_gl_colorscale,
    GST_TYPE_BASE_TRANSFORM, DEBUG_INIT);

static void gst_gl_colorscale_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gl_colorscale_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_gl_colorscale_reset (GstGLColorscale * colorscale);
static gboolean gst_gl_colorscale_set_caps (GstBaseTransform * bt,
    GstCaps * incaps, GstCaps * outcaps);
static GstCaps *gst_gl_colorscale_transform_caps (GstBaseTransform * bt,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static GstCaps *gst_gl_colorscale_fixate_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps);
static gboolean gst_gl_colorscale_start (GstBaseTransform * bt);
static gboolean gst_gl_colorscale_stop (GstBaseTransform * bt);
static GstFlowReturn gst_gl_colorscale_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf);
static gboolean gst_gl_colorscale_get_unit_size (GstBaseTransform * trans,
    GstCaps * caps, gsize * size);


static void
gst_gl_colorscale_class_init (GstGLColorscaleClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;

  gobject_class = (GObjectClass *) klass;
  element_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_gl_colorscale_set_property;
  gobject_class->get_property = gst_gl_colorscale_get_property;

  gst_element_class_set_details_simple (element_class, "OpenGL color scale",
      "Filter/Effect", "Colorspace converter and video scaler",
      "Julien Isorce <julien.isorce@gmail.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_gl_colorscale_src_pad_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_gl_colorscale_sink_pad_template));

  GST_BASE_TRANSFORM_CLASS (klass)->transform_caps =
      gst_gl_colorscale_transform_caps;
  GST_BASE_TRANSFORM_CLASS (klass)->fixate_caps = gst_gl_colorscale_fixate_caps;
  GST_BASE_TRANSFORM_CLASS (klass)->transform = gst_gl_colorscale_transform;
  GST_BASE_TRANSFORM_CLASS (klass)->start = gst_gl_colorscale_start;
  GST_BASE_TRANSFORM_CLASS (klass)->stop = gst_gl_colorscale_stop;
  GST_BASE_TRANSFORM_CLASS (klass)->set_caps = gst_gl_colorscale_set_caps;
  GST_BASE_TRANSFORM_CLASS (klass)->get_unit_size =
      gst_gl_colorscale_get_unit_size;
}

static void
gst_gl_colorscale_init (GstGLColorscale * colorscale)
{
  gst_gl_colorscale_reset (colorscale);
}

static void
gst_gl_colorscale_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  //GstGLColorscale* colorscale = GST_GL_COLORSCALE (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gl_colorscale_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  //GstGLColorscale *colorscale = GST_GL_COLORSCALE (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gl_colorscale_reset (GstGLColorscale * colorscale)
{
  if (colorscale->display) {
    g_object_unref (colorscale->display);
    colorscale->display = NULL;
  }
}

static gboolean
gst_gl_colorscale_start (GstBaseTransform * bt)
{
  //GstGLColorscale* colorscale = GST_GL_COLORSCALE (bt);

  return TRUE;
}

static gboolean
gst_gl_colorscale_stop (GstBaseTransform * bt)
{
  GstGLColorscale *colorscale = GST_GL_COLORSCALE (bt);

  gst_gl_colorscale_reset (colorscale);

  return TRUE;
}

static GstCaps *
gst_gl_colorscale_transform_caps (GstBaseTransform * bt,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  //GstGLColorscale* colorscale = GST_GL_COLORSCALE (bt);
  GstStructure *structure;
  GstCaps *newcaps;
  const GValue *framerate_value;
  const GValue *par_value;

  GST_DEBUG ("transform caps %" GST_PTR_FORMAT, caps);

  structure = gst_caps_get_structure (caps, 0);
  newcaps = gst_caps_new_empty_simple ("video/x-raw");

  framerate_value = gst_structure_get_value (structure, "framerate");
  par_value = gst_structure_get_value (structure, "pixel-aspect-ratio");

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

  newcaps = gst_caps_merge_structure (newcaps, gst_structure_copy (structure));

  GST_DEBUG ("new caps %" GST_PTR_FORMAT, newcaps);

  return newcaps;
}

/* from gst-plugins-base "videoscale" code */
static GstCaps *
gst_gl_colorscale_fixate_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
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

  /* If we're fixating from the sinkpad we always set the PAR and
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
gst_gl_colorscale_set_caps (GstBaseTransform * bt, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstGLColorscale *colorscale;
  GstVideoInfo in_vinfo, out_vinfo;
  gboolean ret;

  GST_DEBUG ("called with %" GST_PTR_FORMAT, incaps);

  ret = FALSE;
  colorscale = GST_GL_COLORSCALE (bt);

  ret = gst_video_info_from_caps (&out_vinfo, outcaps);
  ret |= gst_video_info_from_caps (&in_vinfo, incaps);

  if (!ret) {
    GST_DEBUG ("bad caps");
    return FALSE;
  }

  colorscale->output_video_format = GST_VIDEO_INFO_FORMAT (&out_vinfo);
  colorscale->output_video_width = GST_VIDEO_INFO_WIDTH (&out_vinfo);
  colorscale->output_video_height = GST_VIDEO_INFO_HEIGHT (&out_vinfo);

  colorscale->input_video_format = GST_VIDEO_INFO_FORMAT (&in_vinfo);
  colorscale->input_video_width = GST_VIDEO_INFO_WIDTH (&in_vinfo);
  colorscale->input_video_height = GST_VIDEO_INFO_HEIGHT (&in_vinfo);

  colorscale->display = gst_gl_display_new ();

  //init unvisible opengl context
  ret = gst_gl_display_create_context (colorscale->display, 0);

  if (!ret) {
    GST_ELEMENT_ERROR (colorscale, RESOURCE, NOT_FOUND,
        GST_GL_DISPLAY_ERR_MSG (colorscale->display), (NULL));
    return FALSE;
  }
  //blocking call, init colorspace conversion if needed
  ret = gst_gl_display_init_upload (colorscale->display,
      colorscale->input_video_format, colorscale->output_video_width,
      colorscale->output_video_height, colorscale->input_video_width,
      colorscale->input_video_height);

  if (!ret) {
    GST_ELEMENT_ERROR (colorscale, RESOURCE, NOT_FOUND,
        GST_GL_DISPLAY_ERR_MSG (colorscale->display), (NULL));
    return FALSE;
  }
  //blocking call, init colorspace conversion if needed
  ret = gst_gl_display_init_download (colorscale->display,
      colorscale->output_video_format, colorscale->output_video_width,
      colorscale->output_video_height);

  if (!ret)
    GST_ELEMENT_ERROR (colorscale, RESOURCE, NOT_FOUND,
        GST_GL_DISPLAY_ERR_MSG (colorscale->display), (NULL));

  return ret;
}

static gboolean
gst_gl_colorscale_get_unit_size (GstBaseTransform * trans, GstCaps * caps,
    gsize * size)
{
  GstVideoInfo vinfo;

  if (gst_video_info_from_caps (&vinfo, caps)) {
    *size = GST_VIDEO_INFO_SIZE (&vinfo);
  }

  return TRUE;
}

static GstFlowReturn
gst_gl_colorscale_transform (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  /*GstGLColorscale *colorscale; */
  gboolean ok;

  /*colorscale = GST_GL_COLORSCALE (trans); */
  ok = TRUE;

  /* FIXME: implement using GstGLMeta */
  /*//blocking call
     GstGLBuffer *gl_temp_buffer = gst_gl_buffer_new (colorscale->display,
     colorscale->output_video_width, colorscale->output_video_height);

     GST_DEBUG ("input size %p size %d",
     GST_BUFFER_DATA (inbuf), GST_BUFFER_SIZE (inbuf));

     //blocking call
     ok =
     gst_gl_display_do_upload (colorscale->display, gl_temp_buffer->texture,
     colorscale->input_video_width, colorscale->input_video_height,
     GST_BUFFER_DATA (inbuf));

     GST_DEBUG ("output size %p size %d",
     GST_BUFFER_DATA (outbuf), GST_BUFFER_SIZE (outbuf));

     //blocking call
     ok &=
     gst_gl_display_do_download (colorscale->display, gl_temp_buffer->texture,
     gl_temp_buffer->width, gl_temp_buffer->height, GST_BUFFER_DATA (outbuf));

     gst_buffer_unref (GST_BUFFER_CAST (gl_temp_buffer));
   */

  if (ok)
    return GST_FLOW_OK;
  else
    return GST_FLOW_EOS;
}
