/* GStreamer Wavpack plugin
 * Copyright (c) 2005 Arwed v. Merkatz <v.merkatz@gmx.net>
 * Copyright (c) 1998 - 2005 Conifer Software
 * Copyright (c) 2006 Sebastian Dröge <slomo@circular-chaos.org>
 *
 * gstwavpackcommon.c: common helper functions
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstwavpackcommon.h"
#include <string.h>

GST_DEBUG_CATEGORY_EXTERN (wavpack_debug);
#define GST_CAT_DEFAULT wavpack_debug

gboolean
gst_wavpack_read_header (WavpackHeader * header, guint8 * buf)
{
  g_memmove (header, buf, sizeof (WavpackHeader));

#ifndef WAVPACK_OLD_API
  WavpackLittleEndianToNative (header, WavpackHeaderFormat);
#else
  little_endian_to_native (header, WavpackHeaderFormat);
#endif

  return (memcmp (header->ckID, "wvpk", 4) == 0);
}

/* inspired by the original one in wavpack */
gboolean
gst_wavpack_read_metadata (GstWavpackMetadata * wpmd, guint8 * header_data,
    guint8 ** p_data)
{
  WavpackHeader hdr;
  guint8 *end;

  gst_wavpack_read_header (&hdr, header_data);
  end = header_data + hdr.ckSize + 8;

  if (end - *p_data < 2)
    return FALSE;

  wpmd->id = GST_READ_UINT8 (*p_data);
  wpmd->byte_length = 2 * (guint) GST_READ_UINT8 (*p_data + 1);

  *p_data += 2;

  if ((wpmd->id & ID_LARGE) == ID_LARGE) {
    guint extra;

    wpmd->id &= ~ID_LARGE;

    if (end - *p_data < 2)
      return FALSE;

    extra = GST_READ_UINT16_LE (*p_data);
    wpmd->byte_length += (extra << 9);
    *p_data += 2;
  }

  if ((wpmd->id & ID_ODD_SIZE) == ID_ODD_SIZE) {
    wpmd->id &= ~ID_ODD_SIZE;
    --wpmd->byte_length;
  }

  if (wpmd->byte_length > 0) {
    if (end - *p_data < wpmd->byte_length + (wpmd->byte_length & 1)) {
      wpmd->data = NULL;
      return FALSE;
    }

    wpmd->data = *p_data;
    *p_data += wpmd->byte_length + (wpmd->byte_length & 1);
  } else {
    wpmd->data = NULL;
  }

  return TRUE;
}
