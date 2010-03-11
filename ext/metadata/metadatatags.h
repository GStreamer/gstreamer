/*
 * GStreamer
 * Copyright 2007 Edgard Lima <edgard.lima@indt.org.br>
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

#ifndef __GST_METADATA_TAGS_H__
#define __GST_METADATA_TAGS_H__

/*
 * includes
 */

#include <gst/gst.h>
#include <gst/base/gstadapter.h>

G_BEGIN_DECLS

/*
 * enum and types
 */

/* set bit to desired mapping */
typedef enum {
  METADATA_TAG_MAP_INDIVIDUALS = 1 << 0,
  METADATA_TAG_MAP_WHOLECHUNK =  1 << 1
} MetadataTagMapping;

/*
 * defines
 */

/* *INDENT-OFF* */

/* whole chunk tags */

#define GST_TAG_EXIF                       "exif"

#define GST_TAG_IPTC                       "iptc"

#define GST_TAG_XMP                        "xmp"

/* individual tags */

#define GST_TAG_CAPTURE_APERTURE           "capture-aperture"
#define GST_TAG_CAPTURE_BRIGHTNESS         "capture-brightness"
#define GST_TAG_CAPTURE_COLOR_SPACE        "capture-color-space"
#define GST_TAG_CAPTURE_CONTRAST           "capture-contrast"
#define GST_TAG_CAPTURE_CUSTOM_RENDERED    "capture-custom-rendered"
#define GST_TAG_CAPTURE_DIGITAL_ZOOM       "capture-digital-zoom"
#define GST_TAG_CAPTURE_EXPOSURE_MODE      "capture-exposure-mode"
#define GST_TAG_CAPTURE_EXPOSURE_PROGRAM   "capture-exposure-program"
#define GST_TAG_CAPTURE_EXPOSURE_TIME      "capture-exposure-time"
#define GST_TAG_CAPTURE_FLASH              "capture-flash"
#define GST_TAG_CAPTURE_FNUMBER            "capture-fnumber"
#define GST_TAG_CAPTURE_FOCAL_LEN          "capture-focal-len"
#define GST_TAG_CAPTURE_GAIN               "capture-gain"
#define GST_TAG_CAPTURE_ISO_SPEED_RATINGS  "capture-iso-speed-ratings"
#define GST_TAG_CAPTURE_LIGHT_SOURCE       "capture-light-source"
#define GST_TAG_CAPTURE_ORIENTATION        "capture-orientation"
#define GST_TAG_CAPTURE_SATURATION         "capture-saturation"
#define GST_TAG_CAPTURE_SCENE_CAPTURE_TYPE "capture-scene-capture-type"
#define GST_TAG_CAPTURE_SHUTTER_SPEED      "capture-shutter-speed"
#define GST_TAG_CAPTURE_WHITE_BALANCE      "capture-white-balance"

#define GST_TAG_CREATOR_TOOL               "creator-tool"

#define GST_TAG_DATE_TIME_DIGITIZED        "date-time-digitized"
#define GST_TAG_DATE_TIME_MODIFIED         "date-time-modified"
#define GST_TAG_DATE_TIME_ORIGINAL         "date-time-original"

#define GST_TAG_DEVICE_MAKE                "device-make"
#define GST_TAG_DEVICE_MODEL               "device-model"

#define GST_TAG_EXIF_MAKER_NOTE            "exif-maker-note"

#define GST_TAG_IMAGE_HEIGHT               "image-height"
#define GST_TAG_IMAGE_WIDTH                "image-width"
#define GST_TAG_IMAGE_XRESOLUTION          "image-xresolution"
#define GST_TAG_IMAGE_YRESOLUTION          "image-yresolution"

#define GST_TAG_GPS_AREA_INFORMATION       ""
#define GST_TAG_GPS_DIFFERENTIAL           ""
#define GST_TAG_GPS_DOP                    ""
#define GST_TAG_GPS_IMAGE_DIRECTION        ""
#define GST_TAG_GPS_MEASURE_MODE           ""
#define GST_TAG_GPS_PROCESSING_METHOD      ""
#define GST_TAG_GPS_SATELLITES             ""
#define GST_TAG_GPS_SPEED                  "" 
#define GST_TAG_GPS_TRACK                  ""

/* *INDENT-ON* */

/*
 * external function prototypes
 */

extern void
metadata_tags_register (void);

G_END_DECLS
#endif /* __GST_METADATA_TAGS_H__ */
