/* Gnome-Streamer
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


#ifndef __DVDSRC_H__
#define __DVDSRC_H__


#include <config.h>
#include <gst/gst.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


GstElementDetails dvdsrc_details;


#define GST_TYPE_DVDSRC \
  (dvdsrc_get_type())
#define DVDSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DVDSRC,DVDSrc))
#define DVDSRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DVDSRC,DVDSrcClass))
#define GST_IS_DVDSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DVDSRC))
#define GST_IS_DVDSRC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DVDSRC))

// NOTE: per-element flags start with 16 for now
typedef enum {
  DVDSRC_OPEN		= GST_ELEMENT_FLAG_LAST,

  DVDSRC_FLAG_LAST	= GST_ELEMENT_FLAG_LAST+2,
} DVDSrcFlags;

typedef struct _DVDSrc DVDSrc;
typedef struct _DVDSrcPrivate DVDSrcPrivate;
typedef struct _DVDSrcClass DVDSrcClass;

struct _DVDSrc {
  GstElement element;
  DVDSrcPrivate *priv;
};

struct _DVDSrcClass {
  GstElementClass parent_class;
};

GType dvdsrc_get_type(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __DVDSRC_H__ */
