/* -*- c-basic-offset: 2 -*-
 * vi:si:et:sw=2:sts=8:ts=8:expandtab
 *
 * GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

#ifndef __GST_VOLUME_H__
#define __GST_VOLUME_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>

G_BEGIN_DECLS

#define GST_TYPE_VOLUME \
  (gst_volume_get_type())
#define GST_VOLUME(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VOLUME,GstVolume))
#define GST_VOLUME_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VOLUME,GstVolumeClass))
#define GST_IS_VOLUME(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VOLUME))
#define GST_IS_VOLUME_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VOLUME))

typedef struct _GstVolume GstVolume;
typedef struct _GstVolumeClass GstVolumeClass;

typedef enum {
  GST_VOLUME_FORMAT_NONE,
  GST_VOLUME_FORMAT_INT, 	 
  GST_VOLUME_FORMAT_FLOAT 	 
} GstVolumeFormat;

/**
 * GstVolume:
 *
 * Opaque data structure.
 */
struct _GstVolume {
  GstBaseTransform element;

  void (*process)(GstVolume*, gpointer, guint);

  gboolean mute;
  gint   volume_i32, real_vol_i32;
  gint   volume_i24, real_vol_i24; /* the _i(nt) values get synchronized with the */
  gint   volume_i16, real_vol_i16; /* the _i(nt) values get synchronized with the */
  gint   volume_i8, real_vol_i8;   /* the _i(nt) values get synchronized with the */
  gfloat volume_f, real_vol_f; /* _f(loat) values on each update */
  
  GList *tracklist;
  GstVolumeFormat format;       /* caps variables */
  gint width;
  gboolean silent_buffer;       /* flag for silent buffers */
};

struct _GstVolumeClass {
  GstBaseTransformClass parent_class;
};

GType gst_volume_get_type (void);

G_END_DECLS

#endif /* __GST_VOLUME_H__ */
