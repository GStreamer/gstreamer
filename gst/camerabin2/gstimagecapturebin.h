/* GStreamer
 * Copyright (C) 2010 Thiago Santos <thiago.sousa.santos@collabora.co.uk>
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
#ifndef _GST_IMAGE_CAPTURE_BIN_H_
#define _GST_IMAGE_CAPTURE_BIN_H_

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_IMAGE_CAPTURE_BIN   (gst_image_capture_bin_get_type())
#define GST_IMAGE_CAPTURE_BIN(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_IMAGE_CAPTURE_BIN,GstImageCaptureBin))
#define GST_IMAGE_CAPTURE_BIN_CAST(obj)   ((GstImageCaptureBin *) obj)
#define GST_IMAGE_CAPTURE_BIN_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_IMAGE_CAPTURE_BIN,GstImageCaptureBinClass))
#define GST_IS_IMAGE_CAPTURE_BIN(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_IMAGE_CAPTURE_BIN))
#define GST_IS_IMAGE_CAPTURE_BIN_CLASS(obj)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_IMAGE_CAPTURE_BIN))

typedef struct _GstImageCaptureBin GstImageCaptureBin;
typedef struct _GstImageCaptureBinClass GstImageCaptureBinClass;

struct _GstImageCaptureBin
{
  GstBin bin;

  GstPad *ghostpad;

  gboolean elements_created;
};

struct _GstImageCaptureBinClass
{
  GstBinClass bin_class;
};

GType gst_image_capture_bin_get_type (void);
gboolean gst_image_capture_bin_plugin_init (GstPlugin * plugin);

G_END_DECLS

#endif
