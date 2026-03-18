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
//
// Copyright (c) 2020 Advanced Micro Devices, Inc. All rights reserved.
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
#ifndef AMF_XInput_h
#define AMF_XInput_h

#pragma once

#include "../core/Interface.h"

// XInput injection API:
// - XBox  - like controller emulation
// - event injection
// - vibration notifications

#if defined(__cplusplus)
namespace amf
{
#endif
    //----------------------------------------------------------------------------------------------
    // AMFXInput interface
    //----------------------------------------------------------------------------------------------

    typedef enum AMF_CONTROLLER_TYPE
    {
        AMF_CONTROLLER_XBOX360 = 1,
    } AMF_CONTROLLER_TYPE;


    typedef struct AMFXInputCreationDesc
    {
        AMF_CONTROLLER_TYPE   eType;
        amf_uint32            reserved[100];
    } AMFXInputCreationDesc;

    //----------------------------------------------------------------------------------------------
    // Constants for gamepad buttons - match Xinput.h
    //----------------------------------------------------------------------------------------------
    #define AMF_XINPUT_GAMEPAD_DPAD_UP          0x0001
    #define AMF_XINPUT_GAMEPAD_DPAD_DOWN        0x0002
    #define AMF_XINPUT_GAMEPAD_DPAD_LEFT        0x0004
    #define AMF_XINPUT_GAMEPAD_DPAD_RIGHT       0x0008
    #define AMF_XINPUT_GAMEPAD_START            0x0010
    #define AMF_XINPUT_GAMEPAD_BACK             0x0020
    #define AMF_XINPUT_GAMEPAD_LEFT_THUMB       0x0040
    #define AMF_XINPUT_GAMEPAD_RIGHT_THUMB      0x0080
    #define AMF_XINPUT_GAMEPAD_LEFT_SHOULDER    0x0100
    #define AMF_XINPUT_GAMEPAD_RIGHT_SHOULDER   0x0200
    #define AMF_XINPUT_GAMEPAD_A                0x1000
    #define AMF_XINPUT_GAMEPAD_B                0x2000
    #define AMF_XINPUT_GAMEPAD_X                0x4000
    #define AMF_XINPUT_GAMEPAD_Y                0x8000
    //----------------------------------------------------------------------------------------------
    typedef struct AMFXInputState
    {
        amf_uint32      uiButtonStates;    //  bit-wize flags AMF_XINPUT_GAMEPAD_<> - the same as XINPUT_GAMEPAD_<> from Windows SDK XInput.h
        amf_float       fLeftTrigger;      //  0.0f , 1.0f
        amf_float       fRightTrigger;     //  0.0f , 1.0f
        amf_float       fThumbLX;          // -1.0f , 1.0f
        amf_float       fThumbLY;          // -1.0f , 1.0f
        amf_float       fThumbRX;          // -1.0f , 1.0f
        amf_float       fThumbRY;          // -1.0f , 1.0f
    } AMFXInputState;
    //----------------------------------------------------------------------------------------------
    typedef struct AMFXInputHaptic
    {
        amf_float        fLeftMotor;        //  0.0f , 1.0f - motor level
        amf_float        fRightMotor;    //  0.0f , 1.0f - motor level
    } AMFXInputHaptic;
    //----------------------------------------------------------------------------------------------
#if defined(__cplusplus)
    //----------------------------------------------------------------------------------------------
    class AMF_NO_VTABLE AMFXInputCallback
    {
    public:
        virtual void                AMF_STD_CALL OnHaptic(amf_int32 id, const AMFXInputHaptic* pHaptic) = 0;
    };
    //----------------------------------------------------------------------------------------------
    class AMF_NO_VTABLE AMFXInputController : public AMFInterface
    {
    public:
        AMF_DECLARE_IID(0xbcaaaf0e, 0x6766, 0x46ac, 0xb1, 0xf1, 0x31, 0x5d, 0xca, 0x71, 0xe3, 0x4d)

        virtual amf_int32           AMF_STD_CALL GetControllerID() const = 0;
        virtual AMF_RESULT          AMF_STD_CALL SetCallback(AMFXInputCallback *pCallback) = 0;

        virtual AMF_RESULT          AMF_STD_CALL SetState(const AMFXInputState *pState) = 0;
        virtual AMF_RESULT          AMF_STD_CALL GetState(AMFXInputState *pState) const = 0;
        virtual AMF_RESULT          AMF_STD_CALL Terminate() = 0;
    };
    typedef amf::AMFInterfacePtr_T<AMFXInputController> AMFXInputControllerPtr;
#endif
    //----------------------------------------------------------------------------------------------
#endif
#if defined(__cplusplus)
}

#define AMF_XINPUT_CREATE_CONTROLLER_FUNCTION_NAME             "AMFXInputCreateController"

#if defined(__cplusplus)
extern "C"
{
    typedef AMF_RESULT(AMF_CDECL_CALL *AMFXInputCreateController_Fn)(amf_uint64 version, amf::AMFXInputCreationDesc* params, amf::AMFXInputController **ppController);
}
#else 
    typedef AMF_RESULT(AMF_CDECL_CALL *AMFXInputCreateController_Fn)(amf_uint64 version, AMFXInputCreationDesc* params, AMFXInputController **ppController);
#endif


#endif // AMF_XInput_h
