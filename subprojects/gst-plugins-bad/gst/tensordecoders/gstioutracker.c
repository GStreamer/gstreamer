/*
 * GStreamer gstreamer-ioutracker
 * Copyright (C) 2025 Collabora Ltd.
 *  author: Santosh Mahto <santosh.mahto@collabora.com>
 * gstioutracker.c
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
 * SECTION:element-ioutracker
 * @short_description: Simple object tracking based on Intersection-over-Union
 *
 * This element can parse per-buffer object-detection meta and add tracking information.
 *
 * It does an intersection-over-union, so it tracks if two detections share enough area to
 * be likely to be the same thing.
 *
 * Note: This is meant for simplest cases of object tracking and has known limitations.
 * For complex cases, please choose other advance tracking.
 *
 * \[
 * gst-launch-1.0 filesrc location=bouncing.mp4  ! decodebin
 * ! videoconvertscale add-borders=1 ! 'video/x-raw,pixel-aspect-ratio=1/1' \
 * ! onnxinference execution-provider=cpu model-file=./yolov8s.onnx  \
 * ! yolotensordecoder class-confidence-threshold=0.5 \
 * ! ioutracker iou-score-threshold=0.7 \
 * ! videoconvert ! glimagesink
 * ]|
 *
 * Since: 1.28
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstioutracker.h"

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/analytics/analytics.h>
#include <gst/analytics/gstanalytics_image_util.h>

GST_DEBUG_CATEGORY_STATIC (iou_tracker_debug);
#define GST_CAT_DEFAULT iou_tracker_debug
GST_ELEMENT_REGISTER_DEFINE (iou_tracker, "ioutracker",
    GST_RANK_PRIMARY, GST_TYPE_IOU_TRACKER);

/* GstIouTracker properties */
enum
{
  PROP_0,
  PROP_IOU_SCORE_THRESHOLD,
  PROP_MIN_FRAME_COUNT_FOR_LOST_TRACK
};

#define DEFAULT_MIN_FRAME_COUNT_FOR_LOST_TRACK 5        /* randomly chosen */
#define DEFAULT_IOU_SCORE_THRESHOLD  0.5f       /* 0 to 1 */

static GstStaticPadTemplate gst_iou_tracker_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw")
    );

static GstStaticPadTemplate gst_iou_tracker_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw")
    );

typedef struct _BBox
{
  gint x;
  gint y;
  gint w;
  gint h;
} BBox;

typedef struct _TrackData
{
  guint64 id;
  GstClockTime first_seen;      // First time object was seen
  GstClockTime last_seen;       // Last time object was seen
  GstClockTime last_tracked;    // Last time object was tracked
  guint unseen_frame_count;     // Last frame seen
  gboolean lost;                // Whether the object is lost or not
  GQuark obj_type;              // The object type from the object detection
  GQueue bbqueue;               // List of bounding boxes history for the object
} TrackData;

static void gst_iou_tracker_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_iou_tracker_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_iou_tracker_finalize (GObject * object);
static GstFlowReturn gst_iou_tracker_transform_ip (GstBaseTransform *
    trans, GstBuffer * buf);
static gboolean gst_iou_tracker_stop (GstBaseTransform * trans);

G_DEFINE_TYPE (GstIouTracker, gst_iou_tracker, GST_TYPE_BASE_TRANSFORM);

static void
gst_iou_tracker_class_init (GstIouTrackerClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *element_class = (GstElementClass *) klass;
  GstBaseTransformClass *basetransform_class = (GstBaseTransformClass *) klass;

  GST_DEBUG_CATEGORY_INIT (iou_tracker_debug, "ioutracker", 0,
      "Intersection-over-Union tracker");

  gobject_class->set_property = gst_iou_tracker_set_property;
  gobject_class->get_property = gst_iou_tracker_get_property;
  gobject_class->finalize = gst_iou_tracker_finalize;

  /**
   * GstIouTracker:iou-score-threshold
   *
   * The score below which object is considered as different object.
   *
   */
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_IOU_SCORE_THRESHOLD, g_param_spec_float ("iou-score-threshold",
          "IoU Score threshold",
          "Threshold for deciding wether the object is same in different frames",
          0.0, 1.0, DEFAULT_IOU_SCORE_THRESHOLD,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstIouTracker:min-frame-count-for-lost-track
   *
   * Min number of frame where object is not seen, required to mark object as lost.
   *
   */
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_MIN_FRAME_COUNT_FOR_LOST_TRACK,
      g_param_spec_uint ("min-frame-count-for-lost-track",
          "Min consecutive frame count for lost track",
          "Min number of consecutive frames where object is absent before track is considered lost",
          0, G_MAXUINT, DEFAULT_MIN_FRAME_COUNT_FOR_LOST_TRACK,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (element_class, "ioutracker",
      "Analyzer/Video",
      "Track the objects across frames based on Intersection-over-Union (IoU)",
      "Santosh Mahto <santosh.mahto@collabora.com>");
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_iou_tracker_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_iou_tracker_src_template));

  basetransform_class->transform_ip =
      GST_DEBUG_FUNCPTR (gst_iou_tracker_transform_ip);
  basetransform_class->stop = gst_iou_tracker_stop;
}

static void
gst_iou_tracker_track_data_free (TrackData * data)
{
  if (!data)
    return;

  g_queue_clear_full (&data->bbqueue, (GDestroyNotify) g_free);
  g_free (data);
}

static gboolean
gst_iou_tracker_stop (GstBaseTransform * trans)
{
  GstIouTracker *self = GST_IOU_TRACKER (trans);
  g_hash_table_remove_all (self->picked_odmtds);
  g_list_free_full (self->tracks,
      (GDestroyNotify) gst_iou_tracker_track_data_free);
  self->tracks = NULL;

  return TRUE;
}

static void
gst_iou_tracker_init (GstIouTracker * self)
{
  self->min_frame_count_for_lost_track = DEFAULT_MIN_FRAME_COUNT_FOR_LOST_TRACK;
  self->iou_score_threshold = DEFAULT_IOU_SCORE_THRESHOLD;
  self->tracks = NULL;
  self->next_track_id = 0;

  self->picked_odmtds = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, NULL, NULL);

  gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (self), FALSE);
}

static void
gst_iou_tracker_finalize (GObject * object)
{
  GstIouTracker *self = GST_IOU_TRACKER (object);
  g_hash_table_destroy (self->picked_odmtds);
  g_list_free_full (self->tracks,
      (GDestroyNotify) gst_iou_tracker_track_data_free);
  G_OBJECT_CLASS (gst_iou_tracker_parent_class)->finalize (object);
}

static void
gst_iou_tracker_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstIouTracker *self = GST_IOU_TRACKER (object);

  switch (prop_id) {
    case PROP_IOU_SCORE_THRESHOLD:
      self->iou_score_threshold = g_value_get_float (value);
      break;
    case PROP_MIN_FRAME_COUNT_FOR_LOST_TRACK:
      self->min_frame_count_for_lost_track = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_iou_tracker_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstIouTracker *self = GST_IOU_TRACKER (object);

  switch (prop_id) {
    case PROP_IOU_SCORE_THRESHOLD:
      g_value_set_float (value, self->iou_score_threshold);
      break;
    case PROP_MIN_FRAME_COUNT_FOR_LOST_TRACK:
      g_value_set_uint (value, self->min_frame_count_for_lost_track);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gfloat
gst_iou_tracker_get_iou (BBox b1, BBox b2)
{
  return gst_analytics_image_util_iou_float (b1.x, b1.y, b1.w, b1.h,
      b2.x, b2.y, b2.w, b2.h);
}

static GstFlowReturn
gst_iou_tracker_transform_ip (GstBaseTransform * trans, GstBuffer * buf)
{
  GstIouTracker *self = GST_IOU_TRACKER (trans);
  GstAnalyticsRelationMeta *rmeta;
  GstAnalyticsMtd mtd;
  GstClockTime pt = GST_BUFFER_PTS (buf);
  GstClockTime running_time;
  gpointer state = NULL;

  rmeta = gst_buffer_get_analytics_relation_meta (buf);

  if (!rmeta) {
    GST_DEBUG_OBJECT (self, "No GstAnalyticsRelationMeta found in buffer");
    if (self->tracks) {
      // Tracking has started, so add an rmeta to allow adding
      // TrackingMtd.
      rmeta = gst_buffer_add_analytics_relation_meta (buf);
    } else {
      return GST_FLOW_OK;
    }
  }

  g_hash_table_remove_all (self->picked_odmtds);
  running_time =
      gst_segment_to_running_time (&trans->segment, GST_FORMAT_TIME, pt);

  /*
   * Interate over all the existing tracks and update them with new detections.
   * When object is not seen in `min_lost_frame_count_to_remove_track` consecutive frames,
   * the mark it as lost and track are removed, until then track the object and assume
   * predicted position as new position.
   */
  GList *track = self->tracks;
  while (track) {
    TrackData *tdata = (TrackData *) track->data;
    GstAnalyticsODMtd nearest_mtd;
    GstAnalyticsTrackingMtd tmtd;
    gfloat max_iou_score = 0.0f;
    gpointer state = NULL;
    BBox *cbox;

    // Add the older tracking meta to the relation meta to this new buffer
    if (!gst_analytics_relation_meta_add_tracking_mtd (rmeta,
            tdata->id, tdata->first_seen, &tmtd)) {
      GST_DEBUG_OBJECT (self, "Failed to add tracking meta to relation meta");
      continue;
    }

    gst_analytics_tracking_mtd_update_last_seen (&tmtd, tdata->last_seen);
    if (tdata->lost) {
      gst_analytics_tracking_mtd_set_lost (&tmtd);
    }

    cbox = g_queue_peek_head (&tdata->bbqueue);
    // Iterate over od mtds in current frame and find the latest position of
    // tracked object based on IOU score.
    while (gst_analytics_relation_meta_iterate (rmeta, &state,
            gst_analytics_od_mtd_get_mtd_type (), &mtd)) {
      GstAnalyticsODMtd *od_mtd = (GstAnalyticsODMtd *) & mtd;
      BBox odbox;
      gfloat iou_score = -0.0f;

      if (g_hash_table_contains (self->picked_odmtds,
              GINT_TO_POINTER (od_mtd->id)))
        continue;

      /* Different type, ignore it */
      if (tdata->obj_type != gst_analytics_od_mtd_get_obj_type (od_mtd))
        continue;

      gst_analytics_od_mtd_get_location (od_mtd, &odbox.x, &odbox.y,
          &odbox.w, &odbox.h, NULL);

      // Note: IoU based tracking fails when object position doesn't overlap across frame since iou
      // becomes zero. This mostly happens when frame rate are low or object is moving fast.
      // This is known limitation of current implementation.
      iou_score = gst_iou_tracker_get_iou (odbox, *cbox);
      if (iou_score > max_iou_score) {
        max_iou_score = iou_score;
        nearest_mtd = *od_mtd;
      }
    }

    if (max_iou_score >= self->iou_score_threshold) {
      BBox *new_box = g_new0 (BBox, 1);

      gst_analytics_od_mtd_get_location (&nearest_mtd, &new_box->x,
          &new_box->y, &new_box->w, &new_box->h, NULL);

      g_queue_push_head (&tdata->bbqueue, new_box);

      tdata->last_seen = running_time;
      tdata->last_tracked = running_time;
      tdata->unseen_frame_count = 0;
      gst_analytics_tracking_mtd_update_last_seen (&tmtd, running_time);
      gst_analytics_relation_meta_set_relation (rmeta,
          GST_ANALYTICS_REL_TYPE_RELATE_TO, nearest_mtd.id, tmtd.id);

      GST_DEBUG_OBJECT (self,
          "Total track: %u, Track %" G_GUINT64_FORMAT
          " updated with new last seen time: %"
          GST_TIME_FORMAT, g_list_length (self->tracks),
          tdata->id, GST_TIME_ARGS (tdata->last_seen));

      g_hash_table_insert (self->picked_odmtds,
          GINT_TO_POINTER (nearest_mtd.id), (gpointer) TRUE);
    } else {
      tdata->unseen_frame_count++;

      // Remove the track once we have seen enough frame where object was missing.
      if (tdata->unseen_frame_count >= self->min_frame_count_for_lost_track) {
        gst_analytics_tracking_mtd_set_lost (&tmtd);
        GST_DEBUG_OBJECT (self, "Track %" G_GUINT64_FORMAT " marked as lost",
            tdata->id);
        GList *nexttrack = track->next;
        guint64 trackid = tdata->id;    // Logging purpose
        // Remove current track from the list
        // caution: list element is freed within iteration
        gst_iou_tracker_track_data_free (tdata);
        self->tracks = g_list_delete_link (self->tracks, track);
        track = nexttrack;

        GST_DEBUG_OBJECT (self,
            "Track %" G_GUINT64_FORMAT " FORMAT after %u unseen frames",
            trackid, tdata->unseen_frame_count);

        continue;               // start next iteration
      } else {
        //  Since object is not seen in this frame, we need to calulate predicted position
        //  based on previous position change
        guint count = tdata->bbqueue.length;
        BBox *new_box = g_new0 (BBox, 1);

        BBox *cur_box = g_queue_peek_head (&tdata->bbqueue);
        BBox *last_box = g_queue_peek_tail (&tdata->bbqueue);

        new_box->x = cur_box->x + (cur_box->x - last_box->x) / count;
        new_box->y = cur_box->y + (cur_box->y - last_box->y) / count;
        new_box->w = cur_box->w;
        new_box->h = cur_box->h;
        g_queue_push_head (&tdata->bbqueue, new_box);
        tdata->last_tracked = running_time;
        GST_DEBUG_OBJECT (self, "Track %" G_GUINT64_FORMAT
            " not updated, but predicted position is (%d, %d, %d, %d)",
            tdata->id, new_box->x, new_box->y, new_box->w, new_box->h);
      }
    }
    track = track->next;
  }

  // Add new tracks for all the new object found in detection. so for the first time
  // tracks for all the detections are created.
  while (gst_analytics_relation_meta_iterate (rmeta, &state,
          gst_analytics_od_mtd_get_mtd_type (), &mtd)) {
    GstAnalyticsODMtd *od_mtd = (GstAnalyticsODMtd *) & mtd;
    GstAnalyticsTrackingMtd tmtd;

    if (!g_hash_table_contains (self->picked_odmtds,
            GINT_TO_POINTER (od_mtd->id))) {
      // If the mtd is not picked, it means it is not matched with any track
      // and hence it is a new detection
      if (!gst_analytics_relation_meta_add_tracking_mtd (rmeta,
              self->next_track_id, running_time, &tmtd)) {
        GST_ELEMENT_ERROR (self, STREAM, FAILED, (NULL),
            ("Failed to add tracking mtd for new track"));
        return GST_FLOW_ERROR;
      }

      BBox *new_bbox = g_new0 (BBox, 1);
      gst_analytics_od_mtd_get_location (od_mtd, &new_bbox->x, &new_bbox->y,
          &new_bbox->w, &new_bbox->h, NULL);

      TrackData *new_track_data = g_new0 (TrackData, 1);
      g_queue_init (&new_track_data->bbqueue);
      g_queue_push_head (&new_track_data->bbqueue, new_bbox);
      new_track_data->id = self->next_track_id;
      new_track_data->first_seen = running_time;
      new_track_data->last_seen = running_time;
      new_track_data->last_tracked = running_time;
      new_track_data->lost = FALSE;
      new_track_data->unseen_frame_count = 0;
      new_track_data->obj_type = gst_analytics_od_mtd_get_obj_type (od_mtd);
      self->tracks = g_list_append (self->tracks, new_track_data);
      GST_DEBUG_OBJECT (self,
          "New track created with ID: %" G_GUINT64_FORMAT
          ", First Seen: %" GST_TIME_FORMAT,
          new_track_data->id, GST_TIME_ARGS (new_track_data->first_seen));

      self->next_track_id++;

      if (!gst_analytics_relation_meta_set_relation (rmeta,
              GST_ANALYTICS_REL_TYPE_RELATE_TO, od_mtd->id, tmtd.id)) {
        GST_ERROR_OBJECT (self,
            "Failed to set relation for new track Tracking ID: %u and ODM ID: %u",
            self->next_track_id, od_mtd->id);
      }
    }
  }

  // picked_odmtds is used to keep track of the ODMs for single buffer only, so free it.
  g_hash_table_remove_all (self->picked_odmtds);

  return GST_FLOW_OK;
}
