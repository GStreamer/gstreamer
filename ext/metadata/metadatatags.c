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

/*
 * SECTION: metadatatags
 * @short_description: This module contains has tag definitions to be mapped
 * to EXIF, IPTC and XMP tags.
 *
 * This module register tags need for image metadata but aren't already define
 * in GStreamer base. So, the EXIF, IPTC and XMP tags can be mapped to tags
 * not registered in this file (tags already in GST base)
 *
 * When changing this file, update 'metadata_mapping.htm' file too.
 *
 * Last reviewed on 2008-01-24 (0.10.15)
 */

/*
 * includes
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif
#include "metadatatags.h"

/*
 * static helper functions declaration
 */

static void metadata_tags_exif_register (void);

static void metadata_tags_iptc_register (void);

static void metadata_tags_xmp_register (void);

/*
 * extern functions implementations
 */

void
metadata_tags_register (void)
{

  /* whole chunk tags */

  gst_tag_register (GST_TAG_EXIF, GST_TAG_FLAG_META,
      GST_TYPE_BUFFER, GST_TAG_EXIF, "exif metadata chunk", NULL);

  gst_tag_register (GST_TAG_IPTC, GST_TAG_FLAG_META,
      GST_TYPE_BUFFER, GST_TAG_IPTC, "iptc metadata chunk", NULL);

  gst_tag_register (GST_TAG_XMP, GST_TAG_FLAG_META,
      GST_TYPE_BUFFER, GST_TAG_XMP, "xmp metadata chunk", NULL);

  /* tags related to some metadata */

  metadata_tags_exif_register ();
  metadata_tags_iptc_register ();
  metadata_tags_xmp_register ();

}


/*
 * static helper functions implementation
 */


/*
 * EXIF tags
 */

static void
metadata_tags_exif_register (void)
{

  /* capture tags */

  gst_tag_register (GST_TAG_CAPTURE_APERTURE, GST_TAG_FLAG_META,
      GST_TYPE_FRACTION, GST_TAG_CAPTURE_APERTURE,
      "Aperture (in APEX units)", NULL);

  /* The unit is the APEX value.
     Ordinarily it is given in the range of -99.99 to 99.99.
     if numerator is 0xFFFFFFFF means unknown      
   */
  gst_tag_register (GST_TAG_CAPTURE_BRIGHTNESS, GST_TAG_FLAG_META,
      GST_TYPE_FRACTION, GST_TAG_CAPTURE_BRIGHTNESS,
      "Brightness (APEX from -99.99 to 99.99)", NULL);

  /*
   * 1- sRGB
   * 0xFFFF - Uncalibrated
   */

  gst_tag_register (GST_TAG_CAPTURE_COLOR_SPACE, GST_TAG_FLAG_META,
      G_TYPE_UINT, GST_TAG_CAPTURE_COLOR_SPACE, "Color Space", NULL);

  /*
     from -100 to 100
     [-100, -34] - soft
     [-33, 33] - normal
     [34, 100] - hard
     *** exif is just 0, 1, 2 (normal, soft and hard)
   */
  gst_tag_register (GST_TAG_CAPTURE_CONTRAST, GST_TAG_FLAG_META, G_TYPE_INT,
      GST_TAG_CAPTURE_CONTRAST, "Contrast", NULL);

  /*
   * 0- Normal process
   * 1- Custom process
   */

  gst_tag_register (GST_TAG_CAPTURE_CUSTOM_RENDERED, GST_TAG_FLAG_META,
      G_TYPE_UINT, GST_TAG_CAPTURE_CUSTOM_RENDERED,
      "Indicates the use of special processing on image data", NULL);

  /* if Zero ZOOM not used
   */
  gst_tag_register (GST_TAG_CAPTURE_DIGITAL_ZOOM, GST_TAG_FLAG_META,
      GST_TYPE_FRACTION, GST_TAG_CAPTURE_DIGITAL_ZOOM, "Digital zoom ratio",
      NULL);

  /*
   * 0 - Auto exposure
   * 1 - Manual exposure
   * 2 - Auto bracket (the camera shoots a series of frames of the same scene
   *     at different exposure settings)
   */

  gst_tag_register (GST_TAG_CAPTURE_EXPOSURE_MODE, GST_TAG_FLAG_META,
      G_TYPE_UINT, GST_TAG_CAPTURE_EXPOSURE_MODE, "Exposure Mode", NULL);

  /*
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

  gst_tag_register (GST_TAG_CAPTURE_EXPOSURE_TIME, GST_TAG_FLAG_META,
      GST_TYPE_FRACTION, GST_TAG_CAPTURE_EXPOSURE_TIME,
      "Exposure time in seconds", NULL);

  /*
   * bits (76543210) indicating the flash status:
   * 0- Flash firing
   *   0- Flash did not fire
   *   1- Flash fired
   * 1,2- Flash return
   *   00- No strobe return detection function
   *   01- reserved
   *   10- Strobe return light not detected
   *   11- Strobe return light detected.
   * 3,4- Flash mode
   *   00- unknown
   *   01- Compulsory flash firing
   *   10- Compulsory flash suppression
   *   11- Auto mode
   * 5- if flash function is present
   *   0- Flash function present
   *   1- No flash function
   * 6- Red-eye mode
   *   0- No red-eye reduction mode or unknown
   *   1- Red-eye reduction supported
   * So, we have the following possible values:
   *
   * 0000.H = Flash did not fire.
   * 0001.H = Flash fired.
   * 0005.H = Strobe return light not detected.
   * 0007.H = Strobe return light detected.
   * 0009.H = Flash fired, compulsory flash mode
   * 000D.H = Flash fired, compulsory flash mode, return light not detected
   * 000F.H = Flash fired, compulsory flash mode, return light detected
   * 0010.H = Flash did not fire, compulsory flash mode
   * 0018.H = Flash did not fire, auto mode
   * 0019.H = Flash fired, auto mode
   * 001D.H = Flash fired, auto mode, return light not detected
   * 001F.H = Flash fired, auto mode, return light detected
   * 0020.H = No flash function
   * 0041.H = Flash fired, red-eye reduction mode
   * 0045.H = Flash fired, red-eye reduction mode, return light not detected
   * 0047.H = Flash fired, red-eye reduction mode, return light detected
   * 0049.H = Flash fired, compulsory flash mode, red-eye reduction mode
   * 004D.H = Flash fired, compulsory flash mode, red-eye reduction mode,
   *          return light not detected
   * 004F.H = Flash fired, compulsory flash mode, red-eye reduction mode,
   *          return light detected
   * 0059.H = Flash fired, auto mode, red-eye reduction mode
   * 005D.H = Flash fired, auto mode, return light not detected,
   *          red-eye reduction mode
   * 005F.H = Flash fired, auto mode, return light detected,
   *          red-eye reduction mode
   * Other  = reserved
   */


  gst_tag_register (GST_TAG_CAPTURE_FLASH, GST_TAG_FLAG_META,
      G_TYPE_UINT, GST_TAG_CAPTURE_FLASH, "Flash status", NULL);

  gst_tag_register (GST_TAG_CAPTURE_FNUMBER, GST_TAG_FLAG_META,
      GST_TYPE_FRACTION, GST_TAG_CAPTURE_FNUMBER, "F number (focal ratio)",
      NULL);

  gst_tag_register (GST_TAG_CAPTURE_FOCAL_LEN, GST_TAG_FLAG_META,
      GST_TYPE_FRACTION, GST_TAG_CAPTURE_FOCAL_LEN,
      "Focal length of lens used to take image. Unit is millimeter", NULL);

  /*
     0- None
     1- Low gain up
     2- High gain up
     3- Low gain down
     4- High gain down
   */
  gst_tag_register (GST_TAG_CAPTURE_GAIN, GST_TAG_FLAG_META, G_TYPE_UINT,
      GST_TAG_CAPTURE_GAIN, "", NULL);

  gst_tag_register (GST_TAG_CAPTURE_ISO_SPEED_RATINGS, GST_TAG_FLAG_META,
      G_TYPE_INT, GST_TAG_CAPTURE_ISO_SPEED_RATINGS,
      "ISO Speed and ISO Latitude as specified in ISO 12232", NULL);


  /*
     0-   unknown (default)
     1-   Daylight
     2-   Fluorescent
     3-   Tungsten (incandescent light)
     4-   Flash
     9-   Fine weather
     10-  Cloudy weather
     11-  Shade
     12-  Daylight fluorescent (D 5700 %G–%@ 7100K)
     13-  Day white fluorescent (N 4600 %G–%@ 5400K)
     14-  Cool white fluorescent (W 3900 %G–%@ 4500K)
     15-  White fluorescent (WW 3200 %G–%@ 3700K)
     17-  Standard light A
     18-  Standard light B
     19-  Standard light C
     20-  D55
     21-  D65
     22-  D75
     23-  D50
     24-  ISO studio tungsten
     255- other light source
     Other = reserved
   */

  gst_tag_register (GST_TAG_CAPTURE_LIGHT_SOURCE, GST_TAG_FLAG_META,
      G_TYPE_UINT, GST_TAG_CAPTURE_LIGHT_SOURCE,
      "The kind of light source.", NULL);

  /*
   * The relation of the '0th row' and '0th column' to visual position:
   * 1- top-left
   * 2- top-right
   * 3- bottom-right
   * 4- bottom-left
   * 5- left-top
   * 6- right-top
   * 7- right-bottom
   * 8- left-bottom
   */

  gst_tag_register (GST_TAG_CAPTURE_ORIENTATION, GST_TAG_FLAG_META,
      G_TYPE_UINT, GST_TAG_CAPTURE_ORIENTATION,
      "The orientation of the camera.", NULL);

  /*
     from -100 to 100
     [-100, -34] - low
     [-33, 33] - normal
     [34, 100] - high
     *** exif is just 0, 1, 2 (normal, low and high)
   */
  gst_tag_register (GST_TAG_CAPTURE_SATURATION, GST_TAG_FLAG_META, G_TYPE_INT,
      GST_TAG_CAPTURE_SATURATION, "The saturation", NULL);

  /*
   * 0 - Standard
   * 1 - Landscape
   * 2 - Portrait
   * 3 - Night scene
   */
  gst_tag_register (GST_TAG_CAPTURE_SCENE_CAPTURE_TYPE, GST_TAG_FLAG_META,
      G_TYPE_UINT, GST_TAG_CAPTURE_SCENE_CAPTURE_TYPE, "Scene Type", NULL);

  gst_tag_register (GST_TAG_CAPTURE_SHUTTER_SPEED, GST_TAG_FLAG_META,
      GST_TYPE_FRACTION, GST_TAG_CAPTURE_SHUTTER_SPEED, "Shutter speed (APEX)",
      NULL);

  /*
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

  /* generic tags */

  gst_tag_register (GST_TAG_CREATOR_TOOL, GST_TAG_FLAG_META, G_TYPE_STRING,
      GST_TAG_CREATOR_TOOL,
      "The name of the first known tool used to create the resource."
      " Or firmware or driver version of device", NULL);

  /* date and time tags */
  /* formated as subset of ISO RFC 8601 as described in
   * http://www.w3.org/TR/1998/NOTE-datetime-19980827
   * which is:
   * YYYY
   * YYYY-MM
   * YYYY-MM-DD
   * YYYY-MM-DDThh:mmTZD
   * YYYY-MM-DDThh:mm:ssTZD
   * YYYY-MM-DDThh:mm:ss.sTZD
   * where:
   * YYYY = four-digit year
   * MM = two-digit month (01=January)
   * DD = two-digit day of month (01 through 31)
   * hh = two digits of hour (00 through 23)
   * mm = two digits of minute (00 through 59)
   * ss = two digits of second (00 through 59)
   * s = one or more digits representing a decimal fraction of a second
   * TZD = time zone designator (Z or +hh:mm or -hh:mm)
   */

  gst_tag_register (GST_TAG_DATE_TIME_DIGITIZED, GST_TAG_FLAG_META,
      G_TYPE_STRING, GST_TAG_DATE_TIME_DIGITIZED,
      "Date/Time of image digitized", NULL);

  gst_tag_register (GST_TAG_DATE_TIME_MODIFIED, GST_TAG_FLAG_META,
      G_TYPE_STRING, GST_TAG_DATE_TIME_MODIFIED,
      "Date/Time of image was last modified", NULL);

  gst_tag_register (GST_TAG_DATE_TIME_ORIGINAL, GST_TAG_FLAG_META,
      G_TYPE_STRING, GST_TAG_DATE_TIME_ORIGINAL,
      "Date/Time of original image taken", NULL);

  /* devices tags */

  gst_tag_register (GST_TAG_DEVICE_MAKE, GST_TAG_FLAG_META,
      G_TYPE_STRING, GST_TAG_DEVICE_MAKE,
      "The manufacturer of the recording equipment", NULL);

  gst_tag_register (GST_TAG_DEVICE_MODEL, GST_TAG_FLAG_META, G_TYPE_STRING,
      GST_TAG_DEVICE_MODEL, "The model name or model number of the equipment",
      NULL);

  /* exif specific tags */

  gst_tag_register (GST_TAG_EXIF_MAKER_NOTE, GST_TAG_FLAG_META,
      GST_TYPE_BUFFER, GST_TAG_EXIF_MAKER_NOTE, "Camera private data", NULL);

  /* image tags */

  gst_tag_register (GST_TAG_IMAGE_HEIGHT, GST_TAG_FLAG_META,
      G_TYPE_UINT, GST_TAG_IMAGE_HEIGHT, "Image height in pixels", NULL);

  gst_tag_register (GST_TAG_IMAGE_WIDTH, GST_TAG_FLAG_META,
      G_TYPE_UINT, GST_TAG_IMAGE_WIDTH, "Image width in pixels", NULL);

  gst_tag_register (GST_TAG_IMAGE_XRESOLUTION, GST_TAG_FLAG_META,
      GST_TYPE_FRACTION, GST_TAG_IMAGE_XRESOLUTION,
      "Horizontal resolution in pixels per inch", NULL);

  gst_tag_register (GST_TAG_IMAGE_YRESOLUTION, GST_TAG_FLAG_META,
      GST_TYPE_FRACTION, GST_TAG_IMAGE_YRESOLUTION,
      "Vertical resolution in pixels per inch", NULL);
}

/*
 * IPTC tags
 */

static void
metadata_tags_iptc_register (void)
{

}

/*
 * XMP tags
 */

static void
metadata_tags_xmp_register (void)
{

}
