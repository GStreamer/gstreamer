/*
 * GStreamer
 * Copyright (C) 2007 David Schleef <ds@schleef.org>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
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
    GST_STATIC_CAPS (GST_GL_UPLOAD_VIDEO_CAPS)
    );

static GstStaticPadTemplate gst_gl_filter_sink_pad_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_GL_DOWNLOAD_VIDEO_CAPS)
    );

/* Properties */
enum
{
  PROP_0,
  PROP_EXTERNAL_OPENGL_CONTEXT
};

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_gl_filter_debug, "glfilter", 0, "glfilter element");
#define gst_gl_filter_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstGLFilter, gst_gl_filter, GST_TYPE_BASE_TRANSFORM,
    DEBUG_INIT);

static void gst_gl_filter_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gl_filter_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_gl_filter_query (GstBaseTransform * trans,
    GstPadDirection direction, GstQuery * query);
static GstCaps *gst_gl_filter_transform_caps (GstBaseTransform * bt,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static GstCaps *gst_gl_filter_fixate_caps (GstBaseTransform * bt,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps);
static void gst_gl_filter_reset (GstGLFilter * filter);
static gboolean gst_gl_filter_start (GstBaseTransform * bt);
static gboolean gst_gl_filter_stop (GstBaseTransform * bt);
static gboolean gst_gl_filter_get_unit_size (GstBaseTransform * trans,
    GstCaps * caps, gsize * size);
static GstFlowReturn gst_gl_filter_transform (GstBaseTransform * bt,
    GstBuffer * inbuf, GstBuffer * outbuf);
static gboolean gst_gl_filter_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query);
static gboolean gst_gl_filter_decide_allocation (GstBaseTransform * trans,
    GstQuery * query);
static gboolean gst_gl_filter_set_caps (GstBaseTransform * bt, GstCaps * incaps,
    GstCaps * outcaps);

/* GstGLDisplayThreadFunc */
static void gst_gl_filter_start_gl (GstGLDisplay * display, gpointer data);
static void gst_gl_filter_stop_gl (GstGLDisplay * display, gpointer data);

static void
gst_gl_filter_class_init (GstGLFilterClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;

  gobject_class = (GObjectClass *) klass;
  element_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_gl_filter_set_property;
  gobject_class->get_property = gst_gl_filter_get_property;

  GST_BASE_TRANSFORM_CLASS (klass)->transform_caps =
      gst_gl_filter_transform_caps;
  GST_BASE_TRANSFORM_CLASS (klass)->fixate_caps = gst_gl_filter_fixate_caps;
  GST_BASE_TRANSFORM_CLASS (klass)->transform = gst_gl_filter_transform;
  GST_BASE_TRANSFORM_CLASS (klass)->query = gst_gl_filter_query;
  GST_BASE_TRANSFORM_CLASS (klass)->start = gst_gl_filter_start;
  GST_BASE_TRANSFORM_CLASS (klass)->stop = gst_gl_filter_stop;
  GST_BASE_TRANSFORM_CLASS (klass)->set_caps = gst_gl_filter_set_caps;
  GST_BASE_TRANSFORM_CLASS (klass)->propose_allocation =
      gst_gl_filter_propose_allocation;
  GST_BASE_TRANSFORM_CLASS (klass)->decide_allocation =
      gst_gl_filter_decide_allocation;
  GST_BASE_TRANSFORM_CLASS (klass)->get_unit_size = gst_gl_filter_get_unit_size;

  g_object_class_install_property (gobject_class, PROP_EXTERNAL_OPENGL_CONTEXT,
      g_param_spec_ulong ("external-opengl-context",
          "External OpenGL context",
          "Give an external OpenGL context with which to share textures",
          0, G_MAXULONG, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_gl_filter_src_pad_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_gl_filter_sink_pad_template));

  klass->set_caps = NULL;
  klass->filter = NULL;
  klass->display_init_cb = NULL;
  klass->display_reset_cb = NULL;
  klass->onInitFBO = NULL;
  klass->onStart = NULL;
  klass->onStop = NULL;
  klass->onReset = NULL;
  klass->filter_texture = NULL;
}

static void
gst_gl_filter_init (GstGLFilter * filter)
{
  gst_gl_filter_reset (filter);
}

static void
gst_gl_filter_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGLFilter *filter = GST_GL_FILTER (object);

  switch (prop_id) {
    case PROP_EXTERNAL_OPENGL_CONTEXT:
    {
      filter->external_gl_context = g_value_get_ulong (value);
      break;
    }
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

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_gl_filter_query (GstBaseTransform * trans, GstPadDirection direction,
    GstQuery * query)
{
  GstGLFilter *filter;
  gboolean res;

  filter = GST_GL_FILTER (trans);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CUSTOM:
    {
      GstStructure *structure = gst_query_writable_structure (query);
      if (direction == GST_PAD_SINK &&
          gst_structure_has_name (structure, "gstgldisplay")) {
        gst_structure_set (structure, "gstgldisplay", G_TYPE_POINTER,
            filter->display, NULL);
        res = TRUE;
      } else
        res =
            GST_BASE_TRANSFORM_CLASS (parent_class)->query (trans, direction,
            query);
      break;
    }
    default:
      res =
          GST_BASE_TRANSFORM_CLASS (parent_class)->query (trans, direction,
          query);
      break;
  }

  return res;
}

static void
gst_gl_filter_reset (GstGLFilter * filter)
{
  GstGLFilterClass *filter_class = GST_GL_FILTER_GET_CLASS (filter);

  if (filter->display) {
    if (filter_class->onReset)
      filter_class->onReset (filter);

    if (filter_class->display_reset_cb != NULL) {
      gst_gl_display_thread_add (filter->display, gst_gl_filter_stop_gl,
          filter);
    }
    //blocking call, delete the FBO
    gst_gl_display_del_fbo (filter->display, filter->fbo, filter->depthbuffer);
    g_object_unref (filter->display);
    filter->display = NULL;
  }

  filter->fbo = 0;
  filter->depthbuffer = 0;
  filter->default_shader = NULL;
  filter->external_gl_context = 0;
}

static gboolean
gst_gl_filter_start (GstBaseTransform * bt)
{
  GstGLFilter *filter = GST_GL_FILTER (bt);
  GstGLFilterClass *filter_class = GST_GL_FILTER_GET_CLASS (filter);
  GstStructure *structure;
  GstQuery *display_query;
  const GValue *id_value;

  structure = gst_structure_new_empty ("gstgldisplay");
  display_query = gst_query_new_custom (GST_QUERY_CUSTOM, structure);

  if (!gst_pad_peer_query (bt->srcpad, display_query)) {
    GST_WARNING
        ("Could not query GstGLDisplay from downstream (peer query failed)");
  }

  id_value = gst_structure_get_value (structure, "gstgldisplay");
  if (G_VALUE_HOLDS_POINTER (id_value))
    /* at least one gl element is after in our gl chain */
    filter->display =
        g_object_ref (GST_GL_DISPLAY (g_value_get_pointer (id_value)));
  else {
    GST_INFO ("Creating GstGLDisplay");
    filter->display = gst_gl_display_new ();
    if (!gst_gl_display_create_context (filter->display, 0)) {
      GST_ELEMENT_ERROR (filter, RESOURCE, NOT_FOUND,
          GST_GL_DISPLAY_ERR_MSG (filter->display), (NULL));
      return FALSE;
    }
  }

  if (filter_class->onStart)
    filter_class->onStart (filter);

  return TRUE;
}

static gboolean
gst_gl_filter_stop (GstBaseTransform * bt)
{
  GstGLFilter *filter = GST_GL_FILTER (bt);
  GstGLFilterClass *filter_class = GST_GL_FILTER_GET_CLASS (filter);

  if (filter_class->onStop)
    filter_class->onStop (filter);

  gst_gl_filter_reset (filter);

  return TRUE;
}

static void
gst_gl_filter_start_gl (GstGLDisplay * display, gpointer data)
{
  GstGLFilter *filter = GST_GL_FILTER (data);
  GstGLFilterClass *filter_class = GST_GL_FILTER_GET_CLASS (filter);

  filter_class->display_init_cb (filter);
}

static void
gst_gl_filter_stop_gl (GstGLDisplay * display, gpointer data)
{
  GstGLFilter *filter = GST_GL_FILTER (data);
  GstGLFilterClass *filter_class = GST_GL_FILTER_GET_CLASS (filter);

  filter_class->display_reset_cb (filter);
}

static GstCaps *
gst_gl_filter_fixate_caps (GstBaseTransform * bt,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
  GstStructure *ins, *outs;
  const GValue *from_par, *to_par;
  GValue fpar = { 0, }, tpar = {
  0,};

  othercaps = gst_caps_truncate (othercaps);
  othercaps = gst_caps_make_writable (othercaps);

  GST_DEBUG_OBJECT (bt, "trying to fixate othercaps %" GST_PTR_FORMAT
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
      g_value_init (&tpar, GST_TYPE_FRACTION);
      gst_value_set_fraction (&tpar, 1, 1);
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
      gint n = 1, d = 1;

      GST_DEBUG_OBJECT (bt, "dimensions already set to %dx%d, not fixating",
          w, h);
      if (!gst_value_is_fixed (to_par)) {
        GST_DEBUG_OBJECT (bt, "fixating to_par to %dx%d", n, d);
        if (gst_structure_has_field (outs, "pixel-aspect-ratio"))
          gst_structure_fixate_field_nearest_fraction (outs,
              "pixel-aspect-ratio", 1, 1);
        else if (n != d)
          gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
              1, 1, NULL);
      }
      goto done;
    }

    /* Calculate input DAR */
    if (!gst_util_fraction_multiply (from_w, from_h, from_par_n, from_par_d,
            &from_dar_n, &from_dar_d)) {
      GST_ELEMENT_ERROR (bt, CORE, NEGOTIATION, (NULL),
          ("Error calculating the output scaled size - integer overflow"));
      goto done;
    }

    GST_DEBUG_OBJECT (bt, "Input DAR is %d/%d", from_dar_n, from_dar_d);

    /* If either width or height are fixed there's not much we
     * can do either except choosing a height or width and PAR
     * that matches the DAR as good as possible
     */
    if (h) {
      gint num, den;

      GST_DEBUG_OBJECT (bt, "height is fixed (%d)", h);

      if (!gst_value_is_fixed (to_par)) {
        gst_value_set_fraction (&tpar, 1, 1);
      }

      /* PAR is fixed, choose the height that is nearest to the
       * height with the same DAR */
      to_par_n = gst_value_get_fraction_numerator (to_par);
      to_par_d = gst_value_get_fraction_denominator (to_par);

      GST_DEBUG_OBJECT (bt, "PAR is fixed %d/%d", to_par_n, to_par_d);

      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, to_par_d,
              to_par_n, &num, &den)) {
        GST_ELEMENT_ERROR (bt, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scaled size - integer overflow"));
        goto done;
      }

      w = (guint) gst_util_uint64_scale_int (h, num, den);
      gst_structure_fixate_field_nearest_int (outs, "width", w);

      goto done;
    } else if (w) {
      gint num, den;

      GST_DEBUG_OBJECT (bt, "width is fixed (%d)", w);

      if (!gst_value_is_fixed (to_par)) {
        gst_value_set_fraction (&tpar, 1, 1);
      }

      /* PAR is fixed, choose the height that is nearest to the
       * height with the same DAR */
      to_par_n = gst_value_get_fraction_numerator (to_par);
      to_par_d = gst_value_get_fraction_denominator (to_par);

      GST_DEBUG_OBJECT (bt, "PAR is fixed %d/%d", to_par_n, to_par_d);

      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, to_par_d,
              to_par_n, &num, &den)) {
        GST_ELEMENT_ERROR (bt, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scaled size - integer overflow"));
        goto done;
      }

      h = (guint) gst_util_uint64_scale_int (w, den, num);
      gst_structure_fixate_field_nearest_int (outs, "height", h);

      goto done;
    } else if (gst_value_is_fixed (to_par)) {
      GstStructure *tmp;
      gint set_h, set_w, f_h, f_w;

      to_par_n = gst_value_get_fraction_numerator (to_par);
      to_par_d = gst_value_get_fraction_denominator (to_par);

      /* Calculate scale factor for the PAR change */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, to_par_n,
              to_par_d, &num, &den)) {
        GST_ELEMENT_ERROR (bt, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scaled size - integer overflow"));
        goto done;
      }

      /* Try to keep the input height */
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

      /* width, height and PAR are not fixed */

      /* First try to keep the height and width as good as possible
       * and scale PAR */
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "height", from_h);
      gst_structure_get_int (tmp, "height", &set_h);
      gst_structure_fixate_field_nearest_int (tmp, "width", from_w);
      gst_structure_get_int (tmp, "width", &set_w);

      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, set_h, set_w,
              &to_par_n, &to_par_d)) {
        GST_ELEMENT_ERROR (bt, CORE, NEGOTIATION, (NULL),
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
        GST_ELEMENT_ERROR (bt, CORE, NEGOTIATION, (NULL),
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
  othercaps = gst_caps_fixate (othercaps);

  GST_DEBUG_OBJECT (bt, "fixated othercaps to %" GST_PTR_FORMAT, othercaps);

  if (from_par == &fpar)
    g_value_unset (&fpar);
  if (to_par == &tpar)
    g_value_unset (&tpar);

  return othercaps;
}

static GstCaps *
gst_gl_filter_transform_caps (GstBaseTransform * bt,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  //GstGLFilter* filter = GST_GL_FILTER (bt);
  GstStructure *structure;
  GstCaps *newcaps, *result;
  const GValue *par;
  guint i, n;

  par = NULL;
  newcaps = gst_caps_new_empty ();
  n = gst_caps_get_size (caps);

  for (i = 0; i < n; i++) {
    structure = gst_caps_get_structure (caps, i);

    if (i > 0 && gst_caps_is_subset_structure (newcaps, structure))
      continue;

    structure = gst_structure_copy (structure);

    gst_structure_set (structure,
        "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
        "height", GST_TYPE_INT_RANGE, 1, G_MAXINT, NULL);

    gst_structure_remove_field (structure, "format");

    if ((par = gst_structure_get_value (structure, "pixel-aspect-ratio"))) {
      gst_structure_set (structure,
          "pixel-aspect-ratio", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1,
          NULL);
    }

    gst_caps_append_structure (newcaps, structure);
  }

  if (filter) {
    result =
        gst_caps_intersect_full (filter, newcaps, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (newcaps);
    newcaps = result;
  }

  GST_DEBUG_OBJECT (bt, "returning caps: %" GST_PTR_FORMAT, newcaps);

  return newcaps;
}


static gboolean
gst_gl_filter_get_unit_size (GstBaseTransform * trans, GstCaps * caps,
    gsize * size)
{
  gboolean ret = FALSE;
  GstVideoInfo info;

  ret = gst_video_info_from_caps (&info, caps);
  if (ret)
    *size = GST_VIDEO_INFO_SIZE (&info);

  return TRUE;
}

static gboolean
gst_gl_filter_set_caps (GstBaseTransform * bt, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstGLFilter *filter;
  GstGLFilterClass *filter_class;
  guint in_width, in_height, out_width, out_height;

  filter = GST_GL_FILTER (bt);
  filter_class = GST_GL_FILTER_GET_CLASS (filter);

  if (!gst_video_info_from_caps (&filter->in_info, incaps))
    goto wrong_caps;
  if (!gst_video_info_from_caps (&filter->out_info, outcaps))
    goto wrong_caps;

  in_width = GST_VIDEO_INFO_WIDTH (&filter->in_info);
  in_height = GST_VIDEO_INFO_HEIGHT (&filter->in_info);
  out_width = GST_VIDEO_INFO_WIDTH (&filter->out_info);
  out_height = GST_VIDEO_INFO_HEIGHT (&filter->out_info);

  //blocking call, generate a FBO
  if (!gst_gl_display_gen_fbo (filter->display, out_width, out_height,
          &filter->fbo, &filter->depthbuffer))
    goto display_error;

  gst_gl_display_gen_texture (filter->display, &filter->in_tex_id,
      GST_VIDEO_FORMAT_RGBA, in_width, in_height);

  gst_gl_display_gen_texture (filter->display, &filter->out_tex_id,
      GST_VIDEO_FORMAT_RGBA, out_width, out_height);

  if (filter_class->display_init_cb != NULL) {
    gst_gl_display_thread_add (filter->display, gst_gl_filter_start_gl, filter);
  }

  if (!filter->display->isAlive)
    goto error;

  if (filter_class->onInitFBO) {
    if (!filter_class->onInitFBO (filter))
      goto error;
  }

  if (filter_class->set_caps) {
    if (!filter_class->set_caps (filter, incaps, outcaps))
      goto error;
  }

  GST_DEBUG ("set_caps %dx%d", GST_VIDEO_INFO_WIDTH (&filter->out_info),
      GST_VIDEO_INFO_HEIGHT (&filter->out_info));

  return TRUE;

/* ERRORS */
wrong_caps:
  {
    GST_WARNING ("Wrong caps");
    return FALSE;
  }

display_error:
  {
    GST_ELEMENT_ERROR (filter, RESOURCE, NOT_FOUND,
        GST_GL_DISPLAY_ERR_MSG (filter->display), (NULL));
    return FALSE;
  }

error:
  {
    return FALSE;
  }
}

static gboolean
gst_gl_filter_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query)
{
  GstGLFilter *filter = GST_GL_FILTER (trans);
  GstBufferPool *pool;
  GstStructure *config;
  GstCaps *caps;
  guint size;
  gboolean need_pool;

  gst_query_parse_allocation (query, &caps, &need_pool);

  if (caps == NULL)
    goto no_caps;

  if ((pool = filter->pool))
    gst_object_ref (pool);

  if (pool != NULL) {
    GstCaps *pcaps;

    /* we had a pool, check caps */
    GST_DEBUG_OBJECT (filter, "check existing pool caps");
    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_get_params (config, &pcaps, &size, NULL, NULL);

    if (!gst_caps_is_equal (caps, pcaps)) {
      GST_DEBUG_OBJECT (filter, "pool has different caps");
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

    GST_DEBUG_OBJECT (filter, "create new pool");
    pool = gst_gl_buffer_pool_new (filter->display);

    /* the normal size of a frame */
    size = info.size;

    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_set_params (config, caps, size, 0, 0);
    if (!gst_buffer_pool_set_config (pool, config))
      goto config_failed;
  }
  /* we need at least 2 buffer because we hold on to the last one */
  gst_query_add_allocation_pool (query, pool, size, 1, 0);

  /* we also support various metadata */
  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, 0);

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

static gboolean
gst_gl_filter_decide_allocation (GstBaseTransform * trans, GstQuery * query)
{
  GstGLFilter *filter = GST_GL_FILTER (trans);
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
    pool = gst_gl_buffer_pool_new (filter->display);

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, caps, size, min, max);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  gst_buffer_pool_set_config (pool, config);

  if (update_pool)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);

  gst_object_unref (pool);

  return TRUE;
}

/**
 * gst_gl_filter_filter_texture:
 * @filter: a #GstGLFilter
 * @inbuf: an input buffer
 * @outbuf: an output buffer
 *
 * Perform automatic upload if needed, call filter_texture vfunc and then an
 * automatic download if needed.
 *
 * Returns: whether the transformation succeeded
 */
gboolean
gst_gl_filter_filter_texture (GstGLFilter * filter, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstGLFilterClass *filter_class;
  GstVideoFrame in_frame;
  GstVideoFrame out_frame;
  guint in_tex, out_tex;
  gboolean ret, in_gl_wrapped, out_gl_wrapped;

  filter_class = GST_GL_FILTER_GET_CLASS (filter);

  if (!gst_video_frame_map (&in_frame, &filter->in_info, inbuf,
          GST_MAP_READ | GST_MAP_GL)) {
    return FALSE;
  }
  if (!gst_video_frame_map (&out_frame, &filter->out_info, outbuf,
          GST_MAP_WRITE | GST_MAP_GL)) {
    gst_video_frame_unmap (&in_frame);
    return FALSE;
  }

  in_gl_wrapped = !gst_is_gl_memory (in_frame.map[0].memory);
  out_gl_wrapped = !gst_is_gl_memory (out_frame.map[0].memory);

  if (!in_gl_wrapped && !out_gl_wrapped) {      /* both GL */
    in_tex = *(guint *) in_frame.data[0];
    out_tex = *(guint *) out_frame.data[0];
  } else if (in_gl_wrapped && !out_gl_wrapped) {        /* input: non-GL, output: GL */
    GST_INFO ("Input Buffer does not contain correct meta, "
        "attempting to wrap for upload");

    if (!filter->upload) {
      filter->upload = gst_gl_upload_new (filter->display);

      gst_gl_upload_init_format (filter->upload,
          GST_VIDEO_FRAME_FORMAT (&in_frame),
          GST_VIDEO_FRAME_WIDTH (&in_frame),
          GST_VIDEO_FRAME_HEIGHT (&in_frame),
          GST_VIDEO_FRAME_WIDTH (&out_frame),
          GST_VIDEO_FRAME_HEIGHT (&out_frame));
    }

    in_tex = filter->in_tex_id;
    out_tex = *(guint *) out_frame.data[0];
  } else if (!in_gl_wrapped && out_gl_wrapped) {        /* input: GL, output: non-GL */
    GST_INFO ("Output Buffer does not contain correct memory, "
        "attempting to wrap for download");

    if (!filter->download) {
      filter->download = gst_gl_download_new (filter->display);

      gst_gl_download_init_format (filter->download,
          GST_VIDEO_FRAME_FORMAT (&out_frame),
          GST_VIDEO_FRAME_WIDTH (&out_frame),
          GST_VIDEO_FRAME_HEIGHT (&out_frame));
    }

    in_tex = *(guint *) in_frame.data[0];
    out_tex = filter->out_tex_id;
  } else {                      /* both non-GL */
    if (!filter->upload) {
      filter->upload = gst_gl_upload_new (filter->display);

      gst_gl_upload_init_format (filter->upload,
          GST_VIDEO_FRAME_FORMAT (&in_frame),
          GST_VIDEO_FRAME_WIDTH (&in_frame),
          GST_VIDEO_FRAME_HEIGHT (&in_frame),
          GST_VIDEO_FRAME_WIDTH (&out_frame),
          GST_VIDEO_FRAME_HEIGHT (&out_frame));
    }

    if (!filter->download) {
      filter->download = gst_gl_download_new (filter->display);

      gst_gl_download_init_format (filter->download,
          GST_VIDEO_FRAME_FORMAT (&out_frame),
          GST_VIDEO_FRAME_WIDTH (&out_frame),
          GST_VIDEO_FRAME_HEIGHT (&out_frame));
    }

    gst_gl_upload_perform_with_data (filter->upload, filter->in_tex_id,
        in_frame.data);

    out_tex = filter->out_tex_id;
    in_tex = filter->in_tex_id;
  }

  if (in_gl_wrapped) {
    gst_gl_upload_perform_with_data (filter->upload, in_tex, in_frame.data);
  }

  g_assert (filter_class->filter_texture);
  ret = filter_class->filter_texture (filter, in_tex, out_tex);

  if (out_gl_wrapped) {
    gst_gl_download_perform_with_data (filter->download, out_tex,
        out_frame.data);
  }

  gst_video_frame_unmap (&in_frame);
  gst_video_frame_unmap (&out_frame);

  return ret;
}

static GstFlowReturn
gst_gl_filter_transform (GstBaseTransform * bt, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstGLFilter *filter;
  GstGLFilterClass *filter_class;

  filter = GST_GL_FILTER (bt);
  filter_class = GST_GL_FILTER_GET_CLASS (bt);

  g_assert (filter_class->filter || filter_class->filter_texture);

  if (filter_class->filter)
    filter_class->filter (filter, inbuf, outbuf);
  else if (filter_class->filter_texture)
    gst_gl_filter_filter_texture (filter, inbuf, outbuf);

  return GST_FLOW_OK;
}

/* convenience functions to simplify filter development */
/**
 * gst_gl_filter_render_to_target:
 * @filter: a #GstGLFilter
 * @resize: whether to automatically resize the texture between the input size
 *          and the output size
 * @input: the input texture
 * @target: the output texture
 * @func: the function to transform @input into @output. called with @data
 * @data: the data associated with @func
 *
 * Transforms @input into @output using @func on through FBO.  @resize should
 * only ever be %TRUE whenever @input is the input texture of @filter.
 */
void
gst_gl_filter_render_to_target (GstGLFilter * filter, gboolean resize,
    GLuint input, GLuint target, GLCB func, gpointer data)
{
  guint in_width, in_height, out_width, out_height;

  out_width = GST_VIDEO_INFO_WIDTH (&filter->out_info);
  out_height = GST_VIDEO_INFO_HEIGHT (&filter->out_info);
  if (resize) {
    in_width = GST_VIDEO_INFO_WIDTH (&filter->in_info);
    in_height = GST_VIDEO_INFO_HEIGHT (&filter->in_info);
  } else {
    in_width = out_width;
    in_height = out_height;
  }

  GST_LOG ("rendering to target. in:%ux%u out:%ux%u", in_width, in_height,
      out_width, out_height);

  gst_gl_display_use_fbo (filter->display,
      out_width, out_height,
      filter->fbo, filter->depthbuffer, target,
      func, in_width, in_height, input, 0,
      in_width, 0, in_height, GST_GL_DISPLAY_PROJECTION_ORTHO2D, data);
}

#if GST_GL_HAVE_OPENGL
static void
_draw_with_shader_cb (gint width, gint height, guint texture, gpointer stuff)
{
  GstGLFilter *filter = GST_GL_FILTER (stuff);
  GstGLFuncs *gl = filter->display->gl_vtable;

  gl->MatrixMode (GL_PROJECTION);
  gl->LoadIdentity ();

  gst_gl_shader_use (filter->default_shader);

  gl->ActiveTexture (GL_TEXTURE1);
  gl->Enable (GL_TEXTURE_RECTANGLE_ARB);
  gl->BindTexture (GL_TEXTURE_RECTANGLE_ARB, texture);
  gl->Disable (GL_TEXTURE_RECTANGLE_ARB);

  gst_gl_shader_set_uniform_1i (filter->default_shader, "tex", 1);

  gst_gl_filter_draw_texture (filter, texture, width, height);
}

/**
 * gst_gl_filter_render_to_target_with_shader:
 * @filter: a #GstGLFilter
 * @resize: whether to automatically resize the texture between the input size
 *          and the output size
 * @input: the input texture
 * @target: the output texture
 * @shader: the shader to use.
 *
 * Transforms @input into @output using @shader on FBO.  @resize should
 * only ever be %TRUE whenever @input is the input texture of @filter.
 *
 * See also: gst_gl_filter_render_to_target()
 */
/* attach target to a FBO, use shader, pass input as "tex" uniform to
 * the shader, render input to a quad */
void
gst_gl_filter_render_to_target_with_shader (GstGLFilter * filter,
    gboolean resize, GLuint input, GLuint target, GstGLShader * shader)
{
  g_return_if_fail (gst_gl_display_get_gl_api (filter->display) &
      GST_GL_API_OPENGL);

  filter->default_shader = shader;
  gst_gl_filter_render_to_target (filter, resize, input, target,
      _draw_with_shader_cb, filter);
}

/**
 * gst_gl_filter_draw_texture:
 * @filter: a #GstGLFilter
 * @texture: the texture to draw
 * @width: width of @texture
 * @height: height of texture
 *
 * Draws @texture into the OpenGL scene at the specified @width and @height.
 */
void
gst_gl_filter_draw_texture (GstGLFilter * filter, GLuint texture,
    guint width, guint height)
{
  GstGLFuncs *gl = filter->display->gl_vtable;

  gfloat verts[] = { -1.0f, -1.0f,
    1.0f, -1.0f,
    1.0f, 1.0f,
    -1.0f, 1.0f
  };
  gint texcoords[] = { 0, 0,
    width, 0,
    width, height,
    0, height
  };

  GST_DEBUG ("drawing texture:%u dimensions:%ux%u", texture, width, height);

  gl->ActiveTexture (GL_TEXTURE0);
  gl->Enable (GL_TEXTURE_RECTANGLE_ARB);
  gl->BindTexture (GL_TEXTURE_RECTANGLE_ARB, texture);

  gl->ClientActiveTexture (GL_TEXTURE0);

  gl->EnableClientState (GL_VERTEX_ARRAY);
  gl->EnableClientState (GL_TEXTURE_COORD_ARRAY);

  gl->VertexPointer (2, GL_FLOAT, 0, &verts);
  gl->TexCoordPointer (2, GL_INT, 0, &texcoords);

  gl->DrawArrays (GL_TRIANGLE_FAN, 0, 4);

  gl->DisableClientState (GL_VERTEX_ARRAY);
  gl->DisableClientState (GL_TEXTURE_COORD_ARRAY);
}
#endif /* GST_GL_HAVE_OPENGL */
