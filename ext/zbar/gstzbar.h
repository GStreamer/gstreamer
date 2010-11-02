/* GStreamer
 * Copyright (C) 2009 Stefan Kost <ensonic@users.sf.net>
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


#ifndef __GST_VIDEO_ZBAR_H__
#define __GST_VIDEO_ZBAR_H__

#include <gst/video/gstvideofilter.h>
#include <gst/video/video.h>
#include <zbar.h>

G_BEGIN_DECLS

#define GST_TYPE_ZBAR \
  (gst_zbar_get_type())
#define GST_ZBAR(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ZBAR,GstZBar))
#define GST_ZBAR_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ZBAR,GstZBarClass))
#define GST_IS_ZBAR(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ZBAR))
#define GST_IS_ZBAR_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_ZBAR))

typedef struct _GstZBar GstZBar;
typedef struct _GstZBarClass GstZBarClass;

/**
 * GstZBar:
 *
 * Opaque data structure.
 */
struct _GstZBar
{
  /*< private >*/
  GstVideoFilter videofilter;

  /* format */
  gint           width;
  gint           height;

  GstVideoFormat format;

  /* properties */
  gboolean message;
  gboolean cache;
  
  /* internals */
  zbar_image_scanner_t *scanner;
};

struct _GstZBarClass
{
  GstVideoFilterClass parent_class;
};

GType gst_zbar_get_type(void);

G_END_DECLS

#endif /* __GST_VIDEO_ZBAR_H__ */
