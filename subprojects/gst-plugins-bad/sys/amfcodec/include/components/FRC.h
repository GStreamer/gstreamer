//
// Notice Regarding Standards.  AMD does not provide a license or sublicense to
// any Intellectual Property Rights relating to any standards, including but not
// limited to any audio and/or video codec technologies such as MPEG-2, MPEG-4;
// AVC/H.264; HEVC/H.265; AAC decode/FFMPEG; AAC encode/FFMPEG; VC-1; and MP3
// (collectively, the "Media Technologies"). For clarity, you will pay any
// royalties due for such third party technologies, which may include the Media
// Technologies that are owed as a result of AMD providing the Software to you.
//
// MIT license
//
// Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#ifndef AMFFRC_h
#define AMFFRC_h

#pragma once

#define AMFFRC L"AMFFRC"

// Select rendering API for FRC
enum AMF_FRC_ENGINE
{
    FRC_ENGINE_OFF              = 0,
    FRC_ENGINE_DX12             = 1,
    FRC_ENGINE_OPENCL           = 2,
    FRC_ENGINE_DX11             = 3,
};

// Select present mode for FRC
enum AMF_FRC_MODE_TYPE
{
    FRC_OFF                     = 0,
    FRC_ON                      = 1,
    FRC_ONLY_INTERPOLATED       = 2,
    FRC_x2_PRESENT              = 3,
    TOTAL_FRC_MODES
};

enum AMF_FRC_PROFILE_TYPE {
    FRC_PROFILE_LOW             = 0,
    FRC_PROFILE_HIGH            = 1,
    FRC_PROFILE_SUPER           = 2,
    TOTAL_FRC_PROFILES
};

enum AMF_FRC_MV_SEARCH_MODE_TYPE {
    FRC_MV_SEARCH_NATIVE        = 0,
    FRC_MV_SEARCH_PERFORMANCE   = 1,
    TOTAL_FRC_MV_SEARCH_MODES
};

#define AMF_FRC_ENGINE_TYPE             L"FRCEngineType"            // amf_int64(AMF_FRC_ENGINE); default = DX12; determines how the object is initialized and what kernels to use
#define AMF_FRC_OUTPUT_SIZE             L"FRCSOutputSize"           // AMFSize - output scaling width/height
#define AMF_FRC_MODE                    L"FRCMode"                  // amf_int64(AMF_FRC_MODE_TYPE); default = FRC_ONLY_INTERPOLATED; FRC mode
#define AMF_FRC_ENABLE_FALLBACK         L"FRCEnableFallback"        // bool; default = true; FRC enable fallback mode
#define AMF_FRC_INDICATOR               L"FRCIndicator"             // bool; default : false; draw indicator in the corner
#define AMF_FRC_PROFILE                 L"FRCProfile"               // amf_int64(AMF_FRC_PROFILE_TYPE); default=FRC_PROFILE_HIGH; FRC profile
#define AMF_FRC_MV_SEARCH_MODE          L"FRCMVSEARCHMODE"          // amf_int64(AMF_FRC_MV_SEARCH_MODE_TYPE); defaut = FRC_MV_SEARCH_NATIVE; FRC MV search mode
#define AMF_FRC_USE_FUTURE_FRAME        L"FRCUseFutureFrame"        // bool; default = true; Enable dependency on future frame, improves quality for the cost of latency

#endif //#ifndef AMFFRC_h
