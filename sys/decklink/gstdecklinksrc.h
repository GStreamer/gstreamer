/* GStreamer
 * Copyright (C) 2011 David Schleef <ds@schleef.org>
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

#ifndef _GST_DECKLINK_SRC_H_
#define _GST_DECKLINK_SRC_H_

#include <gst/gst.h>
#include "DeckLinkAPI.h"

G_BEGIN_DECLS

#define GST_TYPE_DECKLINK_SRC   (gst_decklink_src_get_type())
#define GST_DECKLINK_SRC(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DECKLINK_SRC,GstDecklinkSrc))
#define GST_DECKLINK_SRC_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DECKLINK_SRC,GstDecklinkSrcClass))
#define GST_IS_DECKLINK_SRC(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DECKLINK_SRC))
#define GST_IS_DECKLINK_SRC_CLASS(obj)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DECKLINK_SRC))

typedef struct _GstDecklinkSrc GstDecklinkSrc;
typedef struct _GstDecklinkSrcClass GstDecklinkSrcClass;

struct _GstDecklinkSrc
{
  GstElement base_decklinksrc;

  GstPad *audiosrcpad;
  GstPad *videosrcpad;

  GstCaps *audio_caps;

  IDeckLink *decklink;
  IDeckLinkInput *input;

  GMutex *mutex;
  GCond *cond;
  int dropped_frames;
  gboolean stop;
  IDeckLinkVideoInputFrame *video_frame;
  IDeckLinkAudioInputPacket * audio_frame;

  GstTask *task;
  GStaticRecMutex task_mutex;

  int num_audio_samples;

  GstCaps *video_caps;
  int num_frames;
  int fps_n;
  int fps_d;
  int width;
  int height;
  gboolean interlaced;
  BMDDisplayMode bmd_mode;

  /* properties */
  gboolean copy_data;
  int mode;
};

struct _GstDecklinkSrcClass
{
  GstElementClass base_decklinksrc_class;
};

GType gst_decklink_src_get_type (void);

G_END_DECLS

#endif
