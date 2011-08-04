/* GStreamer
 * Copyright (C) 2011 David A. Schleef <ds@schleef.org>
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

#ifndef _GST_INTER_SURFACE_H_
#define _GST_INTER_SURFACE_H_

#include <gst/base/gstadapter.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

typedef struct _GstInterSurface GstInterSurface;

struct _GstInterSurface
{
  GMutex *mutex;

  /* video */
  GstVideoFormat format;
  int fps_n;
  int fps_d;
  int width;
  int height;
  int n_frames;
  int video_buffer_count;

  /* audio */
  int sample_rate;
  int n_channels;

  GstBuffer *video_buffer;
  GstAdapter *audio_adapter;
};


GstInterSurface * gst_inter_surface_get (const char *name);
void gst_inter_surface_init (void);


G_END_DECLS

#endif
