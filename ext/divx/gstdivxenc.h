/* GStreamer divx encoder plugin
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
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

#ifndef __GST_DIVXENC_H__
#define __GST_DIVXENC_H__

#include <gst/gst.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define GST_TYPE_DIVXENC \
  (gst_divxenc_get_type())
#define GST_DIVXENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_DIVXENC, GstDivxEnc))
#define GST_DIVXENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_DIVXENC, GstDivxEnc))
#define GST_IS_DIVXENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_DIVXENC))
#define GST_IS_DIVXENC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_DIVXENC))

typedef struct _GstDivxEnc GstDivxEnc;
typedef struct _GstDivxEncClass GstDivxEncClass;

struct _GstDivxEnc {
  GstElement element;

  /* pads */
  GstPad *sinkpad, *srcpad;

  /* quality of encoded image */
  gulong bitrate;

  /* size of the buffers */
  gulong buffer_size;

  /* max key interval */
  gint max_key_interval;

  /* amount of motion estimation to do */
  gint quality;

  /* divx handle */
  void *handle;
  guint32 csp;
  gint bitcnt; 
  gint width, height;
  gfloat fps;
};

struct _GstDivxEncClass {
  GstElementClass parent_class;

  /* signals */
  void (*frame_encoded) (GstElement *element);
};

GType gst_divxenc_get_type(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __GST_DIVXENC_H__ */
