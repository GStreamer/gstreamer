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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __DVDREADSRC_H__
#define __DVDREADSRC_H__

#include <gst/gst.h>

G_BEGIN_DECLS

GstElementDetails dvdreadsrc_details;

#define GST_TYPE_DVDREADSRC \
  (dvdreadsrc_get_type())
#define DVDREADSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DVDREADSRC,DVDReadSrc))
#define DVDREADSRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DVDREADSRC,DVDReadSrcClass))
#define GST_IS_DVDREADSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DVDREADSRC))
#define GST_IS_DVDREADSRC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DVDREADSRC))

/* NOTE: per-element flags start with 16 for now */
typedef enum {
  DVDREADSRC_OPEN		= (GST_ELEMENT_FLAG_LAST << 0),

  DVDREADSRC_FLAG_LAST          = (GST_ELEMENT_FLAG_LAST << 2)
} DVDReadSrcFlags;

typedef struct _DVDReadSrc DVDReadSrc;
typedef struct _DVDReadSrcPrivate DVDReadSrcPrivate;
typedef struct _DVDReadSrcClass DVDReadSrcClass;

struct _DVDReadSrc {
  GstElement element;
  DVDReadSrcPrivate *priv;
};

struct _DVDReadSrcClass {
  GstElementClass parent_class;
};

GType dvdreadsrc_get_type (void);

G_END_DECLS

#endif /* __DVDREADSRC_H__ */
