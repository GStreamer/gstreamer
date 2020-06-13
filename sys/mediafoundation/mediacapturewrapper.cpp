/* GStreamer
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

#include <gst/gst.h>
#include "gstmfutils.h"
#include "mediacapturewrapper.h"

#include "AsyncOperations.h"
#include <windows.ui.core.h>
#include <locale>
#include <codecvt>
#include <string.h>

using namespace ABI::Windows::ApplicationModel::Core;
using namespace ABI::Windows::Foundation::Collections;
using namespace ABI::Windows::Media::Devices;
using namespace ABI::Windows::Media::MediaProperties;

G_BEGIN_DECLS

GST_DEBUG_CATEGORY_EXTERN (gst_mf_source_object_debug);
#define GST_CAT_DEFAULT gst_mf_source_object_debug

G_END_DECLS

static std::string
convert_hstring_to_string (HString * hstr)
{
  const wchar_t *raw_hstr;

  if (!hstr)
    return std::string();

  raw_hstr = hstr->GetRawBuffer (nullptr);
  if (!raw_hstr)
    return std::string();

  std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> converter;

  return converter.to_bytes (raw_hstr);
}

static std::string
gst_media_capture_subtype_to_video_format (const std::string &subtype)
{
  /* https://docs.microsoft.com/en-us/uwp/api/windows.media.mediaproperties.videoencodingproperties.subtype#Windows_Media_MediaProperties_VideoEncodingProperties_Subtype */
  if (g_ascii_strcasecmp (subtype.c_str(), "RGB32") == 0)
    return "BGRx";
  else if (g_ascii_strcasecmp (subtype.c_str(), "ARGB32") == 0)
    return "BGRA";
  else if (g_ascii_strcasecmp (subtype.c_str(), "RGB24") == 0)
    return "BGR";
  else if (g_ascii_strcasecmp (subtype.c_str(), "NV12") == 0)
    return "NV12";
  else if (g_ascii_strcasecmp (subtype.c_str(), "YV12") == 0)
    return "YV12";
  else if (g_ascii_strcasecmp (subtype.c_str(), "IYUV") == 0)
    return "I420";
  else if (g_ascii_strcasecmp (subtype.c_str(), "YUY2") == 0)
    return "YUY2";

  /* FIXME: add more */

  return std::string();
}

GstWinRTMediaDescription::GstWinRTMediaDescription()
  : caps_(nullptr)
{
}

GstWinRTMediaDescription::GstWinRTMediaDescription
    (const GstWinRTMediaDescription& other)
  : caps_(nullptr)
{
  if (other.source_id_.IsValid())
    other.source_id_.CopyTo(source_id_.GetAddressOf());
  if (other.subtype_.IsValid())
    other.subtype_.CopyTo(subtype_.GetAddressOf());
  gst_caps_replace (&caps_, other.caps_);
}

GstWinRTMediaDescription::~GstWinRTMediaDescription()
{
  Release();
}

void
GstWinRTMediaDescription::Release()
{
  source_id_.Release();
  subtype_.Release();
  gst_clear_caps(&caps_);
}

bool
GstWinRTMediaDescription::IsValid() const
{
  if (!source_id_.IsValid())
    return false;
  if (!subtype_.IsValid())
    return false;
  if (!caps_)
    return false;

  return true;
}

HRESULT
GstWinRTMediaDescription::Fill(HString &source_id,
    const ComPtr<IMediaCaptureVideoProfileMediaDescription>& desc)
{
  Release();

  if (!source_id.IsValid()) {
    GST_WARNING("Invalid source id");
    return E_FAIL;
  }

  ComPtr<IMediaCaptureVideoProfileMediaDescription2> desc2;
  UINT32 width = 0;
  UINT32 height = 0;
  DOUBLE framerate = 0;
  gint fps_n = 0, fps_d = 1;
  HString hstr_subtype;
  std::string subtype;
  std::string format;
  GstCaps *caps;
  HRESULT hr;

  hr = desc.As (&desc2);
  if (!gst_mf_result (hr))
    return hr;

  hr = desc->get_Width (&width);
  if (!gst_mf_result (hr))
    return hr;

  hr = desc->get_Height (&height);
  if (!gst_mf_result (hr))
    return hr;

  hr = desc->get_FrameRate (&framerate);
  if (gst_mf_result (hr) && framerate > 0)
    gst_util_double_to_fraction (framerate, &fps_n, &fps_d);

  hr = desc2->get_Subtype (hstr_subtype.GetAddressOf ());
  if (!gst_mf_result (hr))
    return hr;

  subtype = convert_hstring_to_string (&hstr_subtype);
  if (subtype.empty())
    return E_FAIL;

  format = gst_media_capture_subtype_to_video_format (subtype);
  if (format.empty()) {
    GST_FIXME ("Unhandled subtype %s", subtype.c_str());
    return E_FAIL;
  }

  caps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, format.c_str(), "width", G_TYPE_INT, width,
      "height", G_TYPE_INT, height, NULL);

  if (fps_n > 0 && fps_d > 0)
    gst_caps_set_simple (caps,
        "framerate", GST_TYPE_FRACTION, fps_n, fps_d, NULL);

  source_id.CopyTo (source_id_.GetAddressOf());
  hstr_subtype.CopyTo (subtype_.GetAddressOf());
  caps_ = caps;

  return S_OK;
}

GstWinRTMediaFrameSourceGroup::GstWinRTMediaFrameSourceGroup()
{
}

GstWinRTMediaFrameSourceGroup::GstWinRTMediaFrameSourceGroup
    (const GstWinRTMediaFrameSourceGroup& other)
{
  id_ = other.id_;
  display_name_ = other.display_name_;
  source_group_ = other.source_group_;
  source_list_ = other.source_list_;
}

GstWinRTMediaFrameSourceGroup::~GstWinRTMediaFrameSourceGroup()
{
  Release ();
}

void
GstWinRTMediaFrameSourceGroup::Release()
{
  id_.clear ();
  display_name_.clear ();
  source_group_.Reset ();
  source_list_.clear ();
}

bool
GstWinRTMediaFrameSourceGroup::Contain(const GstWinRTMediaDescription& desc)
{
  if (!desc.IsValid())
    return false;

  if (source_list_.empty())
    return false;

  for (const auto& iter: source_list_) {
    unsigned int dummy;
    if (wcscmp (iter.source_id_.GetRawBuffer (&dummy),
        desc.source_id_.GetRawBuffer (&dummy)))
      continue;

    if (wcscmp (iter.subtype_.GetRawBuffer (&dummy),
        desc.subtype_.GetRawBuffer (&dummy)))
      continue;

    if (gst_caps_is_equal (iter.caps_, desc.caps_))
      return true;
  }

  return false;
}

HRESULT
GstWinRTMediaFrameSourceGroup::Fill
    (const ComPtr<IMediaFrameSourceGroup> &source_group)
{
  HRESULT hr = S_OK;
  HString hstr_id;
  HString hstr_display_name;
  ComPtr<IVectorView<MediaFrameSourceInfo*>> info_list;
  UINT32 count = 0;

  Release();

  hr = source_group->get_Id(hstr_id.GetAddressOf());
  if (!gst_mf_result(hr))
    goto done;

  id_ = convert_hstring_to_string (&hstr_id);
  if (id_.empty()) {
    GST_WARNING ("Emptry source group id");
    hr = E_FAIL;
    goto done;
  }

  hr = source_group->get_DisplayName (hstr_display_name.GetAddressOf());
  if (!gst_mf_result (hr))
    goto done;

  display_name_ = convert_hstring_to_string (&hstr_display_name);
  if (display_name_.empty()) {
    GST_WARNING ("Empty display name");
    hr = E_FAIL;
    goto done;
  }

  hr = source_group->get_SourceInfos (&info_list);
  if (!gst_mf_result (hr))
    goto done;

  hr = info_list->get_Size (&count);
  if (!gst_mf_result (hr))
    goto done;

  if (count == 0) {
    GST_WARNING ("No available source info");
    hr = E_FAIL;
    goto done;
  }

  source_group_ = source_group;

  GST_DEBUG ("source-group has %d entries", count);

  for (UINT32 i = 0; i < count; i++) {
    ComPtr<IMediaFrameSourceInfo> info;
    ComPtr<IMediaFrameSourceInfo2> info2;
    ComPtr<IVectorView<MediaCaptureVideoProfileMediaDescription*>> desc_list;
    MediaFrameSourceKind source_kind;
    MediaStreamType source_type;
    UINT32 desc_count = 0;
    HString source_id;

    hr = info_list->GetAt(i, &info);
    if (!gst_mf_result (hr))
      continue;

    hr = info.As (&info2);
    if (!gst_mf_result (hr))
      continue;

    hr = info->get_SourceKind (&source_kind);
    if (!gst_mf_result (hr))
      continue;

    /* This can be depth, infrared or others */
    /* FIXME: add audio support */
    if (source_kind != MediaFrameSourceKind::MediaFrameSourceKind_Color) {
      GST_FIXME ("source-info has non-color source kind %d",
          (gint) source_kind);
      continue;
    }

    hr = info->get_MediaStreamType (&source_type);
    if (!gst_mf_result (hr))
      continue;

    /* FIXME: support audio */
    if (source_type != MediaStreamType::MediaStreamType_VideoPreview &&
        source_type != MediaStreamType::MediaStreamType_VideoRecord) {
      continue;
    }

    hr = info->get_Id (source_id.GetAddressOf ());
    if (!gst_mf_result (hr))
      continue;

    hr = info2->get_VideoProfileMediaDescription (&desc_list);
    if (!gst_mf_result (hr))
      continue;

    hr = desc_list->get_Size (&desc_count);
    if (!gst_mf_result (hr))
      continue;

    if (desc_count == 0) {
      GST_WARNING("source-info has empty media description");
      continue;
    }

    for (UINT32 j = 0; j < desc_count; j++) {
      ComPtr<IMediaCaptureVideoProfileMediaDescription> desc;

      hr = desc_list->GetAt (j, &desc);
      if (!gst_mf_result (hr))
        continue;

      GstWinRTMediaDescription media_desc;
      hr = media_desc.Fill(source_id, desc);
      if (!gst_mf_result(hr))
        continue;

      source_list_.push_back(media_desc);
    }
  }

done:
  if (source_list_.empty()) {
    GST_WARNING ("No usable source infos");
    hr = E_FAIL;
  }

  if (!gst_mf_result(hr))
    Release();

  return hr;
}

MediaCaptureWrapper::MediaCaptureWrapper()
  : user_data_(nullptr)
{
  user_cb_.frame_arrived = nullptr;
  user_cb_.failed = nullptr;

  /* Store CoreDispatecher if available */
  findCoreDispatcher();
}

MediaCaptureWrapper::~MediaCaptureWrapper()
{
  stopCapture();

  if (frame_reader_)
    frame_reader_->remove_FrameArrived (token_frame_arrived_);

  if (media_capture_)
    media_capture_->remove_Failed (token_capture_failed_);
}

void
MediaCaptureWrapper::RegisterCb (const MediaCaptureWrapperCallbacks &cb,
    void * user_data)
{
  user_cb_.frame_arrived = cb.frame_arrived;
  user_cb_.failed = cb.failed;
  user_data_ = user_data;
}

HRESULT
MediaCaptureWrapper::EnumrateFrameSourceGroup
    (std::vector<GstWinRTMediaFrameSourceGroup> &group_list)
{
  HRESULT hr = S_OK;

  if (dispatcher_) {
    hr = runOnUIThread (INFINITE,
      [this, &group_list] {
          return enumrateFrameSourceGroup(group_list);
      });

  } else {
    hr = enumrateFrameSourceGroup(group_list);
  }

  return hr;
}

HRESULT
MediaCaptureWrapper::SetSourceGroup(const GstWinRTMediaFrameSourceGroup &group)
{
  if (group.source_group_ == nullptr) {
    GST_WARNING ("Invalid MediaFrameSourceGroup");
    return E_FAIL;
  }

  if (group.source_list_.empty()) {
    GST_WARNING ("group doesn't include source lifo");
    return E_FAIL;
  }

  source_group_ =
      std::unique_ptr<GstWinRTMediaFrameSourceGroup>
      (new GstWinRTMediaFrameSourceGroup(group));

  return S_OK;
}

HRESULT
MediaCaptureWrapper::SetMediaDescription(const GstWinRTMediaDescription &desc)
{
  /* Must be source group was specified before this */
  if (source_group_ == nullptr) {
    GST_WARNING ("No frame source group was specified");
    return E_FAIL;
  }

  if (!desc.IsValid()) {
    GST_WARNING("Invalid MediaDescription");
    return E_FAIL;
  }

  if (!source_group_->Contain(desc)) {
    GST_WARNING ("MediaDescription is not part of current source group");
    return E_FAIL;
  }

  media_desc_ =
      std::unique_ptr<GstWinRTMediaDescription>
      (new GstWinRTMediaDescription(desc));

  return S_OK;
}

HRESULT
MediaCaptureWrapper::StartCapture()
{
  HRESULT hr = S_OK;

  hr = openMediaCapture();
  if (!gst_mf_result (hr))
    return hr;

  if (dispatcher_) {
    hr = runOnUIThread (INFINITE,
      [this] {
          return startCapture();
      });

  } else {
    hr = startCapture();
  }

  return S_OK;
}

HRESULT
MediaCaptureWrapper::StopCapture()
{
  HRESULT hr = S_OK;

  if (dispatcher_) {
    hr = runOnUIThread (INFINITE,
      [this] {
          return stopCapture();
      });

  } else {
    hr = stopCapture();
  }

  return S_OK;
}

HRESULT
MediaCaptureWrapper::GetAvailableDescriptions
    (std::vector<GstWinRTMediaDescription> &desc_list)
{
  desc_list.clear();

  if (!source_group_) {
    GST_WARNING ("No frame source group available");
    return E_FAIL;
  }

  desc_list = source_group_->source_list_;

  return S_OK;
}

HRESULT
MediaCaptureWrapper::openMediaCapture()
{
  HRESULT hr;

  if (frame_reader_) {
    GST_INFO ("Frame reader was configured");
    return S_OK;
  }

  if (source_group_ == nullptr) {
    GST_WARNING ("No frame source group was specified");
    return E_FAIL;
  }

  if (media_desc_ == nullptr) {
    GST_WARNING ("No media description was specified");
    return E_FAIL;
  }

  hr = mediaCaptureInitPre ();
  if (!gst_mf_result (hr))
    return hr;

  /* Wait user action and resulting mediaCaptureInitPost */
  std::unique_lock<std::mutex> Lock(lock_);
  if (!init_done_)
    cond_.wait (Lock);

  return frame_reader_ ? S_OK : E_FAIL;
}

HRESULT
MediaCaptureWrapper::mediaCaptureInitPre()
{
  ComPtr<IAsyncAction> async_action;
  HRESULT hr;

  auto work_item = Callback<Implements<RuntimeClassFlags<ClassicCom>,
        IDispatchedHandler, FtmBase>>([this]{
    ComPtr<IMediaCaptureInitializationSettings> settings;
    ComPtr<IMediaCaptureInitializationSettings5> settings5;
    ComPtr<IMediaCapture> media_capture;
    ComPtr<IMediaCapture5> media_capture5;
    ComPtr<IAsyncAction> init_async;
    HRESULT hr;
    HStringReference hstr_setting =
        HStringReference (
            RuntimeClass_Windows_Media_Capture_MediaCaptureInitializationSettings);
    HStringReference hstr_capture =
        HStringReference (RuntimeClass_Windows_Media_Capture_MediaCapture);
    IMediaFrameSourceGroup * source_group =
        source_group_->source_group_.Get();

    hr = ActivateInstance (hstr_setting.Get(), &settings);
    if (!gst_mf_result (hr))
      return hr;

    hr = settings->put_StreamingCaptureMode (
        StreamingCaptureMode::StreamingCaptureMode_Video);
    if (!gst_mf_result (hr))
      return hr;

    hr = settings.As (&settings5);
    if (!gst_mf_result (hr))
      return hr;

    hr = settings5->put_SourceGroup (source_group);
    if (!gst_mf_result (hr))
      return hr;

    /* TODO: support D3D11 memory */
    hr = settings5->put_MemoryPreference (
        MediaCaptureMemoryPreference::MediaCaptureMemoryPreference_Cpu);
    if (!gst_mf_result (hr))
      return hr;

    hr = settings5.As (&settings);
    if (!gst_mf_result (hr))
      return hr;

    hr = ActivateInstance (hstr_capture.Get(), &media_capture5);
    if (!gst_mf_result (hr))
      return hr;

    hr = media_capture5.As (&media_capture);
    if (!gst_mf_result (hr))
      return hr;

    hr = media_capture->InitializeWithSettingsAsync (settings.Get(), &init_async);
    if (!gst_mf_result (hr))
      return hr;

    return StartAsyncThen(
        init_async.Get(),
        [this, init_async, media_capture](_In_ HRESULT hr,
        _In_ IAsyncAction *asyncResult, _In_ AsyncStatus asyncStatus) -> HRESULT
      {
        return mediaCaptureInitPost (init_async, media_capture);
      });
  });

  init_done_ = false;

  if (dispatcher_) {
    hr = dispatcher_->RunAsync(CoreDispatcherPriority_Normal, work_item.Get(),
          &async_action);
  } else {
    hr = work_item->Invoke ();
  }

  return hr;
}

HRESULT
MediaCaptureWrapper::mediaCaptureInitPost (ComPtr<IAsyncAction> init_async,
    ComPtr<IMediaCapture> media_capture)
{
  std::unique_lock<std::mutex> Lock(lock_);
  ComPtr<IMediaFrameSource> frame_source;
  ComPtr<IMapView<HSTRING, MediaFrameSource*>> frameSources;
  ComPtr<IMediaFrameSource> source;
  ComPtr<IMediaFrameFormat> format;
  ComPtr<IVectorView<MediaFrameFormat*>> formatList;
  ComPtr<IMediaCapture5> media_capture5;
  ComPtr<IAsyncAction> set_format_async;
  ComPtr<IAsyncOperation<MediaFrameReader*>> create_reader_async;
  ComPtr<IMediaFrameReader> frame_reader;
  boolean has_key;
  UINT32 count = 0;
  GstVideoInfo videoInfo;
  HRESULT hr;
  ComPtr<ITypedEventHandler<MediaFrameReader*, MediaFrameArrivedEventArgs*>>
      frame_arrived_handler = Callback<ITypedEventHandler<MediaFrameReader*,
          MediaFrameArrivedEventArgs*>> ([&]
          (IMediaFrameReader * reader, IMediaFrameArrivedEventArgs* args)
            {
                return onFrameArrived(reader, args);
            }
        );

  GST_DEBUG ("InitializeWithSettingsAsync done");

  hr = init_async->GetResults ();
  if (!gst_mf_result (hr))
    goto done;

  if (!gst_video_info_from_caps (&videoInfo, media_desc_->caps_)) {
    GST_WARNING ("Couldn't convert caps to videoinfo");
    hr = E_FAIL;
    goto done;
  }

  hr = media_capture.As (&media_capture5);
  if (!gst_mf_result (hr))
    goto done;

  hr = media_capture5->get_FrameSources (&frameSources);
  if (!gst_mf_result (hr))
    goto done;

  hr = frameSources->HasKey (media_desc_->source_id_.Get(), &has_key);
  if (!gst_mf_result (hr))
    goto done;

  if (!has_key) {
    GST_ERROR ("MediaFrameSource unavailable");
    hr = E_FAIL;
    goto done;
  }

  hr = frameSources->Lookup (media_desc_->source_id_.Get(), &source);
  if (!gst_mf_result (hr))
    goto done;

  hr = source->get_SupportedFormats (&formatList);
  if (!gst_mf_result (hr))
    goto done;

  hr = formatList->get_Size (&count);
  if (!gst_mf_result (hr))
    goto done;

  if (count == 0) {
    GST_ERROR ("No supported format object");
    hr = E_FAIL;
    goto done;
  }

  /* FIXME: support audio */
  for (UINT32 i = 0; i < count; i++) {
    ComPtr<IMediaFrameFormat> fmt;
    ComPtr<IVideoMediaFrameFormat> videoFmt;
    ComPtr<IMediaRatio> ratio;
    HString subtype;
    UINT32 width = 0;
    UINT32 height = 0;

    hr = formatList->GetAt (i, &fmt);
    if (!gst_mf_result (hr))
      continue;

    hr = fmt->get_VideoFormat (&videoFmt);
    if (!gst_mf_result (hr))
      continue;

    hr = videoFmt->get_Width (&width);
    if (!gst_mf_result (hr))
      continue;

    hr = videoFmt->get_Height (&height);
    if (!gst_mf_result (hr))
      continue;

    if (width != GST_VIDEO_INFO_WIDTH (&videoInfo))
      continue;

    if (height != GST_VIDEO_INFO_HEIGHT (&videoInfo))
      continue;

    /* TODO: check major type for audio */
    hr = fmt->get_Subtype (subtype.GetAddressOf ());
    if (!gst_mf_result (hr))
      continue;

    if (wcscmp (subtype.GetRawBuffer (nullptr),
        media_desc_->subtype_.GetRawBuffer (nullptr)))
      continue;

    format = fmt;
    break;
  }

  if (!format) {
    GST_ERROR (
        "Couldn't find matching IMediaFrameFormat interface");
    hr = E_FAIL;
    goto done;
  }

  hr = source->SetFormatAsync (format.Get (), &set_format_async);
  if (!gst_mf_result (hr))
    goto done;

  hr = SyncWait<void>(set_format_async.Get ());
  if (!gst_mf_result (hr))
    goto done;

  hr = set_format_async->GetResults ();
  if (!gst_mf_result (hr))
    goto done;

  hr = media_capture5->CreateFrameReaderAsync (source.Get(),
      &create_reader_async);
  if (!gst_mf_result (hr))
    goto done;

  hr = SyncWait<MediaFrameReader*>(create_reader_async.Get());
  if (!gst_mf_result (hr))
    goto done;

  hr = create_reader_async->GetResults(&frame_reader);
  if (!gst_mf_result (hr))
    goto done;

  hr = frame_reader->add_FrameArrived (frame_arrived_handler.Get(),
      &token_frame_arrived_);
  if (!gst_mf_result (hr))
    goto done;

  hr = media_capture->add_Failed
      (Callback<IMediaCaptureFailedEventHandler> ([this]
          (IMediaCapture * capture, IMediaCaptureFailedEventArgs* args)
            {
                return onCaptureFailed(capture, args);
            }
        ).Get(),
      &token_capture_failed_);

  if (!gst_mf_result (hr))
    goto done;

  frame_reader_ = frame_reader;
  media_capture_ = media_capture;

done:
  init_done_ = true;
  cond_.notify_all();

  return S_OK;
}

HRESULT
MediaCaptureWrapper::startCapture()
{
  HRESULT hr;

  if (!frame_reader_) {
    GST_ERROR ("Frame reader wasn't configured");
    return E_FAIL;
  }

  ComPtr<IAsyncOperation<MediaFrameReaderStartStatus>> start_async;
  hr = frame_reader_->StartAsync (&start_async);
  if (!gst_mf_result (hr))
    return hr;

  hr = SyncWait<MediaFrameReaderStartStatus>(start_async.Get());
  if (!gst_mf_result (hr))
    return hr;

  MediaFrameReaderStartStatus reader_status;
  hr = start_async->GetResults(&reader_status);
  if (!gst_mf_result (hr))
    return hr;

  if (reader_status !=
      MediaFrameReaderStartStatus::MediaFrameReaderStartStatus_Success) {
    GST_ERROR ("Cannot start frame reader, status %d",
        (gint) reader_status);
    return E_FAIL;
  }

  return S_OK;
}

HRESULT
MediaCaptureWrapper::stopCapture()
{
  HRESULT hr = S_OK;

  if (frame_reader_) {
    ComPtr<IAsyncAction> async_action;

    hr = frame_reader_->StopAsync (&async_action);
    if (gst_mf_result (hr))
      hr = SyncWait<void>(async_action.Get ());
  }

  return hr;
}

HRESULT
MediaCaptureWrapper::onFrameArrived(IMediaFrameReader *reader,
    IMediaFrameArrivedEventArgs *args)
{
  HRESULT hr;
  ComPtr<IMediaFrameReference> frame_ref;
  ComPtr<IVideoMediaFrame> video_frame;
  ComPtr<ISoftwareBitmap> bitmap;

  hr = reader->TryAcquireLatestFrame (&frame_ref);
  if (!gst_mf_result (hr))
    return hr;

  if (!frame_ref)
    return S_OK;

  hr = frame_ref->get_VideoMediaFrame (&video_frame);
  if (!gst_mf_result (hr))
    return hr;

  hr = video_frame->get_SoftwareBitmap (&bitmap);
  if (!gst_mf_result (hr) || !bitmap)
    return hr;

  /* nothing to do if no callback was installed */
  if (!user_cb_.frame_arrived)
    return S_OK;

  return user_cb_.frame_arrived (bitmap.Get(), user_data_);
}

HRESULT
MediaCaptureWrapper::onCaptureFailed(IMediaCapture *capture,
    IMediaCaptureFailedEventArgs *args)
{
  HRESULT hr;
  UINT32 error_code = 0;
  HString hstr_error_msg;
  std::string error_msg;

  hr = args->get_Code (&error_code);
  gst_mf_result (hr);

  hr = args->get_Message (hstr_error_msg.GetAddressOf());
  gst_mf_result (hr);

  error_msg = convert_hstring_to_string (&hstr_error_msg);

  GST_WARNING ("Have error %s (%d)", error_msg.c_str(), error_code);

  if (user_cb_.failed)
    user_cb_.failed (error_msg, error_code, user_data_);

  return S_OK;
}

void
MediaCaptureWrapper::findCoreDispatcher()
{
  HStringReference hstr_core_app =
      HStringReference(RuntimeClass_Windows_ApplicationModel_Core_CoreApplication);
  HRESULT hr;

  ComPtr<ICoreApplication> core_app;
  hr = GetActivationFactory (hstr_core_app.Get(), &core_app);
  if (!gst_mf_result(hr))
    return;

  ComPtr<ICoreApplicationView> core_app_view;
  hr = core_app->GetCurrentView (&core_app_view);
  if (!gst_mf_result(hr))
    return;

  ComPtr<ICoreWindow> core_window;
  hr = core_app_view->get_CoreWindow (&core_window);
  if (!gst_mf_result(hr))
    return;

  hr = core_window->get_Dispatcher (&dispatcher_);
  if (!gst_mf_result(hr))
    return;

  GST_DEBUG("Main UI dispatcher is available");
}

HRESULT
MediaCaptureWrapper::enumrateFrameSourceGroup
    (std::vector<GstWinRTMediaFrameSourceGroup> &groupList)
{
  ComPtr<IMediaFrameSourceGroupStatics> frame_source_group_statics;
  ComPtr<IAsyncOperation<IVectorView<MediaFrameSourceGroup*>*>> async_op;
  ComPtr<IVectorView<MediaFrameSourceGroup*>> source_group_list;
  HRESULT hr;
  unsigned int cnt = 0;
  HStringReference hstr_frame_source_group =
      HStringReference (
          RuntimeClass_Windows_Media_Capture_Frames_MediaFrameSourceGroup);

  groupList.clear();

  hr = GetActivationFactory (hstr_frame_source_group.Get(),
                            &frame_source_group_statics);
  if (!gst_mf_result(hr))
    return hr;

  hr = frame_source_group_statics->FindAllAsync (&async_op);
  if (!gst_mf_result(hr))
    return hr;

  hr = SyncWait<IVectorView<MediaFrameSourceGroup*>*>(async_op.Get(), 5000);
  if (!gst_mf_result(hr))
    return hr;

  hr = async_op->GetResults (&source_group_list);
  if (!gst_mf_result(hr))
    return hr;

  hr = source_group_list->get_Size (&cnt);
  if (!gst_mf_result(hr))
    return hr;

  if (cnt == 0) {
    GST_WARNING ("No available source group");
    return E_FAIL;
  }

  GST_DEBUG("Have %u source group", cnt);

  for (unsigned int i = 0; i < cnt; i++) {
    ComPtr<IMediaFrameSourceGroup> group;

    hr = source_group_list->GetAt (i, &group);
    if (!gst_mf_result(hr))
      continue;

    GstWinRTMediaFrameSourceGroup source_group;
    hr = source_group.Fill(group);
    if (!gst_mf_result (hr))
      continue;

    groupList.push_back (source_group);
  }

  if (groupList.empty ()) {
    GST_WARNING("No available source group");
    return E_FAIL;
  }

  return S_OK;
}
