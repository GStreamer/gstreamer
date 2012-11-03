/* GStreamer
 * Copyright (C) <2006> Eric Jonas <jonas@mit.edu>
 * Copyright (C) <2006> Antoine Tremblay <hexa00@gmail.com>
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

#ifndef __GST_DC1394_H__
#define __GST_DC1394_H__

#include <gst/gst.h>
#include <gst/base/gstpushsrc.h>
#include <dc1394/control.h>

G_BEGIN_DECLS

#define GST_TYPE_DC1394 \
  (gst_dc1394_get_type())
#define GST_DC1394(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DC1394,GstDc1394))
#define GST_DC1394_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DC1394,GstDc1394))
#define GST_IS_DC1394(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DC1394))
#define GST_IS_DC1394_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DC1394))

typedef struct _GstDc1394 GstDc1394;
typedef struct _GstDc1394Class GstDc1394Class;

struct _GstDc1394 {
  GstPushSrc element;

  /* video state */
  gint width;
  gint height;
  gint vmode; 

  gint bpp;
  gint rate_numerator;
  gint rate_denominator;

  /* private */
  gint64 timestamp_offset;		/* base offset */
  GstClockTime running_time;		/* total running time */
  gint64 n_frames;			/* total frames sent */
  gint64 segment_start_frame;
  gint64 segment_end_frame;
  gboolean segment;
  gint camnum; 
  gint bufsize; 
  gint iso_speed;

  dc1394_t * dc1394;
  dc1394camera_t * camera; 

  GstCaps *caps;
};

struct _GstDc1394Class {
  GstPushSrcClass parent_class;
};

GType gst_dc1394_get_type (void);

G_END_DECLS

#endif /* __GST_DC1394_H__ */
