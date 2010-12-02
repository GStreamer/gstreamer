/*
 * GStreamer
 * Copyright (C) 2009 Nokia Corporation <multimedia@maemo.org>
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

#ifndef __GST_CAMERABIN_ENUM_H__
#define __GST_CAMERABIN_ENUM_H__

#include <gst/gst.h>

G_BEGIN_DECLS

/* XXX find better place for property related enum/defaults */
enum
{
  ARG_0,
  ARG_FILENAME,
  ARG_MODE,
  ARG_READY_FOR_CAPTURE,
  ARG_FLAGS,
  ARG_MUTE,
  ARG_ZOOM,
  ARG_IMAGE_POST,
  ARG_IMAGE_ENC,
  ARG_VIDEO_POST,
  ARG_VIDEO_ENC,
  ARG_AUDIO_ENC,
  ARG_VIDEO_MUX,
  ARG_VF_SINK,
  ARG_VIDEO_SRC,
  ARG_AUDIO_SRC,
  ARG_INPUT_CAPS,
  ARG_FILTER_CAPS,
  ARG_PREVIEW_CAPS,
  ARG_WB_MODE,
  ARG_COLOUR_TONE,
  ARG_SCENE_MODE,
  ARG_FLASH_MODE,
  ARG_FOCUS_STATUS,
  ARG_CAPABILITIES,
  ARG_SHAKE_RISK,
  ARG_EV_COMP,
  ARG_ISO_SPEED,
  ARG_APERTURE,
  ARG_EXPOSURE,
  ARG_VIDEO_SOURCE_FILTER,
  ARG_IMAGE_CAPTURE_SUPPORTED_CAPS,
  ARG_VIEWFINDER_FILTER,
  ARG_FLICKER_MODE,
  ARG_FOCUS_MODE,
  ARG_BLOCK_VIEWFINDER,
  ARG_IMAGE_CAPTURE_WIDTH,
  ARG_IMAGE_CAPTURE_HEIGHT,
  ARG_VIDEO_CAPTURE_WIDTH,
  ARG_VIDEO_CAPTURE_HEIGHT,
  ARG_VIDEO_CAPTURE_FRAMERATE
};

#define DEFAULT_WIDTH 640
#define DEFAULT_HEIGHT 480
#define DEFAULT_CAPTURE_WIDTH 800
#define DEFAULT_CAPTURE_HEIGHT 600
#define DEFAULT_FPS_N 0         /* makes it use the default */
#define DEFAULT_FPS_D 1
#define DEFAULT_ZOOM MIN_ZOOM


/**
 * GstCameraBinMode:
 * @MODE_IMAGE: image capture
 * @MODE_VIDEO: video capture
 *
 * Capture mode to use.
 */
typedef enum
{
  /* MODE_PREVIEW = 0, No use for this */
  MODE_IMAGE = 1,
  MODE_VIDEO = 2,
} GstCameraBinMode;


#define GST_TYPE_CAMERABIN_MODE (gst_camerabin_mode_get_type ())
GType gst_camerabin_mode_get_type (void);

G_END_DECLS

#endif                          /* #ifndef __GST_CAMERABIN_ENUM_H__ */
