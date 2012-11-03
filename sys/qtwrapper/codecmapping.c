/*
 * GStreamer QuickTime codec mapping
 * Copyright <2006, 2007> Fluendo <gstreamer@fluendo.com>
 * Copyright <2006, 2007> Pioneers of the Inevitable <songbird@songbirdnest.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "qtwrapper.h"
#include "codecmapping.h"
#include "qtutils.h"

static GstCaps *
audio_caps_from_string (const gchar * str)
{
  GstCaps *res;

  res = gst_caps_from_string (str);
  gst_caps_set_simple (res,
      "rate", GST_TYPE_INT_RANGE, 8000, 96000,
      "channels", GST_TYPE_INT_RANGE, 1, 2, NULL);

  return res;
}

GstCaps *
fourcc_to_caps (guint32 fourcc)
{
  GstCaps *caps = NULL;

  GST_DEBUG ("%" GST_FOURCC_FORMAT, GST_FOURCC_ARGS (fourcc));

  switch (fourcc) {
      /* VIDEO */
    case QT_MAKE_FOURCC_LE ('S', 'V', 'Q', '1'):
      caps = gst_caps_new_simple ("video/x-svq",
          "svqversion", G_TYPE_INT, 1, NULL);
      break;
    case QT_MAKE_FOURCC_LE ('S', 'V', 'Q', '3'):
      caps = gst_caps_new_simple ("video/x-svq",
          "svqversion", G_TYPE_INT, 3, NULL);
      break;
    case QT_MAKE_FOURCC_LE ('a', 'v', 'c', '1'):
      caps = gst_caps_from_string ("video/x-h264");
      break;
    case QT_MAKE_FOURCC_LE ('m', 'p', '4', 'v'):
      caps =
          gst_caps_from_string
          ("video/mpeg,mpegversion=4,systemstream=(boolean)false");
      break;
    case QT_MAKE_FOURCC_LE ('m', 'p', 'e', 'g'):
      caps = gst_caps_from_string ("video/mpeg, "
          "systemstream = (boolean) false, " "mpegversion = (int) 1");
      break;
    case QT_MAKE_FOURCC_LE ('h', '2', '6', '3'):
    case QT_MAKE_FOURCC_LE ('H', '2', '6', '3'):
    case QT_MAKE_FOURCC_LE ('s', '2', '6', '3'):
    case QT_MAKE_FOURCC_LE ('U', '2', '6', '3'):
      caps = gst_caps_from_string ("video/x-h263");
      break;
    case QT_MAKE_FOURCC_LE ('c', 'v', 'i', 'd'):
      caps = gst_caps_from_string ("video/x-cinepak");
      break;
    case QT_MAKE_FOURCC_LE ('d', 'v', 'c', 'p'):
    case QT_MAKE_FOURCC_LE ('d', 'v', 'c', ' '):
    case QT_MAKE_FOURCC_LE ('d', 'v', 's', 'd'):
    case QT_MAKE_FOURCC_LE ('D', 'V', 'S', 'D'):
    case QT_MAKE_FOURCC_LE ('d', 'v', 'c', 's'):
    case QT_MAKE_FOURCC_LE ('D', 'V', 'C', 'S'):
    case QT_MAKE_FOURCC_LE ('d', 'v', '2', '5'):
    case QT_MAKE_FOURCC_LE ('d', 'v', 'p', 'p'):
      caps = gst_caps_from_string ("video/x-dv, systemstream=(boolean)false");
      break;

      /* AUDIO */
    case QT_MAKE_FOURCC_LE ('.', 'm', 'p', '3'):
      caps =
          audio_caps_from_string
          ("audio/mpeg,mpegversion=1,layer=3,parsed=(boolean)true");
      break;
    case QT_MAKE_FOURCC_LE ('Q', 'D', 'M', '2'):
      caps = audio_caps_from_string ("audio/x-qdm2");
      break;
    case QT_MAKE_FOURCC_LE ('a', 'g', 's', 'm'):
      caps = audio_caps_from_string ("audio/x-gsm");
      break;
    case QT_MAKE_FOURCC_LE ('a', 'l', 'a', 'c'):
      caps = audio_caps_from_string ("audio/x-alac");
      break;
    case QT_MAKE_FOURCC_LE ('a', 'l', 'a', 'w'):
      caps = audio_caps_from_string ("audio/x-alaw");
      break;
    case QT_MAKE_FOURCC_LE ('m', 'p', '4', 'a'):
    case QT_MAKE_FOURCC_LE ('a', 'a', 'c', ' '):
      caps = audio_caps_from_string ("audio/mpeg,mpegversion=4");
      break;
    case QT_MAKE_FOURCC_LE ('s', 'a', 'm', 'r'):
      caps = audio_caps_from_string ("audio/AMR");
      break;
    case QT_MAKE_FOURCC_LE ('u', 'l', 'a', 'w'):
      caps = audio_caps_from_string ("audio/x-mulaw");
      break;
    case QT_MAKE_FOURCC_LE ('A', 'V', 'd', 'n'):
      caps = audio_caps_from_string ("video/x-dnxhd");
      break;
    case QT_MAKE_FOURCC_LE ('i', 'c', 'o', 'd'):
      caps = audio_caps_from_string ("video/x-apple-intermediate-codec");
      break;
      /* TO FILL !! */
    case QT_MAKE_FOURCC_LE ('M', 'A', 'C', '3'):
    case QT_MAKE_FOURCC_LE ('M', 'A', 'C', '6'):
    case QT_MAKE_FOURCC_LE ('Q', 'D', 'M', 'C'):
    case QT_MAKE_FOURCC_LE ('Q', 'c', 'l', 'p'):
    case QT_MAKE_FOURCC_LE ('Q', 'c', 'l', 'q'):
    case QT_MAKE_FOURCC_LE ('d', 'v', 'c', 'a'):
    default:
      break;
  }

  return caps;
}
