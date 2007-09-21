/* GStreamer
 *
 * Copyright (C) 2007 Rene Stadler <mail@renestadler.de>
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

#ifndef __GSTGIOSRC_H__
#define __GSTGIOSRC_H__

#include "gstgio.h"

#include <gio/gfile.h>
#include <gst/base/gstbasesrc.h>

G_BEGIN_DECLS

#define GST_TYPE_GIO_SRC \
  (gst_gio_src_get_type())
#define GST_GIO_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GIO_SRC,GstGioSrc))
#define GST_GIO_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_GIO_SRC,GstGioSrcClass))
#define GST_IS_GIO_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_GIO_SRC))
#define GST_IS_GIO_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_GIO_SRC))

typedef struct _GstGioSrc      GstGioSrc;
typedef struct _GstGioSrcClass GstGioSrcClass;

struct _GstGioSrc
{
  GstBaseSrc src;

  GCancellable *cancel;
  GFile *file;
  gchar *location;
  guint64 position;
  GFileInputStream *stream;
};

struct _GstGioSrcClass 
{
  GstBaseSrcClass parent_class;
};

GType gst_gio_src_get_type (void);

G_END_DECLS

#endif /* __GSTGIOSRC_H__ */
