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


#ifndef __GST_DISKSRC_H__
#define __GST_DISKSRC_H__


#include <config.h>
#include <gst/gst.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


GstElementDetails gst_disksrc_details;


#define GST_TYPE_DISKSRC \
  (gst_disksrc_get_type())
#define GST_DISKSRC(obj) \
  (GTK_CHECK_CAST((obj),GST_TYPE_DISKSRC,GstDiskSrc))
#define GST_DISKSRC_CLASS(klass) \
  (GTK_CHECK_CLASS_CAST((klass),GST_TYPE_DISKSRC,GstDiskSrcClass))
#define GST_IS_DISKSRC(obj) \
  (GTK_CHECK_TYPE((obj),GST_TYPE_DISKSRC))
#define GST_IS_DISKSRC_CLASS(obj) \
  (GTK_CHECK_CLASS_TYPE((klass),GST_TYPE_DISKSRC)))

// NOTE: per-element flags start with 16 for now
typedef enum {
  GST_DISKSRC_OPEN		= (1 << 16),
} GstDiskSrcFlags;

typedef struct _GstDiskSrc GstDiskSrc;
typedef struct _GstDiskSrcClass GstDiskSrcClass;

struct _GstDiskSrc {
  GstSrc src;
  /* pads */
  GstPad *srcpad;

  /* filename */
  gchar *filename;
  /* fd */
  gint fd;

  gulong curoffset;			/* current offset in file */
  gulong bytes_per_read;		/* bytes per read */
  gboolean new_seek;

  gulong seq;				/* buffer sequence number */
  gulong size;				
};

struct _GstDiskSrcClass {
  GstSrcClass parent_class;
};

GtkType gst_disksrc_get_type(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_DISKSRC_H__ */
