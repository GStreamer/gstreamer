/* GStreamer
 * Copyright (C) <2008> Wim Taymans <wim.taymans@gmail.com>
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
 * SECTION:element-smptealpha
 *
 * smptealpha can accept an I420 or AYUV video stream. An alpha channel is added
 * using an effect specific SMPTE mask in the I420 input case. In the AYUV case,
 * the alpha channel is modified using the effect specific SMPTE mask.
 *
 * The #GstSmpteAlpha:position property is a controllabe double between 0.0 and
 * 1.0 that specifies the position in the transition. 0.0 is the start of the
 * transition with the alpha channel to complete opaque where 1.0 has the alpha
 * channel set to completely transparent.
 *
 * The #GstSmpteAlpha:depth property defines the precision in bits of the mask.
 * A higher presision will create a mask with smoother gradients in order to
 * avoid banding.
 *
 * <refsect2>
 * <title>Sample pipelines</title>
 * <para>
 * Here is a pipeline to demonstrate the smpte transition :
 * <programlisting>
 * gst-launch -v videotestsrc ! smptealpha border=20000 type=44
 * position=0.5 ! videomixer ! ffmpegcolorspace ! ximagesink 
 * </programlisting>
 * This shows a midway bowtie-h transition a from a videotestsrc to a
 * transparent image. The edges of the transition are smoothed with a
 * 20000 big border.
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>

#include <gst/controller/gstcontroller.h>

#include "gstsmptealpha.h"
#include "paint.h"

GST_DEBUG_CATEGORY_STATIC (gst_smpte_alpha_debug);
#define GST_CAT_DEFAULT gst_smpte_alpha_debug

static GstStaticPadTemplate gst_smpte_alpha_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("AYUV")
    )
    );

static GstStaticPadTemplate gst_smpte_alpha_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("I420") ";" GST_VIDEO_CAPS_YUV ("AYUV")
    )
    );

/* SMPTE signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

#define DEFAULT_PROP_TYPE	1
#define DEFAULT_PROP_BORDER	0
#define DEFAULT_PROP_DEPTH	16
#define DEFAULT_PROP_POSITION	0.0

enum
{
  PROP_0,
  PROP_TYPE,
  PROP_BORDER,
  PROP_DEPTH,
  PROP_POSITION,
  PROP_LAST,
};

#define AYUV_SIZE(w,h)     ((w) * (h) * 4)

#define GST_TYPE_SMPTE_TRANSITION_TYPE (gst_smpte_alpha_transition_type_get_type())
static GType
gst_smpte_alpha_transition_type_get_type (void)
{
  static GType smpte_transition_type = 0;
  GEnumValue *smpte_transitions;

  if (!smpte_transition_type) {
    const GList *definitions;
    gint i = 0;

    definitions = gst_mask_get_definitions ();
    smpte_transitions =
        g_new0 (GEnumValue, g_list_length ((GList *) definitions) + 1);

    while (definitions) {
      GstMaskDefinition *definition = (GstMaskDefinition *) definitions->data;

      definitions = g_list_next (definitions);

      smpte_transitions[i].value = definition->type;
      /* older GLib versions have the two fields as non-const, hence the cast */
      smpte_transitions[i].value_nick = (gchar *) definition->short_name;
      smpte_transitions[i].value_name = (gchar *) definition->long_name;

      i++;
    }

    smpte_transition_type =
        g_enum_register_static ("GstSMPTEAlphaTransitionType",
        smpte_transitions);
  }
  return smpte_transition_type;
}


static void gst_smpte_alpha_class_init (GstSMPTEAlphaClass * klass);
static void gst_smpte_alpha_base_init (GstSMPTEAlphaClass * klass);
static void gst_smpte_alpha_init (GstSMPTEAlpha * smpte);
static void gst_smpte_alpha_finalize (GstSMPTEAlpha * smpte);

static void gst_smpte_alpha_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_smpte_alpha_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_smpte_alpha_setcaps (GstBaseTransform * btrans,
    GstCaps * incaps, GstCaps * outcaps);
static gboolean gst_smpte_alpha_get_unit_size (GstBaseTransform * btrans,
    GstCaps * caps, guint * size);
static GstFlowReturn gst_smpte_alpha_transform (GstBaseTransform * trans,
    GstBuffer * in, GstBuffer * out);

static GstElementClass *parent_class = NULL;

/*static guint gst_smpte_alpha_signals[LAST_SIGNAL] = { 0 }; */

static GType
gst_smpte_alpha_get_type (void)
{
  static GType smpte_type = 0;

  if (!smpte_type) {
    static const GTypeInfo smpte_info = {
      sizeof (GstSMPTEAlphaClass),
      (GBaseInitFunc) gst_smpte_alpha_base_init,
      NULL,
      (GClassInitFunc) gst_smpte_alpha_class_init,
      NULL,
      NULL,
      sizeof (GstSMPTEAlpha),
      0,
      (GInstanceInitFunc) gst_smpte_alpha_init,
    };

    smpte_type =
        g_type_register_static (GST_TYPE_VIDEO_FILTER, "GstSMPTEAlpha",
        &smpte_info, 0);
  }
  return smpte_type;
}

static void
gst_smpte_alpha_base_init (GstSMPTEAlphaClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_smpte_alpha_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_smpte_alpha_src_template));
  gst_element_class_set_details_simple (element_class, "SMPTE transitions",
      "Filter/Editor/Video",
      "Apply the standard SMPTE transitions as alpha on video images",
      "Wim Taymans <wim.taymans@gmail.com>");
}

static void
gst_smpte_alpha_class_init (GstSMPTEAlphaClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseTransformClass *trans_class;

  gobject_class = (GObjectClass *) klass;
  trans_class = (GstBaseTransformClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_smpte_alpha_set_property;
  gobject_class->get_property = gst_smpte_alpha_get_property;

  gobject_class->finalize = (GObjectFinalizeFunc) gst_smpte_alpha_finalize;

  _gst_mask_init ();

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_TYPE,
      g_param_spec_enum ("type", "Type", "The type of transition to use",
          GST_TYPE_SMPTE_TRANSITION_TYPE, DEFAULT_PROP_TYPE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_BORDER,
      g_param_spec_int ("border", "Border",
          "The border width of the transition", 0, G_MAXINT,
          DEFAULT_PROP_BORDER, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_DEPTH,
      g_param_spec_int ("depth", "Depth", "Depth of the mask in bits", 1, 24,
          DEFAULT_PROP_DEPTH, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_POSITION,
      g_param_spec_double ("position", "Position",
          "Position of the transition effect", 0.0, 1.0, DEFAULT_PROP_POSITION,
          GST_PARAM_CONTROLLABLE | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_smpte_alpha_setcaps);
  trans_class->get_unit_size =
      GST_DEBUG_FUNCPTR (gst_smpte_alpha_get_unit_size);
  trans_class->transform = GST_DEBUG_FUNCPTR (gst_smpte_alpha_transform);
}

static gboolean
gst_smpte_alpha_update_mask (GstSMPTEAlpha * smpte, gint type, gint depth,
    gint width, gint height)
{
  GstMask *newmask;

  /* try to avoid regenerating the mask if we already have one that is
   * correct */
  if (smpte->mask) {
    if (smpte->type == type &&
        smpte->depth == depth &&
        smpte->width == width && smpte->height == height)
      return TRUE;
  }

  newmask = gst_mask_factory_new (type, depth, width, height);
  if (!newmask)
    goto mask_failed;

  if (smpte->mask)
    gst_mask_destroy (smpte->mask);

  smpte->mask = newmask;
  smpte->type = type;
  smpte->depth = depth;
  smpte->width = width;
  smpte->height = height;

  return TRUE;

  /* ERRORS */
mask_failed:
  {
    GST_ERROR_OBJECT (smpte, "failed to create a mask");
    return FALSE;
  }
}

static gboolean
gst_smpte_alpha_setcaps (GstBaseTransform * btrans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstSMPTEAlpha *smpte;
  GstStructure *structure;
  gboolean ret;
  gint width, height;
  guint32 fourcc;

  smpte = GST_SMPTE_ALPHA (btrans);

  structure = gst_caps_get_structure (incaps, 0);

  /* see if we can get essential info */
  ret = gst_structure_get_int (structure, "width", &width);
  ret &= gst_structure_get_int (structure, "height", &height);
  ret &= gst_structure_get_fourcc (structure, "format", &fourcc);
  if (!ret)
    goto no_dimensions;

  /* try to update the mask now, this will also adjust the width/height on
   * success */
  GST_OBJECT_LOCK (smpte);
  ret = gst_smpte_alpha_update_mask (smpte, smpte->type, smpte->depth,
      width, height);
  GST_OBJECT_UNLOCK (smpte);
  if (!ret)
    goto mask_failed;

  switch (fourcc) {
    case GST_MAKE_FOURCC ('A', 'Y', 'U', 'V'):
      smpte->format = GST_VIDEO_FORMAT_AYUV;
      break;
    case GST_MAKE_FOURCC ('I', '4', '2', '0'):
      smpte->format = GST_VIDEO_FORMAT_I420;
      break;
    default:
      goto unsupported_fourcc;
  }

  return ret;

  /* ERRORS */
no_dimensions:
  {
    GST_ERROR_OBJECT (smpte, "no width, height and fourcc given");
    return FALSE;
  }
mask_failed:
  {
    GST_ERROR_OBJECT (smpte, "failed creating the mask");
    return FALSE;
  }
unsupported_fourcc:
  {
    GST_ERROR_OBJECT (smpte, "unsupported fourcc %" GST_FOURCC_FORMAT,
        GST_FOURCC_ARGS (fourcc));
    return FALSE;
  }
}

static gboolean
gst_smpte_alpha_get_unit_size (GstBaseTransform * btrans, GstCaps * caps,
    guint * size)
{
  GstStructure *structure;
  gboolean ret;
  gint width, height;
  guint32 fourcc;

  structure = gst_caps_get_structure (caps, 0);

  ret = gst_structure_get_int (structure, "width", &width);
  ret &= gst_structure_get_int (structure, "height", &height);
  ret &= gst_structure_get_fourcc (structure, "format", &fourcc);
  if (ret) {
    switch (fourcc) {
      case GST_MAKE_FOURCC ('A', 'Y', 'U', 'V'):
        *size =
            gst_video_format_get_size (GST_VIDEO_FORMAT_AYUV, width, height);
        break;
      case GST_MAKE_FOURCC ('I', '4', '2', '0'):
        *size =
            gst_video_format_get_size (GST_VIDEO_FORMAT_I420, width, height);
        break;
      default:
        ret = FALSE;
        break;
    }
  }
  return ret;
}

static void
gst_smpte_alpha_init (GstSMPTEAlpha * smpte)
{
  smpte->type = DEFAULT_PROP_TYPE;
  smpte->border = DEFAULT_PROP_BORDER;
  smpte->depth = DEFAULT_PROP_DEPTH;
}

static void
gst_smpte_alpha_finalize (GstSMPTEAlpha * smpte)
{
  if (smpte->mask)
    gst_mask_destroy (smpte->mask);

  G_OBJECT_CLASS (parent_class)->finalize ((GObject *) smpte);
}

static void
gst_smpte_alpha_do_ayuv (GstSMPTEAlpha * smpte, guint8 * in, guint8 * out,
    GstMask * mask, gint width, gint height, gint border, gint pos)
{
  gint i, j;
  guint32 *maskp;
  gint value;
  gint min, max;

  if (border == 0)
    border++;

  min = pos - border;
  max = pos;
  GST_DEBUG_OBJECT (smpte, "pos %d, min %d, max %d, border %d", pos, min, max,
      border);

  maskp = mask->data;

  /* we basically copy the source to dest but we scale the alpha channel with
   * the mask */
  for (i = 0; i < height; i++) {
    for (j = 0; j < width; j++) {
      value = *maskp++;
      *out++ = (*in++ * ((CLAMP (value, min, max) - min) << 8) / border) >> 8;
      *out++ = *in++;
      *out++ = *in++;
      *out++ = *in++;
    }
  }
}

static void
gst_smpte_alpha_do_i420 (GstSMPTEAlpha * smpte, guint8 * in, guint8 * out,
    GstMask * mask, gint width, gint height, gint border, gint pos)
{
  guint8 *srcY;
  guint8 *srcU;
  guint8 *srcV;
  gint i, j;
  gint src_wrap, src_uv_wrap;
  gint y_stride, uv_stride;
  gboolean odd_width;
  guint32 *maskp;
  gint value;
  gint min, max;

  if (border == 0)
    border++;

  min = pos - border;
  max = pos;
  GST_DEBUG_OBJECT (smpte, "pos %d, min %d, max %d, border %d", pos, min, max,
      border);

  maskp = mask->data;

  y_stride = gst_video_format_get_row_stride (GST_VIDEO_FORMAT_I420, 0, width);
  uv_stride = gst_video_format_get_row_stride (GST_VIDEO_FORMAT_I420, 1, width);

  src_wrap = y_stride - width;
  src_uv_wrap = uv_stride - (width / 2);

  srcY = in;
  srcU = in + gst_video_format_get_component_offset (GST_VIDEO_FORMAT_I420,
      1, width, height);
  srcV = in + gst_video_format_get_component_offset (GST_VIDEO_FORMAT_I420,
      2, width, height);

  odd_width = (width % 2 != 0);

  for (i = 0; i < height; i++) {
    for (j = 0; j < width / 2; j++) {
      value = *maskp++;
      *out++ = (0xff * ((CLAMP (value, min, max) - min) << 8) / border) >> 8;
      *out++ = *srcY++;
      *out++ = *srcU;
      *out++ = *srcV;
      value = *maskp++;
      *out++ = (0xff * ((CLAMP (value, min, max) - min) << 8) / border) >> 8;
      *out++ = *srcY++;
      *out++ = *srcU++;
      *out++ = *srcV++;
    }
    /* Might have one odd column left to do */
    if (odd_width) {
      value = *maskp++;
      *out++ = (0xff * ((CLAMP (value, min, max) - min) << 8) / border) >> 8;
      *out++ = *srcY++;
      *out++ = *srcU;
      *out++ = *srcV;
    }
    if (i % 2 == 0) {
      srcU -= width / 2;
      srcV -= width / 2;
    } else {
      srcU += src_uv_wrap;
      srcV += src_uv_wrap;
    }
    srcY += src_wrap;
  }
}

static GstFlowReturn
gst_smpte_alpha_transform (GstBaseTransform * trans, GstBuffer * in,
    GstBuffer * out)
{
  GstSMPTEAlpha *smpte;
  GstClockTime timestamp, stream_time;
  gdouble position;
  gint border;

  smpte = GST_SMPTE_ALPHA (trans);

  /* first sync the controller to the current stream_time of the buffer */
  timestamp = GST_BUFFER_TIMESTAMP (in);
  stream_time =
      gst_segment_to_stream_time (&trans->segment, GST_FORMAT_TIME, timestamp);

  GST_DEBUG_OBJECT (smpte, "sync to %" GST_TIME_FORMAT,
      GST_TIME_ARGS (timestamp));

  if (GST_CLOCK_TIME_IS_VALID (stream_time))
    gst_object_sync_values (G_OBJECT (smpte), stream_time);

  /* these are the propertis we update with only the object lock, others are
   * only updated with the TRANSFORM_LOCK. */
  GST_OBJECT_LOCK (smpte);
  position = smpte->position;
  border = smpte->border;
  GST_OBJECT_UNLOCK (smpte);

  /* run the type specific filter code */
  switch (smpte->format) {
    case GST_VIDEO_FORMAT_I420:
      gst_smpte_alpha_do_i420 (smpte, GST_BUFFER_DATA (in),
          GST_BUFFER_DATA (out),
          smpte->mask, smpte->width, smpte->height,
          border, ((1 << smpte->depth) + border) * position);
      break;
    case GST_VIDEO_FORMAT_AYUV:
      gst_smpte_alpha_do_ayuv (smpte, GST_BUFFER_DATA (in),
          GST_BUFFER_DATA (out),
          smpte->mask, smpte->width, smpte->height,
          border, ((1 << smpte->depth) + border) * position);
      break;
    default:
      goto not_negotiated;
  }

  return GST_FLOW_OK;

  /* ERRORS */
not_negotiated:
  {
    GST_ELEMENT_ERROR (smpte, CORE, NEGOTIATION, (NULL),
        ("No input format negotiated"));
    return GST_FLOW_NOT_NEGOTIATED;
  }
}

static void
gst_smpte_alpha_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSMPTEAlpha *smpte;

  smpte = GST_SMPTE_ALPHA (object);

  switch (prop_id) {
    case PROP_TYPE:
      GST_BASE_TRANSFORM_LOCK (smpte);
      /* also lock with the object lock so that reading the property doesn't
       * have to wait for the transform lock */
      GST_OBJECT_LOCK (smpte);
      smpte->type = g_value_get_enum (value);
      GST_OBJECT_UNLOCK (smpte);
      gst_smpte_alpha_update_mask (smpte, smpte->type, smpte->depth,
          smpte->width, smpte->height);
      GST_BASE_TRANSFORM_UNLOCK (smpte);
      break;
    case PROP_BORDER:
      GST_OBJECT_LOCK (smpte);
      smpte->border = g_value_get_int (value);
      GST_OBJECT_UNLOCK (smpte);
      break;
    case PROP_DEPTH:
      GST_BASE_TRANSFORM_LOCK (smpte);
      /* also lock with the object lock so that reading the property doesn't
       * have to wait for the transform lock */
      GST_OBJECT_LOCK (smpte);
      smpte->depth = g_value_get_int (value);
      GST_OBJECT_UNLOCK (smpte);
      gst_smpte_alpha_update_mask (smpte, smpte->type, smpte->depth,
          smpte->width, smpte->height);
      GST_BASE_TRANSFORM_UNLOCK (smpte);
      break;
    case PROP_POSITION:
      GST_OBJECT_LOCK (smpte);
      smpte->position = g_value_get_double (value);
      GST_OBJECT_UNLOCK (smpte);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_smpte_alpha_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstSMPTEAlpha *smpte;

  smpte = GST_SMPTE_ALPHA (object);

  switch (prop_id) {
    case PROP_TYPE:
      GST_OBJECT_LOCK (smpte);
      g_value_set_enum (value, smpte->type);
      GST_OBJECT_UNLOCK (smpte);
      break;
    case PROP_BORDER:
      GST_OBJECT_LOCK (smpte);
      g_value_set_int (value, smpte->border);
      GST_OBJECT_UNLOCK (smpte);
      break;
    case PROP_DEPTH:
      GST_OBJECT_LOCK (smpte);
      g_value_set_int (value, smpte->depth);
      GST_OBJECT_UNLOCK (smpte);
      break;
    case PROP_POSITION:
      GST_OBJECT_LOCK (smpte);
      g_value_set_double (value, smpte->position);
      GST_OBJECT_UNLOCK (smpte);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

gboolean
gst_smpte_alpha_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_smpte_alpha_debug, "smptealpha", 0,
      "SMPTE alpha effect");

  /* initialize gst controller library */
  gst_controller_init (NULL, NULL);

  return gst_element_register (plugin, "smptealpha", GST_RANK_NONE,
      GST_TYPE_SMPTE_ALPHA);
}
