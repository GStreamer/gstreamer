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


#ifndef __GST_MNG_ENC_H__
#define __GST_MNG_ENC_H__

#include <gst/gst.h>
#include <libmng.h>

G_BEGIN_DECLS

#define GST_TYPE_MNG_ENC            (gst_mng_enc_get_type())
#define GST_MNG_ENC(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MNG_ENC,GstMngEnc))
#define GST_MNG_ENC_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MNG_ENC,GstMngEncClass))
#define GST_IS_MNG_ENC(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MNG_ENC))
#define GST_IS_MNG_ENC_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MNG_ENC))
#define GST_MNG_ENC_CAST(obj)       (GstMngEnc *)(obj)

typedef struct _GstMngEnc GstMngEnc;
typedef struct _GstMngEncClass GstMngEncClass;

extern GstPadTemplate *mngenc_src_template, *mngenc_sink_template;

struct _GstMngEnc
{
  GstElement element;

  GstPad *sinkpad, *srcpad;
  GstBuffer *buffer_out;

  mng_handle mng;

  gint width;
  gint height;
  gint bpp;
};

struct _GstMngEncClass
{
  GstElementClass parent_class;
};

GType gst_mng_enc_get_type(void);

G_END_DECLS

#endif /* __GST_MNG_ENC_H__ */
