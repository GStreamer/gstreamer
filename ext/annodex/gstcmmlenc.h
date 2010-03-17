/*
 * gstcmmlenc.h - GStreamer CMML encoder
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

#ifndef __GST_CMML_ENC_H__
#define __GST_CMML_ENC_H__

#define GST_TYPE_CMML_ENC (gst_cmml_enc_get_type())
#define GST_CMML_ENC(obj) \
      (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_CMML_ENC, GstCmmlEnc))
#define GST_CMML_ENC_CLASS(klass) \
      (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_CMML_ENC, GstCmmlEncClass))
#define GST_IS_CMML_ENC(obj) \
      (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_CMML_ENC))
#define GST_IS_CMML_ENC_CLASS(klass) \
      (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_CMML_ENC))
#define GST_CMML_ENC_GET_CLASS(obj) \
      (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_CMML_ENC, GstCmmlEncClass))

#include <glib.h>
#include <gst/gst.h>

#include "gstcmmlparser.h"
#include "gstcmmlutils.h"

typedef struct _GstCmmlEnc GstCmmlEnc;
typedef struct _GstCmmlEncClass GstCmmlEncClass;

struct _GstCmmlEnc
{
  GstElement element;

  GstPad *sinkpad;
  GstPad *srcpad;

  gint16 major;
  gint16 minor;
  gint64 granulerate_n;
  gint64 granulerate_d;
  gint8 granuleshift;
  
  GstCmmlParser *parser;
  gboolean streaming;
  GHashTable *tracks;
  GstFlowReturn flow_return;
  guchar *preamble;
  gboolean sent_headers;
  gboolean sent_eos;
};

struct _GstCmmlEncClass
{
  GstElementClass parent_class;
};

GType gst_cmml_enc_get_type (void);

gboolean gst_cmml_enc_plugin_init (GstPlugin * plugin);

#endif /* __GST_CMML_ENC_H__ */
