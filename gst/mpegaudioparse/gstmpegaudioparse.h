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


#ifndef __MP3PARSE_H__
#define __MP3PARSE_H__


#include <gst/gst.h>


#ifdef __cplusplus
extern "C"
{
#endif				/* __cplusplus */


#define GST_TYPE_MP3PARSE \
  (gst_mp3parse_get_type())
#define GST_MP3PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MP3PARSE,GstMPEGAudioParse))
#define GST_MP3PARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MP3PARSE,GstMPEGAudioParse))
#define GST_IS_MP3PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MP3PARSE))
#define GST_IS_MP3PARSE_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MP3PARSE))

  typedef struct _GstMPEGAudioParse GstMPEGAudioParse;
  typedef struct _GstMPEGAudioParseClass GstMPEGAudioParseClass;

  struct _GstMPEGAudioParse
  {
    GstElement element;

    GstPad *sinkpad, *srcpad;

    GstBuffer *partialbuf;	/* previous buffer (if carryover) */
    guint skip;			/* number of frames to skip */
    guint bit_rate;
    gint channels, rate, layer;
    gboolean in_flush;
  };

  struct _GstMPEGAudioParseClass
  {
    GstElementClass parent_class;
  };

  GType gst_mp3parse_get_type (void);


#ifdef __cplusplus
}
#endif				/* __cplusplus */


#endif				/* __MP3PARSE_H__ */
