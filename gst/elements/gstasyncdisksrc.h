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


#ifndef __GST_ASYNCDISKSRC_H__
#define __GST_ASYNCDISKSRC_H__


#include <config.h>
#include <gst/gst.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


GstElementDetails gst_asyncdisksrc_details;


#define GST_TYPE_ASYNCDISKSRC \
  (gst_asyncdisksrc_get_type())
#define GST_ASYNCDISKSRC(obj) \
  (GTK_CHECK_CAST((obj),GST_TYPE_ASYNCDISKSRC,GstAsyncDiskSrc))
#define GST_ASYNCDISKSRC_CLASS(klass) \
  (GTK_CHECK_CLASS_CAST((klass),GST_TYPE_ASYNCDISKSRC,GstAsyncDiskSrcClass))
#define GST_IS_ASYNCDISKSRC(obj) \
  (GTK_CHECK_TYPE((obj),GST_TYPE_ASYNCDISKSRC))
#define GST_IS_ASYNCDISKSRC_CLASS(obj) \
  (GTK_CHECK_CLASS_TYPE((klass),GST_TYPE_ASYNCDISKSRC))

// NOTE: per-element flags start with 16 for now
typedef enum {
  GST_ASYNCDISKSRC_OPEN	= (1 << 16),
} GstAsyncDiskSrcFlags;

typedef struct _GstAsyncDiskSrc GstAsyncDiskSrc;
typedef struct _GstAsyncDiskSrcClass GstAsyncDiskSrcClass;

struct _GstAsyncDiskSrc {
  GstSrc src;
  /* pads */
  GstPad *srcpad;

  /* filename */
  gchar *filename;
  /* fd */
  gint fd;

  /* mapping parameters */
  gulong size;				/* how long is the file? */
  guchar *map;				/* where the file is mapped to */

  /* details for fallback synchronous read */
  gulong curoffset;			/* current offset in file */
  gulong bytes_per_read;		/* bytes per read */

  gulong seq;				/* buffer sequence number */
};

struct _GstAsyncDiskSrcClass {
  GstSrcClass parent_class;
};

GtkType gst_asyncdisksrc_get_type(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_ASYNCDISKSRC_H__ */
