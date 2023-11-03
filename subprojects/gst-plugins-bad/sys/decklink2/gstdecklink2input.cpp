/*
 * GStreamer
 * Copyright (C) 2023 Seungha Yang <seungha@centricular.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstdecklink2input.h"
#include "gstdecklink2device.h"
#include <atomic>
#include <mutex>
#include <new>
#include <condition_variable>
#include <queue>
#include <string.h>
#ifdef G_OS_WIN32
#include <malloc.h>
#elif defined(HAVE_POSIX_MEMALIGN)
#include <stdlib.h>
#endif

GST_DEBUG_CATEGORY_STATIC (gst_decklink2_input_debug);
#define GST_CAT_DEFAULT gst_decklink2_input_debug

#define INVALID_AUDIO_OFFSET ((guint64) -1)

static HRESULT gst_decklink2_input_on_format_changed (GstDeckLink2Input * self,
    BMDVideoInputFormatChangedEvents events, IDeckLinkDisplayMode * mode,
    BMDDetectedVideoInputFormatFlags flags);
static void gst_decklink2_input_on_frame_arrived (GstDeckLink2Input * self,
    IDeckLinkVideoInputFrame * frame, IDeckLinkAudioInputPacket * packet);

static guint8 *
gst_decklink2_aligned_alloc (guint32 size)
{
  guint8 *buf = NULL;

  /* Same alignment as the existing DeckLink implementation:
   * The Decklink SDK requires 16 byte aligned memory at least but for us
   * to work nicely let's align to 64 bytes (512 bits) as this allows
   * aligned AVX2 operations for example */
#ifdef G_OS_WIN32
  buf = (guint8 *) _aligned_malloc ((gsize) size + 64, 64);
#elif defined(HAVE_POSIX_MEMALIGN)
  void *aligned_buf = NULL;
  if (posix_memalign (&aligned_buf, 64, (gsize) size + 64) == 0)
    buf = (guint8 *) aligned_buf;
#else
  guint8 offset = 0;
  buf = (guint8 *) g_try_malloc ((gsize) size + 128);
#endif
  if (!buf)
    return NULL;

  /* Write the allocation size at the very beginning. The base pointer is
   * aligned enough for doing this. */
  *((guint32 *) buf) = size;

#if defined(G_OS_WIN32) || defined(HAVE_POSIX_MEMALIGN)
  return buf + 64;
#else
  if (((guintptr) buf) % 64 != 0)
    offset = ((guintptr) buf) % 64;

  /* Align our buffer */
  buf += 128 - offset;

  /* And write the alignment offset right before the buffer */
  *(buf - 1) = offset;
  return buf;
#endif
}

static guint8 *
gst_decklink2_aligned_alloc_get_base (void *buffer)
{
  guint8 *buf = (guint8 *) buffer;

#if defined(G_OS_WIN32) || defined(HAVE_POSIX_MEMALIGN)
  return buf - 64;
#else
  guint8 offset = *(buf - 1);
  return buf - 128 + offset;
#endif
}

static guint32
gst_decklink2_aligned_alloc_get_size (void *buffer)
{
  return *(guint32 *) gst_decklink2_aligned_alloc_get_base (buffer);
}

static void
gst_decklink2_aligned_free (void *buffer)
{
  void *alloc_buffer = gst_decklink2_aligned_alloc_get_base (buffer);

#ifdef G_OS_WIN32
  _aligned_free (alloc_buffer);
#elif defined(HAVE_POSIX_MEMALIGN)
  free (alloc_buffer);
#else
  g_free (alloc_buffer);
#endif
}

class IGstDeckLinkMemoryAllocator:public IDeckLinkMemoryAllocator
{
public:
  IGstDeckLinkMemoryAllocator ():ref_count_ (1)
  {
  }

  /* IUnknown */
  HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, void **object)
  {
    if (riid == IID_IUnknown) {
      *object = static_cast < IUnknown * >(this);
    } else if (riid == IID_IDeckLinkMemoryAllocator) {
      *object = static_cast < IDeckLinkMemoryAllocator * >(this);
    } else {
      *object = NULL;
      return E_NOINTERFACE;
    }

    AddRef ();

    return S_OK;
  }

  ULONG STDMETHODCALLTYPE AddRef (void)
  {
    ULONG cnt = ref_count_.fetch_add (1);

    return cnt + 1;
  }

  ULONG STDMETHODCALLTYPE Release (void)
  {
    ULONG cnt = ref_count_.fetch_sub (1);
    if (cnt == 1)
      delete this;

    return cnt - 1;
  }

  /* IDeckLinkMemoryAllocator */
  HRESULT STDMETHODCALLTYPE AllocateBuffer (unsigned int size, void **alloced)
  {
    std::unique_lock < std::mutex > lk (lock_);
    guint8 *buf = NULL;

    if (size != last_alloc_size_) {
      GST_DEBUG ("%p size changed %u -> %u", this, last_alloc_size_, size);
      ClearPool ();
      last_alloc_size_ = size;
    }

    if (!buffers_.empty ()) {
      buf = (guint8 *) buffers_.front ();
      buffers_.pop ();
    }
    lk.unlock ();

    if (!buf) {
      /* We wrap decklink frame memory directly in GstBuffer.
       * Use our own allocator to keep the backing memory aligned and to
       * reuse it across frames */
      buf = gst_decklink2_aligned_alloc (size);
      if (!buf) {
        *alloced = NULL;
        return E_OUTOFMEMORY;
      }
    }

    *alloced = (void *) buf;

    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE ReleaseBuffer (void *buffer)
  {
    std::lock_guard < std::mutex > lk (lock_);
    guint32 size = gst_decklink2_aligned_alloc_get_size (buffer);

    if (!commited_ || size != last_alloc_size_)
      gst_decklink2_aligned_free (buffer);
    else
      buffers_.push (buffer);

    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE Commit (void)
  {
    std::lock_guard < std::mutex > lk (lock_);
    GST_DEBUG ("Commit %p", this);

    ClearPool ();
    commited_ = true;
    last_alloc_size_ = 0;

    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE Decommit (void)
  {
    std::lock_guard < std::mutex > lk (lock_);

    GST_DEBUG ("Decommit %p", this);

    ClearPool ();
    commited_ = false;

    return S_OK;
  }

private:
  virtual ~ IGstDeckLinkMemoryAllocator () {
    Decommit ();
  }

  void ClearPool (void)
  {
    while (!buffers_.empty ()) {
      guint8 *buf = (guint8 *) buffers_.front ();
      buffers_.pop ();

      gst_decklink2_aligned_free (buf);
    }
  }

private:
  std::atomic < ULONG > ref_count_;
  std::mutex lock_;
  std::queue < void *>buffers_;
  unsigned int last_alloc_size_ = 0;
  bool commited_ = false;
};

/* IDeckLinkInputCallback interface for API version < 12.0 */
class IGstDeckLinkInputCallback_v11_5_1:public IDeckLinkInputCallback_v11_5_1
{
public:
  IGstDeckLinkInputCallback_v11_5_1 (GstDeckLink2Input * input)
  :ref_count_ (1), input_ (input)
  {
  }

  /* IUnknown */
  HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, void **object)
  {
    if (riid == IID_IUnknown) {
      *object = static_cast < IUnknown * >(this);
    } else if (riid == IID_IDeckLinkInputCallback_v11_5_1) {
      *object = static_cast < IDeckLinkInputCallback_v11_5_1 * >(this);
    } else {
      *object = NULL;
      return E_NOINTERFACE;
    }

    AddRef ();

    return S_OK;
  }

  ULONG STDMETHODCALLTYPE AddRef (void)
  {
    ULONG cnt = ref_count_.fetch_add (1);

    return cnt + 1;
  }

  ULONG STDMETHODCALLTYPE Release (void)
  {
    ULONG cnt = ref_count_.fetch_sub (1);
    if (cnt == 1)
      delete this;

    return cnt - 1;
  }

  /* IDeckLinkInputCallback_v11_5_1 */
  HRESULT STDMETHODCALLTYPE
      VideoInputFormatChanged (BMDVideoInputFormatChangedEvents events,
      IDeckLinkDisplayMode * mode, BMDDetectedVideoInputFormatFlags flags)
  {
    return gst_decklink2_input_on_format_changed (input_, events, mode, flags);
  }

  HRESULT STDMETHODCALLTYPE
      VideoInputFrameArrived (IDeckLinkVideoInputFrame * frame,
      IDeckLinkAudioInputPacket * packet)
  {
    gst_decklink2_input_on_frame_arrived (input_, frame, packet);
    return S_OK;
  }

private:
  virtual ~ IGstDeckLinkInputCallback_v11_5_1 () {
  }

private:
  std::atomic < ULONG > ref_count_;
  GstDeckLink2Input *input_;
};

class IGstDeckLinkInputCallback:public IDeckLinkInputCallback
{
public:
  IGstDeckLinkInputCallback (GstDeckLink2Input * input)
  :ref_count_ (1), input_ (input)
  {
  }

  /* IUnknown */
  HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, void **object)
  {
    if (riid == IID_IUnknown) {
      *object = static_cast < IUnknown * >(this);
    } else if (riid == IID_IDeckLinkInputCallback) {
      *object = static_cast < IDeckLinkInputCallback * >(this);
    } else {
      *object = NULL;
      return E_NOINTERFACE;
    }

    AddRef ();

    return S_OK;
  }

  ULONG STDMETHODCALLTYPE AddRef (void)
  {
    ULONG cnt = ref_count_.fetch_add (1);

    return cnt + 1;
  }

  ULONG STDMETHODCALLTYPE Release (void)
  {
    ULONG cnt = ref_count_.fetch_sub (1);
    if (cnt == 1)
      delete this;

    return cnt - 1;
  }

  /* IDeckLinkInputCallback */
  HRESULT STDMETHODCALLTYPE
      VideoInputFormatChanged (BMDVideoInputFormatChangedEvents events,
      IDeckLinkDisplayMode * mode, BMDDetectedVideoInputFormatFlags flags)
  {
    return gst_decklink2_input_on_format_changed (input_, events, mode, flags);
  }

  HRESULT STDMETHODCALLTYPE
      VideoInputFrameArrived (IDeckLinkVideoInputFrame * frame,
      IDeckLinkAudioInputPacket * packet)
  {
    gst_decklink2_input_on_frame_arrived (input_, frame, packet);
    return S_OK;
  }

private:
  virtual ~ IGstDeckLinkInputCallback () {
  }

private:
  std::atomic < ULONG > ref_count_;
  GstDeckLink2Input *input_;
};

struct GstDecklink2InputAudioData
{
  GstSample *sample;
  guint64 packet_time;
  GstClockTime stream_time;
  GstClockTime calibrated_time;
};

struct GstDecklink2InputData
{
  GstBuffer *buffer;
  GstCaps *caps;
  GstDeckLink2InputStats stats;
};

struct TimeMapping
{
  GstClockTime xbase;
  GstClockTime b;
  GstClockTime num;
  GstClockTime den;
};

struct GstDeckLink2InputMetadata:GstDeckLink2HDRMetadata
{
  GstVideoColorimetry colorimetry;
};

struct GstDeckLink2InputPrivate
{
  GstDeckLink2InputPrivate ()
  {
    signal = false;
    was_restarted = false;
    stopping = false;
  }

  GstDeckLink2InputCalibration calibration = { };
  GstDeckLink2InputMetadata frame_metadata = { };
  GstCaps *frame_caps = nullptr;
  bool api_failed = false;
  bool aspect_ratio_mismatch_warned = false;

  std::mutex lock;
  std::condition_variable cond;
  std::atomic < bool >signal;
  std::atomic < bool >was_restarted;
  std::atomic < bool >stopping;
};

struct _GstDeckLink2Input
{
  GstObject parent;

  GstDeckLink2InputPrivate *priv;

  GstDeckLink2APILevel api_level;

  IDeckLink *device;
  IDeckLinkProfileAttributes *attr;
  IDeckLinkAttributes_v10_11 *attr_10_11;
  GstDeckLink2ColorCapabilities color_caps;
  IDeckLinkConfiguration_v10_11 *config_10_11;
  IDeckLinkConfiguration *config;

  IDeckLinkInput *input;
  IDeckLinkInput_v11_5_1 *input_11_5_1;
  IDeckLinkInput_v11_4 *input_11_4;
  IDeckLinkInput_v10_11 *input_10_11;

  IGstDeckLinkMemoryAllocator *allocator;
  IGstDeckLinkInputCallback_v11_5_1 *callback_11_5_1;
  IGstDeckLinkInputCallback *callback;

  GstCaps *caps;
  GArray *format_table;
  GstCaps *selected_video_caps;
  GstAudioInfo audio_info;
  GstVideoInfo video_info;
  guint max_audio_channels;
  GstCaps *selected_audio_caps;
  gboolean auto_detect;

  guint64 next_audio_offset;

  GstVecDeque *queue;
  GstVecDeque *audio_queue;

  GstDeckLink2DisplayMode selected_mode;
  BMDPixelFormat pixel_format;
  BMDPixelFormat requested_pixel_format;
  GstElement *client;
  gboolean output_cc;
  gboolean output_afd_bar;
  gboolean output_vanc;
  gint aspect_ratio_flag;
  BMDTimecodeFormat timecode_format;
  guint buffer_size;
  gboolean discont;
  gboolean flushing;
  gboolean started;
  GstClockTime skip_first_time;
  GstClockTime start_time;
  GstClockTimeDiff av_sync;

  guint window_size;
  guint window_fill;
  gboolean window_filled;
  guint window_skip;
  guint window_skip_count;
  TimeMapping current_time_mapping;
  TimeMapping next_time_mapping;
  gboolean next_time_mapping_pending;
  gboolean have_time_mapping;

  GstClockTime times[256];

  GstVideoVBIParser *vbi_parser;
  GstVideoFormat anc_vformat;

  gint anc_width;
};

static void gst_decklink2_input_dispose (GObject * object);
static void gst_decklink2_input_finalize (GObject * object);
static HRESULT gst_decklink2_input_set_allocator (GstDeckLink2Input * input,
    IDeckLinkMemoryAllocator * allocator);

static void
gst_decklink2_input_calibration_init (GstDeckLink2InputCalibration *
    calibration)
{
  *calibration = { };
  calibration->slope = 1.0;
  calibration->num = 1;
  calibration->den = 1;
  calibration->diff = GST_CLOCK_STIME_NONE;
}

void
gst_decklink2_input_stats_init (GstDeckLink2InputStats * stats)
{
  *stats = { };
  stats->stream_time = GST_CLOCK_TIME_NONE;
  stats->hardware_time = GST_CLOCK_TIME_NONE;
  stats->hardware_reference_time = GST_CLOCK_TIME_NONE;
  stats->capture_time = GST_CLOCK_TIME_NONE;
  stats->calibrated_time = GST_CLOCK_TIME_NONE;
  stats->audio_packet_time = G_MAXUINT64;
  stats->audio_stream_time = GST_CLOCK_TIME_NONE;
  stats->calibrated_audio_time = GST_CLOCK_TIME_NONE;
  gst_decklink2_input_calibration_init (&stats->calibration);
}

static void
gst_decklink2_input_data_clear (GstDecklink2InputData * data)
{
  gst_clear_buffer (&data->buffer);
  gst_clear_caps (&data->caps);
}

static void
gst_decklink2_input_audio_data_clear (GstDecklink2InputAudioData * data)
{
  gst_sample_unref (data->sample);
}

#define gst_decklink2_input_parent_class parent_class
G_DEFINE_TYPE_WITH_PRIVATE (GstDeckLink2Input, gst_decklink2_input,
    GST_TYPE_OBJECT);

static void
gst_decklink2_input_class_init (GstDeckLink2InputClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = gst_decklink2_input_dispose;
  object_class->finalize = gst_decklink2_input_finalize;

  GST_DEBUG_CATEGORY_INIT (gst_decklink2_input_debug, "decklink2input",
      0, "decklink2input");
}

static void
gst_decklink2_input_init (GstDeckLink2Input * self)
{
  self->format_table = g_array_new (FALSE,
      FALSE, sizeof (GstDeckLink2DisplayMode));
  self->allocator = new IGstDeckLinkMemoryAllocator ();
  self->queue = gst_vec_deque_new_for_struct (sizeof (GstDecklink2InputData),
      6);
  gst_vec_deque_set_clear_func (self->queue,
      (GDestroyNotify) gst_decklink2_input_data_clear);
  self->audio_queue =
      gst_vec_deque_new_for_struct (sizeof (GstDecklink2InputAudioData), 6);
  gst_vec_deque_set_clear_func (self->audio_queue,
      (GDestroyNotify) gst_decklink2_input_audio_data_clear);
  auto priv = (GstDeckLink2InputPrivate *)
      gst_decklink2_input_get_instance_private (self);
  self->priv = new (priv) GstDeckLink2InputPrivate ();
  gst_decklink2_input_calibration_init (&priv->calibration);
}

static void
gst_decklink2_input_dispose (GObject * object)
{
  GstDeckLink2Input *self = GST_DECKLINK2_INPUT (object);

  gst_clear_caps (&self->caps);
  gst_clear_caps (&self->selected_video_caps);
  gst_clear_caps (&self->priv->frame_caps);
  gst_clear_caps (&self->selected_audio_caps);
  g_clear_pointer (&self->vbi_parser, gst_video_vbi_parser_free);
  g_clear_pointer (&self->queue, gst_vec_deque_free);
  g_clear_pointer (&self->audio_queue, gst_vec_deque_free);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_decklink2_input_finalize (GObject * object)
{
  GstDeckLink2Input *self = GST_DECKLINK2_INPUT (object);

  GST_DECKLINK2_CLEAR_COM (self->input);
  GST_DECKLINK2_CLEAR_COM (self->input_11_5_1);
  GST_DECKLINK2_CLEAR_COM (self->input_11_4);
  GST_DECKLINK2_CLEAR_COM (self->input_10_11);
  GST_DECKLINK2_CLEAR_COM (self->allocator);
  GST_DECKLINK2_CLEAR_COM (self->callback);
  GST_DECKLINK2_CLEAR_COM (self->callback_11_5_1);
  GST_DECKLINK2_CLEAR_COM (self->attr);
  GST_DECKLINK2_CLEAR_COM (self->attr_10_11);
  GST_DECKLINK2_CLEAR_COM (self->config);
  GST_DECKLINK2_CLEAR_COM (self->config_10_11);
  GST_DECKLINK2_CLEAR_COM (self->device);

  g_array_unref (self->format_table);

  self->priv->~GstDeckLink2InputPrivate ();

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* Returns true if format is supported without conversion */
static gboolean
gst_decklink2_input_does_support_video_mode (GstDeckLink2Input * self,
    BMDDisplayMode mode, BMDPixelFormat format)
{
  HRESULT hr;
  BMDDisplayModeSupport_v10_11 supported_v10_11 =
      bmdDisplayModeNotSupported_v10_11;
  dlbool_t supported = (dlbool_t) 0;
  BMDDisplayMode actual_mode = mode;

  switch (self->api_level) {
    case GST_DECKLINK2_API_LEVEL_10_11:
      hr = self->input_10_11->DoesSupportVideoMode (mode, format,
          bmdVideoInputFlagDefault, &supported_v10_11, NULL);
      if (hr != S_OK || supported_v10_11 != bmdDisplayModeSupported_v10_11)
        return FALSE;
      break;
    case GST_DECKLINK2_API_LEVEL_11_4:
      hr = self->
          input_11_4->DoesSupportVideoMode (bmdVideoConnectionUnspecified, mode,
          format, bmdSupportedVideoModeDefault, &supported);
      if (hr != S_OK || !supported)
        return FALSE;
      break;
    case GST_DECKLINK2_API_LEVEL_11_5_1:
      hr = self->
          input_11_5_1->DoesSupportVideoMode (bmdVideoConnectionUnspecified,
          mode, format, bmdNoVideoInputConversion, bmdSupportedVideoModeDefault,
          &actual_mode, &supported);
      if (hr != S_OK || !supported || actual_mode != mode)
        return FALSE;
      break;
    case GST_DECKLINK2_API_LEVEL_LATEST:
      hr = self->input->DoesSupportVideoMode (bmdVideoConnectionUnspecified,
          mode, format, bmdNoVideoInputConversion, bmdSupportedVideoModeDefault,
          &actual_mode, &supported);
      if (hr != S_OK || !supported || actual_mode != mode)
        return FALSE;
      break;
    default:
      g_assert_not_reached ();
      return FALSE;
  }

  return TRUE;
}

static IDeckLinkDisplayModeIterator *
gst_decklink2_input_get_iterator (GstDeckLink2Input * self)
{
  IDeckLinkDisplayModeIterator *iter = NULL;

  switch (self->api_level) {
    case GST_DECKLINK2_API_LEVEL_10_11:
      self->input_10_11->GetDisplayModeIterator (&iter);
      break;
    case GST_DECKLINK2_API_LEVEL_11_4:
      self->input_11_4->GetDisplayModeIterator (&iter);
      break;
    case GST_DECKLINK2_API_LEVEL_11_5_1:
      self->input_11_5_1->GetDisplayModeIterator (&iter);
      break;
    case GST_DECKLINK2_API_LEVEL_LATEST:
      self->input->GetDisplayModeIterator (&iter);
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  return iter;
}

GstDeckLink2Input *
gst_decklink2_input_new (IDeckLink * device, GstDeckLink2APILevel api_level)
{
  GstDeckLink2Input *self;
  IDeckLinkDisplayModeIterator *iter = NULL;
  HRESULT hr;

  if (!device || api_level == GST_DECKLINK2_API_LEVEL_UNKNOWN)
    return NULL;

  self = (GstDeckLink2Input *) g_object_new (GST_TYPE_DECKLINK2_INPUT, NULL);
  self->api_level = api_level;
  self->device = device;
  device->AddRef ();

  if (api_level == GST_DECKLINK2_API_LEVEL_10_11) {
    device->QueryInterface (IID_IDeckLinkAttributes_v10_11,
        (void **) &self->attr_10_11);
  } else {
    device->QueryInterface (IID_IDeckLinkProfileAttributes,
        (void **) &self->attr);
  }

  self->max_audio_channels = 2;
  gint64 max_audio_channels = 2;
  if (self->attr) {
    hr = self->attr->GetInt (BMDDeckLinkMaximumAudioChannels,
        &max_audio_channels);
    if (gst_decklink2_result (hr))
      self->max_audio_channels = (guint) max_audio_channels;
  } else if (self->attr_10_11) {
    hr = self->attr_10_11->GetInt (BMDDeckLinkMaximumAudioChannels,
        &max_audio_channels);
    if (gst_decklink2_result (hr))
      self->max_audio_channels = (guint) max_audio_channels;
  }

  if (api_level == GST_DECKLINK2_API_LEVEL_10_11) {
    device->QueryInterface (IID_IDeckLinkConfiguration_v10_11,
        (void **) &self->config_10_11);
  } else {
    device->QueryInterface (IID_IDeckLinkConfiguration,
        (void **) &self->config);
  }

  if (api_level > GST_DECKLINK2_API_LEVEL_11_5_1) {
    self->callback = new IGstDeckLinkInputCallback (self);
  } else {
    self->callback_11_5_1 = new IGstDeckLinkInputCallback_v11_5_1 (self);
  }

  switch (api_level) {
    case GST_DECKLINK2_API_LEVEL_10_11:
      hr = device->QueryInterface (IID_IDeckLinkInput_v10_11,
          (void **) &self->input_10_11);
      if (!gst_decklink2_result (hr)) {
        gst_object_unref (self);
        return NULL;
      }
      break;
    case GST_DECKLINK2_API_LEVEL_11_4:
      hr = device->QueryInterface (IID_IDeckLinkInput_v11_4,
          (void **) &self->input_11_4);
      if (!gst_decklink2_result (hr)) {
        gst_object_unref (self);
        return NULL;
      }
      break;
    case GST_DECKLINK2_API_LEVEL_11_5_1:
      hr = device->QueryInterface (IID_IDeckLinkInput_v11_5_1,
          (void **) &self->input_11_5_1);
      if (!gst_decklink2_result (hr)) {
        gst_object_unref (self);
        return NULL;
      }
      break;
    case GST_DECKLINK2_API_LEVEL_LATEST:
      hr = device->QueryInterface (IID_IDeckLinkInput, (void **) &self->input);
      if (hr != S_OK) {
        gst_object_unref (self);
        return NULL;
      }
      break;
    default:
      g_assert_not_reached ();
      gst_object_unref (self);
      return NULL;
  }

  iter = gst_decklink2_input_get_iterator (self);
  if (!iter) {
    gst_object_unref (self);
    return NULL;
  }

  hr = gst_decklink2_input_set_allocator (self, self->allocator);
  if (!gst_decklink2_result (hr)) {
    iter->Release ();
    gst_object_unref (self);
    return NULL;
  }

  gst_decklink2_get_color_capabilities (device, &self->color_caps);

  self->caps = gst_decklink2_build_template_caps (GST_OBJECT (self), iter,
      (GstDeckLink2DoesSupportVideoMode)
      gst_decklink2_input_does_support_video_mode, self->format_table,
      &self->color_caps);
  iter->Release ();

  if (!self->caps)
    gst_clear_object (&self);

  return self;
}

GstCaps *
gst_decklink2_input_get_caps (GstDeckLink2Input * input, BMDDisplayMode mode,
    BMDPixelFormat format)
{
  IDeckLinkDisplayModeIterator *iter = NULL;
  GstCaps *caps;

  if (mode == bmdModeUnknown && format == bmdFormatUnspecified)
    return gst_caps_ref (input->caps);

  iter = gst_decklink2_input_get_iterator (input);
  if (!iter)
    return NULL;

  caps = gst_decklink2_build_caps (GST_OBJECT (input), iter,
      mode, format, (GstDeckLink2DoesSupportVideoMode)
      gst_decklink2_input_does_support_video_mode, &input->color_caps);
  iter->Release ();

  return caps;
}

gboolean
gst_decklink2_input_get_display_mode (GstDeckLink2Input * input,
    const GstVideoInfo * info, GstDeckLink2DisplayMode * display_mode)
{
  for (guint i = 0; i < input->format_table->len; i++) {
    const GstDeckLink2DisplayMode *m = &g_array_index (input->format_table,
        GstDeckLink2DisplayMode, i);

    if (m->width == info->width && m->height == info->height &&
        m->fps_n == info->fps_n && m->fps_d == info->fps_d &&
        m->par_n == info->par_n && m->par_d == info->par_d) {
      if ((m->interlaced && !GST_VIDEO_INFO_IS_INTERLACED (info)) ||
          (!m->interlaced && GST_VIDEO_INFO_IS_INTERLACED (info))) {
        continue;
      }

      *display_mode = *m;
      return TRUE;
    }
  }

  return FALSE;
}

static gboolean
gst_decklink2_input_get_display_mode_from_native (GstDeckLink2Input * input,
    BMDDisplayMode native, GstDeckLink2DisplayMode * display_mode)
{
  for (guint i = 0; i < input->format_table->len; i++) {
    const GstDeckLink2DisplayMode *m = &g_array_index (input->format_table,
        GstDeckLink2DisplayMode, i);

    if (m->mode == native) {
      *display_mode = *m;
      return TRUE;
    }
  }

  return FALSE;
}

static HRESULT
gst_decklink2_input_set_callback (GstDeckLink2Input * self, IUnknown * callback)
{
  HRESULT hr = E_FAIL;

  switch (self->api_level) {
    case GST_DECKLINK2_API_LEVEL_10_11:
      hr = self->input_10_11->SetCallback (
          (IDeckLinkInputCallback_v11_5_1 *) callback);
      break;
    case GST_DECKLINK2_API_LEVEL_11_4:
      hr = self->input_11_4->SetCallback (
          (IDeckLinkInputCallback_v11_5_1 *) callback);
      break;
    case GST_DECKLINK2_API_LEVEL_11_5_1:
      hr = self->input_11_5_1->SetCallback (
          (IDeckLinkInputCallback_v11_5_1 *) callback);
      break;
    case GST_DECKLINK2_API_LEVEL_LATEST:
      hr = self->input->SetCallback ((IDeckLinkInputCallback *) callback);
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  return hr;
}

static HRESULT
gst_decklink2_input_set_allocator (GstDeckLink2Input * self,
    IDeckLinkMemoryAllocator * allocator)
{
  HRESULT hr = E_FAIL;

  switch (self->api_level) {
    case GST_DECKLINK2_API_LEVEL_10_11:
      hr = self->input_10_11->SetVideoInputFrameMemoryAllocator (allocator);
      break;
    case GST_DECKLINK2_API_LEVEL_11_4:
      hr = self->input_11_4->SetVideoInputFrameMemoryAllocator (allocator);
      break;
    case GST_DECKLINK2_API_LEVEL_11_5_1:
      hr = self->input_11_5_1->SetVideoInputFrameMemoryAllocator (allocator);
      break;
    case GST_DECKLINK2_API_LEVEL_LATEST:
      hr = self->input->SetVideoInputFrameMemoryAllocator (allocator);
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  return hr;
}

static HRESULT
gst_decklink2_input_enable_video (GstDeckLink2Input * self,
    BMDDisplayMode mode, BMDPixelFormat format, BMDVideoInputFlags flags)
{
  HRESULT hr = E_FAIL;

  switch (self->api_level) {
    case GST_DECKLINK2_API_LEVEL_10_11:
      hr = self->input_10_11->EnableVideoInput (mode, format, flags);
      break;
    case GST_DECKLINK2_API_LEVEL_11_4:
      hr = self->input_11_4->EnableVideoInput (mode, format, flags);
      break;
    case GST_DECKLINK2_API_LEVEL_11_5_1:
      hr = self->input_11_5_1->EnableVideoInput (mode, format, flags);
      break;
    case GST_DECKLINK2_API_LEVEL_LATEST:
      hr = self->input->EnableVideoInput (mode, format, flags);
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  return hr;
}

static HRESULT
gst_decklink2_input_disable_video (GstDeckLink2Input * self)
{
  HRESULT hr = E_FAIL;

  switch (self->api_level) {
    case GST_DECKLINK2_API_LEVEL_10_11:
      hr = self->input_10_11->DisableVideoInput ();
      break;
    case GST_DECKLINK2_API_LEVEL_11_4:
      hr = self->input_11_4->DisableVideoInput ();
      break;
    case GST_DECKLINK2_API_LEVEL_11_5_1:
      hr = self->input_11_5_1->DisableVideoInput ();
      break;
    case GST_DECKLINK2_API_LEVEL_LATEST:
      hr = self->input->DisableVideoInput ();
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  return hr;
}

static HRESULT
gst_decklink2_input_enable_audio (GstDeckLink2Input * self,
    BMDAudioSampleRate rate, BMDAudioSampleType type, guint channels)
{
  HRESULT hr = E_FAIL;

  switch (self->api_level) {
    case GST_DECKLINK2_API_LEVEL_10_11:
      hr = self->input_10_11->EnableAudioInput (rate, type, channels);
      break;
    case GST_DECKLINK2_API_LEVEL_11_4:
      hr = self->input_11_4->EnableAudioInput (rate, type, channels);
      break;
    case GST_DECKLINK2_API_LEVEL_11_5_1:
      hr = self->input_11_5_1->EnableAudioInput (rate, type, channels);
      break;
    case GST_DECKLINK2_API_LEVEL_LATEST:
      hr = self->input->EnableAudioInput (rate, type, channels);
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  return hr;
}

static HRESULT
gst_decklink2_input_disable_audio (GstDeckLink2Input * self)
{
  HRESULT hr = E_FAIL;

  switch (self->api_level) {
    case GST_DECKLINK2_API_LEVEL_10_11:
      hr = self->input_10_11->DisableAudioInput ();
      break;
    case GST_DECKLINK2_API_LEVEL_11_4:
      hr = self->input_11_4->DisableAudioInput ();
      break;
    case GST_DECKLINK2_API_LEVEL_11_5_1:
      hr = self->input_11_5_1->DisableAudioInput ();
      break;
    case GST_DECKLINK2_API_LEVEL_LATEST:
      hr = self->input->DisableAudioInput ();
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  return hr;
}

static HRESULT
gst_decklink2_input_start_streams (GstDeckLink2Input * self)
{
  HRESULT hr = E_FAIL;

  switch (self->api_level) {
    case GST_DECKLINK2_API_LEVEL_10_11:
      hr = self->input_10_11->StartStreams ();
      break;
    case GST_DECKLINK2_API_LEVEL_11_4:
      hr = self->input_11_4->StartStreams ();
      break;
    case GST_DECKLINK2_API_LEVEL_11_5_1:
      hr = self->input_11_5_1->StartStreams ();
      break;
    case GST_DECKLINK2_API_LEVEL_LATEST:
      hr = self->input->StartStreams ();
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  return hr;
}

static HRESULT
gst_decklink2_input_stop_streams (GstDeckLink2Input * self)
{
  HRESULT hr = E_FAIL;

  switch (self->api_level) {
    case GST_DECKLINK2_API_LEVEL_10_11:
      hr = self->input_10_11->StopStreams ();
      break;
    case GST_DECKLINK2_API_LEVEL_11_4:
      hr = self->input_11_4->StopStreams ();
      break;
    case GST_DECKLINK2_API_LEVEL_11_5_1:
      hr = self->input_11_5_1->StopStreams ();
      break;
    case GST_DECKLINK2_API_LEVEL_LATEST:
      hr = self->input->StopStreams ();
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  return hr;
}

static HRESULT
gst_decklink2_input_pause_streams (GstDeckLink2Input * self)
{
  HRESULT hr = E_FAIL;

  switch (self->api_level) {
    case GST_DECKLINK2_API_LEVEL_10_11:
      hr = self->input_10_11->PauseStreams ();
      break;
    case GST_DECKLINK2_API_LEVEL_11_4:
      hr = self->input_11_4->PauseStreams ();
      break;
    case GST_DECKLINK2_API_LEVEL_11_5_1:
      hr = self->input_11_5_1->PauseStreams ();
      break;
    case GST_DECKLINK2_API_LEVEL_LATEST:
      hr = self->input->PauseStreams ();
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  return hr;
}

static HRESULT
gst_decklink2_input_flush_streams (GstDeckLink2Input * self)
{
  HRESULT hr = E_FAIL;

  switch (self->api_level) {
    case GST_DECKLINK2_API_LEVEL_10_11:
      hr = self->input_10_11->FlushStreams ();
      break;
    case GST_DECKLINK2_API_LEVEL_11_4:
      hr = self->input_11_4->FlushStreams ();
      break;
    case GST_DECKLINK2_API_LEVEL_11_5_1:
      hr = self->input_11_5_1->FlushStreams ();
      break;
    case GST_DECKLINK2_API_LEVEL_LATEST:
      hr = self->input->FlushStreams ();
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  return hr;
}

static HRESULT
gst_decklink2_input_get_reference_clock (GstDeckLink2Input * self,
    BMDTimeScale scale, BMDTimeValue * hw_time, BMDTimeValue * time_in_frame,
    BMDTimeValue * ticks_per_frame)
{
  HRESULT hr = E_FAIL;

  switch (self->api_level) {
    case GST_DECKLINK2_API_LEVEL_10_11:
      hr = self->input_10_11->GetHardwareReferenceClock (scale,
          hw_time, time_in_frame, ticks_per_frame);
      break;
    case GST_DECKLINK2_API_LEVEL_11_4:
      hr = self->input_11_4->GetHardwareReferenceClock (scale,
          hw_time, time_in_frame, ticks_per_frame);
      break;
    case GST_DECKLINK2_API_LEVEL_11_5_1:
      hr = self->input_11_5_1->GetHardwareReferenceClock (scale,
          hw_time, time_in_frame, ticks_per_frame);
      break;
    case GST_DECKLINK2_API_LEVEL_LATEST:
      hr = self->input->GetHardwareReferenceClock (scale,
          hw_time, time_in_frame, ticks_per_frame);
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  return hr;
}

static void
gst_decklink2_input_reset_time_mapping (GstDeckLink2Input * self)
{
  self->window_size = 64;
  self->window_fill = 0;
  self->window_filled = FALSE;
  self->window_skip = 1;
  self->window_skip_count = 0;
  self->current_time_mapping.xbase = 0;
  self->current_time_mapping.b = 0;
  self->current_time_mapping.num = 1;
  self->current_time_mapping.den = 1;
  self->next_time_mapping.xbase = 0;
  self->next_time_mapping.b = 0;
  self->next_time_mapping.num = 1;
  self->next_time_mapping.den = 1;
  self->next_time_mapping_pending = FALSE;
  self->have_time_mapping = FALSE;
  gst_decklink2_input_calibration_init (&self->priv->calibration);
}

static inline gboolean
is_yuv_pixel_format (BMDPixelFormat format)
{
  if (format == bmdFormat8BitYUV || format == bmdFormat10BitYUV)
    return TRUE;

  return FALSE;
}

static HRESULT
gst_decklink2_input_on_format_changed (GstDeckLink2Input * self,
    BMDVideoInputFormatChangedEvents events, IDeckLinkDisplayMode * mode,
    BMDDetectedVideoInputFormatFlags flags)
{
  BMDPixelFormat pixel_format = bmdFormatUnspecified;
  GstVideoFormat video_format;
  BMDDisplayMode display_mode = mode->GetDisplayMode ();
  GstDeckLink2DisplayMode new_mode;
  GstCaps *caps;
  HRESULT hr;
  GstDeckLink2InputPrivate *priv = self->priv;

  {
    std::lock_guard < std::mutex > lk (priv->lock);
    /* Skip format change handling during stopping */
    if (priv->stopping)
      return S_OK;

    if (priv->api_failed)
      return E_FAIL;
  }

  GST_DEBUG_OBJECT (self, "format changed, flags 0x%x", flags);

  if ((flags & bmdDetectedVideoInputRGB444) != 0) {
    /* XXX: cannot detect RGB format.
     * decklink SDK sample is using this value anyway */
    if ((flags & bmdDetectedVideoInput8BitDepth) != 0 ||
        flags == bmdDetectedVideoInputRGB444) {
      pixel_format = self->requested_pixel_format == bmdFormat8BitBGRA ?
          bmdFormat8BitBGRA : bmdFormat8BitARGB;
    } else if ((flags & bmdDetectedVideoInput10BitDepth) != 0) {
      pixel_format = bmdFormat10BitRGB;
    }
  } else if ((flags & bmdDetectedVideoInputYCbCr422) != 0) {
    if ((flags & bmdDetectedVideoInput8BitDepth) != 0 ||
        (flags == bmdDetectedVideoInputYCbCr422)) {
      pixel_format = bmdFormat8BitYUV;
    } else if ((flags & bmdDetectedVideoInput10BitDepth) != 0) {
      pixel_format = bmdFormat10BitYUV;
    }
  }

  if (pixel_format == bmdFormatUnspecified) {
    GST_WARNING_OBJECT (self, "Unknown pixel format");
    return E_INVALIDARG;
  }

  if (!gst_decklink2_input_get_display_mode_from_native (self,
          display_mode, &new_mode)) {
    GST_WARNING_OBJECT (self, "Unknown display mode");
    return E_INVALIDARG;
  }

  if (self->requested_pixel_format != pixel_format &&
      is_yuv_pixel_format (self->requested_pixel_format) &&
      is_yuv_pixel_format (pixel_format)) {
    GST_DEBUG_OBJECT (self, "Updating format as requested");
    pixel_format = self->requested_pixel_format;
  }

  if (self->requested_pixel_format != bmdFormatUnspecified &&
      self->requested_pixel_format != pixel_format) {
    GST_ERROR_OBJECT (self, "Detected format does not match requested format");
    return E_INVALIDARG;
  }

  if (!self->auto_detect && display_mode !=
      gst_decklink2_get_real_display_mode (self->selected_mode.mode)) {
    GST_ERROR_OBJECT (self, "Detected mode does not match requested mode");
    return E_INVALIDARG;
  }

  if (!self->auto_detect)
    new_mode = self->selected_mode;

  video_format = gst_decklink2_video_format_from_pixel_format (pixel_format);

  caps = gst_decklink2_get_caps_from_mode (&new_mode);
  gst_caps_set_simple (caps, "format", G_TYPE_STRING,
      gst_video_format_to_string (video_format), NULL);

  GST_DEBUG_OBJECT (self, "Updated caps %" GST_PTR_FORMAT, caps);

  {
    std::lock_guard < std::mutex > lk (priv->lock);
    if (self->selected_video_caps &&
        gst_caps_is_equal (self->selected_video_caps, caps) &&
        self->pixel_format == pixel_format) {
      GST_DEBUG_OBJECT (self, "Format not changed, skipping enable video");
      gst_caps_unref (caps);
      return S_OK;
    }
  }

  hr = gst_decklink2_input_pause_streams (self);
  if (hr != S_OK) {
    GST_ERROR_OBJECT (self, "PauseStreams failed, hr: 0x%x", (guint) hr);
    goto error;
  }

  hr = gst_decklink2_input_enable_video (self, display_mode, pixel_format,
      bmdVideoInputEnableFormatDetection);
  if (hr != S_OK) {
    GST_ERROR_OBJECT (self, "EnableVideoInput failed, hr: 0x%x", (guint) hr);
    goto error;
  }

  hr = gst_decklink2_input_flush_streams (self);
  if (hr != S_OK) {
    GST_ERROR_OBJECT (self, "FlushStreams failed, hr: 0x%x", (guint) hr);
    goto error;
  }

  {
    std::lock_guard < std::mutex > lk (priv->lock);
    if (priv->stopping || priv->api_failed) {
      gst_caps_unref (caps);
      return priv->stopping ? S_OK : E_FAIL;
    }
    self->selected_mode = new_mode;
    self->pixel_format = pixel_format;

    gst_clear_caps (&self->selected_video_caps);
    gst_clear_caps (&self->priv->frame_caps);
    self->selected_video_caps = caps;
    caps = nullptr;
    gst_video_info_from_caps (&self->video_info, self->selected_video_caps);
    self->aspect_ratio_flag = -1;
    self->discont = TRUE;
    gst_vec_deque_clear (self->audio_queue);
    self->next_audio_offset = INVALID_AUDIO_OFFSET;
    self->av_sync = 0;
    priv->was_restarted = true;

    gst_decklink2_input_reset_time_mapping (self);
  }

  hr = gst_decklink2_input_start_streams (self);
  if (hr != S_OK) {
    GST_ERROR_OBJECT (self, "StartStreams failed, hr: 0x%x", (guint) hr);
    goto error;
  }

  return S_OK;

error:
  gst_clear_caps (&caps);
  {
    std::lock_guard < std::mutex > lk (priv->lock);
    if (!priv->stopping) {
      priv->api_failed = true;
      priv->cond.notify_all ();
    }
  }
  return hr;
}

static void
gst_decklink2_frame_free (IDeckLinkVideoInputFrame * frame)
{
  frame->Release ();
}

static inline guint16
with_parity (const guint8 word)
{
  guint8 bit8, parity;

  parity = word ^ (word >> 4);
  parity ^= (parity >> 2);
  parity ^= (parity >> 1);
  bit8 = parity & 1;

  return (word | (bit8 << 8) | ((!bit8) << 9));
}

static void
extract_vbi_line (GstDeckLink2Input * self, GstBuffer * buffer,
    IDeckLinkVideoFrameAncillary * vanc_frame, gboolean hd, gboolean interlaced,
    gboolean f2, guint line)
{
  GstVideoAncillary gstanc;
  const guint8 *vancdata;
  int ret;

  ret =
      vanc_frame->GetBufferForVerticalBlankingLine (line, (void **) &vancdata);
  if (ret != S_OK) {
    GST_TRACE_OBJECT (self, "Failed getting VBI data line %u (field 2: %d): %d",
        line, f2, ret);
    return;
  }

  GST_TRACE_OBJECT (self, "Checking for VBI data on line %u (field 2: %d)",
      line, f2);
  gst_video_vbi_parser_add_line (self->vbi_parser, vancdata);

  while (gst_video_vbi_parser_get_ancillary (self->vbi_parser,
          &gstanc) == GST_VIDEO_VBI_PARSER_RESULT_OK) {
    GST_DEBUG_OBJECT (self,
        "Found DID %02x SDID/BN %02x DC %u on line %u (field 2: %d)",
        gstanc.DID, gstanc.SDID_block_number, gstanc.data_count, line, f2);

    if (self->output_vanc) {
      GST_DEBUG_OBJECT (self, "Adding ancillary meta to buffer");
      GstAncillaryMeta *meta = gst_buffer_add_ancillary_meta (buffer);

      meta->c_not_y_channel = hd ? 1 : 0;
      meta->line = line;
      meta->offset = 0xfff;

      meta->DID = with_parity (gstanc.DID);
      meta->SDID_block_number = with_parity (gstanc.SDID_block_number);
      meta->data_count = with_parity (gstanc.data_count);

      meta->data = g_new (guint16, gstanc.data_count);
      guint16 checksum = meta->DID + meta->SDID_block_number + meta->data_count;
      for (guint i = 0; i < gstanc.data_count; i++) {
        meta->data[i] = with_parity (gstanc.data[i]);
        checksum += meta->data[i];
      }

      meta->checksum = with_parity (checksum & 0x1FF);
    }

    switch (GST_VIDEO_ANCILLARY_DID16 (&gstanc)) {
      case GST_VIDEO_ANCILLARY_DID16_S334_EIA_708:
        if (self->output_cc) {
          GST_DEBUG_OBJECT (self, "Adding CEA-708 CDP meta to buffer");
          GST_MEMDUMP_OBJECT (self, "CDP", gstanc.data, gstanc.data_count);
          gst_buffer_add_video_caption_meta (buffer,
              GST_VIDEO_CAPTION_TYPE_CEA708_CDP, gstanc.data,
              gstanc.data_count);
        }
        break;
      case GST_VIDEO_ANCILLARY_DID16_S334_EIA_608:
        if (self->output_cc) {
          GST_DEBUG_OBJECT (self, "Adding CEA-608 meta to buffer");
          GST_MEMDUMP_OBJECT (self, "CEA608", gstanc.data, gstanc.data_count);
          gst_buffer_add_video_caption_meta (buffer,
              GST_VIDEO_CAPTION_TYPE_CEA608_S334_1A, gstanc.data,
              gstanc.data_count);
        }
        break;
      case GST_VIDEO_ANCILLARY_DID16_S2016_3_AFD_BAR:{
        if (self->output_afd_bar) {
          GstVideoAFDValue afd;
          gboolean is_letterbox;
          guint16 bar1, bar2;

          GST_DEBUG_OBJECT (self, "Adding AFD/Bar meta to buffer");
          GST_MEMDUMP_OBJECT (self, "AFD/Bar", gstanc.data, gstanc.data_count);

          if (gstanc.data_count < 8) {
            GST_WARNING_OBJECT (self, "AFD/Bar data too small");
            break;
          }

          self->aspect_ratio_flag = (gstanc.data[0] >> 2) & 0x1;

          afd = (GstVideoAFDValue) ((gstanc.data[0] >> 3) & 0xf);
          is_letterbox = ((gstanc.data[3] >> 4) & 0x3) == 0;
          bar1 = GST_READ_UINT16_BE (&gstanc.data[4]);
          bar2 = GST_READ_UINT16_BE (&gstanc.data[6]);

          gst_buffer_add_video_afd_meta (buffer, f2 ? 1 : 0,
              GST_VIDEO_AFD_SPEC_SMPTE_ST2016_1, afd);
          gst_buffer_add_video_bar_meta (buffer, f2 ? 1 : 0,
              is_letterbox, bar1, bar2);
        }
        break;
      }
      default:
        break;
    }
  }
}

static gboolean
extract_vbi_from_packet (GstDeckLink2Input * self, GstBuffer * buffer,
    IDeckLinkVideoFrameAncillaryPackets * packets)
{
  IDeckLinkAncillaryPacketIterator *it = nullptr;
  HRESULT hr;
  guint field2_start = 0, field2_end = 0;

  if (self->selected_mode.interlaced) {
    /* Match the field-2 VANC ranges used by extract_vbi(). */
    switch (gst_decklink2_get_real_display_mode (self->selected_mode.mode)) {
      case bmdModeNTSC:
      case bmdModeNTSC2398:
        field2_start = 264;
        field2_end = 284;
        break;
      case bmdModePAL:
        field2_start = 311;
        field2_end = 335;
        break;
      case bmdModeHD1080i50:
      case bmdModeHD1080i5994:
      case bmdModeHD1080i6000:
        field2_start = 561;
        field2_end = 583;
        break;
      default:
        break;
    }
  }

  hr = packets->GetPacketIterator (&it);
  if (hr != S_OK || !it) {
    GST_DECKLINK2_CLEAR_COM (it);
    return FALSE;
  }

  while (TRUE) {
    IDeckLinkAncillaryPacket *packet = nullptr;
    hr = it->Next (&packet);
    if (hr != S_OK || !packet) {
      GST_DECKLINK2_CLEAR_COM (packet);
      if (hr != S_FALSE) {
        GST_WARNING_OBJECT (self, "Failed to get ancillary packet: 0x%x",
            (guint) hr);
      }
      break;
    }

    const guint8 *payload = nullptr;
    guint size = 0;
    auto did = packet->GetDID ();
    auto sdid = packet->GetSDID ();
    auto line = packet->GetLineNumber ();
    gboolean f2 = field2_start && line >= field2_start && line <= field2_end;

    hr = packet->GetBytes (bmdAncillaryPacketFormatUInt8,
        (const void **) &payload, &size);
    if (hr != S_OK || (size && !payload) || size > 255) {
      GST_WARNING_OBJECT (self,
          "Invalid ancillary payload, DID %02x SDID %02x size %u: 0x%x",
          did, sdid, size, (guint) hr);
      packet->Release ();
      continue;
    }

    GST_DEBUG_OBJECT (self,
        "Found DID %02x SDID %02x size %u on line %u, stream %u",
        (guint) did, (guint) sdid, size, line, packet->GetDataStreamIndex ());

    if (self->output_vanc) {
      GstAncillaryMeta *meta = gst_buffer_add_ancillary_meta (buffer);

      if (self->selected_mode.interlaced && field2_start && line > 0) {
        meta->field = f2 ? GST_ANCILLARY_META_FIELD_INTERLACED_SECOND :
            GST_ANCILLARY_META_FIELD_INTERLACED_FIRST;
      }

      meta->line = line > 0 && line < 0x7fe ? line : 0x7ff;
      meta->offset = 0xfff;
      meta->DID = with_parity (did);
      meta->SDID_block_number = with_parity (sdid);
      meta->data_count = with_parity ((guint8) size);
      meta->data = g_new (guint16, size);

      guint checksum = meta->DID + meta->SDID_block_number + meta->data_count;
      for (guint i = 0; i < size; i++) {
        meta->data[i] = with_parity (payload[i]);
        checksum += meta->data[i];
      }
      checksum &= 0x1ff;
      meta->checksum = checksum | ((!(checksum >> 8)) << 9);
    }

    switch ((did << 8) | sdid) {
      case GST_VIDEO_ANCILLARY_DID16_S334_EIA_708:
        if (self->output_cc && size > 0) {
          GST_DEBUG_OBJECT (self, "Adding CEA-708 CDP meta to buffer");
          GST_MEMDUMP_OBJECT (self, "CDP", payload, size);
          gst_buffer_add_video_caption_meta (buffer,
              GST_VIDEO_CAPTION_TYPE_CEA708_CDP, payload, size);
        }
        break;
      case GST_VIDEO_ANCILLARY_DID16_S334_EIA_608:
        if (self->output_cc && size > 0) {
          GST_DEBUG_OBJECT (self, "Adding CEA-608 meta to buffer");
          GST_MEMDUMP_OBJECT (self, "CEA608", payload, size);
          gst_buffer_add_video_caption_meta (buffer,
              GST_VIDEO_CAPTION_TYPE_CEA608_S334_1A, payload, size);
        }
        break;
      case GST_VIDEO_ANCILLARY_DID16_S2016_3_AFD_BAR:{
        if (self->output_afd_bar) {
          GstVideoAFDValue afd;
          gboolean is_letterbox;
          guint16 bar1, bar2;

          GST_DEBUG_OBJECT (self, "Adding AFD/Bar meta to buffer");
          GST_MEMDUMP_OBJECT (self, "AFD/Bar", payload, size);

          if (size < 8) {
            GST_WARNING_OBJECT (self, "AFD/Bar data too small");
            break;
          }

          self->aspect_ratio_flag = (payload[0] >> 2) & 0x1;

          afd = (GstVideoAFDValue) ((payload[0] >> 3) & 0xf);
          is_letterbox = ((payload[3] >> 4) & 0x3) == 0;
          bar1 = GST_READ_UINT16_BE (&payload[4]);
          bar2 = GST_READ_UINT16_BE (&payload[6]);

          gst_buffer_add_video_afd_meta (buffer, f2 ? 1 : 0,
              GST_VIDEO_AFD_SPEC_SMPTE_ST2016_1, afd);
          gst_buffer_add_video_bar_meta (buffer, f2 ? 1 : 0,
              is_letterbox, bar1, bar2);
        }
        break;
      }
      default:
        break;
    }

    packet->Release ();
  }

  it->Release ();

  return TRUE;
}

static void
extract_vbi (GstDeckLink2Input * self, GstBuffer * buffer,
    IDeckLinkVideoInputFrame * frame)
{
  IDeckLinkVideoFrameAncillary *vanc_frame = NULL;
  guint line_start = 0, line_end = 0, f2_line_start = 0, f2_line_end = 0;
  int ret;
  GstVideoFormat videoformat;
  BMDDisplayMode mode_enum;
  GstDeckLink2DisplayMode mode;

  ret = frame->GetAncillaryData (&vanc_frame);
  if (ret != S_OK || !vanc_frame) {
    GST_TRACE_OBJECT (self, "Failed getting VBI data: %d", ret);
    if (vanc_frame)
      vanc_frame->Release ();

    return;
  }

  videoformat =
      gst_decklink2_video_format_from_pixel_format (vanc_frame->GetPixelFormat
      ());
  mode_enum = vanc_frame->GetDisplayMode ();
  if (!gst_decklink2_input_get_display_mode_from_native (self,
          mode_enum, &mode)) {
    vanc_frame->Release ();
    return;
  }

  if (videoformat == GST_VIDEO_FORMAT_UNKNOWN) {
    GST_DEBUG_OBJECT (self, "Unknown video format for ancillary data");
    vanc_frame->Release ();
    return;
  }

  if ((videoformat != self->anc_vformat || mode.width != self->anc_width)
      && self->vbi_parser) {
    gst_video_vbi_parser_free (self->vbi_parser);
    self->vbi_parser = NULL;
  }

  if (self->vbi_parser == NULL) {
    self->vbi_parser = gst_video_vbi_parser_new (videoformat, mode.width);
    self->anc_vformat = videoformat;
    self->anc_width = mode.width;
  }

  switch (mode_enum) {
    case bmdModeNTSC:
    case bmdModeNTSC2398:
      line_start = 4;
      line_end = 21;
      f2_line_start = 264;
      f2_line_end = 284;
      break;
    case bmdModeNTSCp:
      line_start = 4;
      line_end = 43;
      break;
    case bmdModePAL:
      line_start = 1;
      line_end = 22;
      f2_line_start = 311;
      f2_line_end = 335;
      break;
    case bmdModePALp:
      line_start = 1;
      line_end = 45;
      break;
    case bmdModeHD1080p2398:
    case bmdModeHD1080p24:
    case bmdModeHD1080p25:
    case bmdModeHD1080p2997:
    case bmdModeHD1080p30:
      line_start = 1;
      line_end = 41;
      break;
    case bmdModeHD1080p50:
    case bmdModeHD1080p5994:
    case bmdModeHD1080p6000:
      line_start = 1;
      line_end = 19;
      break;
    case bmdModeHD1080i50:
    case bmdModeHD1080i5994:
    case bmdModeHD1080i6000:
      line_start = 1;
      line_end = 20;
      f2_line_start = 561;
      f2_line_end = 583;
      break;
    case bmdModeHD720p50:
    case bmdModeHD720p5994:
    case bmdModeHD720p60:
      line_start = 1;
      line_end = 25;
      break;
    case bmdMode4K2160p2398:
    case bmdMode4K2160p24:
    case bmdMode4K2160p25:
    case bmdMode4K2160p2997:
    case bmdMode4K2160p30:
    case bmdMode4K2160p50:
    case bmdMode4K2160p5994:
    case bmdMode4K2160p60:
      line_start = 1;
      line_end = 41;
      break;
    case bmdMode2kDCI2398:
    case bmdMode2kDCI24:
    case bmdMode2kDCI25:
    case bmdMode2kDCI2997:
    case bmdMode2kDCI30:
    case bmdMode2kDCI50:
    case bmdMode2kDCI5994:
    case bmdMode2kDCI60:
      line_start = 1;
      // TODO: Correct end of 50+fps?
      line_end = 41;
      break;
    case bmdMode4kDCI2398:
    case bmdMode4kDCI24:
    case bmdMode4kDCI25:
    case bmdMode4kDCI2997:
    case bmdMode4kDCI30:
    case bmdMode4kDCI50:
    case bmdMode4kDCI5994:
    case bmdMode4kDCI60:
      line_start = 1;
      // TODO: Correct end of 50+fps?
      line_end = 41;
      break;
    case bmdMode8K4320p2398:
    case bmdMode8K4320p24:
    case bmdMode8K4320p25:
    case bmdMode8K4320p2997:
    case bmdMode8K4320p30:
    case bmdMode8K4320p50:
    case bmdMode8K4320p5994:
    case bmdMode8K4320p60:
    case bmdMode8kDCI2398:
    case bmdMode8kDCI24:
    case bmdMode8kDCI25:
    case bmdMode8kDCI2997:
    case bmdMode8kDCI30:
    case bmdMode8kDCI50:
    case bmdMode8kDCI5994:
    case bmdMode8kDCI60:
      // FIXME: Untested
      line_start = 1;
      // TODO: Correct end of 50+fps?
      line_end = 41;
      break;

    default:
      // Unknown, unsupported
      break;
  }

  if (line_start == 0) {
    GST_DEBUG_OBJECT (self, "Unsupported mode for extracting VBI");
    vanc_frame->Release ();
    return;
  }

  GST_DEBUG_OBJECT (self,
      "Checking for ancillary data in VBI (lines %u-%u, %u-%u)", line_start,
      line_end, f2_line_start, f2_line_end);

  /* Extract all progressive / field 1 VANC */
  for (guint line = line_start; line <= line_end; line++) {
    extract_vbi_line (self, buffer, vanc_frame, self->anc_width < 1280,
        mode.interlaced, FALSE, line);
  }

  /* Do the same for field 2 in case of interlaced content */
  if (f2_line_start > 0 && mode.interlaced) {
    for (guint line = f2_line_start; line <= f2_line_end; line++) {
      extract_vbi_line (self,
          buffer, vanc_frame, self->anc_width < 1280, TRUE, TRUE, line);
    }
  }

  vanc_frame->Release ();
}

static void
gst_decklink2_input_update_time_mapping (GstDeckLink2Input * self,
    GstClockTime capture_time, GstClockTime stream_time)
{
  auto & calibration = self->priv->calibration;
  self->have_time_mapping = TRUE;

  if (self->window_skip_count == 0) {
    GstClockTime num, den, b, xbase;
    gdouble r_squared;

    self->times[2 * self->window_fill] = stream_time;
    self->times[2 * self->window_fill + 1] = capture_time;

    self->window_fill++;
    self->window_skip_count++;
    if (self->window_skip_count >= self->window_skip)
      self->window_skip_count = 0;

    if (self->window_fill >= self->window_size) {
      guint fps =
          ((gdouble) self->selected_mode.fps_n + self->selected_mode.fps_d -
          1) / ((gdouble) self->selected_mode.fps_d);

      /* Start by updating first every frame, once full every second frame,
       * etc. until we update once every 4 seconds */
      if (self->window_skip < 4 * fps)
        self->window_skip *= 2;
      if (self->window_skip >= 4 * fps)
        self->window_skip = 4 * fps;

      self->window_fill = 0;
      self->window_filled = TRUE;
    }

    /* First sample ever, create some basic mapping to start */
    if (!self->window_filled && self->window_fill == 1) {
      self->current_time_mapping.xbase = stream_time;
      self->current_time_mapping.b = capture_time;
      self->current_time_mapping.num = 1;
      self->current_time_mapping.den = 1;
      self->next_time_mapping_pending = FALSE;
    }

    /* Only bother calculating anything here once we had enough measurements,
     * i.e. let's take the window size as a start */
    if (self->window_filled &&
        gst_calculate_linear_regression (self->times, &self->times[128],
            self->window_size, &num, &den, &b, &xbase, &r_squared)) {

      GST_LOG_OBJECT (self,
          "Calculated new time mapping: pipeline time = %lf * (stream time - %"
          G_GUINT64_FORMAT ") + %" G_GUINT64_FORMAT " (%lf)",
          ((gdouble) num) / ((gdouble) den), xbase, b, r_squared);

      calibration.generation++;
      calibration.r_squared = r_squared;
      self->next_time_mapping.xbase = xbase;
      self->next_time_mapping.b = b;
      self->next_time_mapping.num = num;
      self->next_time_mapping.den = den;
      self->next_time_mapping_pending = TRUE;
    }
  } else {
    self->window_skip_count++;
    if (self->window_skip_count >= self->window_skip)
      self->window_skip_count = 0;
  }

  if (self->next_time_mapping_pending) {
    GstClockTime expected, new_calculated, diff, max_diff;

    expected =
        gst_clock_adjust_with_calibration (NULL, stream_time,
        self->current_time_mapping.xbase, self->current_time_mapping.b,
        self->current_time_mapping.num, self->current_time_mapping.den);
    new_calculated =
        gst_clock_adjust_with_calibration (NULL, stream_time,
        self->next_time_mapping.xbase, self->next_time_mapping.b,
        self->next_time_mapping.num, self->next_time_mapping.den);

    calibration.diff = GST_CLOCK_DIFF (expected, new_calculated);

    if (new_calculated > expected)
      diff = new_calculated - expected;
    else
      diff = expected - new_calculated;

    /* At most 5% frame duration change per update */
    max_diff =
        gst_util_uint64_scale (GST_SECOND / 20, self->selected_mode.fps_d,
        self->selected_mode.fps_n);

    GST_LOG_OBJECT (self,
        "New time mapping causes difference of %" GST_TIME_FORMAT,
        GST_TIME_ARGS (diff));
    GST_LOG_OBJECT (self, "Maximum allowed per frame %" GST_TIME_FORMAT,
        GST_TIME_ARGS (max_diff));

    if (diff > max_diff) {
      /* adjust so that we move that much closer */
      if (new_calculated > expected) {
        self->current_time_mapping.b = expected + max_diff;
        self->current_time_mapping.xbase = stream_time;
      } else {
        self->current_time_mapping.b = expected - max_diff;
        self->current_time_mapping.xbase = stream_time;
      }
    } else {
      self->current_time_mapping.xbase = self->next_time_mapping.xbase;
      self->current_time_mapping.b = self->next_time_mapping.b;
      self->current_time_mapping.num = self->next_time_mapping.num;
      self->current_time_mapping.den = self->next_time_mapping.den;
      self->next_time_mapping_pending = FALSE;
    }
  }

  calibration.have_mapping = self->have_time_mapping;
  calibration.window_filled = self->window_filled;
  calibration.xbase = self->current_time_mapping.xbase;
  calibration.b = self->current_time_mapping.b;
  calibration.num = self->current_time_mapping.num;
  calibration.den = self->current_time_mapping.den;
  calibration.slope = (gdouble) calibration.num / calibration.den;
}

/* *INDENT-OFF* */
/* Ported from gst_decklink_video_src_got_frame() */
template<typename ID, typename T>
static void
read_frame_metadata (GstDeckLink2Input * self, IDeckLinkVideoInputFrame * frame,
    T * frame_metadata, GstDeckLink2InputMetadata & f)
{
  HRESULT dk_ret;
  auto get_int = [&](BMDDeckLinkFrameMetadataID id, int64_t * value) {
    return frame_metadata->GetInt ((ID) id, value);
  };

  auto get_float = [&](BMDDeckLinkFrameMetadataID id, double *value) {
    *value = 0;
    return frame_metadata->GetFloat ((ID) id, value);
  };

  gint64 colorspace = 0;
  dk_ret = get_int (bmdDeckLinkFrameMetadataColorspace, &colorspace);
  GST_LOG_OBJECT (self, "ret %x colorspace 0x%" G_GINT64_FORMAT,
      (gint) dk_ret, colorspace);

  if (dk_ret == S_OK) {
    switch (colorspace) {
      case bmdColorspaceRec601:
        f.colorimetry.matrix = GST_VIDEO_COLOR_MATRIX_BT601;
        f.colorimetry.primaries = GST_VIDEO_COLOR_PRIMARIES_SMPTE170M;
        f.colorimetry.transfer = GST_VIDEO_TRANSFER_BT601;
        break;
      case bmdColorspaceRec709:
        f.colorimetry.matrix = GST_VIDEO_COLOR_MATRIX_BT709;
        f.colorimetry.primaries = GST_VIDEO_COLOR_PRIMARIES_BT709;
        f.colorimetry.transfer = GST_VIDEO_TRANSFER_BT709;
        break;
      case bmdColorspaceRec2020:
        f.colorimetry.matrix = GST_VIDEO_COLOR_MATRIX_BT2020;
        f.colorimetry.primaries = GST_VIDEO_COLOR_PRIMARIES_BT2020;
        f.colorimetry.transfer = GST_VIDEO_TRANSFER_BT2020_12;
        break;
      default:
        break;
    }
  }

  if (frame->GetFlags () & bmdFrameContainsHDRMetadata) {
    gboolean undefined_mastering_info = TRUE;
    double max_cll = 0, max_fll = 0, x = 0, y = 0;
    gint64 tf = 0;

    f.colorimetry.matrix = GST_VIDEO_COLOR_MATRIX_BT2020;
    f.colorimetry.primaries = GST_VIDEO_COLOR_PRIMARIES_BT2020;
    f.colorimetry.transfer = GST_VIDEO_TRANSFER_BT2020_12;

    dk_ret =
        get_float (bmdDeckLinkFrameMetadataHDRMaximumContentLightLevel,
        &max_cll);
    dk_ret |=
        get_float (bmdDeckLinkFrameMetadataHDRMaximumFrameAverageLightLevel,
        &max_fll);
    GST_LOG_OBJECT (self, "ret %x maxcll %f maxfll %f", (gint) dk_ret, max_cll,
        max_fll);
    if (dk_ret == S_OK && ((guint16) max_cll > 0 || (guint16) max_fll > 0)) {
      f.have_light_level = TRUE;
      f.light_level.max_content_light_level = (guint16) max_cll;
      f.light_level.max_frame_average_light_level = (guint16) max_fll;
    }

    dk_ret = get_float (bmdDeckLinkFrameMetadataHDRDisplayPrimariesRedX, &x);
    dk_ret |= get_float (bmdDeckLinkFrameMetadataHDRDisplayPrimariesRedY, &y);
    f.mastering_info.display_primaries[0].x = (guint16) (x * 50000.0);
    f.mastering_info.display_primaries[0].y = (guint16) (y * 50000.0);
    undefined_mastering_info &= x <= 0 && y <= 0;
    dk_ret |= get_float (bmdDeckLinkFrameMetadataHDRDisplayPrimariesGreenX, &x);
    dk_ret |= get_float (bmdDeckLinkFrameMetadataHDRDisplayPrimariesGreenY, &y);
    f.mastering_info.display_primaries[1].x = (guint16) (x * 50000.0);
    f.mastering_info.display_primaries[1].y = (guint16) (y * 50000.0);
    undefined_mastering_info &= x <= 0 && y <= 0;
    dk_ret |= get_float (bmdDeckLinkFrameMetadataHDRDisplayPrimariesBlueX, &x);
    dk_ret |= get_float (bmdDeckLinkFrameMetadataHDRDisplayPrimariesBlueY, &y);
    f.mastering_info.display_primaries[2].x = (guint16) (x * 50000.0);
    f.mastering_info.display_primaries[2].y = (guint16) (y * 50000.0);
    undefined_mastering_info &= x <= 0 && y <= 0;
    dk_ret |= get_float (bmdDeckLinkFrameMetadataHDRWhitePointX, &x);
    dk_ret |= get_float (bmdDeckLinkFrameMetadataHDRWhitePointY, &y);
    f.mastering_info.white_point.x = (guint16) (x * 50000.0);
    f.mastering_info.white_point.y = (guint16) (y * 50000.0);
    undefined_mastering_info &= x <= 0 && y <= 0;
    dk_ret |=
        get_float (bmdDeckLinkFrameMetadataHDRMaxDisplayMasteringLuminance, &x);
    dk_ret |=
        get_float (bmdDeckLinkFrameMetadataHDRMinDisplayMasteringLuminance, &y);
    f.mastering_info.max_display_mastering_luminance =
        (guint32) (x * 10000.0 / 65535.0);
    f.mastering_info.min_display_mastering_luminance =
        (guint32) (y * 10000.0 / 6.5535);
    undefined_mastering_info &= x <= 0 && y <= 0;
    GST_LOG_OBJECT (self, "ret 0x%x mastering_info "
        "R:%u,%u G:%u,%u B:%u,%u W:%u,%u", (gint) dk_ret,
        f.mastering_info.display_primaries[0].x,
        f.mastering_info.display_primaries[0].y,
        f.mastering_info.display_primaries[1].x,
        f.mastering_info.display_primaries[1].y,
        f.mastering_info.display_primaries[2].x,
        f.mastering_info.display_primaries[2].y,
        f.mastering_info.white_point.x, f.mastering_info.white_point.y);
    if (dk_ret == S_OK && !undefined_mastering_info)
      f.have_mastering_info = TRUE;

    dk_ret =
        get_int (bmdDeckLinkFrameMetadataHDRElectroOpticalTransferFunc, &tf);
    GST_LOG_OBJECT (self, "ret %x transfer func 0x%" G_GINT64_FORMAT,
        (gint) dk_ret, tf);

    if (dk_ret == S_OK) {
      /* as specified in CTA 861.3-A */
      switch (tf) {
        case 0x0:              /* traditional gamma, SDR luminance range */
        case 0x1:              /* traditional gamma, HDR luminance range */
          f.colorimetry.transfer = GST_VIDEO_TRANSFER_BT2020_12;
          break;
        case 0x2:              /* PQ */
          f.colorimetry.transfer = GST_VIDEO_TRANSFER_SMPTE2084;
          break;
        case 0x3:              /* HLG */
          f.colorimetry.transfer = GST_VIDEO_TRANSFER_ARIB_STD_B67;
          break;
        default:
          f.colorimetry.transfer = GST_VIDEO_TRANSFER_UNKNOWN;
          break;
      }
    }
  }
}
/* *INDENT-ON* */

static GstCaps *
gst_decklink2_input_get_frame_caps (GstDeckLink2Input * self,
    IDeckLinkVideoInputFrame * frame)
{
  GstDeckLink2InputMetadata metadata = { };
  auto flags = gst_decklink2_get_colorspace_flags (&self->selected_mode,
      &self->color_caps);

  if (flags & bmdDisplayModeColorspaceRec601)
    gst_video_colorimetry_from_string (&metadata.colorimetry, "bt601");
  if (flags & bmdDisplayModeColorspaceRec709)
    gst_video_colorimetry_from_string (&metadata.colorimetry, "bt709");
  if (flags & bmdDisplayModeColorspaceRec2020)
    gst_video_colorimetry_from_string (&metadata.colorimetry, "bt2020");
  metadata.colorimetry.range = GST_VIDEO_COLOR_RANGE_16_235;

  IDeckLinkVideoFrameMetadataExtensions *ext = nullptr;
  IDeckLinkVideoFrameMetadataExtensions_v11_5 *ext_11_5 = nullptr;
  if (frame->QueryInterface (IID_IDeckLinkVideoFrameMetadataExtensions,
          (void **) &ext) == S_OK) {
    read_frame_metadata < BMDDeckLinkFrameMetadataID > (self, frame, ext,
        metadata);
    ext->Release ();
  } else
      if (frame->QueryInterface
      (IID_IDeckLinkVideoFrameMetadataExtensions_v11_5,
          (void **) &ext_11_5) == S_OK) {
    read_frame_metadata < BMDDeckLinkFrameMetadataID_v11_5 > (self, frame,
        ext_11_5, metadata);
    ext_11_5->Release ();
  }

  auto priv = self->priv;
  const auto & cached = priv->frame_metadata;
  if (priv->frame_caps &&
      gst_video_colorimetry_is_equal (&metadata.colorimetry,
          &cached.colorimetry) &&
      metadata.have_mastering_info == cached.have_mastering_info &&
      (!metadata.have_mastering_info ||
          gst_video_mastering_display_info_is_equal (&metadata.mastering_info,
              &cached.mastering_info)) &&
      metadata.have_light_level == cached.have_light_level &&
      (!metadata.have_light_level ||
          gst_video_content_light_level_is_equal (&metadata.light_level,
              &cached.light_level))) {
    return gst_caps_ref (priv->frame_caps);
  }

  auto new_caps = gst_caps_copy (self->selected_video_caps);
  auto color = gst_video_colorimetry_to_string (&metadata.colorimetry);
  if (color) {
    gst_caps_set_simple (new_caps,
        "colorimetry", G_TYPE_STRING, color, nullptr);
    g_free (color);
  }

  if (metadata.have_mastering_info)
    gst_video_mastering_display_info_add_to_caps (&metadata.mastering_info,
        new_caps);
  if (metadata.have_light_level)
    gst_video_content_light_level_add_to_caps (&metadata.light_level, new_caps);

  gst_clear_caps (&priv->frame_caps);
  priv->frame_caps = new_caps;
  priv->frame_metadata = metadata;

  return gst_caps_ref (priv->frame_caps);
}

static void
gst_decklink2_input_on_frame_arrived (GstDeckLink2Input * self,
    IDeckLinkVideoInputFrame * frame, IDeckLinkAudioInputPacket * packet)
{
  GstDeckLink2InputPrivate *priv = self->priv;
  static GstStaticCaps stream_reference =
      GST_STATIC_CAPS ("timestamp/x-decklink-stream");
  static GstStaticCaps hardware_reference =
      GST_STATIC_CAPS ("timestamp/x-decklink-hardware");
  HRESULT hr;
  GstClockTime capture_time = 0;
  GstClockTime base_time = 0;
  GstBuffer *buffer = NULL;
  GstClock *clock = NULL;
  BMDTimeValue hw_now, dummy, dummy2;
  BMDTimeValue stream_time = GST_CLOCK_TIME_NONE;
  BMDTimeValue stream_dur;
  BMDFrameFlags flags = bmdFrameFlagDefault;
  GstDeckLink2InputStats stats;

  gst_decklink2_input_stats_init (&stats);

  std::unique_lock < std::mutex > lk (priv->lock);
  if (priv->stopping) {
    GST_DEBUG_OBJECT (self, "We are stopping now");
    return;
  }

  if (priv->api_failed)
    return;

  if (frame) {
    gboolean has_signal;
    flags = frame->GetFlags ();

    if ((flags & bmdFrameHasNoInputSource) != 0)
      has_signal = FALSE;
    else
      has_signal = TRUE;

    GST_TRACE_OBJECT (self, "Frame has signal: %d", has_signal);

    if (priv->was_restarted) {
      /* Ignores no-signal flag of the first frame after we do restart */
      priv->was_restarted = false;
    } else {
      if (!has_signal) {
        priv->signal = false;
      } else if (!priv->signal) {
        GST_LOG_OBJECT (self, "Got first frame, reset timing map");
        priv->signal = true;
        gst_decklink2_input_reset_time_mapping (self);
        gst_vec_deque_clear (self->audio_queue);
        gst_vec_deque_clear (self->queue);
        self->next_audio_offset = INVALID_AUDIO_OFFSET;
      } else {
        priv->signal = true;
      }
    }
  }

  hr = gst_decklink2_input_get_reference_clock (self, GST_SECOND,
      &hw_now, &dummy, &dummy2);
  if (!gst_decklink2_result (hr)) {
    GST_WARNING_OBJECT (self, "Couldn't get H/W reference clock");
    hw_now = GST_CLOCK_TIME_NONE;
  }

  if (self->client)
    clock = gst_element_get_clock (GST_ELEMENT (self->client));

  if (!clock) {
    GST_WARNING_OBJECT (self,
        "Frame arrived but we dont have configured clock");
    return;
  }

  base_time = gst_element_get_base_time (GST_ELEMENT (self->client));
  capture_time = gst_clock_get_time (clock);
  gst_object_unref (clock);
  if (capture_time >= base_time)
    capture_time -= base_time;

  if (!GST_CLOCK_TIME_IS_VALID (self->start_time))
    self->start_time = capture_time;

  if (GST_CLOCK_TIME_IS_VALID (self->skip_first_time)) {
    GstClockTime diff = capture_time - self->start_time;
    if (diff < self->skip_first_time) {
      GST_DEBUG_OBJECT (self,
          "Skipping frame as requested: %" GST_TIME_FORMAT " < %"
          GST_TIME_FORMAT, GST_TIME_ARGS (capture_time),
          GST_TIME_ARGS (self->skip_first_time + self->start_time));
      return;
    }

    GST_DEBUG_OBJECT (self, "All frames were skipped as requested");
    self->skip_first_time = GST_CLOCK_TIME_NONE;
  }

  if (frame) {
    void *frame_data;
    gsize frame_size;
    BMDTimeValue frame_time, frame_dur;
    IDeckLinkTimecode *timecode = NULL;
    GstClockTime pts, dur;

    hr = frame->GetBytes (&frame_data);
    if (!gst_decklink2_result (hr)) {
      GST_WARNING_OBJECT (self, "Couldn't get byte from frame");
      return;
    }

    auto frame_height = frame->GetHeight ();

    frame_size = frame_height * frame->GetRowBytes ();
    buffer = gst_buffer_new_wrapped_full (GST_MEMORY_FLAG_READONLY,
        frame_data, frame_size, 0, frame_size, frame,
        (GDestroyNotify) gst_decklink2_frame_free);
    frame->AddRef ();

    hr = frame->GetHardwareReferenceTimestamp (GST_SECOND,
        &frame_time, &frame_dur);
    if (gst_decklink2_result (hr)) {
      stats.hardware_reference_time = frame_time;
      GstCaps *caps = gst_static_caps_get (&hardware_reference);
      gst_buffer_add_reference_timestamp_meta (buffer, caps, frame_time,
          frame_dur);
      gst_caps_unref (caps);

      GST_DEBUG_OBJECT (self, "Hardware timestamp: %" GST_TIME_FORMAT
          ", hardware reference timestamp: %" GST_TIME_FORMAT,
          GST_TIME_ARGS (hw_now), GST_TIME_ARGS (frame_time));

      if (GST_CLOCK_TIME_IS_VALID (hw_now) && hw_now > frame_time) {
        GstClockTime diff = hw_now - frame_time;
        if (capture_time >= diff)
          capture_time -= diff;
      }
    }

    hr = frame->GetStreamTime (&stream_time, &stream_dur, GST_SECOND);
    if (gst_decklink2_result (hr)) {
      stats.stream_time = stream_time;
      GstCaps *caps = gst_static_caps_get (&stream_reference);
      gst_buffer_add_reference_timestamp_meta (buffer, caps, stream_time,
          stream_dur);
      gst_caps_unref (caps);

      gst_decklink2_input_update_time_mapping (self, capture_time, stream_time);
      pts = gst_clock_adjust_with_calibration (NULL, stream_time,
          self->current_time_mapping.xbase, self->current_time_mapping.b,
          self->current_time_mapping.num, self->current_time_mapping.den);
      dur = gst_util_uint64_scale (stream_dur,
          self->current_time_mapping.num, self->current_time_mapping.den);

      GST_DEBUG_OBJECT (self, "Video timestamp, original: %" GST_TIME_FORMAT
          ", calibrated: %" GST_TIME_FORMAT, GST_TIME_ARGS (stream_time),
          GST_TIME_ARGS (pts));
    } else {
      pts = capture_time;
      dur = GST_CLOCK_TIME_NONE;
    }

    if (!priv->signal) {
      GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_GAP);
    } else {
      if (self->output_cc || self->output_afd_bar || self->output_vanc) {
        IDeckLinkVideoFrameAncillaryPackets *packets = nullptr;
        hr = frame->QueryInterface (IID_IDeckLinkVideoFrameAncillaryPackets,
            (void **) &packets);
        gboolean used_packet_api = FALSE;
        if (hr == S_OK && packets)
          used_packet_api = extract_vbi_from_packet (self, buffer, packets);
        GST_DECKLINK2_CLEAR_COM (packets);

        if (!used_packet_api)
          extract_vbi (self, buffer, frame);

        if (self->aspect_ratio_flag != -1 && self->auto_detect) {
          BMDDisplayMode mode = self->selected_mode.mode;

          switch (self->selected_mode.mode) {
            case bmdModeNTSC:
            case bmdModeNTSC_W:
              mode = self->aspect_ratio_flag ? bmdModeNTSC_W : bmdModeNTSC;
              break;
            case bmdModeNTSC2398:
            case bmdModeNTSC2398_W:
              mode = self->aspect_ratio_flag ?
                  bmdModeNTSC2398_W : bmdModeNTSC2398;
              break;
            case bmdModePAL:
            case bmdModePAL_W:
              mode = self->aspect_ratio_flag ? bmdModePAL_W : bmdModePAL;
              break;
            case bmdModeNTSCp:
            case bmdModeNTSCp_W:
              mode = self->aspect_ratio_flag ? bmdModeNTSCp_W : bmdModeNTSCp;
              break;
            case bmdModePALp:
            case bmdModePALp_W:
              mode = self->aspect_ratio_flag ? bmdModePALp_W : bmdModePALp;
              break;
            default:
              break;
          }

          if (mode != self->selected_mode.mode) {
            GstDeckLink2DisplayMode new_mode;

            if (!gst_decklink2_input_get_display_mode_from_native (self,
                    mode, &new_mode)) {
              if (!priv->aspect_ratio_mismatch_warned) {
                GST_WARNING_OBJECT (self,
                    "Unable to update display mode for aspect ratio");
                priv->aspect_ratio_mismatch_warned = true;
              }
            } else {
              priv->aspect_ratio_mismatch_warned = false;
              auto video_format =
                  gst_decklink2_video_format_from_pixel_format
                  (self->pixel_format);
              auto caps = gst_decklink2_get_caps_from_mode (&new_mode);
              gst_caps_set_simple (caps, "format", G_TYPE_STRING,
                  gst_video_format_to_string (video_format), NULL);

              GST_DEBUG_OBJECT (self, "Update caps %" GST_PTR_FORMAT " to %"
                  GST_PTR_FORMAT, self->selected_video_caps, caps);
              self->selected_mode = new_mode;
              gst_caps_replace (&self->selected_video_caps, caps);
              gst_clear_caps (&priv->frame_caps);
              gst_caps_unref (caps);
            }
          }
        }
      }
    }

    gboolean high_frame_rate_tc = FALSE;
    if (self->timecode_format == bmdTimecodeRP188Any) {
      const BMDTimecodeFormat formats[] = {
        bmdTimecodeRP188HighFrameRate, bmdTimecodeRP188VITC1,
        bmdTimecodeRP188LTC, bmdTimecodeRP188VITC2
      };

      for (guint i = 0; i < G_N_ELEMENTS (formats); i++) {
        hr = frame->GetTimecode (formats[i], &timecode);
        if (hr == S_OK) {
          high_frame_rate_tc = formats[i] == bmdTimecodeRP188HighFrameRate;
          break;
        }
      }
    } else {
      hr = frame->GetTimecode (self->timecode_format, &timecode);
    }

    if (hr == S_OK) {
      guint8 h, m, s, f;
      hr = timecode->GetComponents (&h, &m, &s, &f);
      if (gst_decklink2_result (hr)) {
        GstVideoTimeCodeFlags tc_flags = GST_VIDEO_TIME_CODE_FLAGS_NONE;
        guint field_count = 0;
        auto decklink_tc_flags = timecode->GetFlags ();

        if (((double) self->selected_mode.fps_n) /
            ((double) self->selected_mode.fps_d) > 30.0 &&
            !high_frame_rate_tc) {
          f = f * 2 + ((decklink_tc_flags & bmdTimecodeFieldMark) ? 1 : 0);
        }

        if (self->selected_mode.interlaced) {
          tc_flags = (GstVideoTimeCodeFlags) (tc_flags |
              GST_VIDEO_TIME_CODE_FLAGS_INTERLACED);
          field_count = 1;
        }

        if ((decklink_tc_flags & bmdTimecodeIsDropFrame) ==
            bmdTimecodeIsDropFrame) {
          tc_flags = (GstVideoTimeCodeFlags) (tc_flags |
              GST_VIDEO_TIME_CODE_FLAGS_DROP_FRAME);
        }

        gst_buffer_add_video_time_code_meta_full (buffer,
            self->selected_mode.fps_n, self->selected_mode.fps_d,
            nullptr, tc_flags, h, m, s, f, field_count);
      }

      timecode->Release ();
    }

    if (self->selected_mode.interlaced) {
      GST_BUFFER_FLAG_SET (buffer, GST_VIDEO_BUFFER_FLAG_INTERLACED);
      if (self->selected_mode.tff)
        GST_BUFFER_FLAG_SET (buffer, GST_VIDEO_BUFFER_FLAG_TFF);
    }

    if (self->discont) {
      GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DISCONT);
      self->discont = FALSE;
    }

    GST_BUFFER_DTS (buffer) = GST_CLOCK_TIME_NONE;
    stats.hardware_time = hw_now;
    stats.capture_time = capture_time;
    stats.calibrated_time = pts;
    stats.calibration = priv->calibration;
    GST_BUFFER_PTS (buffer) = pts;
    GST_BUFFER_DURATION (buffer) = dur;
  }

  if (packet) {
    GstBuffer *audio_buf = NULL;
    void *packet_data;
    BMDTimeValue packet_time;
    guint64 audio_offset, audio_offset_end;
    gsize audio_buf_size;
    GstDecklink2InputAudioData audio_data;

    if (!self->have_time_mapping) {
      GST_DEBUG_OBJECT (self,
          "Dropping audio packet due to missing time mapping");
      goto out;
    }

    long sample_count = packet->GetSampleFrameCount ();
    if (sample_count == 0) {
      GST_DEBUG_OBJECT (self, "Empty audio packet");
      goto out;
    }

    hr = packet->GetPacketTime (&packet_time, self->audio_info.rate);
    if (!gst_decklink2_result (hr)) {
      GST_WARNING_OBJECT (self, "Unknown audio packet time");
      goto out;
    }

    hr = packet->GetBytes (&packet_data);
    if (!gst_decklink2_result (hr)) {
      GST_WARNING_OBJECT (self, "Couldn't get audio packet data");
      goto out;
    }

    audio_offset = packet_time;
    audio_offset_end = audio_offset + sample_count;
    audio_buf_size = self->audio_info.bpf * sample_count;
    packet->AddRef ();

    audio_buf = gst_buffer_new_wrapped_full (GST_MEMORY_FLAG_READONLY,
        packet_data, audio_buf_size, 0, audio_buf_size, packet,
        [](gpointer data) {
          auto packet = (IDeckLinkAudioInputPacket *) data;
          packet->Release ();
        }
    );

    {
      GstClockTime audio_pts;
      GstClockTime packet_time_in_gst;

      packet_time_in_gst = gst_util_uint64_scale (GST_SECOND, packet_time,
          self->audio_info.rate);

      audio_pts = gst_clock_adjust_with_calibration (NULL,
          packet_time_in_gst,
          self->current_time_mapping.xbase, self->current_time_mapping.b,
          self->current_time_mapping.num, self->current_time_mapping.den);

      GST_DEBUG_OBJECT (self, "Audio timestamp, original: %" GST_TIME_FORMAT
          ", calibrated: %" GST_TIME_FORMAT,
          GST_TIME_ARGS (packet_time_in_gst), GST_TIME_ARGS (audio_pts));

      audio_data.packet_time = packet_time;
      audio_data.stream_time = packet_time_in_gst;
      audio_data.calibrated_time = audio_pts;

      GST_BUFFER_DTS (audio_buf) = GST_CLOCK_TIME_NONE;
      GST_BUFFER_PTS (audio_buf) = audio_pts;
      GST_BUFFER_DURATION (audio_buf) = gst_util_uint64_scale (sample_count,
          GST_SECOND, self->audio_info.rate);
      GST_BUFFER_OFFSET (audio_buf) = audio_offset;
      GST_BUFFER_OFFSET_END (audio_buf) = audio_offset_end;

    }

    if (self->next_audio_offset != INVALID_AUDIO_OFFSET) {
      if (self->next_audio_offset != audio_offset) {
        GST_WARNING_OBJECT (self, "Expected offset %" G_GUINT64_FORMAT
            ", received %" G_GUINT64_FORMAT, self->next_audio_offset,
            audio_offset);
        GST_BUFFER_FLAG_SET (audio_buf, GST_BUFFER_FLAG_DISCONT);
      } else {
        GST_LOG_OBJECT (self, "Got expected audio samples");
      }
    }

    self->next_audio_offset = audio_offset_end;
    auto sample = gst_sample_new (audio_buf, self->selected_audio_caps, nullptr,
        nullptr);
    gst_buffer_unref (audio_buf);
    audio_data.sample = sample;
    gst_vec_deque_push_tail_struct (self->audio_queue, &audio_data);

    /* Avoid too many audio buffers in queue */
    while (gst_vec_deque_get_length (self->audio_queue) > self->buffer_size + 1) {
      auto audio_data = (GstDecklink2InputAudioData *)
          gst_vec_deque_pop_head_struct (self->audio_queue);
      auto audio_sample = audio_data->sample;
      auto audio_buf = gst_sample_get_buffer (audio_sample);
      auto len = gst_vec_deque_get_length (self->audio_queue);

      GST_WARNING_OBJECT (self, "Too large audio queue len: %" G_GSIZE_FORMAT
          ", dropping audio %" GST_PTR_FORMAT, len, audio_buf);

      gst_sample_unref (audio_sample);
    }
  }

out:
  if (buffer) {
    while (gst_vec_deque_get_length (self->queue) > self->buffer_size) {
      GstDecklink2InputData *data = (GstDecklink2InputData *)
          gst_vec_deque_pop_head_struct (self->queue);
      gst_decklink2_input_data_clear (data);
    }

    bool update_av_sync = true;
    while (gst_vec_deque_get_length (self->audio_queue) > 0) {
      auto audio_data = (GstDecklink2InputAudioData *)
          gst_vec_deque_pop_head_struct (self->audio_queue);
      auto audio_sample = audio_data->sample;
      auto audio_buf = gst_sample_get_buffer (audio_sample);

      GST_LOG_OBJECT (self, "Adding audio buffer %" GST_PTR_FORMAT, audio_buf);

      if (update_av_sync) {
        self->av_sync = GST_CLOCK_DIFF (GST_BUFFER_PTS (buffer),
            GST_BUFFER_PTS (audio_buf));
        stats.audio_packet_time = audio_data->packet_time;
        stats.audio_stream_time = audio_data->stream_time;
        stats.calibrated_audio_time = audio_data->calibrated_time;
        update_av_sync = false;
      }

      gst_buffer_add_decklink2_audio_meta (buffer, audio_sample);
      gst_sample_unref (audio_sample);
    }

    GST_LOG_OBJECT (self, "Enqueue buffer %" GST_PTR_FORMAT, buffer);

    GstDecklink2InputData new_data;
    stats.av_sync = self->av_sync;
    new_data.stats = stats;
    new_data.buffer = buffer;
    new_data.caps = gst_decklink2_input_get_frame_caps (self, frame);
    gst_vec_deque_push_tail_struct (self->queue, &new_data);
    priv->cond.notify_all ();
  }
}

static void
gst_decklink2_input_stop_unlocked (GstDeckLink2Input * self)
{
  GstDeckLink2InputPrivate *priv = self->priv;
  HRESULT hr;

  /* Temporarily unlock mutex. below API call may be blocked by callback thread
   * which takes our mutex too */
  priv->stopping = true;
  priv->lock.unlock ();
  hr = gst_decklink2_input_stop_streams (self);
  /* For logging */
  gst_decklink2_result (hr);

  hr = gst_decklink2_input_disable_video (self);
  gst_decklink2_result (hr);

  hr = gst_decklink2_input_disable_audio (self);
  gst_decklink2_result (hr);

  hr = gst_decklink2_input_set_callback (self, NULL);
  gst_decklink2_result (hr);

  priv->lock.lock ();
  priv->stopping = false;
  gst_vec_deque_clear (self->queue);
  gst_vec_deque_clear (self->audio_queue);
  gst_clear_caps (&self->selected_video_caps);
  gst_clear_caps (&self->priv->frame_caps);
  gst_clear_caps (&self->selected_audio_caps);
  priv->signal = false;
  priv->was_restarted = false;
  priv->api_failed = false;
  priv->aspect_ratio_mismatch_warned = false;
  self->skip_first_time = GST_CLOCK_TIME_NONE;
  self->start_time = GST_CLOCK_TIME_NONE;
  self->started = FALSE;
  self->av_sync = 0;
  self->discont = TRUE;
  gst_decklink2_input_calibration_init (&priv->calibration);
}

HRESULT
gst_decklink2_input_start (GstDeckLink2Input * input, GstElement * client,
    BMDProfileID profile_id, guint buffer_size, GstClockTime skip_first_time,
    const GstDeckLink2InputVideoConfig * video_config,
    const GstDeckLink2InputAudioConfig * audio_config)
{
  GstDeckLink2InputPrivate *priv = input->priv;
  std::lock_guard < std::mutex > lk (priv->lock);
  HRESULT hr;
  BMDVideoInputFlags input_flags = bmdVideoInputFlagDefault;

  gst_decklink2_input_stop_unlocked (input);
  gst_decklink2_input_reset_time_mapping (input);

  if (skip_first_time > 0 && GST_CLOCK_TIME_IS_VALID (skip_first_time))
    input->skip_first_time = skip_first_time;

  if (profile_id != bmdProfileDefault) {
    GstDeckLink2Device *device;

    device = (GstDeckLink2Device *) gst_object_get_parent (GST_OBJECT (input));
    g_assert (device);

    hr = gst_decklink2_device_set_profile_id (device, profile_id);
    gst_object_unref (device);

    if (!gst_decklink2_result (hr)) {
      GST_ERROR_OBJECT (input, "Couldn't set profile ID");
      goto error;
    }
  }

  if (video_config->connection != bmdVideoConnectionUnspecified) {
    hr = E_FAIL;
    if (input->config) {
      hr = input->config->SetInt (bmdDeckLinkConfigVideoInputConnection,
          video_config->connection);
    } else if (input->config_10_11) {
      hr = input->config_10_11->SetInt (bmdDeckLinkConfigVideoInputConnection,
          video_config->connection);
    }

    if (!gst_decklink2_result (hr)) {
      GST_ERROR_OBJECT (input, "Couldn't set video connection, hr: 0x%x",
          (guint) hr);
      goto error;
    }

    if (video_config->connection == bmdVideoConnectionComposite) {
      hr = E_FAIL;
      if (input->config) {
        hr = input->config->SetInt (bmdDeckLinkConfigAnalogVideoInputFlags,
            bmdAnalogVideoFlagCompositeSetup75);
      } else if (input->config_10_11) {
        hr = input->
            config_10_11->SetInt (bmdDeckLinkConfigAnalogVideoInputFlags,
            bmdAnalogVideoFlagCompositeSetup75);
      }

      if (!gst_decklink2_result (hr)) {
        GST_ERROR_OBJECT (input,
            "Couldn't set analog video input flags, hr: 0x%x", (guint) hr);
        goto error;
      }
    }
  }

  if (video_config->auto_detect ||
      video_config->pixel_format == bmdFormatUnspecified) {
    dlbool_t auto_detect = false;

    if (input->attr) {
      hr = input->attr->GetFlag (BMDDeckLinkSupportsInputFormatDetection,
          &auto_detect);
      if (!gst_decklink2_result (hr) || !auto_detect) {
        GST_ERROR_OBJECT (input, "Auto detect is not supported");
        goto error;
      }
    } else if (input->attr_10_11) {
      hr = input->attr_10_11->GetFlag (BMDDeckLinkSupportsInputFormatDetection,
          &auto_detect);
      if (!gst_decklink2_result (hr) || !auto_detect) {
        GST_ERROR_OBJECT (input, "Auto detect is not supported");
        goto error;
      }
    } else {
      GST_ERROR_OBJECT (input,
          "IDeckLinkProfileAttributes interface is not available");
      goto error;
    }

    GST_DEBUG_OBJECT (input, "Enable format detection");

    input_flags |= bmdVideoInputEnableFormatDetection;
  }

  input->client = client;
  if (!gst_decklink2_input_get_display_mode_from_native (input,
          video_config->display_mode == bmdModeUnknown ? bmdModeNTSC :
          video_config->display_mode, &input->selected_mode)) {
    GST_ERROR_OBJECT (input, "Unable to get requested display mode");
    goto error;
  }
  input->requested_pixel_format = video_config->pixel_format;
  input->pixel_format = video_config->pixel_format == bmdFormatUnspecified ?
      bmdFormat8BitYUV : video_config->pixel_format;
  input->timecode_format = video_config->timecode_format;
  input->output_cc = video_config->output_cc;
  input->output_afd_bar = video_config->output_afd_bar;
  input->output_vanc = video_config->output_vanc;
  input->buffer_size = buffer_size;
  input->selected_video_caps = gst_decklink2_input_get_caps (input,
      input->selected_mode.mode, input->pixel_format);
  if (!input->selected_video_caps ||
      gst_caps_is_empty (input->selected_video_caps)) {
    GST_ERROR_OBJECT (input, "Unable to get caps from requested mode");
    goto error;
  }

  /* Colorimetry creates multiple structures for the same mode and format.
   * Keep one and use the actual frame metadata to determine colorimetry. */
  input->selected_video_caps = gst_caps_truncate (input->selected_video_caps);
  gst_structure_remove_field (gst_caps_get_structure
      (input->selected_video_caps, 0), "colorimetry");

  if (!gst_video_info_from_caps (&input->video_info,
          input->selected_video_caps)) {
    GST_ERROR_OBJECT (input, "Invalid capture caps");
    goto error;
  }

  input->auto_detect = video_config->auto_detect;
  input->aspect_ratio_flag = -1;
  input->next_audio_offset = INVALID_AUDIO_OFFSET;

  if (input->callback)
    hr = gst_decklink2_input_set_callback (input, input->callback);
  else
    hr = gst_decklink2_input_set_callback (input, input->callback_11_5_1);

  if (!gst_decklink2_result (hr)) {
    GST_ERROR_OBJECT (input, "Couldn't set callback");
    goto error;
  }

  hr = gst_decklink2_input_enable_video (input,
      gst_decklink2_get_real_display_mode (input->selected_mode.mode),
      input->pixel_format, input_flags);
  if (!gst_decklink2_result (hr)) {
    GST_ERROR_OBJECT (input, "Couldn't enable video");
    goto error;
  }

  if (audio_config->channels != GST_DECKLINK2_AUDIO_CHANNELS_DISABLED) {
    guint channels = 2;

    if (audio_config->connection != bmdAudioConnectionUnspecified) {
      hr = E_FAIL;
      if (input->config) {
        hr = input->config->SetInt (bmdDeckLinkConfigAudioInputConnection,
            audio_config->connection);
      } else if (input->config_10_11) {
        hr = input->config_10_11->SetInt (bmdDeckLinkConfigAudioInputConnection,
            audio_config->connection);
      }

      if (!gst_decklink2_result (hr)) {
        GST_ERROR_OBJECT (input, "Couldn't set audio connection, hr: 0x%x",
            (guint) hr);
        goto error;
      }
    }

    switch (audio_config->channels) {
      case GST_DECKLINK2_AUDIO_CHANNELS_2:
        channels = 2;
        break;
      case GST_DECKLINK2_AUDIO_CHANNELS_8:
        channels = 8;
        break;
      case GST_DECKLINK2_AUDIO_CHANNELS_16:
        channels = 16;
        break;
      case GST_DECKLINK2_AUDIO_CHANNELS_MAX:
        channels = input->max_audio_channels;
        break;
      default:
        break;
    }

    hr = gst_decklink2_input_enable_audio (input, bmdAudioSampleRate48kHz,
        audio_config->sample_type, channels);
    if (!gst_decklink2_result (hr)) {
      GST_ERROR_OBJECT (input, "Couldn't enable audio");
      goto error;
    }

    gst_audio_info_set_format (&input->audio_info,
        audio_config->sample_type == bmdAudioSampleType32bitInteger ?
        GST_AUDIO_FORMAT_S32LE : GST_AUDIO_FORMAT_S16LE, 48000, channels, NULL);
    input->selected_audio_caps = gst_audio_info_to_caps (&input->audio_info);
  }

  hr = gst_decklink2_input_start_streams (input);
  if (!gst_decklink2_result (hr)) {
    GST_ERROR_OBJECT (input, "Couldn't start streams");
    goto error;
  }

  input->started = TRUE;

  return S_OK;

error:
  gst_decklink2_input_stop_unlocked (input);
  input->client = NULL;
  return E_FAIL;
}

void
gst_decklink2_input_stop (GstDeckLink2Input * input)
{
  GstDeckLink2InputPrivate *priv = input->priv;
  std::lock_guard < std::mutex > lk (priv->lock);

  gst_decklink2_input_stop_unlocked (input);
  input->client = NULL;

  priv->cond.notify_all ();
}

void
gst_decklink2_input_set_flushing (GstDeckLink2Input * input, gboolean flush)
{
  GstDeckLink2InputPrivate *priv = input->priv;
  std::lock_guard < std::mutex > lk (priv->lock);

  input->flushing = flush;
  priv->cond.notify_all ();
}

GstFlowReturn
gst_decklink2_input_get_data (GstDeckLink2Input * input, GstBuffer ** buf,
    GstCaps ** caps, GstDeckLink2InputStats * stats)
{
  GstDeckLink2InputPrivate *priv = input->priv;
  std::unique_lock < std::mutex > lk (priv->lock);
  GstDecklink2InputData *data;

  *buf = nullptr;
  *caps = nullptr;
  gst_decklink2_input_stats_init (stats);

  while (gst_vec_deque_is_empty (input->queue)
      && !input->flushing && input->started && !priv->api_failed) {
    priv->cond.wait (lk);
  }

  if (priv->api_failed)
    return GST_FLOW_ERROR;

  if (input->flushing || !input->started)
    return GST_FLOW_FLUSHING;

  data = (GstDecklink2InputData *)
      gst_vec_deque_pop_head_struct (input->queue);
  *buf = data->buffer;
  *caps = data->caps;
  *stats = data->stats;

  return GST_FLOW_OK;
}

gboolean
gst_decklink2_input_has_signal (GstDeckLink2Input * input)
{
  GstDeckLink2InputPrivate *priv = input->priv;

  if (priv->signal)
    return TRUE;

  return FALSE;
}
