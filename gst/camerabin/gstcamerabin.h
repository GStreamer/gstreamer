/*
 * GStreamer
 * Copyright (C) 2008 Nokia Corporation <multimedia@maemo.org>
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

#ifndef __GST_CAMERABIN_H__
#define __GST_CAMERABIN_H__

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gstbin.h>
#include <gst/interfaces/photography.h>

#include "gstcamerabin-enum.h"
#include "camerabinimage.h"
#include "camerabinvideo.h"
#include "camerabinpreview.h"

G_BEGIN_DECLS
/* #defines don't like whitespacey bits */
#define GST_TYPE_CAMERABIN \
  (gst_camerabin_get_type())
#define GST_CAMERABIN(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CAMERABIN,GstCameraBin))
#define GST_CAMERABIN_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CAMERABIN,GstCameraBinClass))
#define GST_IS_CAMERABIN(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CAMERABIN))
#define GST_IS_CAMERABIN_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CAMERABIN))
typedef struct _GstCameraBin GstCameraBin;
typedef struct _GstCameraBinClass GstCameraBinClass;

/**
 * GstCameraBin:
 *
 * The opaque #GstCameraBin structure.
 */

struct _GstCameraBin
{
  GstPipeline parent;

  /* private */
  GString *filename;
  gint mode;                      /* MODE_IMAGE or MODE_VIDEO */
  GstCameraBinFlags flags;
  gboolean stop_requested;        /* TRUE if capturing stop needed */
  gboolean paused;                /* TRUE if capturing paused */

  /*
   * Those 2 booleans work together.
   *
   * 'block_viewfinder_prop' is the property, 'block_viewfinder_trigger'
   * is the flag that actually makes the viewfinder block after capture.
   * We need both to avoid blocking the viewfinder if the application resets
   * the flag after issuing the 'capture-start', but before the actual
   * blocking happens. This causes the viewfinder to block even though
   * the application resetted the flag to keep it running already.
   *
   * Here's how this should work:
   * When a capture is started, the property is checked, if it is TRUE, the
   * trigger is set to TRUE. The blocking will only happen if
   * the trigger is TRUE after image capture finishes, ff the property
   * is reset before the blocking happens, the trigger goes to
   * FALSE and no blocking happens.
   */
  gboolean block_viewfinder_prop; /* TRUE if viewfinder blocks after capture */
  gboolean block_viewfinder_trigger;

  /* Resolution of the buffers configured to camerabin */
  gint width;
  gint height;
  /* Frames per second configured to camerabin */
  gint fps_n;
  gint fps_d;

  /* app configured resolution/framerate */
  gint app_width;
  gint app_height;
  gint app_fps_n;
  gint app_fps_d;

  gboolean video_capture_caps_update;

  /* Image capture resolution */
  gint image_capture_width;
  gint image_capture_height;

  /* Image tags are collected here first before sending to imgbin */
  GstTagList *event_tags;

  /* Caps applied to capsfilters when taking still image */
  GstCaps *image_capture_caps;
  gboolean image_capture_caps_update;

  /* Caps applied to capsfilters when in view finder mode */
  GstCaps *view_finder_caps;

  /* Caps that videosrc supports */
  GstCaps *allowed_caps;

  /* Caps used to create preview image */
  GstCaps *preview_caps;

  /* Caps used to create video preview image */
  GstCaps *video_preview_caps;

  /* The digital zoom (from 1.0 to 10.0) */
  gfloat zoom;

  /* concurrency control */
  GMutex *capture_mutex;
  GCond *cond;
  GCond *idle_cond;
  gboolean capturing;
  gboolean eos_handled;
  /* everytime a new capture is started this is incremented, when it is
   * finished/fails it is decremented. Used to know if camerabin is idle */
  gint processing_counter;

  /* pad names for output and input selectors */
  GstPad *pad_src_view;
  GstPad *pad_view_src;
  GstPad *pad_src_img;
  GstPad *pad_src_vid;
  GstPad *pad_view_vid;
  GstPad *pad_src_queue;

  GstElement *img_queue;        /* queue for decoupling capture from
                                   image-postprocessing and saving */
  GstElement *imgbin;           /* bin that holds image capturing elements */
  GstElement *vidbin;           /*  bin that holds video capturing elements */
  GstElement *active_bin;       /* image or video bin that is currently in use */
  /* pipeline for creating preview images */
  GstCameraBinPreviewPipelineData *preview_pipeline;
  /* pipeline for creating video preview image */
  GstCameraBinPreviewPipelineData *video_preview_pipeline;

  GstBuffer *video_preview_buffer;      /* buffer for storing video preview */

  /* source elements */
  GstElement *src_vid_src;
  GstElement *src_filter;
  GstElement *src_zoom_crop;
  GstElement *src_zoom_scale;
  GstElement *src_zoom_filter;
  GstElement *src_out_sel;

  /* view finder elements */
  GstElement *view_in_sel;
  GstElement *aspect_filter;
  GstElement *view_scale;
  GstElement *view_sink;

  /* Application configurable elements */
  GstElement *app_vid_src;
  GstElement *app_vf_sink;
  GstElement *app_video_filter;
  GstElement *app_viewfinder_filter;
  GstElement *app_preview_source_filter;
  GstElement *app_video_preview_source_filter;

  /* Night mode handling */
  gboolean night_mode;
  gint pre_night_fps_n;
  gint pre_night_fps_d;

  /* Buffer probe id for captured image handling */
  gulong image_captured_id;

  /* Optional base crop for frames. Used to crop frames e.g.
     due to wrong aspect ratio, before the crop related to zooming. */
  gint base_crop_top;
  gint base_crop_bottom;
  gint base_crop_left;
  gint base_crop_right;
};

/**
 * GstCameraBinClass:
 *
 * The #GstCameraBin class structure.
 */
struct _GstCameraBinClass
{
  GstPipelineClass parent_class;

  /* action signals */

  void (*capture_start) (GstCameraBin * camera);
  void (*capture_stop) (GstCameraBin * camera);
  void (*capture_pause) (GstCameraBin * camera);
  void (*set_video_resolution_fps) (GstCameraBin * camera, gint width,
      gint height, gint fps_n, gint fps_d);
  void (*set_image_resolution) (GstCameraBin * camera, gint width, gint height);

  /* signals (callback) */

    gboolean (*img_done) (GstCameraBin * camera, const gchar * filename);
};

/**
 * GstCameraBinMode:
 * @MODE_IMAGE: image capture
 * @MODE_VIDEO: video capture
 *
 * Capture mode to use.
 */
typedef enum
{
  MODE_IMAGE = 0,
  MODE_VIDEO
} GstCameraBinMode;

GType gst_camerabin_get_type (void);

G_END_DECLS
#endif /* #ifndef __GST_CAMERABIN_H__ */
