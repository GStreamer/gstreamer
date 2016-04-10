/* GStreamer
 * Copyright (C) 2019 Cesar Fabian Orccon Chipana
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
 * Free Software Foundation, Inc., 51 Franklin Street, Suite 500,
 * Boston, MA 02110-1335, USA.
 */

#ifndef __GST_IMAGESEQUENCESRC_H__
#define __GST_IMAGESEQUENCESRC_H__

#include <gst/gst.h>
#include <gst/base/gstpushsrc.h>

G_BEGIN_DECLS

#define GST_TYPE_IMAGE_SEQUENCE_SRC \
  (gst_image_sequence_src_get_type())
#define GST_IMAGE_SEQUENCE_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_IMAGE_SEQUENCE_SRC,GstImageSequenceSrc))
#define GST_IMAGE_SEQUENCE_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_IMAGE_SEQUENCE_SRC,GstImageSequenceSrcClass))
#define GST_IS_IMAGE_SEQUENCE_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_IMAGE_SEQUENCE_SRC))
#define GST_IS_IMAGE_SEQUENCE_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_IMAGE_SEQUENCE_SRC))

typedef struct _GstImageSequenceSrc GstImageSequenceSrc;
typedef struct _GstImageSequenceSrcClass GstImageSequenceSrcClass;

struct _GstImageSequenceSrc
{
  GstPushSrc parent;

  gchar *filename;
  int start_index;
  int stop_index;
  int index;
  int count_frames;

  guint64 duration;
  guint64 buffer_duration;

  GstCaps *caps;

  gint fps_n, fps_d;
};

struct _GstImageSequenceSrcClass
{
  GstPushSrcClass parent_class;
};

GType gst_image_sequence_src_get_type (void);

G_END_DECLS

#endif /* __GST_IMAGESEQUENCESRC_H__ */
