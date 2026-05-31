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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstwavpackcommon.h"
#include <string.h>

#include <gst/gst.h>

GST_DEBUG_CATEGORY_EXTERN (wavpack_debug);
#define GST_CAT_DEFAULT wavpack_debug

gboolean
gst_wavpack_read_header (WavpackHeader * header, guint8 * buf)
{
  memmove (header, buf, sizeof (WavpackHeader));

  WavpackLittleEndianToNative (header, (char *) WavpackHeaderFormat);

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

guint32
gst_wavpack_get_default_channel_mask (gint nchannels)
{
  guint32 channel_mask = 0;

  /* Set the default channel mask for the given number of channels.
   * It's the same as for WAVE_FORMAT_EXTENDED:
   * http://www.microsoft.com/whdc/device/audio/multichaud.mspx
   */
  switch (nchannels) {
    case 11:
      channel_mask |= 0x00400;
      channel_mask |= 0x00200;
      /* FALLTHROUGH */
    case 9:
      channel_mask |= 0x00100;
      /* FALLTHROUGH */
    case 8:
      channel_mask |= 0x00080;
      channel_mask |= 0x00040;
      /* FALLTHROUGH */
    case 6:
      channel_mask |= 0x00020;
      channel_mask |= 0x00010;
      /* FALLTHROUGH */
    case 4:
      channel_mask |= 0x00008;
      /* FALLTHROUGH */
    case 3:
      channel_mask |= 0x00004;
      /* FALLTHROUGH */
    case 2:
      channel_mask |= 0x00002;
      channel_mask |= 0x00001;
      break;
    case 1:
      /* For mono use front center */
      channel_mask |= 0x00004;
      break;
  }

  return channel_mask;
}

static const struct
{
  const guint32 ms_mask;
  const GstAudioChannelPosition gst_pos;
} layout_mapping[] = {
  {
      0x00001, GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT}, {
      0x00002, GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT}, {
      0x00004, GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER}, {
      0x00008, GST_AUDIO_CHANNEL_POSITION_LFE1}, {
      0x00010, GST_AUDIO_CHANNEL_POSITION_REAR_LEFT}, {
      0x00020, GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT}, {
      0x00040, GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT_OF_CENTER}, {
      0x00080, GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER}, {
      0x00100, GST_AUDIO_CHANNEL_POSITION_REAR_CENTER}, {
      0x00200, GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT}, {
      0x00400, GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT}, {
      0x00800, GST_AUDIO_CHANNEL_POSITION_TOP_CENTER}, {
      0x01000, GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_LEFT}, {
      0x02000, GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_CENTER}, {
      0x04000, GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_RIGHT}, {
      0x08000, GST_AUDIO_CHANNEL_POSITION_TOP_REAR_LEFT}, {
      0x10000, GST_AUDIO_CHANNEL_POSITION_TOP_REAR_CENTER}, {
      0x20000, GST_AUDIO_CHANNEL_POSITION_TOP_REAR_RIGHT}
};

#define MAX_CHANNEL_POSITIONS G_N_ELEMENTS (layout_mapping)

gboolean
gst_wavpack_get_channel_positions (gint num_channels, guint32 layout,
    GstAudioChannelPosition * pos)
{
  gint i, p;

  if (num_channels == 1 && layout == 0x00004) {
    pos[0] = GST_AUDIO_CHANNEL_POSITION_MONO;
    return TRUE;
  }

  p = 0;
  for (i = 0; i < MAX_CHANNEL_POSITIONS; ++i) {
    if ((layout & layout_mapping[i].ms_mask) != 0) {
      pos[p] = layout_mapping[i].gst_pos;
      ++p;
    }
  }

  // Not all channels found, consider it unpositioned
  if (p != num_channels) {
    for (i = 0; i < MIN (64, num_channels); i++)
      pos[i] = GST_AUDIO_CHANNEL_POSITION_NONE;
    return FALSE;
  }

  return TRUE;
}

guint32
gst_wavpack_get_channel_mask_from_positions (const GstAudioChannelPosition *
    pos, gint nchannels)
{
  guint32 channel_mask = 0;
  gint i, j;
  gint found_channels = 0;

  if (nchannels == 1 && pos[0] == GST_AUDIO_CHANNEL_POSITION_MONO) {
    channel_mask = 0x00000004;
    return channel_mask;
  }

  /* FIXME: not exactly efficient but otherwise we need an inverse
   * mapping table too */
  for (i = 0; i < nchannels; i++) {
    for (j = 0; j < MAX_CHANNEL_POSITIONS; j++) {
      if (pos[i] == layout_mapping[j].gst_pos) {
        channel_mask |= layout_mapping[j].ms_mask;
        found_channels++;
        break;
      }
    }
  }

  // If not all channels were found consider it unpositioned
  if (found_channels != nchannels)
    channel_mask = 0;

  return channel_mask;
}

gboolean
gst_wavpack_set_channel_mapping (const GstAudioChannelPosition * pos,
    gint nchannels, gint8 * channel_mapping)
{
  gint i, j;
  gboolean ret = TRUE;
  gint found_channels = 0;

  for (i = 0; i < nchannels; i++) {
    for (j = 0; j < MAX_CHANNEL_POSITIONS; j++) {
      if (pos[i] == layout_mapping[j].gst_pos) {
        channel_mapping[i] = j;
        ret &= (i == j);
        found_channels++;
        break;
      }
    }
  }

  // If not all channels were found, don't reorder anything and consider
  // it unpositioned
  if (found_channels != nchannels) {
    memset (channel_mapping, 0, MIN (64, nchannels));
    ret = TRUE;
  }

  return !ret;
}
