/* Gnome-Streamer
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


#ifndef __GST_PARSEWAV_H__
#define __GST_PARSEWAV_H__


#include <config.h>
#include <gst/gst.h>
#include <gstriff.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define GST_TYPE_PARSEWAV \
  (gst_parsewav_get_type())
#define GST_PARSEWAV(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_PARSEWAV,GstParseWav))
#define GST_PARSEWAV_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_PARSEWAV,GstParseWav))
#define GST_IS_PARSEWAV(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_PARSEWAV))
#define GST_IS_PARSEWAV_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_PARSEWAV))


#define GST_PARSEWAV_UNKNOWN	0	/* initialized state */
#define GST_PARSEWAV_CHUNK_FMT	1	/* searching for fmt */
#define GST_PARSEWAV_CHUNK_DATA	2	/* searching for data */
#define GST_PARSEWAV_DATA	3	/* in data region */
#define GST_PARSEWAV_OTHER	4	/* in unknown region */

typedef struct _GstParseWav GstParseWav;
typedef struct _GstParseWavClass GstParseWavClass;

struct _GstParseWav {
  GstElement element;

  /* pads */
  GstPad *sinkpad,*srcpad;

  /* WAVE decoding state */
  gint state;

  /* RIFF decoding state */
  GstRiff *riff;
  gulong riff_nextlikely;

  /* expected length of audio */
  gulong size;

  /* useful audio data */
  gint bps;

};

struct _GstParseWavClass {
  GstElementClass parent_class;
};

GType gst_parsewav_get_type(void);

typedef struct _GstParseWavFormat GstParseWavFormat;

struct _GstParseWavFormat {
  gint16 wFormatTag;
  guint16 wChannels;
  guint32 dwSamplesPerSec;
  guint32 dwAvgBytesPerSec;
  guint16 wBlockAlign;
  guint16 wBitsPerSample;
};

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_PARSEAU_H__ */
