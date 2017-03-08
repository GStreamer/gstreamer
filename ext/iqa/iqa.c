/* Image Quality Assessment plugin
 * Copyright (C) 2015 Mathieu Duponchelle <mathieu.duponchelle@collabora.co.uk>
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

/**
 * SECTION:element-iqa
 * @title: iqa
 * @short_description: Image Quality Assessment plugin.
 *
 * IQA will perform full reference image quality assessment, with the
 * first added pad being the reference.
 *
 * It will perform comparisons on video streams with the same geometry.
 *
 * The image output will be the heat map of differences, between
 * the two pads with the highest measured difference.
 *
 * For each reference frame, IQA will post a message containing
 * a structure named IQA.
 *
 * The only metric supported for now is "dssim", which will be available
 * if https://github.com/pornel/dssim was installed on the system
 * at the time that plugin was compiled.
 *
 * For each metric activated, this structure will contain another
 * structure, named after the metric.
 *
 * The message will also contain a "time" field.
 *
 * For example, if do-dssim is set to true, and there are
 * two compared streams, the emitted structure will look like this:
 *
 * IQA, dssim=(structure)"dssim\,\ sink_1\=\(double\)0.053621271267184856\,\
 * sink_2\=\(double\)0.0082939683976297474\;",
 * time=(guint64)0;
 *
 * ## Example launch line
 * |[
 * gst-launch-1.0 -m uridecodebin uri=file:///test/file/1 ! iqa name=iqa do-dssim=true \
 * ! videoconvert ! autovideosink uridecodebin uri=file:///test/file/2 ! iqa.
 * ]| This pipeline will output messages to the console for each set of compared frames.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "iqa.h"

#ifdef HAVE_DSSIM
#include "dssim.h"
#endif

GST_DEBUG_CATEGORY_STATIC (gst_iqa_debug);
#define GST_CAT_DEFAULT gst_iqa_debug

#define SINK_FORMATS " { AYUV, BGRA, ARGB, RGBA, ABGR, Y444, Y42B, YUY2, UYVY, "\
                "   YVYU, I420, YV12, NV12, NV21, Y41B, RGB, BGR, xRGB, xBGR, "\
                "   RGBx, BGRx } "

#define SRC_FORMAT " { RGBA } "

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (SRC_FORMAT))
    );

enum
{
  PROP_0,
  PROP_DO_SSIM,
  PROP_LAST,
};

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (SINK_FORMATS))
    );


/* GstIqa */

#define gst_iqa_parent_class parent_class
G_DEFINE_TYPE (GstIqa, gst_iqa, GST_TYPE_VIDEO_AGGREGATOR);

#ifdef HAVE_DSSIM
inline static unsigned char
to_byte (float in)
{
  if (in <= 0)
    return 0;
  if (in >= 255.f / 256.f)
    return 255;
  return in * 256.f;
}

static gboolean
do_dssim (GstIqa * self, GstVideoFrame * ref, GstVideoFrame * cmp,
    GstBuffer * outbuf, GstStructure * msg_structure, gchar * padname)
{
  dssim_attr *attr = dssim_create_attr ();
  gint y;
  unsigned char **ptrs, **ptrs2;
  GstMapInfo ref_info;
  GstMapInfo cmp_info;
  GstMapInfo out_info;
  dssim_image *ref_image;
  dssim_image *cmp_image;
  double dssim;
  dssim_ssim_map map_meta;
  float *map;
  gint i;
  dssim_rgba *out;
  GstStructure *dssim_structure;

  gst_structure_get (msg_structure, "dssim", GST_TYPE_STRUCTURE,
      &dssim_structure, NULL);

  dssim_set_save_ssim_maps (attr, 1, 1);
  if (ref->info.width != cmp->info.width ||
      ref->info.height != cmp->info.height) {
    GST_OBJECT_UNLOCK (self);

    GST_ELEMENT_ERROR (self, STREAM, FAILED,
        ("Video streams do not have the same sizes (add videoscale"
            " and force the sizes to be equal on all sink pads.)"),
        ("Reference width %d - compared width: %d. "
            "Reference height %d - compared height: %d",
            ref->info.width, cmp->info.width, ref->info.height,
            cmp->info.height));

    GST_OBJECT_LOCK (self);
    return FALSE;
  }

  gst_buffer_map (ref->buffer, &ref_info, GST_MAP_READ);
  gst_buffer_map (cmp->buffer, &cmp_info, GST_MAP_READ);
  gst_buffer_map (outbuf, &out_info, GST_MAP_WRITE);
  out = (dssim_rgba *) out_info.data;

  ptrs = g_malloc (sizeof (char **) * ref->info.height);

  for (y = 0; y < ref->info.height; y++) {
    ptrs[y] = ref_info.data + (ref->info.width * 4 * y);
  }

  ref_image =
      dssim_create_image (attr, ptrs, DSSIM_RGBA, ref->info.width,
      ref->info.height, 0.45455);

  ptrs2 = g_malloc (sizeof (char **) * cmp->info.height);

  for (y = 0; y < cmp->info.height; y++) {
    ptrs2[y] = cmp_info.data + (cmp->info.width * 4 * y);
  }

  cmp_image =
      dssim_create_image (attr, ptrs2, DSSIM_RGBA, cmp->info.width,
      cmp->info.height, 0.45455);
  dssim = dssim_compare (attr, ref_image, cmp_image);

  map_meta = dssim_pop_ssim_map (attr, 0, 0);

  if (dssim > self->max_dssim) {
    map = map_meta.data;

    for (i = 0; i < map_meta.width * map_meta.height; i++) {
      const float max = 1.0 - map[i];
      const float maxsq = max * max;
      out[i] = (dssim_rgba) {
      .r = to_byte (max * 3.0),.g = to_byte (maxsq * 6.0),.b =
            to_byte (max / ((1.0 - map_meta.dssim) * 4.0)),.a = 255,};
    }
    self->max_dssim = dssim;
  }

  gst_structure_set (dssim_structure, padname, G_TYPE_DOUBLE, dssim, NULL);
  gst_structure_set (msg_structure, "dssim", GST_TYPE_STRUCTURE,
      dssim_structure, NULL);
  gst_structure_free (dssim_structure);

  g_free (ptrs);
  g_free (ptrs2);
  gst_buffer_unmap (ref->buffer, &ref_info);
  gst_buffer_unmap (cmp->buffer, &cmp_info);
  gst_buffer_unmap (outbuf, &out_info);
  dssim_dealloc_image (ref_image);
  dssim_dealloc_image (cmp_image);
  dssim_dealloc_attr (attr);

  return TRUE;
}
#endif

static gboolean
compare_frames (GstIqa * self, GstVideoFrame * ref, GstVideoFrame * cmp,
    GstBuffer * outbuf, GstStructure * msg_structure, gchar * padname)
{
#ifdef HAVE_DSSIM
  if (self->do_dssim) {
    if (!do_dssim (self, ref, cmp, outbuf, msg_structure, padname))
      return FALSE;
  }
#endif

  return TRUE;
}

static GstFlowReturn
gst_iqa_aggregate_frames (GstVideoAggregator * vagg, GstBuffer * outbuf)
{
  GList *l;
  GstVideoFrame *ref_frame = NULL;
  GstIqa *self = GST_IQA (vagg);
  GstStructure *msg_structure = gst_structure_new_empty ("IQA");
  GstMessage *m = gst_message_new_element (GST_OBJECT (self), msg_structure);
  GstAggregator *agg = GST_AGGREGATOR (vagg);

  if (self->do_dssim) {
    gst_structure_set (msg_structure, "dssim", GST_TYPE_STRUCTURE,
        gst_structure_new_empty ("dssim"), NULL);
    self->max_dssim = 0.0;
  }

  GST_OBJECT_LOCK (vagg);
  for (l = GST_ELEMENT (vagg)->sinkpads; l; l = l->next) {
    GstVideoAggregatorPad *pad = l->data;

    if (pad->aggregated_frame != NULL) {
      if (!ref_frame) {
        ref_frame = pad->aggregated_frame;
      } else {
        gboolean res;
        gchar *padname = gst_pad_get_name (pad);
        GstVideoFrame *cmp_frame = pad->aggregated_frame;

        res = compare_frames (self, ref_frame, cmp_frame, outbuf, msg_structure,
            padname);
        g_free (padname);

        if (!res)
          goto failed;
      }
    }
  }

  GST_OBJECT_UNLOCK (vagg);

  /* We only post the message here, because we can't post it while the object
   * is locked.
   */
  gst_structure_set (msg_structure, "time", GST_TYPE_CLOCK_TIME,
      agg->segment.position, NULL);
  gst_element_post_message (GST_ELEMENT (self), m);
  return GST_FLOW_OK;

failed:
  GST_OBJECT_UNLOCK (vagg);

  return GST_FLOW_ERROR;
}

static void
_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstIqa *self = GST_IQA (object);

  switch (prop_id) {
    case PROP_DO_SSIM:
      self->do_dssim = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstIqa *self = GST_IQA (object);

  switch (prop_id) {
    case PROP_DO_SSIM:
      g_value_set_boolean (value, self->do_dssim);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* GObject boilerplate */
static void
gst_iqa_class_init (GstIqaClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  GstVideoAggregatorClass *videoaggregator_class =
      (GstVideoAggregatorClass *) klass;

  videoaggregator_class->aggregate_frames = gst_iqa_aggregate_frames;

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_factory));

  gobject_class->set_property = _set_property;
  gobject_class->get_property = _get_property;

#ifdef HAVE_DSSIM
  g_object_class_install_property (gobject_class, PROP_DO_SSIM,
      g_param_spec_boolean ("do-dssim", "do-dssim",
          "Run structural similarity checks", FALSE, G_PARAM_READWRITE));
#endif

  gst_element_class_set_static_metadata (gstelement_class, "Iqa",
      "Filter/Analyzer/Video",
      "Provides various Image Quality Assessment metrics",
      "Mathieu Duponchelle <mathieu.duponchelle@collabora.co.uk>");
}

static void
gst_iqa_init (GstIqa * self)
{
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_iqa_debug, "iqa", 0, "iqa");

  return gst_element_register (plugin, "iqa", GST_RANK_PRIMARY, GST_TYPE_IQA);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    iqa,
    "Iqa", plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN)
