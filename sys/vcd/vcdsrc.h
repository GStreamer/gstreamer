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


#ifndef __VCDSRC_H__
#define __VCDSRC_H__


#include <gst/gst.h>

#include <linux/cdrom.h>


#ifdef __cplusplus
extern "C"
{
#endif				/* __cplusplus */


#define VCD_BYTES_PER_SECTOR 2352

#define GST_TYPE_VCDSRC \
  (vcdsrc_get_type())
#define VCDSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VCDSRC,VCDSrc))
#define VCDSRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VCDSRC,VCDSrcClass))
#define GST_IS_VCDSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VCDSRC))
#define GST_IS_VCDSRC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VCDSRC))

/* NOTE: per-element flags start with 16 for now */
  typedef enum
  {
    VCDSRC_OPEN = GST_ELEMENT_FLAG_LAST,

    VCDSRC_FLAG_LAST = GST_ELEMENT_FLAG_LAST + 2,
  } VCDSrcFlags;

  typedef struct _VCDSrc VCDSrc;
  typedef struct _VCDSrcClass VCDSrcClass;

  struct _VCDSrc
  {
    GstElement element;
    /* pads */
    GstPad *srcpad;

    /* device */
    gchar *device;
    /* track number */
    gint track;
    /* fd */
    gint fd;

    struct cdrom_tochdr tochdr;
    gint numtracks;
    struct cdrom_tocentry *tracks;

    /* current time offset */
    gulong trackoffset;
    gulong frameoffset;

    gulong curoffset;		/* current offset in file */
    gulong bytes_per_read;	/* bytes per read */

    gulong seq;			/* buffer sequence number */
    int max_errors;
  };

  struct _VCDSrcClass
  {
    GstElementClass parent_class;
  };

  GType vcdsrc_get_type (void);

#ifdef __cplusplus
}
#endif				/* __cplusplus */


#endif				/* __VCDSRC_H__ */
