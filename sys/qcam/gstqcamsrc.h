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


#ifndef __GST_QCAMSRC_H__
#define __GST_QCAMSRC_H__


#include <gst/gst.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** QuickCam include files */
#include "qcam.h"
#include "qcam-os.h"

#define GST_TYPE_QCAMSRC \
  (gst_qcamsrc_get_type())
#define GST_QCAMSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_QCAMSRC,GstQCamSrc))
#define GST_QCAMSRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_QCAMSRC,GstQCamSrcClass))
#define GST_IS_QCAMSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_QCAMSRC))
#define GST_IS_QCAMSRC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_QCAMSRC))

/* NOTE: per-element flags start with 16 for now */
typedef enum {
  GST_QCAMSRC_OPEN            = GST_ELEMENT_FLAG_LAST,

  GST_QCAMSRC_FLAG_LAST       = GST_ELEMENT_FLAG_LAST+2,
} GstQCamSrcFlags;

typedef struct _GstQCamSrc GstQCamSrc;
typedef struct _GstQCamSrcClass GstQCamSrcClass;

struct _GstQCamSrc {
  GstElement element;

  /* pads */
  GstPad *srcpad;

  struct qcam *qcam;
  gboolean autoexposure;
  gint port;
};

struct _GstQCamSrcClass {
  GstElementClass parent_class;
};

GType gst_qcamsrc_get_type(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_QCAMSRC_H__ */
