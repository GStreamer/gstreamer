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


#ifndef __TARKINENC_H__
#define __TARKINENC_H__


#include <gst/gst.h>

#include "tarkin.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define GST_TYPE_TARKINENC \
  (tarkinenc_get_type())
#define GST_TARKINENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_TARKINENC,TarkinEnc))
#define GST_TARKINENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_TARKINENC,TarkinEncClass))
#define GST_IS_TARKINENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_TARKINENC))
#define GST_IS_TARKINENC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_TARKINENC))

typedef struct _TarkinEnc TarkinEnc;
typedef struct _TarkinEncClass TarkinEncClass;

struct _TarkinEnc {
  GstElement element;

  GstPad *sinkpad,*srcpad;

  ogg_stream_state	 os; /* take physical pages, weld into a logical
			                              stream of packets */
  ogg_page        	 og; /* one Ogg bitstream page.  Tarkin packets are inside */
  ogg_packet       	 op[3]; /* one raw packet of data for decode */

  TarkinStream 		*tarkin_stream;
  TarkinComment 	 tc;
  TarkinInfo 		 ti;
  TarkinVideoLayerDesc 	 layer[1];

  gint 			 frame_num;
        
  gboolean eos;
  gint bitrate;
  gint s_moments;
  gint a_moments;
  gboolean setup;
};

struct _TarkinEncClass {
  GstElementClass parent_class;
};

GType tarkinenc_get_type(void);


#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __TARKINENC_H__ */
