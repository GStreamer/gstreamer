/* GStreamer
 * Copyright (C) 2010 FIXME <fixme@example.com>
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

#ifndef _GST_LINSYS_SDI_SRC_H_
#define _GST_LINSYS_SDI_SRC_H_

#include <gst/gst.h>
#include <gst/base/gstbasesrc.h>

G_BEGIN_DECLS

#define GST_TYPE_LINSYS_SDI_SRC   (gst_linsys_sdi_src_get_type())
#define GST_LINSYS_SDI_SRC(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_LINSYS_SDI_SRC,GstLinsysSdiSrc))
#define GST_LINSYS_SDI_SRC_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_LINSYS_SDI_SRC,GstLinsysSdiSrcClass))
#define GST_IS_LINSYS_SDI_SRC(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_LINSYS_SDI_SRC))
#define GST_IS_LINSYS_SDI_SRC_CLASS(obj)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_LINSYS_SDI_SRC))

typedef struct _GstLinsysSdiSrc GstLinsysSdiSrc;
typedef struct _GstLinsysSdiSrcClass GstLinsysSdiSrcClass;

struct _GstLinsysSdiSrc
{
  GstBaseSrc base_linsyssdisrc;

  /* properties */
  gchar *device;
  gboolean is_625;

  /* state */
  int fd;
  guint8 *tmpdata;
  gboolean have_sync;
  gboolean have_vblank;

};

struct _GstLinsysSdiSrcClass
{
  GstBaseSrcClass base_linsyssdisrc_class;
};

GType gst_linsys_sdi_src_get_type (void);

G_END_DECLS

#endif
