/* Generic video aggregator plugin
 * Copyright (C) 2004, 2008 Wim Taymans <wim@fluendo.com>
 * Copyright (C) 2010 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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
 * SECTION:gstvideoaggregator
 * @title: GstVideoAggregator
 * @short_description: Base class for video aggregators
 *
 * VideoAggregator can accept AYUV, ARGB and BGRA video streams. For each of the requested
 * sink pads it will compare the incoming geometry and framerate to define the
 * output parameters. Indeed output video frames will have the geometry of the
 * biggest incoming video stream and the framerate of the fastest incoming one.
 *
 * VideoAggregator will do colorspace conversion.
 *
 * Zorder for each input stream can be configured on the
 * #GstVideoAggregatorPad.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "gstvideoaggregator.h"
#include "gstvideoaggregatorpad.h"

GST_DEBUG_CATEGORY_STATIC (gst_video_aggregator_debug);
#define GST_CAT_DEFAULT gst_video_aggregator_debug

/* Needed prototypes */
static void gst_video_aggregator_reset_qos (GstVideoAggregator * vagg);

/****************************************
 * GstVideoAggregatorPad implementation *
 ****************************************/

#define DEFAULT_PAD_ZORDER 0
#define DEFAULT_PAD_IGNORE_EOS FALSE
enum
{
  PROP_PAD_0,
  PROP_PAD_ZORDER,
  PROP_PAD_IGNORE_EOS,
};


struct _GstVideoAggregatorPadPrivate
{
  /* Converter, if NULL no conversion is done */
  GstVideoConverter *convert;

  /* caps used for conversion if needed */
  GstVideoInfo conversion_info;
  GstBuffer *converted_buffer;

  GstClockTime start_time;
  GstClockTime end_time;
};


G_DEFINE_TYPE (GstVideoAggregatorPad, gst_video_aggregator_pad,
    GST_TYPE_AGGREGATOR_PAD);

static void
gst_video_aggregator_pad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVideoAggregatorPad *pad = GST_VIDEO_AGGREGATOR_PAD (object);

  switch (prop_id) {
    case PROP_PAD_ZORDER:
      g_value_set_uint (value, pad->zorder);
      break;
    case PROP_PAD_IGNORE_EOS:
      g_value_set_boolean (value, pad->ignore_eos);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static int
pad_zorder_compare (const GstVideoAggregatorPad * pad1,
    const GstVideoAggregatorPad * pad2)
{
  return pad1->zorder - pad2->zorder;
}

static void
gst_video_aggregator_pad_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVideoAggregatorPad *pad = GST_VIDEO_AGGREGATOR_PAD (object);
  GstVideoAggregator *vagg =
      GST_VIDEO_AGGREGATOR (gst_pad_get_parent (GST_PAD (pad)));

  switch (prop_id) {
    case PROP_PAD_ZORDER:
      GST_OBJECT_LOCK (vagg);
      pad->zorder = g_value_get_uint (value);
      GST_ELEMENT (vagg)->sinkpads = g_list_sort (GST_ELEMENT (vagg)->sinkpads,
          (GCompareFunc) pad_zorder_compare);
      GST_OBJECT_UNLOCK (vagg);
      break;
    case PROP_PAD_IGNORE_EOS:
      pad->ignore_eos = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  gst_object_unref (vagg);
}

static GstFlowReturn
_flush_pad (GstAggregatorPad * aggpad, GstAggregator * aggregator)
{
  GstVideoAggregator *vagg = GST_VIDEO_AGGREGATOR (aggregator);
  GstVideoAggregatorPad *pad = GST_VIDEO_AGGREGATOR_PAD (aggpad);

  gst_video_aggregator_reset_qos (vagg);
  gst_buffer_replace (&pad->buffer, NULL);
  pad->priv->start_time = -1;
  pad->priv->end_time = -1;

  return GST_FLOW_OK;
}

static gboolean
gst_video_aggregator_pad_set_info (GstVideoAggregatorPad * pad,
    GstVideoAggregator * vagg G_GNUC_UNUSED,
    GstVideoInfo * current_info, GstVideoInfo * wanted_info)
{
  gchar *colorimetry, *best_colorimetry;
  const gchar *chroma, *best_chroma;

  if (!current_info->finfo)
    return TRUE;

  if (GST_VIDEO_INFO_FORMAT (current_info) == GST_VIDEO_FORMAT_UNKNOWN)
    return TRUE;

  if (pad->priv->convert)
    gst_video_converter_free (pad->priv->convert);

  pad->priv->convert = NULL;

  colorimetry = gst_video_colorimetry_to_string (&(current_info->colorimetry));
  chroma = gst_video_chroma_to_string (current_info->chroma_site);

  best_colorimetry =
      gst_video_colorimetry_to_string (&(wanted_info->colorimetry));
  best_chroma = gst_video_chroma_to_string (wanted_info->chroma_site);

  if (GST_VIDEO_INFO_FORMAT (wanted_info) !=
      GST_VIDEO_INFO_FORMAT (current_info)
      || g_strcmp0 (colorimetry, best_colorimetry)
      || g_strcmp0 (chroma, best_chroma)) {
    GstVideoInfo tmp_info;

    /* Initialize with the wanted video format and our original width and
     * height as we don't want to rescale. Then copy over the wanted
     * colorimetry, and chroma-site and our current pixel-aspect-ratio
     * and other relevant fields.
     */
    gst_video_info_set_format (&tmp_info, GST_VIDEO_INFO_FORMAT (wanted_info),
        current_info->width, current_info->height);
    tmp_info.chroma_site = wanted_info->chroma_site;
    tmp_info.colorimetry = wanted_info->colorimetry;
    tmp_info.par_n = current_info->par_n;
    tmp_info.par_d = current_info->par_d;
    tmp_info.fps_n = current_info->fps_n;
    tmp_info.fps_d = current_info->fps_d;
    tmp_info.flags = current_info->flags;
    tmp_info.interlace_mode = current_info->interlace_mode;

    GST_DEBUG_OBJECT (pad, "This pad will be converted from %d to %d",
        GST_VIDEO_INFO_FORMAT (current_info),
        GST_VIDEO_INFO_FORMAT (&tmp_info));
    pad->priv->convert =
        gst_video_converter_new (current_info, &tmp_info, NULL);
    pad->priv->conversion_info = tmp_info;
    if (!pad->priv->convert) {
      g_free (colorimetry);
      g_free (best_colorimetry);
      GST_WARNING_OBJECT (pad, "No path found for conversion");
      return FALSE;
    }
  } else {
    GST_DEBUG_OBJECT (pad, "This pad will not need conversion");
  }
  g_free (colorimetry);
  g_free (best_colorimetry);

  return TRUE;
}

static void
gst_video_aggregator_pad_finalize (GObject * o)
{
  GstVideoAggregatorPad *vaggpad = GST_VIDEO_AGGREGATOR_PAD (o);

  if (vaggpad->priv->convert)
    gst_video_converter_free (vaggpad->priv->convert);
  vaggpad->priv->convert = NULL;

  G_OBJECT_CLASS (gst_video_aggregator_pad_parent_class)->finalize (o);
}

static gboolean
gst_video_aggregator_pad_prepare_frame (GstVideoAggregatorPad * pad,
    GstVideoAggregator * vagg)
{
  guint outsize;
  GstVideoFrame *converted_frame;
  GstBuffer *converted_buf = NULL;
  GstVideoFrame *frame;
  static GstAllocationParams params = { 0, 15, 0, 0, };

  if (!pad->buffer)
    return TRUE;

  frame = g_slice_new0 (GstVideoFrame);

  if (!gst_video_frame_map (frame, &pad->buffer_vinfo, pad->buffer,
          GST_MAP_READ)) {
    GST_WARNING_OBJECT (vagg, "Could not map input buffer");
    return FALSE;
  }

  if (pad->priv->convert) {
    gint converted_size;

    converted_frame = g_slice_new0 (GstVideoFrame);

    /* We wait until here to set the conversion infos, in case vagg->info changed */
    converted_size = pad->priv->conversion_info.size;
    outsize = GST_VIDEO_INFO_SIZE (&vagg->info);
    converted_size = converted_size > outsize ? converted_size : outsize;
    converted_buf = gst_buffer_new_allocate (NULL, converted_size, &params);

    if (!gst_video_frame_map (converted_frame, &(pad->priv->conversion_info),
            converted_buf, GST_MAP_READWRITE)) {
      GST_WARNING_OBJECT (vagg, "Could not map converted frame");

      g_slice_free (GstVideoFrame, converted_frame);
      gst_video_frame_unmap (frame);
      g_slice_free (GstVideoFrame, frame);
      return FALSE;
    }

    gst_video_converter_frame (pad->priv->convert, frame, converted_frame);
    pad->priv->converted_buffer = converted_buf;
    gst_video_frame_unmap (frame);
    g_slice_free (GstVideoFrame, frame);
  } else {
    converted_frame = frame;
  }

  pad->aggregated_frame = converted_frame;

  return TRUE;
}

static void
gst_video_aggregator_pad_clean_frame (GstVideoAggregatorPad * pad,
    GstVideoAggregator * vagg)
{
  if (pad->aggregated_frame) {
    gst_video_frame_unmap (pad->aggregated_frame);
    g_slice_free (GstVideoFrame, pad->aggregated_frame);
    pad->aggregated_frame = NULL;
  }

  if (pad->priv->converted_buffer) {
    gst_buffer_unref (pad->priv->converted_buffer);
    pad->priv->converted_buffer = NULL;
  }
}

static void
gst_video_aggregator_pad_class_init (GstVideoAggregatorPadClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstAggregatorPadClass *aggpadclass = (GstAggregatorPadClass *) klass;

  gobject_class->set_property = gst_video_aggregator_pad_set_property;
  gobject_class->get_property = gst_video_aggregator_pad_get_property;
  gobject_class->finalize = gst_video_aggregator_pad_finalize;

  g_object_class_install_property (gobject_class, PROP_PAD_ZORDER,
      g_param_spec_uint ("zorder", "Z-Order", "Z Order of the picture",
          0, G_MAXUINT, DEFAULT_PAD_ZORDER,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PAD_IGNORE_EOS,
      g_param_spec_boolean ("ignore-eos", "Ignore EOS", "Aggregate the last "
          "frame on pads that are EOS till they are released",
          DEFAULT_PAD_IGNORE_EOS,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));

  g_type_class_add_private (klass, sizeof (GstVideoAggregatorPadPrivate));

  aggpadclass->flush = GST_DEBUG_FUNCPTR (_flush_pad);
  klass->set_info = GST_DEBUG_FUNCPTR (gst_video_aggregator_pad_set_info);
  klass->prepare_frame =
      GST_DEBUG_FUNCPTR (gst_video_aggregator_pad_prepare_frame);
  klass->clean_frame = GST_DEBUG_FUNCPTR (gst_video_aggregator_pad_clean_frame);
}

static void
gst_video_aggregator_pad_init (GstVideoAggregatorPad * vaggpad)
{
  vaggpad->priv =
      G_TYPE_INSTANCE_GET_PRIVATE (vaggpad, GST_TYPE_VIDEO_AGGREGATOR_PAD,
      GstVideoAggregatorPadPrivate);

  vaggpad->zorder = DEFAULT_PAD_ZORDER;
  vaggpad->ignore_eos = DEFAULT_PAD_IGNORE_EOS;
  vaggpad->aggregated_frame = NULL;
  vaggpad->priv->converted_buffer = NULL;

  vaggpad->priv->convert = NULL;
}

/*********************************
 * GstChildProxy implementation  *
 *********************************/
static GObject *
gst_video_aggregator_child_proxy_get_child_by_index (GstChildProxy *
    child_proxy, guint index)
{
  GObject *obj;
  GstVideoAggregator *vagg = GST_VIDEO_AGGREGATOR (child_proxy);

  GST_OBJECT_LOCK (vagg);
  if ((obj = g_list_nth_data (GST_ELEMENT (vagg)->sinkpads, index)))
    g_object_ref (obj);
  GST_OBJECT_UNLOCK (vagg);

  return obj;
}

static guint
gst_video_aggregator_child_proxy_get_children_count (GstChildProxy *
    child_proxy)
{
  guint count = 0;

  GST_OBJECT_LOCK (child_proxy);
  count = GST_ELEMENT (child_proxy)->numsinkpads;
  GST_OBJECT_UNLOCK (child_proxy);

  GST_INFO_OBJECT (child_proxy, "Children Count: %d", count);

  return count;
}

static void
gst_video_aggregator_child_proxy_init (gpointer g_iface, gpointer iface_data)
{
  GstChildProxyInterface *iface = g_iface;

  GST_INFO ("intializing child proxy interface");
  iface->get_child_by_index =
      gst_video_aggregator_child_proxy_get_child_by_index;
  iface->get_children_count =
      gst_video_aggregator_child_proxy_get_children_count;
}

/**************************************
 * GstVideoAggregator implementation  *
 **************************************/

#define GST_VIDEO_AGGREGATOR_GET_LOCK(vagg) (&GST_VIDEO_AGGREGATOR(vagg)->priv->lock)

#define GST_VIDEO_AGGREGATOR_LOCK(vagg)   G_STMT_START {       \
  GST_LOG_OBJECT (vagg, "Taking EVENT lock from thread %p",    \
        g_thread_self());                                      \
  g_mutex_lock(GST_VIDEO_AGGREGATOR_GET_LOCK(vagg));           \
  GST_LOG_OBJECT (vagg, "Took EVENT lock from thread %p",      \
        g_thread_self());                                      \
  } G_STMT_END

#define GST_VIDEO_AGGREGATOR_UNLOCK(vagg)   G_STMT_START {     \
  GST_LOG_OBJECT (vagg, "Releasing EVENT lock from thread %p", \
        g_thread_self());                                      \
  g_mutex_unlock(GST_VIDEO_AGGREGATOR_GET_LOCK(vagg));         \
  GST_LOG_OBJECT (vagg, "Took EVENT lock from thread %p",      \
        g_thread_self());                                      \
  } G_STMT_END


struct _GstVideoAggregatorPrivate
{
  /* Lock to prevent the state to change while aggregating */
  GMutex lock;

  /* Current downstream segment */
  GstClockTime ts_offset;
  guint64 nframes;

  /* QoS stuff */
  gdouble proportion;
  GstClockTime earliest_time;
  guint64 qos_processed, qos_dropped;

  /* current caps */
  GstCaps *current_caps;

  gboolean live;
};

/* Can't use the G_DEFINE_TYPE macros because we need the
 * videoaggregator class in the _init to be able to set
 * the sink pad non-alpha caps. Using the G_DEFINE_TYPE there
 * seems to be no way of getting the real class being initialized */
static void gst_video_aggregator_init (GstVideoAggregator * self,
    GstVideoAggregatorClass * klass);
static void gst_video_aggregator_class_init (GstVideoAggregatorClass * klass);
static gpointer gst_video_aggregator_parent_class = NULL;

GType
gst_video_aggregator_get_type (void)
{
  static volatile gsize g_define_type_id_volatile = 0;

  if (g_once_init_enter (&g_define_type_id_volatile)) {
    GType g_define_type_id = g_type_register_static_simple (GST_TYPE_AGGREGATOR,
        g_intern_static_string ("GstVideoAggregator"),
        sizeof (GstVideoAggregatorClass),
        (GClassInitFunc) gst_video_aggregator_class_init,
        sizeof (GstVideoAggregator),
        (GInstanceInitFunc) gst_video_aggregator_init,
        (GTypeFlags) G_TYPE_FLAG_ABSTRACT);
    {
      G_IMPLEMENT_INTERFACE (GST_TYPE_CHILD_PROXY,
          gst_video_aggregator_child_proxy_init);
    }
    g_once_init_leave (&g_define_type_id_volatile, g_define_type_id);
  }
  return g_define_type_id_volatile;
}

static void
gst_video_aggregator_find_best_format (GstVideoAggregator * vagg,
    GstCaps * downstream_caps, GstVideoInfo * best_info,
    gboolean * at_least_one_alpha)
{
  GList *tmp;
  GstCaps *possible_caps;
  GstVideoAggregatorPad *pad;
  gboolean need_alpha = FALSE;
  gint best_format_number = 0;
  GHashTable *formats_table = g_hash_table_new (g_direct_hash, g_direct_equal);

  GST_OBJECT_LOCK (vagg);
  for (tmp = GST_ELEMENT (vagg)->sinkpads; tmp; tmp = tmp->next) {
    GstStructure *s;
    gint format_number;

    pad = tmp->data;

    if (!pad->info.finfo)
      continue;

    if (pad->info.finfo->flags & GST_VIDEO_FORMAT_FLAG_ALPHA)
      *at_least_one_alpha = TRUE;

    /* If we want alpha, disregard all the other formats */
    if (need_alpha && !(pad->info.finfo->flags & GST_VIDEO_FORMAT_FLAG_ALPHA))
      continue;

    /* This can happen if we release a pad and another pad hasn't been negotiated_caps yet */
    if (GST_VIDEO_INFO_FORMAT (&pad->info) == GST_VIDEO_FORMAT_UNKNOWN)
      continue;

    possible_caps = gst_video_info_to_caps (&pad->info);

    s = gst_caps_get_structure (possible_caps, 0);
    gst_structure_remove_fields (s, "width", "height", "framerate",
        "pixel-aspect-ratio", "interlace-mode", NULL);

    /* Can downstream accept this format ? */
    if (!gst_caps_can_intersect (downstream_caps, possible_caps)) {
      gst_caps_unref (possible_caps);
      continue;
    }

    gst_caps_unref (possible_caps);

    format_number =
        GPOINTER_TO_INT (g_hash_table_lookup (formats_table,
            GINT_TO_POINTER (GST_VIDEO_INFO_FORMAT (&pad->info))));
    format_number += 1;

    g_hash_table_replace (formats_table,
        GINT_TO_POINTER (GST_VIDEO_INFO_FORMAT (&pad->info)),
        GINT_TO_POINTER (format_number));

    /* If that pad is the first with alpha, set it as the new best format */
    if (!need_alpha && (pad->info.finfo->flags & GST_VIDEO_FORMAT_FLAG_ALPHA)) {
      need_alpha = TRUE;
      *best_info = pad->info;
      best_format_number = format_number;
    } else if (format_number > best_format_number) {
      *best_info = pad->info;
      best_format_number = format_number;
    }
  }
  GST_OBJECT_UNLOCK (vagg);

  g_hash_table_unref (formats_table);
}

/* WITH GST_VIDEO_AGGREGATOR_LOCK TAKEN */
static gboolean
gst_video_aggregator_src_setcaps (GstVideoAggregator * vagg, GstCaps * caps)
{
  GstAggregator *agg = GST_AGGREGATOR (vagg);
  gboolean ret = FALSE;
  GstVideoInfo info;

  GstPad *pad = GST_AGGREGATOR (vagg)->srcpad;

  GST_INFO_OBJECT (pad, "set src caps: %" GST_PTR_FORMAT, caps);

  if (!gst_video_info_from_caps (&info, caps))
    goto done;

  ret = TRUE;

  if (GST_VIDEO_INFO_FPS_N (&vagg->info) != GST_VIDEO_INFO_FPS_N (&info) ||
      GST_VIDEO_INFO_FPS_D (&vagg->info) != GST_VIDEO_INFO_FPS_D (&info)) {
    if (agg->segment.position != -1) {
      vagg->priv->nframes = 0;
      /* The timestamp offset will be updated based on the
       * segment position the next time we aggregate */
      GST_DEBUG_OBJECT (vagg,
          "Resetting frame counter because of framerate change");
    }
    gst_video_aggregator_reset_qos (vagg);
  }

  vagg->info = info;

  if (vagg->priv->current_caps == NULL ||
      gst_caps_is_equal (caps, vagg->priv->current_caps) == FALSE) {
    GstClockTime latency;

    gst_caps_replace (&vagg->priv->current_caps, caps);
    GST_VIDEO_AGGREGATOR_UNLOCK (vagg);

    gst_aggregator_set_src_caps (agg, caps);
    latency = gst_util_uint64_scale (GST_SECOND,
        GST_VIDEO_INFO_FPS_D (&info), GST_VIDEO_INFO_FPS_N (&info));
    gst_aggregator_set_latency (agg, latency, latency);

    GST_VIDEO_AGGREGATOR_LOCK (vagg);
  }

done:
  return ret;
}

static GstCaps *
gst_video_aggregator_default_fixate_caps (GstVideoAggregator * vagg,
    GstCaps * caps)
{
  gint best_width = -1, best_height = -1;
  gint best_fps_n = -1, best_fps_d = -1;
  gdouble best_fps = -1.;
  GstStructure *s;
  GList *l;

  GST_OBJECT_LOCK (vagg);
  for (l = GST_ELEMENT (vagg)->sinkpads; l; l = l->next) {
    GstVideoAggregatorPad *mpad = l->data;
    gint fps_n, fps_d;
    gint width, height;
    gdouble cur_fps;

    fps_n = GST_VIDEO_INFO_FPS_N (&mpad->info);
    fps_d = GST_VIDEO_INFO_FPS_D (&mpad->info);
    width = GST_VIDEO_INFO_WIDTH (&mpad->info);
    height = GST_VIDEO_INFO_HEIGHT (&mpad->info);

    if (width == 0 || height == 0)
      continue;

    if (best_width < width)
      best_width = width;
    if (best_height < height)
      best_height = height;

    if (fps_d == 0)
      cur_fps = 0.0;
    else
      gst_util_fraction_to_double (fps_n, fps_d, &cur_fps);

    if (best_fps < cur_fps) {
      best_fps = cur_fps;
      best_fps_n = fps_n;
      best_fps_d = fps_d;
    }
  }
  GST_OBJECT_UNLOCK (vagg);

  if (best_fps_n <= 0 || best_fps_d <= 0 || best_fps == 0.0) {
    best_fps_n = 25;
    best_fps_d = 1;
    best_fps = 25.0;
  }

  s = gst_caps_get_structure (caps, 0);
  gst_structure_fixate_field_nearest_int (s, "width", best_width);
  gst_structure_fixate_field_nearest_int (s, "height", best_height);
  gst_structure_fixate_field_nearest_fraction (s, "framerate", best_fps_n,
      best_fps_d);
  if (gst_structure_has_field (s, "pixel-aspect-ratio"))
    gst_structure_fixate_field_nearest_fraction (s, "pixel-aspect-ratio", 1, 1);
  caps = gst_caps_fixate (caps);

  return caps;
}

static GstCaps *
gst_video_aggregator_default_update_caps (GstVideoAggregator * vagg,
    GstCaps * caps, GstCaps * filter)
{
  GstVideoAggregatorClass *vagg_klass = GST_VIDEO_AGGREGATOR_GET_CLASS (vagg);
  GstCaps *ret, *best_format_caps;
  gboolean at_least_one_alpha = FALSE;
  GstVideoFormat best_format;
  GstVideoInfo best_info;

  best_format = GST_VIDEO_FORMAT_UNKNOWN;
  gst_video_info_init (&best_info);

  if (vagg_klass->find_best_format) {
    vagg_klass->find_best_format (vagg, caps, &best_info, &at_least_one_alpha);

    best_format = GST_VIDEO_INFO_FORMAT (&best_info);
  }

  if (best_format == GST_VIDEO_FORMAT_UNKNOWN) {
    GstCaps *tmp = gst_caps_fixate (gst_caps_ref (caps));
    gst_video_info_from_caps (&best_info, tmp);
    best_format = GST_VIDEO_INFO_FORMAT (&best_info);
    gst_caps_unref (tmp);
  }

  GST_DEBUG_OBJECT (vagg,
      "The output format will now be : %d with chroma : %s",
      best_format, gst_video_chroma_to_string (best_info.chroma_site));

  best_format_caps = gst_caps_copy (caps);
  gst_caps_set_simple (best_format_caps, "format", G_TYPE_STRING,
      gst_video_format_to_string (best_format), "chroma-site", G_TYPE_STRING,
      gst_video_chroma_to_string (best_info.chroma_site), NULL);
  ret = gst_caps_merge (best_format_caps, gst_caps_ref (caps));

  if (filter) {
    GstCaps *tmp;
    tmp = gst_caps_intersect (ret, filter);
    gst_caps_unref (ret);
    ret = tmp;
  }

  return ret;
}

/* WITH GST_VIDEO_AGGREGATOR_LOCK TAKEN */
static gboolean
gst_video_aggregator_update_src_caps (GstVideoAggregator * vagg)
{
  GstVideoAggregatorClass *vagg_klass = GST_VIDEO_AGGREGATOR_GET_CLASS (vagg);
  GstVideoAggregatorPadClass *vaggpad_klass = g_type_class_peek
      (GST_AGGREGATOR_GET_CLASS (vagg)->sinkpads_type);
  GstAggregator *agg = GST_AGGREGATOR (vagg);
  gboolean ret = TRUE, at_least_one_pad_configured = FALSE;
  gboolean at_least_one_alpha = FALSE;
  GstCaps *downstream_caps;
  GList *l;

  downstream_caps = gst_pad_get_allowed_caps (agg->srcpad);

  if (!downstream_caps || gst_caps_is_empty (downstream_caps)) {
    GST_INFO_OBJECT (vagg, "No downstream caps found %"
        GST_PTR_FORMAT, downstream_caps);
    if (downstream_caps)
      gst_caps_unref (downstream_caps);
    return FALSE;
  }

  GST_OBJECT_LOCK (vagg);
  for (l = GST_ELEMENT (vagg)->sinkpads; l; l = l->next) {
    GstVideoAggregatorPad *mpad = l->data;

    if (GST_VIDEO_INFO_WIDTH (&mpad->info) == 0
        || GST_VIDEO_INFO_HEIGHT (&mpad->info) == 0)
      continue;

    if (mpad->info.finfo->flags & GST_VIDEO_FORMAT_FLAG_ALPHA)
      at_least_one_alpha = TRUE;

    at_least_one_pad_configured = TRUE;
  }
  GST_OBJECT_UNLOCK (vagg);

  if (at_least_one_pad_configured) {
    GstCaps *caps, *peercaps;

    peercaps = gst_pad_peer_query_caps (agg->srcpad, NULL);

    g_assert (vagg_klass->update_caps);
    GST_DEBUG_OBJECT (vagg, "updating caps from %" GST_PTR_FORMAT,
        downstream_caps);
    GST_DEBUG_OBJECT (vagg, "       with filter %" GST_PTR_FORMAT, peercaps);
    if (!(caps = vagg_klass->update_caps (vagg, downstream_caps, peercaps)) ||
        gst_caps_is_empty (caps)) {
      GST_WARNING_OBJECT (vagg, "Subclass failed to update provided caps");
      gst_caps_unref (downstream_caps);
      if (peercaps)
        gst_caps_unref (peercaps);
      ret = FALSE;
      goto done;
    }
    GST_DEBUG_OBJECT (vagg, "               to %" GST_PTR_FORMAT, caps);

    gst_caps_unref (downstream_caps);
    if (peercaps)
      gst_caps_unref (peercaps);

    if (!gst_caps_is_fixed (caps)) {
      g_assert (vagg_klass->fixate_caps);

      caps = gst_caps_make_writable (caps);
      GST_DEBUG_OBJECT (vagg, "fixate caps from %" GST_PTR_FORMAT, caps);
      if (!(caps = vagg_klass->fixate_caps (vagg, caps))) {
        GST_WARNING_OBJECT (vagg, "Subclass failed to fixate provided caps");
        ret = FALSE;
        goto done;
      }
      GST_DEBUG_OBJECT (vagg, "             to %" GST_PTR_FORMAT, caps);
    }

    {
      const GstVideoFormatInfo *finfo;
      const gchar *v_format_str;
      GstVideoFormat v_format;
      GstStructure *s;

      s = gst_caps_get_structure (caps, 0);
      v_format_str = gst_structure_get_string (s, "format");
      g_return_val_if_fail (v_format_str != NULL, FALSE);
      v_format = gst_video_format_from_string (v_format_str);
      g_return_val_if_fail (v_format != GST_VIDEO_FORMAT_UNKNOWN, FALSE);
      finfo = gst_video_format_get_info (v_format);
      g_return_val_if_fail (finfo != NULL, FALSE);

      if (at_least_one_alpha && !(finfo->flags & GST_VIDEO_FORMAT_FLAG_ALPHA)) {
        GST_ELEMENT_ERROR (vagg, CORE, NEGOTIATION,
            ("At least one of the input pads contains alpha, but configured caps don't support alpha."),
            ("Either convert your inputs to not contain alpha or add a videoconvert after the aggregator"));
        ret = FALSE;
        goto done;
      }
    }

    gst_video_info_from_caps (&vagg->info, caps);

    if (vaggpad_klass->set_info) {
      /* Then browse the sinks once more, setting or unsetting conversion if needed */
      for (l = GST_ELEMENT (vagg)->sinkpads; l; l = l->next) {
        GstVideoAggregatorPad *pad = GST_VIDEO_AGGREGATOR_PAD (l->data);

        if (!vaggpad_klass->set_info (pad, vagg, &pad->info, &vagg->info)) {
          return FALSE;
        }
      }
    }

    if (gst_video_aggregator_src_setcaps (vagg, caps)) {
      if (vagg_klass->negotiated_caps)
        ret =
            GST_VIDEO_AGGREGATOR_GET_CLASS (vagg)->negotiated_caps (vagg, caps);
    }
    gst_caps_unref (caps);
  } else {
    /* We couldn't decide the output video info because the sinkpads don't have
     * all the caps yet, so we mark the pad as needing a reconfigure. This
     * allows aggregate() to skip ahead a bit and try again later. */
    GST_DEBUG_OBJECT (vagg, "Couldn't decide output video info");
    gst_pad_mark_reconfigure (agg->srcpad);
    ret = FALSE;
  }

done:
  return ret;
}

static gboolean
gst_video_aggregator_get_sinkpads_interlace_mode (GstVideoAggregator * vagg,
    GstVideoAggregatorPad * skip_pad, GstVideoInterlaceMode * mode)
{
  GList *walk;

  for (walk = GST_ELEMENT (vagg)->sinkpads; walk; walk = g_list_next (walk)) {
    GstVideoAggregatorPad *vaggpad = walk->data;

    if (skip_pad && vaggpad == skip_pad)
      continue;
    if (vaggpad->info.finfo
        && GST_VIDEO_INFO_FORMAT (&vaggpad->info) != GST_VIDEO_FORMAT_UNKNOWN) {
      *mode = GST_VIDEO_INFO_INTERLACE_MODE (&vaggpad->info);
      return TRUE;
    }
  }
  return FALSE;
}

static gboolean
gst_video_aggregator_pad_sink_setcaps (GstPad * pad, GstObject * parent,
    GstCaps * caps)
{
  GstVideoAggregator *vagg;
  GstVideoAggregatorPad *vaggpad;
  GstVideoInfo info;
  gboolean ret = FALSE;

  GST_INFO_OBJECT (pad, "Setting caps %" GST_PTR_FORMAT, caps);

  vagg = GST_VIDEO_AGGREGATOR (parent);
  vaggpad = GST_VIDEO_AGGREGATOR_PAD (pad);

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_DEBUG_OBJECT (pad, "Failed to parse caps");
    goto beach;
  }

  GST_VIDEO_AGGREGATOR_LOCK (vagg);
  {
    GstVideoInterlaceMode pads_mode = GST_VIDEO_INTERLACE_MODE_PROGRESSIVE;
    gboolean has_mode = FALSE;

    /* get the current output setting or fallback to other pads settings */
    if (GST_VIDEO_INFO_FORMAT (&vagg->info) != GST_VIDEO_FORMAT_UNKNOWN) {
      pads_mode = GST_VIDEO_INFO_INTERLACE_MODE (&vagg->info);
      has_mode = TRUE;
    } else {
      has_mode =
          gst_video_aggregator_get_sinkpads_interlace_mode (vagg, vaggpad,
          &pads_mode);
    }

    if (has_mode) {
      if (pads_mode != GST_VIDEO_INFO_INTERLACE_MODE (&info)) {
        GST_ERROR_OBJECT (pad,
            "got input caps %" GST_PTR_FORMAT ", but current caps are %"
            GST_PTR_FORMAT, caps, vagg->priv->current_caps);
        GST_VIDEO_AGGREGATOR_UNLOCK (vagg);
        return FALSE;
      }
    }
  }

  vaggpad->info = info;
  gst_pad_mark_reconfigure (GST_AGGREGATOR_SRC_PAD (vagg));
  ret = TRUE;

  GST_VIDEO_AGGREGATOR_UNLOCK (vagg);

beach:
  return ret;
}

static gboolean
gst_video_aggregator_caps_has_alpha (GstCaps * caps)
{
  guint size = gst_caps_get_size (caps);
  guint i;

  for (i = 0; i < size; i++) {
    GstStructure *s = gst_caps_get_structure (caps, i);
    const GValue *formats = gst_structure_get_value (s, "format");

    if (formats) {
      const GstVideoFormatInfo *info;

      if (GST_VALUE_HOLDS_LIST (formats)) {
        guint list_size = gst_value_list_get_size (formats);
        guint index;

        for (index = 0; index < list_size; index++) {
          const GValue *list_item = gst_value_list_get_value (formats, index);
          info =
              gst_video_format_get_info (gst_video_format_from_string
              (g_value_get_string (list_item)));
          if (GST_VIDEO_FORMAT_INFO_HAS_ALPHA (info))
            return TRUE;
        }

      } else if (G_VALUE_HOLDS_STRING (formats)) {
        info =
            gst_video_format_get_info (gst_video_format_from_string
            (g_value_get_string (formats)));
        if (GST_VIDEO_FORMAT_INFO_HAS_ALPHA (info))
          return TRUE;

      } else {
        g_assert_not_reached ();
        GST_WARNING ("Unexpected type for video 'format' field: %s",
            G_VALUE_TYPE_NAME (formats));
      }

    } else {
      return TRUE;
    }
  }
  return FALSE;
}

static GstCaps *
gst_video_aggregator_pad_sink_getcaps (GstPad * pad, GstVideoAggregator * vagg,
    GstCaps * filter)
{
  GstCaps *srccaps;
  GstCaps *template_caps, *sink_template_caps;
  GstCaps *returned_caps;
  GstStructure *s;
  gint i, n;
  GstAggregator *agg = GST_AGGREGATOR (vagg);
  GstPad *srcpad = GST_PAD (agg->srcpad);
  gboolean has_alpha;
  GstVideoInterlaceMode interlace_mode;
  gboolean has_interlace_mode;

  template_caps = gst_pad_get_pad_template_caps (srcpad);

  GST_DEBUG_OBJECT (pad, "Get caps with filter: %" GST_PTR_FORMAT, filter);

  srccaps = gst_pad_peer_query_caps (srcpad, template_caps);
  srccaps = gst_caps_make_writable (srccaps);
  has_alpha = gst_video_aggregator_caps_has_alpha (srccaps);

  has_interlace_mode =
      gst_video_aggregator_get_sinkpads_interlace_mode (vagg, NULL,
      &interlace_mode);

  n = gst_caps_get_size (srccaps);
  for (i = 0; i < n; i++) {
    s = gst_caps_get_structure (srccaps, i);
    gst_structure_set (s, "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
        "height", GST_TYPE_INT_RANGE, 1, G_MAXINT,
        "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);

    gst_structure_remove_fields (s, "colorimetry", "chroma-site", "format",
        "pixel-aspect-ratio", NULL);
    if (has_interlace_mode)
      gst_structure_set (s, "interlace-mode", G_TYPE_STRING,
          gst_video_interlace_mode_to_string (interlace_mode), NULL);
  }

  if (filter) {
    returned_caps = gst_caps_intersect (srccaps, filter);
    gst_caps_unref (srccaps);
  } else {
    returned_caps = srccaps;
  }

  if (has_alpha) {
    sink_template_caps = gst_pad_get_pad_template_caps (pad);
  } else {
    GstVideoAggregatorClass *klass = GST_VIDEO_AGGREGATOR_GET_CLASS (vagg);
    sink_template_caps = gst_caps_ref (klass->sink_non_alpha_caps);
  }

  {
    GstCaps *intersect = gst_caps_intersect (returned_caps, sink_template_caps);
    gst_caps_unref (returned_caps);
    returned_caps = intersect;
  }

  gst_caps_unref (template_caps);
  gst_caps_unref (sink_template_caps);

  GST_DEBUG_OBJECT (pad, "Returning caps: %" GST_PTR_FORMAT, returned_caps);

  return returned_caps;
}

static void
gst_video_aggregator_update_qos (GstVideoAggregator * vagg, gdouble proportion,
    GstClockTimeDiff diff, GstClockTime timestamp)
{
  gboolean live;

  GST_DEBUG_OBJECT (vagg,
      "Updating QoS: proportion %lf, diff %" GST_STIME_FORMAT ", timestamp %"
      GST_TIME_FORMAT, proportion, GST_STIME_ARGS (diff),
      GST_TIME_ARGS (timestamp));

  live =
      GST_CLOCK_TIME_IS_VALID (gst_aggregator_get_latency (GST_AGGREGATOR
          (vagg)));

  GST_OBJECT_LOCK (vagg);

  vagg->priv->proportion = proportion;
  if (G_LIKELY (timestamp != GST_CLOCK_TIME_NONE)) {
    if (!live && G_UNLIKELY (diff > 0))
      vagg->priv->earliest_time =
          timestamp + 2 * diff + gst_util_uint64_scale_int_round (GST_SECOND,
          GST_VIDEO_INFO_FPS_D (&vagg->info),
          GST_VIDEO_INFO_FPS_N (&vagg->info));
    else
      vagg->priv->earliest_time = timestamp + diff;
  } else {
    vagg->priv->earliest_time = GST_CLOCK_TIME_NONE;
  }
  GST_OBJECT_UNLOCK (vagg);
}

static void
gst_video_aggregator_reset_qos (GstVideoAggregator * vagg)
{
  gst_video_aggregator_update_qos (vagg, 0.5, 0, GST_CLOCK_TIME_NONE);
  vagg->priv->qos_processed = vagg->priv->qos_dropped = 0;
}

static void
gst_video_aggregator_read_qos (GstVideoAggregator * vagg, gdouble * proportion,
    GstClockTime * time)
{
  GST_OBJECT_LOCK (vagg);
  *proportion = vagg->priv->proportion;
  *time = vagg->priv->earliest_time;
  GST_OBJECT_UNLOCK (vagg);
}

static void
gst_video_aggregator_reset (GstVideoAggregator * vagg)
{
  GstAggregator *agg = GST_AGGREGATOR (vagg);
  GList *l;

  gst_video_info_init (&vagg->info);
  vagg->priv->ts_offset = 0;
  vagg->priv->nframes = 0;
  vagg->priv->live = FALSE;

  agg->segment.position = -1;

  gst_video_aggregator_reset_qos (vagg);

  GST_OBJECT_LOCK (vagg);
  for (l = GST_ELEMENT (vagg)->sinkpads; l; l = l->next) {
    GstVideoAggregatorPad *p = l->data;

    gst_buffer_replace (&p->buffer, NULL);
    p->priv->start_time = -1;
    p->priv->end_time = -1;

    gst_video_info_init (&p->info);
  }
  GST_OBJECT_UNLOCK (vagg);
}

#define GST_FLOW_NEEDS_DATA GST_FLOW_CUSTOM_ERROR
static gint
gst_video_aggregator_fill_queues (GstVideoAggregator * vagg,
    GstClockTime output_start_running_time,
    GstClockTime output_end_running_time)
{
  GstAggregator *agg = GST_AGGREGATOR (vagg);
  GList *l;
  gboolean eos = TRUE;
  gboolean need_more_data = FALSE;

  /* get a set of buffers into pad->buffer that are within output_start_running_time
   * and output_end_running_time taking into account finished and unresponsive pads */

  GST_OBJECT_LOCK (vagg);
  for (l = GST_ELEMENT (vagg)->sinkpads; l; l = l->next) {
    GstVideoAggregatorPad *pad = l->data;
    GstSegment segment;
    GstAggregatorPad *bpad;
    GstBuffer *buf;
    GstVideoInfo *vinfo;
    gboolean is_eos;

    bpad = GST_AGGREGATOR_PAD (pad);
    GST_OBJECT_LOCK (bpad);
    segment = bpad->segment;
    GST_OBJECT_UNLOCK (bpad);
    is_eos = gst_aggregator_pad_is_eos (bpad);

    if (!is_eos)
      eos = FALSE;
    buf = gst_aggregator_pad_get_buffer (bpad);
    if (buf) {
      GstClockTime start_time, end_time;

      start_time = GST_BUFFER_TIMESTAMP (buf);
      if (start_time == -1) {
        gst_buffer_unref (buf);
        GST_DEBUG_OBJECT (pad, "Need timestamped buffers!");
        GST_OBJECT_UNLOCK (vagg);
        return GST_FLOW_ERROR;
      }

      vinfo = &pad->info;

      /* FIXME: Make all this work with negative rates */
      end_time = GST_BUFFER_DURATION (buf);

      if (end_time == -1) {
        start_time = MAX (start_time, segment.start);
        start_time =
            gst_segment_to_running_time (&segment, GST_FORMAT_TIME, start_time);

        if (start_time >= output_end_running_time) {
          if (pad->buffer) {
            GST_DEBUG_OBJECT (pad, "buffer duration is -1, start_time >= "
                "output_end_running_time. Keeping previous buffer");
          } else {
            GST_DEBUG_OBJECT (pad, "buffer duration is -1, start_time >= "
                "output_end_running_time. No previous buffer.");
          }
          gst_buffer_unref (buf);
          continue;
        } else if (start_time < output_start_running_time) {
          GST_DEBUG_OBJECT (pad, "buffer duration is -1, start_time < "
              "output_start_running_time.  Discarding old buffer");
          gst_buffer_replace (&pad->buffer, buf);
          pad->buffer_vinfo = *vinfo;
          gst_buffer_unref (buf);
          gst_aggregator_pad_drop_buffer (bpad);
          need_more_data = TRUE;
          continue;
        }
        gst_buffer_unref (buf);
        buf = gst_aggregator_pad_steal_buffer (bpad);
        gst_buffer_replace (&pad->buffer, buf);
        pad->buffer_vinfo = *vinfo;
        /* FIXME: Set start_time and end_time to something here? */
        gst_buffer_unref (buf);
        GST_DEBUG_OBJECT (pad, "buffer duration is -1");
        continue;
      }

      g_assert (start_time != -1 && end_time != -1);
      end_time += start_time;   /* convert from duration to position */

      /* Check if it's inside the segment */
      if (start_time >= segment.stop || end_time < segment.start) {
        GST_DEBUG_OBJECT (pad,
            "Buffer outside the segment : segment: [%" GST_TIME_FORMAT " -- %"
            GST_TIME_FORMAT "]" " Buffer [%" GST_TIME_FORMAT " -- %"
            GST_TIME_FORMAT "]", GST_TIME_ARGS (segment.stop),
            GST_TIME_ARGS (segment.start), GST_TIME_ARGS (start_time),
            GST_TIME_ARGS (end_time));

        gst_buffer_unref (buf);
        gst_aggregator_pad_drop_buffer (bpad);

        need_more_data = TRUE;
        continue;
      }

      /* Clip to segment and convert to running time */
      start_time = MAX (start_time, segment.start);
      if (segment.stop != -1)
        end_time = MIN (end_time, segment.stop);
      start_time =
          gst_segment_to_running_time (&segment, GST_FORMAT_TIME, start_time);
      end_time =
          gst_segment_to_running_time (&segment, GST_FORMAT_TIME, end_time);
      g_assert (start_time != -1 && end_time != -1);

      /* Convert to the output segment rate */
      if (ABS (agg->segment.rate) != 1.0) {
        start_time *= ABS (agg->segment.rate);
        end_time *= ABS (agg->segment.rate);
      }

      GST_TRACE_OBJECT (pad, "dealing with buffer %p start %" GST_TIME_FORMAT
          " end %" GST_TIME_FORMAT " out start %" GST_TIME_FORMAT
          " out end %" GST_TIME_FORMAT, buf, GST_TIME_ARGS (start_time),
          GST_TIME_ARGS (end_time), GST_TIME_ARGS (output_start_running_time),
          GST_TIME_ARGS (output_end_running_time));

      if (pad->priv->end_time != -1 && pad->priv->end_time > end_time) {
        GST_DEBUG_OBJECT (pad, "Buffer from the past, dropping");
        gst_buffer_unref (buf);
        gst_aggregator_pad_drop_buffer (bpad);
        continue;
      }

      if (end_time >= output_start_running_time
          && start_time < output_end_running_time) {
        GST_DEBUG_OBJECT (pad,
            "Taking new buffer with start time %" GST_TIME_FORMAT,
            GST_TIME_ARGS (start_time));
        gst_buffer_replace (&pad->buffer, buf);
        pad->buffer_vinfo = *vinfo;
        pad->priv->start_time = start_time;
        pad->priv->end_time = end_time;

        gst_buffer_unref (buf);
        gst_aggregator_pad_drop_buffer (bpad);
        eos = FALSE;
      } else if (start_time >= output_end_running_time) {
        GST_DEBUG_OBJECT (pad, "Keeping buffer until %" GST_TIME_FORMAT,
            GST_TIME_ARGS (start_time));
        gst_buffer_unref (buf);
        eos = FALSE;
      } else {
        gst_buffer_replace (&pad->buffer, buf);
        pad->buffer_vinfo = *vinfo;
        pad->priv->start_time = start_time;
        pad->priv->end_time = end_time;
        GST_DEBUG_OBJECT (pad,
            "replacing old buffer with a newer buffer, start %" GST_TIME_FORMAT
            " out end %" GST_TIME_FORMAT, GST_TIME_ARGS (start_time),
            GST_TIME_ARGS (output_end_running_time));
        gst_buffer_unref (buf);
        gst_aggregator_pad_drop_buffer (bpad);

        need_more_data = TRUE;
        continue;
      }
    } else {
      if (is_eos && pad->ignore_eos) {
        eos = FALSE;
        GST_DEBUG_OBJECT (pad, "ignoring EOS and re-using previous buffer");
        continue;
      }

      if (pad->priv->end_time != -1) {
        if (pad->priv->end_time <= output_start_running_time) {
          pad->priv->start_time = pad->priv->end_time = -1;
          if (is_eos) {
            GST_DEBUG ("I just need more data");
            need_more_data = TRUE;
          }
        } else if (is_eos) {
          eos = FALSE;
        }
      }

      if (is_eos) {
        gst_buffer_replace (&pad->buffer, NULL);
      }
    }
  }
  GST_OBJECT_UNLOCK (vagg);

  if (need_more_data)
    return GST_FLOW_NEEDS_DATA;
  if (eos)
    return GST_FLOW_EOS;

  return GST_FLOW_OK;
}

static gboolean
sync_pad_values (GstVideoAggregator * vagg, GstVideoAggregatorPad * pad)
{
  GstAggregatorPad *bpad = GST_AGGREGATOR_PAD (pad);
  GstClockTime timestamp;
  gint64 stream_time;

  if (pad->buffer == NULL)
    return TRUE;

  timestamp = GST_BUFFER_TIMESTAMP (pad->buffer);
  GST_OBJECT_LOCK (bpad);
  stream_time = gst_segment_to_stream_time (&bpad->segment, GST_FORMAT_TIME,
      timestamp);
  GST_OBJECT_UNLOCK (bpad);

  /* sync object properties on stream time */
  if (GST_CLOCK_TIME_IS_VALID (stream_time))
    gst_object_sync_values (GST_OBJECT (pad), stream_time);

  return TRUE;
}

static gboolean
prepare_frames (GstVideoAggregator * vagg, GstVideoAggregatorPad * pad)
{
  GstVideoAggregatorPadClass *vaggpad_class =
      GST_VIDEO_AGGREGATOR_PAD_GET_CLASS (pad);

  if (pad->buffer == NULL || !vaggpad_class->prepare_frame)
    return TRUE;

  return vaggpad_class->prepare_frame (pad, vagg);
}

static gboolean
clean_pad (GstVideoAggregator * vagg, GstVideoAggregatorPad * pad)
{
  GstVideoAggregatorPadClass *vaggpad_class =
      GST_VIDEO_AGGREGATOR_PAD_GET_CLASS (pad);

  vaggpad_class->clean_frame (pad, vagg);

  return TRUE;
}

static GstFlowReturn
gst_video_aggregator_do_aggregate (GstVideoAggregator * vagg,
    GstClockTime output_start_time, GstClockTime output_end_time,
    GstBuffer ** outbuf)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (vagg);
  GstVideoAggregatorClass *vagg_klass = (GstVideoAggregatorClass *) klass;
  GstVideoAggregatorPadClass *vaggpad_class = g_type_class_peek
      (GST_AGGREGATOR_CLASS (klass)->sinkpads_type);

  g_assert (vagg_klass->aggregate_frames != NULL);
  g_assert (vagg_klass->get_output_buffer != NULL);

  if ((ret = vagg_klass->get_output_buffer (vagg, outbuf)) != GST_FLOW_OK) {
    GST_WARNING_OBJECT (vagg, "Could not get an output buffer, reason: %s",
        gst_flow_get_name (ret));
    return ret;
  }
  if (*outbuf == NULL) {
    /* sub-class doesn't want to generate output right now */
    return GST_FLOW_OK;
  }

  GST_BUFFER_TIMESTAMP (*outbuf) = output_start_time;
  GST_BUFFER_DURATION (*outbuf) = output_end_time - output_start_time;

  /* Sync pad properties to the stream time */
  gst_aggregator_iterate_sinkpads (GST_AGGREGATOR (vagg),
      (GstAggregatorPadForeachFunc) sync_pad_values, NULL);

  /* Convert all the frames the subclass has before aggregating */
  gst_aggregator_iterate_sinkpads (GST_AGGREGATOR (vagg),
      (GstAggregatorPadForeachFunc) prepare_frames, NULL);

  ret = vagg_klass->aggregate_frames (vagg, *outbuf);

  if (vaggpad_class->clean_frame) {
    gst_aggregator_iterate_sinkpads (GST_AGGREGATOR (vagg),
        (GstAggregatorPadForeachFunc) clean_pad, NULL);
  }

  return ret;
}

/* Perform qos calculations before processing the next frame. Returns TRUE if
 * the frame should be processed, FALSE if the frame can be dropped entirely */
static gint64
gst_video_aggregator_do_qos (GstVideoAggregator * vagg, GstClockTime timestamp)
{
  GstAggregator *agg = GST_AGGREGATOR (vagg);
  GstClockTime qostime, earliest_time;
  gdouble proportion;
  gint64 jitter;

  /* no timestamp, can't do QoS => process frame */
  if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (timestamp))) {
    GST_LOG_OBJECT (vagg, "invalid timestamp, can't do QoS, process frame");
    return -1;
  }

  /* get latest QoS observation values */
  gst_video_aggregator_read_qos (vagg, &proportion, &earliest_time);

  /* skip qos if we have no observation (yet) => process frame */
  if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (earliest_time))) {
    GST_LOG_OBJECT (vagg, "no observation yet, process frame");
    return -1;
  }

  /* qos is done on running time */
  qostime =
      gst_segment_to_running_time (&agg->segment, GST_FORMAT_TIME, timestamp);

  /* see how our next timestamp relates to the latest qos timestamp */
  GST_LOG_OBJECT (vagg, "qostime %" GST_TIME_FORMAT ", earliest %"
      GST_TIME_FORMAT, GST_TIME_ARGS (qostime), GST_TIME_ARGS (earliest_time));

  jitter = GST_CLOCK_DIFF (qostime, earliest_time);
  if (qostime != GST_CLOCK_TIME_NONE && jitter > 0) {
    GST_DEBUG_OBJECT (vagg, "we are late, drop frame");
    return jitter;
  }

  GST_LOG_OBJECT (vagg, "process frame");
  return jitter;
}

static GstClockTime
gst_video_aggregator_get_next_time (GstAggregator * agg)
{
  GstClockTime next_time;

  GST_OBJECT_LOCK (agg);
  if (agg->segment.position == -1 || agg->segment.position < agg->segment.start)
    next_time = agg->segment.start;
  else
    next_time = agg->segment.position;

  if (agg->segment.stop != -1 && next_time > agg->segment.stop)
    next_time = agg->segment.stop;

  next_time =
      gst_segment_to_running_time (&agg->segment, GST_FORMAT_TIME, next_time);
  GST_OBJECT_UNLOCK (agg);

  return next_time;
}

static GstFlowReturn
gst_video_aggregator_check_reconfigure (GstVideoAggregator * vagg,
    gboolean timeout)
{
  GstAggregator *agg = (GstAggregator *) vagg;

  if (GST_VIDEO_INFO_FORMAT (&vagg->info) == GST_VIDEO_FORMAT_UNKNOWN
      || gst_pad_check_reconfigure (GST_AGGREGATOR_SRC_PAD (vagg))) {
    gboolean ret;

  restart:
    ret = gst_video_aggregator_update_src_caps (vagg);
    if (!ret) {
      gst_pad_mark_reconfigure (GST_AGGREGATOR_SRC_PAD (vagg));
      if (timeout) {
        guint64 frame_duration;
        gint fps_d, fps_n;

        GST_DEBUG_OBJECT (vagg,
            "Got timeout before receiving any caps, don't output anything");

        if (agg->segment.position == -1) {
          if (agg->segment.rate > 0.0)
            agg->segment.position = agg->segment.start;
          else
            agg->segment.position = agg->segment.stop;
        }

        /* Advance position */
        fps_d = GST_VIDEO_INFO_FPS_D (&vagg->info) ?
            GST_VIDEO_INFO_FPS_D (&vagg->info) : 1;
        fps_n = GST_VIDEO_INFO_FPS_N (&vagg->info) ?
            GST_VIDEO_INFO_FPS_N (&vagg->info) : 25;
        /* Default to 25/1 if no "best fps" is known */
        frame_duration = gst_util_uint64_scale (GST_SECOND, fps_d, fps_n);
        if (agg->segment.rate > 0.0)
          agg->segment.position += frame_duration;
        else if (agg->segment.position > frame_duration)
          agg->segment.position -= frame_duration;
        else
          agg->segment.position = 0;
        vagg->priv->nframes++;
        return GST_FLOW_NEEDS_DATA;
      } else {
        if (GST_PAD_IS_FLUSHING (GST_AGGREGATOR_SRC_PAD (vagg)))
          return GST_FLOW_FLUSHING;
        else
          return GST_FLOW_NOT_NEGOTIATED;
      }
    } else {
      /* It is possible that during gst_video_aggregator_update_src_caps()
       * we got a caps change on one of the sink pads, in which case we need
       * to redo the negotiation
       * - https://bugzilla.gnome.org/show_bug.cgi?id=755782 */
      if (gst_pad_check_reconfigure (GST_AGGREGATOR_SRC_PAD (vagg)))
        goto restart;
    }
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_video_aggregator_aggregate (GstAggregator * agg, gboolean timeout)
{
  GstVideoAggregator *vagg = GST_VIDEO_AGGREGATOR (agg);
  GstClockTime output_start_time, output_end_time;
  GstClockTime output_start_running_time, output_end_running_time;
  GstBuffer *outbuf = NULL;
  GstFlowReturn flow_ret;
  gint64 jitter;

  GST_VIDEO_AGGREGATOR_LOCK (vagg);

  flow_ret = gst_video_aggregator_check_reconfigure (vagg, timeout);
  if (flow_ret != GST_FLOW_OK) {
    if (flow_ret == GST_FLOW_NEEDS_DATA)
      flow_ret = GST_FLOW_OK;
    goto unlock_and_return;
  }

  output_start_time = agg->segment.position;
  if (agg->segment.position == -1 || agg->segment.position < agg->segment.start)
    output_start_time = agg->segment.start;

  if (vagg->priv->nframes == 0) {
    vagg->priv->ts_offset = output_start_time;
    GST_DEBUG_OBJECT (vagg, "New ts offset %" GST_TIME_FORMAT,
        GST_TIME_ARGS (output_start_time));
  }

  if (GST_VIDEO_INFO_FPS_N (&vagg->info) == 0) {
    output_end_time = -1;
  } else {
    output_end_time =
        vagg->priv->ts_offset +
        gst_util_uint64_scale_round (vagg->priv->nframes + 1,
        GST_SECOND * GST_VIDEO_INFO_FPS_D (&vagg->info),
        GST_VIDEO_INFO_FPS_N (&vagg->info));
  }

  if (agg->segment.stop != -1)
    output_end_time = MIN (output_end_time, agg->segment.stop);

  output_start_running_time =
      gst_segment_to_running_time (&agg->segment, GST_FORMAT_TIME,
      output_start_time);
  output_end_running_time =
      gst_segment_to_running_time (&agg->segment, GST_FORMAT_TIME,
      output_end_time);

  if (output_end_time == output_start_time) {
    flow_ret = GST_FLOW_EOS;
  } else {
    flow_ret =
        gst_video_aggregator_fill_queues (vagg, output_start_running_time,
        output_end_running_time);
  }

  if (flow_ret == GST_FLOW_NEEDS_DATA && !timeout) {
    GST_DEBUG_OBJECT (vagg, "Need more data for decisions");
    flow_ret = GST_FLOW_OK;
    goto unlock_and_return;
  } else if (flow_ret == GST_FLOW_EOS) {
    GST_DEBUG_OBJECT (vagg, "All sinkpads are EOS -- forwarding");
    goto unlock_and_return;
  } else if (flow_ret == GST_FLOW_ERROR) {
    GST_WARNING_OBJECT (vagg, "Error collecting buffers");
    goto unlock_and_return;
  }

  GST_DEBUG_OBJECT (vagg,
      "Producing buffer for %" GST_TIME_FORMAT " to %" GST_TIME_FORMAT
      ", running time start %" GST_TIME_FORMAT ", running time end %"
      GST_TIME_FORMAT, GST_TIME_ARGS (output_start_time),
      GST_TIME_ARGS (output_end_time),
      GST_TIME_ARGS (output_start_running_time),
      GST_TIME_ARGS (output_end_running_time));

  jitter = gst_video_aggregator_do_qos (vagg, output_start_time);
  if (jitter <= 0) {
    flow_ret = gst_video_aggregator_do_aggregate (vagg, output_start_time,
        output_end_time, &outbuf);
    if (flow_ret != GST_FLOW_OK)
      goto done;
    vagg->priv->qos_processed++;
  } else {
    GstMessage *msg;

    vagg->priv->qos_dropped++;

    msg =
        gst_message_new_qos (GST_OBJECT_CAST (vagg), vagg->priv->live,
        output_start_running_time, gst_segment_to_stream_time (&agg->segment,
            GST_FORMAT_TIME, output_start_time), output_start_time,
        output_end_time - output_start_time);
    gst_message_set_qos_values (msg, jitter, vagg->priv->proportion, 1000000);
    gst_message_set_qos_stats (msg, GST_FORMAT_BUFFERS,
        vagg->priv->qos_processed, vagg->priv->qos_dropped);
    gst_element_post_message (GST_ELEMENT_CAST (vagg), msg);

    flow_ret = GST_FLOW_OK;
  }

  GST_VIDEO_AGGREGATOR_UNLOCK (vagg);
  if (outbuf) {
    GST_DEBUG_OBJECT (vagg,
        "Pushing buffer with ts %" GST_TIME_FORMAT " and duration %"
        GST_TIME_FORMAT, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)),
        GST_TIME_ARGS (GST_BUFFER_DURATION (outbuf)));

    flow_ret = gst_aggregator_finish_buffer (agg, outbuf);
  }

  GST_VIDEO_AGGREGATOR_LOCK (vagg);
  vagg->priv->nframes++;
  agg->segment.position = output_end_time;
  GST_VIDEO_AGGREGATOR_UNLOCK (vagg);

  return flow_ret;

done:
  if (outbuf)
    gst_buffer_unref (outbuf);
unlock_and_return:
  GST_VIDEO_AGGREGATOR_UNLOCK (vagg);
  return flow_ret;
}

/* FIXME, the duration query should reflect how long you will produce
 * data, that is the amount of stream time until you will emit EOS.
 *
 * For synchronized aggregating this is always the max of all the durations
 * of upstream since we emit EOS when all of them finished.
 *
 * We don't do synchronized aggregating so this really depends on where the
 * streams where punched in and what their relative offsets are against
 * each other which we can get from the first timestamps we see.
 *
 * When we add a new stream (or remove a stream) the duration might
 * also become invalid again and we need to post a new DURATION
 * message to notify this fact to the parent.
 * For now we take the max of all the upstream elements so the simple
 * cases work at least somewhat.
 */
static gboolean
gst_video_aggregator_query_duration (GstVideoAggregator * vagg,
    GstQuery * query)
{
  GValue item = { 0 };
  gint64 max;
  gboolean res;
  GstFormat format;
  GstIterator *it;
  gboolean done;

  /* parse format */
  gst_query_parse_duration (query, &format, NULL);

  max = -1;
  res = TRUE;
  done = FALSE;

  /* Take maximum of all durations */
  it = gst_element_iterate_sink_pads (GST_ELEMENT_CAST (vagg));
  while (!done) {
    switch (gst_iterator_next (it, &item)) {
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
      case GST_ITERATOR_OK:
      {
        GstPad *pad;
        gint64 duration;

        pad = g_value_get_object (&item);

        /* ask sink peer for duration */
        res &= gst_pad_peer_query_duration (pad, format, &duration);
        /* take max from all valid return values */
        if (res) {
          /* valid unknown length, stop searching */
          if (duration == -1) {
            max = duration;
            done = TRUE;
          }
          /* else see if bigger than current max */
          else if (duration > max)
            max = duration;
        }
        g_value_reset (&item);
        break;
      }
      case GST_ITERATOR_RESYNC:
        max = -1;
        res = TRUE;
        gst_iterator_resync (it);
        break;
      default:
        res = FALSE;
        done = TRUE;
        break;
    }
  }
  g_value_unset (&item);
  gst_iterator_free (it);

  if (res) {
    /* and store the max */
    GST_DEBUG_OBJECT (vagg, "Total duration in format %s: %"
        GST_TIME_FORMAT, gst_format_get_name (format), GST_TIME_ARGS (max));
    gst_query_set_duration (query, format, max);
  }

  return res;
}

static gboolean
gst_video_aggregator_src_query (GstAggregator * agg, GstQuery * query)
{
  GstVideoAggregator *vagg = GST_VIDEO_AGGREGATOR (agg);
  gboolean res = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat format;

      gst_query_parse_position (query, &format, NULL);

      switch (format) {
        case GST_FORMAT_TIME:
          gst_query_set_position (query, format,
              gst_segment_to_stream_time (&agg->segment, GST_FORMAT_TIME,
                  agg->segment.position));
          res = TRUE;
          break;
        default:
          break;
      }
      break;
    }
    case GST_QUERY_DURATION:
      res = gst_video_aggregator_query_duration (vagg, query);
      break;
    case GST_QUERY_LATENCY:
      res =
          GST_AGGREGATOR_CLASS (gst_video_aggregator_parent_class)->src_query
          (agg, query);

      if (res) {
        gst_query_parse_latency (query, &vagg->priv->live, NULL, NULL);
      }
      break;
    default:
      res =
          GST_AGGREGATOR_CLASS (gst_video_aggregator_parent_class)->src_query
          (agg, query);
      break;
  }
  return res;
}

static gboolean
gst_video_aggregator_src_event (GstAggregator * agg, GstEvent * event)
{
  GstVideoAggregator *vagg = GST_VIDEO_AGGREGATOR (agg);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_QOS:
    {
      GstQOSType type;
      GstClockTimeDiff diff;
      GstClockTime timestamp;
      gdouble proportion;

      gst_event_parse_qos (event, &type, &proportion, &diff, &timestamp);
      gst_video_aggregator_update_qos (vagg, proportion, diff, timestamp);
      break;
    }
    case GST_EVENT_SEEK:
    {
      GST_DEBUG_OBJECT (vagg, "Handling SEEK event");
    }
    default:
      break;
  }

  return
      GST_AGGREGATOR_CLASS (gst_video_aggregator_parent_class)->src_event (agg,
      event);
}

static GstFlowReturn
gst_video_aggregator_flush (GstAggregator * agg)
{
  GList *l;
  gdouble abs_rate;
  GstVideoAggregator *vagg = GST_VIDEO_AGGREGATOR (agg);

  GST_INFO_OBJECT (agg, "Flushing");
  GST_OBJECT_LOCK (vagg);
  abs_rate = ABS (agg->segment.rate);
  for (l = GST_ELEMENT (vagg)->sinkpads; l; l = l->next) {
    GstVideoAggregatorPad *p = l->data;

    /* Convert to the output segment rate */
    if (ABS (agg->segment.rate) != abs_rate) {
      if (ABS (agg->segment.rate) != 1.0 && p->buffer) {
        p->priv->start_time /= ABS (agg->segment.rate);
        p->priv->end_time /= ABS (agg->segment.rate);
      }
      if (abs_rate != 1.0 && p->buffer) {
        p->priv->start_time *= abs_rate;
        p->priv->end_time *= abs_rate;
      }
    }
  }
  GST_OBJECT_UNLOCK (vagg);

  agg->segment.position = -1;
  vagg->priv->ts_offset = 0;
  vagg->priv->nframes = 0;

  gst_video_aggregator_reset_qos (vagg);
  return GST_FLOW_OK;
}

static gboolean
gst_video_aggregator_sink_event (GstAggregator * agg, GstAggregatorPad * bpad,
    GstEvent * event)
{
  GstVideoAggregator *vagg = GST_VIDEO_AGGREGATOR (agg);
  GstVideoAggregatorPad *pad = GST_VIDEO_AGGREGATOR_PAD (bpad);
  gboolean ret = TRUE;

  GST_DEBUG_OBJECT (pad, "Got %s event on pad %s:%s",
      GST_EVENT_TYPE_NAME (event), GST_DEBUG_PAD_NAME (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      ret =
          gst_video_aggregator_pad_sink_setcaps (GST_PAD (pad),
          GST_OBJECT (vagg), caps);
      gst_event_unref (event);
      event = NULL;
      break;
    }
    case GST_EVENT_SEGMENT:{
      GstSegment seg;
      gst_event_copy_segment (event, &seg);

      g_assert (seg.format == GST_FORMAT_TIME);
      gst_video_aggregator_reset_qos (vagg);
      break;
    }
    default:
      break;
  }

  if (event != NULL)
    return GST_AGGREGATOR_CLASS (gst_video_aggregator_parent_class)->sink_event
        (agg, bpad, event);

  return ret;
}

static gboolean
gst_video_aggregator_start (GstAggregator * agg)
{
  GstVideoAggregator *vagg = GST_VIDEO_AGGREGATOR (agg);

  gst_caps_replace (&vagg->priv->current_caps, NULL);

  return TRUE;
}

static gboolean
gst_video_aggregator_stop (GstAggregator * agg)
{
  GstVideoAggregator *vagg = GST_VIDEO_AGGREGATOR (agg);

  gst_video_aggregator_reset (vagg);

  return TRUE;
}

/* GstElement vmethods */
static GstPad *
gst_video_aggregator_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * req_name, const GstCaps * caps)
{
  GstVideoAggregator *vagg;
  GstVideoAggregatorPad *vaggpad;

  vagg = GST_VIDEO_AGGREGATOR (element);

  vaggpad = (GstVideoAggregatorPad *)
      GST_ELEMENT_CLASS (gst_video_aggregator_parent_class)->request_new_pad
      (element, templ, req_name, caps);

  if (vaggpad == NULL)
    return NULL;

  GST_OBJECT_LOCK (vagg);
  vaggpad->zorder = GST_ELEMENT (vagg)->numsinkpads;
  vaggpad->priv->start_time = -1;
  vaggpad->priv->end_time = -1;
  element->sinkpads = g_list_sort (element->sinkpads,
      (GCompareFunc) pad_zorder_compare);
  GST_OBJECT_UNLOCK (vagg);

  gst_child_proxy_child_added (GST_CHILD_PROXY (vagg), G_OBJECT (vaggpad),
      GST_OBJECT_NAME (vaggpad));

  return GST_PAD (vaggpad);
}

static void
gst_video_aggregator_release_pad (GstElement * element, GstPad * pad)
{
  GstVideoAggregator *vagg = NULL;
  GstVideoAggregatorPad *vaggpad;
  gboolean last_pad;

  vagg = GST_VIDEO_AGGREGATOR (element);
  vaggpad = GST_VIDEO_AGGREGATOR_PAD (pad);

  GST_VIDEO_AGGREGATOR_LOCK (vagg);

  GST_OBJECT_LOCK (vagg);
  last_pad = (GST_ELEMENT (vagg)->numsinkpads - 1 == 0);
  GST_OBJECT_UNLOCK (vagg);

  if (last_pad)
    gst_video_aggregator_reset (vagg);

  gst_buffer_replace (&vaggpad->buffer, NULL);

  gst_child_proxy_child_removed (GST_CHILD_PROXY (vagg), G_OBJECT (vaggpad),
      GST_OBJECT_NAME (vaggpad));

  GST_ELEMENT_CLASS (gst_video_aggregator_parent_class)->release_pad
      (GST_ELEMENT (vagg), pad);

  gst_pad_mark_reconfigure (GST_AGGREGATOR_SRC_PAD (vagg));

  GST_VIDEO_AGGREGATOR_UNLOCK (vagg);
  return;
}

static GstFlowReturn
gst_video_aggregator_get_output_buffer (GstVideoAggregator * videoaggregator,
    GstBuffer ** outbuf)
{
  guint outsize;
  static GstAllocationParams params = { 0, 15, 0, 0, };

  outsize = GST_VIDEO_INFO_SIZE (&videoaggregator->info);
  *outbuf = gst_buffer_new_allocate (NULL, outsize, &params);

  if (*outbuf == NULL) {
    GST_ERROR_OBJECT (videoaggregator,
        "Could not instantiate buffer of size: %d", outsize);
  }

  return GST_FLOW_OK;
}

static gboolean
gst_video_aggregator_pad_sink_acceptcaps (GstPad * pad,
    GstVideoAggregator * vagg, GstCaps * caps)
{
  gboolean ret;
  GstCaps *modified_caps;
  GstCaps *accepted_caps;
  GstCaps *template_caps;
  gboolean had_current_caps = TRUE;
  gint i, n;
  GstStructure *s;
  GstAggregator *agg = GST_AGGREGATOR (vagg);

  GST_DEBUG_OBJECT (pad, "%" GST_PTR_FORMAT, caps);

  accepted_caps = gst_pad_get_current_caps (GST_PAD (agg->srcpad));

  template_caps = gst_pad_get_pad_template_caps (GST_PAD (agg->srcpad));

  if (accepted_caps == NULL) {
    accepted_caps = template_caps;
    had_current_caps = FALSE;
  }

  accepted_caps = gst_caps_make_writable (accepted_caps);

  GST_LOG_OBJECT (pad, "src caps %" GST_PTR_FORMAT, accepted_caps);

  n = gst_caps_get_size (accepted_caps);
  for (i = 0; i < n; i++) {
    s = gst_caps_get_structure (accepted_caps, i);
    gst_structure_set (s, "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
        "height", GST_TYPE_INT_RANGE, 1, G_MAXINT,
        "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);

    gst_structure_remove_fields (s, "colorimetry", "chroma-site", "format",
        "pixel-aspect-ratio", NULL);
  }

  modified_caps = gst_caps_intersect (accepted_caps, template_caps);

  ret = gst_caps_can_intersect (caps, accepted_caps);
  GST_DEBUG_OBJECT (pad, "%saccepted caps %" GST_PTR_FORMAT,
      (ret ? "" : "not "), caps);
  gst_caps_unref (accepted_caps);
  gst_caps_unref (modified_caps);
  if (had_current_caps)
    gst_caps_unref (template_caps);
  return ret;
}

static gboolean
gst_video_aggregator_sink_query (GstAggregator * agg, GstAggregatorPad * bpad,
    GstQuery * query)
{
  GstVideoAggregator *vagg = GST_VIDEO_AGGREGATOR (agg);
  GstVideoAggregatorPad *pad = GST_VIDEO_AGGREGATOR_PAD (bpad);
  gboolean ret = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *filter, *caps;

      gst_query_parse_caps (query, &filter);
      caps =
          gst_video_aggregator_pad_sink_getcaps (GST_PAD (pad), vagg, filter);
      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);
      ret = TRUE;
      break;
    }
    case GST_QUERY_ACCEPT_CAPS:
    {
      GstCaps *caps;

      gst_query_parse_accept_caps (query, &caps);
      ret =
          gst_video_aggregator_pad_sink_acceptcaps (GST_PAD (pad), vagg, caps);
      gst_query_set_accept_caps_result (query, ret);
      ret = TRUE;
      break;
    }
    default:
      ret =
          GST_AGGREGATOR_CLASS (gst_video_aggregator_parent_class)->sink_query
          (agg, bpad, query);
      break;
  }
  return ret;
}

/* GObject vmethods */
static void
gst_video_aggregator_finalize (GObject * o)
{
  GstVideoAggregator *vagg = GST_VIDEO_AGGREGATOR (o);

  g_mutex_clear (&vagg->priv->lock);

  G_OBJECT_CLASS (gst_video_aggregator_parent_class)->finalize (o);
}

static void
gst_video_aggregator_dispose (GObject * o)
{
  GstVideoAggregator *vagg = GST_VIDEO_AGGREGATOR (o);

  gst_caps_replace (&vagg->priv->current_caps, NULL);

  G_OBJECT_CLASS (gst_video_aggregator_parent_class)->dispose (o);
}

static void
gst_video_aggregator_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_video_aggregator_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* GObject boilerplate */
static void
gst_video_aggregator_class_init (GstVideoAggregatorClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  GstAggregatorClass *agg_class = (GstAggregatorClass *) klass;

  GST_DEBUG_CATEGORY_INIT (gst_video_aggregator_debug, "videoaggregator", 0,
      "base video aggregator");

  gst_video_aggregator_parent_class = g_type_class_peek_parent (klass);

  g_type_class_add_private (klass, sizeof (GstVideoAggregatorPrivate));

  gobject_class->finalize = gst_video_aggregator_finalize;
  gobject_class->dispose = gst_video_aggregator_dispose;

  gobject_class->get_property = gst_video_aggregator_get_property;
  gobject_class->set_property = gst_video_aggregator_set_property;

  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_video_aggregator_request_new_pad);
  gstelement_class->release_pad =
      GST_DEBUG_FUNCPTR (gst_video_aggregator_release_pad);

  agg_class->sinkpads_type = GST_TYPE_VIDEO_AGGREGATOR_PAD;
  agg_class->start = gst_video_aggregator_start;
  agg_class->stop = gst_video_aggregator_stop;
  agg_class->sink_query = gst_video_aggregator_sink_query;
  agg_class->sink_event = gst_video_aggregator_sink_event;
  agg_class->flush = gst_video_aggregator_flush;
  agg_class->aggregate = gst_video_aggregator_aggregate;
  agg_class->src_event = gst_video_aggregator_src_event;
  agg_class->src_query = gst_video_aggregator_src_query;
  agg_class->get_next_time = gst_video_aggregator_get_next_time;

  klass->find_best_format = gst_video_aggregator_find_best_format;
  klass->get_output_buffer = gst_video_aggregator_get_output_buffer;
  klass->update_caps = gst_video_aggregator_default_update_caps;
  klass->fixate_caps = gst_video_aggregator_default_fixate_caps;

  /* Register the pad class */
  g_type_class_ref (GST_TYPE_VIDEO_AGGREGATOR_PAD);
}

static inline GstCaps *
_get_non_alpha_caps_from_template (GstVideoAggregatorClass * klass)
{
  GstCaps *result;
  GstCaps *templatecaps;
  guint i, size;

  templatecaps =
      gst_pad_template_get_caps (gst_element_class_get_pad_template
      (GST_ELEMENT_CLASS (klass), "sink_%u"));

  size = gst_caps_get_size (templatecaps);
  result = gst_caps_new_empty ();
  for (i = 0; i < size; i++) {
    GstStructure *s = gst_caps_get_structure (templatecaps, i);
    const GValue *formats = gst_structure_get_value (s, "format");
    GValue new_formats = { 0, };
    gboolean has_format = FALSE;

    /* FIXME what to do if formats are missing? */
    if (formats) {
      const GstVideoFormatInfo *info;

      if (GST_VALUE_HOLDS_LIST (formats)) {
        guint list_size = gst_value_list_get_size (formats);
        guint index;

        g_value_init (&new_formats, GST_TYPE_LIST);

        for (index = 0; index < list_size; index++) {
          const GValue *list_item = gst_value_list_get_value (formats, index);

          info =
              gst_video_format_get_info (gst_video_format_from_string
              (g_value_get_string (list_item)));
          if (!GST_VIDEO_FORMAT_INFO_HAS_ALPHA (info)) {
            has_format = TRUE;
            gst_value_list_append_value (&new_formats, list_item);
          }
        }

      } else if (G_VALUE_HOLDS_STRING (formats)) {
        info =
            gst_video_format_get_info (gst_video_format_from_string
            (g_value_get_string (formats)));
        if (!GST_VIDEO_FORMAT_INFO_HAS_ALPHA (info)) {
          has_format = TRUE;
          gst_value_init_and_copy (&new_formats, formats);
        }

      } else {
        g_assert_not_reached ();
        GST_WARNING ("Unexpected type for video 'format' field: %s",
            G_VALUE_TYPE_NAME (formats));
      }

      if (has_format) {
        s = gst_structure_copy (s);
        gst_structure_take_value (s, "format", &new_formats);
        gst_caps_append_structure (result, s);
      }

    }
  }

  gst_caps_unref (templatecaps);

  return result;
}

static GMutex sink_caps_mutex;

static void
gst_video_aggregator_init (GstVideoAggregator * vagg,
    GstVideoAggregatorClass * klass)
{

  vagg->priv =
      G_TYPE_INSTANCE_GET_PRIVATE (vagg, GST_TYPE_VIDEO_AGGREGATOR,
      GstVideoAggregatorPrivate);

  vagg->priv->current_caps = NULL;

  g_mutex_init (&vagg->priv->lock);

  /* initialize variables */
  g_mutex_lock (&sink_caps_mutex);
  if (klass->sink_non_alpha_caps == NULL) {
    klass->sink_non_alpha_caps = _get_non_alpha_caps_from_template (klass);

    /* The caps is cached */
    GST_MINI_OBJECT_FLAG_SET (klass->sink_non_alpha_caps,
        GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);
  }
  g_mutex_unlock (&sink_caps_mutex);

  gst_video_aggregator_reset (vagg);
}
