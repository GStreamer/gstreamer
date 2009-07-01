/* Schrodinger
 * Copyright (C) 2008 David Schleef <ds@schleef.org>
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

#ifndef _GST_SCHRO_UTILS_H_
#define _GST_SCHRO_UTILS_H_

#include <gst/gst.h>
#include <gst/video/video.h>
#include <schroedinger/schro.h>

SchroFrame *
gst_schro_buffer_wrap (GstBuffer *buf, GstVideoFormat format, int width,
    int height);
GstBuffer * gst_schro_wrap_schro_buffer (SchroBuffer *buffer);
SchroBuffer * gst_schro_wrap_gst_buffer (GstBuffer *buffer);

#endif

