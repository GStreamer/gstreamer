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


#ifndef __GST_MONKEYENC_H__
#define __GST_MONKEYENC_H__

#include "libmonkeyaudio/All.h"
#include "libmonkeyaudio/GlobalFunctions.h"
#include "libmonkeyaudio/MACLib.h"
#include "libmonkeyaudio/IO.h"
#include "monkey_io.h"

#include <gst/gst.h>

#include "libmonkeyaudio/WAVInputSource.h"
#include "libmonkeyaudio/NoWindows.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */



#define GST_TYPE_MONKEYENC  	(gst_monkeyenc_get_type())
#define GST_MONKEYENC(obj)  	(G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MONKEYENC,GstMonkeyEnc))
#define GST_MONKEYENC_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MONKEYENC,GstMonkeyEnc))
#define GST_IS_MONKEYENC(obj) 	(G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MONKEYENC))
#define GST_IS_MONKEYENC_CLASS(obj)(G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MONKEYENC))

typedef struct _GstMonkeyEnc GstMonkeyEnc;
typedef struct _GstMonkeyEncClass GstMonkeyEncClass;

extern GstPadTemplate *monkeyenc_src_template, *monkeyenc_sink_template;

struct _GstMonkeyEnc
{
  GstElement element;

  GstPad *sinkpad, *srcpad; 
  gboolean init;
  gint channels;
  gint rate;
  gint depth;
  gboolean linked;
  gint total_blocks;
  gint header_size;
  gint terminating;
  guint64 audiobytes;
  guint64 audiobytesleft;

  IAPECompress *compress_engine;
  WAVEFORMATEX waveformatex;
  WAVE_HEADER pWAVHeader;
  srcpad_CIO *src_io;
  sinkpad_CIO *sink_io;
  CWAVInputSource *inputsrc;

};

struct _GstMonkeyEncClass
{
  GstElementClass parent_class;
};

GType gst_monkeyenc_get_type(void);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __GST_MONKEYENC_H__ */

