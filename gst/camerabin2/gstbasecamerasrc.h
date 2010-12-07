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


#ifndef __GST_BASE_CAMERA_SRC_H__
#define __GST_BASE_CAMERA_SRC_H__

#include <gst/gst.h>
#include <gst/gstbin.h>
#include <gst/interfaces/photography.h>
#include <gst/interfaces/colorbalance.h>
#include "gstcamerabin-enum.h"

G_BEGIN_DECLS
#define GST_TYPE_BASE_CAMERA_SRC \
  (gst_base_camera_src_get_type())
#define GST_BASE_CAMERA_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_BASE_CAMERA_SRC,GstBaseCameraSrc))
#define GST_BASE_CAMERA_SRC_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_BASE_CAMERA_SRC, GstBaseCameraSrcClass))
#define GST_BASE_CAMERA_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_BASE_CAMERA_SRC,GstBaseCameraSrcClass))
#define GST_IS_BASE_CAMERA_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_BASE_CAMERA_SRC))
#define GST_IS_BASE_CAMERA_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_BASE_CAMERA_SRC))
#define GST_BASE_CAMERA_SRC_CAST(obj) \
  ((GstBaseCameraSrc *) (obj))
GType gst_base_camera_src_get_type (void);

typedef struct _GstBaseCameraSrc GstBaseCameraSrc;
typedef struct _GstBaseCameraSrcClass GstBaseCameraSrcClass;


/**
 * GstBaseCameraSrc:
 */
struct _GstBaseCameraSrc
{
  GstBin parent;

  GstPad *vfsrc;
  GstPad *imgsrc;
  GstPad *vidsrc;

  gint mode;

  gboolean capturing;
  GMutex *capturing_mutex;

  /* XXX preview pads? */

  /* Resolution of the buffers configured to camerabin */
  gint width;
  gint height;

  /* The digital zoom (from 100% to 1000%) */
  gint zoom;

  /* Image capture resolution */
  gint image_capture_width;
  gint image_capture_height;

  /* Frames per second configured to camerabin */
  gint fps_n;
  gint fps_d;

  /* Night mode handling */
  gboolean night_mode;
  gint pre_night_fps_n;
  gint pre_night_fps_d;

  gpointer _gst_reserved[GST_PADDING_LARGE];
};


/**
 * GstBaseCameraSrcClass:
 * @construct_pipeline: construct pipeline must be implemented by derived
 *    class, and return by reference vfsrc, imgsrc, and vidsrc pads of the
 *    contained pipeline, which will be ghosted to the src pads of the
 *    camerasrc bin (and optionally the preview src pads?)
 * @setup_pipeline:
 * @set_zoom: set the zoom
 * @set_mode: set the mode
 */
struct _GstBaseCameraSrcClass
{
  GstBinClass parent;

  /* construct pipeline must be implemented by derived class, and return by
   * reference vfsrc, imgsrc, and vidsrc pads of the contained pipeline, which
   * will be ghosted to the src pads of the camerasrc bin (and optionally the
   * preview src pads?) */
  gboolean    (*construct_pipeline)  (GstBaseCameraSrc *self,
                                      GstPad **vfsrc, GstPad **imgsrc,
                                      GstPad **vidsrc);

  /* optional */
  gboolean    (*setup_pipeline)      (GstBaseCameraSrc *self);

  /* set the zoom */
  void        (*set_zoom)            (GstBaseCameraSrc *self, gint zoom);

  /* set the mode */
  gboolean    (*set_mode)            (GstBaseCameraSrc *self,
                                      GstCameraBinMode mode);

  /* */
  GstCaps *   (*get_allowed_input_caps) (GstBaseCameraSrc * self);

  void (*private_start_capture) (GstBaseCameraSrc * src);
  void (*private_stop_capture) (GstBaseCameraSrc * src);
  gboolean (*start_capture) (GstBaseCameraSrc * src);
  void (*stop_capture) (GstBaseCameraSrc * src);


  gpointer _gst_reserved[GST_PADDING_LARGE];
};


#define MIN_ZOOM 100
#define MAX_ZOOM 1000
#define ZOOM_1X MIN_ZOOM

GstPhotography * gst_base_camera_src_get_photography (GstBaseCameraSrc *self);
GstColorBalance * gst_base_camera_src_get_color_balance (GstBaseCameraSrc *self);

gboolean gst_base_camera_src_set_mode (GstBaseCameraSrc *self, GstCameraBinMode mode);
void gst_base_camera_src_setup_zoom (GstBaseCameraSrc * self);
GstCaps * gst_base_camera_src_get_allowed_input_caps (GstBaseCameraSrc * self);
const GValue * gst_base_camera_src_find_better_framerate (
    GstBaseCameraSrc * self, GstStructure * st, const GValue * orig_framerate);

void gst_base_camera_src_finish_capture (GstBaseCameraSrc *self);


// XXX add methods to get/set img capture and vid capture caps..

#endif /* __GST_BASE_CAMERA_SRC_H__ */
