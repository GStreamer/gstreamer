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
  PROP_VIDEO_FORMAT,
  PROP_PROFILE_ID,
  PROP_TIMECODE_FORMAT,
  PROP_KEYER_MODE,
  PROP_KEYER_LEVEL,
  PROP_CC_LINE,
  PROP_AFD_BAR_LINE,
  PROP_OUTPUT_VANC,
  PROP_MAPPING_FORMAT,
  PROP_PERSISTENT_ID,
  PROP_N_PREROLL_FRAMES,
  PROP_MIN_BUFFERED_FRAMES,
  PROP_MAX_BUFFERED_FRAMES,
  PROP_AUTO_RESTART,
  PROP_OUTPUT_STATS,
  PROP_DESYNC_THRESHOLD,
  PROP_HW_SERIAL_NUMBER,
  PROP_SINK_LAST,
};

G_END_DECLS
