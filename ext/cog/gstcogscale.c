/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) 2005 David Schleef <ds@schleef.org>
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
 * SECTION:element-videoscale
 * @see_also: videorate, ffmpegcolorspace
 *
 * <refsect2>
 * <para>
 * This element resizes video frames. By default the element will try to
 * negotiate to the same size on the source and sinkpad so that no scaling
 * is needed. It is therefore safe to insert this element in a pipeline to
 * get more robust behaviour without any cost if no scaling is needed.
 * </para>
 * <para>
 * This element supports a wide range of color spaces including various YUV and
 * RGB formats and is therefore generally able to operate anywhere in a
 * pipeline.
 * </para>
 * <title>Example pipelines</title>
 * <para>
 * <programlisting>
 * gst-launch -v filesrc location=videotestsrc.ogg ! oggdemux ! theoradec ! ffmpegcolorspace ! videoscale ! ximagesink
 * </programlisting>
 * Decode an Ogg/Theora and display the video using ximagesink. Since
 * ximagesink cannot perform scaling, the video scaling will be performed by
 * videoscale when you resize the video window.
 * To create the test Ogg/Theora file refer to the documentation of theoraenc.
 * </para>
 * <para>
 * <programlisting>
 * gst-launch -v filesrc location=videotestsrc.ogg ! oggdemux ! theoradec ! videoscale ! video/x-raw-yuv, width=50 ! xvimagesink
 * </programlisting>
 * Decode an Ogg/Theora and display the video using xvimagesink with a width of
 * 50.
 * </para>
 * </refsect2>
 *
 * Last reviewed on 2006-03-02 (0.10.4)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/base/gstbasetransform.h>
#include <cog/cog.h>
#include <cog/cogvirtframe.h>
#include "gstcogutils.h"

GST_DEBUG_CATEGORY_STATIC (cog_scale_debug);
#define GST_CAT_DEFAULT cog_scale_debug

#define GST_TYPE_COG_SCALE \
  (gst_cog_scale_get_type())
#define GST_COG_SCALE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_COG_SCALE,GstCogScale))
#define GST_COG_SCALE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_COG_SCALE,GstCogScaleClass))
#define GST_IS_COG_SCALE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_COG_SCALE))
#define GST_IS_COG_SCALE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_COG_SCALE))

typedef struct _GstCogScale GstCogScale;
typedef struct _GstCogScaleClass GstCogScaleClass;

/**
 * GstCogScale:
 *
 * Opaque data structure
 */
struct _GstCogScale
{
  GstBaseTransform element;

  int quality;

  /* negotiated stuff */
  GstVideoFormat format;
  guint src_size;
  guint dest_size;
  gint to_width;
  gint to_height;
  gint from_width;
  gint from_height;

  /*< private > */
};

struct _GstCogScaleClass
{
  GstBaseTransformClass parent_class;
};

GType gst_cog_scale_get_type (void);

#define DEFAULT_QUALITY 5

enum
{
  PROP_0,
  PROP_QUALITY
};

/* can't handle width/height of 1 yet, since we divide a lot by (n-1) */
#undef GST_VIDEO_SIZE_RANGE
#define GST_VIDEO_SIZE_RANGE "(int) [ 2, MAX ]"

#define TEMPLATE_CAPS \
  GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("{ I420, YV12, YUY2, UYVY, AYUV, Y42B }") ";" \
    GST_VIDEO_CAPS_ARGB)

static GstStaticPadTemplate gst_cog_scale_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    TEMPLATE_CAPS);

static GstStaticPadTemplate gst_cog_scale_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    TEMPLATE_CAPS);

static void gst_cog_scale_base_init (gpointer g_class);
static void gst_cog_scale_class_init (GstCogScaleClass * klass);
static void gst_cog_scale_init (GstCogScale * videoscale);
static void gst_cog_scale_finalize (GstCogScale * videoscale);
static gboolean gst_cog_scale_src_event (GstBaseTransform * trans,
    GstEvent * event);

/* base transform vmethods */
static GstCaps *gst_cog_scale_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps);
static gboolean gst_cog_scale_set_caps (GstBaseTransform * trans,
    GstCaps * in, GstCaps * out);
static gboolean gst_cog_scale_get_unit_size (GstBaseTransform * trans,
    GstCaps * caps, guint * size);
static GstFlowReturn gst_cog_scale_transform (GstBaseTransform * trans,
    GstBuffer * in, GstBuffer * out);
static void gst_cog_scale_fixate_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps);

static void gst_cog_scale_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_cog_scale_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstElementClass *parent_class = NULL;


GType
gst_cog_scale_get_type (void)
{
  static GType cog_scale_type = 0;

  if (!cog_scale_type) {
    static const GTypeInfo cog_scale_info = {
      sizeof (GstCogScaleClass),
      gst_cog_scale_base_init,
      NULL,
      (GClassInitFunc) gst_cog_scale_class_init,
      NULL,
      NULL,
      sizeof (GstCogScale),
      0,
      (GInstanceInitFunc) gst_cog_scale_init,
    };

    cog_scale_type =
        g_type_register_static (GST_TYPE_BASE_TRANSFORM, "GstCogScale",
        &cog_scale_info, 0);

    GST_DEBUG_CATEGORY_INIT (cog_scale_debug, "cogscale", 0, "Cog scale");
  }
  return cog_scale_type;
}

static void
gst_cog_scale_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class, "Video scaler",
      "Filter/Effect/Video",
      "Resizes video", "Wim Taymans <wim.taymans@chello.be>");

  gst_element_class_add_static_pad_template (element_class,
      &gst_cog_scale_src_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_cog_scale_sink_template);
}

static void
gst_cog_scale_class_init (GstCogScaleClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseTransformClass *trans_class;

  gobject_class = (GObjectClass *) klass;
  trans_class = (GstBaseTransformClass *) klass;

  gobject_class->finalize = (GObjectFinalizeFunc) gst_cog_scale_finalize;
  gobject_class->set_property = gst_cog_scale_set_property;
  gobject_class->get_property = gst_cog_scale_get_property;

  g_object_class_install_property (gobject_class, PROP_QUALITY,
      g_param_spec_int ("quality", "quality", "Scaling Quality",
          0, 10, DEFAULT_QUALITY, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  trans_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_cog_scale_transform_caps);
  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_cog_scale_set_caps);
  trans_class->get_unit_size = GST_DEBUG_FUNCPTR (gst_cog_scale_get_unit_size);
  trans_class->transform = GST_DEBUG_FUNCPTR (gst_cog_scale_transform);
  trans_class->fixate_caps = GST_DEBUG_FUNCPTR (gst_cog_scale_fixate_caps);
  trans_class->src_event = GST_DEBUG_FUNCPTR (gst_cog_scale_src_event);

  trans_class->passthrough_on_same_caps = TRUE;

  parent_class = g_type_class_peek_parent (klass);
}

static void
gst_cog_scale_init (GstCogScale * videoscale)
{
  gst_base_transform_set_qos_enabled (GST_BASE_TRANSFORM (videoscale), TRUE);
  videoscale->quality = DEFAULT_QUALITY;
}

static void
gst_cog_scale_finalize (GstCogScale * videoscale)
{
  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (videoscale));
}

static void
gst_cog_scale_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCogScale *vscale = GST_COG_SCALE (object);

  switch (prop_id) {
    case PROP_QUALITY:
      GST_OBJECT_LOCK (vscale);
      vscale->quality = g_value_get_int (value);
      GST_OBJECT_UNLOCK (vscale);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_cog_scale_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstCogScale *vscale = GST_COG_SCALE (object);

  switch (prop_id) {
    case PROP_QUALITY:
      GST_OBJECT_LOCK (vscale);
      g_value_set_int (value, vscale->quality);
      GST_OBJECT_UNLOCK (vscale);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstCaps *
gst_cog_scale_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps)
{
  GstCaps *ret;
  GstStructure *structure;
  const GValue *par;

  /* this function is always called with a simple caps */
  g_return_val_if_fail (GST_CAPS_IS_SIMPLE (caps), NULL);

  structure = gst_caps_get_structure (caps, 0);

  ret = gst_caps_copy (caps);
  structure = gst_caps_get_structure (ret, 0);

  gst_structure_set (structure,
      "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
      "height", GST_TYPE_INT_RANGE, 1, G_MAXINT, NULL);

  /* if pixel aspect ratio, make a range of it */
  if ((par = gst_structure_get_value (structure, "pixel-aspect-ratio"))) {
    GstCaps *copy;
    GstStructure *cstruct;

    /* copy input PAR first, this is the prefered PAR */
    gst_structure_set_value (structure, "pixel-aspect-ratio", par);

    /* then make a copy with a fraction range as a second choice */
    copy = gst_caps_copy (ret);
    cstruct = gst_caps_get_structure (copy, 0);
    gst_structure_set (cstruct,
        "pixel-aspect-ratio", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);

    /* and append */
    gst_caps_append (ret, copy);
  }

  GST_DEBUG_OBJECT (trans, "returning caps: %" GST_PTR_FORMAT, ret);

  return ret;
}

static gboolean
gst_cog_scale_set_caps (GstBaseTransform * trans, GstCaps * in, GstCaps * out)
{
  GstCogScale *videoscale;
  gboolean ret;

  videoscale = GST_COG_SCALE (trans);

  ret = gst_video_format_parse_caps (in, &videoscale->format,
      &videoscale->from_width, &videoscale->from_height);
  ret &= gst_video_format_parse_caps (out, NULL,
      &videoscale->to_width, &videoscale->to_height);
  if (!ret)
    goto done;

  videoscale->src_size = gst_video_format_get_size (videoscale->format,
      videoscale->from_width, videoscale->from_height);
  videoscale->dest_size = gst_video_format_get_size (videoscale->format,
      videoscale->to_width, videoscale->to_height);

  /* FIXME: par */
  GST_DEBUG_OBJECT (videoscale, "from=%dx%d, size %d -> to=%dx%d, size %d",
      videoscale->from_width, videoscale->from_height, videoscale->src_size,
      videoscale->to_width, videoscale->to_height, videoscale->dest_size);

done:
  return ret;
}

static gboolean
gst_cog_scale_get_unit_size (GstBaseTransform * trans, GstCaps * caps,
    guint * size)
{
  GstVideoFormat format;
  gint width, height;

  g_assert (size);

  if (!gst_video_format_parse_caps (caps, &format, &width, &height))
    return FALSE;

  *size = gst_video_format_get_size (format, width, height);

  return TRUE;
}

static void
gst_cog_scale_fixate_caps (GstBaseTransform * base, GstPadDirection direction,
    GstCaps * caps, GstCaps * othercaps)
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

  /* we have both PAR but they might not be fixated */
  if (from_par && to_par) {
    gint from_w, from_h, from_par_n, from_par_d, to_par_n, to_par_d;
    gint count = 0, w = 0, h = 0;
    guint num, den;

    /* from_par should be fixed */
    g_return_if_fail (gst_value_is_fixed (from_par));

    from_par_n = gst_value_get_fraction_numerator (from_par);
    from_par_d = gst_value_get_fraction_denominator (from_par);

    /* fixate the out PAR */
    if (!gst_value_is_fixed (to_par)) {
      GST_DEBUG_OBJECT (base, "fixating to_par to %dx%d", from_par_n,
          from_par_d);
      gst_structure_fixate_field_nearest_fraction (outs, "pixel-aspect-ratio",
          from_par_n, from_par_d);
    }

    to_par_n = gst_value_get_fraction_numerator (to_par);
    to_par_d = gst_value_get_fraction_denominator (to_par);

    /* if both width and height are already fixed, we can't do anything
     * about it anymore */
    if (gst_structure_get_int (outs, "width", &w))
      ++count;
    if (gst_structure_get_int (outs, "height", &h))
      ++count;
    if (count == 2) {
      GST_DEBUG_OBJECT (base, "dimensions already set to %dx%d, not fixating",
          w, h);
      return;
    }

    gst_structure_get_int (ins, "width", &from_w);
    gst_structure_get_int (ins, "height", &from_h);

    if (!gst_video_calculate_display_ratio (&num, &den, from_w, from_h,
            from_par_n, from_par_d, to_par_n, to_par_d)) {
      GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (NULL),
          ("Error calculating the output scaled size - integer overflow"));
      return;
    }

    GST_DEBUG_OBJECT (base,
        "scaling input with %dx%d and PAR %d/%d to output PAR %d/%d",
        from_w, from_h, from_par_n, from_par_d, to_par_n, to_par_d);
    GST_DEBUG_OBJECT (base, "resulting output should respect ratio of %d/%d",
        num, den);

    /* now find a width x height that respects this display ratio.
     * prefer those that have one of w/h the same as the incoming video
     * using wd / hd = num / den */

    /* if one of the output width or height is fixed, we work from there */
    if (h) {
      GST_DEBUG_OBJECT (base, "height is fixed,scaling width");
      w = (guint) gst_util_uint64_scale_int (h, num, den);
    } else if (w) {
      GST_DEBUG_OBJECT (base, "width is fixed, scaling height");
      h = (guint) gst_util_uint64_scale_int (w, den, num);
    } else {
      /* none of width or height is fixed, figure out both of them based only on
       * the input width and height */
      /* check hd / den is an integer scale factor, and scale wd with the PAR */
      if (from_h % den == 0) {
        GST_DEBUG_OBJECT (base, "keeping video height");
        h = from_h;
        w = (guint) gst_util_uint64_scale_int (h, num, den);
      } else if (from_w % num == 0) {
        GST_DEBUG_OBJECT (base, "keeping video width");
        w = from_w;
        h = (guint) gst_util_uint64_scale_int (w, den, num);
      } else {
        GST_DEBUG_OBJECT (base, "approximating but keeping video height");
        h = from_h;
        w = (guint) gst_util_uint64_scale_int (h, num, den);
      }
    }
    GST_DEBUG_OBJECT (base, "scaling to %dx%d", w, h);

    /* now fixate */
    gst_structure_fixate_field_nearest_int (outs, "width", w);
    gst_structure_fixate_field_nearest_int (outs, "height", h);
  } else {
    gint width, height;

    if (gst_structure_get_int (ins, "width", &width)) {
      if (gst_structure_has_field (outs, "width")) {
        gst_structure_fixate_field_nearest_int (outs, "width", width);
      }
    }
    if (gst_structure_get_int (ins, "height", &height)) {
      if (gst_structure_has_field (outs, "height")) {
        gst_structure_fixate_field_nearest_int (outs, "height", height);
      }
    }
  }

  GST_DEBUG_OBJECT (base, "fixated othercaps to %" GST_PTR_FORMAT, othercaps);
}

#if 0
static gboolean
gst_cog_scale_prepare_image (gint format, GstBuffer * buf,
    VSImage * img, VSImage * img_u, VSImage * img_v)
{
  gboolean res = TRUE;

  img->pixels = GST_BUFFER_DATA (buf);

  switch (format) {
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
      img_u->pixels = img->pixels + GST_ROUND_UP_2 (img->height) * img->stride;
      img_u->height = GST_ROUND_UP_2 (img->height) / 2;
      img_u->width = GST_ROUND_UP_2 (img->width) / 2;
      img_u->stride = GST_ROUND_UP_4 (img_u->width);
      memcpy (img_v, img_u, sizeof (*img_v));
      img_v->pixels = img_u->pixels + img_u->height * img_u->stride;
      break;
    default:
      break;
  }
  return res;
}
#endif

static GstFlowReturn
gst_cog_scale_transform (GstBaseTransform * trans, GstBuffer * in,
    GstBuffer * out)
{
  GstCogScale *videoscale;
  GstFlowReturn ret = GST_FLOW_OK;
  CogFrame *outframe;
  CogFrame *frame;
  int w, h;
  int quality;
  static const int n_vert_taps[11] = { 1, 1, 2, 2, 2, 2, 4, 4, 4, 4, 4 };
  static const int n_horiz_taps[11] = { 1, 1, 1, 1, 2, 2, 2, 2, 4, 4, 4 };

  videoscale = GST_COG_SCALE (trans);

  GST_OBJECT_LOCK (videoscale);
  quality = videoscale->quality;
  GST_OBJECT_UNLOCK (videoscale);

  frame = gst_cog_buffer_wrap (gst_buffer_ref (in), videoscale->format,
      videoscale->from_width, videoscale->from_height);
  outframe = gst_cog_buffer_wrap (gst_buffer_ref (out), videoscale->format,
      videoscale->to_width, videoscale->to_height);

  frame = cog_virt_frame_new_unpack (frame);

  w = videoscale->from_width;
  h = videoscale->from_height;
  while (w >= 2 * videoscale->to_width || h >= 2 * videoscale->to_height) {
    if (w >= 2 * videoscale->to_width) {
      frame = cog_virt_frame_new_horiz_downsample (frame, 3);
      w /= 2;
    }
    if (h >= 2 * videoscale->to_height) {
      frame = cog_virt_frame_new_vert_downsample (frame, 4);
      h /= 2;
    }
  }
  if (w != videoscale->to_width) {
    frame = cog_virt_frame_new_horiz_resample (frame, videoscale->to_width,
        n_horiz_taps[quality]);
  }
  if (h != videoscale->to_height) {
    frame = cog_virt_frame_new_vert_resample (frame, videoscale->to_height,
        n_vert_taps[quality]);
  }

  switch (videoscale->format) {
    case GST_VIDEO_FORMAT_YUY2:
      frame = cog_virt_frame_new_pack_YUY2 (frame);
      break;
    case GST_VIDEO_FORMAT_UYVY:
      frame = cog_virt_frame_new_pack_UYVY (frame);
      break;
    default:
      break;
  }

  cog_virt_frame_render (frame, outframe);
  cog_frame_unref (frame);
  cog_frame_unref (outframe);

  GST_LOG_OBJECT (videoscale, "pushing buffer of %d bytes",
      GST_BUFFER_SIZE (out));

  return ret;

  /* ERRORS */
#if 0
unsupported:
  {
    GST_ELEMENT_ERROR (videoscale, STREAM, NOT_IMPLEMENTED, (NULL),
        ("Unsupported format %d for scaling method %d",
            videoscale->format, method));
    return GST_FLOW_ERROR;
  }
unknown_mode:
  {
    GST_ELEMENT_ERROR (videoscale, STREAM, NOT_IMPLEMENTED, (NULL),
        ("Unknown scaling method %d", videoscale->method));
    return GST_FLOW_ERROR;
  }
#endif
}

static gboolean
gst_cog_scale_src_event (GstBaseTransform * trans, GstEvent * event)
{
  GstCogScale *videoscale;
  gboolean ret;
  double a;
  GstStructure *structure;

  videoscale = GST_COG_SCALE (trans);

  GST_DEBUG_OBJECT (videoscale, "handling %s event",
      GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NAVIGATION:
      event =
          GST_EVENT (gst_mini_object_make_writable (GST_MINI_OBJECT (event)));

      structure = (GstStructure *) gst_event_get_structure (event);
      if (gst_structure_get_double (structure, "pointer_x", &a)) {
        gst_structure_set (structure, "pointer_x", G_TYPE_DOUBLE,
            a * videoscale->from_width / videoscale->to_width, NULL);
      }
      if (gst_structure_get_double (structure, "pointer_y", &a)) {
        gst_structure_set (structure, "pointer_y", G_TYPE_DOUBLE,
            a * videoscale->from_height / videoscale->to_height, NULL);
      }
      break;
    case GST_EVENT_QOS:
      break;
    default:
      break;
  }

  ret = GST_BASE_TRANSFORM_CLASS (parent_class)->src_event (trans, event);

  return ret;
}
