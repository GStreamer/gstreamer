/*
 * GStreamer
 * Copyright (C) 2026 Seungha Yang <seungha@centricular.com>
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

#pragma once

#include <gst/gst.h>

G_BEGIN_DECLS

enum
{
  PROP_0,
  PROP_MODE,
  PROP_DEVICE_NUMBER,
  PROP_PERSISTENT_ID,
  PROP_VIDEO_CONNECTION,
  PROP_AUDIO_CONNECTION,
  PROP_VIDEO_FORMAT,
  PROP_AUDIO_CHANNELS,
  PROP_PROFILE_ID,
  PROP_TIMECODE_FORMAT,
  PROP_OUTPUT_CC,
  PROP_OUTPUT_AFD_BAR,
  PROP_OUTPUT_VANC,
  PROP_MAX_BUFFERED_FRAMES,
  PROP_SIGNAL,
  PROP_SKIP_FIRST_TIME,
  PROP_DESYNC_THRESHOLD,
  PROP_DROP_NO_SIGNAL_FRAMES,
  PROP_HW_SERIAL_NUMBER,
  PROP_INPUT_STATS,
  PROP_SRC_LAST,
};

G_END_DECLS
