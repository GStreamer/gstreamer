/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstfilesrc.h: 
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


#ifndef __GST_FILESRC_H__
#define __GST_FILESRC_H__


#include <gst/gst.h>
#include <sys/types.h>

G_BEGIN_DECLS


#define GST_TYPE_FILESRC \
  (gst_filesrc_get_type())
#define GST_FILESRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_FILESRC,GstFileSrc))
#define GST_FILESRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_FILESRC,GstFileSrcClass)) 
#define GST_IS_FILESRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_FILESRC))
#define GST_IS_FILESRC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_FILESRC))

typedef enum {
  GST_FILESRC_OPEN              = GST_ELEMENT_FLAG_LAST,

  GST_FILESRC_FLAG_LAST = GST_ELEMENT_FLAG_LAST + 2
} GstFileSrcFlags;

typedef struct _GstFileSrc GstFileSrc;
typedef struct _GstFileSrcClass GstFileSrcClass;

struct _GstFileSrc {
  GstElement element;
  GstPad *srcpad;

  guint pagesize;			/* system page size*/
 
  gchar *filename;			/* filename */
  gchar *uri;				/* caching the URI */
  gint fd;				/* open file descriptor*/
  off_t filelen;			/* what's the file length?*/

  off_t curoffset;			/* current offset in file*/
  off_t block_size;			/* bytes per read */
  gboolean touch;			/* whether to touch every page */
  gboolean using_mmap;                  /* whether we opened it with mmap */
  gboolean is_regular;                  /* whether it's (symlink to)
                                           a regular file */

  GstBuffer *mapbuf;
  size_t mapsize;

  gint need_discont;
  gboolean need_flush;
};

struct _GstFileSrcClass {
  GstElementClass parent_class;
};

GType gst_filesrc_get_type(void);

G_END_DECLS

#endif /* __GST_FILESRC_H__ */
