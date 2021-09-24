/*
 * y4mreader.h - Y4M parser
 *
 * Copyright (C) 2015 Intel Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA
 */

#include <gst/gst.h>
#include <stdio.h>
#include <gst/vaapi/gstvaapiimage.h>

typedef struct _Y4MReader Y4MReader;

struct _Y4MReader
{
  FILE *fp;
  guint width;
  guint height;
  gint fps_n;
  gint fps_d;
};

Y4MReader *y4m_reader_open (const gchar * filename);

void y4m_reader_close (Y4MReader * file);

gboolean y4m_reader_load_image (Y4MReader * file, GstVaapiImage * image);
