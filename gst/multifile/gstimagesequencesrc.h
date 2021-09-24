/* GStreamer
 * Copyright (C) 2019 Cesar Fabian Orccon Chipana
 * Copyright (C) 2020 Thibault Saunier <tsaunier@igalia.com>
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

#define GST_TYPE_IMAGE_SEQUENCE_SRC (gst_image_sequence_src_get_type())
G_DECLARE_FINAL_TYPE(GstImageSequenceSrc, gst_image_sequence_src, GST, IMAGE_SEQUENCE_SRC, GstPushSrc)

struct _GstImageSequenceSrc
{
  GstPushSrc parent;

  GRecMutex fields_lock;
  gchar* path;
  GstUri *uri;
  gint start_index;
  gint stop_index;
  gint index;
  gint n_frames;

  guint64 duration;
  gboolean reverse;

  GstCaps *caps;

  gint fps_n, fps_d;
};

GST_ELEMENT_REGISTER_DECLARE (imagesequencesrc);

G_END_DECLS

#endif /* __GST_IMAGESEQUENCESRC_H__ */
