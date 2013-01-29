/*
 *  sysdeps.h - System-dependent definitions
 *
 *  Copyright (C) 2012-2013 Intel Corporation
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

#ifndef SYSDEPS_H
#define SYSDEPS_H

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "glibcompat.h"

/* <gst/video/video-overlay-composition.h> compatibility glue */
#ifndef HAVE_GST_VIDEO_OVERLAY_HWCAPS
# define gst_video_overlay_rectangle_get_flags(rect) (0)
# define gst_video_overlay_rectangle_get_global_alpha(rect) (1.0f)
#endif

#endif /* SYSDEPS_H */
