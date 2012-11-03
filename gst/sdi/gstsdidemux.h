/* GStreamer
 * Copyright (C) 2010 REAL_NAME <EMAIL_ADDRESS>
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

#ifndef _GST_SDI_DEMUX_H_
#define _GST_SDI_DEMUX_H_

#include <gst/gst.h>
#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_SDI_DEMUX   (gst_sdi_demux_get_type())
#define GST_SDI_DEMUX(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SDI_DEMUX,GstSdiDemux))
#define GST_SDI_DEMUX_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SDI_DEMUX,GstSdiDemuxClass))
#define GST_IS_SDI_DEMUX(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SDI_DEMUX))
#define GST_IS_SDI_DEMUX_CLASS(obj)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SDI_DEMUX))

typedef struct _GstSdiDemux GstSdiDemux;
typedef struct _GstSdiDemuxClass GstSdiDemuxClass;
typedef struct _GstSdiFormat GstSdiFormat;

struct _GstSdiDemux
{
  GstElement base_sdidemux;
  GstPad *sinkpad;
  GstPad *srcpad;

  GstBuffer *output_buffer;
  int line;
  int offset;

  gboolean have_hsync;
  gboolean have_vsync;
  guchar stored_line[2160]; /* 864/2*5 */

  int frame_number;
  guint32 last_sync;
  GstSdiFormat *format;
};

struct _GstSdiFormat
{
  int lines;
  int active_lines;
  int width;
  int start0;
  int start1;
  int tff;
};

struct _GstSdiDemuxClass
{
  GstElementClass base_sdidemux_class;
};

GType gst_sdi_demux_get_type (void);

G_END_DECLS

#endif
