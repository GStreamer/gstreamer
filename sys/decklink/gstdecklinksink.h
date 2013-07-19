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

#ifndef _GST_DECKLINK_SINK_H_
#define _GST_DECKLINK_SINK_H_

#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include "gstdecklink.h"

G_BEGIN_DECLS

#define GST_TYPE_DECKLINK_SINK   (gst_decklink_sink_get_type())
#define GST_DECKLINK_SINK(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DECKLINK_SINK,GstDecklinkSink))
#define GST_DECKLINK_SINK_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DECKLINK_SINK,GstDecklinkSinkClass))
#define GST_DECKLINK_SINK_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_DECKLINK_SINK, GstDecklinkSinkClass))
#define GST_IS_DECKLINK_SINK(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DECKLINK_SINK))
#define GST_IS_DECKLINK_SINK_CLASS(obj)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DECKLINK_SINK))

typedef struct _GstDecklinkSink GstDecklinkSink;
typedef struct _GstDecklinkSinkClass GstDecklinkSinkClass;

class Output : public IDeckLinkVideoOutputCallback,
public IDeckLinkAudioOutputCallback
{
  public:
    GstDecklinkSink *decklinksink;

    virtual HRESULT STDMETHODCALLTYPE QueryInterface (REFIID iid, LPVOID *ppv)        {return E_NOINTERFACE;}
    virtual ULONG STDMETHODCALLTYPE AddRef () {return 1;}
    virtual ULONG STDMETHODCALLTYPE Release () {return 1;}
    virtual HRESULT STDMETHODCALLTYPE ScheduledFrameCompleted (IDeckLinkVideoFrame* completedFrame, BMDOutputFrameCompletionResult result);
    virtual HRESULT STDMETHODCALLTYPE ScheduledPlaybackHasStopped ();
    virtual HRESULT STDMETHODCALLTYPE RenderAudioSamples (bool preroll);
};

struct _GstDecklinkSink
{
  GstElement base_decklinksink;
  GstAdapter *audio_adapter;

  GstPad *videosinkpad;
  GstPad *audiosinkpad;

  GMutex mutex;
  GCond  cond;
  GMutex audio_mutex;
  GCond  audio_cond;
  int queued_frames;
  gboolean stop;
  gboolean video_eos;
  gboolean audio_eos;
  int video_seqnum;
  int audio_seqnum;

  IDeckLink *decklink;
  IDeckLinkOutput *output;
  Output *callback;
  BMDDisplayMode display_mode;
  BMDPixelFormat pixel_format;
  gboolean video_enabled;
  gboolean sched_started;

  int num_frames;

  /* properties */
  GstDecklinkModeEnum mode;
  int device_number;

#ifdef _MSC_VER
  gboolean comInitialized;
  GMutex   com_init_lock;
  GMutex   com_deinit_lock;
  GCond    com_initialized;
  GCond    com_uninitialize;
  GCond    com_uninitialized;
#endif /* _MSC_VER */
};

struct _GstDecklinkSinkClass
{
  GstElementClass base_decklinksink_class;
};

GType gst_decklink_sink_get_type (void);

G_END_DECLS

#endif
