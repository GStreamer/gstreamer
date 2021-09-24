/*
 *  gstvaapidebug.h - VA-API debugging utilities
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@splitted-desktop.com>
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

#ifndef GST_VAAPI_DEBUG_H
#define GST_VAAPI_DEBUG_H

#include <gst/gstinfo.h>

#if DEBUG
GST_DEBUG_CATEGORY_EXTERN(gst_debug_vaapi);
#define GST_CAT_DEFAULT gst_debug_vaapi
#endif

#if DEBUG_VAAPI_DISPLAY
GST_DEBUG_CATEGORY_EXTERN(gst_debug_vaapi_display);
#define GST_CAT_DEFAULT gst_debug_vaapi_display
#endif

#endif /* GST_VAAPI_DEBUG_H */
