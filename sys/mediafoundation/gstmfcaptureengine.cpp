/* GStreamer
 * Copyright (C) 2019 Seungha Yang <seungha.yang@navercorp.com>
 * Copyright (C) 2020 Seungha Yang <seungha@centricular.com>
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

#include <gst/video/video.h>
#include "gstmfcaptureengine.h"
#include "gstmfutils.h"
#include <mfcaptureengine.h>
#include <string.h>
#include <wrl.h>

using namespace Microsoft::WRL;

extern "C" {
GST_DEBUG_CATEGORY_EXTERN (gst_mf_source_object_debug);
#define GST_CAT_DEFAULT gst_mf_source_object_debug
}

static HRESULT gst_mf_capture_engine_on_event (GstMFCaptureEngine * engine,
    IMFMediaEvent * event);
static HRESULT gst_mf_capture_engine_on_sample (GstMFCaptureEngine * engine,
    IMFSample * sample);

class GstMFCaptureEngineCallbackObject
    : public IMFCaptureEngineOnSampleCallback
    , public IMFCaptureEngineOnEventCallback
{
public:
  GstMFCaptureEngineCallbackObject (GstMFCaptureEngine * listener)
      : _listener (listener)
      , _ref_count (1)
  {
    if (_listener)
      g_object_weak_ref (G_OBJECT (_listener),
          (GWeakNotify) GstMFCaptureEngineCallbackObject::OnWeakNotify, this);
  }

  STDMETHOD (QueryInterface) (REFIID riid, void ** object)
  {
    HRESULT hr = E_NOINTERFACE;

    if (IsEqualIID (riid, IID_IUnknown)) {
      *object = this;
      hr = S_OK;
    } else if (IsEqualIID (riid, IID_IMFCaptureEngineOnSampleCallback)) {
      *object = static_cast<IMFCaptureEngineOnSampleCallback*>(this);
      hr = S_OK;
    } else if (IsEqualIID (riid, IID_IMFCaptureEngineOnEventCallback)) {
      *object = static_cast<IMFCaptureEngineOnEventCallback*>(this);
      hr = S_OK;
    }

    if (SUCCEEDED (hr))
      AddRef();

    return hr;
  }

  STDMETHOD_ (ULONG, AddRef) (void)
  {
    return InterlockedIncrement (&this->_ref_count);
  }

  STDMETHOD_ (ULONG, Release) (void)
  {
    ULONG ref_count;

    ref_count = InterlockedDecrement (&this->_ref_count);

    if (ref_count == 0)
      delete this;

    return ref_count;
  }

  STDMETHOD (OnSample) (IMFSample * sample)
  {
    if (!sample) {
      return S_OK;
    }

    if (this->_listener)
      return gst_mf_capture_engine_on_sample (this->_listener, sample);

    return S_OK;
  }

  STDMETHOD (OnEvent) (IMFMediaEvent * event)
  {
    if (this->_listener)
      return gst_mf_capture_engine_on_event (this->_listener, event);

    return S_OK;
  }

private:
  ~GstMFCaptureEngineCallbackObject ()
  {
    if (_listener)
      g_object_weak_unref (G_OBJECT (_listener),
          (GWeakNotify) GstMFCaptureEngineCallbackObject::OnWeakNotify, this);
  }

  static void
  OnWeakNotify (GstMFCaptureEngineCallbackObject * self, GObject * object)
  {
    self->_listener = NULL;
  }

  GstMFCaptureEngine * _listener;
  volatile ULONG _ref_count;
};

typedef enum
{
  GST_MF_CAPTURE_ENGINE_EVENT_NONE,
  GST_MF_CAPTURE_ENGINE_EVENT_ALL_EFFECTS_REMOVED,
  GST_MF_CAPTURE_ENGINE_EVENT_CAMERA_STREAM_BLOCKED,
  GST_MF_CAPTURE_ENGINE_EVENT_CAMERA_STREAM_UNBLOCKED,
  GST_MF_CAPTURE_ENGINE_EVENT_EFFECT_ADDED,
  GST_MF_CAPTURE_ENGINE_EVENT_EFFECT_REMOVED,
  GST_MF_CAPTURE_ENGINE_EVENT_ERROR,
  GST_MF_CAPTURE_ENGINE_EVENT_INITIALIZED,
  GST_MF_CAPTURE_ENGINE_EVENT_PHOTO_TAKEN,
  GST_MF_CAPTURE_ENGINE_EVENT_PREVIEW_STARTED,
  GST_MF_CAPTURE_ENGINE_EVENT_PREVIEW_STOPPED,
  GST_MF_CAPTURE_ENGINE_EVENT_RECORD_STARTED,
  GST_MF_CAPTURE_ENGINE_EVENT_RECORD_STOPPED,
  GST_MF_CAPTURE_ENGINE_EVENT_SINK_PREPARED,
  GST_MF_CAPTURE_ENGINE_EVENT_SOURCE_CURRENT_DEVICE_MEDIA_TYPE_SET,
} GstMFCaptureEngineEvent;

typedef struct
{
  const GUID & mf_event;
  GstMFCaptureEngineEvent event;
  const gchar *name;
} GstMFCaptureEngineEventMap;

static const GstMFCaptureEngineEventMap mf_event_map[] = {
  {MF_CAPTURE_ENGINE_ALL_EFFECTS_REMOVED,
      GST_MF_CAPTURE_ENGINE_EVENT_ALL_EFFECTS_REMOVED, "all-effects-removed"},
  {MF_CAPTURE_ENGINE_CAMERA_STREAM_BLOCKED,
        GST_MF_CAPTURE_ENGINE_EVENT_CAMERA_STREAM_BLOCKED,
      "camera-stream-blocked"},
  {MF_CAPTURE_ENGINE_CAMERA_STREAM_UNBLOCKED,
        GST_MF_CAPTURE_ENGINE_EVENT_CAMERA_STREAM_UNBLOCKED,
      "camera-stream-unblocked"},
  {MF_CAPTURE_ENGINE_EFFECT_ADDED,
      GST_MF_CAPTURE_ENGINE_EVENT_EFFECT_ADDED, "effect-added"},
  {MF_CAPTURE_ENGINE_EFFECT_REMOVED,
      GST_MF_CAPTURE_ENGINE_EVENT_EFFECT_REMOVED, "effect-removed"},
  {MF_CAPTURE_ENGINE_ERROR,
      GST_MF_CAPTURE_ENGINE_EVENT_ERROR, "error"},
  {MF_CAPTURE_ENGINE_INITIALIZED,
      GST_MF_CAPTURE_ENGINE_EVENT_INITIALIZED, "initialized"},
  {MF_CAPTURE_ENGINE_PHOTO_TAKEN,
      GST_MF_CAPTURE_ENGINE_EVENT_PHOTO_TAKEN, "photo-taken"},
  {MF_CAPTURE_ENGINE_PREVIEW_STARTED,
      GST_MF_CAPTURE_ENGINE_EVENT_PREVIEW_STARTED, "preview-started"},
  {MF_CAPTURE_ENGINE_PREVIEW_STOPPED,
      GST_MF_CAPTURE_ENGINE_EVENT_PREVIEW_STOPPED, "preview-stopped"},
  {MF_CAPTURE_ENGINE_RECORD_STARTED,
      GST_MF_CAPTURE_ENGINE_EVENT_RECORD_STARTED, "record-started"},
  {MF_CAPTURE_ENGINE_RECORD_STOPPED,
      GST_MF_CAPTURE_ENGINE_EVENT_RECORD_STOPPED, "record-stopped"},
  {MF_CAPTURE_SINK_PREPARED,
      GST_MF_CAPTURE_ENGINE_EVENT_SINK_PREPARED, "sink-prepared"},
  {MF_CAPTURE_SOURCE_CURRENT_DEVICE_MEDIA_TYPE_SET,
        GST_MF_CAPTURE_ENGINE_EVENT_SOURCE_CURRENT_DEVICE_MEDIA_TYPE_SET,
      "source-current-device-media-type-set"}
};

static const GstMFCaptureEngineEventMap *
gst_mf_capture_engine_get_event_map (const GUID * event_type)
{
  gint i;

  for (i = 0; i < G_N_ELEMENTS (mf_event_map); i++) {
    if (IsEqualGUID (*event_type, mf_event_map[i].mf_event))
      return &mf_event_map[i];
  }

  return NULL;
}

typedef struct _GstMFStreamMediaType
{
  IMFMediaType *media_type;

  /* the stream index of media type */
  guint stream_index;

  /* the media index in the stream index */
  guint media_type_index;

  GstCaps *caps;
} GstMFStreamMediaType;

struct _GstMFCaptureEngine
{
  GstMFSourceObject parent;

  GMutex lock;
  GCond cond;

  /* protected by lock */
  GQueue *queue;
  GstMFCaptureEngineEvent last_event;

  IMFMediaSource *source;
  IMFCaptureEngine *engine;
  GstMFCaptureEngineCallbackObject *callback_obj;

  GstCaps *supported_caps;
  GList *media_types;
  GstMFStreamMediaType *cur_type;
  GstVideoInfo info;

  gboolean started;
  gboolean flushing;
};

static void gst_mf_capture_engine_finalize (GObject * object);

static gboolean gst_mf_capture_engine_open (GstMFSourceObject * object,
    IMFActivate * activate);
static gboolean gst_mf_capture_engine_start (GstMFSourceObject * object);
static gboolean gst_mf_capture_engine_stop (GstMFSourceObject * object);
static gboolean gst_mf_capture_engine_close (GstMFSourceObject * object);
static GstFlowReturn gst_mf_capture_engine_fill (GstMFSourceObject * object,
    GstBuffer * buffer);
static gboolean gst_mf_capture_engine_unlock (GstMFSourceObject * object);
static gboolean gst_mf_capture_engine_unlock_stop (GstMFSourceObject * object);
static GstCaps *gst_mf_capture_engine_get_caps (GstMFSourceObject * object);
static gboolean gst_mf_capture_engine_set_caps (GstMFSourceObject * object,
    GstCaps * caps);

#define gst_mf_capture_engine_parent_class parent_class
G_DEFINE_TYPE (GstMFCaptureEngine, gst_mf_capture_engine,
    GST_TYPE_MF_SOURCE_OBJECT);

static void
gst_mf_capture_engine_class_init (GstMFCaptureEngineClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstMFSourceObjectClass *source_class = GST_MF_SOURCE_OBJECT_CLASS (klass);

  gobject_class->finalize = gst_mf_capture_engine_finalize;

  source_class->open = GST_DEBUG_FUNCPTR (gst_mf_capture_engine_open);
  source_class->start = GST_DEBUG_FUNCPTR (gst_mf_capture_engine_start);
  source_class->stop = GST_DEBUG_FUNCPTR (gst_mf_capture_engine_stop);
  source_class->close = GST_DEBUG_FUNCPTR (gst_mf_capture_engine_close);
  source_class->fill = GST_DEBUG_FUNCPTR (gst_mf_capture_engine_fill);
  source_class->unlock = GST_DEBUG_FUNCPTR (gst_mf_capture_engine_unlock);
  source_class->unlock_stop =
      GST_DEBUG_FUNCPTR (gst_mf_capture_engine_unlock_stop);
  source_class->get_caps = GST_DEBUG_FUNCPTR (gst_mf_capture_engine_get_caps);
  source_class->set_caps = GST_DEBUG_FUNCPTR (gst_mf_capture_engine_set_caps);
}

static void
gst_mf_capture_engine_init (GstMFCaptureEngine * self)
{
  self->queue = g_queue_new ();
  g_mutex_init (&self->lock);
  g_cond_init (&self->cond);
}

static gboolean
gst_mf_enum_media_type_from_video_capture_source (IMFCaptureSource
    * capture_source, GList ** media_types)
{
  gint i, j;
  HRESULT hr;
  GList *list = NULL;

  g_return_val_if_fail (capture_source != NULL, FALSE);
  g_return_val_if_fail (media_types != NULL, FALSE);

  for (i = 0;; i++) {
    MF_CAPTURE_ENGINE_STREAM_CATEGORY category;

    hr = capture_source->GetDeviceStreamCategory (i, &category);
    if (FAILED (hr)) {
      GST_DEBUG ("failed to get %dth stream category, hr:0x%x", i, (guint) hr);
      break;
    }

    GST_DEBUG ("%dth capture source category %d", i, category);

    if (category != MF_CAPTURE_ENGINE_STREAM_CATEGORY_VIDEO_PREVIEW &&
        category != MF_CAPTURE_ENGINE_STREAM_CATEGORY_VIDEO_CAPTURE)
      continue;

    for (j = 0;; j++) {
      ComPtr<IMFMediaType> media_type;

      hr = capture_source->GetAvailableDeviceMediaType (i, j, &media_type);

      if (SUCCEEDED (hr)) {
        GstMFStreamMediaType *mtype;
        GstCaps *caps = NULL;

        caps = gst_mf_media_type_to_caps (media_type.Get ());

        /* unknown format */
        if (!caps)
          continue;

        mtype = g_new0 (GstMFStreamMediaType, 1);
        mtype->media_type = media_type.Detach ();
        mtype->stream_index = i;
        mtype->media_type_index = j;
        mtype->caps = caps;

        GST_DEBUG ("StreamIndex %d, MediaTypeIndex %d, %" GST_PTR_FORMAT,
            i, j, caps);

        list = g_list_prepend (list, mtype);
      } else if (hr == MF_E_NO_MORE_TYPES) {
        /* no more media type in this stream index, try next stream index */
        break;
      } else if (hr == MF_E_INVALIDSTREAMNUMBER) {
        /* no more streams and media types */
        goto done;
      } else {
        /* undefined return */
        goto done;
      }
    }
  }

done:
  list = g_list_reverse (list);
  *media_types = list;

  return ! !list;
}

static void
gst_mf_stream_media_type_free (GstMFStreamMediaType * media_type)
{
  g_return_if_fail (media_type != NULL);

  if (media_type->media_type)
    media_type->media_type->Release ();

  if (media_type->caps)
    gst_caps_unref (media_type->caps);

  g_free (media_type);
}

static gboolean
gst_mf_capture_engine_open (GstMFSourceObject * object,
    IMFActivate * activate)
{
  GstMFCaptureEngine *self = GST_MF_CAPTURE_ENGINE (object);
  GList *iter;
  ComPtr<IMFCaptureEngineClassFactory> factory;
  ComPtr<IMFCaptureEngine> engine;
  ComPtr<IMFMediaSource> source;
  ComPtr<IMFCaptureSource> capture_source;
  ComPtr<IMFAttributes> attr;
  HRESULT hr;
  GstMFCaptureEngineCallbackObject *callback_obj = NULL;
  GstMFCaptureEngineEvent last_event;

  hr = activate->ActivateObject (IID_IMFMediaSource, (void **) &source);
  if (!gst_mf_result (hr))
    return FALSE;

  hr = CoCreateInstance (CLSID_MFCaptureEngineClassFactory,
      NULL, CLSCTX_INPROC_SERVER,
      IID_IMFCaptureEngineClassFactory, (void **) &factory);
  if (!gst_mf_result (hr))
    return FALSE;

  hr = factory->CreateInstance (CLSID_MFCaptureEngine,
      IID_IMFCaptureEngine, (void **) &engine);
  if (!gst_mf_result (hr))
    return FALSE;

  hr = MFCreateAttributes (&attr, 1);
  if (!gst_mf_result (hr))
    return FALSE;

  hr = attr->SetUINT32 (MF_CAPTURE_ENGINE_USE_VIDEO_DEVICE_ONLY, TRUE);
  if (!gst_mf_result (hr))
    return FALSE;

  callback_obj = new GstMFCaptureEngineCallbackObject (self);
  self->last_event = GST_MF_CAPTURE_ENGINE_EVENT_NONE;

  GST_DEBUG_OBJECT (self, "Start init capture engine");
  hr = engine->Initialize ((IMFCaptureEngineOnEventCallback *) callback_obj,
      attr.Get (), NULL, source.Get ());
  if (!gst_mf_result (hr)) {
    callback_obj->Release ();
    return FALSE;
  }

  /* wait initialized event */
  g_mutex_lock (&self->lock);
  while (self->last_event != GST_MF_CAPTURE_ENGINE_EVENT_ERROR &&
      self->last_event != GST_MF_CAPTURE_ENGINE_EVENT_INITIALIZED)
    g_cond_wait (&self->cond, &self->lock);

  last_event = self->last_event;
  g_mutex_unlock (&self->lock);

  if (last_event == GST_MF_CAPTURE_ENGINE_EVENT_ERROR) {
    GST_ERROR_OBJECT (self, "Failed to initialize");
    callback_obj->Release ();
    return FALSE;
  }

  GST_DEBUG_OBJECT (self, "Finish init capture engine");

  hr = engine->GetSource (&capture_source);
  if (!gst_mf_result (hr)) {
    callback_obj->Release ();
    return FALSE;
  }

  if (!gst_mf_enum_media_type_from_video_capture_source (capture_source.Get (),
          &self->media_types)) {
    GST_ERROR_OBJECT (self, "No available media types");
    callback_obj->Release ();
    return FALSE;
  }

  self->source = source.Detach ();
  self->engine = engine.Detach ();
  self->callback_obj = callback_obj;

  for (iter = self->media_types; iter; iter = g_list_next (iter)) {
    GstMFStreamMediaType *mtype = (GstMFStreamMediaType *) iter->data;
    if (!self->supported_caps)
      self->supported_caps = gst_caps_ref (mtype->caps);
    else
      self->supported_caps =
          gst_caps_merge (self->supported_caps, gst_caps_ref (mtype->caps));
  }

  GST_DEBUG_OBJECT (self, "Available output caps %" GST_PTR_FORMAT,
      self->supported_caps);

  return TRUE;
}

static gboolean
gst_mf_capture_engine_close (GstMFSourceObject * object)
{
  GstMFCaptureEngine *self = GST_MF_CAPTURE_ENGINE (object);

  gst_clear_caps (&self->supported_caps);

  if (self->media_types) {
    g_list_free_full (self->media_types,
        (GDestroyNotify) gst_mf_stream_media_type_free);
    self->media_types = NULL;
  }

  if (self->callback_obj) {
    self->callback_obj->Release ();
    self->callback_obj = NULL;
  }

  if (self->engine) {
    self->engine->Release ();
    self->engine = NULL;
  }

  if (self->source) {
    self->source->Shutdown ();
    self->source->Release ();
    self->source = NULL;
  }

  return TRUE;
}

static void
gst_mf_capture_engine_finalize (GObject * object)
{
  GstMFCaptureEngine *self = GST_MF_CAPTURE_ENGINE (object);

  g_queue_free (self->queue);
  g_mutex_clear (&self->lock);
  g_cond_clear (&self->cond);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static HRESULT
gst_mf_capture_engine_on_sample (GstMFCaptureEngine * self, IMFSample * sample)
{
  HRESULT hr;
  DWORD count = 0, i;

  if (!sample)
    return S_OK;

  hr = sample->GetBufferCount (&count);
  if (!gst_mf_result (hr) || !count)
    return S_OK;

  g_mutex_lock (&self->lock);
  if (self->flushing) {
    g_mutex_unlock (&self->lock);
    return S_OK;
  }

  for (i = 0; i < count; i++) {
    IMFMediaBuffer *buffer = NULL;

    hr = sample->GetBufferByIndex (i, &buffer);
    if (!gst_mf_result (hr) || !buffer)
      continue;

    g_queue_push_tail (self->queue, buffer);
  }

  g_cond_broadcast (&self->cond);
  g_mutex_unlock (&self->lock);

  return S_OK;
}

static HRESULT
gst_mf_capture_engine_on_event (GstMFCaptureEngine * self,
    IMFMediaEvent * event)
{
  const GstMFCaptureEngineEventMap *event_map;
  HRESULT hr;
  GUID event_type;

  hr = event->GetExtendedType (&event_type);
  if (!gst_mf_result (hr))
    return hr;

  event_map = gst_mf_capture_engine_get_event_map (&event_type);
  if (!event_map) {
    GST_WARNING_OBJECT (self, "Unknown event");
    return S_OK;
  }

  GST_DEBUG_OBJECT (self, "Got event %s", event_map->name);
  g_mutex_lock (&self->lock);
  self->last_event = event_map->event;
  switch (event_map->event) {
    case GST_MF_CAPTURE_ENGINE_EVENT_PREVIEW_STARTED:
      self->started = TRUE;
      break;
    case GST_MF_CAPTURE_ENGINE_EVENT_PREVIEW_STOPPED:
      self->started = FALSE;
      break;
    default:
      break;
  }
  g_cond_signal (&self->cond);
  g_mutex_unlock (&self->lock);

  return S_OK;
}

static gboolean
gst_mf_capture_engine_start (GstMFSourceObject * object)
{
  GstMFCaptureEngine *self = GST_MF_CAPTURE_ENGINE (object);
  HRESULT hr;
  ComPtr<IMFCaptureSink> sink;
  ComPtr<IMFCapturePreviewSink> preview_sink;
  DWORD sink_stream_index = 0;
  IMFMediaType *media_type;

  if (!self->cur_type) {
    GST_ERROR_OBJECT (self, "MediaType wasn't specified");
    return FALSE;
  }

  media_type = self->cur_type->media_type;

  hr = media_type->SetUINT32 (MF_MT_DEFAULT_STRIDE,
      GST_VIDEO_INFO_PLANE_STRIDE (&self->info, 0));
  if (!gst_mf_result (hr))
    return FALSE;

  hr = self->engine->GetSink (MF_CAPTURE_ENGINE_SINK_TYPE_PREVIEW, &sink);
  if (!gst_mf_result (hr))
    return FALSE;

  hr = sink.As (&preview_sink);
  if (!gst_mf_result (hr))
    return FALSE;

  hr = preview_sink->RemoveAllStreams ();
  if (!gst_mf_result (hr))
    return FALSE;

  hr = preview_sink->AddStream (self->cur_type->stream_index,
      media_type, NULL, &sink_stream_index);
  if (!gst_mf_result (hr))
    return FALSE;

  hr = preview_sink->SetSampleCallback (sink_stream_index,
      (IMFCaptureEngineOnSampleCallback *) self->callback_obj);
  if (!gst_mf_result (hr))
    return FALSE;

  hr = self->engine->StartPreview ();
  if (!gst_mf_result (hr))
    return FALSE;

  g_mutex_lock (&self->lock);
  while (!self->started)
    g_cond_wait (&self->cond, &self->lock);
  g_mutex_unlock (&self->lock);

  return TRUE;
}

static gboolean
gst_mf_capture_engine_stop (GstMFSourceObject * object)
{
  GstMFCaptureEngine *self = GST_MF_CAPTURE_ENGINE (object);
  HRESULT hr;

  if (self->engine && self->started) {
    GST_DEBUG_OBJECT (self, "Stopping preview");
    hr = self->engine->StopPreview ();
    if (gst_mf_result (hr)) {
      g_mutex_lock (&self->lock);
      while (self->started)
        g_cond_wait (&self->cond, &self->lock);
      g_mutex_unlock (&self->lock);

      GST_DEBUG_OBJECT (self, "Preview stopped");
    } else {
      GST_WARNING_OBJECT (self,
          "Failed to stopping preivew, hr: 0x%x", (guint) hr);
    }
  }

  while (!g_queue_is_empty (self->queue)) {
    IMFMediaBuffer *buffer = (IMFMediaBuffer *) g_queue_pop_head (self->queue);
    buffer->Release ();
  }

  return TRUE;
}

static GstFlowReturn
gst_mf_capture_engine_fill (GstMFSourceObject * object, GstBuffer * buffer)
{
  GstMFCaptureEngine *self = GST_MF_CAPTURE_ENGINE (object);
  GstFlowReturn ret = GST_FLOW_OK;
  HRESULT hr;
  GstVideoFrame frame;
  BYTE *data;
  gint i, j;
  ComPtr<IMFMediaBuffer> media_buffer;

  g_mutex_lock (&self->lock);
  if (self->last_event == GST_MF_CAPTURE_ENGINE_EVENT_ERROR) {
    g_mutex_unlock (&self->lock);
    return GST_FLOW_ERROR;
  }

  if (self->flushing) {
    g_mutex_unlock (&self->lock);
    return GST_FLOW_FLUSHING;
  }

  while (!self->flushing && g_queue_is_empty (self->queue))
    g_cond_wait (&self->cond, &self->lock);

  if (self->flushing) {
    g_mutex_unlock (&self->lock);
    return GST_FLOW_FLUSHING;
  }

  media_buffer.Attach ((IMFMediaBuffer *) g_queue_pop_head (self->queue));
  g_mutex_unlock (&self->lock);

  hr = media_buffer->Lock (&data, NULL, NULL);
  if (!gst_mf_result (hr)) {
    GST_ERROR_OBJECT (self, "Failed to lock media buffer");
    return GST_FLOW_ERROR;
  }

  if (!gst_video_frame_map (&frame, &self->info, buffer, GST_MAP_WRITE)) {
    GST_ERROR_OBJECT (self, "Failed to map buffer");
    media_buffer->Unlock ();
    return GST_FLOW_ERROR;
  }

  for (i = 0; i < GST_VIDEO_INFO_N_PLANES (&self->info); i++) {
    guint8 *src, *dst;
    gint src_stride, dst_stride;
    gint width;

    src = data + GST_VIDEO_INFO_PLANE_OFFSET (&self->info, i);
    dst = (guint8 *) GST_VIDEO_FRAME_PLANE_DATA (&frame, i);

    src_stride = GST_VIDEO_INFO_PLANE_STRIDE (&self->info, i);
    dst_stride = GST_VIDEO_FRAME_PLANE_STRIDE (&frame, i);
    width = GST_VIDEO_INFO_COMP_WIDTH (&self->info, i)
        * GST_VIDEO_INFO_COMP_PSTRIDE (&self->info, i);

    for (j = 0; j < GST_VIDEO_INFO_COMP_HEIGHT (&self->info, i); j++) {
      memcpy (dst, src, width);
      src += src_stride;
      dst += dst_stride;
    }
  }

  gst_video_frame_unmap (&frame);
  media_buffer->Unlock ();

  return ret;
}

static gboolean
gst_mf_capture_engine_unlock (GstMFSourceObject * object)
{
  GstMFCaptureEngine *self = GST_MF_CAPTURE_ENGINE (object);

  g_mutex_lock (&self->lock);
  if (self->flushing) {
    g_mutex_unlock (&self->lock);
    return TRUE;
  }

  self->flushing = TRUE;
  g_cond_broadcast (&self->cond);
  g_mutex_unlock (&self->lock);

  return TRUE;
}

static gboolean
gst_mf_capture_engine_unlock_stop (GstMFSourceObject * object)
{
  GstMFCaptureEngine *self = GST_MF_CAPTURE_ENGINE (object);

  g_mutex_lock (&self->lock);
  if (!self->flushing) {
    g_mutex_unlock (&self->lock);
    return TRUE;
  }

  self->flushing = FALSE;
  g_cond_broadcast (&self->cond);
  g_mutex_unlock (&self->lock);

  return TRUE;
}

static GstCaps *
gst_mf_capture_engine_get_caps (GstMFSourceObject * object)
{
  GstMFCaptureEngine *self = GST_MF_CAPTURE_ENGINE (object);

  if (self->supported_caps)
    return gst_caps_ref (self->supported_caps);

  return NULL;
}

static gboolean
gst_mf_capture_engine_set_caps (GstMFSourceObject * object, GstCaps * caps)
{
  GstMFCaptureEngine *self = GST_MF_CAPTURE_ENGINE (object);
  GList *iter;
  GstMFStreamMediaType *best_type = NULL;

  for (iter = self->media_types; iter; iter = g_list_next (iter)) {
    GstMFStreamMediaType *minfo = (GstMFStreamMediaType *) iter->data;
    if (gst_caps_is_subset (minfo->caps, caps)) {
      best_type = minfo;
      break;
    }
  }

  if (!best_type) {
    GST_ERROR_OBJECT (self,
        "Could not determine target media type with given caps %"
        GST_PTR_FORMAT, caps);

    return FALSE;
  }

  self->cur_type = best_type;
  gst_video_info_from_caps (&self->info, best_type->caps);

  return TRUE;
}

GstMFSourceObject *
gst_mf_capture_engine_new (GstMFSourceType type, gint device_index,
    const gchar * device_name, const gchar * device_path)
{
  GstMFSourceObject *self;
  gchar *name;
  gchar *path;

  /* TODO: add more type */
  g_return_val_if_fail (type == GST_MF_SOURCE_TYPE_VIDEO, NULL);

  name = device_name ? g_strdup (device_name) : g_strdup ("");
  path = device_path ? g_strdup (device_path) : g_strdup ("");

  self = (GstMFSourceObject *) g_object_new (GST_TYPE_MF_CAPTURE_ENGINE,
      "source-type", type, "device-index", device_index, "device-name", name,
      "device-path", path, NULL);

  gst_object_ref_sink (self);
  g_free (name);
  g_free (path);

  if (!self->opened) {
    GST_WARNING_OBJECT (self, "Couldn't open device");
    gst_object_unref (self);
    return NULL;
  }

  return self;
}
