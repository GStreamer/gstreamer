/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstaudiosrc.h: 
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


#ifndef __GST_AUDIOSRC_H__
#define __GST_AUDIOSRC_H__


#include <config.h>
#include <gst/gst.h>
#include <gst/meta/audioraw.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


GstElementDetails gst_audiosrc_details;


#define GST_TYPE_AUDIOSRC \
  (gst_audiosrc_get_type())
#define GST_AUDIOSRC(obj) \
  (GTK_CHECK_CAST((obj),GST_TYPE_AUDIOSRC,GstAudioSrc))
#define GST_AUDIOSRC_CLASS(klass) \
  (GTK_CHECK_CLASS_CAST((klass),GST_TYPE_AUDIOSRC,GstAudioSrcClass))
#define GST_IS_AUDIOSRC(obj) \
  (GTK_CHECK_TYPE((obj),GST_TYPE_AUDIOSRC))
#define GST_IS_AUDIOSRC_CLASS(obj) \
  (GTK_CHECK_CLASS_TYPE((klass),GST_TYPE_AUDIOSRC))

typedef enum {
  GST_AUDIOSRC_OPEN		= GST_ELEMENT_FLAG_LAST,

  GST_AUDIOSRC_FLAG_LAST	= GST_ELEMENT_FLAG_LAST+2,
} GstAudioSrcFlags;

typedef struct _GstAudioSrc GstAudioSrc;
typedef struct _GstAudioSrcClass GstAudioSrcClass;

struct _GstAudioSrc {
  GstElement element;

  /* pads */
  GstPad *srcpad;

  /* sound card */
  gint fd;

  /* audio parameters */
  gint format;
  gint channels;
  gint frequency;

  /* blocking */
  gulong curoffset;
  gulong bytes_per_read;

  gulong seq;

  MetaAudioRaw *meta;
};

struct _GstAudioSrcClass {
  GstElementClass parent_class;
};

GtkType gst_audiosrc_get_type(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_AUDIOSRC_H__ */
