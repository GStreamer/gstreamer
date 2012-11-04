/* GStreamer
 * Copyright (C) <2008> Wim Taymans <wim.taymans@gmail.com>
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

#include <string.h>
#include <stdlib.h>

#include <gst/audio/audio.h>

#ifndef __GST_RTP_CHANNELS_H__
#define __GST_RTP_CHANNELS_H__

typedef struct
{
  const gchar                   *name;
  gint                           channels;
  const GstAudioChannelPosition *pos;
} GstRTPChannelOrder;

static const GstAudioChannelPosition pos_4_1[] = {
  GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
  GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
  GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT,
  GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT
};

static const GstAudioChannelPosition pos_4_2[] = {
  GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
  GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
  GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
  GST_AUDIO_CHANNEL_POSITION_LFE1
};

static const GstAudioChannelPosition pos_4_3[] = {
  GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
  GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
  GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
  GST_AUDIO_CHANNEL_POSITION_LFE1
};

static const GstAudioChannelPosition pos_5_1[] = {
  GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
  GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
  GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT,
  GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT,
  GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER
};

static const GstAudioChannelPosition pos_6_1[] = {
  GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
  GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
  GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT,
  GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT,
  GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
  GST_AUDIO_CHANNEL_POSITION_LFE1
};

static const GstAudioChannelPosition pos_6_2[] = {
  GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
  GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
  GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
  GST_AUDIO_CHANNEL_POSITION_LFE1,
  GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT,
  GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT
};

static const GstAudioChannelPosition pos_8_1[] = {
  GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
  GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
  GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
  GST_AUDIO_CHANNEL_POSITION_LFE1,
  GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT,
  GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT,
  GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
  GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT
};

static const GstAudioChannelPosition pos_8_2[] = {
  GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
  GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
  GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
  GST_AUDIO_CHANNEL_POSITION_LFE1,
  GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT,
  GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT,
  GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
  GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT
};

static const GstAudioChannelPosition pos_8_3[] = {
  GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
  GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
  GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
  GST_AUDIO_CHANNEL_POSITION_LFE1,
  GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT,
  GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT,
  GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
  GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT
};

static const GstAudioChannelPosition pos_def_1[] = {
  GST_AUDIO_CHANNEL_POSITION_MONO
};

static const GstAudioChannelPosition pos_def_2[] = {
  GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
  GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT
};

static const GstAudioChannelPosition pos_def_3[] = {
  GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
  GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
  GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER
};

static const GstAudioChannelPosition pos_def_4[] = {
  GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
  GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
  GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
  GST_AUDIO_CHANNEL_POSITION_LFE1
};

static const GstAudioChannelPosition pos_def_5[] = {
  GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
  GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
  GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
  GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT,
  GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT
};

static const GstAudioChannelPosition pos_def_6[] = {
  GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT,
  GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
  GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
  GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT,
  GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
  GST_AUDIO_CHANNEL_POSITION_LFE1
};

static const GstRTPChannelOrder channel_orders[] =
{
  /* 4 channels */
  { "DV.LRLsRs",            4, pos_4_1 },
  { "DV.LRCS",              4, pos_4_2 },
  { "DV.LRCWo",             4, pos_4_3 },
  /* 5 channels */
  { "DV.LRLsRsC",           5, pos_5_1 },
  /* 6 channels */
  { "DV.LRLsRsCS",          6, pos_6_1 },
  { "DV.LmixRmixTWoQ1Q2",   6, pos_6_2 },
  /* 8 channels */
  { "DV.LRCWoLsRsLmixRmix", 8, pos_8_1 },
  { "DV.LRCWoLs1Rs1Ls2Rs2", 8, pos_8_2 },
  { "DV.LRCWoLsRsLcRc",     8, pos_8_3 },

  /* default layouts */
  { NULL,                   1, pos_def_1 },
  { NULL,                   2, pos_def_2 },
  { NULL,                   3, pos_def_3 },
  { NULL,                   4, pos_def_4 },
  { NULL,                   5, pos_def_5 },
  { NULL,                   6, pos_def_6 },

  /* terminator, invalid entry */
  { NULL,                   0, NULL },
};

const GstRTPChannelOrder *   gst_rtp_channels_get_by_pos     (gint channels,
                                                              const GstAudioChannelPosition *pos);
const GstRTPChannelOrder *   gst_rtp_channels_get_by_order   (gint channels,
                                                              const gchar *order);
const GstRTPChannelOrder *   gst_rtp_channels_get_by_index   (gint channels, guint idx);

void                         gst_rtp_channels_create_default (gint channels, GstAudioChannelPosition *pos);

#endif /* __GST_RTP_CHANNELS_H__ */
