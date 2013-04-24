/*
 * Copyright (C) 2010 Ole André Vadla Ravnås <oleavr@soundrop.com>
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

#ifndef __GST_CEL_API_H__
#define __GST_CEL_API_H__

#include "mtapi.h"

G_BEGIN_DECLS

typedef struct _GstCelApi GstCelApi;
typedef struct _GstCelApiClass GstCelApiClass;

enum
{
  kCelError_ResourceBusy = -12780
};

struct _GstCelApi
{
  GstDynApi parent;

  OSStatus (* FigCreateCaptureDevicesAndStreamsForPreset)
      (CFAllocatorRef allocator, CFStringRef capturePreset,
      CFDictionaryRef audioOptions,
      FigCaptureDeviceRef * outVideoDevice,
      FigCaptureStreamRef * outVideoStream,
      FigCaptureDeviceRef * outAudioDevice,
      FigCaptureStreamRef * outAudioStream);

  CFStringRef * kFigCaptureDeviceProperty_MultiplexStreams;
  CFStringRef * kFigCapturePortType_Bottom;
  CFStringRef * kFigCapturePortType_Camera;
  CFStringRef * kFigCapturePortType_FrontFacingCamera;
  CFStringRef * kFigCapturePortType_Top;
  CFStringRef * kFigCapturePropertyValue_AFEarlyOutAllowPeakAtStart;
  CFStringRef * kFigCapturePropertyValue_AFEarlyOutDecrementAmount;
  CFStringRef * kFigCapturePropertyValue_AFEarlyOutEnable;
  CFStringRef * kFigCapturePropertyValue_AFEarlyOutThreshold;
  CFStringRef * kFigCapturePropertyValue_AFPositionCurrent;
  CFStringRef * kFigCapturePropertyValue_AFPositionInfinity;
  CFStringRef * kFigCapturePropertyValue_AFPositionMacro;
  CFStringRef * kFigCapturePropertyValue_AFSearchPositionArray;
  CFStringRef * kFigCapturePropertyValue_AGC;
  CFStringRef * kFigCapturePropertyValue_CLPFControl;
  CFStringRef * kFigCapturePropertyValue_ColorRangeFull;
  CFStringRef * kFigCapturePropertyValue_ColorRangeSDVideo;
  CFStringRef * kFigCapturePropertyValue_ModuleDate;
  CFStringRef * kFigCapturePropertyValue_ModuleIntegratorInfo;
  CFStringRef * kFigCapturePropertyValue_SensorID;
  CFStringRef * kFigCapturePropertyValue_SigmaFilterControl;
  CFStringRef * kFigCapturePropertyValue_YLPFControl;
  CFStringRef * kFigCapturePropertyValue_hStart;
  CFStringRef * kFigCapturePropertyValue_height;
  CFStringRef * kFigCapturePropertyValue_ispDGain;
  CFStringRef * kFigCapturePropertyValue_sensorDGain;
  CFStringRef * kFigCapturePropertyValue_shutterSpeedDenominator;
  CFStringRef * kFigCapturePropertyValue_shutterSpeedNumerator;
  CFStringRef * kFigCapturePropertyValue_vStart;
  CFStringRef * kFigCapturePropertyValue_weight;
  CFStringRef * kFigCapturePropertyValue_width;
  CFStringRef * kFigCaptureStreamPropertyValue_AEBracketedCaptureParams;
  CFStringRef * kFigCaptureStreamPropertyValue_BLCCompensation;
  CFStringRef * kFigCaptureStreamPropertyValue_BLCDebugMode;
  CFStringRef * kFigCaptureStreamPropertyValue_BandHighFactor;
  CFStringRef * kFigCaptureStreamPropertyValue_BandLowFactor;
  CFStringRef * kFigCaptureStreamPropertyValue_CCMWarmUpWeight;
  CFStringRef * kFigCaptureStreamPropertyValue_EdgeColorSuppressionSlope;
  CFStringRef * kFigCaptureStreamPropertyValue_EdgeColorSuppressionThreshold;
  CFStringRef * kFigCaptureStreamPropertyValue_EnableAESceneDynamicMetering;
  CFStringRef * kFigCaptureStreamPropertyValue_EnableCCMWarmUp;
  CFStringRef * kFigCaptureStreamPropertyValue_EnableHistogram1MetaData;
  CFStringRef * kFigCaptureStreamPropertyValue_EnableHistogram2MetaData;
  CFStringRef * kFigCaptureStreamPropertyValue_EnableHistogram3MetaData;
  CFStringRef * kFigCaptureStreamPropertyValue_EnableHistogram4MetaData;
  CFStringRef * kFigCaptureStreamPropertyValue_EnableHistogram;
  CFStringRef * kFigCaptureStreamPropertyValue_HistogramBinMode;
  CFStringRef * kFigCaptureStreamPropertyValue_HistogramDataType;
  CFStringRef * kFigCaptureStreamPropertyValue_ImageCropRect;
  CFStringRef * kFigCaptureStreamPropertyValue_LPExposure;
  CFStringRef * kFigCaptureStreamPropertyValue_LPGain;
  CFStringRef * kFigCaptureStreamPropertyValue_LowWeight;
  CFStringRef * kFigCaptureStreamPropertyValue_MaxWeight;
  CFStringRef * kFigCaptureStreamPropertyValue_MediumWeight;
  CFStringRef * kFigCaptureStreamPropertyValue_MinWeight;
  CFStringRef * kFigCaptureStreamPropertyValue_WeightDropOff;
  CFStringRef * kFigCaptureStreamPropertyValue_WeightReduction;
  CFStringRef * kFigCaptureStreamProperty_AEConvergenceSpeed;
  CFStringRef * kFigCaptureStreamProperty_AEOutlierClipCount;
  CFStringRef * kFigCaptureStreamProperty_AESceneDynamicMetering;
  CFStringRef * kFigCaptureStreamProperty_AEStability;
  CFStringRef * kFigCaptureStreamProperty_AEWindowManualWeightMatrix;
  CFStringRef * kFigCaptureStreamProperty_AEWindowParams;
  CFStringRef * kFigCaptureStreamProperty_AFEarlyOutParams;
  CFStringRef * kFigCaptureStreamProperty_AFParams;
  CFStringRef * kFigCaptureStreamProperty_AFSearchPositions;
  CFStringRef * kFigCaptureStreamProperty_AFWindowParams;
  CFStringRef * kFigCaptureStreamProperty_AGC;
  CFStringRef * kFigCaptureStreamProperty_AWBWindowParams;
  CFStringRef * kFigCaptureStreamProperty_AdditionalPTSOffset;
  CFStringRef * kFigCaptureStreamProperty_AlternateAWB;
  CFStringRef * kFigCaptureStreamProperty_Apply3AWindowSettings;
  CFStringRef * kFigCaptureStreamProperty_AttachRAW;
  CFStringRef * kFigCaptureStreamProperty_CCMWarmUp;
  CFStringRef * kFigCaptureStreamProperty_ClientMaxBufferCountHint;
  CFStringRef * kFigCaptureStreamProperty_ColorRange;
  CFStringRef * kFigCaptureStreamProperty_ColorSaturation;
  CFStringRef * kFigCaptureStreamProperty_ColorTables;
  CFStringRef * kFigCaptureStreamProperty_EdgeColorSuppressionParams;
  CFStringRef * kFigCaptureStreamProperty_ExposureBias;
  CFStringRef * kFigCaptureStreamProperty_FastSwitchMode;
  CFStringRef * kFigCaptureStreamProperty_FlashMode;
  CFStringRef * kFigCaptureStreamProperty_HistogramParams;
  CFStringRef * kFigCaptureStreamProperty_LockAENow;
  CFStringRef * kFigCaptureStreamProperty_LockAWBNow;
  CFStringRef * kFigCaptureStreamProperty_ManualAENow;
  CFStringRef * kFigCaptureStreamProperty_ManualFocusNow;
  CFStringRef * kFigCaptureStreamProperty_MaxIntegrationTime;
  CFStringRef * kFigCaptureStreamProperty_ModuleInfo;
  CFStringRef * kFigCaptureStreamProperty_NoiseReductionControls;
  CFStringRef * kFigCaptureStreamProperty_PortType;
  CFStringRef * kFigCaptureStreamProperty_PreFrameAE;
  CFStringRef * kFigCaptureStreamProperty_RawImageProcessNow;
  CFStringRef * kFigCaptureStreamProperty_RedEyeReductionParams;
  CFStringRef * kFigCaptureStreamProperty_ResetParams;
  CFStringRef * kFigCaptureStreamProperty_ScalerSharpening;
  CFStringRef * kFigCaptureStreamProperty_SetGainCap;
  CFStringRef * kFigCaptureStreamProperty_SharpeningControl;
  CFStringRef * kFigCaptureStreamProperty_TorchLevel;
  CFStringRef * kFigCaptureStreamProperty_UnlockAENow;
  CFStringRef * kFigCaptureStreamProperty_UnlockAWBNow;
  CFStringRef * kFigCaptureStreamProperty_UseFlashAFAssist;
  CFStringRef * kFigCaptureStreamProperty_UseFlashRedEyeReduction;
  CFStringRef * kFigCaptureStreamProperty_UseHardwareShutter;
  CFStringRef * kFigCaptureStreamProperty_VideoRecordingInProgress;
  CFStringRef * kFigRecorderCapturePreset_AudioRecording;
  CFStringRef * kFigRecorderCapturePreset_AudioVideoRecording;
  CFStringRef * kFigRecorderCapturePreset_PhotoCapture;
  CFStringRef * kFigRecorderCapturePreset_VideoRecording;
};

struct _GstCelApiClass
{
  GstDynApiClass parent_class;
};

GType gst_cel_api_get_type (void);

GstCelApi * gst_cel_api_obtain (GError ** error);

G_END_DECLS

#endif
