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


#ifndef __TARKINDEC_H__
#define __TARKINDEC_H__


#include <gst/gst.h>

#include "tarkin.h"

#ifdef __cplusplus
extern "C"
{
#endif				/* __cplusplus */

#define GST_TYPE_TARKINDEC \
  (tarkindec_get_type())
#define GST_TARKINDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_TARKINDEC,TarkinDec))
#define GST_TARKINDEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_TARKINDEC,TarkinDecClass))
#define GST_IS_TARKINDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_TARKINDEC))
#define GST_IS_TARKINDEC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_TARKINDEC))

  typedef struct _TarkinDec TarkinDec;
  typedef struct _TarkinDecClass TarkinDecClass;

  struct _TarkinDec
  {
    GstElement element;

    GstPad *sinkpad, *srcpad;

    ogg_sync_state oy;
    ogg_stream_state os;
    ogg_page og;
    ogg_packet op;

    TarkinStream *tarkin_stream;
    TarkinComment tc;
    TarkinInfo ti;
    TarkinVideoLayerDesc layer[1];

    gint frame_num;
    gint nheader;

    gboolean eos;
    gint bitrate;
    gboolean setup;
  };

  struct _TarkinDecClass
  {
    GstElementClass parent_class;
  };

  GType tarkindec_get_type (void);


#ifdef __cplusplus
}
#endif				/* __cplusplus */


#endif				/* __TARKINDEC_H__ */
