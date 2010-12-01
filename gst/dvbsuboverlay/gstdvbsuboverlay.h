/* GStreamer DVB subtitles overlay
 * Copyright (c) 2010 Mart Raudsepp <mart.raudsepp@collabora.co.uk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#ifndef __GST_DVBSUB_OVERLAY_H__
#define __GST_DVBSUB_OVERLAY_H__

#include <gst/gst.h>
#include <gst/video/video.h>

#include "dvb-sub.h"

G_BEGIN_DECLS

#define GST_TYPE_DVBSUB_OVERLAY (gst_dvbsub_overlay_get_type())
#define GST_DVBSUB_OVERLAY(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DVBSUB_OVERLAY,GstDVBSubOverlay))
#define GST_DVBSUB_OVERLAY_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DVBSUB_OVERLAY,GstDVBSubOverlayClass))
#define GST_IS_DVBSUB_OVERLAY(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DVBSUB_OVERLAY))
#define GST_IS_DVBSUB_OVERLAY_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DVBSUB_OVERLAY))

typedef struct _GstDVBSubOverlay GstDVBSubOverlay;
typedef struct _GstDVBSubOverlayClass GstDVBSubOverlayClass;
typedef void (*GstAssRenderBlitFunction) (GstDVBSubOverlay *overlay, DVBSubtitles *subs, GstBuffer *buffer);

struct _GstDVBSubOverlay
{
  GstElement element;

  GstPad *video_sinkpad, *text_sinkpad, *srcpad;

  /* properties */
  gboolean enable;

  /* <private> */
  GstSegment video_segment;
  GstSegment subtitle_segment;

  GstVideoFormat format;
  gint width, height;
  gint fps_n, fps_d;
  GstAssRenderBlitFunction blit;

  DVBSubtitles *current_subtitle; /* The currently active set of subtitle regions, if any */
  GQueue *pending_subtitles; /* A queue of raw subtitle region sets with
			      * metadata that are waiting their running time */

  GMutex *subtitle_mutex;
  GCond *subtitle_cond; /* to signal removal of a queued text
			 * buffer, arrival of a text buffer,
			 * a text segment update, or a change
			 * in status (e.g. shutdown, flushing)
			 * FIXME: Update comment for dvbsub case */
  GstBuffer *subtitle_pending;
  gboolean subtitle_flushing;
  gboolean subtitle_eos;

  GMutex *dvbsub_mutex; /* FIXME: Do we need a mutex lock in case of libdvbsub? Probably, but... */
  DvbSub *dvb_sub;

  gboolean renderer_init_ok;
};

struct _GstDVBSubOverlayClass
{
  GstElementClass parent_class;
};

GType gst_dvbsub_overlay_get_type (void);

G_END_DECLS

#endif
