/* GStreamer
 * Copyright (C) 2003 Martin Soto <martinsoto@users.sourceforge.net>
 *
 * dxr3videosink.h: Common declarations for the DXR3 plugin.
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

#ifndef __DXR3COMMON_H__
#define __DXR3COMMON_H__

/* Convert from GStreamer time to MPEG time. */
#define GSTTIME_TO_MPEGTIME(time) (((time) * 9) / (GST_MSECOND/10))


/* The em8300 driver expresses time in units of  1/45000 of second. */

/* Convert from MPEG time to em8300 time. */
#define MPEGTIME_TO_DXRTIME(time) ((guint32) ((time) >> 1))

/* Convert from em8300 time to GStreamer time. */
#define DXRTIME_TO_GSTTIME(time) \
  ((GstClockTime) ((double) (time) * (((double) GST_MSECOND) / 45.0)))


#endif /* __DXR3COMMON_H__ */
