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

#include "videoconvert.h"

#include "gstvideoaggregator.h"
#include "gstvideoaggregatorpad.h"

GST_DEBUG_CATEGORY_STATIC (gst_videoaggregator_debug);
#define GST_CAT_DEFAULT gst_videoaggregator_debug

/* Needed prototypes */
static void gst_videoaggregator_reset_qos (GstVideoAggregator * vagg);

/****************************************
 * GstVideoAggregatorPad implementation *
 ****************************************/

#define DEFAULT_PAD_ZORDER 0
enum
{
  PROP_PAD_0,
  PROP_PAD_ZORDER,
};


struct _GstVideoAggregatorPadPrivate
{
  /* Converter, if NULL no conversion is done */
  VideoConvert *convert;
};

G_DEFINE_TYPE (GstVideoAggregatorPad, gst_videoaggregator_pad,
    GST_TYPE_AGGREGATOR_PAD);

static void
gst_videoaggregator_pad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVideoAggregatorPad *pad = GST_VIDEO_AGGREGATOR_PAD (object);

  switch (prop_id) {
    case PROP_PAD_ZORDER:
      g_value_set_uint (value, pad->zorder);
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
gst_videoaggregator_pad_set_property (GObject * object, guint prop_id,
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
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  gst_object_unref (vagg);
}

static gboolean
_flush_pad (GstAggregatorPad * aggpad, GstAggregator * aggregator)
{
  GstVideoAggregator *vagg = GST_VIDEO_AGGREGATOR (aggregator);
  GstVideoAggregatorPad *pad = GST_VIDEO_AGGREGATOR_PAD (aggpad);

  gst_videoaggregator_reset_qos (vagg);
  gst_buffer_replace (&pad->buffer, NULL);
  pad->start_time = -1;
  pad->end_time = -1;

  return TRUE;
}

static void
gst_videoaggregator_pad_finalize (GObject * o)
{
  GstVideoAggregatorPad *vaggpad = GST_VIDEO_AGGREGATOR_PAD (o);

  if (vaggpad->priv->convert)
    badvideoconvert_convert_free (vaggpad->priv->convert);
  vaggpad->priv->convert = NULL;

  G_OBJECT_CLASS (gst_videoaggregator_pad_parent_class)->finalize (o);
}

static void
gst_videoaggregator_pad_class_init (GstVideoAggregatorPadClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstAggregatorPadClass *aggpadclass = (GstAggregatorPadClass *) klass;

  gobject_class->set_property = gst_videoaggregator_pad_set_property;
  gobject_class->get_property = gst_videoaggregator_pad_get_property;
  gobject_class->finalize = gst_videoaggregator_pad_finalize;

  g_object_class_install_property (gobject_class, PROP_PAD_ZORDER,
      g_param_spec_uint ("zorder", "Z-Order", "Z Order of the picture",
          0, 10000, DEFAULT_PAD_ZORDER,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));

  g_type_class_add_private (klass, sizeof (GstVideoAggregatorPadPrivate));

  aggpadclass->flush = GST_DEBUG_FUNCPTR (_flush_pad);
}

static void
gst_videoaggregator_pad_init (GstVideoAggregatorPad * vaggpad)
{
  vaggpad->priv =
      G_TYPE_INSTANCE_GET_PRIVATE (vaggpad, GST_TYPE_VIDEO_AGGREGATOR_PAD,
      GstVideoAggregatorPadPrivate);

  vaggpad->zorder = DEFAULT_PAD_ZORDER;
  vaggpad->need_conversion_update = FALSE;
  vaggpad->aggregated_frame = NULL;
  vaggpad->converted_buffer = NULL;

  vaggpad->priv->convert = NULL;
}

/*********************************
 * GstChildProxy implementation  *
 *********************************/
static GObject *
gst_videoaggregator_child_proxy_get_child_by_index (GstChildProxy * child_proxy,
    guint index)
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
gst_videoaggregator_child_proxy_get_children_count (GstChildProxy * child_proxy)
{
  guint count = 0;

  GST_OBJECT_LOCK (child_proxy);
  count = GST_ELEMENT (child_proxy)->numsinkpads;
  GST_OBJECT_UNLOCK (child_proxy);

  GST_INFO_OBJECT (child_proxy, "Children Count: %d", count);

  return count;
}

static void
gst_videoaggregator_child_proxy_init (gpointer g_iface, gpointer iface_data)
{
  GstChildProxyInterface *iface = g_iface;

  GST_INFO ("intializing child proxy interface");
  iface->get_child_by_index =
      gst_videoaggregator_child_proxy_get_child_by_index;
  iface->get_children_count =
      gst_videoaggregator_child_proxy_get_children_count;
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


#define GST_VIDEO_AGGREGATOR_GET_SETCAPS_LOCK(vagg) (&GST_VIDEO_AGGREGATOR(vagg)->priv->setcaps_lock)
#define GST_VIDEO_AGGREGATOR_SETCAPS_LOCK(vagg)   G_STMT_START {  \
  GST_LOG_OBJECT (vagg, "Taking SETCAPS lock from thread %p",     \
        g_thread_self());                                         \
  g_mutex_lock(GST_VIDEO_AGGREGATOR_GET_SETCAPS_LOCK(vagg));      \
  GST_LOG_OBJECT (vagg, "Took SETCAPS lock from thread %p",       \
        g_thread_self());                                         \
  } G_STMT_END

#define GST_VIDEO_AGGREGATOR_SETCAPS_UNLOCK(vagg)   G_STMT_START {  \
  GST_LOG_OBJECT (vagg, "Releasing SETCAPS lock from thread %p",    \
        g_thread_self());                                           \
  g_mutex_unlock(GST_VIDEO_AGGREGATOR_GET_SETCAPS_LOCK(vagg));      \
  GST_LOG_OBJECT (vagg, "Took SETCAPS lock from thread %p",         \
        g_thread_self());                                           \
  } G_STMT_END

struct _GstVideoAggregatorPrivate
{
  /* Lock to prevent the state to change while aggregating */
  GMutex lock;

  /* Lock to prevent two src setcaps from happening at the same time  */
  GMutex setcaps_lock;

  /* Current downstream segment */
  GstClockTime ts_offset;
  guint64 nframes;

  /* QoS stuff */
  gdouble proportion;
  GstClockTime earliest_time;
  guint64 qos_processed, qos_dropped;

  /* current caps */
  GstCaps *current_caps;
  gboolean send_caps;
};

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (GstVideoAggregator, gst_videoaggregator,
    GST_TYPE_AGGREGATOR, G_IMPLEMENT_INTERFACE (GST_TYPE_CHILD_PROXY,
        gst_videoaggregator_child_proxy_init));

static void
_find_best_video_format (GstVideoAggregator * vagg, GstCaps * downstream_caps,
    GstVideoInfo * best_info, GstVideoFormat * best_format,
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
      *best_format = GST_VIDEO_INFO_FORMAT (&pad->info);
      *best_info = pad->info;
      best_format_number = format_number;
    } else if (format_number > best_format_number) {
      *best_format = GST_VIDEO_INFO_FORMAT (&pad->info);
      *best_info = pad->info;
      best_format_number = format_number;
    }
  }
  GST_OBJECT_UNLOCK (vagg);

  g_hash_table_unref (formats_table);
}

static gboolean
gst_videoaggregator_update_converters (GstVideoAggregator * vagg)
{
  GList *tmp;
  GstVideoAggregatorPad *pad;
  GstVideoFormat best_format;
  GstVideoInfo best_info;
  gboolean at_least_one_alpha = FALSE;
  GstCaps *downstream_caps;
  gchar *best_colorimetry;
  const gchar *best_chroma;
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (vagg);
  GstVideoAggregatorClass *vagg_klass = (GstVideoAggregatorClass *) klass;
  GstAggregator *agg = GST_AGGREGATOR (vagg);

  best_format = GST_VIDEO_FORMAT_UNKNOWN;
  gst_video_info_init (&best_info);

  downstream_caps = gst_pad_get_allowed_caps (agg->srcpad);

  if (!downstream_caps || gst_caps_is_empty (downstream_caps)) {
    GST_INFO_OBJECT (vagg, "No downstream caps found %"
        GST_PTR_FORMAT, downstream_caps);
    if (downstream_caps)
      gst_caps_unref (downstream_caps);
    return FALSE;
  }


  if (vagg_klass->disable_frame_conversion == FALSE)
    _find_best_video_format (vagg, downstream_caps, &best_info, &best_format,
        &at_least_one_alpha);

  if (best_format == GST_VIDEO_FORMAT_UNKNOWN) {
    downstream_caps = gst_caps_fixate (downstream_caps);
    gst_video_info_from_caps (&best_info, downstream_caps);
    best_format = GST_VIDEO_INFO_FORMAT (&best_info);
  }

  gst_caps_unref (downstream_caps);

  if (at_least_one_alpha
      && !(best_info.finfo->flags & GST_VIDEO_FORMAT_FLAG_ALPHA)) {
    GST_ELEMENT_ERROR (vagg, CORE, NEGOTIATION,
        ("At least one of the input pads contains alpha, but downstream can't support alpha."),
        ("Either convert your inputs to not contain alpha or add a videoconvert after the aggregator"));
    return FALSE;
  }

  best_colorimetry = gst_video_colorimetry_to_string (&(best_info.colorimetry));
  best_chroma = gst_video_chroma_to_string (best_info.chroma_site);
  vagg->info = best_info;

  GST_DEBUG_OBJECT (vagg,
      "The output format will now be : %d with colorimetry : %s and chroma : %s",
      best_format, best_colorimetry, best_chroma);

  /* Then browse the sinks once more, setting or unsetting conversion if needed */
  GST_OBJECT_LOCK (vagg);
  for (tmp = GST_ELEMENT (vagg)->sinkpads; tmp; tmp = tmp->next) {
    gchar *colorimetry;
    const gchar *chroma;

    pad = tmp->data;

    if (!pad->info.finfo)
      continue;

    if (GST_VIDEO_INFO_FORMAT (&pad->info) == GST_VIDEO_FORMAT_UNKNOWN)
      continue;

    if (pad->priv->convert)
      badvideoconvert_convert_free (pad->priv->convert);

    pad->priv->convert = NULL;

    colorimetry = gst_video_colorimetry_to_string (&(pad->info.colorimetry));
    chroma = gst_video_chroma_to_string (pad->info.chroma_site);

    if (best_format != GST_VIDEO_INFO_FORMAT (&pad->info) ||
        g_strcmp0 (colorimetry, best_colorimetry) ||
        g_strcmp0 (chroma, best_chroma)) {
      GST_DEBUG_OBJECT (pad, "This pad will be converted from %d to %d",
          GST_VIDEO_INFO_FORMAT (&pad->info),
          GST_VIDEO_INFO_FORMAT (&best_info));
      pad->priv->convert = badvideoconvert_convert_new (&pad->info, &best_info);
      pad->need_conversion_update = TRUE;
      if (!pad->priv->convert) {
        g_free (colorimetry);
        g_free (best_colorimetry);
        GST_WARNING ("No path found for conversion");
        GST_OBJECT_UNLOCK (vagg);
        return FALSE;
      }
    } else {
      GST_DEBUG_OBJECT (pad, "This pad will not need conversion");
    }
    g_free (colorimetry);
  }
  GST_OBJECT_UNLOCK (vagg);

  g_free (best_colorimetry);
  return TRUE;
}

static gboolean
gst_videoaggregator_src_setcaps (GstVideoAggregator * vagg, GstCaps * caps)
{
  GstAggregator *agg = GST_AGGREGATOR (vagg);
  gboolean ret = FALSE;
  GstVideoInfo info;

  GstPad *pad = GST_AGGREGATOR (vagg)->srcpad;

  GST_INFO_OBJECT (pad, "set src caps: %" GST_PTR_FORMAT, caps);

  if (!gst_video_info_from_caps (&info, caps))
    goto done;

  ret = TRUE;

  GST_VIDEO_AGGREGATOR_LOCK (vagg);

  if (GST_VIDEO_INFO_FPS_N (&vagg->info) != GST_VIDEO_INFO_FPS_N (&info) ||
      GST_VIDEO_INFO_FPS_D (&vagg->info) != GST_VIDEO_INFO_FPS_D (&info)) {
    if (agg->segment.position != -1) {
      vagg->priv->ts_offset = agg->segment.position - agg->segment.start;
      vagg->priv->nframes = 0;
    }
    gst_videoaggregator_reset_qos (vagg);
  }

  vagg->info = info;

  GST_VIDEO_AGGREGATOR_UNLOCK (vagg);

  if (vagg->priv->current_caps == NULL ||
      gst_caps_is_equal (caps, vagg->priv->current_caps) == FALSE) {
    gst_caps_replace (&vagg->priv->current_caps, caps);
    vagg->priv->send_caps = TRUE;
  }

done:
  return ret;
}

static gboolean
gst_videoaggregator_update_src_caps (GstVideoAggregator * vagg)
{
  GList *l;
  gint best_width = -1, best_height = -1;
  gdouble best_fps = -1, cur_fps;
  gint best_fps_n = -1, best_fps_d = -1;
  gboolean ret = TRUE;
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (vagg);
  GstVideoAggregatorClass *vagg_klass = (GstVideoAggregatorClass *) klass;
  GstAggregator *agg = GST_AGGREGATOR (vagg);

  GST_VIDEO_AGGREGATOR_SETCAPS_LOCK (vagg);
  GST_VIDEO_AGGREGATOR_LOCK (vagg);
  GST_OBJECT_LOCK (vagg);
  for (l = GST_ELEMENT (vagg)->sinkpads; l; l = l->next) {
    GstVideoAggregatorPad *mpad = l->data;
    gint this_width, this_height;
    gint fps_n, fps_d;
    gint width, height;

    fps_n = GST_VIDEO_INFO_FPS_N (&mpad->info);
    fps_d = GST_VIDEO_INFO_FPS_D (&mpad->info);
    width = GST_VIDEO_INFO_WIDTH (&mpad->info);
    height = GST_VIDEO_INFO_HEIGHT (&mpad->info);

    if (width == 0 || height == 0)
      continue;

    this_width = width;
    this_height = height;

    if (best_width < this_width)
      best_width = this_width;
    if (best_height < this_height)
      best_height = this_height;

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

  if (best_width > 0 && best_height > 0 && best_fps > 0) {
    GstCaps *caps, *peercaps;
    GstStructure *s;
    GstVideoInfo info;

    if (GST_VIDEO_INFO_FPS_N (&vagg->info) != best_fps_n ||
        GST_VIDEO_INFO_FPS_D (&vagg->info) != best_fps_d) {
      if (agg->segment.position != -1) {
        vagg->priv->ts_offset = agg->segment.position - agg->segment.start;
        vagg->priv->nframes = 0;
      }
    }
    gst_video_info_init (&info);
    gst_video_info_set_format (&info, GST_VIDEO_INFO_FORMAT (&vagg->info),
        best_width, best_height);
    info.fps_n = best_fps_n;
    info.fps_d = best_fps_d;
    info.par_n = GST_VIDEO_INFO_PAR_N (&vagg->info);
    info.par_d = GST_VIDEO_INFO_PAR_D (&vagg->info);

    if (vagg_klass->update_info) {
      if (!vagg_klass->update_info (vagg, &info)) {
        ret = FALSE;
        GST_VIDEO_AGGREGATOR_UNLOCK (vagg);
        goto done;
      }
    }

    caps = gst_video_info_to_caps (&info);

    peercaps = gst_pad_peer_query_caps (agg->srcpad, NULL);
    if (peercaps) {
      GstCaps *tmp;

      s = gst_caps_get_structure (caps, 0);
      gst_structure_set (s, "width", GST_TYPE_INT_RANGE, 1, G_MAXINT, "height",
          GST_TYPE_INT_RANGE, 1, G_MAXINT, "framerate", GST_TYPE_FRACTION_RANGE,
          0, 1, G_MAXINT, 1, NULL);

      tmp = gst_caps_intersect (caps, peercaps);
      gst_caps_unref (caps);
      gst_caps_unref (peercaps);
      caps = tmp;
      if (gst_caps_is_empty (caps)) {
        GST_DEBUG_OBJECT (vagg, "empty caps");
        ret = FALSE;
        GST_VIDEO_AGGREGATOR_UNLOCK (vagg);
        goto done;
      }

      caps = gst_caps_truncate (caps);
      s = gst_caps_get_structure (caps, 0);
      gst_structure_fixate_field_nearest_int (s, "width", info.width);
      gst_structure_fixate_field_nearest_int (s, "height", info.height);
      gst_structure_fixate_field_nearest_fraction (s, "framerate", best_fps_n,
          best_fps_d);

      gst_structure_get_int (s, "width", &info.width);
      gst_structure_get_int (s, "height", &info.height);
      gst_structure_get_fraction (s, "framerate", &info.fps_n, &info.fps_d);
    }

    gst_caps_unref (caps);
    caps = gst_video_info_to_caps (&info);

    GST_VIDEO_AGGREGATOR_UNLOCK (vagg);

    if (gst_videoaggregator_src_setcaps (vagg, caps)) {
      if (vagg_klass->negotiated_caps)
        ret =
            GST_VIDEO_AGGREGATOR_GET_CLASS (vagg)->negotiated_caps (vagg, caps);
    }
    gst_caps_unref (caps);
  } else {
    GST_VIDEO_AGGREGATOR_UNLOCK (vagg);
  }

done:
  GST_VIDEO_AGGREGATOR_SETCAPS_UNLOCK (vagg);

  return ret;
}

static gboolean
gst_videoaggregator_pad_sink_setcaps (GstPad * pad, GstObject * parent,
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
  if (GST_VIDEO_INFO_FORMAT (&vagg->info) != GST_VIDEO_FORMAT_UNKNOWN) {
    if (GST_VIDEO_INFO_PAR_N (&vagg->info) != GST_VIDEO_INFO_PAR_N (&info)
        || GST_VIDEO_INFO_PAR_D (&vagg->info) != GST_VIDEO_INFO_PAR_D (&info) ||
        GST_VIDEO_INFO_INTERLACE_MODE (&vagg->info) !=
        GST_VIDEO_INFO_INTERLACE_MODE (&info)) {
      GST_ERROR_OBJECT (pad,
          "got input caps %" GST_PTR_FORMAT ", but " "current caps are %"
          GST_PTR_FORMAT, caps, vagg->priv->current_caps);
      GST_VIDEO_AGGREGATOR_UNLOCK (vagg);
      return FALSE;
    }
  }

  vaggpad->info = info;

  ret = gst_videoaggregator_update_converters (vagg);
  GST_VIDEO_AGGREGATOR_UNLOCK (vagg);

  if (ret)
    ret = gst_videoaggregator_update_src_caps (vagg);

beach:
  return ret;
}

static GstCaps *
gst_videoaggregator_pad_sink_getcaps (GstPad * pad, GstVideoAggregator * vagg,
    GstCaps * filter)
{
  GstCaps *srccaps;
  GstCaps *template_caps;
  GstCaps *filtered_caps;
  GstCaps *returned_caps;
  GstStructure *s;
  gboolean had_current_caps = TRUE;
  gint i, n;
  GstAggregator *agg = GST_AGGREGATOR (vagg);

  template_caps = gst_pad_get_pad_template_caps (GST_PAD (agg->srcpad));

  srccaps = gst_pad_get_current_caps (GST_PAD (agg->srcpad));
  if (srccaps == NULL) {
    had_current_caps = FALSE;
    srccaps = template_caps;
  }

  srccaps = gst_caps_make_writable (srccaps);

  n = gst_caps_get_size (srccaps);
  for (i = 0; i < n; i++) {
    s = gst_caps_get_structure (srccaps, i);
    gst_structure_set (s, "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
        "height", GST_TYPE_INT_RANGE, 1, G_MAXINT,
        "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);
    if (!gst_structure_has_field (s, "pixel-aspect-ratio"))
      gst_structure_set (s, "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1,
          NULL);

    gst_structure_remove_fields (s, "colorimetry", "chroma-site", "format",
        NULL);
  }

  filtered_caps = srccaps;
  if (filter)
    filtered_caps = gst_caps_intersect (srccaps, filter);
  returned_caps = gst_caps_intersect (filtered_caps, template_caps);

  gst_caps_unref (srccaps);
  if (filter)
    gst_caps_unref (filtered_caps);
  if (had_current_caps)
    gst_caps_unref (template_caps);

  return returned_caps;
}

static void
gst_videoaggregator_update_qos (GstVideoAggregator * vagg, gdouble proportion,
    GstClockTimeDiff diff, GstClockTime timestamp)
{
  GST_DEBUG_OBJECT (vagg,
      "Updating QoS: proportion %lf, diff %s%" GST_TIME_FORMAT ", timestamp %"
      GST_TIME_FORMAT, proportion, (diff < 0) ? "-" : "",
      GST_TIME_ARGS (ABS (diff)), GST_TIME_ARGS (timestamp));

  GST_OBJECT_LOCK (vagg);
  vagg->priv->proportion = proportion;
  if (G_LIKELY (timestamp != GST_CLOCK_TIME_NONE)) {
    if (G_UNLIKELY (diff > 0))
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
gst_videoaggregator_reset_qos (GstVideoAggregator * vagg)
{
  gst_videoaggregator_update_qos (vagg, 0.5, 0, GST_CLOCK_TIME_NONE);
  vagg->priv->qos_processed = vagg->priv->qos_dropped = 0;
}

static void
gst_videoaggregator_read_qos (GstVideoAggregator * vagg, gdouble * proportion,
    GstClockTime * time)
{
  GST_OBJECT_LOCK (vagg);
  *proportion = vagg->priv->proportion;
  *time = vagg->priv->earliest_time;
  GST_OBJECT_UNLOCK (vagg);
}

static void
gst_videoaggregator_reset (GstVideoAggregator * vagg)
{
  GstAggregator *agg = GST_AGGREGATOR (vagg);
  GList *l;

  gst_video_info_init (&vagg->info);
  vagg->priv->ts_offset = 0;
  vagg->priv->nframes = 0;

  gst_segment_init (&agg->segment, GST_FORMAT_TIME);
  agg->segment.position = -1;

  gst_videoaggregator_reset_qos (vagg);

  GST_OBJECT_LOCK (vagg);
  for (l = GST_ELEMENT (vagg)->sinkpads; l; l = l->next) {
    GstVideoAggregatorPad *p = l->data;

    gst_buffer_replace (&p->buffer, NULL);
    gst_buffer_replace (&p->queued, NULL);
    p->start_time = -1;
    p->end_time = -1;

    gst_video_info_init (&p->info);
  }
  GST_OBJECT_UNLOCK (vagg);
}

#define GST_FLOW_NEEDS_DATA GST_FLOW_CUSTOM_ERROR
static gint
gst_videoaggregator_fill_queues (GstVideoAggregator * vagg,
    GstClockTime output_start_time, GstClockTime output_end_time)
{
  GstAggregator *agg = GST_AGGREGATOR (vagg);
  GList *l;
  gboolean eos = TRUE;
  gboolean need_more_data = FALSE;

  GST_OBJECT_LOCK (vagg);
  for (l = GST_ELEMENT (vagg)->sinkpads; l; l = l->next) {
    GstVideoAggregatorPad *pad = l->data;
    GstSegment *segment;
    GstAggregatorPad *bpad;
    GstBuffer *buf;
    GstVideoInfo *vinfo;
    gboolean is_eos;

    bpad = GST_AGGREGATOR_PAD (pad);
    segment = &bpad->segment;
    is_eos = bpad->eos;
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

      if ((pad->buffer && start_time < GST_BUFFER_TIMESTAMP (pad->buffer))
          || (pad->queued && start_time < GST_BUFFER_TIMESTAMP (pad->queued))) {
        GST_DEBUG_OBJECT (pad, "Buffer from the past, dropping");
        gst_buffer_unref (buf);
        buf = gst_aggregator_pad_steal_buffer (bpad);
        gst_buffer_unref (buf);
        need_more_data = TRUE;
        continue;
      }

      if (pad->queued) {
        end_time = start_time - GST_BUFFER_TIMESTAMP (pad->queued);
        start_time = GST_BUFFER_TIMESTAMP (pad->queued);
        gst_buffer_unref (buf);
        buf = gst_buffer_ref (pad->queued);
        vinfo = &pad->queued_vinfo;
      } else {
        end_time = GST_BUFFER_DURATION (buf);

        if (end_time == -1) {
          pad->queued = buf;
          buf = gst_aggregator_pad_steal_buffer (bpad);
          gst_buffer_unref (buf);
          pad->queued_vinfo = pad->info;
          GST_DEBUG ("end time is -1 and nothing queued");
          need_more_data = TRUE;
          continue;
        }
      }

      g_assert (start_time != -1 && end_time != -1);
      end_time += start_time;   /* convert from duration to position */

      /* Check if it's inside the segment */
      if (start_time >= segment->stop || end_time < segment->start) {
        GST_DEBUG_OBJECT (pad,
            "Buffer outside the segment : segment: [%" GST_TIME_FORMAT " -- %"
            GST_TIME_FORMAT "]" " Buffer [%" GST_TIME_FORMAT " -- %"
            GST_TIME_FORMAT "]", GST_TIME_ARGS (segment->stop),
            GST_TIME_ARGS (segment->start), GST_TIME_ARGS (start_time),
            GST_TIME_ARGS (end_time));

        if (buf == pad->queued) {
          gst_buffer_unref (buf);
          gst_buffer_replace (&pad->queued, NULL);
        } else {
          gst_buffer_unref (buf);
          buf = gst_aggregator_pad_steal_buffer (bpad);
          gst_buffer_unref (buf);
        }

        need_more_data = TRUE;
        continue;
      }

      /* Clip to segment and convert to running time */
      start_time = MAX (start_time, segment->start);
      if (segment->stop != -1)
        end_time = MIN (end_time, segment->stop);
      start_time =
          gst_segment_to_running_time (segment, GST_FORMAT_TIME, start_time);
      end_time =
          gst_segment_to_running_time (segment, GST_FORMAT_TIME, end_time);
      g_assert (start_time != -1 && end_time != -1);

      /* Convert to the output segment rate */
      if (ABS (agg->segment.rate) != 1.0) {
        start_time *= ABS (agg->segment.rate);
        end_time *= ABS (agg->segment.rate);
      }

      if (pad->end_time != -1 && pad->end_time > end_time) {
        GST_DEBUG_OBJECT (pad, "Buffer from the past, dropping");
        if (buf == pad->queued) {
          gst_buffer_unref (buf);
          gst_buffer_replace (&pad->queued, NULL);
        } else {
          gst_buffer_unref (buf);
          buf = gst_aggregator_pad_steal_buffer (bpad);
          gst_buffer_unref (buf);
        }

        need_more_data = TRUE;
        continue;
      }

      if (end_time >= output_start_time && start_time < output_end_time) {
        GST_DEBUG_OBJECT (pad,
            "Taking new buffer with start time %" GST_TIME_FORMAT,
            GST_TIME_ARGS (start_time));
        gst_buffer_replace (&pad->buffer, buf);
        pad->buffer_vinfo = *vinfo;
        pad->start_time = start_time;
        pad->end_time = end_time;

        if (buf == pad->queued) {
          gst_buffer_unref (buf);
          gst_buffer_replace (&pad->queued, NULL);
        } else {
          gst_buffer_unref (buf);
          buf = gst_aggregator_pad_steal_buffer (bpad);
          gst_buffer_unref (buf);
        }
        eos = FALSE;
      } else if (start_time >= output_end_time) {
        GST_DEBUG_OBJECT (pad, "Keeping buffer until %" GST_TIME_FORMAT,
            GST_TIME_ARGS (start_time));
        gst_buffer_unref (buf);
        eos = FALSE;
      } else {
        GST_DEBUG_OBJECT (pad, "Too old buffer -- dropping");
        if (buf == pad->queued) {
          gst_buffer_unref (buf);
          gst_buffer_replace (&pad->queued, NULL);
        } else {
          gst_buffer_unref (buf);
          buf = gst_aggregator_pad_steal_buffer (bpad);
          gst_buffer_unref (buf);
        }

        need_more_data = TRUE;
        continue;
      }
    } else {
      if (pad->end_time != -1) {
        if (pad->end_time <= output_start_time) {
          gst_buffer_replace (&pad->buffer, NULL);
          pad->start_time = pad->end_time = -1;
          if (is_eos) {
            GST_DEBUG ("I just need more data");
            need_more_data = TRUE;
          }
        } else if (is_eos) {
          eos = FALSE;
        }
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
prepare_frames (GstVideoAggregator * vagg, GstVideoAggregatorPad * pad)
{
  GstAggregatorPad *bpad = GST_AGGREGATOR_PAD (pad);

  static GstAllocationParams params = { 0, 15, 0, 0, };

  if (pad->buffer != NULL) {
    guint outsize;
    GstClockTime timestamp;
    gint64 stream_time;
    GstSegment *seg;
    GstVideoFrame *converted_frame;
    GstBuffer *converted_buf = NULL;
    GstVideoFrame *frame = g_slice_new0 (GstVideoFrame);

    seg = &bpad->segment;

    timestamp = GST_BUFFER_TIMESTAMP (pad->buffer);

    stream_time = gst_segment_to_stream_time (seg, GST_FORMAT_TIME, timestamp);

    /* sync object properties on stream time */
    if (GST_CLOCK_TIME_IS_VALID (stream_time))
      gst_object_sync_values (GST_OBJECT (pad), stream_time);


    if (!gst_video_frame_map (frame, &pad->buffer_vinfo, pad->buffer,
            GST_MAP_READ)) {
      GST_WARNING_OBJECT (vagg, "Could not map input buffer");
    }

    if (pad->priv->convert) {
      gint converted_size;

      converted_frame = g_slice_new0 (GstVideoFrame);

      /* We wait until here to set the conversion infos, in case vagg->info changed */
      if (pad->need_conversion_update) {
        pad->conversion_info = vagg->info;
        gst_video_info_set_format (&(pad->conversion_info),
            GST_VIDEO_INFO_FORMAT (&vagg->info), pad->info.width,
            pad->info.height);
        pad->need_conversion_update = FALSE;
      }

      converted_size = pad->conversion_info.size;
      outsize = GST_VIDEO_INFO_SIZE (&vagg->info);
      converted_size = converted_size > outsize ? converted_size : outsize;
      converted_buf = gst_buffer_new_allocate (NULL, converted_size, &params);

      if (!gst_video_frame_map (converted_frame, &(pad->conversion_info),
              converted_buf, GST_MAP_READWRITE)) {
        GST_WARNING_OBJECT (vagg, "Could not map converted frame");

        return FALSE;
      }

      badvideoconvert_convert_convert (pad->priv->convert, converted_frame,
          frame);
      pad->converted_buffer = converted_buf;
      gst_video_frame_unmap (frame);
    } else {
      converted_frame = frame;
      converted_buf = pad->buffer;
    }

    pad->aggregated_frame = converted_frame;
  }

  return TRUE;
}

static gboolean
clean_pad (GstVideoAggregator * vagg, GstVideoAggregatorPad * pad)
{
  if (pad->aggregated_frame) {
    gst_video_frame_unmap (pad->aggregated_frame);
    g_slice_free (GstVideoFrame, pad->aggregated_frame);
    pad->aggregated_frame = NULL;
  }

  if (pad->converted_buffer) {
    gst_buffer_unref (pad->converted_buffer);
    pad->converted_buffer = NULL;
  }

  return TRUE;
}

static GstFlowReturn
gst_videoaggregator_do_aggregate (GstVideoAggregator * vagg,
    GstClockTime output_start_time, GstClockTime output_end_time,
    GstBuffer ** outbuf)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (vagg);
  GstVideoAggregatorClass *vagg_klass = (GstVideoAggregatorClass *) klass;

  g_assert (vagg_klass->aggregate_frames != NULL);
  g_assert (vagg_klass->get_output_buffer != NULL);

  if ((ret = vagg_klass->get_output_buffer (vagg, outbuf)) != GST_FLOW_OK) {
    GST_WARNING_OBJECT (vagg, "Could not get an output buffer, reason: %s",
        gst_flow_get_name (ret));
    return ret;
  }

  GST_BUFFER_TIMESTAMP (*outbuf) = output_start_time;
  GST_BUFFER_DURATION (*outbuf) = output_end_time - output_start_time;

  if (vagg_klass->disable_frame_conversion == FALSE) {
    /* Here we convert all the frames the subclass will have to aggregate */
    gst_aggregator_iterate_sinkpads (GST_AGGREGATOR (vagg),
        (GstAggregatorPadForeachFunc) prepare_frames, NULL);
  }

  ret = vagg_klass->aggregate_frames (vagg, *outbuf);

  gst_aggregator_iterate_sinkpads (GST_AGGREGATOR (vagg),
      (GstAggregatorPadForeachFunc) clean_pad, NULL);

  return ret;
}

/* Perform qos calculations before processing the next frame. Returns TRUE if
 * the frame should be processed, FALSE if the frame can be dropped entirely */
static gint64
gst_videoaggregator_do_qos (GstVideoAggregator * vagg, GstClockTime timestamp)
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
  gst_videoaggregator_read_qos (vagg, &proportion, &earliest_time);

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

static GstFlowReturn
gst_videoaggregator_aggregate (GstAggregator * agg)
{
  GstFlowReturn ret;
  GstVideoAggregator *vagg = GST_VIDEO_AGGREGATOR (agg);
  GstClockTime output_start_time, output_end_time;
  GstBuffer *outbuf = NULL;
  gint res;
  gint64 jitter;

  /* If we're not negotiated_caps yet... */
  if (GST_VIDEO_INFO_FORMAT (&vagg->info) == GST_VIDEO_FORMAT_UNKNOWN) {
    GST_INFO_OBJECT (agg, "Not negotiated yet!");
    return GST_FLOW_NOT_NEGOTIATED;
  }

  if (gst_pad_check_reconfigure (agg->srcpad))
    gst_videoaggregator_update_src_caps (vagg);

  if (vagg->priv->send_caps) {
    gst_aggregator_set_src_caps (agg, vagg->priv->current_caps);
    vagg->priv->send_caps = FALSE;
  }

  GST_VIDEO_AGGREGATOR_LOCK (vagg);

  if (agg->segment.position == -1)
    output_start_time = agg->segment.start;
  else
    output_start_time = agg->segment.position;

  output_end_time =
      vagg->priv->ts_offset + gst_util_uint64_scale_round (vagg->priv->nframes +
      1, GST_SECOND * GST_VIDEO_INFO_FPS_D (&vagg->info),
      GST_VIDEO_INFO_FPS_N (&vagg->info)) + agg->segment.start;

  if (agg->segment.stop != -1)
    output_end_time = MIN (output_end_time, agg->segment.stop);

  res =
      gst_videoaggregator_fill_queues (vagg, output_start_time,
      output_end_time);

  if (res == GST_FLOW_NEEDS_DATA) {
    GST_DEBUG_OBJECT (vagg, "Need more data for decisions");
    ret = GST_FLOW_OK;
    goto done;
  } else if (res == GST_FLOW_EOS) {
    GST_VIDEO_AGGREGATOR_UNLOCK (vagg);
    GST_DEBUG_OBJECT (vagg, "All sinkpads are EOS -- forwarding");
    ret = GST_FLOW_EOS;
    goto done_unlocked;
  } else if (res == GST_FLOW_ERROR) {
    GST_WARNING_OBJECT (vagg, "Error collecting buffers");
    ret = GST_FLOW_ERROR;
    goto done;
  }

  jitter = gst_videoaggregator_do_qos (vagg, output_start_time);
  if (jitter <= 0) {
    ret = gst_videoaggregator_do_aggregate (vagg, output_start_time,
        output_end_time, &outbuf);
    vagg->priv->qos_processed++;
  } else {
    GstMessage *msg;

    vagg->priv->qos_dropped++;

    /* TODO: live */
    msg =
        gst_message_new_qos (GST_OBJECT_CAST (vagg), FALSE,
        gst_segment_to_running_time (&agg->segment, GST_FORMAT_TIME,
            output_start_time), gst_segment_to_stream_time (&agg->segment,
            GST_FORMAT_TIME, output_start_time), output_start_time,
        output_end_time - output_start_time);
    gst_message_set_qos_values (msg, jitter, vagg->priv->proportion, 1000000);
    gst_message_set_qos_stats (msg, GST_FORMAT_BUFFERS,
        vagg->priv->qos_processed, vagg->priv->qos_dropped);
    gst_element_post_message (GST_ELEMENT_CAST (vagg), msg);

    ret = GST_FLOW_OK;
  }

  agg->segment.position = output_end_time;
  vagg->priv->nframes++;

  GST_VIDEO_AGGREGATOR_UNLOCK (vagg);
  if (outbuf) {
    GST_DEBUG_OBJECT (vagg,
        "Pushing buffer with ts %" GST_TIME_FORMAT " and duration %"
        GST_TIME_FORMAT, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)),
        GST_TIME_ARGS (GST_BUFFER_DURATION (outbuf)));

    ret = gst_aggregator_finish_buffer (agg, outbuf);
  }
  goto done_unlocked;

done:
  GST_VIDEO_AGGREGATOR_UNLOCK (vagg);

done_unlocked:
  return ret;
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
gst_videoaggregator_query_duration (GstVideoAggregator * vagg, GstQuery * query)
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
gst_videoaggregator_query_latency (GstVideoAggregator * vagg, GstQuery * query)
{
  GstClockTime min, max;
  gboolean live;
  gboolean res;
  GstIterator *it;
  gboolean done;
  GValue item = { 0 };

  res = TRUE;
  done = FALSE;
  live = FALSE;
  min = 0;
  max = GST_CLOCK_TIME_NONE;

  /* Take maximum of all latency values */
  it = gst_element_iterate_sink_pads (GST_ELEMENT_CAST (vagg));
  while (!done) {
    switch (gst_iterator_next (it, &item)) {
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
      case GST_ITERATOR_OK:
      {
        GstPad *pad = g_value_get_object (&item);
        GstQuery *peerquery;
        GstClockTime min_cur, max_cur;
        gboolean live_cur;

        peerquery = gst_query_new_latency ();

        /* Ask peer for latency */
        res &= gst_pad_peer_query (pad, peerquery);

        /* take max from all valid return values */
        if (res) {
          gst_query_parse_latency (peerquery, &live_cur, &min_cur, &max_cur);

          if (min_cur > min)
            min = min_cur;

          if (max_cur != GST_CLOCK_TIME_NONE &&
              ((max != GST_CLOCK_TIME_NONE && max_cur > max) ||
                  (max == GST_CLOCK_TIME_NONE)))
            max = max_cur;

          live = live || live_cur;
        }

        gst_query_unref (peerquery);
        g_value_reset (&item);
        break;
      }
      case GST_ITERATOR_RESYNC:
        live = FALSE;
        min = 0;
        max = GST_CLOCK_TIME_NONE;
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
    /* store the results */
    GST_DEBUG_OBJECT (vagg, "Calculated total latency: live %s, min %"
        GST_TIME_FORMAT ", max %" GST_TIME_FORMAT,
        (live ? "yes" : "no"), GST_TIME_ARGS (min), GST_TIME_ARGS (max));
    gst_query_set_latency (query, live, min, max);
  }

  return res;
}

static gboolean
gst_videoaggregator_src_query (GstAggregator * agg, GstQuery * query)
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
      res = gst_videoaggregator_query_duration (vagg, query);
      break;
    case GST_QUERY_LATENCY:
      res = gst_videoaggregator_query_latency (vagg, query);
      break;
    case GST_QUERY_CAPS:
      res =
          GST_AGGREGATOR_CLASS (gst_videoaggregator_parent_class)->src_query
          (agg, query);
      break;
    default:
      /* FIXME, needs a custom query handler because we have multiple
       * sinkpads */
      res = FALSE;
      break;
  }
  return res;
}

static gboolean
gst_videoaggregator_src_event (GstAggregator * agg, GstEvent * event)
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
      gst_videoaggregator_update_qos (vagg, proportion, diff, timestamp);
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
      GST_AGGREGATOR_CLASS (gst_videoaggregator_parent_class)->src_event (agg,
      event);
}

static GstFlowReturn
gst_videoaggregator_sink_clip (GstAggregator * agg,
    GstAggregatorPad * bpad, GstBuffer * buf, GstBuffer ** outbuf)
{
  GstVideoAggregatorPad *pad = GST_VIDEO_AGGREGATOR_PAD (bpad);
  GstClockTime start_time, end_time;

  start_time = GST_BUFFER_TIMESTAMP (buf);
  if (start_time == -1) {
    GST_DEBUG_OBJECT (pad, "Timestamped buffers required!");
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }

  end_time = GST_BUFFER_DURATION (buf);
  if (end_time == -1 && GST_VIDEO_INFO_FPS_N (&pad->info) != 0)
    end_time =
        gst_util_uint64_scale_int_round (GST_SECOND,
        GST_VIDEO_INFO_FPS_D (&pad->info), GST_VIDEO_INFO_FPS_N (&pad->info));
  if (end_time == -1) {
    *outbuf = buf;
    return GST_FLOW_OK;
  }

  start_time = MAX (start_time, bpad->segment.start);
  start_time =
      gst_segment_to_running_time (&bpad->segment, GST_FORMAT_TIME, start_time);

  end_time += GST_BUFFER_TIMESTAMP (buf);
  if (bpad->segment.stop != -1)
    end_time = MIN (end_time, bpad->segment.stop);
  end_time =
      gst_segment_to_running_time (&bpad->segment, GST_FORMAT_TIME, end_time);

  /* Convert to the output segment rate */
  if (ABS (agg->segment.rate) != 1.0) {
    start_time *= ABS (agg->segment.rate);
    end_time *= ABS (agg->segment.rate);
  }

  if (bpad->buffer != NULL && end_time < pad->end_time) {
    gst_buffer_unref (buf);
    *outbuf = NULL;
    return GST_FLOW_OK;
  }

  *outbuf = buf;
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_videoaggregator_flush (GstAggregator * agg)
{
  GList *l;
  gdouble abs_rate;
  GstVideoAggregator *vagg = GST_VIDEO_AGGREGATOR (agg);

  GST_INFO_OBJECT (agg, "Flushing");
  abs_rate = ABS (agg->segment.rate);
  GST_OBJECT_LOCK (vagg);
  for (l = GST_ELEMENT (vagg)->sinkpads; l; l = l->next) {
    GstVideoAggregatorPad *p = l->data;

    /* Convert to the output segment rate */
    if (ABS (agg->segment.rate) != abs_rate) {
      if (ABS (agg->segment.rate) != 1.0 && p->buffer) {
        p->start_time /= ABS (agg->segment.rate);
        p->end_time /= ABS (agg->segment.rate);
      }
      if (abs_rate != 1.0 && p->buffer) {
        p->start_time *= abs_rate;
        p->end_time *= abs_rate;
      }
    }
  }
  GST_OBJECT_UNLOCK (vagg);

  agg->segment.position = -1;
  vagg->priv->ts_offset = 0;
  vagg->priv->nframes = 0;

  gst_videoaggregator_reset_qos (vagg);
  return GST_FLOW_OK;
}

static gboolean
gst_videoaggregator_sink_event (GstAggregator * agg, GstAggregatorPad * bpad,
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
          gst_videoaggregator_pad_sink_setcaps (GST_PAD (pad),
          GST_OBJECT (vagg), caps);
      gst_event_unref (event);
      event = NULL;
      break;
    }
    case GST_EVENT_SEGMENT:{
      GstSegment seg;
      gst_event_copy_segment (event, &seg);

      g_assert (seg.format == GST_FORMAT_TIME);
      gst_videoaggregator_reset_qos (vagg);
      break;
    }
    default:
      break;
  }

  if (event != NULL)
    return GST_AGGREGATOR_CLASS (gst_videoaggregator_parent_class)->sink_event
        (agg, bpad, event);

  return ret;
}

static gboolean
gst_videoaggregator_start (GstAggregator * agg)
{
  GstVideoAggregator *vagg = GST_VIDEO_AGGREGATOR (agg);

  if (!GST_AGGREGATOR_CLASS (gst_videoaggregator_parent_class)->start (agg))
    return FALSE;

  vagg->priv->send_caps = TRUE;
  gst_segment_init (&agg->segment, GST_FORMAT_TIME);
  gst_caps_replace (&vagg->priv->current_caps, NULL);

  return TRUE;
}

static gboolean
gst_videoaggregator_stop (GstAggregator * agg)
{
  GstVideoAggregator *vagg = GST_VIDEO_AGGREGATOR (agg);

  if (!GST_AGGREGATOR_CLASS (gst_videoaggregator_parent_class)->stop (agg))
    return FALSE;

  gst_videoaggregator_reset (vagg);

  return TRUE;
}

/* GstElement vmethods */
static GstPad *
gst_videoaggregator_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * req_name, const GstCaps * caps)
{
  GstVideoAggregator *vagg;
  GstVideoAggregatorPad *vaggpad;

  vagg = GST_VIDEO_AGGREGATOR (element);

  vaggpad = (GstVideoAggregatorPad *)
      GST_ELEMENT_CLASS (gst_videoaggregator_parent_class)->request_new_pad
      (element, templ, req_name, caps);

  if (vaggpad == NULL)
    return NULL;

  GST_OBJECT_LOCK (vagg);
  vaggpad->zorder = GST_ELEMENT (vagg)->numsinkpads;
  vaggpad->start_time = -1;
  vaggpad->end_time = -1;
  element->sinkpads = g_list_sort (element->sinkpads,
      (GCompareFunc) pad_zorder_compare);
  GST_OBJECT_UNLOCK (vagg);

  gst_child_proxy_child_added (GST_CHILD_PROXY (vagg), G_OBJECT (vaggpad),
      GST_OBJECT_NAME (vaggpad));

  return GST_PAD (vaggpad);
}

static void
gst_videoaggregator_release_pad (GstElement * element, GstPad * pad)
{
  GstVideoAggregator *vagg = NULL;
  GstVideoAggregatorPad *vaggpad;
  gboolean update_caps;

  vagg = GST_VIDEO_AGGREGATOR (element);
  vaggpad = GST_VIDEO_AGGREGATOR_PAD (pad);

  GST_VIDEO_AGGREGATOR_LOCK (vagg);
  gst_videoaggregator_update_converters (vagg);
  gst_buffer_replace (&vaggpad->buffer, NULL);
  update_caps = GST_VIDEO_INFO_FORMAT (&vagg->info) != GST_VIDEO_FORMAT_UNKNOWN;
  GST_VIDEO_AGGREGATOR_UNLOCK (vagg);

  gst_child_proxy_child_removed (GST_CHILD_PROXY (vagg), G_OBJECT (vaggpad),
      GST_OBJECT_NAME (vaggpad));

  GST_ELEMENT_CLASS (gst_videoaggregator_parent_class)->release_pad (GST_ELEMENT
      (vagg), pad);

  if (update_caps)
    gst_videoaggregator_update_src_caps (vagg);

  return;
}

static GstFlowReturn
gst_videoaggregator_get_output_buffer (GstVideoAggregator * videoaggregator,
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
gst_videoaggregator_pad_sink_acceptcaps (GstPad * pad,
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
    if (!gst_structure_has_field (s, "pixel-aspect-ratio"))
      gst_structure_set (s, "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1,
          NULL);

    gst_structure_remove_fields (s, "colorimetry", "chroma-site", "format",
        NULL);
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
gst_videoaggregator_sink_query (GstAggregator * agg, GstAggregatorPad * bpad,
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
      caps = gst_videoaggregator_pad_sink_getcaps (GST_PAD (pad), vagg, filter);
      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);
      ret = TRUE;
      break;
    }
    case GST_QUERY_ACCEPT_CAPS:
    {
      GstCaps *caps;

      gst_query_parse_accept_caps (query, &caps);
      ret = gst_videoaggregator_pad_sink_acceptcaps (GST_PAD (pad), vagg, caps);
      gst_query_set_accept_caps_result (query, ret);
      ret = TRUE;
      break;
    }
    default:
      ret =
          GST_AGGREGATOR_CLASS (gst_videoaggregator_parent_class)->sink_query
          (agg, bpad, query);
      break;
  }
  return ret;
}

/* GObject vmethods */
static void
gst_videoaggregator_finalize (GObject * o)
{
  GstVideoAggregator *vagg = GST_VIDEO_AGGREGATOR (o);

  g_mutex_clear (&vagg->priv->lock);
  g_mutex_clear (&vagg->priv->setcaps_lock);

  G_OBJECT_CLASS (gst_videoaggregator_parent_class)->finalize (o);
}

static void
gst_videoaggregator_dispose (GObject * o)
{
  GstVideoAggregator *vagg = GST_VIDEO_AGGREGATOR (o);

  gst_caps_replace (&vagg->priv->current_caps, NULL);

  G_OBJECT_CLASS (gst_videoaggregator_parent_class)->dispose (o);
}

static void
gst_videoaggregator_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_videoaggregator_set_property (GObject * object,
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
gst_videoaggregator_class_init (GstVideoAggregatorClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  GstAggregatorClass *agg_class = (GstAggregatorClass *) klass;

  GST_DEBUG_CATEGORY_INIT (gst_videoaggregator_debug, "videoaggregator", 0,
      "base video aggregator");

  g_type_class_add_private (klass, sizeof (GstVideoAggregatorPrivate));

  gobject_class->finalize = gst_videoaggregator_finalize;
  gobject_class->dispose = gst_videoaggregator_dispose;

  gobject_class->get_property = gst_videoaggregator_get_property;
  gobject_class->set_property = gst_videoaggregator_set_property;

  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_videoaggregator_request_new_pad);
  gstelement_class->release_pad =
      GST_DEBUG_FUNCPTR (gst_videoaggregator_release_pad);

  gst_element_class_set_static_metadata (gstelement_class,
      "Video aggregator base class", "Filter/Editor/Video",
      "Aggregate multiple video streams",
      "Wim Taymans <wim@fluendo.com>, "
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>, "
      "Mathieu Duponchelle <mathieu.duponchelle@opencreed.com>, "
      "Thibault Saunier <tsaunier@gnome.org>");

  agg_class->sinkpads_type = GST_TYPE_VIDEO_AGGREGATOR_PAD;
  agg_class->start = gst_videoaggregator_start;
  agg_class->stop = gst_videoaggregator_stop;
  agg_class->sink_query = gst_videoaggregator_sink_query;
  agg_class->sink_event = gst_videoaggregator_sink_event;
  agg_class->flush = gst_videoaggregator_flush;
  agg_class->clip = gst_videoaggregator_sink_clip;
  agg_class->aggregate = gst_videoaggregator_aggregate;
  agg_class->src_event = gst_videoaggregator_src_event;
  agg_class->src_query = gst_videoaggregator_src_query;

  klass->get_output_buffer = gst_videoaggregator_get_output_buffer;

  /* Register the pad class */
  g_type_class_ref (GST_TYPE_VIDEO_AGGREGATOR_PAD);
}

static void
gst_videoaggregator_init (GstVideoAggregator * vagg)
{
  vagg->priv =
      G_TYPE_INSTANCE_GET_PRIVATE (vagg, GST_TYPE_VIDEO_AGGREGATOR,
      GstVideoAggregatorPrivate);

  vagg->priv->current_caps = NULL;

  g_mutex_init (&vagg->priv->lock);
  g_mutex_init (&vagg->priv->setcaps_lock);
  /* initialize variables */
  gst_videoaggregator_reset (vagg);
}
