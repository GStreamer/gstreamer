/* GStreamer
 * Copyright (C) 2004 Benjamin Otte <in7y118@public.uni-hamburg.de>
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

#include "gstxine.h"
#include <xine/buffer.h>

typedef struct {
  guint32	xine;
  gchar *	caps;
} GstXineCapsMap;

static GstXineCapsMap _gst_xine_caps_map[] = {
  { BUF_AUDIO_QDESIGN2,		"audio/x-qdm2" },
/* FIXME:
#define BUF_AUDIO_A52		0x03000000
#define BUF_AUDIO_MPEG		0x03010000
#define BUF_AUDIO_LPCM_BE	0x03020000
#define BUF_AUDIO_LPCM_LE	0x03030000
#define BUF_AUDIO_WMAV1		0x03040000
#define BUF_AUDIO_DTS		0x03050000
#define BUF_AUDIO_MSADPCM	0x03060000
#define BUF_AUDIO_MSIMAADPCM	0x03070000
#define BUF_AUDIO_MSGSM		0x03080000
#define BUF_AUDIO_VORBIS        0x03090000
#define BUF_AUDIO_IMC           0x030a0000
#define BUF_AUDIO_LH            0x030b0000
#define BUF_AUDIO_VOXWARE       0x030c0000
#define BUF_AUDIO_ACELPNET      0x030d0000
#define BUF_AUDIO_AAC           0x030e0000
#define BUF_AUDIO_DNET    	0x030f0000
#define BUF_AUDIO_VIVOG723      0x03100000
#define BUF_AUDIO_DK3ADPCM	0x03110000
#define BUF_AUDIO_DK4ADPCM	0x03120000
#define BUF_AUDIO_ROQ		0x03130000
#define BUF_AUDIO_QTIMAADPCM	0x03140000
#define BUF_AUDIO_MAC3		0x03150000
#define BUF_AUDIO_MAC6		0x03160000
#define BUF_AUDIO_QDESIGN1	0x03170000
#define BUF_AUDIO_QDESIGN2	0x03180000
#define BUF_AUDIO_QCLP		0x03190000
#define BUF_AUDIO_SMJPEG_IMA	0x031A0000
#define BUF_AUDIO_VQA_IMA	0x031B0000
#define BUF_AUDIO_MULAW		0x031C0000
#define BUF_AUDIO_ALAW		0x031D0000
#define BUF_AUDIO_GSM610	0x031E0000
#define BUF_AUDIO_EA_ADPCM      0x031F0000
#define BUF_AUDIO_WMAV2		0x03200000
#define BUF_AUDIO_COOK 		0x03210000
#define BUF_AUDIO_ATRK 		0x03220000
#define BUF_AUDIO_14_4 		0x03230000
#define BUF_AUDIO_28_8 		0x03240000
#define BUF_AUDIO_SIPRO		0x03250000
#define BUF_AUDIO_WMAV3		0x03260000
#define BUF_AUDIO_INTERPLAY	0x03270000
#define BUF_AUDIO_XA_ADPCM	0x03280000
#define BUF_AUDIO_WESTWOOD	0x03290000
#define BUF_AUDIO_DIALOGIC_IMA	0x032A0000
#define BUF_AUDIO_NSF		0x032B0000
#define BUF_AUDIO_FLAC		0x032C0000
#define BUF_AUDIO_DV		0x032D0000
#define BUF_AUDIO_WMAV		0x032E0000
#define BUF_AUDIO_SPEEX		0x032F0000
#define BUF_AUDIO_RAWPCM	0x03300000
#define BUF_AUDIO_4X_ADPCM	0x03310000
*/
  { 0,				NULL }
};

const gchar *
gst_xine_get_caps_for_format (guint32 format)
{
  guint i = 0;
  
  while (_gst_xine_caps_map[i].xine != 0) {
    if (_gst_xine_caps_map[i].xine == format)
      return _gst_xine_caps_map[i].caps;
    i++;
  }
  
  return NULL;
}

guint32
gst_xine_get_format_for_caps (const GstCaps *caps)
{
  guint i = 0;
  GstCaps *compare, *intersect;
  
  while (_gst_xine_caps_map[i].xine != 0) {
    compare = gst_caps_from_string (_gst_xine_caps_map[i].caps);
    intersect = gst_caps_intersect (caps, compare);
    gst_caps_free (compare);
    if (!gst_caps_is_empty (intersect)) {
      gst_caps_free (intersect);
      return _gst_xine_caps_map[i].xine;
    }
    gst_caps_free (intersect);
    i++;
  }
  
  return 0;     
}

