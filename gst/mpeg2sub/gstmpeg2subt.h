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


#ifndef __GST_MPEG2SUBT_H__
#define __GST_MPEG2SUBT_H__


#include <gst/gst.h>


#ifdef __cplusplus
extern "C"
{
#endif				/* __cplusplus */


#define GST_TYPE_MPEG2SUBT \
  (gst_mpeg2subt_get_type())
#define GST_MPEG2SUBT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MPEG2SUBT,GstMpeg2Subt))
#define GST_MPEG2SUBT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MPEG2SUBT,GstMpeg2Subt))
#define GST_IS_MPEG2SUBT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MPEG2SUBT))
#define GST_IS_MPEG2SUBT_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MPEG2SUBT))

  typedef struct _GstMpeg2Subt GstMpeg2Subt;
  typedef struct _GstMpeg2SubtClass GstMpeg2SubtClass;

  struct _GstMpeg2Subt
  {
    GstElement element;

    GstPad *videopad, *subtitlepad, *srcpad;

    GstBuffer *partialbuf;	/* previous buffer (if carryover) */

    gboolean have_title;

    guint16 packet_size;
    guint16 data_size;

    gint offset[2];
    guchar color[5];
    guchar trans[4];

    guint duration;

    gint width, height;

  };

  struct _GstMpeg2SubtClass
  {
    GstElementClass parent_class;
  };

  GType gst_mpeg2subt_get_type (void);


#ifdef __cplusplus
}
#endif				/* __cplusplus */


#endif				/* __GST_MPEG2SUBT_H__ */
