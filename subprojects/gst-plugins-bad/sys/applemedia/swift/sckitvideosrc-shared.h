/*
 * Copyright (C) 2024 Piotr Brzeziński <piotr@centricular.com>
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

/* This file is separate so that both Swift and Obj-C can import it without creating a cyclic dependency */

#ifndef __GST_SCKIT_VIDEO_SRC_ENUM_H__
#define __GST_SCKIT_VIDEO_SRC_ENUM_H__

/**
 * GstSCKitVideoSrcMode:
 * @GST_SCKIT_VIDEO_SRC_MODE_DISPLAY: Capture whole specified display with no additional filters
 * @GST_SCKIT_VIDEO_SRC_MODE_WINDOW: Capture specified window (irrelevant of which display it's on)
 * @GST_SCKIT_VIDEO_SRC_MODE_DISPLAY_EXCLUDING_WINDOWS: Capture whole specified display excluding specified windows
 * @GST_SCKIT_VIDEO_SRC_MODE_DISPLAY_INCLUDING_WINDOWS: Capture whole specified display only including specified windows
 * @GST_SCKIT_VIDEO_SRC_MODE_DISPLAY_EXCLUDING_APPLICATIONS_EXCEPT_WINDOWS: Capture whole display excluding specified apps, specified windows are exceptions
 * @GST_SCKIT_VIDEO_SRC_MODE_DISPLAY_INCLUDING_APPLICATIONS_EXCEPT_WINDOWS: Capture whole display with only specified apps, specified windows are exceptions
 *
 * Please see https://developer.apple.com/documentation/screencapturekit/sccontentfilter for more detailed information.
 *
 * Since: 1.26
 */
typedef enum
{
  GST_SCKIT_VIDEO_SRC_MODE_DISPLAY = 0,
  GST_SCKIT_VIDEO_SRC_MODE_WINDOW = 1,
  GST_SCKIT_VIDEO_SRC_MODE_DISPLAY_EXCLUDING_WINDOWS = 2,
  GST_SCKIT_VIDEO_SRC_MODE_DISPLAY_INCLUDING_WINDOWS = 3,
  GST_SCKIT_VIDEO_SRC_MODE_DISPLAY_EXCLUDING_APPLICATIONS_EXCEPT_WINDOWS = 4,
  GST_SCKIT_VIDEO_SRC_MODE_DISPLAY_INCLUDING_APPLICATIONS_EXCEPT_WINDOWS = 5,
} GstSCKitVideoSrcMode;

#define DEFAULT_DISPLAY_ID -1
#define DEFAULT_CAPTURE_MODE 0
#define DEFAULT_SHOW_CURSOR TRUE
#define DEFAULT_ALLOW_TRANSPARENCY FALSE

#endif /* __GST_SCKIT_VIDEO_SRC_ENUM_H__ */