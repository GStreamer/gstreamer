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


#ifndef __GST_FDSRC_H__
#define __GST_FDSRC_H__


#include <config.h>
#include <gst/gst.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


GstElementDetails gst_fdsrc_details;


#define GST_TYPE_FDSRC \
  (gst_fdsrc_get_type())
#define GST_FDSRC(obj) \
  (GTK_CHECK_CAST((obj),GST_TYPE_FDSRC,GstFdSrc))
#define GST_FDSRC_CLASS(klass) \
  (GTK_CHECK_CLASS_CAST((klass),GST_TYPE_FDSRC,GstFdSrcClass))
#define GST_IS_FDSRC(obj) \
  (GTK_CHECK_TYPE((obj),GST_TYPE_FDSRC))
#define GST_IS_FDSRC_CLASS(obj) \
  (GTK_CHECK_CLASS_TYPE((klass),GST_TYPE_FDSRC))


typedef struct _GstFdSrc GstFdSrc;
typedef struct _GstFdSrcClass GstFdSrcClass;

struct _GstFdSrc {
  GstSrc src;
  /* pads */
  GstPad *srcpad;

  /* fd */
  gint fd;

  gulong curoffset;			/* current offset in file */
  gulong bytes_per_read;		/* bytes per read */

  gulong seq;				/* buffer sequence number */
};

struct _GstFdSrcClass {
  GstSrcClass parent_class;
};

GtkType gst_fdsrc_get_type(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_FDSRC_H__ */
