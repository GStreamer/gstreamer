/*
 * GStreamer
 * Copyright (C) 2013 Jan Schmidt <jan@centricular.com>
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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

#ifndef __RASPICAPTURE_H__
#define __RASPICAPTURE_H__

#include <glib.h>
#include <inttypes.h>

#include "interface/mmal/mmal_common.h"
#include "interface/mmal/mmal_types.h"
#include "interface/mmal/mmal_parameters_camera.h"
#include "interface/mmal/mmal_component.h"
#include "RaspiCamControl.h"
#include "RaspiPreview.h"

GST_DEBUG_CATEGORY_EXTERN (gst_rpi_cam_src_debug);
#define GST_CAT_DEFAULT gst_rpi_cam_src_debug

#undef fprintf
#define fprintf(f,...) GST_LOG(__VA_ARGS__)
G_BEGIN_DECLS

/** Structure containing all state information for the current run
 */
typedef struct
{
   int verbose; /// !0 if want detailed run information
   
   int timeout;                        /// Time taken before frame is grabbed and app then shuts down. Units are milliseconds
   int width;                          /// Requested width of image
   int height;                         /// requested height of image
   int bitrate;                        /// Requested bitrate
   int fps_n;                      /// Requested frame rate (fps) numerator
   int fps_d;                      /// Requested frame rate (fps) denominator
   int intraperiod;                    /// Intra-refresh period (key frame rate)
   int demoMode;                       /// Run app in demo mode
   int demoInterval;                   /// Interval between camera settings changes
   int immutableInput;                 /// Flag to specify whether encoder works in place or creates a new buffer. Result is preview can display either
                                       /// the camera output or the encoder output (with compression artifacts)
   int profile;                        /// H264 profile to use for encoding
   RASPIPREVIEW_PARAMETERS preview_parameters;   /// Preview setup parameters
   RASPICAM_CAMERA_PARAMETERS camera_parameters; /// Camera setup parameters
} RASPIVID_CONFIG;

typedef struct RASPIVID_STATE_T RASPIVID_STATE;

void raspicapture_init();
void raspicapture_default_config(RASPIVID_CONFIG *config);
RASPIVID_STATE *raspi_capture_setup(RASPIVID_CONFIG *config);
gboolean raspi_capture_start(RASPIVID_STATE *state);
GstFlowReturn raspi_capture_fill_buffer(RASPIVID_STATE *state, GstBuffer **buf);
void raspi_capture_stop(RASPIVID_STATE *state);
void raspi_capture_free(RASPIVID_STATE *state);
gboolean raspi_capture_request_i_frame(RASPIVID_STATE *state);

G_END_DECLS

#endif
