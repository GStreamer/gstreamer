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
#ifndef _GST_CAMERA_BIN_H_
#define _GST_CAMERA_BIN_H_

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_CAMERA_BIN   (gst_camera_bin_get_type())
#define GST_CAMERA_BIN(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CAMERA_BIN,GstCameraBin))
#define GST_CAMERA_BIN_CAST(obj)   ((GstCameraBin *) obj)
#define GST_CAMERA_BIN_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CAMERA_BIN,GstCameraBinClass))
#define GST_IS_CAMERA_BIN(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CAMERA_BIN))
#define GST_IS_CAMERA_BIN_CLASS(obj)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CAMERA_BIN))

typedef struct _GstCameraBin GstCameraBin;
typedef struct _GstCameraBinClass GstCameraBinClass;

struct _GstCameraBin
{
  GstPipeline pipeline;

  GstElement *src;
  gulong src_capture_notify_id;

  GstElement *vidbin;
  GstElement *imgbin;

  gint vid_index;

  /* properties */
  gint mode;
  gchar *vid_location;
  gchar *img_location;

  gboolean elements_created;
};

struct _GstCameraBinClass
{
  GstPipelineClass pipeline_class;

  /* Action signals */
  void (*start_capture) (GstCameraBin * camera);
  void (*stop_capture) (GstCameraBin * camera);
};

GType gst_camera_bin_get_type (void);
gboolean gst_camera_bin_plugin_init (GstPlugin * plugin);

G_END_DECLS

#endif
