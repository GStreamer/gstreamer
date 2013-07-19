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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef _GST_DECKLINK_SRC_H_
#define _GST_DECKLINK_SRC_H_

#include <gst/gst.h>
#include "gstdecklink.h"

G_BEGIN_DECLS

GST_DEBUG_CATEGORY_EXTERN (gst_decklink_src_debug_category);

#define GST_TYPE_DECKLINK_SRC   (gst_decklink_src_get_type())
#define GST_DECKLINK_SRC(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DECKLINK_SRC,GstDecklinkSrc))
#define GST_DECKLINK_SRC_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DECKLINK_SRC,GstDecklinkSrcClass))
#define GST_DECKLINK_SRC_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_DECKLINK_SRC, GstDecklinkSrcClass))
#define GST_IS_DECKLINK_SRC(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DECKLINK_SRC))
#define GST_IS_DECKLINK_SRC_CLASS(obj)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DECKLINK_SRC))

typedef struct _GstDecklinkSrc GstDecklinkSrc;
typedef struct _GstDecklinkSrcClass GstDecklinkSrcClass;

struct _GstDecklinkSrc
{
  GstElement base_decklinksrc;

  GstPad *audiosrcpad;
  GstPad *videosrcpad;

  gboolean  pending_eos;    /* ATOMIC */

  gboolean  have_events;    /* ATOMIC */
  GList    *pending_events; /* OBJECT_LOCK */

  IDeckLink *decklink;
  IDeckLinkInput *input;
  IDeckLinkConfiguration *config;

  GMutex mutex;
  GCond cond;
  int dropped_frames;
  int dropped_frames_old;
  gboolean stop;
  IDeckLinkVideoInputFrame *video_frame;
  IDeckLinkAudioInputPacket * audio_frame;

  GstTask *task;
  GRecMutex task_mutex;

  guint64 num_audio_samples;

  guint64 frame_num;
  int fps_n;
  int fps_d;
  int width;
  int height;
  gboolean interlaced;
  BMDDisplayMode bmd_mode;

  /* so we send a stream-start, caps, and newsegment events before buffers */
  gboolean started;

  /* properties */
  gboolean copy_data;
  GstDecklinkModeEnum mode;
  GstDecklinkConnectionEnum connection;
  GstDecklinkAudioConnectionEnum audio_connection;
  int device_number;

#ifdef _MSC_VER
  gboolean comInitialized;
  GMutex   *com_init_lock;
  GMutex   *com_deinit_lock;
  GCond    *com_initialized;
  GCond    *com_uninitialize;
  GCond    *com_uninitialized;
#endif /* _MSC_VER */
};

struct _GstDecklinkSrcClass
{
  GstElementClass base_decklinksrc_class;
};

GType gst_decklink_src_get_type (void);

G_END_DECLS

#endif
