/*
 * GStreamer
 * Copyright (C) 2010 Texas Instruments, Inc
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


#ifndef __GST_V4L2_CAMERA_SRC_H__
#define __GST_V4L2_CAMERA_SRC_H__

#include <gst/gst.h>
#include "gstbasecamerasrc.h"

G_BEGIN_DECLS
#define GST_TYPE_V4L2_CAMERA_SRC \
  (gst_v4l2_camera_src_get_type())
#define GST_V4L2_CAMERA_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_V4L2_CAMERA_SRC,GstV4l2CameraSrc))
#define GST_V4L2_CAMERA_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_V4L2_CAMERA_SRC,GstV4l2CameraSrcClass))
#define GST_IS_V4L2_CAMERA_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_V4L2_CAMERA_SRC))
#define GST_IS_V4L2_CAMERA_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_V4L2_CAMERA_SRC))
    GType gst_v4l2_camera_src_get_type (void);

typedef struct _GstV4l2CameraSrc GstV4l2CameraSrc;
typedef struct _GstV4l2CameraSrcClass GstV4l2CameraSrcClass;

enum GstVideoRecordingStatus {
  GST_VIDEO_RECORDING_STATUS_DONE,
  GST_VIDEO_RECORDING_STATUS_STARTING,
  GST_VIDEO_RECORDING_STATUS_RUNNING,
  GST_VIDEO_RECORDING_STATUS_FINISHING
};


/**
 * GstV4l2CameraSrc:
 *
 */
struct _GstV4l2CameraSrc
{
  GstBaseCameraSrc parent;

  GstCameraBinMode mode;

  gboolean capturing;
  GMutex *capturing_mutex;

  /* video recording controls */
  gint video_rec_status;

  /* image capture controls */
  gint image_capture_count;

  /* source elements */
  GstElement *src_vid_src;
  GstElement *src_filter;
  GstElement *src_zoom_crop;
  GstElement *src_zoom_scale;
  GstElement *src_zoom_filter;

  /* srcpads of tee */
  GstPad *tee_vf_srcpad;
  GstPad *tee_image_srcpad;
  GstPad *tee_video_srcpad;

  /* Application configurable elements */
  GstElement *app_vid_src;
  GstElement *app_video_filter;

  /* Caps that videosrc supports */
  GstCaps *allowed_caps;

  /* Optional base crop for frames. Used to crop frames e.g.
     due to wrong aspect ratio, before the crop related to zooming. */
  gint base_crop_top;
  gint base_crop_bottom;
  gint base_crop_left;
  gint base_crop_right;

  /* Caps applied to capsfilters when in view finder mode */
  GstCaps *view_finder_caps;

  /* Caps applied to capsfilters when taking still image */
  GstCaps *image_capture_caps;
  gboolean image_capture_caps_update; // XXX where does this get set..
};


/**
 * GstV4l2CameraSrcClass:
 *
 */
struct _GstV4l2CameraSrcClass
{
  GstBaseCameraSrcClass parent;

  void (*start_capture) (GstV4l2CameraSrc * src);
  void (*stop_capture) (GstV4l2CameraSrc * src);
};

gboolean gst_v4l2_camera_src_plugin_init (GstPlugin * plugin);

#endif /* __GST_V4L2_CAMERA_SRC_H__ */
