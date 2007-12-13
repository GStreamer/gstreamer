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

#include "metadatatags.h"

/*
 * EXIF tags
 */

static void
metadata_tags_exif_register (void)
{
  gst_tag_register (GST_TAG_EXIF, GST_TAG_FLAG_META,
      GST_TYPE_BUFFER, GST_TAG_EXIF, "exif metadata chunk", NULL);
}

/*
 * IPTC tags
 */

static void
metadata_tags_iptc_register (void)
{
  gst_tag_register (GST_TAG_IPTC, GST_TAG_FLAG_META,
      GST_TYPE_BUFFER, GST_TAG_IPTC, "iptc metadata chunk", NULL);
}

/*
 * XMP tags
 */

static void
metadata_tags_xmp_register (void)
{
  gst_tag_register (GST_TAG_XMP, GST_TAG_FLAG_META,
      GST_TYPE_BUFFER, GST_TAG_XMP, "xmp metadata chunk", NULL);
}

/*
 *
 */

void
metadata_tags_register (void)
{
  /* devices tags */

  gst_tag_register (GST_TAG_DEVICE_MAKE, GST_TAG_FLAG_META,
      G_TYPE_STRING, GST_TAG_DEVICE_MAKE,
      "The manufacturer of the recording equipment", NULL);
  gst_tag_register (GST_TAG_DEVICE_MODEL, GST_TAG_FLAG_META, G_TYPE_STRING,
      GST_TAG_DEVICE_MODEL, "The model name or model number of the equipment",
      NULL);

  /* generic tags */

  gst_tag_register (GST_TAG_CREATOR_TOOL, GST_TAG_FLAG_META, G_TYPE_STRING,
      GST_TAG_CREATOR_TOOL,
      "The name of the first known tool used to create the resource."
      " Or firmware or driver version of device", NULL);

  /* image tags */

  gst_tag_register (GST_TAG_IMAGE_XRESOLUTION, GST_TAG_FLAG_META, G_TYPE_FLOAT,
      GST_TAG_IMAGE_XRESOLUTION, "Horizontal resolution in pixels per inch",
      NULL);
  gst_tag_register (GST_TAG_IMAGE_YRESOLUTION, GST_TAG_FLAG_META, G_TYPE_FLOAT,
      GST_TAG_IMAGE_YRESOLUTION, "Vertical resolution in pixels per inch",
      NULL);

  /* capture tags */

  gst_tag_register (GST_TAG_CAPTURE_EXPOSURE_TIME, GST_TAG_FLAG_META,
      G_TYPE_FLOAT, GST_TAG_CAPTURE_EXPOSURE_TIME, "Exposure time in seconds",
      NULL);
  gst_tag_register (GST_TAG_CAPTURE_FNUMBER, GST_TAG_FLAG_META, G_TYPE_FLOAT,
      GST_TAG_CAPTURE_FNUMBER, "F number (focal ratio)", NULL);
  /**
    0 - not defined
    1- Manual
    2- Normal program
    3- Aperture priority
    4- Shutter priority
    5- Creative program (biased toward death of field)
    6- Action program (biased toward fast shutter speed)
    7- Portrait mode (for closeup photos with the background out of focus)
    8- Landscape mode (for landscape photos with the background in focus)
    *** exif is until here ***
    9- Night
    10- Back-light
    11- Spotlight
    12- Snow
    13- Beach
  */
  gst_tag_register (GST_TAG_CAPTURE_EXPOSURE_PROGRAM, GST_TAG_FLAG_META,
      G_TYPE_UINT, GST_TAG_CAPTURE_EXPOSURE_PROGRAM,
      "Class of program used for exposure", NULL);
  /** The unit is the APEX value.
      Ordinarily it is given in the range of -99.99 to 99.99.
      100.0 mean unknown      
  */
  gst_tag_register (GST_TAG_CAPTURE_BRIGHTNESS, GST_TAG_FLAG_META, G_TYPE_FLOAT,
      GST_TAG_CAPTURE_BRIGHTNESS, "Brightness (APEX from -99.99 to 99.99)",
      NULL);
  /**
     0- Auto
     1- Off
     *** exif is until here ***
     2- Sunlight
     3- Cloudy
     4- Shade
     5- Tungsten
     6- Fluorescent
     7- Incandescent
     8- Flash
     9- Horizon (sun on the horizon)
  */
  gst_tag_register (GST_TAG_CAPTURE_WHITE_BALANCE, GST_TAG_FLAG_META,
      G_TYPE_UINT, GST_TAG_CAPTURE_WHITE_BALANCE, "White balance mode", NULL);
  /** if Zero ZOOM not used
   */
  gst_tag_register (GST_TAG_CAPTURE_DIGITAL_ZOOM, GST_TAG_FLAG_META,
      G_TYPE_FLOAT, GST_TAG_CAPTURE_DIGITAL_ZOOM, "Digital zoom ratio", NULL);
  /**
     0- None
     1- Low gain up
     2- High gain up
     3- Low gain down
     4- High gain down
  */
  gst_tag_register (GST_TAG_CAPTURE_GAIN, GST_TAG_FLAG_META, G_TYPE_UINT,
      GST_TAG_CAPTURE_GAIN, "", NULL);
  /**
     from -100 to 100
     [-100, -34] - soft
     [-33, 33] - normal
     [34, 100] - hard
     *** exif is just 0, 1, 2 (normal, soft and hard)
  */
  gst_tag_register (GST_TAG_CAPTURE_CONTRAST, GST_TAG_FLAG_META, G_TYPE_INT,
      GST_TAG_CAPTURE_CONTRAST, "", NULL);
  /**
     from -100 to 100
     [-100, -34] - low
     [-33, 33] - normal
     [34, 100] - high
     *** exif is just 0, 1, 2 (normal, low and high)
  */
  gst_tag_register (GST_TAG_CAPTURE_SATURATION, GST_TAG_FLAG_META, G_TYPE_INT,
      GST_TAG_CAPTURE_SATURATION, "", NULL);

  metadata_tags_exif_register ();
  metadata_tags_iptc_register ();
  metadata_tags_xmp_register ();

}
