/*
 * GStreamer
 * Copyright (C) 2010 Texas Instruments, Inc
 * Copyright (C) 2010 Thiago Santos <thiago.sousa.santos@collabora.co.uk>
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


#ifndef __GST_WRAPPER_CAMERA_BIN_SRC_H__
#define __GST_WRAPPER_CAMERA_BIN_SRC_H__

#include <gst/gst.h>
#include <gst/basecamerabinsrc/gstbasecamerasrc.h>
#include <gst/basecamerabinsrc/gstcamerabinpreview.h>
#include "camerabingeneral.h"

G_BEGIN_DECLS
#define GST_TYPE_WRAPPER_CAMERA_BIN_SRC \
  (gst_wrapper_camera_bin_src_get_type())
#define GST_WRAPPER_CAMERA_BIN_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_WRAPPER_CAMERA_BIN_SRC,GstWrapperCameraBinSrc))
#define GST_WRAPPER_CAMERA_BIN_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_WRAPPER_CAMERA_BIN_SRC,GstWrapperCameraBinSrcClass))
#define GST_IS_WRAPPER_CAMERA_BIN_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_WRAPPER_CAMERA_BIN_SRC))
#define GST_IS_WRAPPER_CAMERA_BIN_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_WRAPPER_CAMERA_BIN_SRC))
    GType gst_wrapper_camera_bin_src_get_type (void);

typedef struct _GstWrapperCameraBinSrc GstWrapperCameraBinSrc;
typedef struct _GstWrapperCameraBinSrcClass GstWrapperCameraBinSrcClass;

enum GstVideoRecordingStatus {
  GST_VIDEO_RECORDING_STATUS_DONE,
  GST_VIDEO_RECORDING_STATUS_STARTING,
  GST_VIDEO_RECORDING_STATUS_RUNNING,
  GST_VIDEO_RECORDING_STATUS_FINISHING
};


/**
 * GstWrapperCameraBinSrc:
 *
 */
struct _GstWrapperCameraBinSrc
{
  GstBaseCameraSrc parent;

  GstCameraBinMode mode;

  GstPad *vfsrc;
  GstPad *imgsrc;
  GstPad *vidsrc;

  /* video recording controls */
  gint video_rec_status;

  /* image capture controls */
  gint image_capture_count;

  /* source elements */
  GstElement *src_vid_src;
  GstElement *video_filter;
  GstElement *src_filter;
  GstElement *src_zoom_crop;
  GstElement *src_zoom_scale;
  GstElement *src_zoom_filter;
  GstElement *output_selector;

  gboolean elements_created;

  gulong src_event_probe_id;
  gulong src_max_zoom_signal_id;

  GstPad *outsel_imgpad;
  GstPad *outsel_vidpad;

  /* For changing caps without losing timestamps */
  gboolean drop_newseg;

  /* Application configurable elements */
  GstElement *app_vid_src;
  GstElement *app_vid_filter;

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
  gboolean image_renegotiate;
  gboolean video_renegotiate;
};


/**
 * GstWrapperCameraBinSrcClass:
 *
 */
struct _GstWrapperCameraBinSrcClass
{
  GstBaseCameraSrcClass parent;
};

gboolean gst_wrapper_camera_bin_src_plugin_init (GstPlugin * plugin);

#endif /* __GST_WRAPPER_CAMERA_BIN_SRC_H__ */
