/*
 * gstskeldec.h - GStreamer annodex skeleton decoder
 * Copyright (C) 2005 Alessandro Decina
 * 
 * Authors:
 *   Alessandro Decina <alessandro@nnva.org>
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
#ifndef __GST_SKEL_DEC_H__
#define __GST_SKEL_DEC_H__

#include <gst/gst.h>

/* GstSkelDec */
#define GST_TYPE_SKEL_DEC (gst_skel_dec_get_type ())
#define GST_SKEL_DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_SKEL_DEC, GstSkelDec))
#define GST_SKEL_DEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_SKEL_DEC, GstSkelDec))
#define GST_SKEL_DEC_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_SKEL_DEC, GstSkelDecClass))

typedef struct _GstSkelDec GstSkelDec;
typedef struct _GstSkelDecClass GstSkelDecClass;

#define GST_SKEL_OGG_FISHEAD_SIZE 64
#define UTC_LEN 20

struct _GstSkelDec
{
  GstElement element;

  GstPad *sinkpad;
  GstPad *srcpad;

  gint16 major;                 /* skeleton version major */
  gint16 minor;                 /* skeleton version minor */
};

struct _GstSkelDecClass
{
  GstElementClass parent_class;
};

GType gst_skel_dec_get_type (void);
gboolean gst_skel_dec_plugin_init (GstPlugin * plugin);

#endif /* __GST_SKEL_DEC_H__ */
