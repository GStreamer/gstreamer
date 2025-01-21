/* GStreamer segmentation overlay
 * Copyright (C) <2023> Collabora Ltd.
 *  @author: Daniel Morin <daniel.morin@collabora.com>
 *
 * gstsegmentationoverlay.c
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
 * SECTION:element-segmentationoverlay
 * @title: segmentationoverlay
 * @see_also: #GstSegmentationOverlay
 *
 * This element create a graphical representation of the analytics object
 * segmentation metadata attached to video stream and overlay graphics above the
 * video.
 *
 * The object segmentation overlay element monitor video stream for
 * @GstAnalyticsRelationMeta and query @GstAnalyticsSegmentationMtd. Retrieved
 * @GstAnalyticsSegmentationMtd are then used to generate an overlay
 * highlighing objects detected.
 *
 * ## Example launch line
 * |[
 * gst-launch-1.0 multifilesrc location=/onnx-models/strawberries.jpg ! jpegdec ! videoconvertscale add-borders=1 ! onnxinference model-file=segmentation.onnx ! yolosegv8tensordec class-confidence-threshold=0.3 iou-threshold=0.3 max-detections=100 ! segmentationoverlay ! imagefreeze ! glimagesink
 * ]| This pipeline create an overlay representing results of an object
 * segmentation.
 *
 * Since: 1.28
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/analytics/analytics.h>
#include <math.h>

#include "gstsegmentationoverlay.h"

struct _GstSegmentationOverlay
{
  GstVideoFilter parent;

  /* State */
  gboolean active;
  gboolean flushing;

  /* properties */
  gsize color_table_size;
  gchar *selected_types_str;
  GSList *selected_type_filter;

  /* composition */
  gboolean attach_compo_to_buffer;
  GstBuffer *canvas;
  gint canvas_length;
  GstVideoOverlayComposition *composition;
  GstVideoOverlayComposition *upstream_composition;

  guint32 *color_table;
  gboolean *mask_filter;
  gsize mask_filter_len;
  gboolean update_mask_filter;
  guint32 bg_color;
};

#define DEFAULT_MAX_COLORS 10

GST_DEBUG_CATEGORY_STATIC (segmentationoverlay_debug);
#define GST_CAT_DEFAULT segmentationoverlay_debug

enum
{
  PROP_HINT_MAX_SEGMENT_TYPE = 1,
  PROP_SELECTED_TYPES,
  _PROP_COUNT
};

#define VIDEO_FORMATS GST_VIDEO_OVERLAY_COMPOSITION_BLEND_FORMATS
#define SEGMENTATION_OVERLAY_CAPS GST_VIDEO_CAPS_MAKE (VIDEO_FORMATS)

static GstStaticCaps sw_template_caps =
GST_STATIC_CAPS (SEGMENTATION_OVERLAY_CAPS);

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (SEGMENTATION_OVERLAY_CAPS)
    );

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (SEGMENTATION_OVERLAY_CAPS)
    );

G_DEFINE_TYPE (GstSegmentationOverlay,
    gst_segmentation_overlay, GST_TYPE_VIDEO_FILTER);

#define parent_class gst_segmentation_overlay_parent_class

GST_ELEMENT_REGISTER_DEFINE (segmentationoverlay, "segmentationoverlay",
    GST_RANK_NONE, GST_TYPE_SEGMENTATION_OVERLAY);

static void gst_segmentation_overlay_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);

static void gst_segmentation_overlay_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static gboolean gst_segmentation_overlay_sink_event (GstBaseTransform *
    trans, GstEvent * event);

static void gst_segmentation_overlay_before_transform (GstBaseTransform * trans,
    GstBuffer * buffer);

static gboolean gst_segmentation_overlay_start (GstBaseTransform * trans);
static gboolean gst_segmentation_overlay_stop (GstBaseTransform * trans);

static void gst_segmentation_overlay_hue_to_rgb (guint32 * rgb, double hue);

static GstFlowReturn
gst_segmentation_overlay_transform_frame_ip (GstVideoFilter * filter,
    GstVideoFrame * buf);

static void gst_segmentation_overlay_finalize (GObject * object);

static void
gst_segmentation_overlay_fill_canvas (GstSegmentationOverlay * overlay,
    GstMapInfo * canvas, GstVideoMeta * cvmeta, GstBuffer * mask,
    GstAnalyticsClsMtd * cls_mtd);


static void
gst_segmentation_overlay_class_init (GstSegmentationOverlayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstBaseTransformClass *basetransform_class;
  GstVideoFilterClass *videofilter_class;

  gobject_class = (GObjectClass *) klass;
  gobject_class->set_property = gst_segmentation_overlay_set_property;
  gobject_class->get_property = gst_segmentation_overlay_get_property;
  gobject_class->finalize = gst_segmentation_overlay_finalize;


  /* To maximum color disparity to represent segment we can set hint-maximum-
   * segment-type.*/
  g_object_class_install_property (gobject_class, PROP_HINT_MAX_SEGMENT_TYPE,
      g_param_spec_uint ("hint-maximum-segment-type",
          "Expected maximum segment type",
          "By providing the expected maximum segment type the overlay can optimize"
          " color differentiation between segment",
          1, G_MAXUINT, DEFAULT_MAX_COLORS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SELECTED_TYPES,
      g_param_spec_string ("selected-types",
          "Select segment types to overlay",
          "List of segment types to overlay separated by ';'",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  element_class = (GstElementClass *) klass;

  gst_element_class_add_static_pad_template (element_class, &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);
  gst_element_class_set_static_metadata (element_class,
      "Segmentation Overlay",
      "Visualization/Video",
      "Overlay a visual representation of segmentation metadata on the video",
      "Daniel Morin");

  basetransform_class = (GstBaseTransformClass *) klass;
  basetransform_class->passthrough_on_same_caps = FALSE;
  basetransform_class->before_transform =
      gst_segmentation_overlay_before_transform;
  basetransform_class->start = gst_segmentation_overlay_start;
  basetransform_class->stop = gst_segmentation_overlay_stop;
  basetransform_class->sink_event = gst_segmentation_overlay_sink_event;

  videofilter_class = (GstVideoFilterClass *) klass;
  videofilter_class->transform_frame_ip =
      gst_segmentation_overlay_transform_frame_ip;
}

static void
gst_segmentation_overlay_finalize (GObject * object)
{
  GstSegmentationOverlay *self = GST_SEGMENTATION_OVERLAY (object);

  g_free (self->selected_types_str);
  g_clear_slist (&self->selected_type_filter, NULL);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_segmentation_overlay_init (GstSegmentationOverlay * overlay)
{
  overlay->attach_compo_to_buffer = TRUE;
  overlay->canvas = NULL;
  overlay->composition = NULL;
  overlay->upstream_composition = NULL;
  overlay->active = FALSE;
  overlay->color_table_size = DEFAULT_MAX_COLORS;
  overlay->color_table = NULL;
  overlay->mask_filter = NULL;
  overlay->mask_filter_len = 0;
  overlay->selected_type_filter = NULL;
  overlay->update_mask_filter = FALSE;
  overlay->selected_types_str = NULL;
  overlay->bg_color = 0x00000000;
  GST_DEBUG_CATEGORY_INIT (segmentationoverlay_debug, "segmentationoverlay", 0,
      "Analytics segmentation overlay");
}

static void
gst_segmentation_overlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSegmentationOverlay *overlay = GST_SEGMENTATION_OVERLAY (object);

  if (overlay->active) {
    GST_WARNING_OBJECT (overlay, "Can't change properties"
        " while element is running");
    return;
  }

  switch (prop_id) {
    case PROP_HINT_MAX_SEGMENT_TYPE:
      overlay->color_table_size = g_value_get_uint (value);
      break;
    case PROP_SELECTED_TYPES:
    {
      char *selected_types = g_value_dup_string (value);
      g_clear_slist (&overlay->selected_type_filter, NULL);
      if (selected_types != NULL) {
        overlay->selected_types_str = selected_types;
        gchar **tokens = g_strsplit (selected_types, ";", -1);
        if (tokens != NULL && tokens[0] != NULL) {
          gchar *token = tokens[0];
          for (gsize i = 0; token != NULL; i++, token = tokens[i]) {
            overlay->selected_type_filter =
                g_slist_prepend (overlay->selected_type_filter,
                GUINT_TO_POINTER (g_quark_from_string (token)));
          }
          overlay->update_mask_filter = TRUE;
        }
      }
    }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_segmentation_overlay_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstSegmentationOverlay *overlay = GST_SEGMENTATION_OVERLAY (object);

  switch (prop_id) {
    case PROP_HINT_MAX_SEGMENT_TYPE:
      g_value_set_uint (value, overlay->color_table_size);
      break;
    case PROP_SELECTED_TYPES:
      g_value_set_string (value, overlay->selected_types_str);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_segmentation_overlay_can_handle_caps (GstCaps * incaps)
{
  gboolean ret;
  GstCaps *caps;

  caps = gst_static_caps_get (&sw_template_caps);
  ret = gst_caps_is_subset (incaps, caps);
  gst_caps_unref (caps);

  return ret;
}

static gboolean
gst_segmentation_overlay_negotiate (GstSegmentationOverlay * overlay,
    GstCaps * caps)
{
  GstBaseTransform *basetransform = GST_BASE_TRANSFORM (overlay);
  gboolean upstream_has_meta = FALSE;
  gboolean caps_has_meta = FALSE;
  gboolean alloc_has_meta = FALSE;
  gboolean attach = FALSE;
  gboolean ret = TRUE;
  guint width, height;
  GstCapsFeatures *f;
  GstCaps *overlay_caps;
  GstQuery *query;
  guint alloc_index;
  GstPad *srcpad = basetransform->srcpad;
  GstPad *sinkpad = basetransform->sinkpad;

  GST_DEBUG_OBJECT (overlay, "performing negotiation");

  /* Clear any pending reconfigure to avoid negotiating twice */
  gst_pad_check_reconfigure (sinkpad);

  /* Check if upstream caps have meta */
  if ((f = gst_caps_get_features (caps, 0))) {
    GST_DEBUG_OBJECT (overlay, "upstream has caps");
    upstream_has_meta = gst_caps_features_contains (f,
        GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION);
  }

  /* Initialize dimensions */
  width = GST_VIDEO_INFO_WIDTH (&GST_VIDEO_FILTER (overlay)->in_info);
  height = GST_VIDEO_INFO_HEIGHT (&GST_VIDEO_FILTER (overlay)->in_info);
  GST_DEBUG_OBJECT (overlay, "initial dims: %ux%u", width, height);

  if (upstream_has_meta) {
    overlay_caps = gst_caps_ref (caps);
  } else {
    GstCaps *peercaps;

    /* BaseTransform requires caps for the allocation query to work */
    overlay_caps = gst_caps_copy (caps);
    f = gst_caps_get_features (overlay_caps, 0);
    gst_caps_features_add (f,
        GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION);

    /* Then check if downstream accept overlay composition in caps */
    /* FIXME: We should probably check if downstream *prefers* the
     * overlay meta, and only enforce usage of it if we can't handle
     * the format ourselves and thus would have to drop the overlays.
     * Otherwise we should prefer what downstream wants here.
     */
    peercaps = gst_pad_peer_query_caps (srcpad, overlay_caps);
    caps_has_meta = !gst_caps_is_empty (peercaps);
    gst_caps_unref (peercaps);

    GST_DEBUG_OBJECT (overlay, "caps have overlay meta %d", caps_has_meta);
  }

  if (upstream_has_meta || caps_has_meta) {
    /* Send caps immediately, it's needed by GstBaseTransform to get a reply
     * from allocation query */
    GST_BASE_TRANSFORM_CLASS (parent_class)->set_caps (basetransform, caps,
        overlay_caps);
    ret = gst_pad_set_caps (srcpad, overlay_caps);

    /* First check if the allocation meta has compositon */
    query = gst_query_new_allocation (overlay_caps, FALSE);

    if (!gst_pad_peer_query (srcpad, query)) {
      /* no problem, we use the query defaults */
      GST_DEBUG_OBJECT (overlay, "ALLOCATION query failed");

      /* In case we were flushing, mark reconfigure and fail this method,
       * will make it retry */
      if (GST_PAD_IS_FLUSHING (GST_BASE_TRANSFORM_SRC_PAD (overlay))) {
        ret = FALSE;
        goto done;
      }
    }

    alloc_has_meta = gst_query_find_allocation_meta (query,
        GST_VIDEO_OVERLAY_COMPOSITION_META_API_TYPE, &alloc_index);

    GST_DEBUG_OBJECT (overlay, "sink alloc has overlay meta %d",
        alloc_has_meta);

    if (alloc_has_meta) {
      const GstStructure *params;

      gst_query_parse_nth_allocation_meta (query, alloc_index, &params);
      if (params) {
        if (gst_structure_get (params, "width", G_TYPE_UINT, &width,
                "height", G_TYPE_UINT, &height, NULL)) {
          GST_DEBUG_OBJECT (overlay, "received window size: %dx%d", width,
              height);
          g_assert (width != 0 && height != 0);
        }
      }
    }

    gst_query_unref (query);
  }

  /* Update render size if needed */
  overlay->canvas_length = width * height;

  /* For backward compatibility, we will prefer blitting if downstream
   * allocation does not support the meta. In other case we will prefer
   * attaching, and will fail the negotiation in the unlikely case we are
   * force to blit, but format isn't supported. */

  if (upstream_has_meta) {
    attach = TRUE;
  } else if (caps_has_meta) {
    if (alloc_has_meta) {
      attach = TRUE;
    } else {
      /* Don't attach unless we cannot handle the format */
      attach = !gst_segmentation_overlay_can_handle_caps (caps);
    }
  } else {
    ret = gst_segmentation_overlay_can_handle_caps (caps);
  }

  /* If we attach, then pick the overlay caps */
  if (attach) {
    GST_DEBUG_OBJECT (overlay, "Using caps %" GST_PTR_FORMAT, overlay_caps);
    /* Caps where already sent */
  } else if (ret) {
    GST_DEBUG_OBJECT (overlay, "Using caps %" GST_PTR_FORMAT, caps);
    GST_BASE_TRANSFORM_CLASS (parent_class)->set_caps (basetransform, caps,
        caps);
    ret = gst_pad_set_caps (srcpad, caps);
  }

  overlay->attach_compo_to_buffer = attach;

done:

  if (!ret) {
    GST_DEBUG_OBJECT (overlay, "negotiation failed, schedule reconfigure");
    gst_pad_mark_reconfigure (srcpad);
  }

  gst_caps_unref (overlay_caps);

  return ret;
}

static gboolean
gst_segmentation_overlay_setcaps (GstSegmentationOverlay * overlay,
    GstCaps * caps)
{
  gboolean ret = FALSE;

  if (!gst_video_info_from_caps (&GST_VIDEO_FILTER (overlay)->in_info, caps))
    goto invalid_caps;

  ret = gst_segmentation_overlay_negotiate (overlay, caps);
  GST_VIDEO_FILTER (overlay)->negotiated = ret;

  if (!overlay->attach_compo_to_buffer &&
      !gst_segmentation_overlay_can_handle_caps (caps)) {
    GST_DEBUG_OBJECT (overlay, "unsupported caps %" GST_PTR_FORMAT, caps);
    ret = FALSE;
  }

  return ret;

  /* ERRORS */
invalid_caps:
  {
    GST_DEBUG_OBJECT (overlay, "could not parse caps");
    return FALSE;
  }
}

static gboolean
gst_segmentation_overlay_sink_event (GstBaseTransform * trans, GstEvent * event)
{
  gboolean ret = FALSE;
  GST_DEBUG_OBJECT (trans, "received sink event %s",
      GST_EVENT_TYPE_NAME (event));

  GstSegmentationOverlay *overlay = GST_SEGMENTATION_OVERLAY (trans);
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;
      gst_event_parse_caps (event, &caps);
      ret = gst_segmentation_overlay_setcaps (overlay, caps);
      gst_event_unref (event);
      break;
    }
    default:
      ret = GST_BASE_TRANSFORM_CLASS (parent_class)->sink_event (trans, event);
      break;
  }

  return ret;
}

static void
gst_segmentation_overlay_before_transform (GstBaseTransform * trans,
    GstBuffer * buffer)
{
  GstSegmentationOverlay *overlay = GST_SEGMENTATION_OVERLAY (trans);
  gdouble inc;

  if (overlay->color_table == NULL) {
    /* Prepare a color table uniformely discributed to maximize distinctivity
     * of each segment */
    overlay->color_table = g_malloc_n (overlay->color_table_size,
        sizeof (guint32));
    inc = 360.0 / overlay->color_table_size;
    for (gsize d = 0; d < overlay->color_table_size; d++) {
      gst_segmentation_overlay_hue_to_rgb (&overlay->color_table[d], d * inc);
    }
  }
}

static gboolean
gst_segmentation_overlay_start (GstBaseTransform * trans)
{
  GstSegmentationOverlay *overlay = GST_SEGMENTATION_OVERLAY (trans);
  overlay->active = TRUE;
  return TRUE;
}

static gboolean
gst_segmentation_overlay_stop (GstBaseTransform * trans)
{
  GstSegmentationOverlay *overlay = GST_SEGMENTATION_OVERLAY (trans);
  gst_clear_buffer (&overlay->canvas);
  g_free (overlay->color_table);
  overlay->color_table = NULL;
  g_free (overlay->mask_filter);
  overlay->mask_filter = NULL;
  overlay->active = FALSE;
  return TRUE;
}

static GstFlowReturn
gst_segmentation_overlay_transform_frame_ip (GstVideoFilter * filter,
    GstVideoFrame * frame)
{
  GstSegmentationOverlay *overlay = GST_SEGMENTATION_OVERLAY (filter);
  GstVideoOverlayCompositionMeta *composition_meta;
  gpointer state = NULL, related_state;
  GstVideoOverlayRectangle *rectangle = NULL;
  GstAnalyticsMtd rlt_seg_mtd, rlt_cls_mtd;
  GstAnalyticsMtd *clsmtd = NULL;
  GstAnalyticsSegmentationMtd *seg_mtd;
  gint ofx = 0, ofy = 0;
  guint canvas_w = 0, canvas_h = 0;
  GstMapInfo cmap;
  GstVideoMeta *cvmeta;
  GstAnalyticsRelationMeta *rmeta;
  GstAnalyticsMtdType rlt_type;
  GstBuffer *canvas, *mask;
  GstVideoInfo canvas_info;

  composition_meta =
      gst_buffer_get_video_overlay_composition_meta (frame->buffer);
  if (composition_meta) {
    if (overlay->upstream_composition != composition_meta->overlay) {
      GST_DEBUG_OBJECT (overlay, "GstVideoOverlayCompositionMeta found.");
      overlay->upstream_composition = composition_meta->overlay;
    }
  } else if (overlay->upstream_composition != NULL) {
    overlay->upstream_composition = NULL;
  }


  /* Retrieve relation-meta attached to this buffer */
  rmeta = (GstAnalyticsRelationMeta *)
      gst_buffer_get_meta (GST_BUFFER (frame->buffer),
      GST_ANALYTICS_RELATION_META_API_TYPE);

  if (rmeta) {
    if (overlay->composition)
      gst_video_overlay_composition_unref (overlay->composition);

    if (overlay->upstream_composition) {
      overlay->composition =
          gst_video_overlay_composition_copy (overlay->upstream_composition);
    } else {
      overlay->composition = gst_video_overlay_composition_new (NULL);
    }

    /* Get the quark representing segmentation metadata type */
    rlt_type = gst_analytics_segmentation_mtd_get_mtd_type ();

    /* Iterate overa all relatable-mtd of type segmentation attached to
     * rmeta.
     */
    while (gst_analytics_relation_meta_iterate (rmeta, &state, rlt_type,
            &rlt_seg_mtd)) {

      GST_DEBUG_OBJECT (filter, "buffer contain seg mtd");
      seg_mtd = (GstAnalyticsSegmentationMtd *) & rlt_seg_mtd;

      /* Retrieve classification mtd associated to segmentation-mtd. If
       * present the classificaiton-mtd allow to retrieve a label associated to
       * segment id. */
      related_state = NULL;
      if (gst_analytics_relation_meta_get_direct_related (rmeta, rlt_seg_mtd.id,
              GST_ANALYTICS_REL_TYPE_N_TO_N,
              gst_analytics_cls_mtd_get_mtd_type (), &related_state,
              &rlt_cls_mtd)) {
        clsmtd = &rlt_cls_mtd;
      }

      if ((mask = gst_analytics_segmentation_mtd_get_mask (seg_mtd, &ofx,
                  &ofy, &canvas_w, &canvas_h)) != NULL) {

        ofx = CLAMP (ofx, 0, GST_VIDEO_INFO_WIDTH (&filter->in_info));
        ofy = CLAMP (ofy, 0, GST_VIDEO_INFO_HEIGHT (&filter->in_info));
        canvas_w =
            MIN (canvas_w, GST_VIDEO_INFO_WIDTH (&filter->in_info) - ofx);
        canvas_h =
            MIN (canvas_h, GST_VIDEO_INFO_HEIGHT (&filter->in_info) - ofy);
      } else {
        GST_TRACE_OBJECT (filter, "Received a segmentation mtd without mask");
        continue;
      }

      /* Calculate canvas size required */
      gst_video_info_set_format (&canvas_info,
          GST_VIDEO_OVERLAY_COMPOSITION_FORMAT_RGB, canvas_w, canvas_h);
      /* Allocate buffer to store canvas */
      canvas = gst_buffer_new_and_alloc (canvas_info.size);
      cvmeta = gst_buffer_add_video_meta (canvas, GST_VIDEO_FRAME_FLAG_NONE,
          GST_VIDEO_OVERLAY_COMPOSITION_FORMAT_RGB, canvas_w, canvas_h);

      /* Keep an handle on canvas to free it if required */
      gst_buffer_replace (&overlay->canvas, canvas);
      gst_buffer_unref (canvas);

      gst_buffer_map (canvas, &cmap, GST_MAP_READWRITE);

      /* Fill canvas with segmentation mask */
      gst_segmentation_overlay_fill_canvas (overlay, &cmap, cvmeta, mask,
          clsmtd);
      gst_buffer_unmap (canvas, &cmap);

      /* Specify where the canvas need to be overlaid */
      rectangle = gst_video_overlay_rectangle_new_raw (overlay->canvas,
          ofx, ofy, canvas_w, canvas_h, GST_VIDEO_OVERLAY_FORMAT_FLAG_NONE);

      /* Add rectangle to composition */
      gst_video_overlay_composition_add_rectangle (overlay->composition,
          rectangle);

      gst_video_overlay_rectangle_unref (rectangle);
    }
  }

  if (overlay->composition) {
    GST_DEBUG_OBJECT (filter, "have composition");

    if (overlay->attach_compo_to_buffer) {
      GST_DEBUG_OBJECT (filter, "attach");
      gst_buffer_add_video_overlay_composition_meta (frame->buffer,
          overlay->composition);
    } else {
      gst_video_overlay_composition_blend (overlay->composition, frame);
    }
  }

  return GST_FLOW_OK;
}

/*
 * gst_segmentation_overlay_hue_to_rgb:
 * @rgb: Fill rgb values corresponding to #hue.
 * @hue: hue value from HSV colorspace
 * Covert #hue from HSV colorspace to rgb values in RGB colorspace
 */
static void
gst_segmentation_overlay_hue_to_rgb (guint32 * rgb, double hue)
{
  hue = fmod (hue, 360.0);
  guint32 x =
      (guint32) round ((1.0 - fabs (fmod (hue / 60.0, 2.0) - 1.0)) * 255.0);

  if (hue >= 0 && hue < 60) {
    *rgb = 255 << 16 | x << 8;
  } else if (hue >= 60 && hue < 120) {
    *rgb = x << 16 | 255 << 8;
  } else if (hue >= 120 && hue < 180) {
    *rgb = 255 << 8 | x;
  } else if (hue >= 180 && hue < 240) {
    *rgb = x << 8 | 255;
  } else if (hue >= 240 && hue < 300) {
    *rgb = x << 16 | 255;
  } else if (hue >= 300 && hue < 360) {
    *rgb = 255 << 16 | x;
  }
}

static void
gst_segmentation_overlay_update_mask_filter (GstSegmentationOverlay * overlay,
    GstAnalyticsClsMtd * cls_mtd)
{
  GQuark seg_type;

  g_return_if_fail (cls_mtd != NULL);

  /* If not segment type filter is set, all mask are shown */
  if (overlay->selected_type_filter != NULL) {
    gsize length = gst_analytics_cls_mtd_get_length (cls_mtd);
    if (overlay->mask_filter == NULL || overlay->mask_filter_len != length ||
        overlay->update_mask_filter == TRUE) {
      overlay->mask_filter = g_realloc (overlay->mask_filter, length *
          sizeof (gboolean));
      overlay->mask_filter_len = length;
      for (gsize i = 0; i < length; i++) {
        seg_type = gst_analytics_cls_mtd_get_quark (cls_mtd, i);
        overlay->mask_filter[i] = g_slist_find (overlay->selected_type_filter,
            GUINT_TO_POINTER (seg_type)) != NULL;
      }
    }
    overlay->update_mask_filter = FALSE;
  }
}

static void
gst_segmentation_overlay_resampling (GstSegmentationOverlay * overlay,
    gint32 * canvas_data, guint8 * mask_data, GstVideoMeta * cvmeta,
    GstVideoMeta * mvmeta)
{
  gsize mask_col_idx, mask_line_idx, last_mask_line_idx = -1;
  gint32 *cline = canvas_data, *pcline = NULL;
  guint8 *mline = mask_data;
  gsize color_count = overlay->color_table_size + 1;
  guint32 *color_table = overlay->color_table;
  gboolean *mask_filter = overlay->mask_filter;

#define CTBL_IDX(val) (mline[val] % color_count)
#define MASK_FILTER(val) (mask_filter == NULL || mask_filter [mline [val]])

  for (gint cl = 0; cl < cvmeta->height; cl++) {
    mask_line_idx = (cl * mvmeta->height) / cvmeta->height;
    if (last_mask_line_idx != mask_line_idx) {
      mask_col_idx = 0;
      for (gint cc = 0; cc < cvmeta->width; cc++) {
        mask_col_idx = (cc * mvmeta->width) / cvmeta->width;
        if (CTBL_IDX (mask_col_idx) != 0 && MASK_FILTER (mask_col_idx)) {
          cline[cc] = 0x80000000 | color_table[CTBL_IDX (mask_col_idx) - 1];
        } else {
          cline[cc] = overlay->bg_color;
        }
      }
    } else {
      /* If current line would be generate from the same line from the mask
       * as the previous line in canvas we can simply copy the previous
       * line into the current line */
      memcpy (cline, pcline, sizeof (guint32) * cvmeta->width);
    }
    last_mask_line_idx = mask_line_idx;
    pcline = cline;
    cline += cvmeta->width;
    mline = (mask_line_idx * mvmeta->width) + mask_data;
  }
}

static void
gst_segmentation_overlay_fill_canvas (GstSegmentationOverlay * overlay,
    GstMapInfo * cmap, GstVideoMeta * cvmeta, GstBuffer * mask,
    GstAnalyticsClsMtd * cls_mtd)
{
  GstVideoMeta *mvmeta;
  GstMapInfo mmap;

  /* Retrieve video-meta describing the mask */
  mvmeta = gst_buffer_get_video_meta (mask);
  if (mvmeta != NULL) {
    if (cls_mtd != NULL)
      gst_segmentation_overlay_update_mask_filter (overlay, cls_mtd);

    gst_buffer_map (mask, &mmap, GST_MAP_READ);
    gst_segmentation_overlay_resampling (overlay,
        (gint32 *) cmap->data, mmap.data, cvmeta, mvmeta);
    gst_buffer_unmap (mask, &mmap);
  }
  gst_buffer_unref (mask);
}
