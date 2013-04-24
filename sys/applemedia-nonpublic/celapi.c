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

#include "celapi.h"

#include "dynapi-internal.h"

#define CELESTIAL_FRAMEWORK_PATH "/System/Library/PrivateFrameworks/" \
    "Celestial.framework/Celestial"

G_DEFINE_TYPE (GstCelApi, gst_cel_api, GST_TYPE_DYN_API);

static void
gst_cel_api_init (GstCelApi * self)
{
}

static void
gst_cel_api_class_init (GstCelApiClass * klass)
{
}

#define SYM_SPEC(name) GST_DYN_SYM_SPEC (GstCelApi, name)

GstCelApi *
gst_cel_api_obtain (GError ** error)
{
  static const GstDynSymSpec symbols[] = {
    SYM_SPEC (FigCreateCaptureDevicesAndStreamsForPreset),

    SYM_SPEC (kFigCaptureDeviceProperty_MultiplexStreams),
    SYM_SPEC (kFigCapturePortType_Bottom),
    SYM_SPEC (kFigCapturePortType_Camera),
    SYM_SPEC (kFigCapturePortType_FrontFacingCamera),
    SYM_SPEC (kFigCapturePortType_Top),
    SYM_SPEC (kFigCapturePropertyValue_AFEarlyOutAllowPeakAtStart),
    SYM_SPEC (kFigCapturePropertyValue_AFEarlyOutDecrementAmount),
    SYM_SPEC (kFigCapturePropertyValue_AFEarlyOutEnable),
    SYM_SPEC (kFigCapturePropertyValue_AFEarlyOutThreshold),
    SYM_SPEC (kFigCapturePropertyValue_AFPositionCurrent),
    SYM_SPEC (kFigCapturePropertyValue_AFPositionInfinity),
    SYM_SPEC (kFigCapturePropertyValue_AFPositionMacro),
    SYM_SPEC (kFigCapturePropertyValue_AFSearchPositionArray),
    SYM_SPEC (kFigCapturePropertyValue_AGC),
    SYM_SPEC (kFigCapturePropertyValue_CLPFControl),
    SYM_SPEC (kFigCapturePropertyValue_ColorRangeFull),
    SYM_SPEC (kFigCapturePropertyValue_ColorRangeSDVideo),
    SYM_SPEC (kFigCapturePropertyValue_ModuleDate),
    SYM_SPEC (kFigCapturePropertyValue_ModuleIntegratorInfo),
    SYM_SPEC (kFigCapturePropertyValue_SensorID),
    SYM_SPEC (kFigCapturePropertyValue_SigmaFilterControl),
    SYM_SPEC (kFigCapturePropertyValue_YLPFControl),
    SYM_SPEC (kFigCapturePropertyValue_hStart),
    SYM_SPEC (kFigCapturePropertyValue_height),
    SYM_SPEC (kFigCapturePropertyValue_ispDGain),
    SYM_SPEC (kFigCapturePropertyValue_sensorDGain),
    SYM_SPEC (kFigCapturePropertyValue_shutterSpeedDenominator),
    SYM_SPEC (kFigCapturePropertyValue_shutterSpeedNumerator),
    SYM_SPEC (kFigCapturePropertyValue_vStart),
    SYM_SPEC (kFigCapturePropertyValue_weight),
    SYM_SPEC (kFigCapturePropertyValue_width),
    SYM_SPEC (kFigCaptureStreamPropertyValue_AEBracketedCaptureParams),
    SYM_SPEC (kFigCaptureStreamPropertyValue_BLCCompensation),
    SYM_SPEC (kFigCaptureStreamPropertyValue_BLCDebugMode),
    SYM_SPEC (kFigCaptureStreamPropertyValue_BandHighFactor),
    SYM_SPEC (kFigCaptureStreamPropertyValue_BandLowFactor),
    SYM_SPEC (kFigCaptureStreamPropertyValue_CCMWarmUpWeight),
    SYM_SPEC (kFigCaptureStreamPropertyValue_EdgeColorSuppressionSlope),
    SYM_SPEC (kFigCaptureStreamPropertyValue_EdgeColorSuppressionThreshold),
    SYM_SPEC (kFigCaptureStreamPropertyValue_EnableAESceneDynamicMetering),
    SYM_SPEC (kFigCaptureStreamPropertyValue_EnableCCMWarmUp),
    SYM_SPEC (kFigCaptureStreamPropertyValue_EnableHistogram1MetaData),
    SYM_SPEC (kFigCaptureStreamPropertyValue_EnableHistogram2MetaData),
    SYM_SPEC (kFigCaptureStreamPropertyValue_EnableHistogram3MetaData),
    SYM_SPEC (kFigCaptureStreamPropertyValue_EnableHistogram4MetaData),
    SYM_SPEC (kFigCaptureStreamPropertyValue_EnableHistogram),
    SYM_SPEC (kFigCaptureStreamPropertyValue_HistogramBinMode),
    SYM_SPEC (kFigCaptureStreamPropertyValue_HistogramDataType),
    SYM_SPEC (kFigCaptureStreamPropertyValue_ImageCropRect),
    SYM_SPEC (kFigCaptureStreamPropertyValue_LPExposure),
    SYM_SPEC (kFigCaptureStreamPropertyValue_LPGain),
    SYM_SPEC (kFigCaptureStreamPropertyValue_LowWeight),
    SYM_SPEC (kFigCaptureStreamPropertyValue_MaxWeight),
    SYM_SPEC (kFigCaptureStreamPropertyValue_MediumWeight),
    SYM_SPEC (kFigCaptureStreamPropertyValue_MinWeight),
    SYM_SPEC (kFigCaptureStreamPropertyValue_WeightDropOff),
    SYM_SPEC (kFigCaptureStreamPropertyValue_WeightReduction),
    SYM_SPEC (kFigCaptureStreamProperty_AEConvergenceSpeed),
    SYM_SPEC (kFigCaptureStreamProperty_AEOutlierClipCount),
    SYM_SPEC (kFigCaptureStreamProperty_AESceneDynamicMetering),
    SYM_SPEC (kFigCaptureStreamProperty_AEStability),
    SYM_SPEC (kFigCaptureStreamProperty_AEWindowManualWeightMatrix),
    SYM_SPEC (kFigCaptureStreamProperty_AEWindowParams),
    SYM_SPEC (kFigCaptureStreamProperty_AFEarlyOutParams),
    SYM_SPEC (kFigCaptureStreamProperty_AFParams),
    SYM_SPEC (kFigCaptureStreamProperty_AFSearchPositions),
    SYM_SPEC (kFigCaptureStreamProperty_AFWindowParams),
    SYM_SPEC (kFigCaptureStreamProperty_AGC),
    SYM_SPEC (kFigCaptureStreamProperty_AWBWindowParams),
    SYM_SPEC (kFigCaptureStreamProperty_AdditionalPTSOffset),
    SYM_SPEC (kFigCaptureStreamProperty_AlternateAWB),
    SYM_SPEC (kFigCaptureStreamProperty_Apply3AWindowSettings),
    SYM_SPEC (kFigCaptureStreamProperty_AttachRAW),
    SYM_SPEC (kFigCaptureStreamProperty_CCMWarmUp),
    SYM_SPEC (kFigCaptureStreamProperty_ClientMaxBufferCountHint),
    SYM_SPEC (kFigCaptureStreamProperty_ColorRange),
    SYM_SPEC (kFigCaptureStreamProperty_ColorSaturation),
    SYM_SPEC (kFigCaptureStreamProperty_ColorTables),
    SYM_SPEC (kFigCaptureStreamProperty_EdgeColorSuppressionParams),
    SYM_SPEC (kFigCaptureStreamProperty_ExposureBias),
    SYM_SPEC (kFigCaptureStreamProperty_FastSwitchMode),
    SYM_SPEC (kFigCaptureStreamProperty_FlashMode),
    SYM_SPEC (kFigCaptureStreamProperty_HistogramParams),
    SYM_SPEC (kFigCaptureStreamProperty_LockAENow),
    SYM_SPEC (kFigCaptureStreamProperty_LockAWBNow),
    SYM_SPEC (kFigCaptureStreamProperty_ManualAENow),
    SYM_SPEC (kFigCaptureStreamProperty_ManualFocusNow),
    SYM_SPEC (kFigCaptureStreamProperty_MaxIntegrationTime),
    SYM_SPEC (kFigCaptureStreamProperty_ModuleInfo),
    SYM_SPEC (kFigCaptureStreamProperty_NoiseReductionControls),
    SYM_SPEC (kFigCaptureStreamProperty_PortType),
    SYM_SPEC (kFigCaptureStreamProperty_PreFrameAE),
    SYM_SPEC (kFigCaptureStreamProperty_RawImageProcessNow),
    SYM_SPEC (kFigCaptureStreamProperty_RedEyeReductionParams),
    SYM_SPEC (kFigCaptureStreamProperty_ResetParams),
    SYM_SPEC (kFigCaptureStreamProperty_ScalerSharpening),
    SYM_SPEC (kFigCaptureStreamProperty_SetGainCap),
    SYM_SPEC (kFigCaptureStreamProperty_SharpeningControl),
    SYM_SPEC (kFigCaptureStreamProperty_TorchLevel),
    SYM_SPEC (kFigCaptureStreamProperty_UnlockAENow),
    SYM_SPEC (kFigCaptureStreamProperty_UnlockAWBNow),
    SYM_SPEC (kFigCaptureStreamProperty_UseFlashAFAssist),
    SYM_SPEC (kFigCaptureStreamProperty_UseFlashRedEyeReduction),
    SYM_SPEC (kFigCaptureStreamProperty_UseHardwareShutter),
    SYM_SPEC (kFigCaptureStreamProperty_VideoRecordingInProgress),
    SYM_SPEC (kFigRecorderCapturePreset_AudioRecording),
    SYM_SPEC (kFigRecorderCapturePreset_AudioVideoRecording),
    SYM_SPEC (kFigRecorderCapturePreset_PhotoCapture),
    SYM_SPEC (kFigRecorderCapturePreset_VideoRecording),

    {NULL, 0},
  };

  return _gst_dyn_api_new (gst_cel_api_get_type (), CELESTIAL_FRAMEWORK_PATH,
      symbols, error);
}
