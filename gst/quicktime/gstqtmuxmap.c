/* Quicktime muxer plugin for GStreamer
 * Copyright (C) 2008 Thiago Sousa Santos <thiagoss@embedded.ufcg.edu.br>
 * Copyright (C) 2008 Mark Nauwelaerts <mnauw@users.sf.net>
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

#include "gstqtmuxmap.h"
#include "fourcc.h"
#include "ftypcc.h"

/* static info related to various format */

#define COMMON_VIDEO_CAPS \
  "width = (int) [ 16, 4096 ], " \
  "height = (int) [ 16, 4096 ], " \
  "framerate = (fraction) [ 0, MAX ]"

#define COMMON_VIDEO_CAPS_NO_FRAMERATE \
  "width = (int) [ 16, 4096 ], " \
  "height = (int) [ 16, 4096 ] "

#define H264_CAPS \
  "video/x-h264, " \
  COMMON_VIDEO_CAPS

#define MPEG4V_CAPS \
  "video/mpeg, " \
  "mpegversion = (int) 4, "\
  "systemstream = (boolean) false, " \
  COMMON_VIDEO_CAPS "; " \
  "video/x-divx, " \
  "divxversion = (int) 5, "\
  COMMON_VIDEO_CAPS

#define COMMON_AUDIO_CAPS(c, r) \
  "channels = (int) [ 1, " G_STRINGIFY (c) " ], " \
  "rate = (int) [ 1, " G_STRINGIFY (r) " ]"

#define PCM_CAPS \
  "audio/x-raw-int, " \
  "width = (int) 8, " \
  "depth = (int) 8, " \
  COMMON_AUDIO_CAPS (2, MAX) ", " \
  "signed = (boolean) { true, false }; " \
  "audio/x-raw-int, " \
  "width = (int) 16, " \
  "depth = (int) 16, " \
  "endianness = (int) { BIG_ENDIAN, LITTLE_ENDIAN }, " \
  COMMON_AUDIO_CAPS (2, MAX) ", " \
  "signed = (boolean) true " \

#define PCM_CAPS_FULL \
  PCM_CAPS "; " \
  "audio/x-raw-int, " \
  "width = (int) 24, " \
  "depth = (int) 24, " \
  "endianness = (int) { BIG_ENDIAN, LITTLE_ENDIAN }, " \
  COMMON_AUDIO_CAPS (2, MAX) ", " \
  "signed = (boolean) true; " \
  "audio/x-raw-int, " \
  "width = (int) 32, " \
  "depth = (int) 32, " \
  "endianness = (int) { BIG_ENDIAN, LITTLE_ENDIAN }, " \
  COMMON_AUDIO_CAPS (2, MAX) ", " \
  "signed = (boolean) true "

#define MP3_CAPS \
  "audio/mpeg, " \
  "mpegversion = (int) 1, " \
  "layer = (int) 3, " \
  COMMON_AUDIO_CAPS (2, MAX)

#define AAC_CAPS \
  "audio/mpeg, " \
  "mpegversion = (int) 4, " \
  COMMON_AUDIO_CAPS (8, MAX)


GstQTMuxFormatProp gst_qt_mux_format_list[] = {
  /* original QuickTime format; see Apple site (e.g. qtff.pdf) */
  {
        GST_QT_MUX_FORMAT_QT,
        "qtmux",
        "QuickTime",
        "GstQTMux",
        GST_STATIC_CAPS ("video/quicktime"),
        GST_STATIC_CAPS ("video/x-raw-rgb, "
            COMMON_VIDEO_CAPS "; "
            "video/x-raw-yuv, "
            "format = (fourcc) UYVY, "
            COMMON_VIDEO_CAPS "; "
            "video/x-h263, "
            "h263version = (string) h263, "
            COMMON_VIDEO_CAPS "; "
            MPEG4V_CAPS "; "
            H264_CAPS "; "
            "video/x-dv, "
            "systemstream = (boolean) false, "
            COMMON_VIDEO_CAPS "; "
            "image/jpeg, "
            COMMON_VIDEO_CAPS_NO_FRAMERATE "; " "video/x-qt-part"),
        GST_STATIC_CAPS (PCM_CAPS_FULL "; "
            MP3_CAPS " ; "
            AAC_CAPS " ; "
            "audio/x-alaw, "
            COMMON_AUDIO_CAPS (2, MAX) "; "
            "audio/x-mulaw, " COMMON_AUDIO_CAPS (2, MAX))
      }
  ,
  /* ISO 14496-14: mp42 as ISO base media extension
   * (supersedes original ISO 144996-1 mp41) */
  {
        GST_QT_MUX_FORMAT_MP4,
        "mp4mux",
        "MP4",
        "GstMP4Mux",
        /* FIXME does not feel right, due to qt caps mess */
        GST_STATIC_CAPS ("video/quicktime"),
        GST_STATIC_CAPS (MPEG4V_CAPS "; " H264_CAPS),
        GST_STATIC_CAPS (MP3_CAPS "; " AAC_CAPS)
      }
  ,
  /* 3GPP Technical Specification 26.244 V7.3.0
   * (extended in 3GPP2 File Formats for Multimedia Services) */
  {
        GST_QT_MUX_FORMAT_3GP,
        "gppmux",
        "3GPP",
        "GstGPPMux",
        GST_STATIC_CAPS ("application/x-3gp"),
        GST_STATIC_CAPS (H264_CAPS),
        GST_STATIC_CAPS ("audio/AMR, "
            COMMON_AUDIO_CAPS (8, MAX) "; " MP3_CAPS "; " AAC_CAPS)
      }
  ,
  /* ISO 15444-3: Motion-JPEG-2000 (also ISO base media extension) */
  {
        GST_QT_MUX_FORMAT_MJ2,
        "mj2mux",
        "MJ2",
        "GstMJ2Mux",
        GST_STATIC_CAPS ("video/mj2"),
        GST_STATIC_CAPS ("image/x-j2c, " COMMON_VIDEO_CAPS),
        GST_STATIC_CAPS (PCM_CAPS)
      }
  ,
  {
        GST_QT_MUX_FORMAT_NONE,
      }
  ,
};

/* pretty static, but may turn out needed a few times */
AtomsTreeFlavor
gst_qt_mux_map_format_to_flavor (GstQTMuxFormat format)
{
  if (format == GST_QT_MUX_FORMAT_QT)
    return ATOMS_TREE_FLAVOR_MOV;
  else
    return ATOMS_TREE_FLAVOR_ISOM;
}

/* pretty static, but possibly dynamic format info */

/* notes:
 * - avc1 brand is not used, since the specific extensions indicated by it
 *   are not used (e.g. sample groupings, etc)
 * - 3GPP2 specific formats not (yet) used, only 3GPP, so no need yet either
 *   for 3g2a (but later on, moov might be used to conditionally switch to
 *   3g2a if needed) */
void
gst_qt_mux_map_format_to_header (GstQTMuxFormat format, GstBuffer ** _prefix,
    guint32 * _major, guint32 * _version, GList ** _compatible, AtomMOOV * moov)
{
  static guint32 qt_brands[] = { 0 };
  static guint32 mp4_brands[] = { FOURCC_mp41, FOURCC_isom, FOURCC_iso2, 0 };
  static guint32 gpp_brands[] = { FOURCC_isom, FOURCC_iso2, 0 };
  static guint32 mjp2_brands[] = { FOURCC_isom, FOURCC_iso2, 0 };
  static guint8 mjp2_prefix[] =
      { 0, 0, 0, 12, 'j', 'P', ' ', ' ', 0x0D, 0x0A, 0x87, 0x0A };
  guint32 *comp = NULL;
  guint32 major = 0, version;
  GstBuffer *prefix = NULL;
  GList *result = NULL;

  g_return_if_fail (_prefix != NULL);
  g_return_if_fail (_major != NULL);
  g_return_if_fail (_version != NULL);
  g_return_if_fail (_compatible != NULL);

  version = 1;
  switch (format) {
    case GST_QT_MUX_FORMAT_QT:
      major = FOURCC_qt__;
      comp = qt_brands;
      version = 0x20050300;
      break;
    case GST_QT_MUX_FORMAT_MP4:
      major = FOURCC_mp42;
      comp = mp4_brands;
      break;
    case GST_QT_MUX_FORMAT_3GP:
      major = FOURCC_3gg7;
      comp = gpp_brands;
      break;
    case GST_QT_MUX_FORMAT_MJ2:
      major = FOURCC_mjp2;
      comp = mjp2_brands;
      prefix = gst_buffer_new_and_alloc (sizeof (mjp2_prefix));
      memcpy (GST_BUFFER_DATA (prefix), mjp2_prefix, GST_BUFFER_SIZE (prefix));
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  /* convert list to list, hm */
  while (comp && *comp != 0) {
    /* order matters over efficiency */
    result = g_list_append (result, GUINT_TO_POINTER (*comp));
    comp++;
  }

  *_major = major;
  *_version = version;
  *_prefix = prefix;
  *_compatible = result;

  /* TODO 3GPP may include mp42 as compatible if applicable */
  /* TODO 3GPP major brand 3gp7 if at most 1 video and audio track */
}
