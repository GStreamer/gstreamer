/*
 * GStreamer
 * Copyright (C) 2008 Nokia Corporation <multimedia@maemo.org>
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

#ifndef __CAMERABIN_VIDEO_H__
#define __CAMERABIN_VIDEO_H__

#include <gst/gstbin.h>

#include "gstcamerabin-enum.h"

G_BEGIN_DECLS
#define ARG_DEFAULT_MUTE FALSE
#define GST_TYPE_CAMERABIN_VIDEO             (gst_camerabin_video_get_type())
#define GST_CAMERABIN_VIDEO_CAST(obj)        ((GstCameraBinVideo*)(obj))
#define GST_CAMERABIN_VIDEO(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CAMERABIN_VIDEO,GstCameraBinVideo))
#define GST_CAMERABIN_VIDEO_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CAMERABIN_VIDEO,GstCameraBinVideoClass))
#define GST_IS_CAMERABIN_VIDEO(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CAMERABIN_VIDEO))
#define GST_IS_CAMERABIN_VIDEO_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CAMERABIN_VIDEO))
/**
 * GstCameraBinVideo:
 *
 * The opaque #GstCameraBinVideo structure.
 */
typedef struct _GstCameraBinVideo GstCameraBinVideo;
typedef struct _GstCameraBinVideoClass GstCameraBinVideoClass;

struct _GstCameraBinVideo
{
  GstBin parent;

  GString *filename;

  /* A/V timestamp rewriting */
  guint64 adjust_ts_video;
  guint64 last_ts_video;
  gboolean calculate_adjust_ts_video;

  /* Sink and src pads of video bin */
  GstPad *sinkpad;
  GstPad *srcpad;

  /* Tee src pads leading to video encoder and view finder */
  GstPad *tee_video_srcpad;
  GstPad *tee_vf_srcpad;

  /* Application set elements */
  GstElement *app_post;         /* Video post processing */
  GstElement *app_vid_enc;
  GstElement *app_aud_enc;
  GstElement *app_aud_src;
  GstElement *app_mux;

  /* Other elements */
  GstElement *aud_src;          /* Audio source */
  GstElement *sink;             /* Sink for recorded video */
  GstElement *tee;              /* Split output to view finder and recording sink */
  GstElement *volume;           /* Volume for muting */
  GstElement *video_queue;      /* Buffer for raw video frames */
  GstElement *vid_enc;          /* Video encoder */
  GstElement *aud_enc;          /* Audio encoder */
  GstElement *muxer;            /* Muxer */

  GstEvent *pending_eos;

  /* Probe IDs */
  gulong vid_src_probe_id;
  gulong vid_tee_probe_id;
  gulong vid_sink_probe_id;

  gboolean mute;
  GstCameraBinFlags flags;
};

struct _GstCameraBinVideoClass
{
  GstBinClass parent_class;
};

GType gst_camerabin_video_get_type (void);

/*
 * external function prototypes
 */

void gst_camerabin_video_set_mute (GstCameraBinVideo * vid, gboolean mute);

void gst_camerabin_video_set_post (GstCameraBinVideo * vid, GstElement * post);

void
gst_camerabin_video_set_video_enc (GstCameraBinVideo * vid,
    GstElement * video_enc);

void
gst_camerabin_video_set_audio_enc (GstCameraBinVideo * vid,
    GstElement * audio_enc);

void
gst_camerabin_video_set_muxer (GstCameraBinVideo * vid, GstElement * muxer);

void
gst_camerabin_video_set_audio_src (GstCameraBinVideo * vid,
    GstElement * audio_src);

void
gst_camerabin_video_set_flags (GstCameraBinVideo * vid,
    GstCameraBinFlags flags);


gboolean gst_camerabin_video_get_mute (GstCameraBinVideo * vid);

GstElement *gst_camerabin_video_get_post (GstCameraBinVideo * vid);

GstElement *gst_camerabin_video_get_video_enc (GstCameraBinVideo * vid);

GstElement *gst_camerabin_video_get_audio_enc (GstCameraBinVideo * vid);

GstElement *gst_camerabin_video_get_muxer (GstCameraBinVideo * vid);

GstElement *gst_camerabin_video_get_audio_src (GstCameraBinVideo * vid);

G_END_DECLS
#endif /* #ifndef __CAMERABIN_VIDEO_H__ */
