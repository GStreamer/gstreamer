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


#ifndef __GST_AVIMUX_H__
#define __GST_AVIMUX_H__


#include <config.h>
#include <gst/gst.h>
#include <libs/riff/gstriff.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define GST_TYPE_AVIMUX \
  (gst_avimux_get_type())
#define GST_AVIMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AVIMUX,GstAviMux))
#define GST_AVIMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AVIMUX,GstAviMux))
#define GST_IS_AVIMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AVIMUX))
#define GST_IS_AVIMUX_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AVIMUX))


#define GST_AVIMUX_INITIAL	  0	/* initialized state */
#define GST_AVIMUX_MOVI	  1	/* encoding movi */

#define GST_AVIMUX_MAX_AUDIO_PADS	8	
#define GST_AVIMUX_MAX_VIDEO_PADS	8	

typedef struct _GstAviMux GstAviMux;
typedef struct _GstAviMuxClass GstAviMuxClass;

struct _GstAviMux {
  GstElement element;

  /* pads */
  GstPad *srcpad;

  /* AVI encoding state */
  gint state;

  /* RIFF encoding state */
  GstRiff *riff;

  guint64 next_time;
  guint64 time_interval;

  gst_riff_avih  *aviheader;    /* the avi header */
  guint num_audio_pads;
  guint num_video_pads;

  GstPad             *audio_pad[GST_AVIMUX_MAX_AUDIO_PADS];
  gst_riff_strf_auds *audio_header[GST_AVIMUX_MAX_AUDIO_PADS];
  
  GstPad             *video_pad[GST_AVIMUX_MAX_VIDEO_PADS];
  gst_riff_strf_vids *video_header[GST_AVIMUX_MAX_VIDEO_PADS];
};

struct _GstAviMuxClass {
  GstElementClass parent_class;
};

GType gst_avimux_get_type(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_AVIMUX_H__ */
