/* GStreamer
 * Copyright (C) 2002 David I. Lehn <dlehn@users.sourceforge.net>
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


#ifndef __DVDNAVSRC_H__
#define __DVDNAVSRC_H__


#include <config.h>
#include <gst/gst.h>


G_BEGIN_DECLS

GstElementDetails dvdnavsrc_details;


#define GST_TYPE_DVDNAVSRC \
  (dvdnavsrc_get_type())
#define DVDNAVSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DVDNAVSRC,DVDNavSrc))
#define DVDNAVSRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DVDNAVSRC,DVDNavSrcClass))
#define GST_IS_DVDNAVSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DVDNAVSRC))
#define GST_IS_DVDNAVSRC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DVDNAVSRC))

/* NOTE: per-element flags start with 16 for now */
typedef enum {
  DVDNAVSRC_OPEN		= GST_ELEMENT_FLAG_LAST,

  DVDNAVSRC_FLAG_LAST	= GST_ELEMENT_FLAG_LAST+2,
} DVDNavSrcFlags;

typedef struct _DVDNavSrc DVDNavSrc;
typedef struct _DVDNavSrcPrivate DVDNavSrcPrivate;
typedef struct _DVDNavSrcClass DVDNavSrcClass;

struct _DVDNavSrc {
  GstElement element;
  DVDNavSrcPrivate *priv;
};

struct _DVDNavSrcClass {
  GstElementClass parent_class;
};

GType dvdnavsrc_get_type(void);

G_END_DECLS

#endif /* __DVDNAVSRC_H__ */
