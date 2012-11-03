/* GStreamer
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */


#ifndef __VCDSRC_H__
#define __VCDSRC_H__

#include <linux/cdrom.h>

#include <gst/gst.h>
#include <gst/base/gstpushsrc.h>

G_BEGIN_DECLS

#define VCD_BYTES_PER_SECTOR 2352

#define GST_TYPE_VCDSRC \
  (gst_vcdsrc_get_type())
#define GST_VCDSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VCDSRC,GstVCDSrc))
#define GST_VCDSRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VCDSRC,GstVCDSrcClass))
#define GST_IS_VCDSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VCDSRC))
#define GST_IS_VCDSRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VCDSRC))

typedef struct _GstVCDSrc GstVCDSrc;
typedef struct _GstVCDSrcClass GstVCDSrcClass;

struct _GstVCDSrc {
  GstPushSrc parent_object;

  /* device */
  gchar *device;
  /* track number */
  gint track;
  int max_errors;

  /* fd */
  gint fd;
  gint numtracks;
  struct cdrom_tochdr tochdr;
  struct cdrom_tocentry *tracks;

  /* current time offset */
  gulong trackoffset;
  gulong curoffset;                     /* current offset in file */
  gulong bytes_per_read;                /* bytes per read */
};

struct _GstVCDSrcClass {
  GstPushSrcClass parent_class;
};

GType gst_vcdsrc_get_type (void);

G_END_DECLS

#endif /* __VCDSRC_H__ */
