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


#ifndef __GST_MODPLUG_H__
#define __GST_MODPLUG_H__

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <gst/gst.h>
#include <gst/bytestream/bytestream.h>
	
#define GST_TYPE_MODPLUG \
  (gst_modplug_get_type())
  
#define GST_MODPLUG(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MODPLUG,GstModPlug))
#define GST_MODPLUG_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ULAW,GstModPlug))
#define GST_IS_MODPLUG(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MODPLUG))
#define GST_IS_MODPLUG_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MODPLUG))
  
struct _GstModPlug {
  GstElement element;
  GstPad *sinkpad, *srcpad;
  guint8 *buffer_in;
  GstByteStream *bs;

  const gchar *songname;
  gboolean reverb;
  gint reverb_depth;
  gint reverb_delay;
  gboolean megabass;
  gint megabass_amount;
  gint megabass_range;
  gboolean surround;
  gint surround_depth;
  gint surround_delay;
  gboolean noise_reduction;
  gboolean _16bit;
  gboolean oversamp;
  gint channel;
  gint frequency;

  guchar *audiobuffer;
  gint32 length;
  guint state;
  guint bitsPerSample;
  gboolean need_discont;
  gboolean eos;
  gint64 seek_at;
  guint64 song_size;

  CSoundFile *mSoundFile;
  gboolean opened; /* set to TRUE when mSoundFile is created */
};

struct _GstModPlugClass {
  GstElementClass parent_class;
};

typedef struct _GstModPlug GstModPlug;
typedef struct _GstModPlugClass GstModPlugClass;

GstPad *srcpad;
int need_sync;

GType gst_modplug_get_type(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __GST_MODPLUG_H__ */
