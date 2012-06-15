/* GStreamer Jasper based j2k image encoder
 * Copyright (C) 2008 Mark Nauwelaerts <mnauw@users.sf.net>
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

#ifndef __GST_JASPER_ENC_H__
#define __GST_JASPER_ENC_H__

#include <gst/gst.h>
#include <gst/video/video.h>

#include <jasper/jasper.h>

G_BEGIN_DECLS

/* #define's don't like whitespacey bits */
#define GST_TYPE_JASPER_ENC			\
  (gst_jasper_enc_get_type())
#define GST_JASPER_ENC(obj)						\
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_JASPER_ENC,GstJasperEnc))
#define GST_JASPER_ENC_CLASS(klass)					\
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_JASPER_ENC,GstJasperEncClass))
#define GST_IS_JASPER_ENC(obj)					\
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_JASPER_ENC))
#define GST_IS_JASPER_ENC_CLASS(klass)				\
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_JASPER_ENC))

typedef struct _GstJasperEnc      GstJasperEnc;
typedef struct _GstJasperEncClass GstJasperEncClass;

enum {
  GST_JP2ENC_MODE_J2C = 0,
  GST_JP2ENC_MODE_JPC,
  GST_JP2ENC_MODE_JP2
};

#define GST_JASPER_ENC_MAX_COMPONENT  4

struct _GstJasperEnc
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  jas_image_t *image;
  glong *buf;

  /* jasper image fmt */
  gint fmt;
  gint mode;
  jas_clrspc_t clrspc;

  /* stream/image properties */
  GstVideoFormat format;
  gint width;
  gint height;
  gint channels;
  gint fps_num, fps_den;
  gint par_num, par_den;
  /* standard video_format indexed */
  gint stride[GST_JASPER_ENC_MAX_COMPONENT];
  gint offset[GST_JASPER_ENC_MAX_COMPONENT];
  gint inc[GST_JASPER_ENC_MAX_COMPONENT];
  gint cwidth[GST_JASPER_ENC_MAX_COMPONENT];
  gint cheight[GST_JASPER_ENC_MAX_COMPONENT];
};

struct _GstJasperEncClass
{
  GstElementClass parent_class;
};

GType gst_jasper_enc_get_type (void);

G_END_DECLS

#endif /* __GST_JASPER_ENC_H__ */
