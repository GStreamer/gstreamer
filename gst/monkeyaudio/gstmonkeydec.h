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


#ifndef __GST_MONKEYDEC_H__
#define __GST_MONKEYDEC_H__

#include "libmonkeyaudio/All.h"
#include "libmonkeyaudio/GlobalFunctions.h"
#include "libmonkeyaudio/MACLib.h"
#include "libmonkeyaudio/IO.h"
#include "libmonkeyaudio/APETag.h"

#include <gst/gst.h>

#include "monkey_io.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define GST_TYPE_MONKEYDEC  	(gst_monkeydec_get_type())
#define GST_MONKEYDEC(obj)  	(G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MONKEYDEC,GstMonkeyDec))
#define GST_MONKEYDEC_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MONKEYDEC,GstMonkeyDec))
#define GST_IS_MONKEYDEC(obj) 	(G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MONKEYDEC))
#define GST_IS_MONKEYDEC_CLASS(obj)(G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MONKEYDEC))

typedef struct _GstMonkeyDec GstMonkeyDec;
typedef struct _GstMonkeyDecClass GstMonkeyDecClass;

extern GstPadTemplate *monkeydec_src_template, *monkeydec_sink_template;

struct _GstMonkeyDec
{
  GstElement element;

  GstPad *sinkpad, *srcpad;
  gboolean init;
  guint64 total_samples;
  guint64 seek_to;
  guint channels;
  guint frequency;
  guint depth;
	GstCaps *metadata;
	
  IAPEDecompress *decomp;
	  
  sinkpad_CIO *io;
};

struct _GstMonkeyDecClass
{
  GstElementClass parent_class;
};

GType gst_monkeydec_get_type(void);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __GST_MONKEYDEC_H__ */
