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


#ifndef __GST_GST1394_H__
#define __GST_GST1394_H__


#include <gst/gst.h>
#include <libraw1394/raw1394.h>


#ifdef __cplusplus
extern "C"
{
#endif				/* __cplusplus */


#define GST_TYPE_DV1394SRC \
  (gst_dv1394src_get_type())
#define GST_DV1394SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DV1394SRC,GstDV1394Src))
#define GST_DV1394SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DV1394SRC,GstDV1394Src))
#define GST_IS_DV1394SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DV1394SRC))
#define GST_IS_DV1394SRC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DV1394SRC))

  typedef struct _GstDV1394Src GstDV1394Src;
  typedef struct _GstDV1394SrcClass GstDV1394SrcClass;

  struct _GstDV1394Src
  {
    GstElement element;

    GstPad *srcpad;

    // consecutive=2, skip=4 will skip 4 frames, then let 2 consecutive ones thru
    gint consecutive;
    gint skip;
    gboolean drop_incomplete;

    int numcards, numports;
    int card, port, channel;

    struct raw1394_portinfo pinfo[16];
    raw1394handle_t handle;

    gboolean started;
    GstBuffer *buf;

    GstBuffer *frame;
    guint frameSize;
    guint bytesInFrame;
    guint frameSequence;

    gboolean negotiated;
  };

  struct _GstDV1394SrcClass
  {
    GstElementClass parent_class;
  };

  GType gst_dv1394src_get_type (void);

#ifdef __cplusplus
}
#endif				/* __cplusplus */


#endif				/* __GST_GST1394_H__ */
