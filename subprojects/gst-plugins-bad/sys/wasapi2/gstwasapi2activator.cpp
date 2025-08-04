/* GStreamer
 * Copyright (C) 2025 Seungha Yang <seungha@centricular.com>
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

#include "gstwasapi2activator.h"
#include <objidl.h>

GST_DEBUG_CATEGORY_EXTERN (gst_wasapi2_debug);
#define GST_CAT_DEFAULT gst_wasapi2_debug

/* *INDENT-OFF* */
using namespace Microsoft::WRL;

void
Wasapi2ActivationHandler::CreateInstance (Wasapi2ActivationHandler ** handler,
    const wchar_t * device_id,
    const AUDIOCLIENT_ACTIVATION_PARAMS * params)
{
  auto self = new Wasapi2ActivationHandler ();
  self->device_id_ = device_id;

  if (params) {
    self->params_ = *params;
    self->prop_.vt = VT_BLOB;
    self->prop_.blob.cbSize = sizeof (AUDIOCLIENT_ACTIVATION_PARAMS);
    self->prop_.blob.pBlobData = (BYTE *) &self->params_;
    self->have_params_ = true;
  }

  *handler = self;
}

STDMETHODIMP_ (ULONG)
Wasapi2ActivationHandler::AddRef (void)
{
  return InterlockedIncrement (&refcount_);
}

STDMETHODIMP_ (ULONG)
Wasapi2ActivationHandler::Release (void)
{
  ULONG ref_count;

  ref_count = InterlockedDecrement (&refcount_);

  if (ref_count == 0)
    delete this;

  return ref_count;
}

STDMETHODIMP
Wasapi2ActivationHandler::QueryInterface (REFIID riid, void ** object)
{
  if (riid == __uuidof(IUnknown) || riid == __uuidof(IAgileObject)) {
    *object = static_cast<IUnknown *>(static_cast<Wasapi2ActivationHandler*>(this));
  } else if (riid == __uuidof(IActivateAudioInterfaceCompletionHandler)) {
    *object = static_cast<IActivateAudioInterfaceCompletionHandler *>(
        static_cast<Wasapi2ActivationHandler*>(this));
  } else if (riid == IID_Wasapi2ActivationHandler) {
    *object = this;
  } else {
    *object = nullptr;
    return E_NOINTERFACE;
  }

  AddRef ();

  return S_OK;
}

STDMETHODIMP
Wasapi2ActivationHandler::ActivateCompleted (IActivateAudioInterfaceAsyncOperation * op)
{
  ComPtr<IUnknown> iface;
  HRESULT hr = S_OK;
  HRESULT activate_hr = S_OK;
  hr = op->GetActivateResult (&activate_hr, &iface);
  if (!gst_wasapi2_result (hr))
    GST_ERROR ("Couldn't get activate result, hr: 0x%x", (guint) hr);

  if (!gst_wasapi2_result (activate_hr)) {
    GST_ERROR ("GetActivateResult failed, hr: 0x%x", (guint) activate_hr);
    hr = activate_hr;
  }

  if (SUCCEEDED (hr) && !iface) {
    GST_ERROR ("Couldn't get inteface from asyncop");
    hr = E_FAIL;
  }

  if (FAILED (hr)) {
    activate_hr_ = hr;
    SetEvent (event_);
    return hr;
  }

  {
    std::lock_guard<std::mutex> lk (lock_);
    hr = iface.As (&client_);
    activate_hr_ = hr;
  }

  GST_LOG ("Activation result 0x%x", (guint) hr);

  SetEvent (event_);

  return hr;
}

HRESULT
Wasapi2ActivationHandler::ActivateAsync ()
{
  ComPtr<IActivateAudioInterfaceAsyncOperation> async_op;
  auto hr = ActivateAudioInterfaceAsync (device_id_.c_str (),
      __uuidof (IAudioClient), have_params_ ? &prop_ : nullptr,
      this, &async_op);
  if (!gst_wasapi2_result (hr)) {
    activate_hr_ = hr;
    SetEvent (event_);
  }

  return hr;
}

HRESULT
Wasapi2ActivationHandler::GetClient (IAudioClient ** client, DWORD timeout)
{
  WaitForSingleObject (event_, timeout);
  auto hr = activate_hr_.load ();
  if (!gst_wasapi2_result (hr))
    return hr;

  if (!client)
    return S_OK;

  std::lock_guard<std::mutex> lk (lock_);
  if (!client_)
    return E_FAIL;

  *client = client_.Get ();
  (*client)->AddRef ();

  return S_OK;
}

Wasapi2ActivationHandler::Wasapi2ActivationHandler ()
{
  event_ = CreateEvent (nullptr, TRUE, FALSE, nullptr);
}

Wasapi2ActivationHandler::~Wasapi2ActivationHandler ()
{
  CloseHandle (event_);
}
/* *INDENT-ON* */
