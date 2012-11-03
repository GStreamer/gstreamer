/* GStreamer
 *
 * Copyright (C) 2007 Hans de Goede <j.w.r.degoede@hhs.nl>
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

#ifndef __GST_REAL_H__
#define __GST_REAL_H__

#ifdef HAVE_CPU_I386
#define DEFAULT_REAL_CODECS_PATH \
  "/usr/lib/win32:/usr/lib/codecs:/usr/local/RealPlayer/codecs:" \
  "/usr/local/lib/win32:/usr/local/lib/codecs"
#endif
#ifdef HAVE_CPU_X86_64
#define DEFAULT_REAL_CODECS_PATH \
  "/usr/lib64/win32:/usr/lib64/codecs:" \
  "/usr/local/lib64/win32:/usr/local/lib64/codecs"
#endif

#endif /* __GST_REAL_H__ */
