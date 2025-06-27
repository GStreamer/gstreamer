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

#pragma once

#include <gst/gst.h>
#include "gstwasapi2util.h"
#include <wrl.h>
#include <atomic>
#include <string>

/* Copy of audioclientactivationparams.h since those types are defined only for
 * NTDDI_VERSION >= NTDDI_WIN10_FE */
#define VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK L"VAD\\Process_Loopback"
typedef enum
{
  PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE = 0,
  PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE = 1
} PROCESS_LOOPBACK_MODE;

typedef struct
{
  DWORD TargetProcessId;
  PROCESS_LOOPBACK_MODE ProcessLoopbackMode;
} AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS;

typedef enum
{
  AUDIOCLIENT_ACTIVATION_TYPE_DEFAULT = 0,
  AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK = 1
} AUDIOCLIENT_ACTIVATION_TYPE;

typedef struct
{
  AUDIOCLIENT_ACTIVATION_TYPE ActivationType;
  union
  {
    AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS ProcessLoopbackParams;
  } DUMMYUNIONNAME;
} AUDIOCLIENT_ACTIVATION_PARAMS;
/* End of audioclientactivationparams.h */

DEFINE_GUID (IID_Wasapi2ActivationHandler, 0xaa7e8f85, 0x211e,
    0x42cc, 0x8c, 0x86, 0x99, 0x83, 0x5b, 0xef, 0x54, 0x86);
class Wasapi2ActivationHandler :
  public IActivateAudioInterfaceCompletionHandler
{
public:
  static void CreateInstance (Wasapi2ActivationHandler ** handler,
                              const wchar_t * device_id,
                              const AUDIOCLIENT_ACTIVATION_PARAMS * params);

  STDMETHODIMP_ (ULONG) AddRef (void);

  STDMETHODIMP_ (ULONG) Release (void);

  STDMETHODIMP QueryInterface (REFIID riid, void ** object);

  STDMETHODIMP ActivateCompleted (IActivateAudioInterfaceAsyncOperation * op);

  HRESULT ActivateAsync (void);

  HRESULT GetClient (IAudioClient ** client, DWORD timeout);

private:
  Wasapi2ActivationHandler ();
  virtual ~Wasapi2ActivationHandler ();

private:
  Microsoft::WRL::ComPtr<IAudioClient> client_;
  std::atomic<HRESULT> activate_hr_ = { E_FAIL };
  HANDLE event_;
  PROPVARIANT prop_ = { };
  AUDIOCLIENT_ACTIVATION_PARAMS params_ = { };
  bool have_params_ = false;
  std::wstring device_id_;
  ULONG refcount_ = 1;
};
