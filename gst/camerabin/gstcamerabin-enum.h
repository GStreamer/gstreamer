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

enum
{
  ARG_0,
  ARG_FILENAME,
  ARG_MODE,
  ARG_FLAGS,
  ARG_MUTE,
  ARG_ZOOM,
  ARG_IMAGE_POST,
  ARG_IMAGE_ENC,
  ARG_IMAGE_FORMATTER,
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
  ARG_VIDEO_CAPTURE_FRAMERATE,
  ARG_PREVIEW_SOURCE_FILTER,
  ARG_READY_FOR_CAPTURE,
  ARG_IDLE
};

/**
 * GstCameraBinFlags:
 * @GST_CAMERABIN_FLAG_SOURCE_RESIZE: enable video crop and scale
 *   after capture
 * @GST_CAMERABIN_FLAG_SOURCE_COLOR_CONVERSION: enable conversion
 *   of native video format by enabling ffmpegcolorspace
 * @GST_CAMERABIN_FLAG_VIEWFINDER_COLOR_CONVERSION: enable color
 *   conversion for viewfinder element
 * @GST_CAMERABIN_FLAG_VIEWFINDER_SCALE: enable scaling in
 *   viewfinder element retaining aspect ratio
 * @GST_CAMERABIN_FLAG_AUDIO_CONVERSION:  enable audioconvert and
 *   audioresample elements
 * @GST_CAMERABIN_FLAG_DISABLE_AUDIO:  disable audio elements
 * @GST_CAMERABIN_FLAG_IMAGE_COLOR_CONVERSION:  enable color
 *   conversion for image output element
 * @GST_CAMERABIN_FLAG_VIDEO_COLOR_CONVERSION:  enable color
 *   conversion for video encoder element
 *
 * Extra flags to configure the behaviour of the sinks.
 */
typedef enum {
  GST_CAMERABIN_FLAG_SOURCE_RESIZE               = (1 << 0),
  GST_CAMERABIN_FLAG_SOURCE_COLOR_CONVERSION     = (1 << 1),
  GST_CAMERABIN_FLAG_VIEWFINDER_COLOR_CONVERSION = (1 << 2),
  GST_CAMERABIN_FLAG_VIEWFINDER_SCALE            = (1 << 3),
  GST_CAMERABIN_FLAG_AUDIO_CONVERSION            = (1 << 4),
  GST_CAMERABIN_FLAG_DISABLE_AUDIO               = (1 << 5),
  GST_CAMERABIN_FLAG_IMAGE_COLOR_CONVERSION      = (1 << 6),
  GST_CAMERABIN_FLAG_VIDEO_COLOR_CONVERSION      = (1 << 7)
} GstCameraBinFlags;

#define GST_TYPE_CAMERABIN_FLAGS (gst_camerabin_flags_get_type())
GType gst_camerabin_flags_get_type (void);

G_END_DECLS

#endif                          /* #ifndef __GST_CAMERABIN_ENUM_H__ */
